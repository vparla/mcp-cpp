#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: CMakeLists.txt
# Purpose: MCP C++ SDKS
#==========================================================================================================
cmake_minimum_required(VERSION 3.20)

# Fail early if a lower standard is attempted
if(DEFINED CMAKE_CXX_STANDARD AND CMAKE_CXX_STANDARD LESS 20)
  message(FATAL_ERROR "C++20 is required for this project.")
endif()

project(mcp_cpp
    VERSION 0.1.0
    DESCRIPTION "MCP C++ SDK"
    LANGUAGES CXX)

# Options
option(MCP_BUILD_EXAMPLES "Build examples" ON)
option(MCP_BUILD_TESTS "Build tests" OFF)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Baseline warnings globally (not Werror to avoid impacting third-party deps)
if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Library target
add_library(mcp_cpp STATIC
    src/mcp/version.cpp
    src/logging/Logger.cpp
    src/mcp/JSONParser.cpp
    src/mcp/StdioTransport.cpp
    src/mcp/InMemoryTransport.cpp
    src/mcp/SharedMemoryTransport.cpp
    src/mcp/HTTPTransport.cpp
    src/mcp/HTTPServer.cpp
    src/mcp/Client.cpp
    src/mcp/Server.cpp
    src/mcp/auth/OAuthClient.cpp
    src/mcp/auth/OAuth2ClientCredentialsAuth.cpp
    src/mcp/auth/WwwAuthenticate.cpp
    src/mcp/auth/ServerAuth.cpp
    src/mcp/JsonRpcMessageRouter.cpp
    src/mcp/ContentLengthFramer.cpp
)
add_library(mcp::cpp ALIAS mcp_cpp)

# Require C++20 features for the library and propagate to dependents
target_compile_features(mcp_cpp PUBLIC cxx_std_20)

# Treat warnings as errors for our library target only
if(MSVC)
  target_compile_options(mcp_cpp PRIVATE /WX)
else()
  target_compile_options(mcp_cpp PRIVATE -Werror)
endif()

# Public headers
target_include_directories(mcp_cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# HTTP Transport dependencies: header-only Boost (Beast/Asio) + OpenSSL
find_path(BOOST_INCLUDE_DIR NAMES boost/beast.hpp PATH_SUFFIXES include)
if(NOT BOOST_INCLUDE_DIR)
  message(FATAL_ERROR "Boost headers not found. Please install Boost headers (e.g., libboost-dev or vcpkg).")
endif()
find_package(OpenSSL REQUIRED)

target_include_directories(mcp_cpp PUBLIC ${BOOST_INCLUDE_DIR})
target_compile_definitions(mcp_cpp PUBLIC
  BOOST_ERROR_CODE_HEADER_ONLY
  BOOST_SYSTEM_NO_DEPRECATED
  BOOST_ASIO_NO_DEPRECATED
  BOOST_DATE_TIME_NO_LIB)

target_link_libraries(mcp_cpp PUBLIC OpenSSL::SSL OpenSSL::Crypto)

if(WIN32)
  target_link_libraries(mcp_cpp PUBLIC ws2_32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(mcp_cpp PUBLIC Threads::Threads)
endif()

# Platform-specific defines (reserved for future use)
if(WIN32)
  target_compile_definitions(mcp_cpp PUBLIC MCP_PLATFORM_WINDOWS)
elseif(APPLE)
  target_compile_definitions(mcp_cpp PUBLIC MCP_PLATFORM_MACOS)
elseif(UNIX)
  target_compile_definitions(mcp_cpp PUBLIC MCP_PLATFORM_LINUX)
endif()

# Examples
if(MCP_BUILD_EXAMPLES)
  # Examples
  add_subdirectory(examples/basic)
  add_subdirectory(examples/json_test)
  add_subdirectory(examples/mcp_client)
  add_subdirectory(examples/mcp_server)
  add_subdirectory(examples/stdio_smoke)
  add_subdirectory(examples/subscriptions_progress)
  add_subdirectory(examples/typed_quickstart)
  add_subdirectory(examples/resource_chunking)
  add_subdirectory(examples/sampling_roundtrip)
  add_subdirectory(examples/logging_demo)
  add_subdirectory(examples/keepalive_demo)
endif()

# Tests (to be implemented in a later step)
if(MCP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(tests/http)
endif()
