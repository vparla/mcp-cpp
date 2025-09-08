//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Content.h
// Purpose: Helpers for constructing and extracting typed content (e.g., text) from MCP results
//==========================================================================================================

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "mcp/Protocol.h"

namespace mcp {
namespace typed {

//------------------------------ Builders ------------------------------
inline JSONValue makeText(const std::string& text) {
    JSONValue::Object obj;
    obj["type"] = std::make_shared<JSONValue>(std::string("text"));
    obj["text"] = std::make_shared<JSONValue>(text);
    return JSONValue{obj};
}

//------------------------------ Inspectors ------------------------------
inline bool isText(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto itType = o.find("type");
    if (itType == o.end() || !itType->second) return false;
    if (!std::holds_alternative<std::string>(itType->second->value)) return false;
    return std::get<std::string>(itType->second->value) == std::string("text");
}

inline std::optional<std::string> getText(const JSONValue& v) {
    if (!isText(v)) return std::nullopt;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("text");
    if (it == o.end() || !it->second) return std::nullopt;
    if (!std::holds_alternative<std::string>(it->second->value)) return std::nullopt;
    return std::get<std::string>(it->second->value);
}

inline std::vector<std::string> collectText(const std::vector<JSONValue>& arr) {
    std::vector<std::string> out;
    out.reserve(arr.size());
    for (const auto& v : arr) {
        auto t = getText(v);
        if (t.has_value()) out.push_back(t.value());
    }
    return out;
}

//------------------------------ From results ------------------------------
inline std::vector<std::string> collectText(const CallToolResult& r) {
    return collectText(r.content);
}

inline std::vector<std::string> collectText(const ReadResourceResult& r) {
    return collectText(r.contents);
}

inline std::vector<std::string> collectText(const GetPromptResult& r) {
    return collectText(r.messages);
}

inline std::optional<std::string> firstText(const CallToolResult& r) {
    auto v = collectText(r);
    if (v.empty()) return std::nullopt;
    return v.front();
}

inline std::optional<std::string> firstText(const ReadResourceResult& r) {
    auto v = collectText(r);
    if (v.empty()) return std::nullopt;
    return v.front();
}

inline std::optional<std::string> firstText(const GetPromptResult& r) {
    auto v = collectText(r);
    if (v.empty()) return std::nullopt;
    return v.front();
}

} // namespace typed
} // namespace mcp
