<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: .codex/workflows/agent-skill-gate.md
Purpose: Codex workflow bridge for enforcing mcp-cpp agent and skill guardrails
==========================================================================================================
-->

# Codex Agent And Skill Gate

Codex should treat `agents.md` and `SKILLS.MD` as the canonical repo contract.

1. Read `agents.md`.
2. Read `SKILLS.MD`.
3. Use `.vscode/tasks.json` for repeatable editor-run Docker flows when working in VS Code.
4. Use Docker-only execution, WSL on Windows, and `bash` on Linux or macOS.
5. Do not use bind mounts, named volumes, `docker cp`, `-o type=local`, or any Docker pattern that writes
   back to the host.
6. If any failure appears, even if it looks unrelated, stop and fix it before continuing.
