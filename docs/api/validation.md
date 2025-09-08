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
- In this initial scaffolding, the toggle is a no-op (no checks executed).
- Upcoming iterations will add lightweight validators for tools/resources/prompts responses, sampling, and progress.
- Failures will be reported as typed errors or `std::runtime_error` (for typed wrappers), aligned with the SDK error handling policy.
