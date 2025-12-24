#ifndef ORDER_SERVICE_HPP
#define ORDER_SERVICE_HPP

#include <memory>
#include <vector>
#include <string>
#include "database.hpp"
#include "message_queue.hpp"
#include "models.hpp"

class OrderService {
public:
    OrderService(std::shared_ptr<Database> db, const MessageQueueConfig& mq_config);

    models::Order create_order(const std::string& user_id, double amount, const std::string& description);
    std::vector<models::Order> get_user_orders(const std::string& user_id);
    models::Order get_order(const std::string& order_id);
    void update_order_status(const std::string& order_id, const std::string& status);

private:
    std::shared_ptr<Database> db_;
    MessageQueueConfig mq_config_;
    std::unique_ptr<MessageQueue> message_queue_;
};

#endif
