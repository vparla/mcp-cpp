//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: MCP client example
//==========================================================================================================

#include "logging/Logger.h"
#include "mcp/Client.h"
#include "mcp/StdioTransport.hpp"
#include "mcp/Protocol.h"
#include <iostream>
#include <chrono>

using namespace mcp;

int main() {
    FUNC_SCOPE();
    Logger::setLogLevel(LogLevel::LOG_DEBUG_LEVEL);

    // Create client and connect over stdio
    ClientFactory factory;
    Implementation info{"MCP Demo Client","1.0.0"};
    auto client = factory.CreateClient(info);

    // Create stdio transport via factory for consistency
    StdioTransportFactory tFactory;
    auto transport = tFactory.CreateTransport("timeout_ms=30000");
    client->Connect(std::move(transport)).get();

    // Initialize and list tools
    ClientCapabilities caps; caps.sampling = SamplingCapability{};
    auto serverCaps = client->Initialize(info, caps).get();
    (void)serverCaps;

    auto tools = client->ListTools().get();
    for (const auto& t : tools) {
        LOG_INFO("Tool: {} - {}", t.name, t.description);
    }

    // Exit: closing stdio will end the session
    client->Disconnect().get();
    return 0;
}
