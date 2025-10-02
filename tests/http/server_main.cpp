//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/http/server_main.cpp
// Purpose: Minimal HTTP(S) MCP server (TLS 1.3 only for HTTPS) for e2e tests in a separate container
//==========================================================================================================

#include <csignal>
#include <chrono>
#include <thread>
#include <atomic>
#include <iostream>

#include "logging/Logger.h"
#include "mcp/JSONRPCTypes.h"
#include "mcp/HTTPServer.hpp"

static std::atomic<bool> gRunning{true};

static void handleSig(int) {
    gRunning.store(false);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    ::signal(SIGTERM, handleSig);
    ::signal(SIGINT, handleSig);

    mcp::HTTPServer::Options opts;
    opts.address = "0.0.0.0";
    opts.port = "9443";
    opts.rpcPath = "/mcp/rpc";
    opts.notifyPath = "/mcp/notify";
    opts.scheme = "https";
    opts.certFile = "/certs/cert.pem";
    opts.keyFile  = "/certs/key.pem";

    mcp::HTTPServer server(opts);

    server.SetRequestHandler([](const mcp::JSONRPCRequest& req) -> std::unique_ptr<mcp::JSONRPCResponse> {
        if (req.method == "echo") {
            auto resp = std::make_unique<mcp::JSONRPCResponse>();
            resp->id = req.id;
            // Simple echo result: { "ok": true, "method": "..." }
            mcp::JSONValue::Object obj;
            obj["ok"] = std::make_shared<mcp::JSONValue>(true);
            obj["method"] = std::make_shared<mcp::JSONValue>(req.method);
            resp->result = mcp::JSONValue(obj);
            return resp;
        }
        if (req.method == "sleep") {
            int64_t ms = 0;
            bool ok = false;
            if (req.params.has_value()) {
                const auto& pv = req.params.value().value;
                if (std::holds_alternative<mcp::JSONValue::Object>(pv)) {
                    const auto& obj = std::get<mcp::JSONValue::Object>(pv);
                    auto it = obj.find("ms");
                    if (it != obj.end() && it->second) {
                        if (std::holds_alternative<int64_t>(it->second->value)) {
                            ms = std::get<int64_t>(it->second->value);
                            ok = ms > 0;
                        }
                    }
                }
            }
            if (!ok) {
                auto er = std::make_unique<mcp::JSONRPCResponse>(); er->id = req.id;
                er->error = mcp::CreateErrorObject(mcp::JSONRPCErrorCodes::InvalidParams, "sleep requires integer ms > 0");
                return er;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(ms)));
            auto resp = std::make_unique<mcp::JSONRPCResponse>();
            resp->id = req.id;
            mcp::JSONValue::Object obj; obj["sleptMs"] = std::make_shared<mcp::JSONValue>(ms);
            resp->result = mcp::JSONValue(obj);
            return resp;
        }
        auto err = std::make_unique<mcp::JSONRPCResponse>();
        err->id = req.id;
        err->error = mcp::CreateErrorObject(mcp::JSONRPCErrorCodes::MethodNotFound, "Method not found");
        return err;
    });

    server.SetErrorHandler([](const std::string& err){
        LOG_ERROR("Server error: {}", err);
    });

    auto fut = server.Start();
    fut.wait();
    LOG_INFO("HTTP(S) server started on 0.0.0.0:9443");

    while (gRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    (void)server.Stop().wait();
    LOG_INFO("HTTP(S) server stopped");
    return 0;
}
