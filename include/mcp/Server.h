//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Server.h
// Purpose: MCP server interface - COM-style abstraction for MCP server operations
//==========================================================================================================

#pragma once

#include "Protocol.h"
#include "mcp/validation/Validation.h"
#include "Transport.h"
#include <memory>
#include <functional>
#include <future>
#include <unordered_map>
#include <mutex>
#include <future>
#include <functional>
#include <stop_token>

namespace mcp {

// Forward declarations
class IServer;
class IServerFactory;

// Type aliases for handler functions
using ToolResult = CallToolResult;
using ResourceContent = ReadResourceResult;
using PromptResult = GetPromptResult;

// Async, cancellable handler forms using std::stop_token (C++20)
// Note: These are the canonical handler types. Handlers must return a future.
using ToolHandler = std::function<std::future<ToolResult>(const JSONValue&, std::stop_token)>;
using ResourceHandler = std::function<std::future<ResourceContent>(const std::string&, std::stop_token)>;
using PromptHandler = std::function<PromptResult(const JSONValue&)>;

struct ServerCapabilities;
struct ClientCapabilities;
struct Tool;
struct Resource;
struct ResourceTemplate;
struct Prompt;

//==========================================================================================================
// MCP Server interface
// Purpose: MCP server interface definitions.
//==========================================================================================================
class IServer {
public:
    virtual ~IServer() = default;

    /////////////////////////////////////////// Connection management //////////////////////////////////////////
    //==========================================================================================================
    // Starts the server using the provided transport and wires request/notification handlers.
    // Args:
    //   transport: Transport implementation to own and use for JSON-RPC.
    // Returns:
    //   A future that completes once the transport receiver loop is running.
    //==========================================================================================================
    virtual std::future<void> Start(std::unique_ptr<ITransport> transport) = 0;

    //==========================================================================================================
    // Stops the server and closes the underlying transport.
    // Args:
    //   (none)
    // Returns:
    //   A future that completes when the server has fully stopped.
    //==========================================================================================================
    virtual std::future<void> Stop() = 0;

    //==========================================================================================================
    // Indicates whether the server transport is running.
    // Args:
    //   (none)
    // Returns:
    //   true if running; false otherwise.
    //==========================================================================================================
    virtual bool IsRunning() const = 0;

    ////////////////////////////////////////// Protocol initialization /////////////////////////////////////////
    //==========================================================================================================
    // Handles the MCP initialize request from the client.
    // Args:
    //   clientInfo: Client implementation info.
    //   capabilities: Client-advertised capabilities.
    // Returns:
    //   A future completing when the server has processed initialization.
    //==========================================================================================================
    virtual std::future<void> HandleInitialize(
        const Implementation& clientInfo,
        const ClientCapabilities& capabilities) = 0;

    ////////////////////////////////////////////// Tool management /////////////////////////////////////////////
    //==========================================================================================================
    // Registers a cancellable, async tool handler by name.
    // Args:
    //   name: Tool name.
    //   handler: Async callback receiving a std::stop_token to cooperatively cancel work.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void RegisterTool(const std::string& name, ToolHandler handler) = 0;

    // New: Register tool with metadata (name, description, inputSchema)
    //==========================================================================================================
    // Registers a cancellable, async tool with metadata and handler.
    // Args:
    //   tool: Tool metadata (name, description, inputSchema).
    //   handler: Async callback receiving a std::stop_token to cooperatively cancel work.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void RegisterTool(const Tool& tool, ToolHandler handler) = 0;

    //==========================================================================================================
    // Unregisters a tool by name.
    // Args:
    //   name: Tool name.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void UnregisterTool(const std::string& name) = 0;

    //==========================================================================================================
    // Returns the full list of registered tools (metadata only).
    // Args:
    //   (none)
    // Returns:
    //   Vector of Tool items.
    //==========================================================================================================
    virtual std::vector<Tool> ListTools() = 0;

    //==========================================================================================================
    // Invokes a tool by name with JSON arguments (server-initiated path).
    // Args:
    //   name: Tool name.
    //   arguments: JSON parameters.
    // Returns:
    //   Future resolving to JSON result mirroring tools/call response.
    //==========================================================================================================
    virtual std::future<JSONValue> CallTool(const std::string& name, const JSONValue& arguments) = 0;

    //////////////////////////////////////////// Resource management ///////////////////////////////////////////
    //==========================================================================================================
    // Registers a cancellable, async resource handler for a given URI.
    // Args:
    //   uri: Resource identifier.
    //   handler: Async callback receiving a std::stop_token to cooperatively cancel work.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void RegisterResource(const std::string& uri, ResourceHandler handler) = 0;

    //==========================================================================================================
    // Unregisters the resource handler for a given URI.
    // Args:
    //   uri: Resource identifier.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void UnregisterResource(const std::string& uri) = 0;

    //==========================================================================================================
    // Lists resources (metadata only) currently registered.
    // Args:
    //   (none)
    // Returns:
    //   Vector of Resource items.
    //==========================================================================================================
    virtual std::vector<Resource> ListResources() = 0;

    //==========================================================================================================
    // Reads a resource by URI (server-initiated path).
    // Args:
    //   uri: Resource identifier.
    // Returns:
    //   Future with JSON result mirroring resources/read response shape.
    //==========================================================================================================
    virtual std::future<JSONValue> ReadResource(const std::string& uri) = 0;

    /////////////////////////////////////// Resource template management ///////////////////////////////////////
    //==========================================================================================================
    // Registers a resource template (uriTemplate + metadata).
    // Args:
    //   resourceTemplate: Template to add or replace (by uriTemplate).
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void RegisterResourceTemplate(const ResourceTemplate& resourceTemplate) = 0;

    //==========================================================================================================
    // Unregisters a resource template by uriTemplate.
    // Args:
    //   uriTemplate: Template identifier.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void UnregisterResourceTemplate(const std::string& uriTemplate) = 0;

    //==========================================================================================================
    // Lists all registered resource templates.
    // Args:
    //   (none)
    // Returns:
    //   Vector of ResourceTemplate items.
    //==========================================================================================================
    virtual std::vector<ResourceTemplate> ListResourceTemplates() = 0;

    ///////////////////////////////////////////// Prompt management ////////////////////////////////////////////
    //==========================================================================================================
    // Registers a prompt handler by name.
    // Args:
    //   name: Prompt name.
    //   handler: Callback invoked for prompts/get.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void RegisterPrompt(const std::string& name, PromptHandler handler) = 0;

    //==========================================================================================================
    // Unregisters a prompt by name.
    // Args:
    //   name: Prompt name.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void UnregisterPrompt(const std::string& name) = 0;

    //==========================================================================================================
    // Returns the full list of prompt definitions (metadata only).
    // Args:
    //   (none)
    // Returns:
    //   Vector of Prompt items.
    //==========================================================================================================
    virtual std::vector<Prompt> ListPrompts() = 0;

    //==========================================================================================================
    // Retrieves a prompt by name with arguments (server-initiated path).
    // Args:
    //   name: Prompt name.
    //   arguments: Optional JSON arguments.
    // Returns:
    //   Future with JSON result mirroring prompts/get response shape.
    //==========================================================================================================
    virtual std::future<JSONValue> GetPrompt(const std::string& name, const JSONValue& arguments) = 0;

    //////////////////////////////// Sampling handler (for LLM integration) ////////////////////////////////////
    //==========================================================================================================
    // Registers a server-side sampling handler to respond to client-initiated sampling requests.
    // Args:
    //   handler: Callback receiving messages, modelPreferences, systemPrompt, includeContext.
    // Returns:
    //   (none)
    //==========================================================================================================
    using SamplingHandler = std::function<std::future<JSONValue>(
        const JSONValue& messages,
        const JSONValue& modelPreferences,
        const JSONValue& systemPrompt,
        const JSONValue& includeContext)>;
    virtual void SetSamplingHandler(SamplingHandler handler) = 0;

    ////////////////////////////////////////// Keepalive / Heartbeat //////////////////////////////////////////
    //==========================================================================================================
    // Configures a periodic keepalive notification from server to client. When set to a positive interval,
    // the server will send `notifications/keepalive` at approximately the given cadence.
    // Args:
    //   intervalMs: Interval in milliseconds. If not set or <= 0, keepalive is disabled.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void SetKeepaliveIntervalMs(const std::optional<int>& intervalMs) = 0;

    /////////////////////////////////////////// Logging rate limiting //////////////////////////////////////////
    //==========================================================================================================
    // Configures a simple per-second throttle for notifications/log. When set to a positive value, the server
    // will deliver at most the specified number of log notifications per second. When unset or <= 0, the
    // throttle is disabled (unlimited), subject only to the client's minimum log level filter.
    // Args:
    //   perSecond: Maximum notifications/log per second; disable when not set or <= 0.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void SetLoggingRateLimitPerSecond(const std::optional<unsigned int>& perSecond) = 0;

    ///////////////////////////////////////////// Logging to client ////////////////////////////////////////////
    //==========================================================================================================
    // Sends a structured log message to the connected client via notifications/log. Messages below the
    // client-advertised log level (capabilities.experimental.logLevel) will be suppressed.
    // Args:
    //   level: Log severity (DEBUG, INFO, WARN, ERROR, FATAL)
    //   message: Free-form message string
    //   data: Optional structured data payload (JSONValue object/array/primitive)
    // Returns:
    //   Future completing when the notification is written (or an already-ready future if suppressed)
    //==========================================================================================================
    virtual std::future<void> LogToClient(const std::string& level,
                                          const std::string& message,
                                          const std::optional<JSONValue>& data = std::nullopt) = 0;

    ///////////////////////////////////////// Server-initiated sampling ////////////////////////////////////////
    //==========================================================================================================
    // Requests the client to create a message via sampling/createMessage.
    // Args:
    //   params: Message creation parameters (messages, model prefs, etc.).
    // Returns:
    //   Future with raw JSON result from the client's handler.
    //==========================================================================================================
    virtual std::future<JSONValue> RequestCreateMessage(const CreateMessageParams& params) = 0;

    /////////////////////////////////////////// Notification sending ///////////////////////////////////////////
    //==========================================================================================================
    // Sends an arbitrary JSON-RPC notification (method + params) to the client.
    // Args:
    //   method: Notification method.
    //   params: JSON parameters.
    // Returns:
    //   Future that completes when the notification is sent.
    //==========================================================================================================
    virtual std::future<void> SendNotification(const std::string& method, 
                                              const JSONValue& params) = 0;

    /////////////////////////////////////////////// Notifications //////////////////////////////////////////////
    //==========================================================================================================
    // Notifies clients that the resources list has changed.
    // Args:
    //   (none)
    // Returns:
    //   Future completing when the notification is sent.
    //==========================================================================================================
    virtual std::future<void> NotifyResourcesListChanged() = 0;

    //==========================================================================================================
    // Notifies clients that a specific resource has been updated.
    // Args:
    //   uri: Resource identifier that changed.
    // Returns:
    //   Future completing when the notification is sent (or a ready future if filtered out).
    //==========================================================================================================
    virtual std::future<void> NotifyResourceUpdated(const std::string& uri) = 0;

    //==========================================================================================================
    // Notifies clients that the tools list has changed.
    // Args:
    //   (none)
    // Returns:
    //   Future completing when the notification is sent.
    //==========================================================================================================
    virtual std::future<void> NotifyToolsListChanged() = 0;

    //==========================================================================================================
    // Notifies clients that the prompts list has changed.
    // Args:
    //   (none)
    // Returns:
    //   Future completing when the notification is sent.
    //==========================================================================================================
    virtual std::future<void> NotifyPromptsListChanged() = 0;

    //////////////////////////////////////////// Progress reporting ////////////////////////////////////////////
    //==========================================================================================================
    // Sends a progress notification to the client.
    // Args:
    //   token: Progress token to correlate updates.
    //   progress: Value in [0.0, 1.0].
    //   message: Optional progress message.
    // Returns:
    //   Future completing when the progress notification is sent.
    //==========================================================================================================
    virtual std::future<void> SendProgress(const std::string& token, 
                                          double progress, 
                                          const std::string& message) = 0;

    ///////////////////////////////////////////// Error handling ///////////////////////////////////////////////
    //==========================================================================================================
    // Registers an error handler to receive transport or server errors.
    // Args:
    //   handler: Callback receiving error string.
    // Returns:
    //   (none)
    //==========================================================================================================
    using ErrorHandler = std::function<void(const std::string& error)>;
    virtual void SetErrorHandler(ErrorHandler handler) = 0;

    /////////////////////////////////////////// Server capabilities ////////////////////////////////////////////
    //==========================================================================================================
    // Returns the server's current capabilities.
    // Args:
    //   (none)
    // Returns:
    //   ServerCapabilities snapshot.
    //==========================================================================================================
    virtual ServerCapabilities GetCapabilities() const = 0;

    //==========================================================================================================
    // Sets the server's capabilities (advertised on initialize).
    // Args:
    //   capabilities: Capabilities to advertise.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void SetCapabilities(const ServerCapabilities& capabilities) = 0;

    ////////////////////////////////////////// Validation (opt-in) /////////////////////////////////////////////
    //==========================================================================================================
    // Configures runtime schema validation for server-side request/response handling. Default: Off (no-op).
    // Args:
    //   mode: validation::ValidationMode::{Off, Strict}
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void SetValidationMode(validation::ValidationMode mode) = 0;
    //==========================================================================================================
    // Returns the current validation mode.
    //==========================================================================================================
    virtual validation::ValidationMode GetValidationMode() const = 0;
};

// Standard MCP Server implementation
class Server : public IServer {
public:
    //==========================================================================================================
    // Constructs a standard MCP Server.
    // Args:
    //   serverInfo: Human-readable server name and/or version string.
    //==========================================================================================================
    explicit Server(const std::string& serverInfo);
    virtual ~Server();

    // IServer implementation
    std::future<void> Start(std::unique_ptr<ITransport> transport) override;
    std::future<void> Stop() override;
    bool IsRunning() const override;

    std::future<void> HandleInitialize(
        const Implementation& clientInfo,
        const ClientCapabilities& capabilities) override;

    // Tool management
    void RegisterTool(const std::string& name, ToolHandler handler) override;
    void RegisterTool(const Tool& tool, ToolHandler handler) override;
    void UnregisterTool(const std::string& name) override;
    std::vector<Tool> ListTools() override;
    std::future<JSONValue> CallTool(const std::string& name, const JSONValue& arguments) override;

    // Resource management
    void RegisterResource(const std::string& uri, ResourceHandler handler) override;
    void UnregisterResource(const std::string& uri) override;
    std::vector<Resource> ListResources() override;
    std::future<JSONValue> ReadResource(const std::string& uri) override;

    // Resource template management
    void RegisterResourceTemplate(const ResourceTemplate& resourceTemplate) override;
    void UnregisterResourceTemplate(const std::string& uriTemplate) override;
    std::vector<ResourceTemplate> ListResourceTemplates() override;

    // Prompt management
    void RegisterPrompt(const std::string& name, PromptHandler handler) override;
    void UnregisterPrompt(const std::string& name) override;
    std::vector<Prompt> ListPrompts() override;
    std::future<JSONValue> GetPrompt(const std::string& name, const JSONValue& arguments) override;

    // Sampling handler
    void SetSamplingHandler(SamplingHandler handler) override;

    // Keepalive / Heartbeat
    void SetKeepaliveIntervalMs(const std::optional<int>& intervalMs) override;

    // Logging rate limiting
    void SetLoggingRateLimitPerSecond(const std::optional<unsigned int>& perSecond) override;

    // Logging to client
    std::future<void> LogToClient(const std::string& level,
                                  const std::string& message,
                                  const std::optional<JSONValue>& data = std::nullopt) override;

    // Server-initiated sampling (request client to create a message)
    std::future<JSONValue> RequestCreateMessage(const CreateMessageParams& params) override;

    // IServer message sending
    std::future<void> SendNotification(const std::string& method, const JSONValue& params) override;
    std::future<void> SendProgress(const std::string& token, double progress, const std::string& message) override;

    // Notifications (overrides)
    std::future<void> NotifyResourcesListChanged() override;
    std::future<void> NotifyResourceUpdated(const std::string& uri) override;
    std::future<void> NotifyToolsListChanged() override;
    std::future<void> NotifyPromptsListChanged() override;

    // Error handling
    void SetErrorHandler(ErrorHandler handler) override;

    // Server capabilities
    ServerCapabilities GetCapabilities() const override;
    void SetCapabilities(const ServerCapabilities& capabilities) override;

    // Validation (opt-in)
    void SetValidationMode(validation::ValidationMode mode) override;
    validation::ValidationMode GetValidationMode() const override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// Server factory interface
class IServerFactory {
public:
    virtual ~IServerFactory() = default;
    
    //==========================================================================================================
    // Creates a new server instance with the provided implementation metadata.
    // Args:
    //   serverInfo: Implementation information (name and version).
    // Returns:
    //   A unique_ptr to an IServer implementation.
    //==========================================================================================================
    virtual std::unique_ptr<IServer> CreateServer(const Implementation& serverInfo) = 0;
};

// Standard server factory
class ServerFactory : public IServerFactory {
public:
    //==========================================================================================================
    // Creates a standard MCP Server.
    // Args:
    //   serverInfo: Implementation information (name and version).
    // Returns:
    //   A unique_ptr to Server.
    //==========================================================================================================
    std::unique_ptr<IServer> CreateServer(const Implementation& serverInfo) override;
};

} // namespace mcp
