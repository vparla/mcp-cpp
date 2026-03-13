//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: src/mcp/ConformanceServerSupport.h
// Purpose: Internal helpers to register an MCP server profile compatible with the official server conformance suite
//==========================================================================================================

#pragma once

#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "mcp/Server.h"
#include "mcp/typed/Content.h"

namespace mcp::conformance {

namespace detail {

inline constexpr const char* kTestPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jZ5QAAAAASUVORK5CYII=";
inline constexpr const char* kTestWavBase64 = "UklGRiQAAABXQVZFZm10IBAAAAABAAEAESsAACJWAAACABAAZGF0YQAAAAA=";

inline JSONValue makeSchemaProperty(const std::string& type,
                                    const std::optional<std::string>& description = std::nullopt) {
    JSONValue::Object property;
    property["type"] = std::make_shared<JSONValue>(type);
    if (description.has_value()) {
        property["description"] = std::make_shared<JSONValue>(description.value());
    }
    return JSONValue{property};
}

inline JSONValue makeObjectSchema(const JSONValue::Object& properties = JSONValue::Object{},
                                  const std::vector<std::string>& requiredKeys = {}) {
    JSONValue::Object schema;
    schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    schema["properties"] = std::make_shared<JSONValue>(properties);
    schema["additionalProperties"] = std::make_shared<JSONValue>(false);
    if (!requiredKeys.empty()) {
        JSONValue::Array required;
        for (const auto& key : requiredKeys) {
            required.push_back(std::make_shared<JSONValue>(key));
        }
        schema["required"] = std::make_shared<JSONValue>(required);
    }
    return JSONValue{schema};
}

inline JSONValue makeEnumArray(const std::vector<std::string>& values) {
    JSONValue::Array array;
    for (const auto& value : values) {
        array.push_back(std::make_shared<JSONValue>(value));
    }
    return JSONValue{array};
}

inline JSONValue makeTitledEnumOptions(const std::vector<std::pair<std::string, std::string>>& values) {
    JSONValue::Array options;
    for (const auto& [value, title] : values) {
        JSONValue::Object item;
        item["const"] = std::make_shared<JSONValue>(value);
        item["title"] = std::make_shared<JSONValue>(title);
        options.push_back(std::make_shared<JSONValue>(item));
    }
    return JSONValue{options};
}

inline JSONValue makePromptArguments(const std::vector<std::tuple<std::string, std::string, bool>>& args) {
    JSONValue::Array values;
    for (const auto& [name, description, required] : args) {
        JSONValue::Object item;
        item["name"] = std::make_shared<JSONValue>(name);
        item["description"] = std::make_shared<JSONValue>(description);
        item["required"] = std::make_shared<JSONValue>(required);
        values.push_back(std::make_shared<JSONValue>(item));
    }
    return JSONValue{values};
}

inline JSONValue makePromptMessage(const std::string& role, const JSONValue& content) {
    JSONValue::Object message;
    message["role"] = std::make_shared<JSONValue>(role);
    message["content"] = std::make_shared<JSONValue>(content);
    return JSONValue{message};
}

inline std::string getStringArgument(const JSONValue& arguments,
                                     const std::string& key,
                                     const std::string& fallback = std::string()) {
    if (!std::holds_alternative<JSONValue::Object>(arguments.value)) {
        return fallback;
    }
    const auto& objectValue = std::get<JSONValue::Object>(arguments.value);
    auto it = objectValue.find(key);
    if (it == objectValue.end() || !it->second ||
        !std::holds_alternative<std::string>(it->second->value)) {
        return fallback;
    }
    return std::get<std::string>(it->second->value);
}

inline std::string extractFirstTextContent(const JSONValue& createMessageResult) {
    if (!std::holds_alternative<JSONValue::Object>(createMessageResult.value)) {
        return {};
    }
    const auto& resultObject = std::get<JSONValue::Object>(createMessageResult.value);
    auto contentIt = resultObject.find("content");
    if (contentIt == resultObject.end() || !contentIt->second) {
        return {};
    }
    if (std::holds_alternative<JSONValue::Object>(contentIt->second->value)) {
        auto text = typed::getText(*contentIt->second);
        return text.value_or(std::string());
    }
    if (!std::holds_alternative<JSONValue::Array>(contentIt->second->value)) {
        return {};
    }
    const auto& contentArray = std::get<JSONValue::Array>(contentIt->second->value);
    for (const auto& item : contentArray) {
        if (!item) {
            continue;
        }
        auto text = typed::getText(*item);
        if (text.has_value()) {
            return text.value();
        }
    }
    return {};
}

inline JSONValue makeJsonSchema202012ToolSchema() {
    JSONValue::Object addressProperties;
    addressProperties["street"] = std::make_shared<JSONValue>(makeSchemaProperty("string"));
    addressProperties["city"] = std::make_shared<JSONValue>(makeSchemaProperty("string"));

    JSONValue::Object addressSchema;
    addressSchema["type"] = std::make_shared<JSONValue>(std::string("object"));
    addressSchema["properties"] = std::make_shared<JSONValue>(addressProperties);

    JSONValue::Object defs;
    defs["address"] = std::make_shared<JSONValue>(addressSchema);

    JSONValue::Object properties;
    properties["name"] = std::make_shared<JSONValue>(makeSchemaProperty("string"));

    JSONValue::Object addressRef;
    addressRef["$ref"] = std::make_shared<JSONValue>(std::string("#/$defs/address"));
    properties["address"] = std::make_shared<JSONValue>(addressRef);

    JSONValue::Object schema;
    schema["$schema"] = std::make_shared<JSONValue>(std::string("https://json-schema.org/draft/2020-12/schema"));
    schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    schema["$defs"] = std::make_shared<JSONValue>(defs);
    schema["properties"] = std::make_shared<JSONValue>(properties);
    schema["additionalProperties"] = std::make_shared<JSONValue>(false);
    return JSONValue{schema};
}

inline std::future<ReadResourceResult> makeTextResource(const std::string& uri,
                                                        const std::string& mimeType,
                                                        const std::string& text) {
    return std::async(std::launch::deferred, [uri, mimeType, text]() {
        ReadResourceResult result;
        JSONValue::Object content;
        content["uri"] = std::make_shared<JSONValue>(uri);
        content["mimeType"] = std::make_shared<JSONValue>(mimeType);
        content["text"] = std::make_shared<JSONValue>(text);
        result.contents.push_back(JSONValue{content});
        return result;
    });
}

inline std::future<ReadResourceResult> makeBlobResource(const std::string& uri,
                                                        const std::string& mimeType,
                                                        const std::string& blob) {
    return std::async(std::launch::deferred, [uri, mimeType, blob]() {
        ReadResourceResult result;
        JSONValue::Object content;
        content["uri"] = std::make_shared<JSONValue>(uri);
        content["mimeType"] = std::make_shared<JSONValue>(mimeType);
        content["blob"] = std::make_shared<JSONValue>(blob);
        result.contents.push_back(JSONValue{content});
        return result;
    });
}

inline void sleepForConformanceStep() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

}  // namespace detail

inline void RegisterConformanceServerProfile(Server& server) {
    server.SetCompletionHandler([](const CompleteParams&) -> std::future<CompletionResult> {
        return std::async(std::launch::deferred, []() {
            CompletionResult result;
            result.values = {"alpha", "beta", "gamma"};
            result.total = static_cast<int64_t>(result.values.size());
            result.hasMore = false;
            return result;
        });
    });

    server.RegisterResource(
        Resource{"test://static-text",
                 "Static Text Resource",
                 std::optional<std::string>{"Static text for MCP conformance testing."},
                 std::optional<std::string>{"text/plain"}},
        [](const std::string& uri, std::stop_token st) -> std::future<ReadResourceResult> {
            (void)st;
            return detail::makeTextResource(
                uri,
                "text/plain",
                "This is the content of the static text resource.");
        });

    server.RegisterResource(
        Resource{"test://static-binary",
                 "Static Binary Resource",
                 std::optional<std::string>{"Static binary resource for MCP conformance testing."},
                 std::optional<std::string>{"image/png"}},
        [](const std::string& uri, std::stop_token st) -> std::future<ReadResourceResult> {
            (void)st;
            return detail::makeBlobResource(uri, "image/png", detail::kTestPngBase64);
        });

    server.RegisterResource(
        Resource{"test://watched-resource",
                 "Watched Resource",
                 std::optional<std::string>{"Resource used for subscribe/unsubscribe conformance checks."},
                 std::optional<std::string>{"text/plain"}},
        [](const std::string& uri, std::stop_token st) -> std::future<ReadResourceResult> {
            (void)st;
            return detail::makeTextResource(uri, "text/plain", "Watched resource content.");
        });

    server.RegisterResourceTemplate(
        ResourceTemplate{"test://template/{id}/data",
                         "Template Data Resource",
                         std::optional<std::string>{"Template-backed resource for MCP conformance testing."},
                         std::optional<std::string>{"application/json"}},
        [](const std::string& uri,
           const ResourceTemplateVariables& variables,
           std::stop_token st) -> std::future<ReadResourceResult> {
            (void)st;
            const auto idIt = variables.find("id");
            const std::string idValue = idIt != variables.end() ? idIt->second : std::string("unknown");
            return detail::makeTextResource(
                uri,
                "application/json",
                std::string("{\"id\":\"") + idValue +
                    std::string("\",\"templateTest\":true,\"data\":\"Data for ID: ") + idValue +
                    std::string("\"}"));
        });

    server.RegisterPrompt(
        Prompt{"test_simple_prompt", "Simple prompt for MCP conformance testing."},
        [](const JSONValue&) -> GetPromptResult {
            GetPromptResult result;
            result.description = "Simple prompt for MCP conformance testing.";
            result.messages.push_back(
                detail::makePromptMessage("user", typed::makeText("This is a simple prompt for testing.")));
            return result;
        });

    server.RegisterPrompt(
        Prompt{"test_prompt_with_arguments",
               "Prompt with argument substitution for MCP conformance testing.",
               detail::makePromptArguments({
                   {"arg1", "First test argument", true},
                   {"arg2", "Second test argument", true},
               })},
        [](const JSONValue& arguments) -> GetPromptResult {
            GetPromptResult result;
            result.description = "Prompt with argument substitution for MCP conformance testing.";
            const std::string arg1 = detail::getStringArgument(arguments, "arg1");
            const std::string arg2 = detail::getStringArgument(arguments, "arg2");
            result.messages.push_back(detail::makePromptMessage(
                "user",
                typed::makeText(
                    std::string("Prompt with arguments: arg1='") + arg1 +
                    std::string("', arg2='") + arg2 + std::string("'"))));
            return result;
        });

    server.RegisterPrompt(
        Prompt{"test_prompt_with_embedded_resource",
               "Prompt containing an embedded resource for MCP conformance testing.",
               detail::makePromptArguments({
                   {"resourceUri", "URI of the resource to embed", true},
               })},
        [](const JSONValue& arguments) -> GetPromptResult {
            GetPromptResult result;
            result.description = "Prompt containing an embedded resource for MCP conformance testing.";
            const std::string resourceUri =
                detail::getStringArgument(arguments, "resourceUri", "test://example-resource");
            result.messages.push_back(detail::makePromptMessage(
                "user",
                typed::makeEmbeddedTextResource(
                    resourceUri,
                    "Embedded resource content for testing.",
                    std::optional<std::string>{"text/plain"})));
            result.messages.push_back(detail::makePromptMessage(
                "user",
                typed::makeText("Please process the embedded resource above.")));
            return result;
        });

    server.RegisterPrompt(
        Prompt{"test_prompt_with_image", "Prompt containing image content for MCP conformance testing."},
        [](const JSONValue&) -> GetPromptResult {
            GetPromptResult result;
            result.description = "Prompt containing image content for MCP conformance testing.";
            result.messages.push_back(
                detail::makePromptMessage("user", typed::makeImage("image/png", detail::kTestPngBase64)));
            result.messages.push_back(
                detail::makePromptMessage("user", typed::makeText("Please analyze the image above.")));
            return result;
        });

    Tool simpleTextTool;
    simpleTextTool.name = "test_simple_text";
    simpleTextTool.description = "Return simple text content for MCP conformance testing.";
    simpleTextTool.inputSchema = detail::makeObjectSchema();
    server.RegisterTool(simpleTextTool, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        (void)st;
        return std::async(std::launch::deferred, []() {
            ToolResult result;
            result.content.push_back(typed::makeText("This is a simple text response for testing."));
            return result;
        });
    });

    Tool imageTool;
    imageTool.name = "test_image_content";
    imageTool.description = "Return image content for MCP conformance testing.";
    imageTool.inputSchema = detail::makeObjectSchema();
    server.RegisterTool(imageTool, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        (void)st;
        return std::async(std::launch::deferred, []() {
            ToolResult result;
            result.content.push_back(typed::makeImage("image/png", detail::kTestPngBase64));
            return result;
        });
    });

    Tool audioTool;
    audioTool.name = "test_audio_content";
    audioTool.description = "Return audio content for MCP conformance testing.";
    audioTool.inputSchema = detail::makeObjectSchema();
    server.RegisterTool(audioTool, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        (void)st;
        return std::async(std::launch::deferred, []() {
            ToolResult result;
            result.content.push_back(typed::makeAudio("audio/wav", detail::kTestWavBase64));
            return result;
        });
    });

    Tool embeddedResourceTool;
    embeddedResourceTool.name = "test_embedded_resource";
    embeddedResourceTool.description = "Return embedded resource content for MCP conformance testing.";
    embeddedResourceTool.inputSchema = detail::makeObjectSchema();
    server.RegisterTool(embeddedResourceTool, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        (void)st;
        return std::async(std::launch::deferred, []() {
            ToolResult result;
            result.content.push_back(typed::makeEmbeddedTextResource(
                "test://embedded-resource",
                "This is an embedded resource content.",
                std::optional<std::string>{"text/plain"}));
            return result;
        });
    });

    Tool mixedContentTool;
    mixedContentTool.name = "test_multiple_content_types";
    mixedContentTool.description = "Return multiple content types for MCP conformance testing.";
    mixedContentTool.inputSchema = detail::makeObjectSchema();
    server.RegisterTool(mixedContentTool, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        (void)st;
        return std::async(std::launch::deferred, []() {
            ToolResult result;
            result.content.push_back(typed::makeText("Multiple content types test:"));
            result.content.push_back(typed::makeImage("image/png", detail::kTestPngBase64));
            result.content.push_back(typed::makeEmbeddedTextResource(
                "test://mixed-content-resource",
                "{\"test\":\"data\",\"value\":123}",
                std::optional<std::string>{"application/json"}));
            return result;
        });
    });

    Tool loggingTool;
    loggingTool.name = "test_tool_with_logging";
    loggingTool.description = "Send log notifications during tool execution.";
    loggingTool.inputSchema = detail::makeObjectSchema();
    server.RegisterTool(loggingTool, [&server](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        return std::async(std::launch::async, [&server, st]() {
            if (!st.stop_requested()) {
                server.LogToClient("info", "Tool execution started").get();
            }
            detail::sleepForConformanceStep();
            if (!st.stop_requested()) {
                server.LogToClient("info", "Tool processing data").get();
            }
            detail::sleepForConformanceStep();
            if (!st.stop_requested()) {
                server.LogToClient("info", "Tool execution completed").get();
            }
            ToolResult result;
            result.content.push_back(typed::makeText("Tool with logging completed."));
            return result;
        });
    });

    Tool errorTool;
    errorTool.name = "test_error_handling";
    errorTool.description = "Return a tool error result for MCP conformance testing.";
    errorTool.inputSchema = detail::makeObjectSchema();
    server.RegisterTool(errorTool, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        (void)st;
        return std::async(std::launch::deferred, []() {
            ToolResult result;
            result.isError = true;
            result.content.push_back(typed::makeText("This tool intentionally returns an error for testing"));
            return result;
        });
    });

    Tool progressTool;
    progressTool.name = "test_tool_with_progress";
    progressTool.description = "Report progress notifications during tool execution.";
    progressTool.inputSchema = detail::makeObjectSchema();
    server.RegisterTool(progressTool, [&server](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        const std::string progressToken = CurrentProgressToken().value_or(std::string("progress-test-1"));
        return std::async(std::launch::async, [&server, st, progressToken]() {
            if (!st.stop_requested()) {
                server.SendProgress(progressToken, 0.0, "Starting tool execution").get();
            }
            detail::sleepForConformanceStep();
            if (!st.stop_requested()) {
                server.SendProgress(progressToken, 0.5, "Processing tool execution").get();
            }
            detail::sleepForConformanceStep();
            if (!st.stop_requested()) {
                server.SendProgress(progressToken, 1.0, "Completed tool execution").get();
            }
            ToolResult result;
            result.content.push_back(typed::makeText("Tool with progress completed."));
            return result;
        });
    });

    JSONValue::Object samplingProperties;
    samplingProperties["prompt"] = std::make_shared<JSONValue>(
        detail::makeSchemaProperty("string", std::optional<std::string>{"The prompt to send to the LLM"}));
    Tool samplingTool;
    samplingTool.name = "test_sampling";
    samplingTool.description = "Request client-side sampling and return the response.";
    samplingTool.inputSchema = detail::makeObjectSchema(samplingProperties, {"prompt"});
    server.RegisterTool(samplingTool, [&server](const JSONValue& arguments, std::stop_token st) -> std::future<ToolResult> {
        return std::async(std::launch::async, [&server, arguments, st]() {
            ToolResult result;
            if (st.stop_requested()) {
                result.isError = true;
                result.content.push_back(typed::makeText("Sampling was cancelled."));
                return result;
            }

            CreateMessageParams params;
            JSONValue::Object message;
            message["role"] = std::make_shared<JSONValue>(std::string("user"));
            JSONValue::Array content;
            content.push_back(std::make_shared<JSONValue>(
                typed::makeText(detail::getStringArgument(arguments, "prompt", "Test prompt for sampling"))));
            message["content"] = std::make_shared<JSONValue>(content);
            params.messages.push_back(JSONValue{message});
            params.maxTokens = 100;

            try {
                const JSONValue samplingResult = server.RequestCreateMessage(params).get();
                const std::string text = detail::extractFirstTextContent(samplingResult);
                result.content.push_back(typed::makeText(std::string("LLM response: ") + text));
            } catch (const std::exception& e) {
                result.isError = true;
                result.content.push_back(typed::makeText(std::string("Sampling failed: ") + e.what()));
            }
            return result;
        });
    });

    JSONValue::Object elicitationProperties;
    elicitationProperties["message"] = std::make_shared<JSONValue>(
        detail::makeSchemaProperty("string", std::optional<std::string>{"The message to show the user"}));
    Tool elicitationTool;
    elicitationTool.name = "test_elicitation";
    elicitationTool.description = "Request client-side elicitation and return the user response.";
    elicitationTool.inputSchema = detail::makeObjectSchema(elicitationProperties, {"message"});
    server.RegisterTool(elicitationTool, [&server](const JSONValue& arguments, std::stop_token st) -> std::future<ToolResult> {
        return std::async(std::launch::async, [&server, arguments, st]() {
            ToolResult result;
            if (st.stop_requested()) {
                result.isError = true;
                result.content.push_back(typed::makeText("Elicitation was cancelled."));
                return result;
            }

            JSONValue::Object requestedSchemaProperties;
            requestedSchemaProperties["username"] = std::make_shared<JSONValue>(
                detail::makeSchemaProperty("string", std::optional<std::string>{"User's response"}));
            requestedSchemaProperties["email"] = std::make_shared<JSONValue>(
                detail::makeSchemaProperty("string", std::optional<std::string>{"User's email address"}));

            ElicitationRequest request;
            request.message = detail::getStringArgument(arguments, "message", "Please provide your information");
            request.requestedSchema = detail::makeObjectSchema(requestedSchemaProperties, {"username", "email"});

            try {
                const ElicitationResult elicitationResult = server.RequestElicitation(request).get();
                std::string responseText = std::string("User response: ") + elicitationResult.action;
                if (elicitationResult.content.has_value()) {
                    responseText += std::string(" received");
                }
                result.content.push_back(typed::makeText(responseText));
            } catch (const std::exception& e) {
                result.isError = true;
                result.content.push_back(typed::makeText(std::string("Elicitation failed: ") + e.what()));
            }
            return result;
        });
    });

    Tool elicitationDefaultsTool;
    elicitationDefaultsTool.name = "test_elicitation_sep1034_defaults";
    elicitationDefaultsTool.description = "Request elicitation with primitive default values for SEP-1034 conformance.";
    elicitationDefaultsTool.inputSchema = detail::makeObjectSchema();
    server.RegisterTool(elicitationDefaultsTool, [&server](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        return std::async(std::launch::async, [&server, st]() {
            ToolResult result;
            if (st.stop_requested()) {
                result.isError = true;
                result.content.push_back(typed::makeText("Elicitation defaults test was cancelled."));
                return result;
            }

            JSONValue::Object properties;

            JSONValue::Object nameSchema;
            nameSchema["type"] = std::make_shared<JSONValue>(std::string("string"));
            nameSchema["default"] = std::make_shared<JSONValue>(std::string("John Doe"));
            properties["name"] = std::make_shared<JSONValue>(nameSchema);

            JSONValue::Object ageSchema;
            ageSchema["type"] = std::make_shared<JSONValue>(std::string("integer"));
            ageSchema["default"] = std::make_shared<JSONValue>(static_cast<int64_t>(30));
            properties["age"] = std::make_shared<JSONValue>(ageSchema);

            JSONValue::Object scoreSchema;
            scoreSchema["type"] = std::make_shared<JSONValue>(std::string("number"));
            scoreSchema["default"] = std::make_shared<JSONValue>(95.5);
            properties["score"] = std::make_shared<JSONValue>(scoreSchema);

            JSONValue::Object statusSchema;
            statusSchema["type"] = std::make_shared<JSONValue>(std::string("string"));
            statusSchema["enum"] = std::make_shared<JSONValue>(
                detail::makeEnumArray({"active", "inactive", "pending"}));
            statusSchema["default"] = std::make_shared<JSONValue>(std::string("active"));
            properties["status"] = std::make_shared<JSONValue>(statusSchema);

            JSONValue::Object verifiedSchema;
            verifiedSchema["type"] = std::make_shared<JSONValue>(std::string("boolean"));
            verifiedSchema["default"] = std::make_shared<JSONValue>(true);
            properties["verified"] = std::make_shared<JSONValue>(verifiedSchema);

            ElicitationRequest request;
            request.message = "Test client default value handling - please accept with defaults";
            request.requestedSchema = detail::makeObjectSchema(properties);

            try {
                const ElicitationResult elicitationResult = server.RequestElicitation(request).get();
                result.content.push_back(typed::makeText(
                    std::string("Elicitation completed: action=") + elicitationResult.action +
                    std::string(", content=") + (elicitationResult.content.has_value() ? "present" : "missing")));
            } catch (const std::exception& e) {
                result.isError = true;
                result.content.push_back(typed::makeText(std::string("Elicitation failed: ") + e.what()));
            }
            return result;
        });
    });

    Tool elicitationEnumsTool;
    elicitationEnumsTool.name = "test_elicitation_sep1330_enums";
    elicitationEnumsTool.description = "Request elicitation using SEP-1330 enum schema variants.";
    elicitationEnumsTool.inputSchema = detail::makeObjectSchema();
    server.RegisterTool(elicitationEnumsTool, [&server](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        return std::async(std::launch::async, [&server, st]() {
            ToolResult result;
            if (st.stop_requested()) {
                result.isError = true;
                result.content.push_back(typed::makeText("Elicitation enum test was cancelled."));
                return result;
            }

            JSONValue::Object properties;

            JSONValue::Object untitledSingle;
            untitledSingle["type"] = std::make_shared<JSONValue>(std::string("string"));
            untitledSingle["enum"] = std::make_shared<JSONValue>(
                detail::makeEnumArray({"option1", "option2", "option3"}));
            properties["untitledSingle"] = std::make_shared<JSONValue>(untitledSingle);

            JSONValue::Object titledSingle;
            titledSingle["type"] = std::make_shared<JSONValue>(std::string("string"));
            titledSingle["oneOf"] = std::make_shared<JSONValue>(detail::makeTitledEnumOptions({
                {"value1", "First Option"},
                {"value2", "Second Option"},
                {"value3", "Third Option"},
            }));
            properties["titledSingle"] = std::make_shared<JSONValue>(titledSingle);

            JSONValue::Object legacyEnum;
            legacyEnum["type"] = std::make_shared<JSONValue>(std::string("string"));
            legacyEnum["enum"] = std::make_shared<JSONValue>(
                detail::makeEnumArray({"opt1", "opt2", "opt3"}));
            legacyEnum["enumNames"] = std::make_shared<JSONValue>(
                detail::makeEnumArray({"Option One", "Option Two", "Option Three"}));
            properties["legacyEnum"] = std::make_shared<JSONValue>(legacyEnum);

            JSONValue::Object untitledMultiItems;
            untitledMultiItems["type"] = std::make_shared<JSONValue>(std::string("string"));
            untitledMultiItems["enum"] = std::make_shared<JSONValue>(
                detail::makeEnumArray({"option1", "option2", "option3"}));
            JSONValue::Object untitledMulti;
            untitledMulti["type"] = std::make_shared<JSONValue>(std::string("array"));
            untitledMulti["items"] = std::make_shared<JSONValue>(untitledMultiItems);
            properties["untitledMulti"] = std::make_shared<JSONValue>(untitledMulti);

            JSONValue::Object titledMultiItems;
            titledMultiItems["anyOf"] = std::make_shared<JSONValue>(detail::makeTitledEnumOptions({
                {"value1", "First Choice"},
                {"value2", "Second Choice"},
                {"value3", "Third Choice"},
            }));
            JSONValue::Object titledMulti;
            titledMulti["type"] = std::make_shared<JSONValue>(std::string("array"));
            titledMulti["items"] = std::make_shared<JSONValue>(titledMultiItems);
            properties["titledMulti"] = std::make_shared<JSONValue>(titledMulti);

            ElicitationRequest request;
            request.message = "Please respond to the enum schema variants.";
            request.requestedSchema = detail::makeObjectSchema(properties);

            try {
                const ElicitationResult elicitationResult = server.RequestElicitation(request).get();
                result.content.push_back(typed::makeText(
                    std::string("Elicitation completed: action=") + elicitationResult.action +
                    std::string(", content=") + (elicitationResult.content.has_value() ? "present" : "missing")));
            } catch (const std::exception& e) {
                result.isError = true;
                result.content.push_back(typed::makeText(std::string("Elicitation failed: ") + e.what()));
            }
            return result;
        });
    });

    Tool jsonSchemaTool;
    jsonSchemaTool.name = "json_schema_2020_12_tool";
    jsonSchemaTool.description = "Tool with JSON Schema 2020-12 features";
    jsonSchemaTool.inputSchema = detail::makeJsonSchema202012ToolSchema();
    server.RegisterTool(jsonSchemaTool, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        (void)st;
        return std::async(std::launch::deferred, []() {
            ToolResult result;
            result.content.push_back(typed::makeText("JSON Schema 2020-12 tool executed."));
            return result;
        });
    });
}

}  // namespace mcp::conformance
