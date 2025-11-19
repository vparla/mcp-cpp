//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_inmemory_concurrency.cpp
// Purpose: Tests for InMemoryTransport concurrency
//==========================================================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <string>

#include "mcp/InMemoryTransport.hpp"
#include "mcp/JSONRPCTypes.h"

using namespace mcp;

TEST(InMemoryTransportConcurrency, NotificationWhileRequestProcessing) {
    auto pair = InMemoryTransport::CreatePair();
    auto server = std::move(pair.first);
    auto client = std::move(pair.second);

    auto requestStarted = std::make_shared<std::promise<void>>();
    auto started = requestStarted->get_future();
    auto gate = std::make_shared<std::promise<void>>();
    std::shared_future<void> allowFinish = gate->get_future().share();

    server->SetRequestHandler([requestStarted, allowFinish, gate](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        (void)req;
        requestStarted->set_value();
        allowFinish.wait();
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->result = JSONValue(static_cast<int64_t>(1));
        return resp;
    });

    auto notifiedPromise = std::make_shared<std::promise<void>>();
    std::shared_future<void> notified = notifiedPromise->get_future().share();
    server->SetNotificationHandler([notifiedPromise](std::unique_ptr<JSONRPCNotification> note) {
        (void)note;
        notifiedPromise->set_value();
    });

    ASSERT_NO_THROW({ server->Start().get(); });
    ASSERT_NO_THROW({ client->Start().get(); });

    auto req = std::make_unique<JSONRPCRequest>();
    req->method = std::string("long");
    auto fut = client->SendRequest(std::move(req));

    ASSERT_EQ(started.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto note = std::make_unique<JSONRPCNotification>();
    note->method = std::string("n");
    (void)client->SendNotification(std::move(note));

    ASSERT_EQ(notified.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    gate->set_value();
    auto resp = std::move(fut).get();
    ASSERT_TRUE(resp);
    ASSERT_TRUE(resp->result.has_value());
    const auto& v = resp->result->get();
    ASSERT_TRUE(std::holds_alternative<int64_t>(v));
    ASSERT_EQ(std::get<int64_t>(v), 1);

    ASSERT_NO_THROW({ client->Close().get(); });
    ASSERT_NO_THROW({ server->Close().get(); });
}
