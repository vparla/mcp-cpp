//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: examples/conformance_server/main.cpp
// Purpose: Standalone streamable HTTP MCP server fixture for the official server conformance suite
//==========================================================================================================

#include <atomic>
#include <chrono>
#include <csignal>
#include <optional>
#include <string>
#include <thread>

#include "logging/Logger.h"
#include "mcp/HTTPServer.hpp"
#include "mcp/Server.h"
#include "mcp/validation/Validation.h"
#include "src/mcp/ConformanceServerSupport.h"

using namespace mcp;

namespace {

std::atomic<bool> gStopRequested{false};

void handleSignal(int) {
    gStopRequested.store(true);
}

std::optional<std::string> getArgValue(int argc, char** argv, const std::string& key) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        std::string arg = argv[i];
        const size_t eq = arg.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        if (arg.substr(0, eq) == key) {
            return arg.substr(eq + 1);
        }
    }
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    Logger::setLogLevelFromString("INFO");
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    HTTPServer::Options options;
    options.scheme = "http";
    options.address = getArgValue(argc, argv, "--address").value_or("127.0.0.1");
    options.port = getArgValue(argc, argv, "--port").value_or("3001");
    options.endpointPath = getArgValue(argc, argv, "--endpointPath").value_or("/mcp");
    options.streamPath = getArgValue(argc, argv, "--streamPath").value_or("");

    Server server("MCP Conformance Server");
    server.SetValidationMode(validation::ValidationMode::Strict);
    conformance::RegisterConformanceServerProfile(server);
    server.SetErrorHandler([](const std::string& error) {
        LOG_ERROR("Conformance server error: {}", error);
        gStopRequested.store(true);
    });

    LOG_INFO("Starting conformance server on http://{}:{}{}", options.address, options.port, options.endpointPath);
    server.Start(std::make_unique<HTTPServer>(options)).get();

    while (!gStopRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO("Stopping conformance server");
    server.Stop().get();
    return 0;
}
