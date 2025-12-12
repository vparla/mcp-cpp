//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Protocol.h
// Purpose: MCP protocol data structures and constants
//==========================================================================================================

#pragma once

#include "JSONRPCTypes.h"
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace mcp {
//==========================================================================================================
// MCP Protocol types and constants
// Purpose: Shared protocol structures, capabilities, and method names.
//==========================================================================================================
///////////////////////////////////////// Protocol constants ///////////////////////////////////////////
// MCP Protocol version
constexpr const char* PROTOCOL_VERSION = "2025-11-25";

// MCP Extensions identifiers (optional capability negotiation)
namespace Extensions {
    // UI apps extension identifier (SEP-1865)
    constexpr const char* uiExtensionId = "io.modelcontextprotocol/ui";
}

///////////////////////////////////////// Implementation ///////////////////////////////////////////
// Implementation information
struct Implementation {
    std::string name;
    std::string version;
    
    Implementation() = default;
    Implementation(std::string name, std::string version)
        : name(std::move(name)), version(std::move(version)) {}
};

// (paged list results defined after item types)

///////////////////////////////////////// Capabilities ///////////////////////////////////////////
// Capabilities structures
struct ToolsCapability {
    bool listChanged = false;
};

struct ResourcesCapability {
    bool subscribe = false;
    bool listChanged = false;
};

struct PromptsCapability {
    bool listChanged = false;
};

struct SamplingCapability {
    // Empty for now - may be extended in future
};

struct LoggingCapability {
    // Empty for now - presence indicates logging notifications are supported
};

struct ServerCapabilities {
    std::optional<ToolsCapability> tools;
    std::optional<ResourcesCapability> resources;
    std::optional<PromptsCapability> prompts;
    std::optional<SamplingCapability> sampling;
    std::optional<LoggingCapability> logging;
    std::unordered_map<std::string, JSONValue> experimental;
};

struct ClientCapabilities {
    std::optional<SamplingCapability> sampling;
    std::unordered_map<std::string, JSONValue> experimental;
    // Optional negotiated extensions per SEP-1724 (e.g., io.modelcontextprotocol/ui)
    std::unordered_map<std::string, JSONValue> extensions;
};

///////////////////////////////////////// Tools ///////////////////////////////////////////
// Tool structures
struct Tool {
    std::string name;
    std::string description;
    JSONValue inputSchema;  // JSON Schema for tool parameters
    std::optional<JSONValue> meta; // serialized as _meta in tools/list
    
    Tool() = default;
    Tool(std::string name, std::string description, JSONValue inputSchema = JSONValue{},
         std::optional<JSONValue> metaValue = std::nullopt)
        : name(std::move(name)), description(std::move(description)),
          inputSchema(std::move(inputSchema)), meta(std::move(metaValue)) {}
};

struct CallToolParams {
    std::string name;
    JSONValue arguments;
};

struct CallToolResult {
    std::vector<JSONValue> content;  // Array of content items
    bool isError = false;
};

///////////////////////////////////////// Resources ///////////////////////////////////////////
// Resource structures
struct Resource {
    std::string uri;
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mimeType;
    
    Resource() = default;
    Resource(std::string uri, std::string name, 
             std::optional<std::string> description = std::nullopt,
             std::optional<std::string> mimeType = std::nullopt)
        : uri(std::move(uri)), name(std::move(name)), 
          description(std::move(description)), mimeType(std::move(mimeType)) {}
};

struct ResourceTemplate {
    std::string uriTemplate;
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mimeType;
    
    ResourceTemplate() = default;
    ResourceTemplate(std::string uriTemplate, std::string name,
                     std::optional<std::string> description = std::nullopt,
                     std::optional<std::string> mimeType = std::nullopt)
        : uriTemplate(std::move(uriTemplate)), name(std::move(name)),
          description(std::move(description)), mimeType(std::move(mimeType)) {}
};

struct ReadResourceParams {
    std::string uri;
    // Experimental: optional byte offset and length to support chunked reads
    std::optional<int64_t> offset; // default 0 when not provided
    std::optional<int64_t> length; // read to end when not provided; must be > 0 if provided
};

struct ReadResourceResult {
    std::vector<JSONValue> contents;  // Array of resource contents
};

///////////////////////////////////////// Prompts ///////////////////////////////////////////
// Prompt structures
struct Prompt {
    std::string name;
    std::string description;
    std::optional<JSONValue> arguments;  // JSON Schema for prompt arguments
    
    Prompt() = default;
    Prompt(std::string name, std::string description, 
           std::optional<JSONValue> arguments = std::nullopt)
        : name(std::move(name)), description(std::move(description)), 
          arguments(std::move(arguments)) {}
};

struct GetPromptParams {
    std::string name;
    std::optional<JSONValue> arguments;
};

struct GetPromptResult {
    std::string description;
    std::vector<JSONValue> messages;  // Array of message objects
};

/////////////////////////////////////// Paged list results /////////////////////////////////////////
// Paged list results (defined after item types for completeness)
struct ToolsListResult {
    std::vector<Tool> tools;
    std::optional<std::string> nextCursor;
};

struct ResourcesListResult {
    std::vector<Resource> resources;
    std::optional<std::string> nextCursor;
};

struct ResourceTemplatesListResult {
    std::vector<ResourceTemplate> resourceTemplates;
    std::optional<std::string> nextCursor;
};

struct PromptsListResult {
    std::vector<Prompt> prompts;
    std::optional<std::string> nextCursor;
};

///////////////////////////////////////// Sampling ///////////////////////////////////////////
// Sampling structures (for LLM integration)
struct CreateMessageParams {
    std::vector<JSONValue> messages;
    std::optional<JSONValue> modelPreferences;
    std::optional<std::string> systemPrompt;
    std::optional<std::string> includeContext;
    std::optional<int> maxTokens;
    std::optional<double> temperature;
    std::optional<std::vector<std::string>> stopSequences;
    std::optional<JSONValue> metadata;
};

struct CreateMessageResult {
    std::string model;
    std::string role;
    std::vector<JSONValue> content;
    std::optional<JSONValue> stopReason;
};

///////////////////////////////////////// Progress ///////////////////////////////////////////
// Progress structures
struct ProgressParams {
    std::string progressToken;
    double progress;  // 0.0 to 1.0
    std::optional<int> total;
};

///////////////////////////////////////// Notifications ///////////////////////////////////////////
// Notification structures
struct ResourceListChangedParams {
    // Empty - just signals that resource list changed
};

struct ToolListChangedParams {
    // Empty - just signals that tool list changed
};

struct PromptListChangedParams {
    // Empty - just signals that prompt list changed
};

///////////////////////////////////////// Method names ///////////////////////////////////////////
// MCP method names
namespace Methods {
    // Client to server
    constexpr const char* Initialize = "initialize";
    constexpr const char* ListTools = "tools/list";
    constexpr const char* CallTool = "tools/call";
    constexpr const char* ListResources = "resources/list";
    constexpr const char* ReadResource = "resources/read";
    constexpr const char* Subscribe = "resources/subscribe";
    constexpr const char* Unsubscribe = "resources/unsubscribe";
    constexpr const char* SetLogLevel = "logging/setLevel";
    constexpr const char* ListResourceTemplates = "resources/templates/list";
    constexpr const char* ListPrompts = "prompts/list";
    constexpr const char* GetPrompt = "prompts/get";
    
    // Server to client (sampling)
    constexpr const char* CreateMessage = "sampling/createMessage";
    
    // Notifications
    constexpr const char* Initialized = "notifications/initialized";
    constexpr const char* Progress = "notifications/progress";
    constexpr const char* Keepalive = "notifications/keepalive";
    constexpr const char* Log = "notifications/message";
    constexpr const char* ResourceListChanged = "notifications/resources/list_changed";
    constexpr const char* ToolListChanged = "notifications/tools/list_changed";
    constexpr const char* PromptListChanged = "notifications/prompts/list_changed";
    constexpr const char* Cancelled = "notifications/cancelled";
}

} // namespace mcp
