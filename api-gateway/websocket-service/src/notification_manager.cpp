#include "notification_manager.hpp"
#include "websocket_server.hpp"

void NotificationManager::subscribe(const std::string& order_id, const std::shared_ptr<WebSocketSession>& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_[order_id].insert(session);
}

void NotificationManager::unsubscribe(const std::string& order_id, const std::shared_ptr<WebSocketSession>& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(order_id);
    if (it == subscriptions_.end()) return;

    it->second.erase(session);
    if (it->second.empty()) {
        subscriptions_.erase(it);
    }
}

void NotificationManager::notify(const std::string& order_id, const nlohmann::json& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(order_id);
    if (it == subscriptions_.end()) return;

    auto payload = message.dump();

    for (auto iter = it->second.begin(); iter != it->second.end();) {
        if (auto s = iter->lock()) {
            s->send(payload);
            ++iter;
        } else {
            iter = it->second.erase(iter);
        }
    }

    if (it->second.empty()) {
        subscriptions_.erase(it);
    }
}
