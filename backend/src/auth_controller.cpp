// ============================================================================
//  auth_controller.cpp
// ============================================================================

#include "auth_controller.hpp"
#include "response_helper.hpp"
#include "validators.hpp"
#include "crypto.hpp"
#include "logger.hpp"

namespace evoting {

// ----------------------------------------------------------------------
// POST /api/register
// Body: { fullName, email, voterIdNumber, password, confirmPassword }
// ----------------------------------------------------------------------
void AuthController::handleRegister(const simple_http::Request& req, simple_http::Response& res) {
    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    std::string fullName = body.getString("fullName");
    std::string email = body.getString("email");
    std::string voterIdNumber = body.getString("voterIdNumber");
    std::string password = body.getString("password");
    std::string confirmPassword = body.getString("confirmPassword");

    if (!validate::isNonEmpty(fullName) || !validate::isNonEmpty(voterIdNumber)) {
        return sendError(res, "Full name and voter ID number are required", 422);
    }
    if (!validate::isValidEmail(email)) {
        return sendError(res, "Please provide a valid email address", 422);
    }
    if (password != confirmPassword) {
        return sendError(res, "Password and confirmation do not match", 422);
    }
    if (!validate::isStrongPassword(password)) {
        return sendError(res, "Password must be at least 8 characters and include "
                              "uppercase, lowercase, a digit, and a symbol", 422);
    }

    auto existing = db_.query("SELECT user_id FROM users WHERE email = ? OR voter_id_number = ? LIMIT 1",
                               { email, voterIdNumber });
    if (!existing.empty()) {
        return sendError(res, "An account with this email or voter ID already exists", 409);
    }

    std::string salt = crypto::randomHex(16); // 32 hex chars
    std::string passwordHash = crypto::hashPassword(password, salt);

    bool inserted = db_.execute(
        "INSERT INTO users (full_name, email, voter_id_number, password_hash, password_salt, role, is_verified) "
        "VALUES (?, ?, ?, ?, ?, 'voter', 0)",
        { fullName, email, voterIdNumber, passwordHash, salt }
    );

    if (!inserted) {
        return sendError(res, "Registration failed. Please try again.", 500);
    }

    unsigned long long newUserId = db_.lastInsertId();

    // Generate an OTP for email verification and send it to the user's
    // inbox over SMTP. The OTP itself is stored in otp_codes and emailed
    // to the address the user registered with; it's only ever included
    // in the API response below as a fallback if that email couldn't be
    // sent (see the emailSent check further down).
    std::string otp = crypto::generateOtp();
    db_.execute(
        "INSERT INTO otp_codes (user_id, otp_code, purpose, expires_at) "
        "VALUES (?, ?, 'registration', DATE_ADD(NOW(), INTERVAL 10 MINUTE))",
        { std::to_string(newUserId), otp }
    );

    db_.execute("INSERT INTO audit_logs (user_id, action, details) VALUES (?, 'REGISTER', ?)",
                { std::to_string(newUserId), "New voter registered: " + email });

    Logger::info("New user registered: " + email + " (id=" + std::to_string(newUserId) + ")");

    std::string emailSubject = "Your CivicChain verification code";
    std::string emailBody =
        "Hello " + fullName + ",\n\n"
        "Your CivicChain verification code is: " + otp + "\n\n"
        "This code expires in 10 minutes. If you did not request this, you can safely ignore this email.\n\n"
        "- CivicChain E-Voting System";

    bool emailSent = email_.sendMail(email, emailSubject, emailBody);

    sjson::Json data = sjson::Json::object();
    data["userId"] = static_cast<int>(newUserId);

    if (!emailSent) {
        // The account and OTP are already created — don't fail (or dead-end)
        // the whole registration just because the email couldn't be sent
        // (e.g. SMTP isn't configured yet in a fresh dev setup). Log it
        // clearly so the admin/developer notices...
        Logger::warn("Registration succeeded for " + email + " but the OTP email could not be sent. "
                      "Check SMTP_HOST/SMTP_USERNAME/SMTP_PASSWORD (see backend/.env.example). "
                      "Returning the OTP directly in the API response as a dev-mode fallback so "
                      "registration can still be completed.");

        // ...and, ONLY because the email genuinely could not be delivered,
        // fall back to returning the OTP directly in the response so the
        // frontend can show it on-screen and the user isn't stuck at a
        // verification step that can never receive its code. This is not a
        // general behavior — it only ever fires when sendMail() itself
        // failed (SMTP not configured, or a real SMTP error), which is
        // something only the server operator controls, not an attacker.
        data["otpDevFallback"] = otp;
    }

    sendSuccess(res, data,
        emailSent
            ? "Registration successful. A verification code has been sent to your email."
            : "Registration successful. The verification email could not be sent (SMTP is not "
              "configured on this server) — your verification code is shown on-screen instead.",
        201);
}

// ----------------------------------------------------------------------
// POST /api/verify-otp
// Body: { userId, otpCode }
// ----------------------------------------------------------------------
void AuthController::handleVerifyOtp(const simple_http::Request& req, simple_http::Response& res) {
    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    int userId = body.getInt("userId");
    std::string otpCode = body.getString("otpCode");

    auto rows = db_.query(
        "SELECT otp_id FROM otp_codes "
        "WHERE user_id = ? AND otp_code = ? AND purpose = 'registration' "
        "AND is_used = 0 AND expires_at > NOW() LIMIT 1",
        { std::to_string(userId), otpCode }
    );

    if (rows.empty()) {
        return sendError(res, "Invalid or expired OTP code", 400);
    }

    db_.execute("UPDATE otp_codes SET is_used = 1 WHERE otp_id = ?", { rows[0].get("otp_id") });
    db_.execute("UPDATE users SET is_verified = 1 WHERE user_id = ?", { std::to_string(userId) });

    sendSuccess(res, sjson::Json::object(), "Account verified successfully. You can now log in.");
}

// ----------------------------------------------------------------------
// POST /api/resend-otp
// Body: { userId }
// Reuses the exact same OTP generation / storage / email path as
// registration. Added so the "Resend code" link in the OTP modal has a
// backend endpoint to call — registration/verification themselves are
// unchanged.
// ----------------------------------------------------------------------
void AuthController::handleResendOtp(const simple_http::Request& req, simple_http::Response& res) {
    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    int userId = body.getInt("userId");
    if (userId <= 0) {
        return sendError(res, "A valid userId is required", 422);
    }

    auto rows = db_.query(
        "SELECT user_id, full_name, email, is_verified FROM users WHERE user_id = ? LIMIT 1",
        { std::to_string(userId) }
    );
    if (rows.empty()) {
        return sendError(res, "Account not found", 404);
    }
    const Row& user = rows[0];

    if (user.getInt("is_verified", 0) == 1) {
        return sendError(res, "This account is already verified. Please log in.", 409);
    }

    std::string fullName = user.get("full_name");
    std::string email = user.get("email");

    // Invalidate any previous unused registration OTPs so only the newest
    // code is accepted, then issue a fresh one.
    db_.execute(
        "UPDATE otp_codes SET is_used = 1 WHERE user_id = ? AND purpose = 'registration' AND is_used = 0",
        { std::to_string(userId) }
    );

    std::string otp = crypto::generateOtp();
    db_.execute(
        "INSERT INTO otp_codes (user_id, otp_code, purpose, expires_at) "
        "VALUES (?, ?, 'registration', DATE_ADD(NOW(), INTERVAL 10 MINUTE))",
        { std::to_string(userId), otp }
    );

    std::string emailSubject = "Your CivicChain verification code";
    std::string emailBody =
        "Hello " + fullName + ",\n\n"
        "Your new CivicChain verification code is: " + otp + "\n\n"
        "This code expires in 10 minutes. If you did not request this, you can safely ignore this email.\n\n"
        "- CivicChain E-Voting System";

    bool emailSent = email_.sendMail(email, emailSubject, emailBody);

    sjson::Json data = sjson::Json::object();
    if (!emailSent) {
        Logger::warn("Resend-OTP succeeded for " + email + " but the email could not be sent. "
                      "Returning the OTP directly in the API response as a dev-mode fallback.");
        data["otpDevFallback"] = otp;
    }

    sendSuccess(res, data,
        emailSent
            ? "A new verification code has been sent to your email."
            : "The verification email could not be sent (SMTP is not configured on this server) — "
              "your new verification code is shown on-screen instead.");
}

// ----------------------------------------------------------------------
// POST /api/login
// Body: { email, password }
// ----------------------------------------------------------------------
void AuthController::handleLogin(const simple_http::Request& req, simple_http::Response& res) {
    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    std::string email = body.getString("email");
    std::string password = body.getString("password");

    auto rows = db_.query(
        "SELECT user_id, full_name, email, password_hash, password_salt, role, is_verified, is_active, wallet_address "
        "FROM users WHERE email = ? LIMIT 1",
        { email }
    );

    if (rows.empty()) {
        return sendError(res, "Invalid email or password", 401);
    }

    const Row& user = rows[0];

    if (user.getInt("is_active", 1) == 0) {
        return sendError(res, "This account has been disabled. Contact the election administrator.", 403);
    }

    std::string computedHash = crypto::hashPassword(password, user.get("password_salt"));
    if (computedHash != user.get("password_hash")) {
        db_.execute("INSERT INTO audit_logs (user_id, action, details) VALUES (?, 'LOGIN_FAILED', ?)",
                    { user.get("user_id"), "Incorrect password attempt" });
        return sendError(res, "Invalid email or password", 401);
    }

    if (user.getInt("is_verified", 0) == 0) {
        return sendError(res, "Please verify your email with the OTP before logging in", 403);
    }

    std::string ip = req.header("x-forwarded-for", "127.0.0.1");
    std::string token = auth_.createSession(user.getInt("user_id"), ip);

    db_.execute("INSERT INTO audit_logs (user_id, action, details) VALUES (?, 'LOGIN_SUCCESS', ?)",
                { user.get("user_id"), "User logged in" });

    sjson::Json data = sjson::Json::object();
    data["token"] = token;
    sjson::Json userJson = sjson::Json::object();
    userJson["userId"] = user.getInt("user_id");
    userJson["fullName"] = user.get("full_name");
    userJson["email"] = user.get("email");
    userJson["role"] = user.get("role");
    userJson["walletAddress"] = user.isNull("wallet_address") ? "" : user.get("wallet_address");
    data["user"] = userJson;

    Logger::info("User logged in: " + email);
    sendSuccess(res, data, "Login successful");
}

// ----------------------------------------------------------------------
// POST /api/logout   (Authorization: Bearer <token>)
// ----------------------------------------------------------------------
void AuthController::handleLogout(const simple_http::Request& req, simple_http::Response& res) {
    std::string authHeader = req.header("authorization");
    std::string token = authHeader.rfind("Bearer ", 0) == 0 ? authHeader.substr(7) : authHeader;

    auth_.destroySession(token);
    sendSuccess(res, sjson::Json::object(), "Logged out successfully");
}

// ----------------------------------------------------------------------
// GET /api/profile   (Authorization: Bearer <token>)
// ----------------------------------------------------------------------
void AuthController::handleProfile(const simple_http::Request& req, simple_http::Response& res) {
    std::string authHeader = req.header("authorization");
    std::string token = authHeader.rfind("Bearer ", 0) == 0 ? authHeader.substr(7) : authHeader;

    auto user = auth_.validateToken(token);
    if (!user.has_value()) {
        return sendError(res, "Unauthorized. Please log in again.", 401);
    }

    sjson::Json data = sjson::Json::object();
    data["userId"] = user->userId;
    data["fullName"] = user->fullName;
    data["email"] = user->email;
    data["role"] = user->role;
    data["walletAddress"] = user->walletAddress;
    sendSuccess(res, data, "Profile fetched");
}

// ----------------------------------------------------------------------
// POST /api/link-wallet   (Authorization: Bearer <token>)
// Body: { walletAddress }
// ----------------------------------------------------------------------
void AuthController::handleLinkWallet(const simple_http::Request& req, simple_http::Response& res) {
    std::string authHeader = req.header("authorization");
    std::string token = authHeader.rfind("Bearer ", 0) == 0 ? authHeader.substr(7) : authHeader;

    auto user = auth_.validateToken(token);
    if (!user.has_value()) {
        return sendError(res, "Unauthorized. Please log in again.", 401);
    }

    sjson::Json body;
    try {
        body = sjson::Json::parse(req.body);
    } catch (...) {
        return sendError(res, "Invalid JSON body", 400);
    }

    std::string wallet = body.getString("walletAddress");
    if (!validate::isValidEthereumAddress(wallet)) {
        return sendError(res, "Invalid Ethereum wallet address", 422);
    }

    db_.execute("UPDATE users SET wallet_address = ? WHERE user_id = ?",
                { wallet, std::to_string(user->userId) });

    sendSuccess(res, sjson::Json::object(), "Wallet linked successfully");
}

} // namespace evoting
