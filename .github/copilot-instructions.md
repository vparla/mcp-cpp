<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: .github/copilot-instructions.md
Purpose: VS Code Copilot instructions for enforcing mcp-cpp agent and skill rules
==========================================================================================================
-->

# mcp-cpp Copilot Instructions

Start every task by reading `agents.md` and `SKILLS.MD`.

- Use the matching skill sections from `SKILLS.MD` before proposing commands or edits.
- Run architecture checks before broader validation.
- Use `.vscode/tasks.json` for repeatable editor-run Docker workflows.
- Use Docker-first execution only. Windows commands go through WSL; Linux and macOS use `bash`.
- Do not use bind mounts, named volumes, `docker cp`, or any Docker export that writes back to the host.
- Treat every failure as blocking, even if it is not directly tied to the active request. Fix it immediately before
  continuing.
