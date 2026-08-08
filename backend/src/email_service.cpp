// ============================================================================
//  email_service.cpp
//  Implements a minimal SMTPS (implicit TLS) client using OpenSSL's BIO
//  socket API. This talks directly to the mail server with plain SMTP
//  commands (EHLO / AUTH LOGIN / MAIL FROM / RCPT TO / DATA) over a TLS
//  connection — no extra third-party mail library required, since
//  OpenSSL is already linked into this project for password hashing.
// ============================================================================

#include "email_service.hpp"
#include "logger.hpp"

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace evoting {

// ---------------------------------------------------------------------
// Base64 encoding — needed for AUTH LOGIN (SMTP auth sends the username
// and password as base64, not because it's secure on its own, but
// because the whole exchange already happens inside the TLS tunnel).
// ---------------------------------------------------------------------
static std::string base64Encode(const std::string& input) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(table[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(table[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static std::string getEnvOrDefault(const char* name, const std::string& def) {
    const char* val = std::getenv(name);
    return (val && val[0] != '\0') ? std::string(val) : def;
}

SmtpConfig EmailService::loadFromEnvironment(const std::string& fallbackHost,
                                              int fallbackPort,
                                              const std::string& fallbackFromName) {
    SmtpConfig cfg;
    cfg.host = getEnvOrDefault("SMTP_HOST", fallbackHost);

    std::string portStr = getEnvOrDefault("SMTP_PORT", std::to_string(fallbackPort));
    try { cfg.port = std::stoi(portStr); } catch (...) { cfg.port = fallbackPort; }

    // Accept either SMTP_USERNAME or SMTP_EMAIL, whichever the person set.
    cfg.username = getEnvOrDefault("SMTP_USERNAME", getEnvOrDefault("SMTP_EMAIL", ""));

    // Accept either SMTP_PASSWORD or SMTP_APP_PASSWORD.
    cfg.password = getEnvOrDefault("SMTP_PASSWORD", getEnvOrDefault("SMTP_APP_PASSWORD", ""));

    cfg.fromName = getEnvOrDefault("SMTP_FROM_NAME", fallbackFromName);

    cfg.configured = !cfg.host.empty() && !cfg.username.empty() && !cfg.password.empty();
    return cfg;
}

// ---------------------------------------------------------------------
// Small helper: read one (or more, if the server sends a multi-line
// reply like "250-...") SMTP response from the TLS BIO, returning the
// numeric status code found at the start of the last line.
// ---------------------------------------------------------------------
static int readSmtpResponse(BIO* bio, std::string* fullResponseOut = nullptr) {
    std::string all;
    char buf[512];

    while (true) {
        int n = BIO_read(bio, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        all += buf;
        // Stop once we have a line ending in "\r\n" whose 4th char is a
        // space (i.e. "250 " not "250-"), which marks the final line of
        // a (possibly multi-line) SMTP reply.
        size_t lastLineStart = all.rfind('\n', all.size() >= 2 ? all.size() - 2 : std::string::npos);
        lastLineStart = (lastLineStart == std::string::npos) ? 0 : lastLineStart + 1;
        if (all.size() - lastLineStart >= 4 && all[lastLineStart + 3] == ' ') break;
        if (all.size() > 8192) break; // safety guard
    }

    if (fullResponseOut) *fullResponseOut = all;
    if (all.size() < 3) return -1;
    try { return std::stoi(all.substr(0, 3)); } catch (...) { return -1; }
}

static bool sendSmtpCommand(BIO* bio, const std::string& command) {
    std::string withCrlf = command + "\r\n";
    return BIO_write(bio, withCrlf.c_str(), (int)withCrlf.size()) > 0;
}

bool EmailService::sendMail(const std::string& toAddress,
                             const std::string& subject,
                             const std::string& bodyText) const {
    if (!config_.configured) {
        Logger::warn("EmailService::sendMail called but SMTP is not configured "
                      "(missing SMTP_HOST/SMTP_USERNAME/SMTP_PASSWORD env vars). "
                      "Email was NOT sent to " + toAddress);
        return false;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        Logger::error("EmailService: failed to create SSL context");
        return false;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr); // simplified for a college-project SMTP client

    BIO* bio = BIO_new_ssl_connect(ctx);
    if (!bio) {
        Logger::error("EmailService: failed to create SSL BIO");
        SSL_CTX_free(ctx);
        return false;
    }

    std::string hostPort = config_.host + ":" + std::to_string(config_.port);
    BIO_set_conn_hostname(bio, hostPort.c_str());

    SSL* ssl = nullptr;
    BIO_get_ssl(bio, &ssl);
    if (ssl) SSL_set_tlsext_host_name(ssl, config_.host.c_str());

    if (BIO_do_connect(bio) <= 0) {
        Logger::error("EmailService: could not connect to SMTP server " + hostPort);
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return false;
    }

    auto fail = [&](const std::string& stage, int code, const std::string& raw) {
        Logger::error("EmailService: SMTP failed at '" + stage + "' (code " +
                       std::to_string(code) + "): " + raw);
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return false;
    };

    std::string resp;
    int code;

    // ---- SMTP handshake -------------------------------------------------
    code = readSmtpResponse(bio, &resp); // server greeting
    if (code != 220) return fail("connect", code, resp);

    sendSmtpCommand(bio, "EHLO evoting-backend");
    code = readSmtpResponse(bio, &resp);
    if (code != 250) return fail("EHLO", code, resp);

    sendSmtpCommand(bio, "AUTH LOGIN");
    code = readSmtpResponse(bio, &resp);
    if (code != 334) return fail("AUTH LOGIN", code, resp);

    sendSmtpCommand(bio, base64Encode(config_.username));
    code = readSmtpResponse(bio, &resp);
    if (code != 334) return fail("AUTH username", code, resp);

    sendSmtpCommand(bio, base64Encode(config_.password));
    code = readSmtpResponse(bio, &resp);
    if (code != 235) return fail("AUTH password", code, resp);

    sendSmtpCommand(bio, "MAIL FROM:<" + config_.username + ">");
    code = readSmtpResponse(bio, &resp);
    if (code != 250) return fail("MAIL FROM", code, resp);

    sendSmtpCommand(bio, "RCPT TO:<" + toAddress + ">");
    code = readSmtpResponse(bio, &resp);
    if (code != 250 && code != 251) return fail("RCPT TO", code, resp);

    sendSmtpCommand(bio, "DATA");
    code = readSmtpResponse(bio, &resp);
    if (code != 354) return fail("DATA", code, resp);

    std::ostringstream message;
    message << "From: " << config_.fromName << " <" << config_.username << ">\r\n"
            << "To: <" << toAddress << ">\r\n"
            << "Subject: " << subject << "\r\n"
            << "MIME-Version: 1.0\r\n"
            << "Content-Type: text/plain; charset=UTF-8\r\n"
            << "\r\n"
            << bodyText << "\r\n"
            << ".";

    sendSmtpCommand(bio, message.str());
    code = readSmtpResponse(bio, &resp);
    if (code != 250) return fail("message body", code, resp);

    sendSmtpCommand(bio, "QUIT");
    BIO_free_all(bio);
    SSL_CTX_free(ctx);

    Logger::info("OTP email sent to " + toAddress);
    return true;
}

} // namespace evoting
