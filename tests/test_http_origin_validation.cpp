//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_http_origin_validation.cpp
// Purpose: Tests loopback Host/Origin validation for HTTPServer streamable HTTP endpoints
//==========================================================================================================

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "mcp/HTTPServer.hpp"
#include "mcp/Server.h"

using namespace mcp;

namespace {
namespace asio = boost::asio;
namespace http = boost::beast::http;
using tcp = asio::ip::tcp;

unsigned short findFreePort() {
    asio::io_context io;
    tcp::acceptor acceptor(io, {tcp::v4(), 0});
    const unsigned short port = acceptor.local_endpoint().port();
    boost::system::error_code ec;
    acceptor.close(ec);
    return port;
}

http::response<http::string_body> sendInitializeRequest(unsigned short port,
                                                        const std::string& hostHeader,
                                                        const std::string& originHeader) {
    asio::io_context io;
    tcp::resolver resolver(io);
    boost::beast::tcp_stream stream(io);
    const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
    stream.connect(endpoints);

    http::request<http::string_body> request{http::verb::post, "/mcp", 11};
    request.set(http::field::host, hostHeader);
    request.set(http::field::content_type, "application/json");
    request.set(http::field::accept, "application/json, text/event-stream");
    request.set("Origin", originHeader);
    request.body() =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\","
        "\"capabilities\":{},\"clientInfo\":{\"name\":\"origin-test\",\"version\":\"1.0.0\"}}}";
    request.prepare_payload();
    http::write(stream, request);

    boost::beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    boost::system::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    return response;
}

}  // namespace

TEST(HTTPOriginValidation, RejectsNonLocalHostAndOriginOnLoopbackBinding) {
    HTTPServer::Options options;
    options.scheme = "http";
    options.address = "127.0.0.1";
    options.port = std::to_string(findFreePort());
    options.endpointPath = "/mcp";

    Server server("HTTP Origin Validation Server");
    ASSERT_NO_THROW(server.Start(std::make_unique<HTTPServer>(options)).get());

    const auto response = sendInitializeRequest(
        static_cast<unsigned short>(std::stoi(options.port)),
        "evil.example.com",
        "http://evil.example.com");

    EXPECT_EQ(response.result(), http::status::forbidden);
    EXPECT_NE(response.body().find("Forbidden"), std::string::npos);

    ASSERT_NO_THROW(server.Stop().get());
}

TEST(HTTPOriginValidation, AcceptsLocalHostAndOriginOnLoopbackBinding) {
    HTTPServer::Options options;
    options.scheme = "http";
    options.address = "127.0.0.1";
    options.port = std::to_string(findFreePort());
    options.endpointPath = "/mcp";

    Server server("HTTP Origin Validation Server");
    ASSERT_NO_THROW(server.Start(std::make_unique<HTTPServer>(options)).get());

    const auto response = sendInitializeRequest(
        static_cast<unsigned short>(std::stoi(options.port)),
        std::string("127.0.0.1:") + options.port,
        std::string("http://127.0.0.1:") + options.port);

    EXPECT_EQ(response.result(), http::status::ok);
    EXPECT_FALSE(response.body().empty());

    ASSERT_NO_THROW(server.Stop().get());
}
