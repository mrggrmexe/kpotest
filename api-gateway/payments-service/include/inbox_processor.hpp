#ifndef INBOX_PROCESSOR_HPP
#define INBOX_PROCESSOR_HPP

#include <memory>
#include <string>
#include <atomic>
#include "database.hpp"
#include "message_queue.hpp"
#include "payment_service.hpp"

class InboxProcessor {
public:
    InboxProcessor(std::shared_ptr<Database> db,
                   const MessageQueueConfig& mq_config,
                   PaymentService& payment_service);
    void run();
    void stop();

private:
    void handle_payment_request(const std::string& message);

    std::shared_ptr<Database> db_;
    MessageQueueConfig mq_config_;
    PaymentService& payment_service_;
    std::unique_ptr<MessageQueue> message_queue_;
    std::atomic_bool running_{true};
};

#endif
