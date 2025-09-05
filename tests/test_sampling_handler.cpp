//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_sampling_handler.cpp
// Purpose: Tests for server-side sampling handler wiring
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include <future>
#include <chrono>

using namespace mcp;

TEST(ServerSampling, HandlesCreateMessageRequest) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");

    // Set sampling handler that echoes a minimal CreateMessageResult shape
    server.SetSamplingHandler([](const JSONValue& messages,
                                 const JSONValue& modelPreferences,
                                 const JSONValue& systemPrompt,
                                 const JSONValue& includeContext) {
        (void)messages; (void)modelPreferences; (void)systemPrompt; (void)includeContext;
        return std::async(std::launch::deferred, [](){
            JSONValue::Object resultObj;
            resultObj["model"] = std::make_shared<JSONValue>(std::string("test-model"));
            resultObj["role"] = std::make_shared<JSONValue>(std::string("assistant"));
            JSONValue::Array contentArr;
            JSONValue::Object textContent;
            textContent["type"] = std::make_shared<JSONValue>(std::string("text"));
            textContent["text"] = std::make_shared<JSONValue>(std::string("hello"));
            contentArr.push_back(std::make_shared<JSONValue>(textContent));
            resultObj["content"] = std::make_shared<JSONValue>(contentArr);
            return JSONValue{resultObj};
        });
    });

    // Start server and client transports
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Send sampling/createMessage request to server
    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::CreateMessage;
    JSONValue::Object params;
    JSONValue::Array messages;
    JSONValue::Object m0; m0["role"] = std::make_shared<JSONValue>(std::string("user"));
    m0["content"] = std::make_shared<JSONValue>(std::string("hi"));
    messages.push_back(std::make_shared<JSONValue>(m0));
    params["messages"] = std::make_shared<JSONValue>(messages);
    // Assign via emplace to avoid optional copy-assignment paths
    req->params.emplace(params);

    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    ASSERT_TRUE(resp->result.has_value());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(resp->result->value));
    const auto& obj = std::get<JSONValue::Object>(resp->result->value);
    auto it = obj.find("model");
    ASSERT_TRUE(it != obj.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(it->second->value));
    EXPECT_EQ(std::get<std::string>(it->second->value), "test-model");

    client->Close().get();
}

TEST(ServerSampling, DoesNotAdvertiseSamplingWithoutHandler) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");

    // Start server and client transports
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Send initialize and assert that 'sampling' is not advertised in capabilities
    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::Initialize;
    req->params.emplace(JSONValue::Object{});
    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    ASSERT_TRUE(resp->result.has_value());
    const auto& res = resp->result.value();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(res.value));
    const auto& ro = std::get<JSONValue::Object>(res.value);
    auto capIt = ro.find("capabilities");
    ASSERT_TRUE(capIt != ro.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(capIt->second->value));
    const auto& capsObj = std::get<JSONValue::Object>(capIt->second->value);
    EXPECT_TRUE(capsObj.find("sampling") == capsObj.end());
}

TEST(ServerSampling, AdvertisesSamplingWithHandler) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");

    // Set sampling handler so capability is advertised
    server.SetSamplingHandler([](const JSONValue& messages,
                                 const JSONValue& modelPreferences,
                                 const JSONValue& systemPrompt,
                                 const JSONValue& includeContext){
        (void)messages; (void)modelPreferences; (void)systemPrompt; (void)includeContext;
        return std::async(std::launch::deferred, [](){ return JSONValue{JSONValue::Object{}}; });
    });

    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::Initialize;
    req->params.emplace(JSONValue::Object{});
    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    ASSERT_TRUE(resp->result.has_value());
    const auto& res = resp->result.value();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(res.value));
    const auto& ro = std::get<JSONValue::Object>(res.value);
    auto capIt = ro.find("capabilities");
    ASSERT_TRUE(capIt != ro.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(capIt->second->value));
    const auto& capsObj = std::get<JSONValue::Object>(capIt->second->value);
    EXPECT_TRUE(capsObj.find("sampling") != capsObj.end());
}

