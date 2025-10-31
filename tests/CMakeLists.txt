#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: tests/CMakeLists.txt
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
# Do not install GTest when building in CI/containers
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(mcp_tests
  test_resource_templates.cpp
  test_prompts_get.cpp
  test_tools_inputschema.cpp
  test_sampling_handler.cpp
  test_read_resource.cpp
  test_keepalive_threshold_config.cpp
  test_negative_api.cpp
  test_client_paging.cpp
  test_resource_read_chunking.cpp
  test_resource_read_chunking_capability_absence.cpp
  test_resource_read_chunking_overload.cpp
  test_acceptor_interface.cpp
  test_acceptor_factory.cpp
  test_cancellation.cpp
  test_capabilities_logging.cpp
  test_client_cache.cpp
  test_client_cache_extended.cpp
  test_client_subscribe_uri.cpp
  test_client_typed.cpp
  test_errors.cpp
  test_initialize_notifications.cpp
  test_inmemory_transport.cpp
  test_keepalive.cpp
  test_logging_rate_limit.cpp
  test_progress.cpp
  test_prompt_pre_dispatch_cancel.cpp
  test_resource_subscriptions.cpp
  test_sampling_cancellation_e2e.cpp
  test_sampling_result_builder.cpp
  test_sampling_typed_helpers.cpp
  test_server_initiated_sampling.cpp
  test_server_logging.cpp
  test_handle_jsonrpc.cpp
  test_typed_cancellation.cpp
  test_validation_lists.cpp
  test_validation_mode.cpp
  test_validation_progress.cpp
  test_validation_prompts.cpp
  test_validation_sampling.cpp
  test_validation_strict.cpp
  test_shared_memory_transport.cpp
  test_http_auth.cpp
  test_http_server_bearer.cpp
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

set(RUN_DEMO_SH "${CMAKE_SOURCE_DIR}/scripts/run_demo.sh")
set(STDIO_HARDENING_SH "${CMAKE_SOURCE_DIR}/scripts/test_stdio_hardening.sh")

if (EXISTS "${RUN_DEMO_SH}" AND EXISTS "${STDIO_HARDENING_SH}")
  # Stdio transport integration test: run demo wiring server<->client over FIFOs
  add_test(
    NAME TransportDemo.Run
    COMMAND /bin/bash -lc "MCP_STDIO_MODE=1 SERVER_BIN=/src/build/examples/mcp_server/mcp_server CLIENT_BIN=/src/build/examples/mcp_client/mcp_client bash ${RUN_DEMO_SH}"
  )

  # Stdio hardening negative scenarios
  add_test(
    NAME StdioHardening.IdleReadTimeout
    COMMAND /bin/bash -lc "SERVER_BIN=/src/build/examples/mcp_server/mcp_server bash ${STDIO_HARDENING_SH} idle"
  )
  add_test(
    NAME StdioHardening.WriteQueueOverflow
    # Reduce DEBUG noise in CI: lots of framed notification debug lines during overflow scenarios
    # We set MCP_LOG_LEVEL=INFO so logs are still visible but less verbose.
    COMMAND /bin/bash -lc "MCP_LOG_LEVEL=INFO SERVER_BIN=/src/build/examples/mcp_server/mcp_server bash ${STDIO_HARDENING_SH} overflow"
  )
  add_test(
    NAME StdioHardening.WriteTimeout
    # Reduce DEBUG noise in CI: many DEBUG messages when writes stall without a reader.
    # We set MCP_LOG_LEVEL=INFO so logs are still visible but less verbose.
    COMMAND /bin/bash -lc "MCP_LOG_LEVEL=INFO SERVER_BIN=/src/build/examples/mcp_server/mcp_server bash ${STDIO_HARDENING_SH} writetimeout"
  )
  add_test(
    NAME StdioHardening.BadContentLength
    COMMAND /bin/bash -lc "SERVER_BIN=/src/build/examples/mcp_server/mcp_server bash ${STDIO_HARDENING_SH} badlength"
  )
  # HTTP Bearer auth demo: unauthorized probe (401 + WWW-Authenticate) then authorized client (200)
  add_test(
    NAME HTTPDemo.BearerAuth
    COMMAND /bin/bash -lc "MCP_LOG_LEVEL=INFO MCP_HTTP_PORT=9443 MCP_HTTP_REQUIRE_BEARER=1 MCP_HTTP_RESOURCE_METADATA_URL=https://auth.example.com/rs MCP_HTTP_DEMO_TOKEN=demo SERVER_BIN=/src/build/examples/mcp_server/mcp_server CLIENT_BIN=/src/build/examples/mcp_client/mcp_client bash ${RUN_DEMO_SH}"
  )
  set_tests_properties(HTTPDemo.BearerAuth PROPERTIES RUN_SERIAL ON)

  # HTTP Bearer auth demo: authorized requests with insufficient scopes => 403 + WWW-Authenticate (rpc + notify)
  add_test(
    NAME HTTPDemo.BearerAuth.Forbidden
    COMMAND /bin/bash -lc "MCP_LOG_LEVEL=INFO MCP_HTTP_PORT=9444 MCP_HTTP_REQUIRE_BEARER=1 MCP_HTTP_EXPECT_FORBIDDEN=1 MCP_HTTP_RESOURCE_METADATA_URL=https://auth.example.com/rs MCP_HTTP_DEMO_TOKEN=demo MCP_HTTP_REQUIRED_SCOPES=need SERVER_BIN=/src/build/examples/mcp_server/mcp_server CLIENT_BIN=/src/build/examples/mcp_client/mcp_client bash ${RUN_DEMO_SH}"
  )
  set_tests_properties(HTTPDemo.BearerAuth.Forbidden PROPERTIES RUN_SERIAL ON)
else()
  message(STATUS "Scripts not found; skipping stdio integration/hardening script-based tests")
endif()
