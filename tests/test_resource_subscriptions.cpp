//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_resource_subscriptions.cpp
// Purpose: Tests for per-URI subscriptions and filtered resource updates
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include <future>
#include <chrono>

using namespace mcp;

TEST(ResourceSubscriptions, FiltersUpdatesByUri) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Subscribe to URI A
    const std::string uriA = "test://a";
    const std::string uriB = "test://b";

    // Set up client to send subscribe request
    auto subReq = std::make_unique<JSONRPCRequest>();
    subReq->method = Methods::Subscribe;
    JSONValue::Object subParams; subParams["uri"] = std::make_shared<JSONValue>(uriA);
    // Assign via emplace to avoid optional copy-assignment path
    subReq->params.emplace(subParams);
    auto subFut = client->SendRequest(std::move(subReq));
    ASSERT_EQ(subFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Capture notifications
    std::promise<std::string> notePromise; auto noteFuture = notePromise.get_future();
    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (note && note->method == std::string("notifications/resources/updated")) {
            if (note->params.has_value() && std::holds_alternative<JSONValue::Object>(note->params->value)) {
                const auto& o = std::get<JSONValue::Object>(note->params->value);
                auto it = o.find("uri");
                if (it != o.end() && std::holds_alternative<std::string>(it->second->value)) {
                    notePromise.set_value(std::get<std::string>(it->second->value));
                }
            }
        }
    });

    // Send update for uriB first (should be filtered out)
    server.NotifyResourceUpdated(uriB).get();
    // Then update for uriA (should pass)
    server.NotifyResourceUpdated(uriA).get();

    ASSERT_EQ(noteFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(noteFuture.get(), uriA);
}
