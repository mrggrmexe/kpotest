#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <string>
#include <initializer_list>

#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <nlohmann/json.hpp>

#include "message_queue.hpp"
#include "notification_manager.hpp"
#include "websocket_server.hpp"

namespace asio = boost::asio;
using json = nlohmann::json;

static std::string env_any(std::initializer_list<const char*> keys, const char* def_val) {
    for (auto* k : keys) {
        if (const char* v = std::getenv(k)) {
            if (*v) return std::string(v);
        }
    }
    return std::string(def_val);
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

        MessageQueueConfig mq_config{
            env_any({"MQ_HOST", "RABBITMQ_HOST"}, "localhost"),
            env_any({"MQ_PORT", "RABBITMQ_PORT"}, "5672"),
            env_any({"MQ_USER", "RABBITMQ_USER"}, "admin"),
            env_any({"MQ_PASS", "RABBITMQ_PASS"}, "password")
        };

        NotificationManager notification_manager;
        MessageQueue message_queue(mq_config);

        std::thread consumer([&]() {
            try {
                message_queue.consume(
                    "payment.results",
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
                            // ignore bad messages
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
        server->run(
            env_any({"WS_HOST"}, "0.0.0.0"),
            static_cast<unsigned short>(std::stoi(env_any({"WS_PORT"}, "8080")))
        );

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
