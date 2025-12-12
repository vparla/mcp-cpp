//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: WwwAuthenticate.hpp
// Purpose: Parser for HTTP WWW-Authenticate Bearer challenges (RFC 6750/RFC 9728 parameters)
//==========================================================================================================

#pragma once

#include <string>
#include <unordered_map>

namespace mcp::auth {

//==========================================================================================================
// WwwAuthChallenge
// Purpose: Parsed representation of a single WWW-Authenticate challenge line.
//==========================================================================================================
struct WwwAuthChallenge {
    std::string scheme;                                          // e.g., "Bearer" (canonicalized as lower-case)
    std::unordered_map<std::string, std::string> params;         // key -> value (unquoted, unescaped)
};

//==========================================================================================================
// parseWwwAuthenticate
// Purpose: Parse a single WWW-Authenticate header value. Supports Bearer scheme with comma-separated
//          key=value parameters (values may be quoted). Returns true on successful parse of Bearer scheme.
// Notes:
//   - Returns false for unsupported schemes (e.g., Basic), or malformed inputs.
//   - Keys are case-sensitive as transmitted; scheme is returned lower-case.
//==========================================================================================================
bool parseWwwAuthenticate(const std::string& header, WwwAuthChallenge& out);

} // namespace mcp::auth
