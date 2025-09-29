//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_keepalive_threshold_config.cpp
// Purpose: Tests for configurable keepalive failure threshold (advertisement + behavior)
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Transport.h"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"
#include <future>
#include <chrono>
#include <string>

using namespace mcp;

TEST(ServerKeepaliveThreshold, AdvertisesCustomThreshold) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.SetKeepaliveIntervalMs(50);
    server.SetKeepaliveFailureThreshold(7);

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(client->Start().get());

    // Send initialize and inspect capabilities.experimental.keepalive.failureThreshold
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
    auto capIt = ro.find("capabilities"); ASSERT_TRUE(capIt != ro.end());
    const auto& capsObj = std::get<JSONValue::Object>(capIt->second->value);
    auto expIt = capsObj.find("experimental"); ASSERT_TRUE(expIt != capsObj.end());
    const auto& expObj = std::get<JSONValue::Object>(expIt->second->value);
    auto keepIt = expObj.find("keepalive"); ASSERT_TRUE(keepIt != expObj.end());
    const auto& kv = std::get<JSONValue::Object>(keepIt->second->value);
    auto thrIt = kv.find("failureThreshold"); ASSERT_TRUE(thrIt != kv.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(thrIt->second->value));
    EXPECT_EQ(std::get<int64_t>(thrIt->second->value), static_cast<int64_t>(7));

    ASSERT_NO_THROW(client->Close().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ServerKeepaliveThreshold, ClosesAfterSingleFailureWhenThresholdIsOne) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    std::promise<void> closedPromise; auto closedFuture = closedPromise.get_future();
    server.SetErrorHandler([&](const std::string& err){
        if (err.find("Keepalive failure threshold") != std::string::npos) {
            try { closedPromise.set_value(); } catch (...) {}
        }
    });

    server.SetKeepaliveIntervalMs(20);
    server.SetKeepaliveFailureThreshold(1);

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(client->Start().get());

    // Force send failures by closing the client transport
    ASSERT_NO_THROW(client->Close().get());

    // Expect server to close after a single failure
    ASSERT_EQ(closedFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    ASSERT_NO_THROW(server.Stop().get());
}
