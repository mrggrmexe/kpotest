#include "websocket_server.hpp"
#include "notification_manager.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

WebSocketSession::WebSocketSession(tcp::socket socket, asio::io_context& ioc, NotificationManager& notification_manager)
    : ws_(std::move(socket)),
      strand_(asio::make_strand(ioc)),
      notification_manager_(notification_manager) {
}

void WebSocketSession::start() {
    ws_.async_accept(
        asio::bind_executor(
            strand_,
            [self = shared_from_this()](beast::error_code ec) {
                self->on_accept(ec);
            }
        )
    );
}

void WebSocketSession::send(std::string message) {
    asio::post(
        strand_,
        [self = shared_from_this(), msg = std::move(message)]() mutable {
            self->enqueue_write(std::move(msg));
        }
    );
}

void WebSocketSession::on_accept(beast::error_code ec) {
    if (ec) return;
    do_read();
}

void WebSocketSession::do_read() {
    ws_.async_read(
        buffer_,
        asio::bind_executor(
            strand_,
            [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
                self->on_read(ec, bytes_transferred);
            }
        )
    );
}

void WebSocketSession::on_read(beast::error_code ec, std::size_t) {
    if (ec == websocket::error::closed) {
        if (!order_id_.empty()) {
            notification_manager_.unsubscribe(order_id_, shared_from_this());
        }
        return;
    }

    if (ec) return;

    try {
        auto text = beast::buffers_to_string(buffer_.data());
        auto j = json::parse(text);

        if (j.value("type", std::string{}) == "subscribe" && j.contains("order_id")) {
            if (!order_id_.empty()) {
                notification_manager_.unsubscribe(order_id_, shared_from_this());
            }

            order_id_ = j.at("order_id").get<std::string>();
            notification_manager_.subscribe(order_id_, shared_from_this());

            json resp = {{"type", "subscribed"}, {"order_id", order_id_}};
            send(resp.dump());
        }
    } catch (...) {
    }

    buffer_.consume(buffer_.size());
    do_read();
}

void WebSocketSession::enqueue_write(std::string message) {
    write_queue_.push_back(std::move(message));
    if (!writing_) {
        do_write();
    }
}

void WebSocketSession::do_write() {
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    writing_ = true;

    ws_.async_write(
        asio::buffer(write_queue_.front()),
        asio::bind_executor(
            strand_,
            [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
                self->on_write(ec, bytes_transferred);
            }
        )
    );
}

void WebSocketSession::on_write(beast::error_code ec, std::size_t) {
    if (ec) {
        if (!order_id_.empty()) {
            notification_manager_.unsubscribe(order_id_, shared_from_this());
        }
        return;
    }

    write_queue_.pop_front();
    do_write();
}

WebSocketServer::WebSocketServer(asio::io_context& ioc, NotificationManager& notification_manager)
    : ioc_(ioc), acceptor_(ioc), notification_manager_(notification_manager) {
}

void WebSocketServer::run(const std::string& address, unsigned short port) {
    tcp::endpoint endpoint(asio::ip::make_address(address), port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
    do_accept();
}

void WebSocketServer::do_accept() {
    acceptor_.async_accept(
        [this](beast::error_code ec, tcp::socket socket) {
            on_accept(ec, std::move(socket));
        }
    );
}

void WebSocketServer::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) return;
    std::make_shared<WebSocketSession>(std::move(socket), ioc_, notification_manager_)->start();
    do_accept();
}
