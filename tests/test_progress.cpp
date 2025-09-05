//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_progress.cpp
// Purpose: Tests for server -> client progress notifications (notifications/progress)
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include <future>
#include <chrono>
#include <atomic>
#include <string>

using namespace mcp;

TEST(ServerProgress, DeliversProgressNotificationWithShape) {
    // Create in-memory transport pair
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Start server
    Server server("ProgressTestServer");
    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    // Create typed client and connect
    ClientFactory factory; Implementation info{"ProgressClient", "1.0.0"};
    auto client = factory.CreateClient(info);

    std::promise<void> gotPromise; auto gotFuture = gotPromise.get_future();
    std::string gotToken; double gotProgress = -1.0; std::string gotMessage;
    client->SetProgressHandler([&](const std::string& token, double prog, const std::string& msg){
        gotToken = token; gotProgress = prog; gotMessage = msg; 
        try { gotPromise.set_value(); } catch (...) {}
    });

    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());

    // Initialize handshake (not strictly required for notifications, but validates normal flow)
    ClientCapabilities caps; auto initFut = client->Initialize(info, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    // Send a progress notification from server
    ASSERT_NO_THROW(server.SendProgress("tok-1", 0.25, "quarter").get());

    ASSERT_EQ(gotFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(gotToken, std::string("tok-1"));
    EXPECT_DOUBLE_EQ(gotProgress, 0.25);
    EXPECT_EQ(gotMessage, std::string("quarter"));

    // Cleanup
    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
