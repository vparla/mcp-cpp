//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: examples/stdio_smoke/main.cpp
// Purpose: Windows-native stdio smoke test to exercise StdioTransport paths
//==========================================================================================================

#include <chrono>
#include <thread>
#include <iostream>
#include "mcp/StdioTransport.hpp"
#include "logging/Logger.h"

int main() {
    using namespace mcp;
    Logger::setLogLevel(LogLevel::LOG_INFO_LEVEL);

    StdioTransportFactory f;
    // Very short timeouts to traverse code paths quickly
    auto t = f.CreateTransport("timeout_ms=100; idle_read_timeout_ms=100; write_timeout_ms=100; write_queue_max_bytes=1024");

    // Start and immediately close to exercise start/stop on native platform
    try { 
        t->Start().get(); 
    } catch (...) { 
        LOG_ERROR("Start failed"); 
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    try { 
        t->Close().get(); 
    } catch (...) {
        LOG_ERROR("Close failed"); 
    }

    // If we reached here without crashing, declare success
    std::cout << "stdio_smoke: ok" << std::endl;
    return 0;   
}
