#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "database.hpp"
#include "message_queue.hpp"
#include "order_service.hpp"
#include "outbox_processor.hpp"

static std::atomic_bool g_stop{false};

static std::string env_str(const char* key, const std::string& def) {
  const char* v = std::getenv(key);
  return (v && *v) ? std::string(v) : def;
}

static int env_int(const char* key, int def) {
  const char* v = std::getenv(key);
  if (!v || !*v) return def;
  try { return std::stoi(v); } catch (...) { return def; }
}

static nlohmann::json read_json_file(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) return nlohmann::json::object();
  nlohmann::json j;
  try { f >> j; } catch (...) { return nlohmann::json::object(); }
  return j;
}

static std::string pg_conninfo(const std::string& host, int port,
                               const std::string& db, const std::string& user,
                               const std::string& pass) {
  std::string s = "host=" + host + " port=" + std::to_string(port) + " dbname=" + db + " user=" + user;
  if (!pass.empty()) s += " password=" + pass;
  return s;
}

static void handle_signal(int) { g_stop.store(true); }

int main(int argc, char** argv) {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  const std::string config_path = (argc > 1) ? argv[1] : env_str("CONFIG_PATH", "config.json");
  const auto cfg = read_json_file(config_path);

  const std::string host = env_str("HOST", cfg.value("host", std::string("0.0.0.0")));
  const int port = env_int("PORT", cfg.value("port", 8080));

  const auto dbj = cfg.value("db", nlohmann::json::object());
  const std::string db_host = env_str("DB_HOST", dbj.value("host", std::string("postgres-orders")));
  const int db_port = env_int("DB_PORT", dbj.value("port", 5432));
  const std::string db_name = env_str("DB_NAME", dbj.value("name", std::string("orders")));
  const std::string db_user = env_str("DB_USER", dbj.value("user", std::string("postgres")));
  const std::string db_pass = env_str("DB_PASS", dbj.value("password", std::string("postgres")));

  const auto mqj = cfg.value("rabbitmq", nlohmann::json::object());
  MessageQueueConfig mq{};
  mq.host = env_str("MQ_HOST", mqj.value("host", std::string("rabbitmq")));
  mq.port = env_int("MQ_PORT", mqj.value("port", 5672));
  mq.username = env_str("MQ_USER", mqj.value("username", std::string("admin")));
  mq.password = env_str("MQ_PASS", mqj.value("password", std::string("admin")));
  mq.vhost = env_str("MQ_VHOST", mqj.value("vhost", std::string("/")));

  try {
    auto db = std::make_shared<Database>(pg_conninfo(db_host, db_port, db_name, db_user, db_pass));
    db->init_schema();

    OrderService order_service(db, mq);
    OutboxProcessor outbox(db, mq);

    std::thread outbox_thread([&] { outbox.run(); });

    httplib::Server server;

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
      res.set_content("ok\n", "text/plain");
      res.status = 200;
    });

    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
      res.set_content("orders-service\n", "text/plain");
      res.status = 200;
    });

    std::thread http_thread([&] {
      server.listen(host.c_str(), port);
    });

    while (!g_stop.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    server.stop();
    outbox.stop();

    if (http_thread.joinable()) http_thread.join();
    if (outbox_thread.joinable()) outbox_thread.join();
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}
