#ifndef MODELS_HPP
#define MODELS_HPP

#include <string>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace models {

struct Order {
    std::string id;
    std::string user_id;
    double amount{};
    std::string description;
    std::string status;
    std::chrono::system_clock::time_point created_at{std::chrono::system_clock::now()};

    static Order from_json(const json& j) {
        Order o;
        o.id = j.at("id").get<std::string>();
        o.user_id = j.at("user_id").get<std::string>();
        o.amount = j.at("amount").get<double>();
        o.description = j.value("description", std::string{});
        o.status = j.at("status").get<std::string>();
        o.created_at = std::chrono::system_clock::now();
        return o;
    }

    json to_json() const {
        return {
            {"id", id},
            {"user_id", user_id},
            {"amount", amount},
            {"description", description},
            {"status", status},
            {"created_at", std::chrono::system_clock::to_time_t(created_at)}
        };
    }
};

struct Account {
    std::string user_id;
    double balance{};
    int version{};

    json to_json() const {
        return {
            {"user_id", user_id},
            {"balance", balance},
            {"version", version}
        };
    }
};

namespace messages {

struct PaymentRequest {
    std::string order_id;
    std::string user_id;
    double amount{};

    static PaymentRequest from_json(const json& j) {
        PaymentRequest r;
        r.order_id = j.at("order_id").get<std::string>();
        r.user_id = j.at("user_id").get<std::string>();
        r.amount = j.at("amount").get<double>();
        return r;
    }

    json to_json() const {
        return {
            {"order_id", order_id},
            {"user_id", user_id},
            {"amount", amount}
        };
    }
};

struct PaymentResult {
    std::string order_id;
    std::string user_id;
    bool success{};
    std::string message;

    static PaymentResult from_json(const json& j) {
        PaymentResult r;
        r.order_id = j.at("order_id").get<std::string>();
        r.user_id = j.at("user_id").get<std::string>();
        r.success = j.at("success").get<bool>();
        r.message = j.value("message", std::string{});
        return r;
    }

    json to_json() const {
        return {
            {"order_id", order_id},
            {"user_id", user_id},
            {"success", success},
            {"message", message}
        };
    }
};

}

}

#endif
