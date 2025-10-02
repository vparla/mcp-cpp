# Stage that already contains compiled tree (copied from prebuilt image tag)
ARG BUILD_IMAGE=mcp-cpp-build
FROM ${BUILD_IMAGE} AS build

# Stage to generate self-signed certs
FROM ubuntu:24.04 AS certs
RUN apt-get update \
 && apt-get install -y --no-install-recommends openssl ca-certificates \
 && rm -rf /var/lib/apt/lists/*
RUN mkdir -p /out/certs \
 && openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
      -keyout /out/certs/key.pem -out /out/certs/cert.pem \
      -subj "/CN=http-server" \
      -addext "subjectAltName = DNS:http-mcp-server"

# Runtime stage with both server and client test binaries from the prebuilt image
FROM ubuntu:24.04 AS http
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates openssl \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/tests/http/http_server_bin /usr/local/bin/http_server_bin
COPY --from=build /src/build/tests/http/http_client_tests /usr/local/bin/http_client_tests
COPY --from=certs /out/certs /certs

ENV MCP_HTTP_HOST=http-server
ENV MCP_HTTP_PORT=9443
ENV MCP_HTTP_RPC=/mcp/rpc
ENV MCP_HTTP_CA=/certs/cert.pem
ENV MCP_HTTP_SNI=http-server

EXPOSE 9443
