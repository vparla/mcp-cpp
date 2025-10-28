#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <chrono>
#include <boost/asio/awaitable.hpp>

#include "mcp/auth/IAuth.hpp"
#include "mcp/auth/OAuthClient.hpp"

namespace mcp::auth {

class OAuth2ClientCredentialsAuth final : public IAuth {
public:
    OAuth2ClientCredentialsAuth(
        std::string tokenUrl,
        std::string clientId,
        std::string clientSecret,
        std::string scope,
        unsigned int tokenRefreshSkewSeconds,
        unsigned int connectTimeoutMs,
        unsigned int readTimeoutMs,
        std::string caFile,
        std::string caPath);

    boost::asio::awaitable<void> ensureReady() override;
    std::vector<HeaderKV> headers() const override;
    void setErrorHandler(std::function<void(const std::string&)> fn) override;

private:
    std::string tokenUrl;
    std::string clientId;
    std::string clientSecret;
    std::string scope;
    unsigned int tokenRefreshSkewSeconds{60};
    unsigned int connectTimeoutMs{10000};
    unsigned int readTimeoutMs{30000};
    std::string caFile;
    std::string caPath;

    mutable std::mutex mtx;
    std::string cachedAccessToken;
    std::chrono::steady_clock::time_point tokenExpiry{};

    std::function<void(const std::string&)> onError;
};

} // namespace mcp::auth
