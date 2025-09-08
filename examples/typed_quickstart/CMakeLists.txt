#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: examples/typed_quickstart/CMakeLists.txt
# Purpose: Typed client wrappers quick-start example
#==========================================================================================================
cmake_minimum_required(VERSION 3.20)

project(typed_quickstart_example LANGUAGES CXX)

add_executable(typed_quickstart
    main.cpp
)

target_link_libraries(typed_quickstart PRIVATE mcp::cpp)

if(MSVC)
  target_compile_options(typed_quickstart PRIVATE /W4)
else()
  target_compile_options(typed_quickstart PRIVATE -Wall -Wextra -Wpedantic)
endif()
