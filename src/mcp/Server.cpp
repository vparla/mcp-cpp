//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Server.cpp
// Purpose: MCP server implementation
//==========================================================================================================
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <cctype>
#include <future>
#include <stop_token>
#include "mcp/Server.h"
#include "mcp/Protocol.h"
#include "logging/Logger.h"
#include "mcp/async/Task.h"
#include "mcp/async/FutureAwaitable.h"
#include "mcp/errors/Errors.h"


namespace mcp {

// Server implementation
class Server::Impl {
public:
    friend class Server;
    // Coroutine helpers (declarations)
    mcp::async::Task<void> coStart(std::unique_ptr<ITransport> transport);
    mcp::async::Task<void> coStop();
    mcp::async::Task<void> coHandleInitialize(const Implementation& clientInfo, const ClientCapabilities& capabilities);
    mcp::async::Task<JSONValue> coCallTool(const std::string& name, const JSONValue& arguments);
    mcp::async::Task<JSONValue> coReadResource(const std::string& uri);
    mcp::async::Task<JSONValue> coGetPrompt(const std::string& name, const JSONValue& arguments);
    mcp::async::Task<JSONValue> coRequestCreateMessage(const CreateMessageParams& params);
    mcp::async::Task<void> coSendNotification(const std::string& method, const JSONValue& params);
    mcp::async::Task<void> coNotifyResourcesListChanged();
    mcp::async::Task<void> coNotifyToolsListChanged();
    mcp::async::Task<void> coNotifyPromptsListChanged();
    mcp::async::Task<void> coNotifyResourceUpdated(const std::string& uri);
    mcp::async::Task<void> coSendProgress(const std::string& token, double progress, const std::string& message);
    mcp::async::Task<void> coLogToClient(const std::string& level, const std::string& message, const std::optional<JSONValue>& data);
    std::unique_ptr<ITransport> transport;
    ServerCapabilities capabilities;
    ClientCapabilities clientCapabilities;
    std::string serverInfo;
    std::atomic<bool> initialized{false};
    // (coroutine helper definitions appear after the class)
    std::function<void(const std::string&)> errorCallback;
    std::atomic<bool> resourcesSubscribed{false};
    std::unordered_set<std::string> subscribedUris; // per-URI subscriptions
    IServer::SamplingHandler samplingHandler;       // optional sampling handler
    // Keepalive state
    std::atomic<int> keepaliveIntervalMs{-1};
    std::atomic<bool> keepaliveStop{false};
    std::thread keepaliveThread;
    std::atomic<unsigned int> keepaliveConsecutiveFailures{0u};
    std::atomic<unsigned int> keepaliveFailureThreshold{3u};
    std::atomic<bool> keepaliveSending{false};
    std::atomic<bool> keepaliveSendFailed{false};
    // Client logging preference (minimum severity)
    std::atomic<Logger::Level> clientLogMin{Logger::Level::DEBUG};

    // Logging rate limiting (per-second)
    std::atomic<unsigned int> logRateLimitPerSec{0u}; // 0 disables throttling
    std::mutex logRateMutex;                 // protects window state below
    std::chrono::steady_clock::time_point logWindowStart{std::chrono::steady_clock::now()};
    unsigned int logWindowCount{0u};

    // Cancellation support
    struct CancellationToken { std::atomic<bool> cancelled{false}; };
    std::mutex cancelMutex;
    std::unordered_map<std::string, std::shared_ptr<CancellationToken>> cancelMap;
    // Track stop_sources for cooperative cancellation of async handlers per request id
    std::unordered_map<std::string, std::vector<std::shared_ptr<std::stop_source>>> stopSources;

    static std::string idToString(const JSONRPCId& id) {
        std::string idStr;
        std::visit([&](const auto& v){
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) { idStr = v; }
            else if constexpr (std::is_same_v<T, int64_t>) { idStr = std::to_string(v); }
            else { idStr = ""; }
        }, id);
        return idStr;
    }

    std::shared_ptr<CancellationToken> registerCancelToken(const std::string& idStr) {
        if (idStr.empty()) return std::make_shared<CancellationToken>();
        std::lock_guard<std::mutex> lk(cancelMutex);
        auto it = cancelMap.find(idStr);
        if (it != cancelMap.end()) return it->second;
        auto tok = std::make_shared<CancellationToken>();
        cancelMap[idStr] = tok;
        return tok;
    }

    void unregisterCancelToken(const std::string& idStr) {
        if (idStr.empty()) return;
        std::lock_guard<std::mutex> lk(cancelMutex);
        cancelMap.erase(idStr);
        // Also clear any stale stop sources for this id
        stopSources.erase(idStr);
    }

    void cancelById(const std::string& idStr) {
        std::lock_guard<std::mutex> lk(cancelMutex);
        auto it = cancelMap.find(idStr);
        if (it == cancelMap.end() || !it->second) {
            // Create a token so late registrants will observe the cancelled state
            auto tok = std::make_shared<CancellationToken>();
            tok->cancelled.store(true);
            cancelMap[idStr] = tok;
        } else {
            it->second->cancelled.store(true);
        }
        // Request stop on any registered async handlers for this id
        auto itS = stopSources.find(idStr);
        if (itS != stopSources.end()) {
            for (auto& src : itS->second) {
                if (src) {
                    try { src->request_stop(); } catch (...) {}
                }
            }
        }
    }

    // Register a stop_source for the given id and return it. Caller should unregister when done.
    std::shared_ptr<std::stop_source> registerStopSource(const std::string& idStr) {
        auto src = std::make_shared<std::stop_source>();
        std::lock_guard<std::mutex> lk(cancelMutex);
        stopSources[idStr].push_back(src);
        // If this id has already been cancelled before the stop_source was registered,
        // immediately request stop so cooperative handlers observe cancellation.
        auto it = cancelMap.find(idStr);
        if (it != cancelMap.end() && it->second && it->second->cancelled.load()) {
            try { src->request_stop(); } catch (...) {}
        }
        return src;
    }

    void unregisterStopSource(const std::string& idStr, const std::shared_ptr<std::stop_source>& src) {
        std::lock_guard<std::mutex> lk(cancelMutex);
        auto it = stopSources.find(idStr);
        if (it == stopSources.end()) return;
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const std::shared_ptr<std::stop_source>& p){ return p.get() == src.get(); }), vec.end());
        if (vec.empty()) stopSources.erase(it);
    }

    // Registry maps
    std::unordered_map<std::string, ToolHandler> toolHandlers;
    std::unordered_map<std::string, Tool> toolMetadata;
    std::unordered_map<std::string, ResourceHandler> resourceHandlers;
    std::unordered_map<std::string, PromptHandler> promptHandlers;
    std::vector<std::string> resourceUris;
    std::vector<ResourceTemplate> resourceTemplates;
    
    // Mutex for thread safety
    std::mutex registryMutex;

    Impl()
        : serverInfo("") {
        
        // Set up transport handlers
        // this->transport->SetNotificationHandler(
        //     [this](std::unique_ptr<JSONRPCNotification> notification) {
        //         handleNotification(std::move(notification));
        //     });
        
        // Set default server capabilities
        capabilities.prompts = PromptsCapability{true};
        capabilities.resources = ResourcesCapability{true, true};
        capabilities.tools = ToolsCapability{true};
        capabilities.logging = LoggingCapability{};
    }

    void handleNotification(std::unique_ptr<JSONRPCNotification> notification) {
        LOG_DEBUG("Received notification: {}", notification->method);
        
        if (notification->method == Methods::Initialized) {
            initialized = true;
            LOG_INFO("MCP server initialized by client");
        } else if (notification->method == Methods::Cancelled) {
            // Handle cancellation
            std::string idStr;
            if (notification->params.has_value() && std::holds_alternative<JSONValue::Object>(notification->params->value)) {
                const auto& o = std::get<JSONValue::Object>(notification->params->value);
                auto it = o.find("id");
                if (it != o.end()) {
                    if (std::holds_alternative<std::string>(it->second->value)) {
                        idStr = std::get<std::string>(it->second->value);
                    } else if (std::holds_alternative<int64_t>(it->second->value)) {
                        idStr = std::to_string(std::get<int64_t>(it->second->value));
                    }
                }
            }
            if (!idStr.empty()) {
                cancelById(idStr);
                LOG_INFO("Cancellation received for id={}", idStr);
            } else {
                LOG_WARN("Cancellation notification missing id");
            }
        } else if (notification->method == Methods::Progress) {
            // Handle progress updates
            LOG_DEBUG("Progress update received");
        } else if (notification->method == Methods::ResourceListChanged) {
            // Handle resources list change
            LOG_DEBUG("Resources list changed");
        } else if (notification->method == std::string("notifications/resources/updated")) {
            // Handle resource updates
            LOG_DEBUG("Resources updated");
        } else if (notification->method == Methods::ToolListChanged) {
            // Handle tools list change
            LOG_DEBUG("Tools list changed");
        } else if (notification->method == Methods::PromptListChanged) {
            // Handle prompts list change
            LOG_DEBUG("Prompts list changed");
        } else {
            LOG_WARN("Unknown notification method: {}", notification->method);
        }
    }

    JSONValue serializeServerCapabilities() const {
        JSONValue::Object caps;
        
        if (!capabilities.experimental.empty()) {
            JSONValue::Object experimentalObj;
            for (const auto& [key, val] : capabilities.experimental) {
                experimentalObj[key] = std::make_shared<JSONValue>(val);
            }
            caps["experimental"] = std::make_shared<JSONValue>(experimentalObj);
        }
        
        if (capabilities.prompts.has_value()) {
            JSONValue::Object promptsObj;
            promptsObj["listChanged"] = std::make_shared<JSONValue>(capabilities.prompts->listChanged);
            caps["prompts"] = std::make_shared<JSONValue>(promptsObj);
        }
        
        if (capabilities.resources.has_value()) {
            JSONValue::Object resourcesObj;
            resourcesObj["subscribe"] = std::make_shared<JSONValue>(capabilities.resources->subscribe);
            resourcesObj["listChanged"] = std::make_shared<JSONValue>(capabilities.resources->listChanged);
            caps["resources"] = std::make_shared<JSONValue>(resourcesObj);
        }
        
        if (capabilities.tools.has_value()) {
            JSONValue::Object toolsObj;
            toolsObj["listChanged"] = std::make_shared<JSONValue>(capabilities.tools->listChanged);
            caps["tools"] = std::make_shared<JSONValue>(toolsObj);
        }
        
        // Advertise sampling capability if present
        if (capabilities.sampling.has_value()) {
            JSONValue::Object samplingObj;
            caps["sampling"] = std::make_shared<JSONValue>(samplingObj);
        }
        
        // Advertise logging capability if present
        if (capabilities.logging.has_value()) {
            JSONValue::Object loggingObj;
            caps["logging"] = std::make_shared<JSONValue>(loggingObj);
        }
        
        return JSONValue(caps);
    }

    void parseClientCapabilities(const JSONValue& params) {
        if (std::holds_alternative<JSONValue::Object>(params.value)) {
            const auto& obj = std::get<JSONValue::Object>(params.value);
            
            auto capsIt = obj.find("capabilities");
            if (capsIt != obj.end() && std::holds_alternative<JSONValue::Object>((*capsIt->second).value)) {
                const auto& capsObj = std::get<JSONValue::Object>((*capsIt->second).value);
                
                // Parse sampling capability
                auto samplingIt = capsObj.find("sampling");
                if (samplingIt != capsObj.end()) {
                    clientCapabilities.sampling = SamplingCapability{};
                }
                // Parse experimental.logLevel
                auto expIt = capsObj.find("experimental");
                if (expIt != capsObj.end() && std::holds_alternative<JSONValue::Object>((*expIt->second).value)) {
                    const auto& expObj = std::get<JSONValue::Object>((*expIt->second).value);
                    auto llIt = expObj.find("logLevel");
                    if (llIt != expObj.end() && std::holds_alternative<std::string>(llIt->second->value)) {
                        this->clientLogMin.store(Logger::levelFromString(std::get<std::string>(llIt->second->value)));
                    }
                }
            }
        }
    }

    std::unique_ptr<JSONRPCResponse> handleInitialize(const JSONRPCRequest& request) {
        LOG_INFO("Handling initialize request");
        
        if (request.params.has_value()) {
            parseClientCapabilities(request.params.value());
        }
        
        // Build initialize response
        JSONValue::Object resultObj;
        resultObj["protocolVersion"] = std::make_shared<JSONValue>(PROTOCOL_VERSION);
        resultObj["capabilities"] = std::make_shared<JSONValue>(serializeServerCapabilities());
        JSONValue::Object serverInfoObj;
        serverInfoObj["name"] = std::make_shared<JSONValue>(serverInfo);
        serverInfoObj["version"] = std::make_shared<JSONValue>("1.0.0");
        resultObj["serverInfo"] = std::make_shared<JSONValue>(serverInfoObj);
        
        JSONValue result(resultObj);
        
        auto response = std::make_unique<JSONRPCResponse>();
        response->id = request.id;
        response->result = result;
        
        return response;
    }

    std::unique_ptr<JSONRPCResponse> handleToolsList(const JSONRPCRequest& request) {
        LOG_DEBUG("Handling tools/list request");
        
        // Parse optional paging params
        size_t start = 0;
        std::optional<size_t> limitOpt;
        if (request.params.has_value() && std::holds_alternative<JSONValue::Object>(request.params->value)) {
            const auto& o = std::get<JSONValue::Object>(request.params->value);
            auto it = o.find("cursor");
            if (it != o.end()) {
                if (std::holds_alternative<std::string>(it->second->value)) {
                    try { start = static_cast<size_t>(std::stoll(std::get<std::string>(it->second->value))); } catch (...) {}
                } else if (std::holds_alternative<int64_t>(it->second->value)) {
                    start = static_cast<size_t>(std::get<int64_t>(it->second->value));
                }
            }
            it = o.find("limit");
            if (it != o.end() && std::holds_alternative<int64_t>(it->second->value)) {
                auto lim = std::get<int64_t>(it->second->value);
                if (lim > 0) limitOpt = static_cast<size_t>(lim);
            }
        }
        
        std::lock_guard<std::mutex> lock(registryMutex);
        
        // Gather tools (from metadata) into a stable vector for paging to include sync and async tools
        std::vector<std::string> names;
        names.reserve(toolMetadata.size());
        for (const auto& [name, _] : toolMetadata) names.push_back(name);
        std::sort(names.begin(), names.end());
        
        JSONValue::Array toolsArray;
        size_t total = names.size();
        size_t count = 0;
        size_t i = start < total ? start : total;
        for (; i < total; ++i) {
            if (limitOpt.has_value() && count >= *limitOpt) break;
            const auto& name = names[i];
            JSONValue::Object toolObj;
            toolObj["name"] = std::make_shared<JSONValue>(name);
            auto mdIt = toolMetadata.find(name);
            if (mdIt != toolMetadata.end()) {
                toolObj["description"] = std::make_shared<JSONValue>(mdIt->second.description);
                toolObj["inputSchema"] = std::make_shared<JSONValue>(mdIt->second.inputSchema);
            } else {
                toolObj["description"] = std::make_shared<JSONValue>(std::string("Tool: ") + name);
                JSONValue::Object inputSchema;
                inputSchema["type"] = std::make_shared<JSONValue>("object");
                inputSchema["properties"] = std::make_shared<JSONValue>(JSONValue::Object{});
                toolObj["inputSchema"] = std::make_shared<JSONValue>(inputSchema);
            }
            toolsArray.push_back(std::make_shared<JSONValue>(toolObj));
            ++count;
        }
        
        JSONValue::Object resultObj;
        resultObj["tools"] = std::make_shared<JSONValue>(toolsArray);
        if (limitOpt.has_value() && i < total) {
            resultObj["nextCursor"] = std::make_shared<JSONValue>(std::to_string(i));
        }
        JSONValue result(resultObj);
        
        auto response = std::make_unique<JSONRPCResponse>();
        response->id = request.id;
        response->result = result;
        
        return response;
    }

    std::unique_ptr<JSONRPCResponse> handleToolsCall(const JSONRPCRequest& request,
                                                     const std::shared_ptr<CancellationToken>& token) {
        LOG_DEBUG("Handling tools/call request");
        
        if (!request.params.has_value()) {
            errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid params";
            return errors::makeErrorResponse(request.id, e);
        }
        
        std::string toolName;
        JSONValue arguments;
        
        if (std::holds_alternative<JSONValue::Object>(request.params->value)) {
            const auto& paramsObj = std::get<JSONValue::Object>(request.params->value);
            
            auto nameIt = paramsObj.find("name");
            if (nameIt != paramsObj.end() && std::holds_alternative<std::string>(nameIt->second->value)) {
                toolName = std::get<std::string>(nameIt->second->value);
            }
            
            auto argsIt = paramsObj.find("arguments");
            if (argsIt != paramsObj.end()) {
                arguments = *argsIt->second;
            }
        }
        
        // Early cancellation state before invoking handler
        const std::string idStr = idToString(request.id);
        const bool preCancelled = (token && token->cancelled.load());
        
        ToolHandler handlerCopy;
        {
            std::lock_guard<std::mutex> lock(registryMutex);
            auto it = toolHandlers.find(toolName);
            if (it == toolHandlers.end()) {
                auto response = std::make_unique<JSONRPCResponse>();
                response->id = request.id;
                JSONValue::Object errorObj;
                errorObj["code"] = std::make_shared<JSONValue>(static_cast<int64_t>(-32601));
                errorObj["message"] = std::make_shared<JSONValue>(std::string("Tool not found: ") + toolName);
                response->error = JSONValue(errorObj);
                return response;
            }
            handlerCopy = it->second;
        }
        
        // Invoke async handler with stop_token for cooperative cancellation
        ToolResult result;
        {
            auto stopSrc = registerStopSource(idStr);
            // If cancellation arrived before the handler was invoked, request stop immediately
            if (preCancelled) { try { stopSrc->request_stop(); } catch (...) {} }
            struct StopGuard { Impl* impl; std::string id; std::shared_ptr<std::stop_source> src; ~StopGuard(){ if (impl && src) impl->unregisterStopSource(id, src); } } stopGuard{ this, idStr, stopSrc };
            auto fut = handlerCopy(arguments, stopSrc->get_token());
            if (preCancelled) {
                // Return cancelled immediately but allow cooperative handler to observe stop and exit.
                std::thread([f = std::move(fut)]() mutable { try { f.wait(); } catch (...) {} }).detach();
                return CreateErrorResponse(request.id, JSONRPCErrorCodes::InternalError, "Cancelled", std::nullopt);
            }
            result = fut.get();
        }
        // Check for cancellation after handler returns (mid-flight cancellation)
        if (token && token->cancelled.load()) {
            LOG_DEBUG("Cancellation detected after tool handler for id={}", idStr);
            return CreateErrorResponse(request.id, JSONRPCErrorCodes::InternalError, "Cancelled", std::nullopt);
        }
        
        JSONValue::Object resultObj;
        resultObj["isError"] = std::make_shared<JSONValue>(result.isError);
        JSONValue::Array contentArray;
        if (!result.content.empty()) {
            for (auto& v : result.content) {
                contentArray.push_back(std::make_shared<JSONValue>(std::move(v)));
            }
        } else {
            JSONValue::Object contentObj;
            contentObj["type"] = std::make_shared<JSONValue>(std::string("text"));
            contentObj["text"] = std::make_shared<JSONValue>(std::string("Error"));
            contentArray.push_back(std::make_shared<JSONValue>(contentObj));
        }
        resultObj["content"] = std::make_shared<JSONValue>(contentArray);
        
        auto response = std::make_unique<JSONRPCResponse>();
        response->id = request.id;
        response->result = JSONValue{resultObj};
        
        return response;
    }

    std::unique_ptr<JSONRPCResponse> handleResourcesList(const JSONRPCRequest& request) {
        LOG_DEBUG("Handling resources/list request");
        
        // Parse optional paging params
        size_t start = 0;
        std::optional<size_t> limitOpt;
        if (request.params.has_value() && std::holds_alternative<JSONValue::Object>(request.params->value)) {
            const auto& o = std::get<JSONValue::Object>(request.params->value);
            auto it = o.find("cursor");
            if (it != o.end()) {
                if (std::holds_alternative<std::string>(it->second->value)) {
                    try { start = static_cast<size_t>(std::stoll(std::get<std::string>(it->second->value))); } catch (...) {}
                } else if (std::holds_alternative<int64_t>(it->second->value)) {
                    start = static_cast<size_t>(std::get<int64_t>(it->second->value));
                }
            }
            it = o.find("limit");
            if (it != o.end() && std::holds_alternative<int64_t>(it->second->value)) {
                auto lim = std::get<int64_t>(it->second->value);
                if (lim > 0) limitOpt = static_cast<size_t>(lim);
            }
        }
        
        std::lock_guard<std::mutex> lock(registryMutex);
        
        JSONValue::Array resourcesArray;
        size_t total = resourceUris.size();
        size_t count = 0;
        size_t i = start < total ? start : total;
        for (; i < total; ++i) {
            if (limitOpt.has_value() && count >= *limitOpt) break;
            const auto& uri = resourceUris[i];
            JSONValue::Object resourceObj;
            resourceObj["uri"] = std::make_shared<JSONValue>(uri);
            resourceObj["name"] = std::make_shared<JSONValue>(uri);
            resourceObj["description"] = std::make_shared<JSONValue>(std::string("Resource: ") + uri);
            resourceObj["mimeType"] = std::make_shared<JSONValue>(std::string("text/plain"));
            resourcesArray.push_back(std::make_shared<JSONValue>(resourceObj));
            ++count;
        }
        
        JSONValue::Object resultObj;
        resultObj["resources"] = std::make_shared<JSONValue>(resourcesArray);
        if (limitOpt.has_value() && i < total) {
            resultObj["nextCursor"] = std::make_shared<JSONValue>(std::to_string(i));
        }
        JSONValue result(resultObj);
        
        auto response = std::make_unique<JSONRPCResponse>();
        response->id = request.id;
        response->result = result;
        
        return response;
    }

    std::unique_ptr<JSONRPCResponse> handleResourceTemplatesList(const JSONRPCRequest& request) {
        LOG_DEBUG("Handling resources/templates/list request");

        // Parse optional paging params
        size_t start = 0;
        std::optional<size_t> limitOpt;
        if (request.params.has_value() && std::holds_alternative<JSONValue::Object>(request.params->value)) {
            const auto& o = std::get<JSONValue::Object>(request.params->value);
            auto it = o.find("cursor");
            if (it != o.end()) {
                if (std::holds_alternative<std::string>(it->second->value)) {
                    try { start = static_cast<size_t>(std::stoll(std::get<std::string>(it->second->value))); } catch (...) {}
                } else if (std::holds_alternative<int64_t>(it->second->value)) {
                    start = static_cast<size_t>(std::get<int64_t>(it->second->value));
                }
            }
            it = o.find("limit");
            if (it != o.end() && std::holds_alternative<int64_t>(it->second->value)) {
                auto lim = std::get<int64_t>(it->second->value);
                if (lim > 0) limitOpt = static_cast<size_t>(lim);
            }
        }

        std::lock_guard<std::mutex> lock(registryMutex);

        JSONValue::Array templatesArray;
        size_t total = resourceTemplates.size();
        size_t count = 0;
        size_t i = start < total ? start : total;
        for (; i < total; ++i) {
            if (limitOpt.has_value() && count >= *limitOpt) break;
            const auto& rt = resourceTemplates[i];
            JSONValue::Object obj;
            obj["uriTemplate"] = std::make_shared<JSONValue>(rt.uriTemplate);
            obj["name"] = std::make_shared<JSONValue>(rt.name);
            if (rt.description.has_value()) {
                obj["description"] = std::make_shared<JSONValue>(rt.description.value());
            }
            if (rt.mimeType.has_value()) {
                obj["mimeType"] = std::make_shared<JSONValue>(rt.mimeType.value());
            }
            templatesArray.push_back(std::make_shared<JSONValue>(obj));
            ++count;
        }

        JSONValue::Object resultObj;
        resultObj["resourceTemplates"] = std::make_shared<JSONValue>(templatesArray);
        if (limitOpt.has_value() && i < total) {
            resultObj["nextCursor"] = std::make_shared<JSONValue>(std::to_string(i));
        }
        auto response = std::make_unique<JSONRPCResponse>();
        response->id = request.id;
        response->result = JSONValue{resultObj};
        return response;
    }

    std::unique_ptr<JSONRPCResponse> handleResourcesRead(const JSONRPCRequest& request,
                                                         const std::shared_ptr<CancellationToken>& token) {
        LOG_DEBUG("Handling resources/read request");
        
        if (!request.params.has_value()) {
            errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid params";
            return errors::makeErrorResponse(request.id, e);
        }
        
        std::string uri;
        if (std::holds_alternative<JSONValue::Object>(request.params->value)) {
            const auto& paramsObj = std::get<JSONValue::Object>(request.params->value);
            auto uriIt = paramsObj.find("uri");
            if (uriIt != paramsObj.end() && std::holds_alternative<std::string>(uriIt->second->value)) {
                uri = std::get<std::string>(uriIt->second->value);
            }
        }
        
        // Early cancellation state before invoking handler
        const std::string idStr = idToString(request.id);
        const bool preCancelled = (token && token->cancelled.load());
        
        ResourceHandler handlerCopy;
        {
            std::lock_guard<std::mutex> lock(registryMutex);
            auto it = resourceHandlers.find(uri);
            if (it == resourceHandlers.end()) {
                auto response = std::make_unique<JSONRPCResponse>();
                response->id = request.id;
                JSONValue::Object errorObj;
                errorObj["code"] = std::make_shared<JSONValue>(static_cast<int64_t>(-32601));
                errorObj["message"] = std::make_shared<JSONValue>(std::string("Resource not found: ") + uri);
                response->error = JSONValue(errorObj);
                return response;
            }
            handlerCopy = it->second;
        }
        
        // Invoke async handler with stop_token for cooperative cancellation
        ResourceContent content;
        {
            auto stopSrc = registerStopSource(idStr);
            if (preCancelled) { try { stopSrc->request_stop(); } catch (...) {} }
            struct StopGuard { Impl* impl; std::string id; std::shared_ptr<std::stop_source> src; ~StopGuard(){ if (impl && src) impl->unregisterStopSource(id, src); } } stopGuard{ this, idStr, stopSrc };
            auto rfut = handlerCopy(uri, stopSrc->get_token());
            if (preCancelled) {
                std::thread([f = std::move(rfut)]() mutable { try { f.wait(); } catch (...) {} }).detach();
                return CreateErrorResponse(request.id, JSONRPCErrorCodes::InternalError, "Cancelled", std::nullopt);
            }
            content = rfut.get();
        }
        // Check for cancellation after handler returns (mid-flight cancellation)
        if (token && token->cancelled.load()) {
            LOG_DEBUG("Cancellation detected after resource handler for id={}", idStr);
            return CreateErrorResponse(request.id, JSONRPCErrorCodes::InternalError, "Cancelled", std::nullopt);
        }
        
        JSONValue::Object resultObj;
        JSONValue::Array contentsArray;
        for (auto& v : content.contents) {
            contentsArray.push_back(std::make_shared<JSONValue>(std::move(v)));
        }
        resultObj["contents"] = std::make_shared<JSONValue>(contentsArray);
        
        auto response = std::make_unique<JSONRPCResponse>();
        response->id = request.id;
        response->result = JSONValue{resultObj};
        
        return response;
    }

    std::unique_ptr<JSONRPCResponse> handlePromptsList(const JSONRPCRequest& request) {
        LOG_DEBUG("Handling prompts/list request");
        
        // Parse optional paging params
        size_t start = 0;
        std::optional<size_t> limitOpt;
        if (request.params.has_value() && std::holds_alternative<JSONValue::Object>(request.params->value)) {
            const auto& o = std::get<JSONValue::Object>(request.params->value);
            auto it = o.find("cursor");
            if (it != o.end()) {
                if (std::holds_alternative<std::string>(it->second->value)) {
                    try { start = static_cast<size_t>(std::stoll(std::get<std::string>(it->second->value))); } catch (...) {}
                } else if (std::holds_alternative<int64_t>(it->second->value)) {
                    start = static_cast<size_t>(std::get<int64_t>(it->second->value));
                }
            }
            it = o.find("limit");
            if (it != o.end() && std::holds_alternative<int64_t>(it->second->value)) {
                auto lim = std::get<int64_t>(it->second->value);
                if (lim > 0) limitOpt = static_cast<size_t>(lim);
            }
        }
        
        std::lock_guard<std::mutex> lock(registryMutex);
        
        // Stable ordering: sort names
        std::vector<std::string> names;
        names.reserve(promptHandlers.size());
        for (const auto& [name, _] : promptHandlers) names.push_back(name);
        std::sort(names.begin(), names.end());
        
        JSONValue::Array promptsArray;
        size_t total = names.size();
        size_t count = 0;
        size_t i = start < total ? start : total;
        for (; i < total; ++i) {
            if (limitOpt.has_value() && count >= *limitOpt) break;
            const auto& name = names[i];
            JSONValue::Object promptObj;
            promptObj["name"] = std::make_shared<JSONValue>(name);
            promptObj["description"] = std::make_shared<JSONValue>(std::string("Prompt: ") + name);
            promptObj["arguments"] = std::make_shared<JSONValue>(JSONValue::Array{});
            promptsArray.push_back(std::make_shared<JSONValue>(promptObj));
            ++count;
        }
        
        JSONValue::Object resultObj;
        resultObj["prompts"] = std::make_shared<JSONValue>(promptsArray);
        if (limitOpt.has_value() && i < total) {
            resultObj["nextCursor"] = std::make_shared<JSONValue>(std::to_string(i));
        }
        JSONValue result(resultObj);
        
        auto response = std::make_unique<JSONRPCResponse>();
        response->id = request.id;
        response->result = result;
        
        return response;
    }

    std::unique_ptr<JSONRPCResponse> handlePromptsGet(const JSONRPCRequest& request) {
        LOG_DEBUG("Handling prompts/get request");
        
        if (!request.params.has_value()) {
            errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid params";
            return errors::makeErrorResponse(request.id, e);
        }
        
        std::string promptName;
        JSONValue arguments;
        
        if (std::holds_alternative<JSONValue::Object>(request.params->value)) {
            const auto& paramsObj = std::get<JSONValue::Object>(request.params->value);
            
            auto nameIt = paramsObj.find("name");
            if (nameIt != paramsObj.end() && std::holds_alternative<std::string>(nameIt->second->value)) {
                promptName = std::get<std::string>(nameIt->second->value);
            }
            
            auto argsIt = paramsObj.find("arguments");
            if (argsIt != paramsObj.end()) {
                arguments = *argsIt->second;
            }
        }
        
        std::lock_guard<std::mutex> lock(registryMutex);
        auto it = promptHandlers.find(promptName);
        if (it == promptHandlers.end()) {
            auto response = std::make_unique<JSONRPCResponse>();
            response->id = request.id;
            JSONValue::Object errorObj;
            errorObj["code"] = std::make_shared<JSONValue>(static_cast<int64_t>(-32601));
            errorObj["message"] = std::make_shared<JSONValue>(std::string("Prompt not found: ") + promptName);
            response->error = JSONValue(errorObj);
            return response;
        }
        
        // Call the prompt handler
        PromptResult result = it->second(arguments);
        
        JSONValue::Object resultObj;
        resultObj["description"] = std::make_shared<JSONValue>(result.description);
        // Serialize actual messages from PromptResult
        JSONValue::Array messagesArray;
        for (auto& v : result.messages) {
            messagesArray.push_back(std::make_shared<JSONValue>(std::move(v)));
        }
        resultObj["messages"] = std::make_shared<JSONValue>(messagesArray);
        
        auto response = std::make_unique<JSONRPCResponse>();
        response->id = request.id;
        response->result = JSONValue{resultObj};
        
        return response;
    }
};

//============================ Impl coroutine helper definitions ============================
mcp::async::Task<void> Server::Impl::coStart(std::unique_ptr<ITransport> transport) {
    FUNC_SCOPE();
    this->transport = std::move(transport);
    // Wire transport handlers for server-side processing
    this->transport->SetNotificationHandler([this](std::unique_ptr<JSONRPCNotification> n){
        if (!n) return;
        try { this->handleNotification(std::move(n)); }
        catch (const std::exception& e) { LOG_ERROR("Server notification handler exception: {}", e.what()); }
    });

    this->transport->SetErrorHandler([this](const std::string& err){
        LOG_ERROR("Transport error: {}", err);
        // If an error occurs during a keepalive send, mark last send as failed
        if (this->keepaliveSending.load()) {
            this->keepaliveSendFailed.store(true);
        }
        if (this->errorCallback) {
            try { this->errorCallback(err); }
            catch (const std::exception& e) { LOG_ERROR("Server error callback exception: {}", e.what()); }
        }
    });

    this->transport->SetRequestHandler([this](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        try {
            const std::string idStr = Impl::idToString(req.id);
            // Register cancellation token for this request and auto-unregister on exit
            auto token = this->registerCancelToken(idStr);
            struct ScopeGuard { std::function<void()> f; ~ScopeGuard(){ if (f) f(); } } guard{ [this, idStr](){ this->unregisterCancelToken(idStr); } };
            if (req.method == Methods::Initialize) {
                auto resp = this->handleInitialize(req);
                // Send notifications/initialized asynchronously
                std::thread([this]() {
                    auto note = std::make_unique<JSONRPCNotification>();
                    note->method = Methods::Initialized;
                    note->params = JSONValue{JSONValue::Object{}};
                    (void)this->transport->SendNotification(std::move(note));
                    // Broadcast initial list_changed notifications so clients learn current state
                    {
                        auto n = std::make_unique<JSONRPCNotification>();
                        n->method = Methods::ToolListChanged;
                        n->params = JSONValue{JSONValue::Object{}};
                        (void)this->transport->SendNotification(std::move(n));
                    }
                    {
                        auto n = std::make_unique<JSONRPCNotification>();
                        n->method = Methods::ResourceListChanged;
                        n->params = JSONValue{JSONValue::Object{}};
                        (void)this->transport->SendNotification(std::move(n));
                    }
                    {
                        auto n = std::make_unique<JSONRPCNotification>();
                        n->method = Methods::PromptListChanged;
                        n->params = JSONValue{JSONValue::Object{}};
                        (void)this->transport->SendNotification(std::move(n));
                    }
                }).detach();
                return resp;
            } else if (req.method == Methods::ListTools) {
                return this->handleToolsList(req);
            } else if (req.method == Methods::CallTool) {
                auto r = this->handleToolsCall(req, token);
                if (token && token->cancelled.load()) {
                    return CreateErrorResponse(req.id, JSONRPCErrorCodes::InternalError, "Cancelled", std::nullopt);
                }
                return r;
            } else if (req.method == Methods::ListResources) {
                return this->handleResourcesList(req);
            } else if (req.method == Methods::ListResourceTemplates) {
                return this->handleResourceTemplatesList(req);
            } else if (req.method == Methods::Subscribe) {
                // Subscribe to a specific URI if provided
                this->resourcesSubscribed = true;
                if (req.params.has_value() && std::holds_alternative<JSONValue::Object>(req.params->value)) {
                    const auto& o = std::get<JSONValue::Object>(req.params->value);
                    auto it = o.find("uri");
                    if (it != o.end() && std::holds_alternative<std::string>(it->second->value)) {
                        const std::string& uri = std::get<std::string>(it->second->value);
                        std::lock_guard<std::mutex> lk(this->registryMutex);
                        this->subscribedUris.insert(uri);
                    }
                }
                auto resp = std::make_unique<JSONRPCResponse>();
                resp->id = req.id;
                resp->result = JSONValue{JSONValue::Object{}};
                return resp;
            } else if (req.method == Methods::Unsubscribe) {
                // Unsubscribe from a specific URI if provided; if none, clear all
                if (req.params.has_value() && std::holds_alternative<JSONValue::Object>(req.params->value)) {
                    const auto& o = std::get<JSONValue::Object>(req.params->value);
                    auto it = o.find("uri");
                    if (it != o.end() && std::holds_alternative<std::string>(it->second->value)) {
                        const std::string& uri = std::get<std::string>(it->second->value);
                        std::lock_guard<std::mutex> lk(this->registryMutex);
                        this->subscribedUris.erase(uri);
                    } else {
                        std::lock_guard<std::mutex> lk(this->registryMutex);
                        this->subscribedUris.clear();
                    }
                } else {
                    std::lock_guard<std::mutex> lk(this->registryMutex);
                    this->subscribedUris.clear();
                }
                // If no URIs remain, consider overall subscription off
                this->resourcesSubscribed = !this->subscribedUris.empty();
                auto resp = std::make_unique<JSONRPCResponse>();
                resp->id = req.id;
                resp->result = JSONValue{JSONValue::Object{}};
                return resp;
            } else if (req.method == Methods::CreateMessage) {
                // Client-initiated sampling request to the server (if supported)
                if (!this->samplingHandler) {
                    errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "No server sampling handler registered";
                    return errors::makeErrorResponse(req.id, e);
                }
                JSONValue messages; JSONValue modelPreferences; JSONValue systemPrompt; JSONValue includeContext;
                if (req.params.has_value() && std::holds_alternative<JSONValue::Object>(req.params->value)) {
                    const auto& o = std::get<JSONValue::Object>(req.params->value);
                    auto it = o.find("messages"); if (it != o.end()) messages = *it->second;
                    it = o.find("modelPreferences"); if (it != o.end()) modelPreferences = *it->second;
                    it = o.find("systemPrompt"); if (it != o.end()) systemPrompt = *it->second;
                    it = o.find("includeContext"); if (it != o.end()) includeContext = *it->second;
                }
                auto fut = this->samplingHandler(messages, modelPreferences, systemPrompt, includeContext);
                JSONValue result = fut.get();
                if (token && token->cancelled.load()) {
                    return CreateErrorResponse(req.id, JSONRPCErrorCodes::InternalError, "Cancelled", std::nullopt);
                }
                auto resp = std::make_unique<JSONRPCResponse>();
                resp->id = req.id;
                resp->result = result;
                return resp;
            } else if (req.method == Methods::ReadResource) {
                auto r = this->handleResourcesRead(req, token);
                if (token && token->cancelled.load()) {
                    return CreateErrorResponse(req.id, JSONRPCErrorCodes::InternalError, "Cancelled", std::nullopt);
                }
                return r;
            } else if (req.method == Methods::ListPrompts) {
                return this->handlePromptsList(req);
            } else if (req.method == Methods::GetPrompt) {
                auto r = this->handlePromptsGet(req);
                if (token && token->cancelled.load()) {
                    return CreateErrorResponse(req.id, JSONRPCErrorCodes::InternalError, "Cancelled", std::nullopt);
                }
                return r;
            }
            { errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotFound; e.message = "Method not found"; return errors::makeErrorResponse(req.id, e); }
        } catch (const std::exception& e) {
            return CreateErrorResponse(req.id, JSONRPCErrorCodes::InternalError, e.what(), std::nullopt);
        }
    });

    auto fut = this->transport->Start();
    try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); }
    catch (const std::exception& e) { LOG_ERROR("Server start exception: {}", e.what()); }
    co_return;
}

mcp::async::Task<void> Server::Impl::coStop() {
    FUNC_SCOPE();
    LOG_INFO("Stopping MCP server");
    if (this->keepaliveThread.joinable()) {
        this->keepaliveStop.store(true);
        try { this->keepaliveThread.join(); } catch (...) {}
    }
    if (this->transport) {
        this->initialized = false;
        auto fut = this->transport->Close();
        try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); }
        catch (const std::exception& e) { LOG_ERROR("Server stop exception: {}", e.what()); }
    }
    co_return;
}

mcp::async::Task<void> Server::Impl::coHandleInitialize(const Implementation& clientInfo, const ClientCapabilities& capabilities) {
    FUNC_SCOPE();
    (void)clientInfo;
    this->clientCapabilities = capabilities;
    this->initialized = true;
    co_return;
}

mcp::async::Task<JSONValue> Server::Impl::coCallTool(const std::string& name, const JSONValue& arguments) {
    FUNC_SCOPE();
    ToolHandler handlerCopy;
    {
        std::lock_guard<std::mutex> lock(this->registryMutex);
        auto it = this->toolHandlers.find(name);
        if (it != this->toolHandlers.end()) handlerCopy = it->second;
    }
    if (handlerCopy) {
        std::stop_source src;
        auto fut = handlerCopy(arguments, src.get_token());
        ToolResult result = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        JSONValue::Object resultObj;
        JSONValue::Array contentArray;
        for (auto& v : result.content) contentArray.push_back(std::make_shared<JSONValue>(std::move(v)));
        resultObj["content"] = std::make_shared<JSONValue>(contentArray);
        resultObj["isError"] = std::make_shared<JSONValue>(result.isError);
        co_return JSONValue{resultObj};
    }
    co_return JSONValue{nullptr};
}

mcp::async::Task<JSONValue> Server::Impl::coReadResource(const std::string& uri) {
    FUNC_SCOPE();
    ResourceHandler handlerCopy;
    {
        std::lock_guard<std::mutex> lock(this->registryMutex);
        auto it = this->resourceHandlers.find(uri);
        if (it != this->resourceHandlers.end()) handlerCopy = it->second;
    }
    if (handlerCopy) {
        std::stop_source src;
        auto fut = handlerCopy(uri, src.get_token());
        ResourceContent result = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        JSONValue::Object resultObj;
        JSONValue::Array contentsArray;
        for (auto& v : result.contents) contentsArray.push_back(std::make_shared<JSONValue>(std::move(v)));
        resultObj["contents"] = std::make_shared<JSONValue>(contentsArray);
        co_return JSONValue{resultObj};
    }
    co_return JSONValue{nullptr};
}

mcp::async::Task<JSONValue> Server::Impl::coGetPrompt(const std::string& name, const JSONValue& arguments) {
    FUNC_SCOPE();
    std::lock_guard<std::mutex> lock(this->registryMutex);
    auto it = this->promptHandlers.find(name);
    if (it != this->promptHandlers.end()) {
        PromptResult result = it->second(arguments);
        JSONValue::Object resultObj;
        resultObj["description"] = std::make_shared<JSONValue>(result.description);
        JSONValue::Array messagesArray;
        for (auto& v : result.messages) {
            messagesArray.push_back(std::make_shared<JSONValue>(std::move(v)));
        }
        resultObj["messages"] = std::make_shared<JSONValue>(messagesArray);
        co_return JSONValue{resultObj};
    }
    co_return JSONValue{nullptr};
}

mcp::async::Task<void> Server::Impl::coSendNotification(const std::string& method, const JSONValue& params) {
    FUNC_SCOPE();
    auto notification = std::make_unique<JSONRPCNotification>();
    notification->method = method;
    notification->params = params;
    auto fut = this->transport->SendNotification(std::move(notification));
    try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); }
    catch (const std::exception& e) { LOG_ERROR("SendNotification exception: {}", e.what()); }
    co_return;
}

mcp::async::Task<void> Server::Impl::coNotifyResourcesListChanged() {
    FUNC_SCOPE();
    auto fut = this->coSendNotification(Methods::ResourceListChanged, JSONValue{JSONValue::Object{}}).toFuture();
    try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); } catch (...) {}
    co_return;
}

mcp::async::Task<void> Server::Impl::coNotifyToolsListChanged() {
    FUNC_SCOPE();
    auto fut = this->coSendNotification(Methods::ToolListChanged, JSONValue{JSONValue::Object{}}).toFuture();
    try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); } catch (...) {}
    co_return;
}

mcp::async::Task<void> Server::Impl::coNotifyPromptsListChanged() {
    FUNC_SCOPE();
    auto fut = this->coSendNotification(Methods::PromptListChanged, JSONValue{JSONValue::Object{}}).toFuture();
    try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); } catch (...) {}
    co_return;
}

mcp::async::Task<void> Server::Impl::coNotifyResourceUpdated(const std::string& uri) {
    FUNC_SCOPE();
    bool shouldSend = true;
    if (this->resourcesSubscribed.load()) {
        std::lock_guard<std::mutex> lock(this->registryMutex);
        if (!this->subscribedUris.empty() && this->subscribedUris.find(uri) == this->subscribedUris.end()) {
            shouldSend = false;
        }
    }
    if (!shouldSend) co_return;
    JSONValue::Object paramsObj;
    paramsObj["uri"] = std::make_shared<JSONValue>(uri);
    {
        auto fut = this->coSendNotification("notifications/resources/updated", JSONValue{paramsObj}).toFuture();
        try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); } catch (...) {}
    }
    co_return;
}

mcp::async::Task<void> Server::Impl::coSendProgress(const std::string& token, double progress, const std::string& message) {
    FUNC_SCOPE();
    JSONValue::Object paramsObj;
    paramsObj["progressToken"] = std::make_shared<JSONValue>(token);
    paramsObj["progress"] = std::make_shared<JSONValue>(progress);
    paramsObj["message"] = std::make_shared<JSONValue>(message);
    {
        auto fut = this->coSendNotification(Methods::Progress, JSONValue{paramsObj}).toFuture();
        try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); } catch (...) {}
    }
    co_return;
}

mcp::async::Task<void> Server::Impl::coLogToClient(const std::string& level, const std::string& message, const std::optional<JSONValue>& data) {
    FUNC_SCOPE();
    Logger::Level sev = Logger::levelFromString(level);
    if (sev < this->clientLogMin.load()) {
        co_return; // Suppressed
    }
    // Apply simple per-second rate limiting if enabled
    {
        unsigned int limit = this->logRateLimitPerSec.load();
        if (limit > 0u) {
            std::lock_guard<std::mutex> lk(this->logRateMutex);
            auto now = std::chrono::steady_clock::now();
            if (now - this->logWindowStart >= std::chrono::seconds(1)) {
                this->logWindowStart = now;
                this->logWindowCount = 0u;
            }
            if (this->logWindowCount >= limit) {
                co_return; // Throttled
            }
            ++this->logWindowCount;
        }
    }
    JSONValue::Object obj;
    obj["level"] = std::make_shared<JSONValue>(level);
    obj["message"] = std::make_shared<JSONValue>(message);
    if (data.has_value()) {
        obj["data"] = std::make_shared<JSONValue>(data.value());
    }
    {
        auto fut = this->coSendNotification(Methods::Log, JSONValue{obj}).toFuture();
        try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); } catch (...) {}
    }
    co_return;
}

mcp::async::Task<JSONValue> Server::Impl::coRequestCreateMessage(const CreateMessageParams& params) {
    FUNC_SCOPE();
    if (!this->transport) {
        LOG_ERROR("RequestCreateMessage called without transport");
        co_return JSONValue{};
    }
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::CreateMessage;
    JSONValue::Object obj;
    // messages (array)
    JSONValue::Array msgs;
    msgs.reserve(params.messages.size());
    for (const auto& m : params.messages) msgs.push_back(std::make_shared<JSONValue>(m));
    obj["messages"] = std::make_shared<JSONValue>(msgs);
    // Optional fields
    if (params.modelPreferences.has_value()) {
        obj["modelPreferences"] = std::make_shared<JSONValue>(params.modelPreferences.value());
    }
    if (params.systemPrompt.has_value()) {
        obj["systemPrompt"] = std::make_shared<JSONValue>(params.systemPrompt.value());
    }
    if (params.includeContext.has_value()) {
        obj["includeContext"] = std::make_shared<JSONValue>(params.includeContext.value());
    }
    if (params.maxTokens.has_value()) {
        obj["maxTokens"] = std::make_shared<JSONValue>(static_cast<int64_t>(params.maxTokens.value()));
    }
    if (params.temperature.has_value()) {
        obj["temperature"] = std::make_shared<JSONValue>(params.temperature.value());
    }
    if (params.stopSequences.has_value()) {
        JSONValue::Array arr;
        for (const auto& s : params.stopSequences.value()) {
            arr.push_back(std::make_shared<JSONValue>(s));
        }
        obj["stopSequences"] = std::make_shared<JSONValue>(arr);
    }
    if (params.metadata.has_value()) {
        obj["metadata"] = std::make_shared<JSONValue>(params.metadata.value());
    }
    request->params = JSONValue{obj};

    auto fut = this->transport->SendRequest(std::move(request));
    try {
        auto resp = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (resp) {
            if (resp->result.has_value()) co_return resp->result.value();
            if (resp->error.has_value()) co_return resp->error.value();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("RequestCreateMessage exception: {}", e.what());
    }
    co_return JSONValue{};
}

Server::Server(const std::string& serverInfo)
    : pImpl(std::make_unique<Impl>()) {
    FUNC_SCOPE();
    pImpl->serverInfo = serverInfo;
}

Server::~Server() {
    FUNC_SCOPE();
    // Ensure keepalive thread is stopped
    if (pImpl && pImpl->keepaliveThread.joinable()) {
        pImpl->keepaliveStop.store(true);
        try { pImpl->keepaliveThread.join(); } catch (...) {}
    }
}

std::future<void> Server::Start(std::unique_ptr<ITransport> transport) {
    FUNC_SCOPE();
    return pImpl->coStart(std::move(transport)).toFuture();
}

std::future<void> Server::Stop() {
    FUNC_SCOPE();
    return pImpl->coStop().toFuture();
}

bool Server::IsRunning() const {
    FUNC_SCOPE();
    bool val = pImpl->initialized.load();
    if (pImpl->transport) {
        val = val && pImpl->transport->IsConnected();
    }
    return val;
}

std::future<void> Server::HandleInitialize(
    const Implementation& clientInfo,
    const ClientCapabilities& capabilities) {
    FUNC_SCOPE();
    return pImpl->coHandleInitialize(clientInfo, capabilities).toFuture();
}

void Server::RegisterTool(const std::string& name, ToolHandler handler) {
    FUNC_SCOPE();
    LOG_DEBUG("Registering tool: {}", name);
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    pImpl->toolHandlers[name] = std::move(handler);
    // Default metadata if not already provided via overload
    if (pImpl->toolMetadata.find(name) == pImpl->toolMetadata.end()) {
        JSONValue::Object emptySchema;
        emptySchema["type"] = std::make_shared<JSONValue>(std::string("object"));
        emptySchema["properties"] = std::make_shared<JSONValue>(JSONValue::Object{});
        pImpl->toolMetadata[name] = Tool{name, std::string("Tool: ") + name, JSONValue{emptySchema}};
    }
    if (pImpl->transport && pImpl->transport->IsConnected()) {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::ToolListChanged;
        n->params = JSONValue{JSONValue::Object{}};
        (void)pImpl->transport->SendNotification(std::move(n));
    }
}

void Server::RegisterTool(const Tool& tool, ToolHandler handler) {
    FUNC_SCOPE();
    LOG_DEBUG("Registering tool: {}", tool.name);
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    pImpl->toolHandlers[tool.name] = std::move(handler);
    pImpl->toolMetadata[tool.name] = tool;
    if (pImpl->transport && pImpl->transport->IsConnected()) {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::ToolListChanged;
        n->params = JSONValue{JSONValue::Object{}};
        (void)pImpl->transport->SendNotification(std::move(n));
    }
}

void Server::UnregisterTool(const std::string& name) {
    FUNC_SCOPE();
    LOG_DEBUG("Unregistering tool: {}", name);
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    pImpl->toolHandlers.erase(name);
    pImpl->toolMetadata.erase(name);
    if (pImpl->transport && pImpl->transport->IsConnected()) {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::ToolListChanged;
        n->params = JSONValue{JSONValue::Object{}};
        (void)pImpl->transport->SendNotification(std::move(n));
    }
}

void Server::RegisterResource(const std::string& uri, ResourceHandler handler) {
    FUNC_SCOPE();
    LOG_DEBUG("Registering resource: {}", uri);
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    pImpl->resourceHandlers[uri] = std::move(handler);
    pImpl->resourceUris.push_back(uri);
    if (pImpl->transport && pImpl->transport->IsConnected()) {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::ResourceListChanged;
        n->params = JSONValue{JSONValue::Object{}};
        (void)pImpl->transport->SendNotification(std::move(n));
    }
}

void Server::UnregisterResource(const std::string& uri) {
    FUNC_SCOPE();
    LOG_DEBUG("Unregistering resource: {}", uri);
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    pImpl->resourceHandlers.erase(uri);
    pImpl->resourceUris.erase(
        std::remove(pImpl->resourceUris.begin(), pImpl->resourceUris.end(), uri),
        pImpl->resourceUris.end());
    if (pImpl->transport && pImpl->transport->IsConnected()) {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::ResourceListChanged;
        n->params = JSONValue{JSONValue::Object{}};
        (void)pImpl->transport->SendNotification(std::move(n));
    }
}

void Server::RegisterPrompt(const std::string& name, PromptHandler handler) {
    FUNC_SCOPE();
    LOG_DEBUG("Registering prompt: {}", name);
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    pImpl->promptHandlers[name] = std::move(handler);
    if (pImpl->transport && pImpl->transport->IsConnected()) {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::PromptListChanged;
        n->params = JSONValue{JSONValue::Object{}};
        (void)pImpl->transport->SendNotification(std::move(n));
    }
}

void Server::UnregisterPrompt(const std::string& name) {
    FUNC_SCOPE();
    LOG_DEBUG("Unregistering prompt: {}", name);
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    pImpl->promptHandlers.erase(name);
    if (pImpl->transport && pImpl->transport->IsConnected()) {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::PromptListChanged;
        n->params = JSONValue{JSONValue::Object{}};
        (void)pImpl->transport->SendNotification(std::move(n));
    }
}

std::future<void> Server::NotifyResourcesListChanged() {
    FUNC_SCOPE();
    return pImpl->coNotifyResourcesListChanged().toFuture();
}

std::future<void> Server::NotifyResourceUpdated(const std::string& uri) {
    FUNC_SCOPE();
    return pImpl->coNotifyResourceUpdated(uri).toFuture();
}

std::future<void> Server::NotifyToolsListChanged() {
    FUNC_SCOPE();
    return pImpl->coNotifyToolsListChanged().toFuture();
}

std::future<void> Server::NotifyPromptsListChanged() {
    FUNC_SCOPE();
    return pImpl->coNotifyPromptsListChanged().toFuture();
}

std::future<void> Server::SendNotification(const std::string& method, const JSONValue& params) {
    FUNC_SCOPE();
    return pImpl->coSendNotification(method, params).toFuture();
}

std::future<void> Server::LogToClient(const std::string& level, const std::string& message, const std::optional<JSONValue>& data) {
    FUNC_SCOPE();
    return pImpl->coLogToClient(level, message, data).toFuture();
}

std::future<void> Server::SendProgress(const std::string& token, double progress, const std::string& message) {
    FUNC_SCOPE();
    return pImpl->coSendProgress(token, progress, message).toFuture();
}

std::vector<Tool> Server::ListTools() {
    FUNC_SCOPE();
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    // Build from metadata to include both sync and async registered tools
    std::vector<Tool> out;
    out.reserve(pImpl->toolMetadata.size());
    for (const auto& [name, meta] : pImpl->toolMetadata) {
        out.push_back(meta);
    }
    return out;
}

std::future<JSONValue> Server::CallTool(const std::string& name, const JSONValue& arguments) {
    FUNC_SCOPE();
    return pImpl->coCallTool(name, arguments).toFuture();
}

std::vector<Resource> Server::ListResources() {
    FUNC_SCOPE();
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    std::vector<Resource> resources;
    for (const auto& uri : pImpl->resourceUris) {
        resources.emplace_back(uri, "Resource: " + uri);
    }
    return resources;
}

std::future<JSONValue> Server::ReadResource(const std::string& uri) {
    FUNC_SCOPE();
    return pImpl->coReadResource(uri).toFuture();
}

std::vector<Prompt> Server::ListPrompts() {
    FUNC_SCOPE();
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    std::vector<Prompt> prompts;
    for (const auto& [name, handler] : pImpl->promptHandlers) {
        prompts.emplace_back(name, "Prompt: " + name);
    }
    return prompts;
}

std::future<JSONValue> Server::GetPrompt(const std::string& name, const JSONValue& arguments) {
    FUNC_SCOPE();
    return pImpl->coGetPrompt(name, arguments).toFuture();
}

void Server::SetSamplingHandler(SamplingHandler handler) {
    FUNC_SCOPE();
    pImpl->samplingHandler = std::move(handler);
    // Advertise sampling capability now that a handler is available
    pImpl->capabilities.sampling = SamplingCapability{};
}

void Server::SetKeepaliveIntervalMs(const std::optional<int>& intervalMs) {
    FUNC_SCOPE();
    int ms = intervalMs.has_value() ? intervalMs.value() : -1;
    if (ms <= 0) {
        // Disable keepalive
        pImpl->keepaliveIntervalMs.store(-1);
        // Remove experimental keepalive advertisement
        pImpl->capabilities.experimental.erase("keepalive");
        if (pImpl->keepaliveThread.joinable()) {
            pImpl->keepaliveStop.store(true);
            try { pImpl->keepaliveThread.join(); } catch (...) {}
            pImpl->keepaliveStop.store(false);
        }
        return;
    }

    // Advertise keepalive capability (experimental)
    JSONValue::Object kv;
    kv["enabled"] = std::make_shared<JSONValue>(true);
    kv["intervalMs"] = std::make_shared<JSONValue>(static_cast<int64_t>(ms));
    kv["failureThreshold"] = std::make_shared<JSONValue>(static_cast<int64_t>(pImpl->keepaliveFailureThreshold.load()));
    pImpl->capabilities.experimental["keepalive"] = JSONValue{kv};

    pImpl->keepaliveIntervalMs.store(ms);

    // Start background loop if not running
    if (!pImpl->keepaliveThread.joinable()) {
        pImpl->keepaliveStop.store(false);
        pImpl->keepaliveThread = std::thread([this]() {
            while (!pImpl->keepaliveStop.load()) {
                int delay = pImpl->keepaliveIntervalMs.load();
                if (delay <= 0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                if (pImpl->keepaliveStop.load()) break;
                if (!pImpl->transport || !pImpl->transport->IsConnected()) continue;
                try {
                    pImpl->keepaliveSending.store(true);
                    pImpl->keepaliveSendFailed.store(false);
                    auto n = std::make_unique<JSONRPCNotification>();
                    n->method = Methods::Keepalive;
                    n->params = JSONValue{JSONValue::Object{}};
                    (void)pImpl->transport->SendNotification(std::move(n));
                } catch (...) {
                    pImpl->keepaliveSendFailed.store(true);
                }
                pImpl->keepaliveSending.store(false);
                if (pImpl->keepaliveSendFailed.load()) {
                    unsigned int fails = 1u + pImpl->keepaliveConsecutiveFailures.load();
                    pImpl->keepaliveConsecutiveFailures.store(fails);
                    if (fails >= pImpl->keepaliveFailureThreshold.load()) {
                        LOG_ERROR("Keepalive failure threshold reached ({}); closing transport", fails);
                        try { (void)pImpl->transport->Close(); } catch (...) {}
                        if (pImpl->errorCallback) {
                            try { pImpl->errorCallback("Keepalive failure threshold reached; closing transport"); }
                            catch (...) {}
                        }
                        break;
                    }
                } else {
                    pImpl->keepaliveConsecutiveFailures.store(0u);
                }
            }
        });
    }
}

void Server::SetLoggingRateLimitPerSecond(const std::optional<unsigned int>& perSecond) {
    FUNC_SCOPE();
    unsigned int n = perSecond.has_value() ? perSecond.value() : 0u;
    if (n == 0u) {
        pImpl->logRateLimitPerSec.store(0u);
        // Remove experimental advertisement
        pImpl->capabilities.experimental.erase("loggingRateLimit");
        // Reset window state
        {
            std::lock_guard<std::mutex> lk(pImpl->logRateMutex);
            pImpl->logWindowStart = std::chrono::steady_clock::now();
            pImpl->logWindowCount = 0u;
        }
        return;
    }
    pImpl->logRateLimitPerSec.store(n);
    // Advertise experimental logging rate limit
    JSONValue::Object lv;
    lv["enabled"] = std::make_shared<JSONValue>(true);
    lv["perSecond"] = std::make_shared<JSONValue>(static_cast<int64_t>(n));
    pImpl->capabilities.experimental["loggingRateLimit"] = JSONValue{lv};
    // Reset window state
    {
        std::lock_guard<std::mutex> lk(pImpl->logRateMutex);
        pImpl->logWindowStart = std::chrono::steady_clock::now();
        pImpl->logWindowCount = 0u;
    }
}

std::future<JSONValue> Server::RequestCreateMessage(const CreateMessageParams& params) {
    FUNC_SCOPE();
    return pImpl->coRequestCreateMessage(params).toFuture();
}

void Server::RegisterResourceTemplate(const ResourceTemplate& resourceTemplate) {
    FUNC_SCOPE();
    LOG_DEBUG("Registering resource template: {}", resourceTemplate.uriTemplate);
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    auto& v = pImpl->resourceTemplates;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const ResourceTemplate& rt){ return rt.uriTemplate == resourceTemplate.uriTemplate; }), v.end());
    v.push_back(resourceTemplate);
    if (pImpl->transport && pImpl->transport->IsConnected()) {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::ResourceListChanged;
        n->params = JSONValue{JSONValue::Object{}};
        (void)pImpl->transport->SendNotification(std::move(n));
    }
}

void Server::UnregisterResourceTemplate(const std::string& uriTemplate) {
    FUNC_SCOPE();
    LOG_DEBUG("Unregistering resource template: {}", uriTemplate);
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    auto& v = pImpl->resourceTemplates;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const ResourceTemplate& rt){ return rt.uriTemplate == uriTemplate; }), v.end());
    if (pImpl->transport && pImpl->transport->IsConnected()) {
        auto n = std::make_unique<JSONRPCNotification>();
        n->method = Methods::ResourceListChanged;
        n->params = JSONValue{JSONValue::Object{}};
        (void)pImpl->transport->SendNotification(std::move(n));
    }
}

std::vector<ResourceTemplate> Server::ListResourceTemplates() {
    FUNC_SCOPE();
    std::lock_guard<std::mutex> lock(pImpl->registryMutex);
    std::vector<ResourceTemplate> out = pImpl->resourceTemplates;
    return out;
}

void Server::SetErrorHandler(ErrorHandler handler) {
    FUNC_SCOPE();
    pImpl->errorCallback = std::move(handler);
}

ServerCapabilities Server::GetCapabilities() const {
    FUNC_SCOPE();
    auto caps = pImpl->capabilities;
    return caps;
}

void Server::SetCapabilities(const ServerCapabilities& capabilities) {
    FUNC_SCOPE();
    pImpl->capabilities = capabilities;
}

// Server factory implementation
std::unique_ptr<IServer> ServerFactory::CreateServer(const Implementation& serverInfo) {
    FUNC_SCOPE();
    auto svr = std::make_unique<Server>(serverInfo.name + " v" + serverInfo.version);
    return svr;
}

} // namespace mcp
