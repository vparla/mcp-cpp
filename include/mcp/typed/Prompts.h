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

} // namespace prompts
} // namespace typed
} // namespace mcp
