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
        throw std::runtime_error("Failed to connect to database: " + 
                                 std::string(e.what()));
    }
}

pqxx::transaction_base& Database::begin_transaction() {
    transaction_ = std::make_unique<pqxx::work>(*conn_);
    return *transaction_;
}

pqxx::result Database::query(const std::string& sql) {
    try {
        pqxx::nontransaction nt(*conn_);
        return nt.exec(sql);
    } catch (const std::exception& e) {
        throw std::runtime_error("Query failed: " + std::string(e.what()));
    }
}

pqxx::result Database::query(pqxx::transaction_base& tx, 
                            const std::string& sql) {
    try {
        return tx.exec(sql);
    } catch (const std::exception& e) {
        throw std::runtime_error("Query failed: " + std::string(e.what()));
    }
}

void Database::execute(const std::string& sql) {
    try {
        pqxx::work w(*conn_);
        w.exec(sql);
        w.commit();
    } catch (const std::exception& e) {
        throw std::runtime_error("Execute failed: " + std::string(e.what()));
    }
}

void Database::execute(pqxx::transaction_base& tx, 
                      const std::string& sql) {
    try {
        tx.exec(sql);
    } catch (const std::exception& e) {
        throw std::runtime_error("Execute failed: " + std::string(e.what()));
    }
}

template<typename... Args>
pqxx::result Database::query(const std::string& sql, Args&&... args) {
    try {
        pqxx::nontransaction nt(*conn_);
        return nt.exec_params(sql, std::forward<Args>(args)...);
    } catch (const std::exception& e) {
        throw std::runtime_error("Query failed: " + std::string(e.what()));
    }
}

template<typename... Args>
pqxx::result Database::query(pqxx::transaction_base& tx, 
                            const std::string& sql, 
                            Args&&... args) {
    try {
        return tx.exec_params(sql, std::forward<Args>(args)...);
    } catch (const std::exception& e) {
        throw std::runtime_error("Query failed: " + std::string(e.what()));
    }
}

template<typename... Args>
void Database::execute(const std::string& sql, Args&&... args) {
    try {
        pqxx::work w(*conn_);
        w.exec_params(sql, std::forward<Args>(args)...);
        w.commit();
    } catch (const std::exception& e) {
        throw std::runtime_error("Execute failed: " + std::string(e.what()));
    }
}

template<typename... Args>
void Database::execute(pqxx::transaction_base& tx, 
                      const std::string& sql, 
                      Args&&... args) {
    try {
        tx.exec_params(sql, std::forward<Args>(args)...);
    } catch (const std::exception& e) {
        throw std::runtime_error("Execute failed: " + std::string(e.what()));
    }
}

void Database::initialize_schema() {
    execute(
        "CREATE TABLE IF NOT EXISTS orders ("
        "   id VARCHAR(255) PRIMARY KEY,"
        "   user_id VARCHAR(255) NOT NULL,"
        "   amount DECIMAL(10,2) NOT NULL,"
        "   description TEXT,"
        "   status VARCHAR(50) NOT NULL DEFAULT 'NEW',"
        "   created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
    
    execute(
        "CREATE TABLE IF NOT EXISTS outbox_events ("
        "   id VARCHAR(255) PRIMARY KEY,"
        "   type VARCHAR(100) NOT NULL,"
        "   payload JSONB NOT NULL,"
        "   status VARCHAR(50) NOT NULL DEFAULT 'PENDING',"
        "   created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
    
    execute(
        "CREATE INDEX IF NOT EXISTS idx_outbox_status ON outbox_events(status)"
    );
    
    execute(
        "CREATE INDEX IF NOT EXISTS idx_orders_user_id ON orders(user_id)"
    );
}