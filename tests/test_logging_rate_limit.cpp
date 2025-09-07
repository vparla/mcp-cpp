//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_logging_rate_limit.cpp
// Purpose: Tests for server logging rate limiting (per-second throttle)
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
#include <thread>

using namespace mcp;

TEST(LoggingRateLimit, SuppressesAndRecovers) {
    // Create in-memory transport pair
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Capture notifications/log on client side
    std::atomic<unsigned int> received{0u};
    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (note && note->method == Methods::Log) {
            received.fetch_add(1u, std::memory_order_relaxed);
        }
    });

    // Start server and client
    Server server("TestServer");
    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(client->Start().get());

    // Initialize without experimental.logLevel (server default min=DEBUG so INFO passes)
    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    init->params.emplace(JSONValue::Object{});
    auto initRespFut = client->SendRequest(std::move(init));
    ASSERT_EQ(initRespFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Enable rate limiting to 2 logs/second
    server.SetLoggingRateLimitPerSecond(2u);

    // Send 5 logs back-to-back; only 2 should pass in the first second window
    for (unsigned int i = 0; i < 5u; ++i) {
        ASSERT_NO_THROW(server.LogToClient("INFO", "msg", std::nullopt));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    unsigned int firstCount = received.load(std::memory_order_relaxed);
    EXPECT_LE(firstCount, 2u);

    // Wait for the next window and send one more
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ASSERT_NO_THROW(server.LogToClient("INFO", "again", std::nullopt));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    unsigned int secondCount = received.load(std::memory_order_relaxed);
    EXPECT_GE(secondCount, 3u); // at least one in the new window

    ASSERT_NO_THROW(client->Close().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(LoggingRateLimit, DisabledAllowsAll) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::atomic<unsigned int> received{0u};
    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (note && note->method == Methods::Log) {
            received.fetch_add(1u, std::memory_order_relaxed);
        }
    });

    Server server("TestServer");
    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(client->Start().get());

    // Initialize default
    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    init->params.emplace(JSONValue::Object{});
    auto initRespFut = client->SendRequest(std::move(init));
    ASSERT_EQ(initRespFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Disable rate limiting explicitly (0 or nullopt)
    server.SetLoggingRateLimitPerSecond(0u);

    // Send 5 logs; all should pass eventually
    for (unsigned int i = 0; i < 5u; ++i) {
        ASSERT_NO_THROW(server.LogToClient("INFO", "bulk", std::nullopt));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_GE(received.load(std::memory_order_relaxed), 5u);

    ASSERT_NO_THROW(client->Close().get());
    ASSERT_NO_THROW(server.Stop().get());
}
