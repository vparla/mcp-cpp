//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_handle_jsonrpc.cpp
// Purpose: Unit tests for Server::HandleJSONRPC bridging path (acceptor-style wiring)
//==========================================================================================================

#include <gtest/gtest.h>
#include <future>
#include <stop_token>

#include "mcp/Server.h"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"

using namespace mcp;

//==========================================================================================================
// Verifies that Server::HandleJSONRPC handles initialize and tools/list without an owned transport.
// Args:
//   (none)
// Returns:
//   (none)
//==========================================================================================================
TEST(Server_HandleJSONRPC, InitializeAndListTools) {
    // Arrange: create server and register a tool
    Server server("HandleJSONRPC-Server");

    // Minimal inputSchema for tool metadata: { type: "object", properties: {} }
    JSONValue::Object schema;
    schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    schema["properties"] = std::make_shared<JSONValue>(JSONValue::Object{});
    Tool tool{"echo", "Echo tool", JSONValue{schema}};
    server.RegisterTool(tool, [](const JSONValue& /*args*/, std::stop_token /*st*/) {
        return std::async(std::launch::deferred, [](){
            ToolResult tr; tr.isError = false; return tr;
        });
    });

    // Act: send initialize via HandleJSONRPC
    JSONRPCRequest initReq;
    initReq.id = std::string("1");
    initReq.method = Methods::Initialize;
    JSONValue::Object params;
    params["capabilities"] = std::make_shared<JSONValue>(JSONValue::Object{});
    initReq.params.emplace(JSONValue{params});

    auto initResp = server.HandleJSONRPC(initReq);

    // Assert: initialize response shape
    ASSERT_TRUE(initResp != nullptr);
    ASSERT_FALSE(initResp->IsError());
    ASSERT_TRUE(initResp->result.has_value());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(initResp->result->value));
    const auto& initObj = std::get<JSONValue::Object>(initResp->result->value);
    // protocolVersion and capabilities should be present
    ASSERT_TRUE(initObj.find("protocolVersion") != initObj.end());
    ASSERT_TRUE(initObj.find("capabilities") != initObj.end());

    // Act: send tools/list via HandleJSONRPC
    JSONRPCRequest listReq;
    listReq.id = std::string("2");
    listReq.method = Methods::ListTools;
    auto listResp = server.HandleJSONRPC(listReq);

    // Assert: tools/list response contains exactly 1 tool
    ASSERT_TRUE(listResp != nullptr);
    ASSERT_FALSE(listResp->IsError());
    ASSERT_TRUE(listResp->result.has_value());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(listResp->result->value));
    const auto& listObj = std::get<JSONValue::Object>(listResp->result->value);
    auto itTools = listObj.find("tools");
    ASSERT_TRUE(itTools != listObj.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Array>(itTools->second->value));
    const auto& arr = std::get<JSONValue::Array>(itTools->second->value);
    ASSERT_EQ(arr.size(), static_cast<size_t>(1));
}
