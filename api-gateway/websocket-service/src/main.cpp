#include <nlohmann/json.hpp>

#include <boost/asio.hpp>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "message_queue.hpp"
#include "notification_manager.hpp"
#include "websocket_server.hpp"

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

static void handle_signal(int) { g_stop.store(true); }

int main(int argc, char** argv) {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  const std::string config_path = (argc > 1) ? argv[1] : env_str("CONFIG_PATH", "config.json");
  const auto cfg = read_json_file(config_path);

  const std::string host = env_str("HOST", cfg.value("host", std::string("0.0.0.0")));
  const int port = env_int("PORT", cfg.value("port", 8080));

  const auto mqj = cfg.value("rabbitmq", nlohmann::json::object());
  MessageQueueConfig mq{};
  mq.host = env_str("MQ_HOST", mqj.value("host", std::string("rabbitmq")));
  mq.port = env_int("MQ_PORT", mqj.value("port", 5672));
  mq.username = env_str("MQ_USER", mqj.value("username", std::string("admin")));
  mq.password = env_str("MQ_PASS", mqj.value("password", std::string("admin")));
  mq.vhost = env_str("MQ_VHOST", mqj.value("vhost", std::string("/")));

  try {
    boost::asio::io_context ioc(1);

    NotificationManager notifications;

    auto addr = boost::asio::ip::make_address(host);
    boost::asio::ip::tcp::endpoint ep{addr, static_cast<unsigned short>(port)};

    WebSocketServer server(ioc, ep, notifications);
    server.run();

    std::thread mq_thread([&] {
      try {
        MessageQueue mq_client(mq);
        mq_client.consume("payment.notifications", [&](const std::string& msg) {
          notifications.broadcast_message(msg);
        });
      } catch (const std::exception& e) {
        std::cerr << "MQ thread fatal: " << e.what() << std::endl;
      }
    });
    mq_thread.detach();

    ioc.run();
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}
