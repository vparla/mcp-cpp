//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_cancellation.cpp
// Purpose: Tests for server-side cancellation propagation via notifications/cancelled
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"
#include <thread>
#include <chrono>
#include <atomic>

using namespace mcp;

TEST(Cancellation, CancelsLongRunningTool) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.RegisterTool("slow", [](const JSONValue&, std::stop_token st) {
        return std::async(std::launch::async, [st]() mutable {
            // Simulate long-running work
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            // Optionally react to cancellation (not required for this test)
            ToolResult out; out.isError = false; return out;
        });
    });

    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Send tools/call with a known id so we can cancel it
    auto req = std::make_unique<JSONRPCRequest>();
    req->id = std::string("cancel-1");
    req->method = Methods::CallTool;
    JSONValue::Object params;
    params["name"] = std::make_shared<JSONValue>(std::string("slow"));
    params["arguments"] = std::make_shared<JSONValue>(JSONValue::Object{});
    req->params.emplace(params);

    auto fut = client->SendRequest(std::move(req));

    // Issue cancellation for the same id
    auto note = std::make_unique<JSONRPCNotification>();
    note->method = Methods::Cancelled;
    JSONValue::Object cancelParams;
    cancelParams["id"] = std::make_shared<JSONValue>(std::string("cancel-1"));
    note->params.emplace(cancelParams);
    (void)client->SendNotification(std::move(note));

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

    client->Close().get();
    server.Stop().get();
}

TEST(Cancellation, CooperativeToolStopsEarly) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    std::atomic<bool> stopObserved{false};
    std::promise<void> observed; auto observedFut = observed.get_future();
    server.RegisterTool("cooperative", [&](const JSONValue&, std::stop_token st) {
        return std::async(std::launch::async, [&, st]() mutable {
            // Loop until cancelled
            while (!st.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            stopObserved.store(true);
            try { observed.set_value(); } catch (...) {}
            ToolResult out; out.isError = false; return out;
        });
    });

    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->id = std::string("cancel-3");
    req->method = Methods::CallTool;
    JSONValue::Object params;
    params["name"] = std::make_shared<JSONValue>(std::string("cooperative"));
    params["arguments"] = std::make_shared<JSONValue>(JSONValue::Object{});
    req->params.emplace(params);

    auto fut = client->SendRequest(std::move(req));

    // Cancel immediately
    auto note = std::make_unique<JSONRPCNotification>();
    note->method = Methods::Cancelled;
    JSONValue::Object cancelParams;
    cancelParams["id"] = std::make_shared<JSONValue>(std::string("cancel-3"));
    note->params.emplace(cancelParams);
    (void)client->SendNotification(std::move(note));

    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_TRUE(resp->IsError());
    // Wait up to 2s for cooperative handler to observe cancellation
    (void)observedFut.wait_for(std::chrono::seconds(2));
    EXPECT_TRUE(stopObserved.load());

    client->Close().get();
    server.Stop().get();
}

TEST(Cancellation, CooperativeReadResourceStopsEarly) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    std::atomic<bool> stopObserved{false};
    std::promise<void> observed; auto observedFut = observed.get_future();
    server.RegisterResource("cooperative://r", [&](const std::string&, std::stop_token st) {
        return std::async(std::launch::async, [&, st]() mutable {
            while (!st.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            stopObserved.store(true);
            try { observed.set_value(); } catch (...) {}
            ReadResourceResult res; return res;
        });
    });

    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->id = std::string("cancel-4");
    req->method = Methods::ReadResource;
    JSONValue::Object params;
    params["uri"] = std::make_shared<JSONValue>(std::string("cooperative://r"));
    req->params.emplace(params);
    auto fut = client->SendRequest(std::move(req));

    auto note = std::make_unique<JSONRPCNotification>();
    note->method = Methods::Cancelled;
    JSONValue::Object cancelParams;
    cancelParams["id"] = std::make_shared<JSONValue>(std::string("cancel-4"));
    note->params.emplace(cancelParams);
    (void)client->SendNotification(std::move(note));

    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    ASSERT_TRUE(resp->IsError());
    (void)observedFut.wait_for(std::chrono::seconds(2));
    EXPECT_TRUE(stopObserved.load());

    client->Close().get();
    server.Stop().get();
}

TEST(Cancellation, CancelsLongRunningReadResource) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("TestServer");
    server.RegisterResource("slow://r", [](const std::string&, std::stop_token st) {
        return std::async(std::launch::async, [st]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            ReadResourceResult res; return res;
        });
    });

    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    auto req = std::make_unique<JSONRPCRequest>();
    req->id = std::string("cancel-2");
    req->method = Methods::ReadResource;
    JSONValue::Object params;
    params["uri"] = std::make_shared<JSONValue>(std::string("slow://r"));
    req->params.emplace(params);

    auto fut = client->SendRequest(std::move(req));

    auto note = std::make_unique<JSONRPCNotification>();
    note->method = Methods::Cancelled;
    JSONValue::Object cancelParams;
    cancelParams["id"] = std::make_shared<JSONValue>(std::string("cancel-2"));
    note->params.emplace(cancelParams);
    (void)client->SendNotification(std::move(note));

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

    client->Close().get();
    server.Stop().get();
}
