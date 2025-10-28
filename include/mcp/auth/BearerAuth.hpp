//==========================================================================================================
// SPDX-License-Identifier: MIT 
// File: include/mcp/auth/BearerAuth.hpp
// Purpose: Simple static bearer token auth implementing IAuth
//==========================================================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <boost/asio/awaitable.hpp>

#include "mcp/auth/IAuth.hpp"

namespace mcp::auth {

class BearerAuth final : public IAuth {
public:
    explicit BearerAuth(std::string token) : token(std::move(token)) {}

    boost::asio::awaitable<void> ensureReady() override {
        co_return;
    }

    std::vector<HeaderKV> headers() const override {
        if (token.empty()) return {};
        return { HeaderKV{ "Authorization", std::string("Bearer ") + token } };
    }

    void setErrorHandler(std::function<void(const std::string&)> fn) override {
        (void)fn; // no-op for bearer
    }

private:
    std::string token;
};

using BearerAuthPtr = std::shared_ptr<BearerAuth>;

} // namespace mcp::auth
