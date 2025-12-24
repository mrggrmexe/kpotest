#include "httplib.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

#include "../include/order_service.hpp"
#include "../include/outbox_processor.hpp"

using json = nlohmann::json;

static std::string env_str(const char* key, const std::string& def) {
  if (const char* v = std::getenv(key)) {
    if (*v) return std::string(v);
  }
  return def;
}

static int env_int(const char* key, int def) {
  if (const char* v = std::getenv(key)) {
    try { return std::stoi(v); } catch (...) {}
  }
  return def;
}

static std::optional<json> load_config(const std::string& path) {
  std::ifstream f(path);
  if (!f) return std::nullopt;
  json j;
  try {
    f >> j;
    return j;
  } catch (...) {
    return std::nullopt;
  }
}

static std::string json_port_to_string(const json& j, const std::string& key, const std::string& def) {
  if (!j.contains(key)) return def;
  const auto& v = j.at(key);
  if (v.is_string()) return v.get<std::string>();
  if (v.is_number_integer()) return std::to_string(v.get<int>());
  if (v.is_number_float()) return std::to_string(static_cast<int>(v.get<double>()));
  return def;
}

int main(int argc, char** argv) {
  std::string config_path = env_str("CONFIG_PATH", "include/config.json");
  auto cfg = load_config(config_path);

  json serverj = cfg ? (*cfg).value("server", json::object()) : json::object();
  json dbj = cfg ? (*cfg).value("database", json::object()) : json::object();
  json mqj = cfg ? ((*cfg).contains("rabbitmq") ? (*cfg)["rabbitmq"] : (*cfg).value("message_queue", json::object()))
                 : json::object();

  std::string host = env_str("HOST", serverj.value("host", std::string("0.0.0.0")));
  int port = env_int("PORT", serverj.value("port", 8080));

  std::string db_host = env_str("DB_HOST", dbj.value("host", std::string("postgres-orders")));
  std::string db_port = env_str("DB_PORT", json_port_to_string(dbj, "port", "5432"));
  std::string db_name = env_str("DB_NAME", dbj.value("dbname", std::string("orders_db")));
  std::string db_user = env_str("DB_USER", dbj.value("user", std::string("microservice")));
  std::string db_pass = env_str("DB_PASSWORD", dbj.value("password", std::string("password")));

  std::string mq_host = env_str("MQ_HOST", mqj.value("host", std::string("rabbitmq")));
  std::string mq_port = env_str("MQ_PORT", json_port_to_string(mqj, "port", "5672"));
  std::string mq_user = env_str("MQ_USER", mqj.value("user", std::string("admin")));
  std::string mq_pass = env_str("MQ_PASSWORD", mqj.value("password", std::string("admin")));

  auto db = std::make_shared<Database>(db_host, db_port, db_name, db_user, db_pass);
  db->initialize_schema();

  MessageQueueConfig mq_cfg{mq_host, mq_port, mq_user, mq_pass};

  OrderService orders(db, mq_cfg);

  std::thread outbox_thread([&]() {
    for (;;) {
      try {
        OutboxProcessor outbox(db, mq_cfg);
        outbox.run();
      } catch (...) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
  });
  outbox_thread.detach();

  httplib::Server svr;

  svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("ok", "text/plain");
  });

  svr.Post("/api/orders", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      auto body = json::parse(req.body);
      std::string user_id = body.value("user_id", "");
      std::string product_id = body.value("product_id", "");
      int quantity = body.value("quantity", 0);
      double price = body.value("price", 0.0);

      if (user_id.empty() || product_id.empty() || quantity <= 0 || price <= 0.0) {
        res.status = 400;
        res.set_content(R"({"error":"invalid payload"})", "application/json");
        return;
      }

      int order_id = orders.create_order(user_id, product_id, quantity, price);
      json out = {{"order_id", order_id}};
      res.status = 201;
      res.set_content(out.dump(), "application/json");
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid json"})", "application/json");
    }
  });

  svr.Get("/api/orders", [&](const httplib::Request&, httplib::Response& res) {
    try {
      auto list = orders.list_orders();
      res.set_content(list.dump(), "application/json");
    } catch (...) {
      res.status = 500;
      res.set_content(R"({"error":"internal"})", "application/json");
    }
  });

  svr.listen(host.c_str(), port);
  return 0;
}
