// ============================================================================
//  simple_http.hpp
//  A minimal, single-header, dependency-free HTTP/1.1 server for Windows.
//
//  Why a custom header instead of pulling in cpp-httplib / Crow?
//  This project's #1 requirement is "runs on Windows with almost zero
//  setup" — no package manager, no vcpkg, no downloading a 20,000-line
//  vendor header. This file implements exactly the subset of HTTP/1.1
//  server behaviour the e-voting REST API needs (routing, JSON bodies,
//  query strings, CORS, static-ish text responses) directly on top of
//  Winsock2, with zero third-party dependencies and zero pthread usage.
//
//  Design notes:
//   - Blocking I/O, one thread per connection (std::thread), which is
//     more than adequate for a college/portfolio project's request volume.
//   - Windows-only: uses Winsock2 (ws2_32). No <pthread.h>, no <unistd.h>,
//     no POSIX sockets. Nothing here will compile Linux-style headers.
//   - Header-only: #include this file, no separate .cpp/.lib to build.
//
//  Author: Ishan — Blockchain E-Voting System (backend)
// ============================================================================

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <iostream>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "ws2_32.lib")

namespace simple_http {

// ---------------------------------------------------------------------------
// Request / Response models
// ---------------------------------------------------------------------------
struct Request {
    std::string method;                                   // GET, POST, PUT, DELETE...
    std::string path;                                      // "/api/login" (no query string)
    std::unordered_map<std::string, std::string> query;    // parsed "?a=b&c=d"
    std::unordered_map<std::string, std::string> headers;  // lower-cased keys
    std::unordered_map<std::string, std::string> params;   // path params, e.g. ":id"
    std::string body;

    std::string header(const std::string& key, const std::string& def = "") const {
        std::string lower = key;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        auto it = headers.find(lower);
        return it != headers.end() ? it->second : def;
    }
};

struct Response {
    int status = 200;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    void set_json(const std::string& json_text) {
        headers["Content-Type"] = "application/json";
        body = json_text;
    }
};

using Handler = std::function<void(const Request&, Response&)>;

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------
class Server {
public:
    Server() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~Server() {
        WSACleanup();
    }

    void Get(const std::string& path, Handler h)    { addRoute("GET", path, h); }
    void Post(const std::string& path, Handler h)   { addRoute("POST", path, h); }
    void Put(const std::string& path, Handler h)    { addRoute("PUT", path, h); }
    void Del(const std::string& path, Handler h)    { addRoute("DELETE", path, h); }
    void Options(const std::string& path, Handler h){ addRoute("OPTIONS", path, h); }

    // Called for every response before it's sent — used to inject CORS
    // headers globally so the frontend (served from file:// or a static
    // server on a different origin) can call the API without extra setup.
    void set_default_headers(std::unordered_map<std::string, std::string> hdrs) {
        defaultHeaders_ = std::move(hdrs);
    }

    bool listen(const std::string& host, int port) {
        SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock == INVALID_SOCKET) {
            std::cerr << "[simple_http] socket() failed\n";
            return false;
        }

        int opt = 1;
        setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        if (host == "0.0.0.0") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        }

        if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            std::cerr << "[simple_http] bind() failed on " << host << ":" << port
                      << " — is the port already in use?\n";
            closesocket(listenSock);
            return false;
        }

        if (::listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "[simple_http] listen() failed\n";
            closesocket(listenSock);
            return false;
        }

        std::cout << "[simple_http] Listening on http://" << host << ":" << port << "\n";

        running_ = true;
        while (running_) {
            sockaddr_in clientAddr{};
            int clientAddrLen = sizeof(clientAddr);
            SOCKET clientSock = accept(listenSock, (sockaddr*)&clientAddr, &clientAddrLen);
            if (clientSock == INVALID_SOCKET) {
                if (!running_) break;
                continue;
            }
            // One thread per connection — simple and sufficient for a
            // portfolio-scale e-voting demo (dozens/hundreds of voters,
            // not internet-scale traffic).
            std::thread(&Server::handleClient, this, clientSock).detach();
        }

        closesocket(listenSock);
        return true;
    }

    void stop() { running_ = false; }

private:
    struct Route {
        std::string method;
        std::string pathPattern;   // may contain ":param" segments
        Handler handler;
    };

    std::vector<Route> routes_;
    std::unordered_map<std::string, std::string> defaultHeaders_;
    bool running_ = false;

    void addRoute(const std::string& method, const std::string& path, Handler h) {
        routes_.push_back({method, path, h});
    }

    static std::vector<std::string> splitSegments(const std::string& path) {
        std::vector<std::string> segs;
        std::stringstream ss(path);
        std::string seg;
        while (std::getline(ss, seg, '/')) {
            if (!seg.empty()) segs.push_back(seg);
        }
        return segs;
    }

    // Matches request path against a route pattern that may contain
    // ":name" segments, e.g. "/api/candidates/:id" matches "/api/candidates/7"
    // with params["id"] = "7".
    static bool matchRoute(const std::string& pattern, const std::string& path,
                            std::unordered_map<std::string, std::string>& params) {
        auto patSegs = splitSegments(pattern);
        auto pathSegs = splitSegments(path);
        if (patSegs.size() != pathSegs.size()) return false;

        for (size_t i = 0; i < patSegs.size(); ++i) {
            if (!patSegs[i].empty() && patSegs[i][0] == ':') {
                params[patSegs[i].substr(1)] = pathSegs[i];
            } else if (patSegs[i] != pathSegs[i]) {
                return false;
            }
        }
        return true;
    }

    static std::string urlDecode(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '%' && i + 2 < in.size()) {
                int val = std::stoi(in.substr(i + 1, 2), nullptr, 16);
                out += static_cast<char>(val);
                i += 2;
            } else if (in[i] == '+') {
                out += ' ';
            } else {
                out += in[i];
            }
        }
        return out;
    }

    static void parseQuery(const std::string& qs, std::unordered_map<std::string, std::string>& out) {
        std::stringstream ss(qs);
        std::string pair;
        while (std::getline(ss, pair, '&')) {
            auto eq = pair.find('=');
            if (eq != std::string::npos) {
                out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
            } else if (!pair.empty()) {
                out[urlDecode(pair)] = "";
            }
        }
    }

    static std::string statusText(int code) {
        switch (code) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 409: return "Conflict";
            case 422: return "Unprocessable Entity";
            case 500: return "Internal Server Error";
            default:  return "Unknown";
        }
    }

    void handleClient(SOCKET clientSock) {
        std::string raw;
        char buf[8192];

        // Read headers first (until \r\n\r\n), then the body based on
        // Content-Length, so this works correctly for JSON POST bodies.
        int received;
        size_t headerEnd = std::string::npos;
        while ((received = recv(clientSock, buf, sizeof(buf), 0)) > 0) {
            raw.append(buf, received);
            headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) break;
            if (raw.size() > 1 * 1024 * 1024) break; // guard against runaway headers
        }

        if (headerEnd == std::string::npos) {
            closesocket(clientSock);
            return;
        }

        std::string headerBlock = raw.substr(0, headerEnd);
        std::string bodySoFar = raw.substr(headerEnd + 4);

        std::istringstream headerStream(headerBlock);
        std::string requestLine;
        std::getline(headerStream, requestLine);
        if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

        std::istringstream rl(requestLine);
        std::string method, fullPath, httpVersion;
        rl >> method >> fullPath >> httpVersion;

        Request req;
        req.method = method;

        auto qpos = fullPath.find('?');
        if (qpos != std::string::npos) {
            req.path = fullPath.substr(0, qpos);
            parseQuery(fullPath.substr(qpos + 1), req.query);
        } else {
            req.path = fullPath;
        }
        req.path = urlDecode(req.path);

        std::string line;
        while (std::getline(headerStream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                // trim leading space
                size_t start = val.find_first_not_of(' ');
                if (start != std::string::npos) val = val.substr(start);
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                req.headers[key] = val;
            }
        }

        size_t contentLength = 0;
        auto clIt = req.headers.find("content-length");
        if (clIt != req.headers.end()) {
            contentLength = static_cast<size_t>(std::stoul(clIt->second));
        }

        // Keep reading until we have the full declared body.
        while (bodySoFar.size() < contentLength) {
            received = recv(clientSock, buf, sizeof(buf), 0);
            if (received <= 0) break;
            bodySoFar.append(buf, received);
        }
        req.body = bodySoFar.substr(0, contentLength);

        Response res;

        bool matched = false;
        bool pathMatchedAnyMethod = false;
        for (auto& route : routes_) {
            std::unordered_map<std::string, std::string> params;
            if (matchRoute(route.pathPattern, req.path, params)) {
                pathMatchedAnyMethod = true;
                if (route.method == req.method) {
                    req.params = params;
                    try {
                        route.handler(req, res);
                    } catch (const std::exception& e) {
                        res.status = 500;
                        res.set_json(std::string("{\"success\":false,\"message\":\"Internal server error: ") + e.what() + "\"}");
                    } catch (...) {
                        res.status = 500;
                        res.set_json("{\"success\":false,\"message\":\"Unknown internal server error\"}");
                    }
                    matched = true;
                    break;
                }
            }
        }

        if (!matched) {
            if (req.method == "OPTIONS") {
                // Preflight — respond 204 with CORS headers, handled below.
                res.status = 204;
            } else if (pathMatchedAnyMethod) {
                res.status = 405;
                res.set_json("{\"success\":false,\"message\":\"Method not allowed\"}");
            } else {
                res.status = 404;
                res.set_json("{\"success\":false,\"message\":\"Route not found\"}");
            }
        }

        // Apply default (CORS) headers if not already set by the handler.
        for (auto& kv : defaultHeaders_) {
            if (res.headers.find(kv.first) == res.headers.end()) {
                res.headers[kv.first] = kv.second;
            }
        }
        if (res.headers.find("Content-Type") == res.headers.end()) {
            res.headers["Content-Type"] = "text/plain";
        }

        std::ostringstream out;
        out << "HTTP/1.1 " << res.status << " " << statusText(res.status) << "\r\n";
        for (auto& kv : res.headers) {
            out << kv.first << ": " << kv.second << "\r\n";
        }
        out << "Content-Length: " << res.body.size() << "\r\n";
        out << "Connection: close\r\n\r\n";
        out << res.body;

        std::string outStr = out.str();
        send(clientSock, outStr.c_str(), (int)outStr.size(), 0);

        shutdown(clientSock, SD_SEND);
        closesocket(clientSock);
    }
};

} // namespace simple_http
