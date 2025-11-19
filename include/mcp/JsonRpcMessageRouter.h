//========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: JsonRpcMessageRouter.h
// Purpose: Interface for JSON-RPC message routing (classification and dispatch)
//========================================================================================================

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <memory>

#include "mcp/Transport.h"
#include "mcp/JSONRPCTypes.h"

namespace mcp {

struct RouterHandlers {
    ITransport::RequestHandler requestHandler;
    ITransport::NotificationHandler notificationHandler;
    ITransport::ErrorHandler errorHandler;
};

using ResponseResolver = std::function<void(JSONRPCResponse&&)>;

class IJsonRpcMessageRouter {
public:
    virtual ~IJsonRpcMessageRouter() = default;

    enum class MessageKind {
        Request,
        Response,
        Notification,
        Unknown
    };

    // Classify a JSON-RPC message without invoking handlers.
    virtual MessageKind classify(const std::string& json) = 0;

    // Routes a JSON-RPC message. If a response should be sent (for requests), returns
    // the serialized response payload; for responses/notifications, returns std::nullopt.
    // The resolver is invoked when a JSON-RPC response is received and must resolve any pending promise.
    virtual std::optional<std::string> route(
        const std::string& json,
        RouterHandlers& handlers,
        const ResponseResolver& resolve) = 0;
};

// Factory: returns the default router implementation
std::unique_ptr<IJsonRpcMessageRouter> MakeDefaultJsonRpcMessageRouter();

} // namespace mcp
