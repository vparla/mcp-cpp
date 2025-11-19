//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_content_framer.cpp
// Purpose: Tests for ContentFramer
//==========================================================================================================

#include <gtest/gtest.h>

#include <string>

#include "mcp/ContentFramer.h"

TEST(ContentLengthFramerTest, EncodeProducesExpectedHeader) {
    auto framer = mcp::MakeContentLengthFramer(1024);
    const std::string payload = "{}";
    const std::string frame = framer->encode(payload);
    EXPECT_EQ(frame, std::string("Content-Length: 2\r\n\r\n{}"));
}

TEST(ContentLengthFramerTest, TryDecodeExOkAtBoundary) {
    auto framer = mcp::MakeContentLengthFramer(4);
    const std::string payload = "abcd";
    std::string buffer = std::string("Content-Length: 4\r\n\r\n") + payload;
    auto ex = framer->tryDecodeEx(buffer);
    EXPECT_EQ(ex.status, mcp::IContentFramer::DecodeStatus::Ok);
    ASSERT_TRUE(ex.payload.has_value());
    EXPECT_EQ(ex.payload.value(), payload);
    EXPECT_GT(ex.bytesConsumed, 0u);
}

TEST(ContentLengthFramerTest, TryDecodeExBodyTooLargeAboveBoundary) {
    auto framer = mcp::MakeContentLengthFramer(4);
    std::string buffer = "Content-Length: 5\r\n\r\nabcde";
    auto ex = framer->tryDecodeEx(buffer);
    EXPECT_EQ(ex.status, mcp::IContentFramer::DecodeStatus::BodyTooLarge);
    EXPECT_FALSE(ex.payload.has_value());
    EXPECT_GT(ex.bytesConsumed, 0u);
    auto dec = framer->tryDecode(buffer);
    EXPECT_FALSE(dec.has_value());
    EXPECT_EQ(buffer, std::string("Content-Length: 5\r\n\r\nabcde"));
}

TEST(ContentLengthFramerTest, TryDecodeExInvalidHeader) {
    auto framer = mcp::MakeContentLengthFramer(1024);
    std::string buffer = "Content-Length: abc\r\n\r\n{}";
    auto ex = framer->tryDecodeEx(buffer);
    EXPECT_EQ(ex.status, mcp::IContentFramer::DecodeStatus::InvalidHeader);
    EXPECT_FALSE(ex.payload.has_value());
    EXPECT_GT(ex.bytesConsumed, 0u);
    auto dec = framer->tryDecode(buffer);
    EXPECT_FALSE(dec.has_value());
    EXPECT_EQ(buffer, std::string("Content-Length: abc\r\n\r\n{}"));
}

TEST(ContentLengthFramerTest, TryDecodeExIncompleteHeader) {
    auto framer = mcp::MakeContentLengthFramer(1024);
    std::string buffer = "Content-Leng";
    auto ex = framer->tryDecodeEx(buffer);
    EXPECT_EQ(ex.status, mcp::IContentFramer::DecodeStatus::Incomplete);
    EXPECT_FALSE(ex.payload.has_value());
    EXPECT_EQ(ex.bytesConsumed, 0u);
}

TEST(ContentLengthFramerTest, DecodeSingleFrame) {
    auto framer = mcp::MakeContentLengthFramer(1024);
    std::string buffer = "Content-Length: 2\r\n\r\n{}";
    auto decoded = framer->tryDecode(buffer);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), "{}");
    EXPECT_TRUE(buffer.empty());
}

TEST(ContentLengthFramerTest, DecodeIncompleteHeaderReturnsNullopt) {
    auto framer = mcp::MakeContentLengthFramer(1024);
    std::string buffer = "Content-Leng"; // no CRLF CRLF yet
    auto decoded = framer->tryDecode(buffer);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(buffer, std::string("Content-Leng")); // unchanged
}

TEST(ContentLengthFramerTest, DecodeIncompleteBodyReturnsNulloptAndKeepsBuffer) {
    auto framer = mcp::MakeContentLengthFramer(1024);
    std::string buffer = "Content-Length: 4\r\n\r\nab"; // body missing 2 bytes
    auto decoded = framer->tryDecode(buffer);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(buffer, std::string("Content-Length: 4\r\n\r\nab"));
}

TEST(ContentLengthFramerTest, InvalidContentLengthLeavesBuffer) {
    auto framer = mcp::MakeContentLengthFramer(1024);
    std::string buffer = "Content-Length: abc\r\n\r\n{}";
    auto decoded = framer->tryDecode(buffer);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(buffer, std::string("Content-Length: abc\r\n\r\n{}"));
}

TEST(ContentLengthFramerTest, OversizeContentLengthLeavesBuffer) {
    auto framer = mcp::MakeContentLengthFramer(4);
    std::string buffer = "Content-Length: 9999999\r\n\r\nX";
    auto decoded = framer->tryDecode(buffer);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(buffer, std::string("Content-Length: 9999999\r\n\r\nX"));
}

TEST(ContentLengthFramerTest, DecodeMultipleFramesSequentially) {
    auto framer = mcp::MakeContentLengthFramer(1024);
    std::string buffer;
    buffer += "Content-Length: 1\r\n\r\na";
    buffer += "Content-Length: 2\r\n\r\nbc";

    auto d1 = framer->tryDecode(buffer);
    ASSERT_TRUE(d1.has_value());
    EXPECT_EQ(d1.value(), std::string("a"));

    auto d2 = framer->tryDecode(buffer);
    ASSERT_TRUE(d2.has_value());
    EXPECT_EQ(d2.value(), std::string("bc"));

    EXPECT_TRUE(buffer.empty());
}

TEST(ContentLengthFramerTest, TryDecodeExZeroLengthOk) {
    auto framer = mcp::MakeContentLengthFramer(1024);
    std::string buffer = "Content-Length: 0\r\n\r\n";
    auto ex = framer->tryDecodeEx(buffer);
    EXPECT_EQ(ex.status, mcp::IContentFramer::DecodeStatus::Ok);
    ASSERT_TRUE(ex.payload.has_value());
    EXPECT_EQ(ex.payload.value(), std::string(""));
    EXPECT_GT(ex.bytesConsumed, 0u);
}

TEST(ContentLengthFramerTest, TryDecodeExGarbageThenHeaderInvalidHeader) {
    auto framer = mcp::MakeContentLengthFramer(1024);
    std::string buffer = "XContent-Length: 2\r\n\r\n{}";
    auto ex = framer->tryDecodeEx(buffer);
    EXPECT_EQ(ex.status, mcp::IContentFramer::DecodeStatus::InvalidHeader);
    EXPECT_FALSE(ex.payload.has_value());
    EXPECT_GT(ex.bytesConsumed, 0u);
    auto dec = framer->tryDecode(buffer);
    EXPECT_FALSE(dec.has_value());
    EXPECT_EQ(buffer, std::string("XContent-Length: 2\r\n\r\n{}"));
}
