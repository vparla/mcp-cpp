<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: SKILLS.MD
Purpose: Repo-local skill map for agentic tools working in mcp-cpp
==========================================================================================================
-->

# MCP C++ SDK Skills

`agents.md` is the canonical policy file. This file turns those rules into repeatable execution skills for
Codex, Windsurf, Cursor, VS Code, and Claude Code.

## Load Order

1. Read `agents.md`.
2. Read `SKILLS.MD`.
3. Select every matching skill before acting.
4. If any command, test, build, access check, lint, doc check, or unrelated validation fails, stop and fix it
   immediately before continuing.

## Skill: repo-governance

Use for every task.

- Reuse `include/mcp/` interfaces and shared transport/auth abstractions before adding parallel code paths.
- Run architecture checks first. If architecture enforcement fails, all other work is blocked.
- Keep SPDX headers, repository-relative `File:` paths, tests, and docs aligned with the change.
- Never run builds, tests, or debug commands on the host.

## Skill: docker-first-no-host-write

Use for build, test, debug, inspection, or automation work that executes code.

- Linux and macOS commands use `bash`.
- Windows commands use `wsl -d Ubuntu -- bash -lc "..."`
- Build from repo context, then run from image-embedded source. Do not mount the repo into containers.
- Do not use bind mounts, named volumes, `docker cp`, `-o type=local`, or any Docker pattern that writes back to
  the host.
- Prefer `--network none`. If network or IPC is required, stop and get explicit human approval first.

```bash
# Linux/macOS
docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test .
docker run --rm --network none --read-only \
  --tmpfs /tmp:rw,noexec,nosuid,size=1g \
  --entrypoint bash mcp-cpp-test \
  -lc "cp -a /src/build /tmp/build && ctest --test-dir /tmp/build -R '^Architecture' -VV --output-on-failure"
docker run --rm --network none --read-only \
  --tmpfs /tmp:rw,noexec,nosuid,size=1g \
  --entrypoint bash mcp-cpp-test \
  -lc "cp -a /src/build /tmp/build && ctest --test-dir /tmp/build -VV --output-on-failure"

# Windows (PowerShell via WSL)
wsl -d Ubuntu -- bash -lc "cd /mnt/<drive>/<path-to-repo>/mcp-cpp && \
docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test ."
wsl -d Ubuntu -- bash -lc "docker run --rm --network none --read-only \
  --tmpfs /tmp:rw,noexec,nosuid,size=1g \
  --entrypoint bash mcp-cpp-test \
  -lc 'cp -a /src/build /tmp/build && ctest --test-dir /tmp/build -R \"^Architecture\" -VV --output-on-failure'"
wsl -d Ubuntu -- bash -lc "docker run --rm --network none --read-only \
  --tmpfs /tmp:rw,noexec,nosuid,size=1g \
  --entrypoint bash mcp-cpp-test \
  -lc 'cp -a /src/build /tmp/build && ctest --test-dir /tmp/build -VV --output-on-failure'"
```

Emit results to stdout or stderr only. Do not export artifacts back to the host.

## Skill: failure-first-remediation

Use whenever anything fails, including failures that look unrelated to the active task.

- Treat access-denied errors, safe-directory errors, failing unrelated tests, header drift, doc drift, lint issues,
  and Docker misconfiguration as blocking failures.
- Fix the failure immediately, rerun the failed gate, then rerun the next broader gate that depends on it.
- Do not scope a failure away as "out of band" or "not part of this task."

## Skill: review-and-delivery

Use when reviewing or finalizing work.

- Findings come first in reviews, with file and line references.
- For implementation work, verify the architecture gate before broader test suites.
- If repository rules and tool rules conflict, obey the stricter rule and update the enforcement docs in the same
  change.
