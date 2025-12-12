//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_capabilities_extensions.cpp
// Purpose: Functional and Negative/edge tests for client capabilities.extensions parsing in initialize
//==========================================================================================================

#include <gtest/gtest.h>

#include <string>

#include "mcp/Server.h"
#include "mcp/Protocol.h"
#include "mcp/JSONRPCTypes.h"

using namespace mcp;

static JSONRPCRequest makeInitReqWithParams(const JSONValue& paramsVal) {
    JSONRPCRequest req;
    req.id = std::string("1");
    req.method = Methods::Initialize;
    req.params.emplace(paramsVal);
    return req;
}

TEST(CapabilitiesExtensions, Initialize_WithValidUiExtension) {
    Server server("Caps-Ext-Server");

    JSONValue::Object mimeObj; // value for the extension is an object with mimeTypes array
    {
        JSONValue::Array arr;
        arr.push_back(std::make_shared<JSONValue>(std::string("text/html+mcp")));
        JSONValue::Object uiObj;
        uiObj["mimeTypes"] = std::make_shared<JSONValue>(arr);
        JSONValue::Object extObj;
        extObj[Extensions::uiExtensionId] = std::make_shared<JSONValue>(JSONValue{uiObj});
        JSONValue::Object caps;
        caps["extensions"] = std::make_shared<JSONValue>(JSONValue{extObj});
        JSONValue::Object params;
        params["capabilities"] = std::make_shared<JSONValue>(JSONValue{caps});
        auto resp = server.HandleJSONRPC(makeInitReqWithParams(JSONValue{params}));
        ASSERT_TRUE(resp != nullptr);
        ASSERT_FALSE(resp->IsError());
        ASSERT_TRUE(resp->result.has_value());
        ASSERT_TRUE(std::holds_alternative<JSONValue::Object>(resp->result->value));
        const auto& initObj = std::get<JSONValue::Object>(resp->result->value);
        ASSERT_TRUE(initObj.find("protocolVersion") != initObj.end());
        ASSERT_TRUE(initObj.find("capabilities") != initObj.end());
    }
}

TEST(CapabilitiesExtensions, Initialize_WithEmptyExtensionsObject) {
    Server server("Caps-Ext-Server");

    JSONValue::Object params;
    JSONValue::Object caps;
    caps["extensions"] = std::make_shared<JSONValue>(JSONValue::Object{});
    params["capabilities"] = std::make_shared<JSONValue>(JSONValue{caps});

    auto resp = server.HandleJSONRPC(makeInitReqWithParams(JSONValue{params}));
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
}

TEST(CapabilitiesExtensions, Initialize_WithNonObjectExtensions) {
    Server server("Caps-Ext-Server");

    JSONValue::Object params;
    JSONValue::Object caps;
    caps["extensions"] = std::make_shared<JSONValue>(std::string("bad-shape"));
    params["capabilities"] = std::make_shared<JSONValue>(JSONValue{caps});

    auto resp = server.HandleJSONRPC(makeInitReqWithParams(JSONValue{params}));
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
}

TEST(CapabilitiesExtensions, Initialize_WithWrongTypeInsideExtensionValue) {
    Server server("Caps-Ext-Server");

    JSONValue::Object extObj;
    extObj[Extensions::uiExtensionId] = std::make_shared<JSONValue>(static_cast<int64_t>(42));
    JSONValue::Object caps;
    caps["extensions"] = std::make_shared<JSONValue>(JSONValue{extObj});
    JSONValue::Object params;
    params["capabilities"] = std::make_shared<JSONValue>(JSONValue{caps});

    auto resp = server.HandleJSONRPC(makeInitReqWithParams(JSONValue{params}));
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
}

TEST(CapabilitiesExtensions, Initialize_WithoutCapabilitiesAndParams) {
    Server server("Caps-Ext-Server");

    JSONRPCRequest req;
    req.id = std::string("1");
    req.method = Methods::Initialize;
    auto resp = server.HandleJSONRPC(req);
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
}

TEST(CapabilitiesExtensions, Initialize_WithExperimentalAndExtensionsTogether) {
    Server server("Caps-Ext-Server");

    JSONValue::Object expObj;
    expObj["logLevel"] = std::make_shared<JSONValue>(std::string("ERROR"));
    JSONValue::Object caps;
    caps["experimental"] = std::make_shared<JSONValue>(JSONValue{expObj});
    caps["extensions"] = std::make_shared<JSONValue>(JSONValue::Object{});
    JSONValue::Object params;
    params["capabilities"] = std::make_shared<JSONValue>(JSONValue{caps});

    auto resp = server.HandleJSONRPC(makeInitReqWithParams(JSONValue{params}));
    ASSERT_TRUE(resp != nullptr);
    ASSERT_FALSE(resp->IsError());
}
