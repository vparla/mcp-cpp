//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_tools_inputschema.cpp
// Purpose: Tools inputSchema end-to-end tests
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/Transport.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include <chrono>
#include <stop_token>

using namespace mcp;

namespace {
std::future<ToolResult> sampleEchoHandler(const JSONValue& arguments, std::stop_token st) {
    (void)st;
    return std::async(std::launch::async, [arguments]() mutable {
        ToolResult tr;
        JSONValue::Object contentObj;
        contentObj["type"] = std::make_shared<JSONValue>(std::string("text"));
        contentObj["text"] = std::make_shared<JSONValue>(std::string("ok"));
        tr.content.push_back(JSONValue{contentObj});
        tr.isError = false;
        (void)arguments;
        return tr;
    });
}

bool hasRequiredField(const JSONValue& schema, const std::string& fieldName) {
    if (!std::holds_alternative<JSONValue::Object>(schema.value)) return false;
    const auto& obj = std::get<JSONValue::Object>(schema.value);
    auto reqIt = obj.find("required");
    if (reqIt == obj.end() || !std::holds_alternative<JSONValue::Array>(reqIt->second->value)) return false;
    const auto& arr = std::get<JSONValue::Array>(reqIt->second->value);
    for (const auto& v : arr) {
        if (v && std::holds_alternative<std::string>(v->value)) {
            if (std::get<std::string>(v->value) == fieldName) return true;
        }
    }
    return false;
}
}

TEST(ServerToolsInputSchema, ListIncludesProvidedSchema) {
    // Create in-memory transport pair (client/server)
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    // Prepare server and register one tool with metadata
    Server server("MCP Test Server");
    // Build echo schema: { type: object, properties: { message: { type: string } }, required: ["message"] }
    JSONValue::Object msgType;
    msgType["type"] = std::make_shared<JSONValue>(std::string("string"));
    JSONValue::Object props;
    props["message"] = std::make_shared<JSONValue>(JSONValue{msgType});
    JSONValue::Array required;
    required.push_back(std::make_shared<JSONValue>(std::string("message")));
    JSONValue::Object schema;
    schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    schema["properties"] = std::make_shared<JSONValue>(JSONValue{props});
    schema["required"] = std::make_shared<JSONValue>(JSONValue{required});

    Tool echoMeta{"echo", "Echoes back the provided message", JSONValue{schema}};
    server.RegisterTool(echoMeta, sampleEchoHandler);

    // Start server
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    // Create and connect client
    ClientFactory factory;
    Implementation clientInfo{"MCP Test Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    // Initialize client
    ClientCapabilities caps; caps.sampling = SamplingCapability{}; // include sampling to exercise init path
    auto initFut = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    // List tools and verify schema
    auto listFut = client->ListTools();
    ASSERT_EQ(listFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto tools = listFut.get();

    bool foundEcho = false;
    for (const auto& t : tools) {
        if (t.name == "echo") {
            foundEcho = true;
            // description
            EXPECT_EQ(t.description, std::string("Echoes back the provided message"));
            // inputSchema checks
            ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(t.inputSchema.value));
            const auto& sch = std::get<JSONValue::Object>(t.inputSchema.value);
            auto typeIt = sch.find("type");
            ASSERT_TRUE(typeIt != sch.end());
            ASSERT_TRUE(std::holds_alternative<std::string>(typeIt->second->value));
            EXPECT_EQ(std::get<std::string>(typeIt->second->value), std::string("object"));
            auto propsIt = sch.find("properties");
            ASSERT_TRUE(propsIt != sch.end());
            ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(propsIt->second->value));
            const auto& propsObj = std::get<JSONValue::Object>(propsIt->second->value);
            auto msgIt = propsObj.find("message");
            ASSERT_TRUE(msgIt != propsObj.end());
            ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(msgIt->second->value));
            const auto& msgObj = std::get<JSONValue::Object>(msgIt->second->value);
            auto mtIt = msgObj.find("type");
            ASSERT_TRUE(mtIt != msgObj.end());
            ASSERT_TRUE(std::holds_alternative<std::string>(mtIt->second->value));
            EXPECT_EQ(std::get<std::string>(mtIt->second->value), std::string("string"));
            // required includes message
            EXPECT_TRUE(hasRequiredField(t.inputSchema, "message"));
        }
    }
    EXPECT_TRUE(foundEcho);

    // Cleanup
    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
