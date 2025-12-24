#ifndef NOTIFICATION_MANAGER_HPP
#define NOTIFICATION_MANAGER_HPP

#include <string>
#include <unordered_map>
#include <set>
#include <mutex>
#include <memory>
#include <nlohmann/json.hpp>

class WebSocketSession;

class NotificationManager {
public:
    void subscribe(const std::string& order_id, const std::shared_ptr<WebSocketSession>& session);
    void unsubscribe(const std::string& order_id, const std::shared_ptr<WebSocketSession>& session);
    void notify(const std::string& order_id, const nlohmann::json& message);

private:
    using WeakSession = std::weak_ptr<WebSocketSession>;
    using WeakSet = std::set<WeakSession, std::owner_less<WeakSession>>;

    std::unordered_map<std::string, WeakSet> subscriptions_;
    std::mutex mutex_;
};

#endif
