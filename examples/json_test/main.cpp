//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: JSON-RPC round-trip test example
//==========================================================================================================

#include <iostream>
#include "logging/Logger.h"
#include "mcp/JSONRPCTypes.h"
#include "mcp/Protocol.h"

using namespace mcp;

int main() {
    FUNC_SCOPE();
    Logger::setLogLevel(LogLevel::LOG_DEBUG_LEVEL);

    // Build a request
    JSONRPCRequest req("1", Methods::ListTools);
    std::string s = req.Serialize();
    LOG_INFO("Serialized request: {}", s);

    // Parse it back
    JSONRPCRequest parsed;
    if (parsed.Deserialize(s)) {
        LOG_INFO("Parsed request method: {}", parsed.method);
    } else {
        LOG_ERROR("Failed to parse request");
    }

    // Build a response with a minimal result
    JSONValue::Object resultObj;
    resultObj["ok"] = std::make_shared<JSONValue>(true);
    JSONRPCResponse resp("1", JSONValue{resultObj});
    std::string r = resp.Serialize();
    LOG_INFO("Serialized response: {}", r);

    // Parse the response
    JSONRPCResponse parsedResp;
    if (parsedResp.Deserialize(r)) {
        LOG_INFO("Parsed response has result: {}", parsedResp.result.has_value() ? "yes" : "no");
    } else {
        LOG_ERROR("Failed to parse response");
    }

    std::cout << "json_test example finished" << std::endl;
    return 0;
}
