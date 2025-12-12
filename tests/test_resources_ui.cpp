//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_resources_ui.cpp
// Purpose: Functional and Negative/edge tests for UI Resources
//==========================================================================================================

#include <gtest/gtest.h>
#include <future>
#include <stop_token>
#include "mcp/Server.h"
#include "mcp/Protocol.h"
using namespace mcp;

namespace {
std::future<ResourceContent> makeUiHandler(const std::string& html, const JSONValue& uiMeta) {
    return std::async(std::launch::deferred, [html, uiMeta]() {
        ReadResourceResult r;
        JSONValue::Object item;
        item["type"] = std::make_shared<JSONValue>(std::string("text"));
        item["text"] = std::make_shared<JSONValue>(html);
        item["_meta"] = std::make_shared<JSONValue>(uiMeta);
        r.contents.push_back(JSONValue{item});
        return r;
    });
}
static void initializeWithUiExtension(Server& server) {
    JSONValue::Array arr;
    arr.push_back(std::make_shared<JSONValue>(std::string("text/html+mcp")));
    JSONValue::Object uiObj; uiObj["mimeTypes"] = std::make_shared<JSONValue>(arr);
    JSONValue::Object extObj; extObj[Extensions::uiExtensionId] = std::make_shared<JSONValue>(JSONValue{uiObj});
    JSONValue::Object caps; caps["extensions"] = std::make_shared<JSONValue>(JSONValue{extObj});
    JSONValue::Object params; params["capabilities"] = std::make_shared<JSONValue>(JSONValue{caps});
    JSONRPCRequest req; req.id = std::string("1"); req.method = Methods::Initialize; req.params.emplace(JSONValue{params});
    (void)server.HandleJSONRPC(req);
}
}

static JSONRPCRequest makeListResourcesReq() { JSONRPCRequest req; req.id = std::string("1"); req.method = Methods::ListResources; return req; }

TEST(ResourcesUi, ListAdvertisesMimeTypeForUiScheme) {
    Server server("UI-Server");
    const std::string uri = "ui://demo/app";
    JSONValue::Object ui; ui["csp"] = std::make_shared<JSONValue>(std::string("default-src 'self'")); ui["domain"] = std::make_shared<JSONValue>(std::string("demo.local")); ui["prefersBorder"] = std::make_shared<JSONValue>(true);
    server.RegisterResource(uri, [uri, ui](const std::string& reqUri, std::stop_token st) -> std::future<ResourceContent> {
        (void)st; EXPECT_EQ(reqUri, uri);
        JSONValue::Object meta; meta["ui"] = std::make_shared<JSONValue>(JSONValue{ui});
        return makeUiHandler("<!doctype html><html><body>ok</body></html>", JSONValue{meta});
    });
    initializeWithUiExtension(server);
    auto listResp = server.HandleJSONRPC(makeListResourcesReq());
    ASSERT_TRUE(listResp && !listResp->IsError() && listResp->result.has_value());
    const auto& ro = std::get<JSONValue::Object>(listResp->result->value);
    const auto& arr = std::get<JSONValue::Array>(ro.at("resources")->value);
    bool found = false; for (const auto& v : arr) { const auto& obj = std::get<JSONValue::Object>(v->value); auto uIt = obj.find("uri"); ASSERT_TRUE(uIt != obj.end()); if (std::holds_alternative<std::string>(uIt->second->value) && std::get<std::string>(uIt->second->value) == uri) { auto mIt = obj.find("mimeType"); ASSERT_TRUE(mIt != obj.end()); ASSERT_TRUE(std::holds_alternative<std::string>(mIt->second->value)); EXPECT_EQ(std::get<std::string>(mIt->second->value), std::string("text/html+mcp")); found = true; } }
    EXPECT_TRUE(found);
}

TEST(ResourcesUi, ReadPassesThroughUiMeta) {
    Server server("UI-Server");
    const std::string uri = "ui://demo/app";
    JSONValue::Object ui; ui["csp"] = std::make_shared<JSONValue>(std::string("default-src 'self'")); ui["domain"] = std::make_shared<JSONValue>(std::string("demo.local")); ui["prefersBorder"] = std::make_shared<JSONValue>(true);
    JSONValue::Object meta; meta["ui"] = std::make_shared<JSONValue>(JSONValue{ui});
    server.RegisterResource(uri, [meta, uri](const std::string& reqUri, std::stop_token st) -> std::future<ResourceContent> { (void)st; EXPECT_EQ(reqUri, uri); return makeUiHandler("<html>ok</html>", JSONValue{meta}); });
    initializeWithUiExtension(server);
    auto res = server.ReadResource(uri).get(); ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(res.value)); const auto& obj = std::get<JSONValue::Object>(res.value);
    auto metaIt = obj.find("_meta"); ASSERT_TRUE(metaIt != obj.end()); ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(metaIt->second->value)); const auto& mo = std::get<JSONValue::Object>(metaIt->second->value);
    auto uiIt = mo.find("ui"); ASSERT_TRUE(uiIt != mo.end()); ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(uiIt->second->value)); const auto& uo = std::get<JSONValue::Object>(uiIt->second->value);
    ASSERT_TRUE(std::holds_alternative<std::string>(uo.at("csp")->value)); ASSERT_TRUE(std::holds_alternative<std::string>(uo.at("domain")->value)); ASSERT_TRUE(std::holds_alternative<bool>(uo.at("prefersBorder")->value));
}

TEST(ResourcesUi, ReadWithoutMetaOmitsTopLevelMeta) {
    Server server("UI-Server"); const std::string uri = "ui://demo/no-meta";
    server.RegisterResource(uri, [uri](const std::string& reqUri, std::stop_token st) -> std::future<ResourceContent> { (void)st; EXPECT_EQ(reqUri, uri); return std::async(std::launch::deferred, [](){ ReadResourceResult r; JSONValue::Object item; item["type"] = std::make_shared<JSONValue>(std::string("text")); item["text"] = std::make_shared<JSONValue>(std::string("no meta")); r.contents.push_back(JSONValue{item}); return r; }); });
    auto res = server.ReadResource(uri).get(); ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(res.value)); const auto& obj = std::get<JSONValue::Object>(res.value); EXPECT_TRUE(obj.find("_meta") == obj.end());
}

TEST(ResourcesUi, ListHidesUiWhenExtensionAbsent) {
    Server server("UI-Server");
    const std::string uri = "ui://hidden/app";
    server.RegisterResource(uri, [uri](const std::string& reqUri, std::stop_token st) -> std::future<ResourceContent> { (void)st; EXPECT_EQ(reqUri, uri); JSONValue::Object meta; JSONValue::Object ui; ui["csp"] = std::make_shared<JSONValue>(std::string("default-src 'self'")); meta["ui"] = std::make_shared<JSONValue>(JSONValue{ui}); return makeUiHandler("<html>hidden</html>", JSONValue{meta}); });
    auto listResp = server.HandleJSONRPC(makeListResourcesReq());
    ASSERT_TRUE(listResp && !listResp->IsError() && listResp->result.has_value());
    const auto& ro = std::get<JSONValue::Object>(listResp->result->value);
    const auto& arr = std::get<JSONValue::Array>(ro.at("resources")->value);
    bool found = false; for (const auto& v : arr) { const auto& obj = std::get<JSONValue::Object>(v->value); auto uIt = obj.find("uri"); ASSERT_TRUE(uIt != obj.end()); if (std::holds_alternative<std::string>(uIt->second->value) && std::get<std::string>(uIt->second->value) == uri) { found = true; } }
    EXPECT_FALSE(found);
}

TEST(ResourcesUi, ReadStripsUiMetaWhenExtensionAbsent) {
    Server server("UI-Server");
    const std::string uri = "ui://demo/strip";
    JSONValue::Object ui; ui["csp"] = std::make_shared<JSONValue>(std::string("default-src 'self'"));
    JSONValue::Object meta; meta["ui"] = std::make_shared<JSONValue>(JSONValue{ui});
    server.RegisterResource(uri, [meta, uri](const std::string& reqUri, std::stop_token st) -> std::future<ResourceContent> { (void)st; EXPECT_EQ(reqUri, uri); return makeUiHandler("<html>ok</html>", JSONValue{meta}); });
    auto res = server.ReadResource(uri).get();
    ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(res.value));
    const auto& obj = std::get<JSONValue::Object>(res.value);
    auto metaIt = obj.find("_meta");
    EXPECT_TRUE(metaIt == obj.end());
}
