//==========================================================================================================
// SPDX-License-Identifier: MIT 
// Copyright (c) 2025 Vinny Parla
// File: include/mcp/auth/IAuth.hpp
// Purpose: Authentication interface for transports
//==========================================================================================================
#pragma once

#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <boost/asio/awaitable.hpp>

namespace mcp::auth {

struct HeaderKV {
    std::string name;
    std::string value;
};

class IAuth {
public:
    virtual ~IAuth() = default;

    // Ensure credentials are ready (may perform async network, e.g., token fetch/refresh)
    virtual boost::asio::awaitable<void> ensureReady() = 0;

    // Return headers to apply to an outgoing HTTP request
    virtual std::vector<HeaderKV> headers() const = 0;

    // Optional diagnostic sink
    virtual void setErrorHandler(std::function<void(const std::string&)> fn) = 0;
};

} // namespace mcp::auth
