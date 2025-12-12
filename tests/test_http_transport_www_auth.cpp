//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_http_transport_www_auth.cpp
// Purpose: HTTPTransport tests for capturing WWW-Authenticate and exposing last HTTP response
//==========================================================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <sstream>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "mcp/HTTPTransport.hpp"
#include "mcp/JSONRPCTypes.h"
#include "mcp/auth/WwwAuthenticate.hpp"

using namespace std::chrono_literals;

namespace {

class MiniAuthServer {
public:
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor acceptor{io};
    std::thread thr;
    std::atomic<bool> running{false};
    unsigned short port{0};

    static void writeResponseWithStatusAndHeader(
        boost::beast::tcp_stream& stream,
        const boost::beast::http::request<boost::beast::http::string_body>& req,
        boost::beast::http::status statusCode,
        const std::string& headerValue) {
        namespace http = boost::beast::http;
        http::response<http::string_body> res{ statusCode, req.version() };
        res.set(http::field::server, "mini-auth-server");
        res.set(http::field::content_type, "application/json");
        if (!headerValue.empty()) {
            res.set(http::field::www_authenticate, headerValue);
        }
        res.keep_alive(false);
        res.body() = std::string("{}");
        res.prepare_payload();
        http::write(stream, res);
    }

    void runOnce() {
        using boost::asio::ip::tcp;
        namespace http = boost::beast::http;
        try {
            tcp::socket socket{io};
            acceptor.accept(socket);
            boost::beast::tcp_stream stream{std::move(socket)};
            boost::beast::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(stream, buffer, req);
            const std::string target = std::string(req.target());

            if (target == std::string("/rpc_401")) {
                const std::string wa =
                    "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource\", scope=\"files:read\"";
                writeResponseWithStatusAndHeader(stream, req, http::status::unauthorized, wa);
                running.store(false);
            } else if (target == std::string("/rpc_403")) {
                const std::string wa =
                    "Bearer error=\"insufficient_scope\", scope=\"files:write\", resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource\"";
                writeResponseWithStatusAndHeader(stream, req, http::status::forbidden, wa);
                running.store(false);
            } else {
                // Default: 200 OK
                http::response<http::string_body> res{ http::status::ok, req.version() };
                res.set(http::field::server, "mini-auth-server");
                res.set(http::field::content_type, "application/json");
                res.keep_alive(false);
                res.body() = std::string("{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"result\":{}} ");
                res.prepare_payload();
                http::write(stream, res);
                running.store(false);
            }
            boost::system::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        } catch (...) {
            (void)0;
        }
    }

    void start() {
        using boost::asio::ip::tcp;
        try {
            tcp::endpoint ep{tcp::v4(), 0};
            acceptor.open(ep.protocol());
            acceptor.set_option(tcp::acceptor::reuse_address(true));
            acceptor.bind(ep);
            acceptor.listen();
            port = acceptor.local_endpoint().port();
            running.store(true);
            thr = std::thread([this]() {
                while (running.load()) {
                    runOnce();
                }
            });
        } catch (...) {
            running.store(false);
        }
    }

    void stop() {
        try {
            running.store(false);
            boost::system::error_code ec;
            // Poke acceptor
            try {
                boost::asio::ip::tcp::socket poke{io};
                boost::asio::ip::tcp::endpoint ep{boost::asio::ip::make_address("127.0.0.1"), port};
                poke.connect(ep, ec);
                poke.close();
            } catch (...) { (void)0; }
            acceptor.close(ec);
            io.stop();
            if (thr.joinable()) {
                thr.detach();
            }
        } catch (...) { (void)0; }
    }
};

} // namespace

TEST(HTTPTransportWWWAuth, Captures401Header) {
    MiniAuthServer srv; srv.start();

    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc_401; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());

    (void)t->Start().get();

    // Fire a simple request
    auto req = std::make_unique<mcp::JSONRPCRequest>(std::string(""), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(5s), std::future_status::ready);
    (void)fut.get();

    mcp::HTTPTransport::HttpResponseInfo info;
    ASSERT_TRUE(static_cast<mcp::HTTPTransport*>(t.get())->QueryLastHttpResponse(info));
    EXPECT_EQ(info.status, 401);
    ASSERT_FALSE(info.wwwAuthenticate.empty());

    // Parse and validate fields
    mcp::auth::WwwAuthChallenge c; ASSERT_TRUE(mcp::auth::parseWwwAuthenticate(info.wwwAuthenticate, c));
    EXPECT_EQ(c.scheme, std::string("bearer"));
    EXPECT_NE(c.params.find("resource_metadata"), c.params.end());

    (void)t->Close().get();
    srv.stop();
}

TEST(HTTPTransportWWWAuth, Captures403Header) {
    MiniAuthServer srv; srv.start();

    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc_403; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());
    (void)t->Start().get();

    auto req = std::make_unique<mcp::JSONRPCRequest>(std::string(""), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(5s), std::future_status::ready);
    (void)fut.get();

    mcp::HTTPTransport::HttpResponseInfo info;
    ASSERT_TRUE(static_cast<mcp::HTTPTransport*>(t.get())->QueryLastHttpResponse(info));
    EXPECT_EQ(info.status, 403);
    ASSERT_FALSE(info.wwwAuthenticate.empty());

    mcp::auth::WwwAuthChallenge c; ASSERT_TRUE(mcp::auth::parseWwwAuthenticate(info.wwwAuthenticate, c));
    EXPECT_EQ(c.params["error"], std::string("insufficient_scope"));

    (void)t->Close().get();
    srv.stop();
}

TEST(HTTPTransportWWWAuth, NoLastResponseBeforeAnyRequest) {
    mcp::HTTPTransportFactory f;
    auto t = f.CreateTransport("scheme=http; host=127.0.0.1; port=9; rpcPath=/rpc; notifyPath=/notify; auth=none");
    (void)t->Start().get();

    mcp::HTTPTransport::HttpResponseInfo info;
    EXPECT_FALSE(static_cast<mcp::HTTPTransport*>(t.get())->QueryLastHttpResponse(info));

    (void)t->Close().get();
}
