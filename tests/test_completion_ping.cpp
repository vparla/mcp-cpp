//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_completion_ping.cpp
// Purpose: End-to-end and strict-validation tests for completion/complete and ping
//==========================================================================================================

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>

#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/Server.h"
#include "mcp/validation/Validation.h"

using namespace mcp;

namespace {

JSONValue makeCompletionRef(const std::string& kind, const std::string& name) {
    JSONValue::Object obj;
    obj["type"] = std::make_shared<JSONValue>(kind);
    obj["name"] = std::make_shared<JSONValue>(name);
    return JSONValue{obj};
}

JSONValue makeInitializeResult(const bool advertiseCompletions) {
    JSONValue::Object result;
    result["protocolVersion"] = std::make_shared<JSONValue>(std::string(PROTOCOL_VERSION));

    JSONValue::Object serverInfo;
    serverInfo["name"] = std::make_shared<JSONValue>(std::string("StrictCompletionServer"));
    serverInfo["version"] = std::make_shared<JSONValue>(std::string("1.0.0"));
    result["serverInfo"] = std::make_shared<JSONValue>(serverInfo);

    JSONValue::Object capabilities;
    if (advertiseCompletions) {
        capabilities["completions"] = std::make_shared<JSONValue>(JSONValue::Object{});
    }
    result["capabilities"] = std::make_shared<JSONValue>(capabilities);
    return JSONValue{result};
}

}  // namespace

TEST(CompletionPing, CompletionRoundTripAndPingBothDirections) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Completion Server");
    server.SetValidationMode(validation::ValidationMode::Strict);
    server.SetCompletionHandler([](const CompleteParams& params) -> std::future<CompletionResult> {
        return std::async(std::launch::deferred, [params]() {
            CompletionResult out;
            out.values = {
                params.argument.value + std::string("/alpha"),
                params.argument.value + std::string("/beta"),
            };
            out.total = 2;
            out.hasMore = false;
            return out;
        });
    });
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Completion Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);

    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps;
    auto initFuture = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto serverCaps = initFuture.get();
    ASSERT_TRUE(serverCaps.completions.has_value());

    CompleteParams params;
    params.ref = makeCompletionRef("tool", "filesystem");
    params.argument = CompleteArgument{"path", "/tmp"};
    auto completeFuture = client->Complete(params);
    ASSERT_EQ(completeFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto completion = completeFuture.get();

    ASSERT_EQ(completion.values.size(), 2u);
    EXPECT_EQ(completion.values[0], "/tmp/alpha");
    EXPECT_EQ(completion.values[1], "/tmp/beta");
    ASSERT_TRUE(completion.total.has_value());
    EXPECT_EQ(completion.total.value(), 2);
    EXPECT_FALSE(completion.hasMore);

    ASSERT_NO_THROW(client->Ping().get());
    ASSERT_NO_THROW(server.Ping().get());

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(CompletionPing, StrictModeRejectsInvalidCompletionAndPingShapes) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    ASSERT_NO_THROW(serverTransport->Start().get());
    serverTransport->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification>) {});
    serverTransport->SetRequestHandler([](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = req.id;
        if (req.method == Methods::Initialize) {
            resp->result = makeInitializeResult(true);
            return resp;
        }
        if (req.method == Methods::Complete) {
            JSONValue::Object completion;
            JSONValue::Array values;
            values.push_back(std::make_shared<JSONValue>(static_cast<int64_t>(7)));
            completion["values"] = std::make_shared<JSONValue>(values);
            JSONValue::Object result;
            result["completion"] = std::make_shared<JSONValue>(completion);
            resp->result = JSONValue{result};
            return resp;
        }
        if (req.method == Methods::Ping) {
            JSONValue::Array badResult;
            badResult.push_back(std::make_shared<JSONValue>(std::string("pong")));
            resp->result = JSONValue{badResult};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    ClientFactory factory;
    Implementation clientInfo{"Strict Completion Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps;
    auto initFuture = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFuture.get();

    CompleteParams params;
    params.ref = makeCompletionRef("tool", "filesystem");
    params.argument = CompleteArgument{"path", "/tmp"};
    EXPECT_THROW((void)client->Complete(params).get(), std::runtime_error);
    EXPECT_THROW((void)client->Ping().get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(serverTransport->Close().get());
}
