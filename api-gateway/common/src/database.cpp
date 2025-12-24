#include "database.hpp"
#include <stdexcept>

Database::Database(const std::string& host,
                   const std::string& port,
                   const std::string& dbname,
                   const std::string& user,
                   const std::string& password) {
    std::string conn_str = "host=" + host +
                          " port=" + port +
                          " dbname=" + dbname +
                          " user=" + user +
                          " password=" + password;
    try {
        conn_ = std::make_unique<pqxx::connection>(conn_str);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to connect to database: " + std::string(e.what()));
    }
}

pqxx::transaction_base& Database::begin_transaction() {
    transaction_ = std::make_unique<pqxx::work>(*conn_);
    return *transaction_;
}

pqxx::result Database::query(const std::string& sql) {
    pqxx::nontransaction nt(*conn_);
    return nt.exec(sql);
}

pqxx::result Database::query(pqxx::transaction_base& tx, const std::string& sql) {
    return tx.exec(sql);
}

void Database::execute(const std::string& sql) {
    pqxx::work w(*conn_);
    w.exec(sql);
    w.commit();
}

void Database::execute(pqxx::transaction_base& tx, const std::string& sql) {
    tx.exec(sql);
}

void Database::initialize_schema() {
}
