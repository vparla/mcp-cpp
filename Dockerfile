# syntax=docker/dockerfile:1.7

# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla

# Build stage: configure and build the SDK with CMake (C++20)
FROM --platform=$BUILDPLATFORM ubuntu:24.04 AS build
ARG TARGETPLATFORM
ARG TARGETARCH

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      ninja-build \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Configure and build
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j

# Artifacts stage: export headers, static lib, and example binary
FROM scratch AS artifacts
COPY --from=build /src/include/ /include/
COPY --from=build /src/build/libmcp_cpp.a /lib/libmcp_cpp.a
COPY --from=build /src/build/examples/basic/mcp_basic /bin/mcp_basic

# Test stage: run the example binary to validate the build
FROM ubuntu:24.04 AS test
COPY --from=build /src/build/examples/basic/mcp_basic /mcp_basic
RUN /mcp_basic
