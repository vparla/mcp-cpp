//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_resource_template_reads.cpp
// Purpose: Tests template-backed resource reads and resolution precedence
//==========================================================================================================

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>

#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Server.h"

using namespace mcp;

namespace {

std::string firstTextContent(const JSONValue& readResult) {
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
    if (contents.empty() || !contents.front() ||
        !std::holds_alternative<JSONValue::Object>(contents.front()->value)) {
        return {};
    }
    const auto& first = std::get<JSONValue::Object>(contents.front()->value);
    auto textIt = first.find("text");
    if (textIt == first.end() || !textIt->second ||
        !std::holds_alternative<std::string>(textIt->second->value)) {
        return {};
    }
    return std::get<std::string>(textIt->second->value);
}

}  // namespace

TEST(ResourceTemplateReads, ClientReadsTemplateBackedResource) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Template Resource Server");
    server.RegisterResourceTemplate(
        ResourceTemplate{"test://template/{id}/data",
                         "Template Data",
                         std::optional<std::string>{"Template-backed resource"},
                         std::optional<std::string>{"application/json"}},
        [](const std::string& uri,
           const ResourceTemplateVariables& variables,
           std::stop_token st) -> std::future<ReadResourceResult> {
            (void)st;
            return std::async(std::launch::deferred, [uri, variables]() {
                ReadResourceResult result;
                JSONValue::Object content;
                content["uri"] = std::make_shared<JSONValue>(uri);
                content["mimeType"] = std::make_shared<JSONValue>(std::string("application/json"));
                const auto it = variables.find("id");
                const std::string id = it != variables.end() ? it->second : std::string("missing");
                content["text"] = std::make_shared<JSONValue>(std::string("{\"id\":\"") + id + std::string("\"}"));
                result.contents.push_back(JSONValue{content});
                return result;
            });
        });
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Template Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps;
    auto initFuture = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFuture.get();

    auto readFuture = client->ReadResource("test://template/123/data");
    ASSERT_EQ(readFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    const JSONValue readResult = readFuture.get();
    EXPECT_EQ(firstTextContent(readResult), "{\"id\":\"123\"}");

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceTemplateReads, DirectResourceWinsOverTemplateMatch) {
    Server server("Template Precedence Server");
    server.RegisterResource(
        "test://template/direct/data",
        [](const std::string& uri, std::stop_token st) -> std::future<ReadResourceResult> {
            (void)st;
            return std::async(std::launch::deferred, [uri]() {
                ReadResourceResult result;
                JSONValue::Object content;
                content["uri"] = std::make_shared<JSONValue>(uri);
                content["mimeType"] = std::make_shared<JSONValue>(std::string("text/plain"));
                content["text"] = std::make_shared<JSONValue>(std::string("direct"));
                result.contents.push_back(JSONValue{content});
                return result;
            });
        });
    server.RegisterResourceTemplate(
        ResourceTemplate{"test://template/{id}/data",
                         "Template Data",
                         std::optional<std::string>{"Template-backed resource"},
                         std::optional<std::string>{"text/plain"}},
        [](const std::string& uri,
           const ResourceTemplateVariables& variables,
           std::stop_token st) -> std::future<ReadResourceResult> {
            (void)st;
            return std::async(std::launch::deferred, [uri, variables]() {
                ReadResourceResult result;
                JSONValue::Object content;
                content["uri"] = std::make_shared<JSONValue>(uri);
                content["mimeType"] = std::make_shared<JSONValue>(std::string("text/plain"));
                const auto it = variables.find("id");
                content["text"] = std::make_shared<JSONValue>(
                    it != variables.end() ? it->second : std::string("template"));
                result.contents.push_back(JSONValue{content});
                return result;
            });
        });

    const JSONValue readResult = server.ReadResource("test://template/direct/data").get();
    EXPECT_EQ(firstTextContent(readResult), "direct");
}

TEST(ResourceTemplateReads, AmbiguousTemplateMatchFailsClosed) {
    Server server("Ambiguous Template Server");
    server.RegisterResourceTemplate(
        ResourceTemplate{"test://template/{id}/data",
                         "Template A",
                         std::optional<std::string>{"Ambiguous template A"},
                         std::optional<std::string>{"text/plain"}},
        [](const std::string&, const ResourceTemplateVariables&, std::stop_token) -> std::future<ReadResourceResult> {
            return std::async(std::launch::deferred, []() { return ReadResourceResult{}; });
        });
    server.RegisterResourceTemplate(
        ResourceTemplate{"test://template/{name}/data",
                         "Template B",
                         std::optional<std::string>{"Ambiguous template B"},
                         std::optional<std::string>{"text/plain"}},
        [](const std::string&, const ResourceTemplateVariables&, std::stop_token) -> std::future<ReadResourceResult> {
            return std::async(std::launch::deferred, []() { return ReadResourceResult{}; });
        });

    EXPECT_THROW((void)server.ReadResource("test://template/123/data").get(), std::runtime_error);
}
