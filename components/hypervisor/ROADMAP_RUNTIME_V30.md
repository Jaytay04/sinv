# Runtime V30 - Non-Migratable VMCS Execution Context

Status: accepted at Gate 69; no repetition gate is planned.

## Problem

Runtime V29 proves that VMPTRST equals the tracked per-processor VMCS, but the
shared predicate did not locally prove that the caller had entered the
non-migratable instruction level. The surrounding activation layer raises to
DISPATCH_LEVEL, yet relying only on that distant sequencing leaves the
attestation-to-use interval without a local execution-context invariant.

If composition were ever called below DISPATCH_LEVEL, a thread could migrate
after VMPTRST and before the dependent VMREAD, VMWRITE, VMLAUNCH, or VMRESUME.
An exact pointer check at the old processor would then say nothing about the
new processor's current VMCS.

## Decision

Require exact `DISPATCH_LEVEL` in the single shared current-VMCS predicate
before executing VMPTRST. Reject lower or higher IRQL with a distinct,
redacted `VMCS_EXECUTION_CONTEXT_MISMATCH` marker. Only after that check may
composition compare hardware's current VMCS and reach a dependent operation.

The existing external `VMCS_BINDING_ATTESTED` markers remain unchanged. On the
Runtime V30 source commit, each marker proves both the local DISPATCH_LEVEL
check and exact VMPTRST equality because every valid-current composition path
must pass the shared predicate first.

## Checkpoints

1. **V30A - Local execution-level invariant**
   - require `KeGetCurrentIrql() == DISPATCH_LEVEL` before VMPTRST;
   - reject all other execution levels before any VMX intrinsic;
   - emit no address or processor identity on failure.
2. **V30B - Attestation-to-use ordering**
   - retain one shared current-VMCS predicate;
   - prove execution level, VMPTRST, equality, and dependent access order;
   - mutation-test removal, inversion, weakening, and reordering.
3. **V30C - Gate 69**
   - run all seven diagnostics under one resident LOAD;
   - prove the same fifteen external binding attestations on the hardened
     source commit;
   - reject either execution-context or VMCS-binding mismatch and prove
     canonical unload.

## Acceptance criteria

1. Every valid-current composition context is rejected unless the current
   IRQL is exactly DISPATCH_LEVEL.
2. The IRQL check occurs before the sole composition VMPTRST site and before
   every dependent VMX operation.
3. Neither failure marker exposes a VMCS physical address or processor
   identity.
4. Mutation-backed contracts reject removal, inversion, weakening, and
   post-VMPTRST placement of the execution-level check.
5. The complete WDK/MSVC/MASM build and CTest suite pass.
6. Gate 69 completes all seven operations, records fifteen binding
   attestations, unloads canonically, and produces no mismatch, crash, dump,
   failure event, stderr, fatal marker, or service residue.

## Live-test boundary

Gate 69 is necessary because the new local predicate could reject a legitimate
launch, diagnostic-resume, or per-processor unload path if any one arrives at
the wrong IRQL. Static ordering proves fail-closed structure but not the live
kernel execution level. One composite lifecycle covers all fifteen external
boundaries. The invariant is deterministic, so 10/100-cycle repetition would
not qualify a distinct behavior unless the first evidence shows intermittency.

## Accepted evidence

- Source commit:
  `f98291d51a4379d0d5777194478e06052b1d4f59`.
- Complete WDK/MSVC/MASM build and CTest: `292/292` passed.
- Signed package SHA-256:
  `661710459C6C2126A76F1F96F1F9A439ED7BE1AE7EC539D0897FAB2159606B3C`.
- Accepted RunId:
  `gate69-vmcs-execution-context-20260906-201148-fad2837eb7`.
- Guest evidence archive SHA-256:
  `9D12B5B04D4ACAE4B9E9181EC6058D8F2ADA810884A902A9D5803F580D995CD7`.
- Closed KD log SHA-256:
  `C064F4AA3D234A17679C8EBD854B593CE6FBA4A646FADB40D7891B1F525DFF13`.

The closed KD log contains exactly four launch attestations and eleven resume
attestations. Resume state zero appears four times and states 1, 3, 5, 7, 9,
11, and 13 each appear once. No execution-context mismatch, VMCS-binding
mismatch, or fatal marker appears. The guest completed all seven diagnostics,
emitted no stderr, removed the service, and produced no failure event or new
dump. The broker restored `Gate6-HostRunner-ReducedMemory-Clean` and powered
off the VM.
