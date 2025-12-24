#include "inbox_processor.hpp"
#include <uuid.h>
#include <thread>
#include <chrono>

InboxProcessor::InboxProcessor(std::shared_ptr<Database> db,
                               const MessageQueueConfig& mq_config,
                               PaymentService& payment_service)
    : db_(db), mq_config_(mq_config), 
      payment_service_(payment_service), running_(true) {
    message_queue_ = std::make_unique<MessageQueue>(mq_config);
}

void InboxProcessor::run() {
    message_queue_->consume("payment.requests", 
        [this](const std::string& message) {
            this->handle_payment_request(message);
        });
        
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void InboxProcessor::stop() {
    running_ = false;
}

void InboxProcessor::handle_payment_request(const std::string& message) {
    try {
        auto json_msg = nlohmann::json::parse(message);
        auto payment_request = models::messages::PaymentRequest::from_json(json_msg);
        
        auto event_id = payment_request.order_id;
        
        auto existing = db_->query(
            "SELECT id FROM inbox_events WHERE id = $1",
            event_id);
            
        if (!existing.empty()) {
            return;
        }
        
        auto tx = db_->begin_transaction();
        
        db_->execute(tx,
            "INSERT INTO inbox_events (id, type, payload, status, processed_at) "
            "VALUES ($1, 'PAYMENT_REQUEST', $2, 'PENDING', to_timestamp($3))",
            event_id, message,
            std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
            
        bool success = payment_service_.process_payment(
            payment_request.user_id, 
            payment_request.order_id, 
            payment_request.amount);
            
        std::string status = success ? "PROCESSED" : "FAILED";
        
        db_->execute(tx,
            "UPDATE inbox_events SET status = $1 WHERE id = $2",
            status, event_id);
            
        models::messages::PaymentResult result;
        result.order_id = payment_request.order_id;
        result.user_id = payment_request.user_id;
        result.success = success;
        result.message = success ? "Payment successful" : "Payment failed";
        
        uuids::uuid_random_generator uuid_gen;
        auto outbox_id = uuids::to_string(uuid_gen());
        
        db_->execute(tx,
            "INSERT INTO outbox_events (id, type, payload, status, created_at) "
            "VALUES ($1, 'PAYMENT_RESULT', $2, 'PENDING', to_timestamp($3))",
            outbox_id, result.to_json().dump(),
            std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
            
        tx.commit();
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to handle payment request: " << e.what() << std::endl;
    }
}