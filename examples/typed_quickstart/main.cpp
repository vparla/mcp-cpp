//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: Typed client wrappers quick-start example
//==========================================================================================================

#include <iostream>
#include <future>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"

using namespace mcp;

static ToolResult makeOk(const std::string& text) {
    ToolResult r; r.isError = false;
    JSONValue::Object msg; msg["type"] = std::make_shared<JSONValue>(std::string("text")); msg["text"] = std::make_shared<JSONValue>(text);
    r.content.push_back(JSONValue{msg});
    return r;
}

int main() {
    // Wire an in-memory client/server pair
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Typed QuickStart Server");
    server.Start(std::move(serverTransport)).get();

    // Register a tool with metadata and a simple handler
    Tool echo; echo.name = "echo"; echo.description = "echoes 'hello'";
    JSONValue::Object schema; schema["type"] = std::make_shared<JSONValue>(std::string("object")); echo.inputSchema = JSONValue{schema};
    server.RegisterTool(echo, [](const JSONValue&, std::stop_token){
        return std::async(std::launch::async, [](){ return makeOk("hello"); });
    });

    ClientFactory factory;
    Implementation clientInfo{"Typed QuickStart Client","1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->Connect(std::move(clientTransport)).get();
    ClientCapabilities caps; (void)client->Initialize(clientInfo, caps).get();

    // Typed call + content helpers
    CallToolResult res = typed::callTool(*client, "echo", JSONValue{JSONValue::Object{}}).get();
    if (auto first = typed::firstText(res)) {
        std::cout << "tool text: " << *first << std::endl;
    } else {
        std::cout << "no text content" << std::endl;
    }

    client->Disconnect().get();
    server.Stop().get();
    return 0;
}
