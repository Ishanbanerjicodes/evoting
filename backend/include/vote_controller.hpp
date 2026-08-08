// ============================================================================
//  vote_controller.hpp
//  Handles: POST /api/vote, GET /api/results?electionId=1
//
//  The actual on-chain vote transaction is signed and submitted by the
//  frontend via MetaMask/Ethers.js directly against the smart contract
//  (the backend never holds a voter's private key — that would defeat the
//  purpose of using a wallet). This controller's job is to RECORD the
//  resulting transaction hash for audit/reporting once the frontend
//  confirms the on-chain transaction succeeded, and to enforce a second,
//  database-level guard against double voting.
// ============================================================================

#pragma once

#include "../lib/simple_http.hpp"
#include "database.hpp"
#include "auth_service.hpp"

namespace evoting {

class VoteController {
public:
    VoteController(Database& db, AuthService& auth) : db_(db), auth_(auth) {}

    void handleCastVote(const simple_http::Request& req, simple_http::Response& res);
    void handleGetResults(const simple_http::Request& req, simple_http::Response& res);
    void handleCheckVoted(const simple_http::Request& req, simple_http::Response& res);

private:
    Database& db_;
    AuthService& auth_;
};

} // namespace evoting
