//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: MCP client example
//==========================================================================================================

#include "logging/Logger.h"
#include "mcp/Client.h"
#include "mcp/StdioTransport.hpp"
#include "mcp/SharedMemoryTransport.hpp"
#include "mcp/HTTPTransport.hpp"
#include "mcp/Protocol.h"
#include <iostream>
#include <chrono>

using namespace mcp;

//==========================================================================================================
// getArgValue
// Purpose: Parses key=value style CLI options.
// Args:
//   argc: Argument count
//   argv: Argument vector
//   key: Key string including leading dashes (e.g., "--transport")
// Returns:
//   Optional string containing the value when present
//==========================================================================================================
static std::optional<std::string> getArgValue(int argc, char** argv, const std::string& key) {
    for (size_t i = 1; i < static_cast<size_t>(argc); ++i) {
        std::string a = argv[i];
        auto eq = a.find('=');
        if (eq != std::string::npos) {
            std::string k = a.substr(0, eq);
            std::string v = a.substr(eq + 1);
            if (k == key) {
                return v;
            }
        }
    }
    return std::nullopt;
}

//==========================================================================================================
// parseHttpUrlToConfig
// Purpose: Converts a simple http(s)://host[:port] URL to HTTPTransportFactory config string.
// Args:
//   url: URL like http://127.0.0.1:9443
// Returns:
//   Factory config such as "scheme=http; host=127.0.0.1; port=9443; rpcPath=/mcp/rpc; notifyPath=/mcp/notify; serverName=127.0.0.1"
//==========================================================================================================
static std::string parseHttpUrlToConfig(const std::string& url) {
    std::string scheme = "http";
    std::string rest = url;
    if (rest.rfind("http://", 0) == 0) {
        scheme = "http"; rest = rest.substr(7);
    } else if (rest.rfind("https://", 0) == 0) {
        scheme = "https"; rest = rest.substr(8);
    }
    std::string host = rest;
    std::string port = "9443";
    auto colon = rest.rfind(':');
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
        port = rest.substr(colon + 1);
    }
    std::string cfg = std::string("scheme=") + scheme + "; host=" + host + "; port=" + port + 
        "; rpcPath=/mcp/rpc; notifyPath=/mcp/notify; serverName=" + host;
    return cfg;
}

int main(int argc, char** argv) {
    FUNC_SCOPE();
    Logger::setLogLevel(LogLevel::LOG_INFO_LEVEL);

    // Create client and connect over stdio
    ClientFactory factory;
    Implementation info{"MCP Demo Client","1.0.0"};
    auto client = factory.CreateClient(info);
    client->SetErrorHandler([](const std::string& err){ LOG_INFO("HTTP DEBUG: {}", err); });

    // Choose transport via CLI
    std::string transportKind = getArgValue(argc, argv, "--transport").value_or("stdio");
    std::unique_ptr<ITransport> transport;
    if (transportKind == "stdio") {
        StdioTransportFactory f;
        std::string cfg = getArgValue(argc, argv, "--stdiocfg").value_or("timeout_ms=30000");
        transport = f.CreateTransport(cfg);
    } else if (transportKind == "shm") {
        SharedMemoryTransportFactory f;
        std::string channel = getArgValue(argc, argv, "--channel").value_or("mcp-shm");
        std::string cfg = std::string("shm://") + channel; // client side: create=false default
        transport = f.CreateTransport(cfg);
    } else if (transportKind == "http") {
        HTTPTransportFactory f;
        std::string url = getArgValue(argc, argv, "--url").value_or("http://127.0.0.1:9443");
        std::string cfg = parseHttpUrlToConfig(url);
        if (auto extra = getArgValue(argc, argv, "--httpcfg"); extra.has_value()) {
            if (!extra->empty()) {
                if (!cfg.empty() && cfg.back() != ';') { cfg += "; "; }
                cfg += *extra;
            }
        }
        transport = f.CreateTransport(cfg);
    } else {
        LOG_ERROR("Unknown --transport option: {} (expected stdio|shm|http)", transportKind);
        return 2;
    }
    client->Connect(std::move(transport)).get();

    // Initialize and list tools
    ClientCapabilities caps; caps.sampling = SamplingCapability{};
    auto serverCaps = client->Initialize(info, caps).get();
    (void)serverCaps;

    auto tools = client->ListTools().get();
    for (const auto& t : tools) {
        LOG_INFO("Tool: {} - {}", t.name, t.description);
    }

    // Exit: disconnect transport
    client->Disconnect().get();
    return 0;
}
