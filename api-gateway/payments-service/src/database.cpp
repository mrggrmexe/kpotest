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
    execute(
        "CREATE TABLE IF NOT EXISTS accounts ("
        "   user_id VARCHAR(255) PRIMARY KEY,"
        "   balance DECIMAL(10,2) NOT NULL DEFAULT 0.0,"
        "   version INTEGER NOT NULL DEFAULT 0,"
        "   created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "   updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ")"
    );

    execute(
        "CREATE TABLE IF NOT EXISTS inbox_events ("
        "   id VARCHAR(255) PRIMARY KEY,"
        "   type VARCHAR(100) NOT NULL,"
        "   payload JSONB NOT NULL,"
        "   status VARCHAR(50) NOT NULL DEFAULT 'PENDING',"
        "   processed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "   retry_count INTEGER NOT NULL DEFAULT 0"
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

    execute("CREATE INDEX IF NOT EXISTS idx_inbox_status ON inbox_events(status)");
    execute("CREATE INDEX IF NOT EXISTS idx_outbox_status ON outbox_events(status)");
    execute("CREATE INDEX IF NOT EXISTS idx_inbox_id ON inbox_events(id)");

    execute(
        "CREATE OR REPLACE FUNCTION update_updated_at_column() "
        "RETURNS TRIGGER AS $$ "
        "BEGIN "
        "   NEW.updated_at = CURRENT_TIMESTAMP; "
        "   RETURN NEW; "
        "END; "
        "$$ language 'plpgsql'"
    );

    execute("DROP TRIGGER IF EXISTS update_accounts_updated_at ON accounts");

    execute(
        "CREATE TRIGGER update_accounts_updated_at "
        "BEFORE UPDATE ON accounts "
        "FOR EACH ROW "
        "EXECUTE FUNCTION update_updated_at_column()"
    );
}
