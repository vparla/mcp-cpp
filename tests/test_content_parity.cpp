//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_content_parity.cpp
// Purpose: Tests richer content blocks, tool metadata parity, and structured tool outputs
//==========================================================================================================

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>

#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/Server.h"
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"
#include "mcp/validation/Validation.h"
#include "mcp/validation/Validators.h"

using namespace mcp;

namespace {

JSONValue makeAnnotations() {
    JSONValue::Object annotations;
    JSONValue::Array audience;
    audience.push_back(std::make_shared<JSONValue>(std::string("user")));
    annotations["audience"] = std::make_shared<JSONValue>(audience);
    annotations["priority"] = std::make_shared<JSONValue>(static_cast<int64_t>(1));
    return JSONValue{annotations};
}

JSONValue makeToolSchema(const std::string& propertyName) {
    JSONValue::Object schema;
    schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    JSONValue::Object properties;
    JSONValue::Object property;
    property["type"] = std::make_shared<JSONValue>(std::string("string"));
    properties[propertyName] = std::make_shared<JSONValue>(property);
    schema["properties"] = std::make_shared<JSONValue>(properties);
    return JSONValue{schema};
}

}  // namespace

TEST(ContentParity, TypedBuildersAndValidatorsAcceptRichContent) {
    const JSONValue text = typed::makeText("hello");
    const JSONValue image = typed::makeImage("image/png", "iVBORw0KGgo=");
    const JSONValue audio = typed::makeAudio("audio/wav", "UklGRg==");
    const JSONValue resourceLink = typed::makeResourceLink(
        "file:///tmp/report.json",
        "report",
        std::optional<std::string>{"Report"},
        std::optional<std::string>{"Generated report"},
        std::optional<std::string>{"application/json"});
    const JSONValue embeddedText = typed::makeEmbeddedTextResource("mem://inline", "inline text", std::optional<std::string>{"text/plain"});
    const JSONValue embeddedBlob = typed::makeEmbeddedBlobResource("mem://blob", "AAEC", "application/octet-stream");

    EXPECT_TRUE(typed::isText(text));
    EXPECT_TRUE(typed::isImage(image));
    EXPECT_TRUE(typed::isAudio(audio));
    EXPECT_TRUE(typed::isResourceLink(resourceLink));
    EXPECT_TRUE(typed::isEmbeddedResource(embeddedText));
    EXPECT_TRUE(typed::isEmbeddedResource(embeddedBlob));

    JSONValue::Array contentArray;
    contentArray.push_back(std::make_shared<JSONValue>(text));
    contentArray.push_back(std::make_shared<JSONValue>(image));
    contentArray.push_back(std::make_shared<JSONValue>(audio));
    contentArray.push_back(std::make_shared<JSONValue>(resourceLink));
    contentArray.push_back(std::make_shared<JSONValue>(embeddedText));
    contentArray.push_back(std::make_shared<JSONValue>(embeddedBlob));

    JSONValue::Object callToolResult;
    callToolResult["content"] = std::make_shared<JSONValue>(contentArray);
    JSONValue::Object structured;
    structured["ok"] = std::make_shared<JSONValue>(true);
    callToolResult["structuredContent"] = std::make_shared<JSONValue>(structured);
    JSONValue::Object meta;
    meta["source"] = std::make_shared<JSONValue>(std::string("unit-test"));
    callToolResult["_meta"] = std::make_shared<JSONValue>(meta);
    EXPECT_TRUE(validation::validateCallToolResultJson(JSONValue{callToolResult}));

    JSONValue::Object resourceResult;
    resourceResult["contents"] = std::make_shared<JSONValue>(contentArray);
    EXPECT_TRUE(validation::validateReadResourceResultJson(JSONValue{resourceResult}));

    JSONValue::Object promptMessage;
    promptMessage["role"] = std::make_shared<JSONValue>(std::string("assistant"));
    JSONValue::Array promptContent;
    promptContent.push_back(std::make_shared<JSONValue>(text));
    promptContent.push_back(std::make_shared<JSONValue>(image));
    promptMessage["content"] = std::make_shared<JSONValue>(promptContent);

    JSONValue::Object promptResult;
    promptResult["description"] = std::make_shared<JSONValue>(std::string("rich prompt"));
    JSONValue::Array messages;
    messages.push_back(std::make_shared<JSONValue>(promptMessage));
    promptResult["messages"] = std::make_shared<JSONValue>(messages);
    EXPECT_TRUE(validation::validateGetPromptResultJson(JSONValue{promptResult}));
}

TEST(ContentParity, ToolMetadataAndStructuredOutputsRoundTrip) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Content Parity Server");
    server.SetValidationMode(validation::ValidationMode::Strict);

    Tool tool;
    tool.name = "rich-tool";
    tool.description = "Returns rich content";
    tool.inputSchema = makeToolSchema("input");
    tool.outputSchema = makeToolSchema("output");
    tool.annotations = makeAnnotations();
    JSONValue::Object execution;
    execution["destructive"] = std::make_shared<JSONValue>(false);
    tool.execution = JSONValue{execution};
    JSONValue::Object meta;
    meta["category"] = std::make_shared<JSONValue>(std::string("rich"));
    tool.meta = JSONValue{meta};

    server.RegisterTool(tool, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        (void)st;
        return std::async(std::launch::deferred, []() {
            ToolResult result;
            result.content.push_back(typed::makeText("done"));
            result.content.push_back(typed::makeResourceLink(
                "file:///tmp/report.json",
                "report",
                std::optional<std::string>{"Report"},
                std::optional<std::string>{"Generated report"},
                std::optional<std::string>{"application/json"}));
            JSONValue::Object structured;
            structured["status"] = std::make_shared<JSONValue>(std::string("ok"));
            result.structuredContent = JSONValue{structured};
            JSONValue::Object metaValue;
            metaValue["origin"] = std::make_shared<JSONValue>(std::string("tool"));
            result.meta = JSONValue{metaValue};
            return result;
        });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Content Parity Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps;
    auto initFuture = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFuture.get();

    auto toolsFuture = client->ListToolsPaged(std::nullopt, std::nullopt);
    ASSERT_EQ(toolsFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto tools = toolsFuture.get();
    ASSERT_EQ(tools.tools.size(), 1u);
    const auto& listedTool = tools.tools.front();
    ASSERT_TRUE(listedTool.outputSchema.has_value());
    ASSERT_TRUE(listedTool.annotations.has_value());
    ASSERT_TRUE(listedTool.execution.has_value());
    ASSERT_TRUE(listedTool.meta.has_value());

    JSONValue args{JSONValue::Object{}};
    auto callFuture = typed::callTool(*client, "rich-tool", args);
    ASSERT_EQ(callFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto result = callFuture.get();

    EXPECT_FALSE(result.isError);
    ASSERT_EQ(result.content.size(), 2u);
    auto text = typed::firstText(result);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(text.value(), "done");
    ASSERT_TRUE(result.structuredContent.has_value());
    ASSERT_TRUE(result.meta.has_value());
    EXPECT_TRUE(validation::validateCallToolResult(result));

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
