#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: examples/resource_chunking/CMakeLists.txt
# Purpose: Example demonstrating experimental resource read chunking
#==========================================================================================================
cmake_minimum_required(VERSION 3.20)

project(resource_chunking_example LANGUAGES CXX)

add_executable(resource_chunking
    main.cpp
)

target_link_libraries(resource_chunking PRIVATE mcp::cpp)

if(MSVC)
  target_compile_options(resource_chunking PRIVATE /W4)
else()
  target_compile_options(resource_chunking PRIVATE -Wall -Wextra -Wpedantic)
endif()
