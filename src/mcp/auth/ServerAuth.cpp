//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: src/mcp/auth/ServerAuth.cpp
// Purpose: Server-side bearer authentication helpers implementation
//==========================================================================================================

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <vector>

#include "mcp/auth/ServerAuth.hpp"

namespace mcp::auth {

namespace {
    // Thread-local storage for per-request TokenInfo.
    thread_local const TokenInfo* gCurrentTokenInfo = nullptr;

    static bool icaseEqual(char a, char b) {
        return (std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)));
    }

    static bool startsWithBearer(const std::string& s) {
        const std::string pfx = "Bearer ";
        if (s.size() < pfx.size()) {
            return false;
        }
        for (size_t i = 0; i < pfx.size(); ++i) {
            if (!icaseEqual(s[i], pfx[i])) {
                return false;
            }
        }
        return true;
    }

    static bool containsAllScopes(const std::vector<std::string>& have, const std::vector<std::string>& need) {
        for (size_t i = 0; i < need.size(); ++i) {
            const std::string& s = need[i];
            bool found = false;
            for (size_t j = 0; j < have.size(); ++j) {
                if (have[j] == s) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }
}

const TokenInfo* CurrentTokenInfo() {
    return gCurrentTokenInfo;
}

TokenInfoScope::TokenInfoScope(const TokenInfo* info) : prev(gCurrentTokenInfo) {
    gCurrentTokenInfo = info;
}

TokenInfoScope::~TokenInfoScope() {
    gCurrentTokenInfo = prev;
}

BearerCheckResult CheckBearerAuth(
    const std::string& authHeader,
    ITokenVerifier& verifier,
    const RequireBearerTokenOptions& opts,
    TokenInfo& outInfo) {

    BearerCheckResult r;

    // Validate header shape
    if (authHeader.empty()) {
        r.ok = false;
        r.httpStatus = 401;
        r.errorMessage = std::string("no bearer token");
        r.includeWWWAuthenticate = true;
        return r;
    }
    if (!startsWithBearer(authHeader)) {
        r.ok = false;
        r.httpStatus = 401;
        r.errorMessage = std::string("no bearer token");
        r.includeWWWAuthenticate = true;
        return r;
    }
    std::string token = authHeader.substr(7);
    // Trim leading spaces on token
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())) != 0) {
        token.erase(token.begin());
    }
    if (token.empty()) {
        r.ok = false;
        r.httpStatus = 401;
        r.errorMessage = std::string("no bearer token");
        r.includeWWWAuthenticate = true;
        return r;
    }

    // Verify token via callback
    std::string err;
    TokenInfo info;
    if (!verifier.Verify(token, info, err)) {
        r.ok = false;
        r.httpStatus = 401;
        r.errorMessage = err.empty() ? std::string("invalid token") : err;
        r.includeWWWAuthenticate = true;
        return r;
    }

    // Check scopes
    if (!opts.requiredScopes.empty()) {
        if (!containsAllScopes(info.scopes, opts.requiredScopes)) {
            r.ok = false;
            r.httpStatus = 403;
            r.errorMessage = std::string("insufficient scope");
            r.includeWWWAuthenticate = true;
            return r;
        }
    }

    // Check expiration
    if (info.expiration.time_since_epoch().count() == 0) {
        r.ok = false;
        r.httpStatus = 401;
        r.errorMessage = std::string("token missing expiration");
        r.includeWWWAuthenticate = true;
        return r;
    }
    if (info.expiration <= std::chrono::system_clock::now()) {
        r.ok = false;
        r.httpStatus = 401;
        r.errorMessage = std::string("token expired");
        r.includeWWWAuthenticate = true;
        return r;
    }

    // Success
    outInfo = std::move(info);
    r.ok = true;
    r.httpStatus = 200;
    r.includeWWWAuthenticate = false;
    return r;
}

} // namespace mcp::auth
