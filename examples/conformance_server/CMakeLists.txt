#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: examples/conformance_server/CMakeLists.txt
# Purpose: Build the MCP server conformance example
#==========================================================================================================

cmake_minimum_required(VERSION 3.20)

project(mcp_conformance_server_example LANGUAGES CXX)

add_executable(conformance_server
    main.cpp
)

target_link_libraries(conformance_server PRIVATE mcp::cpp)
target_include_directories(conformance_server PRIVATE ${CMAKE_SOURCE_DIR})

if(MSVC)
  target_compile_options(conformance_server PRIVATE /W4)
else()
  target_compile_options(conformance_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
