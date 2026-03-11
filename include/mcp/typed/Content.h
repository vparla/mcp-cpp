//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: include/mcp/typed/Content.h
// Purpose: Helpers for constructing and extracting typed MCP content blocks
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

inline JSONValue makeImage(const std::string& mimeType, const std::string& data) {
    JSONValue::Object obj;
    obj["type"] = std::make_shared<JSONValue>(std::string("image"));
    obj["mimeType"] = std::make_shared<JSONValue>(mimeType);
    obj["data"] = std::make_shared<JSONValue>(data);
    return JSONValue{obj};
}

inline JSONValue makeAudio(const std::string& mimeType, const std::string& data) {
    JSONValue::Object obj;
    obj["type"] = std::make_shared<JSONValue>(std::string("audio"));
    obj["mimeType"] = std::make_shared<JSONValue>(mimeType);
    obj["data"] = std::make_shared<JSONValue>(data);
    return JSONValue{obj};
}

inline JSONValue makeResourceLink(const std::string& uri,
                                  const std::string& name,
                                  const std::optional<std::string>& title = std::nullopt,
                                  const std::optional<std::string>& description = std::nullopt,
                                  const std::optional<std::string>& mimeType = std::nullopt) {
    JSONValue::Object obj;
    obj["type"] = std::make_shared<JSONValue>(std::string("resource_link"));
    obj["uri"] = std::make_shared<JSONValue>(uri);
    obj["name"] = std::make_shared<JSONValue>(name);
    if (title.has_value()) {
        obj["title"] = std::make_shared<JSONValue>(title.value());
    }
    if (description.has_value()) {
        obj["description"] = std::make_shared<JSONValue>(description.value());
    }
    if (mimeType.has_value()) {
        obj["mimeType"] = std::make_shared<JSONValue>(mimeType.value());
    }
    return JSONValue{obj};
}

inline JSONValue makeEmbeddedTextResource(const std::string& uri,
                                          const std::string& text,
                                          const std::optional<std::string>& mimeType = std::nullopt) {
    JSONValue::Object resource;
    resource["uri"] = std::make_shared<JSONValue>(uri);
    resource["text"] = std::make_shared<JSONValue>(text);
    if (mimeType.has_value()) {
        resource["mimeType"] = std::make_shared<JSONValue>(mimeType.value());
    }

    JSONValue::Object obj;
    obj["type"] = std::make_shared<JSONValue>(std::string("resource"));
    obj["resource"] = std::make_shared<JSONValue>(JSONValue{resource});
    return JSONValue{obj};
}

inline JSONValue makeEmbeddedBlobResource(const std::string& uri,
                                          const std::string& blob,
                                          const std::string& mimeType) {
    JSONValue::Object resource;
    resource["uri"] = std::make_shared<JSONValue>(uri);
    resource["blob"] = std::make_shared<JSONValue>(blob);
    resource["mimeType"] = std::make_shared<JSONValue>(mimeType);

    JSONValue::Object obj;
    obj["type"] = std::make_shared<JSONValue>(std::string("resource"));
    obj["resource"] = std::make_shared<JSONValue>(JSONValue{resource});
    return JSONValue{obj};
}

//------------------------------ Inspectors ------------------------------
inline std::optional<std::string> getType(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return std::nullopt;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto itType = o.find("type");
    if (itType == o.end() || !itType->second) return std::nullopt;
    if (!std::holds_alternative<std::string>(itType->second->value)) return std::nullopt;
    return std::get<std::string>(itType->second->value);
}

inline bool isText(const JSONValue& v) {
    auto type = getType(v);
    return type.has_value() && type.value() == std::string("text");
}

inline bool isImage(const JSONValue& v) {
    auto type = getType(v);
    return type.has_value() && type.value() == std::string("image");
}

inline bool isAudio(const JSONValue& v) {
    auto type = getType(v);
    return type.has_value() && type.value() == std::string("audio");
}

inline bool isResourceLink(const JSONValue& v) {
    auto type = getType(v);
    return type.has_value() && type.value() == std::string("resource_link");
}

inline bool isEmbeddedResource(const JSONValue& v) {
    auto type = getType(v);
    return type.has_value() && type.value() == std::string("resource");
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
