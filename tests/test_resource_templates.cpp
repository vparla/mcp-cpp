//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_resource_templates.cpp
// Purpose: Server resource templates end-to-end tests
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/Transport.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include <chrono>
#include <optional>

using namespace mcp;

TEST(ServerResourceTemplates, ListRoundTrip) {
    // Create in-memory transport pair (client/server)
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    // Start server
    Server server("MCP Test Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    // Create and connect client
    ClientFactory factory;
    Implementation clientInfo{"MCP Test Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    // Initialize client
    ClientCapabilities caps; caps.sampling = SamplingCapability{};
    auto initFut = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    // Register resource templates on server
    ResourceTemplate rt1{"file:///{path}", "File Reader", std::optional<std::string>("Reads files"), std::optional<std::string>("text/plain")};
    ResourceTemplate rt2{"mem://{key}", "Memory KV", std::optional<std::string>("Reads keys"), std::optional<std::string>("application/json")};
    server.RegisterResourceTemplate(rt1);
    server.RegisterResourceTemplate(rt2);

    // List resource templates via client
    auto listFut = client->ListResourceTemplates();
    ASSERT_EQ(listFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto templates = listFut.get();

    // We expect at least the two we registered (order not guaranteed)
    ASSERT_GE(templates.size(), 2u);

    // Verify contents
    bool found1 = false, found2 = false;
    for (const auto& rt : templates) {
        if (rt.uriTemplate == rt1.uriTemplate && rt.name == rt1.name) {
            ASSERT_TRUE(rt.description.has_value());
            ASSERT_TRUE(rt.mimeType.has_value());
            EXPECT_EQ(rt.description.value(), rt1.description.value());
            EXPECT_EQ(rt.mimeType.value(), rt1.mimeType.value());
            found1 = true;
        }
        if (rt.uriTemplate == rt2.uriTemplate && rt.name == rt2.name) {
            ASSERT_TRUE(rt.description.has_value());
            ASSERT_TRUE(rt.mimeType.has_value());
            EXPECT_EQ(rt.description.value(), rt2.description.value());
            EXPECT_EQ(rt.mimeType.value(), rt2.mimeType.value());
            found2 = true;
        }
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);

    // Cleanup
    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
