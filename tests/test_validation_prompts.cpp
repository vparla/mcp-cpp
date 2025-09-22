//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_validation_prompts.cpp
// Purpose: Strict validation tests for prompts/get (server and client paths)
//==========================================================================================================

#include <gtest/gtest.h>
#include <future>
#include <chrono>

#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"
#include "mcp/typed/ClientTyped.h"

using namespace mcp;

// Server Strict: invalid handler result yields InternalError
TEST(ValidationPrompts, ServerStrict_InvalidHandlerResult_ReturnsError) {
    auto pair = InMemoryTransport::CreatePair();
    auto rawClient = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    server.SetValidationMode(mcp::validation::ValidationMode::Strict);

    // Prompt handler returns invalid typed result (message missing required fields)
    server.RegisterPrompt("bad", [](const JSONValue&) -> GetPromptResult {
        GetPromptResult r; r.description = "desc";
        // Invalid content item (missing type/text)
        r.messages.push_back(JSONValue{JSONValue::Object{}});
        return r;
    });

    server.Start(std::move(serverTrans)).get();
    rawClient->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::GetPrompt;
    JSONValue::Object params; params["name"] = std::make_shared<JSONValue>(std::string("bad"));
    params["arguments"] = std::make_shared<JSONValue>(JSONValue::Object{});
    req->params.emplace(params);

    auto fut = rawClient->SendRequest(std::move(req));
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_TRUE(resp->IsError());
    const auto& err = resp->error.value();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(err.value));
    const auto& eo = std::get<JSONValue::Object>(err.value);
    auto itCode = eo.find("code");
    ASSERT_TRUE(itCode != eo.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(itCode->second->value));
    EXPECT_EQ(static_cast<int>(std::get<int64_t>(itCode->second->value)), JSONRPCErrorCodes::InternalError);
}

// Client Strict: invalid JSON result shape from server causes typed wrapper to throw
TEST(ValidationPrompts, ClientStrict_InvalidJsonShape_Throws) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Manual server-side request handler returning invalid prompts/get JSON shape
    serverTrans->SetRequestHandler([&](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Initialize) {
            JSONValue::Object resultObj;
            resultObj["protocolVersion"] = std::make_shared<JSONValue>(PROTOCOL_VERSION);
            JSONValue::Object caps; resultObj["capabilities"] = std::make_shared<JSONValue>(JSONValue{caps});
            JSONValue::Object si; si["name"] = std::make_shared<JSONValue>(std::string("t")); si["version"] = std::make_shared<JSONValue>(std::string("1"));
            resultObj["serverInfo"] = std::make_shared<JSONValue>(JSONValue{si});
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id; resp->result = JSONValue{resultObj};
            return resp;
        }
        if (req.method == Methods::GetPrompt) {
            // Invalid: missing required 'messages' array
            JSONValue::Object resultObj; resultObj["description"] = std::make_shared<JSONValue>(std::string("d"));
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id; resp->result = JSONValue{resultObj};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    serverTrans->Start().get();

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    client->Connect(std::move(clientTrans)).get();
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();
    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    // Typed wrapper should throw due to invalid JSON shape
    JSONValue args; EXPECT_THROW({ auto fut = typed::getPrompt(*client, "any", args); (void)fut.get(); }, std::runtime_error);
}
