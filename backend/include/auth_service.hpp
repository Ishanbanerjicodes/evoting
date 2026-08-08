// ============================================================================
//  auth_service.hpp
//  Handles session token issuing/validation and role-based access checks.
//  Sits between controllers and the database so route handlers stay thin.
// ============================================================================

#pragma once

#include "database.hpp"
#include <string>
#include <optional>

namespace evoting {

struct AuthenticatedUser {
    int userId = 0;
    std::string fullName;
    std::string email;
    std::string role; // "admin" or "voter"
    std::string walletAddress;
};

class AuthService {
public:
    explicit AuthService(Database& db) : db_(db) {}

    // Creates a new session row and returns the bearer token to give to
    // the client. Sessions expire after 24 hours.
    std::string createSession(int userId, const std::string& ipAddress);

    // Validates a bearer token from the Authorization header. Returns
    // std::nullopt if the token is missing, invalid, or expired.
    std::optional<AuthenticatedUser> validateToken(const std::string& token);

    // Deletes the session row associated with a token (logout).
    bool destroySession(const std::string& token);

private:
    Database& db_;
};

} // namespace evoting
