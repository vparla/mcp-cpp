//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Prompts.h
// Purpose: Helpers to build typed prompt arguments and messages for MCP
//==========================================================================================================

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>

#include "mcp/Protocol.h"

namespace mcp {
namespace typed {
namespace prompts {

class ArgsBuilder {
public:
    ArgsBuilder() = default;

    ArgsBuilder& add(const std::string& key, const JSONValue& value) {
        obj_[key] = std::make_shared<JSONValue>(value);
        return *this;
    }

    ArgsBuilder& addString(const std::string& key, const std::string& value) {
        obj_[key] = std::make_shared<JSONValue>(value);
        return *this;
    }

    ArgsBuilder& addInt(const std::string& key, int64_t value) {
        obj_[key] = std::make_shared<JSONValue>(value);
        return *this;
    }

    ArgsBuilder& addBool(const std::string& key, bool value) {
        obj_[key] = std::make_shared<JSONValue>(value);
        return *this;
    }

    JSONValue toJSON() const { return JSONValue{obj_}; }

private:
    JSONValue::Object obj_;
};

// Convenience to build messages array with simple text items
inline std::vector<JSONValue> makeTextMessages(const std::vector<std::string>& texts) {
    std::vector<JSONValue> msgs; msgs.reserve(texts.size());
    for (const auto& t : texts) {
        JSONValue::Object m; m["type"] = std::make_shared<JSONValue>(std::string("text")); m["text"] = std::make_shared<JSONValue>(t);
        msgs.emplace_back(JSONValue{m});
    }
    return msgs;
}

// Builder to compose typed GetPromptResult easily
class ResultBuilder {
public:
    ResultBuilder& setDescription(const std::string& description) {
        this->description_ = description;
        return *this;
    }

    ResultBuilder& addText(const std::string& text) {
        JSONValue::Object item; item["type"] = std::make_shared<JSONValue>(std::string("text")); item["text"] = std::make_shared<JSONValue>(text);
        messages_.push_back(JSONValue{item});
        return *this;
    }

    GetPromptResult build() const {
        GetPromptResult r; r.description = this->description_; r.messages = this->messages_; return r;
    }

private:
    std::string description_;
    std::vector<JSONValue> messages_;
};

// Convenience factory
inline GetPromptResult makeTextPromptResult(const std::string& description, const std::vector<std::string>& texts) {
    ResultBuilder b; b.setDescription(description); for (const auto& t : texts) b.addText(t); return b.build();
}

} // namespace prompts
} // namespace typed
} // namespace mcp
