# Runtime V36 - Frozen Transient Callback Table

Status: statically accepted. No new live gate is planned.

## Problem

The retained transient launch-probe validated the caller-owned callback table
and then repeatedly read function pointers from that external object across
the complete VMX interval. A callback or concurrent owner mutation could
therefore change a later operation after validation and redirect execution to
an unvalidated function.

## Decision

After input-span preflight, copy the complete callback table once into local
storage. Validate that snapshot, then perform every privileged, planning,
launch, and teardown call through the same local copy. Never reread a callback
from the caller-owned table after the snapshot.

Keep callback signatures, valid callbacks, VMCS data, reports, public ABI, and
hardware sequencing unchanged.

## Checkpoints

1. **V36A - Local callback snapshot**
   - copy the complete platform before callback validation;
   - validate only the local snapshot;
   - route every later callback through the snapshot.
2. **V36B - Mutation resistance**
   - mutate later entries in the caller-owned table from the first callback;
   - prove no poisoned callback runs;
   - prove valid execution and teardown retain their accepted order.
3. **V36C - Static qualification**
   - mutation-test removal, reordering, or bypass of the snapshot;
   - pass focused tests and the complete WDK/MSVC/MASM build and CTest suite.

## Acceptance criteria

1. No callback is read from caller-owned platform storage after snapshot.
2. The local snapshot is complete and validated before its first callback.
3. Caller-table mutation during execution cannot redirect later operations.
4. Valid hardware operation order and output remain unchanged.
5. Mutation-backed contracts and the complete static suite pass.

## Live-test decision

No new live gate is required. Runtime V36 changes only mutation of an injected
callback table after entry. The production callback table is immutable and
valid callbacks execute in the same order with identical arguments. Unit
mutation injection and source-order contracts directly exercise the new
boundary.

## Accepted verification

- Accepted implementation commit:
  `d0f3bcf8bfbdddd618db1e03dc5b4adc67b8754f`.
- Fresh build directory:
  `build/codex-runtime-v36-20260906-225846`.
- Complete WDK/MSVC/MASM build: `457/457` targets built.
- Focused transient, V34, V35, and V36 tests: `4/4` passed.
- Complete CTest rerun: `297/297` passed, with zero failures.
- The first callback mutated later entries in caller-owned platform storage;
  execution used only the validated local snapshot and no poisoned callback.
- Mutation-backed contracts rejected missing, reordered, partially bypassed,
  or externally re-read callback snapshots.

No live artifact was produced because the production callback table is
immutable and valid hardware sequencing is unchanged.
