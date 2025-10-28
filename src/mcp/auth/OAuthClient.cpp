#include <string>
#include <sstream>
#include <utility>
#include <memory>
#include <chrono>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>
#include <openssl/ssl.h>
#include "mcp/auth/OAuthClient.hpp"

namespace mcp::auth {
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;

struct UrlParts { std::string scheme, host, port, path, serverName; };

static UrlParts parseUrl(const std::string& url) {
    UrlParts parts; 
    std::size_t pos = 0;
    std::size_t schemeEnd = url.find("://");
    if (schemeEnd != std::string::npos) {
        parts.scheme = url.substr(0, schemeEnd);
        pos = schemeEnd + 3;
    } else {
        parts.scheme = std::string("http");
        pos = 0;
    }
    std::size_t slash = url.find('/', pos);
    std::string hostPort;
    if (slash == std::string::npos) {
        hostPort = url.substr(pos);
        parts.path = std::string("/");
    } else {
        hostPort = url.substr(pos, slash - pos);
        parts.path = url.substr(slash);
    }
    std::size_t colon = hostPort.find(':');
    if (colon == std::string::npos) {
        parts.host = hostPort;
        parts.port = (parts.scheme == std::string("https")) ? std::string("443") : std::string("80");
    } else {
        parts.host = hostPort.substr(0, colon);
        parts.port = hostPort.substr(colon + 1);
    }
    parts.serverName = parts.host;
    return parts;
}

net::awaitable<std::string> coPostFormUrlencoded(
    const TokenFetchParams& params,
    const std::string& body,
    ssl::context* sslCtxOpt,
    ErrorFn errorHandler) {
    try {
        UrlParts u = parseUrl(params.url);
        tcp::resolver resolver(co_await net::this_coro::executor);
        auto results = co_await resolver.async_resolve(u.host, u.port, net::use_awaitable);
        if (errorHandler) {
        errorHandler(std::string("HTTP DEBUG: token resolved ") + u.host + std::string(":") + u.port + std::string(" path=") + u.path);
    }

        if (u.scheme == std::string("https")) {
            ssl::context* ctxPtr = sslCtxOpt;
            std::unique_ptr<ssl::context> localCtx;
            if (!ctxPtr) {
                localCtx = std::make_unique<ssl::context>(ssl::context::tls_client);
                ::SSL_CTX_set_min_proto_version(localCtx->native_handle(), TLS1_3_VERSION);
                ::SSL_CTX_set_max_proto_version(localCtx->native_handle(), TLS1_3_VERSION);
                try {
                    localCtx->set_default_verify_paths();
                } catch (const std::exception& e) {
                    if (errorHandler) {
                        errorHandler(std::string("HTTP DEBUG: token https set_default_verify_paths failed: ") + e.what());
                    }
                } catch (...) {
                    if (errorHandler) {
                        errorHandler("HTTP DEBUG: token https set_default_verify_paths failed (unknown exception)");
                    }
                }
                localCtx->set_verify_mode(ssl::verify_peer);
                ctxPtr = localCtx.get();
            }
            boost::beast::ssl_stream<boost::beast::tcp_stream> stream(co_await net::this_coro::executor, *ctxPtr);
            std::string sni = params.serverName.empty() ? u.serverName : params.serverName;
            if (!sni.empty()) {
                if (!::SSL_set_tlsext_host_name(stream.native_handle(), sni.c_str())) {
                    if (errorHandler) {
                        errorHandler("HTTP DEBUG: token https SNI set failed");
                    }
                }
                (void)::SSL_set1_host(stream.native_handle(), sni.c_str());
            }
            stream.next_layer().expires_after(std::chrono::milliseconds(params.connectTimeoutMs));
            co_await stream.next_layer().async_connect(results, net::use_awaitable);
            if (errorHandler) {
                errorHandler("HTTP DEBUG: token https connected");
            }
            co_await stream.async_handshake(ssl::stream_base::client, net::use_awaitable);
            if (errorHandler) {
                errorHandler("HTTP DEBUG: token https handshake complete");
            }

            http::request<http::string_body> req{http::verb::post, u.path, 11};
            req.set(http::field::host, sni);
            req.set(http::field::content_type, "application/x-www-form-urlencoded");
            req.set(http::field::accept, "application/json");
            req.set(http::field::connection, "close");
            req.body() = body;
            req.prepare_payload();

            stream.next_layer().expires_after(std::chrono::milliseconds(params.readTimeoutMs));
            co_await http::async_write(stream, req, net::use_awaitable);
            if (errorHandler) {
                errorHandler("HTTP DEBUG: token https wrote form");
            }
            boost::beast::flat_buffer buffer;
            http::response<http::string_body> res;
            co_await http::async_read(stream, buffer, res, net::use_awaitable);
            if (errorHandler) {
                errorHandler(std::string("HTTP DEBUG: token https read response bytes=") + std::to_string(res.body().size()));
            }
            boost::system::error_code ec; stream.shutdown(ec);
            co_return res.body();
        } else {
            boost::beast::tcp_stream stream(co_await net::this_coro::executor);
            stream.expires_after(std::chrono::milliseconds(params.connectTimeoutMs));
            co_await stream.async_connect(results, net::use_awaitable);
            if (errorHandler) {
                errorHandler("HTTP DEBUG: token http connected");
            }

            http::request<http::string_body> req{http::verb::post, u.path, 11};
            req.set(http::field::host, u.host);
            req.set(http::field::content_type, "application/x-www-form-urlencoded");
            req.set(http::field::accept, "application/json");
            req.set(http::field::connection, "close");
            req.body() = body;
            req.prepare_payload();

            stream.expires_after(std::chrono::milliseconds(params.readTimeoutMs));
            co_await http::async_write(stream, req, net::use_awaitable);
            if (errorHandler) {
                errorHandler("HTTP DEBUG: token http wrote form");
            }
            boost::beast::flat_buffer buffer;
            http::response<http::string_body> res;
            co_await http::async_read(stream, buffer, res, net::use_awaitable);
            if (errorHandler) {
                errorHandler(std::string("HTTP DEBUG: token http read response bytes=") + std::to_string(res.body().size()));
            }
            boost::system::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            co_return res.body();
        }
    } catch (const std::exception& e) {
        if (errorHandler) {
            errorHandler(std::string("HTTP coPostFormUrlencoded failed: ") + e.what());
        }
    }
    co_return std::string();
}

} // namespace mcp::auth
