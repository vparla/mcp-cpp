//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_validation_sampling.cpp
// Purpose: Strict validation negative-path tests for sampling/createMessage (client and server)
//==========================================================================================================

#include <gtest/gtest.h>
#include <future>
#include <chrono>

#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"

using namespace mcp;

//------------------------------ Helpers ------------------------------
static JSONValue makeInvalidSamplingParamsMessage() {
    // { role: "user", content: "hi" } -> invalid because content must be an array of content items
    JSONValue::Object msg;
    msg["role"] = std::make_shared<JSONValue>(std::string("user"));
    msg["content"] = std::make_shared<JSONValue>(std::string("hi"));
    return JSONValue{msg};
}

static JSONValue makeInvalidSamplingResult() {
    // Missing required 'content' array (use wrong type)
    JSONValue::Object obj;
    obj["model"] = std::make_shared<JSONValue>(std::string("m"));
    obj["role"] = std::make_shared<JSONValue>(std::string("assistant"));
    obj["content"] = std::make_shared<JSONValue>(std::string("oops"));
    return JSONValue{obj};
}

//------------------------------ Tests (server->client path) ------------------------------
// Client Strict: invalid params from server yields InvalidParams error to server
TEST(ValidationSampling, ClientStrict_InvalidParamsFromServer_ReturnsError) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    server.Start(std::move(serverTrans)).get();

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    client->Connect(std::move(clientTrans)).get();
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    // Register a valid sampling handler (won't be reached due to invalid params)
    client->SetSamplingHandler([](const JSONValue&, const JSONValue&, const JSONValue&, const JSONValue&){
        return std::async(std::launch::deferred, [](){
            JSONValue::Object resultObj;
            resultObj["model"] = std::make_shared<JSONValue>(std::string("ok"));
            resultObj["role"] = std::make_shared<JSONValue>(std::string("assistant"));
            JSONValue::Array contentArr; JSONValue::Object text;
            text["type"] = std::make_shared<JSONValue>(std::string("text"));
            text["text"] = std::make_shared<JSONValue>(std::string("hi"));
            contentArr.push_back(std::make_shared<JSONValue>(text));
            resultObj["content"] = std::make_shared<JSONValue>(contentArr);
            return JSONValue{resultObj};
        });
    });

    // Enable Strict on client
    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    // Server requests client to create a message with invalid params
    CreateMessageParams p; p.messages = { makeInvalidSamplingParamsMessage() };
    auto fut = server.RequestCreateMessage(p);
    auto val = fut.get();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(val.value));
    const auto& o = std::get<JSONValue::Object>(val.value);
    auto itCode = o.find("code");
    ASSERT_TRUE(itCode != o.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(itCode->second->value));
    EXPECT_EQ(static_cast<int>(std::get<int64_t>(itCode->second->value)), JSONRPCErrorCodes::InvalidParams);
}

// Client Strict: invalid result from client handler becomes InternalError to server
TEST(ValidationSampling, ClientStrict_InvalidResultFromHandler_ReturnsError) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    server.Start(std::move(serverTrans)).get();

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    client->Connect(std::move(clientTrans)).get();
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    // Sampling handler returns invalid result shape
    client->SetSamplingHandler([](const JSONValue&, const JSONValue&, const JSONValue&, const JSONValue&){
        return std::async(std::launch::deferred, [](){ return makeInvalidSamplingResult(); });
    });

    CreateMessageParams p; // valid messages (empty OK -> messages == [])? messages must be an array; keep empty
    auto fut = server.RequestCreateMessage(p);
    auto val = fut.get();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(val.value));
    const auto& o = std::get<JSONValue::Object>(val.value);
    auto itCode = o.find("code");
    ASSERT_TRUE(itCode != o.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(itCode->second->value));
    EXPECT_EQ(static_cast<int>(std::get<int64_t>(itCode->second->value)), JSONRPCErrorCodes::InternalError);
}

//------------------------------ Tests (client->server path) ------------------------------
// Server Strict: invalid params from client yields InvalidParams
TEST(ValidationSampling, ServerStrict_InvalidParamsFromClient_ReturnsError) {
    auto pair = InMemoryTransport::CreatePair();
    auto rawClient = std::move(pair.first); // direct JSON-RPC client
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    server.SetValidationMode(mcp::validation::ValidationMode::Strict);

    // Sampling handler returns a valid result (not reached for invalid params)
    server.SetSamplingHandler([](const JSONValue&, const JSONValue&, const JSONValue&, const JSONValue&){
        return std::async(std::launch::deferred, [](){
            JSONValue::Object resultObj; JSONValue::Array content;
            resultObj["model"] = std::make_shared<JSONValue>(std::string("m"));
            resultObj["role"] = std::make_shared<JSONValue>(std::string("assistant"));
            JSONValue::Object t; t["type"] = std::make_shared<JSONValue>(std::string("text")); t["text"] = std::make_shared<JSONValue>(std::string("ok"));
            content.push_back(std::make_shared<JSONValue>(t));
            resultObj["content"] = std::make_shared<JSONValue>(content);
            return JSONValue{resultObj};
        });
    });

    server.Start(std::move(serverTrans)).get();
    rawClient->Start().get();

    // Send CreateMessage with invalid params
    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::CreateMessage;
    JSONValue::Object params;
    JSONValue::Array msgs; msgs.push_back(std::make_shared<JSONValue>(makeInvalidSamplingParamsMessage()));
    params["messages"] = std::make_shared<JSONValue>(msgs);
    req->params.emplace(params);

    auto fut = rawClient->SendRequest(std::move(req));
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_TRUE(resp->IsError());
    ASSERT_TRUE(resp->error.has_value());
    const auto& err = resp->error.value();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(err.value));
    const auto& eo = std::get<JSONValue::Object>(err.value);
    auto itCode = eo.find("code");
    ASSERT_TRUE(itCode != eo.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(itCode->second->value));
    EXPECT_EQ(static_cast<int>(std::get<int64_t>(itCode->second->value)), JSONRPCErrorCodes::InvalidParams);
}

// Server Strict: invalid handler result yields InternalError
TEST(ValidationSampling, ServerStrict_InvalidHandlerResult_ReturnsError) {
    auto pair = InMemoryTransport::CreatePair();
    auto rawClient = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    server.SetValidationMode(mcp::validation::ValidationMode::Strict);

    // Sampling handler returns invalid result
    server.SetSamplingHandler([](const JSONValue&, const JSONValue&, const JSONValue&, const JSONValue&){
        return std::async(std::launch::deferred, [](){ return makeInvalidSamplingResult(); });
    });

    server.Start(std::move(serverTrans)).get();
    rawClient->Start().get();

    // Send CreateMessage with valid params (empty messages acceptable)
    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::CreateMessage;
    JSONValue::Object params; params["messages"] = std::make_shared<JSONValue>(JSONValue::Array{});
    req->params.emplace(params);

    auto fut = rawClient->SendRequest(std::move(req));
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_TRUE(resp->IsError());
    ASSERT_TRUE(resp->error.has_value());
    const auto& err = resp->error.value();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(err.value));
    const auto& eo = std::get<JSONValue::Object>(err.value);
    auto itCode = eo.find("code");
    ASSERT_TRUE(itCode != eo.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(itCode->second->value));
    EXPECT_EQ(static_cast<int>(std::get<int64_t>(itCode->second->value)), JSONRPCErrorCodes::InternalError);
}
