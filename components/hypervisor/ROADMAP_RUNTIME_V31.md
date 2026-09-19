# Runtime V31 - Transactional VMCS Plan Composition

Status: statically accepted. No new live gate is planned.

## Problem

The control-field planner preflighted its complete append, but the guest
segment, guest machine-state, and host-state planners appended one field at a
time while ignoring individual insertion failures. A duplicate field or
insufficient capacity could therefore leave a caller-owned plan partially
mutated even though the planner returned an incomplete count.

The production launch path builds a fresh bounded plan, so accepted live gates
used the valid path. The defect is nevertheless unsafe at the reusable plan
boundary: failure must not publish a prefix that a future caller could mistake
for a complete VMCS configuration.

## Decision

Route every public single- and multi-field append through one bounded helper.
The helper validates the sentinel/count contract, capacity, nonzero field
encodings, duplicates against the existing plan, and duplicates within the
incoming batch before changing any caller-owned byte. It commits the complete
batch only after every check passes.

Keep the existing field order, values, capacity, and public API unchanged.
Multi-field planners return zero and preserve the complete input plan
byte-for-byte on any failure.

## Checkpoints

1. **V31A - Shared atomic append**
   - centralize capacity and duplicate preflight;
   - commit only after the full incoming batch is valid;
   - retain exact-once single-field insertion through the same helper.
2. **V31B - Complete planner adoption**
   - migrate controls, guest segments, guest machine state, and host state;
   - retain exact valid-path field order and values;
   - document byte-for-byte failure preservation.
3. **V31C - Static qualification**
   - test duplicate and one-short-capacity failures for every multi-field
     planner against a complete before-image;
   - mutation-test capacity, existing-duplicate, incoming-duplicate, and
     planner-bypass checks;
   - pass the complete WDK/MSVC/MASM build and CTest suite.

## Acceptance criteria

1. No public planner partially changes its input on failure.
2. Existing and incoming duplicate fields are rejected before mutation.
3. Capacity is proven for the complete batch before mutation.
4. Valid plans retain the exact existing field order, values, and counts.
5. Mutation-backed contracts reject removal or bypass of any preflight class.
6. The complete WDK/MSVC/MASM build and CTest suite pass.

## Live-test decision

No new live gate is required. Runtime V31 does not alter valid VMCS bytes,
instruction sequencing, hardware controls, public ABI, or runtime lifecycle.
Gate 69 already exercised the unchanged valid plan across all four processors.
The new behavior exists only on rejected duplicate/capacity inputs and is
fully observable through byte-for-byte unit and mutation tests.

## Accepted verification

- Accepted implementation commit:
  `b5b13962585d13b888200be112809dba531670ea`.
- Fresh build directory:
  `build/codex-runtime-v31-20260906-202939`.
- Complete WDK/MSVC/MASM build: `457/457` targets built.
- Focused VMCS-plan, Runtime V30 attestation, and Runtime V31 mutation tests:
  `3/3` passed.
- Complete CTest suite: `292/292` passed, with zero failures.
- `test_vmcs_plan_transaction_contracts` proved complete preflight adoption and
  rejected capacity, zero-field, duplicate, and planner-bypass mutations.
- `test_vmcs_plan` proved byte-for-byte preservation for duplicate and
  one-short-capacity failures across every multi-field planner.

Gate 69 is retained only as evidence that the unchanged valid composition
path operated across all four VM processors. It is not Runtime V31 acceptance
evidence, and no live Runtime V31 gate was added.
