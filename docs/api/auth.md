<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: docs/api/auth.md
Purpose: Consolidated authentication docs (HTTP Bearer server; OAuth 2.1 client credentials)
==========================================================================================================
-->

# Authentication

This page consolidates server- and client-side authentication flows supported by the SDK and points to the detailed API references and demos/tests.

- Server-side: HTTP Bearer enforcement in the HTTP acceptor.
- Client-side: OAuth 2.1 Client Credentials grant to obtain a Bearer token for HTTP requests.

See also:
- Server Bearer API details: [server.md#server-side-bearer-authentication-http-acceptor](./server.md#server-side-bearer-authentication-http-acceptor)
- HTTP/HTTPS authentication (client): [transport.md#httphttps-authentication](./transport.md#httphttps-authentication)

## HTTP Bearer (server)

When enabled on the HTTP acceptor, incoming POSTs to `rpcPath` and `notifyPath` must present `Authorization: Bearer <token>`. Tokens are verified via `ITokenVerifier`, and responses use `WWW-Authenticate` on failures.

- Behavior
  - Missing/invalid/expired token → 401 Unauthorized
  - Insufficient scopes → 403 Forbidden
  - Success → 200 OK; token metadata is available to handlers via `mcp::auth::currentTokenInfo()`
  - When `resourceMetadataUrl` is configured, failures include:
    - `WWW-Authenticate: Bearer resource_metadata=<url>`

- Demo environment variables (scripts/run_demo.sh)
  - `MCP_HTTP_REQUIRE_BEARER=1` — enable Bearer enforcement in the demo server.
  - `MCP_HTTP_RESOURCE_METADATA_URL=https://auth.example.com/rs` — advertised in `WWW-Authenticate`.
  - `MCP_HTTP_DEMO_TOKEN=demo` — token accepted by the demo verifier.
  - `MCP_HTTP_DEMO_SCOPES=s1,s2` — token scopes attached by the demo verifier (default `s1,s2`).
  - `MCP_HTTP_REQUIRED_SCOPES=s1` — scopes required by the server (if set, enforces → 403 when missing).
  - `MCP_HTTP_EXPECT_FORBIDDEN=1` — switch demo probes to expect 403 (insufficient scope) instead of 200.
  - `MCP_HTTP_PORT=9443` — HTTP listen port for the demo (defaults to 9443).
  - `MCP_SHM_CHANNEL=<name>` — override the shared‑memory demo channel (normally unique per run).

### Flow (enforcement outcomes)

```
+---------+                                            +------------------+
| Client  |                                            | HTTPServer       |
+---------+                                            +------------------+
    |  POST /mcp/rpc (no Authorization)                         |
    |---------------------------------------------------------->|
    |                                                           |
    |<----------------------------------------------------------|
    | 401 Unauthorized                                          |
    | WWW-Authenticate: Bearer resource_metadata=<url>          |
    |                                                           |
    |  POST /mcp/rpc                                            |
    |(Authorization: Bearer <token with bad scope>)             |
    |---------------------------------------------------------->|
    |                                                           |
    |<----------------------------------------------------------|
    | 403 Forbidden                                             |
    | WWW-Authenticate: Bearer resource_metadata=<url>          |
    |                                                           |
    |  POST /mcp/rpc                                            |
    |(Authorization: Bearer <valid token>)                      |
    |---------------------------------------------------------->|
    |                                                           |
    |<----------------------------------------------------------|
    | 200 OK (JSON-RPC response)                                |
```

## OAuth 2.1 Client Credentials (client)

Clients can obtain a Bearer token via the OAuth 2.1 Client Credentials grant and send it as `Authorization: Bearer <token>` for HTTP requests. See [transport.md](./transport.md) for configuration and examples.

### Flow (client credentials grant)

```
+-----------+               +--------------+                 +------------------+
| Client    |               | Auth Server  |                 | Resource Server  |
+-----------+               +--------------+                 +------------------+
    | POST /oauth2/token 
    | (grant_type=client_credentials, 
    |  client_id, client_secret, scope?)
    |-------------------------->|
    |                           | 
    |                           | 200 OK {access_token, expires_in, ...}
    |                           |<---------------------------------|
    | Build HTTP request (JSON-RPC over HTTP)                      |
    | Authorization: Bearer <access_token>                         |
    |------------------------------------------------------------->|
    |                                                              | POST /mcp/rpc
    |                                                              |
    |                                                              | 200 OK / Error
    |<-------------------------------------------------------------|
```


## Demo and tests

- Demo script: [`scripts/run_demo.sh`](../../scripts/run_demo.sh)
  - Supports toggles to exercise 401, 403, and 200 paths end-to-end.
- CTests:
  - `HTTPDemo.BearerAuth` — unauthorized probe (401) then authorized success (200). Listens on port 9443.
  - `HTTPDemo.BearerAuth.Forbidden` — authorized RPC/notify expect 403 (insufficient scope). Listens on port 9444.
- CI: [`.github/workflows/bearer-auth-demo.yml`](../../.github/workflows/bearer-auth-demo.yml)
  - Builds the Docker test image and runs both CTests.

## How to run (Docker-first)

Windows (PowerShell via WSL2 Ubuntu):

```powershell
# Build test image
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test ."

# Run Bearer demo (401 -> 200) on port 9443
wsl -d Ubuntu -- bash -lc 'docker run --rm --ipc=host mcp-cpp-test ctest --test-dir build -VV --progress -j1 -R "^HTTPDemo\\.BearerAuth$"'

# Run Forbidden demo (403 insufficient scope) on port 9444
wsl -d Ubuntu -- bash -lc 'docker run --rm --ipc=host mcp-cpp-test ctest --test-dir build -VV --progress -j1 -R "^HTTPDemo\\.BearerAuth\\.Forbidden$"'
```

For more details and variants, see [BUILD+TEST.MD](../../BUILD+TEST.MD).
