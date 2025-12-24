#include "order_service.hpp"
#include "utils.hpp"
#include <chrono>
#include <ctime>

OrderService::OrderService(std::shared_ptr<Database> db,
                           const MessageQueueConfig& mq_config)
    : db_(std::move(db)), mq_config_(mq_config) {
}

models::Order OrderService::create_order(const std::string& user_id,
                                        double amount,
                                        const std::string& description) {
    auto& tx = db_->begin_transaction();

    auto order_id = utils::generate_uuid();

    models::Order order;
    order.id = order_id;
    order.user_id = user_id;
    order.amount = amount;
    order.description = description;
    order.status = "NEW";
    order.created_at = std::chrono::system_clock::now();

    db_->execute(tx,
        "INSERT INTO orders (id, user_id, amount, description, status, created_at) "
        "VALUES ($1, $2, $3, $4, $5, to_timestamp($6))",
        order.id, order.user_id, order.amount,
        order.description, order.status,
        static_cast<long long>(std::chrono::system_clock::to_time_t(order.created_at)));

    models::messages::PaymentRequest payment_request;
    payment_request.order_id = order.id;
    payment_request.user_id = user_id;
    payment_request.amount = amount;

    auto outbox_id = utils::generate_uuid();

    db_->execute(tx,
        "INSERT INTO outbox_events (id, type, payload, status, created_at) "
        "VALUES ($1, 'PAYMENT_REQUEST', $2::jsonb, 'PENDING', to_timestamp($3))",
        outbox_id, payment_request.to_json().dump(),
        static_cast<long long>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));

    tx.commit();

    return order;
}

std::vector<models::Order> OrderService::get_user_orders(const std::string& user_id) {
    auto result = db_->query(
        "SELECT id, user_id, amount, description, status, "
        "extract(epoch from created_at) as created_at "
        "FROM orders WHERE user_id = $1 ORDER BY created_at DESC",
        user_id);

    std::vector<models::Order> orders;
    for (const auto& row : result) {
        models::Order order;
        order.id = row["id"].as<std::string>();
        order.user_id = row["user_id"].as<std::string>();
        order.amount = row["amount"].as<double>();
        order.description = row["description"].is_null() ? std::string{} : row["description"].as<std::string>();
        order.status = row["status"].as<std::string>();

        auto created_sec = static_cast<std::time_t>(row["created_at"].as<double>());
        order.created_at = std::chrono::system_clock::from_time_t(created_sec);

        orders.push_back(std::move(order));
    }

    return orders;
}

models::Order OrderService::get_order(const std::string& order_id) {
    auto result = db_->query(
        "SELECT id, user_id, amount, description, status, "
        "extract(epoch from created_at) as created_at "
        "FROM orders WHERE id = $1",
        order_id);

    if (result.empty()) {
        return models::Order{};
    }

    const auto& row = result[0];
    models::Order order;
    order.id = row["id"].as<std::string>();
    order.user_id = row["user_id"].as<std::string>();
    order.amount = row["amount"].as<double>();
    order.description = row["description"].is_null() ? std::string{} : row["description"].as<std::string>();
    order.status = row["status"].as<std::string>();

    auto created_sec = static_cast<std::time_t>(row["created_at"].as<double>());
    order.created_at = std::chrono::system_clock::from_time_t(created_sec);

    return order;
}

void OrderService::update_order_status(const std::string& order_id,
                                      const std::string& status) {
    db_->execute(
        "UPDATE orders SET status = $1 WHERE id = $2",
        status, order_id);
}
