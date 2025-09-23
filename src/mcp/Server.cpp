//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Server.cpp
// Purpose: MCP server implementation
//==========================================================================================================
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "logging/Logger.h"
#include "mcp/Protocol.h"
#include "mcp/Server.h"
#include "mcp/async/FutureAwaitable.h"
#include "mcp/async/Task.h"
#include "mcp/errors/Errors.h"
#include "mcp/validation/Validation.h"
#include "mcp/validation/Validators.h"


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
    // Helper methods to keep request handling concise
    // Cancellation token used for request cancellation tracking
    struct CancellationToken { std::atomic<bool> cancelled{false}; };
    std::unique_ptr<JSONRPCResponse> dispatchRequest(const JSONRPCRequest& req);
    void sendInitializedAndListChangedAsync();
    std::unique_ptr<JSONRPCResponse> handleCreateMessageRequest(const JSONRPCRequest& req, const std::shared_ptr<CancellationToken>& token);
    std::unique_ptr<JSONRPCResponse> handleSubscribeRequest(const JSONRPCRequest& req);
    std::unique_ptr<JSONRPCResponse> handleUnsubscribeRequest(const JSONRPCRequest& req);
    // Paging + validation helpers (for list handlers)
    void parsePagingParams(const JSONRPCRequest& request, size_t& start, std::optional<size_t>& limitOpt);
    std::optional<std::string> computeNextCursor(size_t nextIndex, size_t total, const std::optional<size_t>& limitOpt);
    bool validateListResultStrict(const JSONValue& result, const std::string& methodName, const std::string& arrayKey);

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
    std::mutex logRateMutex;                 // Logging rate limit
    std::chrono::steady_clock::time_point logWindowStart{};
    unsigned int logWindowCount{0u};

    // Validation mode (opt-in)
    validation::ValidationMode validationMode{validation::ValidationMode::Off};

    // Cancellation support
    std::mutex cancelMutex;
    std::unordered_map<std::string, std::shared_ptr<CancellationToken>> cancelMap;
    
    // Track stop_sources for cooperative cancellation of async handlers per request id
    std::unordered_map<std::string, std::vector<std::shared_ptr<std::stop_source>>> stopSources;

    // RAII helper to register/unregister std::stop_source for a request id
    struct StopSourceGuard {
        Impl* self{nullptr};
        std::string id;
        std::shared_ptr<std::stop_source> src;
        StopSourceGuard(Impl* s, const std::string& id) : self(s), id(id) {
            if (self) { src = self->registerStopSource(id); }
        }
        ~StopSourceGuard() {
            if (self && src) { self->unregisterStopSource(id, src); }
        }
    };

    static std::string idToString(const JSONRPCId& id) {
        std::string idStr;
        std::visit([&](const auto& v){
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) { idStr = v;     }
            else if constexpr (std::is_same_v<T, int64_t>) { idStr = std::to_string(v); }
            else { idStr = ""; }
        }, id);
        return idStr;
    }

    void logInvalidCreateMessageParamsContext(const JSONValue& paramsVal) const {
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
        LOG_ERROR("Validation failed (Strict): {} params invalid | hasMessages={} messagesCount={} hasModelPreferences={} hasSystemPrompt={} hasIncludeContext={} (server)",
                  Methods::CreateMessage, hasMessages, messagesCount, hasModelPreferences, hasSystemPrompt, hasIncludeContext);
    }

    void logInvalidCreateMessageResultContext(const JSONValue& result) const {
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
        LOG_ERROR("Validation failed (Strict): {} result invalid | hasModel={} hasRole={} hasContentArray={} contentCount={} (server)",
                  Methods::CreateMessage, hasModel, hasRole, hasContentArray, contentCount);
    }

    JSONValue::Object makeToolObj(const Tool& t) const {
        JSONValue::Object to;
        to["name"] = std::make_shared<JSONValue>(t.name);
        to["description"] = std::make_shared<JSONValue>(t.description);
        // inputSchema may be empty JSONValue
        to["inputSchema"] = std::make_shared<JSONValue>(t.inputSchema);
        return to;
    }

    JSONValue::Object makeResourceObj(const Resource& r) const {
        JSONValue::Object ro;
        ro["uri"] = std::make_shared<JSONValue>(r.uri);
        ro["name"] = std::make_shared<JSONValue>(r.name);
        if (r.description.has_value()) ro["description"] = std::make_shared<JSONValue>(r.description.value());
        if (r.mimeType.has_value()) ro["mimeType"] = std::make_shared<JSONValue>(r.mimeType.value());
        return ro;
    }

    JSONValue::Object makeResourceTemplateObj(const ResourceTemplate& rt) const {
        JSONValue::Object rto;
        rto["uriTemplate"] = std::make_shared<JSONValue>(rt.uriTemplate);
        rto["name"] = std::make_shared<JSONValue>(rt.name);
        if (rt.description.has_value()) rto["description"] = std::make_shared<JSONValue>(rt.description.value());
        if (rt.mimeType.has_value()) rto["mimeType"] = std::make_shared<JSONValue>(rt.mimeType.value());
        return rto;
    }

    JSONValue::Object makePromptObj(const std::string& name) const {
        JSONValue::Object po;
        po["name"] = std::make_shared<JSONValue>(name);
        po["description"] = std::make_shared<JSONValue>(std::string("Prompt: ") + name);
        return po;
    }

    std::string parseIdFromParams(const JSONValue& params) const {
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

    void parsePromptsGetParams(const JSONRPCRequest& request, std::string& name, JSONValue& arguments) const {
        name.clear();
        arguments = JSONValue{};
        if (request.params.has_value() && std::holds_alternative<JSONValue::Object>(request.params->value)) {
            const auto& paramsObj = std::get<JSONValue::Object>(request.params->value);
            auto nameIt = paramsObj.find("name");
            if (nameIt != paramsObj.end() && std::holds_alternative<std::string>(nameIt->second->value)) {
                name = std::get<std::string>(nameIt->second->value);
            }
            auto argsIt = paramsObj.find("arguments");
            if (argsIt != paramsObj.end() && argsIt->second) {
                arguments = *argsIt->second;
            }
        }
    }

    JSONValue serializePromptResult(const PromptResult& result) const {
        JSONValue::Object resultObj;
        resultObj["description"] = std::make_shared<JSONValue>(result.description);
        JSONValue::Array messagesArray;
        for (auto& v : result.messages) {
            messagesArray.push_back(std::make_shared<JSONValue>(v));
        }
        resultObj["messages"] = std::make_shared<JSONValue>(messagesArray);
        return JSONValue{resultObj};
    }

    std::unique_ptr<JSONRPCResponse> handleToolsList(const JSONRPCRequest& req) {
    LOG_DEBUG("Handling tools/list request");
    size_t start = 0; std::optional<size_t> limitOpt; parsePagingParams(req, start, limitOpt);
    std::vector<Tool> tools;
    {
        std::lock_guard<std::mutex> lock(this->registryMutex);
        tools.reserve(this->toolMetadata.size());
        for (const auto& [name, meta] : this->toolMetadata) tools.push_back(meta);
    }
    std::sort(tools.begin(), tools.end(), [](const Tool& a, const Tool& b){ return a.name < b.name; });
    const size_t total = tools.size();
    if (start > total) start = total;
    const size_t end = limitOpt.has_value() ? std::min(total, start + limitOpt.value()) : total;

    JSONValue::Object resultObj;
    JSONValue::Array arr;
    for (size_t i = start; i < end; ++i) {
        const auto& t = tools[i];
        arr.push_back(std::make_shared<JSONValue>(makeToolObj(t)));
    }
    resultObj["tools"] = std::make_shared<JSONValue>(arr);
    auto next = computeNextCursor(end, total, limitOpt);
    if (next.has_value()) {
        resultObj["nextCursor"] = std::make_shared<JSONValue>(next.value());
    }
    JSONValue result{resultObj};
    if (this->validationMode == validation::ValidationMode::Strict) {
        if (!validation::validateToolsListResultJson(result) || !validateListResultStrict(result, Methods::ListTools, "tools")) {
            errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid tools/list result shape"; return errors::makeErrorResponse(req.id, e);
        }
    }
    auto resp = std::make_unique<JSONRPCResponse>();
    resp->id = req.id; resp->result = result; return resp;
}

    std::unique_ptr<JSONRPCResponse> handleResourcesList(const JSONRPCRequest& req) {
    LOG_DEBUG("Handling resources/list request");
    size_t start = 0; std::optional<size_t> limitOpt; parsePagingParams(req, start, limitOpt);
    std::vector<Resource> resources;
    {
        std::lock_guard<std::mutex> lock(this->registryMutex);
        resources.reserve(this->resourceUris.size());
        for (const auto& uri : this->resourceUris) resources.emplace_back(uri, std::string("Resource: ") + uri);
    }
    std::sort(resources.begin(), resources.end(), [](const Resource& a, const Resource& b){ return a.uri < b.uri; });
    const size_t total = resources.size();
    if (start > total) start = total;
    const size_t end = limitOpt.has_value() ? std::min(total, start + limitOpt.value()) : total;

    JSONValue::Object resultObj;
    JSONValue::Array arr;
    for (size_t i = start; i < end; ++i) {
        const auto& r = resources[i];
        arr.push_back(std::make_shared<JSONValue>(makeResourceObj(r)));
    }
    resultObj["resources"] = std::make_shared<JSONValue>(arr);
    auto next = computeNextCursor(end, total, limitOpt);
    if (next.has_value()) {
        resultObj["nextCursor"] = std::make_shared<JSONValue>(next.value());
    }
    JSONValue result{resultObj};
    if (this->validationMode == validation::ValidationMode::Strict) {
        if (!validation::validateResourcesListResultJson(result) || !validateListResultStrict(result, Methods::ListResources, "resources")) {
            errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid resources/list result shape"; return errors::makeErrorResponse(req.id, e);
        }
    }
    auto resp = std::make_unique<JSONRPCResponse>();
    resp->id = req.id; resp->result = result; return resp;
}

    std::unique_ptr<JSONRPCResponse> handleResourceTemplatesList(const JSONRPCRequest& req) {
    LOG_DEBUG("Handling resources/templates/list request");
    size_t start = 0; std::optional<size_t> limitOpt; parsePagingParams(req, start, limitOpt);
    std::vector<ResourceTemplate> templatesCopy;
    {
        std::lock_guard<std::mutex> lock(this->registryMutex);
        templatesCopy = this->resourceTemplates;
    }
    std::sort(templatesCopy.begin(), templatesCopy.end(), [](const ResourceTemplate& a, const ResourceTemplate& b){ return a.uriTemplate < b.uriTemplate; });
    const size_t total = templatesCopy.size();
    if (start > total) start = total;
    const size_t end = limitOpt.has_value() ? std::min(total, start + limitOpt.value()) : total;

    JSONValue::Object resultObj;
    JSONValue::Array arr;
    for (size_t i = start; i < end; ++i) {
        const auto& rt = templatesCopy[i];
        arr.push_back(std::make_shared<JSONValue>(makeResourceTemplateObj(rt)));
    }
    resultObj["resourceTemplates"] = std::make_shared<JSONValue>(arr);
    auto next = computeNextCursor(end, total, limitOpt);
    if (next.has_value()) {
        resultObj["nextCursor"] = std::make_shared<JSONValue>(next.value());
    }
    JSONValue result{resultObj};
    if (this->validationMode == validation::ValidationMode::Strict) {
        if (!validation::validateResourceTemplatesListResultJson(result) || !validateListResultStrict(result, Methods::ListResourceTemplates, "resourceTemplates")) {
            errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid resources/templates/list result shape"; return errors::makeErrorResponse(req.id, e);
        }
    }
    auto resp = std::make_unique<JSONRPCResponse>();
    resp->id = req.id; resp->result = result; return resp;
}

    std::unique_ptr<JSONRPCResponse> handlePromptsList(const JSONRPCRequest& req) {
    LOG_DEBUG("Handling prompts/list request");
    size_t start = 0; std::optional<size_t> limitOpt; parsePagingParams(req, start, limitOpt);
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(this->registryMutex);
        names.reserve(this->promptHandlers.size());
        for (const auto& kv : this->promptHandlers) names.push_back(kv.first);
    }
    std::sort(names.begin(), names.end());
    const size_t total = names.size();
    if (start > total) start = total;
    const size_t end = limitOpt.has_value() ? std::min(total, start + limitOpt.value()) : total;

    JSONValue::Object resultObj;
    JSONValue::Array arr;
    for (size_t i = start; i < end; ++i) {
        const auto& name = names[i];
        arr.push_back(std::make_shared<JSONValue>(makePromptObj(name)));
    }
    resultObj["prompts"] = std::make_shared<JSONValue>(arr);
    auto next = computeNextCursor(end, total, limitOpt);
    if (next.has_value()) {
        resultObj["nextCursor"] = std::make_shared<JSONValue>(next.value());
    }
    JSONValue result{resultObj};
    if (this->validationMode == validation::ValidationMode::Strict) {
        if (!validation::validatePromptsListResultJson(result) || !validateListResultStrict(result, Methods::ListPrompts, "prompts")) {
            errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid prompts/list result shape"; return errors::makeErrorResponse(req.id, e);
        }
    }
    auto resp = std::make_unique<JSONRPCResponse>();
    resp->id = req.id; resp->result = result; return resp;
}

    std::unique_ptr<JSONRPCResponse> handleToolsCall(const JSONRPCRequest& req, const std::shared_ptr<CancellationToken>& token) {
    LOG_DEBUG("Handling tools/call request");
    std::string name; JSONValue arguments;
    if (req.params.has_value() && std::holds_alternative<JSONValue::Object>(req.params->value)) {
        const auto& o = std::get<JSONValue::Object>(req.params->value);
        auto it = o.find("name"); if (it != o.end() && std::holds_alternative<std::string>(it->second->value)) name = std::get<std::string>(it->second->value);
        auto ia = o.find("arguments"); if (ia != o.end() && ia->second) arguments = *(ia->second);
    }
    if (name.empty()) { errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid params"; return errors::makeErrorResponse(req.id, e); }
    ToolHandler handler;
    {
        std::lock_guard<std::mutex> lock(this->registryMutex);
        auto it = this->toolHandlers.find(name);
        if (it != this->toolHandlers.end()) handler = it->second;
    }
    if (!handler) { errors::McpError e; e.code = JSONRPCErrorCodes::ToolNotFound; e.message = "Tool not found"; return errors::makeErrorResponse(req.id, e); }

    const std::string idStr = Impl::idToString(req.id);
    StopSourceGuard guard{this, idStr};
    auto src = guard.src;

    ToolResult tr;
    try {
        auto fut = handler(arguments, src->get_token());
        tr = fut.get();
    } catch (const std::exception& e) {
        errors::McpError err; err.code = JSONRPCErrorCodes::InternalError; err.message = e.what();
        return errors::makeErrorResponse(req.id, err);
    }
    if (token && token->cancelled.load()) {
        errors::McpError err; err.code = JSONRPCErrorCodes::InternalError; err.message = "Cancelled";
        return errors::makeErrorResponse(req.id, err);
    }
    JSONValue::Object obj;
    JSONValue::Array content;
    for (auto& v : tr.content) content.push_back(std::make_shared<JSONValue>(std::move(v)));
    obj["content"] = std::make_shared<JSONValue>(content);
    obj["isError"] = std::make_shared<JSONValue>(tr.isError);
    JSONValue result{obj};
    if (this->validationMode == validation::ValidationMode::Strict) {
        if (!validation::validateCallToolResultJson(result)) {
            LOG_ERROR("Validation failed (Strict): {} result invalid (server)", Methods::CallTool);
            errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid tool result shape"; return errors::makeErrorResponse(req.id, e);
        }
    }
    auto resp = std::make_unique<JSONRPCResponse>(); resp->id = req.id; resp->result = result; return resp;
}

    std::unique_ptr<JSONRPCResponse> handleResourcesRead(const JSONRPCRequest& req, const std::shared_ptr<CancellationToken>& token) {
    LOG_DEBUG("Handling resources/read request");
    std::string uri;
    std::optional<int64_t> offsetOpt; std::optional<int64_t> lengthOpt;
    if (req.params.has_value() && std::holds_alternative<JSONValue::Object>(req.params->value)) {
        const auto& o = std::get<JSONValue::Object>(req.params->value);
        auto it = o.find("uri"); if (it != o.end() && std::holds_alternative<std::string>(it->second->value)) uri = std::get<std::string>(it->second->value);
        auto io = o.find("offset"); if (io != o.end() && std::holds_alternative<int64_t>(io->second->value)) offsetOpt = std::get<int64_t>(io->second->value);
        auto il = o.find("length"); if (il != o.end() && std::holds_alternative<int64_t>(il->second->value)) lengthOpt = std::get<int64_t>(il->second->value);
    }
    if (uri.empty()) { errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid params"; return errors::makeErrorResponse(req.id, e); }
    if (offsetOpt.has_value() && offsetOpt.value() < 0) { errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid offset/length"; return errors::makeErrorResponse(req.id, e); }
    if (lengthOpt.has_value() && lengthOpt.value() <= 0) { errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid offset/length"; return errors::makeErrorResponse(req.id, e); }
    ResourceHandler handler;
    {
        std::lock_guard<std::mutex> lock(this->registryMutex);
        auto it = this->resourceHandlers.find(uri);
        if (it != this->resourceHandlers.end()) handler = it->second;
    }
    if (!handler) { errors::McpError e; e.code = JSONRPCErrorCodes::ResourceNotFound; e.message = "Resource not found"; return errors::makeErrorResponse(req.id, e); }

    const std::string idStr = Impl::idToString(req.id);
    StopSourceGuard guard{this, idStr};
    auto src = guard.src;

    ReadResourceResult rr;
    try {
        auto fut = handler(uri, src->get_token());
        rr = fut.get();
    } catch (const std::exception& e) {
        errors::McpError err; err.code = JSONRPCErrorCodes::InternalError; err.message = e.what();
        return errors::makeErrorResponse(req.id, err);
    }
    if (token && token->cancelled.load()) {
        errors::McpError err; err.code = JSONRPCErrorCodes::InternalError; err.message = "Cancelled";
        return errors::makeErrorResponse(req.id, err);
    }

    // Build result (optionally applying experimental chunking)
    JSONValue::Object obj;
    JSONValue::Array contents;
    if (!offsetOpt.has_value() && !lengthOpt.has_value()) {
        for (auto& v : rr.contents) { contents.push_back(std::make_shared<JSONValue>(std::move(v))); }
    } else {
        // Flatten text content and slice
        std::string flat;
        flat.reserve(1024);
        for (const auto& v : rr.contents) {
            if (!std::holds_alternative<JSONValue::Object>(v.value)) { errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Chunking requires text content"; return errors::makeErrorResponse(req.id, e); }
            const auto& o = std::get<JSONValue::Object>(v.value);
            auto itType = o.find("type"); auto itText = o.find("text");
            if (itType == o.end() || itText == o.end() || !itType->second || !itText->second) { errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Chunking requires text content"; return errors::makeErrorResponse(req.id, e); }
            if (!std::holds_alternative<std::string>(itType->second->value) || std::get<std::string>(itType->second->value) != std::string("text")) { errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Chunking requires text content"; return errors::makeErrorResponse(req.id, e); }
            if (!std::holds_alternative<std::string>(itText->second->value)) { errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Chunking requires text content"; return errors::makeErrorResponse(req.id, e); }
            flat += std::get<std::string>(itText->second->value);
        }
        size_t start = static_cast<size_t>(offsetOpt.value_or(0));
        if (start >= flat.size()) {
            // return empty contents
        } else {
            size_t maxLen = flat.size() - start;
            size_t take = lengthOpt.has_value() ? static_cast<size_t>(lengthOpt.value()) : maxLen;
            if (take > maxLen) { take = maxLen; }
            std::string slice = flat.substr(start, take);
            JSONValue::Object t; t["type"] = std::make_shared<JSONValue>(std::string("text")); t["text"] = std::make_shared<JSONValue>(slice);
            contents.push_back(std::make_shared<JSONValue>(JSONValue{t}));
        }
    }
    obj["contents"] = std::make_shared<JSONValue>(contents);
    JSONValue result{obj};
    if (this->validationMode == validation::ValidationMode::Strict) {
        if (!validation::validateReadResourceResultJson(result)) {
            LOG_ERROR("Validation failed (Strict): {} result invalid (server)", Methods::ReadResource);
            errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid resource read result shape"; return errors::makeErrorResponse(req.id, e);
        }
    }
    auto resp = std::make_unique<JSONRPCResponse>(); resp->id = req.id; resp->result = result; return resp;
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
        // Advertise experimental resource read chunking capability by default
        {
            JSONValue::Object rrc;
            rrc["enabled"] = std::make_shared<JSONValue>(true);
            rrc["maxChunkBytes"] = std::make_shared<JSONValue>(static_cast<int64_t>(65536));
            capabilities.experimental["resourceReadChunking"] = JSONValue{rrc};
        }
    }

    void handleNotification(std::unique_ptr<JSONRPCNotification> notification) {
        LOG_DEBUG("Received notification: {}", notification->method);
        
        if (notification->method == Methods::Initialized) {
            initialized = true;
            LOG_INFO("MCP server initialized by client");
        } else if (notification->method == Methods::Cancelled) {
            // Handle cancellation
            std::string idStr;
            if (notification->params.has_value()) {
                idStr = parseIdFromParams(notification->params.value());
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

    std::unique_ptr<JSONRPCResponse> handlePromptsGet(const JSONRPCRequest& request) {
        LOG_DEBUG("Handling prompts/get request");
        if (!request.params.has_value()) {
            errors::McpError e; e.code = JSONRPCErrorCodes::InvalidParams; e.message = "Invalid params";
            return errors::makeErrorResponse(request.id, e);
        }
        std::string promptName; JSONValue arguments;
        parsePromptsGetParams(request, promptName, arguments);
        std::lock_guard<std::mutex> lock(registryMutex);
        auto it = promptHandlers.find(promptName);
        if (it == promptHandlers.end()) {
            errors::McpError e; e.code = JSONRPCErrorCodes::PromptNotFound; e.message = "Prompt not found";
            return errors::makeErrorResponse(request.id, e);
        }
        PromptResult result = it->second(arguments);
        if (validationMode == validation::ValidationMode::Strict) {
            if (!validation::validateGetPromptResult(result)) {
                errors::McpError e; e.code = JSONRPCErrorCodes::InternalError; e.message = "Invalid handler result shape";
                return errors::makeErrorResponse(request.id, e);
            }
        }
        auto response = std::make_unique<JSONRPCResponse>();
        response->id = request.id;
        response->result = serializePromptResult(result);
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
        return this->dispatchRequest(req);
    });

    auto fut = this->transport->Start();
    try { (void) co_await mcp::async::makeFutureAwaitable(std::move(fut)); }
    catch (const std::exception& e) { LOG_ERROR("Server start exception: {}", e.what()); }
    co_return;
}

//================================ Helper method definitions =================================
std::unique_ptr<JSONRPCResponse> Server::Impl::dispatchRequest(const JSONRPCRequest& req) {
    try {
        const std::string idStr = Impl::idToString(req.id);
        auto token = this->registerCancelToken(idStr);
        struct ScopeGuard { std::function<void()> f; ~ScopeGuard(){ if (f) f(); } } guard{ [this, idStr](){ this->unregisterCancelToken(idStr); } };

        if (req.method == Methods::Initialize) {
            auto resp = this->handleInitialize(req);
            this->sendInitializedAndListChangedAsync();
            return resp;
        } else if (req.method == Methods::ListTools) {
            return this->handleToolsList(req);
        } else if (req.method == Methods::CallTool) {
            auto r = this->handleToolsCall(req, token);
            if (token && token->cancelled.load()) {
                errors::McpError err; err.code = JSONRPCErrorCodes::InternalError; err.message = "Cancelled";
                return errors::makeErrorResponse(req.id, err);
            }
            return r;
        } else if (req.method == Methods::ListResources) {
            return this->handleResourcesList(req);
        } else if (req.method == Methods::ListResourceTemplates) {
            return this->handleResourceTemplatesList(req);
        } else if (req.method == Methods::Subscribe) {
            return this->handleSubscribeRequest(req);
        } else if (req.method == Methods::Unsubscribe) {
            return this->handleUnsubscribeRequest(req);
        } else if (req.method == Methods::CreateMessage) {
            return this->handleCreateMessageRequest(req, token);
        } else if (req.method == Methods::ReadResource) {
            auto r = this->handleResourcesRead(req, token);
            if (token && token->cancelled.load()) {
                errors::McpError err; err.code = JSONRPCErrorCodes::InternalError; err.message = "Cancelled";
                return errors::makeErrorResponse(req.id, err);
            }
            return r;
        } else if (req.method == Methods::ListPrompts) {
            return this->handlePromptsList(req);
        } else if (req.method == Methods::GetPrompt) {
            auto r = this->handlePromptsGet(req);
            if (token && token->cancelled.load()) {
                errors::McpError err; err.code = JSONRPCErrorCodes::InternalError; err.message = "Cancelled";
                return errors::makeErrorResponse(req.id, err);
            }
            return r;
        }
        { errors::McpError e; e.code = JSONRPCErrorCodes::MethodNotFound; e.message = "Method not found"; return errors::makeErrorResponse(req.id, e); }
    } catch (const std::exception& e) {
        errors::McpError err; err.code = JSONRPCErrorCodes::InternalError; err.message = e.what();
        return errors::makeErrorResponse(req.id, err);
    }
}

void Server::Impl::sendInitializedAndListChangedAsync() {
    std::thread([this]() {
        try {
            auto note = std::make_unique<JSONRPCNotification>();
            note->method = Methods::Initialized;
            note->params = JSONValue{JSONValue::Object{}};
            (void)this->transport->SendNotification(std::move(note));
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
        } catch (const std::exception& e) {
            LOG_ERROR("Initialized/list_changed async send exception: {}", e.what());
        }
    }).detach();
}

// Common helpers for paging and strict validation (used by list handlers)
void Server::Impl::parsePagingParams(const JSONRPCRequest& request, size_t& start, std::optional<size_t>& limitOpt) {
    start = 0;
    limitOpt.reset();
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
}

std::optional<std::string> Server::Impl::computeNextCursor(size_t nextIndex, size_t total, const std::optional<size_t>& limitOpt) {
    if (limitOpt.has_value() && nextIndex < total) {
        return std::to_string(nextIndex);
    }
    return std::nullopt;
}

bool Server::Impl::validateListResultStrict(const JSONValue& result, const std::string& methodName, const std::string& arrayKey) {
    size_t items = 0; bool hasNext = false; std::string nextType;
    bool ok = true;
    if (std::holds_alternative<JSONValue::Object>(result.value)) {
        const auto& o = std::get<JSONValue::Object>(result.value);
        auto itArr = o.find(arrayKey);
        if (itArr != o.end() && std::holds_alternative<JSONValue::Array>(itArr->second->value)) {
            items = std::get<JSONValue::Array>(itArr->second->value).size();
        } else {
            ok = false;
        }
        auto itNc = o.find("nextCursor");
        hasNext = (itNc != o.end());
        if (hasNext) {
            if (std::holds_alternative<std::string>(itNc->second->value)) {
                nextType = "string";
            } else if (std::holds_alternative<int64_t>(itNc->second->value)) {
                // For sanity, we require string; track type for logging
                nextType = "int";
                ok = false;
            } else {
                nextType = "other";
                ok = false;
            }
        }
    } else {
        ok = false;
    }
    if (!ok) {
        LOG_ERROR("Validation failed (Strict): {} result invalid | items={} hasNextCursor={} nextCursorType={}", methodName, items, hasNext, nextType);
    }
    return ok;
}

std::unique_ptr<JSONRPCResponse> Server::Impl::handleSubscribeRequest(const JSONRPCRequest& req) {
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
}

std::unique_ptr<JSONRPCResponse> Server::Impl::handleUnsubscribeRequest(const JSONRPCRequest& req) {
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
    this->resourcesSubscribed = !this->subscribedUris.empty();
    auto resp = std::make_unique<JSONRPCResponse>();
    resp->id = req.id;
    resp->result = JSONValue{JSONValue::Object{}};
    return resp;
}

std::unique_ptr<JSONRPCResponse> Server::Impl::handleCreateMessageRequest(const JSONRPCRequest& req, const std::shared_ptr<CancellationToken>& token) {
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
    if (token && token->cancelled.load()) {
        errors::McpError err; err.code = JSONRPCErrorCodes::InternalError; err.message = "Cancelled";
        return errors::makeErrorResponse(req.id, err);
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

// Validation (opt-in)
void Server::SetValidationMode(validation::ValidationMode mode) {
    FUNC_SCOPE();
    pImpl->validationMode = mode;
}

validation::ValidationMode Server::GetValidationMode() const {
    FUNC_SCOPE();
    return pImpl->validationMode;
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
