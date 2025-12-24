#ifndef PAYMENT_SERVICE_HPP
#define PAYMENT_SERVICE_HPP

#include <memory>
#include <string>
#include "models.hpp"
#include "database.hpp"

class PaymentService {
public:
    explicit PaymentService(std::shared_ptr<Database> db);

    models::Account create_account(const std::string& user_id);
    models::Account get_account(const std::string& user_id);
    models::Account deposit(const std::string& user_id, double amount);
    bool process_payment(const std::string& user_id, const std::string& order_id, double amount);
    double get_balance(const std::string& user_id);

private:
    std::shared_ptr<Database> db_;
};

#endif
