<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: .windsurf/skills/repo-guardrails/SKILL.md
Purpose: Windsurf skill for enforcing mcp-cpp repo guardrails
==========================================================================================================
-->

---
name: repo-guardrails
description: >-
  Apply mcp-cpp agent rules and repo skills to every task. Use Docker-first
  execution, WSL on Windows, bash on macOS/Linux, block all host-write Docker
  patterns, and stop immediately on any failure.
---

# Repo Guardrails

Read `agents.md` first, then `SKILLS.MD`.

- Use the matching skill sections in `SKILLS.MD` before running commands or proposing edits.
- Keep all build, test, and debug work inside Docker images built from repo context.
- Do not mount the repo into containers and do not write artifacts back to the host.
- If any failure appears, fix it immediately before continuing, even when it looks tangential.
- Use `bash` on Linux and macOS, and `wsl -d Ubuntu -- bash -lc "..."` on Windows.
