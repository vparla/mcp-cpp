//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_http_streamable.cpp
// Purpose: Streamable HTTP end-to-end tests for session lifecycle and bidirectional requests
//==========================================================================================================

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <boost/asio.hpp>

#include "mcp/Client.h"
#include "mcp/HTTPServer.hpp"
#include "mcp/HTTPTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/Server.h"
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"
#include "mcp/validation/Validation.h"

using namespace mcp;

namespace {

unsigned short findFreePort() {
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor acceptor(io, {boost::asio::ip::tcp::v4(), 0});
    const auto port = acceptor.local_endpoint().port();
    boost::system::error_code ec;
    acceptor.close(ec);
    return port;
}

template <typename Predicate>
bool waitUntil(Predicate&& predicate,
               const std::chrono::milliseconds timeout = std::chrono::milliseconds(2000),
               const std::chrono::milliseconds interval = std::chrono::milliseconds(25)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(interval);
    }
    return predicate();
}

JSONValue makeInitializeParams() {
    JSONValue::Object params;
    params["protocolVersion"] = std::make_shared<JSONValue>(std::string(PROTOCOL_VERSION));
    params["capabilities"] = std::make_shared<JSONValue>(JSONValue::Object{});

    JSONValue::Object clientInfo;
    clientInfo["name"] = std::make_shared<JSONValue>(std::string("Factory Client"));
    clientInfo["version"] = std::make_shared<JSONValue>(std::string("1.0.0"));
    params["clientInfo"] = std::make_shared<JSONValue>(clientInfo);
    return JSONValue{params};
}

JSONValue makeInitializeResult() {
    JSONValue::Object result;
    result["protocolVersion"] = std::make_shared<JSONValue>(std::string(PROTOCOL_VERSION));

    JSONValue::Object serverInfo;
    serverInfo["name"] = std::make_shared<JSONValue>(std::string("Factory Server"));
    serverInfo["version"] = std::make_shared<JSONValue>(std::string("1.0.0"));
    result["serverInfo"] = std::make_shared<JSONValue>(serverInfo);
    result["capabilities"] = std::make_shared<JSONValue>(JSONValue::Object{});
    return JSONValue{result};
}

}  // namespace

TEST(HTTPStreamable, ServerAndClientRoundTripOverStreamableHttp) {
    const auto port = findFreePort();

    HTTPServer::Options serverOptions;
    serverOptions.scheme = "http";
    serverOptions.address = "127.0.0.1";
    serverOptions.port = std::to_string(port);
    serverOptions.endpointPath = "/mcp";
    serverOptions.streamPath = "/events";

    auto httpServer = std::make_unique<HTTPServer>(serverOptions);
    auto* httpServerRaw = httpServer.get();

    Server server("Streamable HTTP Server");
    server.SetValidationMode(validation::ValidationMode::Strict);

    Tool tool;
    tool.name = "echo";
    tool.description = "echo";
    tool.inputSchema = JSONValue{JSONValue::Object{}};
    server.RegisterTool(tool, [](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        (void)st;
        return std::async(std::launch::deferred, []() {
            ToolResult result;
            result.content.push_back(typed::makeText("http-ok"));
            return result;
        });
    });

    ASSERT_NO_THROW(server.Start(std::unique_ptr<ITransport>(httpServer.release())).get());

    HTTPTransport::Options transportOptions;
    transportOptions.scheme = "http";
    transportOptions.host = "127.0.0.1";
    transportOptions.port = std::to_string(port);
    transportOptions.endpointPath = "/mcp";
    transportOptions.streamPath = "/events";
    transportOptions.connectTimeoutMs = 500;
    transportOptions.readTimeoutMs = 1500;

    auto httpTransport = std::make_unique<HTTPTransport>(transportOptions);
    auto* httpTransportRaw = httpTransport.get();

    ClientFactory factory;
    Implementation clientInfo{"Streamable HTTP Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);
    client->SetRootsListHandler([]() -> std::future<RootsListResult> {
        return std::async(std::launch::deferred, []() {
            RootsListResult result;
            result.roots.push_back(Root{"file:///workspace/http", std::optional<std::string>{"http-root"}});
            return result;
        });
    });

    ASSERT_NO_THROW(client->Connect(std::unique_ptr<ITransport>(httpTransport.release())).get());

    ClientCapabilities caps;
    caps.roots = RootsCapability{true};
    auto initFuture = client->Initialize(clientInfo, caps);
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto serverCaps = initFuture.get();
    ASSERT_TRUE(serverCaps.tools.has_value());

    ASSERT_TRUE(waitUntil([&]() {
        return !httpTransportRaw->GetSessionId().empty() && !httpServerRaw->GetSessionId().empty();
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    JSONValue emptyArgs{JSONValue::Object{}};
    auto toolFuture = typed::callTool(*client, "echo", emptyArgs);
    ASSERT_EQ(toolFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto toolResult = toolFuture.get();
    auto text = typed::firstText(toolResult);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(text.value(), "http-ok");

    auto rootsFuture = server.RequestRootsList();
    ASSERT_EQ(rootsFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto roots = rootsFuture.get();
    ASSERT_EQ(roots.roots.size(), 1u);
    EXPECT_EQ(roots.roots.front().uri, "file:///workspace/http");

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_TRUE(waitUntil([&]() { return httpServerRaw->GetSessionId().empty(); }));
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(HTTPStreamable, FactoryParsesStreamableEndpointAndStreamPaths) {
    const auto port = findFreePort();

    HTTPServerFactory factory;
    const std::string config =
        std::string("http://127.0.0.1:") + std::to_string(port) +
        std::string("?endpointPath=/mcp&streamPath=/events");
    auto acceptor = factory.CreateTransportAcceptor(config);
    auto* httpServer = dynamic_cast<HTTPServer*>(acceptor.get());
    ASSERT_NE(httpServer, nullptr);

    acceptor->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification>) {});
    acceptor->SetErrorHandler([](const std::string&) {});
    acceptor->SetRequestHandler([](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = req.id;
        if (req.method == Methods::Initialize || req.method == Methods::Ping) {
            resp->result = req.method == Methods::Initialize ? makeInitializeResult() : JSONValue{JSONValue::Object{}};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    ASSERT_NO_THROW(acceptor->Start().get());

    HTTPTransport::Options transportOptions;
    transportOptions.scheme = "http";
    transportOptions.host = "127.0.0.1";
    transportOptions.port = std::to_string(port);
    transportOptions.endpointPath = "/mcp";
    transportOptions.streamPath = "/events";
    transportOptions.connectTimeoutMs = 500;
    transportOptions.readTimeoutMs = 1500;

    HTTPTransport transport(transportOptions);
    transport.SetRequestHandler([](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = req.id;
        if (req.method == Methods::Ping) {
            resp->result = JSONValue{JSONValue::Object{}};
            return resp;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    ASSERT_NO_THROW(transport.Start().get());

    auto initialize = std::make_unique<JSONRPCRequest>();
    initialize->method = Methods::Initialize;
    initialize->params = makeInitializeParams();
    auto initFuture = transport.SendRequest(std::move(initialize));
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto initResponse = initFuture.get();
    ASSERT_TRUE(initResponse != nullptr);
    ASSERT_FALSE(initResponse->IsError());
    ASSERT_TRUE(waitUntil([&]() { return !httpServer->GetSessionId().empty(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto ping = std::make_unique<JSONRPCRequest>();
    ping->method = Methods::Ping;
    auto pingFuture = httpServer->SendRequest(std::move(ping));
    ASSERT_EQ(pingFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto pingResponse = pingFuture.get();
    ASSERT_TRUE(pingResponse != nullptr);
    ASSERT_FALSE(pingResponse->IsError());

    ASSERT_NO_THROW(transport.Close().get());
    ASSERT_TRUE(waitUntil([&]() { return httpServer->GetSessionId().empty(); }));
    ASSERT_NO_THROW(acceptor->Stop().get());
}
