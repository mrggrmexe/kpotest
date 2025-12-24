#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "../include/notification_manager.hpp"
#include "../include/websocket_server.hpp"

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
  json mqj = cfg ? ((*cfg).contains("rabbitmq") ? (*cfg)["rabbitmq"] : (*cfg).value("message_queue", json::object()))
                 : json::object();

  std::string host = env_str("HOST", serverj.value("host", std::string("0.0.0.0")));
  int port = env_int("PORT", serverj.value("port", 8080));

  std::string mq_host = env_str("MQ_HOST", mqj.value("host", std::string("rabbitmq")));
  std::string mq_port = env_str("MQ_PORT", json_port_to_string(mqj, "port", "5672"));
  std::string mq_user = env_str("MQ_USER", mqj.value("user", std::string("admin")));
  std::string mq_pass = env_str("MQ_PASSWORD", mqj.value("password", std::string("admin")));

  MessageQueueConfig mq_cfg{mq_host, mq_port, mq_user, mq_pass};

  auto manager = std::make_shared<NotificationManager>(mq_cfg);
  manager->start();

  WebSocketServer server(host, static_cast<unsigned short>(port), manager);
  server.run();

  return 0;
}
