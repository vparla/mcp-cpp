//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_validation_progress.cpp
// Purpose: Strict validation behavior for progress notifications (client-side)
//==========================================================================================================

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/validation/Validation.h"

using namespace mcp;

TEST(ValidationStrict, Progress_Invalid_Dropped) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Validation Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory f; Implementation ci{"Validation Client","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    client->SetValidationMode(mcp::validation::ValidationMode::Strict);

    std::atomic<unsigned int> calls{0u};
    client->SetProgressHandler([&](const std::string& token, double progress, const std::string& message) {
        (void)token; (void)progress; (void)message;
        ++calls;
    });

    // Send invalid progress: empty token should be dropped under Strict
    ASSERT_NO_THROW(server.SendProgress("", 0.5, "invalid").get());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(calls.load(), 0u);

    // Send valid progress: should be delivered
    ASSERT_NO_THROW(server.SendProgress("t1", 0.6, "ok").get());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(calls.load(), 1u);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
