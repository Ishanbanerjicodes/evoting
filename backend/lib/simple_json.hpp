// ============================================================================
//  simple_json.hpp
//  A small, dependency-free JSON value type + parser/serializer.
//
//  Purpose-built for the e-voting REST API instead of vendoring the full
//  nlohmann::json (25k+ lines) — keeps the "zero setup" build lightweight
//  while still giving ergonomic JSON construction/parsing in C++.
//
//  Supported: objects, arrays, strings, numbers (double + int convenience),
//  booleans, null. Enough for REST request/response bodies.
//
//  Author: Ishan — Blockchain E-Voting System (backend)
// ============================================================================

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cmath>

namespace sjson {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() : type_(Type::Null) {}
    Json(std::nullptr_t) : type_(Type::Null) {}
    Json(bool b) : type_(Type::Bool), boolVal_(b) {}
    Json(int i) : type_(Type::Number), numVal_(i) {}
    Json(long i) : type_(Type::Number), numVal_(static_cast<double>(i)) {}
    Json(long long i) : type_(Type::Number), numVal_(static_cast<double>(i)) {}
    Json(size_t i) : type_(Type::Number), numVal_(static_cast<double>(i)) {}
    Json(double d) : type_(Type::Number), numVal_(d) {}
    Json(const char* s) : type_(Type::String), strVal_(s) {}
    Json(const std::string& s) : type_(Type::String), strVal_(s) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }

    // ---- Object access -----------------------------------------------
    Json& operator[](const std::string& key) {
        if (type_ == Type::Null) type_ = Type::Object;
        ensureKeyOrder(key);
        return objVal_[key];
    }

    bool has(const std::string& key) const {
        return type_ == Type::Object && objVal_.find(key) != objVal_.end();
    }

    // ---- Array access ---------------------------------------------------
    void push_back(const Json& v) {
        if (type_ == Type::Null) type_ = Type::Array;
        arrVal_.push_back(v);
    }

    Json& operator[](size_t idx) { return arrVal_[idx]; }
    const Json& operator[](size_t idx) const { return arrVal_[idx]; }
    size_t size() const {
        if (type_ == Type::Array) return arrVal_.size();
        if (type_ == Type::Object) return objVal_.size();
        return 0;
    }

    const std::vector<Json>& items() const { return arrVal_; }

    // ---- Value getters (with defaults, since REST inputs are messy) -----
    std::string getString(const std::string& key, const std::string& def = "") const {
        auto it = objVal_.find(key);
        if (it == objVal_.end() || it->second.type_ != Type::String) return def;
        return it->second.strVal_;
    }
    int getInt(const std::string& key, int def = 0) const {
        auto it = objVal_.find(key);
        if (it == objVal_.end() || it->second.type_ != Type::Number) return def;
        return static_cast<int>(it->second.numVal_);
    }
    double getDouble(const std::string& key, double def = 0.0) const {
        auto it = objVal_.find(key);
        if (it == objVal_.end() || it->second.type_ != Type::Number) return def;
        return it->second.numVal_;
    }
    bool getBool(const std::string& key, bool def = false) const {
        auto it = objVal_.find(key);
        if (it == objVal_.end() || it->second.type_ != Type::Bool) return def;
        return it->second.boolVal_;
    }
    Json getArray(const std::string& key) const {
        auto it = objVal_.find(key);
        if (it == objVal_.end() || it->second.type_ != Type::Array) return Json::array();
        return it->second;
    }

    std::string asString() const { return type_ == Type::String ? strVal_ : ""; }
    double asDouble() const { return type_ == Type::Number ? numVal_ : 0.0; }
    int asInt() const { return type_ == Type::Number ? static_cast<int>(numVal_) : 0; }
    bool asBool() const { return type_ == Type::Bool ? boolVal_ : false; }

    // ---- Serialize --------------------------------------------------------
    std::string dump() const {
        std::ostringstream out;
        write(out);
        return out.str();
    }

    // ---- Parse --------------------------------------------------------
    static Json parse(const std::string& text) {
        size_t pos = 0;
        skipWs(text, pos);
        Json result = parseValue(text, pos);
        return result;
    }

private:
    Type type_ = Type::Null;
    bool boolVal_ = false;
    double numVal_ = 0.0;
    std::string strVal_;
    std::vector<Json> arrVal_;
    std::map<std::string, Json> objVal_;
    std::vector<std::string> keyOrder_;

    void ensureKeyOrder(const std::string& key) {
        for (auto& k : keyOrder_) if (k == key) return;
        keyOrder_.push_back(key);
    }

    static void writeEscaped(std::ostringstream& out, const std::string& s) {
        out << '"';
        for (char c : s) {
            switch (c) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out << buf;
                    } else {
                        out << c;
                    }
            }
        }
        out << '"';
    }

    void write(std::ostringstream& out) const {
        switch (type_) {
            case Type::Null: out << "null"; break;
            case Type::Bool: out << (boolVal_ ? "true" : "false"); break;
            case Type::Number: {
                if (numVal_ == static_cast<long long>(numVal_)) {
                    out << static_cast<long long>(numVal_);
                } else {
                    out << numVal_;
                }
                break;
            }
            case Type::String: writeEscaped(out, strVal_); break;
            case Type::Array: {
                out << '[';
                for (size_t i = 0; i < arrVal_.size(); ++i) {
                    if (i) out << ',';
                    arrVal_[i].write(out);
                }
                out << ']';
                break;
            }
            case Type::Object: {
                out << '{';
                bool first = true;
                // preserve insertion order for readable API responses
                for (auto& key : keyOrder_) {
                    auto it = objVal_.find(key);
                    if (it == objVal_.end()) continue;
                    if (!first) out << ',';
                    first = false;
                    writeEscaped(out, key);
                    out << ':';
                    it->second.write(out);
                }
                out << '}';
                break;
            }
        }
    }

    static void skipWs(const std::string& s, size_t& pos) {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    }

    static Json parseValue(const std::string& s, size_t& pos) {
        skipWs(s, pos);
        if (pos >= s.size()) throw std::runtime_error("Unexpected end of JSON");

        char c = s[pos];
        if (c == '{') return parseObject(s, pos);
        if (c == '[') return parseArray(s, pos);
        if (c == '"') return Json(parseString(s, pos));
        if (c == 't' || c == 'f') return parseBool(s, pos);
        if (c == 'n') { pos += 4; return Json(nullptr); }
        return parseNumber(s, pos);
    }

    static Json parseObject(const std::string& s, size_t& pos) {
        Json obj = Json::object();
        ++pos; // {
        skipWs(s, pos);
        if (pos < s.size() && s[pos] == '}') { ++pos; return obj; }

        while (pos < s.size()) {
            skipWs(s, pos);
            std::string key = parseString(s, pos);
            skipWs(s, pos);
            if (pos < s.size() && s[pos] == ':') ++pos;
            Json val = parseValue(s, pos);
            obj[key] = val;
            skipWs(s, pos);
            if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
            if (pos < s.size() && s[pos] == '}') { ++pos; break; }
            break;
        }
        return obj;
    }

    static Json parseArray(const std::string& s, size_t& pos) {
        Json arr = Json::array();
        ++pos; // [
        skipWs(s, pos);
        if (pos < s.size() && s[pos] == ']') { ++pos; return arr; }

        while (pos < s.size()) {
            Json val = parseValue(s, pos);
            arr.push_back(val);
            skipWs(s, pos);
            if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
            if (pos < s.size() && s[pos] == ']') { ++pos; break; }
            break;
        }
        return arr;
    }

    static std::string parseString(const std::string& s, size_t& pos) {
        skipWs(s, pos);
        if (pos >= s.size() || s[pos] != '"') throw std::runtime_error("Expected string");
        ++pos;
        std::string out;
        while (pos < s.size() && s[pos] != '"') {
            char c = s[pos];
            if (c == '\\' && pos + 1 < s.size()) {
                char next = s[pos + 1];
                switch (next) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (pos + 5 < s.size()) {
                            std::string hex = s.substr(pos + 2, 4);
                            int codepoint = std::stoi(hex, nullptr, 16);
                            if (codepoint < 0x80) {
                                out += static_cast<char>(codepoint);
                            } else {
                                // Minimal UTF-8 encode for BMP codepoints.
                                out += static_cast<char>(0xC0 | (codepoint >> 6));
                                out += static_cast<char>(0x80 | (codepoint & 0x3F));
                            }
                            pos += 4;
                        }
                        break;
                    }
                    default: out += next;
                }
                pos += 2;
            } else {
                out += c;
                ++pos;
            }
        }
        ++pos; // closing quote
        return out;
    }

    static Json parseBool(const std::string& s, size_t& pos) {
        if (s.compare(pos, 4, "true") == 0) { pos += 4; return Json(true); }
        if (s.compare(pos, 5, "false") == 0) { pos += 5; return Json(false); }
        throw std::runtime_error("Invalid boolean literal");
    }

    static Json parseNumber(const std::string& s, size_t& pos) {
        size_t start = pos;
        if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) ++pos;
        while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) ||
                                   s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E' ||
                                   s[pos] == '+' || s[pos] == '-')) {
            ++pos;
        }
        std::string numStr = s.substr(start, pos - start);
        if (numStr.empty()) throw std::runtime_error("Invalid number literal");
        return Json(std::stod(numStr));
    }
};

} // namespace sjson
