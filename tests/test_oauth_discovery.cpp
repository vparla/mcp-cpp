//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_oauth_discovery.cpp
// Purpose: Unit tests for OAuth 2.0 discovery (RFC 9728, RFC 8414)
//==========================================================================================================

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mcp/auth/OAuthDiscovery.hpp"

using namespace mcp::auth;

TEST(OAuthDiscoveryUrlBuilder, WellKnownResourceMetadata_WithPath) {
    auto urls = buildWellKnownResourceMetadataUrls("https://example.com/mcp/rpc");
    ASSERT_EQ(urls.size(), 2u);
    EXPECT_EQ(urls[0], std::string("https://example.com/.well-known/oauth-protected-resource/mcp"));
    EXPECT_EQ(urls[1], std::string("https://example.com/.well-known/oauth-protected-resource"));
}

TEST(OAuthDiscoveryUrlBuilder, WellKnownResourceMetadata_RootPath) {
    auto urls = buildWellKnownResourceMetadataUrls("https://example.com/");
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0], std::string("https://example.com/.well-known/oauth-protected-resource"));
}

TEST(OAuthDiscoveryUrlBuilder, WellKnownResourceMetadata_NoPath) {
    auto urls = buildWellKnownResourceMetadataUrls("https://example.com");
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0], std::string("https://example.com/.well-known/oauth-protected-resource"));
}

TEST(OAuthDiscoveryUrlBuilder, WellKnownResourceMetadata_CustomPort) {
    auto urls = buildWellKnownResourceMetadataUrls("https://example.com:8443/api/mcp");
    ASSERT_EQ(urls.size(), 2u);
    EXPECT_EQ(urls[0], std::string("https://example.com:8443/.well-known/oauth-protected-resource/api"));
    EXPECT_EQ(urls[1], std::string("https://example.com:8443/.well-known/oauth-protected-resource"));
}

TEST(OAuthDiscoveryUrlBuilder, WellKnownResourceMetadata_HttpDefaultPort) {
    auto urls = buildWellKnownResourceMetadataUrls("http://localhost/mcp");
    ASSERT_EQ(urls.size(), 2u);
    EXPECT_EQ(urls[0], std::string("http://localhost/.well-known/oauth-protected-resource/mcp"));
    EXPECT_EQ(urls[1], std::string("http://localhost/.well-known/oauth-protected-resource"));
}

TEST(OAuthDiscoveryUrlBuilder, ASMetadata_WithPath) {
    auto urls = buildAuthorizationServerMetadataUrls("https://auth.example.com/tenant1");
    ASSERT_EQ(urls.size(), 3u);
    EXPECT_EQ(urls[0], std::string("https://auth.example.com/.well-known/oauth-authorization-server/tenant1"));
    EXPECT_EQ(urls[1], std::string("https://auth.example.com/.well-known/openid-configuration/tenant1"));
    EXPECT_EQ(urls[2], std::string("https://auth.example.com/tenant1/.well-known/openid-configuration"));
}

TEST(OAuthDiscoveryUrlBuilder, ASMetadata_NoPath) {
    auto urls = buildAuthorizationServerMetadataUrls("https://auth.example.com");
    ASSERT_EQ(urls.size(), 2u);
    EXPECT_EQ(urls[0], std::string("https://auth.example.com/.well-known/oauth-authorization-server"));
    EXPECT_EQ(urls[1], std::string("https://auth.example.com/.well-known/openid-configuration"));
}

TEST(OAuthDiscoveryUrlBuilder, ASMetadata_TrailingSlash) {
    auto urls = buildAuthorizationServerMetadataUrls("https://auth.example.com/");
    ASSERT_EQ(urls.size(), 2u);
    EXPECT_EQ(urls[0], std::string("https://auth.example.com/.well-known/oauth-authorization-server"));
    EXPECT_EQ(urls[1], std::string("https://auth.example.com/.well-known/openid-configuration"));
}

TEST(OAuthDiscoveryParsing, ProtectedResourceMetadata_Valid) {
    std::string json = R"({
        "resource": "https://mcp.example.com",
        "authorization_servers": ["https://auth.example.com", "https://backup.example.com"],
        "scopes_supported": ["files:read", "files:write"],
        "bearer_methods_supported": "header",
        "resource_documentation": "https://docs.example.com"
    })";
    ProtectedResourceMetadata meta;
    EXPECT_TRUE(parseProtectedResourceMetadata(json, meta));
    EXPECT_EQ(meta.resource, std::string("https://mcp.example.com"));
    ASSERT_EQ(meta.authorizationServers.size(), 2u);
    EXPECT_EQ(meta.authorizationServers[0], std::string("https://auth.example.com"));
    EXPECT_EQ(meta.authorizationServers[1], std::string("https://backup.example.com"));
    ASSERT_EQ(meta.scopesSupported.size(), 2u);
    EXPECT_EQ(meta.scopesSupported[0], std::string("files:read"));
    EXPECT_EQ(meta.scopesSupported[1], std::string("files:write"));
    EXPECT_EQ(meta.bearerMethodsSupported, std::string("header"));
    EXPECT_EQ(meta.resourceDocumentation, std::string("https://docs.example.com"));
}

TEST(OAuthDiscoveryParsing, ProtectedResourceMetadata_MinimalValid) {
    std::string json = R"({"authorization_servers":["https://auth.example.com"]})";
    ProtectedResourceMetadata meta;
    EXPECT_TRUE(parseProtectedResourceMetadata(json, meta));
    ASSERT_EQ(meta.authorizationServers.size(), 1u);
    EXPECT_EQ(meta.authorizationServers[0], std::string("https://auth.example.com"));
}

TEST(OAuthDiscoveryParsing, ProtectedResourceMetadata_EmptyAuthServers) {
    std::string json = R"({"authorization_servers":[]})";
    ProtectedResourceMetadata meta;
    EXPECT_FALSE(parseProtectedResourceMetadata(json, meta));
}

TEST(OAuthDiscoveryParsing, ProtectedResourceMetadata_MissingAuthServers) {
    std::string json = R"({"resource":"https://example.com"})";
    ProtectedResourceMetadata meta;
    EXPECT_FALSE(parseProtectedResourceMetadata(json, meta));
}

TEST(OAuthDiscoveryParsing, ProtectedResourceMetadata_InvalidJson) {
    std::string json = "not valid json";
    ProtectedResourceMetadata meta;
    EXPECT_FALSE(parseProtectedResourceMetadata(json, meta));
}

TEST(OAuthDiscoveryParsing, AuthorizationServerMetadata_Valid) {
    std::string json = R"({
        "issuer": "https://auth.example.com",
        "authorization_endpoint": "https://auth.example.com/authorize",
        "token_endpoint": "https://auth.example.com/token",
        "registration_endpoint": "https://auth.example.com/register",
        "jwks_uri": "https://auth.example.com/.well-known/jwks.json",
        "scopes_supported": ["openid", "profile", "email"],
        "response_types_supported": ["code", "token"],
        "grant_types_supported": ["authorization_code", "client_credentials"],
        "token_endpoint_auth_methods_supported": ["client_secret_basic", "client_secret_post"],
        "client_id_metadata_document_supported": true
    })";
    AuthorizationServerMetadata meta;
    EXPECT_TRUE(parseAuthorizationServerMetadata(json, meta));
    EXPECT_EQ(meta.issuer, std::string("https://auth.example.com"));
    EXPECT_EQ(meta.authorizationEndpoint, std::string("https://auth.example.com/authorize"));
    EXPECT_EQ(meta.tokenEndpoint, std::string("https://auth.example.com/token"));
    EXPECT_EQ(meta.registrationEndpoint, std::string("https://auth.example.com/register"));
    EXPECT_EQ(meta.jwksUri, std::string("https://auth.example.com/.well-known/jwks.json"));
    ASSERT_EQ(meta.scopesSupported.size(), 3u);
    EXPECT_EQ(meta.scopesSupported[0], std::string("openid"));
    ASSERT_EQ(meta.responseTypesSupported.size(), 2u);
    ASSERT_EQ(meta.grantTypesSupported.size(), 2u);
    ASSERT_EQ(meta.tokenEndpointAuthMethodsSupported.size(), 2u);
    EXPECT_TRUE(meta.clientIdMetadataDocumentSupported);
}

TEST(OAuthDiscoveryParsing, AuthorizationServerMetadata_MinimalValid) {
    std::string json = R"({"token_endpoint":"https://auth.example.com/token"})";
    AuthorizationServerMetadata meta;
    EXPECT_TRUE(parseAuthorizationServerMetadata(json, meta));
    EXPECT_EQ(meta.tokenEndpoint, std::string("https://auth.example.com/token"));
}

TEST(OAuthDiscoveryParsing, AuthorizationServerMetadata_MissingTokenEndpoint) {
    std::string json = R"({"issuer":"https://auth.example.com"})";
    AuthorizationServerMetadata meta;
    EXPECT_FALSE(parseAuthorizationServerMetadata(json, meta));
}

TEST(OAuthDiscoveryParsing, AuthorizationServerMetadata_ClientIdMetadataFalse) {
    std::string json = R"({
        "token_endpoint": "https://auth.example.com/token",
        "client_id_metadata_document_supported": false
    })";
    AuthorizationServerMetadata meta;
    EXPECT_TRUE(parseAuthorizationServerMetadata(json, meta));
    EXPECT_FALSE(meta.clientIdMetadataDocumentSupported);
}

TEST(OAuthDiscoveryParsing, AuthorizationServerMetadata_EscapedStrings) {
    std::string json = R"({
        "issuer": "https://auth.example.com/path\"with\"quotes",
        "token_endpoint": "https://auth.example.com/token"
    })";
    AuthorizationServerMetadata meta;
    EXPECT_TRUE(parseAuthorizationServerMetadata(json, meta));
    EXPECT_EQ(meta.issuer, std::string("https://auth.example.com/path\"with\"quotes"));
}
