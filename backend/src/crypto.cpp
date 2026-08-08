// ============================================================================
//  crypto.cpp
//  Implements SHA-256 hashing (via OpenSSL EVP API) and secure random
//  generation using the Windows Cryptography API (bcrypt) as the entropy
//  source, so nothing here depends on /dev/urandom or any POSIX facility.
// ============================================================================

#include "crypto.hpp"

#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace evoting {
namespace crypto {

std::string sha256(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    EVP_DigestFinal_ex(ctx, digest, &digestLen);
    EVP_MD_CTX_free(ctx);

    std::ostringstream hex;
    for (unsigned int i = 0; i < digestLen; ++i) {
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return hex.str();
}

std::string randomHex(size_t byteLength) {
    std::vector<unsigned char> buffer(byteLength);

    // BCryptGenRandom is the Windows-native CSPRNG — the correct Windows
    // equivalent of reading from /dev/urandom on Linux, so this stays
    // fully Windows-native with no POSIX fallback needed.
    NTSTATUS status = BCryptGenRandom(
        nullptr,
        buffer.data(),
        static_cast<ULONG>(buffer.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );

    if (status != 0 /* STATUS_SUCCESS */) {
        // Extremely unlikely fallback: seed std::random_device instead,
        // so token generation never hard-crashes the server.
        std::random_device rd;
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& b : buffer) b = static_cast<unsigned char>(dist(rd));
    }

    std::ostringstream hex;
    for (unsigned char b : buffer) {
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return hex.str();
}

std::string hashPassword(const std::string& password, const std::string& salt) {
    return sha256(password + salt);
}

std::string generateOtp() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 999999);
    int code = dist(gen);

    std::ostringstream out;
    out << std::setw(6) << std::setfill('0') << code;
    return out.str();
}

} // namespace crypto
} // namespace evoting
