#include "payment_service.hpp"
#include <stdexcept>

PaymentService::PaymentService(std::shared_ptr<Database> db) : db_(std::move(db)) {}

models::Account PaymentService::create_account(const std::string& user_id) {
    auto existing = db_->query(
        "SELECT user_id FROM accounts WHERE user_id = $1",
        user_id
    );

    if (!existing.empty()) {
        throw std::runtime_error("Account already exists");
    }

    db_->execute(
        "INSERT INTO accounts (user_id, balance, version) VALUES ($1, 0, 0)",
        user_id
    );

    return get_account(user_id);
}

models::Account PaymentService::get_account(const std::string& user_id) {
    auto result = db_->query(
        "SELECT user_id, balance, version FROM accounts WHERE user_id = $1",
        user_id
    );

    if (result.empty()) {
        throw std::runtime_error("Account not found");
    }

    const auto& row = result[0];
    models::Account account;
    account.user_id = row["user_id"].as<std::string>();
    account.balance = row["balance"].as<double>();
    account.version = row["version"].as<int>();
    return account;
}

models::Account PaymentService::deposit(const std::string& user_id, double amount) {
    if (amount <= 0) {
        throw std::runtime_error("Amount must be positive");
    }

    auto& tx = db_->begin_transaction();

    auto result = db_->query(tx,
        "UPDATE accounts SET balance = balance + $1, version = version + 1 "
        "WHERE user_id = $2 "
        "RETURNING user_id, balance, version",
        amount, user_id
    );

    if (result.empty()) {
        tx.abort();
        db_->rollback();
        throw std::runtime_error("Account not found");
    }

    tx.commit();
    db_->commit();

    const auto& row = result[0];
    models::Account account;
    account.user_id = row["user_id"].as<std::string>();
    account.balance = row["balance"].as<double>();
    account.version = row["version"].as<int>();
    return account;
}

bool PaymentService::process_payment(const std::string& user_id,
                                     const std::string&,
                                     double amount) {
    if (amount <= 0) return false;

    try {
        auto account = get_account(user_id);

        auto& tx = db_->begin_transaction();

        auto result = db_->query(tx,
            "UPDATE accounts SET balance = balance - $1, version = version + 1 "
            "WHERE user_id = $2 AND balance >= $3 AND version = $4 "
            "RETURNING user_id, balance, version",
            amount, user_id, amount, account.version
        );

        if (result.empty()) {
            tx.abort();
            db_->rollback();
            return false;
        }

        tx.commit();
        db_->commit();
        return true;
    } catch (...) {
        return false;
    }
}

double PaymentService::get_balance(const std::string& user_id) {
    return get_account(user_id).balance;
}
