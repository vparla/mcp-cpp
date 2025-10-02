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

#include "mcp/JSONRPCTypes.h"
#include "mcp/HTTPTransport.hpp"

using namespace std::chrono_literals;

static std::string envOr(const char* key, const char* defVal) {
    const char* v = std::getenv(key);
    return v ? std::string(v) : std::string(defVal);
}

TEST(Http, BasicRoundtrip) {
    auto host = envOr("MCP_HTTP_HOST", "http-server");
    auto port = envOr("MCP_HTTP_PORT", "9443");
    auto rpc = envOr("MCP_HTTP_RPC", "/mcp/rpc");
    auto caFile = envOr("MCP_HTTP_CA", "/certs/cert.pem");
    auto sni = envOr("MCP_HTTP_SNI", "http-server");

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
    mcp::HTTPTransport::Options opts; opts.scheme = "https"; opts.host = envOr("MCP_HTTP_HOST", "http-server"); opts.port = envOr("MCP_HTTP_PORT", "9443"); opts.serverName = envOr("MCP_HTTP_SNI", "http-server"); opts.caFile = envOr("MCP_HTTP_CA", "/certs/cert.pem");
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
