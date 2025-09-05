# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)

project(mcp_client_example LANGUAGES CXX)

add_executable(mcp_client
    main.cpp
)

# Inherit C++ standard from parent; link against the SDK
target_link_libraries(mcp_client PRIVATE mcp::cpp)

# Useful warnings locally for the example
if(MSVC)
  target_compile_options(mcp_client PRIVATE /W4)
else()
  target_compile_options(mcp_client PRIVATE -Wall -Wextra -Wpedantic)
endif()
