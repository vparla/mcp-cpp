//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_server_logging.cpp
// Purpose: Tests for server logging to client with capability-based filtering
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Transport.h"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"
#include <atomic>
#include <future>
#include <chrono>
#include <thread>

using namespace mcp;

TEST(ServerLogging, RespectsClientLogLevel) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Capture notifications/log on client side
    std::atomic<unsigned int> received{0u};
    std::promise<std::string> gotErrorMsg; auto gotErrorFut = gotErrorMsg.get_future();

    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (!note) {
            return;
        }
        if (note->method == Methods::Log && note->params.has_value()) {
            received.fetch_add(1u);
            const auto& v = note->params.value();
            if (std::holds_alternative<JSONValue::Object>(v.value)) {
                const auto& o = std::get<JSONValue::Object>(v.value);
                auto it = o.find("message");
                if (it != o.end() && std::holds_alternative<std::string>(it->second->value)) {
                    gotErrorMsg.set_value(std::get<std::string>(it->second->value));
                }
            }
        }
    });

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Initialize with client capabilities.experimental.logLevel = "WARN"
    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    JSONValue::Object caps; // { capabilities: { experimental: { logLevel: "WARN" } } }
    JSONValue::Object exp; exp["logLevel"] = std::make_shared<JSONValue>(std::string("WARN"));
    JSONValue::Object c; c["experimental"] = std::make_shared<JSONValue>(exp);
    JSONValue::Object root; root["capabilities"] = std::make_shared<JSONValue>(c);
    init->params.emplace(root);
    auto initRespFut = client->SendRequest(std::move(init));
    ASSERT_EQ(initRespFut.wait_for(std::chrono::seconds(1)), std::future_status::ready);

    // INFO log should be suppressed
    server.LogToClient("INFO", "info-message", std::nullopt);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(received.load(), 0);

    // ERROR log should be delivered
    server.LogToClient("ERROR", "error-message", std::nullopt);
    ASSERT_EQ(gotErrorFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(gotErrorFut.get(), std::string("error-message"));

    client->Close().get();
    server.Stop().get();
}

TEST(ServerLogging, WarningAliasDeliveredAtThreshold) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::promise<JSONValue> gotPayload; auto fut = gotPayload.get_future();
    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (note && note->method == Methods::Log && note->params.has_value()) {
            try {
                gotPayload.set_value(note->params.value());
            } catch (...) {}
        }
    });

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Initialize with WARN min
    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    JSONValue::Object exp; exp["logLevel"] = std::make_shared<JSONValue>(std::string("WARN"));
    JSONValue::Object caps; caps["experimental"] = std::make_shared<JSONValue>(exp);
    JSONValue::Object root; root["capabilities"] = std::make_shared<JSONValue>(caps);
    init->params.emplace(root);
    auto initRespFut = client->SendRequest(std::move(init));
    ASSERT_EQ(initRespFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Send WARNING (alias of WARN) should pass
    server.LogToClient("WARNING", "alias-ok", std::nullopt);
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto v = fut.get();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(v.value));
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto itMsg = o.find("message");
    ASSERT_TRUE(itMsg != o.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(itMsg->second->value));
    EXPECT_EQ(std::get<std::string>(itMsg->second->value), std::string("alias-ok"));
    auto itLvl = o.find("level");
    ASSERT_TRUE(itLvl != o.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(itLvl->second->value));
    EXPECT_EQ(std::get<std::string>(itLvl->second->value), std::string("WARNING"));

    client->Close().get();
    server.Stop().get();
}

TEST(ServerLogging, UnknownLevelSuppressedAtWarnMin) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::atomic<unsigned int> received{0u};
    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (note && note->method == Methods::Log) {
            received.fetch_add(1u);
        }
    });

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Initialize with WARN min
    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    JSONValue::Object exp; exp["logLevel"] = std::make_shared<JSONValue>(std::string("WARN"));
    JSONValue::Object caps; caps["experimental"] = std::make_shared<JSONValue>(exp);
    JSONValue::Object root; root["capabilities"] = std::make_shared<JSONValue>(caps);
    init->params.emplace(root);
    auto initRespFut = client->SendRequest(std::move(init));
    ASSERT_EQ(initRespFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Send unknown level like TRACE -> maps to DEBUG -> should be suppressed
    server.LogToClient("TRACE", "nope", std::nullopt);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(received.load(), 0);

    client->Close().get();
    server.Stop().get();
}

TEST(ServerLogging, DeliversWarnWithStructuredData) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Capture notifications/log on client side
    std::promise<JSONValue> gotPayload; auto payloadFut = gotPayload.get_future();

    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (!note) {
            return;
        }
        if (note->method == Methods::Log && note->params.has_value()) {
            try {
                gotPayload.set_value(note->params.value());
            } catch (...) {}
        }
    });

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Initialize with client capabilities.experimental.logLevel = "WARN"
    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    JSONValue::Object exp; exp["logLevel"] = std::make_shared<JSONValue>(std::string("WARN"));
    JSONValue::Object caps; caps["experimental"] = std::make_shared<JSONValue>(exp);
    JSONValue::Object root; root["capabilities"] = std::make_shared<JSONValue>(caps);
    init->params.emplace(root);
    auto initRespFut = client->SendRequest(std::move(init));
    ASSERT_EQ(initRespFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Send WARN with structured data (should be delivered at threshold)
    JSONValue::Object dataObj;
    dataObj["foo"] = std::make_shared<JSONValue>(std::string("bar"));
    dataObj["n"] = std::make_shared<JSONValue>(static_cast<int64_t>(123));
    server.LogToClient("WARN", "warn-message", JSONValue{dataObj});

    ASSERT_EQ(payloadFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    JSONValue v = payloadFut.get();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(v.value));
    const auto& o = std::get<JSONValue::Object>(v.value);
    auto itLvl = o.find("level");
    ASSERT_TRUE(itLvl != o.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(itLvl->second->value));
    EXPECT_EQ(std::get<std::string>(itLvl->second->value), std::string("WARN"));

    auto itMsg = o.find("message");
    ASSERT_TRUE(itMsg != o.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(itMsg->second->value));
    EXPECT_EQ(std::get<std::string>(itMsg->second->value), std::string("warn-message"));

    auto itData = o.find("data");
    ASSERT_TRUE(itData != o.end());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(itData->second->value));
    const auto& d = std::get<JSONValue::Object>(itData->second->value);
    auto itFoo = d.find("foo");
    ASSERT_TRUE(itFoo != d.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(itFoo->second->value));
    EXPECT_EQ(std::get<std::string>(itFoo->second->value), std::string("bar"));
    auto itN = d.find("n");
    ASSERT_TRUE(itN != d.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(itN->second->value));
    EXPECT_EQ(std::get<int64_t>(itN->second->value), static_cast<int64_t>(123));

    client->Close().get();
    server.Stop().get();
}

TEST(ServerLogging, DefaultLogLevelAllowsInfo) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::promise<std::string> gotInfo; auto gotInfoFut = gotInfo.get_future();
    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (!note) {
            return;
        }
        if (note->method == Methods::Log && note->params.has_value()) {
            const auto& v = note->params.value();
            if (std::holds_alternative<JSONValue::Object>(v.value)) {
                const auto& o = std::get<JSONValue::Object>(v.value);
                auto it = o.find("message");
                if (it != o.end() && std::holds_alternative<std::string>(it->second->value)) {
                    try {
                        gotInfo.set_value(std::get<std::string>(it->second->value));
                    } catch (...) {}
                }
            }
        }
    });

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Initialize without experimental.logLevel (defaults to DEBUG min on server)
    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    init->params.emplace(JSONValue::Object{});
    auto initRespFut = client->SendRequest(std::move(init));
    ASSERT_EQ(initRespFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    server.LogToClient("INFO", "info-ok", std::nullopt);
    ASSERT_EQ(gotInfoFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(gotInfoFut.get(), std::string("info-ok"));

    client->Close().get();
    server.Stop().get();
}

TEST(ServerLogging, InvalidClientLogLevelFallsBackToDebug) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::promise<std::string> gotInfo; auto gotInfoFut = gotInfo.get_future();
    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (!note) return;
        if (note->method == Methods::Log && note->params.has_value()) {
            const auto& v = note->params.value();
            if (std::holds_alternative<JSONValue::Object>(v.value)) {
                const auto& o = std::get<JSONValue::Object>(v.value);
                auto it = o.find("message");
                if (it != o.end() && std::holds_alternative<std::string>(it->second->value)) {
                    try { gotInfo.set_value(std::get<std::string>(it->second->value)); } catch (...) {}
                }
            }
        }
    });

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Initialize with an invalid experimental.logLevel; server should default to DEBUG min
    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    JSONValue::Object exp; exp["logLevel"] = std::make_shared<JSONValue>(std::string("NOPE"));
    JSONValue::Object caps; caps["experimental"] = std::make_shared<JSONValue>(exp);
    JSONValue::Object root; root["capabilities"] = std::make_shared<JSONValue>(caps);
    init->params.emplace(root);
    auto initRespFut = client->SendRequest(std::move(init));
    ASSERT_EQ(initRespFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // INFO should be delivered because min falls back to DEBUG
    server.LogToClient("INFO", "info-delivered", std::nullopt);
    ASSERT_EQ(gotInfoFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(gotInfoFut.get(), std::string("info-delivered"));

    client->Close().get();
    server.Stop().get();
}

TEST(ServerLogging, OmitsDataFieldWhenNotProvided) {
    auto pair = InMemoryTransport::CreatePair();
    auto client = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    std::promise<JSONValue> gotPayload; auto payloadFut = gotPayload.get_future();
    client->SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        if (!note) return;
        if (note->method == Methods::Log && note->params.has_value()) {
            try { gotPayload.set_value(note->params.value()); } catch (...) {}
        }
    });

    Server server("TestServer");
    server.Start(std::move(serverTrans)).get();
    client->Start().get();

    // Initialize without logLevel capability; default min is DEBUG so INFO is allowed
    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    init->params.emplace(JSONValue::Object{});
    auto initRespFut = client->SendRequest(std::move(init));
    ASSERT_EQ(initRespFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    server.LogToClient("INFO", "no-data", std::nullopt);
    ASSERT_EQ(payloadFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    JSONValue v = payloadFut.get();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(v.value));
    const auto& o = std::get<JSONValue::Object>(v.value);
    // Ensure 'data' key is absent
    EXPECT_TRUE(o.find("data") == o.end());

    client->Close().get();
    server.Stop().get();
}

