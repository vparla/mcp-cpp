//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: OAuthDiscovery.cpp
// Purpose: OAuth 2.0 Protected Resource and Authorization Server metadata discovery (RFC 9728, RFC 8414)
//==========================================================================================================

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>

#include "mcp/auth/OAuthDiscovery.hpp"

namespace mcp::auth {

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;

struct UrlParts {
    std::string scheme;
    std::string host;
    std::string port;
    std::string path;
};

static UrlParts parseUrlParts(const std::string& url) {
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
    return parts;
}

static std::string getOrigin(const UrlParts& u) {
    std::string origin = u.scheme + std::string("://") + u.host;
    if ((u.scheme == std::string("https") && u.port != std::string("443")) ||
        (u.scheme == std::string("http") && u.port != std::string("80"))) {
        origin += std::string(":") + u.port;
    }
    return origin;
}

static void skipWhitespace(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
        ++i;
    }
}

static bool parseJsonStringValue(const std::string& json, size_t& i, std::string& out) {
    out.clear();
    if (i >= json.size() || json[i] != '"') {
        return false;
    }
    ++i;
    while (i < json.size()) {
        char ch = json[i];
        if (ch == '\\' && (i + 1) < json.size()) {
            char next = json[i + 1];
            if (next == '"' || next == '\\' || next == '/') {
                out.push_back(next);
            } else if (next == 'n') {
                out.push_back('\n');
            } else if (next == 'r') {
                out.push_back('\r');
            } else if (next == 't') {
                out.push_back('\t');
            } else {
                out.push_back(next);
            }
            i += 2;
            continue;
        }
        if (ch == '"') {
            ++i;
            return true;
        }
        out.push_back(ch);
        ++i;
    }
    return false;
}

static bool parseJsonStringField(const std::string& json, const std::string& key, std::string& out) {
    out.clear();
    std::string needle = std::string("\"") + key + std::string("\"");
    std::size_t k = json.find(needle);
    if (k == std::string::npos) {
        return false;
    }
    std::size_t i = k + needle.size();
    skipWhitespace(json, i);
    if (i >= json.size() || json[i] != ':') {
        return false;
    }
    ++i;
    skipWhitespace(json, i);
    return parseJsonStringValue(json, i, out);
}

static bool parseJsonStringArray(const std::string& json, const std::string& key, std::vector<std::string>& out) {
    out.clear();
    std::string needle = std::string("\"") + key + std::string("\"");
    std::size_t k = json.find(needle);
    if (k == std::string::npos) {
        return false;
    }
    std::size_t i = k + needle.size();
    skipWhitespace(json, i);
    if (i >= json.size() || json[i] != ':') {
        return false;
    }
    ++i;
    skipWhitespace(json, i);
    if (i >= json.size() || json[i] != '[') {
        return false;
    }
    ++i;
    while (i < json.size()) {
        skipWhitespace(json, i);
        if (i < json.size() && json[i] == ']') {
            ++i;
            return true;
        }
        if (i < json.size() && json[i] == ',') {
            ++i;
            skipWhitespace(json, i);
        }
        if (i < json.size() && json[i] == '"') {
            std::string val;
            if (!parseJsonStringValue(json, i, val)) {
                return false;
            }
            out.push_back(std::move(val));
        } else if (i < json.size() && json[i] == ']') {
            ++i;
            return true;
        } else {
            return false;
        }
    }
    return false;
}

static bool parseJsonBoolField(const std::string& json, const std::string& key, bool& out) {
    std::string needle = std::string("\"") + key + std::string("\"");
    std::size_t k = json.find(needle);
    if (k == std::string::npos) {
        return false;
    }
    std::size_t i = k + needle.size();
    skipWhitespace(json, i);
    if (i >= json.size() || json[i] != ':') {
        return false;
    }
    ++i;
    skipWhitespace(json, i);
    if (i + 4 <= json.size() && json.substr(i, 4) == std::string("true")) {
        out = true;
        return true;
    }
    if (i + 5 <= json.size() && json.substr(i, 5) == std::string("false")) {
        out = false;
        return true;
    }
    return false;
}

static std::unique_ptr<ssl::context> createDefaultSslContext(
    const DiscoveryOptions& opts,
    DiscoveryErrorFn errorHandler) {
    auto ctx = std::make_unique<ssl::context>(ssl::context::tls_client);
    ::SSL_CTX_set_min_proto_version(ctx->native_handle(), TLS1_2_VERSION);
    ::SSL_CTX_set_max_proto_version(ctx->native_handle(), TLS1_3_VERSION);
    try {
        ctx->set_default_verify_paths();
    } catch (const std::exception& e) {
        if (errorHandler) {
            errorHandler(std::string("Discovery: set_default_verify_paths failed: ") + e.what());
        }
    } catch (...) {
        if (errorHandler) {
            errorHandler(std::string("Discovery: set_default_verify_paths failed (unknown)"));
        }
    }
    if (!opts.caFile.empty()) {
        try {
            ctx->load_verify_file(opts.caFile);
        } catch (const std::exception& e) {
            if (errorHandler) {
                errorHandler(std::string("Discovery: load_verify_file failed: ") + e.what());
            }
        }
    }
    if (!opts.caPath.empty()) {
        try {
            ctx->add_verify_path(opts.caPath);
        } catch (const std::exception& e) {
            if (errorHandler) {
                errorHandler(std::string("Discovery: add_verify_path failed: ") + e.what());
            }
        }
    }
    ctx->set_verify_mode(ssl::verify_peer);
    return ctx;
}

static net::awaitable<std::pair<unsigned int, std::string>> doHttpsGet(
    const UrlParts& u,
    const DiscoveryOptions& opts,
    ssl::context* sslCtxOpt,
    DiscoveryErrorFn errorHandler) {
    tcp::resolver resolver(co_await net::this_coro::executor);
    auto results = co_await resolver.async_resolve(u.host, u.port, net::use_awaitable);

    ssl::context* ctxPtr = sslCtxOpt;
    std::unique_ptr<ssl::context> localCtx;
    if (!ctxPtr) {
        localCtx = createDefaultSslContext(opts, errorHandler);
        ctxPtr = localCtx.get();
    }
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream(co_await net::this_coro::executor, *ctxPtr);
    std::string sni = opts.serverName.empty() ? u.host : opts.serverName;
    if (!sni.empty()) {
        if (!::SSL_set_tlsext_host_name(stream.native_handle(), sni.c_str())) {
            if (errorHandler) {
                errorHandler(std::string("Discovery: SNI set failed"));
            }
        }
        (void)::SSL_set1_host(stream.native_handle(), sni.c_str());
    }
    stream.next_layer().expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
    co_await stream.next_layer().async_connect(results, net::use_awaitable);
    co_await stream.async_handshake(ssl::stream_base::client, net::use_awaitable);

    http::request<http::string_body> req{http::verb::get, u.path, 11};
    req.set(http::field::host, u.host);
    req.set(http::field::accept, "application/json");
    req.set(http::field::connection, "close");
    req.prepare_payload();

    stream.next_layer().expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
    co_await http::async_write(stream, req, net::use_awaitable);
    boost::beast::flat_buffer buffer;
    http::response<http::string_body> res;
    co_await http::async_read(stream, buffer, res, net::use_awaitable);
    boost::system::error_code ec;
    stream.shutdown(ec);
    co_return std::make_pair(static_cast<unsigned int>(res.result_int()), res.body());
}

static net::awaitable<std::pair<unsigned int, std::string>> doHttpGet(
    const UrlParts& u,
    const DiscoveryOptions& opts) {
    tcp::resolver resolver(co_await net::this_coro::executor);
    auto results = co_await resolver.async_resolve(u.host, u.port, net::use_awaitable);

    boost::beast::tcp_stream stream(co_await net::this_coro::executor);
    stream.expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
    co_await stream.async_connect(results, net::use_awaitable);

    http::request<http::string_body> req{http::verb::get, u.path, 11};
    req.set(http::field::host, u.host);
    req.set(http::field::accept, "application/json");
    req.set(http::field::connection, "close");
    req.prepare_payload();

    stream.expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
    co_await http::async_write(stream, req, net::use_awaitable);
    boost::beast::flat_buffer buffer;
    http::response<http::string_body> res;
    co_await http::async_read(stream, buffer, res, net::use_awaitable);
    boost::system::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    co_return std::make_pair(static_cast<unsigned int>(res.result_int()), res.body());
}

boost::asio::awaitable<std::pair<unsigned int, std::string>> coHttpGetWithStatus(
    const std::string& url,
    const DiscoveryOptions& opts,
    ssl::context* sslCtxOpt,
    DiscoveryErrorFn errorHandler) {
    try {
        UrlParts u = parseUrlParts(url);
        if (errorHandler) {
            errorHandler(std::string("Discovery: GET ") + url);
        }
        if (u.scheme == std::string("https")) {
            co_return co_await doHttpsGet(u, opts, sslCtxOpt, errorHandler);
        } else {
            co_return co_await doHttpGet(u, opts);
        }
    } catch (const std::exception& e) {
        if (errorHandler) {
            errorHandler(std::string("Discovery GET failed: ") + e.what());
        }
    }
    co_return std::make_pair(0u, std::string());
}

boost::asio::awaitable<std::string> coHttpGet(
    const std::string& url,
    const DiscoveryOptions& opts,
    ssl::context* sslCtxOpt,
    DiscoveryErrorFn errorHandler) {
    auto [status, body] = co_await coHttpGetWithStatus(url, opts, sslCtxOpt, errorHandler);
    if (status >= 200 && status < 300) {
        co_return body;
    }
    co_return std::string();
}

bool parseProtectedResourceMetadata(const std::string& json, ProtectedResourceMetadata& out) {
    out = ProtectedResourceMetadata{};
    parseJsonStringField(json, std::string("resource"), out.resource);
    parseJsonStringArray(json, std::string("authorization_servers"), out.authorizationServers);
    parseJsonStringArray(json, std::string("scopes_supported"), out.scopesSupported);
    parseJsonStringField(json, std::string("bearer_methods_supported"), out.bearerMethodsSupported);
    parseJsonStringField(json, std::string("resource_documentation"), out.resourceDocumentation);
    return !out.authorizationServers.empty();
}

bool parseAuthorizationServerMetadata(const std::string& json, AuthorizationServerMetadata& out) {
    out = AuthorizationServerMetadata{};
    parseJsonStringField(json, std::string("issuer"), out.issuer);
    parseJsonStringField(json, std::string("authorization_endpoint"), out.authorizationEndpoint);
    parseJsonStringField(json, std::string("token_endpoint"), out.tokenEndpoint);
    parseJsonStringField(json, std::string("registration_endpoint"), out.registrationEndpoint);
    parseJsonStringField(json, std::string("jwks_uri"), out.jwksUri);
    parseJsonStringArray(json, std::string("scopes_supported"), out.scopesSupported);
    parseJsonStringArray(json, std::string("response_types_supported"), out.responseTypesSupported);
    parseJsonStringArray(json, std::string("grant_types_supported"), out.grantTypesSupported);
    parseJsonStringArray(json, std::string("token_endpoint_auth_methods_supported"), out.tokenEndpointAuthMethodsSupported);
    parseJsonBoolField(json, std::string("client_id_metadata_document_supported"), out.clientIdMetadataDocumentSupported);
    return !out.tokenEndpoint.empty();
}

std::vector<std::string> buildWellKnownResourceMetadataUrls(const std::string& baseUrl) {
    std::vector<std::string> urls;
    UrlParts u = parseUrlParts(baseUrl);
    std::string origin = getOrigin(u);

    std::string path = u.path;
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }

    if (!path.empty() && path != std::string("/")) {
        std::size_t lastSlash = path.rfind('/');
        std::string basePath;
        if (lastSlash != std::string::npos && lastSlash > 0) {
            basePath = path.substr(0, lastSlash);
        } else {
            basePath = path;
        }
        urls.push_back(origin + std::string("/.well-known/oauth-protected-resource") + basePath);
    }

    urls.push_back(origin + std::string("/.well-known/oauth-protected-resource"));
    return urls;
}

std::vector<std::string> buildAuthorizationServerMetadataUrls(const std::string& issuerUrl) {
    std::vector<std::string> urls;
    UrlParts u = parseUrlParts(issuerUrl);
    std::string origin = getOrigin(u);

    std::string path = u.path;
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }

    if (!path.empty() && path != std::string("/")) {
        urls.push_back(origin + std::string("/.well-known/oauth-authorization-server") + path);
        urls.push_back(origin + std::string("/.well-known/openid-configuration") + path);
        urls.push_back(origin + path + std::string("/.well-known/openid-configuration"));
    } else {
        urls.push_back(origin + std::string("/.well-known/oauth-authorization-server"));
        urls.push_back(origin + std::string("/.well-known/openid-configuration"));
    }
    return urls;
}

boost::asio::awaitable<ProtectedResourceMetadata> coDiscoverProtectedResourceMetadata(
    const std::string& resourceMetadataUrl,
    const std::string& mcpServerUrl,
    const DiscoveryOptions& opts,
    ssl::context* sslCtxOpt,
    DiscoveryErrorFn errorHandler) {
    ProtectedResourceMetadata meta;

    if (!resourceMetadataUrl.empty()) {
        if (errorHandler) {
            errorHandler(std::string("Discovery: fetching resource metadata from WWW-Authenticate: ") + resourceMetadataUrl);
        }
        std::string body = co_await coHttpGet(resourceMetadataUrl, opts, sslCtxOpt, errorHandler);
        if (!body.empty() && parseProtectedResourceMetadata(body, meta)) {
            co_return meta;
        }
        if (errorHandler) {
            errorHandler(std::string("Discovery: WWW-Authenticate URL failed, falling back to well-known"));
        }
    }

    std::vector<std::string> urls = buildWellKnownResourceMetadataUrls(mcpServerUrl);
    for (const std::string& url : urls) {
        if (errorHandler) {
            errorHandler(std::string("Discovery: probing ") + url);
        }
        auto [status, body] = co_await coHttpGetWithStatus(url, opts, sslCtxOpt, errorHandler);
        if (status == 200 && !body.empty() && parseProtectedResourceMetadata(body, meta)) {
            co_return meta;
        }
    }

    if (errorHandler) {
        errorHandler(std::string("Discovery: failed to discover Protected Resource Metadata"));
    }
    co_return ProtectedResourceMetadata{};
}

boost::asio::awaitable<AuthorizationServerMetadata> coDiscoverAuthorizationServerMetadata(
    const std::string& issuerUrl,
    const DiscoveryOptions& opts,
    ssl::context* sslCtxOpt,
    DiscoveryErrorFn errorHandler) {
    AuthorizationServerMetadata meta;

    std::vector<std::string> urls = buildAuthorizationServerMetadataUrls(issuerUrl);
    for (const std::string& url : urls) {
        if (errorHandler) {
            errorHandler(std::string("Discovery: probing AS metadata at ") + url);
        }
        auto [status, body] = co_await coHttpGetWithStatus(url, opts, sslCtxOpt, errorHandler);
        if (status == 200 && !body.empty() && parseAuthorizationServerMetadata(body, meta)) {
            co_return meta;
        }
    }

    if (errorHandler) {
        errorHandler(std::string("Discovery: failed to discover Authorization Server Metadata for ") + issuerUrl);
    }
    co_return AuthorizationServerMetadata{};
}

} // namespace mcp::auth
