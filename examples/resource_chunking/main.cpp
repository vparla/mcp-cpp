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

    // Configure a server-side clamp and start server so it's advertised during initialize
    server.SetResourceReadChunkingMaxBytes(3);
    // Start server
    server.Start(std::move(serverTrans)).get();

    // Create and initialize client
    ClientFactory f;
    Implementation ci{"ChunkCli", "1.0"};
    auto client = f.CreateClient(ci);
    client->Connect(std::move(clientTrans)).get();
    ClientCapabilities caps; ServerCapabilities scaps = client->Initialize(ci, caps).get();

    // Extract clamp hint from capabilities via typed helper
    std::optional<size_t> clampHint = typed::extractResourceReadClamp(scaps);

    // Demonstrate reading a slice: offset=3, length=4 (will be clamped to 3 bytes by server)
    ReadResourceResult slice = typed::readResourceRange(*client, uri, std::optional<int64_t>(3), std::optional<int64_t>(4)).get();
    std::cout << "Slice [3..7): ";
    for (const auto& s : typed::collectText(slice)) std::cout << s;
    std::cout << "\n";

    // Demonstrate reading an entire resource in fixed-size chunks and reassembling
    const size_t chunkSize = 8;
    ReadResourceResult agg = typed::readAllResourceInChunks(*client, uri, chunkSize, scaps).get();
    std::string all;
    for (const auto& s : typed::collectText(agg)) all += s;
    std::cout << "Reassembled (preferred chunkSize=" << chunkSize << ", clamp=" << (clampHint ? std::to_string(*clampHint) : std::string("none")) << "): " << all << "\n";

    client->Disconnect().get();
    server.Stop().get();
    return 0;
}
