//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: ServerAuth.hpp
// Purpose: Server-side bearer authentication interfaces and helpers for HTTP server
//==========================================================================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>

namespace mcp::auth {

//==========================================================================================================
// TokenInfo
// Purpose: Information extracted from a bearer token (scopes, expiration, and optional extra fields).
//==========================================================================================================
struct TokenInfo {
    std::vector<std::string> scopes;
    std::chrono::system_clock::time_point expiration;
    std::unordered_map<std::string, std::string> extra;
};

//==========================================================================================================
// ITokenVerifier
// Purpose: Interface to validate a bearer token and populate TokenInfo when valid.
// Returns: true on success (TokenInfo populated); false on failure (set errorMessage).
//==========================================================================================================
class ITokenVerifier {
public:
    virtual ~ITokenVerifier() = default;
    virtual bool Verify(const std::string& token, TokenInfo& outInfo, std::string& errorMessage) = 0;
};

//==========================================================================================================
// RequireBearerTokenOptions
// Purpose: Options controlling server-side bearer enforcement and resource metadata advertisement.
// Fields:
//   resourceMetadataUrl: URL to include in WWW-Authenticate header when returning 401/403.
//   requiredScopes: All listed scopes must be present in the token.
//==========================================================================================================
struct RequireBearerTokenOptions {
    std::string resourceMetadataUrl;
    std::vector<std::string> requiredScopes;
};

//==========================================================================================================
// BearerCheckResult
// Purpose: Result of checking a bearer Authorization header.
// Fields:
//   ok: True if authorization passed.
//   httpStatus: HTTP status to use on failure (401 or 403).
//   errorMessage: Error message to include in payload.
//   includeWWWAuthenticate: Whether the caller should include a WWW-Authenticate header.
//==========================================================================================================
struct BearerCheckResult {
    bool ok{false};
    int httpStatus{401};
    std::string errorMessage;
    bool includeWWWAuthenticate{false};
};

//==========================================================================================================
// CheckBearerAuth
// Purpose: Validate Authorization header using the supplied verifier and options.
// Args:
//   authHeader: Value of the Authorization header (may be empty).
//   verifier: Token verifier (must not be null if enforcement is enabled).
//   opts: Enforcement options (required scopes and resource metadata URL).
//   outInfo: Populated on success with token information.
// Returns:
//   BearerCheckResult indicating success or failure details.
//==========================================================================================================
BearerCheckResult CheckBearerAuth(
    const std::string& authHeader,
    ITokenVerifier& verifier,
    const RequireBearerTokenOptions& opts,
    TokenInfo& outInfo);

//==========================================================================================================
// Per-request TokenInfo context accessors
// Purpose: Provide access to the current request's TokenInfo for server handlers.
//==========================================================================================================
const TokenInfo* CurrentTokenInfo();

// RAII helper: sets the current TokenInfo for the lifetime of this object, then clears.
class TokenInfoScope {
public:
    explicit TokenInfoScope(const TokenInfo* info);
    ~TokenInfoScope();
private:
    const TokenInfo* prev{nullptr};
};

} // namespace mcp::auth
