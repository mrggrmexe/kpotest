#include "message_queue.hpp"
#include <amqp_tcp_socket.h>
#include <stdexcept>
#include <string>
#include <sys/time.h>

static void ensure_ok(const amqp_rpc_reply_t& reply, const char* what) {
    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
        throw std::runtime_error(std::string("RabbitMQ error: ") + what);
    }
}

MessageQueue::MessageQueue(const MessageQueueConfig& config) {
    connection_ = amqp_new_connection();
    if (!connection_) {
        throw std::runtime_error("Cannot create RabbitMQ connection");
    }

    amqp_socket_t* socket = amqp_tcp_socket_new(connection_);
    if (!socket) {
        amqp_destroy_connection(connection_);
        connection_ = nullptr;
        throw std::runtime_error("Cannot create TCP socket");
    }

    int status = amqp_socket_open(socket, config.host.c_str(), std::stoi(config.port));
    if (status) {
        amqp_destroy_connection(connection_);
        connection_ = nullptr;
        throw std::runtime_error("Cannot open socket");
    }

    auto reply = amqp_login(connection_, "/", 0, 131072, 0,
                            AMQP_SASL_METHOD_PLAIN,
                            config.user.c_str(),
                            config.password.c_str());
    ensure_ok(reply, "login");

    amqp_channel_open(connection_, channel_);
    reply = amqp_get_rpc_reply(connection_);
    ensure_ok(reply, "channel_open");
}

MessageQueue::~MessageQueue() {
    if (connection_) {
        amqp_channel_close(connection_, channel_, AMQP_REPLY_SUCCESS);
        amqp_connection_close(connection_, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(connection_);
        connection_ = nullptr;
    }
}

void MessageQueue::publish(const std::string& queue, const std::string& message) {
    amqp_bytes_t queue_bytes = amqp_cstring_bytes(queue.c_str());

    amqp_queue_declare(connection_, channel_, queue_bytes, 0, 1, 0, 0, amqp_empty_table);
    auto reply = amqp_get_rpc_reply(connection_);
    ensure_ok(reply, "queue_declare");

    amqp_bytes_t body;
    body.len = message.size();
    body.bytes = const_cast<char*>(message.data());

    amqp_basic_properties_t props;
    props._flags = AMQP_BASIC_DELIVERY_MODE_FLAG | AMQP_BASIC_CONTENT_TYPE_FLAG;
    props.delivery_mode = 2;
    props.content_type = amqp_cstring_bytes("application/json");

    int result = amqp_basic_publish(connection_, channel_, amqp_cstring_bytes(""),
                                    queue_bytes, 0, 0, &props, body);
    if (result < 0) {
        throw std::runtime_error("Failed to publish message");
    }
}

void MessageQueue::consume(const std::string& queue,
                           std::function<void(const std::string&)> callback,
                           std::atomic_bool& running) {
    amqp_bytes_t queue_bytes = amqp_cstring_bytes(queue.c_str());

    amqp_queue_declare(connection_, channel_, queue_bytes, 0, 1, 0, 0, amqp_empty_table);
    auto reply = amqp_get_rpc_reply(connection_);
    ensure_ok(reply, "queue_declare");

    amqp_basic_consume(connection_, channel_, queue_bytes, amqp_empty_bytes, 0, 1, 0, amqp_empty_table);
    reply = amqp_get_rpc_reply(connection_);
    ensure_ok(reply, "basic_consume");

    while (running.load()) {
        amqp_envelope_t envelope;
        amqp_maybe_release_buffers(connection_);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        amqp_rpc_reply_t ret = amqp_consume_message(connection_, &envelope, &timeout, 0);
        if (ret.reply_type == AMQP_RESPONSE_NORMAL) {
            std::string msg(static_cast<char*>(envelope.message.body.bytes), envelope.message.body.len);
            callback(msg);
            amqp_destroy_envelope(&envelope);
            continue;
        }

        if (ret.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION && ret.library_error == AMQP_STATUS_TIMEOUT) {
            continue;
        }

        throw std::runtime_error("RabbitMQ consume error");
    }
}
