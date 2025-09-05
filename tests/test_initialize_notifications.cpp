//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_initialize_notifications.cpp
// Purpose: Verify exactly one list-changed notification per category is sent after initialize
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"
#include <atomic>
#include <future>
#include <chrono>

using namespace mcp;

TEST(InitializeNotifications, ExactlyOneListChangedPerCategory) {
    // Create transport pair
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Create server
    Server server("TestServer");
    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    // Create client and connect
    ClientFactory factory; Implementation info{"TestClient","1.0.0"};
    auto client = factory.CreateClient(info);

    // Set up notification handlers and promises
    std::promise<void> toolsOnce; auto toolsFut = toolsOnce.get_future();
    std::promise<void> resourcesOnce; auto resourcesFut = resourcesOnce.get_future();
    std::promise<void> promptsOnce; auto promptsFut = promptsOnce.get_future();

    std::atomic<int> toolsCount{0};
    std::atomic<int> resourcesCount{0};
    std::atomic<int> promptsCount{0};

    client->SetNotificationHandler(Methods::ToolListChanged,
        [&](const std::string& method, const JSONValue& params){
            (void)method; (void)params;
            if (toolsCount.fetch_add(1, std::memory_order_relaxed) == 0) {
                toolsOnce.set_value();
            }
        });

    client->SetNotificationHandler(Methods::ResourceListChanged,
        [&](const std::string& method, const JSONValue& params){
            (void)method; (void)params;
            if (resourcesCount.fetch_add(1, std::memory_order_relaxed) == 0) {
                resourcesOnce.set_value();
            }
        });

    client->SetNotificationHandler(Methods::PromptListChanged,
        [&](const std::string& method, const JSONValue& params){
            (void)method; (void)params;
            if (promptsCount.fetch_add(1, std::memory_order_relaxed) == 0) {
                promptsOnce.set_value();
            }
        });

    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());

    // Initialize
    ClientCapabilities caps; // defaults are fine
    auto initFut = client->Initialize(info, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    // Expect exactly one notification for each category within timeout
    ASSERT_EQ(toolsFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_EQ(resourcesFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_EQ(promptsFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Give a brief moment to ensure no duplicate arrives
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(toolsCount.load(), 1);
    EXPECT_EQ(resourcesCount.load(), 1);
    EXPECT_EQ(promptsCount.load(), 1);

    // Cleanup
    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
