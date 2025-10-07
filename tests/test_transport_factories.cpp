//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_transport_factories.cpp
// Purpose: Validate client-side transport factories create working transports
//==========================================================================================================

#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <memory>
#include <string>

#include "mcp/JSONRPCTypes.h"
#include "mcp/Transport.h"
#include "mcp/HTTPTransport.hpp"
#include "mcp/StdioTransport.hpp"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/SharedMemoryTransport.hpp"

using namespace mcp;

TEST(TransportFactories, HTTPTransportFactory_StartClose) {
    HTTPTransportFactory factory;
    // Use a simple semicolon-delimited config per implementation docs
    const std::string cfg = "scheme=https;host=localhost;port=9443;rpcPath=/mcp/rpc;notifyPath=/mcp/notify;serverName=localhost";
    auto t = factory.CreateTransport(cfg);
    ASSERT_NE(t, nullptr);
    // Start/Close do not perform a network connect; safe without server
    EXPECT_NO_THROW({ t->Start().get(); });
    EXPECT_TRUE(t->IsConnected());
    EXPECT_NO_THROW({ t->Close().get(); });
}

TEST(TransportFactories, StdioTransportFactory_CreateStartClose) {
    StdioTransportFactory factory;
    // Config supports timeout_ms; use very small value
    auto t = factory.CreateTransport("timeout_ms=10");
    ASSERT_NE(t, nullptr);
    // Start spawns reader; Close will wake and join quickly
    EXPECT_NO_THROW({ t->Start().get(); });
    EXPECT_TRUE(t->IsConnected());
    EXPECT_NO_THROW({ t->Close().get(); });
}

TEST(TransportFactories, InMemoryTransportFactory_StartClose) {
    InMemoryTransportFactory factory;
    auto t = factory.CreateTransport("");
    ASSERT_NE(t, nullptr);
    EXPECT_NO_THROW({ t->Start().get(); });
    EXPECT_TRUE(t->IsConnected());
    EXPECT_NO_THROW({ t->Close().get(); });
}

TEST(TransportFactories, SharedMemoryTransportFactory_Roundtrip) {
    // Unique channel per test run
    const std::string channel = "test-shm-factory-roundtrip";

    SharedMemoryTransportFactory factory;
    auto server = factory.CreateTransport("shm://" + channel + "?create=true&maxSize=262144&maxCount=64");
    auto client = factory.CreateTransport("shm://" + channel);

    ASSERT_NE(server, nullptr);
    ASSERT_NE(client, nullptr);

    // Server handles a simple request with { ok: true }
    server->SetRequestHandler([](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        (void)req;
        auto resp = std::make_unique<JSONRPCResponse>();
        JSONValue::Object obj;
        obj["ok"] = std::make_shared<JSONValue>(true);
        resp->result = JSONValue{obj};
        return resp;
    });

    // Start server first so client open_only succeeds
    EXPECT_NO_THROW({ server->Start().get(); });
    EXPECT_NO_THROW({ client->Start().get(); });

    // Request/response
    auto req = std::make_unique<JSONRPCRequest>();
    req->method = "ping";
    req->params.emplace(JSONValue::Object{});

    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    ASSERT_TRUE(resp->result.has_value());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(resp->result->value));
    const auto& obj = std::get<JSONValue::Object>(resp->result->value);
    auto it = obj.find("ok");
    ASSERT_TRUE(it != obj.end());
    ASSERT_TRUE(std::holds_alternative<bool>(it->second->value));
    EXPECT_TRUE(std::get<bool>(it->second->value));

    EXPECT_NO_THROW({ client->Close().get(); });
    EXPECT_NO_THROW({ server->Close().get(); });
}
