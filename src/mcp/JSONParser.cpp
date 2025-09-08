//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: JSONParser.cpp
// Purpose: Minimalistic JSON parser using only std library
//==========================================================================================================

#include <sstream>
#include <cctype>
#include <stdexcept>
#include <iomanip>
#include "mcp/JSONRPCTypes.h"
#include "logging/Logger.h"


namespace mcp {

//----------------------------------------------------------------------------------------------------------
// JSONValue special members (out-of-line definitions)
//----------------------------------------------------------------------------------------------------------
JSONValue::JSONValue() : value(nullptr) {}
JSONValue::JSONValue(const JSONValue&) = default;
JSONValue::JSONValue(JSONValue&&) = default;
JSONValue& JSONValue::operator=(const JSONValue&) = default;
JSONValue& JSONValue::operator=(JSONValue&&) = default;
JSONValue::~JSONValue() {}

// Explicit constructors
JSONValue::JSONValue(std::nullptr_t) : value(nullptr) {}
JSONValue::JSONValue(bool v) : value(v) {}
JSONValue::JSONValue(int64_t v) : value(v) {}
JSONValue::JSONValue(double v) : value(v) {}
JSONValue::JSONValue(const char* s) : value(std::string(s ? s : "")) {}
JSONValue::JSONValue(const std::string& s) : value(s) {}
JSONValue::JSONValue(std::string&& s) : value(std::move(s)) {}
JSONValue::JSONValue(const Array& a) : value(a) {}
JSONValue::JSONValue(Array&& a) : value(std::move(a)) {}
JSONValue::JSONValue(const Object& o) : value(o) {}
JSONValue::JSONValue(Object&& o) : value(std::move(o)) {}

// -------------------------------
// Minimal recursive JSON parser
// -------------------------------
namespace {
struct JsonParser {
    const std::string& s;
    std::size_t i{0};

    explicit JsonParser(const std::string& str, std::size_t start = 0) : s(str), i(start) {}

    void skipWs() {
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; } else { break; }
        }
    }

    bool match(char c) {
        skipWs();
        if (i < s.size() && s[i] == c) { ++i; return true; }
        return false;
    }

    char peek() const { return (i < s.size()) ? s[i] : '\0'; }

    std::string parseString() {
        skipWs();
        if (i >= s.size() || s[i] != '"') throw std::runtime_error("Expected '\"' at string start");
        ++i; // skip opening quote
        std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') break;
            if (c == '\\') {
                if (i >= s.size()) throw std::runtime_error("Invalid escape");
                char e = s[i++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        // Parse 4 hex digits, best-effort: store as literal if non-ASCII
                        if (i + 4 > s.size()) throw std::runtime_error("Invalid unicode escape");
                        std::string hex = s.substr(i, 4);
                        i += 4;
                        // Convert BMP code point to UTF-8 (basic, no surrogate pairs)
                        unsigned int code = 0;
                        for (char h : hex) {
                            code <<= 4;
                            if (h >= '0' && h <= '9') code += (h - '0');
                            else if (h >= 'a' && h <= 'f') code += 10 + (h - 'a');
                            else if (h >= 'A' && h <= 'F') code += 10 + (h - 'A');
                            else throw std::runtime_error("Invalid hex in unicode escape");
                        }
                        if (code <= 0x7F) {
                            out.push_back(static_cast<char>(code));
                        } else if (code <= 0x7FF) {
                            out.push_back(static_cast<char>(0xC0 | ((code >> 6) & 0x1F)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | ((code >> 12) & 0x0F)));
                            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default: throw std::runtime_error("Unknown escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    JSONValue parseNumber() {
        skipWs();
        std::size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        bool isFloat = false;
        if (i < s.size() && s[i] == '.') {
            isFloat = true; ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            isFloat = true; ++i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        std::string num = s.substr(start, i - start);
        try {
            if (!isFloat) {
                long long v = std::stoll(num);
                return JSONValue(static_cast<int64_t>(v));
            } else {
                double v = std::stod(num);
                return JSONValue(v);
            }
        } catch (...) {
            return JSONValue(nullptr);
        }
    }

    JSONValue parseArray() {
        if (!match('[')) throw std::runtime_error("Expected '['");
        JSONValue::Array arr;
        skipWs();
        if (match(']')) return JSONValue(arr);
        while (true) {
            JSONValue val = parseValue();
            arr.push_back(std::make_shared<JSONValue>(std::move(val)));
            skipWs();
            if (match(']')) break;
            if (!match(',')) throw std::runtime_error("Expected ',' in array");
        }
        return JSONValue(arr);
    }

    JSONValue parseObject() {
        if (!match('{')) throw std::runtime_error("Expected '{'");
        JSONValue::Object obj;
        skipWs();
        if (match('}')) return JSONValue(obj);
        while (true) {
            std::string key = parseString();
            skipWs();
            if (!match(':')) throw std::runtime_error("Expected ':' after key");
            JSONValue val = parseValue();
            obj[key] = std::make_shared<JSONValue>(std::move(val));
            skipWs();
            if (match('}')) break;
            if (!match(',')) throw std::runtime_error("Expected ',' in object");
        }
        return JSONValue(obj);
    }

    JSONValue parseValue() {
        skipWs();
        if (i >= s.size()) throw std::runtime_error("Unexpected end of JSON");
        char c = s[i];
        if (c == '"') return JSONValue(parseString());
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == 't') { // true
            if (s.compare(i, 4, "true") == 0) { i += 4; return JSONValue(true); }
        }
        if (c == 'f') { // false
            if (s.compare(i, 5, "false") == 0) { i += 5; return JSONValue(false); }
        }
        if (c == 'n') { // null
            if (s.compare(i, 4, "null") == 0) { i += 4; return JSONValue(nullptr); }
        }
        // number
        return parseNumber();
    }
};
} // namespace

// Simple JSON serialization
std::string serializeJSONValue(const JSONValue& value) {
    FUNC_SCOPE();
    auto result = std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(v);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::to_string(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            std::ostringstream oss;
            oss << '"';
            for (char c : v) {
                switch (c) {
                    case '"': oss << "\\\""; break;
                    case '\\': oss << "\\\\"; break;
                    case '\b': oss << "\\b"; break;
                    case '\f': oss << "\\f"; break;
                    case '\n': oss << "\\n"; break;
                    case '\r': oss << "\\r"; break;
                    case '\t': oss << "\\t"; break;
                    default:
                        if (c >= 0 && c < 32) {
                            oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                        } else {
                            oss << c;
                        }
                        break;
                }
            }
            oss << '"';
            return oss.str();
        } else if constexpr (std::is_same_v<T, std::vector<std::shared_ptr<JSONValue>>>) {
            std::ostringstream oss;
            oss << '[';
            for (size_t i = 0; i < v.size(); ++i) {
                if (i > 0) oss << ',';
                oss << serializeJSONValue(*v[i]);
            }
            oss << ']';
            return oss.str();
        } else if constexpr (std::is_same_v<T, std::unordered_map<std::string, std::shared_ptr<JSONValue>>>) {
            std::ostringstream oss;
            oss << '{';
            bool first = true;
            for (const auto& [key, val] : v) {
                if (!first) oss << ',';
                first = false;
                oss << '"' << key << "\":" << serializeJSONValue(*val);
            }
            oss << '}';
            return oss.str();
        } else {
            return "null";
        }
    }, value.get());
    return result;
}

// Simple JSON parsing - basic implementation for testing
JSONValue parseSimpleJSON(const std::string& json) {
    FUNC_SCOPE();
    // Very basic JSON parsing for testing - just handle simple cases
    if (json == "null") {
        return JSONValue(nullptr);
    } else if (json == "true") {
        return JSONValue(true);
    } else if (json == "false") {
        return JSONValue(false);
    } else if (json.front() == '"' && json.back() == '"') {
        return JSONValue(json.substr(1, json.length() - 2));
    } else {
        try {
            if (json.find('.') != std::string::npos) {
                return JSONValue(std::stod(json));
            } else {
                return JSONValue(static_cast<int64_t>(std::stoll(json)));
            }
        } catch (...) {
            return JSONValue(nullptr);
        }
    }
}

// JSONRPCRequest implementation
std::string JSONRPCRequest::Serialize() const {
    FUNC_SCOPE();
    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"" << jsonrpc << "\"";
    
    // Serialize ID
    oss << ",\"id\":";
    std::visit([&oss](const auto& id) {
        using T = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<T, std::string>) {
            oss << '"' << id << '"';
        } else if constexpr (std::is_same_v<T, int64_t>) {
            oss << id;
        } else {
            oss << "null";
        }
    }, id);
    
    oss << ",\"method\":\"" << method << "\"";
    
    if (params.has_value()) {
        oss << ",\"params\":" << serializeJSONValue(params.value());
    }
    
    oss << "}";
    return oss.str();
}

bool JSONRPCRequest::Deserialize(const std::string& json) {
    FUNC_SCOPE();
    try {
        // Basic parsing: extract method and id
        size_t methodPos = json.find("\"method\":");
        if (methodPos != std::string::npos) {
            size_t start = json.find('"', methodPos + 9) + 1;
            size_t end = json.find('"', start);
            if (start != std::string::npos && end != std::string::npos) {
                method = json.substr(start, end - start);
            }
        }

        // Extract id (string, number, or null)
        size_t idPos = json.find("\"id\":");
        if (idPos != std::string::npos) {
            size_t valStart = json.find_first_not_of(" \t\r\n", idPos + 5);
            if (valStart != std::string::npos) {
                char c = json[valStart];
                if (c == '"') {
                    size_t s = valStart + 1;
                    size_t e = json.find('"', s);
                    if (e != std::string::npos) {
                        id = json.substr(s, e - s);
                    }
                } else if ((c == '-') || (c >= '0' && c <= '9')) {
                    size_t e = valStart;
                    while (e < json.size() && (json[e] == '-' || (json[e] >= '0' && json[e] <= '9'))) ++e;
                    try {
                        id = static_cast<int64_t>(std::stoll(json.substr(valStart, e - valStart)));
                    } catch (...) {
                        id = nullptr;
                    }
                } else if (json.compare(valStart, 4, "null") == 0) {
                    id = nullptr;
                }
            }
        }

        // Extract params if present
        size_t paramsPos = json.find("\"params\":");
        if (paramsPos != std::string::npos) {
            std::size_t valStart = json.find_first_not_of(" \t\r\n", paramsPos + 9);
            if (valStart != std::string::npos) {
                JsonParser p(json, valStart);
                JSONValue v = p.parseValue();
                params = std::move(v);
            }
        }

        return !method.empty();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to deserialize JSONRPCRequest: {}", e.what());
        return false;
    }
}

// JSONRPCResponse implementation
std::string JSONRPCResponse::Serialize() const {
    FUNC_SCOPE();
    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"" << jsonrpc << "\"";
    
    // Serialize ID
    oss << ",\"id\":";
    std::visit([&oss](const auto& id) {
        using T = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<T, std::string>) {
            oss << '"' << id << '"';
        } else if constexpr (std::is_same_v<T, int64_t>) {
            oss << id;
        } else {
            oss << "null";
        }
    }, id);
    
    if (result.has_value()) {
        oss << ",\"result\":" << serializeJSONValue(result.value());
    }
    
    if (error.has_value()) {
        oss << ",\"error\":" << serializeJSONValue(error.value());
    }
    
    oss << "}";
    return oss.str();
}

bool JSONRPCResponse::Deserialize(const std::string& json) {
    FUNC_SCOPE();
    try {
        // Basic parsing: extract id
        size_t idPos = json.find("\"id\":");
        if (idPos != std::string::npos) {
            size_t valStart = json.find_first_not_of(" \t\r\n", idPos + 5);
            if (valStart != std::string::npos) {
                char c = json[valStart];
                if (c == '"') {
                    size_t s = valStart + 1;
                    size_t e = json.find('"', s);
                    if (e != std::string::npos) {
                        id = json.substr(s, e - s);
                    }
                } else if ((c == '-') || (c >= '0' && c <= '9')) {
                    size_t e = valStart;
                    while (e < json.size() && (json[e] == '-' || (json[e] >= '0' && json[e] <= '9'))) ++e;
                    try {
                        id = static_cast<int64_t>(std::stoll(json.substr(valStart, e - valStart)));
                    } catch (...) {
                        id = nullptr;
                    }
                } else if (json.compare(valStart, 4, "null") == 0) {
                    id = nullptr;
                }
            }
        }

        // Parse result if present
        size_t resultPos = json.find("\"result\":");
        if (resultPos != std::string::npos) {
            std::size_t valStart = json.find_first_not_of(" \t\r\n", resultPos + 9);
            if (valStart != std::string::npos) {
                JsonParser p(json, valStart);
                JSONValue v = p.parseValue();
                result = std::move(v);
            }
        }

        // Parse error if present
        size_t errorPos = json.find("\"error\":");
        if (errorPos != std::string::npos) {
            std::size_t valStart = json.find_first_not_of(" \t\r\n", errorPos + 8);
            if (valStart != std::string::npos) {
                JsonParser p(json, valStart);
                JSONValue v = p.parseValue();
                error = std::move(v);
            }
        }

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to deserialize JSONRPCResponse: {}", e.what());
        return false;
    }
}

// JSONRPCNotification implementation
std::string JSONRPCNotification::Serialize() const {
    FUNC_SCOPE();
    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"" << jsonrpc << "\"";
    oss << ",\"method\":\"" << method << "\"";
    
    if (params.has_value()) {
        oss << ",\"params\":" << serializeJSONValue(params.value());
    }
    
    oss << "}";
    return oss.str();
}

bool JSONRPCNotification::Deserialize(const std::string& json) {
    FUNC_SCOPE();
    try {
        // Extract method
        size_t methodPos = json.find("\"method\":");
        if (methodPos != std::string::npos) {
            size_t start = json.find('"', methodPos + 9) + 1;
            size_t end = json.find('"', start);
            if (start != std::string::npos && end != std::string::npos) {
                method = json.substr(start, end - start);
            }
        }

        // Parse params if present
        size_t paramsPos = json.find("\"params\":");
        if (paramsPos != std::string::npos) {
            std::size_t valStart = json.find_first_not_of(" \t\r\n", paramsPos + 9);
            if (valStart != std::string::npos) {
                JsonParser p(json, valStart);
                JSONValue v = p.parseValue();
                params = std::move(v);
            }
        }

        return !method.empty();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to deserialize JSONRPCNotification: {}", e.what());
        return false;
    }
}

// Utility functions
JSONValue CreateErrorObject(int code, const std::string& message, 
                           const std::optional<JSONValue>& data) {
    FUNC_SCOPE();
    std::unordered_map<std::string, std::shared_ptr<JSONValue>> errorObj;
    errorObj["code"] = std::make_shared<JSONValue>(static_cast<int64_t>(code));
    errorObj["message"] = std::make_shared<JSONValue>(message);
    
    if (data.has_value()) {
        errorObj["data"] = std::make_shared<JSONValue>(data.value());
    }
    
    return JSONValue(errorObj);
}

std::unique_ptr<JSONRPCResponse> CreateErrorResponse(
    const JSONRPCId& id, int code, const std::string& message,
    const std::optional<JSONValue>& data) {
    FUNC_SCOPE();
    
    auto response = std::make_unique<JSONRPCResponse>();
    response->id = id;
    response->error = CreateErrorObject(code, message, data);
    return response;
}

} // namespace mcp
