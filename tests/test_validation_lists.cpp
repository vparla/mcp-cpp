//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_validation_lists.cpp
// Purpose: Strict validation negative-path tests for list endpoints (tools/resources/prompts/templates)
//==========================================================================================================

#include <gtest/gtest.h>
#include <future>
#include <chrono>
#include <optional>

#include "mcp/Client.h"
#include "mcp/Protocol.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/validation/Validation.h"

using namespace mcp;

namespace {

// Helper: minimal valid initialize response
static std::unique_ptr<JSONRPCResponse> makeInitializeResponse(const JSONRPCId& id) {
    JSONValue::Object resultObj;
    resultObj["protocolVersion"] = std::make_shared<JSONValue>(PROTOCOL_VERSION);
    JSONValue::Object caps; // advertise nothing for this test
    resultObj["capabilities"] = std::make_shared<JSONValue>(JSONValue{caps});
    JSONValue::Object serverInfo; serverInfo["name"] = std::make_shared<JSONValue>(std::string("Test")); serverInfo["version"] = std::make_shared<JSONValue>(std::string("1.0"));
    resultObj["serverInfo"] = std::make_shared<JSONValue>(JSONValue{serverInfo});
    auto resp = std::make_unique<JSONRPCResponse>();
    resp->id = id;
    resp->result = JSONValue{resultObj};
    return resp;
}

} // namespace

TEST(ValidationLists, ToolsList_InvalidShape_Throws) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Server-side manual request handler that returns invalid tools/list shape
    serverTrans->SetRequestHandler([&](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Initialize) {
            return makeInitializeResponse(req.id);
        }
        if (req.method == Methods::ListTools) {
            JSONValue::Object resultObj;
            // Invalid: tool item missing required 'description'
            JSONValue::Array tools;
            JSONValue::Object t; t["name"] = std::make_shared<JSONValue>(std::string("t1"));
            tools.push_back(std::make_shared<JSONValue>(t));
            resultObj["tools"] = std::make_shared<JSONValue>(tools);
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = JSONValue{resultObj};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    // Start both ends
    serverTrans->Start().get();

    ClientFactory f; Implementation ci{"ListTest","1.0"};
    auto client = f.CreateClient(ci);
    client->Connect(std::move(clientTrans)).get();
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();
    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    EXPECT_THROW(client->ListTools().get(), std::runtime_error);
}

TEST(ValidationLists, ResourcesList_InvalidShape_Throws) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    serverTrans->SetRequestHandler([&](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Initialize) return makeInitializeResponse(req.id);
        if (req.method == Methods::ListResources) {
            JSONValue::Object resultObj;
            JSONValue::Array resources;
            // Invalid: resource item missing required 'uri'
            JSONValue::Object r; r["name"] = std::make_shared<JSONValue>(std::string("n1"));
            resources.push_back(std::make_shared<JSONValue>(r));
            resultObj["resources"] = std::make_shared<JSONValue>(resources);
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = JSONValue{resultObj};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    serverTrans->Start().get();

    ClientFactory f; Implementation ci{"ListTest","1.0"};
    auto client = f.CreateClient(ci);
    client->Connect(std::move(clientTrans)).get();
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();
    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    EXPECT_THROW(client->ListResources().get(), std::runtime_error);
}

TEST(ValidationLists, ResourceTemplatesList_InvalidShape_Throws) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    serverTrans->SetRequestHandler([&](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Initialize) return makeInitializeResponse(req.id);
        if (req.method == Methods::ListResourceTemplates) {
            JSONValue::Object resultObj;
            JSONValue::Array arr;
            // Invalid: template item missing required 'name'
            JSONValue::Object t; t["uriTemplate"] = std::make_shared<JSONValue>(std::string("mem://{x}"));
            arr.push_back(std::make_shared<JSONValue>(t));
            resultObj["resourceTemplates"] = std::make_shared<JSONValue>(arr);
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = JSONValue{resultObj};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    serverTrans->Start().get();

    ClientFactory f; Implementation ci{"ListTest","1.0"};
    auto client = f.CreateClient(ci);
    client->Connect(std::move(clientTrans)).get();
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();
    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    EXPECT_THROW(client->ListResourceTemplates().get(), std::runtime_error);
}

TEST(ValidationLists, PromptsList_InvalidShape_Throws) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    serverTrans->SetRequestHandler([&](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Initialize) return makeInitializeResponse(req.id);
        if (req.method == Methods::ListPrompts) {
            JSONValue::Object resultObj;
            JSONValue::Array arr;
            // Invalid: prompt item missing required 'description'
            JSONValue::Object p; p["name"] = std::make_shared<JSONValue>(std::string("p1"));
            arr.push_back(std::make_shared<JSONValue>(p));
            resultObj["prompts"] = std::make_shared<JSONValue>(arr);
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = JSONValue{resultObj};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    serverTrans->Start().get();

    ClientFactory f; Implementation ci{"ListTest","1.0"};
    auto client = f.CreateClient(ci);
    client->Connect(std::move(clientTrans)).get();
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();
    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    EXPECT_THROW(client->ListPrompts().get(), std::runtime_error);
}
