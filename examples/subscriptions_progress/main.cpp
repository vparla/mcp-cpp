//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: examples/subscriptions_progress/main.cpp
// Purpose: Demonstrate per-URI resource subscriptions and server progress notifications using InMemoryTransport
//==========================================================================================================

#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <optional>

#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "logging/Logger.h"

int main() {
    using namespace mcp;
    Logger::setLogLevel(LogLevel::LOG_INFO_LEVEL);

    // Create a connected in-memory pair
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    // Create server and start it
    Server server("Subscriptions+Progress Demo Server");
    server.Start(std::move(serverTransport)).get();

    // Register a resource the client can subscribe to
    const std::string uriA = "demo://a";
    server.RegisterResource(uriA, [uriA](const std::string& reqUri, std::stop_token st) {
        (void)st;
        return std::async(std::launch::async, [uriA, reqUri]() {
            ReadResourceResult r;
            JSONValue::Object content;
            content["uri"] = std::make_shared<JSONValue>(uriA);
            content["mimeType"] = std::make_shared<JSONValue>(std::string("text/plain"));
            content["text"] = std::make_shared<JSONValue>(std::string("demo"));
            r.contents.push_back(JSONValue{content});
            return r;
        });
    });

    // Create client and connect
    ClientFactory factory;
    Implementation clientInfo{"SubscriptionsProgressClient", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->Connect(std::move(clientTransport)).get();

    // Initialize (capabilities defaults ok)
    ClientCapabilities caps;
    (void)client->Initialize(clientInfo, caps).get();

    // Wire a notification handler to observe filtered resource updates
    std::atomic<int> updatesSeen{0};
    client->SetNotificationHandler("notifications/resources/updated",
        [&](const std::string& method, const JSONValue& params){
            (void)method;
            if (std::holds_alternative<JSONValue::Object>(params.value)) {
                const auto& o = std::get<JSONValue::Object>(params.value);
                auto it = o.find("uri");
                if (it != o.end() && std::holds_alternative<std::string>(it->second->value)) {
                    std::cout << "resource updated: " << std::get<std::string>(it->second->value) << std::endl;
                    updatesSeen.fetch_add(1);
                }
            }
        });

    // Subscribe only to uriA; updates for other URIs should be filtered by the server
    client->SubscribeResources(std::optional<std::string>(uriA)).get();

    // Send an update for a different URI first; this should be filtered
    server.NotifyResourceUpdated("demo://b").get();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send an update for uriA; this should be delivered
    server.NotifyResourceUpdated(uriA).get();

    // Demonstrate server progress notifications -> client progress handler
    client->SetProgressHandler([](const std::string& token, double progress, const std::string& message){
        std::cout << "progress [" << token << "]: " << progress << " - " << message << std::endl;
    });

    server.SendProgress("tok-1", 0.25, "starting").get();
    server.SendProgress("tok-1", 0.50, "halfway").get();
    server.SendProgress("tok-1", 1.00, "done").get();

    // Allow async delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "updatesSeen=" << updatesSeen.load() << std::endl;

    // Cleanup
    client->Disconnect().get();
    server.Stop().get();

    std::cout << "subscriptions_progress: ok" << std::endl;
    return 0;
}
