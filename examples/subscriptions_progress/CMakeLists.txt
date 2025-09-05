#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: examples/subscriptions_progress/CMakeLists.txt
# Purpose: Example demonstrating per-URI subscriptions and progress notifications
#==========================================================================================================

cmake_minimum_required(VERSION 3.20)

add_executable(subscriptions_progress
  main.cpp
)

target_link_libraries(subscriptions_progress PRIVATE mcp_cpp)

target_compile_features(subscriptions_progress PRIVATE cxx_std_20)

target_include_directories(subscriptions_progress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
