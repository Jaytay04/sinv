# Runtime V25 - Authenticated HLT VM-Exit Roundtrip

Status: complete. Gate 64 is accepted on the disposable two-group/four-CPU VM.

## Problem

The persistent runtime has proven several pass-through and diagnostic paths,
but it has not yet demonstrated an instruction that is deliberately converted
from native guest execution into a VM exit by a temporary VMCS control change.
That leaves the control-transition, dispatcher-resume, and exact-restoration
boundary unqualified.

## Decision

Add one authenticated `hlt-probe` operation. On one selected persistent VMCS,
the operation reads the primary processor controls, requires HLT exiting to be
clear, arms only the HLT-exiting bit, verifies the live VMCS write, and enters
the synthetic guest. The guest executes one HLT. The typed dispatcher accepts
only exit reason 12, zero qualification, and instruction length one, advances
guest RIP by exactly one byte, and resumes the guest. The composition owner
then restores and verifies the exact original primary controls before any
result is published.

No control value, completion marker, or response is accepted without the
existing state/complement and target/complement authentication. Failed arm or
restore verification is fail-closed.

## Checkpoints

1. **V25A - Public and internal contracts**
   - add exact 16-byte request and 48-byte response ABIs;
   - add capability bit 10 and the thirtieth operational IOCTL;
   - add matching C/MASM payload and state layouts.
2. **V25B - Transactional VMCS control ownership**
   - require the original HLT-exiting bit to be clear;
   - arm and read back only that bit;
   - restore and read back the exact original controls on every completed path;
   - fail closed if exact restoration cannot be proved.
3. **V25C - Typed HLT dispatch**
   - execute one guest HLT;
   - accept only reason 12, qualification zero, and length one;
   - advance RIP exactly once and publish a contracted completion.
4. **V25D - Gate 64**
   - run one disposable-VM lifecycle on the established two-group/four-CPU
     topology;
   - prove the before/armed/after control transaction in loader and KD evidence;
   - preserve canonical unload, service absence, empty stderr, clean events,
     and no new dump.

## Acceptance criteria

1. Request, response, state, and payload layouts are exact and reserved fields
   remain zero.
2. The guest cannot reach HLT without target and state authentication.
3. The dispatcher rejects any non-HLT reason, nonzero qualification, wrong
   instruction length, or RIP-write failure.
4. `PrimaryControlsArmed == PrimaryControlsBefore | 0x80` and
   `PrimaryControlsAfter == PrimaryControlsBefore`.
5. Static mutation tests reject weakened authentication, dispatch, and
   restoration checks.
6. A fresh WDK/MSVC/MASM build and full CTest pass before live execution.
7. Gate 64 runs once through the existing disposable-VM broker and is not
   automatically retried after a real failure.

## Live-test boundary

Gate 64 is necessary because a real HLT VM exit, live VMCS control mutation,
dispatcher resume, and restoration cannot be established by static tests.
One lifecycle is proportionate; repetition-only 10/100-cycle gates are not
planned unless evidence exposes an intermittent defect.

## First live-run finding

Run `gate64-hlt-exit-roundtrip-20260906-010439-6bc9eb0b2f` reached the
post-`VmxV3dPersistentLaunch` restoration path, then bugchecked with `0x3B` /
`STATUS_ILLEGAL_INSTRUCTION`. Private-symbol and linked-image inspection maps
the fault to the primary-control VMWRITE in
`vmx_persistent_composition.c`.

The HLT completion state was accepted by the C dispatcher but was missing from
the MASM `V3dDiagnosticReturnGate`. A successful HLT completion therefore took
the fail-closed path, cleared the VMCS, executed VMXOFF, and returned to
composition; the subsequent VMWRITE correctly faulted because VMX operation
had already ended. The
correction admits the authenticated HLT-complete state through the same
resident-return path as the existing diagnostic completions. A mutation-backed
contract now rejects removal of that admission.

Fresh host-only WDK/MSVC/MASM verification after that correction built all 455
targets and passed the complete `284/284` CTest suite. The corrected live run
`gate64-hlt-exit-roundtrip-20260906-085448-e93a816374` nevertheless failed at
the same protected post-return VMWRITE before the guest HLT could execute. Its
last composition marker reported diagnostic state 13 (`HLT_ARMED`). Linked
MASM inspection then proved the symmetric defect: `V3dEptResumeGate` admitted
the existing armed diagnostic states only through `CET_VMCS_ARMED`, omitting
`HLT_ARMED`. That omission also selected fail-closed teardown and returned to
the protected C restoration boundary after VMXOFF.

The complete correction now admits both `HLT_ARMED` through the authenticated
VMRESUME gate and `HLT_COMPLETE` through the authenticated resident-return
gate. Mutation-backed contracts cover both admissions. This remains static
evidence only; Gate 64 remains pending until a fresh immutable package and
fresh RunId succeed in the disposable VM.

Run `gate64-hlt-exit-roundtrip-20260906-090644-172664f1b0` then proved the
complete live runtime boundary: KD recorded reason 12, length 1,
`primary-armed == primary-before | 0x80`, exact primary-control restoration,
completion `0x484C5431`, and the successful HLT boundary marker. The gate
harness rejected those valid loader observations only because its multiline
regexes required LF immediately after each value while the Windows process
capture retained CRLF. The harness now accepts an optional carriage return
before the line end, with mutation coverage. A fresh lifecycle rerun remains
necessary only to complete unload and negative-evidence collection that the
harness stopped before reaching.

## Accepted evidence

Run `gate64-hlt-exit-roundtrip-20260906-093922-e27dda76db` is the accepted
Runtime V25 result for source `cd62b0a2fc7efe810efdeea0bba68cad13ac6a3b`.
The immutable package SHA-256 is
`8E7CE54AA4D249BC280B732E3315E5D2AE28B786AF0CB4705DB1480AF8F15F6D`.

Loader and KD evidence agree on exit reason 12, instruction length 1,
completion `0x484C5431`, and the exact primary-control transaction
`0x9400E172 -> 0x9400E1F2 -> 0x9400E172`. Authoritative status remained
persistent LOADED / EPT READY. UNLOAD succeeded, service absence was confirmed,
stderr and failure events were empty, and no new dump was reported. The broker
then powered off the VM and restored `Gate6-HostRunner-ReducedMemory-Clean`.

Evidence archive SHA-256:
`A335CF7F7BEB4678CE8463242B0EDA40C29906A510D4721ACE5AF65206C03B83`.
Closed KD log SHA-256:
`FEBF2D1C0597A15E8F3271B917D24FC1C127A748DD0162A7CB7E25B3E6F2010D`.
One successful lifecycle qualifies this material boundary; no repetition-only
Gate 65 is planned.
