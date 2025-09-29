//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Client.h
// Purpose: MCP client interface - COM-style abstraction for MCP client operations
//==========================================================================================================

#pragma once

#include "Transport.h"
#include "JSONRPCTypes.h"
#include "Protocol.h"
#include "mcp/validation/Validation.h"
#include <memory>
#include <string>
#include <vector>
#include <future>
#include <functional>
#include <stop_token>

namespace mcp {

// Forward declarations for MCP protocol types
struct Implementation;
struct ServerCapabilities;
struct ClientCapabilities;
struct Tool;
struct Resource;
struct ResourceTemplate;
struct Prompt;

//==========================================================================================================
// MCP Client interface
// Purpose: MCP client interface definitions.
//==========================================================================================================
class IClient {
public:
    virtual ~IClient() = default;

    ////////////////////////////////////////// Connection management ///////////////////////////////////////////
    //==========================================================================================================
    // Establishes a connection to the server over the provided transport.
    // Args:
    //   transport: The transport implementation to use (takes ownership).
    // Returns:
    //   A future that completes when the transport has started and the client wiring is ready.
    //==========================================================================================================
    virtual std::future<void> Connect(std::unique_ptr<ITransport> transport) = 0;
   
    //==========================================================================================================
    // Closes the active transport connection if present.
    // Args:
    //   (none)
    // Returns:
    //   A future that completes when the transport is closed.
    //==========================================================================================================
    virtual std::future<void> Disconnect() = 0;
    
    //==========================================================================================================
    // Checks whether the client is currently connected.
    // Args:
    //   (none)
    // Returns:
    //   true if connected; false otherwise.
    //==========================================================================================================
    virtual bool IsConnected() const = 0;

    ////////////////////////////////////////// Protocol initialization /////////////////////////////////////////
    //==========================================================================================================
    // Performs the MCP initialize handshake with the server.
    // Args:
    //   clientInfo: Implementation info (name and version) for this client.
    //   capabilities: Client capabilities to advertise to the server.
    // Returns:
    //   A future resolving to the server's capabilities as advertised in the initialize response.
    //==========================================================================================================
    virtual std::future<ServerCapabilities> Initialize(
        const Implementation& clientInfo,
        const ClientCapabilities& capabilities) = 0;

    ////////////////////////////////////////// Tool operations /////////////////////////////////////////////////
    //==========================================================================================================
    // Lists tools exposed by the server (non-paged helper).
    // Args:
    //   (none)
    // Returns:
    //   A future with the full list of Tool items.
    //==========================================================================================================
    virtual std::future<std::vector<Tool>> ListTools() = 0;
    
    //==========================================================================================================
    // Lists tools exposed by the server with optional paging.
    // Args:
    //   cursor: Optional opaque cursor indicating the starting position (as provided by nextCursor).
    //   limit: Optional maximum number of items to return; must be positive when provided.
    // Returns:
    //   A future with ToolsListResult containing a page of tools and an optional nextCursor.
    //==========================================================================================================
    virtual std::future<ToolsListResult> ListToolsPaged(const std::optional<std::string>& cursor,
                                                       const std::optional<int>& limit) = 0;
    //==========================================================================================================
    // Invokes a server tool by name with JSON arguments.
    // Args:
    //   name: The tool name.
    //   arguments: JSON object containing the tool parameters.
    // Returns:
    //   A future with the raw JSON-RPC result (including tool content array and error flag if present).
    //==========================================================================================================
    virtual std::future<JSONValue> CallTool(const std::string& name, 
                                           const JSONValue& arguments) = 0;

    ////////////////////////////////////////// Resource operations /////////////////////////////////////////////
    //==========================================================================================================
    // Lists resources exposed by the server (non-paged helper).
    // Args:
    //   (none)
    // Returns:
    //   A future with the full list of Resource items.
    //==========================================================================================================
    virtual std::future<std::vector<Resource>> ListResources() = 0;
    
    //==========================================================================================================
    // Lists resources exposed by the server with optional paging.
    // Args:
    //   cursor: Optional opaque cursor indicating the starting position (as provided by nextCursor).
    //   limit: Optional maximum number of items to return; must be positive when provided.
    // Returns:
    //   A future with ResourcesListResult containing a page of resources and an optional nextCursor.
    //==========================================================================================================
    virtual std::future<ResourcesListResult> ListResourcesPaged(const std::optional<std::string>& cursor,
                                                               const std::optional<int>& limit) = 0;
    
    //==========================================================================================================
    // Reads the contents for the specified resource.
    // Args:
    //   uri: Resource identifier to read.
    // Returns:
    //   A future with the raw JSON result carrying a contents array (shape aligns with MCP spec).
    //==========================================================================================================
    virtual std::future<JSONValue> ReadResource(const std::string& uri) = 0;
    //==========================================================================================================
    // Reads the contents for the specified resource with optional byte-range parameters (experimental).
    // Args:
    //   uri: Resource identifier to read.
    //   offset: Optional starting byte offset (>= 0).
    //   length: Optional maximum number of bytes to return (> 0 when provided).
    // Returns:
    //   A future with the raw JSON result carrying a contents array.
    //==========================================================================================================
    virtual std::future<JSONValue> ReadResource(const std::string& uri,
                                               const std::optional<int64_t>& offset,
                                               const std::optional<int64_t>& length) = 0;
    
    //==========================================================================================================
    // Lists resource templates advertised by the server (non-paged helper).
    // Args:
    //   (none)
    // Returns:
    //   A future with the full list of ResourceTemplate items.
    //==========================================================================================================
    virtual std::future<std::vector<ResourceTemplate>> ListResourceTemplates() = 0;
    
    //==========================================================================================================
    // Lists resource templates with optional paging.
    // Args:
    //   cursor: Optional opaque cursor indicating the starting position (as provided by nextCursor).
    //   limit: Optional maximum number of items to return; must be positive when provided.
    // Returns:
    //   A future with ResourceTemplatesListResult containing a page of templates and an optional nextCursor.
    //==========================================================================================================
    virtual std::future<ResourceTemplatesListResult> ListResourceTemplatesPaged(const std::optional<std::string>& cursor,
                                                                               const std::optional<int>& limit) = 0;
    //==========================================================================================================
    // Subscribes to resource updates (optionally per-URI via server-side handling of params).
    // Args:
    //   (none)
    // Returns:
    //   A future that completes when the subscription request is acknowledged.
    //==========================================================================================================
    virtual std::future<void> SubscribeResources() = 0;
    //==========================================================================================================
    // Subscribes to resource updates for a specific URI when provided; subscribes globally otherwise.
    // Args:
    //   uri: Optional resource URI to subscribe to.
    // Returns:
    //   A future that completes when the subscription request is acknowledged.
    //==========================================================================================================
    virtual std::future<void> SubscribeResources(const std::optional<std::string>& uri) = 0;
    
    //==========================================================================================================
    // Unsubscribes from resource updates (optionally per-URI via server-side handling of params).
    // Args:
    //   (none)
    // Returns:
    //   A future that completes when the unsubscribe request is acknowledged.
    //==========================================================================================================
    virtual std::future<void> UnsubscribeResources() = 0;
    
    //==========================================================================================================
    // Unsubscribes from resource updates for a specific URI when provided; unsubscribes all otherwise.
    // Args:
    //   uri: Optional resource URI to unsubscribe from.
    // Returns:
    //   A future that completes when the unsubscribe request is acknowledged.
    //==========================================================================================================
    virtual std::future<void> UnsubscribeResources(const std::optional<std::string>& uri) = 0;

    ////////////////////////////////////////// Prompt operations ///////////////////////////////////////////////
    //==========================================================================================================
    // Lists prompts exposed by the server (non-paged helper).
    // Args:
    //   (none)
    // Returns:
    //   A future with the full list of Prompt items.
    //==========================================================================================================
    virtual std::future<std::vector<Prompt>> ListPrompts() = 0;
    //==========================================================================================================
    // Lists prompts exposed by the server with optional paging.
    // Args:
    //   cursor: Optional opaque cursor indicating the starting position (as provided by nextCursor).
    //   limit: Optional maximum number of items to return; must be positive when provided.
    // Returns:
    //   A future with PromptsListResult containing a page of prompts and an optional nextCursor.
    //==========================================================================================================
    virtual std::future<PromptsListResult> ListPromptsPaged(const std::optional<std::string>& cursor,
                                                           const std::optional<int>& limit) = 0;
    //==========================================================================================================
    // Retrieves a concrete prompt by name with optional arguments.
    // Args:
    //   name: Prompt identifier.
    //   arguments: Optional JSON arguments matching the prompt schema.
    // Returns:
    //   A future with the raw JSON result, including description and messages per MCP spec.
    //==========================================================================================================
    virtual std::future<JSONValue> GetPrompt(const std::string& name,
                                            const JSONValue& arguments) = 0;

    //////////////////////////// Server-initiated sampling handler registration ////////////////////////////////
    //==========================================================================================================
    // Registers a sampling handler to service server-initiated sampling/createMessage requests.
    // Args:
    //   handler: Callback invoked with messages, model preferences, system prompt, and includeContext.
    // Returns:
    //   (none)
    //==========================================================================================================
    using SamplingHandler = std::function<std::future<JSONValue>(
        const JSONValue& messages,
        const JSONValue& modelPreferences,
        const JSONValue& systemPrompt,
        const JSONValue& includeContext)>;
    virtual void SetSamplingHandler(SamplingHandler handler) = 0;

    // Optional cancelable sampling handler variant that receives a std::stop_token.
    // When set, this handler will be used instead of the non-cancelable variant for
    // servicing server-initiated sampling/createMessage requests.
    using SamplingHandlerCancelable = std::function<std::future<JSONValue>(
        const JSONValue& messages,
        const JSONValue& modelPreferences,
        const JSONValue& systemPrompt,
        const JSONValue& includeContext,
        std::stop_token st)>;
    virtual void SetSamplingHandlerCancelable(SamplingHandlerCancelable handler) = 0;

    ////////////////////////////////////////// Notification handlers ///////////////////////////////////////////
    //==========================================================================================================
    // Registers a notification handler for a specific method name.
    // Args:
    //   method: Notification method name to listen for.
    //   handler: Callback receiving method and params JSON.
    // Returns:
    //   (none)
    //==========================================================================================================
    using NotificationHandler = std::function<void(const std::string& method, const JSONValue& params)>;
    virtual void SetNotificationHandler(const std::string& method, NotificationHandler handler) = 0;
    
    //==========================================================================================================
    // Removes a previously registered notification handler for the given method name.
    // Args:
    //   method: Notification method name.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void RemoveNotificationHandler(const std::string& method) = 0;

    ////////////////////////////////////////// Progress tracking ///////////////////////////////////////////////
    //==========================================================================================================
    // Registers a progress handler to receive progress notifications from the server.
    // Args:
    //   handler: Callback with progress token, progress value [0.0, 1.0], and message.
    // Returns:
    //   (none)
    //==========================================================================================================
    using ProgressHandler = std::function<void(const std::string& token, double progress, const std::string& message)>;
    virtual void SetProgressHandler(ProgressHandler handler) = 0;

    //////////////////////////////////////////// Error handling ////////////////////////////////////////////////
    //==========================================================================================================
    // Registers an error handler to receive transport/client errors.
    // Args:
    //   handler: Callback with error string.
    // Returns:
    //   (none)
    //==========================================================================================================
    using ErrorHandler = std::function<void(const std::string& error)>;
    virtual void SetErrorHandler(ErrorHandler handler) = 0;

    ////////////////////////////////////////// Validation (opt-in) /////////////////////////////////////////////
    //==========================================================================================================
    // Configures runtime schema validation for client-side request/response handling. Default: Off (no-op).
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

    ////////////////////////////////////////// Listings cache (optional) ////////////////////////////////////////
    //==========================================================================================================
    // Enables or disables client-side caching for non-paged list endpoints (tools/resources/prompts/templates).
    // When enabled, consecutive calls within the specified TTL window return cached results and avoid a server
    // request. Caches are automatically invalidated on notifications/*/list_changed.
    // Args:
    //   ttlMs: Optional TTL in milliseconds. When not set, caching is disabled. When set to 0, caching is
    //          effectively disabled. Positive values enable caching.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void SetListingsCacheTtlMs(const std::optional<uint64_t>& ttlMs) = 0;
};

// Standard MCP Client implementation
class Client : public IClient {
public:
    //==========================================================================================================
    // Constructs a standard MCP Client with the given implementation info.
    // Args:
    //   clientInfo: Implementation info recorded by the client.
    //==========================================================================================================
    explicit Client(const Implementation& clientInfo);
    virtual ~Client();

    ////////////////////////////////////////// IClient implementation //////////////////////////////////////////
    std::future<void> Connect(std::unique_ptr<ITransport> transport) override;
    std::future<void> Disconnect() override;
    bool IsConnected() const override;

    std::future<ServerCapabilities> Initialize(
        const Implementation& clientInfo,
        const ClientCapabilities& capabilities) override;

    std::future<std::vector<Tool>> ListTools() override;
    std::future<ToolsListResult> ListToolsPaged(const std::optional<std::string>& cursor,
                                               const std::optional<int>& limit) override;
    std::future<JSONValue> CallTool(const std::string& name, 
                                   const JSONValue& arguments) override;

    std::future<std::vector<Resource>> ListResources() override;
    std::future<ResourcesListResult> ListResourcesPaged(const std::optional<std::string>& cursor,
                                                       const std::optional<int>& limit) override;
    std::future<JSONValue> ReadResource(const std::string& uri) override;
    std::future<JSONValue> ReadResource(const std::string& uri,
                                       const std::optional<int64_t>& offset,
                                       const std::optional<int64_t>& length) override;
    std::future<std::vector<ResourceTemplate>> ListResourceTemplates() override;
    std::future<ResourceTemplatesListResult> ListResourceTemplatesPaged(const std::optional<std::string>& cursor,
                                                                       const std::optional<int>& limit) override;
    std::future<void> SubscribeResources() override;
    std::future<void> SubscribeResources(const std::optional<std::string>& uri) override;
    std::future<void> UnsubscribeResources() override;
    std::future<void> UnsubscribeResources(const std::optional<std::string>& uri) override;

    std::future<std::vector<Prompt>> ListPrompts() override;
    std::future<PromptsListResult> ListPromptsPaged(const std::optional<std::string>& cursor,
                                                   const std::optional<int>& limit) override;
    std::future<JSONValue> GetPrompt(const std::string& name,
                                    const JSONValue& arguments) override;

    void SetSamplingHandler(SamplingHandler handler) override;
    void SetSamplingHandlerCancelable(SamplingHandlerCancelable handler) override;

    void SetNotificationHandler(const std::string& method, NotificationHandler handler) override;
    void RemoveNotificationHandler(const std::string& method) override;
    void SetProgressHandler(ProgressHandler handler) override;
    void SetErrorHandler(ErrorHandler handler) override;

    // Validation (opt-in)
    void SetValidationMode(validation::ValidationMode mode) override;
    validation::ValidationMode GetValidationMode() const override;

    // Listings cache (optional)
    void SetListingsCacheTtlMs(const std::optional<uint64_t>& ttlMs) override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// Client factory interface
class IClientFactory {
public:
    virtual ~IClientFactory() = default;
    virtual std::unique_ptr<IClient> CreateClient(const Implementation& clientInfo) = 0;
};

// Standard client factory
class ClientFactory : public IClientFactory {
public:
    std::unique_ptr<IClient> CreateClient(const Implementation& clientInfo) override;
};

} // namespace mcp
