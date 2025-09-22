//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: ClientTyped.h
// Purpose: Header-only typed client wrappers for tools/resources/prompts (+ paging helpers)
//==========================================================================================================

#pragma once

#include <future>
#include <string>
#include <vector>

#include "mcp/Client.h"
#include "mcp/Protocol.h"
#include "mcp/errors/Errors.h"
#include "mcp/validation/Validators.h"
#include "logging/Logger.h"

namespace mcp {
namespace typed {

//------------------------------ Internal helpers ------------------------------
inline bool isErrorObject(const JSONValue& v) {
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return false;
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto itCode = o.find("code");
    auto itMsg = o.find("message");
    return itCode != o.end() && itMsg != o.end();
}

inline CallToolResult parseCallToolResult(const JSONValue& v) {
    CallToolResult out;
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return out;
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto it = obj.find("content");
    if (it != obj.end() && std::holds_alternative<JSONValue::Array>(it->second->value)) {
        const auto& arr = std::get<JSONValue::Array>(it->second->value);
        out.content.reserve(arr.size());
        for (const auto& p : arr) { if (p) out.content.push_back(*p); }
    }
    auto e = obj.find("isError");
    if (e != obj.end() && std::holds_alternative<bool>(e->second->value)) {
        out.isError = std::get<bool>(e->second->value);
    }
    return out;
}

inline ReadResourceResult parseReadResourceResult(const JSONValue& v) {
    ReadResourceResult out;
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return out;
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto it = obj.find("contents");
    if (it != obj.end() && std::holds_alternative<JSONValue::Array>(it->second->value)) {
        const auto& arr = std::get<JSONValue::Array>(it->second->value);
        out.contents.reserve(arr.size());
        for (const auto& p : arr) { if (p) out.contents.push_back(*p); }
    }
    return out;
}

inline GetPromptResult parseGetPromptResult(const JSONValue& v) {
    GetPromptResult out;
    if (!std::holds_alternative<JSONValue::Object>(v.value)) return out;
    const auto& obj = std::get<JSONValue::Object>(v.value);
    auto d = obj.find("description");
    if (d != obj.end() && std::holds_alternative<std::string>(d->second->value)) {
        out.description = std::get<std::string>(d->second->value);
    }
    auto m = obj.find("messages");
    if (m != obj.end() && std::holds_alternative<JSONValue::Array>(m->second->value)) {
        const auto& arr = std::get<JSONValue::Array>(m->second->value);
        out.messages.reserve(arr.size());
        for (const auto& p : arr) { if (p) out.messages.push_back(*p); }
    }
    return out;
}

//------------------------------ Typed wrappers ------------------------------
inline std::future<CallToolResult> callTool(IClient& client, const std::string& name, const JSONValue& arguments) {
    return std::async(std::launch::async, [&client, name, arguments]() {
        JSONValue v = client.CallTool(name, arguments).get();
        if (isErrorObject(v)) {
            auto e = errors::mcpErrorFromErrorValue(v);
            if (e.has_value()) {
                LOG_ERROR("tools/call '{}' failed: code={} message={}", name, e->code, e->message);
            } else {
                LOG_ERROR("tools/call '{}' failed with unknown error shape", name);
            }
            throw std::runtime_error(e.has_value() ? e->message : std::string("tools/call error"));
        }
        if (client.GetValidationMode() == validation::ValidationMode::Strict) {
            if (!validation::validateCallToolResultJson(v)) {
                LOG_ERROR("Validation failed (Strict): tools/call result shape for '{}'", name);
                throw std::runtime_error("Validation failed: tools/call result shape");
            }
        }
        return parseCallToolResult(v);
    });
}

inline std::future<ReadResourceResult> readResource(IClient& client, const std::string& uri) {
    return std::async(std::launch::async, [&client, uri]() {
        JSONValue v = client.ReadResource(uri).get();
        if (isErrorObject(v)) {
            auto e = errors::mcpErrorFromErrorValue(v);
            if (e.has_value()) {
                LOG_ERROR("resources/read '{}' failed: code={} message={}", uri, e->code, e->message);
            } else {
                LOG_ERROR("resources/read '{}' failed with unknown error shape", uri);
            }
            throw std::runtime_error(e.has_value() ? e->message : std::string("resources/read error"));
        }
        if (client.GetValidationMode() == validation::ValidationMode::Strict) {
            if (!validation::validateReadResourceResultJson(v)) {
                LOG_ERROR("Validation failed (Strict): resources/read result shape for '{}'", uri);
                throw std::runtime_error("Validation failed: resources/read result shape");
            }
        }
        return parseReadResourceResult(v);
    });
}

inline std::future<GetPromptResult> getPrompt(IClient& client, const std::string& name, const JSONValue& arguments) {
    return std::async(std::launch::async, [&client, name, arguments]() {
        JSONValue v = client.GetPrompt(name, arguments).get();
        if (isErrorObject(v)) {
            auto e = errors::mcpErrorFromErrorValue(v);
            if (e.has_value()) {
                LOG_ERROR("prompts/get '{}' failed: code={} message={}", name, e->code, e->message);
            } else {
                LOG_ERROR("prompts/get '{}' failed with unknown error shape", name);
            }
            throw std::runtime_error(e.has_value() ? e->message : std::string("prompts/get error"));
        }
        if (client.GetValidationMode() == validation::ValidationMode::Strict) {
            if (!validation::validateGetPromptResultJson(v)) {
                LOG_ERROR("Validation failed (Strict): prompts/get result shape for '{}'", name);
                throw std::runtime_error("Validation failed: prompts/get result shape");
            }
        }
        return parseGetPromptResult(v);
    });
}

//------------------------------ Paging helpers ------------------------------
inline std::future<std::vector<Tool>> listAllTools(IClient& client, const std::optional<int>& pageLimit = std::nullopt) {
    return std::async(std::launch::async, [&client, pageLimit]() {
        std::vector<Tool> all;
        std::optional<std::string> cursor;
        while (true) {
            auto page = client.ListToolsPaged(cursor, pageLimit).get();
            for (auto& t : page.tools) all.push_back(std::move(t));
            if (!page.nextCursor.has_value()) break;
            cursor = page.nextCursor;
        }
        return all;
    });
}

inline std::future<std::vector<Resource>> listAllResources(IClient& client, const std::optional<int>& pageLimit = std::nullopt) {
    return std::async(std::launch::async, [&client, pageLimit]() {
        std::vector<Resource> all;
        std::optional<std::string> cursor;
        while (true) {
            auto page = client.ListResourcesPaged(cursor, pageLimit).get();
            for (auto& r : page.resources) all.push_back(std::move(r));
            if (!page.nextCursor.has_value()) break;
            cursor = page.nextCursor;
        }
        return all;
    });
}

inline std::future<std::vector<ResourceTemplate>> listAllResourceTemplates(IClient& client, const std::optional<int>& pageLimit = std::nullopt) {
    return std::async(std::launch::async, [&client, pageLimit]() {
        std::vector<ResourceTemplate> all;
        std::optional<std::string> cursor;
        while (true) {
            auto page = client.ListResourceTemplatesPaged(cursor, pageLimit).get();
            for (auto& rt : page.resourceTemplates) all.push_back(std::move(rt));
            if (!page.nextCursor.has_value()) break;
            cursor = page.nextCursor;
        }
        return all;
    });
}

inline std::future<std::vector<Prompt>> listAllPrompts(IClient& client, const std::optional<int>& pageLimit = std::nullopt) {
    return std::async(std::launch::async, [&client, pageLimit]() {
        std::vector<Prompt> all;
        std::optional<std::string> cursor;
        while (true) {
            auto page = client.ListPromptsPaged(cursor, pageLimit).get();
            for (auto& p : page.prompts) all.push_back(std::move(p));
            if (!page.nextCursor.has_value()) break;
            cursor = page.nextCursor;
        }
        return all;
    });
}

} // namespace typed
} // namespace mcp
