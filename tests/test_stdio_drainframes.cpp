//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_stdio_drainframes.cpp
// Purpose: StdioTransport negative-path tests (invalid/empty responses)
//==========================================================================================================

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "mcp/StdioTransport.hpp"
#include "mcp/JSONRPCTypes.h"

using namespace mcp;

TEST(StdioDrainFrames, InvalidHeaderRecovers) {
    StdioTransport t;
    bool notified = false;
    t.SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){ (void)note; notified = true; });
    StdioTransportTestHooks::setConnected(t, true);

    std::string buffer = "Content-Length: abc\r\n\r\n";
    std::string payload = "{\"jsonrpc\":\"2.0\",\"method\":\"notify\"}";
    buffer += std::string("Content-Length: ") + std::to_string(payload.size()) + "\r\n\r\n" + payload;

    StdioTransportTestHooks::drainFrames(t, buffer);

    EXPECT_TRUE(notified);
    EXPECT_TRUE(buffer.empty());
}

TEST(StdioDrainFrames, BodyTooLargeCloses) {
    StdioTransport t;
    t.SetMaxContentLength(4);
    bool errored = false;
    t.SetErrorHandler([&](const std::string&){ errored = true; });
    StdioTransportTestHooks::setConnected(t, true);

    std::string buffer = "Content-Length: 5\r\n\r\nabcde";

    StdioTransportTestHooks::drainFrames(t, buffer);

    EXPECT_TRUE(errored);
    EXPECT_FALSE(StdioTransportTestHooks::isConnected(t));
}

TEST(StdioDrainFrames, IncompleteFrameWaits) {
    StdioTransport t;
    StdioTransportTestHooks::setConnected(t, true);

    std::string buffer = "Content-Length: 4\r\n\r\nab";

    StdioTransportTestHooks::drainFrames(t, buffer);

    EXPECT_TRUE(StdioTransportTestHooks::isConnected(t));
    EXPECT_EQ(buffer, std::string("Content-Length: 4\r\n\r\nab"));
}
