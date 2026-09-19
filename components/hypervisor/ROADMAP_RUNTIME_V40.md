# Runtime V40 - Frozen EPT Installation Transaction

Status: statically accepted. No new live gate is planned.

## Problem

The EPT installation coordinator copied its callback platform but passed the
caller's processor identity to session admission after the capability callback
ran. An injected callback could mutate that caller-owned identity and redirect
or reject coverage admission after the transaction had already been admitted.
The existing platform snapshot also lacked a direct mutation regression.

## Decision

Snapshot the processor identity and installation platform before the first
callback. Use the identity snapshot for session installation preparation and
the platform snapshot for every capability, VMREAD, VMWRITE, and instruction
error operation.

Keep valid EPT capability checks, coverage marking, VMCS field order and
values, exact readback, failure reporting, public ABI, native adapter, and
hardware sequencing unchanged.

## Checkpoints

1. **V40A - Transaction snapshots**
   - preserve the existing complete-span and separation preflight;
   - snapshot identity and platform before capability evaluation;
   - retain the admitted identity through coverage marking.
2. **V40B - Mutation regression**
   - mutate the caller identity from the capability callback;
   - poison the caller platform table from the same callback;
   - prove the admitted transaction completes through frozen inputs.
3. **V40C - Static qualification**
   - strengthen the mutation-backed installation contract;
   - pass the focused coordinator tests and contracts;
   - pass a fresh complete WDK/MSVC/MASM build and CTest suite.

## Acceptance criteria

1. Invalid or overlapping fixed-size inputs preserve the caller report and
   reach no callback.
2. Callback-driven identity mutation cannot redirect session coverage.
3. Callback-driven platform mutation cannot redirect later VMCS operations.
4. Valid activation still performs exactly two initial reads, three writes,
   and three readback verifications in the accepted order.
5. Public structures and the production native adapter remain unchanged.
6. The mutation-backed contract and complete static suite pass.

## Live-test decision

No new live gate is planned. Production installation receives an independent
processor record and immutable native callback table. Runtime V40 changes only
post-entry mutation of injected caller storage; Gate 69 already validated the
unchanged EPT activation and teardown path.

## Accepted verification

- Fresh build: `build/codex-runtime-v40-20260907-015828`.
- Complete WDK/MSVC/MASM build: 457/457 targets built.
- Focused installation coordinator and mutation-backed contract: 2/2 passed.
- Complete CTest suite: 300/300 passed.
- Exact platform/report overlap preserved caller storage and reached no
  callback.
- Capability-callback mutation changed the caller identity and poisoned the
  caller platform while the admitted transaction still marked the original
  processor and completed the exact eight-operation VMCS sequence.
- The driver, both MASM objects, and all loader and probe binaries were
  produced. No artifact was signed, installed, loaded, or executed live.
