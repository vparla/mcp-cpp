//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_negative_api.cpp
// Purpose: Negative-path tests for server request validation and error shaping
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Protocol.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/JSONRPCTypes.h"
#include <future>
#include <chrono>

using namespace mcp;

// Helper to extract error code from a JSONRPCResponse
static int extractErrorCode(const JSONRPCResponse& resp) {
    EXPECT_TRUE(resp.IsError());
    EXPECT_TRUE(resp.error.has_value());
    const JSONValue& err = resp.error.value();
    EXPECT_TRUE(std::holds_alternative<JSONValue::Object>(err.value));
    const auto& obj = std::get<JSONValue::Object>(err.value);
    auto it = obj.find("code");
    EXPECT_TRUE(it != obj.end());
    EXPECT_TRUE(std::holds_alternative<int64_t>(it->second->value));
    return static_cast<int>(std::get<int64_t>(it->second->value));
}

TEST(NegativeAPI, ToolsCall_InvalidParams) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::CallTool;
    // No params -> InvalidParams (-32602)
    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    EXPECT_TRUE(resp->IsError());
    EXPECT_EQ(extractErrorCode(*resp), JSONRPCErrorCodes::InvalidParams);
}

TEST(NegativeAPI, PromptsGet_MissingParams) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::GetPrompt;
    // No params -> InvalidParams (-32602)
    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    EXPECT_TRUE(resp->IsError());
    EXPECT_EQ(extractErrorCode(*resp), JSONRPCErrorCodes::InvalidParams);
}

TEST(NegativeAPI, PromptsGet_NotFound) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::GetPrompt;
    JSONValue::Object params;
    params["name"] = std::make_shared<JSONValue>(std::string("does-not-exist"));
    // optional arguments provided as empty object
    params["arguments"] = std::make_shared<JSONValue>(JSONValue::Object{});
    req->params.emplace(JSONValue{params});

    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    EXPECT_TRUE(resp->IsError());
    EXPECT_EQ(extractErrorCode(*resp), JSONRPCErrorCodes::MethodNotFound);
}

TEST(NegativeAPI, ResourcesRead_MissingParams) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::ReadResource;
    // No params -> InvalidParams (-32602)
    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    EXPECT_TRUE(resp->IsError());
    EXPECT_EQ(extractErrorCode(*resp), JSONRPCErrorCodes::InvalidParams);
}

TEST(NegativeAPI, CreateMessage_NoServerHandler) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::CreateMessage;
    JSONValue::Object params;
    JSONValue::Array messages;
    messages.push_back(std::make_shared<JSONValue>(JSONValue::Object{}));
    params["messages"] = std::make_shared<JSONValue>(messages);
    req->params.emplace(JSONValue{params});

    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    EXPECT_TRUE(resp->IsError());
    EXPECT_EQ(extractErrorCode(*resp), JSONRPCErrorCodes::MethodNotAllowed);
}
