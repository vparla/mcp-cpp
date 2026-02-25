//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: OAuthDiscovery.hpp
// Purpose: OAuth 2.0 Protected Resource and Authorization Server metadata discovery (RFC 9728, RFC 8414)
//==========================================================================================================

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl.hpp>

namespace mcp::auth {

//==========================================================================================================
// ProtectedResourceMetadata
// Purpose: RFC 9728 Protected Resource Metadata document fields.
//==========================================================================================================
struct ProtectedResourceMetadata {
    std::string resource;
    std::vector<std::string> authorizationServers;
    std::vector<std::string> scopesSupported;
    std::string bearerMethodsSupported;
    std::string resourceDocumentation;
};

//==========================================================================================================
// AuthorizationServerMetadata
// Purpose: OAuth 2.0 Authorization Server Metadata (RFC 8414) / OpenID Connect Discovery fields.
//==========================================================================================================
struct AuthorizationServerMetadata {
    std::string issuer;
    std::string authorizationEndpoint;
    std::string tokenEndpoint;
    std::string registrationEndpoint;
    std::string jwksUri;
    std::vector<std::string> scopesSupported;
    std::vector<std::string> responseTypesSupported;
    std::vector<std::string> grantTypesSupported;
    std::vector<std::string> tokenEndpointAuthMethodsSupported;
    bool clientIdMetadataDocumentSupported{false};
};

//==========================================================================================================
// DiscoveryOptions
// Purpose: Configuration for OAuth discovery operations.
//==========================================================================================================
struct DiscoveryOptions {
    std::string serverName;
    std::string caFile;
    std::string caPath;
    unsigned int connectTimeoutMs{10000};
    unsigned int readTimeoutMs{30000};
};

using DiscoveryErrorFn = std::function<void(const std::string&)>;

//==========================================================================================================
// coHttpGet
// Purpose: Async HTTP GET returning response body. Returns empty string on failure.
//==========================================================================================================
boost::asio::awaitable<std::string> coHttpGet(
    const std::string& url,
    const DiscoveryOptions& opts,
    boost::asio::ssl::context* sslCtxOpt,
    DiscoveryErrorFn errorHandler);

//==========================================================================================================
// coHttpGetWithStatus
// Purpose: Async HTTP GET returning (statusCode, body). Returns (0, "") on connection failure.
//==========================================================================================================
boost::asio::awaitable<std::pair<unsigned int, std::string>> coHttpGetWithStatus(
    const std::string& url,
    const DiscoveryOptions& opts,
    boost::asio::ssl::context* sslCtxOpt,
    DiscoveryErrorFn errorHandler);

//==========================================================================================================
// parseProtectedResourceMetadata
// Purpose: Parse RFC 9728 Protected Resource Metadata JSON into struct.
// Returns: true if parsing succeeded and authorization_servers is non-empty.
//==========================================================================================================
bool parseProtectedResourceMetadata(const std::string& json, ProtectedResourceMetadata& out);

//==========================================================================================================
// parseAuthorizationServerMetadata
// Purpose: Parse RFC 8414 / OIDC Authorization Server Metadata JSON into struct.
// Returns: true if parsing succeeded and token_endpoint is present.
//==========================================================================================================
bool parseAuthorizationServerMetadata(const std::string& json, AuthorizationServerMetadata& out);

//==========================================================================================================
// buildWellKnownResourceMetadataUrls
// Purpose: Build well-known URLs for Protected Resource Metadata discovery per RFC 9728.
// Args:
//   baseUrl: The MCP server base URL (e.g., "https://example.com/mcp")
// Returns: List of URLs to try in priority order (path-based first, then root).
//==========================================================================================================
std::vector<std::string> buildWellKnownResourceMetadataUrls(const std::string& baseUrl);

//==========================================================================================================
// buildAuthorizationServerMetadataUrls
// Purpose: Build well-known URLs for AS metadata discovery per RFC 8414 / OIDC.
// Args:
//   issuerUrl: The authorization server issuer URL from Protected Resource Metadata.
// Returns: List of URLs to try in priority order (OAuth 2.0 AS, OIDC path-insert, OIDC path-append).
//==========================================================================================================
std::vector<std::string> buildAuthorizationServerMetadataUrls(const std::string& issuerUrl);

//==========================================================================================================
// coDiscoverProtectedResourceMetadata
// Purpose: Discover Protected Resource Metadata using WWW-Authenticate or well-known fallback.
// Args:
//   resourceMetadataUrl: URL from WWW-Authenticate header (may be empty for fallback probing).
//   mcpServerUrl: The MCP server URL for well-known fallback probing.
//   opts: Discovery options (timeouts, TLS settings).
//   sslCtxOpt: Optional SSL context (nullptr to use default).
//   errorHandler: Callback for error/debug messages.
// Returns: Parsed metadata on success, or struct with empty authorizationServers on failure.
//==========================================================================================================
boost::asio::awaitable<ProtectedResourceMetadata> coDiscoverProtectedResourceMetadata(
    const std::string& resourceMetadataUrl,
    const std::string& mcpServerUrl,
    const DiscoveryOptions& opts,
    boost::asio::ssl::context* sslCtxOpt,
    DiscoveryErrorFn errorHandler);

//==========================================================================================================
// coDiscoverAuthorizationServerMetadata
// Purpose: Discover Authorization Server Metadata using RFC 8414 / OIDC well-known endpoints.
// Args:
//   issuerUrl: The authorization server issuer URL.
//   opts: Discovery options (timeouts, TLS settings).
//   sslCtxOpt: Optional SSL context (nullptr to use default).
//   errorHandler: Callback for error/debug messages.
// Returns: Parsed metadata on success, or struct with empty tokenEndpoint on failure.
//==========================================================================================================
boost::asio::awaitable<AuthorizationServerMetadata> coDiscoverAuthorizationServerMetadata(
    const std::string& issuerUrl,
    const DiscoveryOptions& opts,
    boost::asio::ssl::context* sslCtxOpt,
    DiscoveryErrorFn errorHandler);

} // namespace mcp::auth
