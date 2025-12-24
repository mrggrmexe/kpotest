#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <nlohmann/json.hpp>
#include "message_queue.hpp"
#include "notification_manager.hpp"
#include "websocket_server.hpp"

namespace asio = boost::asio;
using json = nlohmann::json;

static const char* env_or(const char* key, const char* def_val) {
    const char* v = std::getenv(key);
    return v ? v : def_val;
}

int main() {
    try {
        asio::io_context ioc;

        asio::signal_set signals(ioc, SIGINT, SIGTERM);
        std::atomic_bool running{true};

        signals.async_wait([&](const boost::system::error_code&, int) {
            running.store(false);
            ioc.stop();
        });

        auto mq_config = MessageQueueConfig{
            env_or("RABBITMQ_HOST", "localhost"),
            env_or("RABBITMQ_PORT", "5672"),
            env_or("RABBITMQ_USER", "admin"),
            env_or("RABBITMQ_PASS", "password")
        };

        NotificationManager notification_manager;
        MessageQueue message_queue(mq_config);

        std::thread consumer([&]() {
            try {
                message_queue.consume("payment.results",
                    [&](const std::string& message) {
                        try {
                            auto j = json::parse(message);
                            auto order_id = j.at("order_id").get<std::string>();

                            json notification = {
                                {"type", "order_update"},
                                {"order_id", order_id},
                                {"status", j.value("success", false) ? "FINISHED" : "CANCELLED"},
                                {"message", j.value("message", std::string{})},
                                {"timestamp", std::time(nullptr)}
                            };

                            notification_manager.notify(order_id, notification);
                        } catch (...) {
                        }
                    },
                    running
                );
            } catch (const std::exception& e) {
                std::cerr << "Consumer error: " << e.what() << std::endl;
                running.store(false);
                ioc.stop();
            }
        });

        auto server = std::make_shared<WebSocketServer>(ioc, notification_manager);
        server->run(env_or("WS_HOST", "0.0.0.0"),
                    static_cast<unsigned short>(std::stoi(env_or("WS_PORT", "8080"))));

        std::cout << "WebSocket Service starting on port 8080..." << std::endl;
        ioc.run();

        running.store(false);
        if (consumer.joinable()) consumer.join();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
