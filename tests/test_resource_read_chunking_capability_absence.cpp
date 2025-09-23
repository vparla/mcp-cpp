//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_resource_read_chunking_capability_absence.cpp
// Purpose: Verify range reads work even when server does not advertise experimental chunking capability
//==========================================================================================================

#include <gtest/gtest.h>
#include <future>
#include <optional>
#include <string>

#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"

using namespace mcp;

namespace {

static ReadResourceResult makeTextResource(const std::string& s) {
    ReadResourceResult r; r.contents.push_back(mcp::typed::makeText(s)); return r;
}

} // namespace

TEST(ResourceReadChunkingCapabilityAbsence, RangeWorksWithoutAdvertisement) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");

    // Remove experimental advertisement before initialize
    {
        auto caps = server.GetCapabilities();
        caps.experimental.erase("resourceReadChunking");
        server.SetCapabilities(caps);
    }

    const std::string uri = "mem://doc2";
    const std::string text = "abcdefghij"; // 10 bytes

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    auto initCaps = client->Initialize(ci, ClientCapabilities{}).get();
    // Ensure server did not advertise chunking capability
    EXPECT_TRUE(initCaps.experimental.find("resourceReadChunking") == initCaps.experimental.end());

    // Range read should still work
    auto rr = mcp::typed::readResourceRange(*client, uri, std::optional<int64_t>(3), std::optional<int64_t>(4)).get();
    auto parts = mcp::typed::collectText(rr);
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0], std::string("defg"));

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
