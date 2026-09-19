# Hypervisor project instructions

## Project scope

This repository contains an educational and research Type 2 Intel VT-x hypervisor for Windows 11 x86_64. Treat kernel, VMX, driver-loading, and host-security changes as high risk.

## Architecture

- Keep modules narrowly responsible, with explicit interfaces and minimal coupling.
- Prefer structures that are easy to test, debug, replace, extend, and reuse.
- Use nested modules when they clarify ownership. Do not combine unrelated responsibilities in one file, class, service, or large function.
- Define proportionate acceptance criteria and verification before implementation.
- Keep related tests cohesive without weakening coverage, assertions, or failure visibility.

## Project context

The maintained project documents are under `agent_docs/`:

- `project_progress.md`: current implementation status, active worktree, and next milestone.
- `latest_session_work.md`: latest verified work and continuation point.
- `project_diary.md`: durable decisions, discarded approaches, and lessons.
- `codex_ai_agent_build.md`: Windows build and static-verification procedure.
- `persistent_runtime_v3_plan.md`: retained implementation plan for the persistent runtime work.

Read only the documents relevant to the current task. Before continuing active implementation or building, inspect `project_progress.md` and confirm the active worktree and branch. Update durable documentation only with verified facts; keep temporary reasoning and raw logs out of it.

OpenSpec, Engram, gentle-ai, and `.atl` artifacts are historical references unless the user explicitly asks to use them. Preserve them and do not treat them as the active planning system by default.

## Build and verification

For the Windows driver build and static verification sequence, follow `agent_docs/codex_ai_agent_build.md`. Use the active worktree named there or in `project_progress.md`; do not assume the parent checkout is the implementation target.

Batch independent read-only inspections and validations when that improves speed without obscuring failures. Keep operations sequential when later steps depend on earlier results or when they share mutable state such as a build directory, device, port, Git index, or VM.

## Safety boundary

Static builds and unit tests are the default verification boundary. Do not sign, install, load, or run the driver; invoke the loader; change Hyper-V, VBS, boot, driver-signing, or security settings; execute VMXON or VMLAUNCH; mutate control registers; or perform live VM-exit validation unless the user explicitly authorizes a bounded test-VM procedure.

Prefer a disposable VM with a recoverable snapshot for live validation. Never perform privileged hypervisor testing on the user's primary Windows installation.

## Native subagents

Use the global Codex native-subagent policy. When delegation is appropriate, give each write-capable agent non-overlapping ownership and preserve all existing user and agent changes. Project safety and verification rules apply equally to every subagent.
