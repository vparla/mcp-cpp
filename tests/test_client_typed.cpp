//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_client_typed.cpp
// Purpose: GoogleTests for header-only typed client wrappers (tools/resources/prompts + paging helpers)
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/typed/ClientTyped.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/Protocol.h"
#include "mcp/typed/Content.h"
#include <chrono>
#include <stop_token>

using namespace mcp;

namespace {

static ToolResult makeToolOk(const std::string& text) {
    ToolResult r; r.isError = false;
    JSONValue::Object msg;
    msg["type"] = std::make_shared<JSONValue>(std::string("text"));
    msg["text"] = std::make_shared<JSONValue>(text);
    r.content.push_back(JSONValue{msg});
    return r;
}

static ResourceContent makeResourceContent(const std::string& text) {
    ResourceContent r;
    JSONValue::Object msg;
    msg["type"] = std::make_shared<JSONValue>(std::string("text"));
    msg["text"] = std::make_shared<JSONValue>(text);
    r.contents.push_back(JSONValue{msg});
    return r;
}

static PromptResult makePromptResult(const std::string& text) {
    PromptResult pr;
    pr.description = "desc";
    JSONValue::Object msg;
    msg["type"] = std::make_shared<JSONValue>(std::string("text"));
    msg["text"] = std::make_shared<JSONValue>(text);
    pr.messages.push_back(JSONValue{msg});
    return pr;
}

} // namespace

TEST(ClientTyped, CallTool_Basic) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Typed Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    // Register tool with metadata + handler
    Tool meta; meta.name = "echo"; meta.description = "echo";
    JSONValue::Object schema; schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    meta.inputSchema = JSONValue{schema};
    server.RegisterTool(meta, [](const JSONValue& args, std::stop_token st) -> std::future<ToolResult> {
        (void)st; (void)args;
        return std::async(std::launch::async, [](){ return makeToolOk("ok"); });
    });

    ClientFactory factory;
    Implementation clientInfo{"Typed Client","1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());
    ClientCapabilities caps; auto capsFut = client->Initialize(clientInfo, caps);
    ASSERT_EQ(capsFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Typed call
    JSONValue::Object argsObj; argsObj["x"] = std::make_shared<JSONValue>(static_cast<int64_t>(1));
    auto fut = typed::callTool(*client, "echo", JSONValue{argsObj});
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r = fut.get();
    ASSERT_FALSE(r.isError);
    ASSERT_EQ(r.content.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(r.content[0].value));
    const auto& o = std::get<JSONValue::Object>(r.content[0].value);
    auto it = o.find("text");
    ASSERT_TRUE(it != o.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(it->second->value));
    EXPECT_EQ(std::get<std::string>(it->second->value), std::string("ok"));

    // Content helpers
    auto ft = typed::firstText(r);
    ASSERT_TRUE(ft.has_value());
    EXPECT_EQ(ft.value(), std::string("ok"));
    auto all = typed::collectText(r);
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0], std::string("ok"));

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ClientTyped, ReadResource_Basic) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Typed Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    server.RegisterResource("mem://foo", [](const std::string& uri, std::stop_token st) -> std::future<ResourceContent> {
        (void)st; (void)uri;
        return std::async(std::launch::async, [](){ return makeResourceContent("R"); });
    });

    ClientFactory factory;
    Implementation clientInfo{"Typed Client","1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());
    ClientCapabilities caps; (void)client->Initialize(clientInfo, caps).get();

    auto fut = typed::readResource(*client, "mem://foo");
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r = fut.get();
    ASSERT_EQ(r.contents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(r.contents[0].value));
    const auto& o = std::get<JSONValue::Object>(r.contents[0].value);
    auto it = o.find("text");
    ASSERT_TRUE(it != o.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(it->second->value));
    EXPECT_EQ(std::get<std::string>(it->second->value), std::string("R"));

    auto ft = typed::firstText(r);
    ASSERT_TRUE(ft.has_value());
    EXPECT_EQ(ft.value(), std::string("R"));
    auto all = typed::collectText(r);
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0], std::string("R"));

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ClientTyped, GetPrompt_Basic) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Typed Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    server.RegisterPrompt("hello", [](const JSONValue&) -> PromptResult { return makePromptResult("hello"); });

    ClientFactory factory;
    Implementation clientInfo{"Typed Client","1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());
    ClientCapabilities caps; (void)client->Initialize(clientInfo, caps).get();

    JSONValue args; // empty
    auto fut = typed::getPrompt(*client, "hello", args);
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r = fut.get();
    EXPECT_EQ(r.description, std::string("desc"));
    ASSERT_EQ(r.messages.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(r.messages[0].value));

    auto ft = typed::firstText(r);
    ASSERT_TRUE(ft.has_value());
    EXPECT_EQ(ft.value(), std::string("hello"));
    auto all = typed::collectText(r);
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0], std::string("hello"));

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ClientTyped, PagingHelpers_ListAllTools) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Typed Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    // Register tools
    for (const auto& name : {"t1","t2","t3"}) {
        Tool t; t.name = name; t.description = name; JSONValue::Object schema; schema["type"] = std::make_shared<JSONValue>(std::string("object")); t.inputSchema = JSONValue{schema};
        server.RegisterTool(t, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
            (void)st; return std::async(std::launch::async, [](){ ToolResult r; return r; });
        });
    }

    ClientFactory factory;
    Implementation clientInfo{"Typed Client","1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());
    ClientCapabilities caps; (void)client->Initialize(clientInfo, caps).get();

    auto fut = typed::listAllTools(*client, 1);
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto all = fut.get();
    ASSERT_EQ(all.size(), 3u);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ClientTyped, WrappersThrowOnServerError) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Typed Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Typed Client","1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());
    ClientCapabilities caps; (void)client->Initialize(clientInfo, caps).get();

    // read non-existent resource -> server error -> wrapper should throw
    EXPECT_THROW({ auto f = typed::readResource(*client, "mem://missing"); (void)f.get(); }, std::runtime_error);

    // call non-existent tool -> wrapper should throw
    JSONValue args; EXPECT_THROW({ auto f = typed::callTool(*client, "missing", args); (void)f.get(); }, std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
