#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "payment_service.hpp"
#include "inbox_processor.hpp"
#include "outbox_processor.hpp"

using json = nlohmann::json;

static std::string env_str(const char* key, const std::string& def) {
  if (const char* v = std::getenv(key); v && *v) return std::string(v);
  return def;
}

static int env_int(const char* key, int def) {
  try { return std::stoi(env_str(key, std::to_string(def))); }
  catch (...) { return def; }
}

static std::optional<json> load_json_any(const std::initializer_list<std::string>& paths) {
  for (const auto& p : paths) {
    std::ifstream in(p);
    if (!in.good()) continue;
    json j; in >> j;
    return j;
  }
  return std::nullopt;
}

int main(int argc, char** argv) {
  try {
    std::string cfg_path = (argc > 1) ? argv[1] : env_str("CONFIG_PATH", "");
    std::optional<json> cfg;

    if (!cfg_path.empty()) cfg = load_json_any({cfg_path});
    else cfg = load_json_any({"config.json", "include/config.json"});

    const std::string bind_host = env_str("PAYMENTS_BIND", "0.0.0.0");
    const int port = env_int("PAYMENTS_PORT", 8081);

    // DB defaults must be service-name, not localhost
    const std::string db_host = env_str("DB_HOST", cfg ? (*cfg)["database"].value("host", "postgres-payments") : "postgres-payments");
    const std::string db_port = env_str("DB_PORT", cfg ? std::to_string((*cfg)["database"].value("port", 5432)) : "5432");
    const std::string db_name = env_str("DB_NAME", cfg ? (*cfg)["database"].value("name", "payments_db") : "payments_db");
    const std::string db_user = env_str("DB_USER", cfg ? (*cfg)["database"].value("user", "postgres") : "postgres");
    const std::string db_pass = env_str("DB_PASSWORD", cfg ? (*cfg)["database"].value("password", "password") : "password");

    std::shared_ptr<Database> db;

    for (int i = 1; i <= 60; i++) {
      try {
        db = std::make_shared<Database>(db_host, db_port, db_name, db_user, db_pass);
        db->initialize_schema();
        break;
      } catch (const std::exception& e) {
        if (i == 60) throw;
        std::cerr << "DB not ready (" << i << "/60): " << e.what() << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }

    MessageQueueConfig mq_cfg{
      env_str("MQ_HOST", cfg ? (*cfg)["message_queue"].value("host", "rabbitmq") : "rabbitmq"),
      env_int("MQ_PORT", cfg ? (*cfg)["message_queue"].value("port", 5672) : 5672),
      env_str("MQ_USER", cfg ? (*cfg)["message_queue"].value("user", "admin") : "admin"),
      env_str("MQ_PASSWORD", cfg ? (*cfg)["message_queue"].value("password", "admin") : "admin"),
    };

    PaymentService payments(db, mq_cfg);

    // фоновые обработчики (если используются в твоём проекте)
    InboxProcessor inbox(db, mq_cfg);
    OutboxProcessor outbox(db, mq_cfg);
    std::thread([&] { inbox.run(); }).detach();
    std::thread([&] { outbox.run(); }).detach();

    httplib::Server svr;

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
      res.set_content("ok\n", "text/plain");
    });

    // POST /api/accounts  (smoke_tests)
    svr.Post("/api/accounts", [&](const httplib::Request& req, httplib::Response& res) {
      try {
        auto body = json::parse(req.body);
        const std::string user_id = body.value("user_id", "");
        if (user_id.empty()) {
          res.status = 400;
          res.set_content(R"({"error":"user_id required"})", "application/json");
          return;
        }
        auto acc = payments.create_account(user_id);
        res.status = 201;
        res.set_content(acc.to_json().dump(), "application/json");
      } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(json{{"error", e.what()}}.dump(), "application/json");
      }
    });

    // POST /api/accounts/<user_id>/deposit  (smoke_tests)
    svr.Post(R"(/api/accounts/([^/]+)/deposit)", [&](const httplib::Request& req, httplib::Response& res) {
      try {
        const std::string user_id = req.matches[1];
        auto body = json::parse(req.body);
        const double amount = body.value("amount", 0.0);
        if (amount <= 0.0) {
          res.status = 400;
          res.set_content(R"({"error":"amount must be > 0"})", "application/json");
          return;
        }
        auto acc = payments.deposit(user_id, amount);
        res.set_content(acc.to_json().dump(), "application/json");
      } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(json{{"error", e.what()}}.dump(), "application/json");
      }
    });

    // POST /api/accounts/<user_id>/withdraw
    svr.Post(R"(/api/accounts/([^/]+)/withdraw)", [&](const httplib::Request& req, httplib::Response& res) {
      try {
        const std::string user_id = req.matches[1];
        auto body = json::parse(req.body);
        const double amount = body.value("amount", 0.0);
        if (amount <= 0.0) {
          res.status = 400;
          res.set_content(R"({"error":"amount must be > 0"})", "application/json");
          return;
        }
        auto acc = payments.withdraw(user_id, amount);
        res.set_content(acc.to_json().dump(), "application/json");
      } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(json{{"error", e.what()}}.dump(), "application/json");
      }
    });

    // GET /api/accounts/<user_id>
    svr.Get(R"(/api/accounts/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
      try {
        const std::string user_id = req.matches[1];
        const double balance = payments.get_balance(user_id);
        res.set_content(json{{"user_id", user_id}, {"balance", balance}}.dump(), "application/json");
      } catch (const std::exception& e) {
        res.status = 404;
        res.set_content(json{{"error", e.what()}}.dump(), "application/json");
      }
    });

    std::cout << "payments-service listening on " << bind_host << ":" << port << std::endl;
    svr.listen(bind_host.c_str(), port);
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}
