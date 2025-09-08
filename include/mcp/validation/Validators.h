//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Validators.h
// Purpose: Lightweight validators for common MCP response shapes
//==========================================================================================================

#pragma once

#include <string>
#include <optional>
#include <vector>
#include "mcp/Protocol.h"

namespace mcp {
namespace validation {

//------------------------------ Primitive content checks ------------------------------
inline bool isTextContentItem(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto itType = o.find("type");
    if (itType == o.end() || !itType->second) return false;
    if (!std::holds_alternative<std::string>(itType->second->value)) return false;
    if (std::get<std::string>(itType->second->value) != std::string("text")) return false;
    auto itText = o.find("text");
    if (itText == o.end() || !itText->second) return false;
    if (!std::holds_alternative<std::string>(itText->second->value)) return false;
    return true;
}

inline bool isContentArrayOfText(const std::vector<JSONValue>& arr) {
    for (const auto& item : arr) {
        if (!isTextContentItem(item)) return false;
    }
    return true;
}

//------------------------------ JSON validators (for client-side raw JSON) ------------------------------
inline bool validateCallToolResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto it = obj.find("content");
    if (it == obj.end() || !it->second) return false;
    if (!std::holds_alternative<JSONValue::Array>(it->second->value)) return false;
    const auto& arr = std::get<JSONValue::Array>(it->second->value);
    for (const auto& p : arr) {
        if (!p) return false;
        if (!isTextContentItem(*p)) return false;
    }
    return true;
}

inline bool validateReadResourceResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto it = obj.find("contents");
    if (it == obj.end() || !it->second) return false;
    if (!std::holds_alternative<JSONValue::Array>(it->second->value)) return false;
    const auto& arr = std::get<JSONValue::Array>(it->second->value);
    for (const auto& p : arr) {
        if (!p) return false;
        if (!isTextContentItem(*p)) return false;
    }
    return true;
}

inline bool validateGetPromptResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto d = obj.find("description");
    if (d == obj.end() || !d->second || !std::holds_alternative<std::string>(d->second->value)) return false;
    auto m = obj.find("messages");
    if (m == obj.end() || !m->second || !std::holds_alternative<JSONValue::Array>(m->second->value)) return false;
    const auto& arr = std::get<JSONValue::Array>(m->second->value);
    for (const auto& p : arr) {
        if (!p) return false;
        if (!isTextContentItem(*p)) return false;
    }
    return true;
}

//------------------------------ Typed struct validators (for server-side before serialize) --------------
inline bool validateCallToolResult(const CallToolResult& r) {
    return isContentArrayOfText(r.content);
}

inline bool validateReadResourceResult(const ReadResourceResult& r) {
    return isContentArrayOfText(r.contents);
}

inline bool validateGetPromptResult(const GetPromptResult& r) {
    // description can be empty but must be present when serialized by server logic
    return isContentArrayOfText(r.messages);
}

} // namespace validation
} // namespace mcp
