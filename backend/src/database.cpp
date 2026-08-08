// ============================================================================
//  database.cpp
//  Implements the Database RAII wrapper declared in database.hpp.
// ============================================================================

#include "database.hpp"
#include "logger.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace evoting {

Database::Database() {
    conn_ = mysql_init(nullptr);
}

Database::~Database() {
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool Database::connect(const std::string& configPath) {
    std::ifstream f(configPath);
    if (!f.is_open()) {
        Logger::error("Could not open config file: " + configPath);
        return false;
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    sjson::Json cfg = sjson::Json::parse(buffer.str());
    sjson::Json db = cfg["database"];

    std::string host = db.getString("host", "127.0.0.1");
    int port = db.getInt("port", 3306);
    std::string user = db.getString("user", "root");
    std::string password = db.getString("password", "");
    std::string dbname = db.getString("name", "evoting_db");

    bool reconnect = true;
mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnect);

    MYSQL* result = mysql_real_connect(
        conn_,
        host.c_str(),
        user.c_str(),
        password.c_str(),
        dbname.c_str(),
        static_cast<unsigned int>(port),
        nullptr,
        0
    );

    if (!result) {
        Logger::error(std::string("MySQL connection failed: ") + mysql_error(conn_));
        Logger::error("Check backend/config.json and make sure MySQL 8.0 is running, "
                       "and that database/schema.sql has been imported.");
        connected_ = false;
        return false;
    }

    mysql_set_character_set(conn_, "utf8mb4");
    connected_ = true;
    Logger::info("Connected to MySQL database '" + dbname + "' at " + host + ":" + std::to_string(port));
    return true;
}

std::string Database::escapeString(const std::string& input) {
    std::vector<char> buf(input.size() * 2 + 1);
    unsigned long len = mysql_real_escape_string(conn_, buf.data(), input.c_str(), (unsigned long)input.size());
    return std::string(buf.data(), len);
}

std::string Database::bindParams(const std::string& sql, const std::vector<std::string>& params) {
    std::string out;
    out.reserve(sql.size() + 32);
    size_t paramIdx = 0;

    for (size_t i = 0; i < sql.size(); ++i) {
        if (sql[i] == '?' && paramIdx < params.size()) {
            out += "'";
            out += escapeString(params[paramIdx]);
            out += "'";
            ++paramIdx;
        } else {
            out += sql[i];
        }
    }
    return out;
}

bool Database::execute(const std::string& sql, const std::vector<std::string>& params) {
    if (!connected_) {
        Logger::error("execute() called with no active DB connection");
        return false;
    }
    std::string finalSql = bindParams(sql, params);
    if (mysql_query(conn_, finalSql.c_str()) != 0) {
        Logger::error(std::string("MySQL execute error: ") + mysql_error(conn_) + " | SQL: " + finalSql);
        return false;
    }
    return true;
}

std::vector<Row> Database::query(const std::string& sql, const std::vector<std::string>& params) {
    std::vector<Row> rows;
    if (!connected_) {
        Logger::error("query() called with no active DB connection");
        return rows;
    }

    std::string finalSql = bindParams(sql, params);
    if (mysql_query(conn_, finalSql.c_str()) != 0) {
        Logger::error(std::string("MySQL query error: ") + mysql_error(conn_) + " | SQL: " + finalSql);
        return rows;
    }

    MYSQL_RES* result = mysql_store_result(conn_);
    if (!result) {
        // Not necessarily an error — e.g. a SELECT with an empty result
        // set still returns a valid (empty) MYSQL_RES normally; null here
        // usually means the query wasn't a SELECT at all.
        return rows;
    }

    unsigned int numFields = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        unsigned long* lengths = mysql_fetch_lengths(result);
        Row r;
        for (unsigned int i = 0; i < numFields; ++i) {
            if (row[i] == nullptr) {
                r.set(fields[i].name, "", true);
            } else {
                r.set(fields[i].name, std::string(row[i], lengths[i]), false);
            }
        }
        rows.push_back(r);
    }

    mysql_free_result(result);
    return rows;
}

unsigned long long Database::lastInsertId() {
    return connected_ ? mysql_insert_id(conn_) : 0;
}

unsigned long long Database::affectedRows() {
    return connected_ ? mysql_affected_rows(conn_) : 0;
}

} // namespace evoting
