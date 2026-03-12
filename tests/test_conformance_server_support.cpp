//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_conformance_server_support.cpp
// Purpose: Tests the shared MCP server conformance fixture used by the example server and Docker automation
//==========================================================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/Server.h"
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"
#include "mcp/validation/Validation.h"
#include "src/mcp/ConformanceServerSupport.h"

using namespace mcp;

namespace {

template <typename Predicate>
bool waitUntil(Predicate&& predicate,
               const std::chrono::milliseconds timeout = std::chrono::milliseconds(2000),
               const std::chrono::milliseconds interval = std::chrono::milliseconds(25)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(interval);
    }
    return predicate();
}

std::string firstTextFromPrompt(const JSONValue& promptResult) {
    if (!std::holds_alternative<JSONValue::Object>(promptResult.value)) {
        return {};
    }
    const auto& resultObject = std::get<JSONValue::Object>(promptResult.value);
    auto messagesIt = resultObject.find("messages");
    if (messagesIt == resultObject.end() || !messagesIt->second ||
        !std::holds_alternative<JSONValue::Array>(messagesIt->second->value)) {
        return {};
    }
    const auto& messages = std::get<JSONValue::Array>(messagesIt->second->value);
    for (const auto& message : messages) {
        if (!message || !std::holds_alternative<JSONValue::Object>(message->value)) {
            continue;
        }
        const auto& messageObject = std::get<JSONValue::Object>(message->value);
        auto contentIt = messageObject.find("content");
        if (contentIt == messageObject.end() || !contentIt->second) {
            continue;
        }
        if (std::holds_alternative<JSONValue::Array>(contentIt->second->value)) {
            const auto& items = std::get<JSONValue::Array>(contentIt->second->value);
            for (const auto& item : items) {
                if (!item) {
                    continue;
                }
                auto text = typed::getText(*item);
                if (text.has_value()) {
                    return text.value();
                }
            }
            continue;
        }
        auto text = typed::getText(*contentIt->second);
        if (text.has_value()) {
            return text.value();
        }
    }
    return {};
}

std::string firstTextFromRead(const JSONValue& readResult) {
    if (!std::holds_alternative<JSONValue::Object>(readResult.value)) {
        return {};
    }
    const auto& resultObject = std::get<JSONValue::Object>(readResult.value);
    auto contentsIt = resultObject.find("contents");
    if (contentsIt == resultObject.end() || !contentsIt->second ||
        !std::holds_alternative<JSONValue::Array>(contentsIt->second->value)) {
        return {};
    }
    const auto& contents = std::get<JSONValue::Array>(contentsIt->second->value);
    for (const auto& item : contents) {
        if (!item || !std::holds_alternative<JSONValue::Object>(item->value)) {
            continue;
        }
        const auto& contentObject = std::get<JSONValue::Object>(item->value);
        auto textIt = contentObject.find("text");
        if (textIt != contentObject.end() && textIt->second &&
            std::holds_alternative<std::string>(textIt->second->value)) {
            return std::get<std::string>(textIt->second->value);
        }
    }
    return {};
}

}  // namespace

TEST(ConformanceServerSupport, RegistersExpectedSurface) {
    Server server("Conformance Surface Server");
    conformance::RegisterConformanceServerProfile(server);

    const auto tools = server.ListTools();
    const auto resources = server.ListResources();
    const auto resourceTemplates = server.ListResourceTemplates();
    const auto prompts = server.ListPrompts();

    auto hasTool = [&](const std::string& name) {
        return std::find_if(tools.begin(), tools.end(), [&](const Tool& tool) { return tool.name == name; }) != tools.end();
    };
    auto hasResource = [&](const std::string& uri) {
        return std::find_if(resources.begin(), resources.end(), [&](const Resource& resource) { return resource.uri == uri; }) != resources.end();
    };
    auto hasPrompt = [&](const std::string& name) {
        return std::find_if(prompts.begin(), prompts.end(), [&](const Prompt& prompt) { return prompt.name == name; }) != prompts.end();
    };

    EXPECT_TRUE(hasTool("test_simple_text"));
    EXPECT_TRUE(hasTool("test_sampling"));
    EXPECT_TRUE(hasTool("test_elicitation"));
    EXPECT_TRUE(hasTool("json_schema_2020_12_tool"));
    EXPECT_TRUE(hasResource("test://static-text"));
    EXPECT_TRUE(hasResource("test://static-binary"));
    EXPECT_TRUE(hasPrompt("test_simple_prompt"));
    EXPECT_TRUE(hasPrompt("test_prompt_with_image"));
    ASSERT_EQ(resourceTemplates.size(), 1u);
    EXPECT_EQ(resourceTemplates.front().uriTemplate, "test://template/{id}/data");

    const auto schemaToolIt = std::find_if(tools.begin(), tools.end(), [](const Tool& tool) {
        return tool.name == "json_schema_2020_12_tool";
    });
    ASSERT_NE(schemaToolIt, tools.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(schemaToolIt->inputSchema.value));
    const auto& schemaObject = std::get<JSONValue::Object>(schemaToolIt->inputSchema.value);
    ASSERT_TRUE(schemaObject.find("$schema") != schemaObject.end());
    ASSERT_TRUE(schemaObject.find("$defs") != schemaObject.end());
    ASSERT_TRUE(schemaObject.find("additionalProperties") != schemaObject.end());
}

TEST(ConformanceServerSupport, EndToEndResourcesPromptsAndSchema) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Conformance Resource Server");
    server.SetValidationMode(validation::ValidationMode::Strict);
    conformance::RegisterConformanceServerProfile(server);
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Conformance Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps;
    auto initFuture = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFuture.get();

    auto readFuture = client->ReadResource("test://template/123/data");
    ASSERT_EQ(readFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_NE(firstTextFromRead(readFuture.get()).find("123"), std::string::npos);

    JSONValue::Object promptArgs;
    promptArgs["arg1"] = std::make_shared<JSONValue>(std::string("testValue1"));
    promptArgs["arg2"] = std::make_shared<JSONValue>(std::string("testValue2"));
    auto promptFuture = client->GetPrompt("test_prompt_with_arguments", JSONValue{promptArgs});
    ASSERT_EQ(promptFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    const auto promptText = firstTextFromPrompt(promptFuture.get());
    EXPECT_NE(promptText.find("testValue1"), std::string::npos);
    EXPECT_NE(promptText.find("testValue2"), std::string::npos);

    auto toolsFuture = client->ListToolsPaged(std::nullopt, std::nullopt);
    ASSERT_EQ(toolsFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    const auto listedTools = toolsFuture.get();
    const auto schemaToolIt = std::find_if(listedTools.tools.begin(), listedTools.tools.end(), [](const Tool& tool) {
        return tool.name == "json_schema_2020_12_tool";
    });
    ASSERT_NE(schemaToolIt, listedTools.tools.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(schemaToolIt->inputSchema.value));
    const auto& schemaObject = std::get<JSONValue::Object>(schemaToolIt->inputSchema.value);
    auto schemaIt = schemaObject.find("$schema");
    ASSERT_TRUE(schemaIt != schemaObject.end());
    ASSERT_TRUE(schemaIt->second);
    ASSERT_TRUE(std::holds_alternative<std::string>(schemaIt->second->value));
    EXPECT_EQ(std::get<std::string>(schemaIt->second->value), "https://json-schema.org/draft/2020-12/schema");

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ConformanceServerSupport, EndToEndLoggingProgressSamplingAndElicitation) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Conformance Tool Server");
    server.SetValidationMode(validation::ValidationMode::Strict);
    conformance::RegisterConformanceServerProfile(server);
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Conformance Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);

    std::atomic<int> logCount{0};
    std::vector<double> progressValues;
    std::mutex progressMutex;

    client->SetNotificationHandler(Methods::Log, [&](const std::string&, const JSONValue&) {
        ++logCount;
    });
    client->SetProgressHandler([&](const std::string&, double progress, const std::string&) {
        std::lock_guard<std::mutex> lock(progressMutex);
        progressValues.push_back(progress);
    });
    client->SetSamplingHandler([](const JSONValue&, const JSONValue&, const JSONValue&, const JSONValue&) {
        return std::async(std::launch::deferred, []() {
            JSONValue::Object result;
            result["model"] = std::make_shared<JSONValue>(std::string("test-model"));
            result["role"] = std::make_shared<JSONValue>(std::string("assistant"));
            JSONValue::Array content;
            content.push_back(
                std::make_shared<JSONValue>(typed::makeText("This is a test response from the client")));
            result["content"] = std::make_shared<JSONValue>(content);
            return JSONValue{result};
        });
    });
    client->SetElicitationHandler([](const ElicitationRequest&) -> std::future<ElicitationResult> {
        return std::async(std::launch::deferred, []() {
            ElicitationResult result;
            result.action = "accept";
            JSONValue::Object content;
            content["username"] = std::make_shared<JSONValue>(std::string("testuser"));
            content["email"] = std::make_shared<JSONValue>(std::string("test@example.com"));
            result.content = JSONValue{content};
            return result;
        });
    });

    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps;
    caps.sampling = SamplingCapability{};
    caps.elicitation = ElicitationCapability{{"form"}};
    auto initFuture = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFuture.get();

    auto loggingFuture = typed::callTool(*client, "test_tool_with_logging", JSONValue{JSONValue::Object{}});
    ASSERT_EQ(loggingFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto loggingResult = loggingFuture.get();
    EXPECT_FALSE(loggingResult.isError);
    ASSERT_TRUE(waitUntil([&]() { return logCount.load() >= 3; }));

    auto progressFuture = typed::callTool(*client, "test_tool_with_progress", JSONValue{JSONValue::Object{}});
    ASSERT_EQ(progressFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto progressResult = progressFuture.get();
    EXPECT_FALSE(progressResult.isError);
    ASSERT_TRUE(waitUntil([&]() {
        std::lock_guard<std::mutex> lock(progressMutex);
        return progressValues.size() >= 3;
    }));
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        ASSERT_GE(progressValues.size(), 3u);
        EXPECT_LE(progressValues[0], progressValues[1]);
        EXPECT_LE(progressValues[1], progressValues[2]);
    }

    JSONValue::Object samplingArgs;
    samplingArgs["prompt"] = std::make_shared<JSONValue>(std::string("Test prompt for sampling"));
    auto samplingFuture = typed::callTool(*client, "test_sampling", JSONValue{samplingArgs});
    ASSERT_EQ(samplingFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto samplingResult = samplingFuture.get();
    auto samplingText = typed::firstText(samplingResult);
    ASSERT_TRUE(samplingText.has_value());
    EXPECT_NE(samplingText->find("This is a test response from the client"), std::string::npos);

    JSONValue::Object elicitationArgs;
    elicitationArgs["message"] = std::make_shared<JSONValue>(std::string("Please provide your information"));
    auto elicitationFuture = typed::callTool(*client, "test_elicitation", JSONValue{elicitationArgs});
    ASSERT_EQ(elicitationFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto elicitationResult = elicitationFuture.get();
    auto elicitationText = typed::firstText(elicitationResult);
    ASSERT_TRUE(elicitationText.has_value());
    EXPECT_NE(elicitationText->find("accept"), std::string::npos);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
