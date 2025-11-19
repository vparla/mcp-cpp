//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_stdio_writer.cpp
// Purpose: StdioTransport negative-path tests (invalid/empty responses)
//==========================================================================================================

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <future>
#include <string>

#include "mcp/StdioTransport.hpp"
#include "mcp/JSONRPCTypes.h"

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

using namespace std::chrono_literals;

TEST(StdioWriter, WriteQueueOverflowCloses) {
    mcp::StdioTransport t;

    std::promise<void> errPromise;
    std::atomic<bool> sawError{false};
    t.SetErrorHandler([&](const std::string&){ if (!sawError.exchange(true)) { errPromise.set_value(); } });

    t.SetWriteQueueMaxBytes(16);
    t.Start().get();

    auto n = std::make_unique<mcp::JSONRPCNotification>();
    n->method = "notify/overflow";
    mcp::JSONValue::Object obj; obj["data"] = std::make_shared<mcp::JSONValue>(std::string(128, 'x'));
    n->params.emplace(obj);

    t.SendNotification(std::move(n)).get();

    auto fut = errPromise.get_future();
    ASSERT_EQ(fut.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(t.IsConnected());

    t.Close().get();
}

#ifndef _WIN32
TEST(StdioWriter, WriteTimeoutCloses) {
    // Redirect stdout to a non-blocking pipe whose write end is full
    int saved = ::dup(STDOUT_FILENO);
    ASSERT_GE(saved, 0);

    int fds[2]{};
    ASSERT_EQ(::pipe(fds), 0);
    int readFd = fds[0];
    int writeFd = fds[1];

    int flags = ::fcntl(writeFd, F_GETFL, 0);
    ASSERT_GE(flags, 0);
    ASSERT_GE(::fcntl(writeFd, F_SETFL, flags | O_NONBLOCK), 0);

    // Fill the pipe until EAGAIN
    std::string chunk(4096, 'a');
    for (;;) {
        ssize_t w = ::write(writeFd, chunk.data(), chunk.size());
        if (w < 0) {
            ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
            break;
        }
        if (w == 0) {
            break;
        }
    }

    ASSERT_GE(::dup2(writeFd, STDOUT_FILENO), 0);
    ::close(writeFd);

    mcp::StdioTransport t;
    std::promise<void> errPromise;
    std::atomic<bool> sawError{false};
    t.SetErrorHandler([&](const std::string&){ if (!sawError.exchange(true)) { errPromise.set_value(); } });
    t.SetWriteTimeoutMs(50);
    t.Start().get();

    auto n = std::make_unique<mcp::JSONRPCNotification>();
    n->method = "notify/timeout";
    // Large payload to ensure multiple write attempts
    n->params.emplace(mcp::JSONValue::Object{{"blob", std::make_shared<mcp::JSONValue>(std::string(65536, 'b'))}});
    t.SendNotification(std::move(n)).get();

    auto fut = errPromise.get_future();
    ASSERT_EQ(fut.wait_for(3s), std::future_status::ready);
    EXPECT_FALSE(t.IsConnected());

    // Restore stdout and cleanup
    ASSERT_GE(::dup2(saved, STDOUT_FILENO), 0);
    ::close(saved);
    ::close(readFd);

    t.Close().get();
}
#endif
