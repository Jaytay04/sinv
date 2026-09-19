# Runtime V7 Roadmap - Authenticated Coherent Observation Bundles

## Baseline and objective

- Reviewed Runtime V6 source commit: `17c085eab8389dbad81dbd3d5bc17f4092385bab`
- Development branch: `feature/hypervisor-core`
- Objective: eliminate the cross-command skew window between the accepted
  telemetry and redacted dispatch status surfaces without adding a new
  VM-exit writer, history buffer, or guest-address exposure.

Runtime V7 composes both accepted records inside one owner-serialized,
dispatch-sequence-stable capture. The public response remains fixed-width,
versioned, pointer-free, and redacted. It reports current authenticated state;
it does not encode external live-gate acceptance.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V7A Pure bundle model | Complete | Fixed 240-byte ABI and fail-closed identity, generation, sequence, counter, and nested-record validation. |
| V7B Runtime query path | Complete | One per-index owner/runtime operation and one dispatch publication window produce both nested records through IOCTL `0x816`. |
| V7C Static authorization | Complete - GO | Fresh 388-target WDK/MSVC/MASM build, focused 2/2 contracts, and full CTest 250/250 passed. |
| V7D Live coherence | Complete - GO | Gate 45 proved exact baseline/post-CPUID bundles and canonical teardown on the disposable two-group/four-processor VM. |

## Design contract

- The response is exactly 240 bytes, eight-byte aligned, pointer-free, and
  contains only the already accepted 112-byte telemetry and dispatch records.
- One operation acquires the driver owner and runtime operation boundary once.
- The telemetry snapshot and dispatch record are copied while one nonzero even
  dispatch sequence remains unchanged.
- Nested processor count, system index, group, number, owner generation, and
  telemetry sequence must match exactly.
- The telemetry category sum must equal total exits without integer overflow.
- Nested version, size, classification, reserved, sequence, error-valid, and
  fail-closed invariants remain mandatory.
- Failure preserves the caller's zero output and publishes no partial bundle.
- Existing telemetry and dispatch IOCTLs remain ABI-compatible.

## Gate 45 - observation-bundle CPUID coherence

Gate 45 is the only necessary new live boundary. It loads once, captures all
four coherent bundles with `observation-status`, executes one already-qualified
authenticated CPUID probe on system index 0, captures all four bundles again,
and unloads canonically.

PASS requires exact generation-1 initial records at telemetry/dispatch
sequence 2. After the probe, system index 0 must advance both nested sequences
to 10 with total/CPUID/VMCALL deltas 4/3/1 and the exact completion dispatch:
reason 18, length 3, action 5, protocol 7, validation flags `0x33`, successful
VMCS access, and no fail-closed state. Indices 1 through 3 must remain at their
exact sequence-2 initial records. Canonical unload, service/module absence,
clean failure events, and no new dump remain mandatory.

One run is sufficient. Gate 22 already qualifies the CPUID route for 100
cycles, Gate 44 proves the two legacy surfaces advance together, and Gate 45
adds only the new atomic read boundary. Repeating this read-only bundle would
not exercise a new ownership or hardware case.

## Safety and stop conditions

- Never sign, install, load, or live-test the driver on the host machine.
- Run Gate 45 only from the clean disposable two-group/four-processor VM
  checkpoint under WinDbg.
- Stop on any signature, topology, identity, generation, sequence, counter,
  dispatch, unload, service, event, dump, hang, or bugcheck mismatch.
- After a failed load or uncertain runtime result, do not retry LOAD and do not
  run UNLOAD manually. Preserve the VM and debugger state.

## Immediate checkpoint

Fresh host-only authorization completed in
`build/codex-gate45-20260829-223944`: WDK 10.0.28000.0 was found, all 388
WDK/MSVC/MASM targets built, focused observation checks passed 2/2, and full
CTest passed 250/250. Required driver, loader, PDB, and MASM outputs are
present. No signing, installation, loading, or live VMX execution occurred.

Create one matched signed stage and run Gate 45 once. No host live execution
is permitted.

Gate 45 passed on 2026-08-29 for source commit
`a1359502d0ceb79694c8236fd2aaf44a824df1f2`. The accepted evidence archive
SHA-256 is
`62689C8F1D6ADDD1C60C27F907DB921CDEE929F9C65E19FA3448CFBEDF74D7E5`;
the closed WinDbg log SHA-256 is
`3FE7A7E8DD00FF43001943B5814F93ED8CBB084837AF17EFDE9A363DE01819CD`.
All four initial bundles reported matching generation-1 telemetry/dispatch
sequence 2. System index 0 advanced both nested sequences to 10 with exact
total/CPUID/VMCALL deltas 4/3/1 and the accepted completion dispatch, while
indices 1 through 3 remained unchanged. Canonical unload removed the service
and module, both failure-event captures were empty, all stderr captures were
empty, and WinDbg recorded only operator-requested breaks before normal log
closure.

The decision is **GO - Runtime V7 research scope complete**. Future work must
use a successor roadmap and preserve the accepted fixed-width redaction and
coherence boundary.
