#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: examples/stdio_smoke/CMakeLists.txt
# Purpose: Windows-native stdio smoke example (also builds on Linux/macOS)
#==========================================================================================================

cmake_minimum_required(VERSION 3.20)

add_executable(stdio_smoke
  main.cpp
)

# Link against the SDK static library
target_link_libraries(stdio_smoke PRIVATE mcp_cpp)

target_compile_features(stdio_smoke PRIVATE cxx_std_20)

target_include_directories(stdio_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
