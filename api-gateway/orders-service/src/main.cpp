#include <iostream>
#include <thread>
#include <memory>
#include <cstdlib>
#include <string>
#include <initializer_list>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "database.hpp"
#include "order_service.hpp"
#include "outbox_processor.hpp"

using json = nlohmann::json;
using namespace httplib;

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
        auto db = std::make_shared<Database>(
            env_any({"DB_HOST", "POSTGRES_HOST"}, "localhost"),
            env_any({"DB_PORT", "POSTGRES_PORT"}, "5432"),
            env_any({"DB_NAME"}, "orders_db"),
            env_any({"DB_USER", "POSTGRES_USER"}, "microservice"),
            env_any({"DB_PASSWORD", "POSTGRES_PASSWORD"}, "password")
        );

        db->initialize_schema();

        MessageQueueConfig mq_config{
            env_any({"MQ_HOST", "RABBITMQ_HOST"}, "localhost"),
            env_any({"MQ_PORT", "RABBITMQ_PORT"}, "5672"),
            env_any({"MQ_USER", "RABBITMQ_USER"}, "admin"),
            env_any({"MQ_PASS", "RABBITMQ_PASS"}, "password")
        };

        OrderService order_service(db, mq_config);
        OutboxProcessor outbox_processor(db, mq_config);

        std::thread outbox_thread([&]() { outbox_processor.run(); });

        Server svr;

        svr.Post("/api/orders", [&](const Request& req, Response& res) {
            try {
                auto body = json::parse(req.body);
                auto user_id = body.at("user_id").get<std::string>();
                auto amount = body.at("amount").get<double>();
                auto description = body.value("description", std::string{});

                auto order = order_service.create_order(user_id, amount, description);
                res.set_content(order.to_json().dump(), "application/json");
                res.status = 201;
            } catch (const std::exception& e) {
                json err = {{"error", e.what()}};
                res.set_content(err.dump(), "application/json");
                res.status = 400;
            }
        });

        svr.Get("/api/orders", [&](const Request& req, Response& res) {
            try {
                auto user_id = req.get_param_value("user_id");
                if (user_id.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"user_id is required"})", "application/json");
                    return;
                }

                auto orders = order_service.get_user_orders(user_id);
                json arr = json::array();
                for (const auto& o : orders) arr.push_back(o.to_json());

                res.set_content(arr.dump(), "application/json");
                res.status = 200;
            } catch (const std::exception& e) {
                json err = {{"error", e.what()}};
                res.set_content(err.dump(), "application/json");
                res.status = 500;
            }
        });

        svr.Get(R"(/api/orders/([A-Za-z0-9\-]+))", [&](const Request& req, Response& res) {
            try {
                auto order_id = req.matches[1].str();
                auto order = order_service.get_order(order_id);

                if (order.id.empty()) {
                    res.status = 404;
                    res.set_content(R"({"error":"Order not found"})", "application/json");
                    return;
                }

                res.set_content(order.to_json().dump(), "application/json");
                res.status = 200;
            } catch (const std::exception& e) {
                json err = {{"error", e.what()}};
                res.set_content(err.dump(), "application/json");
                res.status = 500;
            }
        });

        svr.Get("/health", [](const Request&, Response& res) {
            res.set_content("OK", "text/plain");
            res.status = 200;
        });

        std::cout << "Orders Service starting on port 8080..." << std::endl;
        svr.listen("0.0.0.0", 8080);

        outbox_processor.stop();
        if (outbox_thread.joinable()) outbox_thread.join();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
