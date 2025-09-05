//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: JSONRPCTypes.h
// Purpose: JSON-RPC 2.0 message types for MCP protocol
//==========================================================================================================

#pragma once

#include <string>
#include <memory>
#include <variant>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mcp {

// JSON value type - simplified JSON representation using std library only
struct JSONValue {
    using Array = std::vector<std::shared_ptr<JSONValue>>;
    using Object = std::unordered_map<std::string, std::shared_ptr<JSONValue>>;
    
    std::variant<
        std::nullptr_t,
        bool,
        int64_t,
        double,
        std::string,
        Array,
        Object
    > value;

    // Constructors and special members (defined out-of-line)
    JSONValue();
    JSONValue(const JSONValue&);
    JSONValue(JSONValue&&);
    JSONValue& operator=(const JSONValue&);
    JSONValue& operator=(JSONValue&&);
    ~JSONValue();
    
    // Explicit constructors for supported types
    explicit JSONValue(std::nullptr_t);
    explicit JSONValue(bool v);
    explicit JSONValue(int64_t v);
    explicit JSONValue(double v);
    explicit JSONValue(const char* s);
    explicit JSONValue(const std::string& s);
    explicit JSONValue(std::string&& s);
    explicit JSONValue(const Array& a);
    explicit JSONValue(Array&& a);
    explicit JSONValue(const Object& o);
    explicit JSONValue(Object&& o);

    // Access the underlying variant
    auto& get() { return value; }
    const auto& get() const { return value; }
};

// JSON-RPC 2.0 ID type
using JSONRPCId = std::variant<std::string, int64_t, std::nullptr_t>;

// Base JSON-RPC message
class JSONRPCMessage {
public:
    std::string jsonrpc = "2.0";
    
    virtual ~JSONRPCMessage() = default;
    virtual std::string Serialize() const = 0;
    virtual bool Deserialize(const std::string& json) = 0;
};

// JSON-RPC Request
class JSONRPCRequest : public JSONRPCMessage {
public:
    JSONRPCId id;
    std::string method;
    std::optional<JSONValue> params;

    JSONRPCRequest() = default;
    JSONRPCRequest(JSONRPCId id, std::string method, std::optional<JSONValue> params = std::nullopt)
        : id(std::move(id)), method(std::move(method)), params(std::move(params)) {}

    std::string Serialize() const override;
    bool Deserialize(const std::string& json) override;
};

// JSON-RPC Response
class JSONRPCResponse : public JSONRPCMessage {
public:
    JSONRPCId id;
    std::optional<JSONValue> result;
    std::optional<JSONValue> error;

    JSONRPCResponse() = default;
    JSONRPCResponse(JSONRPCId id, JSONValue result)
        : id(std::move(id)), result(std::move(result)) {}
    JSONRPCResponse(JSONRPCId id, JSONValue error, bool /*isError*/)
        : id(std::move(id)), error(std::move(error)) {}

    std::string Serialize() const override;
    bool Deserialize(const std::string& json) override;
    
    bool IsError() const { return error.has_value(); }
};

// JSON-RPC Notification
class JSONRPCNotification : public JSONRPCMessage {
public:
    std::string method;
    std::optional<JSONValue> params;

    JSONRPCNotification() = default;
    JSONRPCNotification(std::string method, std::optional<JSONValue> params = std::nullopt)
        : method(std::move(method)), params(std::move(params)) {}

    std::string Serialize() const override;
    bool Deserialize(const std::string& json) override;
};

// JSON-RPC Error codes (standard + MCP specific)
namespace JSONRPCErrorCodes {
    constexpr int ParseError = -32700;
    constexpr int InvalidRequest = -32600;
    constexpr int MethodNotFound = -32601;
    constexpr int InvalidParams = -32602;
    constexpr int InternalError = -32603;
    
    // MCP specific error codes
    constexpr int InvalidRequestId = -32000;
    constexpr int MethodNotAllowed = -32001;
    constexpr int ResourceNotFound = -32002;
    constexpr int ToolNotFound = -32003;
    constexpr int PromptNotFound = -32004;
}

// Utility functions for JSON-RPC error responses
JSONValue CreateErrorObject(int code, const std::string& message, 
                           const std::optional<JSONValue>& data = std::nullopt);

std::unique_ptr<JSONRPCResponse> CreateErrorResponse(
    const JSONRPCId& id, int code, const std::string& message,
    const std::optional<JSONValue>& data = std::nullopt);

} // namespace mcp
