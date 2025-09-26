//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_typed_cancellation.cpp
// Purpose: Tests for cancellation-aware typed chunk helper readAllResourceInChunks
//==========================================================================================================

#include <gtest/gtest.h>
#include <future>
#include <optional>
#include <stop_token>
#include <thread>
#include <chrono>

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

TEST(TypedCancellation, ChunkReadStopsOnStopToken) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "ABCDEFGHIJ"; // 10 bytes

    // Slow handler to allow cancellation to arrive before the next chunk request
    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return makeTextResource(text);
        });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; ServerCapabilities scaps = client->Initialize(ci, caps).get();

    // Create a stop source and schedule cancellation before the second loop iteration
    std::stop_source src;
    auto tok = src.get_token();
    std::thread canceller([&src](){
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        src.request_stop();
    });

    const size_t chunk = 3; // expect at most first 3 bytes to be reassembled
    auto agg = mcp::typed::readAllResourceInChunks(*client, uri, chunk, scaps, std::optional<std::stop_token>(tok)).get();
    std::string reassembled;
    for (const auto& s : mcp::typed::collectText(agg)) reassembled += s;

    // Cancellation should stop after first chunk
    EXPECT_EQ(reassembled, text.substr(0, chunk));

    canceller.join();
    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
