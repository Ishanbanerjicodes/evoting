// ============================================================================
//  logger.cpp
// ============================================================================

#include "logger.hpp"
#include <iostream>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <direct.h>

namespace evoting {

static std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmBuf;
    localtime_s(&tmBuf, &t);
    std::ostringstream out;
    out << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

void Logger::write(const std::string& level, const std::string& message) {
    std::string line = "[" + timestamp() + "] [" + level + "] " + message;

    // Console output — colored on Windows terminals that support ANSI
    // (Windows 10+ default consoles do), gracefully plain otherwise.
    std::cout << line << std::endl;

    // Persist to logs/server.log as well.
    _mkdir("logs");
    std::ofstream file("logs/server.log", std::ios::app);
    if (file.is_open()) {
        file << line << "\n";
    }
}

void Logger::info(const std::string& message)  { write("INFO",  message); }
void Logger::warn(const std::string& message)  { write("WARN",  message); }
void Logger::error(const std::string& message) { write("ERROR", message); }

} // namespace evoting
