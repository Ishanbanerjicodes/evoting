// ============================================================================
//  logger.hpp
//  A tiny leveled console logger with timestamps. Also appends to
//  backend/logs/server.log so there's a persistent audit trail for the
//  demo/report, independent of the database's audit_logs table.
// ============================================================================

#pragma once
#include <string>

namespace evoting {

class Logger {
public:
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);

private:
    static void write(const std::string& level, const std::string& message);
};

} // namespace evoting
