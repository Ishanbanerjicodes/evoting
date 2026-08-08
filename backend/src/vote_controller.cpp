// ============================================================================
//  vote_controller.cpp
// ============================================================================

#include "vote_controller.hpp"
#include "response_helper.hpp"
#include "validators.hpp"
#include "crypto.hpp"
#include "logger.hpp"

namespace evoting {

static std::optional<AuthenticatedUser> requireAuth(AuthService& auth, const simple_http::Request& req) {
    std::string authHeader = req.header("authorization");
    std::string token = authHeader.rfind("Bearer ", 0) == 0 ? authHeader.substr(7) : authHeader;
    return auth.validateToken(token);
}

// ----------------------------------------------------------------------
// POST /api/vote  (Authorization: Bearer <token>)
// Body: { electionId, candidateId, voterWallet, txHash, blockNumber }
//
// Called by the frontend AFTER MetaMask has confirmed the on-chain vote
// transaction. The backend re-verifies eligibility server-side (not just
// trusting the frontend) before persisting the audit record.
// ----------------------------------------------------------------------
void VoteController::handleCastVote(const simple_http::Request& req, simple_http::Response& res) {
    auto user = requireAuth(auth_, req);
    if (!user.has_value()) return sendError(res, "Unauthorized. Please log in again.", 401);

    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    int electionId = body.getInt("electionId");
    int candidateId = body.getInt("candidateId");
    std::string voterWallet = body.getString("voterWallet");
    std::string txHash = body.getString("txHash");
    int blockNumber = body.getInt("blockNumber");

    if (electionId <= 0 || candidateId <= 0 || !validate::isNonEmpty(txHash)) {
        return sendError(res, "electionId, candidateId and txHash are required", 422);
    }
    if (!validate::isValidEthereumAddress(voterWallet)) {
        return sendError(res, "A valid connected wallet address is required to vote", 422);
    }

    // 1) Confirm the election exists and is currently active.
    auto electionRows = db_.query(
        "SELECT status, start_time, end_time FROM elections WHERE election_id = ? LIMIT 1",
        { std::to_string(electionId) }
    );
    if (electionRows.empty()) return sendError(res, "Election not found", 404);
    if (electionRows[0].get("status") != "active") {
        return sendError(res, "Voting is not currently open for this election", 403);
    }

    // 2) Confirm the candidate belongs to this election.
    auto candidateRows = db_.query(
        "SELECT candidate_id FROM candidates WHERE candidate_id = ? AND election_id = ? LIMIT 1",
        { std::to_string(candidateId), std::to_string(electionId) }
    );
    if (candidateRows.empty()) return sendError(res, "Candidate does not belong to this election", 400);

    // 3) Database-level double-vote guard (mirrors the smart contract's
    //    own hasVoted mapping — belt-and-braces defense in depth).
    auto existingVote = db_.query(
        "SELECT vote_id FROM votes WHERE election_id = ? AND user_id = ? LIMIT 1",
        { std::to_string(electionId), std::to_string(user->userId) }
    );
    if (!existingVote.empty()) {
        return sendError(res, "You have already voted in this election", 409);
    }

    // 4) Build a tamper-evident fingerprint of the vote payload for the
    //    audit log — independent of (and in addition to) the blockchain
    //    transaction itself.
    std::string fingerprint = std::to_string(electionId) + "|" + std::to_string(candidateId) + "|" +
                               std::to_string(user->userId) + "|" + voterWallet + "|" + txHash;
    std::string voteHash = crypto::sha256(fingerprint);

    bool inserted = db_.execute(
        "INSERT INTO votes (election_id, candidate_id, user_id, voter_wallet, tx_hash, block_number, vote_hash) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        {
            std::to_string(electionId), std::to_string(candidateId), std::to_string(user->userId),
            voterWallet, txHash, std::to_string(blockNumber), voteHash
        }
    );

    if (!inserted) {
        // Most likely cause: the UNIQUE(election_id, user_id) or
        // UNIQUE(tx_hash) constraint fired — i.e. a race-condition double
        // vote or a resubmitted transaction hash.
        return sendError(res, "Vote could not be recorded. You may have already voted, "
                              "or this transaction was already recorded.", 409);
    }

    db_.execute("INSERT INTO audit_logs (user_id, action, details) VALUES (?, 'VOTE_CAST', ?)",
                { std::to_string(user->userId), "Voted in election " + std::to_string(electionId) +
                  " | tx: " + txHash });

    Logger::info("Vote recorded: user=" + user->email + " election=" + std::to_string(electionId) +
                 " candidate=" + std::to_string(candidateId) + " tx=" + txHash);

    sjson::Json data = sjson::Json::object();
    data["voteHash"] = voteHash;
    sendSuccess(res, data, "Your vote has been recorded successfully", 201);
}

// ----------------------------------------------------------------------
// GET /api/results?electionId=1  — public (results are meant to be
// transparent and independently verifiable against the blockchain)
// ----------------------------------------------------------------------
void VoteController::handleGetResults(const simple_http::Request& req, simple_http::Response& res) {
    auto it = req.query.find("electionId");
    if (it == req.query.end()) return sendError(res, "electionId query parameter is required", 400);

    auto rows = db_.query(
        "SELECT candidate_id, full_name, party_name, vote_count_cache, vote_percentage "
        "FROM election_results WHERE election_id = ? ORDER BY vote_count_cache DESC",
        { it->second }
    );

    int totalVotes = 0;
    for (auto& r : rows) totalVotes += r.getInt("vote_count_cache");

    sjson::Json data = sjson::Json::object();
    data["totalVotes"] = totalVotes;

    sjson::Json candidates = sjson::Json::array();
    for (auto& r : rows) {
        sjson::Json cj = sjson::Json::object();
        cj["candidateId"] = r.getInt("candidate_id");
        cj["fullName"] = r.get("full_name");
        cj["partyName"] = r.isNull("party_name") ? "" : r.get("party_name");
        cj["voteCount"] = r.getInt("vote_count_cache");
        cj["votePercentage"] = r.get("vote_percentage", "0");
        candidates.push_back(cj);
    }
    data["candidates"] = candidates;

    sendSuccess(res, data, "Results fetched");
}

// ----------------------------------------------------------------------
// GET /api/vote/check?electionId=1  (Authorization: Bearer <token>)
// Lets the frontend disable the vote button if the logged-in voter has
// already voted in this election.
// ----------------------------------------------------------------------
void VoteController::handleCheckVoted(const simple_http::Request& req, simple_http::Response& res) {
    auto user = requireAuth(auth_, req);
    if (!user.has_value()) return sendError(res, "Unauthorized", 401);

    auto it = req.query.find("electionId");
    if (it == req.query.end()) return sendError(res, "electionId query parameter is required", 400);

    auto rows = db_.query(
        "SELECT vote_id, tx_hash FROM votes WHERE election_id = ? AND user_id = ? LIMIT 1",
        { it->second, std::to_string(user->userId) }
    );

    sjson::Json data = sjson::Json::object();
    data["hasVoted"] = !rows.empty();
    data["txHash"] = rows.empty() ? "" : rows[0].get("tx_hash");
    sendSuccess(res, data, "Vote status checked");
}

} // namespace evoting
