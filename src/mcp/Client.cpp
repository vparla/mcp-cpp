//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: src/mcp/Client.cpp
// Purpose: MCP client implementation
//==========================================================================================================
#include <atomic>
#include <chrono>
#include <mutex>
#include <algorithm>
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
#include "src/mcp/MetadataSupport.h"
#include "src/mcp/TaskSupport.h"


namespace mcp {
namespace {

void PopulateToolFromListItem(const JSONValue::Object& item, Tool& tool) {
    auto nameIt = item.find("name");
    if (nameIt != item.end() && nameIt->second &&
        std::holds_alternative<std::string>(nameIt->second->value)) {
        tool.name = std::get<std::string>(nameIt->second->value);
    }
    auto titleIt = item.find("title");
    if (titleIt != item.end() && titleIt->second &&
        std::holds_alternative<std::string>(titleIt->second->value)) {
        tool.title = std::get<std::string>(titleIt->second->value);
    }
    auto descIt = item.find("description");
    if (descIt != item.end() && descIt->second &&
        std::holds_alternative<std::string>(descIt->second->value)) {
        tool.description = std::get<std::string>(descIt->second->value);
    }
    tool.icons = metadata::ParseIconsField(item, "icons");
    auto schemaIt = item.find("inputSchema");
    if (schemaIt != item.end() && schemaIt->second) {
        tool.inputSchema = *schemaIt->second;
    }
    auto outputSchemaIt = item.find("outputSchema");
    if (outputSchemaIt != item.end() && outputSchemaIt->second) {
        tool.outputSchema = *outputSchemaIt->second;
    }
    auto annotationsIt = item.find("annotations");
    if (annotationsIt != item.end() && annotationsIt->second) {
        tool.annotations = *annotationsIt->second;
    }
    auto executionIt = item.find("execution");
    if (executionIt != item.end() && executionIt->second) {
        tool.execution = *executionIt->second;
    }
    auto metaIt = item.find("_meta");
    if (metaIt != item.end() && metaIt->second) {
        tool.meta = *metaIt->second;
    }
}

void PopulateResourceFromListItem(const JSONValue::Object& item, Resource& resource) {
    auto uriIt = item.find("uri");
    if (uriIt != item.end() && uriIt->second &&
        std::holds_alternative<std::string>(uriIt->second->value)) {
        resource.uri = std::get<std::string>(uriIt->second->value);
    }
    auto nameIt = item.find("name");
    if (nameIt != item.end() && nameIt->second &&
        std::holds_alternative<std::string>(nameIt->second->value)) {
        resource.name = std::get<std::string>(nameIt->second->value);
    }
    auto titleIt = item.find("title");
    if (titleIt != item.end() && titleIt->second &&
        std::holds_alternative<std::string>(titleIt->second->value)) {
        resource.title = std::get<std::string>(titleIt->second->value);
    }
    auto descIt = item.find("description");
    if (descIt != item.end() && descIt->second &&
        std::holds_alternative<std::string>(descIt->second->value)) {
        resource.description = std::get<std::string>(descIt->second->value);
    }
    auto mimeIt = item.find("mimeType");
    if (mimeIt != item.end() && mimeIt->second &&
        std::holds_alternative<std::string>(mimeIt->second->value)) {
        resource.mimeType = std::get<std::string>(mimeIt->second->value);
    }
    auto sizeIt = item.find("size");
    if (sizeIt != item.end() && sizeIt->second &&
        std::holds_alternative<int64_t>(sizeIt->second->value)) {
        resource.size = std::get<int64_t>(sizeIt->second->value);
    }
    auto annotationsIt = item.find("annotations");
    if (annotationsIt != item.end() && annotationsIt->second) {
        resource.annotations = *annotationsIt->second;
    }
    auto metaIt = item.find("_meta");
    if (metaIt != item.end() && metaIt->second) {
        resource.meta = *metaIt->second;
    }
    resource.icons = metadata::ParseIconsField(item, "icons");
}

void PopulateResourceTemplateFromListItem(const JSONValue::Object& item,
                                          ResourceTemplate& resourceTemplate) {
    auto templateIt = item.find("uriTemplate");
    if (templateIt != item.end() && templateIt->second &&
        std::holds_alternative<std::string>(templateIt->second->value)) {
        resourceTemplate.uriTemplate = std::get<std::string>(templateIt->second->value);
    }
    auto nameIt = item.find("name");
    if (nameIt != item.end() && nameIt->second &&
        std::holds_alternative<std::string>(nameIt->second->value)) {
        resourceTemplate.name = std::get<std::string>(nameIt->second->value);
    }
    auto titleIt = item.find("title");
    if (titleIt != item.end() && titleIt->second &&
        std::holds_alternative<std::string>(titleIt->second->value)) {
        resourceTemplate.title = std::get<std::string>(titleIt->second->value);
    }
    auto descIt = item.find("description");
    if (descIt != item.end() && descIt->second &&
        std::holds_alternative<std::string>(descIt->second->value)) {
        resourceTemplate.description = std::get<std::string>(descIt->second->value);
    }
    auto mimeIt = item.find("mimeType");
    if (mimeIt != item.end() && mimeIt->second &&
        std::holds_alternative<std::string>(mimeIt->second->value)) {
        resourceTemplate.mimeType = std::get<std::string>(mimeIt->second->value);
    }
    auto annotationsIt = item.find("annotations");
    if (annotationsIt != item.end() && annotationsIt->second) {
        resourceTemplate.annotations = *annotationsIt->second;
    }
    auto metaIt = item.find("_meta");
    if (metaIt != item.end() && metaIt->second) {
        resourceTemplate.meta = *metaIt->second;
    }
    resourceTemplate.icons = metadata::ParseIconsField(item, "icons");
}

void PopulatePromptFromListItem(const JSONValue::Object& item, Prompt& prompt) {
    auto nameIt = item.find("name");
    if (nameIt != item.end() && nameIt->second &&
        std::holds_alternative<std::string>(nameIt->second->value)) {
        prompt.name = std::get<std::string>(nameIt->second->value);
    }
    auto titleIt = item.find("title");
    if (titleIt != item.end() && titleIt->second &&
        std::holds_alternative<std::string>(titleIt->second->value)) {
        prompt.title = std::get<std::string>(titleIt->second->value);
    }
    auto descIt = item.find("description");
    if (descIt != item.end() && descIt->second &&
        std::holds_alternative<std::string>(descIt->second->value)) {
        prompt.description = std::get<std::string>(descIt->second->value);
    }
    auto argsIt = item.find("arguments");
    if (argsIt != item.end() && argsIt->second) {
        prompt.arguments = *argsIt->second;
    }
    auto metaIt = item.find("_meta");
    if (metaIt != item.end() && metaIt->second) {
        prompt.meta = *metaIt->second;
    }
    prompt.icons = metadata::ParseIconsField(item, "icons");
}

}  // namespace

// Client implementation
class Client::Impl {
private:
    friend class Client; // Allow outer Client to invoke private coroutine helpers
    std::unique_ptr<ITransport> transport;
    ClientCapabilities capabilities;
    ServerCapabilities serverCapabilities;
    Implementation clientInfo;
    std::atomic<bool> connected{false};
    std::unordered_map<std::string, IClient::NotificationHandler> notificationHandlers;
    IClient::ProgressHandler progressHandler;
    IClient::TaskStatusHandler taskStatusHandler;
    IClient::ErrorHandler errorHandler;
    IClient::RootsListHandler rootsListHandler;
    IClient::SamplingHandler samplingHandler;
    IClient::SamplingHandlerCancelable samplingHandlerCancelable;
    IClient::ElicitationHandler elicitationHandler;
    validation::ValidationMode validationMode{validation::ValidationMode::Off};

    // Listings cache (optional)
    std::optional<uint64_t> listingsCacheTtlMs; // milliseconds; disabled when not set or == 0
    std::mutex cacheMutex;
    struct ToolsCache { std::vector<Tool> data; std::chrono::steady_clock::time_point ts; bool set{false}; } toolsCache;
    struct ResourcesCache { std::vector<Resource> data; std::chrono::steady_clock::time_point ts; bool set{false}; } resourcesCache;
    struct TemplatesCache { std::vector<ResourceTemplate> data; std::chrono::steady_clock::time_point ts; bool set{false}; } templatesCache;
    struct PromptsCache { std::vector<Prompt> data; std::chrono::steady_clock::time_point ts; bool set{false}; } promptsCache;

    // Cancellation support for server->client requests (e.g., sampling/createMessage)
    struct CancellationToken { std::atomic<bool> cancelled{false}; };
    std::mutex cancelMutex;
    std::unordered_map<std::string, std::shared_ptr<CancellationToken>> cancelMap;
    std::unordered_map<std::string, std::vector<std::shared_ptr<std::stop_source>>> stopSources;
    tasks::TaskStore receivedTasks;
    tasks::BackgroundTaskGroup taskWorkers;
    std::mutex outboundTasksMutex;
    std::unordered_map<std::string, tasks::TaskKind> outboundTaskKinds;

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

    static std::string parseIdFromParams(const JSONValue& params) {
        std::string idStr;
        if (std::holds_alternative<JSONValue::Object>(params.value)) {
            const auto& o = std::get<JSONValue::Object>(params.value);
            auto it = o.find("id");
            if (it != o.end() && it->second) {
                if (std::holds_alternative<std::string>(it->second->value)) {
                    idStr = std::get<std::string>(it->second->value);
                } else if (std::holds_alternative<int64_t>(it->second->value)) {
                    idStr = std::to_string(std::get<int64_t>(it->second->value));
                }
            }
        }
        return idStr;
    }

    std::shared_ptr<CancellationToken> registerCancelToken(const std::string& idStr) {
        if (idStr.empty()) {
            return std::make_shared<CancellationToken>();
        }
        std::lock_guard<std::mutex> lk(cancelMutex);
        auto it = cancelMap.find(idStr);
        if (it != cancelMap.end()) {
            return it->second;
        }
        auto tok = std::make_shared<CancellationToken>();
        cancelMap[idStr] = tok;
        return tok;
    }
    void unregisterCancelToken(const std::string& idStr) {
        if (idStr.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lk(cancelMutex);
        cancelMap.erase(idStr);
        stopSources.erase(idStr);
    }
    void cancelById(const std::string& idStr) {
        std::lock_guard<std::mutex> lk(cancelMutex);
        auto it = cancelMap.find(idStr);
        if (it == cancelMap.end() || !it->second) {
            auto tok = std::make_shared<CancellationToken>();
            tok->cancelled.store(true);
            cancelMap[idStr] = tok;
        } else {
            it->second->cancelled.store(true);
        }
        auto itS = stopSources.find(idStr);
        if (itS != stopSources.end()) {
            for (auto& src : itS->second) {
                if (src) {
                    try {
                        src->request_stop();
                    } catch (...) {
                    }
                }
            }
        }
    }
    std::shared_ptr<std::stop_source> registerStopSource(const std::string& idStr) {
        auto src = std::make_shared<std::stop_source>();
        std::lock_guard<std::mutex> lk(cancelMutex);
        stopSources[idStr].push_back(src);
        auto it = cancelMap.find(idStr);
        if (it != cancelMap.end() && it->second && it->second->cancelled.load()) {
            try {
                src->request_stop();
            } catch (...) {}
        }
        return src;
    }
    void unregisterStopSource(const std::string& idStr, const std::shared_ptr<std::stop_source>& src) {
        std::lock_guard<std::mutex> lk(cancelMutex);
        auto it = stopSources.find(idStr);
        if (it == stopSources.end()) {
            return;
        }
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const std::shared_ptr<std::stop_source>& p){ return p.get() == src.get(); }), vec.end());
        if (vec.empty()) {
            stopSources.erase(it);
        }
    }

    explicit Impl(const Implementation& info)
        : clientInfo(info) {
        // Set default client capabilities
        capabilities.experimental = {};
        capabilities.sampling = {};
        receivedTasks.SetStatusCallback([this](const Task& task) {
            this->sendTaskStatusNotification(task);
        });
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

        if (caps.elicitation.has_value()) {
            JSONValue::Object elicitationObj;
            JSONValue::Array modes;
            for (const auto& mode : caps.elicitation->modes) {
                modes.push_back(std::make_shared<JSONValue>(mode));
            }
            if (!modes.empty()) {
                elicitationObj["modes"] = std::make_shared<JSONValue>(modes);
            }
            obj["elicitation"] = std::make_shared<JSONValue>(elicitationObj);
        }

        if (caps.tasks.has_value()) {
            JSONValue::Object tasksObj;
            if (caps.tasks->list) {
                tasksObj["list"] = std::make_shared<JSONValue>(JSONValue::Object{});
            }
            if (caps.tasks->cancel) {
                tasksObj["cancel"] = std::make_shared<JSONValue>(JSONValue::Object{});
            }
            JSONValue::Object requestsObj;
            if (caps.tasks->requests.createMessage) {
                JSONValue::Object samplingObj;
                samplingObj["createMessage"] = std::make_shared<JSONValue>(JSONValue::Object{});
                requestsObj["sampling"] = std::make_shared<JSONValue>(samplingObj);
            }
            if (caps.tasks->requests.elicitationCreate) {
                JSONValue::Object elicitationReqObj;
                elicitationReqObj["create"] = std::make_shared<JSONValue>(JSONValue::Object{});
                requestsObj["elicitation"] = std::make_shared<JSONValue>(elicitationReqObj);
            }
            if (!requestsObj.empty()) {
                tasksObj["requests"] = std::make_shared<JSONValue>(requestsObj);
            }
            obj["tasks"] = std::make_shared<JSONValue>(tasksObj);
        }

        if (caps.roots.has_value()) {
            JSONValue::Object rootsObj;
            rootsObj["listChanged"] = std::make_shared<JSONValue>(caps.roots->listChanged);
            obj["roots"] = std::make_shared<JSONValue>(rootsObj);
        }
        
        return JSONValue{obj};
    }

    void parseServerCapabilities(const JSONValue& result) {
        if (std::holds_alternative<JSONValue::Object>(result.value)) {
            const auto& obj = std::get<JSONValue::Object>(result.value);
            
            auto capsIt = obj.find("capabilities");
            if (capsIt != obj.end() && std::holds_alternative<JSONValue::Object>(capsIt->second->value)) {
                this->serverCapabilities = ServerCapabilities{};
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

                auto completionsIt = capsObj.find("completions");
                if (completionsIt != capsObj.end()) {
                    serverCapabilities.completions = CompletionsCapability{};
                }

                auto tasksIt = capsObj.find("tasks");
                if (tasksIt != capsObj.end() && tasksIt->second &&
                    std::holds_alternative<JSONValue::Object>(tasksIt->second->value)) {
                    ServerTasksCapability taskCaps{};
                    const auto& tasksObj = std::get<JSONValue::Object>(tasksIt->second->value);
                    taskCaps.list = tasksObj.find("list") != tasksObj.end();
                    taskCaps.cancel = tasksObj.find("cancel") != tasksObj.end();
                    auto requestsIt = tasksObj.find("requests");
                    if (requestsIt != tasksObj.end() && requestsIt->second &&
                        std::holds_alternative<JSONValue::Object>(requestsIt->second->value)) {
                        const auto& requestsObj = std::get<JSONValue::Object>(requestsIt->second->value);
                        auto toolsIt = requestsObj.find("tools");
                        if (toolsIt != requestsObj.end() && toolsIt->second &&
                            std::holds_alternative<JSONValue::Object>(toolsIt->second->value)) {
                            const auto& toolsObj = std::get<JSONValue::Object>(toolsIt->second->value);
                            taskCaps.requests.toolCall = toolsObj.find("call") != toolsObj.end();
                        }
                    }
                    serverCapabilities.tasks = taskCaps;
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
    mcp::async::Task<CreateTaskResult> coCallToolTask(const std::string& name,
                                                      const JSONValue& arguments,
                                                      const TaskMetadata& task);
    mcp::async::Task<Task> coGetTask(const std::string& taskId);
    mcp::async::Task<std::vector<Task>> coListTasks();
    mcp::async::Task<TasksListResult> coListTasksPaged(const std::optional<std::string>& cursor,
                                                       const std::optional<int>& limit);
    mcp::async::Task<JSONValue> coGetTaskResult(const std::string& taskId);
    mcp::async::Task<Task> coCancelTask(const std::string& taskId);
    mcp::async::Task<CompletionResult> coComplete(const CompleteParams& params);
    mcp::async::Task<void> coPing();
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
    mcp::async::Task<void> coNotifyRootsListChanged();

    // Helpers to keep functions short and readable
    void onNotification(std::unique_ptr<JSONRPCNotification> n);
    void handleProgressNotification(const JSONValue::Object& o);
    void invalidateCachesForListChanged(const std::string& method);
    std::unique_ptr<JSONRPCResponse> onRequest(const JSONRPCRequest& req);
    void sendTaskStatusNotification(const Task& task);
    void rememberOutboundTask(const std::string& taskId, tasks::TaskKind kind);
    std::optional<tasks::TaskKind> outboundTaskKindFor(const std::string& taskId);
    static std::string errorMessageFromPayload(const JSONValue& payload);
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

void Client::Impl::sendTaskStatusNotification(const Task& task) {
    if (!this->transport || !this->transport->IsConnected()) {
        return;
    }
    try {
        auto notification = std::make_unique<JSONRPCNotification>();
        notification->method = Methods::TaskStatus;
        notification->params = tasks::SerializeTask(task);
        (void)this->transport->SendNotification(std::move(notification));
    } catch (const std::exception& e) {
        LOG_ERROR("Task status notification exception: {}", e.what());
    }
}

void Client::Impl::rememberOutboundTask(const std::string& taskId, tasks::TaskKind kind) {
    if (taskId.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lk(this->outboundTasksMutex);
    this->outboundTaskKinds[taskId] = kind;
}

std::optional<tasks::TaskKind> Client::Impl::outboundTaskKindFor(const std::string& taskId) {
    std::lock_guard<std::mutex> lk(this->outboundTasksMutex);
    auto it = this->outboundTaskKinds.find(taskId);
    if (it == this->outboundTaskKinds.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string Client::Impl::errorMessageFromPayload(const JSONValue& payload) {
    if (!std::holds_alternative<JSONValue::Object>(payload.value)) {
        return "request failed";
    }
    const auto& obj = std::get<JSONValue::Object>(payload.value);
    auto it = obj.find("message");
    if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
        return std::get<std::string>(it->second->value);
    }
    return "request failed";
}

void Client::Impl::onNotification(std::unique_ptr<JSONRPCNotification> n) {
    if (!n) { return; }
    try {
        if (n->method == Methods::Progress && this->progressHandler && n->params.has_value()) {
            if (std::holds_alternative<JSONValue::Object>(n->params->value)) {
                const auto& o = std::get<JSONValue::Object>(n->params->value);
                this->handleProgressNotification(o);
            }
        } else if (n->method == Methods::Cancelled) {
            // Server-initiated cancellation for a pending request id
            if (n->params.has_value()) {
                std::string idStr = parseIdFromParams(n->params.value());
                if (!idStr.empty()) {
                    this->cancelById(idStr);
                }
            }
        } else if (n->method == Methods::TaskStatus && this->taskStatusHandler && n->params.has_value()) {
            if (this->validationMode == validation::ValidationMode::Strict &&
                !validation::validateTaskStatusNotificationParamsJson(n->params.value())) {
                LOG_WARN("Dropping invalid task status notification under Strict mode");
                return;
            }
            this->taskStatusHandler(tasks::ParseTask(n->params.value()));
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
        if (req.method == Methods::Ping) {
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = JSONValue{JSONValue::Object{}};
            return resp;
        } else if (req.method == Methods::ListRoots) {
            if (!this->rootsListHandler) {
                errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "No roots/list handler registered";
                return errors::makeErrorResponse(req.id, e);
            }

            RootsListResult rootsResult = this->rootsListHandler().get();
            JSONValue::Object resultObj;
            JSONValue::Array rootsArray;
            rootsArray.reserve(rootsResult.roots.size());
            for (const auto& root : rootsResult.roots) {
                JSONValue::Object rootObj;
                rootObj["uri"] = std::make_shared<JSONValue>(root.uri);
                if (root.name.has_value()) {
                    rootObj["name"] = std::make_shared<JSONValue>(root.name.value());
                }
                if (root.meta.has_value()) {
                    rootObj["_meta"] = std::make_shared<JSONValue>(root.meta.value());
                }
                rootsArray.push_back(std::make_shared<JSONValue>(rootObj));
            }
            resultObj["roots"] = std::make_shared<JSONValue>(rootsArray);

            JSONValue result{resultObj};
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validateRootsListResultJson(result)) {
                    errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid roots/list result shape";
                    return errors::makeErrorResponse(req.id, e);
                }
            }

            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = result;
            return resp;
        } else if (req.method == Methods::GetTask) {
            if (!this->capabilities.tasks.has_value()) {
                errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "Tasks not supported";
                return errors::makeErrorResponse(req.id, e);
            }
            std::string taskId;
            if (!tasks::TryParseTaskId(req.params, taskId)) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid taskId";
                return errors::makeErrorResponse(req.id, e);
            }
            auto task = this->receivedTasks.GetTask(taskId);
            if (!task.has_value()) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Unknown task";
                return errors::makeErrorResponse(req.id, e);
            }
            JSONValue result = tasks::SerializeTask(task.value());
            if (this->validationMode == validation::ValidationMode::Strict &&
                !validation::validateTaskJson(result)) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid task result shape";
                return errors::makeErrorResponse(req.id, e);
            }
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = result;
            return resp;
        } else if (req.method == Methods::ListTasks) {
            if (!this->capabilities.tasks.has_value() || !this->capabilities.tasks->list) {
                errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "Task listing not supported";
                return errors::makeErrorResponse(req.id, e);
            }
            size_t start = 0;
            std::optional<size_t> limitOpt;
            if (req.params.has_value() && std::holds_alternative<JSONValue::Object>(req.params->value)) {
                const auto& obj = std::get<JSONValue::Object>(req.params->value);
                auto cursorIt = obj.find("cursor");
                if (cursorIt != obj.end() && cursorIt->second) {
                    if (!std::holds_alternative<std::string>(cursorIt->second->value)) {
                        errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid cursor";
                        return errors::makeErrorResponse(req.id, e);
                    }
                    try {
                        start = static_cast<size_t>(std::stoll(std::get<std::string>(cursorIt->second->value)));
                    } catch (...) {
                        errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid cursor";
                        return errors::makeErrorResponse(req.id, e);
                    }
                }
                auto limitIt = obj.find("limit");
                if (limitIt != obj.end() && limitIt->second) {
                    if (!std::holds_alternative<int64_t>(limitIt->second->value) ||
                        std::get<int64_t>(limitIt->second->value) <= 0) {
                        errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid limit";
                        return errors::makeErrorResponse(req.id, e);
                    }
                    limitOpt = static_cast<size_t>(std::get<int64_t>(limitIt->second->value));
                }
            }
            JSONValue result = tasks::SerializeTasksListResult(this->receivedTasks.ListTasks(start, limitOpt));
            if (this->validationMode == validation::ValidationMode::Strict &&
                !validation::validateTasksListResultJson(result)) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid tasks/list result shape";
                return errors::makeErrorResponse(req.id, e);
            }
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = result;
            return resp;
        } else if (req.method == Methods::GetTaskResult) {
            if (!this->capabilities.tasks.has_value()) {
                errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "Tasks not supported";
                return errors::makeErrorResponse(req.id, e);
            }
            std::string taskId;
            if (!tasks::TryParseTaskId(req.params, taskId)) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid taskId";
                return errors::makeErrorResponse(req.id, e);
            }
            auto kind = this->receivedTasks.GetKind(taskId);
            auto payload = this->receivedTasks.WaitForTerminalPayload(taskId);
            if (!payload.has_value()) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Unknown task";
                return errors::makeErrorResponse(req.id, e);
            }
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            if (payload->first) {
                resp->error = tasks::AttachRelatedTaskMetaToError(payload->second, taskId);
                return resp;
            }
            JSONValue result = tasks::AttachRelatedTaskMetaToResult(payload->second, taskId);
            if (this->validationMode == validation::ValidationMode::Strict && kind.has_value()) {
                const bool valid =
                    (*kind == tasks::TaskKind::CreateMessage && validation::validateCreateMessageResultJson(result)) ||
                    (*kind == tasks::TaskKind::Elicitation && validation::validateElicitationResultJson(result));
                if (!valid) {
                    errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid task result shape";
                    return errors::makeErrorResponse(req.id, e);
                }
            }
            resp->result = result;
            return resp;
        } else if (req.method == Methods::CancelTask) {
            if (!this->capabilities.tasks.has_value() || !this->capabilities.tasks->cancel) {
                errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "Task cancellation not supported";
                return errors::makeErrorResponse(req.id, e);
            }
            std::string taskId;
            if (!tasks::TryParseTaskId(req.params, taskId)) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid taskId";
                return errors::makeErrorResponse(req.id, e);
            }
            const JSONValue errorPayload = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Cancelled");
            Task cancelledTask;
            if (!this->receivedTasks.CancelTask(taskId, errorPayload, &cancelledTask)) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Task is not cancellable";
                return errors::makeErrorResponse(req.id, e);
            }
            JSONValue result = tasks::SerializeTask(cancelledTask);
            if (this->validationMode == validation::ValidationMode::Strict &&
                !validation::validateTaskJson(result)) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid task result shape";
                return errors::makeErrorResponse(req.id, e);
            }
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = result;
            return resp;
        } else if (req.method == Methods::Elicit) {
            if (!this->elicitationHandler) {
                errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "No elicitation handler registered";
                return errors::makeErrorResponse(req.id, e);
            }

            JSONValue paramsVal = req.params.value_or(JSONValue{JSONValue::Object{}});
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validateElicitationRequestJson(paramsVal)) {
                    errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid elicitation/create params";
                    return errors::makeErrorResponse(req.id, e);
                }
            }

            ElicitationRequest request;
            if (std::holds_alternative<JSONValue::Object>(paramsVal.value)) {
                const auto& obj = std::get<JSONValue::Object>(paramsVal.value);
                auto it = obj.find("message");
                if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
                    request.message = std::get<std::string>(it->second->value);
                }
                it = obj.find("requestedSchema");
                if (it != obj.end() && it->second) {
                    request.requestedSchema = *it->second;
                }
                it = obj.find("title");
                if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
                    request.title = std::get<std::string>(it->second->value);
                }
                it = obj.find("mode");
                if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
                    request.mode = std::get<std::string>(it->second->value);
                }
                it = obj.find("url");
                if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
                    request.url = std::get<std::string>(it->second->value);
                }
                it = obj.find("elicitationId");
                if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
                    request.elicitationId = std::get<std::string>(it->second->value);
                }
                it = obj.find("metadata");
                if (it != obj.end() && it->second) {
                    request.metadata = *it->second;
                }
            }

            TaskMetadata taskMetadata;
            const bool isTaskRequest = tasks::TryParseTaskMetadata(req.params, taskMetadata);
            if (isTaskRequest) {
                if (!this->capabilities.tasks.has_value() || !this->capabilities.tasks->requests.elicitationCreate) {
                    errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "Task-augmented elicitation not supported";
                    return errors::makeErrorResponse(req.id, e);
                }
                CreateTaskResult created;
                created.task = this->receivedTasks.CreateTask(tasks::TaskKind::Elicitation, taskMetadata);
                const std::string taskId = created.task.taskId;
                this->taskWorkers.Add(std::async(std::launch::async, [this, taskId, request]() {
                    try {
                        ElicitationResult resultValue = this->elicitationHandler(request).get();
                        JSONValue::Object resultObj;
                        resultObj["action"] = std::make_shared<JSONValue>(resultValue.action);
                        if (resultValue.content.has_value()) {
                            resultObj["content"] = std::make_shared<JSONValue>(resultValue.content.value());
                        }
                        if (resultValue.elicitationId.has_value()) {
                            resultObj["elicitationId"] = std::make_shared<JSONValue>(resultValue.elicitationId.value());
                        }
                        JSONValue result{resultObj};
                        if (this->validationMode == validation::ValidationMode::Strict &&
                            !validation::validateElicitationResultJson(result)) {
                            const JSONValue errorPayload = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Invalid elicitation result shape");
                            (void)this->receivedTasks.CompleteTask(taskId, tasks::Status::Failed,
                                                                  std::string("Invalid elicitation result shape"),
                                                                  true, errorPayload, nullptr);
                            return;
                        }
                        (void)this->receivedTasks.CompleteTask(taskId, tasks::Status::Completed,
                                                              std::nullopt, false, result, nullptr);
                    } catch (const std::exception& e) {
                        const JSONValue errorPayload = CreateErrorObject(JSONRPCErrorCodes::InternalError, e.what());
                        (void)this->receivedTasks.CompleteTask(taskId, tasks::Status::Failed, std::string(e.what()),
                                                              true, errorPayload, nullptr);
                    }
                }));
                JSONValue result = tasks::SerializeCreateTaskResult(created);
                if (this->validationMode == validation::ValidationMode::Strict &&
                    !validation::validateCreateTaskResultJson(result)) {
                    errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid task creation result shape";
                    return errors::makeErrorResponse(req.id, e);
                }
                auto resp = std::make_unique<JSONRPCResponse>();
                resp->id = req.id;
                resp->result = result;
                return resp;
            }

            ElicitationResult resultValue = this->elicitationHandler(request).get();
            JSONValue::Object resultObj;
            resultObj["action"] = std::make_shared<JSONValue>(resultValue.action);
            if (resultValue.content.has_value()) {
                resultObj["content"] = std::make_shared<JSONValue>(resultValue.content.value());
            }
            if (resultValue.elicitationId.has_value()) {
                resultObj["elicitationId"] = std::make_shared<JSONValue>(resultValue.elicitationId.value());
            }
            JSONValue result{resultObj};
            if (this->validationMode == validation::ValidationMode::Strict) {
                if (!validation::validateElicitationResultJson(result)) {
                    errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid elicitation result shape";
                    return errors::makeErrorResponse(req.id, e);
                }
            }

            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = result;
            return resp;
        } else if (req.method == Methods::CreateMessage) {
            TaskMetadata taskMetadata;
            const bool isTaskRequest = tasks::TryParseTaskMetadata(req.params, taskMetadata);
            if (isTaskRequest) {
                if (!this->capabilities.tasks.has_value() || !this->capabilities.tasks->requests.createMessage) {
                    errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "Task-augmented sampling not supported";
                    return errors::makeErrorResponse(req.id, e);
                }
                if (!this->samplingHandler && !this->samplingHandlerCancelable) {
                    errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotAllowed; e.message = "No sampling handler registered";
                    return errors::makeErrorResponse(req.id, e);
                }
                JSONValue paramsVal = req.params.value_or(JSONValue{JSONValue::Object{}});
                if (this->validationMode == validation::ValidationMode::Strict &&
                    !validation::validateCreateMessageParamsJson(paramsVal)) {
                    this->logInvalidCreateMessageParamsContext(paramsVal);
                    errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid sampling/createMessage params";
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
                CreateTaskResult created;
                created.task = this->receivedTasks.CreateTask(tasks::TaskKind::CreateMessage, taskMetadata);
                const std::string taskId = created.task.taskId;
                auto stopSource = this->receivedTasks.GetStopSource(taskId);
                this->taskWorkers.Add(std::async(std::launch::async, [this, taskId, messages, modelPreferences, systemPrompt, includeContext, stopSource]() {
                    try {
                        std::future<JSONValue> future = this->samplingHandler
                            ? this->samplingHandler(messages, modelPreferences, systemPrompt, includeContext)
                            : this->samplingHandlerCancelable(messages, modelPreferences, systemPrompt, includeContext,
                                                              stopSource ? stopSource->get_token() : std::stop_token{});
                        JSONValue result = future.get();
                        if (this->validationMode == validation::ValidationMode::Strict &&
                            !validation::validateCreateMessageResultJson(result)) {
                            this->logInvalidCreateMessageResultContext(result);
                            const JSONValue errorPayload = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Invalid sampling result shape");
                            (void)this->receivedTasks.CompleteTask(taskId, tasks::Status::Failed,
                                                                  std::string("Invalid sampling result shape"),
                                                                  true, errorPayload, nullptr);
                            return;
                        }
                        (void)this->receivedTasks.CompleteTask(taskId, tasks::Status::Completed,
                                                              std::nullopt, false, result, nullptr);
                    } catch (const std::exception& e) {
                        const JSONValue errorPayload = CreateErrorObject(JSONRPCErrorCodes::InternalError, e.what());
                        (void)this->receivedTasks.CompleteTask(taskId, tasks::Status::Failed, std::string(e.what()),
                                                              true, errorPayload, nullptr);
                    }
                }));
                JSONValue result = tasks::SerializeCreateTaskResult(created);
                if (this->validationMode == validation::ValidationMode::Strict &&
                    !validation::validateCreateTaskResultJson(result)) {
                    errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid task creation result shape";
                    return errors::makeErrorResponse(req.id, e);
                }
                auto resp = std::make_unique<JSONRPCResponse>();
                resp->id = req.id;
                resp->result = result;
                return resp;
            }

            // Register cancellation and stop_source for this request id
            const std::string idStr = Impl::idToString(req.id);
            auto token = this->registerCancelToken(idStr);
            struct ScopeGuard { std::function<void()> f; ~ScopeGuard(){ if (f) f(); } } guard{ [this, idStr](){ this->unregisterCancelToken(idStr); } };
            auto src = this->registerStopSource(idStr);

            if (!this->samplingHandler && !this->samplingHandlerCancelable) {
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
            std::future<JSONValue> fut = this->samplingHandler
                ? this->samplingHandler(messages, modelPreferences, systemPrompt, includeContext)
                : this->samplingHandlerCancelable(messages, modelPreferences, systemPrompt, includeContext, src->get_token());
            JSONValue result = fut.get();
            // If cancelled while or after handler ran, return Cancelled
            if (token && token->cancelled.load()) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Cancelled";
                return errors::makeErrorResponse(req.id, e);
            }
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
        if (this->errorHandler) {
            this->errorHandler(err);
        }
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
    if (!this->transport) {
        this->taskWorkers.WaitAll();
        co_return;
    }
    auto fut = this->transport->Close();
    try {
        (void) co_await mcp::async::makeFutureAwaitable(std::move(fut));
    } catch (const std::exception& e) {
        LOG_ERROR("Disconnect exception: {}", e.what());
    }
    this->taskWorkers.WaitAll();
    co_return;
}
mcp::async::Task<ServerCapabilities> Client::Impl::coInitialize(
    const Implementation& clientInfo,
    const ClientCapabilities& capabilities) {
    FUNC_SCOPE();
    this->capabilities = capabilities;
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
        if (response) {
            LOG_DEBUG("Initialize response: {}", response->Serialize());
        } else {
            LOG_DEBUG("Initialize response: <null>");
        }
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

mcp::async::Task<CompletionResult> Client::Impl::coComplete(const CompleteParams& params) {
    FUNC_SCOPE();
    CompletionResult out;
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::Complete;
    JSONValue::Object paramsObj;
    paramsObj["ref"] = std::make_shared<JSONValue>(params.ref);
    JSONValue::Object argumentObj;
    argumentObj["name"] = std::make_shared<JSONValue>(params.argument.name);
    argumentObj["value"] = std::make_shared<JSONValue>(params.argument.value);
    paramsObj["argument"] = std::make_shared<JSONValue>(argumentObj);
    if (params.context.has_value()) {
        paramsObj["context"] = std::make_shared<JSONValue>(params.context.value());
    }
    request->params = JSONValue{paramsObj};

    auto fut = this->transport->SendRequest(std::move(request));
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (response && response->result.has_value()) {
            if (this->validationMode == validation::ValidationMode::Strict &&
                !validation::validateCompletionResultJson(response->result.value())) {
                throw std::runtime_error("Validation failed: completion/complete result shape");
            }
            const auto& rv = response->result.value();
            if (std::holds_alternative<JSONValue::Object>(rv.value)) {
                const auto& obj = std::get<JSONValue::Object>(rv.value);
                auto completionIt = obj.find("completion");
                if (completionIt != obj.end() && completionIt->second &&
                    std::holds_alternative<JSONValue::Object>(completionIt->second->value)) {
                    const auto& completionObj = std::get<JSONValue::Object>(completionIt->second->value);
                    auto valuesIt = completionObj.find("values");
                    if (valuesIt != completionObj.end() && valuesIt->second &&
                        std::holds_alternative<JSONValue::Array>(valuesIt->second->value)) {
                        const auto& arr = std::get<JSONValue::Array>(valuesIt->second->value);
                        for (const auto& item : arr) {
                            if (item && std::holds_alternative<std::string>(item->value)) {
                                out.values.push_back(std::get<std::string>(item->value));
                            }
                        }
                    }
                    auto totalIt = completionObj.find("total");
                    if (totalIt != completionObj.end() && totalIt->second &&
                        std::holds_alternative<int64_t>(totalIt->second->value)) {
                        out.total = std::get<int64_t>(totalIt->second->value);
                    }
                    auto hasMoreIt = completionObj.find("hasMore");
                    if (hasMoreIt != completionObj.end() && hasMoreIt->second &&
                        std::holds_alternative<bool>(hasMoreIt->second->value)) {
                        out.hasMore = std::get<bool>(hasMoreIt->second->value);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Complete exception: {}", e.what());
        throw;
    }
    co_return out;
}

mcp::async::Task<void> Client::Impl::coPing() {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::Ping;
    auto fut = this->transport->SendRequest(std::move(request));
    try {
        auto response = co_await mcp::async::makeFutureAwaitable(std::move(fut));
        if (!response || response->IsError() || !response->result.has_value()) {
            throw std::runtime_error("ping failed");
        }
        if (this->validationMode == validation::ValidationMode::Strict &&
            !validation::validatePingResultJson(response->result.value())) {
            throw std::runtime_error("Validation failed: ping result shape");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Ping exception: {}", e.what());
        throw;
    }
    co_return;
}

mcp::async::Task<CreateTaskResult> Client::Impl::coCallToolTask(const std::string& name,
                                                                const JSONValue& arguments,
                                                                const TaskMetadata& task) {
    FUNC_SCOPE();
    CreateTaskResult out;
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::CallTool;
    JSONValue::Object paramsObj;
    paramsObj["name"] = std::make_shared<JSONValue>(name);
    paramsObj["arguments"] = std::make_shared<JSONValue>(arguments);
    paramsObj["task"] = std::make_shared<JSONValue>(tasks::SerializeTaskMetadata(task));
    request->params = JSONValue{paramsObj};
    auto future = this->transport->SendRequest(std::move(request));
    auto response = co_await mcp::async::makeFutureAwaitable(std::move(future));
    if (!response || response->IsError() || !response->result.has_value()) {
        throw std::runtime_error(response && response->error.has_value()
            ? errorMessageFromPayload(response->error.value())
            : "tools/call task request failed");
    }
    if (this->validationMode == validation::ValidationMode::Strict &&
        !validation::validateCreateTaskResultJson(response->result.value())) {
        throw std::runtime_error("Validation failed: tools/call task result shape");
    }
    out = tasks::ParseCreateTaskResult(response->result.value());
    this->rememberOutboundTask(out.task.taskId, tasks::TaskKind::ToolCall);
    co_return out;
}

mcp::async::Task<Task> Client::Impl::coGetTask(const std::string& taskId) {
    FUNC_SCOPE();
    Task out;
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::GetTask;
    JSONValue::Object paramsObj;
    paramsObj["taskId"] = std::make_shared<JSONValue>(taskId);
    request->params = JSONValue{paramsObj};
    auto future = this->transport->SendRequest(std::move(request));
    auto response = co_await mcp::async::makeFutureAwaitable(std::move(future));
    if (!response || response->IsError() || !response->result.has_value()) {
        throw std::runtime_error(response && response->error.has_value()
            ? errorMessageFromPayload(response->error.value())
            : "tasks/get failed");
    }
    if (this->validationMode == validation::ValidationMode::Strict &&
        !validation::validateTaskJson(response->result.value())) {
        throw std::runtime_error("Validation failed: tasks/get result shape");
    }
    out = tasks::ParseTask(response->result.value());
    co_return out;
}

mcp::async::Task<std::vector<Task>> Client::Impl::coListTasks() {
    FUNC_SCOPE();
    auto future = this->coListTasksPaged(std::nullopt, std::nullopt).toFuture();
    auto page = co_await mcp::async::makeFutureAwaitable(std::move(future));
    co_return page.tasks;
}

mcp::async::Task<TasksListResult> Client::Impl::coListTasksPaged(const std::optional<std::string>& cursor,
                                                                 const std::optional<int>& limit) {
    FUNC_SCOPE();
    TasksListResult out;
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::ListTasks;
    JSONValue::Object paramsObj;
    if (cursor.has_value()) {
        paramsObj["cursor"] = std::make_shared<JSONValue>(cursor.value());
    }
    if (limit.has_value() && *limit > 0) {
        paramsObj["limit"] = std::make_shared<JSONValue>(static_cast<int64_t>(*limit));
    }
    if (!paramsObj.empty()) {
        request->params = JSONValue{paramsObj};
    }
    auto future = this->transport->SendRequest(std::move(request));
    auto response = co_await mcp::async::makeFutureAwaitable(std::move(future));
    if (!response || response->IsError() || !response->result.has_value()) {
        throw std::runtime_error(response && response->error.has_value()
            ? errorMessageFromPayload(response->error.value())
            : "tasks/list failed");
    }
    if (this->validationMode == validation::ValidationMode::Strict &&
        !validation::validateTasksListResultJson(response->result.value())) {
        throw std::runtime_error("Validation failed: tasks/list result shape");
    }
    out = tasks::ParseTasksListResult(response->result.value());
    co_return out;
}

mcp::async::Task<JSONValue> Client::Impl::coGetTaskResult(const std::string& taskId) {
    FUNC_SCOPE();
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::GetTaskResult;
    JSONValue::Object paramsObj;
    paramsObj["taskId"] = std::make_shared<JSONValue>(taskId);
    request->params = JSONValue{paramsObj};
    auto future = this->transport->SendRequest(std::move(request));
    auto response = co_await mcp::async::makeFutureAwaitable(std::move(future));
    if (!response) {
        co_return JSONValue{};
    }
    if (response->IsError()) {
        co_return response->error.value_or(JSONValue{});
    }
    if (!response->result.has_value()) {
        co_return JSONValue{};
    }
    auto kind = this->outboundTaskKindFor(taskId);
    if (this->validationMode == validation::ValidationMode::Strict && kind.has_value()) {
        const bool valid =
            (*kind == tasks::TaskKind::ToolCall && validation::validateCallToolResultJson(response->result.value())) ||
            (*kind == tasks::TaskKind::CreateMessage && validation::validateCreateMessageResultJson(response->result.value())) ||
            (*kind == tasks::TaskKind::Elicitation && validation::validateElicitationResultJson(response->result.value()));
        if (!valid) {
            throw std::runtime_error("Validation failed: tasks/result payload shape");
        }
    }
    co_return response->result.value();
}

mcp::async::Task<Task> Client::Impl::coCancelTask(const std::string& taskId) {
    FUNC_SCOPE();
    Task out;
    auto request = std::make_unique<JSONRPCRequest>();
    request->method = Methods::CancelTask;
    JSONValue::Object paramsObj;
    paramsObj["taskId"] = std::make_shared<JSONValue>(taskId);
    request->params = JSONValue{paramsObj};
    auto future = this->transport->SendRequest(std::move(request));
    auto response = co_await mcp::async::makeFutureAwaitable(std::move(future));
    if (!response || response->IsError() || !response->result.has_value()) {
        throw std::runtime_error(response && response->error.has_value()
            ? errorMessageFromPayload(response->error.value())
            : "tasks/cancel failed");
    }
    if (this->validationMode == validation::ValidationMode::Strict &&
        !validation::validateTaskJson(response->result.value())) {
        throw std::runtime_error("Validation failed: tasks/cancel result shape");
    }
    out = tasks::ParseTask(response->result.value());
    co_return out;
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
            if (response->result.has_value()) {
                co_return response->result.value();
            }
            if (response->error.has_value()) {
                co_return response->error.value();
            }
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

mcp::async::Task<void> Client::Impl::coNotifyRootsListChanged() {
    FUNC_SCOPE();
    if (!this->transport) {
        LOG_ERROR("NotifyRootsListChanged called without transport");
        co_return;
    }

    auto notification = std::make_unique<JSONRPCNotification>();
    notification->method = Methods::RootListChanged;
    notification->params = JSONValue{JSONValue::Object{}};
    try {
        (void) co_await mcp::async::makeFutureAwaitable(this->transport->SendNotification(std::move(notification)));
    } catch (const std::exception& e) {
        LOG_ERROR("NotifyRootsListChanged exception: {}", e.what());
    }
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
                            PopulateToolFromListItem(std::get<JSONValue::Object>(toolJson->value), tool);
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
    if (cursor.has_value()) {
        params["cursor"] = std::make_shared<JSONValue>(cursor.value());
    }
    if (limit.has_value() && *limit > 0) {
        params["limit"] = std::make_shared<JSONValue>(static_cast<int64_t>(*limit));
    }
    if (!params.empty()) {
        request->params = JSONValue{params};
    }
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
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) {
                            continue;
                        }
                        Tool tool;
                        PopulateToolFromListItem(std::get<JSONValue::Object>(itemPtr->value), tool);
                        out.tools.push_back(std::move(tool));
                    }
                }
                auto curIt = obj.find("nextCursor");
                if (curIt != obj.end()) {
                    if (std::holds_alternative<std::string>(curIt->second->value)) {
                        out.nextCursor = std::get<std::string>(curIt->second->value);
                    } else if (std::holds_alternative<int64_t>(curIt->second->value)) {
                        out.nextCursor = std::to_string(std::get<int64_t>(curIt->second->value));
                    }
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
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) {
                            continue;
                        }
                        Resource r;
                        PopulateResourceFromListItem(std::get<JSONValue::Object>(itemPtr->value), r);
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
    if (cursor.has_value()) {
        params["cursor"] = std::make_shared<JSONValue>(cursor.value());
    }
    if (limit.has_value() && *limit > 0) {
        params["limit"] = std::make_shared<JSONValue>(static_cast<int64_t>(*limit));
    }
    if (!params.empty()) {
        request->params = JSONValue{params};
    }
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
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) {
                            continue;
                        }
                        Resource r;
                        PopulateResourceFromListItem(std::get<JSONValue::Object>(itemPtr->value), r);
                        out.resources.push_back(std::move(r));
                    }
                }
                auto curIt = obj.find("nextCursor");
                if (curIt != obj.end()) {
                    if (std::holds_alternative<std::string>(curIt->second->value)) {
                        out.nextCursor = std::get<std::string>(curIt->second->value);
                    } else if (std::holds_alternative<int64_t>(curIt->second->value)) {
                        out.nextCursor = std::to_string(std::get<int64_t>(curIt->second->value));
                    }
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
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) {
                            continue;
                        }
                        ResourceTemplate rt;
                        PopulateResourceTemplateFromListItem(std::get<JSONValue::Object>(itemPtr->value), rt);
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
    if (cursor.has_value()) {
        params["cursor"] = std::make_shared<JSONValue>(cursor.value());
    }
    if (limit.has_value() && *limit > 0) {
        params["limit"] = std::make_shared<JSONValue>(static_cast<int64_t>(*limit));
    }
    if (!params.empty()) {
        request->params = JSONValue{params};
    }
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
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) {
                            continue;
                        }
                        ResourceTemplate rt;
                        PopulateResourceTemplateFromListItem(std::get<JSONValue::Object>(itemPtr->value), rt);
                        out.resourceTemplates.push_back(std::move(rt));
                    }
                }
                auto curIt = obj.find("nextCursor");
                if (curIt != obj.end()) {
                    if (std::holds_alternative<std::string>(curIt->second->value)) {
                        out.nextCursor = std::get<std::string>(curIt->second->value);
                    } else if (std::holds_alternative<int64_t>(curIt->second->value)) {
                        out.nextCursor = std::to_string(std::get<int64_t>(curIt->second->value));
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("ListResourceTemplatesPaged exception: {}", e.what()); 
        throw; 
    }
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
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) {
                            continue;
                        }
                        Prompt pr;
                        PopulatePromptFromListItem(std::get<JSONValue::Object>(itemPtr->value), pr);
                        prompts.push_back(std::move(pr));
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("ListPrompts exception: {}", e.what()); 
        throw; 
    }
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
    if (cursor.has_value()) {
        params["cursor"] = std::make_shared<JSONValue>(cursor.value());
    }
    if (limit.has_value() && *limit > 0) {
        params["limit"] = std::make_shared<JSONValue>(static_cast<int64_t>(*limit));
    }
    if (!params.empty()) {
        request->params = JSONValue{params};
    }
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
                        if (!itemPtr || !std::holds_alternative<JSONValue::Object>(itemPtr->value)) {
                            continue;
                        }
                        Prompt pr;
                        PopulatePromptFromListItem(std::get<JSONValue::Object>(itemPtr->value), pr);
                        out.prompts.push_back(std::move(pr));
                    }
                }
                auto curIt = obj.find("nextCursor");
                if (curIt != obj.end()) {
                    if (std::holds_alternative<std::string>(curIt->second->value)) {
                        out.nextCursor = std::get<std::string>(curIt->second->value);
                    } else if (std::holds_alternative<int64_t>(curIt->second->value)) {
                        out.nextCursor = std::to_string(std::get<int64_t>(curIt->second->value));
                    }
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
            if (response->result.has_value()) {
                co_return response->result.value();
            }
            if (response->error.has_value()) {
                co_return response->error.value();
            }
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
            if (response->result.has_value()) {
                co_return response->result.value();
            }
            if (response->error.has_value()) {
                co_return response->error.value();
            }
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
            if (response->result.has_value()) {
                co_return response->result.value();
            }
            if (response->error.has_value()) {
                co_return response->error.value();
            }
        }
    } catch (const std::exception& e) { 
        LOG_ERROR("GetPrompt exception: {}", e.what());
    }
    co_return JSONValue{};
}


Client::Client(const Implementation& clientInfo)
    : pImpl(std::unique_ptr<Impl>(new Impl(clientInfo))) {
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

std::future<CreateTaskResult> Client::CallToolTask(const std::string& name,
                                                   const JSONValue& arguments,
                                                   const TaskMetadata& task) {
    FUNC_SCOPE();
    return pImpl->coCallToolTask(name, arguments, task).toFuture();
}

std::future<Task> Client::GetTask(const std::string& taskId) {
    FUNC_SCOPE();
    return pImpl->coGetTask(taskId).toFuture();
}

std::future<std::vector<Task>> Client::ListTasks() {
    FUNC_SCOPE();
    return pImpl->coListTasks().toFuture();
}

std::future<TasksListResult> Client::ListTasksPaged(const std::optional<std::string>& cursor,
                                                    const std::optional<int>& limit) {
    FUNC_SCOPE();
    return pImpl->coListTasksPaged(cursor, limit).toFuture();
}

std::future<JSONValue> Client::GetTaskResult(const std::string& taskId) {
    FUNC_SCOPE();
    return pImpl->coGetTaskResult(taskId).toFuture();
}

std::future<Task> Client::CancelTask(const std::string& taskId) {
    FUNC_SCOPE();
    return pImpl->coCancelTask(taskId).toFuture();
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

std::future<CompletionResult> Client::Complete(const CompleteParams& params) {
    FUNC_SCOPE();
    return pImpl->coComplete(params).toFuture();
}

std::future<void> Client::Ping() {
    FUNC_SCOPE();
    return pImpl->coPing().toFuture();
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

void Client::SetTaskStatusHandler(TaskStatusHandler handler) {
    FUNC_SCOPE();
    pImpl->taskStatusHandler = std::move(handler);
}

void Client::SetRootsListHandler(RootsListHandler handler) {
    FUNC_SCOPE();
    pImpl->rootsListHandler = std::move(handler);
}

std::future<void> Client::NotifyRootsListChanged() {
    FUNC_SCOPE();
    return pImpl->coNotifyRootsListChanged().toFuture();
}

void Client::SetSamplingHandler(SamplingHandler handler) {
    FUNC_SCOPE();
    pImpl->samplingHandler = std::move(handler);
}

void Client::SetSamplingHandlerCancelable(SamplingHandlerCancelable handler) {
    FUNC_SCOPE();
    pImpl->samplingHandlerCancelable = std::move(handler);
}

void Client::SetElicitationHandler(ElicitationHandler handler) {
    FUNC_SCOPE();
    pImpl->elicitationHandler = std::move(handler);
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
