//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_http_server_bearer.cpp
// Purpose: GoogleTests for HTTPServer Bearer authentication middleware (P0)
//==========================================================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "mcp/HTTPServer.hpp"
#include "mcp/JSONRPCTypes.h"
#include "mcp/auth/ServerAuth.hpp"

using namespace std::chrono;
namespace http = boost::beast::http;

namespace {

//==========================================================================================================
// MockTokenVerifier
// Purpose: Simple in-memory token verifier for tests
//==========================================================================================================
class MockTokenVerifier : public mcp::auth::ITokenVerifier {
public:
    bool Verify(const std::string& token, mcp::auth::TokenInfo& outInfo, std::string& errorMessage) override {
        if (token == std::string("good")) {
            outInfo.scopes = { std::string("s1"), std::string("s2") };
            outInfo.expiration = std::chrono::system_clock::now() + std::chrono::minutes(5);
            return true;
        }
        if (token == std::string("expired")) {
            outInfo.scopes = { std::string("s1") };
            outInfo.expiration = std::chrono::system_clock::now() - std::chrono::minutes(1);
            return true;
        }
        if (token == std::string("noscope")) {
            outInfo.scopes = { std::string("other") };
            outInfo.expiration = std::chrono::system_clock::now() + std::chrono::minutes(5);
            return true;
        }
        errorMessage = std::string("invalid token");
        return false;
    }
};

//==========================================================================================================
// httpPost
// Purpose: Send a single HTTP POST and capture the response (synchronously).
//==========================================================================================================
static http::response<http::string_body> httpPost(const std::string& host,
                                                  unsigned short port,
                                                  const std::string& target,
                                                  const std::string& body,
                                                  const std::optional<std::string>& bearer) {
    using boost::asio::ip::tcp;
    boost::asio::io_context ioc;
    tcp::resolver resolver{ioc};
    auto r = resolver.resolve(host, std::to_string(port));
    tcp::socket socket{ioc};
    boost::asio::connect(socket, r);

    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::content_type, "application/json");
    if (bearer.has_value()) {
        req.set(http::field::authorization, std::string("Bearer ") + bearer.value());
    }
    req.body() = body;
    req.prepare_payload();

    http::write(socket, req);

    boost::beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(socket, buffer, res);

    boost::system::error_code ec;
    socket.shutdown(tcp::socket::shutdown_both, ec);
    return res;
}

//==========================================================================================================
// findFreePort
// Purpose: Obtain an available local TCP port by binding to port 0 temporarily.
//==========================================================================================================
static unsigned short findFreePort() {
    using boost::asio::ip::tcp;
    boost::asio::io_context ioc;
    tcp::acceptor acc(ioc);
    tcp::endpoint ep(tcp::v4(), 0);
    acc.open(ep.protocol());
    acc.set_option(tcp::acceptor::reuse_address(true));
    acc.bind(ep);
    auto port = acc.local_endpoint().port();
    acc.close();
    return port;
}

//==========================================================================================================
// makeJsonRpc
// Purpose: Build a minimal JSON-RPC request body for tests.
//==========================================================================================================
static std::string makeJsonRpc() {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":\"1\",\"method\":\"ping\"}");
}

} // namespace

//==========================================================================================================
// Valid token: 200 OK, JSON-RPC handler executed; TokenInfo is present during handler.
//==========================================================================================================
TEST(HTTPServerBearer, ValidToken_AllowsRequest_AndSetsTokenInfo) {
    unsigned short port = findFreePort();

    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);

    // Verify token info flows into handler scope
    std::atomic<bool> sawToken{false};
    server.SetRequestHandler([&](const mcp::JSONRPCRequest& req){
        (void)req;
        auto* ti = mcp::auth::CurrentTokenInfo();
        if (ti != nullptr) { sawToken.store(true); }
        auto resp = std::make_unique<mcp::JSONRPCResponse>(); resp->id = req.id; return resp;
    });

    MockTokenVerifier verifier;
    mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("s1") };
    server.SetBearerAuth(verifier, aopts);

    ASSERT_NO_THROW({ server.Start().get(); });
    auto res = httpPost(std::string("127.0.0.1"), port, std::string("/mcp/rpc"), makeJsonRpc(), std::optional<std::string>(std::string("good")));
    EXPECT_EQ(static_cast<int>(res.result()), 200);
    EXPECT_TRUE(sawToken.load());
    ASSERT_NO_THROW({ server.Stop().get(); });
}

//==========================================================================================================
// Missing header: 401 with WWW-Authenticate
//==========================================================================================================
TEST(HTTPServerBearer, MissingHeader_401_WithWWWAuthenticate) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);

    server.SetRequestHandler([&](const mcp::JSONRPCRequest& req){ auto resp = std::make_unique<mcp::JSONRPCResponse>(); resp->id = req.id; return resp; });

    MockTokenVerifier verifier;
    mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("s1") };
    server.SetBearerAuth(verifier, aopts);

    ASSERT_NO_THROW({ server.Start().get(); });
    auto res = httpPost(std::string("127.0.0.1"), port, std::string("/mcp/rpc"), makeJsonRpc(), std::nullopt);
    EXPECT_EQ(static_cast<int>(res.result()), 401);
    auto www = res.base().find(http::field::www_authenticate);
    ASSERT_NE(www, res.base().end());
    EXPECT_NE(std::string(www->value()).find("resource_metadata=https://auth.example.com/rs"), std::string::npos);
    ASSERT_NO_THROW({ server.Stop().get(); });
}

//==========================================================================================================
// Wrong scheme: 401
//==========================================================================================================
TEST(HTTPServerBearer, WrongScheme_401_WithWWWAuthenticate) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);

    server.SetRequestHandler([&](const mcp::JSONRPCRequest& req){ auto resp = std::make_unique<mcp::JSONRPCResponse>(); resp->id = req.id; return resp; });

    MockTokenVerifier verifier;
    mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("s1") };
    server.SetBearerAuth(verifier, aopts);

    ASSERT_NO_THROW({ server.Start().get(); });
    // Use Basic scheme which should be rejected as 'no bearer token'
    using boost::asio::ip::tcp; (void)tcp::v4();
    using std::string; using std::optional;
    auto res = [&](){
        using boost::asio::ip::tcp;
        boost::asio::io_context ioc;
        tcp::resolver resolver{ioc};
        auto r = resolver.resolve(string("127.0.0.1"), std::to_string(port));
        tcp::socket socket{ioc};
        boost::asio::connect(socket, r);
        http::request<http::string_body> req{http::verb::post, string("/mcp/rpc"), 11};
        req.set(http::field::host, string("127.0.0.1"));
        req.set(http::field::content_type, string("application/json"));
        req.set(http::field::authorization, string("Basic Zm9vOmJhcg=="));
        req.body() = makeJsonRpc(); req.prepare_payload();
        http::write(socket, req);
        boost::beast::flat_buffer buffer; http::response<http::string_body> res;
        http::read(socket, buffer, res);
        boost::system::error_code ec; socket.shutdown(tcp::socket::shutdown_both, ec);
        return res;
    }();
    EXPECT_EQ(static_cast<int>(res.result()), 401);
    auto www = res.base().find(http::field::www_authenticate);
    ASSERT_NE(www, res.base().end());
    EXPECT_NE(std::string(www->value()).find("resource_metadata=https://auth.example.com/rs"), std::string::npos);
    ASSERT_NO_THROW({ server.Stop().get(); });
}

//==========================================================================================================
// Invalid token: 401
//==========================================================================================================
TEST(HTTPServerBearer, InvalidToken_401_WithWWWAuthenticate) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);
    server.SetRequestHandler([&](const mcp::JSONRPCRequest& req){ auto resp = std::make_unique<mcp::JSONRPCResponse>(); resp->id = req.id; return resp; });
    MockTokenVerifier verifier; mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("s1") }; server.SetBearerAuth(verifier, aopts);
    ASSERT_NO_THROW({ server.Start().get(); });
    auto res = httpPost(std::string("127.0.0.1"), port, std::string("/mcp/rpc"), makeJsonRpc(), std::optional<std::string>(std::string("bad")));
    EXPECT_EQ(static_cast<int>(res.result()), 401);
    auto www = res.base().find(http::field::www_authenticate);
    ASSERT_NE(www, res.base().end());
    ASSERT_NO_THROW({ server.Stop().get(); });
}

//==========================================================================================================
// Insufficient scope: 403
//==========================================================================================================
TEST(HTTPServerBearer, InsufficientScope_403_WithWWWAuthenticate) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);
    server.SetRequestHandler([&](const mcp::JSONRPCRequest& req){ auto resp = std::make_unique<mcp::JSONRPCResponse>(); resp->id = req.id; return resp; });
    MockTokenVerifier verifier; mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("need") }; server.SetBearerAuth(verifier, aopts);
    ASSERT_NO_THROW({ server.Start().get(); });
    auto res = httpPost(std::string("127.0.0.1"), port, std::string("/mcp/rpc"), makeJsonRpc(), std::optional<std::string>(std::string("noscope")));
    EXPECT_EQ(static_cast<int>(res.result()), 403);
    auto www = res.base().find(http::field::www_authenticate);
    ASSERT_NE(www, res.base().end());
    ASSERT_NO_THROW({ server.Stop().get(); });
}

//==========================================================================================================
// Expired: 401
//==========================================================================================================
TEST(HTTPServerBearer, ExpiredToken_401_WithWWWAuthenticate) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);
    server.SetRequestHandler([&](const mcp::JSONRPCRequest& req){ auto resp = std::make_unique<mcp::JSONRPCResponse>(); resp->id = req.id; return resp; });
    MockTokenVerifier verifier; mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("s1") }; server.SetBearerAuth(verifier, aopts);
    ASSERT_NO_THROW({ server.Start().get(); });
    auto res = httpPost(std::string("127.0.0.1"), port, std::string("/mcp/rpc"), makeJsonRpc(), std::optional<std::string>(std::string("expired")));
    EXPECT_EQ(static_cast<int>(res.result()), 401);
    auto www = res.base().find(http::field::www_authenticate);
    ASSERT_NE(www, res.base().end());
    ASSERT_NO_THROW({ server.Stop().get(); });
}

//==========================================================================================================
// Notify path enforcement: Missing header => 401
//==========================================================================================================
TEST(HTTPServerBearer, Notify_MissingHeader_401) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);

    server.SetNotificationHandler([&](std::unique_ptr<mcp::JSONRPCNotification> note){ (void)note; });

    MockTokenVerifier verifier;
    mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("s1") };
    server.SetBearerAuth(verifier, aopts);

    ASSERT_NO_THROW({ server.Start().get(); });
    auto res = httpPost(std::string("127.0.0.1"), port, std::string("/mcp/notify"), std::string("{\"jsonrpc\":\"2.0\",\"method\":\"n\"}"), std::nullopt);
    EXPECT_EQ(static_cast<int>(res.result()), 401);
    ASSERT_NO_THROW({ server.Stop().get(); });
}

//==========================================================================================================
// No auth configured => request succeeds without Authorization header
//==========================================================================================================
TEST(HTTPServerBearer, NoAuthConfigured_AllowsRequests) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);

    server.SetRequestHandler([&](const mcp::JSONRPCRequest& req){ auto resp = std::make_unique<mcp::JSONRPCResponse>(); resp->id = req.id; return resp; });

    ASSERT_NO_THROW({ server.Start().get(); });
    auto res = httpPost(std::string("127.0.0.1"), port, std::string("/mcp/rpc"), makeJsonRpc(), std::nullopt);
    EXPECT_EQ(static_cast<int>(res.result()), 200);
    ASSERT_NO_THROW({ server.Stop().get(); });
}

//==========================================================================================================
// Empty token: 401 with WWW-Authenticate
//==========================================================================================================
TEST(HTTPServerBearer, EmptyToken_401_WithWWWAuthenticate) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);

    server.SetRequestHandler([&](const mcp::JSONRPCRequest& req){ auto resp = std::make_unique<mcp::JSONRPCResponse>(); resp->id = req.id; return resp; });

    MockTokenVerifier verifier; mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("s1") }; server.SetBearerAuth(verifier, aopts);
    ASSERT_NO_THROW({ server.Start().get(); });
    // Send Authorization: Bearer <empty>
    using boost::asio::ip::tcp;
    boost::asio::io_context ioc; tcp::resolver resolver{ioc}; auto r = resolver.resolve(std::string("127.0.0.1"), std::to_string(port)); tcp::socket socket{ioc}; boost::asio::connect(socket, r);
    http::request<http::string_body> req{http::verb::post, std::string("/mcp/rpc"), 11};
    req.set(http::field::host, std::string("127.0.0.1")); req.set(http::field::content_type, std::string("application/json")); req.set(http::field::authorization, std::string("Bearer "));
    req.body() = makeJsonRpc(); req.prepare_payload(); http::write(socket, req);
    boost::beast::flat_buffer buffer; http::response<http::string_body> res; http::read(socket, buffer, res); boost::system::error_code ec; socket.shutdown(tcp::socket::shutdown_both, ec);
    EXPECT_EQ(static_cast<int>(res.result()), 401);
    auto www = res.base().find(http::field::www_authenticate);
    ASSERT_NE(www, res.base().end());
    ASSERT_NO_THROW({ server.Stop().get(); });
}

//==========================================================================================================
// Lowercase scheme 'bearer' is accepted: 200
//==========================================================================================================
TEST(HTTPServerBearer, LowercaseScheme_AllowsRequest) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);
    server.SetRequestHandler([&](const mcp::JSONRPCRequest& req){ auto resp = std::make_unique<mcp::JSONRPCResponse>(); resp->id = req.id; return resp; });
    MockTokenVerifier verifier; mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("s1") }; server.SetBearerAuth(verifier, aopts);
    ASSERT_NO_THROW({ server.Start().get(); });
    using boost::asio::ip::tcp; boost::asio::io_context ioc; tcp::resolver resolver{ioc}; auto r = resolver.resolve(std::string("127.0.0.1"), std::to_string(port)); tcp::socket socket{ioc}; boost::asio::connect(socket, r);
    http::request<http::string_body> req{http::verb::post, std::string("/mcp/rpc"), 11}; req.set(http::field::host, std::string("127.0.0.1")); req.set(http::field::content_type, std::string("application/json")); req.set(http::field::authorization, std::string("bearer good")); req.body() = makeJsonRpc(); req.prepare_payload(); http::write(socket, req);
    boost::beast::flat_buffer buffer; http::response<http::string_body> res; http::read(socket, buffer, res); boost::system::error_code ec; socket.shutdown(tcp::socket::shutdown_both, ec);
    EXPECT_EQ(static_cast<int>(res.result()), 200);
    ASSERT_NO_THROW({ server.Stop().get(); });
}

//==========================================================================================================
// Notify path: invalid token => 401 with header; valid token => 200
//==========================================================================================================
TEST(HTTPServerBearer, Notify_TokenEnforcement_InvalidThenValid) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);
    server.SetNotificationHandler([&](std::unique_ptr<mcp::JSONRPCNotification> note){ (void)note; });
    MockTokenVerifier verifier; mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("s1") }; server.SetBearerAuth(verifier, aopts);
    ASSERT_NO_THROW({ server.Start().get(); });
    // Invalid
    auto r1 = httpPost(std::string("127.0.0.1"), port, std::string("/mcp/notify"), std::string("{\"jsonrpc\":\"2.0\",\"method\":\"n\"}"), std::optional<std::string>(std::string("bad")));
    EXPECT_EQ(static_cast<int>(r1.result()), 401);
    auto www = r1.base().find(http::field::www_authenticate);
    ASSERT_NE(www, r1.base().end());
    // Valid
    auto r2 = httpPost(std::string("127.0.0.1"), port, std::string("/mcp/notify"), std::string("{\"jsonrpc\":\"2.0\",\"method\":\"n\"}"), std::optional<std::string>(std::string("good")));
    EXPECT_EQ(static_cast<int>(r2.result()), 200);
    ASSERT_NO_THROW({ server.Stop().get(); });
}
