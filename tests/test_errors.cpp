//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_errors.cpp
// Purpose: GoogleTests for typed error structures and JSON-RPC error mapping helpers
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/JSONRPCTypes.h"
#include "mcp/errors/Errors.h"

using namespace mcp;

TEST(Errors, CategoryMapping) {
    using mcp::errors::ErrorCategory;
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(JSONRPCErrorCodes::ParseError), ErrorCategory::JsonRpcParse);
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(JSONRPCErrorCodes::InvalidRequest), ErrorCategory::JsonRpcInvalidRequest);
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(JSONRPCErrorCodes::MethodNotFound), ErrorCategory::JsonRpcMethodNotFound);
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(JSONRPCErrorCodes::InvalidParams), ErrorCategory::JsonRpcInvalidParams);
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(JSONRPCErrorCodes::InternalError), ErrorCategory::JsonRpcInternal);
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(JSONRPCErrorCodes::InvalidRequestId), ErrorCategory::McpInvalidRequestId);
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(JSONRPCErrorCodes::MethodNotAllowed), ErrorCategory::McpMethodNotAllowed);
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(JSONRPCErrorCodes::ResourceNotFound), ErrorCategory::McpResourceNotFound);
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(JSONRPCErrorCodes::ToolNotFound), ErrorCategory::McpToolNotFound);
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(JSONRPCErrorCodes::PromptNotFound), ErrorCategory::McpPromptNotFound);
    EXPECT_EQ(mcp::errors::errorCategoryFromCode(12345), ErrorCategory::Unknown);
}

TEST(Errors, FromErrorValue_Valid) {
    JSONValue::Object dataObj; dataObj["foo"] = std::make_shared<JSONValue>(std::string("bar"));
    JSONValue::Object errObj;
    errObj["code"] = std::make_shared<JSONValue>(static_cast<int64_t>(JSONRPCErrorCodes::MethodNotFound));
    errObj["message"] = std::make_shared<JSONValue>(std::string("Method not found"));
    errObj["data"] = std::make_shared<JSONValue>(JSONValue{dataObj});

    JSONValue errVal{errObj};
    auto parsed = mcp::errors::mcpErrorFromErrorValue(errVal);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->code, JSONRPCErrorCodes::MethodNotFound);
    EXPECT_EQ(parsed->message, std::string("Method not found"));
    ASSERT_TRUE(parsed->data.has_value());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(parsed->data->value));
    const auto& o = std::get<JSONValue::Object>(parsed->data->value);
    auto it = o.find("foo");
    ASSERT_TRUE(it != o.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(it->second->value));
    EXPECT_EQ(std::get<std::string>(it->second->value), std::string("bar"));
}

TEST(Errors, FromErrorValue_InvalidShape) {
    // Not an object
    JSONValue notObj{nullptr};
    EXPECT_FALSE(mcp::errors::mcpErrorFromErrorValue(notObj).has_value());

    // Missing code
    JSONValue::Object missCode; missCode["message"] = std::make_shared<JSONValue>(std::string("m"));
    EXPECT_FALSE(mcp::errors::mcpErrorFromErrorValue(JSONValue{missCode}).has_value());

    // Missing message
    JSONValue::Object missMsg; missMsg["code"] = std::make_shared<JSONValue>(static_cast<int64_t>(-1));
    EXPECT_FALSE(mcp::errors::mcpErrorFromErrorValue(JSONValue{missMsg}).has_value());

    // Wrong types
    JSONValue::Object wrongTypes;
    wrongTypes["code"] = std::make_shared<JSONValue>(std::string("-32601"));
    wrongTypes["message"] = std::make_shared<JSONValue>(static_cast<int64_t>(123));
    EXPECT_FALSE(mcp::errors::mcpErrorFromErrorValue(JSONValue{wrongTypes}).has_value());
}

TEST(Errors, FromResponse) {
    auto resp = CreateErrorResponse(std::string("1"), JSONRPCErrorCodes::InvalidParams, "bad args", std::nullopt);
    ASSERT_TRUE(resp != nullptr);
    auto parsed = mcp::errors::mcpErrorFromResponse(*resp);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->code, JSONRPCErrorCodes::InvalidParams);
    EXPECT_EQ(parsed->message, std::string("bad args"));
    EXPECT_FALSE(parsed->data.has_value());
}

TEST(Errors, MakeErrorValue_RoundTrip) {
    mcp::errors::McpError e;
    e.code = JSONRPCErrorCodes::ResourceNotFound;
    e.message = "missing";
    JSONValue::Object dataObj; dataObj["uri"] = std::make_shared<JSONValue>(std::string("mem://x"));
    e.data = JSONValue{dataObj};

    JSONValue v = mcp::errors::makeErrorValue(e);
    auto parsed = mcp::errors::mcpErrorFromErrorValue(v);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->code, e.code);
    EXPECT_EQ(parsed->message, e.message);
    ASSERT_TRUE(parsed->data.has_value());
    const auto& o = std::get<JSONValue::Object>(parsed->data->value);
    auto it = o.find("uri");
    ASSERT_TRUE(it != o.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(it->second->value));
    EXPECT_EQ(std::get<std::string>(it->second->value), std::string("mem://x"));
}

TEST(Errors, MakeErrorResponse_ContainsError) {
    mcp::errors::McpError e;
    e.code = JSONRPCErrorCodes::ToolNotFound;
    e.message = "no such tool";
    auto resp = mcp::errors::makeErrorResponse(std::string("7"), e);
    ASSERT_TRUE(resp != nullptr);
    EXPECT_TRUE(resp->IsError());
    ASSERT_TRUE(resp->error.has_value());
    auto parsed = mcp::errors::mcpErrorFromResponse(*resp);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->code, e.code);
    EXPECT_EQ(parsed->message, e.message);
}
