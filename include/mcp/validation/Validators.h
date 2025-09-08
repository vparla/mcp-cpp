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

//------------------------------ List endpoints (JSON validators) ------------------------------------------
inline bool validateToolsListResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("tools");
    if (it == o.end() || !it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) return false;
    const auto& a = std::get<JSONValue::Array>(it->second->value);
    for (const auto& e : a) {
        if (!e || !std::holds_alternative<JSONValue::Object>(e->value)) return false;
        const auto& to = std::get<JSONValue::Object>(e->value);
        auto n = to.find("name"); if (n == to.end() || !n->second || !std::holds_alternative<std::string>(n->second->value)) return false;
        auto d = to.find("description"); if (d == to.end() || !d->second || !std::holds_alternative<std::string>(d->second->value)) return false;
        // inputSchema may be any JSONValue; if present ensure pointer exists
        auto s = to.find("inputSchema"); if (s != to.end() && !s->second) return false;
    }
    auto nc = o.find("nextCursor");
    if (nc != o.end()) {
        if (!nc->second) return false;
        if (!std::holds_alternative<std::string>(nc->second->value) && !std::holds_alternative<int64_t>(nc->second->value)) return false;
    }
    return true;
}

inline bool validateResourcesListResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("resources");
    if (it == o.end() || !it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) return false;
    const auto& a = std::get<JSONValue::Array>(it->second->value);
    for (const auto& e : a) {
        if (!e || !std::holds_alternative<JSONValue::Object>(e->value)) return false;
        const auto& ro = std::get<JSONValue::Object>(e->value);
        auto u = ro.find("uri"); if (u == ro.end() || !u->second || !std::holds_alternative<std::string>(u->second->value)) return false;
        auto n = ro.find("name"); if (n == ro.end() || !n->second || !std::holds_alternative<std::string>(n->second->value)) return false;
        auto d = ro.find("description"); if (d != ro.end() && (!d->second || !std::holds_alternative<std::string>(d->second->value))) return false;
        auto m = ro.find("mimeType"); if (m != ro.end() && (!m->second || !std::holds_alternative<std::string>(m->second->value))) return false;
    }
    auto nc = o.find("nextCursor");
    if (nc != o.end()) {
        if (!nc->second) return false;
        if (!std::holds_alternative<std::string>(nc->second->value) && !std::holds_alternative<int64_t>(nc->second->value)) return false;
    }
    return true;
}

inline bool validateResourceTemplatesListResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("resourceTemplates");
    if (it == o.end() || !it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) return false;
    const auto& a = std::get<JSONValue::Array>(it->second->value);
    for (const auto& e : a) {
        if (!e || !std::holds_alternative<JSONValue::Object>(e->value)) return false;
        const auto& rt = std::get<JSONValue::Object>(e->value);
        auto u = rt.find("uriTemplate"); if (u == rt.end() || !u->second || !std::holds_alternative<std::string>(u->second->value)) return false;
        auto n = rt.find("name"); if (n == rt.end() || !n->second || !std::holds_alternative<std::string>(n->second->value)) return false;
        auto d = rt.find("description"); if (d != rt.end() && (!d->second || !std::holds_alternative<std::string>(d->second->value))) return false;
        auto m = rt.find("mimeType"); if (m != rt.end() && (!m->second || !std::holds_alternative<std::string>(m->second->value))) return false;
    }
    auto nc = o.find("nextCursor");
    if (nc != o.end()) {
        if (!nc->second) return false;
        if (!std::holds_alternative<std::string>(nc->second->value) && !std::holds_alternative<int64_t>(nc->second->value)) return false;
    }
    return true;
}

inline bool validatePromptsListResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("prompts");
    if (it == o.end() || !it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) return false;
    const auto& a = std::get<JSONValue::Array>(it->second->value);
    for (const auto& e : a) {
        if (!e || !std::holds_alternative<JSONValue::Object>(e->value)) return false;
        const auto& pr = std::get<JSONValue::Object>(e->value);
        auto n = pr.find("name"); if (n == pr.end() || !n->second || !std::holds_alternative<std::string>(n->second->value)) return false;
        auto d = pr.find("description"); if (d == pr.end() || !d->second || !std::holds_alternative<std::string>(d->second->value)) return false;
        // arguments optional, any JSON
    }
    auto nc = o.find("nextCursor");
    if (nc != o.end()) {
        if (!nc->second) return false;
        if (!std::holds_alternative<std::string>(nc->second->value) && !std::holds_alternative<int64_t>(nc->second->value)) return false;
    }
    return true;
}

//------------------------------ Sampling (server-initiated) ----------------------------------------------
inline bool validateCreateMessageParamsJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("messages");
    if (it == o.end() || !it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) return false;
    const auto& msgs = std::get<JSONValue::Array>(it->second->value);
    for (const auto& m : msgs) {
        if (!m || !std::holds_alternative<JSONValue::Object>(m->value)) return false;
        const auto& mo = std::get<JSONValue::Object>(m->value);
        auto c = mo.find("content");
        if (c != mo.end()) {
            if (!c->second || !std::holds_alternative<JSONValue::Array>(c->second->value)) return false;
            const auto& carr = std::get<JSONValue::Array>(c->second->value);
            for (const auto& ci : carr) {
                if (!ci) return false;
                if (!isTextContentItem(*ci)) return false;
            }
        }
    }
    // modelPreferences/systemPrompt/includeContext are optional, any JSON types accepted
    return true;
}

inline bool validateCreateMessageResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto mdl = o.find("model"); if (mdl == o.end() || !mdl->second || !std::holds_alternative<std::string>(mdl->second->value)) return false;
    auto role = o.find("role"); if (role == o.end() || !role->second || !std::holds_alternative<std::string>(role->second->value)) return false;
    auto cont = o.find("content"); if (cont == o.end() || !cont->second || !std::holds_alternative<JSONValue::Array>(cont->second->value)) return false;
    const auto& arr = std::get<JSONValue::Array>(cont->second->value);
    for (const auto& ci : arr) {
        if (!ci) return false;
        if (!isTextContentItem(*ci)) return false;
    }
    // stopReason optional
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
