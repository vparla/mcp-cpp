//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_tools_meta.cpp
// Purpose: Tools _meta serialization tests for tools/list
//==========================================================================================================

#include <gtest/gtest.h>

#include <string>
#include <future>
#include <stop_token>

#include "mcp/Server.h"
#include "mcp/Protocol.h"

using namespace mcp;

static std::future<ToolResult> makeNoopHandler(const JSONValue&, std::stop_token) {
    return std::async(std::launch::deferred, [](){ ToolResult r; r.isError = false; return r; });
}

static JSONRPCRequest makeListToolsReq() {
    JSONRPCRequest req; req.id = std::string("1"); req.method = Methods::ListTools; return req;
}

TEST(ToolsMeta, PresentIsSerialized) {
    Server server("ToolsMeta-Server");

    // Minimal JSON Schema: { type: "object", properties: {} }
    JSONValue::Object schema; schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    schema["properties"] = std::make_shared<JSONValue>(JSONValue::Object{});

    // _meta with UI linkage
    JSONValue::Object metaObj; metaObj["ui/resourceUri"] = std::make_shared<JSONValue>(std::string("ui://demo/widget"));
    std::optional<JSONValue> metaOpt{ JSONValue{metaObj} };

    Tool tool{"echo", "Echo tool", JSONValue{schema}, metaOpt};
    server.RegisterTool(tool, makeNoopHandler);

    auto resp = server.HandleJSONRPC(makeListToolsReq());
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    ASSERT_TRUE(resp->result.has_value());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(resp->result->value));

    const auto& ro = std::get<JSONValue::Object>(resp->result->value);
    auto it = ro.find("tools"); ASSERT_NE(it, ro.end());
    ASSERT_TRUE(it->second);
    ASSERT_TRUE(std::holds_alternative<JSONValue::Array>(it->second->value));

    const auto& arr = std::get<JSONValue::Array>(it->second->value);
    bool found = false;
    for (const auto& v : arr) {
        ASSERT_TRUE(v);
        ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(v->value));
        const auto& to = std::get<JSONValue::Object>(v->value);
        auto nm = to.find("name");
        if (nm != to.end() && std::holds_alternative<std::string>(nm->second->value) && std::get<std::string>(nm->second->value) == "echo") {
            auto mt = to.find("_meta");
            ASSERT_NE(mt, to.end());
            ASSERT_TRUE(mt->second);
            ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(mt->second->value));
            const auto& mo = std::get<JSONValue::Object>(mt->second->value);
            auto uri = mo.find("ui/resourceUri");
            ASSERT_NE(uri, mo.end());
            ASSERT_TRUE(std::holds_alternative<std::string>(uri->second->value));
            EXPECT_EQ(std::get<std::string>(uri->second->value), "ui://demo/widget");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ToolsMeta, AbsentIsOmitted) {
    Server server("ToolsMeta-Server");

    // Register tool without meta
    server.RegisterTool("noMeta", makeNoopHandler);

    auto resp = server.HandleJSONRPC(makeListToolsReq());
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    ASSERT_TRUE(resp->result.has_value());
    const auto& ro = std::get<JSONValue::Object>(resp->result->value);
    const auto& arr = std::get<JSONValue::Array>(ro.at("tools")->value);

    bool found = false;
    for (const auto& v : arr) {
        const auto& to = std::get<JSONValue::Object>(v->value);
        auto nm = to.find("name");
        if (nm != to.end() && std::holds_alternative<std::string>(nm->second->value) && std::get<std::string>(nm->second->value) == "noMeta") {
            EXPECT_TRUE(to.find("_meta") == to.end());
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ToolsMeta, NonObjectMetaPreserved) {
    Server server("ToolsMeta-Server");

    // Minimal schema
    JSONValue::Object schema; schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    schema["properties"] = std::make_shared<JSONValue>(JSONValue::Object{});

    // Meta as a plain number to ensure passthrough of arbitrary JSONValue
    std::optional<JSONValue> metaNum{ JSONValue{ static_cast<int64_t>(42) } };
    Tool tool{"numMeta", "Number meta", JSONValue{schema}, metaNum};
    server.RegisterTool(tool, makeNoopHandler);

    auto resp = server.HandleJSONRPC(makeListToolsReq());
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
    const auto& ro = std::get<JSONValue::Object>(resp->result->value);
    const auto& arr = std::get<JSONValue::Array>(ro.at("tools")->value);

    bool found = false;
    for (const auto& v : arr) {
        const auto& to = std::get<JSONValue::Object>(v->value);
        auto nm = to.find("name");
        if (nm != to.end() && std::holds_alternative<std::string>(nm->second->value) && std::get<std::string>(nm->second->value) == "numMeta") {
            auto mt = to.find("_meta");
            ASSERT_NE(mt, to.end());
            ASSERT_TRUE(std::holds_alternative<int64_t>(mt->second->value));
            EXPECT_EQ(std::get<int64_t>(mt->second->value), 42);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}
