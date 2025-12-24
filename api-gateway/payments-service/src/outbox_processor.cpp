#include "outbox_processor.hpp"
#include <thread>
#include <chrono>
#include <iostream>

OutboxProcessor::OutboxProcessor(std::shared_ptr<Database> db,
                                 const MessageQueueConfig& mq_config)
    : db_(std::move(db)), mq_config_(mq_config) {
    message_queue_ = std::make_unique<MessageQueue>(mq_config_);
}

void OutboxProcessor::run() {
    while (running_.load()) {
        try {
            process_pending_events();
        } catch (const std::exception& e) {
            std::cerr << "Outbox processor error: " << e.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void OutboxProcessor::stop() {
    running_.store(false);
}

void OutboxProcessor::process_pending_events() {
    auto& tx = db_->begin_transaction();

    auto events = db_->query(tx,
        "SELECT id, type, payload FROM outbox_events "
        "WHERE status = 'PENDING' "
        "ORDER BY created_at ASC "
        "FOR UPDATE SKIP LOCKED LIMIT 10"
    );

    for (const auto& row : events) {
        auto event_id = row["id"].as<std::string>();
        auto type = row["type"].as<std::string>();
        auto payload = row["payload"].as<std::string>();

        try {
            if (type == "PAYMENT_RESULT") {
                message_queue_->publish("payment.results", payload);
            }

            db_->execute(tx,
                "UPDATE outbox_events SET status = 'PROCESSED' WHERE id = $1",
                event_id
            );
        } catch (const std::exception& e) {
            std::cerr << "Failed to process outbox event " << event_id << ": " << e.what() << std::endl;
        }
    }

    tx.commit();
    db_->commit();
}
