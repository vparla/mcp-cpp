---
description: >-
  Open agents.md and SKILLS.MD first, then use the repo's docker-first
  fail-stop workflow before any other work.
---

<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: .windsurf/workflows/agent-skill-gate.md
Purpose: Windsurf workflow for enforcing mcp-cpp agent and skill guardrails
==========================================================================================================
-->

# Agent And Skill Gate

1. Open `agents.md`.
2. Open `SKILLS.MD`.
3. Select the matching skills.
4. Use only the Docker command templates from `SKILLS.MD`.
5. Use sequential verification for auth, remotes, repo existence, branch tracking, commits, and pushes.
6. If any failure, denial, failing test, or unrelated regression appears, stop and fix it before doing anything else.
7. If the only path needs host writes, host IPC, or host network access, escalate to the human first.
