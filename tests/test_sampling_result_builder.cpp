//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_sampling_result_builder.cpp
// Purpose: Tests for SamplingResultBuilder chainable constructor for sampling results
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/typed/Sampling.h"

using namespace mcp;

TEST(SamplingBuilder, BuildsModelRoleAndSingleText) {
    mcp::typed::SamplingResultBuilder b;
    JSONValue v = b.setModel("m").setRole("assistant").addText("hi").build();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(v.value));
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto itModel = o.find("model");
    auto itRole = o.find("role");
    auto itContent = o.find("content");
    ASSERT_TRUE(itModel != o.end());
    ASSERT_TRUE(itRole != o.end());
    ASSERT_TRUE(itContent != o.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(itModel->second->value));
    ASSERT_TRUE(std::holds_alternative<std::string>(itRole->second->value));
    ASSERT_TRUE(std::holds_alternative<JSONValue::Array>(itContent->second->value));
    const auto& arr = std::get<JSONValue::Array>(itContent->second->value);
    ASSERT_EQ(arr.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(arr[0]->value));
    const auto& c0 = std::get<JSONValue::Object>(arr[0]->value);
    auto tIt = c0.find("type");
    auto txtIt = c0.find("text");
    ASSERT_TRUE(tIt != c0.end());
    ASSERT_TRUE(txtIt != c0.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(tIt->second->value));
    ASSERT_TRUE(std::holds_alternative<std::string>(txtIt->second->value));
    EXPECT_EQ(std::get<std::string>(tIt->second->value), std::string("text"));
    EXPECT_EQ(std::get<std::string>(txtIt->second->value), std::string("hi"));
}
