// ============================================================================
//  auth_service.cpp
// ============================================================================

#include "auth_service.hpp"
#include "crypto.hpp"
#include "logger.hpp"

namespace evoting {

std::string AuthService::createSession(int userId, const std::string& ipAddress) {
    std::string token = crypto::randomHex(32); // 64 hex chars

    db_.execute(
        "INSERT INTO sessions (user_id, session_token, ip_address, expires_at) "
        "VALUES (?, ?, ?, DATE_ADD(NOW(), INTERVAL 24 HOUR))",
        { std::to_string(userId), token, ipAddress }
    );

    return token;
}

std::optional<AuthenticatedUser> AuthService::validateToken(const std::string& token) {
    if (token.empty()) return std::nullopt;

    auto rows = db_.query(
        "SELECT u.user_id, u.full_name, u.email, u.role, u.wallet_address, u.is_active "
        "FROM sessions s "
        "JOIN users u ON u.user_id = s.user_id "
        "WHERE s.session_token = ? AND s.expires_at > NOW() "
        "LIMIT 1",
        { token }
    );

    if (rows.empty()) return std::nullopt;

    const Row& r = rows[0];
    if (r.getInt("is_active", 1) == 0) return std::nullopt;

    AuthenticatedUser user;
    user.userId = r.getInt("user_id");
    user.fullName = r.get("full_name");
    user.email = r.get("email");
    user.role = r.get("role");
    user.walletAddress = r.isNull("wallet_address") ? "" : r.get("wallet_address");
    return user;
}

bool AuthService::destroySession(const std::string& token) {
    return db_.execute("DELETE FROM sessions WHERE session_token = ?", { token });
}

} // namespace evoting
