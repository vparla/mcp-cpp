//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: include/mcp/Protocol.h
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

struct Icon {
    std::string src;
    std::optional<std::string> mimeType;
    std::optional<std::vector<std::string>> sizes;
    std::optional<std::string> theme;

    Icon() = default;
    explicit Icon(std::string srcValue,
                  std::optional<std::string> mimeTypeValue = std::nullopt,
                  std::optional<std::vector<std::string>> sizesValue = std::nullopt,
                  std::optional<std::string> themeValue = std::nullopt)
        : src(std::move(srcValue)),
          mimeType(std::move(mimeTypeValue)),
          sizes(std::move(sizesValue)),
          theme(std::move(themeValue)) {}
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

struct RootsCapability {
    bool listChanged = false;
};

struct SamplingCapability {
    // Empty for now - may be extended in future
};

struct CompletionsCapability {
    // Empty for now - presence indicates completion/complete is supported
};

struct ElicitationCapability {
    std::vector<std::string> modes;
};

struct ServerTaskRequestCapabilities {
    bool toolCall = false;
};

struct ClientTaskRequestCapabilities {
    bool createMessage = false;
    bool elicitationCreate = false;
};

struct ServerTasksCapability {
    bool list = false;
    bool cancel = false;
    ServerTaskRequestCapabilities requests;
};

struct ClientTasksCapability {
    bool list = false;
    bool cancel = false;
    ClientTaskRequestCapabilities requests;
};

struct LoggingCapability {
    // Empty for now - presence indicates logging notifications are supported
};

struct ServerCapabilities {
    std::optional<ToolsCapability> tools;
    std::optional<ResourcesCapability> resources;
    std::optional<PromptsCapability> prompts;
    std::optional<SamplingCapability> sampling;
    std::optional<CompletionsCapability> completions;
    std::optional<ServerTasksCapability> tasks;
    std::optional<LoggingCapability> logging;
    std::unordered_map<std::string, JSONValue> experimental;
};

struct ClientCapabilities {
    std::optional<RootsCapability> roots;
    std::optional<SamplingCapability> sampling;
    std::optional<ElicitationCapability> elicitation;
    std::optional<ClientTasksCapability> tasks;
    std::unordered_map<std::string, JSONValue> experimental;
    // Optional negotiated extensions per SEP-1724 (e.g., io.modelcontextprotocol/ui)
    std::unordered_map<std::string, JSONValue> extensions;
};

///////////////////////////////////////// Tools ///////////////////////////////////////////
// Tool structures
struct Tool {
    std::string name;
    std::optional<std::string> title;
    std::string description;
    std::optional<std::vector<Icon>> icons;
    JSONValue inputSchema;  // JSON Schema for tool parameters
    std::optional<JSONValue> outputSchema;
    std::optional<JSONValue> annotations;
    std::optional<JSONValue> execution;
    std::optional<JSONValue> meta; // serialized as _meta in tools/list
    
    Tool() = default;
    Tool(std::string name, std::string description, JSONValue inputSchema = JSONValue{},
         std::optional<JSONValue> metaValue = std::nullopt,
         std::optional<JSONValue> outputSchemaValue = std::nullopt,
         std::optional<JSONValue> annotationsValue = std::nullopt,
         std::optional<JSONValue> executionValue = std::nullopt,
         std::optional<std::string> titleValue = std::nullopt,
         std::optional<std::vector<Icon>> iconsValue = std::nullopt)
        : name(std::move(name)),
          title(std::move(titleValue)),
          description(std::move(description)),
          icons(std::move(iconsValue)),
          inputSchema(std::move(inputSchema)), outputSchema(std::move(outputSchemaValue)),
          annotations(std::move(annotationsValue)), execution(std::move(executionValue)),
          meta(std::move(metaValue)) {}
};

struct CallToolParams {
    std::string name;
    JSONValue arguments;
};

struct TaskMetadata {
    std::optional<int64_t> ttl;
};

struct Task {
    std::string taskId;
    std::string status;
    std::optional<std::string> statusMessage;
    std::string createdAt;
    std::string lastUpdatedAt;
    std::optional<int64_t> ttl;
    std::optional<int64_t> pollInterval;
};

struct CreateTaskResult {
    Task task;
    std::optional<JSONValue> meta;  // serialized as _meta in task creation responses
};

struct CallToolResult {
    std::vector<JSONValue> content;  // Array of content items
    std::optional<JSONValue> structuredContent;
    std::optional<JSONValue> meta;   // serialized as _meta in tools/call
    bool isError = false;
};

///////////////////////////////////////// Completion ///////////////////////////////////////////
struct CompleteArgument {
    std::string name;
    std::string value;
};

struct CompleteParams {
    JSONValue ref;
    CompleteArgument argument;
    std::optional<JSONValue> context;
};

struct CompletionResult {
    std::vector<std::string> values;
    std::optional<int64_t> total;
    bool hasMore = false;
};

///////////////////////////////////////// Resources ///////////////////////////////////////////
// Resource structures
struct Resource {
    std::string uri;
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> mimeType;
    std::optional<int64_t> size;
    std::optional<JSONValue> annotations;
    std::optional<JSONValue> meta; // serialized as _meta in resources/list
    std::optional<std::vector<Icon>> icons;
    
    Resource() = default;
    Resource(std::string uri, std::string name, 
             std::optional<std::string> description = std::nullopt,
             std::optional<std::string> mimeType = std::nullopt,
             std::optional<std::string> titleValue = std::nullopt,
             std::optional<int64_t> sizeValue = std::nullopt,
             std::optional<JSONValue> annotationsValue = std::nullopt,
             std::optional<JSONValue> metaValue = std::nullopt,
             std::optional<std::vector<Icon>> iconsValue = std::nullopt)
        : uri(std::move(uri)),
          name(std::move(name)),
          title(std::move(titleValue)),
          description(std::move(description)),
          mimeType(std::move(mimeType)),
          size(std::move(sizeValue)),
          annotations(std::move(annotationsValue)),
          meta(std::move(metaValue)),
          icons(std::move(iconsValue)) {}
};

struct ResourceTemplate {
    std::string uriTemplate;
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> mimeType;
    std::optional<JSONValue> annotations;
    std::optional<JSONValue> meta; // serialized as _meta in resources/templates/list
    std::optional<std::vector<Icon>> icons;
    
    ResourceTemplate() = default;
    ResourceTemplate(std::string uriTemplate, std::string name,
                     std::optional<std::string> description = std::nullopt,
                     std::optional<std::string> mimeType = std::nullopt,
                     std::optional<std::string> titleValue = std::nullopt,
                     std::optional<JSONValue> annotationsValue = std::nullopt,
                     std::optional<JSONValue> metaValue = std::nullopt,
                     std::optional<std::vector<Icon>> iconsValue = std::nullopt)
        : uriTemplate(std::move(uriTemplate)),
          name(std::move(name)),
          title(std::move(titleValue)),
          description(std::move(description)),
          mimeType(std::move(mimeType)),
          annotations(std::move(annotationsValue)),
          meta(std::move(metaValue)),
          icons(std::move(iconsValue)) {}
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

///////////////////////////////////////// Roots ///////////////////////////////////////////
// Root structures
struct Root {
    std::string uri;
    std::optional<std::string> name;
    std::optional<JSONValue> meta; // serialized as _meta in roots/list

    Root() = default;
    Root(std::string uri,
         std::optional<std::string> name = std::nullopt,
         std::optional<JSONValue> metaValue = std::nullopt)
        : uri(std::move(uri)), name(std::move(name)), meta(std::move(metaValue)) {}
};

///////////////////////////////////////// Prompts ///////////////////////////////////////////
// Prompt structures
struct Prompt {
    std::string name;
    std::optional<std::string> title;
    std::string description;
    std::optional<JSONValue> arguments;  // JSON Schema for prompt arguments
    std::optional<JSONValue> meta;       // serialized as _meta in prompts/list
    std::optional<std::vector<Icon>> icons;
    
    Prompt() = default;
    Prompt(std::string name, std::string description, 
           std::optional<JSONValue> arguments = std::nullopt,
           std::optional<std::string> titleValue = std::nullopt,
           std::optional<JSONValue> metaValue = std::nullopt,
           std::optional<std::vector<Icon>> iconsValue = std::nullopt)
        : name(std::move(name)),
          title(std::move(titleValue)),
          description(std::move(description)),
          arguments(std::move(arguments)),
          meta(std::move(metaValue)),
          icons(std::move(iconsValue)) {}
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

struct RootsListResult {
    std::vector<Root> roots;
};

struct TasksListResult {
    std::vector<Task> tasks;
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

///////////////////////////////////////// Elicitation ///////////////////////////////////////////
struct ElicitationRequest {
    std::string message;
    JSONValue requestedSchema;
    std::optional<std::string> title;
    std::optional<std::string> mode;
    std::optional<std::string> url;
    std::optional<std::string> elicitationId;
    std::optional<JSONValue> metadata;
};

struct ElicitationResult {
    std::string action;
    std::optional<JSONValue> content;
    std::optional<std::string> elicitationId;
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

struct RootListChangedParams {
    // Empty - just signals that root list changed
};

struct TaskStatusNotificationParams {
    Task task;
};

///////////////////////////////////////// Method names ///////////////////////////////////////////
// MCP method names
namespace Methods {
    // Client to server
    constexpr const char* Ping = "ping";
    constexpr const char* Initialize = "initialize";
    constexpr const char* ListTools = "tools/list";
    constexpr const char* CallTool = "tools/call";
    constexpr const char* Complete = "completion/complete";
    constexpr const char* GetTask = "tasks/get";
    constexpr const char* ListTasks = "tasks/list";
    constexpr const char* GetTaskResult = "tasks/result";
    constexpr const char* CancelTask = "tasks/cancel";
    constexpr const char* ListResources = "resources/list";
    constexpr const char* ReadResource = "resources/read";
    constexpr const char* Subscribe = "resources/subscribe";
    constexpr const char* Unsubscribe = "resources/unsubscribe";
    constexpr const char* SetLogLevel = "logging/setLevel";
    constexpr const char* ListResourceTemplates = "resources/templates/list";
    constexpr const char* ListPrompts = "prompts/list";
    constexpr const char* GetPrompt = "prompts/get";
    constexpr const char* ListRoots = "roots/list";
    
    // Server to client (sampling + elicitation)
    constexpr const char* CreateMessage = "sampling/createMessage";
    constexpr const char* Elicit = "elicitation/create";
    
    // Notifications
    constexpr const char* Initialized = "notifications/initialized";
    constexpr const char* Progress = "notifications/progress";
    constexpr const char* Keepalive = "notifications/keepalive";
    constexpr const char* Log = "notifications/message";
    constexpr const char* ResourceListChanged = "notifications/resources/list_changed";
    constexpr const char* ToolListChanged = "notifications/tools/list_changed";
    constexpr const char* PromptListChanged = "notifications/prompts/list_changed";
    constexpr const char* RootListChanged = "notifications/roots/list_changed";
    constexpr const char* ElicitationComplete = "notifications/elicitation/complete";
    constexpr const char* TaskStatus = "notifications/tasks/status";
    constexpr const char* Cancelled = "notifications/cancelled";
}

} // namespace mcp
