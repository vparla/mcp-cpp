#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: examples/keepalive_demo/CMakeLists.txt
# Purpose: Build configuration for the keepalive_demo example
#==========================================================================================================

add_executable(keepalive_demo
  main.cpp
)

target_link_libraries(keepalive_demo
  PRIVATE
    mcp_cpp
)

target_compile_features(keepalive_demo PRIVATE cxx_std_20)

target_include_directories(keepalive_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
