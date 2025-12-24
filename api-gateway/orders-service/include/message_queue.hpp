#ifndef MESSAGE_QUEUE_HPP
#define MESSAGE_QUEUE_HPP

#include <string>
#include <functional>
#include <amqp.h>

struct MessageQueueConfig {
    std::string host;
    std::string port;
    std::string user;
    std::string password;
};

class MessageQueue {
public:
    explicit MessageQueue(const MessageQueueConfig& config);
    ~MessageQueue();

    void publish(const std::string& queue, const std::string& message);
    void consume(const std::string& queue, std::function<void(const std::string&)> callback);

private:
    amqp_connection_state_t connection_{};
    amqp_channel_t channel_{1};
};

#endif
