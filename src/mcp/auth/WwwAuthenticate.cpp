//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: WwwAuthenticate.cpp
// Purpose: Parser for HTTP WWW-Authenticate Bearer challenges (RFC 6750/RFC 9728 parameters)
//==========================================================================================================

#include "mcp/auth/WwwAuthenticate.hpp"

#include <cctype>

namespace mcp::auth {

static std::string toLower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    }
    return s;
}

static void skipSpaces(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
}

static bool parseToken(const std::string& s, size_t& i, std::string& out) {
    out.clear();
    size_t start = i;
    while (i < s.size()) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch == ' ' || ch == '\t' || ch == '=' || ch == ',' || ch == '"') {
            break;
        }
        ++i;
    }
    if (i == start) {
        return false;
    }
    out = s.substr(start, i - start);
    return true;
}

static bool parseQuotedString(const std::string& s, size_t& i, std::string& out) {
    out.clear();
    if (i >= s.size() || s[i] != '"') {
        return false;
    }
    ++i; // consume opening quote
    while (i < s.size()) {
        char ch = s[i];
        if (ch == '\\') {
            if ((i + 1) < s.size()) {
                out.push_back(s[i + 1]);
                i += 2;
                continue;
            } else {
                return false;
            }
        }
        if (ch == '"') {
            ++i; // closing quote
            return true;
        }
        out.push_back(ch);
        ++i;
    }
    return false; // unterminated
}

bool parseWwwAuthenticate(const std::string& header, WwwAuthChallenge& out) {
    out.scheme.clear();
    out.params.clear();

    size_t i = 0;
    skipSpaces(header, i);

    // Parse scheme
    std::string scheme;
    if (!parseToken(header, i, scheme)) {
        return false;
    }
    scheme = toLower(scheme);
    if (scheme != std::string("bearer")) {
        return false; // only handling Bearer here
    }
    out.scheme = scheme;

    // Parameters (optional)
    skipSpaces(header, i);
    if (i >= header.size()) {
        return true; // no params
    }

    // Expect parameters as key[=value][, key[=value] ...]
    while (i < header.size()) {
        // Skip optional separators/spaces
        if (header[i] == ',') {
            ++i;
            skipSpaces(header, i);
        }
        skipSpaces(header, i);
        if (i >= header.size()) {
            break;
        }

        // Parse key
        std::string key;
        if (!parseToken(header, i, key)) {
            // If we hit an extra comma or trailing spaces, stop gracefully
            break;
        }
        // Lower-case keys for convenience
        for (size_t k = 0; k < key.size(); ++k) {
            key[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(key[k])));
        }

        skipSpaces(header, i);
        if (i >= header.size() || header[i] != '=') {
            // Parameter with no value: allow empty string
            out.params[key] = std::string();
            // Continue to next param separated by comma or end
            continue;
        }
        ++i; // consume '='
        skipSpaces(header, i);

        // Parse value (quoted or token)
        std::string value;
        if (i < header.size() && header[i] == '"') {
            if (!parseQuotedString(header, i, value)) {
                return false;
            }
        } else {
            // token until comma or end
            size_t vstart = i;
            while (i < header.size() && header[i] != ',') {
                // trim trailing spaces will be handled by rtrim below
                ++i;
            }
            size_t vend = i;
            // rtrim spaces
            while (vend > vstart && (header[vend - 1] == ' ' || header[vend - 1] == '\t')) {
                --vend;
            }
            value = header.substr(vstart, vend - vstart);
            // Also ltrim spaces
            size_t l = 0;
            while (l < value.size() && (value[l] == ' ' || value[l] == '\t')) {
                ++l;
            }
            value = value.substr(l);
        }
        out.params[key] = value;
        skipSpaces(header, i);
        if (i < header.size() && header[i] == ',') {
            // loop continues to parse next key
            continue;
        }
    }

    return true;
}

} // namespace mcp::auth
