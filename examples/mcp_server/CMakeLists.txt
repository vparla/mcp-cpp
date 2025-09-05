# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)

project(mcp_server_example LANGUAGES CXX)

add_executable(mcp_server
    main.cpp
)

# Inherit C++ standard from parent; link against the SDK
target_link_libraries(mcp_server PRIVATE mcp::cpp)

# Useful warnings locally for the example
if(MSVC)
  target_compile_options(mcp_server PRIVATE /W4)
else()
  target_compile_options(mcp_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
