# Runtime V26 - Authenticated Diagnostic State Admission

Status: complete. Gate 65 is accepted.

## Problem

The persistent runtime uses seven authenticated diagnostic state pairs. Their
numeric layout is deliberate: supported armed states are odd values 1 through
13, and their matching complete states are the immediately following even
values 2 through 14. C composition, C activation, C dispatcher completion,
and two MASM return gates nevertheless repeated independent lists of those
values.

Gate 64 exposed the cost of that duplication. HLT completion and armed states
were each omitted from a different MASM list, causing fail-closed VMX teardown
at the wrong boundary. Adding another diagnostic would repeat the same
cross-language omission risk.

## Decision

Make the bounded odd/even state layout an explicit private ABI contract. C and
MASM admit only states inside the supported range with the correct parity. C
also verifies the exact state complement and requires each operation's
complete state to equal its armed state plus one.

Operation-specific guest routing and payload validation remain explicit. The
range contract does not make an undefined operation executable, and extending
the range still requires an intentional ABI, dispatcher, activation, and test
change.

## Checkpoints

1. **V26A - Shared state classification**
   - define first/last armed and complete states plus the parity mask;
   - add C helpers for class, complement, and adjacent-pair validation;
   - mirror the numeric bounds in the MASM include.
2. **V26B - Admission convergence**
   - replace C composition state lists with the shared helpers;
   - reject invalid activation pairs before touching resident state;
   - replace the C dispatcher completion list with the shared helpers;
   - replace both MASM state lists with bounded parity gates.
3. **V26C - Mutation-backed verification**
   - exhaustively test every supported state and pair;
   - reject zero, out-of-range, wrong-parity, wrong-complement, and
     non-adjacent values;
   - reject weakened C or MASM admission mutations.
4. **V26D - Gate 65**
   - run all seven existing diagnostic operations once in one loaded resident
     runtime;
   - prove their typed completion markers and authoritative status;
   - unload canonically and preserve negative crash/event evidence.

## Acceptance criteria

1. Every supported armed state is admitted by the same bounded odd-state
   contract in C and MASM.
2. Every supported complete state is admitted by the same bounded even-state
   contract in C and MASM.
3. Zero, values above 14, wrong parity, wrong complements, and mismatched pairs
   fail closed.
4. Operation-specific dispatch and payload validation remain explicit.
5. A fresh WDK/MSVC/MASM build and the complete CTest suite pass.
6. Gate 65 proves all seven existing state pairs in one composite lifecycle.

## Live-test boundary

Gate 65 is necessary because this change replaces two assembly admission
boundaries used by every resident diagnostic resume and return. Static tests
prove the encoding, but not that every existing operation still crosses both
live VMX boundaries. One composite lifecycle is proportionate. No 10/100-cycle
repetition gate is planned unless evidence identifies an intermittent defect.

## Accepted live evidence

Gate 65 passed for source commit
`3ce937d773c5e0c52df6a461fd6f3305061b295a` under RunId
`gate65-diagnostic-admission-matrix-rerun-20260906-131352-a9c609baa5`.
The immutable package SHA-256 is
`21DEED9A4A87927D21B354659B5D74CBD7FE77AC718A8EFC83CB368E8046A248`.
The evidence archive SHA-256 is
`6FF8B5CF25CEB477843F5ADCB47731385AFED968D2F2B6213D225EB9E7AE3CE4`,
and the closed KD log SHA-256 is
`A162EC10AC41B356689C7260F495F195E6590D91067E3D771BFEBCF4EC6536F7`.

One resident LOAD crossed all seven armed/complete pairs exactly once. KD
recorded armed states 1, 3, 5, 7, 9, 11, and 13 and matching complete states
2, 4, 6, 8, 10, 12, and 14. Authoritative status remained LOADED / EPT READY,
the EPT roundtrip correctly left CR3 observation as OBSERVED, unload removed
the service, stderr was empty, no failure event or dump was produced, and the
broker restored the clean snapshot.

The first package completed the runtime and unloaded cleanly but its harness
incorrectly required CR3 observation to remain INACTIVE after the EPT
roundtrip. Commit `3ce937d` corrected that test-only expectation and added a
mutation-backed regression. No runtime binary changed for the accepted rerun.

After acceptance, the final independent completion-state list in the C
dispatcher was replaced by the same complete-state and complement helpers.
That refactor is semantics-equivalent for the seven accepted states, changes
no hardware control or public ABI, and is protected by exhaustive and
mutation-backed static contracts. The complete CTest suite passed 286/286.
Gate 65 already exercised every affected completion state live, so another
live gate is not warranted for this convergence-only cleanup.
