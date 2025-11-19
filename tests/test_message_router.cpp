//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_message_router.cpp
// Purpose: Tests for JsonRpcMessageRouter
//==========================================================================================================

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include <stdexcept>
#include "mcp/JsonRpcMessageRouter.h"
#include "mcp/JSONRPCTypes.h"

namespace mcp {

TEST(Router, ClassifyBasic) {
    auto router = MakeDefaultJsonRpcMessageRouter();

    JSONRPCRequest req; req.id = std::string("id-1"); req.method = std::string("ping");
    auto reqJson = req.Serialize();
    EXPECT_EQ(router->classify(reqJson), IJsonRpcMessageRouter::MessageKind::Request);

    JSONRPCResponse resp; resp.id = std::string("id-1"); resp.result = JSONValue(static_cast<int64_t>(123));
    auto respJson = resp.Serialize();
    EXPECT_EQ(router->classify(respJson), IJsonRpcMessageRouter::MessageKind::Response);

    JSONRPCNotification note{"notify", std::nullopt};
    auto noteJson = note.Serialize();
    EXPECT_EQ(router->classify(noteJson), IJsonRpcMessageRouter::MessageKind::Notification);
}

TEST(Router, ClassifyInvalidJsonIsUnknown) {
    auto router = MakeDefaultJsonRpcMessageRouter();
    std::string bad = "{"; // invalid JSON
    EXPECT_EQ(router->classify(bad), IJsonRpcMessageRouter::MessageKind::Unknown);
}

TEST(Router, ClassifyIdOnlyIsUnknown) {
    auto router = MakeDefaultJsonRpcMessageRouter();
    std::string json = "{\"jsonrpc\":\"2.0\",\"id\":\"x\"}";
    EXPECT_EQ(router->classify(json), IJsonRpcMessageRouter::MessageKind::Unknown);
}

TEST(Router, RouteInvalidJsonCallsErrorHandler) {
    auto router = MakeDefaultJsonRpcMessageRouter();
    std::string bad = "{"; // invalid JSON
    bool errored = false;
    RouterHandlers handlers{};
    handlers.errorHandler = [&](const std::string&){ errored = true; };
    auto resolve = [](JSONRPCResponse&&){};
    auto out = router->route(bad, handlers, resolve);
    EXPECT_FALSE(out.has_value());
    EXPECT_TRUE(errored);
}

TEST(Router, RouteRequestHandlerThrowsReturnsErrorResponse) {
    auto router = MakeDefaultJsonRpcMessageRouter();
    JSONRPCRequest req; req.id = std::string("t-1"); req.method = std::string("boom");
    auto reqJson = req.Serialize();
    RouterHandlers handlers{};
    handlers.requestHandler = [](const JSONRPCRequest&) -> std::unique_ptr<JSONRPCResponse> {
        throw std::runtime_error("handler threw");
    };
    auto resolve = [](JSONRPCResponse&&){};
    auto out = router->route(reqJson, handlers, resolve);
    ASSERT_TRUE(out.has_value());
    JSONRPCResponse parsed;
    ASSERT_TRUE(parsed.Deserialize(out.value()));
    EXPECT_TRUE(parsed.IsError());
    std::string idStr;
    std::visit([&](const auto& id){ using T = std::decay_t<decltype(id)>; if constexpr (std::is_same_v<T, std::string>) { idStr = id; } }, parsed.id);
    EXPECT_EQ(idStr, std::string("t-1"));
}

TEST(Router, RouteRequestNullIdPreservedInResponse) {
    auto router = MakeDefaultJsonRpcMessageRouter();
    JSONRPCRequest req; req.id = nullptr; req.method = std::string("noop");
    auto reqJson = req.Serialize();
    RouterHandlers handlers{};
    handlers.requestHandler = [](const JSONRPCRequest& r) -> std::unique_ptr<JSONRPCResponse> {
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = r.id; // echo id
        resp->result = JSONValue(static_cast<int64_t>(1));
        return resp;
    };
    auto resolve = [](JSONRPCResponse&&){};
    auto out = router->route(reqJson, handlers, resolve);
    ASSERT_TRUE(out.has_value());
    JSONRPCResponse parsed;
    ASSERT_TRUE(parsed.Deserialize(out.value()));
    bool isNull = false;
    std::visit([&](const auto& id){ using T = std::decay_t<decltype(id)>; if constexpr (std::is_same_v<T, std::nullptr_t>) { isNull = true; } }, parsed.id);
    EXPECT_TRUE(isNull);
}

TEST(Router, ClassifyMethodOnlyIsNotification) {
    auto router = MakeDefaultJsonRpcMessageRouter();
    std::string json = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\"}";
    EXPECT_EQ(router->classify(json), IJsonRpcMessageRouter::MessageKind::Notification);
}

TEST(Router, ClassifyNestedIdIsNotification) {
    auto router = MakeDefaultJsonRpcMessageRouter();
    std::string json = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"params\":{\"id\":\"x\"}}";
    EXPECT_EQ(router->classify(json), IJsonRpcMessageRouter::MessageKind::Notification);
}

TEST(Router, ClassifyNullIdIsRequest) {
    auto router = MakeDefaultJsonRpcMessageRouter();
    std::string json = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":null}";
    EXPECT_EQ(router->classify(json), IJsonRpcMessageRouter::MessageKind::Request);
}

TEST(Router, ClassifyNestedResultNotResponse) {
    auto router = MakeDefaultJsonRpcMessageRouter();
    std::string json = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"params\":{\"result\":{}}}";
    EXPECT_NE(router->classify(json), IJsonRpcMessageRouter::MessageKind::Response);
}

TEST(Router, ClassifyUnknownWhenNoKeys) {
    auto router = MakeDefaultJsonRpcMessageRouter();
    std::string json = "{\"jsonrpc\":\"2.0\"}";
    EXPECT_EQ(router->classify(json), IJsonRpcMessageRouter::MessageKind::Unknown);
}

TEST(Router, RouteResponseResolves) {
    auto router = MakeDefaultJsonRpcMessageRouter();

    // Prepare response JSON
    JSONRPCResponse resp; resp.id = std::string("abc"); resp.result = JSONValue(static_cast<int64_t>(42));
    auto respJson = resp.Serialize();

    // Capture resolved id
    std::string resolvedId;
    auto resolve = [&](JSONRPCResponse&& r) {
        std::visit([&](auto&& idVal) {
            using T = std::decay_t<decltype(idVal)>;
            if constexpr (std::is_same_v<T, std::string>) {
                resolvedId = idVal;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                resolvedId = std::to_string(idVal);
            }
        }, r.id);
    };

    RouterHandlers handlers{};
    auto out = router->route(respJson, handlers, resolve);
    EXPECT_FALSE(out.has_value());
    EXPECT_EQ(resolvedId, "abc");
}

TEST(Router, RouteRequestReturnsSerialized) {
    auto router = MakeDefaultJsonRpcMessageRouter();

    JSONRPCRequest req; req.id = std::string("r-1"); req.method = std::string("echo");
    auto reqJson = req.Serialize();

    RouterHandlers handlers{};
    handlers.requestHandler = [](const JSONRPCRequest& r) -> std::unique_ptr<JSONRPCResponse> {
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = r.id;
        resp->result = JSONValue(static_cast<int64_t>(7));
        return resp;
    };

    auto resolve = [](JSONRPCResponse&&) {};
    auto out = router->route(reqJson, handlers, resolve);
    ASSERT_TRUE(out.has_value());

    JSONRPCResponse parsed;
    ASSERT_TRUE(parsed.Deserialize(out.value()));
    EXPECT_FALSE(parsed.IsError());
    ASSERT_TRUE(parsed.result.has_value());
}

TEST(Router, RouteNotificationDispatches) {
    auto router = MakeDefaultJsonRpcMessageRouter();

    JSONRPCNotification note{"progress", std::nullopt};
    auto noteJson = note.Serialize();

    bool notified = false;
    RouterHandlers handlers{};
    handlers.notificationHandler = [&](std::unique_ptr<JSONRPCNotification> n) {
        (void)n;
        notified = true;
    };

    auto resolve = [](JSONRPCResponse&&) {};
    auto out = router->route(noteJson, handlers, resolve);
    EXPECT_FALSE(out.has_value());
    EXPECT_TRUE(notified);
}

TEST(Router, RouteUnknownWarns) {
    auto router = MakeDefaultJsonRpcMessageRouter();

    std::string badJson = "{}";
    bool errored = false;
    RouterHandlers handlers{};
    handlers.errorHandler = [&](const std::string& msg) {
        (void)msg;
        errored = true;
    };

    auto resolve = [](JSONRPCResponse&&) {};
    auto out = router->route(badJson, handlers, resolve);
    EXPECT_FALSE(out.has_value());
    EXPECT_TRUE(errored);
}

} // namespace mcp
