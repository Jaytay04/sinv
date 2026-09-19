# Runtime V37 - Frozen Launch-Probe Lifecycle Operations

Status: statically accepted. No new live gate is planned.

## Problem

The launch-probe lifecycle zeroed its response before validating the injected
operations table and then reread callbacks and dependency pointers from that
caller-owned table across create, execute, destroy, and map. Output overlap or
table mutation could therefore corrupt or redirect an already admitted
lifecycle.

## Decision

Preflight complete non-wrapping operation and response spans and reject their
exact or partial overlap before response initialization. Copy a present
operations table once, validate the local snapshot, and perform the complete
lifecycle through that frozen copy. Preserve the established zero-response
behavior for null or invalid separate operation tables.

Keep valid callbacks, cleanup order, response mapping, public ABI, and hardware
sequencing unchanged.

## Checkpoints

1. **V37A - Lifecycle input preflight**
   - reject wrapping operation and response spans;
   - reject exact and partial output overlap before mutation;
   - preserve null/invalid separate-table response semantics.
2. **V37B - Frozen lifecycle operations**
   - snapshot and validate the complete operations table;
   - route create, execute, destroy, and map through the snapshot;
   - prove first-callback mutation cannot redirect later operations.
3. **V37C - Static qualification**
   - mutation-test span arithmetic, separation, snapshot, and routing;
   - pass focused tests and the complete WDK/MSVC/MASM build and CTest suite.

## Acceptance criteria

1. Overlapping or wrapping inputs reach no callback or mutation.
2. Rejected overlapping bytes remain unchanged.
3. Caller-table mutation cannot redirect an admitted lifecycle.
4. Valid mapping and cleanup order remain unchanged.
5. Mutation-backed contracts and the complete static suite pass.

## Live-test decision

No new live gate is required. Runtime V37 changes only malformed injected
lifecycle inputs and post-entry mutation of an internal operations table. The
production table and response storage are independent and immutable, so valid
hardware sequencing is unchanged. Unit preservation and callback-poison tests
directly exercise the new boundary.

## Accepted verification

- Accepted implementation commit: `a726bd8`
- Fresh build: `build/codex-runtime-v37-20260906-230718`
- Build targets: 457/457 passed
- Focused tests: 3/3 passed
- Complete CTest suite: 298/298 passed
- Exact and partial overlap preservation, wrapping-span rejection, frozen
  callback routing, lifecycle cleanup order, and mutation-backed contract
  checks passed.
- No signed or live artifact was produced or executed.
