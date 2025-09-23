//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Client.cpp
// Purpose: MCP client implementation
//==========================================================================================================
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "logging/Logger.h"
#include "mcp/Client.h"
#include "mcp/Protocol.h"
#include "mcp/async/FutureAwaitable.h"
#include "mcp/async/Task.h"
#include "mcp/errors/Errors.h"
#include "mcp/validation/Validation.h"
#include "mcp/validation/Validators.h"


namespace mcp {

// Client implementation
class Client::Impl {
public:
    friend class Client; // Allow outer Client to invoke private coroutine helpers
    std::unique_ptr<ITransport> transport;
    ClientCapabilities capabilities;
    ServerCapabilities serverCapabilities;
    Implementation clientInfo;
    std::atomic<bool> connected{false};
    std::unordered_map<std::string, IClient::NotificationHandler> notificationHandlers;
    IClient::ProgressHandler progressHandler;
    IClient::ErrorHandler errorHandler;
    IClient::SamplingHandler samplingHandler;
    validation::ValidationMode validationMode{validation::ValidationMode::Off};

    // Listings cache (optional)
    std::optional<uint64_t> listingsCacheTtlMs; // milliseconds; disabled when not set or == 0
    std::mutex cacheMutex;
    struct ToolsCache { std::vector<Tool> data; std::chrono::steady_clock::time_point ts; bool set{false}; } toolsCache;
    struct ResourcesCache { std::vector<Resource> data; std::chrono::steady_clock::time_point ts; bool set{false}; } resourcesCache;
    struct TemplatesCache { std::vector<ResourceTemplate> data; std::chrono::steady_clock::time_point ts; bool set{false}; } templatesCache;
    struct PromptsCache { std::vector<Prompt> data; std::chrono::steady_clock::time_point ts; bool set{false}; } promptsCache;

    explicit Impl(const Implementation& info)
        : clientInfo(info) {
        // Set default client capabilities
        capabilities.experimental = {};
        capabilities.sampling = {};
    }
    // coroutine helpers moved to anonymous namespace below
    // paged list methods are defined after their non-paged counterparts

    JSONValue serializeClientCapabilities(const ClientCapabilities& caps) {
        JSONValue::Object obj;
        
        if (!caps.experimental.empty()) {
            JSONValue::Object expObj;
            for (const auto& [k, v] : caps.experimental) {
                expObj[k] = std::make_shared<JSONValue>(v);
            }
            obj["experimental"] = std::make_shared<JSONValue>(expObj);
        }
        
        if (caps.sampling.has_value()) {
            obj["sampling"] = std::make_shared<JSONValue>(JSONValue::Object{});
        }
        
        return JSONValue{obj};
    }

    void parseServerCapabilities(const JSONValue& result) {
        if (std::holds_alternative<JSONValue::Object>(result.value)) {
            const auto& obj = std::get<JSONValue::Object>(result.value);
            
            auto capsIt = obj.find("capabilities");
            if (capsIt != obj.end() && std::holds_alternative<JSONValue::Object>(capsIt->second->value)) {
                const auto& capsObj = std::get<JSONValue::Object>(capsIt->second->value);
                // Parse experimental capability passthrough
                auto expIt = capsObj.find("experimental");
                if (expIt != capsObj.end() && std::holds_alternative<JSONValue::Object>(expIt->second->value)) {
                    const auto& expObj = std::get<JSONValue::Object>(expIt->second->value);
                    for (const auto& [k, v] : expObj) {
                        this->serverCapabilities.experimental[k] = *v;
                    }
                }
                
                // Parse prompts capability
                auto promptsIt = capsObj.find("prompts");
                if (promptsIt != capsObj.end()) {
                    serverCapabilities.prompts = PromptsCapability{true};
                }
                
                // Parse resources capability
                auto resourcesIt = capsObj.find("resources");
                if (resourcesIt != capsObj.end()) {
                    serverCapabilities.resources = ResourcesCapability{true, true};
                }
                
                // Parse tools capability
                auto toolsIt = capsObj.find("tools");
                if (toolsIt != capsObj.end()) {
                    serverCapabilities.tools = ToolsCapability{true};
                }
                
                // Parse sampling capability
                auto samplingIt = capsObj.find("sampling");
                if (samplingIt != capsObj.end()) {
                    serverCapabilities.sampling = SamplingCapability{};
                }
                
                // Parse logging capability
                auto loggingIt = capsObj.find("logging");
                if (loggingIt != capsObj.end()) {
                    serverCapabilities.logging = LoggingCapability{};
                }
            }
        }
    }
private:
    mcp::async::Task<void> coConnect(std::unique_ptr<ITransport> transport);
    mcp::async::Task<void> coDisconnect();
    mcp::async::Task<ServerCapabilities> coInitialize(const Implementation& clientInfo,
                                                     const ClientCapabilities& capabilities);
    mcp::async::Task<JSONValue> coCallTool(const std::string& name, const JSONValue& arguments);
    mcp::async::Task<void> coSubscribeResources(const std::optional<std::string>& uri);
    mcp::async::Task<void> coUnsubscribeResources(const std::optional<std::string>& uri);
    mcp::async::Task<std::vector<Tool>> coListTools();
    mcp::async::Task<ToolsListResult> coListToolsPaged(const std::optional<std::string>& cursor,
                                                       const std::optional<int>& limit);
    mcp::async::Task<std::vector<Resource>> coListResources();
    mcp::async::Task<ResourcesListResult> coListResourcesPaged(const std::optional<std::string>& cursor,
                                                               const std::optional<int>& limit);
    mcp::async::Task<std::vector<ResourceTemplate>> coListResourceTemplates();
    mcp::async::Task<ResourceTemplatesListResult> coListResourceTemplatesPaged(const std::optional<std::string>& cursor,
                                                                               const std::optional<int>& limit);
    mcp::async::Task<std::vector<Prompt>> coListPrompts();
    mcp::async::Task<PromptsListResult> coListPromptsPaged(const std::optional<std::string>& cursor,
                                                           const std::optional<int>& limit);
    mcp::async::Task<JSONValue> coReadResource(const std::string& uri);
    mcp::async::Task<JSONValue> coReadResource(const std::string& uri,
                                               const std::optional<int64_t>& offset,
                                               const std::optional<int64_t>& length);
    mcp::async::Task<JSONValue> coGetPrompt(const std::string& name, const JSONValue& arguments);

    // Helpers to keep functions short and readable
    void onNotification(std::unique_ptr<JSONRPCNotification> n);
    void handleProgressNotification(const JSONValue::Object& o);
    void invalidateCachesForListChanged(const std::string& method);
    std::unique_ptr<JSONRPCResponse> onRequest(const JSONRPCRequest& req);
    void logInvalidCreateMessageParamsContext(const JSONValue& paramsVal);
    void logInvalidCreateMessageResultContext(const JSONValue& result);
};

void Client::Impl::handleProgressNotification(const JSONValue::Object& o) {
    std::string token;
    double progress = 0.0;
    std::string message;
    auto tIt = o.find("progressToken");
    if (tIt != o.end() && std::holds_alternative<std::string>(tIt->second->value)) {
        token = std::get<std::string>(tIt->second->value);
    }
    auto pIt = o.find("progress");
    if (pIt != o.end() && std::holds_alternative<double>(pIt->second->value)) {
        progress = std::get<double>(pIt->second->value);
    }
    auto mIt = o.find("message");
    if (mIt != o.end() && std::holds_alternative<std::string>(mIt->second->value)) {
        message = std::get<std::string>(mIt->second->value);
    }
    if (this->validationMode == validation::ValidationMode::Strict) {
        bool ok = !token.empty() && (progress >= 0.0 && progress <= 1.0);
        if (!ok) {
            LOG_WARN("Dropping invalid progress notification under Strict mode");
            return;
        }
    }
    if (this->progressHandler) {
        this->progressHandler(token, progress, message);
    }
}

void Client::Impl::invalidateCachesForListChanged(const std::string& method) {
    if (method == Methods::ToolListChanged) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        this->toolsCache.set = false;
    } else if (method == Methods::ResourceListChanged) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        this->resourcesCache.set = false;
        this->templatesCache.set = false;
    } else if (method == Methods::PromptListChanged) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        this->promptsCache.set = false;
    }
}

void Client::Impl::onNotification(std::unique_ptr<JSONRPCNotification> n) {
    if (!n) { return; }
    try {
        if (n->method == Methods::Progress && this->progressHandler && n->params.has_value()) {
            if (std::holds_alternative<JSONValue::Object>(n->params->value)) {
                const auto& o = std::get<JSONValue::Object>(n->params->value);
                this->handleProgressNotification(o);
            }
        } else {
            this->invalidateCachesForListChanged(n->method);
            auto it = this->notificationHandlers.find(n->method);
            if (it != this->notificationHandlers.end()) {
                it->second(n->method, n->params.value_or(JSONValue{}));
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Notification handler exception: {}", e.what());
    }
}

void Client::Impl::logInvalidCreateMessageParamsContext(const JSONValue& paramsVal) {
    bool hasMessages = false, hasModelPreferences = false, hasSystemPrompt = false, hasIncludeContext = false;
    size_t messagesCount = 0;
    if (std::holds_alternative<JSONValue::Object>(paramsVal.value)) {
        const auto& po = std::get<JSONValue::Object>(paramsVal.value);
        auto itP = po.find("messages");
        hasMessages = (itP != po.end());
        if (hasMessages && std::holds_alternative<JSONValue::Array>(itP->second->value)) {
            messagesCount = std::get<JSONValue::Array>(itP->second->value).size();
        }
        hasModelPreferences = (po.find("modelPreferences") != po.end());
        hasSystemPrompt = (po.find("systemPrompt") != po.end());
        hasIncludeContext = (po.find("includeContext") != po.end());
    }
    LOG_ERROR("Validation failed (Strict): {} params invalid | hasMessages={} messagesCount={} hasModelPreferences={} hasSystemPrompt={} hasIncludeContext={}",
              Methods::CreateMessage, hasMessages, messagesCount, hasModelPreferences, hasSystemPrompt, hasIncludeContext);
}

void Client::Impl::logInvalidCreateMessageResultContext(const JSONValue& result) {
    bool hasModel = false, hasRole = false, hasContentArray = false; size_t contentCount = 0;
    if (std::holds_alternative<JSONValue::Object>(result.value)) {
        const auto& ro = std::get<JSONValue::Object>(result.value);
        auto itM = ro.find("model"); hasModel = (itM != ro.end());
        auto itR = ro.find("role"); hasRole = (itR != ro.end());
        auto itC = ro.find("content");
        if (itC != ro.end() && std::holds_alternative<JSONValue::Array>(itC->second->value)) {
            hasContentArray = true;
            contentCount = std::get<JSONValue::Array>(itC->second->value).size();
        }
    }
    LOG_ERROR("Validation failed (Strict): {} result invalid | hasModel={} hasRole={} hasContentArray={} contentCount={}",
              Methods::CreateMessage, hasModel, hasRole, hasContentArray, contentCount);
}

std::unique_ptr<JSONRPCResponse> Client::Impl::onRequest(const JSONRPCRequest& req) {
    try {
        if (req.method == Methods::CreateMessage) {
            if (!this->samplingHandler) {
                errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "No sampling handler registered";
                return errors::makeErrorResponse(req.id, e);
            }
            JSONValue messages, modelPreferences, systemPrompt, includeContext;
            if (req.params.has_value() && std::holds_alternative<JSONValue::Object>(req.params->value)) {
                const auto& o = std::get<JSONValue::Object>(req.params->value);
                auto it = o.find("messages"); if (it != o.end()) messages = *it->second;
                it = o.find("modelPreferences"); if (it != o.end()) modelPreferences = *it->second;
                it = o.find("systemPrompt"); if (it != o.end()) systemPrompt = *it->second;
                it = o.find("includeContext"); if (it != o.end()) includeContext = *it->second;
            }
            if (this->validationMode == validation::ValidationMode::Strict) {
                JSONValue paramsVal = req.params.has_value() ? req.params.value() : JSONValue{JSONValue::Object{}};
                if (!validation::validateCreateMessageParamsJson(paramsVal)) {
                    this->logInvalidCreateMessageParamsContext(paramsVal);
                    errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid sampling/createMessage params";
                    return errors::makeErrorResponse(req.id, e);
                }
            }
            auto fut = this->samplingHandler(messages, modelPreferences, systemPrompt, includeContext);
            JSONValue result = fut.get();
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validateCreateMessageResultJson(result)) {
                    this->logInvalidCreateMessageResultContext(result);
                    errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid sampling result shape";
                    return errors::makeErrorResponse(req.id, e);
                }
            }
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = result;
            return resp;
        }
        {
            errors::McpError e2; e2.code = JSONRPCErrorCodes::MethodNotFound; e2.message = "Method not found";
            return errors::makeErrorResponse(req.id, e2);
        }
    } catch (const std::exception& e) {
        errors::McpError err; err.code = JSONRPCErrorCodes::InternalError; err.message = e.what();
        return errors::makeErrorResponse(req.id, err);
    }
}

mcp::async::Task<void> Client::Impl::coConnect(std::unique_ptr<ITransport> transport) {
    FUNC_SCOPE();
    this->transport = std::move(transport);
    this->transport->SetNotificationHandler([this](std::unique_ptr<JSONRPCNotification> n) {
        this->onNotification(std::move(n));
    });
    this->transport->SetErrorHandler([this](const std::string& err) {
        if (this->errorHandler) this->errorHandler(err);
    });
    this->transport->SetRequestHandler([this](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        return this->onRequest(req);
    });
    auto fut = this->transport->Start();
    try {
        (void) co_await mcp::async::makeFutureAwaitable(std::move(fut));
    } catch (const std::exception& e) {
        LOG_ERROR("Connect exception: {}", e.what());
    }
    this->connected = this->transport->IsConnected();
    co_return;
}
mcp::async::Task<void> Client::Impl::coDisconnect() {
    FUNC_SCOPE();
    if (!this->transport) co_return;
    auto fut = this->transport->Close();
    try {
        (void) co_await mcp::async::makeFutureAwaitable(std::move(fut));
    } catch (const std::exception& e) {
        LOG_ERROR("Disconnect exception: {}", e.what());
    }
    co_return;
}
mcp::async::Task<ServerCapabilities> Client::Impl::coInitialize(
    const Implementation& clientInfo,
    const ClientCapabilities& capabilities) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::Initialize;
    JSONValue::Object paramsObj;
    paramsObj["protocolVersion"] = std::make_shared<JSONValue>(std::string(PROTOCOL_VERSION));
    paramsObj["capabilities"] = std::make_shared<JSONValue>(this->serializeClientCapabilities(capabilities));
    JSONValue::Object ci;
    ci["name"] = std::make_shared<JSONValue>(clientInfo.name);
    ci["version"] = std::make_shared<JSONValue>(clientInfo.version);
    paramsObj["clientInfo"] = std::make_shared<JSONValue>(ci);
    request->params = JSONValue{paramsObj};

    LOG_INFO("Initializing MCP client");
    auto fut = this->transport->SendRequest(std::move(request));
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response && !response->IsError() && response->result.has_value()) {
            this->parseServerCapabilities(response->result.value());
            co_return this->serverCapabilities;
        }
        LOG_ERROR("Initialize failed or missing result");
    } catch (const std::exception& e) {
        LOG_ERROR("Initialize exception: {}", e.what());
    }
    co_return ServerCapabilities{};
}

mcp::async::Task<JSONValue> Client::Impl::coCallTool(const std::string& name, const JSONValue& arguments) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::CallTool;
    JSONValue::Object paramsObj;
    paramsObj["name"] = std::make_shared<JSONValue>(name);
    paramsObj["arguments"] = std::make_shared<JSONValue>(arguments);
    request->params = JSONValue{paramsObj};
    LOG_DEBUG("Calling tool: {}", name);
    auto fut = this->transport->SendRequest(std::move(request));
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response) {
            if (response->result.has_value()) co_return response->result.value();
            if (response->error.has_value()) co_return response->error.value();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("CallTool exception: {}", e.what());
    }
    co_return JSONValue{};
}

mcp::async::Task<void> Client::Impl::coSubscribeResources(const std::optional<std::string>& uri) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::Subscribe;
    JSONValue::Object params;
    if (uri.has_value()) {
        params["uri"] = std::make_shared<JSONValue>(uri.value());
        LOG_DEBUG("Subscribing to resources updates for URI: {}", uri.value());
    } else {
        LOG_DEBUG("Subscribing to resources updates");
    }
    request->params = JSONValue{params};
    auto fut = this->transport->SendRequest(std::move(request));
    try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); } catch (const std::exception& e) { LOG_ERROR("SubscribeResources exception: {}", e.what()); }
    co_return;
}

mcp::async::Task<void> Client::Impl::coUnsubscribeResources(const std::optional<std::string>& uri) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::Unsubscribe;
    JSONValue::Object params;
    if (uri.has_value()) {
        params["uri"] = std::make_shared<JSONValue>(uri.value());
        LOG_DEBUG("Unsubscribing from resources updates for URI: {}", uri.value());
    } else {
        LOG_DEBUG("Unsubscribing from resources updates");
    }
    request->params = JSONValue{params};
    auto fut = this->transport->SendRequest(std::move(request));
    try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); } catch (const std::exception& e) { LOG_ERROR("UnsubscribeResources exception: {}", e.what()); }
    co_return;
}

mcp::async::Task<std::vector<Tool>> Client::Impl::coListTools() {
    FUNC_SCOPE();
    // Cache hit path
    if (this->listingsCacheTtlMs.has_value() && this->listingsCacheTtlMs.value() > 0) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        if (this->toolsCache.set) {
            auto now = std::chrono::steady_clock::now();
            auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->toolsCache.ts).count();
            if (ageMs <= static_cast<int64_t>(this->listingsCacheTtlMs.value())) {
                LOG_DEBUG("ListTools cache hit (ageMs={})", ageMs);
                co_return this->toolsCache.data;
            }
        }
    }
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ListTools;
    LOG_DEBUG("Requesting tools list");
    auto fut = this->transport->SendRequest(std::move(request));
    std::vector<Tool> tools;
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response && !response->IsError() && response->result.has_value()) {
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validateToolsListResultJson(response->result.value())) {
                    const auto& rv = response->result.value();
                    size_t items = 0; bool hasNext = false; std::string nextType;
                    if (std::holds_alternative<JSONValue::Object>(rv.value)) {
                        const auto& objV = std::get<JSONValue::Object>(rv.value);
                        auto itArr = objV.find("tools");
                        if (itArr != objV.end() && std::holds_alternative<JSONValue::Array>(itArr->second->value)) {
                            items = std::get<JSONValue::Array>(itArr->second->value).size();
                        }
                        auto itNc = objV.find("nextCursor");
                        hasNext = (itNc != objV.end());
                        if (hasNext) {
                            nextType = std::holds_alternative<std::string>(itNc->second->value) ? "string" : (std::holds_alternative<int64_t>(itNc->second->value) ? "int" : "other");
                        }
                    }
                    LOG_ERROR("Validation failed (Strict): {} result invalid | items={} hasNextCursor={} nextCursorType={}", Methods::ListTools, items, hasNext, nextType);
                    throw std::runtime_error("Validation failed: tools/list result shape");
                }
            }
            const auto& result = response->result.value();
            if (std::holds_alternative<JSONValue::Object>(result.value)) {
                const auto& obj = std::get<JSONValue::Object>(result.value);
                auto toolsIt = obj.find("tools");
                if (toolsIt != obj.end() && std::holds_alternative<JSONValue::Array>(toolsIt->second->value)) {
                    const auto& toolsArray = std::get<JSONValue::Array>(toolsIt->second->value);
                    for (const auto& toolJson : toolsArray) {
                        Tool tool;
                        if (std::holds_alternative<JSONValue::Object>(toolJson->value)) {
                            const auto& toolObj = std::get<JSONValue::Object>(toolJson->value);
                            auto nameIt = toolObj.find("name");
                            if (nameIt != toolObj.end() && std::holds_alternative<std::string>(nameIt->second->value)) tool.name = std::get<std::string>(nameIt->second->value);
                            auto descIt = toolObj.find("description");
                            if (descIt != toolObj.end() && std::holds_alternative<std::string>(descIt->second->value)) tool.description = std::get<std::string>(descIt->second->value);
                            auto schemaIt = toolObj.find("inputSchema");
                            if (schemaIt != toolObj.end()) tool.inputSchema = *schemaIt->second;
                        }
                        tools.push_back(std::move(tool));
                    }
                }
            }
        }
    } catch (const std::exception& e) { LOG_ERROR("ListTools exception: {}", e.what()); throw; }
    // Update cache on success
    if (this->listingsCacheTtlMs.has_value() && this->listingsCacheTtlMs.value() > 0) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        this->toolsCache.data = tools;
        this->toolsCache.ts = std::chrono::steady_clock::now();
        this->toolsCache.set = true;
    }
    co_return tools;
}

mcp::async::Task<ToolsListResult> Client::Impl::coListToolsPaged(const std::optional<std::string>& cursor, const std::optional<int>& limit) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ListTools;
    JSONValue::Object params;
    if (cursor.has_value()) params["cursor"] = std::make_shared<JSONValue>(cursor.value());
    if (limit.has_value() && *limit > 0) params["limit"] = std::make_shared<JSONValue>(static_cast<int64_t>(*limit));
    if (!params.empty()) request->params = JSONValue{params};
    auto fut = this->transport->SendRequest(std::move(request));
    ToolsListResult out;
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response && !response->IsError() && response->result.has_value()) {
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validateToolsListResultJson(response->result.value())) {
                    const auto& rv = response->result.value();
                    size_t items = 0; bool hasNext = false; std::string nextType;
                    if (std::holds_alternative<JSONValue::Object>(rv.value)) {
                        const auto& objV = std::get<JSONValue::Object>(rv.value);
                        auto itArr = objV.find("tools");
                        if (itArr != objV.end() && std::holds_alternative<JSONValue::Array>(itArr->second->value)) {
                            items = std::get<JSONValue::Array>(itArr->second->value).size();
                        }
                        auto itNc = objV.find("nextCursor");
                        hasNext = (itNc != objV.end());
                        if (hasNext) {
                            nextType = std::holds_alternative<std::string>(itNc->second->value) ? "string" : (std::holds_alternative<int64_t>(itNc->second->value) ? "int" : "other");
                        }
                    }
                    LOG_ERROR("Validation failed (Strict): {} (paged) result invalid | items={} hasNextCursor={} nextCursorType={}", Methods::ListTools, items, hasNext, nextType);
                    throw std::runtime_error("Validation failed: tools/list (paged) result shape");
                }
            }
            const auto& v = response->result.value();
            if (std::holds_alternative<JSONValue::Object>(v.value)) {
                const auto& obj = std::get<JSONValue::Object>(v.value);
                auto toolsIt = obj.find("tools");
                if (toolsIt != obj.end() && std::holds_alternative<JSONValue::Array>(toolsIt->second->value)) {
                    const auto& arr = std::get<JSONValue::Array>(toolsIt->second->value);
                    for (const auto& itemPtr : arr) {
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) continue;
                        const auto& item = std::get<JSONValue::Object>(itemPtr->value);
                        Tool tool;
                        auto nameIt = item.find("name");
                        if (nameIt != item.end() && std::holds_alternative<std::string>(nameIt->second->value)) tool.name = std::get<std::string>(nameIt->second->value);
                        auto descIt = item.find("description");
                        if (descIt != item.end() && std::holds_alternative<std::string>(descIt->second->value)) tool.description = std::get<std::string>(descIt->second->value);
                        auto schemaIt = item.find("inputSchema");
                        if (schemaIt != item.end()) tool.inputSchema = *schemaIt->second;
                        out.tools.push_back(std::move(tool));
                    }
                }
                auto curIt = obj.find("nextCursor");
                if (curIt != obj.end()) {
                    if (std::holds_alternative<std::string>(curIt->second->value)) out.nextCursor = std::get<std::string>(curIt->second->value);
                    else if (std::holds_alternative<int64_t>(curIt->second->value)) out.nextCursor = std::to_string(std::get<int64_t>(curIt->second->value));
                }
            }
        }
    } catch (const std::exception& e) { LOG_ERROR("ListToolsPaged exception: {}", e.what()); throw; }
    co_return out;
}

mcp::async::Task<std::vector<Resource>> Client::Impl::coListResources() {
    FUNC_SCOPE();
    if (this->listingsCacheTtlMs.has_value() && this->listingsCacheTtlMs.value() > 0) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        if (this->resourcesCache.set) {
            auto now = std::chrono::steady_clock::now();
            auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->resourcesCache.ts).count();
            if (ageMs <= static_cast<int64_t>(this->listingsCacheTtlMs.value())) {
                LOG_DEBUG("ListResources cache hit (ageMs={})", ageMs);
                co_return this->resourcesCache.data;
            }
        }
    }
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ListResources;
    LOG_DEBUG("Requesting resources list");
    auto fut = this->transport->SendRequest(std::move(request));
    std::vector<Resource> resources;
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response && !response->IsError() && response->result.has_value()) {
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validateResourcesListResultJson(response->result.value())) {
                    const auto& rv = response->result.value();
                    size_t items = 0; bool hasNext = false; std::string nextType;
                    if (std::holds_alternative<JSONValue::Object>(rv.value)) {
                        const auto& objV = std::get<JSONValue::Object>(rv.value);
                        auto itArr = objV.find("resources");
                        if (itArr != objV.end() && std::holds_alternative<JSONValue::Array>(itArr->second->value)) {
                            items = std::get<JSONValue::Array>(itArr->second->value).size();
                        }
                        auto itNc = objV.find("nextCursor");
                        hasNext = (itNc != objV.end());
                        if (hasNext) {
                            nextType = std::holds_alternative<std::string>(itNc->second->value) ? "string" : (std::holds_alternative<int64_t>(itNc->second->value) ? "int" : "other");
                        }
                    }
                    LOG_ERROR("Validation failed (Strict): {} result invalid | items={} hasNextCursor={} nextCursorType={}", Methods::ListResources, items, hasNext, nextType);
                    throw std::runtime_error("Validation failed: resources/list result shape");
                }
            }
            const auto& result = response->result.value();
            if (std::holds_alternative<JSONValue::Object>(result.value)) {
                const auto& obj = std::get<JSONValue::Object>(result.value);
                auto resIt = obj.find("resources");
                if (resIt != obj.end() && std::holds_alternative<JSONValue::Array>(resIt->second->value)) {
                    const auto& arr = std::get<JSONValue::Array>(resIt->second->value);
                    for (const auto& itemPtr : arr) {
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) continue;
                        const auto& item = std::get<JSONValue::Object>(itemPtr->value);
                        Resource r;
                        auto uriIt = item.find("uri");
                        if (uriIt != item.end() && std::holds_alternative<std::string>(uriIt->second->value)) r.uri = std::get<std::string>(uriIt->second->value);
                        auto nameIt = item.find("name");
                        if (nameIt != item.end() && std::holds_alternative<std::string>(nameIt->second->value)) r.name = std::get<std::string>(nameIt->second->value);
                        auto descIt = item.find("description");
                        if (descIt != item.end() && std::holds_alternative<std::string>(descIt->second->value)) r.description = std::get<std::string>(descIt->second->value);
                        auto mimeIt = item.find("mimeType");
                        if (mimeIt != item.end() && std::holds_alternative<std::string>(mimeIt->second->value)) r.mimeType = std::get<std::string>(mimeIt->second->value);
                        resources.push_back(std::move(r));
                    }
                }
            }
        }
    } catch (const std::exception& e) { LOG_ERROR("ListResources exception: {}", e.what()); throw; }
    if (this->listingsCacheTtlMs.has_value() && this->listingsCacheTtlMs.value() > 0) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        this->resourcesCache.data = resources;
        this->resourcesCache.ts = std::chrono::steady_clock::now();
        this->resourcesCache.set = true;
    }
    co_return resources;
}

mcp::async::Task<ResourcesListResult> Client::Impl::coListResourcesPaged(const std::optional<std::string>& cursor, const std::optional<int>& limit) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ListResources;
    JSONValue::Object params;
    if (cursor.has_value()) params["cursor"] = std::make_shared<JSONValue>(cursor.value());
    if (limit.has_value() && *limit > 0) params["limit"] = std::make_shared<JSONValue>(static_cast<int64_t>(*limit));
    if (!params.empty()) request->params = JSONValue{params};
    auto fut = this->transport->SendRequest(std::move(request));
    ResourcesListResult out;
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response && !response->IsError() && response->result.has_value()) {
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validateResourcesListResultJson(response->result.value())) {
                    const auto& rv = response->result.value();
                    size_t items = 0; bool hasNext = false; std::string nextType;
                    if (std::holds_alternative<JSONValue::Object>(rv.value)) {
                        const auto& objV = std::get<JSONValue::Object>(rv.value);
                        auto itArr = objV.find("resources");
                        if (itArr != objV.end() && std::holds_alternative<JSONValue::Array>(itArr->second->value)) {
                            items = std::get<JSONValue::Array>(itArr->second->value).size();
                        }
                        auto itNc = objV.find("nextCursor");
                        hasNext = (itNc != objV.end());
                        if (hasNext) {
                            nextType = std::holds_alternative<std::string>(itNc->second->value) ? "string" : (std::holds_alternative<int64_t>(itNc->second->value) ? "int" : "other");
                        }
                    }
                    LOG_ERROR("Validation failed (Strict): {} (paged) result invalid | items={} hasNextCursor={} nextCursorType={}", Methods::ListResources, items, hasNext, nextType);
                    throw std::runtime_error("Validation failed: resources/list (paged) result shape");
                }
            }
            const auto& v = response->result.value();
            if (std::holds_alternative<JSONValue::Object>(v.value)) {
                const auto& obj = std::get<JSONValue::Object>(v.value);
                auto arrIt = obj.find("resources");
                if (arrIt != obj.end() && std::holds_alternative<JSONValue::Array>(arrIt->second->value)) {
                    const auto& arr = std::get<JSONValue::Array>(arrIt->second->value);
                    for (const auto& itemPtr : arr) {
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) continue;
                        const auto& item = std::get<JSONValue::Object>(itemPtr->value);
                        Resource r;
                        auto uriIt = item.find("uri");
                        if (uriIt != item.end() && std::holds_alternative<std::string>(uriIt->second->value)) r.uri = std::get<std::string>(uriIt->second->value);
                        auto nameIt = item.find("name");
                        if (nameIt != item.end() && std::holds_alternative<std::string>(nameIt->second->value)) r.name = std::get<std::string>(nameIt->second->value);
                        auto descIt = item.find("description");
                        if (descIt != item.end() && std::holds_alternative<std::string>(descIt->second->value)) r.description = std::get<std::string>(descIt->second->value);
                        auto mimeIt = item.find("mimeType");
                        if (mimeIt != item.end() && std::holds_alternative<std::string>(mimeIt->second->value)) r.mimeType = std::get<std::string>(mimeIt->second->value);
                        out.resources.push_back(std::move(r));
                    }
                }
                auto curIt = obj.find("nextCursor");
                if (curIt != obj.end()) {
                    if (std::holds_alternative<std::string>(curIt->second->value)) out.nextCursor = std::get<std::string>(curIt->second->value);
                    else if (std::holds_alternative<int64_t>(curIt->second->value)) out.nextCursor = std::to_string(std::get<int64_t>(curIt->second->value));
                }
            }
        }
    } catch (const std::exception& e) { LOG_ERROR("ListResourcesPaged exception: {}", e.what()); throw; }
    co_return out;
}

mcp::async::Task<std::vector<ResourceTemplate>> Client::Impl::coListResourceTemplates() {
    FUNC_SCOPE();
    if (this->listingsCacheTtlMs.has_value() && this->listingsCacheTtlMs.value() > 0) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        if (this->templatesCache.set) {
            auto now = std::chrono::steady_clock::now();
            auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->templatesCache.ts).count();
            if (ageMs <= static_cast<int64_t>(this->listingsCacheTtlMs.value())) {
                LOG_DEBUG("ListResourceTemplates cache hit (ageMs={})", ageMs);
                co_return this->templatesCache.data;
            }
        }
    }
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ListResourceTemplates;
    LOG_DEBUG("Requesting resource templates list");
    auto fut = this->transport->SendRequest(std::move(request));
    std::vector<ResourceTemplate> templates;
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response && !response->IsError() && response->result.has_value()) {
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validateResourceTemplatesListResultJson(response->result.value())) {
                    const auto& rv = response->result.value();
                    size_t items = 0; bool hasNext = false; std::string nextType;
                    if (std::holds_alternative<JSONValue::Object>(rv.value)) {
                        const auto& objV = std::get<JSONValue::Object>(rv.value);
                        auto itArr = objV.find("resourceTemplates");
                        if (itArr != objV.end() && std::holds_alternative<JSONValue::Array>(itArr->second->value)) {
                            items = std::get<JSONValue::Array>(itArr->second->value).size();
                        }
                        auto itNc = objV.find("nextCursor");
                        hasNext = (itNc != objV.end());
                        if (hasNext) {
                            nextType = std::holds_alternative<std::string>(itNc->second->value) ? "string" : (std::holds_alternative<int64_t>(itNc->second->value) ? "int" : "other");
                        }
                    }
                    LOG_ERROR("Validation failed (Strict): {} result invalid | items={} hasNextCursor={} nextCursorType={}", Methods::ListResourceTemplates, items, hasNext, nextType);
                    throw std::runtime_error("Validation failed: resources/templates/list result shape");
                }
            }
            const auto& result = response->result.value();
            if (std::holds_alternative<JSONValue::Object>(result.value)) {
                const auto& obj = std::get<JSONValue::Object>(result.value);
                auto tIt = obj.find("resourceTemplates");
                if (tIt != obj.end() && std::holds_alternative<JSONValue::Array>(tIt->second->value)) {
                    const auto& arr = std::get<JSONValue::Array>(tIt->second->value);
                    for (const auto& itemPtr : arr) {
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) continue;
                        const auto& item = std::get<JSONValue::Object>(itemPtr->value);
                        ResourceTemplate rt;
                        auto utIt = item.find("uriTemplate");
                        if (utIt != item.end() && std::holds_alternative<std::string>(utIt->second->value)) rt.uriTemplate = std::get<std::string>(utIt->second->value);
                        auto nameIt = item.find("name");
                        if (nameIt != item.end() && std::holds_alternative<std::string>(nameIt->second->value)) rt.name = std::get<std::string>(nameIt->second->value);
                        auto descIt = item.find("description");
                        if (descIt != item.end() && std::holds_alternative<std::string>(descIt->second->value)) rt.description = std::get<std::string>(descIt->second->value);
                        auto mimeIt = item.find("mimeType");
                        if (mimeIt != item.end() && std::holds_alternative<std::string>(mimeIt->second->value)) rt.mimeType = std::get<std::string>(mimeIt->second->value);
                        templates.push_back(std::move(rt));
                    }
                }
            }
        }
    } catch (const std::exception& e) { LOG_ERROR("ListResourceTemplates exception: {}", e.what()); throw; }
    if (this->listingsCacheTtlMs.has_value() && this->listingsCacheTtlMs.value() > 0) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        this->templatesCache.data = templates;
        this->templatesCache.ts = std::chrono::steady_clock::now();
        this->templatesCache.set = true;
    }
    co_return templates;
}

mcp::async::Task<ResourceTemplatesListResult> Client::Impl::coListResourceTemplatesPaged(const std::optional<std::string>& cursor, const std::optional<int>& limit) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ListResourceTemplates;
    JSONValue::Object params;
    if (cursor.has_value()) params["cursor"] = std::make_shared<JSONValue>(cursor.value());
    if (limit.has_value() && *limit > 0) params["limit"] = std::make_shared<JSONValue>(static_cast<int64_t>(*limit));
    if (!params.empty()) request->params = JSONValue{params};
    auto fut = this->transport->SendRequest(std::move(request));
    ResourceTemplatesListResult out;
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response && !response->IsError() && response->result.has_value()) {
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validateResourceTemplatesListResultJson(response->result.value())) {
                    const auto& rv = response->result.value();
                    size_t items = 0; bool hasNext = false; std::string nextType;
                    if (std::holds_alternative<JSONValue::Object>(rv.value)) {
                        const auto& objV = std::get<JSONValue::Object>(rv.value);
                        auto itArr = objV.find("resourceTemplates");
                        if (itArr != objV.end() && std::holds_alternative<JSONValue::Array>(itArr->second->value)) {
                            items = std::get<JSONValue::Array>(itArr->second->value).size();
                        }
                        auto itNc = objV.find("nextCursor");
                        hasNext = (itNc != objV.end());
                        if (hasNext) {
                            nextType = std::holds_alternative<std::string>(itNc->second->value) ? "string" : (std::holds_alternative<int64_t>(itNc->second->value) ? "int" : "other");
                        }
                    }
                    LOG_ERROR("Validation failed (Strict): {} (paged) result invalid | items={} hasNextCursor={} nextCursorType={}", Methods::ListResourceTemplates, items, hasNext, nextType);
                    throw std::runtime_error("Validation failed: resources/templates/list (paged) result shape");
                }
            }
            const auto& v = response->result.value();
            if (std::holds_alternative<JSONValue::Object>(v.value)) {
                const auto& obj = std::get<JSONValue::Object>(v.value);
                auto arrIt = obj.find("resourceTemplates");
                if (arrIt != obj.end() && std::holds_alternative<JSONValue::Array>(arrIt->second->value)) {
                    const auto& arr = std::get<JSONValue::Array>(arrIt->second->value);
                    for (const auto& itemPtr : arr) {
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) continue;
                        const auto& item = std::get<JSONValue::Object>(itemPtr->value);
                        ResourceTemplate rt;
                        auto utIt = item.find("uriTemplate");
                        if (utIt != item.end() && std::holds_alternative<std::string>(utIt->second->value)) rt.uriTemplate = std::get<std::string>(utIt->second->value);
                        auto nameIt = item.find("name");
                        if (nameIt != item.end() && std::holds_alternative<std::string>(nameIt->second->value)) rt.name = std::get<std::string>(nameIt->second->value);
                        auto descIt = item.find("description");
                        if (descIt != item.end() && std::holds_alternative<std::string>(descIt->second->value)) rt.description = std::get<std::string>(descIt->second->value);
                        auto mimeIt = item.find("mimeType");
                        if (mimeIt != item.end() && std::holds_alternative<std::string>(mimeIt->second->value)) rt.mimeType = std::get<std::string>(mimeIt->second->value);
                        out.resourceTemplates.push_back(std::move(rt));
                    }
                }
                auto curIt = obj.find("nextCursor");
                if (curIt != obj.end()) {
                    if (std::holds_alternative<std::string>(curIt->second->value)) out.nextCursor = std::get<std::string>(curIt->second->value);
                    else if (std::holds_alternative<int64_t>(curIt->second->value)) out.nextCursor = std::to_string(std::get<int64_t>(curIt->second->value));
                }
            }
        }
    } catch (const std::exception& e) { LOG_ERROR("ListResourceTemplatesPaged exception: {}", e.what()); throw; }
    co_return out;
}

mcp::async::Task<std::vector<Prompt>> Client::Impl::coListPrompts() {
    FUNC_SCOPE();
    if (this->listingsCacheTtlMs.has_value() && this->listingsCacheTtlMs.value() > 0) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        if (this->promptsCache.set) {
            auto now = std::chrono::steady_clock::now();
            auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->promptsCache.ts).count();
            if (ageMs <= static_cast<int64_t>(this->listingsCacheTtlMs.value())) {
                LOG_DEBUG("ListPrompts cache hit (ageMs={})", ageMs);
                co_return this->promptsCache.data;
            }
        }
    }
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ListPrompts;
    LOG_DEBUG("Requesting prompts list");
    auto fut = this->transport->SendRequest(std::move(request));
    std::vector<Prompt> prompts;
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response && !response->IsError() && response->result.has_value()) {
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validatePromptsListResultJson(response->result.value())) {
                    const auto& rv = response->result.value();
                    size_t items = 0; bool hasNext = false; std::string nextType;
                    if (std::holds_alternative<JSONValue::Object>(rv.value)) {
                        const auto& objV = std::get<JSONValue::Object>(rv.value);
                        auto itArr = objV.find("prompts");
                        if (itArr != objV.end() && std::holds_alternative<JSONValue::Array>(itArr->second->value)) {
                            items = std::get<JSONValue::Array>(itArr->second->value).size();
                        }
                        auto itNc = objV.find("nextCursor");
                        hasNext = (itNc != objV.end());
                        if (hasNext) {
                            nextType = std::holds_alternative<std::string>(itNc->second->value) ? "string" : (std::holds_alternative<int64_t>(itNc->second->value) ? "int" : "other");
                        }
                    }
                    LOG_ERROR("Validation failed (Strict): {} result invalid | items={} hasNextCursor={} nextCursorType={}", Methods::ListPrompts, items, hasNext, nextType);
                    throw std::runtime_error("Validation failed: prompts/list result shape");
                }
            }
            const auto& result = response->result.value();
            if (std::holds_alternative<JSONValue::Object>(result.value)) {
                const auto& obj = std::get<JSONValue::Object>(result.value);
                auto pIt = obj.find("prompts");
                if (pIt != obj.end() && std::holds_alternative<JSONValue::Array>(pIt->second->value)) {
                    const auto& arr = std::get<JSONValue::Array>(pIt->second->value);
                    for (const auto& itemPtr : arr) {
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) continue;
                        const auto& item = std::get<JSONValue::Object>(itemPtr->value);
                        Prompt pr;
                        auto nameIt = item.find("name");
                        if (nameIt != item.end() && std::holds_alternative<std::string>(nameIt->second->value)) pr.name = std::get<std::string>(nameIt->second->value);
                        auto descIt = item.find("description");
                        if (descIt != item.end() && std::holds_alternative<std::string>(descIt->second->value)) pr.description = std::get<std::string>(descIt->second->value);
                        auto argsIt = item.find("arguments");
                        if (argsIt != item.end()) pr.arguments = *argsIt->second;
                        prompts.push_back(std::move(pr));
                    }
                }
            }
        }
    } catch (const std::exception& e) { LOG_ERROR("ListPrompts exception: {}", e.what()); throw; }
    if (this->listingsCacheTtlMs.has_value() && this->listingsCacheTtlMs.value() > 0) {
        std::lock_guard<std::mutex> lk(this->cacheMutex);
        this->promptsCache.data = prompts;
        this->promptsCache.ts = std::chrono::steady_clock::now();
        this->promptsCache.set = true;
    }
    co_return prompts;
}

mcp::async::Task<PromptsListResult> Client::Impl::coListPromptsPaged(const std::optional<std::string>& cursor, const std::optional<int>& limit) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ListPrompts;
    JSONValue::Object params;
    if (cursor.has_value()) params["cursor"] = std::make_shared<JSONValue>(cursor.value());
    if (limit.has_value() && *limit > 0) params["limit"] = std::make_shared<JSONValue>(static_cast<int64_t>(*limit));
    if (!params.empty()) request->params = JSONValue{params};
    auto fut = this->transport->SendRequest(std::move(request));
    PromptsListResult out;
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response && !response->IsError() && response->result.has_value()) {
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validatePromptsListResultJson(response->result.value())) {
                    const auto& rv = response->result.value();
                    size_t items = 0; bool hasNext = false; std::string nextType;
                    if (std::holds_alternative<JSONValue::Object>(rv.value)) {
                        const auto& objV = std::get<JSONValue::Object>(rv.value);
                        auto itArr = objV.find("prompts");
                        if (itArr != objV.end() && std::holds_alternative<JSONValue::Array>(itArr->second->value)) {
                            items = std::get<JSONValue::Array>(itArr->second->value).size();
                        }
                        auto itNc = objV.find("nextCursor");
                        hasNext = (itNc != objV.end());
                        if (hasNext) {
                            nextType = std::holds_alternative<std::string>(itNc->second->value) ? "string" : (std::holds_alternative<int64_t>(itNc->second->value) ? "int" : "other");
                        }
                    }
                    LOG_ERROR("Validation failed (Strict): {} (paged) result invalid | items={} hasNextCursor={} nextCursorType={}", Methods::ListPrompts, items, hasNext, nextType);
                    throw std::runtime_error("Validation failed: prompts/list (paged) result shape");
                }
            }
            const auto& v = response->result.value();
            if (std::holds_alternative<JSONValue::Object>(v.value)) {
                const auto& obj = std::get<JSONValue::Object>(v.value);
                auto arrIt = obj.find("prompts");
                if (arrIt != obj.end() && std::holds_alternative<JSONValue::Array>(arrIt->second->value)) {
                    const auto& arr = std::get<JSONValue::Array>(arrIt->second->value);
                    for (const auto& itemPtr : arr) {
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) continue;
                        const auto& item = std::get<JSONValue::Object>(itemPtr->value);
                        Prompt pr;
                        auto nameIt = item.find("name");
                        if (nameIt != item.end() && std::holds_alternative<std::string>(nameIt->second->value)) pr.name = std::get<std::string>(nameIt->second->value);
                        auto descIt = item.find("description");
                        if (descIt != item.end() && std::holds_alternative<std::string>(descIt->second->value)) pr.description = std::get<std::string>(descIt->second->value);
                        auto argsIt = item.find("arguments");
                        if (argsIt != item.end()) pr.arguments = *argsIt->second;
                        out.prompts.push_back(std::move(pr));
                    }
                }
                auto curIt = obj.find("nextCursor");
                if (curIt != obj.end()) {
                    if (std::holds_alternative<std::string>(curIt->second->value)) out.nextCursor = std::get<std::string>(curIt->second->value);
                    else if (std::holds_alternative<int64_t>(curIt->second->value)) out.nextCursor = std::to_string(std::get<int64_t>(curIt->second->value));
                }
            }
        }
    } catch (const std::exception& e) { LOG_ERROR("ListPromptsPaged exception: {}", e.what()); throw; }
    co_return out;
}

mcp::async::Task<JSONValue> Client::Impl::coReadResource(const std::string& uri) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ReadResource;
    JSONValue::Object paramsObj;
    paramsObj["uri"] = std::make_shared<JSONValue>(uri);
    request->params = JSONValue{paramsObj};
    LOG_DEBUG("Reading resource: {}", uri);
    auto fut = this->transport->SendRequest(std::move(request));
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response) {
            if (response->result.has_value()) co_return response->result.value();
            if (response->error.has_value()) co_return response->error.value();
        }
    } catch (const std::exception& e) { LOG_ERROR("ReadResource exception: {}", e.what()); }
    co_return JSONValue{};
}

mcp::async::Task<JSONValue> Client::Impl::coReadResource(const std::string& uri,
                                                         const std::optional<int64_t>& offset,
                                                         const std::optional<int64_t>& length) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ReadResource;
    JSONValue::Object paramsObj;
    paramsObj["uri"] = std::make_shared<JSONValue>(uri);
    if (offset.has_value()) { paramsObj["offset"] = std::make_shared<JSONValue>(static_cast<int64_t>(offset.value())); }
    if (length.has_value()) { paramsObj["length"] = std::make_shared<JSONValue>(static_cast<int64_t>(length.value())); }
    request->params = JSONValue{paramsObj};
    LOG_DEBUG("Reading resource (range): uri='{}' offset={} length={}", uri,
              offset.has_value() ? std::to_string(offset.value()) : std::string("<none>"),
              length.has_value() ? std::to_string(length.value()) : std::string("<none>"));
    auto fut = this->transport->SendRequest(std::move(request));
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response) {
            if (response->result.has_value()) co_return response->result.value();
            if (response->error.has_value()) co_return response->error.value();
        }
    } catch (const std::exception& e) { LOG_ERROR("ReadResource(range) exception: {}", e.what()); }
    co_return JSONValue{};
}

mcp::async::Task<JSONValue> Client::Impl::coGetPrompt(const std::string& name, const JSONValue& arguments) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::GetPrompt;
    JSONValue::Object paramsObj;
    paramsObj["name"] = std::make_shared<JSONValue>(name);
    paramsObj["arguments"] = std::make_shared<JSONValue>(arguments);
    request->params = JSONValue{paramsObj};
    LOG_DEBUG("Getting prompt: {}", name);
    auto fut = this->transport->SendRequest(std::move(request));
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response) {
            if (response->result.has_value()) co_return response->result.value();
            if (response->error.has_value()) co_return response->error.value();
        }
    } catch (const std::exception& e) { LOG_ERROR("GetPrompt exception: {}", e.what()); }
    co_return JSONValue{};
}


Client::Client(const Implementation& clientInfo)
    : pImpl(std::make_unique<Impl>(clientInfo)) {
    FUNC_SCOPE();
}

Client::~Client() {
    FUNC_SCOPE();
}

std::future<void> Client::Connect(std::unique_ptr<ITransport> transport) {
    FUNC_SCOPE();
    return pImpl->coConnect(std::move(transport)).toFuture();
}

std::future<void> Client::Disconnect() {
    FUNC_SCOPE();
    if (!pImpl->transport) {
        LOG_DEBUG("Disconnect: no transport");
        return pImpl->coDisconnect().toFuture();
    }
    return pImpl->coDisconnect().toFuture();
}

bool Client::IsConnected() const {
    FUNC_SCOPE();
    bool val = pImpl->transport ? pImpl->transport->IsConnected() : false;
    return val;
}

std::future<ServerCapabilities> Client::Initialize(
    const Implementation& clientInfo,
    const ClientCapabilities& capabilities) {
    FUNC_SCOPE();
    return pImpl->coInitialize(clientInfo, capabilities).toFuture();
}

std::future<void> Client::SubscribeResources() {
    FUNC_SCOPE();
    return pImpl->coSubscribeResources(std::optional<std::string>{}).toFuture();
}

std::future<void> Client::SubscribeResources(const std::optional<std::string>& uri) {
    FUNC_SCOPE();
    return pImpl->coSubscribeResources(uri).toFuture();
}

std::future<void> Client::UnsubscribeResources() {
    FUNC_SCOPE();
    return pImpl->coUnsubscribeResources(std::optional<std::string>{}).toFuture();
}

std::future<void> Client::UnsubscribeResources(const std::optional<std::string>& uri) {
    FUNC_SCOPE();
    return pImpl->coUnsubscribeResources(uri).toFuture();
}

std::future<std::vector<Tool>> Client::ListTools() {
    FUNC_SCOPE();
    return pImpl->coListTools().toFuture();
}

std::future<ResourceTemplatesListResult> Client::ListResourceTemplatesPaged(const std::optional<std::string>& cursor,
                                                                           const std::optional<int>& limit) {
    FUNC_SCOPE();
    return pImpl->coListResourceTemplatesPaged(cursor, limit).toFuture();
}

std::future<ToolsListResult> Client::ListToolsPaged(const std::optional<std::string>& cursor,
                                                   const std::optional<int>& limit) {
    FUNC_SCOPE();
    return pImpl->coListToolsPaged(cursor, limit).toFuture();
}

std::future<JSONValue> Client::CallTool(const std::string& name, const JSONValue& arguments) {
    FUNC_SCOPE();
    return pImpl->coCallTool(name, arguments).toFuture();
}

std::future<std::vector<Resource>> Client::ListResources() {
    FUNC_SCOPE();
    return pImpl->coListResources().toFuture();
}

std::future<ResourcesListResult> Client::ListResourcesPaged(const std::optional<std::string>& cursor,
                                                           const std::optional<int>& limit) {
    FUNC_SCOPE();
    return pImpl->coListResourcesPaged(cursor, limit).toFuture();
}

std::future<std::vector<ResourceTemplate>> Client::ListResourceTemplates() {
    FUNC_SCOPE();
    return pImpl->coListResourceTemplates().toFuture();
}

std::future<JSONValue> Client::ReadResource(const std::string& uri) {
    FUNC_SCOPE();
    return pImpl->coReadResource(uri).toFuture();
}

std::future<JSONValue> Client::ReadResource(const std::string& uri,
                                           const std::optional<int64_t>& offset,
                                           const std::optional<int64_t>& length) {
    FUNC_SCOPE();
    return pImpl->coReadResource(uri, offset, length).toFuture();
}

std::future<std::vector<Prompt>> Client::ListPrompts() {
    FUNC_SCOPE();
    return pImpl->coListPrompts().toFuture();
}

std::future<PromptsListResult> Client::ListPromptsPaged(const std::optional<std::string>& cursor,
                                                       const std::optional<int>& limit) {
    FUNC_SCOPE();
    return pImpl->coListPromptsPaged(cursor, limit).toFuture();
}

std::future<JSONValue> Client::GetPrompt(const std::string& name, const JSONValue& arguments) {
    FUNC_SCOPE();
    return pImpl->coGetPrompt(name, arguments).toFuture();
}

void Client::SetNotificationHandler(const std::string& method, NotificationHandler handler) {
    FUNC_SCOPE();
    pImpl->notificationHandlers[method] = std::move(handler);
}

void Client::RemoveNotificationHandler(const std::string& method) {
    FUNC_SCOPE();
    pImpl->notificationHandlers.erase(method);
}

void Client::SetProgressHandler(ProgressHandler handler) {
    FUNC_SCOPE();
    pImpl->progressHandler = std::move(handler);
}

void Client::SetSamplingHandler(SamplingHandler handler) {
    FUNC_SCOPE();
    pImpl->samplingHandler = std::move(handler);
}

void Client::SetErrorHandler(ErrorHandler handler) {
    FUNC_SCOPE();
    pImpl->errorHandler = std::move(handler);
}

void Client::SetListingsCacheTtlMs(const std::optional<uint64_t>& ttlMs) {
    FUNC_SCOPE();
    // Normalize: disable when not set or <= 0
    if (!ttlMs.has_value() || ttlMs.value() == 0) {
        pImpl->listingsCacheTtlMs.reset();
        std::lock_guard<std::mutex> lk(pImpl->cacheMutex);
        pImpl->toolsCache.set = false;
        pImpl->resourcesCache.set = false;
        pImpl->templatesCache.set = false;
        pImpl->promptsCache.set = false;
        return;
    }
    pImpl->listingsCacheTtlMs = ttlMs;
}

// Validation (opt-in)
void Client::SetValidationMode(validation::ValidationMode mode) {
    FUNC_SCOPE();
    pImpl->validationMode = mode;
}

validation::ValidationMode Client::GetValidationMode() const {
    FUNC_SCOPE();
    return pImpl->validationMode;
}

// Client factory implementation
std::unique_ptr<IClient> ClientFactory::CreateClient(const Implementation& clientInfo) {
    FUNC_SCOPE();
    auto result = std::make_unique<Client>(clientInfo);
    return result;
}

} // namespace mcp
