// ============================================================================
//  candidate_controller.cpp
// ============================================================================

#include "candidate_controller.hpp"
#include "response_helper.hpp"
#include "validators.hpp"
#include "logger.hpp"

namespace evoting {

static std::optional<AuthenticatedUser> requireAuth(AuthService& auth, const simple_http::Request& req) {
    std::string authHeader = req.header("authorization");
    std::string token = authHeader.rfind("Bearer ", 0) == 0 ? authHeader.substr(7) : authHeader;
    return auth.validateToken(token);
}

// ----------------------------------------------------------------------
// GET /api/candidates?electionId=1  — public
// ----------------------------------------------------------------------
void CandidateController::handleListCandidates(const simple_http::Request& req, simple_http::Response& res) {
    auto it = req.query.find("electionId");
    if (it == req.query.end()) return sendError(res, "electionId query parameter is required", 400);

    auto rows = db_.query(
        "SELECT * FROM candidates WHERE election_id = ? ORDER BY on_chain_index ASC", { it->second }
    );

    sjson::Json list = sjson::Json::array();
    for (auto& c : rows) {
        sjson::Json cj = sjson::Json::object();
        cj["candidateId"] = c.getInt("candidate_id");
        cj["electionId"] = c.getInt("election_id");
        cj["fullName"] = c.get("full_name");
        cj["partyName"] = c.isNull("party_name") ? "" : c.get("party_name");
        cj["symbolUrl"] = c.isNull("symbol_url") ? "" : c.get("symbol_url");
        cj["bio"] = c.isNull("bio") ? "" : c.get("bio");
        cj["onChainIndex"] = c.getInt("on_chain_index");
        cj["voteCount"] = c.getInt("vote_count_cache");
        list.push_back(cj);
    }

    sendSuccess(res, list, "Candidates fetched");
}

// ----------------------------------------------------------------------
// POST /api/candidates  — admin only
// Body: { electionId, fullName, partyName, symbolUrl, bio, onChainIndex }
// ----------------------------------------------------------------------
void CandidateController::handleCreateCandidate(const simple_http::Request& req, simple_http::Response& res) {
    auto user = requireAuth(auth_, req);
    if (!user.has_value()) return sendError(res, "Unauthorized", 401);
    if (user->role != "admin") return sendError(res, "Only administrators can add candidates", 403);

    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    int electionId = body.getInt("electionId");
    std::string fullName = body.getString("fullName");
    std::string partyName = body.getString("partyName");
    std::string symbolUrl = body.getString("symbolUrl");
    std::string bio = body.getString("bio");
    int onChainIndex = body.getInt("onChainIndex");

    if (electionId <= 0 || !validate::isNonEmpty(fullName)) {
        return sendError(res, "electionId and fullName are required", 422);
    }

    bool ok = db_.execute(
        "INSERT INTO candidates (election_id, full_name, party_name, symbol_url, bio, on_chain_index) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        { std::to_string(electionId), fullName, partyName, symbolUrl, bio, std::to_string(onChainIndex) }
    );

    if (!ok) return sendError(res, "Failed to add candidate (check on-chain index is unique for this election)", 500);

    unsigned long long candidateId = db_.lastInsertId();

    db_.execute("INSERT INTO audit_logs (user_id, action, details) VALUES (?, 'CANDIDATE_ADDED', ?)",
                { std::to_string(user->userId), "Added candidate: " + fullName });

    sjson::Json data = sjson::Json::object();
    data["candidateId"] = static_cast<int>(candidateId);
    sendSuccess(res, data, "Candidate added successfully", 201);
}

// ----------------------------------------------------------------------
// PUT /api/candidates/:id  — admin only
// ----------------------------------------------------------------------
void CandidateController::handleUpdateCandidate(const simple_http::Request& req, simple_http::Response& res) {
    auto user = requireAuth(auth_, req);
    if (!user.has_value()) return sendError(res, "Unauthorized", 401);
    if (user->role != "admin") return sendError(res, "Only administrators can update candidates", 403);

    auto it = req.params.find("id");
    if (it == req.params.end()) return sendError(res, "Candidate id required", 400);

    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    std::string fullName = body.getString("fullName");
    std::string partyName = body.getString("partyName");
    std::string bio = body.getString("bio");

    db_.execute(
        "UPDATE candidates SET full_name = ?, party_name = ?, bio = ? WHERE candidate_id = ?",
        { fullName, partyName, bio, it->second }
    );

    sendSuccess(res, sjson::Json::object(), "Candidate updated successfully");
}

// ----------------------------------------------------------------------
// DELETE /api/candidates/:id  — admin only
// ----------------------------------------------------------------------
void CandidateController::handleDeleteCandidate(const simple_http::Request& req, simple_http::Response& res) {
    auto user = requireAuth(auth_, req);
    if (!user.has_value()) return sendError(res, "Unauthorized", 401);
    if (user->role != "admin") return sendError(res, "Only administrators can delete candidates", 403);

    auto it = req.params.find("id");
    if (it == req.params.end()) return sendError(res, "Candidate id required", 400);

    db_.execute("DELETE FROM candidates WHERE candidate_id = ?", { it->second });

    sendSuccess(res, sjson::Json::object(), "Candidate deleted successfully");
}

} // namespace evoting
