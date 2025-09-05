//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: MCP server example
//==========================================================================================================

#include "logging/Logger.h"
#include "mcp/Server.h"
#include "mcp/StdioTransport.hpp"
#include "mcp/Protocol.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <future>
#include <stop_token>

using namespace mcp;

int main() {
    FUNC_SCOPE();
    Logger::setLogLevel(LogLevel::LOG_DEBUG_LEVEL);

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
    server->SetErrorHandler([&stopped](const std::string& err) {
        LOG_INFO("Server stopping: {}", err);
        try { stopped.set_value(); } catch (...) {}
    });

    // Start server on stdio via factory
    StdioTransportFactory tFactory;
    auto transport = tFactory.CreateTransport("timeout_ms=30000");
    server->Start(std::move(transport)).get();

    // Wait for transport termination (e.g., stdin EOF)
    stopped.get_future().wait();
    return 0;
}
