<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: agents.md
Purpose: Canonical MCP C++ SDK project rules and engineering requirements
==========================================================================================================
-->

# MCP C++ SDK Project Rules and Guidelines

> Canonical rules file for this repository (project-specific requirements only).
> Last updated: February 2026

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [File Headers and Licensing](#file-headers-and-licensing)
3. [Coding Style](#coding-style)
4. [Object-Oriented Design Requirements](#object-oriented-design-requirements)
5. [Low-Code / High-Config Design Pattern](#low-code--high-config-design-pattern)
6. [Docker-First Development](#docker-first-development)
7. [Async Programming Requirements](#async-programming-requirements)
8. [Security-First Design](#security-first-design)
9. [Unit and Architecture Testing Requirements](#unit-and-architecture-testing-requirements)
10. [Documentation Standards](#documentation-standards)
11. [Git Workflow](#git-workflow)
12. [Single-Method Architecture (No Fallbacks)](#single-method-architecture-no-fallbacks)
13. [Context Compaction Preflight (Strict)](#context-compaction-preflight-strict)
14. [Forbidden Actions](#forbidden-actions)
15. [Quick Reference](#quick-reference)

---

## Project Overview

The MCP C++ SDK is a C++20 implementation of the Model Context Protocol with client, server, transport, and auth
components.

### Repository Structure

```text
<repo-root>/
├── include/                    # Public SDK headers
│   └── mcp/
├── src/                        # Implementation code
│   ├── mcp/
│   └── logging/
├── tests/                      # GoogleTest suites (required)
│   └── http/
├── docs/                       # API and architecture docs
├── scripts/                    # Build/test helper scripts
├── Dockerfile
├── Dockerfile.demo
├── CMakeLists.txt
└── BUILD+TEST.MD
```

### Key Technologies

- **C++20** - primary implementation language
- **CMake** - build system
- **GoogleTest** - test framework
- **Docker** - required build/test execution environment

---

## File Headers and Licensing

All C++, header, CMake, shell, YAML, and Markdown files MUST include SPDX/license header blocks matching repository
style.

### C++/Header Example

```cpp
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: src/mcp/SomeFile.cpp
// Purpose: Brief description
```

### Markdown Example

```markdown
<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: docs/some-file.md
Purpose: Brief description
==========================================================================================================
-->
```

### Header Path Rule

The `File:` path MUST be repository-relative, not absolute.

---

## Coding Style

### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Classes/structs | PascalCase | `JsonRpcMessageRouter` |
| Public methods | PascalCase | `Connect()`, `Initialize()` |
| Local vars/params | camelCase | `sessionId`, `transportFactory` |
| Constants/macros | UPPER_SNAKE_CASE | `MAX_MESSAGE_SIZE` |
| Files | PascalCase or snake_case (existing style) | `Client.cpp`, `test_client_cache.cpp` |

### Core Rules

1. Follow existing style in neighboring files; do not mix incompatible naming styles in one file.
2. Keep includes minimal and correctly layered (`include/mcp` contract first, internal impl second).
3. Avoid hidden global state.
4. Prefer RAII and explicit ownership over raw lifecycle management.

---

## Object-Oriented Design Requirements

### Shared Code First Model (Mandatory)

Before adding new code, check existing shared abstractions and extend them first:

1. Reuse public interfaces under `include/mcp/` instead of adding parallel APIs.
2. Reuse transport/auth base abstractions before creating protocol-specific forks.
3. If a behavior is needed in 2+ modules, move it to shared/core components.

### Do Not Duplicate Core Logic

Forbidden:

1. Copy/pasting transport framing logic across implementations.
2. Re-implementing auth parsing independently in multiple files.
3. Creating one-off client/server behavior that bypasses common validation pathways.

---

## Low-Code / High-Config Design Pattern

Runtime behavior should be driven by configuration, protocol metadata, and capabilities instead of hardcoded special
cases.

### Rules

1. Capability negotiation and metadata must drive behavior where applicable.
2. Prefer declarative validation/config over branching on one-off literals.
3. Keep hardcoded environment/service assumptions out of protocol paths.

---

## Docker-First Development

All development, build, and test execution MUST run inside Docker.

### Required Workflow

1. Build with Docker (`Dockerfile` / `Dockerfile.demo`).
2. Run all tests inside Docker containers.
3. Use WSL-based Docker flow on Windows.

### Command Documentation Standard

Every operational command in docs must be provided in this order:

1. Linux/macOS (Bash)
2. Windows (PowerShell via WSL)

---

## Async Programming Requirements

The SDK is async-heavy and must remain non-blocking in critical paths.

### Rules

1. Use `std::future`/`std::async` and callback patterns consistently with existing APIs.
2. Honor cancellation semantics where exposed.
3. Avoid blocking operations on transport event paths.
4. Protect shared state with correct synchronization discipline.

### Anti-Patterns (Forbidden)

1. Blocking waits in notification handlers.
2. Detached background work without lifecycle control.
3. Cross-thread data mutation without synchronization.

---

## Security-First Design

1. Validate all inbound JSON-RPC and transport payloads.
2. Do not trust client-supplied metadata without validation.
3. Keep auth/token handling centralized and redact sensitive logs.
4. Fail closed on malformed, ambiguous, or unauthorized requests.

---

## Unit and Architecture Testing Requirements

### Unit Tests (Mandatory)

1. All behavior changes require tests.
2. Existing tests must remain green.
3. Regression tests are required for bug fixes.

### Architecture Test Pattern (Mandatory)

All code must comply with architecture test constraints:

1. Layer boundaries: public API headers, core logic, transport implementations, and auth modules remain separated.
2. Shared-first enforcement: no duplicate implementation of shared concerns.
3. Validation path enforcement: protocol handling must pass through canonical validators.

### Architecture Enforcement First Gate (Mandatory)

Before any new protocol-parity or feature-phase implementation proceeds:

1. `tests/test_architecture_enforcement.cpp` MUST exist and be compiled into the GoogleTest target.
2. Architecture enforcement tests MUST run and pass in Docker before running broader suites.
3. If architecture enforcement fails, feature work is blocked until violations are fixed.
4. Changes that remove or bypass architecture enforcement are forbidden.

### Enforcement

A change is not merge-ready unless:

1. Unit tests pass in Docker.
2. Architecture/contract tests pass.
3. No layering or duplication violations are introduced.

---

## Documentation Standards

1. Keep docs consistent with actual commands and file paths.
2. No references to local absolute paths in committed docs.
3. Keep README and BUILD+TEST guidance synchronized with CI behavior.

---

## Git Workflow

### Commit Message Format

```text
<type>: <short description>
```

Types:

- `feat`
- `fix`
- `docs`
- `test`
- `refactor`
- `chore`

### Pre-Commit Checklist

1. Build and test in Docker.
2. Add/update tests.
3. Verify docs if behavior changed.

---

## Single-Method Architecture (No Fallbacks)

Use one canonical method per concern. Do not add silent fallback paths.

### Forbidden

1. Try one parser then silently fallback to another implementation.
2. Support legacy + new schema in parallel indefinitely.
3. Quietly bypass required config with hardcoded defaults.

### Required

1. Fail loudly on missing required dependencies/config.
2. Migrate call sites to canonical pathways.
3. Remove legacy branches once canonical path exists.

---

## Context Compaction Preflight (Strict)

### CRITICAL: No Compaction Without Preflight

Before any summarization, context-window compaction, handoff note generation, or automation-generated session
compression, a strict preflight check MUST run.

Compaction without preflight is forbidden.

### Critical Context Manifest (Required)

Preflight MUST build a `criticalContextManifest` containing all non-negotiable items:

1. Hard requirements and prohibitions (`MUST`, `MUST NOT`, release blockers)
2. Security and compliance constraints
3. Open decisions, unresolved risks, and explicit assumptions
4. Canonical identifiers and references:
   - repository, branch, commit hash
   - file paths and required environment variable names
   - transport mode/runtime mode selections
5. Validation and acceptance criteria:
   - required Docker builds/tests and pass/fail gates
6. Active user-requested constraints for the current session

### Enforcement Gates (Blocking)

Compaction may proceed ONLY if all gates pass:

1. **Coverage Gate (100%)** - every manifest item exists in compacted output
2. **Integrity Gate (0 drift)** - values and constraints are unchanged
3. **Contradiction Gate** - no compacted statement conflicts with canonical rules or current user constraints
4. **Traceability Gate** - compacted output preserves pointers to source files/sections used for critical decisions
5. **Confidence Gate** - low-confidence semantic matches require human confirmation

### Failure Policy (Fail Closed)

If any gate fails:

1. Reject compaction
2. Retain full context
3. Emit a preflight failure report listing:
   - missing items
   - changed values
   - contradictions found
4. Block downstream execution/merge until preflight passes

### CI/CD and Runtime Enforcement

1. Any workflow step that compacts/summarizes context MUST run `preflight_context_check` first
2. Protected branches MUST reject changes when preflight artifacts are missing or failing
3. Bypass flags for preflight are forbidden in CI and production automation

### Minimal Preflight Artifact Contract

Each compaction run MUST persist machine-readable artifacts:

1. `preflight-manifest.json`
2. `preflight-result.json`
3. `compaction-diff-report.md`

Required fields in `preflight-result.json`:

- `coveragePercent`
- `integrityStatus`
- `contradictionCount`
- `confidenceStatus`
- `approvedBy` (automation or human reviewer identity)

---

## Forbidden Actions

1. Running tests outside Docker.
2. Bypassing architecture test constraints for convenience.
3. Duplicating shared core logic in feature modules.
4. Introducing blocking async anti-patterns.
5. Committing secrets or credentials.
6. Merging untested code.

---

## Quick Reference

```bash
# Linux/macOS (Bash)
docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test .
docker run --rm mcp-cpp-test ctest --test-dir build --output-on-failure

# Windows (PowerShell via WSL)
wsl -d Ubuntu -- bash -lc "cd /mnt/c/<path-to-repo>/mcp-cpp && docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test ."
wsl -d Ubuntu -- bash -lc "cd /mnt/c/<path-to-repo>/mcp-cpp && docker run --rm mcp-cpp-test ctest --test-dir build --output-on-failure"
```

---

## Version History

| Date | Version | Changes |
|------|---------|---------|
| 2026-02 | 1.0 | Initial AGENTS.md for mcp-cpp, adapted from key governance elements |
| 2026-02 | 1.1 | Expanded context compaction preflight policy with blocking gates and fail-closed enforcement |
| 2026-02 | 1.2 | Added explicit architecture-enforcement-first gate requirements for phased implementation |
