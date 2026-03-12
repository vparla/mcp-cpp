//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: include/mcp/validation/Validators.h
// Purpose: Lightweight validators for common MCP request and response shapes
//==========================================================================================================

#pragma once

#include <string>
#include <optional>
#include <vector>
#include "mcp/Protocol.h"

namespace mcp {
namespace validation {

//------------------------------ Primitive content checks ------------------------------
inline bool isObject(const JSONValue& v) {
    return std::holds_alternative<JSONValue::Object>(v.value);
}

inline bool isStringField(const JSONValue::Object& obj, const char* key) {
    auto it = obj.find(key);
    return it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value);
}

inline bool isOptionalStringField(const JSONValue::Object& obj, const char* key) {
    auto it = obj.find(key);
    return it == obj.end() || (it->second && std::holds_alternative<std::string>(it->second->value));
}

inline bool isOptionalObjectField(const JSONValue::Object& obj, const char* key) {
    auto it = obj.find(key);
    return it == obj.end() || (it->second && std::holds_alternative<JSONValue::Object>(it->second->value));
}

inline bool isOptionalBoolField(const JSONValue::Object& obj, const char* key) {
    auto it = obj.find(key);
    return it == obj.end() || (it->second && std::holds_alternative<bool>(it->second->value));
}

inline bool isOptionalIntegerField(const JSONValue::Object& obj, const char* key) {
    auto it = obj.find(key);
    return it == obj.end() || (it->second && std::holds_alternative<int64_t>(it->second->value));
}

inline bool isStringArray(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Array>(v.value)) {
        return false;
    }
    const auto& arr = std::get<JSONValue::Array>(v.value);
    for (const auto& item : arr) {
        if (!item || !std::holds_alternative<std::string>(item->value)) {
            return false;
        }
    }
    return true;
}

inline bool isIconObject(const JSONValue& v) {
    if (!isObject(v)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    if (!isStringField(obj, "src")) {
        return false;
    }
    if (!isOptionalStringField(obj, "mimeType")) {
        return false;
    }
    auto sizesIt = obj.find("sizes");
    if (sizesIt != obj.end()) {
        if (!sizesIt->second || !isStringArray(*sizesIt->second)) {
            return false;
        }
    }
    auto themeIt = obj.find("theme");
    if (themeIt != obj.end()) {
        if (!themeIt->second || !std::holds_alternative<std::string>(themeIt->second->value)) {
            return false;
        }
        const auto& theme = std::get<std::string>(themeIt->second->value);
        if (theme != std::string("light") && theme != std::string("dark")) {
            return false;
        }
    }
    return true;
}

inline bool isOptionalIconsField(const JSONValue::Object& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return true;
    }
    if (!it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) {
        return false;
    }
    const auto& arr = std::get<JSONValue::Array>(it->second->value);
    for (const auto& item : arr) {
        if (!item || !isIconObject(*item)) {
            return false;
        }
    }
    return true;
}

inline bool isAnnotationsShape(const JSONValue& v) {
    if (!isObject(v)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto audienceIt = obj.find("audience");
    if (audienceIt != obj.end()) {
        if (!audienceIt->second || !std::holds_alternative<JSONValue::Array>(audienceIt->second->value)) {
            return false;
        }
        const auto& arr = std::get<JSONValue::Array>(audienceIt->second->value);
        for (const auto& item : arr) {
            if (!item || !std::holds_alternative<std::string>(item->value)) {
                return false;
            }
        }
    }
    auto priorityIt = obj.find("priority");
    if (priorityIt != obj.end()) {
        if (!priorityIt->second) {
            return false;
        }
        const bool numeric = std::holds_alternative<int64_t>(priorityIt->second->value) ||
                             std::holds_alternative<double>(priorityIt->second->value);
        if (!numeric) {
            return false;
        }
    }
    return true;
}

inline bool isOptionalAnnotationsField(const JSONValue::Object& obj, const char* key) {
    auto it = obj.find(key);
    return it == obj.end() || (it->second && isAnnotationsShape(*it->second));
}

inline bool isEmbeddedResourceObject(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& o = std::get<JSONValue::Object>(v.value);
    if (!isStringField(o, "uri")) {
        return false;
    }
    const bool hasText = isStringField(o, "text");
    const bool hasBlob = isStringField(o, "blob");
    if (hasText == hasBlob) {
        return false;
    }
    if (!isOptionalStringField(o, "mimeType")) {
        return false;
    }
    return true;
}

inline bool isContentItem(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto itType = o.find("type");
    if (itType == o.end() || !itType->second || !std::holds_alternative<std::string>(itType->second->value)) {
        return false;
    }
    const auto& type = std::get<std::string>(itType->second->value);
    if (type == std::string("text")) {
        if (!isStringField(o, "text")) {
            return false;
        }
    } else if (type == std::string("image") || type == std::string("audio")) {
        if (!isStringField(o, "mimeType") || !isStringField(o, "data")) {
            return false;
        }
    } else if (type == std::string("resource_link")) {
        if (!isStringField(o, "uri") || !isStringField(o, "name")) {
            return false;
        }
        if (!isOptionalStringField(o, "title") ||
            !isOptionalStringField(o, "description") ||
            !isOptionalStringField(o, "mimeType") ||
            !isOptionalIntegerField(o, "size")) {
            return false;
        }
    } else if (type == std::string("resource")) {
        auto itRes = o.find("resource");
        if (itRes == o.end() || !itRes->second || !isEmbeddedResourceObject(*itRes->second)) {
            return false;
        }
    } else {
        return false;
    }
    if (!isOptionalAnnotationsField(o, "annotations")) {
        return false;
    }
    return true;
}

inline bool isTextContentItem(const JSONValue& v) {
    if (!isContentItem(v)) {
        return false;
    }
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto itType = o.find("type");
    return itType != o.end() && itType->second &&
           std::holds_alternative<std::string>(itType->second->value) &&
           std::get<std::string>(itType->second->value) == std::string("text");
}

inline bool isContentArray(const std::vector<JSONValue>& arr) {
    for (const auto& item : arr) {
        if (!isContentItem(item)) {
            return false;
        }
    }
    return true;
}

inline bool isPromptMessageItem(const JSONValue& v) {
    if (isContentItem(v)) {
        return true;
    }
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    if (!isStringField(obj, "role")) {
        return false;
    }
    auto contentIt = obj.find("content");
    if (contentIt == obj.end() || !contentIt->second || !std::holds_alternative<JSONValue::Array>(contentIt->second->value)) {
        return false;
    }
    const auto& arr = std::get<JSONValue::Array>(contentIt->second->value);
    for (const auto& item : arr) {
        if (!item || !isContentItem(*item)) {
            return false;
        }
    }
    return true;
}

//------------------------------ JSON validators (for client-side raw JSON) ------------------------------
inline bool validateCallToolResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto it = obj.find("content");
    if (it == obj.end() || !it->second) {
        return false;
    }
    if (!std::holds_alternative<JSONValue::Array>(it->second->value)) {
        return false;
    }
    const auto& arr = std::get<JSONValue::Array>(it->second->value);
    for (const auto& p : arr) {
        if (!p) {
            return false;
        }
        if (!isContentItem(*p)) {
            return false;
        }
    }
    auto isErrorIt = obj.find("isError");
    if (isErrorIt != obj.end() && (!isErrorIt->second || !std::holds_alternative<bool>(isErrorIt->second->value))) {
        return false;
    }
    auto structuredIt = obj.find("structuredContent");
    if (structuredIt != obj.end() && !structuredIt->second) {
        return false;
    }
    auto metaIt = obj.find("_meta");
    if (metaIt != obj.end() && !metaIt->second) {
        return false;
    }
    return true;
}

inline bool validateReadResourceResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto it = obj.find("contents");
    if (it == obj.end() || !it->second) {
        return false;
    }
    if (!std::holds_alternative<JSONValue::Array>(it->second->value)) {
        return false;
    }
    const auto& arr = std::get<JSONValue::Array>(it->second->value);
    for (const auto& p : arr) {
        if (!p) {
            return false;
        }
        if (!isContentItem(*p)) {
            return false;
        }
    }
    auto metaIt = obj.find("_meta");
    if (metaIt != obj.end() && !metaIt->second) {
        return false;
    }
    return true;
}

inline bool validateGetPromptResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto d = obj.find("description");
    if (d == obj.end() || !d->second || !std::holds_alternative<std::string>(d->second->value)) {
        return false;
    }
    auto m = obj.find("messages");
    if (m == obj.end() || !m->second || !std::holds_alternative<JSONValue::Array>(m->second->value)) {
        return false;
    }
    const auto& arr = std::get<JSONValue::Array>(m->second->value);
    for (const auto& p : arr) {
        if (!p) {
            return false;
        }
        if (!isPromptMessageItem(*p)) {
            return false;
        }
    }
    return true;
}

inline bool validateCompletionResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto completionIt = obj.find("completion");
    if (completionIt == obj.end() || !completionIt->second || !std::holds_alternative<JSONValue::Object>(completionIt->second->value)) {
        return false;
    }
    const auto& completionObj = std::get<JSONValue::Object>(completionIt->second->value);
    auto valuesIt = completionObj.find("values");
    if (valuesIt == completionObj.end() || !valuesIt->second || !std::holds_alternative<JSONValue::Array>(valuesIt->second->value)) {
        return false;
    }
    const auto& values = std::get<JSONValue::Array>(valuesIt->second->value);
    for (const auto& item : values) {
        if (!item || !std::holds_alternative<std::string>(item->value)) {
            return false;
        }
    }
    if (!isOptionalIntegerField(completionObj, "total")) {
        return false;
    }
    if (!isOptionalBoolField(completionObj, "hasMore")) {
        return false;
    }
    return true;
}

inline bool validateElicitationRequestJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    if (!isStringField(obj, "message")) {
        return false;
    }
    auto schemaIt = obj.find("requestedSchema");
    if (schemaIt == obj.end() || !schemaIt->second) {
        return false;
    }
    if (!isOptionalStringField(obj, "title") ||
        !isOptionalStringField(obj, "mode") ||
        !isOptionalStringField(obj, "url") ||
        !isOptionalStringField(obj, "elicitationId")) {
        return false;
    }
    auto metadataIt = obj.find("metadata");
    if (metadataIt != obj.end() && !metadataIt->second) {
        return false;
    }
    return true;
}

inline bool validateElicitationResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    if (!isStringField(obj, "action")) {
        return false;
    }
    const auto& action = std::get<std::string>(obj.at("action")->value);
    if (action != std::string("accept") &&
        action != std::string("decline") &&
        action != std::string("cancel")) {
        return false;
    }
    auto contentIt = obj.find("content");
    if (contentIt != obj.end() && !contentIt->second) {
        return false;
    }
    if (!isOptionalStringField(obj, "elicitationId")) {
        return false;
    }
    return true;
}

inline bool validatePingResultJson(const JSONValue& v) {
    return std::holds_alternative<JSONValue::Object>(v.value);
}

inline bool validateTaskJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    if (!isStringField(obj, "taskId") ||
        !isStringField(obj, "status") ||
        !isStringField(obj, "createdAt") ||
        !isStringField(obj, "lastUpdatedAt")) {
        return false;
    }
    if (!isOptionalStringField(obj, "statusMessage") ||
        !isOptionalIntegerField(obj, "pollInterval")) {
        return false;
    }
    auto ttlIt = obj.find("ttl");
    if (ttlIt != obj.end() && ttlIt->second &&
        !std::holds_alternative<int64_t>(ttlIt->second->value) &&
        !std::holds_alternative<std::nullptr_t>(ttlIt->second->value)) {
        return false;
    }
    return true;
}

inline bool validateCreateTaskResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto taskIt = obj.find("task");
    if (taskIt == obj.end() || !taskIt->second || !validateTaskJson(*taskIt->second)) {
        return false;
    }
    auto metaIt = obj.find("_meta");
    if (metaIt != obj.end() && !metaIt->second) {
        return false;
    }
    return true;
}

inline bool validateTasksListResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto tasksIt = obj.find("tasks");
    if (tasksIt == obj.end() || !tasksIt->second ||
        !std::holds_alternative<JSONValue::Array>(tasksIt->second->value)) {
        return false;
    }
    const auto& arr = std::get<JSONValue::Array>(tasksIt->second->value);
    for (const auto& item : arr) {
        if (!item || !validateTaskJson(*item)) {
            return false;
        }
    }
    if (!isOptionalStringField(obj, "nextCursor")) {
        return false;
    }
    return true;
}

inline bool validateTaskStatusNotificationParamsJson(const JSONValue& v) {
    return validateTaskJson(v);
}

//------------------------------ List endpoints (JSON validators) ------------------------------------------
inline bool validateToolsListResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("tools");
    if (it == o.end() || !it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) {
        return false;
    }
    const auto& a = std::get<JSONValue::Array>(it->second->value);
    for (const auto& e : a) {
        if (!e || !std::holds_alternative<JSONValue::Object>(e->value)) {
            return false;
        }
        const auto& to = std::get<JSONValue::Object>(e->value);
        auto n = to.find("name"); if (n == to.end() || !n->second || !std::holds_alternative<std::string>(n->second->value)) { return false; }
        if (!isOptionalStringField(to, "title")) { return false; }
        auto d = to.find("description"); if (d != to.end() && (!d->second || !std::holds_alternative<std::string>(d->second->value))) { return false; }
        // inputSchema may be any JSONValue; if present ensure pointer exists
        auto s = to.find("inputSchema"); if (s != to.end() && !s->second) { return false; }
        auto os = to.find("outputSchema"); if (os != to.end() && !os->second) { return false; }
        if (!isOptionalAnnotationsField(to, "annotations")) { return false; }
        auto ex = to.find("execution"); if (ex != to.end() && !ex->second) { return false; }
        auto meta = to.find("_meta"); if (meta != to.end() && !meta->second) { return false; }
        if (!isOptionalIconsField(to, "icons")) { return false; }
    }
    auto nc = o.find("nextCursor");
    if (nc != o.end()) {
        if (!nc->second) { return false; }
        if (!std::holds_alternative<std::string>(nc->second->value) && !std::holds_alternative<int64_t>(nc->second->value)) { return false; }
    }
    return true;
}

inline bool validateResourcesListResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("resources");
    if (it == o.end() || !it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) {
        return false;
    }
    const auto& a = std::get<JSONValue::Array>(it->second->value);
    for (const auto& e : a) {
        if (!e || !std::holds_alternative<JSONValue::Object>(e->value)) {
            return false;
        }
        const auto& ro = std::get<JSONValue::Object>(e->value);
        auto u = ro.find("uri"); if (u == ro.end() || !u->second || !std::holds_alternative<std::string>(u->second->value)) { return false; }
        auto n = ro.find("name"); if (n == ro.end() || !n->second || !std::holds_alternative<std::string>(n->second->value)) { return false; }
        if (!isOptionalStringField(ro, "title")) { return false; }
        auto d = ro.find("description"); if (d != ro.end() && (!d->second || !std::holds_alternative<std::string>(d->second->value))) { return false; }
        auto m = ro.find("mimeType"); if (m != ro.end() && (!m->second || !std::holds_alternative<std::string>(m->second->value))) { return false; }
        if (!isOptionalIntegerField(ro, "size")) { return false; }
        if (!isOptionalAnnotationsField(ro, "annotations")) { return false; }
        auto meta = ro.find("_meta"); if (meta != ro.end() && !meta->second) { return false; }
        if (!isOptionalIconsField(ro, "icons")) { return false; }
    }
    auto nc = o.find("nextCursor");
    if (nc != o.end()) {
        if (!nc->second) { return false; }
        if (!std::holds_alternative<std::string>(nc->second->value) && !std::holds_alternative<int64_t>(nc->second->value)) { return false; }
    }
    return true;
}

inline bool validateResourceTemplatesListResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("resourceTemplates");
    if (it == o.end() || !it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) {
        return false;
    }
    const auto& a = std::get<JSONValue::Array>(it->second->value);
    for (const auto& e : a) {
        if (!e || !std::holds_alternative<JSONValue::Object>(e->value)) {
            return false;
        }
        const auto& rt = std::get<JSONValue::Object>(e->value);
        auto u = rt.find("uriTemplate"); if (u == rt.end() || !u->second || !std::holds_alternative<std::string>(u->second->value)) { return false; }
        auto n = rt.find("name"); if (n == rt.end() || !n->second || !std::holds_alternative<std::string>(n->second->value)) { return false; }
        if (!isOptionalStringField(rt, "title")) { return false; }
        auto d = rt.find("description"); if (d != rt.end() && (!d->second || !std::holds_alternative<std::string>(d->second->value))) { return false; }
        auto m = rt.find("mimeType"); if (m != rt.end() && (!m->second || !std::holds_alternative<std::string>(m->second->value))) { return false; }
        if (!isOptionalAnnotationsField(rt, "annotations")) { return false; }
        auto meta = rt.find("_meta"); if (meta != rt.end() && !meta->second) { return false; }
        if (!isOptionalIconsField(rt, "icons")) { return false; }
    }
    auto nc = o.find("nextCursor");
    if (nc != o.end()) {
        if (!nc->second) { return false; }
        if (!std::holds_alternative<std::string>(nc->second->value) && !std::holds_alternative<int64_t>(nc->second->value)) { return false; }
    }
    return true;
}

inline bool validatePromptsListResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("prompts");
    if (it == o.end() || !it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) {
        return false;
    }
    const auto& a = std::get<JSONValue::Array>(it->second->value);
    for (const auto& e : a) {
        if (!e || !std::holds_alternative<JSONValue::Object>(e->value)) {
            return false;
        }
        const auto& pr = std::get<JSONValue::Object>(e->value);
        auto n = pr.find("name"); if (n == pr.end() || !n->second || !std::holds_alternative<std::string>(n->second->value)) { return false; }
        if (!isOptionalStringField(pr, "title")) { return false; }
        auto d = pr.find("description"); if (d != pr.end() && (!d->second || !std::holds_alternative<std::string>(d->second->value))) { return false; }
        // arguments optional, any JSON
        auto args = pr.find("arguments"); if (args != pr.end() && !args->second) { return false; }
        auto meta = pr.find("_meta"); if (meta != pr.end() && !meta->second) { return false; }
        if (!isOptionalIconsField(pr, "icons")) { return false; }
    }
    auto nc = o.find("nextCursor");
    if (nc != o.end()) {
        if (!nc->second) { return false; }
        if (!std::holds_alternative<std::string>(nc->second->value) && !std::holds_alternative<int64_t>(nc->second->value)) { return false; }
    }
    return true;
}

inline bool validateRootsListResultJson(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) {
        return false;
    }
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto it = o.find("roots");
    if (it == o.end() || !it->second || !std::holds_alternative<JSONValue::Array>(it->second->value)) {
        return false;
    }
    const auto& a = std::get<JSONValue::Array>(it->second->value);
    for (const auto& e : a) {
        if (!e || !std::holds_alternative<JSONValue::Object>(e->value)) {
            return false;
        }
        const auto& ro = std::get<JSONValue::Object>(e->value);
        auto u = ro.find("uri"); if (u == ro.end() || !u->second || !std::holds_alternative<std::string>(u->second->value)) { return false; }
        auto n = ro.find("name"); if (n != ro.end() && (!n->second || !std::holds_alternative<std::string>(n->second->value))) { return false; }
        auto m = ro.find("_meta"); if (m != ro.end() && !m->second) { return false; }
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
                    if (!isContentItem(*ci)) return false;
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
        if (!isContentItem(*ci)) return false;
    }
    // stopReason optional
    return true;
}

//------------------------------ Typed struct validators (for server-side before serialize) --------------
inline bool validateCallToolResult(const CallToolResult& r) {
    return isContentArray(r.content);
}

inline bool validateReadResourceResult(const ReadResourceResult& r) {
    return isContentArray(r.contents);
}

inline bool validateGetPromptResult(const GetPromptResult& r) {
    // description can be empty but must be present when serialized by server logic
    for (const auto& item : r.messages) {
        if (!isPromptMessageItem(item)) {
            return false;
        }
    }
    return true;
}

} // namespace validation
} // namespace mcp
