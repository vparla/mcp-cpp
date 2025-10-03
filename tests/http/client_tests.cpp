//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/http/client_tests.cpp
// Purpose: GoogleTest client exercising HTTP(S) transport against server container (TLS 1.3 only)
//==========================================================================================================

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <future>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>

#include "mcp/JSONRPCTypes.h"
#include "mcp/HTTPTransport.hpp"

using namespace std::chrono_literals;

static std::string envOr(const char* key, const char* defVal) {
    const char* v = std::getenv(key);
    return v ? std::string(v) : std::string(defVal);
}

static bool serverListening(const std::string& host, const std::string& port, int timeoutMs = 300) {
    struct addrinfo hints{}; std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo* res = nullptr;
    int rc = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (rc != 0 || !res) return false;
    bool ok = false;
    for (struct addrinfo* p = res; p && !ok; p = p->ai_next) {
        int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int c = ::connect(fd, p->ai_addr, p->ai_addrlen);
        if (c == 0) { ok = true; ::close(fd); break; }
        if (errno == EINPROGRESS) {
            fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
            struct timeval tv; tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000;
            int sel = ::select(fd + 1, nullptr, &wfds, nullptr, &tv);
            if (sel > 0 && FD_ISSET(fd, &wfds)) {
                int soErr = 0; socklen_t sl = sizeof(soErr);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &sl) == 0 && soErr == 0) {
                    ok = true; ::close(fd); break;
                }
            }
        }
        ::close(fd);
    }
    if (res) ::freeaddrinfo(res);
    return ok;
}

TEST(Http, BasicRoundtrip) {
    auto host = envOr("MCP_HTTP_HOST", "http-server");
    auto port = envOr("MCP_HTTP_PORT", "9443");
    auto rpc = envOr("MCP_HTTP_RPC", "/mcp/rpc");
    auto caFile = envOr("MCP_HTTP_CA", "/certs/cert.pem");
    auto sni = envOr("MCP_HTTP_SNI", "http-server");

    if (!serverListening(host, port)) {
        GTEST_SKIP() << "HTTP server unavailable (" << host << ":" << port << ") – skipping e2e roundtrip";
    }

    mcp::HTTPTransport::Options opts;
    opts.scheme = "https";
    opts.host = host;
    opts.port = port;
    opts.rpcPath = rpc;
    opts.serverName = sni;
    opts.caFile = caFile;

    mcp::HTTPTransport transport(opts);
    ASSERT_NO_THROW({ transport.Start().wait(); });
    ASSERT_TRUE(transport.IsConnected());

    auto req = std::make_unique<mcp::JSONRPCRequest>();
    req->method = "echo";
    req->id = std::string("1");

    auto fut = transport.SendRequest(std::move(req));
    auto resp = fut.get();
    ASSERT_NE(resp, nullptr);
    ASSERT_TRUE(resp->result.has_value());
    const auto& val = resp->result.value();
    ASSERT_TRUE(std::holds_alternative<mcp::JSONValue::Object>(val.value));

    (void)transport.Close().wait();
}

TEST(Http, MethodNotFound) {
    auto host = envOr("MCP_HTTP_HOST", "http-server");
    auto port = envOr("MCP_HTTP_PORT", "9443");
    if (!serverListening(host, port)) {
        GTEST_SKIP() << "HTTP server unavailable (" << host << ":" << port << ") – skipping method-not-found e2e";
    }
    mcp::HTTPTransport::Options opts; opts.scheme = "https"; opts.host = host; opts.port = port; opts.serverName = envOr("MCP_HTTP_SNI", "http-server"); opts.caFile = envOr("MCP_HTTP_CA", "/certs/cert.pem");
    mcp::HTTPTransport transport(opts); transport.Start().wait(); ASSERT_TRUE(transport.IsConnected());

    auto req = std::make_unique<mcp::JSONRPCRequest>();
    req->method = "noSuchMethod";
    req->id = std::string("2");
    auto fut = transport.SendRequest(std::move(req));
    auto resp = fut.get();
    ASSERT_NE(resp, nullptr);
    ASSERT_TRUE(resp->IsError());
    ASSERT_TRUE(resp->error.has_value());
    const auto& errVal = resp->error.value();
    ASSERT_TRUE(std::holds_alternative<mcp::JSONValue::Object>(errVal.value));
    const auto& obj = std::get<mcp::JSONValue::Object>(errVal.value);
    auto itCode = obj.find("code"); ASSERT_NE(itCode, obj.end());
    ASSERT_TRUE(itCode->second);
    ASSERT_TRUE(std::holds_alternative<int64_t>(itCode->second->value));
    EXPECT_EQ(std::get<int64_t>(itCode->second->value), static_cast<int64_t>(mcp::JSONRPCErrorCodes::MethodNotFound));

    (void)transport.Close().wait();
}

TEST(Http, BadSni) {
    mcp::HTTPTransport::Options opts; opts.scheme = "https"; opts.host = envOr("MCP_HTTP_HOST", "http-server"); opts.port = envOr("MCP_HTTP_PORT", "9443"); opts.serverName = std::string("wrong-sni"); opts.caFile = envOr("MCP_HTTP_CA", "/certs/cert.pem");
    mcp::HTTPTransport transport(opts); transport.Start().wait();

    auto req = std::make_unique<mcp::JSONRPCRequest>(); req->method = "echo"; req->id = std::string("3");
    auto resp = transport.SendRequest(std::move(req)).get();
    ASSERT_NE(resp, nullptr);
    // Expect error due to TLS hostname verification failure
    EXPECT_TRUE(resp->IsError());
    (void)transport.Close().wait();
}

TEST(Http, BadCa) {
    mcp::HTTPTransport::Options opts; opts.scheme = "https"; opts.host = envOr("MCP_HTTP_HOST", "http-server"); opts.port = envOr("MCP_HTTP_PORT", "9443"); opts.serverName = envOr("MCP_HTTP_SNI", "http-server"); opts.caFile = std::string("/certs/does-not-exist.pem");
    mcp::HTTPTransport transport(opts); transport.Start().wait();
    auto req = std::make_unique<mcp::JSONRPCRequest>(); req->method = "echo"; req->id = std::string("4");
    auto resp = transport.SendRequest(std::move(req)).get();
    ASSERT_NE(resp, nullptr);
    EXPECT_TRUE(resp->IsError());
    (void)transport.Close().wait();
}

TEST(Http, UnknownPath404) {
    mcp::HTTPTransport::Options opts; opts.scheme = "https"; opts.host = envOr("MCP_HTTP_HOST", "http-server"); opts.port = envOr("MCP_HTTP_PORT", "9443"); opts.serverName = envOr("MCP_HTTP_SNI", "http-server"); opts.caFile = envOr("MCP_HTTP_CA", "/certs/cert.pem");
    opts.rpcPath = "/unknown"; // cause 404
    mcp::HTTPTransport transport(opts); transport.Start().wait(); ASSERT_TRUE(transport.IsConnected());

    auto req = std::make_unique<mcp::JSONRPCRequest>(); req->method = "echo"; req->id = std::string("5");
    auto resp = transport.SendRequest(std::move(req)).get();
    ASSERT_NE(resp, nullptr);
    EXPECT_TRUE(resp->IsError());
    (void)transport.Close().wait();
}
