//========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: JsonRpcMessageRouter.cpp
// Purpose: Default implementation for JSON-RPC message routing
//========================================================================================================

#include <optional>
#include <string>

#include "logging/Logger.h"
#include "mcp/JsonRpcMessageRouter.h"
#include "mcp/JSONRPCTypes.h"

namespace mcp {

namespace {
// Detect if a given top-level key exists (e.g., id at root, not inside params)
static bool hasTopLevelKey(const std::string& s, const std::string& key) {
    std::size_t i = 0;
    auto isWs = [](char c){ return c==' ' || c=='\t' || c=='\r' || c=='\n'; };
    while (i < s.size() && isWs(s[i])) {
        ++i;
    }
    if (i >= s.size() || s[i] != '{') {
        return false;
    }
    ++i;
    unsigned int depth = 1u;
    bool inStr = false;
    bool esc = false;
    while (i < s.size()) {
        char c = s[i++];
        if (inStr) {
            if (esc) {
                esc = false;
                continue;
            }
            if (c == '\\') {
                esc = true;
                continue;
            }
            if (c == '"') {
                inStr = false;
            }
            continue;
        }
        if (c == '"') {
            std::string keyStr;
            bool e2 = false;
            while (i < s.size()) {
                char d = s[i++];
                if (e2) {
                    e2 = false;
                    continue;
                }
                if (d == '\\') {
                    e2 = true;
                    continue;
                }
                if (d == '"') {
                    break;
                }
                keyStr.push_back(d);
            }
            while (i < s.size() && isWs(s[i])) {
                ++i;
            }
            if (i < s.size() && s[i] == ':') {
                ++i;
                if (depth == 1u && keyStr == key) {
                    return true;
                }
            }
            continue;
        }
        if (c == '{') {
            ++depth;
            continue;
        }
        if (c == '}') {
            if (depth > 0u) {
                --depth;
                if (depth == 0u) {
                    break;
                }
            } else {
                break;
            }
            continue;
        }
    }
    return false;
}

class JsonRpcMessageRouter : public IJsonRpcMessageRouter {
public:
    MessageKind classify(const std::string& json) override {
        if (hasTopLevelKey(json, "result") || hasTopLevelKey(json, "error")) {
            return MessageKind::Response;
        }
        if (json.find("\"method\"") != std::string::npos) {
            if (hasTopLevelKey(json, "id")) {
                return MessageKind::Request;
            }
            return MessageKind::Notification;
        }
        return MessageKind::Unknown;
    }

    std::optional<std::string> route(
        const std::string& json,
        RouterHandlers& handlers,
        const ResponseResolver& resolve) override {
        // First, try response fast-path (presence of top-level result or error keys)
        if (hasTopLevelKey(json, "result") || hasTopLevelKey(json, "error")) {
            JSONRPCResponse response;
            if (response.Deserialize(json)) {
                resolve(std::move(response));
                return std::nullopt;
            }
        }

        // Next, try request
        if (json.find("\"method\"") != std::string::npos && hasTopLevelKey(json, "id")) {
            JSONRPCRequest request;
            if (request.Deserialize(json)) {
                if (handlers.requestHandler) {
                    try {
                        auto resp = handlers.requestHandler(request);
                        if (!resp) {
                            resp = std::make_unique<JSONRPCResponse>();
                            resp->id = request.id;
                            resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Null response from handler", std::nullopt);
                        } else {
                            resp->id = request.id;
                        }
                        return resp->Serialize();
                    } catch (const std::exception& e) {
                        LOG_ERROR("Request handler exception: {}", e.what());
                        auto resp = std::make_unique<JSONRPCResponse>();
                        resp->id = request.id;
                        resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, e.what(), std::nullopt);
                        return resp->Serialize();
                    }
                }
            }
        }

        // Finally, notification
        {
            JSONRPCNotification notification;
            if (notification.Deserialize(json)) {
                if (handlers.notificationHandler) {
                    handlers.notificationHandler(std::make_unique<JSONRPCNotification>(std::move(notification)));
                }
                return std::nullopt;
            }
        }

        LOG_WARN("Router: unrecognized JSON-RPC message: {}", json);
        if (handlers.errorHandler) {
            handlers.errorHandler("Router: unrecognized JSON-RPC message");
        }
        return std::nullopt;
    }
};
} // namespace

std::unique_ptr<IJsonRpcMessageRouter> MakeDefaultJsonRpcMessageRouter() {
    return std::make_unique<JsonRpcMessageRouter>();
}

} // namespace mcp
