//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: Example demonstrating server keepalive notifications and failure handling
//==========================================================================================================

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"

using namespace mcp;

int main() {
    // Create connected in-memory pair
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Start server with keepalive enabled
    Server server("KeepaliveDemoSrv");
    server.SetKeepaliveIntervalMs(50); // 50ms cadence

    std::promise<void> closedPromise; auto closedFuture = closedPromise.get_future();
    server.SetErrorHandler([&](const std::string& err){
        std::cout << "server error: " << err << "\n";
        if (err.find("Keepalive failure threshold") != std::string::npos) {
            try { closedPromise.set_value(); } catch (...) {}
        }
    });

    server.Start(std::move(serverTrans)).get();

    // Create client and connect
    ClientFactory f; Implementation ci{"KeepaliveDemoCli","1.0"};
    auto client = f.CreateClient(ci);
    client->Connect(std::move(clientTrans)).get();

    // Observe keepalives
    std::atomic<unsigned int> seen{0u};
    client->SetNotificationHandler(Methods::Keepalive, [&](const std::string& method, const JSONValue& params){
        (void)method; (void)params;
        seen.fetch_add(1u);
        std::cout << "keepalive #" << seen.load() << "\n";
    });

    // Allow some keepalives, then simulate failure by closing client transport
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    client->Disconnect().get();

    // Expect server to report failure after threshold
    (void)closedFuture.wait_for(std::chrono::seconds(2));

    server.Stop().get();
    return 0;
}
