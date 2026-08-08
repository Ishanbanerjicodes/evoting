// ============================================================================
//  main.cpp
//  Blockchain-Based E-Voting System — Backend Entry Point
//
//  Wires together the database connection, auth service, and every
//  controller's routes, then starts the HTTP server. Designed to be
//  built and run with nothing more than:
//      mkdir build && cd build && cmake .. && cmake --build .
//      .\evoting_server.exe
// ============================================================================

#include "../lib/simple_http.hpp"
#include "database.hpp"
#include "auth_service.hpp"
#include "email_service.hpp"
#include "auth_controller.hpp"
#include "election_controller.hpp"
#include "candidate_controller.hpp"
#include "vote_controller.hpp"
#include "admin_controller.hpp"
#include "logger.hpp"
#include "response_helper.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <filesystem>

using namespace evoting;

// ============================================================================
//  loadDotEnv()
//  Loads backend/.env (simple KEY=VALUE lines, '#' comments, blank lines
//  ignored) into the process environment, so the SMTP credentials and any
//  other secrets never have to be typed into PowerShell/CMD by hand every
//  time the server starts. This is the ONE-TIME-SETUP, EVERY-TIME-AUTOMATIC
//  solution: copy backend/.env.example to backend/.env, fill in real
//  values once, and every future run (including start_project.bat) just
//  picks it up automatically.
//
//  Values already present in the real OS environment are left alone (an
//  explicit `setx`/session env var always wins over .env), matching the
//  usual behaviour of dotenv-style loaders.
//
//  This never logs secret values, only which keys were loaded.
// ============================================================================
static void loadDotEnv(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        return; // .env is optional — no error if it doesn't exist yet.
    }

    std::string line;
    int loadedCount = 0;
    while (std::getline(f, line)) {
        // Strip trailing \r (Windows/Unix line-ending safety) and whitespace.
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                                  line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim whitespace around key.
        size_t keyEnd = key.find_last_not_of(" \t");
        if (keyEnd != std::string::npos) key = key.substr(0, keyEnd + 1);

        // Trim whitespace around value and strip surrounding quotes if present.
        size_t valStart = value.find_first_not_of(" \t");
        value = (valStart == std::string::npos) ? "" : value.substr(valStart);
        size_t valEnd = value.find_last_not_of(" \t");
        if (valEnd != std::string::npos) value = value.substr(0, valEnd + 1);
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        if (key.empty()) continue;

        // Don't clobber a real environment variable the user already set.
        if (std::getenv(key.c_str()) != nullptr) continue;

#if defined(_WIN32)
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 0);
#endif
        ++loadedCount;
    }

    if (loadedCount > 0) {
        std::cout << "[dotenv] Loaded " << loadedCount << " variable(s) from " << path << "\n";
    }
}

int main() {
    // Diagnostic: print exactly which folder config.json/.env are being
    // looked for in, since both are loaded with relative paths ("config.json",
    // ".env") resolved against the process's current working directory -
    // NOT necessarily the folder the .exe file lives in (e.g. if launched
    // from a shortcut or IDE with a different "Start in" folder).
    std::cout << "[startup] Working directory: " << std::filesystem::current_path().string() << "\n";

    // Load backend/.env (if present) before anything else reads the
    // environment, so config.json's fallback SMTP values are only ever
    // used as a last resort and no manual PowerShell export is required.
    loadDotEnv(".env");
    if (!std::filesystem::exists(".env")) {
        std::cout << "[dotenv] No .env found in the working directory above - "
                     "SMTP will only work if SMTP_HOST/SMTP_USERNAME/SMTP_PASSWORD "
                     "are already set as real environment variables. See backend/.env.example.\n";
    }

    std::cout <<
        "==================================================================\n"
        "  Blockchain-Based E-Voting System — Backend Server\n"
        "  C++17 | cpp httplib-style router | MySQL 8.0 | OpenSSL SHA-256\n"
        "==================================================================\n";

    // ---- Load server config -------------------------------------------
    std::ifstream cfgFile("config.json");
    if (!cfgFile.is_open()) {
        std::cerr << "[FATAL] Could not find backend/config.json. "
                     "Run the server from inside the 'backend' folder "
                     "(or the build folder created next to it), or copy "
                     "config.json next to the .exe.\n";
        return 1;
    }
    std::stringstream cfgBuf;
    cfgBuf << cfgFile.rdbuf();
    sjson::Json config = sjson::Json::parse(cfgBuf.str());

    std::string host = config["server"].getString("host", "0.0.0.0");
    int port = config["server"].getInt("port", 8080);
    std::string corsOrigin = config["cors"].getString("allow_origin", "*");

    // ---- Load SMTP config (env vars override config.json's non-secret
    //      defaults; credentials are only ever read from the environment,
    //      never from config.json or source code) --------------------------
    std::string smtpFallbackHost = config["smtp"].getString("host", "");
    int smtpFallbackPort = config["smtp"].getInt("port", 465);
    std::string smtpFallbackFromName = config["smtp"].getString("from_name", "CivicChain E-Voting");

    SmtpConfig smtpConfig = EmailService::loadFromEnvironment(smtpFallbackHost, smtpFallbackPort, smtpFallbackFromName);
    EmailService emailService(smtpConfig);

    if (!smtpConfig.configured) {
        std::string missing;
        if (smtpConfig.host.empty())     missing += "SMTP_HOST ";
        if (smtpConfig.username.empty()) missing += "SMTP_USERNAME ";
        if (smtpConfig.password.empty()) missing += "SMTP_PASSWORD ";
        Logger::warn("SMTP is not configured - missing: " + missing +
                      "(never logging the actual values). Registration will still work, but OTP "
                      "verification emails will NOT be sent until these are set in backend/.env "
                      "or as real environment variables. See docs/INSTALL.md section 3.");
    } else {
        Logger::info("SMTP configured: sending OTP emails via " + smtpConfig.host + ":" + std::to_string(smtpConfig.port) +
                      " as " + smtpConfig.username);
    }

    // ---- Connect to MySQL -----------------------------------------------
    Database db;
    if (!db.connect("config.json")) {
        std::cerr << "\n[FATAL] Could not connect to MySQL.\n"
                     "  1) Make sure MySQL 8.0 server is running.\n"
                     "  2) Make sure you imported database/schema.sql:\n"
                     "       mysql -u root -p < ../database/schema.sql\n"
                     "  3) Check backend/config.json has the correct password.\n\n";
        return 1;
    }

    AuthService authService(db);

    AuthController authController(db, authService, emailService);
    ElectionController electionController(db, authService);
    CandidateController candidateController(db, authService);
    VoteController voteController(db, authService);
    AdminController adminController(db, authService);

    simple_http::Server server;

    server.set_default_headers({
        { "Access-Control-Allow-Origin", corsOrigin },
        { "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS" },
        { "Access-Control-Allow-Headers", "Content-Type, Authorization" }
    });

    // ---- Health check -----------------------------------------------------
    server.Get("/api/health", [](const simple_http::Request&, simple_http::Response& res) {
        sjson::Json data = sjson::Json::object();
        data["status"] = "ok";
        data["service"] = "evoting-backend";
        sendSuccess(res, data, "Server is running");
    });

    // ---- Auth routes --------------------------------------------------
    server.Post("/api/register", [&](const simple_http::Request& req, simple_http::Response& res) {
        authController.handleRegister(req, res);
    });
    server.Post("/api/verify-otp", [&](const simple_http::Request& req, simple_http::Response& res) {
        authController.handleVerifyOtp(req, res);
    });
    server.Post("/api/resend-otp", [&](const simple_http::Request& req, simple_http::Response& res) {
        authController.handleResendOtp(req, res);
    });
    server.Post("/api/login", [&](const simple_http::Request& req, simple_http::Response& res) {
        authController.handleLogin(req, res);
    });
    server.Post("/api/logout", [&](const simple_http::Request& req, simple_http::Response& res) {
        authController.handleLogout(req, res);
    });
    server.Get("/api/profile", [&](const simple_http::Request& req, simple_http::Response& res) {
        authController.handleProfile(req, res);
    });
    server.Post("/api/link-wallet", [&](const simple_http::Request& req, simple_http::Response& res) {
        authController.handleLinkWallet(req, res);
    });

    // ---- Election routes --------------------------------------------------
    server.Get("/api/elections", [&](const simple_http::Request& req, simple_http::Response& res) {
        electionController.handleListElections(req, res);
    });
    server.Get("/api/elections/:id", [&](const simple_http::Request& req, simple_http::Response& res) {
        electionController.handleGetElection(req, res);
    });
    server.Post("/api/elections", [&](const simple_http::Request& req, simple_http::Response& res) {
        electionController.handleCreateElection(req, res);
    });
    server.Put("/api/elections/:id", [&](const simple_http::Request& req, simple_http::Response& res) {
        electionController.handleUpdateElection(req, res);
    });
    server.Del("/api/elections/:id", [&](const simple_http::Request& req, simple_http::Response& res) {
        electionController.handleDeleteElection(req, res);
    });

    // ---- Candidate routes --------------------------------------------------
    server.Get("/api/candidates", [&](const simple_http::Request& req, simple_http::Response& res) {
        candidateController.handleListCandidates(req, res);
    });
    server.Post("/api/candidates", [&](const simple_http::Request& req, simple_http::Response& res) {
        candidateController.handleCreateCandidate(req, res);
    });
    server.Put("/api/candidates/:id", [&](const simple_http::Request& req, simple_http::Response& res) {
        candidateController.handleUpdateCandidate(req, res);
    });
    server.Del("/api/candidates/:id", [&](const simple_http::Request& req, simple_http::Response& res) {
        candidateController.handleDeleteCandidate(req, res);
    });

    // ---- Vote routes --------------------------------------------------
    server.Post("/api/vote", [&](const simple_http::Request& req, simple_http::Response& res) {
        voteController.handleCastVote(req, res);
    });
    server.Get("/api/results", [&](const simple_http::Request& req, simple_http::Response& res) {
        voteController.handleGetResults(req, res);
    });
    server.Get("/api/vote/check", [&](const simple_http::Request& req, simple_http::Response& res) {
        voteController.handleCheckVoted(req, res);
    });

    // ---- Admin routes --------------------------------------------------
    server.Get("/api/admin/stats", [&](const simple_http::Request& req, simple_http::Response& res) {
        adminController.handleStats(req, res);
    });
    server.Get("/api/admin/users", [&](const simple_http::Request& req, simple_http::Response& res) {
        adminController.handleListUsers(req, res);
    });
    server.Put("/api/admin/users/:id/status", [&](const simple_http::Request& req, simple_http::Response& res) {
        adminController.handleUpdateUserStatus(req, res);
    });
    server.Get("/api/admin/audit-logs", [&](const simple_http::Request& req, simple_http::Response& res) {
        adminController.handleAuditLogs(req, res);
    });

    Logger::info("Routes registered. Starting server on " + host + ":" + std::to_string(port));
    std::cout << "\n  API base URL: http://127.0.0.1:" << port << "/api\n"
              << "  Health check: http://127.0.0.1:" << port << "/api/health\n"
              << "  Default admin login -> admin@evoting.local / Admin@123\n"
              << "  Press Ctrl+C to stop.\n\n";

    server.listen(host, port);

    return 0;
}
