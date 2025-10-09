//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: MCP server example
//==========================================================================================================

#include "logging/Logger.h"
#include "mcp/Server.h"
#include "mcp/StdioTransport.hpp"
#include "mcp/SharedMemoryTransport.hpp"
#include "mcp/HTTPServer.hpp"
#include "mcp/Protocol.h"
#include <iostream>
#include <cstddef>
#include <chrono>
#include <thread>
#include <future>
#include <stop_token>
#include "env/EnvVars.h"

using namespace mcp;

//==========================================================================================================
// Parses simple key=value style command-line options.
// Args:
//   argc: Argument count
//   argv: Argument values
//   key: Option name including leading dashes (e.g., "--transport")
// Returns:
//   Optional value string when present; empty optional otherwise
//==========================================================================================================
static std::optional<std::string> getArgValue(int argc, char** argv, const std::string& key) {
    for (std::size_t i = 1; i < static_cast<std::size_t>(argc); ++i) {
        const char* arg = argv[i];
        if (arg == nullptr) {
            continue;
        }
        std::string a = arg;
        std::size_t eq = a.find('=');
        if (eq != std::string::npos) {
            std::string k = a.substr(0, eq);
            std::string v;
            if (eq + 1 <= a.size()) {
                v = a.substr(eq + 1);
            } else {
                v = std::string();
            }
            if (k == key) {
                return v;
            }
        }
    }
    return std::nullopt;
}

int main(int argc, char** argv) {
    FUNC_SCOPE();
    // Respect MCP_LOG_LEVEL for CI/scripts. Default to INFO to avoid excessive DEBUG noise
    // in stdio hardening tests where many keepalive notifications are generated.
    Logger::setLogLevelFromString(GetEnvOrDefault("MCP_LOG_LEVEL", "INFO"));

    // Create server via factory and register simple echo tool with inputSchema
    Implementation info{"MCP Demo Server","1.0.0"};
    ServerFactory factory;
    auto server = factory.CreateServer(info);
    JSONValue::Object msgType; msgType["type"] = std::make_shared<JSONValue>(std::string("string"));
    JSONValue::Object props; props["message"] = std::make_shared<JSONValue>(JSONValue{msgType});
    JSONValue::Array required; required.push_back(std::make_shared<JSONValue>(std::string("message")));
    JSONValue::Object schema; schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    schema["properties"] = std::make_shared<JSONValue>(JSONValue{props});
    schema["required"] = std::make_shared<JSONValue>(JSONValue{required});
    Tool echo{"echo","Echo a message", JSONValue{schema}};
    server->RegisterTool(echo, [](const JSONValue& args, std::stop_token st) -> std::future<ToolResult> {
        (void)st;
        return std::async(std::launch::async, [args]() mutable {
            ToolResult tr; tr.isError = false;
            JSONValue::Object content; content["type"] = std::make_shared<JSONValue>(std::string("text")); content["text"] = std::make_shared<JSONValue>(std::string("ok"));
            tr.content.push_back(JSONValue{content});
            (void)args; return tr;
        });
    });

    // Prepare waitable completion and handler (fires on EOF or transport error)
    std::promise<void> stopped;
    server->SetErrorHandler([&stopped, &server](const std::string& err) {
        LOG_INFO("Server stopping: {}", err);
        // Proactively stop the server/transport so the demo process exits promptly
        try {
            (void)server->Stop().get();
        } catch (...) {}
        try {
            stopped.set_value();
        } catch (...) {}
    });

    // Optional: server keepalive interval configured by environment
    {
        std::string keepalive = GetEnvOrDefault("MCP_KEEPALIVE_MS", "");
        if (!keepalive.empty()) {
            try {
                int ms = std::stoi(keepalive);
                if (ms > 0) {
                    server->SetKeepaliveIntervalMs(ms);
                    LOG_INFO("Enabled keepalive: {} ms", ms);
                }
            } catch (...) {}
        }
    }

    // Transport selection via CLI
    std::string transportKind = getArgValue(argc, argv, "--transport").value_or("stdio");
    LOG_INFO("Server starting with transport={}", transportKind);

    if (transportKind == "stdio") {
        StdioTransportFactory tFactory;
        std::string cfg = GetEnvOrDefault("MCP_STDIO_CONFIG", "timeout_ms=30000");
        if (auto v = getArgValue(argc, argv, "--stdiocfg"); v.has_value()) {
            cfg = v.value();
        }
        if (cfg != "timeout_ms=30000") {
            LOG_INFO("Using stdio config: {}", cfg);
        }
        auto transport = tFactory.CreateTransport(cfg);
        server->Start(std::move(transport)).get();
        stopped.get_future().wait();
    } else if (transportKind == "shm") {
        // Shared memory channel; server (creator) must set create=true
        std::string channel = getArgValue(argc, argv, "--channel").value_or("mcp-shm");
        std::string cfg = std::string("shm://") + channel + "?create=true";
        LOG_INFO("Using shared memory channel: {}", channel);
        SharedMemoryTransportFactory f;
        auto transport = f.CreateTransport(cfg);
        server->Start(std::move(transport)).get();
        stopped.get_future().wait();
    } else if (transportKind == "http") {
        // HTTP acceptor demo: bridge request handling via Server::HandleJSONRPC
        std::string listen = getArgValue(argc, argv, "--listen").value_or("http://127.0.0.1:9443");
        LOG_INFO("HTTPServer listening at: {} (rpcPath=/mcp/rpc, notifyPath=/mcp/notify)", listen);
        HTTPServerFactory hf;
        auto acceptor = hf.CreateTransportAcceptor(listen);
        acceptor->SetRequestHandler([&server](const JSONRPCRequest& req) {
            return server->HandleJSONRPC(req);
        });
        acceptor->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note) {
            if (note) {
                LOG_INFO("HTTP notification received: {}", note->method);
            }
        });
        acceptor->SetErrorHandler([&stopped](const std::string& e){
            LOG_ERROR("HTTPServer error: {}", e);
            try { stopped.set_value(); } catch (...) {}
        });
        (void)acceptor->Start().get();

        // Optional: allow pressing Enter to exit demo
        std::thread waiter([&stopped]() {
            try {
                LOG_INFO("Press ENTER to stop HTTP demo...");
                (void)std::getchar();
                try {
                    stopped.set_value();
                } catch (...) {}
            } catch (...) {}
        });
        stopped.get_future().wait();
        if (waiter.joinable()) {
            waiter.join();
        }
        (void)acceptor->Stop().get();
    } else {
        LOG_ERROR("Unknown --transport option: {} (expected stdio|shm|http)", transportKind);
        return 2;
    }
    return 0;
}
