//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_inmemory_transport.cpp
// Purpose: InMemoryTransport basic tests
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Transport.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/JSONRPCTypes.h"
#include "mcp/Protocol.h"
#include <future>
#include <chrono>

using namespace mcp;

TEST(InMemoryTransport, RequestResponseRoutes) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto server = std::move(pair.second);

    // Server: handle any request with { message: "ok" }
    server->SetRequestHandler([](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        (void)req;
        auto resp = std::make_unique<JSONRPCResponse>();
        JSONValue::Object obj; obj["message"] = std::make_shared<JSONValue>(std::string("ok"));
        resp->result = JSONValue{obj};
        return resp;
    });

    // Start both ends
    client->Start().get();
    server->Start().get();

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

TEST(InMemoryTransport, ErrorWhenPeerDisconnected) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto server = std::move(pair.second);

    client->Start().get();
    server->Start().get();

    // Disconnect server first
    server->Close().get();

    // Attempt to send request should immediately return error response
    auto req = std::make_unique<JSONRPCRequest>();
    req->method = "test/any";
    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_TRUE(resp->IsError());

    client->Close().get();
}

TEST(InMemoryTransport, NotificationRouting) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto server = std::move(pair.second);

    std::promise<std::string> methodPromise;
    auto methodFuture = methodPromise.get_future();

    server->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        methodPromise.set_value(note->method);
    });

    client->Start().get();
    server->Start().get();

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

TEST(InMemoryTransport, PendingRequestsFailOnClose) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto server = std::move(pair.second);

    // Do not set a request handler on server, so client's request stays pending
    client->Start().get();
    server->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->method = "test/wait";
    auto fut = client->SendRequest(std::move(req));

    // Close client to fail pending requests
    client->Close().get();

    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_TRUE(resp->IsError());

    server->Close().get();
}
