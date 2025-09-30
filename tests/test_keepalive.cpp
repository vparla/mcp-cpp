//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_keepalive.cpp
// Purpose: Tests for server keepalive/heartbeat notifications
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Transport.h"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"
#include <atomic>
#include <future>
#include <chrono>
#include <string>

using namespace mcp;

TEST(ServerKeepalive, SendsKeepaliveNotifications) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.SetKeepaliveIntervalMs(50); // 50ms cadence

    std::promise<void> gotOnePromise;
    auto gotOneFuture = gotOnePromise.get_future();
    std::atomic<unsigned int> count{0u};

    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (!note) {
            return;
        }
        if (note->method == Methods::Keepalive) {
            if (count.fetch_add(1u, std::memory_order_relaxed) == 0u) {
                gotOnePromise.set_value();
            }
        }
    });

    // Start server and client
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    ASSERT_EQ(gotOneFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Cleanup
    client->Close().get();
    server.Stop().get();
}
TEST(ServerKeepalive, AdvertisesKeepaliveCapability) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.SetKeepaliveIntervalMs(50);

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(client->Start().get());

    // Send initialize and inspect capabilities.experimental.keepalive
    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::Initialize;
    req->params.emplace(JSONValue::Object{});
    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    ASSERT_TRUE(resp->result.has_value());
    const auto& res = resp->result.value();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(res.value));
    const auto& ro = std::get<JSONValue::Object>(res.value);
    auto capIt = ro.find("capabilities");
    ASSERT_TRUE(capIt != ro.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(capIt->second->value));
    const auto& capsObj = std::get<JSONValue::Object>(capIt->second->value);
    auto expIt = capsObj.find("experimental");
    ASSERT_TRUE(expIt != capsObj.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(expIt->second->value));
    const auto& expObj = std::get<JSONValue::Object>(expIt->second->value);
    auto keepIt = expObj.find("keepalive");
    ASSERT_TRUE(keepIt != expObj.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(keepIt->second->value));
    const auto& kv = std::get<JSONValue::Object>(keepIt->second->value);
    auto enIt = kv.find("enabled");
    ASSERT_TRUE(enIt != kv.end());
    ASSERT_TRUE(std::holds_alternative<bool>(enIt->second->value));
    EXPECT_TRUE(std::get<bool>(enIt->second->value));
    auto intIt = kv.find("intervalMs");
    ASSERT_TRUE(intIt != kv.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(intIt->second->value));
    EXPECT_GE(std::get<int64_t>(intIt->second->value), static_cast<int64_t>(1));
    auto thrIt = kv.find("failureThreshold");
    ASSERT_TRUE(thrIt != kv.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(thrIt->second->value));
    EXPECT_GE(std::get<int64_t>(thrIt->second->value), static_cast<int64_t>(1));

    ASSERT_NO_THROW(client->Close().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ServerKeepalive, DisablingRemovesKeepaliveAdvertisement) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.SetKeepaliveIntervalMs(50);
    server.SetKeepaliveIntervalMs(std::optional<int>(0)); // disable

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(client->Start().get());

    auto req = std::make_unique<JSONRPCRequest>();
    req->method = Methods::Initialize;
    req->params.emplace(JSONValue::Object{});
    auto fut = client->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    ASSERT_TRUE(resp->result.has_value());
    const auto& res = resp->result.value();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(res.value));
    const auto& ro = std::get<JSONValue::Object>(res.value);
    auto capIt = ro.find("capabilities");
    ASSERT_TRUE(capIt != ro.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(capIt->second->value));
    const auto& capsObj = std::get<JSONValue::Object>(capIt->second->value);
    auto expIt = capsObj.find("experimental");
    if (expIt != capsObj.end() && std::holds_alternative<JSONValue::Object>(expIt->second->value)) {
        const auto& expObj = std::get<JSONValue::Object>(expIt->second->value);
        EXPECT_TRUE(expObj.find("keepalive") == expObj.end());
    }

    ASSERT_NO_THROW(client->Close().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ServerKeepalive, ClosesAfterConsecutiveFailures) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    std::promise<void> closedPromise; auto closedFuture = closedPromise.get_future();
    server.SetErrorHandler([&](const std::string& err){
        if (err.find("Keepalive failure threshold") != std::string::npos) {
            closedPromise.set_value();
        }
    });

    server.SetKeepaliveIntervalMs(20); // fast cadence to trigger quickly

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(client->Start().get());

    // Force send failures by closing the client transport
    ASSERT_NO_THROW(client->Close().get());

    // Expect server to observe consecutive failures and close transport
    ASSERT_EQ(closedFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Cleanup
    ASSERT_NO_THROW(server.Stop().get());
}
