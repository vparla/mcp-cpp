//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_http_auth.cpp
// Purpose: Tests for HTTPTransport authentication (Bearer and OAuth2 client-credentials)
//==========================================================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <future>
#include <iostream>
#include <utility>
#include <sstream>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "mcp/HTTPTransport.hpp"
#include "mcp/JSONRPCTypes.h"
#include "mcp/auth/BearerAuth.hpp"
#include "mcp/auth/OAuth2ClientCredentialsAuth.hpp"

namespace {

struct TestServer {
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor acceptor{io};
    std::thread thr;
    std::atomic<bool> running{false};

    // Captured headers from last /rpc request
    std::string lastAuthHeader;

    unsigned short port{0};

    std::string tokenResponseOverride;

    static void writeResponse(boost::beast::tcp_stream& stream,
                              const boost::beast::http::request<boost::beast::http::string_body>& req,
                              const std::string& body,
                              const std::string& contentType) {
        boost::beast::http::response<boost::beast::http::string_body> res{
            boost::beast::http::status::ok, req.version()
        };
        res.set(boost::beast::http::field::server, "test-server");
        res.set(boost::beast::http::field::content_type, contentType);
        res.keep_alive(false);
        res.body() = body;
        res.prepare_payload();
        boost::beast::http::write(stream, res);
    }

    void runOnce();
    void start();
    void stop();
};

TEST(HTTPAuth, SetAuthReference_BearerInjected) {
    TestServer srv; srv.start();
    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());
    auto* http = dynamic_cast<mcp::HTTPTransport*>(t.get());
    ASSERT_NE(http, nullptr);
    mcp::auth::BearerAuth auth("XYZ");
    http->SetAuth(auth);
    (void)t->Start().get();
    auto req = std::make_unique<mcp::JSONRPCRequest>(std::string(""), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    auto status = fut.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready);
    (void)fut.get();
    (void)t->Close().get();
    EXPECT_EQ(srv.lastAuthHeader, std::string("Bearer XYZ"));
    srv.stop();
}

TEST(HTTPAuth, SetAuthSharedPtr_BearerInjected) {
    TestServer srv; srv.start();
    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());
    auto* http = dynamic_cast<mcp::HTTPTransport*>(t.get());
    ASSERT_NE(http, nullptr);
    auto auth = std::make_shared<mcp::auth::BearerAuth>("XYZ");
    http->SetAuth(auth);
    (void)t->Start().get();
    auto req = std::make_unique<mcp::JSONRPCRequest>(std::string(""), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    auto status = fut.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready);
    (void)fut.get();
    (void)t->Close().get();
    EXPECT_EQ(srv.lastAuthHeader, std::string("Bearer XYZ"));
    srv.stop();
}

TEST(HTTPAuth, SetAuthReference_OAuthClientCredentials) {
    TestServer srv; srv.start();
    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());
    auto* http = dynamic_cast<mcp::HTTPTransport*>(t.get());
    ASSERT_NE(http, nullptr);
    mcp::auth::OAuth2ClientCredentialsAuth auth(
        std::string("http://127.0.0.1:") + std::to_string(srv.port) + std::string("/token"),
        std::string("cid"),
        std::string("cs"),
        std::string("s1"),
        60u,
        500u,
        1500u,
        std::string(),
        std::string());
    http->SetAuth(auth);
    (void)t->Start().get();
    auto req = std::make_unique<mcp::JSONRPCRequest>(std::string(""), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    auto status = fut.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready);
    (void)fut.get();
    (void)t->Close().get();
    EXPECT_EQ(srv.lastAuthHeader, std::string("Bearer ABC123"));
    srv.stop();
}

TEST(HTTPAuth, SetAuthSharedPtr_OAuthClientCredentials) {
    TestServer srv; srv.start();
    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());
    auto* http = dynamic_cast<mcp::HTTPTransport*>(t.get());
    ASSERT_NE(http, nullptr);
    auto auth = std::make_shared<mcp::auth::OAuth2ClientCredentialsAuth>(
        std::string("http://127.0.0.1:") + std::to_string(srv.port) + std::string("/token"),
        std::string("cid"),
        std::string("cs"),
        std::string("s1"),
        60u,
        500u,
        1500u,
        std::string(),
        std::string());
    http->SetAuth(auth);
    (void)t->Start().get();
    auto req = std::make_unique<mcp::JSONRPCRequest>(std::string(""), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    auto status = fut.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready);
    (void)fut.get();
    (void)t->Close().get();
    EXPECT_EQ(srv.lastAuthHeader, std::string("Bearer ABC123"));
    srv.stop();
}

TEST(HTTPAuth, OAuthClientCredentials_InvalidTokenResponse_NoHeader) {
    TestServer srv; srv.tokenResponseOverride = std::string("{\"expires_in\":60}"); srv.start();
    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());
    auto* http = dynamic_cast<mcp::HTTPTransport*>(t.get());
    ASSERT_NE(http, nullptr);
    auto auth = std::make_shared<mcp::auth::OAuth2ClientCredentialsAuth>(
        std::string("http://127.0.0.1:") + std::to_string(srv.port) + std::string("/token"),
        std::string("cid"),
        std::string("cs"),
        std::string("s1"),
        60u,
        500u,
        1500u,
        std::string(),
        std::string());
    http->SetAuth(auth);
    (void)t->Start().get();
    auto req = std::make_unique<mcp::JSONRPCRequest>(std::string(""), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    auto status = fut.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready);
    (void)fut.get();
    (void)t->Close().get();
    EXPECT_EQ(srv.lastAuthHeader, std::string());
    srv.stop();
}

TEST(HTTPAuth, OAuthClientCredentials_UnreachableTokenEndpoint_NoHeader) {
    TestServer srv; srv.start();
    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc; notifyPath=/notify; auth=none; connectTimeoutMs=200; readTimeoutMs=800";
    auto t = f.CreateTransport(cfg.str());
    auto* http = dynamic_cast<mcp::HTTPTransport*>(t.get());
    ASSERT_NE(http, nullptr);
    unsigned short badPort = static_cast<unsigned short>(srv.port + 1);
    auto auth = std::make_shared<mcp::auth::OAuth2ClientCredentialsAuth>(
        std::string("http://127.0.0.1:") + std::to_string(badPort) + std::string("/token"),
        std::string("cid"),
        std::string("cs"),
        std::string("s1"),
        60u,
        200u,
        800u,
        std::string(),
        std::string());
    http->SetAuth(auth);
    (void)t->Start().get();
    auto req = std::make_unique<mcp::JSONRPCRequest>(std::string(""), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    auto status = fut.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready);
    (void)fut.get();
    (void)t->Close().get();
    EXPECT_EQ(srv.lastAuthHeader, std::string());
    srv.stop();
}

void TestServer::runOnce() {
    using boost::asio::ip::tcp;
    try {
        tcp::socket socket{io};
        acceptor.accept(socket);
        std::cerr << "[server] accepted connection" << std::endl;
        boost::beast::tcp_stream stream{std::move(socket)};
        // Short read deadline to avoid deadlock if a premature /rpc connection arrives before /token
        stream.expires_after(std::chrono::milliseconds(300));

        // If stop() was called, avoid blocking on read
        if (!running.load()) {
            std::cerr << "[server] stopping, skipping read" << std::endl;
            boost::system::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            return;
        }

        boost::beast::flat_buffer buffer;
        boost::beast::http::request<boost::beast::http::string_body> req;
        boost::beast::http::read(stream, buffer, req);
        std::cerr << "[server] request target: " << std::string(req.target()) << std::endl;

        const std::string target = std::string(req.target());
        if (target == std::string("/token")) {
            std::string tokenJson = tokenResponseOverride.empty()
                ? std::string("{\"access_token\":\"ABC123\",\"expires_in\":60,\"token_type\":\"Bearer\"}")
                : tokenResponseOverride;
            writeResponse(stream, req, tokenJson, std::string("application/json"));
            std::cerr << "[server] responded to /token" << std::endl;
        } else {
            // Capture Authorization header for /rpc or other
            auto it = req.base().find(boost::beast::http::field::authorization);
            if (it != req.base().end()) {
                lastAuthHeader = std::string(it->value());
            } else {
                lastAuthHeader.clear();
            }
            // Return a minimal JSON-RPC response (id may not match; ok for this test)
            const std::string okJson = std::string("{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"result\":{}}");
            writeResponse(stream, req, okJson, std::string("application/json"));
            std::cerr << "[server] responded to /rpc with auth='" << lastAuthHeader << "'" << std::endl;
            // After serving /rpc, no more requests are expected in this test.
            // For OAuth test, the first request is /token, second is /rpc. This stops after /rpc.
            running.store(false);
        }

        boost::system::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    } catch (...) {
        // swallow in test server
    }
}

void TestServer::start() {
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

void TestServer::stop() {
    try {
        std::cerr << "[server] stop begin" << std::endl;
        running.store(false);
        boost::system::error_code ec;
        // Poke accept by connecting to ourselves before closing acceptor
        try {
            boost::asio::ip::tcp::socket pokeSock{io};
            boost::asio::ip::tcp::endpoint ep{boost::asio::ip::make_address("127.0.0.1"), port};
            pokeSock.connect(ep, ec);
            std::cerr << "[server] poke connect ec=" << ec.message() << std::endl;
            pokeSock.close();
        } catch (...) {}
        acceptor.close(ec);
        std::cerr << "[server] acceptor.close done (ec=" << ec.message() << ")" << std::endl;
        io.stop();
        std::cerr << "[server] io.stop done" << std::endl;
        if (thr.joinable()) {
            std::cerr << "[server] detaching thread" << std::endl;
            thr.detach();
        }
        std::cerr << "[server] stop end" << std::endl;
    } catch (...) {
    }
}

} // namespace

TEST(HTTPAuth, BearerHeaderInjected) {
    // Start test HTTP server
    TestServer srv;
    srv.start();
    std::cerr << "[test] server started on port " << srv.port << std::endl;

    // Build transport with bearer token
    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc; notifyPath=/notify; auth=bearer; bearerToken=XYZ; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());

    // Capture transport errors to aid debugging and avoid silent hangs
    std::atomic<bool> sawError{false};
    t->SetErrorHandler([&](const std::string& m) {
        sawError.store(true);
        std::cerr << "[HTTPTransport error] " << m << std::endl;
    });

    // Start and send one request
    std::cerr << "[test] starting transport" << std::endl;
    (void)t->Start().get();
    std::cerr << "[test] transport started" << std::endl;
    auto req = std::make_unique<mcp::JSONRPCRequest>(std::string(""), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    std::cerr << "[test] request posted" << std::endl;
    std::cerr << "[test] waiting for future..." << std::endl;
    auto status = fut.wait_for(std::chrono::seconds(5));
    std::cerr << "[test] wait_for status=" << (status == std::future_status::ready ? "ready" : "timeout") << std::endl;
    ASSERT_EQ(status, std::future_status::ready) << "request timed out (no response)";
    (void)fut.get();
    std::cerr << "[test] future get done" << std::endl;
    std::cerr << "[test] closing transport" << std::endl;
    (void)t->Close().get();
    std::cerr << "[test] close done" << std::endl;

    // Authorization header should be set
    std::cerr << "[test] checking header" << std::endl;
    EXPECT_EQ(srv.lastAuthHeader, std::string("Bearer XYZ"));
    std::cerr << "[test] header ok" << std::endl;

    srv.stop();
    std::cerr << "[test] server stopped" << std::endl;
    std::cerr << "[test] end" << std::endl;
}

TEST(HTTPAuth, OAuthClientCredentialsFetchAndUseToken) {
    // Start test HTTP server
    TestServer srv;
    srv.start();
    std::cerr << "[test] server started on port " << srv.port << std::endl;

    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc; notifyPath=/notify; auth=oauth2; oauthUrl=http://127.0.0.1:" << srv.port
        << "/token; clientId=cid; clientSecret=cs; scope=s1; connectTimeoutMs=500; readTimeoutMs=1500";

    auto t = f.CreateTransport(cfg.str());

    // Capture transport errors to aid debugging and avoid silent hangs
    std::atomic<bool> sawError{false};
    t->SetErrorHandler([&](const std::string& m) {
        sawError.store(true);
        std::cerr << "[HTTPTransport error] " << m << std::endl;
    });

    (void)t->Start().get();
    auto req = std::make_unique<mcp::JSONRPCRequest>(std::string(""), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    auto status = fut.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready) << "request timed out (no response)";
    (void)fut.get();
    (void)t->Close().get();

    EXPECT_EQ(srv.lastAuthHeader, std::string("Bearer ABC123"));

    srv.stop();
    std::cerr << "[test] server stopped (oauth)" << std::endl;
    std::cerr << "[test] end (oauth)" << std::endl;
}
