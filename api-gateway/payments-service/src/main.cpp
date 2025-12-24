#include "httplib.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

#include "../include/database.hpp"
#include "../include/inbox_processor.hpp"
#include "../include/outbox_processor.hpp"
#include "../include/payment_service.hpp"

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

  std::string db_host = env_str("DB_HOST", dbj.value("host", std::string("postgres-payments")));
  std::string db_port = env_str("DB_PORT", json_port_to_string(dbj, "port", "5432"));
  std::string db_name = env_str("DB_NAME", dbj.value("dbname", std::string("payments_db")));
  std::string db_user = env_str("DB_USER", dbj.value("user", std::string("microservice")));
  std::string db_pass = env_str("DB_PASSWORD", dbj.value("password", std::string("password")));

  std::string mq_host = env_str("MQ_HOST", mqj.value("host", std::string("rabbitmq")));
  std::string mq_port = env_str("MQ_PORT", json_port_to_string(mqj, "port", "5672"));
  std::string mq_user = env_str("MQ_USER", mqj.value("user", std::string("admin")));
  std::string mq_pass = env_str("MQ_PASSWORD", mqj.value("password", std::string("admin")));

  auto db = std::make_shared<Database>(db_host, db_port, db_name, db_user, db_pass);
  db->initialize_schema();

  MessageQueueConfig mq_cfg{mq_host, mq_port, mq_user, mq_pass};

  PaymentService payments(db);

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

  std::thread inbox_thread([&]() {
    for (;;) {
      try {
        InboxProcessor inbox(db, mq_cfg, payments);
        inbox.run();
      } catch (...) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
  });
  inbox_thread.detach();

  httplib::Server svr;

  svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("ok", "text/plain");
  });

  svr.Post("/api/accounts", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      auto body = json::parse(req.body);
      std::string user_id = body.value("user_id", "");
      if (user_id.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"user_id required"})", "application/json");
        return;
      }

      bool ok = payments.create_account(user_id);
      res.status = ok ? 201 : 200;
      res.set_content(R"({"status":"ok"})", "application/json");
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid json"})", "application/json");
    }
  });

  svr.Post(R"(/api/accounts/([^/]+)/deposit)", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      std::string user_id = req.matches[1];

      auto body = json::parse(req.body);
      double amount = body.value("amount", 0.0);
      if (user_id.empty() || amount <= 0.0) {
        res.status = 400;
        res.set_content(R"({"error":"invalid payload"})", "application/json");
        return;
      }

      bool ok = payments.deposit(user_id, amount);
      if (!ok) {
        res.status = 400;
        res.set_content(R"({"error":"deposit failed"})", "application/json");
        return;
      }

      res.set_content(R"({"status":"ok"})", "application/json");
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid json"})", "application/json");
    }
  });

  svr.listen(host.c_str(), port);
  return 0;
}
