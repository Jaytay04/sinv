# Runtime V39 - Frozen EPT Invalidation Boundaries

Status: statically accepted. No new live gate is planned.

## Problem

The reusable INVEPT executor rejected output overlap but did not preflight all
fixed-size input pairs and validated caller-owned capabilities, request, and
descriptor objects before snapshotting them. The invalidation coordinator also
reused the caller-owned processor identity when accepting the instruction
report after an injected platform callback had run. Callback-driven mutation
could therefore redirect transaction completion even though the admitted
instruction used the original logical processor.

## Decision

Preflight every complete fixed-size INVEPT executor span, require pairwise
separation, snapshot platform, capabilities, request, and descriptor inputs,
and validate and execute only through those frozen values. In the coordinator,
retain one admitted identity and platform snapshot across request construction,
instruction execution, and report acceptance.

Keep valid INVEPT type and descriptor values, instruction order, EPT coverage
commit rules, public ABI, native adapter, and hardware sequencing unchanged.

## Checkpoints

1. **V39A - Executor admission**
   - reject wrapping or overlapping fixed-size inputs and report storage;
   - snapshot every immutable executor input before validation;
   - execute only through the frozen callback table.
2. **V39B - Coordinator transaction identity**
   - preserve existing owner/span authentication before snapshotting;
   - use one identity snapshot for build and acceptance;
   - use one platform snapshot for the checked executor.
3. **V39C - Static qualification**
   - add preservation, callback-mutation, and identity-mutation tests;
   - add mutation-backed source/header/test contracts;
   - pass focused tests and the complete WDK/MSVC/MASM build and CTest suite.

## Acceptance criteria

1. Invalid, wrapping, or overlapping executor inputs reach no callback and
   preserve caller-owned output.
2. Callback-driven caller-table mutation cannot redirect later execution.
3. Callback-driven identity mutation cannot redirect report acceptance after a
   successful instruction.
4. Valid instruction results and exact N-of-N EPT invalidation semantics are
   unchanged.
5. Public structures and the production native adapter remain unchanged.
6. Mutation-backed contracts and the complete static suite pass.

## Live-test decision

No new live gate is planned. Production EPT invalidation inputs use independent
storage and immutable platform tables. Runtime V39 changes malformed injected
inputs and post-entry caller mutation only. Gate 69 on Runtime V38 has already
revalidated the unchanged valid EPT lifecycle and native INVEPT route.

## Accepted verification

- Accepted implementation commit: `9cd9fc8`.
- Fresh build: `build/codex-runtime-v39-20260907-003845`.
- Complete WDK/MSVC/MASM build: 457/457 targets built.
- Focused EPT executor, invalidation coordinator, retained coordinator
  contract, and frozen-input contract: 4/4 passed.
- Complete CTest suite: 300/300 passed.
- Pairwise overlap, wrapping-pointer preservation, callback-table mutation,
  caller-request mutation, and callback-driven identity mutation checks
  passed.
- The driver, both MASM objects, and all loader and probe binaries were
  produced. No artifact was signed, installed, loaded, or executed live.
