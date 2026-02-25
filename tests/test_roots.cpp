//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_roots.cpp
// Purpose: E2E and strict-validation tests for roots capability and roots/list (server -> client)
//==========================================================================================================

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <optional>
#include <string>

#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/Server.h"
#include "mcp/validation/Validation.h"

using namespace mcp;

TEST(Roots, ServerRequestsRootsListFromClient) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("RootsTestServer");
    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory factory;
    Implementation info{"RootsTestClient", "1.0.0"};
    auto client = factory.CreateClient(info);
    client->SetRootsListHandler([]() -> std::future<RootsListResult> {
        return std::async(std::launch::deferred, []() {
            RootsListResult out;
            out.roots.push_back(Root{"file:///workspace/project", std::optional<std::string>{"project"}});
            out.roots.push_back(Root{"file:///workspace/shared"});
            return out;
        });
    });

    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());

    ClientCapabilities caps;
    caps.roots = RootsCapability{true};
    auto initFut = client->Initialize(info, caps);
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFut.get();

    auto rootsFut = server.RequestRootsList();
    ASSERT_EQ(rootsFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto rootsResult = rootsFut.get();
    ASSERT_EQ(rootsResult.roots.size(), 2u);
    EXPECT_EQ(rootsResult.roots[0].uri, "file:///workspace/project");
    ASSERT_TRUE(rootsResult.roots[0].name.has_value());
    EXPECT_EQ(rootsResult.roots[0].name.value(), "project");
    EXPECT_EQ(rootsResult.roots[1].uri, "file:///workspace/shared");
    EXPECT_FALSE(rootsResult.roots[1].name.has_value());

    ASSERT_NO_THROW(client->NotifyRootsListChanged().get());
    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(Roots, StrictModeRejectsInvalidRootsListShape) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("RootsStrictServer");
    server.SetValidationMode(mcp::validation::ValidationMode::Strict);
    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());
    ASSERT_NO_THROW(clientTrans->Start().get());

    clientTrans->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification> note) {
        (void)note;
    });

    clientTrans->SetRequestHandler([](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::ListRoots) {
            JSONValue::Object resultObj;
            JSONValue::Array roots;
            JSONValue::Object badRoot;
            badRoot["name"] = std::make_shared<JSONValue>(std::string("missing-uri"));
            roots.push_back(std::make_shared<JSONValue>(badRoot));
            resultObj["roots"] = std::make_shared<JSONValue>(roots);
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = JSONValue{resultObj};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    JSONValue::Object initParams;
    initParams["protocolVersion"] = std::make_shared<JSONValue>(std::string(PROTOCOL_VERSION));
    JSONValue::Object capsObj;
    JSONValue::Object rootsCapsObj;
    rootsCapsObj["listChanged"] = std::make_shared<JSONValue>(true);
    capsObj["roots"] = std::make_shared<JSONValue>(rootsCapsObj);
    initParams["capabilities"] = std::make_shared<JSONValue>(capsObj);
    JSONValue::Object clientInfo;
    clientInfo["name"] = std::make_shared<JSONValue>(std::string("StrictRootsClient"));
    clientInfo["version"] = std::make_shared<JSONValue>(std::string("1.0.0"));
    initParams["clientInfo"] = std::make_shared<JSONValue>(clientInfo);
    init->params = JSONValue{initParams};

    auto initFut = clientTrans->SendRequest(std::move(init));
    ASSERT_EQ(initFut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto initResp = initFut.get();
    ASSERT_TRUE(initResp != nullptr);
    ASSERT_FALSE(initResp->IsError());

    EXPECT_THROW((void)server.RequestRootsList().get(), std::runtime_error);

    ASSERT_NO_THROW(clientTrans->Close().get());
    ASSERT_NO_THROW(server.Stop().get());
}
