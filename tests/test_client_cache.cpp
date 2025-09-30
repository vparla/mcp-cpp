//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_client_cache.cpp
// Purpose: GoogleTests for client-side listings cache with TTL and list_changed invalidation
//==========================================================================================================

#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <optional>
#include <atomic>

#include "mcp/InMemoryTransport.hpp"
#include "mcp/Client.h"
#include "mcp/Protocol.h"

using namespace mcp;

namespace {

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

TEST(ClientCache, ListTools_CacheHitAndInvalidate) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::atomic<unsigned int> listCalls{0};

    // Server handler for requests
    serverTrans->SetRequestHandler([&](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Initialize) {
            return makeInitializeResponse(req.id);
        }
        if (req.method == Methods::ListTools) {
            listCalls.fetch_add(1);
            JSONValue::Object resultObj;
            JSONValue::Array tools;
            JSONValue::Object t; t["name"] = std::make_shared<JSONValue>(std::string("t1")); t["description"] = std::make_shared<JSONValue>(std::string("desc"));
            tools.push_back(std::make_shared<JSONValue>(t));
            resultObj["tools"] = std::make_shared<JSONValue>(tools);
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = JSONValue{resultObj};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    ASSERT_NO_THROW(serverTrans->Start().get());

    ClientFactory f; Implementation ci{"CacheClient","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; ASSERT_NO_THROW((void)client->Initialize(ci, caps).get());

    // Enable caching with long TTL
    client->SetListingsCacheTtlMs(5000);

    // First request -> server call
    auto v1 = client->ListTools().get();
    EXPECT_EQ(listCalls.load(), 1u);
    ASSERT_EQ(v1.size(), 1u);
    EXPECT_EQ(v1[0].name, std::string("t1"));

    // Second request within TTL -> cache hit (no new server call)
    auto v2 = client->ListTools().get();
    EXPECT_EQ(listCalls.load(), 1u);
    ASSERT_EQ(v2.size(), 1u);

    // Invalidate via notification
    {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::ToolListChanged;
        (void) serverTrans->SendNotification(std::move(n)).get();
        // Allow the client's notification processing thread to handle invalidation
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Next request -> server call again
    auto v3 = client->ListTools().get();
    EXPECT_EQ(listCalls.load(), 2u);
    ASSERT_EQ(v3.size(), 1u);
}

TEST(ClientCache, ListTools_CacheDisabled_Default) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::atomic<unsigned int> listCalls{0};

    serverTrans->SetRequestHandler([&](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Initialize) {
            return makeInitializeResponse(req.id);
        }
        if (req.method == Methods::ListTools) {
            listCalls.fetch_add(1);
            JSONValue::Object resultObj; JSONValue::Array tools; JSONValue::Object t; t["name"] = std::make_shared<JSONValue>(std::string("t1")); t["description"] = std::make_shared<JSONValue>(std::string("d")); tools.push_back(std::make_shared<JSONValue>(t)); resultObj["tools"] = std::make_shared<JSONValue>(tools);
            auto resp = std::make_unique<JSONRPCResponse>(); resp->id = req.id; resp->result = JSONValue{resultObj}; return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    ASSERT_NO_THROW(serverTrans->Start().get());

    ClientFactory f; Implementation ci{"CacheClient","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; ASSERT_NO_THROW((void)client->Initialize(ci, caps).get());

    // Default: caching disabled
    auto v1 = client->ListTools().get();
    auto v2 = client->ListTools().get();
    EXPECT_EQ(listCalls.load(), 2u);
    ASSERT_EQ(v1.size(), 1u);
    ASSERT_EQ(v2.size(), 1u);
}
