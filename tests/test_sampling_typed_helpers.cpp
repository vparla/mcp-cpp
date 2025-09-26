//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_sampling_typed_helpers.cpp
// Purpose: Tests for typed sampling helper makeTextSamplingResult
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/typed/Sampling.h"

using namespace mcp;

TEST(SamplingTypedHelpers, MakeTextSamplingResult_ShapeIsCorrect) {
    JSONValue v = mcp::typed::makeTextSamplingResult("unit-model", "assistant", "ok");
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(v.value));
    const auto& obj = std::get<JSONValue::Object>(v.value);

    auto itModel = obj.find("model");
    ASSERT_TRUE(itModel != obj.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(itModel->second->value));
    EXPECT_EQ(std::get<std::string>(itModel->second->value), "unit-model");

    auto itRole = obj.find("role");
    ASSERT_TRUE(itRole != obj.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(itRole->second->value));
    EXPECT_EQ(std::get<std::string>(itRole->second->value), "assistant");

    auto itContent = obj.find("content");
    ASSERT_TRUE(itContent != obj.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Array>(itContent->second->value));
    const auto& arr = std::get<JSONValue::Array>(itContent->second->value);
    ASSERT_EQ(arr.size(), 1u);
    ASSERT_TRUE(arr[0] != nullptr);
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(arr[0]->value));
    const auto& c0 = std::get<JSONValue::Object>(arr[0]->value);
    auto tIt = c0.find("type");
    auto txtIt = c0.find("text");
    ASSERT_TRUE(tIt != c0.end());
    ASSERT_TRUE(txtIt != c0.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(tIt->second->value));
    ASSERT_TRUE(std::holds_alternative<std::string>(txtIt->second->value));
    EXPECT_EQ(std::get<std::string>(tIt->second->value), "text");
    EXPECT_EQ(std::get<std::string>(txtIt->second->value), "ok");
}
