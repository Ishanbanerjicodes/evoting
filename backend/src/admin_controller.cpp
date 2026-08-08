// ============================================================================
//  admin_controller.cpp
// ============================================================================

#include "admin_controller.hpp"
#include "response_helper.hpp"

namespace evoting {

static std::optional<AuthenticatedUser> requireAdmin(AuthService& auth, const simple_http::Request& req) {
    std::string authHeader = req.header("authorization");
    std::string token = authHeader.rfind("Bearer ", 0) == 0 ? authHeader.substr(7) : authHeader;
    auto user = auth.validateToken(token);
    if (!user.has_value() || user->role != "admin") return std::nullopt;
    return user;
}

// ----------------------------------------------------------------------
// GET /api/admin/stats  — admin only
// ----------------------------------------------------------------------
void AdminController::handleStats(const simple_http::Request& req, simple_http::Response& res) {
    auto admin = requireAdmin(auth_, req);
    if (!admin.has_value()) return sendError(res, "Unauthorized. Admin access required.", 403);

    auto totalVoters = db_.query("SELECT COUNT(*) AS c FROM users WHERE role = 'voter'");
    auto totalElections = db_.query("SELECT COUNT(*) AS c FROM elections");
    auto activeElections = db_.query("SELECT COUNT(*) AS c FROM elections WHERE status = 'active'");
    auto totalVotes = db_.query("SELECT COUNT(*) AS c FROM votes");
    auto verifiedVoters = db_.query("SELECT COUNT(*) AS c FROM users WHERE role = 'voter' AND is_verified = 1");

    sjson::Json data = sjson::Json::object();
    data["totalVoters"] = totalVoters.empty() ? 0 : totalVoters[0].getInt("c");
    data["verifiedVoters"] = verifiedVoters.empty() ? 0 : verifiedVoters[0].getInt("c");
    data["totalElections"] = totalElections.empty() ? 0 : totalElections[0].getInt("c");
    data["activeElections"] = activeElections.empty() ? 0 : activeElections[0].getInt("c");
    data["totalVotes"] = totalVotes.empty() ? 0 : totalVotes[0].getInt("c");

    sendSuccess(res, data, "Dashboard stats fetched");
}

// ----------------------------------------------------------------------
// GET /api/admin/users  — admin only
// ----------------------------------------------------------------------
void AdminController::handleListUsers(const simple_http::Request& req, simple_http::Response& res) {
    auto admin = requireAdmin(auth_, req);
    if (!admin.has_value()) return sendError(res, "Unauthorized. Admin access required.", 403);

    auto rows = db_.query(
        "SELECT user_id, full_name, email, voter_id_number, role, is_verified, is_active, "
        "wallet_address, created_at FROM users ORDER BY created_at DESC"
    );

    sjson::Json list = sjson::Json::array();
    for (auto& r : rows) {
        sjson::Json u = sjson::Json::object();
        u["userId"] = r.getInt("user_id");
        u["fullName"] = r.get("full_name");
        u["email"] = r.get("email");
        u["voterIdNumber"] = r.get("voter_id_number");
        u["role"] = r.get("role");
        u["isVerified"] = r.getInt("is_verified") == 1;
        u["isActive"] = r.getInt("is_active") == 1;
        u["walletAddress"] = r.isNull("wallet_address") ? "" : r.get("wallet_address");
        u["createdAt"] = r.get("created_at");
        list.push_back(u);
    }

    sendSuccess(res, list, "Users fetched");
}

// ----------------------------------------------------------------------
// PUT /api/admin/users/:id/status  — admin only
// Body: { isActive: bool }
// ----------------------------------------------------------------------
void AdminController::handleUpdateUserStatus(const simple_http::Request& req, simple_http::Response& res) {
    auto admin = requireAdmin(auth_, req);
    if (!admin.has_value()) return sendError(res, "Unauthorized. Admin access required.", 403);

    auto it = req.params.find("id");
    if (it == req.params.end()) return sendError(res, "User id required", 400);

    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    bool isActive = body.getBool("isActive", true);

    db_.execute("UPDATE users SET is_active = ? WHERE user_id = ?",
                { isActive ? "1" : "0", it->second });

    db_.execute("INSERT INTO audit_logs (user_id, action, details) VALUES (?, 'USER_STATUS_CHANGED', ?)",
                { std::to_string(admin->userId),
                  "Set user " + it->second + " active=" + std::string(isActive ? "1" : "0") });

    sendSuccess(res, sjson::Json::object(), "User status updated");
}

// ----------------------------------------------------------------------
// GET /api/admin/audit-logs  — admin only
// ----------------------------------------------------------------------
void AdminController::handleAuditLogs(const simple_http::Request& req, simple_http::Response& res) {
    auto admin = requireAdmin(auth_, req);
    if (!admin.has_value()) return sendError(res, "Unauthorized. Admin access required.", 403);

    auto rows = db_.query(
        "SELECT al.log_id, al.action, al.details, al.ip_address, al.created_at, "
        "u.full_name, u.email "
        "FROM audit_logs al LEFT JOIN users u ON u.user_id = al.user_id "
        "ORDER BY al.created_at DESC LIMIT 200"
    );

    sjson::Json list = sjson::Json::array();
    for (auto& r : rows) {
        sjson::Json l = sjson::Json::object();
        l["logId"] = r.getInt("log_id");
        l["action"] = r.get("action");
        l["details"] = r.isNull("details") ? "" : r.get("details");
        l["userFullName"] = r.isNull("full_name") ? "System" : r.get("full_name");
        l["userEmail"] = r.isNull("email") ? "" : r.get("email");
        l["createdAt"] = r.get("created_at");
        list.push_back(l);
    }

    sendSuccess(res, list, "Audit logs fetched");
}

} // namespace evoting
