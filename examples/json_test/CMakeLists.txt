# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)

project(json_test_example LANGUAGES CXX)

add_executable(json_test
    main.cpp
)

target_link_libraries(json_test PRIVATE mcp::cpp)

if(MSVC)
  target_compile_options(json_test PRIVATE /W4)
else()
  target_compile_options(json_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
