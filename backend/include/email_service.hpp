// ============================================================================
//  email_service.hpp
//  Minimal SMTP client used ONLY to send OTP verification emails.
//
//  This is intentionally small and self-contained — it does not touch or
//  depend on anything else in the backend (no changes needed to Database,
//  AuthService, or any other controller). It speaks plain SMTP over a
//  TLS socket (implicit TLS, i.e. SMTPS — the standard on port 465 for
//  Gmail and most providers), using OpenSSL directly, since that's
//  already a project dependency (crypto.cpp) and keeps this free of any
//  extra third-party library.
//
//  Credentials are NEVER hardcoded: they are read from environment
//  variables at startup (see loadFromEnvironment()). config.json only
//  carries non-secret connection settings (host/port/from-name) as a
//  convenience default that env vars override.
// ============================================================================

#pragma once

#include <string>

namespace evoting {

struct SmtpConfig {
    std::string host;          // e.g. "smtp.gmail.com"
    int port = 465;            // 465 = implicit TLS (SMTPS), used here
    std::string username;      // full sender email address
    std::string password;      // SMTP app password (never a real account password)
    std::string fromName = "CivicChain E-Voting";
    bool configured = false;   // true only if host/username/password are all present
};

class EmailService {
public:
    // Reads SMTP_HOST, SMTP_PORT, SMTP_USERNAME (or SMTP_EMAIL), and
    // SMTP_PASSWORD (or SMTP_APP_PASSWORD) from the process environment.
    // `fallbackHost`/`fallbackPort`/`fallbackFromName` come from
    // config.json's optional "smtp" block and are used only for the
    // non-secret fields if the matching env var isn't set.
    static SmtpConfig loadFromEnvironment(const std::string& fallbackHost,
                                           int fallbackPort,
                                           const std::string& fallbackFromName);

    explicit EmailService(SmtpConfig config) : config_(std::move(config)) {}

    bool isConfigured() const { return config_.configured; }

    // Sends a plain-text email. Returns true on success. On any failure
    // (not configured, connection error, auth failure, etc.) returns
    // false and logs the reason via Logger — callers should treat a
    // false return as "could not email the OTP" and respond accordingly,
    // but this never throws, so it can't crash the request handler.
    bool sendMail(const std::string& toAddress,
                  const std::string& subject,
                  const std::string& bodyText) const;

private:
    SmtpConfig config_;
};

} // namespace evoting
