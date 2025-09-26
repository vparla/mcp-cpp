//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Sampling.h
// Purpose: Typed sampling helpers for building createMessage results
//==========================================================================================================

#pragma once

#include "mcp/JSONRPCTypes.h"
#include <string>

namespace mcp {
namespace typed {

// Builds a simple sampling/createMessage result JSON with a single text content item.
// Args:
//   model: Model identifier to report in the result.
//   role: Role for the message (e.g., "assistant").
//   text: Text content for the single content item.
// Returns:
//   JSONValue object with keys: model, role, content=[{type:"text", text:<text>}]
inline JSONValue makeTextSamplingResult(const std::string& model,
                                        const std::string& role,
                                        const std::string& text) {
    JSONValue::Object resultObj;
    resultObj["model"] = std::make_shared<JSONValue>(model);
    resultObj["role"] = std::make_shared<JSONValue>(role);

    JSONValue::Array contentArr;
    JSONValue::Object textContent;
    textContent["type"] = std::make_shared<JSONValue>(std::string("text"));
    textContent["text"] = std::make_shared<JSONValue>(text);
    contentArr.push_back(std::make_shared<JSONValue>(textContent));

    resultObj["content"] = std::make_shared<JSONValue>(contentArr);
    return JSONValue{resultObj};
}

//==========================================================================================================
// SamplingResultBuilder
// Purpose: Chainable helper to construct a sampling/createMessage result
//
// Usage:
//   JSONValue v = SamplingResultBuilder()
//                   .setModel("m")
//                   .setRole("assistant")
//                   .addText("hello")
//                   .build();
//
// Args (for methods):
//   setModel: Model identifier string.
//   setRole: Role string (e.g., "assistant").
//   addText: Appends a text content item.
// Returns:
//   Chainable reference for setters; build() returns a JSONValue with keys: model, role, content[].
//==========================================================================================================
class SamplingResultBuilder {
public:
    inline SamplingResultBuilder& setModel(const std::string& m) {
        this->model = m; return *this;
    }

    inline SamplingResultBuilder& setRole(const std::string& r) {
        this->role = r; return *this;
    }

    inline SamplingResultBuilder& addText(const std::string& text) {
        JSONValue::Object t;
        t["type"] = std::make_shared<JSONValue>(std::string("text"));
        t["text"] = std::make_shared<JSONValue>(text);
        contents.push_back(std::make_shared<JSONValue>(t));
        return *this;
    }

    inline JSONValue build() const {
        JSONValue::Object resultObj;
        resultObj["model"] = std::make_shared<JSONValue>(this->model);
        resultObj["role"] = std::make_shared<JSONValue>(this->role);
        resultObj["content"] = std::make_shared<JSONValue>(this->contents);
        return JSONValue{resultObj};
    }

private:
    std::string model;
    std::string role;
    JSONValue::Array contents;
};

} // namespace typed
} // namespace mcp
