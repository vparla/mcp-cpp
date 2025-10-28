#pragma once

#include <string>
#include <functional>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl.hpp>

namespace mcp::auth {

struct TokenFetchParams {
    std::string url;
    std::string serverName;
    std::string caFile;
    std::string caPath;
    unsigned int connectTimeoutMs{0};
    unsigned int readTimeoutMs{0};
};

using ErrorFn = std::function<void(const std::string&)>;

boost::asio::awaitable<std::string> coPostFormUrlencoded(
    const TokenFetchParams& params,
    const std::string& body,
    boost::asio::ssl::context* sslCtxOpt,
    ErrorFn errorHandler);

} // namespace mcp::auth
