# Runtime V34 - Exact Transient VMCS Planner Batches

Status: statically accepted. No new live gate is planned.

## Problem

The retained Runtime V2 launch-probe path verified the control planner's
return count but treated its three void planner callbacks as trusted. A
missing, incomplete, or over-complete guest-segment, guest-machine, or host
planner could therefore pass a structurally valid partial plan to VMWRITE.
Hardware would fail closed eventually, but only after avoidable partial VMCS
mutation.

## Decision

Attest the plan count after every injected planner. Require exactly 36 guest
segment fields, 16 guest machine-state fields, and 22 host-state fields before
the first VMWRITE. Stop at the first count mismatch, retain the last completed
lifecycle stage, and perform the existing VMX teardown without calling later
planners, VMWRITE, or launch.

Keep callback signatures, valid field order, VMCS values, public ABI, and
hardware sequencing unchanged.

## Checkpoints

1. **V34A - Per-stage count attestation**
   - derive each expected count from the previously accepted count;
   - require exact 36, 16, and 22-field deltas in order;
   - publish guest/host planning stages only after complete batches.
2. **V34B - Fail-closed injected planners**
   - stop later planners after the first mismatch;
   - prove zero VMWRITE and launch calls;
   - retain full teardown after VMXON.
3. **V34C - Static qualification**
   - use full real planner batches in the valid-path fixture;
   - mutation-test each count gate and its ordering;
   - pass the complete WDK/MSVC/MASM build and CTest suite.

## Acceptance criteria

1. No incomplete or over-complete injected planner reaches VMWRITE.
2. Later planners and launch are not called after the first mismatch.
3. VMX teardown remains complete after every rejected planner.
4. Valid plans retain exact field order, values, and hardware operation order.
5. Mutation-backed contracts and the complete static suite pass.

## Live-test decision

No new live gate is required. Runtime V34 changes only malformed injected
planner behavior in the retained transient diagnostic path. The production
callbacks already emit the exact required batches, so valid VMCS bytes and
hardware sequencing are unchanged. Unit callback counts and mutation-backed
ordering contracts directly exercise the new boundary.

## Accepted verification

- Accepted implementation commit:
  `3a0b7005926e84f5181a826a7f6da583a24db22f`.
- Fresh build directory:
  `build/codex-runtime-v34-20260906-205729`.
- Complete WDK/MSVC/MASM build: `457/457` targets built.
- Focused transient, VMCS-plan, V32, and V34 tests: `4/4` passed.
- Complete CTest suite: `295/295` passed, with zero failures.
- Unit tests rejected incomplete segment, machine, and host planner batches
  before any VMWRITE or launch while retaining VMX teardown.
- Mutation-backed contracts rejected altered batch counts and removal of the
  per-stage count guards.

No live artifact was produced because valid VMCS data and hardware sequencing
are unchanged.
