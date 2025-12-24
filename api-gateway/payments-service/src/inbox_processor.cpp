#include "inbox_processor.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include "utils.hpp"
#include "models.hpp"

using json = nlohmann::json;

InboxProcessor::InboxProcessor(std::shared_ptr<Database> db,
                               const MessageQueueConfig& mq_config,
                               PaymentService& payment_service)
    : db_(std::move(db)), mq_config_(mq_config),
      payment_service_(payment_service) {
    message_queue_ = std::make_unique<MessageQueue>(mq_config_);
}

void InboxProcessor::run() {
    message_queue_->consume(
        "payment.requests",
        [this](const std::string& message) {
            this->handle_payment_request(message);
        },
        running_
    );
}

void InboxProcessor::stop() {
    running_.store(false);
}

void InboxProcessor::handle_payment_request(const std::string& message) {
    try {
        auto json_msg = json::parse(message);
        auto payment_request = models::messages::PaymentRequest::from_json(json_msg);

        auto event_id = payment_request.order_id;

        auto existing = db_->query(
            "SELECT id FROM inbox_events WHERE id = $1",
            event_id
        );

        if (!existing.empty()) return;

        auto& tx = db_->begin_transaction();

        db_->execute(tx,
            "INSERT INTO inbox_events (id, type, payload, status, processed_at) "
            "VALUES ($1, 'PAYMENT_REQUEST', $2::jsonb, 'PENDING', to_timestamp($3))",
            event_id, message,
            static_cast<long long>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()))
        );

        bool success = payment_service_.process_payment(
            payment_request.user_id,
            payment_request.order_id,
            payment_request.amount
        );

        std::string status = success ? "PROCESSED" : "FAILED";

        db_->execute(tx,
            "UPDATE inbox_events SET status = $1 WHERE id = $2",
            status, event_id
        );

        models::messages::PaymentResult result;
        result.order_id = payment_request.order_id;
        result.user_id = payment_request.user_id;
        result.success = success;
        result.message = success ? "Payment successful" : "Payment failed";

        auto outbox_id = utils::generate_uuid();

        db_->execute(tx,
            "INSERT INTO outbox_events (id, type, payload, status, created_at) "
            "VALUES ($1, 'PAYMENT_RESULT', $2::jsonb, 'PENDING', to_timestamp($3))",
            outbox_id, result.to_json().dump(),
            static_cast<long long>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()))
        );

        tx.commit();
        db_->commit();
    } catch (const std::exception& e) {
        std::cerr << "Failed to handle payment request: " << e.what() << std::endl;
    }
}
