// ============================================================================
//  validators.hpp
//  Reusable input-validation helpers shared across controllers so
//  validation rules live in exactly one place (DRY / clean architecture).
// ============================================================================

#pragma once

#include <string>
#include <regex>

namespace evoting {
namespace validate {

inline bool isValidEmail(const std::string& email) {
    static const std::regex pattern(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)");
    return std::regex_match(email, pattern);
}

// Requires: at least 8 chars, 1 uppercase, 1 lowercase, 1 digit, 1 symbol.
inline bool isStrongPassword(const std::string& password) {
    if (password.size() < 8) return false;
    bool hasUpper = false, hasLower = false, hasDigit = false, hasSymbol = false;
    for (char c : password) {
        if (std::isupper(static_cast<unsigned char>(c))) hasUpper = true;
        else if (std::islower(static_cast<unsigned char>(c))) hasLower = true;
        else if (std::isdigit(static_cast<unsigned char>(c))) hasDigit = true;
        else hasSymbol = true;
    }
    return hasUpper && hasLower && hasDigit && hasSymbol;
}

inline bool isValidEthereumAddress(const std::string& address) {
    static const std::regex pattern(R"(^0x[a-fA-F0-9]{40}$)");
    return std::regex_match(address, pattern);
}

inline bool isNonEmpty(const std::string& s) {
    return !s.empty();
}

} // namespace validate
} // namespace evoting
