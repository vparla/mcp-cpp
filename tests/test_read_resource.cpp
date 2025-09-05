//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_read_resource.cpp
// Purpose: Tests for Server::ReadResource contents shape
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Protocol.h"

using namespace mcp;

TEST(ServerReadResource, ReturnsContents) {
    Server server("TestServer");

    const std::string uri = "test://res1";

    server.RegisterResource(uri, [uri](const std::string& reqUri, std::stop_token st) -> std::future<ReadResourceResult> {
        (void)st;
        return std::async(std::launch::async, [uri, reqUri]() {
            EXPECT_EQ(reqUri, uri);
            ReadResourceResult r;
            // One content item with fields similar to MCP spec
            JSONValue::Object content;
            content["uri"] = std::make_shared<JSONValue>(uri);
            content["mimeType"] = std::make_shared<JSONValue>(std::string("text/plain"));
            content["text"] = std::make_shared<JSONValue>(std::string("hello world"));
            r.contents.push_back(JSONValue{content});
            return r;
        });
    });

    auto res = server.ReadResource(uri).get();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(res.value));
    const auto& obj = std::get<JSONValue::Object>(res.value);
    auto it = obj.find("contents");
    ASSERT_TRUE(it != obj.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Array>(it->second->value));
    const auto& arr = std::get<JSONValue::Array>(it->second->value);
    ASSERT_EQ(arr.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(arr[0]->value));
    const auto& c0 = std::get<JSONValue::Object>(arr[0]->value);
    auto uIt = c0.find("uri");
    ASSERT_TRUE(uIt != c0.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(uIt->second->value));
    EXPECT_EQ(std::get<std::string>(uIt->second->value), uri);
}
