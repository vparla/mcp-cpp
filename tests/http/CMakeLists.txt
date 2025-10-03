#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: tests/http/CMakeLists.txt
# Purpose: Build HTTP(S) server binary and client GoogleTest suite (TLS 1.3 only for HTTPS)
#==========================================================================================================

cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# Fetch GoogleTest for client tests
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Server binary
add_executable(http_server_bin
  server_main.cpp
)

target_link_libraries(http_server_bin
  PRIVATE
    mcp_cpp
)

target_compile_features(http_server_bin PRIVATE cxx_std_20)

target_include_directories(http_server_bin
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Client tests
add_executable(http_client_tests
  client_tests.cpp
)

target_link_libraries(http_client_tests
  PRIVATE
    mcp_cpp
    gtest_main
)

target_compile_features(http_client_tests PRIVATE cxx_std_20)

target_include_directories(http_client_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

include(GoogleTest)
gtest_discover_tests(http_client_tests)
