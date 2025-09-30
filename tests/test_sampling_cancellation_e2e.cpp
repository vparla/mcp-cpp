//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_sampling_cancellation_e2e.cpp
// Purpose: E2E test for server-initiated sampling cancellation (server -> client RequestCreateMessage)
//==========================================================================================================

#include <gtest/gtest.h>
#include <future>
#include <chrono>
#include <thread>
#include <atomic>

#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"

using namespace mcp;

TEST(SamplingCancellationE2E, ServerCancelsClientCreateMessageById) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());

    // Register a cancelable sampling handler that waits for cancellation
    std::atomic<bool> started{false};
    client->SetSamplingHandlerCancelable([&](const JSONValue& messages,
                                             const JSONValue& modelPreferences,
                                             const JSONValue& systemPrompt,
                                             const JSONValue& includeContext,
                                             std::stop_token st){
        (void)messages; (void)modelPreferences; (void)systemPrompt; (void)includeContext;
        return std::async(std::launch::async, [&, st]() mutable {
            started.store(true);
            // Wait up to 2s or until cancellation requested
            for (int i = 0; i < 200; ++i) {
                if (st.stop_requested()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            // Return any result; server/client RPC layer should convert to Cancelled if stop was requested
            JSONValue::Object resultObj;
            resultObj["model"] = std::make_shared<JSONValue>(std::string("cancel-model"));
            resultObj["role"] = std::make_shared<JSONValue>(std::string("assistant"));
            JSONValue::Array contentArr; JSONValue::Object t; t["type"] = std::make_shared<JSONValue>(std::string("text")); t["text"] = std::make_shared<JSONValue>(std::string("ignored"));
            contentArr.push_back(std::make_shared<JSONValue>(t));
            resultObj["content"] = std::make_shared<JSONValue>(contentArr);
            return JSONValue{resultObj};
        });
    });

    // Issue server->client createMessage with a known id so we can cancel it
    CreateMessageParams p; p.messages = { JSONValue{JSONValue::Object{}} };
    const std::string rid = "samp-1";
    auto fut = server.RequestCreateMessageWithId(p, rid);

    // Give client a moment to start the handler
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Send server-side cancellation notification targeting the same id
    JSONValue::Object cancelParams; cancelParams["id"] = std::make_shared<JSONValue>(rid);
    ASSERT_NO_THROW(server.SendNotification(Methods::Cancelled, JSONValue{cancelParams}).get());

    ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto respVal = fut.get();

    // Expect an error value with message "Cancelled"
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(respVal.value));
    const auto& obj = std::get<JSONValue::Object>(respVal.value);
    auto itMsg = obj.find("message");
    ASSERT_TRUE(itMsg != obj.end());
    ASSERT_TRUE(std::holds_alternative<std::string>(itMsg->second->value));
    EXPECT_EQ(std::get<std::string>(itMsg->second->value), std::string("Cancelled"));
}
