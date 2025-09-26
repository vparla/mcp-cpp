//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: ClientTyped.h
// Purpose: Header-only typed client wrappers for tools/resources/prompts (+ paging helpers)
//==========================================================================================================

#pragma once

#include <future>
#include <optional>
#include <stdexcept>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <stop_token>

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

// Extracts the server-advertised clamp for resource read chunking, if present.
// Returns maxChunkBytes when capabilities.experimental.resourceReadChunking.maxChunkBytes > 0
// Otherwise returns std::nullopt.
inline std::optional<size_t> extractResourceReadClamp(const ServerCapabilities& scaps) {
    auto it = scaps.experimental.find("resourceReadChunking");
    if (it == scaps.experimental.end()) return std::nullopt;
    if (!std::holds_alternative<JSONValue::Object>(it->second.value)) return std::nullopt;
    const auto& rrc = std::get<JSONValue::Object>(it->second.value);
    auto itMax = rrc.find("maxChunkBytes");
    if (itMax == rrc.end() || !itMax->second) return std::nullopt;
    if (!std::holds_alternative<int64_t>(itMax->second->value)) return std::nullopt;
    int64_t v = std::get<int64_t>(itMax->second->value);
    if (v <= 0) return std::nullopt;
    return static_cast<size_t>(v);
}

// Forward declarations for parsers used below
inline ReadResourceResult parseReadResourceResult(const JSONValue& v);

// Helper executed on background thread for range reads
inline ReadResourceResult doReadResourceRange(IClient& client,
                                              const std::string& uri,
                                              std::optional<int64_t> offset,
                                              std::optional<int64_t> length) {
    JSONValue v = client.ReadResource(uri, offset, length).get();
    if (isErrorObject(v)) {
        auto e = errors::mcpErrorFromErrorValue(v);
        if (e.has_value()) {
            LOG_ERROR("resources/read (range) '{}' failed: code={} message={}", uri, e->code, e->message);
        } else {
            LOG_ERROR("resources/read (range) '{}' failed with unknown error shape", uri);
        }
        throw std::runtime_error(e.has_value() ? e->message : std::string("resources/read (range) error"));
    }
    if (client.GetValidationMode() == validation::ValidationMode::Strict) {
        if (!validation::validateReadResourceResultJson(v)) {
            LOG_ERROR("Validation failed (Strict): resources/read (range) result shape for '{}'", uri);
            throw std::runtime_error("Validation failed: resources/read (range) result shape");
        }
    }
    return parseReadResourceResult(v);
}

inline std::future<ReadResourceResult> readResourceRange(IClient& client,
                                                         const std::string& uri,
                                                         const std::optional<int64_t>& offset,
                                                         const std::optional<int64_t>& length) {
    return std::async(std::launch::async, doReadResourceRange, std::ref(client), uri, offset, length);
}

// Helper for assembling full resource via chunks
inline ReadResourceResult doReadAllResourceInChunks(IClient& client,
                                                    const std::string& uri,
                                                    size_t chunkSize) {
    if (chunkSize == 0) {
        throw std::invalid_argument("chunkSize must be > 0");
    }
    ReadResourceResult agg; size_t offset = 0;
    while (true) {
        auto part = readResourceRange(client, uri, static_cast<int64_t>(offset), static_cast<int64_t>(chunkSize)).get();
        if (part.contents.empty()) {
            break;
        }
        // Append to aggregate and compute the actual returned byte count to advance offset by.
        size_t returnedBytes = 0;
        for (auto& v : part.contents) {
            if (std::holds_alternative<JSONValue::Object>(v.value)) {
                const auto& o = std::get<JSONValue::Object>(v.value);
                auto itType = o.find("type");
                auto itText = o.find("text");
                if (itType != o.end() && itText != o.end() && itType->second && itText->second &&
                    std::holds_alternative<std::string>(itType->second->value) &&
                    std::get<std::string>(itType->second->value) == std::string("text") &&
                    std::holds_alternative<std::string>(itText->second->value)) {
                    returnedBytes += std::get<std::string>(itText->second->value).size();
                }
            }
            agg.contents.push_back(std::move(v));
        }
        if (returnedBytes == 0) {
            // Defensive: if server returned non-text content for a range (shouldn't happen as client would throw),
            // or returned empty text, stop to avoid infinite loop.
            break;
        }
        offset += returnedBytes;
    }
    return agg;
}

// Cancel-aware variant: respects stopToken between chunked reads (does not cancel in-flight RPC)
inline ReadResourceResult doReadAllResourceInChunksCancelable(IClient& client,
                                                             const std::string& uri,
                                                             size_t chunkSize,
                                                             const std::optional<std::stop_token>& stopToken) {
    if (chunkSize == 0) {
        throw std::invalid_argument("chunkSize must be > 0");
    }
    ReadResourceResult agg; size_t offset = 0;
    while (true) {
        if (stopToken.has_value() && stopToken->stop_requested()) {
            break;
        }
        auto part = readResourceRange(client, uri, static_cast<int64_t>(offset), static_cast<int64_t>(chunkSize)).get();
        if (part.contents.empty()) {
            break;
        }
        size_t returnedBytes = 0;
        for (auto& v : part.contents) {
            if (std::holds_alternative<JSONValue::Object>(v.value)) {
                const auto& o = std::get<JSONValue::Object>(v.value);
                auto itType = o.find("type");
                auto itText = o.find("text");
                if (itType != o.end() && itText != o.end() && itType->second && itText->second &&
                    std::holds_alternative<std::string>(itType->second->value) &&
                    std::get<std::string>(itType->second->value) == std::string("text") &&
                    std::holds_alternative<std::string>(itText->second->value)) {
                    returnedBytes += std::get<std::string>(itText->second->value).size();
                }
            }
            agg.contents.push_back(std::move(v));
        }
        if (returnedBytes == 0) {
            break;
        }
        offset += returnedBytes;
    }
    return agg;
}

inline std::future<ReadResourceResult> readAllResourceInChunks(IClient& client,
                                                               const std::string& uri,
                                                               size_t chunkSize) {
    return std::async(std::launch::async, doReadAllResourceInChunks, std::ref(client), uri, chunkSize);
}

// Overload: cancel-aware using std::stop_token (checked between chunk requests)
inline std::future<ReadResourceResult> readAllResourceInChunks(IClient& client,
                                                               const std::string& uri,
                                                               size_t chunkSize,
                                                               const std::optional<std::stop_token>& stopToken) {
    return std::async(std::launch::async, doReadAllResourceInChunksCancelable, std::ref(client), uri, chunkSize, stopToken);
}

// Overload: clamp-aware; uses min(preferredChunkSize, serverClampHint) when clamp is present
inline std::future<ReadResourceResult> readAllResourceInChunks(IClient& client,
                                                               const std::string& uri,
                                                               size_t preferredChunkSize,
                                                               const std::optional<size_t>& serverClampHint) {
    return std::async(std::launch::async, [&client, uri, preferredChunkSize, serverClampHint]() {
        size_t effective = preferredChunkSize;
        if (serverClampHint.has_value() && serverClampHint.value() > 0) {
            effective = std::min(preferredChunkSize, serverClampHint.value());
        }
        return doReadAllResourceInChunks(client, uri, effective);
    });
}

// Overload: clamp + cancel-aware
inline std::future<ReadResourceResult> readAllResourceInChunks(IClient& client,
                                                               const std::string& uri,
                                                               size_t preferredChunkSize,
                                                               const std::optional<size_t>& serverClampHint,
                                                               const std::optional<std::stop_token>& stopToken) {
    return std::async(std::launch::async, [&client, uri, preferredChunkSize, serverClampHint, stopToken]() {
        size_t effective = preferredChunkSize;
        if (serverClampHint.has_value() && serverClampHint.value() > 0) {
            effective = std::min(preferredChunkSize, serverClampHint.value());
        }
        return doReadAllResourceInChunksCancelable(client, uri, effective, stopToken);
    });
}

// Overload: accepts ServerCapabilities and derives clamp via extractResourceReadClamp
inline std::future<ReadResourceResult> readAllResourceInChunks(IClient& client,
                                                               const std::string& uri,
                                                               size_t preferredChunkSize,
                                                               const ServerCapabilities& scaps) {
    auto clampHint = extractResourceReadClamp(scaps);
    return readAllResourceInChunks(client, uri, preferredChunkSize, clampHint);
}

// Overload: accepts ServerCapabilities and stop token
inline std::future<ReadResourceResult> readAllResourceInChunks(IClient& client,
                                                               const std::string& uri,
                                                               size_t preferredChunkSize,
                                                               const ServerCapabilities& scaps,
                                                               const std::optional<std::stop_token>& stopToken) {
    auto clampHint = extractResourceReadClamp(scaps);
    return readAllResourceInChunks(client, uri, preferredChunkSize, clampHint, stopToken);
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
