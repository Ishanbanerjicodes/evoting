// ============================================================================
//  response_helper.hpp
//  Small helpers to keep every controller returning a consistent JSON
//  envelope shape: { "success": bool, "message": string, "data": ... }
// ============================================================================

#pragma once

#include "../lib/simple_http.hpp"
#include "../lib/simple_json.hpp"

namespace evoting {

inline void sendSuccess(simple_http::Response& res, const sjson::Json& data,
                         const std::string& message = "OK", int status = 200) {
    sjson::Json body = sjson::Json::object();
    body["success"] = true;
    body["message"] = message;
    body["data"] = data;
    res.status = status;
    res.set_json(body.dump());
}

inline void sendError(simple_http::Response& res, const std::string& message, int status = 400) {
    sjson::Json body = sjson::Json::object();
    body["success"] = false;
    body["message"] = message;
    res.status = status;
    res.set_json(body.dump());
}

} // namespace evoting
