#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "order_service.hpp"
#include "outbox_processor.hpp"

using json = nlohmann::json;

static std::string env_str(const char* key, const std::string& def) {
  if (const char* v = std::getenv(key); v && *v) return std::string(v);
  return def;
}

static int env_int(const char* key, int def) {
  try {
    return std::stoi(env_str(key, std::to_string(def)));
  } catch (...) {
    return def;
  }
}

static std::optional<json> load_json_any(const std::initializer_list<std::string>& paths) {
  for (const auto& p : paths) {
    std::ifstream in(p);
    if (!in.good()) continue;
    try {
      json j; in >> j;
      return j;
    } catch (...) {
      // если файл есть, но битый — лучше падать явно
      throw std::runtime_error("Failed to parse JSON: " + p);
    }
  }
  return std::nullopt;
}

int main(int argc, char** argv) {
  try {
    // --- config file (опционально) ---
    std::string cfg_path = (argc > 1) ? argv[1] : env_str("CONFIG_PATH", "");
    std::optional<json> cfg;

    if (!cfg_path.empty()) {
      cfg = load_json_any({cfg_path});
    } else {
      // типичные варианты расположения внутри контейнера/проекта
      cfg = load_json_any({"config.json", "include/config.json"});
    }

    // --- service settings ---
    const std::string bind_host = env_str("ORDERS_BIND", "0.0.0.0");
    const int port = env_int("ORDERS_PORT", 8080);

    // --- DB defaults (Docker-friendly) ---
    // В контейнере localhost = сам контейнер, поэтому дефолт должен быть postgres-orders
    const std::string db_host = env_str("DB_HOST",  cfg ? (*cfg)["database"].value("host", "postgres-orders") : "postgres-orders");
    const std::string db_port = env_str("DB_PORT",  cfg ? std::to_string((*cfg)["database"].value("port", 5432)) : "5432");
    const std::string db_name = env_str("DB_NAME",  cfg ? (*cfg)["database"].value("name", "orders_db") : "orders_db");
    const std::string db_user = env_str("DB_USER",  cfg ? (*cfg)["database"].value("user", "postgres") : "postgres");
    const std::string db_pass = env_str("DB_PASSWORD", cfg ? (*cfg)["database"].value("password", "password") : "password");

    // Database ctor: (host, port, dbname, user, password)
    std::shared_ptr<Database> db;

    // ретраи — чтобы сервис не умирал, если Postgres поднимается чуть позже
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

    // --- MQ defaults ---
    // чтобы не было "Cannot open socket" — дефолт rabbitmq, а не localhost
    MessageQueueConfig mq_cfg{
      env_str("MQ_HOST", cfg ? (*cfg)["message_queue"].value("host", "rabbitmq") : "rabbitmq"),
      env_int("MQ_PORT", cfg ? (*cfg)["message_queue"].value("port", 5672) : 5672),
      env_str("MQ_USER", cfg ? (*cfg)["message_queue"].value("user", "admin") : "admin"),
      env_str("MQ_PASSWORD", cfg ? (*cfg)["message_queue"].value("password", "admin") : "admin"),
    };

    OrderService order_service(db, mq_cfg);

    OutboxProcessor outbox(db, mq_cfg);
    std::thread outbox_thread([&] { outbox.run(); });
    outbox_thread.detach();

    httplib::Server svr;

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
      res.set_content("ok\n", "text/plain");
    });

    // POST /api/orders  (именно так дергает CI)
    svr.Post("/api/orders", [&](const httplib::Request& req, httplib::Response& res) {
      try {
        auto body = json::parse(req.body);
        const std::string user_id = body.value("user_id", "");
        const double amount = body.value("amount", 0.0);
        const std::string description = body.value("description", "");

        if (user_id.empty() || amount <= 0.0) {
          res.status = 400;
          res.set_content(R"({"error":"invalid payload"})", "application/json");
          return;
        }

        auto order = order_service.create_order(user_id, amount, description);
        res.status = 201;
        res.set_content(order.to_json().dump(), "application/json");
      } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(json{{"error", e.what()}}.dump(), "application/json");
      }
    });

    // GET /api/orders/<id>
    svr.Get(R"(/api/orders/([A-Za-z0-9\-_]+))", [&](const httplib::Request& req, httplib::Response& res) {
      try {
        const std::string order_id = req.matches[1];
        auto order = order_service.get_order(order_id);
        res.set_content(order.to_json().dump(), "application/json");
      } catch (const std::exception& e) {
        res.status = 404;
        res.set_content(json{{"error", e.what()}}.dump(), "application/json");
      }
    });

    std::cout << "orders-service listening on " << bind_host << ":" << port << std::endl;
    svr.listen(bind_host.c_str(), port);
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}
