//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_resource_read_chunking_overload.cpp
// Purpose: Tests for clamp-aware typed overload readAllResourceInChunks (preferred vs clamp)
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

TEST(ResourceReadChunkingOverload, ClampAwareOverload_UsesMinAndReassembles) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "ABCDEFGHIJ"; // 10 bytes

    // Configure server clamp smaller than preferred chunk (3 < 8)
    server.SetResourceReadChunkingMaxBytes(3);

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; ServerCapabilities scaps = client->Initialize(ci, caps).get();

    // Extract clamp hint via typed helper
    std::optional<size_t> clampHint = mcp::typed::extractResourceReadClamp(scaps);

    ASSERT_TRUE(clampHint.has_value());
    EXPECT_EQ(*clampHint, static_cast<size_t>(3));

    // Preferred chunk is 8, but clamp is 3; overload should use min(8,3)=3 internally
    const size_t preferred = 8;
    auto agg = mcp::typed::readAllResourceInChunks(*client, uri, preferred, clampHint).get();
    std::string reassembled;
    for (const auto& s : mcp::typed::collectText(agg)) {
        reassembled += s;
    }
    EXPECT_EQ(reassembled, text);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
