//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: Basic example printing SDK version and a few log lines
//==========================================================================================================

#include <iostream>
#include "mcp/version.h"
#include "logging/Logger.h"

int main() {
    FUNC_SCOPE();
    Logger::setLogLevel(LogLevel::LOG_DEBUG_LEVEL);

    auto v = mcp::getVersionString();
    std::cout << "MCP C++ SDK version: " << v << std::endl;

    LOG_INFO("Starting basic example... Version: {}", v);
    LOG_DEBUG("Debug value: {}", 42);
    LOG_WARN("This is a warning about {}", "something");
    LOG_ERROR("This is a test error path");
    return 0;
}
