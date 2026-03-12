//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_icon_parity.cpp
// Purpose: Tests icon and metadata parity for listable MCP protocol entities
//==========================================================================================================

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>
#include <vector>

#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/Server.h"
#include "mcp/validation/Validation.h"
#include "mcp/validation/Validators.h"

using namespace mcp;

namespace {

std::vector<Icon> makeIcons() {
    return {
        Icon{"https://example.com/light.png",
             std::optional<std::string>{"image/png"},
             std::optional<std::vector<std::string>>{{"16x16", "32x32"}},
             std::optional<std::string>{"light"}},
        Icon{"https://example.com/dark.png",
             std::optional<std::string>{"image/png"},
             std::optional<std::vector<std::string>>{{"16x16"}},
             std::optional<std::string>{"dark"}},
    };
}

JSONValue makeSchema(const std::string& propertyName) {
    JSONValue::Object schema;
    schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    JSONValue::Object properties;
    JSONValue::Object property;
    property["type"] = std::make_shared<JSONValue>(std::string("string"));
    properties[propertyName] = std::make_shared<JSONValue>(property);
    schema["properties"] = std::make_shared<JSONValue>(properties);
    return JSONValue{schema};
}

JSONValue makeAnnotations() {
    JSONValue::Object annotations;
    JSONValue::Array audience;
    audience.push_back(std::make_shared<JSONValue>(std::string("user")));
    annotations["audience"] = std::make_shared<JSONValue>(audience);
    annotations["priority"] = std::make_shared<JSONValue>(static_cast<int64_t>(1));
    return JSONValue{annotations};
}

JSONValue makeMeta(const std::string& key, const std::string& value) {
    JSONValue::Object meta;
    meta[key] = std::make_shared<JSONValue>(value);
    return JSONValue{meta};
}

JSONValue makeIconJson(const std::string& src,
                       const std::optional<std::string>& theme = std::nullopt,
                       const std::optional<JSONValue::Array>& sizes = std::nullopt) {
    JSONValue::Object icon;
    icon["src"] = std::make_shared<JSONValue>(src);
    icon["mimeType"] = std::make_shared<JSONValue>(std::string("image/png"));
    if (sizes.has_value()) {
        icon["sizes"] = std::make_shared<JSONValue>(sizes.value());
    }
    if (theme.has_value()) {
        icon["theme"] = std::make_shared<JSONValue>(theme.value());
    }
    return JSONValue{icon};
}

JSONValue::Array makeValidIconArray() {
    JSONValue::Array icons;
    JSONValue::Array sizes;
    sizes.push_back(std::make_shared<JSONValue>(std::string("16x16")));
    icons.push_back(std::make_shared<JSONValue>(makeIconJson("https://example.com/icon.png", "light", sizes)));
    return icons;
}

std::future<ResourceContent> makeResourceHandler(const std::string& expectedUri) {
    return std::async(std::launch::deferred, [expectedUri]() {
        ReadResourceResult result;
        JSONValue::Object item;
        item["type"] = std::make_shared<JSONValue>(std::string("text"));
        item["text"] = std::make_shared<JSONValue>(expectedUri);
        result.contents.push_back(JSONValue{item});
        return result;
    });
}

}  // namespace

TEST(IconParity, ClientRoundTripAcrossListSurfaces) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Icon Parity Server");
    server.SetValidationMode(validation::ValidationMode::Strict);

    Tool tool;
    tool.name = "icon-tool";
    tool.title = "Icon Tool";
    tool.description = "Returns icon-aware metadata";
    tool.inputSchema = makeSchema("input");
    tool.meta = makeMeta("category", "icons");
    tool.icons = makeIcons();
    server.RegisterTool(tool, [](const JSONValue&, std::stop_token) -> std::future<ToolResult> {
        return std::async(std::launch::deferred, []() {
            ToolResult result;
            JSONValue::Object item;
            item["type"] = std::make_shared<JSONValue>(std::string("text"));
            item["text"] = std::make_shared<JSONValue>(std::string("ok"));
            result.content.push_back(JSONValue{item});
            return result;
        });
    });

    Resource resource{
        "file:///tmp/icon-resource.txt",
        "Icon Resource",
        std::optional<std::string>{"Resource description"},
        std::optional<std::string>{"text/plain"},
        std::optional<std::string>{"Resource Title"},
        std::optional<int64_t>{512},
        std::optional<JSONValue>{makeAnnotations()},
        std::optional<JSONValue>{makeMeta("origin", "resource")},
        std::optional<std::vector<Icon>>{makeIcons()}
    };
    server.RegisterResource(resource, [uri = resource.uri](const std::string&, std::stop_token) {
        return makeResourceHandler(uri);
    });

    ResourceTemplate resourceTemplate{
        "file:///{path}",
        "Icon Template",
        std::optional<std::string>{"Template description"},
        std::optional<std::string>{"text/plain"},
        std::optional<std::string>{"Template Title"},
        std::optional<JSONValue>{makeAnnotations()},
        std::optional<JSONValue>{makeMeta("origin", "template")},
        std::optional<std::vector<Icon>>{makeIcons()}
    };
    server.RegisterResourceTemplate(resourceTemplate);

    Prompt prompt{
        "icon-prompt",
        "Prompt description",
        std::optional<JSONValue>{makeSchema("topic")},
        std::optional<std::string>{"Prompt Title"},
        std::optional<JSONValue>{makeMeta("origin", "prompt")},
        std::optional<std::vector<Icon>>{makeIcons()}
    };
    server.RegisterPrompt(prompt, [](const JSONValue&) -> PromptResult {
        PromptResult result;
        result.description = "Prompt result";
        JSONValue::Object content;
        content["type"] = std::make_shared<JSONValue>(std::string("text"));
        content["text"] = std::make_shared<JSONValue>(std::string("hello"));
        JSONValue::Object message;
        message["role"] = std::make_shared<JSONValue>(std::string("assistant"));
        message["content"] = std::make_shared<JSONValue>(content);
        result.messages.push_back(JSONValue{message});
        return result;
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Icon Parity Client", "1.0.0"};
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
    EXPECT_EQ(tools.tools.front().name, tool.name);
    ASSERT_TRUE(tools.tools.front().title.has_value());
    EXPECT_EQ(tools.tools.front().title.value(), tool.title.value());
    ASSERT_TRUE(tools.tools.front().icons.has_value());
    ASSERT_EQ(tools.tools.front().icons->size(), 2u);
    EXPECT_EQ(tools.tools.front().icons->front().theme.value(), "light");

    auto resourcesFuture = client->ListResourcesPaged(std::nullopt, std::nullopt);
    ASSERT_EQ(resourcesFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resources = resourcesFuture.get();
    ASSERT_EQ(resources.resources.size(), 1u);
    EXPECT_EQ(resources.resources.front().uri, resource.uri);
    ASSERT_TRUE(resources.resources.front().title.has_value());
    EXPECT_EQ(resources.resources.front().title.value(), resource.title.value());
    ASSERT_TRUE(resources.resources.front().size.has_value());
    EXPECT_EQ(resources.resources.front().size.value(), resource.size.value());
    ASSERT_TRUE(resources.resources.front().annotations.has_value());
    ASSERT_TRUE(resources.resources.front().meta.has_value());
    ASSERT_TRUE(resources.resources.front().icons.has_value());
    EXPECT_EQ(resources.resources.front().icons->back().theme.value(), "dark");

    auto templatesFuture = client->ListResourceTemplatesPaged(std::nullopt, std::nullopt);
    ASSERT_EQ(templatesFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto templates = templatesFuture.get();
    ASSERT_EQ(templates.resourceTemplates.size(), 1u);
    EXPECT_EQ(templates.resourceTemplates.front().uriTemplate, resourceTemplate.uriTemplate);
    ASSERT_TRUE(templates.resourceTemplates.front().title.has_value());
    EXPECT_EQ(templates.resourceTemplates.front().title.value(), resourceTemplate.title.value());
    ASSERT_TRUE(templates.resourceTemplates.front().annotations.has_value());
    ASSERT_TRUE(templates.resourceTemplates.front().meta.has_value());
    ASSERT_TRUE(templates.resourceTemplates.front().icons.has_value());

    auto promptsFuture = client->ListPromptsPaged(std::nullopt, std::nullopt);
    ASSERT_EQ(promptsFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto prompts = promptsFuture.get();
    ASSERT_EQ(prompts.prompts.size(), 1u);
    EXPECT_EQ(prompts.prompts.front().name, prompt.name);
    ASSERT_TRUE(prompts.prompts.front().title.has_value());
    EXPECT_EQ(prompts.prompts.front().title.value(), prompt.title.value());
    ASSERT_TRUE(prompts.prompts.front().arguments.has_value());
    ASSERT_TRUE(prompts.prompts.front().meta.has_value());
    ASSERT_TRUE(prompts.prompts.front().icons.has_value());

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(IconParity, StrictValidatorsAcceptListShapesWithIcons) {
    JSONValue::Array icons = makeValidIconArray();

    JSONValue::Object tool;
    tool["name"] = std::make_shared<JSONValue>(std::string("icon-tool"));
    tool["title"] = std::make_shared<JSONValue>(std::string("Icon Tool"));
    tool["description"] = std::make_shared<JSONValue>(std::string("Tool description"));
    tool["inputSchema"] = std::make_shared<JSONValue>(makeSchema("input"));
    tool["icons"] = std::make_shared<JSONValue>(icons);
    JSONValue::Object toolsResult;
    JSONValue::Array toolsArray;
    toolsArray.push_back(std::make_shared<JSONValue>(tool));
    toolsResult["tools"] = std::make_shared<JSONValue>(toolsArray);
    EXPECT_TRUE(validation::validateToolsListResultJson(JSONValue{toolsResult}));

    JSONValue::Object resource;
    resource["uri"] = std::make_shared<JSONValue>(std::string("file:///tmp/icon-resource.txt"));
    resource["name"] = std::make_shared<JSONValue>(std::string("Icon Resource"));
    resource["title"] = std::make_shared<JSONValue>(std::string("Resource Title"));
    resource["size"] = std::make_shared<JSONValue>(static_cast<int64_t>(512));
    resource["annotations"] = std::make_shared<JSONValue>(makeAnnotations());
    resource["_meta"] = std::make_shared<JSONValue>(makeMeta("origin", "resource"));
    resource["icons"] = std::make_shared<JSONValue>(icons);
    JSONValue::Object resourcesResult;
    JSONValue::Array resourcesArray;
    resourcesArray.push_back(std::make_shared<JSONValue>(resource));
    resourcesResult["resources"] = std::make_shared<JSONValue>(resourcesArray);
    EXPECT_TRUE(validation::validateResourcesListResultJson(JSONValue{resourcesResult}));

    JSONValue::Object resourceTemplate;
    resourceTemplate["uriTemplate"] = std::make_shared<JSONValue>(std::string("file:///{path}"));
    resourceTemplate["name"] = std::make_shared<JSONValue>(std::string("Icon Template"));
    resourceTemplate["title"] = std::make_shared<JSONValue>(std::string("Template Title"));
    resourceTemplate["annotations"] = std::make_shared<JSONValue>(makeAnnotations());
    resourceTemplate["_meta"] = std::make_shared<JSONValue>(makeMeta("origin", "template"));
    resourceTemplate["icons"] = std::make_shared<JSONValue>(icons);
    JSONValue::Object templatesResult;
    JSONValue::Array templatesArray;
    templatesArray.push_back(std::make_shared<JSONValue>(resourceTemplate));
    templatesResult["resourceTemplates"] = std::make_shared<JSONValue>(templatesArray);
    EXPECT_TRUE(validation::validateResourceTemplatesListResultJson(JSONValue{templatesResult}));

    JSONValue::Object prompt;
    prompt["name"] = std::make_shared<JSONValue>(std::string("icon-prompt"));
    prompt["title"] = std::make_shared<JSONValue>(std::string("Prompt Title"));
    prompt["_meta"] = std::make_shared<JSONValue>(makeMeta("origin", "prompt"));
    prompt["icons"] = std::make_shared<JSONValue>(icons);
    JSONValue::Object promptsResult;
    JSONValue::Array promptsArray;
    promptsArray.push_back(std::make_shared<JSONValue>(prompt));
    promptsResult["prompts"] = std::make_shared<JSONValue>(promptsArray);
    EXPECT_TRUE(validation::validatePromptsListResultJson(JSONValue{promptsResult}));
}

TEST(IconParity, StrictValidatorsRejectMalformedIcons) {
    JSONValue::Array invalidThemeIcons;
    invalidThemeIcons.push_back(std::make_shared<JSONValue>(makeIconJson("https://example.com/icon.png", "night")));

    JSONValue::Object tool;
    tool["name"] = std::make_shared<JSONValue>(std::string("icon-tool"));
    tool["inputSchema"] = std::make_shared<JSONValue>(makeSchema("input"));
    tool["icons"] = std::make_shared<JSONValue>(invalidThemeIcons);
    JSONValue::Array toolsArray;
    toolsArray.push_back(std::make_shared<JSONValue>(tool));
    JSONValue::Object toolsResult;
    toolsResult["tools"] = std::make_shared<JSONValue>(toolsArray);
    EXPECT_FALSE(validation::validateToolsListResultJson(JSONValue{toolsResult}));

    JSONValue::Object missingSrcIcon;
    missingSrcIcon["mimeType"] = std::make_shared<JSONValue>(std::string("image/png"));
    JSONValue::Array missingSrcIcons;
    missingSrcIcons.push_back(std::make_shared<JSONValue>(JSONValue{missingSrcIcon}));
    JSONValue::Object resource;
    resource["uri"] = std::make_shared<JSONValue>(std::string("file:///tmp/icon-resource.txt"));
    resource["name"] = std::make_shared<JSONValue>(std::string("Icon Resource"));
    resource["icons"] = std::make_shared<JSONValue>(missingSrcIcons);
    JSONValue::Array resourcesArray;
    resourcesArray.push_back(std::make_shared<JSONValue>(resource));
    JSONValue::Object resourcesResult;
    resourcesResult["resources"] = std::make_shared<JSONValue>(resourcesArray);
    EXPECT_FALSE(validation::validateResourcesListResultJson(JSONValue{resourcesResult}));

    JSONValue::Array badSizes;
    badSizes.push_back(std::make_shared<JSONValue>(static_cast<int64_t>(16)));
    JSONValue::Array badSizeIcons;
    badSizeIcons.push_back(std::make_shared<JSONValue>(makeIconJson("https://example.com/icon.png", "light", badSizes)));

    JSONValue::Object resourceTemplate;
    resourceTemplate["uriTemplate"] = std::make_shared<JSONValue>(std::string("file:///{path}"));
    resourceTemplate["name"] = std::make_shared<JSONValue>(std::string("Icon Template"));
    resourceTemplate["icons"] = std::make_shared<JSONValue>(badSizeIcons);
    JSONValue::Array templatesArray;
    templatesArray.push_back(std::make_shared<JSONValue>(resourceTemplate));
    JSONValue::Object templatesResult;
    templatesResult["resourceTemplates"] = std::make_shared<JSONValue>(templatesArray);
    EXPECT_FALSE(validation::validateResourceTemplatesListResultJson(JSONValue{templatesResult}));

    JSONValue::Object prompt;
    prompt["name"] = std::make_shared<JSONValue>(std::string("icon-prompt"));
    prompt["icons"] = std::make_shared<JSONValue>(invalidThemeIcons);
    JSONValue::Array promptsArray;
    promptsArray.push_back(std::make_shared<JSONValue>(prompt));
    JSONValue::Object promptsResult;
    promptsResult["prompts"] = std::make_shared<JSONValue>(promptsArray);
    EXPECT_FALSE(validation::validatePromptsListResultJson(JSONValue{promptsResult}));
}
