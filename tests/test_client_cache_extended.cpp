//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_client_cache_extended.cpp
// Purpose: Extended tests for client-side listings cache (resources/prompts/templates, TTL expiry)
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
    JSONValue::Object caps; // advertise nothing for these tests
    resultObj["capabilities"] = std::make_shared<JSONValue>(JSONValue{caps});
    JSONValue::Object serverInfo; serverInfo["name"] = std::make_shared<JSONValue>(std::string("Test")); serverInfo["version"] = std::make_shared<JSONValue>(std::string("1.0"));
    resultObj["serverInfo"] = std::make_shared<JSONValue>(JSONValue{serverInfo});
    auto resp = std::make_unique<JSONRPCResponse>();
    resp->id = id;
    resp->result = JSONValue{resultObj};
    return resp;
}

} // namespace

TEST(ClientCacheExtended, ListResources_CacheHitAndInvalidate) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::atomic<unsigned int> listCalls{0u};

    serverTrans->SetRequestHandler([&](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Initialize) return makeInitializeResponse(req.id);
        if (req.method == Methods::ListResources) {
            listCalls.fetch_add(1u);
            JSONValue::Object resultObj;
            JSONValue::Array resources;
            JSONValue::Object r; r["uri"] = std::make_shared<JSONValue>(std::string("r://1")); r["name"] = std::make_shared<JSONValue>(std::string("R1"));
            resources.push_back(std::make_shared<JSONValue>(r));
            resultObj["resources"] = std::make_shared<JSONValue>(resources);
            auto resp = std::make_unique<JSONRPCResponse>(); resp->id = req.id; resp->result = JSONValue{resultObj}; return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    ASSERT_NO_THROW(serverTrans->Start().get());

    ClientFactory f; Implementation ci{"CacheClient","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; ASSERT_NO_THROW((void)client->Initialize(ci, caps).get());

    client->SetListingsCacheTtlMs(5000);

    auto v1 = client->ListResources().get();
    EXPECT_EQ(listCalls.load(), 1u);
    ASSERT_EQ(v1.size(), 1u);

    auto v2 = client->ListResources().get();
    EXPECT_EQ(listCalls.load(), 1u);

    // Invalidate via notification
    {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::ResourceListChanged;
        (void) serverTrans->SendNotification(std::move(n)).get();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    auto v3 = client->ListResources().get();
    EXPECT_EQ(listCalls.load(), 2u);
    ASSERT_EQ(v3.size(), 1u);
}

TEST(ClientCacheExtended, ListPrompts_CacheHitAndInvalidate) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::atomic<unsigned int> listCalls{0u};

    serverTrans->SetRequestHandler([&](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Initialize) return makeInitializeResponse(req.id);
        if (req.method == Methods::ListPrompts) {
            listCalls.fetch_add(1u);
            JSONValue::Object resultObj;
            JSONValue::Array prompts;
            JSONValue::Object p; p["name"] = std::make_shared<JSONValue>(std::string("p1")); p["description"] = std::make_shared<JSONValue>(std::string("desc"));
            prompts.push_back(std::make_shared<JSONValue>(p));
            resultObj["prompts"] = std::make_shared<JSONValue>(prompts);
            auto resp = std::make_unique<JSONRPCResponse>(); resp->id = req.id; resp->result = JSONValue{resultObj}; return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    ASSERT_NO_THROW(serverTrans->Start().get());

    ClientFactory f; Implementation ci{"CacheClient","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; ASSERT_NO_THROW((void)client->Initialize(ci, caps).get());

    client->SetListingsCacheTtlMs(5000);

    auto v1 = client->ListPrompts().get();
    EXPECT_EQ(listCalls.load(), 1u);
    ASSERT_EQ(v1.size(), 1u);

    auto v2 = client->ListPrompts().get();
    EXPECT_EQ(listCalls.load(), 1u);

    // Invalidate via notification
    {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::PromptListChanged;
        (void) serverTrans->SendNotification(std::move(n)).get();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    auto v3 = client->ListPrompts().get();
    EXPECT_EQ(listCalls.load(), 2u);
    ASSERT_EQ(v3.size(), 1u);
}

TEST(ClientCacheExtended, ListTemplates_TtlExpiry) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::atomic<unsigned int> listCalls{0u};

    serverTrans->SetRequestHandler([&](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Initialize) return makeInitializeResponse(req.id);
        if (req.method == Methods::ListResourceTemplates) {
            listCalls.fetch_add(1u);
            JSONValue::Object resultObj;
            JSONValue::Array templates;
            JSONValue::Object t; t["uriTemplate"] = std::make_shared<JSONValue>(std::string("x://{id}")); t["name"] = std::make_shared<JSONValue>(std::string("X"));
            templates.push_back(std::make_shared<JSONValue>(t));
            resultObj["resourceTemplates"] = std::make_shared<JSONValue>(templates);
            auto resp = std::make_unique<JSONRPCResponse>(); resp->id = req.id; resp->result = JSONValue{resultObj}; return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    ASSERT_NO_THROW(serverTrans->Start().get());

    ClientFactory f; Implementation ci{"CacheClient","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; ASSERT_NO_THROW((void)client->Initialize(ci, caps).get());

    client->SetListingsCacheTtlMs(20); // very short TTL

    auto v1 = client->ListResourceTemplates().get();
    EXPECT_EQ(listCalls.load(), 1u);
    ASSERT_EQ(v1.size(), 1u);

    auto v2 = client->ListResourceTemplates().get();
    EXPECT_EQ(listCalls.load(), 1u); // within TTL -> cache hit

    std::this_thread::sleep_for(std::chrono::milliseconds(30)); // expire TTL

    auto v3 = client->ListResourceTemplates().get();
    EXPECT_EQ(listCalls.load(), 2u);
    ASSERT_EQ(v3.size(), 1u);
}
