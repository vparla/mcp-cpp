//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_shared_memory_transport.cpp
// Purpose: SharedMemoryTransport basic tests
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Transport.h"
#include "mcp/SharedMemoryTransport.hpp"
#include "mcp/JSONRPCTypes.h"
#include "mcp/Protocol.h"
#include <future>
#include <chrono>

using namespace mcp;

static SharedMemoryTransport::Options makeServerOpts(const std::string& name) {
    SharedMemoryTransport::Options o;
    o.channelName = name;
    o.create = true;
    // Keep under Docker default /dev/shm (64MB). Two queues are created per channel.
    // 64 KiB * 64 = 4 MiB per queue => ~8 MiB total for both queues, leaving ample headroom.
    o.maxMessageSize = 64 * 1024;   // 64 KiB
    o.maxMessageCount = 64;
    return o;
}

static SharedMemoryTransport::Options makeClientOpts(const std::string& name) {
    SharedMemoryTransport::Options o;
    o.channelName = name;
    o.create = false;
    return o;
}

TEST(SharedMemoryTransport, RequestResponseRoutes) {
    const std::string chan = "test-shm-reqresp";
    auto server = std::make_unique<SharedMemoryTransport>(makeServerOpts(chan));
    auto client = std::make_unique<SharedMemoryTransport>(makeClientOpts(chan));

    // Server: handle any request with { message: "ok" }
    server->SetRequestHandler([](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        (void)req;
        auto resp = std::make_unique<JSONRPCResponse>();
        JSONValue::Object obj; obj["message"] = std::make_shared<JSONValue>(std::string("ok"));
        resp->result = JSONValue{obj};
        return resp;
    });

    // Start both ends
    server->Start().get();
    client->Start().get();

    // Client sends request
    auto req = std::make_unique<JSONRPCRequest>();
    req->method = "test/echo";
    req->params.emplace(JSONValue::Object{});

    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    ASSERT_TRUE(resp->result.has_value());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(resp->result->value));
    const auto& obj = std::get<JSONValue::Object>(resp->result->value);
    auto it = obj.find("message");
    ASSERT_TRUE(it != obj.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(it->second->value));
    EXPECT_EQ(std::get<std::string>(it->second->value), "ok");

    // Close
    client->Close().get();
    server->Close().get();
}

TEST(SharedMemoryTransport, ErrorWhenPeerDisconnected) {
    const std::string chan = "test-shm-peer-disconnect";
    auto server = std::make_unique<SharedMemoryTransport>(makeServerOpts(chan));
    auto client = std::make_unique<SharedMemoryTransport>(makeClientOpts(chan));

    server->Start().get();
    client->Start().get();

    // Disconnect server first
    server->Close().get();

    // Attempt to send request should complete and indicate error response
    auto req = std::make_unique<JSONRPCRequest>();
    req->method = "test/any";
    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_TRUE(resp->IsError());

    client->Close().get();
}

TEST(SharedMemoryTransport, NotificationRouting) {
    const std::string chan = "test-shm-notify";
    auto server = std::make_unique<SharedMemoryTransport>(makeServerOpts(chan));
    auto client = std::make_unique<SharedMemoryTransport>(makeClientOpts(chan));

    std::promise<std::string> methodPromise;
    auto methodFuture = methodPromise.get_future();

    server->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        methodPromise.set_value(note->method);
    });

    server->Start().get();
    client->Start().get();

    auto n = std::make_unique<JSONRPCNotification>();
    n->method = "notify/ping";
    JSONValue::Object obj; obj["x"] = std::make_shared<JSONValue>(static_cast<int64_t>(1));
    n->params.emplace(obj);
    client->SendNotification(std::move(n)).get();

    ASSERT_EQ(methodFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(methodFuture.get(), std::string("notify/ping"));

    client->Close().get();
    server->Close().get();
}
