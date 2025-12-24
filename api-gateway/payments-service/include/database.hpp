#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <memory>
#include <string>
#include <utility>
#include <pqxx/pqxx>

class Database {
public:
    Database(const std::string& host,
             const std::string& port,
             const std::string& dbname,
             const std::string& user,
             const std::string& password);

    pqxx::transaction_base& begin_transaction();

    void commit() {
        if (transaction_) {
            transaction_->commit();
            transaction_.reset();
        }
    }

    void rollback() {
        if (transaction_) {
            transaction_->abort();
            transaction_.reset();
        }
    }

    pqxx::result query(const std::string& sql);
    pqxx::result query(pqxx::transaction_base& tx, const std::string& sql);

    void execute(const std::string& sql);
    void execute(pqxx::transaction_base& tx, const std::string& sql);

    template<typename... Args>
    pqxx::result query(const std::string& sql, Args&&... args) {
        pqxx::nontransaction nt(*conn_);
        return nt.exec_params(sql, std::forward<Args>(args)...);
    }

    template<typename... Args>
    pqxx::result query(pqxx::transaction_base& tx, const std::string& sql, Args&&... args) {
        return tx.exec_params(sql, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void execute(const std::string& sql, Args&&... args) {
        pqxx::work w(*conn_);
        w.exec_params(sql, std::forward<Args>(args)...);
        w.commit();
    }

    template<typename... Args>
    void execute(pqxx::transaction_base& tx, const std::string& sql, Args&&... args) {
        tx.exec_params(sql, std::forward<Args>(args)...);
    }

    void initialize_schema();

private:
    std::unique_ptr<pqxx::connection> conn_;
    std::unique_ptr<pqxx::work> transaction_;
};

#endif
