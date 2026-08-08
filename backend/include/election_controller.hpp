// ============================================================================
//  election_controller.hpp
//  Handles: GET /api/elections, GET /api/elections/:id, POST /api/elections,
//           PUT /api/elections/:id, DELETE /api/elections/:id
// ============================================================================

#pragma once

#include "../lib/simple_http.hpp"
#include "database.hpp"
#include "auth_service.hpp"

namespace evoting {

class ElectionController {
public:
    ElectionController(Database& db, AuthService& auth) : db_(db), auth_(auth) {}

    void handleListElections(const simple_http::Request& req, simple_http::Response& res);
    void handleGetElection(const simple_http::Request& req, simple_http::Response& res);
    void handleCreateElection(const simple_http::Request& req, simple_http::Response& res);
    void handleUpdateElection(const simple_http::Request& req, simple_http::Response& res);
    void handleDeleteElection(const simple_http::Request& req, simple_http::Response& res);

private:
    Database& db_;
    AuthService& auth_;
};

} // namespace evoting
