//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_capabilities_logging.cpp
// Purpose: Verify that the server advertises the logging capability and the client parses it
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Transport.h"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"
#include <future>
#include <chrono>

using namespace mcp;

TEST(CapabilitiesLogging, InitializeAdvertisesLogging_ClientParses) {
    // Transport pair
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Server
    Server server("TestServer");
    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    // Client (high-level)
    ClientFactory factory; Implementation info{"TestClient", "1.0.0"};
    auto client = factory.CreateClient(info);

    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());

    // Initialize and fetch server capabilities
    ClientCapabilities clientCaps; // default: no sampling, no experimental
    auto initFut = client->Initialize(info, clientCaps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ServerCapabilities caps = initFut.get();

    // Expect logging capability to be advertised by server and parsed by client
    ASSERT_TRUE(caps.logging.has_value());

    // Cleanup
    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(CapabilitiesLogging, InitializeJsonContainsLoggingField) {
    // Use low-level transport to inspect raw initialize response shape
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(client->Start().get());

    // Minimal initialize params
    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    JSONValue::Object params; // leave empty
    init->params.emplace(params);

    auto fut = client->SendRequest(std::move(init));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    ASSERT_TRUE(resp->result.has_value());

    const JSONValue& result = resp->result.value();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(result.value));
    const auto& obj = std::get<JSONValue::Object>(result.value);

    auto capsIt = obj.find("capabilities");
    ASSERT_TRUE(capsIt != obj.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(capsIt->second->value));
    const auto& capsObj = std::get<JSONValue::Object>(capsIt->second->value);

    // Verify the presence of the 'logging' capability field (object)
    auto loggingIt = capsObj.find("logging");
    ASSERT_TRUE(loggingIt != capsObj.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(loggingIt->second->value));

    // Cleanup
    ASSERT_NO_THROW(client->Close().get());
    ASSERT_NO_THROW(server.Stop().get());
}
