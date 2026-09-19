# Runtime V6 Roadmap - Authenticated Dispatch Diagnostics

## Baseline and objective

- Reviewed Runtime V5 source commit:
  `88e957674b9323ea4aa32769265ebcef5fb42a57`
- Development branch: `feature/hypervisor-core`
- Objective: expose a stable, redacted, per-processor view of the most recent
  persistent VM-exit dispatch without exposing guest addresses, CR3 values,
  kernel pointers, or raw exit-reason metadata.

Runtime V6 treats last-dispatch state as an operator diagnostic, not as proof
of external live-gate acceptance. The query is serialized through the existing
driver owner and runtime operation boundary, authenticates processor identity
and owner generation, correlates the record with stable even dispatch and
telemetry sequences, and returns only fixed-width values needed to classify
dispatch health.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V6A Pure redaction model | Complete | Fixed 112-byte pointer-free ABI, basic-reason masking, invariant validation, and mutation-resistant unit coverage. |
| V6B Runtime query path | Complete | Owner-serialized, sequence-stable per-index capture, exact topology/generation validation, IOCTL `0x815`, timeout-safe loader command, and no raw address fields. |
| V6C Static authorization | Complete - GO | Fresh 385-target WDK/MSVC/MASM build, 6/6 focused contracts/disassembly checks, and 247/247 full CTest passed. |
| V6D Live baseline | Complete - GO | Gate 43 proved exact initial VMCALL dispatch diagnostics on all four processors and canonical unload. |
| V6E Dynamic coherence | Complete - GO | Gate 44 proved that one authenticated CPUID roundtrip advances dispatch and telemetry publication together only on the target processor. |

## Design contract

- The public response is fixed-width, versioned, and pointer-free.
- `LastExitReason` is reduced to its architectural 16-bit basic reason.
- Guest RIP, exit qualification, guest CR3, diagnostic targets, kernel
  pointers, and host addresses never cross the ABI.
- The driver owner and runtime operation locks serialize capture against every
  operator-triggered diagnostic resume.
- Processor count, system index, group, number, and owner generation must
  match the authenticated frozen topology.
- Dispatch publication brackets the complete mutable diagnostic record with a
  nonzero even sequence. Query retries unless both sequence reads match.
- Dispatcher-owned action, protocol, validation, and VMCS outcome fields are
  latched before the even publication. Later MASM lifecycle normalization
  cannot rewrite the operator-visible last-dispatch record.
- The corresponding telemetry sequence must also be nonzero and even.
- Error-valid fields are Boolean; an invalid error field must contain zero.
- A nonzero fail-closed code requires the fail-closed assembly action, and the
  fail-closed action requires a nonzero code.
- Runtime status continues to describe current state only. It cannot claim
  that Gate 43 passed.

## Gate 43 - exact initial dispatch baseline

Gate 43 is the next necessary live boundary because host-only tests cannot
prove that the installed persistent VMCS exits publish the expected redacted
record on each resident logical processor. It performs one clean load, queries
all four dispatch records, and unloads canonically.

PASS requires exact group-aware identities and generation 1 on system indices
0 through 3. Every initial record must correlate with dispatch sequence 2 and
telemetry sequence 2 and report basic exit reason 18 (VMCALL), instruction
length 3, assembly action 1,
resident protocol step 3, protocol mask `0x00000FFF`, validation flags
`0x00000003`, successful VMREAD/VMWRITE outcomes, zero error-valid/error
fields, zero fail-closed code, and zero failed VMCS field. The service and
driver module must be absent after unload, with no failure event or dump.

One run is sufficient. Gate 42 already closes package/lifecycle provenance and
the earlier 100-cycle gates qualify lifecycle stability. Gate 43 introduces
one new read-only diagnostic boundary; repeating it would not exercise a new
hardware or ownership case.

Gate 43 passed on 2026-08-29. The accepted evidence archive SHA-256 is
`CDE0D45C0A65F22623A3C81C137D23E998B24378FE926EB88C6F77EF6846EE04` and
the closed WinDbg log SHA-256 is
`7CAE0A8A3813174B2EA8ACDA6AB40C78513EA38F177A7A2F17113A0D20A0B1CA`.
The VM reported the exact four generation-1, sequence-2 VMCALL records,
canonical unload, service and module absence, and no crash, WHEA,
application-failure, fail-closed, or dump evidence.

## Gate 44 - dynamic dispatch/telemetry coherence

Gate 43 proves the initial record but cannot prove that later VM exits replace
it coherently. Gate 44 is therefore the next and final Runtime V6 live
boundary. It performs one authenticated CPUID diagnostic on system index 0,
which produces three CPUID exits and one diagnostic-completion VMCALL.

PASS requires the target telemetry and dispatch sequences to advance together
from 2 to 10. Target telemetry must advance by total 4, CPUID 3, and VMCALL 1.
The initial latched records must report READY_EXIT protocol step 2; this is the
actual dispatch-time state, not the later MASM-normalized RESIDENT step 3.
The final target dispatch must be the completion VMCALL: reason 18, length 3,
assembly action 5, protocol step 7, validation mask `0x00000FFF`, successful
VMREAD/VMWRITE, live validation flags `0x00000033` (header, ready, resume
armed, and resume valid), and no error or fail-closed state. All three non-target records
must remain byte-for-byte equivalent to their initial redacted records at
sequence 2. Canonical unload, service/module absence, and clean failure-event
and dump checks remain mandatory.

One run is sufficient. This gate tests a new cross-surface publication
invariant; it is not another lifecycle stress test. Gate 22 already qualified
the CPUID route for 100 cycles, and Gate 43 qualified the read-only baseline.

## Safety and stop conditions

- Never sign, install, load, or live-test the driver on the host machine.
- Run Gate 44 only from the clean disposable two-group/four-processor VM
  checkpoint under WinDbg.
- Stop on any identity, generation, redaction, reason, protocol, error,
  fail-closed, service, event, dump, hang, bugcheck, or unload mismatch.
- After a failed load or uncertain runtime result, do not retry LOAD and do not
  run UNLOAD manually. Preserve the VM and debugger state.

## Immediate checkpoint

Gate 43 is accepted. Host-only Gate 44 authorization completed from the final
source state in `build/codex-gate44-authorized-20260829-214733`: WDK
10.0.28000.0 was found, 385/385 targets built, required driver, loader, PDB,
and MASM objects were present, focused dispatch/status/coherence checks passed,
the corrected persistent disassembly contract passed, and full CTest passed
248/248. No host signing, installation, loading, or live VMX execution was
performed.

Commit the Gate 44 boundary, create one matched signed stage, and run Gate 44
once under WinDbg in the disposable VM. Runtime V6 closes only after that
dynamic coherence evidence is accepted.

The first Gate 44 attempt stopped safely before the CPUID probe because its
script still expected the pre-snapshot normalized protocol value 3. Live
evidence showed exact generation-1, sequence-2 records with action 1 and the
correct dispatch-time protocol value 2 on all processors. The service remained
running by design after the fail-fast stop; the rerun requires a clean VM
checkpoint rather than manual teardown.

The second Gate 44 attempt reached and completed the authenticated CPUID route.
Its telemetry and dispatch sequences advanced together from 2 to 10 only on
system index 0, with the exact total/CPUID/VMCALL deltas and completion record.
The script then stopped before unload because it expected the initial `0x03`
validation flags instead of the correct live post-resume value `0x33`. The
runtime remained healthy with no crash, WHEA, application-failure, or dump
evidence. The corrected rerun must again start from a clean checkpoint because
the fail-fast stop intentionally preserved the running service.
The evidence archive SHA-256 is
`926DF794E12F86221B04CAD7C6BF2ACA830BA01D71A1EBC84EEB6F6DD250DEF6`;
the closed WinDbg log SHA-256 is
`D878A1AF5438F5E3D673DCB18626137458C6D15A2D43708E4C9CD877493BB2FC`.

The corrected Gate 44 run passed on 2026-08-29 for source commit
`17c085eab8389dbad81dbd3d5bc17f4092385bab`. Its accepted evidence archive
SHA-256 is
`32DD19D2C337C10DE0AE6E72917E776A611D6BB7CC72FF0381CC7D2097AA3EBA`;
the closed WinDbg log SHA-256 is
`851E565D9C16AE04066A5023B6DE7EF33D060E81CE6E6670F8E8F18B23FA0CBB`.
The exact target telemetry and dispatch sequences advanced from 2 to 10 with
total/CPUID/VMCALL deltas 4/3/1, while all non-target records remained at
sequence 2. The final dispatch reported completion action 5, protocol 7,
validation flags `0x33`, successful VMCS access, and no fail-closed state.
Canonical unload removed the service and module, and both failure-event
captures were empty. The debugger recorded only operator-requested breaks and
normal log closure.

The decision is **GO - Runtime V6 research scope complete**. Successor work
must preserve the fixed-width redaction boundary and may not reinterpret this
accepted evidence as production readiness.
