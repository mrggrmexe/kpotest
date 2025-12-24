#include <iostream>
#include <thread>
#include <memory>
#include <cstdlib>
#include <string>
#include <initializer_list>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "database.hpp"
#include "payment_service.hpp"
#include "inbox_processor.hpp"
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
            env_any({"DB_NAME"}, "payments_db"),
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

        PaymentService payment_service(db);
        InboxProcessor inbox_processor(db, mq_config, payment_service);
        OutboxProcessor outbox_processor(db, mq_config);

        std::thread inbox_thread([&]() { inbox_processor.run(); });
        std::thread outbox_thread([&]() { outbox_processor.run(); });

        Server svr;

        svr.Post("/api/accounts", [&](const Request& req, Response& res) {
            try {
                auto body = json::parse(req.body);
                auto user_id = body.at("user_id").get<std::string>();

                auto account = payment_service.create_account(user_id);
                res.set_content(account.to_json().dump(), "application/json");
                res.status = 201;
            } catch (const std::exception& e) {
                json err = {{"error", e.what()}};
                res.set_content(err.dump(), "application/json");
                res.status = 400;
            }
        });

        svr.Post(R"(/api/accounts/([A-Za-z0-9\-]+)/deposit)", [&](const Request& req, Response& res) {
            try {
                auto user_id = req.matches[1].str();
                auto body = json::parse(req.body);
                auto amount = body.at("amount").get<double>();

                auto account = payment_service.deposit(user_id, amount);
                res.set_content(account.to_json().dump(), "application/json");
                res.status = 200;
            } catch (const std::exception& e) {
                json err = {{"error", e.what()}};
                res.set_content(err.dump(), "application/json");
                res.status = 400;
            }
        });

        svr.Get(R"(/api/accounts/([A-Za-z0-9\-]+)/balance)", [&](const Request& req, Response& res) {
            try {
                auto user_id = req.matches[1].str();
                auto balance = payment_service.get_balance(user_id);

                json response = {{"user_id", user_id}, {"balance", balance}};
                res.set_content(response.dump(), "application/json");
                res.status = 200;
            } catch (const std::exception& e) {
                json err = {{"error", e.what()}};
                res.set_content(err.dump(), "application/json");
                res.status = 404;
            }
        });

        svr.Get("/health", [](const Request&, Response& res) {
            res.set_content("OK", "text/plain");
            res.status = 200;
        });

        std::cout << "Payments Service starting on port 8080..." << std::endl;
        svr.listen("0.0.0.0", 8080);

        inbox_processor.stop();
        outbox_processor.stop();
        if (inbox_thread.joinable()) inbox_thread.join();
        if (outbox_thread.joinable()) outbox_thread.join();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
