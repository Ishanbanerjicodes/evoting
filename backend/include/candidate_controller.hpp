// ============================================================================
//  candidate_controller.hpp
//  Handles: GET /api/candidates, POST /api/candidates,
//           PUT /api/candidates/:id, DELETE /api/candidates/:id
// ============================================================================

#pragma once

#include "../lib/simple_http.hpp"
#include "database.hpp"
#include "auth_service.hpp"

namespace evoting {

class CandidateController {
public:
    CandidateController(Database& db, AuthService& auth) : db_(db), auth_(auth) {}

    void handleListCandidates(const simple_http::Request& req, simple_http::Response& res);
    void handleCreateCandidate(const simple_http::Request& req, simple_http::Response& res);
    void handleUpdateCandidate(const simple_http::Request& req, simple_http::Response& res);
    void handleDeleteCandidate(const simple_http::Request& req, simple_http::Response& res);

private:
    Database& db_;
    AuthService& auth_;
};

} // namespace evoting
