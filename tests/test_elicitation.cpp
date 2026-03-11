//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_elicitation.cpp
// Purpose: End-to-end and strict-validation tests for elicitation/create
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

JSONValue makeInitializeRequestParams(const bool advertiseElicitation) {
    JSONValue::Object params;
    params["protocolVersion"] = std::make_shared<JSONValue>(std::string(PROTOCOL_VERSION));

    JSONValue::Object caps;
    if (advertiseElicitation) {
        JSONValue::Object elicitation;
        JSONValue::Array modes;
        modes.push_back(std::make_shared<JSONValue>(std::string("form")));
        modes.push_back(std::make_shared<JSONValue>(std::string("inline")));
        elicitation["modes"] = std::make_shared<JSONValue>(modes);
        caps["elicitation"] = std::make_shared<JSONValue>(elicitation);
    }
    params["capabilities"] = std::make_shared<JSONValue>(caps);

    JSONValue::Object clientInfo;
    clientInfo["name"] = std::make_shared<JSONValue>(std::string("Elicitation Client"));
    clientInfo["version"] = std::make_shared<JSONValue>(std::string("1.0.0"));
    params["clientInfo"] = std::make_shared<JSONValue>(clientInfo);
    return JSONValue{params};
}

JSONValue makeRequestedSchema() {
    JSONValue::Object schema;
    schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    JSONValue::Object properties;
    JSONValue::Object answer;
    answer["type"] = std::make_shared<JSONValue>(std::string("string"));
    properties["answer"] = std::make_shared<JSONValue>(answer);
    schema["properties"] = std::make_shared<JSONValue>(properties);
    return JSONValue{schema};
}

}  // namespace

TEST(Elicitation, ServerRequestsStructuredInputFromClient) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Elicitation Server");
    server.SetValidationMode(validation::ValidationMode::Strict);
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Elicitation Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);
    client->SetElicitationHandler([](const ElicitationRequest& request) -> std::future<ElicitationResult> {
        return std::async(std::launch::deferred, [request]() {
            ElicitationResult result;
            result.action = "accept";
            JSONValue::Object content;
            content["answer"] = std::make_shared<JSONValue>(request.message + std::string(" acknowledged"));
            result.content = JSONValue{content};
            result.elicitationId = request.elicitationId;
            return result;
        });
    });

    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities caps;
    caps.elicitation = ElicitationCapability{{"form", "inline"}};
    auto initFuture = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFuture.get();

    ElicitationRequest request;
    request.message = "Need approval";
    request.title = "Approval";
    request.mode = "form";
    request.elicitationId = "elic-1";
    request.requestedSchema = makeRequestedSchema();

    auto elicitationFuture = server.RequestElicitation(request);
    ASSERT_EQ(elicitationFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto result = elicitationFuture.get();

    EXPECT_EQ(result.action, "accept");
    ASSERT_TRUE(result.elicitationId.has_value());
    EXPECT_EQ(result.elicitationId.value(), "elic-1");
    ASSERT_TRUE(result.content.has_value());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(result.content->value));
    const auto& content = std::get<JSONValue::Object>(result.content->value);
    auto answerIt = content.find("answer");
    ASSERT_TRUE(answerIt != content.end());
    ASSERT_TRUE(answerIt->second);
    ASSERT_TRUE(std::holds_alternative<std::string>(answerIt->second->value));
    EXPECT_EQ(std::get<std::string>(answerIt->second->value), "Need approval acknowledged");

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(Elicitation, StrictModeRejectsInvalidResultShape) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Strict Elicitation Server");
    server.SetValidationMode(validation::ValidationMode::Strict);
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ASSERT_NO_THROW(clientTransport->Start().get());
    clientTransport->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification>) {});
    clientTransport->SetRequestHandler([](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (req.method == Methods::Elicit) {
            JSONValue::Object result;
            result["action"] = std::make_shared<JSONValue>(std::string("maybe"));
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id;
            resp->result = JSONValue{result};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    auto init = std::make_unique<JSONRPCRequest>();
    init->method = Methods::Initialize;
    init->params = makeInitializeRequestParams(true);
    auto initFuture = clientTransport->SendRequest(std::move(init));
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto initResponse = initFuture.get();
    ASSERT_TRUE(initResponse != nullptr);
    ASSERT_FALSE(initResponse->IsError());

    ElicitationRequest request;
    request.message = "Need approval";
    request.requestedSchema = makeRequestedSchema();
    request.mode = "form";
    EXPECT_THROW((void)server.RequestElicitation(request).get(), std::runtime_error);

    ASSERT_NO_THROW(clientTransport->Close().get());
    ASSERT_NO_THROW(server.Stop().get());
}
