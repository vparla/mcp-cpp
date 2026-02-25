#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "mcp/HTTPServer.hpp"
#include "mcp/JSONRPCTypes.h"
#include "mcp/auth/ServerAuth.hpp"
#include "mcp/auth/WwwAuthenticate.hpp"

using namespace std::chrono;
namespace http = boost::beast::http;

namespace {

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

static http::response<http::string_body> httpGet(const std::string& host,
                                                 unsigned short port,
                                                 const std::string& target) {
    using boost::asio::ip::tcp;
    boost::asio::io_context ioc;
    tcp::resolver resolver{ioc};
    auto r = resolver.resolve(host, std::to_string(port));
    tcp::socket socket{ioc};
    boost::asio::connect(socket, r);
    http::request<http::string_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    http::write(socket, req);
    boost::beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(socket, buffer, res);
    boost::system::error_code ec;
    socket.shutdown(tcp::socket::shutdown_both, ec);
    return res;
}

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

static std::string makeJsonRpc() {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":\"1\",\"method\":\"ping\"}");
}

} // namespace

TEST(HTTPServerMetadata, RootWellKnown_ReturnsConfiguredFields) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);

    mcp::auth::RequireBearerTokenOptions meta;
    meta.authorizationServers = { std::string("https://auth.example.com") };
    meta.scopesSupported = { std::string("files:read"), std::string("files:write") };
    server.SetProtectedResourceMetadata(meta);

    ASSERT_NO_THROW({ server.Start().get(); });
    auto res = httpGet(std::string("127.0.0.1"), port, std::string("/.well-known/oauth-protected-resource"));
    EXPECT_EQ(static_cast<int>(res.result()), 200);
    const std::string b = res.body();
    EXPECT_NE(b.find("\"authorization_servers\""), std::string::npos);
    EXPECT_NE(b.find("https://auth.example.com"), std::string::npos);
    EXPECT_NE(b.find("\"scopes_supported\""), std::string::npos);
    EXPECT_NE(b.find("files:read"), std::string::npos);
    EXPECT_NE(b.find("files:write"), std::string::npos);
    ASSERT_NO_THROW({ server.Stop().get(); });
}

TEST(HTTPServerMetadata, RpcBaseWellKnown_ReturnsConfiguredFields) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    opts.rpcPath = std::string("/mcp/rpc");
    mcp::HTTPServer server(opts);

    mcp::auth::RequireBearerTokenOptions meta;
    meta.authorizationServers = { std::string("https://auth.example.com") };
    meta.scopesSupported = { std::string("s1") };
    server.SetProtectedResourceMetadata(meta);

    ASSERT_NO_THROW({ server.Start().get(); });
    auto res = httpGet(std::string("127.0.0.1"), port, std::string("/.well-known/oauth-protected-resource/mcp"));
    EXPECT_EQ(static_cast<int>(res.result()), 200);
    const std::string b = res.body();
    EXPECT_NE(b.find("https://auth.example.com"), std::string::npos);
    EXPECT_NE(b.find("\"scopes_supported\""), std::string::npos);
    EXPECT_NE(b.find("s1"), std::string::npos);
    ASSERT_NO_THROW({ server.Stop().get(); });
}

TEST(HTTPServerWwwAuth, Enriches401WithScopeAndResourceMetadata) {
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
    auto it = res.base().find(http::field::www_authenticate);
    ASSERT_NE(it, res.base().end());
    mcp::auth::WwwAuthChallenge c; ASSERT_TRUE(mcp::auth::parseWwwAuthenticate(std::string(it->value()), c));
    EXPECT_EQ(c.scheme, std::string("bearer"));
    ASSERT_NE(c.params.find("resource_metadata"), c.params.end());
    EXPECT_EQ(c.params["resource_metadata"], std::string("https://auth.example.com/rs"));
    ASSERT_NE(c.params.find("scope"), c.params.end());
    EXPECT_EQ(c.params["scope"], std::string("s1"));
    ASSERT_NO_THROW({ server.Stop().get(); });
}

TEST(HTTPServerWwwAuth, Enriches403WithErrorScopeAndResourceMetadata) {
    unsigned short port = findFreePort();
    mcp::HTTPServer::Options opts; opts.scheme = std::string("http"); opts.address = std::string("127.0.0.1"); opts.port = std::to_string(port);
    mcp::HTTPServer server(opts);

    server.SetRequestHandler([&](const mcp::JSONRPCRequest& req){ auto resp = std::make_unique<mcp::JSONRPCResponse>(); resp->id = req.id; return resp; });

    MockTokenVerifier verifier; mcp::auth::RequireBearerTokenOptions aopts; aopts.resourceMetadataUrl = std::string("https://auth.example.com/rs"); aopts.requiredScopes = { std::string("need") }; server.SetBearerAuth(verifier, aopts);

    ASSERT_NO_THROW({ server.Start().get(); });
    auto res = httpPost(std::string("127.0.0.1"), port, std::string("/mcp/rpc"), makeJsonRpc(), std::optional<std::string>(std::string("noscope")));
    EXPECT_EQ(static_cast<int>(res.result()), 403);
    auto it = res.base().find(http::field::www_authenticate);
    ASSERT_NE(it, res.base().end());
    mcp::auth::WwwAuthChallenge c; ASSERT_TRUE(mcp::auth::parseWwwAuthenticate(std::string(it->value()), c));
    EXPECT_EQ(c.scheme, std::string("bearer"));
    ASSERT_NE(c.params.find("resource_metadata"), c.params.end());
    EXPECT_EQ(c.params["resource_metadata"], std::string("https://auth.example.com/rs"));
    ASSERT_NE(c.params.find("error"), c.params.end());
    EXPECT_EQ(c.params["error"], std::string("insufficient_scope"));
    ASSERT_NE(c.params.find("scope"), c.params.end());
    EXPECT_EQ(c.params["scope"], std::string("need"));
    ASSERT_NO_THROW({ server.Stop().get(); });
}
