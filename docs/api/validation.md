<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: docs/api/validation.md
Purpose: Opt-in schema validation (Off/Strict) for Client/Server
==========================================================================================================
-->
# Validation (opt-in)

Header: `include/mcp/validation/Validation.h`

Validation mode provides a toggle to enable runtime shape checks of common MCP results and notifications. It is disabled by default to avoid overhead.

## Modes
- `Off` (default): No validation is performed.
- `Strict`: Validate response/notification shapes at runtime and surface failures.

## API

Client:
- `void IClient::SetValidationMode(validation::ValidationMode mode)`
- `validation::ValidationMode IClient::GetValidationMode() const`

Server:
- `void IServer::SetValidationMode(validation::ValidationMode mode)`
- `validation::ValidationMode IServer::GetValidationMode() const`

## Usage

```cpp
#include "mcp/validation/Validation.h"

client->SetValidationMode(mcp::validation::ValidationMode::Strict);
server.SetValidationMode(mcp::validation::ValidationMode::Strict);
```

## Behavior

When set to Strict, the SDK validates the following shapes at runtime:

- Tools list responses (`tools/list`): requires each item to have `name` and `description`, optional `inputSchema`, and optional `nextCursor` as string or integer.
- Resources list responses (`resources/list`): requires each item to have `uri` and `name`, optional `description` and `mimeType`, and optional `nextCursor`.
- Resource templates list responses (`resources/templates/list`): requires each item to have `uriTemplate` and `name`, optional `description` and `mimeType`, and optional `nextCursor`.
- Prompts list responses (`prompts/list`): requires each item to have `name` and `description`, optional `arguments`, and optional `nextCursor`.
- Server-initiated sampling (`sampling/createMessage`) request params and result:
  - Params: `messages` must be an array; when present, each message `content` must be an array of text content items (`{ type: "text", text: string }`).
  - Result: requires `model`, `role`, and `content` (array of text content items); optional `stopReason`.
- Progress notifications: require non-empty `progressToken` and `progress` in [0.0, 1.0].

On the client side, invalid list responses and typed wrapper results throw `std::runtime_error` with an error logged before the throw. For server-initiated sampling, invalid params/results are returned as JSON-RPC errors (InvalidParams/InternalError) with an error log entry.

On the server side, invalid handler results or response constructions under Strict mode return JSON-RPC errors and are logged.

## Failure examples

- `tools/list` invalid item (missing description):
  ```json
  { "tools": [ { "name": "t1" } ] }
  ```
  Client Strict mode logs and throws; server Strict mode returns InternalError.

- `sampling/createMessage` invalid params (message content not array):
  ```json
  { "messages": [ { "role": "user", "content": "hi" } ] }
  ```
  Client Strict mode responds with InvalidParams to the server.

- `sampling/createMessage` invalid result (content not array):
  ```json
  { "model": "m", "role": "assistant", "content": "oops" }
  ```
  Client/Server Strict mode responds with InternalError.
