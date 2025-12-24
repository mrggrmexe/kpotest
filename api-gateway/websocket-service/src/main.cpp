#include <cstdlib>
#include <iostream>
#include <string>

#include "websocket_server.hpp"

static int env_int(const char* key, int def) {
  const char* v = std::getenv(key);
  if (!v || !*v) return def;
  try { return std::stoi(v); } catch (...) { return def; }
}

static std::string env_str(const char* key, const std::string& def) {
  const char* v = std::getenv(key);
  return (v && *v) ? std::string(v) : def;
}

int main() {
  try {
    const std::string host = env_str("WS_BIND", "0.0.0.0");
    const int port = env_int("WS_PORT", 9002);

    std::cout << "websocket-service listening on " << host << ":" << port << std::endl;
    WebsocketServer server(host, port);
    server.run();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}
