// ============================================================================
//  admin_controller.hpp
//  Handles: GET /api/admin/stats, GET /api/admin/users,
//           PUT /api/admin/users/:id/status, GET /api/admin/audit-logs
// ============================================================================

#pragma once

#include "../lib/simple_http.hpp"
#include "database.hpp"
#include "auth_service.hpp"

namespace evoting {

class AdminController {
public:
    AdminController(Database& db, AuthService& auth) : db_(db), auth_(auth) {}

    void handleStats(const simple_http::Request& req, simple_http::Response& res);
    void handleListUsers(const simple_http::Request& req, simple_http::Response& res);
    void handleUpdateUserStatus(const simple_http::Request& req, simple_http::Response& res);
    void handleAuditLogs(const simple_http::Request& req, simple_http::Response& res);

private:
    Database& db_;
    AuthService& auth_;
};

} // namespace evoting
