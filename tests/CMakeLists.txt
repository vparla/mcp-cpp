#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: tests/CMakeLists.txt
# Purpose: MCP C++ SDKS
#==========================================================================================================


cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# Fetch GoogleTest (build-time only)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For MSVC: use static runtime by default
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(mcp_tests
  test_inmemory_transport.cpp
  test_resource_templates.cpp
  test_prompts_get.cpp
  test_tools_inputschema.cpp
  test_sampling_handler.cpp
  test_read_resource.cpp
  test_resource_subscriptions.cpp
  test_client_subscribe_uri.cpp
  test_client_paging.cpp
  test_keepalive.cpp
  test_progress.cpp
  test_server_logging.cpp
  test_logging_rate_limit.cpp
  test_capabilities_logging.cpp
  test_server_initiated_sampling.cpp
  test_cancellation.cpp
  test_initialize_notifications.cpp
  test_errors.cpp
  test_client_typed.cpp
  test_validation_mode.cpp
)

target_link_libraries(mcp_tests
  PRIVATE
    mcp_cpp
    gtest_main
)

target_compile_features(mcp_tests PRIVATE cxx_std_20)

target_include_directories(mcp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

include(GoogleTest)
gtest_discover_tests(mcp_tests)

# Stdio transport integration test: run demo wiring server<->client over FIFOs
add_test(
  NAME StdioDemo.Run
  COMMAND /bin/bash -lc "MCP_STDIO_MODE=1 SERVER_BIN=/src/build/examples/mcp_server/mcp_server CLIENT_BIN=/src/build/examples/mcp_client/mcp_client bash ${CMAKE_SOURCE_DIR}/scripts/run_demo.sh"
)

# Stdio hardening negative scenarios
add_test(
  NAME StdioHardening.IdleReadTimeout
  COMMAND /bin/bash -lc "SERVER_BIN=/src/build/examples/mcp_server/mcp_server bash ${CMAKE_SOURCE_DIR}/scripts/test_stdio_hardening.sh idle"
)
add_test(
  NAME StdioHardening.WriteQueueOverflow
  # Reduce DEBUG noise in CI: lots of framed notification debug lines during overflow scenarios
  # We set MCP_LOG_LEVEL=INFO so logs are still visible but less verbose.
  COMMAND /bin/bash -lc "MCP_LOG_LEVEL=INFO SERVER_BIN=/src/build/examples/mcp_server/mcp_server bash ${CMAKE_SOURCE_DIR}/scripts/test_stdio_hardening.sh overflow"
)
add_test(
  NAME StdioHardening.WriteTimeout
  # Reduce DEBUG noise in CI: many DEBUG messages when writes stall without a reader.
  # We set MCP_LOG_LEVEL=INFO so logs are still visible but less verbose.
  COMMAND /bin/bash -lc "MCP_LOG_LEVEL=INFO SERVER_BIN=/src/build/examples/mcp_server/mcp_server bash ${CMAKE_SOURCE_DIR}/scripts/test_stdio_hardening.sh writetimeout"
)
add_test(
  NAME StdioHardening.BadContentLength
  COMMAND /bin/bash -lc "SERVER_BIN=/src/build/examples/mcp_server/mcp_server bash ${CMAKE_SOURCE_DIR}/scripts/test_stdio_hardening.sh badlength"
)
