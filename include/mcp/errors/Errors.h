//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Errors.h
// Purpose: Typed error structures and JSON-RPC error mapping helpers for MCP C++ SDK
//==========================================================================================================

#pragma once

#include <optional>
#include <string>

#include "mcp/JSONRPCTypes.h"

namespace mcp {
namespace errors {

// Categorization of common JSON-RPC and MCP error codes.
enum class ErrorCategory {
    JsonRpcParse,
    JsonRpcInvalidRequest,
    JsonRpcMethodNotFound,
    JsonRpcInvalidParams,
    JsonRpcInternal,
    McpInvalidRequestId,
    McpMethodNotAllowed,
    McpResourceNotFound,
    McpToolNotFound,
    McpPromptNotFound,
    Unknown
};

// Typed error representation used by the SDK.
struct McpError {
    int code{0};
    std::string message;
    std::optional<JSONValue> data;
    ErrorCategory category{ErrorCategory::Unknown};
};

// Map a JSON-RPC/MCP numeric error code to an ErrorCategory.
//
// Args:
//   code: The integer error code (JSON-RPC standard or MCP-specific).
//
// Returns:
//   ErrorCategory corresponding to the code, or Unknown when unmapped.
inline ErrorCategory errorCategoryFromCode(int code) {
    switch (code) {
        case JSONRPCErrorCodes::ParseError: return ErrorCategory::JsonRpcParse;
        case JSONRPCErrorCodes::InvalidRequest: return ErrorCategory::JsonRpcInvalidRequest;
        case JSONRPCErrorCodes::MethodNotFound: return ErrorCategory::JsonRpcMethodNotFound;
        case JSONRPCErrorCodes::InvalidParams: return ErrorCategory::JsonRpcInvalidParams;
        case JSONRPCErrorCodes::InternalError: return ErrorCategory::JsonRpcInternal;
        case JSONRPCErrorCodes::InvalidRequestId: return ErrorCategory::McpInvalidRequestId;
        case JSONRPCErrorCodes::MethodNotAllowed: return ErrorCategory::McpMethodNotAllowed;
        case JSONRPCErrorCodes::ResourceNotFound: return ErrorCategory::McpResourceNotFound;
        case JSONRPCErrorCodes::ToolNotFound: return ErrorCategory::McpToolNotFound;
        case JSONRPCErrorCodes::PromptNotFound: return ErrorCategory::McpPromptNotFound;
        default: return ErrorCategory::Unknown;
    }
}

// Convert a JSON-RPC error object (shape: { code, message, data? }) to McpError.
// Returns std::nullopt when the input is not a valid error object.
//
// Args:
//   errVal: JSONValue expected to be an Object with code/message and optional data.
//
// Returns:
//   std::optional<McpError> populated when shape is valid.
inline std::optional<McpError> mcpErrorFromErrorValue(const JSONValue& errVal) {
    if (!std::holds_alternative<JSONValue::Object>(errVal.value)) {
        return std::nullopt;
    }
    const auto& obj = std::get<JSONValue::Object>(errVal.value);
    auto itCode = obj.find("code");
    auto itMsg = obj.find("message");
    if (itCode == obj.end() || itMsg == obj.end()) {
        return std::nullopt;
    }
    if (!itCode->second || !itMsg->second) {
        return std::nullopt;
    }
    if (!std::holds_alternative<int64_t>(itCode->second->value) ||
        !std::holds_alternative<std::string>(itMsg->second->value)) {
        return std::nullopt;
    }
    int code = static_cast<int>(std::get<int64_t>(itCode->second->value));
    std::string message = std::get<std::string>(itMsg->second->value);

    std::optional<JSONValue> data;
    auto itData = obj.find("data");
    if (itData != obj.end() && itData->second) {
        data = *(itData->second);
    }

    McpError e;
    e.code = code;
    e.message = std::move(message);
    e.data = std::move(data);
    e.category = errorCategoryFromCode(code);
    return e;
}

// Extract McpError from a JSONRPCResponse if it carries an error.
//
// Args:
//   response: JSONRPCResponse that may contain an error object.
//
// Returns:
//   std::optional<McpError> when response.IsError() and shape is valid.
inline std::optional<McpError> mcpErrorFromResponse(const JSONRPCResponse& response) {
    if (!response.error.has_value()) {
        return std::nullopt;
    }
    return mcpErrorFromErrorValue(response.error.value());
}

// Create a JSONValue error object from a typed McpError.
//
// Args:
//   err: The McpError to serialize.
//
// Returns:
//   JSONValue of Object type with code/message and optional data.
inline JSONValue makeErrorValue(const McpError& err) {
    JSONValue::Object obj;
    obj["code"] = std::make_shared<JSONValue>(static_cast<int64_t>(err.code));
    obj["message"] = std::make_shared<JSONValue>(err.message);
    if (err.data.has_value()) {
        obj["data"] = std::make_shared<JSONValue>(err.data.value());
    }
    return JSONValue{obj};
}

// Convenience: Create a JSONRPCResponse error from McpError and id.
//
// Args:
//   id: JSON-RPC id to echo in the response.
//   err: Typed error to map.
//
// Returns:
//   std::unique_ptr<JSONRPCResponse> containing an error.
inline std::unique_ptr<JSONRPCResponse> makeErrorResponse(const JSONRPCId& id, const McpError& err) {
    return CreateErrorResponse(id, err.code, err.message, err.data);
}

} // namespace errors
} // namespace mcp
