// ============================================================================
//  election_controller.cpp
// ============================================================================

#include "election_controller.hpp"
#include "response_helper.hpp"
#include "validators.hpp"
#include "logger.hpp"

namespace evoting {

static std::optional<AuthenticatedUser> requireAuth(AuthService& auth, const simple_http::Request& req) {
    std::string authHeader = req.header("authorization");
    std::string token = authHeader.rfind("Bearer ", 0) == 0 ? authHeader.substr(7) : authHeader;
    return auth.validateToken(token);
}

static sjson::Json electionRowToJson(const Row& r) {
    sjson::Json e = sjson::Json::object();
    e["electionId"] = r.getInt("election_id");
    e["title"] = r.get("title");
    e["description"] = r.isNull("description") ? "" : r.get("description");
    e["contractAddress"] = r.isNull("contract_address") ? "" : r.get("contract_address");
    e["startTime"] = r.get("start_time");
    e["endTime"] = r.get("end_time");
    e["status"] = r.get("status");
    return e;
}

// ----------------------------------------------------------------------
// GET /api/elections  — public, any visitor can browse elections
// ----------------------------------------------------------------------
void ElectionController::handleListElections(const simple_http::Request& req, simple_http::Response& res) {
    auto rows = db_.query(
        "SELECT * FROM elections ORDER BY "
        "FIELD(status, 'active', 'upcoming', 'ended', 'draft', 'cancelled'), start_time DESC"
    );

    sjson::Json list = sjson::Json::array();
    for (auto& r : rows) list.push_back(electionRowToJson(r));

    sendSuccess(res, list, "Elections fetched");
}

// ----------------------------------------------------------------------
// GET /api/elections/:id  — includes candidates
// ----------------------------------------------------------------------
void ElectionController::handleGetElection(const simple_http::Request& req, simple_http::Response& res) {
    auto it = req.params.find("id");
    if (it == req.params.end()) return sendError(res, "Election id required", 400);

    auto rows = db_.query("SELECT * FROM elections WHERE election_id = ? LIMIT 1", { it->second });
    if (rows.empty()) return sendError(res, "Election not found", 404);

    sjson::Json data = electionRowToJson(rows[0]);

    auto candidateRows = db_.query(
        "SELECT * FROM candidates WHERE election_id = ? ORDER BY on_chain_index ASC", { it->second }
    );
    sjson::Json candidates = sjson::Json::array();
    for (auto& c : candidateRows) {
        sjson::Json cj = sjson::Json::object();
        cj["candidateId"] = c.getInt("candidate_id");
        cj["fullName"] = c.get("full_name");
        cj["partyName"] = c.isNull("party_name") ? "" : c.get("party_name");
        cj["symbolUrl"] = c.isNull("symbol_url") ? "" : c.get("symbol_url");
        cj["bio"] = c.isNull("bio") ? "" : c.get("bio");
        cj["onChainIndex"] = c.getInt("on_chain_index");
        cj["voteCount"] = c.getInt("vote_count_cache");
        candidates.push_back(cj);
    }
    data["candidates"] = candidates;

    sendSuccess(res, data, "Election fetched");
}

// ----------------------------------------------------------------------
// POST /api/elections  — admin only
// Body: { title, description, startTime, endTime, contractAddress }
// ----------------------------------------------------------------------
void ElectionController::handleCreateElection(const simple_http::Request& req, simple_http::Response& res) {
    auto user = requireAuth(auth_, req);
    if (!user.has_value()) return sendError(res, "Unauthorized", 401);
    if (user->role != "admin") return sendError(res, "Only administrators can create elections", 403);

    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    std::string title = body.getString("title");
    std::string description = body.getString("description");
    std::string startTime = body.getString("startTime");
    std::string endTime = body.getString("endTime");
    std::string contractAddress = body.getString("contractAddress");

    if (!validate::isNonEmpty(title) || !validate::isNonEmpty(startTime) || !validate::isNonEmpty(endTime)) {
        return sendError(res, "Title, startTime and endTime are required", 422);
    }

    bool ok = db_.execute(
        "INSERT INTO elections (title, description, contract_address, start_time, end_time, status, created_by) "
        "VALUES (?, ?, ?, ?, ?, 'upcoming', ?)",
        { title, description, contractAddress, startTime, endTime, std::to_string(user->userId) }
    );

    if (!ok) return sendError(res, "Failed to create election", 500);

    unsigned long long electionId = db_.lastInsertId();

    db_.execute("INSERT INTO audit_logs (user_id, action, details) VALUES (?, 'ELECTION_CREATED', ?)",
                { std::to_string(user->userId), "Created election: " + title });

    sjson::Json data = sjson::Json::object();
    data["electionId"] = static_cast<int>(electionId);
    Logger::info("Election created: " + title + " by " + user->email);
    sendSuccess(res, data, "Election created successfully", 201);
}

// ----------------------------------------------------------------------
// PUT /api/elections/:id  — admin only
// ----------------------------------------------------------------------
void ElectionController::handleUpdateElection(const simple_http::Request& req, simple_http::Response& res) {
    auto user = requireAuth(auth_, req);
    if (!user.has_value()) return sendError(res, "Unauthorized", 401);
    if (user->role != "admin") return sendError(res, "Only administrators can update elections", 403);

    auto it = req.params.find("id");
    if (it == req.params.end()) return sendError(res, "Election id required", 400);

    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    std::string status = body.getString("status");
    std::string contractAddress = body.getString("contractAddress");

    if (!status.empty()) {
        db_.execute("UPDATE elections SET status = ? WHERE election_id = ?", { status, it->second });
    }
    if (!contractAddress.empty()) {
        db_.execute("UPDATE elections SET contract_address = ? WHERE election_id = ?",
                    { contractAddress, it->second });
    }

    db_.execute("INSERT INTO audit_logs (user_id, action, details) VALUES (?, 'ELECTION_UPDATED', ?)",
                { std::to_string(user->userId), "Updated election id " + it->second });

    sendSuccess(res, sjson::Json::object(), "Election updated successfully");
}

// ----------------------------------------------------------------------
// DELETE /api/elections/:id  — admin only
// ----------------------------------------------------------------------
void ElectionController::handleDeleteElection(const simple_http::Request& req, simple_http::Response& res) {
    auto user = requireAuth(auth_, req);
    if (!user.has_value()) return sendError(res, "Unauthorized", 401);
    if (user->role != "admin") return sendError(res, "Only administrators can delete elections", 403);

    auto it = req.params.find("id");
    if (it == req.params.end()) return sendError(res, "Election id required", 400);

    db_.execute("DELETE FROM elections WHERE election_id = ?", { it->second });

    db_.execute("INSERT INTO audit_logs (user_id, action, details) VALUES (?, 'ELECTION_DELETED', ?)",
                { std::to_string(user->userId), "Deleted election id " + it->second });

    sendSuccess(res, sjson::Json::object(), "Election deleted successfully");
}

} // namespace evoting
