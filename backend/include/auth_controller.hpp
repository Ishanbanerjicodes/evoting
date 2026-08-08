// ============================================================================
//  auth_controller.hpp
//  Handles: POST /api/register, POST /api/login, POST /api/logout,
//           GET /api/profile, POST /api/verify-otp, POST /api/resend-otp
// ============================================================================

#pragma once

#include "../lib/simple_http.hpp"
#include "database.hpp"
#include "auth_service.hpp"
#include "email_service.hpp"

namespace evoting {

class AuthController {
public:
    AuthController(Database& db, AuthService& auth, EmailService& email)
        : db_(db), auth_(auth), email_(email) {}

    void handleRegister(const simple_http::Request& req, simple_http::Response& res);
    void handleLogin(const simple_http::Request& req, simple_http::Response& res);
    void handleLogout(const simple_http::Request& req, simple_http::Response& res);
    void handleProfile(const simple_http::Request& req, simple_http::Response& res);
    void handleVerifyOtp(const simple_http::Request& req, simple_http::Response& res);
    void handleResendOtp(const simple_http::Request& req, simple_http::Response& res);
    void handleLinkWallet(const simple_http::Request& req, simple_http::Response& res);

private:
    Database& db_;
    AuthService& auth_;
    EmailService& email_;
};

} // namespace evoting
