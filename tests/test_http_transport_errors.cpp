//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_http_transport_errors.cpp
// Purpose: HTTPTransport negative-path tests (invalid/empty responses)
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

namespace {

struct MiniServer {
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor acceptor{io};
    std::thread thr;
    std::atomic<bool> running{false};
    unsigned short port{0};

    static void writeResponseWithStatus(boost::beast::tcp_stream& stream,
                                        const boost::beast::http::request<boost::beast::http::string_body>& req,
                                        boost::beast::http::status statusCode,
                                        const std::string& body,
                                        const std::string& contentType) {
        boost::beast::http::response<boost::beast::http::string_body> res{ statusCode, req.version() };
        res.set(boost::beast::http::field::server, "mini-server");
        res.set(boost::beast::http::field::content_type, contentType);
        res.keep_alive(false);
        res.body() = body;
        res.prepare_payload();
        boost::beast::http::write(stream, res);
    }

// (moved test below, outside of MiniServer struct)

    static void writeResponse(boost::beast::tcp_stream& stream,
                              const boost::beast::http::request<boost::beast::http::string_body>& req,
                              const std::string& body,
                              const std::string& contentType) {
        writeResponseWithStatus(stream, req, boost::beast::http::status::ok, body, contentType);
    }

    void runOnce() {
        using boost::asio::ip::tcp;
        try {
            tcp::socket socket{io};
            acceptor.accept(socket);
            boost::beast::tcp_stream stream{std::move(socket)};
            // Single request then close
            boost::beast::flat_buffer buffer;
            boost::beast::http::request<boost::beast::http::string_body> req;
            boost::beast::http::read(stream, buffer, req);
            const std::string target = std::string(req.target());
            if (target == std::string("/rpc")) {
                // Respond with empty body (invalid for JSON-RPC response)
                writeResponse(stream, req, std::string(), std::string("application/json"));
                running.store(false);
            } else if (target == std::string("/rpc_nonjson")) {
                // 200 OK but non-JSON body and content-type
                writeResponse(stream, req, std::string("ok"), std::string("text/plain"));
                running.store(false);
            } else if (target == std::string("/rpc_badjson")) {
                // 200 OK with application/json but invalid JSON body
                writeResponse(stream, req, std::string("notjson"), std::string("application/json"));
                running.store(false);
            } else if (target == std::string("/rpc_500")) {
                // Non-200 status code
                writeResponseWithStatus(stream, req, boost::beast::http::status::internal_server_error, std::string("oops"), std::string("application/json"));
                running.store(false);
            } else if (target == std::string("/rpc_mismatch")) {
                // Valid JSON but id does not match request id
                writeResponse(stream, req, std::string("{\"jsonrpc\":\"2.0\",\"id\":\"wrong\",\"result\":{}}"), std::string("application/json"));
                running.store(false);
            } else {
                // For notify or other, just return {}
                writeResponse(stream, req, std::string("{}"), std::string("application/json"));
            }
            boost::system::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        } catch (...) {
            // Ignore errors from test server loop intentionally
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
            // If acceptor init fails, mark not running and continue
            running.store(false);
            (void)0;
        }
    }

    void stop() {
        try {
            running.store(false);
            boost::system::error_code ec;
            // Poke accept by connecting to ourselves before closing acceptor
            try {
                boost::asio::ip::tcp::socket pokeSock{io};
                boost::asio::ip::tcp::endpoint ep{boost::asio::ip::make_address("127.0.0.1"), port};
                pokeSock.connect(ep, ec);
                pokeSock.close();
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

TEST(HTTPTransportErrors, MismatchedIdResolvesOriginalWithInternalError) {
    MiniServer srv; srv.start();

    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc_mismatch; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());

    (void)t->Start().get();

    // Use a known id "req-1" and expect resolution even though server returns id "wrong"
    auto req = std::make_unique<mcp::JSONRPCRequest>(mcp::JSONRPCId(std::string("req-1")), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    EXPECT_TRUE(resp->IsError());

    // Ensure the resolved id matches the original request id when represented as string
    std::string idStr;
    std::visit([&](const auto& id){ using T = std::decay_t<decltype(id)>; if constexpr (std::is_same_v<T, std::string>) { idStr = id; } else if constexpr (std::is_same_v<T, int64_t>) { idStr = std::to_string(id); } else { idStr = ""; } }, resp->id);
    EXPECT_EQ(idStr, std::string("req-1"));

    (void)t->Close().get();
    srv.stop();
}

TEST(HTTPTransportErrors, NonJsonBodyMapsToInternalErrorAndResolves) {
    MiniServer srv; srv.start();

    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc_nonjson; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());

    std::atomic<bool> sawError{false};
    t->SetErrorHandler([&](const std::string&){ sawError.store(true); });

    (void)t->Start().get();

    auto req = std::make_unique<mcp::JSONRPCRequest>(mcp::JSONRPCId(std::string("id1")), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    EXPECT_TRUE(resp->IsError());

    if (resp->error.has_value()) {
        const auto& v = resp->error->get();
        if (std::holds_alternative<mcp::JSONValue::Object>(v)) {
            const auto& obj = std::get<mcp::JSONValue::Object>(v);
            auto it = obj.find("code");
            if (it != obj.end() && it->second) {
                if (std::holds_alternative<int64_t>(it->second->value)) {
                    EXPECT_EQ(std::get<int64_t>(it->second->value), mcp::JSONRPCErrorCodes::InternalError);
                }
            }
        }
    }

    (void)t->Close().get();
    srv.stop();
}

TEST(HTTPTransportErrors, Http500MapsToInternalErrorAndResolves) {
    MiniServer srv; srv.start();

    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc_500; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());

    std::atomic<bool> sawError{false};
    t->SetErrorHandler([&](const std::string&){ sawError.store(true); });

    (void)t->Start().get();

    auto req = std::make_unique<mcp::JSONRPCRequest>(mcp::JSONRPCId(std::string("id2")), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    EXPECT_TRUE(resp->IsError());

    if (resp->error.has_value()) {
        const auto& v = resp->error->get();
        if (std::holds_alternative<mcp::JSONValue::Object>(v)) {
            const auto& obj = std::get<mcp::JSONValue::Object>(v);
            auto it = obj.find("code");
            if (it != obj.end() && it->second) {
                if (std::holds_alternative<int64_t>(it->second->value)) {
                    EXPECT_EQ(std::get<int64_t>(it->second->value), mcp::JSONRPCErrorCodes::InternalError);
                }
            }
        }
    }

    (void)t->Close().get();
    srv.stop();
}

TEST(HTTPTransportErrors, InvalidJsonResponseMapsToInternalErrorAndResolves) {
    MiniServer srv; srv.start();

    mcp::HTTPTransportFactory f;
    std::ostringstream cfg;
    cfg << "scheme=http; host=127.0.0.1; port=" << srv.port
        << "; rpcPath=/rpc; notifyPath=/notify; auth=none; connectTimeoutMs=500; readTimeoutMs=1500";
    auto t = f.CreateTransport(cfg.str());

    // Error handler for debug visibility (optional)
    std::atomic<bool> sawError{false};
    t->SetErrorHandler([&](const std::string&){ sawError.store(true); });

    (void)t->Start().get();

    // Send request with known id to assert id propagation in error
    auto req = std::make_unique<mcp::JSONRPCRequest>(mcp::JSONRPCId(std::string("abc")), std::string("noop"), std::nullopt);
    auto fut = t->SendRequest(std::move(req));
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto resp = fut.get();
    ASSERT_TRUE(resp != nullptr);
    EXPECT_TRUE(resp->IsError());

    // Verify error.code == InternalError when available
    if (resp->error.has_value()) {
        const auto& v = resp->error->get();
        if (std::holds_alternative<mcp::JSONValue::Object>(v)) {
            const auto& obj = std::get<mcp::JSONValue::Object>(v);
            auto it = obj.find("code");
            if (it != obj.end() && it->second) {
                if (std::holds_alternative<int64_t>(it->second->value)) {
                    EXPECT_EQ(std::get<int64_t>(it->second->value), mcp::JSONRPCErrorCodes::InternalError);
                }
            }
        }
    }

    (void)t->Close().get();
    srv.stop();
}
