// ============================================================================
//  api.js
//  Thin wrapper around fetch() for talking to the C++ backend REST API.
//  Centralizes the base URL, auth header injection, and error handling so
//  every page's JS stays focused on UI logic instead of repeating fetch
//  boilerplate.
// ============================================================================

const API_BASE_URL = "http://127.0.0.1:8080/api";

const Api = (() => {
    function getToken() {
        return localStorage.getItem("evoting_token") || "";
    }

    function setToken(token) {
        if (token) localStorage.setItem("evoting_token", token);
    }

    function clearToken() {
        localStorage.removeItem("evoting_token");
        localStorage.removeItem("evoting_user");
    }

    function getUser() {
        const raw = localStorage.getItem("evoting_user");
        return raw ? JSON.parse(raw) : null;
    }

    function setUser(user) {
        localStorage.setItem("evoting_user", JSON.stringify(user));
    }

    async function request(method, path, body) {
        const headers = { "Content-Type": "application/json" };
        const token = getToken();
        if (token) headers["Authorization"] = "Bearer " + token;

        let response;
        try {
            response = await fetch(API_BASE_URL + path, {
                method,
                headers,
                body: body !== undefined ? JSON.stringify(body) : undefined
            });
        } catch (networkErr) {
            // Logged so it's visible in DevTools even though the UI only
            // shows the friendlier message below.
            console.error("[Api] Network error calling", method, path, networkErr);
            throw new ApiError(
                "Could not reach the backend server. Make sure evoting_server.exe is running " +
                "on http://127.0.0.1:8080 (see README for setup steps).",
                0
            );
        }

        let payload;
        try {
            payload = await response.json();
        } catch (parseErr) {
            throw new ApiError("Unexpected response from server", response.status);
        }

        if (!response.ok || payload.success === false) {
            throw new ApiError(payload.message || "Request failed", response.status, payload);
        }

        return payload;
    }

    class ApiError extends Error {
        constructor(message, status, payload) {
            super(message);
            this.status = status;
            this.payload = payload;
        }
    }

    return {
        get: (path) => request("GET", path),
        post: (path, body) => request("POST", path, body),
        put: (path, body) => request("PUT", path, body),
        del: (path) => request("DELETE", path),
        getToken, setToken, clearToken, getUser, setUser,
        ApiError
    };
})();
