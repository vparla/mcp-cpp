//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_www_authenticate.cpp
// Purpose: Unit tests for WWW-Authenticate parser (Bearer challenges)
//==========================================================================================================

#include <gtest/gtest.h>

#include <string>

#include "mcp/auth/WwwAuthenticate.hpp"

using namespace mcp::auth;

TEST(WwwAuthenticate, ParseBearerWithResourceMetadataAndScope) {
    const std::string h =
        "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource\", scope=\"files:read files:write\"";
    WwwAuthChallenge c;
    ASSERT_TRUE(parseWwwAuthenticate(h, c));
    EXPECT_EQ(c.scheme, std::string("bearer"));
    ASSERT_TRUE(c.params.find("resource_metadata") != c.params.end());
    EXPECT_EQ(c.params["resource_metadata"], std::string("https://mcp.example.com/.well-known/oauth-protected-resource"));
    ASSERT_TRUE(c.params.find("scope") != c.params.end());
    EXPECT_EQ(c.params["scope"], std::string("files:read files:write"));
}

TEST(WwwAuthenticate, ParseBearerWithoutParams) {
    const std::string h = "Bearer";
    WwwAuthChallenge c;
    ASSERT_TRUE(parseWwwAuthenticate(h, c));
    EXPECT_EQ(c.scheme, std::string("bearer"));
    EXPECT_TRUE(c.params.empty());
}

TEST(WwwAuthenticate, RejectNonBearerScheme) {
    const std::string h = "Basic realm=\"X\"";
    WwwAuthChallenge c;
    EXPECT_FALSE(parseWwwAuthenticate(h, c));
}

TEST(WwwAuthenticate, HandleQuotedEscapes) {
    const std::string h = R"(Bearer error="insufficient_scope", error_description="need \"files:write\"")";
    WwwAuthChallenge c;
    ASSERT_TRUE(parseWwwAuthenticate(h, c));
    EXPECT_EQ(c.params["error"], std::string("insufficient_scope"));
    EXPECT_EQ(c.params["error_description"], std::string("need \"files:write\""));
}
