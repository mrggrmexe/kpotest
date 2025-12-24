#ifndef PAYMENTS_OUTBOX_PROCESSOR_HPP
#define PAYMENTS_OUTBOX_PROCESSOR_HPP

#include <memory>
#include <string>
#include <atomic>
#include "database.hpp"
#include "message_queue.hpp"

class OutboxProcessor {
public:
    OutboxProcessor(std::shared_ptr<Database> db, const MessageQueueConfig& mq_config);
    void run();
    void stop();

private:
    void process_pending_events();

    std::shared_ptr<Database> db_;
    MessageQueueConfig mq_config_;
    std::unique_ptr<MessageQueue> message_queue_;
    std::atomic_bool running_{true};
};

#endif
