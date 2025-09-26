#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: examples/logging_demo/CMakeLists.txt
# Purpose: Build configuration for the logging_demo example
#==========================================================================================================

add_executable(logging_demo
  main.cpp
)

target_link_libraries(logging_demo
  PRIVATE
    mcp_cpp
)

target_compile_features(logging_demo PRIVATE cxx_std_20)

target_include_directories(logging_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
