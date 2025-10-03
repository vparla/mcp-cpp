//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_client_paging.cpp
// Purpose: Client paging end-to-end tests using InMemoryTransport
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/Transport.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include <chrono>
#include <optional>
#include <string>
#include <vector>
#include <stop_token>

using namespace mcp;

namespace {

static Tool makeTool(const std::string& name) {
    Tool t;
    t.name = name;
    t.description = std::string("Tool: ") + name;
    // minimal inputSchema: { "type": "object" }
    JSONValue::Object schema;
    schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    t.inputSchema = JSONValue{schema};
    return t;
}

static ReadResourceResult makeReadResult(const std::string& text) {
    ReadResourceResult r;
    JSONValue::Object content;
    content["type"] = std::make_shared<JSONValue>(std::string("text"));
    content["text"] = std::make_shared<JSONValue>(text);
    r.contents.push_back(JSONValue{content});
    return r;
}

} // namespace

TEST(ClientPaging, ToolsListPaged) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Paging Test Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Paging Test Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps; caps.sampling = SamplingCapability{};
    auto initFut = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    // Register 5 tools with metadata
    for (const auto& name : {"a", "b", "c", "d", "e"}) {
        Tool t = makeTool(name);
        server.RegisterTool(t, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
            (void)st;
            return std::async(std::launch::async, [](){ ToolResult r; r.isError = false; return r; });
        });
    }

    // Page 1 (limit 2) => [a, b], nextCursor = "2"
    auto p1 = client->ListToolsPaged(std::optional<std::string>{}, 2);
    ASSERT_EQ(p1.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r1 = p1.get();
    ASSERT_EQ(r1.tools.size(), 2u);
    ASSERT_TRUE(r1.nextCursor.has_value());
    EXPECT_EQ(r1.nextCursor.value(), std::string("2"));

    // Page 2 (cursor 2, limit 2) => [c, d], nextCursor = "4"
    auto p2 = client->ListToolsPaged(std::optional<std::string>("2"), 2);
    ASSERT_EQ(p2.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r2 = p2.get();
    ASSERT_EQ(r2.tools.size(), 2u);
    ASSERT_TRUE(r2.nextCursor.has_value());
    EXPECT_EQ(r2.nextCursor.value(), std::string("4"));

    // Page 3 (cursor 4, limit 2) => [e], nextCursor = none
    auto p3 = client->ListToolsPaged(std::optional<std::string>("4"), 2);
    ASSERT_EQ(p3.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r3 = p3.get();
    ASSERT_EQ(r3.tools.size(), 1u);
    EXPECT_FALSE(r3.nextCursor.has_value());

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ClientPaging, ResourcesListPaged) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Paging Test Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Paging Test Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps; caps.sampling = SamplingCapability{};
    auto initFut = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    // Register 5 resources
    for (const auto& uri : {"mem://1", "mem://2", "mem://3", "mem://4", "mem://5"}) {
        server.RegisterResource(uri, [uri](const std::string&, std::stop_token st) -> std::future<ResourceContent> {
            (void)st;
            return std::async(std::launch::async, [uri]() { return makeReadResult(std::string("data:") + uri); });
        });
    }

    auto p1 = client->ListResourcesPaged(std::optional<std::string>{}, 2);
    ASSERT_EQ(p1.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r1 = p1.get();
    ASSERT_EQ(r1.resources.size(), 2u);
    ASSERT_TRUE(r1.nextCursor.has_value());
    EXPECT_EQ(r1.nextCursor.value(), std::string("2"));

    auto p2 = client->ListResourcesPaged(std::optional<std::string>("2"), 2);
    ASSERT_EQ(p2.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r2 = p2.get();
    ASSERT_EQ(r2.resources.size(), 2u);
    ASSERT_TRUE(r2.nextCursor.has_value());
    EXPECT_EQ(r2.nextCursor.value(), std::string("4"));

    auto p3 = client->ListResourcesPaged(std::optional<std::string>("4"), 2);
    ASSERT_EQ(p3.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r3 = p3.get();
    ASSERT_EQ(r3.resources.size(), 1u);
    EXPECT_FALSE(r3.nextCursor.has_value());

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ClientPaging, ResourceTemplatesListPaged) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Paging Test Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Paging Test Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());
    ClientCapabilities caps; caps.sampling = SamplingCapability{};
    auto initFut = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    // Register 5 templates
    for (size_t i = 1; i <= 5u; ++i) { 
      ResourceTemplate rt{std::string("mem://{") + "k" + std::to_string(i) + "}",
                          std::string("KV") + std::to_string(i),
                          std::optional<std::string>("desc"),
                          std::optional<std::string>("application/json")};
      server.RegisterResourceTemplate(rt);
    }

    auto p1 = client->ListResourceTemplatesPaged(std::optional<std::string>{}, 2);
    ASSERT_EQ(p1.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r1 = p1.get();
    ASSERT_EQ(r1.resourceTemplates.size(), 2u);
    ASSERT_TRUE(r1.nextCursor.has_value());
    EXPECT_EQ(r1.nextCursor.value(), std::string("2"));

    auto p2 = client->ListResourceTemplatesPaged(std::optional<std::string>("2"), 2);
    ASSERT_EQ(p2.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r2 = p2.get();
    ASSERT_EQ(r2.resourceTemplates.size(), 2u);
    ASSERT_TRUE(r2.nextCursor.has_value());
    EXPECT_EQ(r2.nextCursor.value(), std::string("4"));

    auto p3 = client->ListResourceTemplatesPaged(std::optional<std::string>("4"), 2);
    ASSERT_EQ(p3.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r3 = p3.get();
    ASSERT_EQ(r3.resourceTemplates.size(), 1u);
    EXPECT_FALSE(r3.nextCursor.has_value());

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ClientPaging, PromptsListPaged) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Paging Test Server");
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Paging Test Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps; caps.sampling = SamplingCapability{};
    auto initFut = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    // Register 5 prompts
    for (const auto& name : {"p1", "p2", "p3", "p4", "p5"}) {
        server.RegisterPrompt(name, [](const JSONValue&) -> PromptResult {
            PromptResult pr;
            pr.description = "desc";
            // messages = [{ type: "text", text: "hello" }]
            JSONValue::Object msg;
            msg["type"] = std::make_shared<JSONValue>(std::string("text"));
            msg["text"] = std::make_shared<JSONValue>(std::string("hello"));
            pr.messages.push_back(JSONValue{msg});
            return pr;
        });
    }

    auto p1 = client->ListPromptsPaged(std::optional<std::string>{}, 2);
    ASSERT_EQ(p1.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r1 = p1.get();
    ASSERT_EQ(r1.prompts.size(), 2u);
    ASSERT_TRUE(r1.nextCursor.has_value());
    EXPECT_EQ(r1.nextCursor.value(), std::string("2"));

    auto p2 = client->ListPromptsPaged(std::optional<std::string>("2"), 2);
    ASSERT_EQ(p2.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r2 = p2.get();
    ASSERT_EQ(r2.prompts.size(), 2u);
    ASSERT_TRUE(r2.nextCursor.has_value());
    EXPECT_EQ(r2.nextCursor.value(), std::string("4"));

    auto p3 = client->ListPromptsPaged(std::optional<std::string>("4"), 2);
    ASSERT_EQ(p3.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto r3 = p3.get();
    ASSERT_EQ(r3.prompts.size(), 1u);
    EXPECT_FALSE(r3.nextCursor.has_value());

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
