<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: CLAUDE.md
Purpose: Claude Code entrypoint for mcp-cpp repo rules and skills
==========================================================================================================
-->

# Claude Code Instructions

Load `@agents.md` and `@SKILLS.MD` before planning, editing, or running commands.

- Treat `agents.md` as the canonical policy file and `SKILLS.MD` as the execution workflow.
- Use Docker-first execution only. Windows uses `wsl -d Ubuntu -- bash -lc "..."`; Linux and macOS use `bash`.
- No Docker host writes: no bind mounts, named volumes, `docker cp`, or `-o type=local`.
- Use sequential verification for auth, remote, repo, commit, and push workflows. Do not parallelize state-changing
  commands with verification commands.
- Any failure is blocking, even if it looks unrelated to the active task. Fix it immediately, rerun the failed gate,
  and only then continue.
- If the only path forward needs host writes, host IPC, or host network access, stop and ask the human first.
