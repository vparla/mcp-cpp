//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_prompts_get.cpp
// Purpose: Prompts/get end-to-end tests
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/Transport.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include <chrono>

using namespace mcp;

TEST(PromptsGet, ReturnsActualMessages) {
    // Create in-memory transport pair (client/server)
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    // Start server
    Server server("MCP Test Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    // Register a prompt that returns a real message object
    server.RegisterPrompt("greet", [](const JSONValue&) -> GetPromptResult {
        GetPromptResult pr;
        pr.description = "Greeting prompt";
        // message: { role: "user", content: { type: "text", text: "Hello" } }
        JSONValue::Object contentObj; contentObj["type"] = std::make_shared<JSONValue>(std::string("text"));
        contentObj["text"] = std::make_shared<JSONValue>(std::string("Hello"));
        JSONValue::Object messageObj; messageObj["role"] = std::make_shared<JSONValue>(std::string("user"));
        messageObj["content"] = std::make_shared<JSONValue>(JSONValue{contentObj});
        pr.messages.push_back(JSONValue{messageObj});
        return pr;
    });

    // Create and connect client
    ClientFactory factory;
    Implementation clientInfo{"MCP Test Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    // Initialize client
    ClientCapabilities caps; caps.sampling = SamplingCapability{};
    auto initFut = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    // Get the prompt
    JSONValue args{JSONValue::Object{}};
    auto getFut = client->GetPrompt("greet", args);
    ASSERT_EQ(getFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto res = getFut.get();

    // Validate result structure
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(res.value));
    const auto& obj = std::get<JSONValue::Object>(res.value);

    auto dIt = obj.find("description");
    ASSERT_TRUE(dIt != obj.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(dIt->second->value));
    EXPECT_EQ(std::get<std::string>(dIt->second->value), "Greeting prompt");

    auto mIt = obj.find("messages");
    ASSERT_TRUE(mIt != obj.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Array>(mIt->second->value));
    const auto& arr = std::get<JSONValue::Array>(mIt->second->value);
    ASSERT_FALSE(arr.empty());
    ASSERT_TRUE(arr[0] != nullptr);
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(arr[0]->value));
    const auto& msg0 = std::get<JSONValue::Object>(arr[0]->value);

    auto roleIt = msg0.find("role");
    ASSERT_TRUE(roleIt != msg0.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(roleIt->second->value));
    EXPECT_EQ(std::get<std::string>(roleIt->second->value), "user");

    auto contentIt = msg0.find("content");
    ASSERT_TRUE(contentIt != msg0.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(contentIt->second->value));
    const auto& cont = std::get<JSONValue::Object>(contentIt->second->value);

    auto typeIt = cont.find("type");
    ASSERT_TRUE(typeIt != cont.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(typeIt->second->value));
    EXPECT_EQ(std::get<std::string>(typeIt->second->value), "text");

    auto textIt = cont.find("text");
    ASSERT_TRUE(textIt != cont.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(textIt->second->value));
    EXPECT_EQ(std::get<std::string>(textIt->second->value), "Hello");

    // Cleanup
    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
