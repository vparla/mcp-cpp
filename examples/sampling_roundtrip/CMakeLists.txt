#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: examples/sampling_roundtrip/CMakeLists.txt
# Purpose: Build configuration for the sampling_roundtrip example
#==========================================================================================================

add_executable(sampling_roundtrip
  main.cpp
)

target_link_libraries(sampling_roundtrip
  PRIVATE
    mcp_cpp
)

target_compile_features(sampling_roundtrip PRIVATE cxx_std_20)

target_include_directories(sampling_roundtrip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
