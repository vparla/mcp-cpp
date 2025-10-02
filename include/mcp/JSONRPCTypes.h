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

//==========================================================================================================
// JSONValue
// Purpose: Simplified JSON representation backed by std::variant and shared_ptr graphs.
// Fields:
//   Array: vector<shared_ptr<JSONValue>> representing a JSON array.
//   Object: unordered_map<string, shared_ptr<JSONValue>> representing a JSON object.
//   value: variant holding nullptr, bool, int64_t, double, string, Array, or Object.
//==========================================================================================================
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

//==========================================================================================================
// JSONRPCId
// Purpose: JSON-RPC 2.0 id variant (per spec): string, integer, or null.
//==========================================================================================================
// JSON-RPC 2.0 ID type
using JSONRPCId = std::variant<std::string, int64_t, std::nullptr_t>;

//==========================================================================================================
// JSONRPCMessage
// Purpose: Abstract base for JSON-RPC 2.0 messages providing serialization APIs.
// Methods:
//   Serialize(): Returns canonical JSON string for the message.
//   Deserialize(json): Parses JSON string into this object; returns true on success.
//==========================================================================================================
// Base JSON-RPC message
class JSONRPCMessage {
public:
    std::string jsonrpc = "2.0";
    
    virtual ~JSONRPCMessage() = default;
    virtual std::string Serialize() const = 0;
    virtual bool Deserialize(const std::string& json) = 0;
};

//==========================================================================================================
// JSONRPCRequest
// Purpose: JSON-RPC 2.0 request message with id, method, and optional params.
// Ctors:
//   JSONRPCRequest(id, method, params?): Initializes fields (params optional).
// Methods:
//   Serialize(): JSON string.
//   Deserialize(json): Returns true when parsed successfully.
//==========================================================================================================
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

//==========================================================================================================
// JSONRPCResponse
// Purpose: JSON-RPC 2.0 response message carrying either result or error.
// Ctors:
//   JSONRPCResponse(id, result): Success response with result set.
//   JSONRPCResponse(id, error, /*isError*/): Error response with error set.
// Methods:
//   Serialize()/Deserialize(json)
//   IsError(): True when error is present.
//==========================================================================================================
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

//==========================================================================================================
// JSONRPCNotification
// Purpose: JSON-RPC 2.0 notification (no id, no response).
// Ctors:
//   JSONRPCNotification(method, params?): Initializes method and optional params.
// Methods:
//   Serialize()/Deserialize(json)
//==========================================================================================================
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

//==========================================================================================================
// JSONRPCErrorCodes
// Purpose: Standard JSON-RPC error codes plus MCP-specific codes for SDK ergonomics.
// Notes:
//   Values are stable and align with JSON-RPC 2.0 spec; MCP codes are negative values in the -320xx range.
//==========================================================================================================
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

//==========================================================================================================
// CreateErrorObject
// Purpose: Build a JSON error object with shape { code, message, data? }.
// Args:
//   code: Integer error code (standard or MCP-specific).
//   message: Human-readable description.
//   data: Optional structured payload.
// Returns:
//   JSONValue object representing the error.
//==========================================================================================================
// Utility functions for JSON-RPC error responses
JSONValue CreateErrorObject(int code, const std::string& message, 
                           const std::optional<JSONValue>& data = std::nullopt);

//==========================================================================================================
// CreateErrorResponse
// Purpose: Convenience to wrap an error object into a JSONRPCResponse with the given id.
// Args:
//   id: Request id to echo in the response (string | int64 | null).
//   code: Integer error code.
//   message: Error message.
//   data: Optional structured payload.
// Returns:
//   unique_ptr<JSONRPCResponse> with error populated.
//==========================================================================================================
std::unique_ptr<JSONRPCResponse> CreateErrorResponse(
    const JSONRPCId& id, int code, const std::string& message,
    const std::optional<JSONValue>& data = std::nullopt);

} // namespace mcp
