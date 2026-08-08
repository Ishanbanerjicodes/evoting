// ============================================================================
//  crypto.hpp
//  Password hashing, salting, and secure random token generation using
//  OpenSSL's SHA-256 implementation.
// ============================================================================

#pragma once
#include <string>

namespace evoting {
namespace crypto {

// Returns a lowercase hex SHA-256 digest of the given input string.
std::string sha256(const std::string& input);

// Generates a cryptographically random hex string of `byteLength` random
// bytes (so the returned string is 2 * byteLength hex characters long).
// Used for password salts and session tokens.
std::string randomHex(size_t byteLength);

// Hashes a password with a salt using the scheme SHA256(password + salt),
// matching exactly what database/schema.sql's seed data uses.
std::string hashPassword(const std::string& password, const std::string& salt);

// Generates a random 6-digit numeric OTP code as a string, e.g. "042917".
std::string generateOtp();

} // namespace crypto
} // namespace evoting
