//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: Example demonstrating experimental resource read chunking (offset/length) and chunk reassembly
//==========================================================================================================

#include <chrono>
#include <future>
#include <iostream>
#include <optional>
#include <string>

#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Server.h"
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"

using namespace mcp;

namespace {

static ReadResourceResult makeTextResource(const std::string& s) {
    ReadResourceResult r;
    r.contents.push_back(mcp::typed::makeText(s));
    return r;
}

} // namespace

int main() {
    // Wire up an in-memory server/client pair
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("ChunkSrv");

    // Register a text resource
    const std::string uri = "mem://demo";
    const std::string text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    server.RegisterResource(uri, [=](const std::string& reqUri, std::stop_token) {
        return std::async(std::launch::async, [=]() {
            (void)reqUri; // unused
            return makeTextResource(text);
        });
    });

    // Start server
    server.Start(std::move(serverTrans)).get();

    // Create and initialize client
    ClientFactory f;
    Implementation ci{"ChunkCli", "1.0"};
    auto client = f.CreateClient(ci);
    client->Connect(std::move(clientTrans)).get();
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    // Demonstrate reading a slice: offset=3, length=4 => DEFG
    ReadResourceResult slice = typed::readResourceRange(*client, uri, std::optional<int64_t>(3), std::optional<int64_t>(4)).get();
    std::cout << "Slice [3..7): ";
    for (const auto& s : typed::collectText(slice)) std::cout << s;
    std::cout << "\n";

    // Demonstrate reading an entire resource in fixed-size chunks and reassembling
    const size_t chunkSize = 8;
    ReadResourceResult agg = typed::readAllResourceInChunks(*client, uri, chunkSize).get();
    std::string all;
    for (const auto& s : typed::collectText(agg)) all += s;
    std::cout << "Reassembled (chunkSize=" << chunkSize << "): " << all << "\n";

    // Now configure a server-side clamp smaller than the requested chunk size
    // Note: Changing clamp after initialize does not update the advertisement; enforcement still applies.
    server.SetResourceReadChunkingMaxBytes(3);

    // Demonstrate clamped range: request length=10 from offset=5 -> expect only 3 bytes returned
    ReadResourceResult clampedSlice = typed::readResourceRange(*client, uri, std::optional<int64_t>(5), std::optional<int64_t>(10)).get();
    std::cout << "Clamped slice [5..): ";
    for (const auto& s : typed::collectText(clampedSlice)) std::cout << s; // prints 3 bytes
    std::cout << "\n";

    // Demonstrate reassembly still works when server clamp < requested chunk size
    ReadResourceResult clampedAgg = typed::readAllResourceInChunks(*client, uri, /*chunkSize*/ 8).get();
    std::string clampedAll;
    for (const auto& s : typed::collectText(clampedAgg)) clampedAll += s;
    std::cout << "Reassembled under clamp (maxChunkBytes=3, requested chunkSize=8): " << clampedAll << "\n";

    client->Disconnect().get();
    server.Stop().get();
    return 0;
}
