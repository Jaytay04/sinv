# Runtime V29 - Current-VMCS Binding Attestation

Status: accepted. Gate 68 passed once; no repetition gate is required.

## Problem

Persistent composition tracks the current VMCS physical address in the
per-processor BasicOps context, but its generic VMREAD, VMWRITE, CPUID,
launch, resume, and teardown paths previously trusted that software record.
The EPT and VPID adapters already compare VMPTRST against their tracked VMCS.
Without the same check at the shared composition boundary, a stale or
cross-bound context could direct VMX access or VMRESUME at the wrong current
VMCS.

## Decision

Treat hardware's current-VMCS pointer as an authenticated binding. Whenever a
valid current VMCS is required or presented to persistent composition, execute
VMPTRST and require the observed address to equal the aligned, nonzero physical
address in the exact 32-byte per-processor BasicOps context. Reject mismatches
before any dependent VMREAD, VMWRITE, CPUID callback, initial VMLAUNCH, external
VMRESUME, or teardown work.

The shared helper emits only a typed `VMCS_BINDING_MISMATCH` marker on failure;
it never exposes a physical address. Successful external VM-entry boundaries
emit typed phase/state markers after all readiness checks and before entry.
Internal callback checks remain silent so evidence counts describe transactions,
not implementation call counts.

## Checkpoints

1. **V29A - Hardware current-VMCS predicate**
   - initialize the VMPTRST output defensively;
   - compare the observed pointer with the tracked per-processor VMCS;
   - reject mismatch without logging either address.
2. **V29B - Shared composition enforcement**
   - apply the predicate to every valid-current context accepted by composition;
   - preserve the no-current bind state and existing structural checks;
   - keep exactly one native VMPTRST site in the composition module.
3. **V29C - External boundary evidence**
   - record one attested launch marker per processor;
   - record one attested resume marker per diagnostic or unload transaction;
   - place markers after readiness and before VM entry.
4. **V29D - Static qualification**
   - correct the BasicOps size contract from 24 to 32 bytes;
   - mutation-test removal, bypass, comparison weakening, and marker drift;
   - pass the full WDK/MSVC/MASM build and CTest suite.
5. **V29E - Gate 68**
   - run all seven authenticated diagnostics under one resident LOAD;
   - require four launch, seven diagnostic-resume, and four unload-resume
     attestations in the closed KD log;
   - reject any mismatch/fatal marker and prove canonical unload.

## Acceptance criteria

1. A valid-current composition context is accepted only when VMPTRST equals
   its aligned, nonzero tracked VMCS physical address.
2. The no-current pre-bind state remains valid only when no current VMCS is
   required and both current fields encode absence.
3. The shared composition module contains exactly one VMPTRST site, and all
   VMX-dependent composition paths pass through it.
4. Successful external boundaries emit exactly four launch/state-0 markers,
   one resume marker for each diagnostic state 1, 3, 5, 7, 9, 11, and 13, and
   four resume/state-0 unload markers: fifteen attestations total.
5. Pure, mutation-backed contract, WDK/MSVC/MASM, and complete CTest
   verification pass.
6. Gate 68 completes all seven operations, unloads canonically, and produces
   no mismatch, crash, dump, failure event, stderr, fatal marker, or service
   residue.

## Live-test boundary

Gate 68 is necessary because static tests cannot establish that VMPTRST
matches the tracked VMCS at real nested-VMX launch, diagnostic resume, and
per-processor unload boundaries. One composite lifecycle exercises all fifteen
external entries and all existing diagnostic routes. The binding is a
deterministic per-entry invariant, so 10/100-cycle repetition would add cost
without qualifying a distinct boundary unless the first evidence reveals an
intermittent defect.

## Accepted evidence

Implementation commit `67b30aa560e1aee4c1d8c4c7a2b703f112e0a253`
passed the complete WDK/MSVC/MASM build and all `291/291` tests. Signed package
SHA-256:
`4D855ACA802CD83C3902E58E23B71F072F6FEB22D16AAF134251A42D999CE96D`.

Gate 68 RunId
`gate68-vmcs-binding-attestation-20260906-185511-cb77ea61f3` passed one
complete resident lifecycle. The closed KD log recorded four exact launch
attestations, seven exact diagnostic-resume attestations for states 1, 3, 5,
7, 9, 11, and 13, and four exact state-0 unload-resume attestations. It
contained no binding mismatch, fatal-system-error, bugcheck, fail-closed, or
watchdog-expiry marker. The guest completed all seven diagnostics, produced
empty stderr, unloaded canonically, confirmed service absence, and produced no
failure event or new dump.

Evidence archive SHA-256:
`8BCEDBB8F6B57BD7E295F94C68D395CB4FBD615C792D7F0F0B780493D9270AF1`.
Closed KD log SHA-256:
`7EA09455E07F373840B261FF2160BA137CBA532C0C9CF88F4A0F6A303905120E`.
The broker powered off and restored `Gate6-HostRunner-ReducedMemory-Clean`,
and both shared Hypervisor VM leases were released.
