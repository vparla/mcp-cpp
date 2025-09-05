//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_client_subscribe_uri.cpp
// Purpose: Client API tests for per-URI resource subscriptions and filtering
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include <future>
#include <chrono>

using namespace mcp;

TEST(ClientSubscribeUri, SubAndFilterByUri) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("TestServer");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"TestClient", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps; caps.sampling = SamplingCapability{};
    auto initFut = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    const std::string uriA = "test://a";
    const std::string uriB = "test://b";

    // Subscribe to a specific URI via Client API
    ASSERT_NO_THROW(client->SubscribeResources(std::optional<std::string>(uriA)).get());

    // Capture notifications via Client API handler
    std::promise<std::string> notePromise; auto noteFuture = notePromise.get_future();
    client->SetNotificationHandler("notifications/resources/updated",
        [&](const std::string& method, const JSONValue& params) {
            (void)method;
            if (std::holds_alternative<JSONValue::Object>(params.value)) {
                const auto& o = std::get<JSONValue::Object>(params.value);
                auto it = o.find("uri");
                if (it != o.end() && std::holds_alternative<std::string>(it->second->value)) {
                    notePromise.set_value(std::get<std::string>(it->second->value));
                }
            }
        });

    // Send update for uriB first (should be filtered out)
    ASSERT_NO_THROW(server.NotifyResourceUpdated(uriB).get());
    // Then update for uriA (should pass)
    ASSERT_NO_THROW(server.NotifyResourceUpdated(uriA).get());

    ASSERT_EQ(noteFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(noteFuture.get(), uriA);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
