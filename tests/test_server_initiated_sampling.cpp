//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_server_initiated_sampling.cpp
// Purpose: E2E test for server-initiated sampling (server -> client createMessage)
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/Protocol.h"
#include "mcp/InMemoryTransport.hpp"
#include <future>
#include <chrono>

using namespace mcp;

TEST(ServerInitiatedSampling, ClientHandlesCreateMessage) {
    // Create transport pair
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Create server
    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();

    // Create client and connect
    ClientFactory factory; Implementation info{"TestClient","1.0.0"};
    auto client = factory.CreateClient(info);
    client->Connect(std::move(clientTrans)).get();

    // Register client sampling handler
    client->SetSamplingHandler([](const JSONValue& messages,
                                  const JSONValue& modelPreferences,
                                  const JSONValue& systemPrompt,
                                  const JSONValue& includeContext){
        (void)messages; (void)modelPreferences; (void)systemPrompt; (void)includeContext;
        return std::async(std::launch::deferred, [](){
            JSONValue::Object resultObj;
            resultObj["model"] = std::make_shared<JSONValue>(std::string("unit-model"));
            resultObj["role"] = std::make_shared<JSONValue>(std::string("assistant"));
            JSONValue::Array contentArr;
            JSONValue::Object textContent;
            textContent["type"] = std::make_shared<JSONValue>(std::string("text"));
            textContent["text"] = std::make_shared<JSONValue>(std::string("ok"));
            contentArr.push_back(std::make_shared<JSONValue>(textContent));
            resultObj["content"] = std::make_shared<JSONValue>(contentArr);
            return JSONValue{resultObj};
        });
    });

    // Server requests client to create a message
    CreateMessageParams params;
    params.messages = { JSONValue{JSONValue::Object{}} };
    auto fut = server.RequestCreateMessage(params);

    auto result = fut.get();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(result.value));
    const auto& obj = std::get<JSONValue::Object>(result.value);
    auto it = obj.find("model");
    ASSERT_TRUE(it != obj.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(it->second->value));
    EXPECT_EQ(std::get<std::string>(it->second->value), "unit-model");

    // Also assert role and content shape
    auto itRole = obj.find("role");
    ASSERT_TRUE(itRole != obj.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(itRole->second->value));
    EXPECT_EQ(std::get<std::string>(itRole->second->value), "assistant");

    auto itContent = obj.find("content");
    ASSERT_TRUE(itContent != obj.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Array>(itContent->second->value));
    const auto& arr = std::get<JSONValue::Array>(itContent->second->value);
    ASSERT_FALSE(arr.empty());
    ASSERT_TRUE(arr[0] != nullptr);
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(arr[0]->value));
    const auto& c0 = std::get<JSONValue::Object>(arr[0]->value);
    auto tIt = c0.find("type");
    auto txtIt = c0.find("text");
    ASSERT_TRUE(tIt != c0.end());
    ASSERT_TRUE(txtIt != c0.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(tIt->second->value));
    ASSERT_TRUE(std::holds_alternative<std::string>(txtIt->second->value));
    EXPECT_EQ(std::get<std::string>(tIt->second->value), "text");
    EXPECT_EQ(std::get<std::string>(txtIt->second->value), "ok");

    // Cleanup
    server.Stop().get();
}
