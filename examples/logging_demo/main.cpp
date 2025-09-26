//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: Example demonstrating server logging to client with log level filter and rate limiting
//==========================================================================================================

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

    // Start server
    Server server("LoggingDemoSrv");
    server.SetLoggingRateLimitPerSecond(5); // throttle burst
    server.Start(std::move(serverTrans)).get();

    // Create client and connect
    ClientFactory factory; Implementation info{"LoggingDemoCli","1.0.0"};
    auto client = factory.CreateClient(info);
    client->Connect(std::move(clientTrans)).get();

    // Capture log notifications on client
    client->SetNotificationHandler(Methods::Log, [&](const std::string& method, const JSONValue& params){
        (void)method;
        if (std::holds_alternative<JSONValue::Object>(params.value)) {
            const auto& o = std::get<JSONValue::Object>(params.value);
            auto itLvl = o.find("level");
            auto itMsg = o.find("message");
            if (itLvl != o.end() && itMsg != o.end() &&
                std::holds_alternative<std::string>(itLvl->second->value) &&
                std::holds_alternative<std::string>(itMsg->second->value)) {
                std::cout << "log [" << std::get<std::string>(itLvl->second->value) << "]: "
                          << std::get<std::string>(itMsg->second->value) << "\n";
            }
        }
    });

    // Initialize with client min log level WARN
    ClientCapabilities caps; caps.experimental["logLevel"] = JSONValue{std::string("WARN")};
    (void)client->Initialize(info, caps).get();

    // INFO should be suppressed, ERROR delivered
    server.LogToClient("INFO", "info suppressed", std::nullopt);
    server.LogToClient("ERROR", "error delivered", std::nullopt);

    // Burst logs to demonstrate rate limiting
    for (int i = 0; i < 20; ++i) {
        server.LogToClient("ERROR", std::string("burst ") + std::to_string(i), std::nullopt);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    client->Disconnect().get();
    server.Stop().get();
    return 0;
}
