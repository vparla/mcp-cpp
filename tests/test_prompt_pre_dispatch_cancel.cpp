//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_prompt_pre_dispatch_cancel.cpp
// Purpose: Tests for prompts/get pre-dispatch cancellation returning "Cancelled"
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace mcp;

TEST(PromptsCancel, PreDispatchCancelledReturnsCancelled) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    std::atomic<bool> handlerInvoked{false};
    server.RegisterPrompt("p", [&](const JSONValue&) -> PromptResult {
        handlerInvoked.store(true);
        PromptResult r; r.description = "d"; return r;
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(client->Start().get());

    // Issue cancellation BEFORE dispatching the request
    auto note = std::make_unique<JSONRPCNotification>();
    note->method = Methods::Cancelled;
    JSONValue::Object cancelParams; cancelParams["id"] = std::make_shared<JSONValue>(std::string("pre-1"));
    note->params.emplace(cancelParams);
    (void)client->SendNotification(std::move(note));

    // Give the server a brief moment to record the cancelled token
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Now send prompts/get with the same id
    auto req = std::make_unique<JSONRPCRequest>();
    req->id = std::string("pre-1");
    req->method = Methods::GetPrompt;
    JSONValue::Object params; params["name"] = std::make_shared<JSONValue>(std::string("p"));
    req->params.emplace(params);
    auto fut = client->SendRequest(std::move(req));

    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_TRUE(resp->IsError());
    ASSERT_TRUE(resp->error.has_value());
    const auto& err = resp->error.value();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(err.value));
    const auto& obj = std::get<JSONValue::Object>(err.value);
    auto it = obj.find("message");
    ASSERT_TRUE(it != obj.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(it->second->value));
    EXPECT_EQ(std::get<std::string>(it->second->value), "Cancelled");

    // Ensure handler was not invoked
    EXPECT_FALSE(handlerInvoked.load());

    ASSERT_NO_THROW(client->Close().get());
    ASSERT_NO_THROW(server.Stop().get());
}
