<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: docs/api/client.md
Purpose: Client API reference (IClient / Client)
==========================================================================================================
-->
# Client API (IClient / Client)

Header: [include/mcp/Client.h](../../include/mcp/Client.h)

This page summarizes the public client APIs with signatures and brief descriptions. See README for conceptual guides.

## Connection management
- std::future<void> Connect(std::unique_ptr<ITransport> transport)
  - Start the transport and wire client handlers.
- std::future<void> Disconnect()
  - Close the active transport.
- bool IsConnected() const
  - Returns true if connected.

## Initialization
- std::future<ServerCapabilities> Initialize(const Implementation& clientInfo,
                                            const ClientCapabilities& capabilities)
  - Perform MCP initialize; resolves to server capabilities.

## Tools
- std::future<std::vector<Tool>> ListTools()
  - Non-paged helper for listing tools.
- std::future<ToolsListResult> ListToolsPaged(const std::optional<std::string>& cursor,
                                             const std::optional<int>& limit)
  - Paged listing for tools.
- std::future<JSONValue> CallTool(const std::string& name, const JSONValue& arguments)
  - Invoke a tool; result shape mirrors tools/call.

## Resources
- std::future<std::vector<Resource>> ListResources()
  - Non-paged helper for listing resources.
- std::future<ResourcesListResult> ListResourcesPaged(const std::optional<std::string>& cursor,
                                                     const std::optional<int>& limit)
  - Paged listing for resources.
- std::future<JSONValue> ReadResource(const std::string& uri)
  - Read resource contents; result contains a contents array.
- std::future<JSONValue> ReadResource(const std::string& uri,
                                     const std::optional<int64_t>& offset,
                                     const std::optional<int64_t>& length)
  - Experimental range read: when provided, the server slices text content to the requested range and returns the same result shape as a normal read.
  - The server may enforce a hard clamp per slice using `capabilities.experimental.resourceReadChunking.maxChunkBytes`, returning at most that many bytes regardless of the requested `length`.
  - See typed helpers for ergonomics: `typed::readResourceRange(...)` and `typed::readAllResourceInChunks(...)` in [docs/api/typed.md](./typed.md). The chunk helper advances by the actual returned bytes to remain correct under server clamping.

## Resource templates
- std::future<std::vector<ResourceTemplate>> ListResourceTemplates()
- std::future<ResourceTemplatesListResult> ListResourceTemplatesPaged(const std::optional<std::string>& cursor,
                                                                     const std::optional<int>& limit)

## Resource subscriptions
- std::future<void> SubscribeResources()
  - Subscribe globally to resource updates.
- std::future<void> SubscribeResources(const std::optional<std::string>& uri)
  - Subscribe to updates for a single URI if provided.
- std::future<void> UnsubscribeResources()
- std::future<void> UnsubscribeResources(const std::optional<std::string>& uri)

## Prompts
- std::future<std::vector<Prompt>> ListPrompts()
- std::future<PromptsListResult> ListPromptsPaged(const std::optional<std::string>& cursor,
                                                 const std::optional<int>& limit)
- std::future<JSONValue> GetPrompt(const std::string& name, const JSONValue& arguments)

## Sampling (server → client)
- using SamplingHandler = std::function<std::future<JSONValue>(
    const JSONValue& messages,
    const JSONValue& modelPreferences,
    const JSONValue& systemPrompt,
    const JSONValue& includeContext)>
- void SetSamplingHandler(SamplingHandler handler)
  - Register a handler for server-initiated sampling/createMessage.
 - using SamplingHandlerCancelable = std::function<std::future<JSONValue>(
    const JSONValue& messages,
    const JSONValue& modelPreferences,
    const JSONValue& systemPrompt,
    const JSONValue& includeContext,
    std::stop_token st)>
 - void SetSamplingHandlerCancelable(SamplingHandlerCancelable handler)
  - Register a cancelable variant that receives a `std::stop_token`. When set, the client uses this handler for `sampling/createMessage` and will request stop when it receives `notifications/cancelled` targeting the in-flight request id.

## Notifications, progress, and errors
- using NotificationHandler = std::function<void(const std::string& method, const JSONValue& params)>;
- void SetNotificationHandler(const std::string& method, NotificationHandler handler)
- void RemoveNotificationHandler(const std::string& method)
- using ProgressHandler = std::function<void(const std::string& token, double progress, const std::string& message)>;
- void SetProgressHandler(ProgressHandler handler)
- using ErrorHandler = std::function<void(const std::string& error)>;
- void SetErrorHandler(ErrorHandler handler)

## Typed client wrappers

For ergonomic helpers that return strongly-typed result structs and provide paging and content helpers, see:

- [docs/api/typed.md](./typed.md)

## Validation (opt-in)

Client-side runtime shape validation can be enabled via the validation mode toggle (Off/Strict):

- See API reference in [docs/api/validation.md](./validation.md)

## Listings cache (optional)

- void SetListingsCacheTtlMs(const std::optional<uint64_t>& ttlMs)
  - Enables or disables client-side caching for non-paged list endpoints (tools/resources/prompts/templates).
  - When enabled with a positive TTL, consecutive calls within the TTL window are served from an in-memory cache.
  - Caches are automatically invalidated on corresponding `notifications/*/list_changed`.
