# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)

project(basic_example LANGUAGES CXX)

add_executable(basic
    main.cpp
)

target_link_libraries(basic PRIVATE mcp::cpp)

if(MSVC)
  target_compile_options(basic PRIVATE /W4)
else()
  target_compile_options(basic PRIVATE -Wall -Wextra -Wpedantic)
endif()
