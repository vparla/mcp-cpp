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
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "mcp/Client.h"
#include "mcp/HTTPServer.hpp"
#include "mcp/HTTPTransport.hpp"
#include "mcp/JSONRPCTypes.h"
#include "mcp/Protocol.h"
#include "mcp/Server.h"
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"
#include "mcp/validation/Validation.h"

using namespace mcp;
namespace beast = boost::beast;
namespace http = beast::http;

namespace {

struct RawHttpResponse {
    int status{0};
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

struct RawSseEvent {
    std::string id;
    std::string data;
};

struct RawSseStream {
    boost::asio::io_context io;
    beast::tcp_stream stream;
    beast::flat_buffer buffer;
    int status{0};
    std::unordered_map<std::string, std::string> headers;
    std::string pending;

    RawSseStream() : stream(io) {}
};

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

RawHttpResponse sendHttpRequest(
    const unsigned short port,
    const http::verb method,
    const std::string& target,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers = {}) {
    boost::asio::io_context io;
    boost::asio::ip::tcp::resolver resolver(io);
    beast::tcp_stream stream(io);
    auto results = resolver.resolve("127.0.0.1", std::to_string(port));
    stream.connect(results);

    http::request<http::string_body> request{method, target, 11};
    request.set(http::field::host, "127.0.0.1");
    for (const auto& header : headers) {
        request.set(header.first, header.second);
    }
    request.body() = body;
    request.prepare_payload();
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    RawHttpResponse out;
    out.status = response.result_int();
    out.body = response.body();
    for (const auto& header : response.base()) {
        out.headers.emplace(std::string(header.name_string()), std::string(header.value()));
    }

    boost::system::error_code ec;
    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    return out;
}

void appendBufferedData(RawSseStream& stream) {
    if (stream.buffer.size() == 0) {
        return;
    }
    stream.pending += beast::buffers_to_string(stream.buffer.data());
    stream.buffer.consume(stream.buffer.size());
}

std::unique_ptr<RawSseStream> openSseStream(
    const unsigned short port,
    const std::string& target,
    const std::vector<std::pair<std::string, std::string>>& headers = {}) {
    auto out = std::make_unique<RawSseStream>();
    boost::asio::ip::tcp::resolver resolver(out->io);
    auto results = resolver.resolve("127.0.0.1", std::to_string(port));
    out->stream.connect(results);

    http::request<http::empty_body> request{http::verb::get, target, 11};
    request.set(http::field::host, "127.0.0.1");
    request.set(http::field::accept, "text/event-stream");
    for (const auto& header : headers) {
        request.set(header.first, header.second);
    }
    http::write(out->stream, request);

    http::response_parser<http::empty_body> parser;
    parser.skip(true);
    http::read_header(out->stream, out->buffer, parser);

    out->status = parser.get().result_int();
    for (const auto& header : parser.get().base()) {
        out->headers.emplace(std::string(header.name_string()), std::string(header.value()));
    }
    appendBufferedData(*out);
    return out;
}

RawSseEvent readNextSseEvent(
    RawSseStream& stream,
    const std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    auto findDelimiter = [&]() -> std::pair<size_t, size_t> {
        const auto crlf = stream.pending.find("\r\n\r\n");
        if (crlf != std::string::npos) {
            return {crlf, 4u};
        }
        const auto lf = stream.pending.find("\n\n");
        if (lf != std::string::npos) {
            return {lf, 2u};
        }
        return {std::string::npos, 0u};
    };

    while (true) {
        const auto [delimiter, delimiterSize] = findDelimiter();
        if (delimiter != std::string::npos) {
            RawSseEvent event;
            const std::string raw = stream.pending.substr(0, delimiter);
            stream.pending.erase(0, delimiter + delimiterSize);

            std::istringstream input(raw);
            std::string line;
            while (std::getline(input, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.rfind("id:", 0) == 0) {
                    event.id = line.substr(3);
                    if (!event.id.empty() && event.id.front() == ' ') {
                        event.id.erase(0, 1);
                    }
                    continue;
                }
                if (line.rfind("data:", 0) == 0) {
                    std::string value = line.substr(5);
                    if (!value.empty() && value.front() == ' ') {
                        value.erase(0, 1);
                    }
                    if (!event.data.empty()) {
                        event.data.push_back('\n');
                    }
                    event.data += value;
                }
            }
            return event;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("Timed out waiting for SSE event");
        }

        stream.stream.expires_after(std::chrono::milliseconds(250));
        char chunk[512];
        boost::system::error_code ec;
        const auto bytesRead = stream.stream.socket().read_some(boost::asio::buffer(chunk), ec);
        if (ec) {
            throw std::runtime_error(std::string("Failed reading SSE stream: ") + ec.message());
        }
        stream.pending.append(chunk, bytesRead);
    }
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

TEST(HTTPStreamable, InitializedNotificationReturnsAcceptedOnEndpoint) {
    const auto port = findFreePort();

    HTTPServer::Options serverOptions;
    serverOptions.scheme = "http";
    serverOptions.address = "127.0.0.1";
    serverOptions.port = std::to_string(port);
    serverOptions.endpointPath = "/mcp";

    auto httpServer = std::make_unique<HTTPServer>(serverOptions);
    Server server("Accepted Notification Server");
    server.SetValidationMode(validation::ValidationMode::Strict);
    ASSERT_NO_THROW(server.Start(std::unique_ptr<ITransport>(httpServer.release())).get());

    JSONRPCRequest initializeRequest;
    initializeRequest.id = static_cast<int64_t>(1);
    initializeRequest.method = Methods::Initialize;
    initializeRequest.params = makeInitializeParams();
    const auto initializeResponse = sendHttpRequest(
        port,
        http::verb::post,
        "/mcp",
        initializeRequest.Serialize(),
        {{"Content-Type", "application/json"},
         {"Accept", "application/json, text/event-stream"}});

    ASSERT_EQ(initializeResponse.status, 200);
    const auto sessionIt = initializeResponse.headers.find("Mcp-Session-Id");
    ASSERT_NE(sessionIt, initializeResponse.headers.end());
    ASSERT_FALSE(sessionIt->second.empty());

    JSONRPCNotification initialized;
    initialized.method = Methods::Initialized;
    const auto initializedResponse = sendHttpRequest(
        port,
        http::verb::post,
        "/mcp",
        initialized.Serialize(),
        {{"Content-Type", "application/json"},
         {"Accept", "application/json, text/event-stream"},
         {"Mcp-Session-Id", sessionIt->second},
         {"MCP-Protocol-Version", PROTOCOL_VERSION}});

    EXPECT_EQ(initializedResponse.status, 202);
    EXPECT_TRUE(initializedResponse.body.empty());

    ASSERT_NO_THROW(server.Stop().get());
}

TEST(HTTPStreamable, ToolCallCanRoundTripServerInitiatedSamplingResponseOverEndpoint) {
    const auto port = findFreePort();

    HTTPServer::Options serverOptions;
    serverOptions.scheme = "http";
    serverOptions.address = "127.0.0.1";
    serverOptions.port = std::to_string(port);
    serverOptions.endpointPath = "/mcp";

    auto httpServer = std::make_unique<HTTPServer>(serverOptions);
    Server server("Nested Sampling Server");
    server.SetValidationMode(validation::ValidationMode::Strict);

    Tool tool;
    tool.name = "sample-via-client";
    tool.description = "Request sampling over streamable HTTP.";
    tool.inputSchema = JSONValue{JSONValue::Object{}};
    server.RegisterTool(tool, [&server](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        return std::async(std::launch::async, [&server, st]() {
            ToolResult result;
            if (st.stop_requested()) {
                result.isError = true;
                result.content.push_back(typed::makeText("cancelled"));
                return result;
            }

            CreateMessageParams params;
            JSONValue::Object message;
            message["role"] = std::make_shared<JSONValue>(std::string("user"));
            message["content"] = std::make_shared<JSONValue>(typed::makeText("nested prompt"));
            params.messages.push_back(JSONValue{message});
            params.maxTokens = 32;

            const auto response = server.RequestCreateMessage(params).get();
            if (!std::holds_alternative<JSONValue::Object>(response.value)) {
                throw std::runtime_error("Expected object sampling result");
            }
            const auto& responseObject = std::get<JSONValue::Object>(response.value);
            const auto contentIt = responseObject.find("content");
            if (contentIt == responseObject.end() || contentIt->second == nullptr) {
                throw std::runtime_error("Expected sampling content");
            }
            const auto content = typed::getText(*contentIt->second);
            result.content.push_back(typed::makeText(content.value_or(std::string())));
            return result;
        });
    });

    ASSERT_NO_THROW(server.Start(std::unique_ptr<ITransport>(httpServer.release())).get());

    JSONRPCRequest initializeRequest;
    initializeRequest.id = static_cast<int64_t>(1);
    initializeRequest.method = Methods::Initialize;
    initializeRequest.params = makeInitializeParams();
    const auto initializeResponse = sendHttpRequest(
        port,
        http::verb::post,
        "/mcp",
        initializeRequest.Serialize(),
        {{"Content-Type", "application/json"},
         {"Accept", "application/json, text/event-stream"}});

    ASSERT_EQ(initializeResponse.status, 200);
    const auto sessionIt = initializeResponse.headers.find("Mcp-Session-Id");
    ASSERT_NE(sessionIt, initializeResponse.headers.end());
    ASSERT_FALSE(sessionIt->second.empty());

    JSONRPCNotification initialized;
    initialized.method = Methods::Initialized;
    const auto initializedResponse = sendHttpRequest(
        port,
        http::verb::post,
        "/mcp",
        initialized.Serialize(),
        {{"Content-Type", "application/json"},
         {"Accept", "application/json, text/event-stream"},
         {"Mcp-Session-Id", sessionIt->second},
         {"MCP-Protocol-Version", PROTOCOL_VERSION}});
    ASSERT_EQ(initializedResponse.status, 202);

    auto sseStream = openSseStream(
        port,
        "/mcp",
        {{"Mcp-Session-Id", sessionIt->second},
         {"MCP-Protocol-Version", PROTOCOL_VERSION}});
    ASSERT_EQ(sseStream->status, 200);

    auto toolCallFuture = std::async(std::launch::async, [&]() {
        JSONValue::Object params;
        params["name"] = std::make_shared<JSONValue>(std::string("sample-via-client"));
        params["arguments"] = std::make_shared<JSONValue>(JSONValue::Object{});

        JSONRPCRequest toolCall;
        toolCall.id = static_cast<int64_t>(2);
        toolCall.method = Methods::CallTool;
        toolCall.params = JSONValue{params};
        return sendHttpRequest(
            port,
            http::verb::post,
            "/mcp",
            toolCall.Serialize(),
            {{"Content-Type", "application/json"},
             {"Accept", "application/json, text/event-stream"},
             {"Mcp-Session-Id", sessionIt->second},
             {"MCP-Protocol-Version", PROTOCOL_VERSION}});
    });

    JSONRPCRequest samplingRequest;
    bool sawSamplingRequest = false;
    for (int attempt = 0; attempt < 6 && !sawSamplingRequest; ++attempt) {
        const auto event = readNextSseEvent(*sseStream);
        if (event.data.empty()) {
            continue;
        }
        JSONRPCRequest maybeRequest;
        if (!maybeRequest.Deserialize(event.data)) {
            continue;
        }
        if (maybeRequest.method == Methods::CreateMessage) {
            samplingRequest = maybeRequest;
            sawSamplingRequest = true;
        }
    }
    ASSERT_TRUE(sawSamplingRequest);

    JSONValue::Object samplingResult;
    samplingResult["role"] = std::make_shared<JSONValue>(std::string("assistant"));
    samplingResult["content"] = std::make_shared<JSONValue>(typed::makeText("sampled-text"));
    samplingResult["model"] = std::make_shared<JSONValue>(std::string("test-model"));
    samplingResult["stopReason"] = std::make_shared<JSONValue>(std::string("endTurn"));

    JSONRPCResponse samplingResponse;
    samplingResponse.id = samplingRequest.id;
    samplingResponse.result = JSONValue{samplingResult};
    const auto responsePost = sendHttpRequest(
        port,
        http::verb::post,
        "/mcp",
        samplingResponse.Serialize(),
        {{"Content-Type", "application/json"},
         {"Accept", "application/json, text/event-stream"},
         {"Mcp-Session-Id", sessionIt->second},
         {"MCP-Protocol-Version", PROTOCOL_VERSION}});

    EXPECT_EQ(responsePost.status, 202);
    EXPECT_TRUE(responsePost.body.empty());

    ASSERT_EQ(toolCallFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto toolCallResponse = toolCallFuture.get();
    ASSERT_EQ(toolCallResponse.status, 200);

    JSONRPCResponse toolResponse;
    ASSERT_TRUE(toolResponse.Deserialize(toolCallResponse.body));
    ASSERT_TRUE(toolResponse.result.has_value());
    const auto texts = typed::collectText(typed::parseCallToolResult(toolResponse.result.value()));
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_EQ(texts.front(), "sampled-text");

    boost::system::error_code ec;
    sseStream->stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(HTTPStreamable, ToolCallProgressUsesCallerProvidedProgressToken) {
    const auto port = findFreePort();

    HTTPServer::Options serverOptions;
    serverOptions.scheme = "http";
    serverOptions.address = "127.0.0.1";
    serverOptions.port = std::to_string(port);
    serverOptions.endpointPath = "/mcp";

    auto httpServer = std::make_unique<HTTPServer>(serverOptions);
    Server server("Progress Token Server");
    server.SetValidationMode(validation::ValidationMode::Strict);

    Tool tool;
    tool.name = "emit-progress";
    tool.description = "Emit progress using the current request token.";
    tool.inputSchema = JSONValue{JSONValue::Object{}};
    server.RegisterTool(tool, [&server](const JSONValue&, std::stop_token st) -> std::future<ToolResult> {
        const auto progressToken = CurrentProgressToken().value_or(std::string("fallback-token"));
        return std::async(std::launch::async, [&server, st, progressToken]() {
            if (!st.stop_requested()) {
                server.SendProgress(progressToken, 0.25, "quarter").get();
            }
            ToolResult result;
            result.content.push_back(typed::makeText("done"));
            return result;
        });
    });

    ASSERT_NO_THROW(server.Start(std::unique_ptr<ITransport>(httpServer.release())).get());

    JSONRPCRequest initializeRequest;
    initializeRequest.id = static_cast<int64_t>(1);
    initializeRequest.method = Methods::Initialize;
    initializeRequest.params = makeInitializeParams();
    const auto initializeResponse = sendHttpRequest(
        port,
        http::verb::post,
        "/mcp",
        initializeRequest.Serialize(),
        {{"Content-Type", "application/json"},
         {"Accept", "application/json, text/event-stream"}});

    ASSERT_EQ(initializeResponse.status, 200);
    const auto sessionIt = initializeResponse.headers.find("Mcp-Session-Id");
    ASSERT_NE(sessionIt, initializeResponse.headers.end());
    ASSERT_FALSE(sessionIt->second.empty());

    JSONRPCNotification initialized;
    initialized.method = Methods::Initialized;
    const auto initializedResponse = sendHttpRequest(
        port,
        http::verb::post,
        "/mcp",
        initialized.Serialize(),
        {{"Content-Type", "application/json"},
         {"Accept", "application/json, text/event-stream"},
         {"Mcp-Session-Id", sessionIt->second},
         {"MCP-Protocol-Version", PROTOCOL_VERSION}});
    ASSERT_EQ(initializedResponse.status, 202);

    auto sseStream = openSseStream(
        port,
        "/mcp",
        {{"Mcp-Session-Id", sessionIt->second},
         {"MCP-Protocol-Version", PROTOCOL_VERSION}});
    ASSERT_EQ(sseStream->status, 200);

    auto toolCallFuture = std::async(std::launch::async, [&]() {
        JSONValue::Object meta;
        meta["progressToken"] = std::make_shared<JSONValue>(std::string("custom-progress-42"));

        JSONValue::Object params;
        params["name"] = std::make_shared<JSONValue>(std::string("emit-progress"));
        params["arguments"] = std::make_shared<JSONValue>(JSONValue::Object{});
        params["_meta"] = std::make_shared<JSONValue>(meta);

        JSONRPCRequest toolCall;
        toolCall.id = static_cast<int64_t>(2);
        toolCall.method = Methods::CallTool;
        toolCall.params = JSONValue{params};
        return sendHttpRequest(
            port,
            http::verb::post,
            "/mcp",
            toolCall.Serialize(),
            {{"Content-Type", "application/json"},
             {"Accept", "application/json, text/event-stream"},
             {"Mcp-Session-Id", sessionIt->second},
             {"MCP-Protocol-Version", PROTOCOL_VERSION}});
    });

    JSONRPCNotification progressNotification;
    bool sawProgress = false;
    for (int attempt = 0; attempt < 6 && !sawProgress; ++attempt) {
        const auto event = readNextSseEvent(*sseStream);
        if (event.data.empty()) {
            continue;
        }
        JSONRPCNotification maybeNotification;
        if (!maybeNotification.Deserialize(event.data)) {
            continue;
        }
        if (maybeNotification.method == Methods::Progress) {
            progressNotification = maybeNotification;
            sawProgress = true;
        }
    }
    ASSERT_TRUE(sawProgress);
    ASSERT_TRUE(progressNotification.params.has_value());
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(progressNotification.params->value));
    const auto& progressParams = std::get<JSONValue::Object>(progressNotification.params->value);
    ASSERT_TRUE(progressParams.contains("progressToken"));
    ASSERT_TRUE(std::holds_alternative<std::string>(progressParams.at("progressToken")->value));
    EXPECT_EQ(std::get<std::string>(progressParams.at("progressToken")->value), "custom-progress-42");

    ASSERT_EQ(toolCallFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto toolCallResponse = toolCallFuture.get();
    ASSERT_EQ(toolCallResponse.status, 200);

    boost::system::error_code ec;
    sseStream->stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    ASSERT_NO_THROW(server.Stop().get());
}
