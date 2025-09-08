//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_validation_strict.cpp
// Purpose: Strict validation negative-path tests for typed client wrappers
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"
#include "mcp/validation/Validation.h"

using namespace mcp;

static ToolResult makeBadToolResult() {
    ToolResult r; r.isError = false;
    // Invalid content shape for Strict: type != text and missing text field
    JSONValue::Object msg; msg["type"] = std::make_shared<JSONValue>(std::string("image"));
    r.content.push_back(JSONValue{msg});
    return r;
}

static ReadResourceResult makeBadResource() {
    ReadResourceResult out;
    JSONValue::Object msg; msg["type"] = std::make_shared<JSONValue>(std::string("image"));
    out.contents.push_back(JSONValue{msg});
    return out;
}

static GetPromptResult makeBadPrompt() {
    GetPromptResult out;
    out.description = "desc";
    JSONValue::Object msg; msg["type"] = std::make_shared<JSONValue>(std::string("image"));
    out.messages.push_back(JSONValue{msg});
    return out;
}

TEST(ValidationStrict, CallTool_InvalidShape_Throws) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Validation Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    // Register tool with metadata and bad result
    Tool bad; bad.name = "badTool"; bad.description = "returns invalid content";
    JSONValue::Object schema; schema["type"] = std::make_shared<JSONValue>(std::string("object")); bad.inputSchema = JSONValue{schema};
    server.RegisterTool(bad, [](const JSONValue&, std::stop_token){
        return std::async(std::launch::async, [](){ return makeBadToolResult(); });
    });

    ClientFactory f; Implementation ci{"Validation Client","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    EXPECT_THROW(typed::callTool(*client, "badTool", JSONValue{JSONValue::Object{}}).get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ValidationStrict, ReadResource_InvalidShape_Throws) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Validation Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    // Register resource that returns invalid contents entries under Strict
    server.RegisterResource("mem://bad", [](const std::string&, std::stop_token){
        return std::async(std::launch::async, [](){ return makeBadResource(); });
    });

    ClientFactory f; Implementation ci{"Validation Client","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    EXPECT_THROW(typed::readResource(*client, "mem://bad").get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ValidationStrict, GetPrompt_InvalidShape_Throws) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Validation Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    server.RegisterPrompt("badPrompt", [](const JSONValue&){
        return makeBadPrompt();
    });

    ClientFactory f; Implementation ci{"Validation Client","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    JSONValue args; // none
    EXPECT_THROW(typed::getPrompt(*client, "badPrompt", args).get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
