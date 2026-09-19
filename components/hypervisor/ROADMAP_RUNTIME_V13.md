# Runtime V13 Roadmap - Control Protocol Negotiation

## Baseline and objective

- Accepted Runtime V12 source commit:
  `df2910d42e51dab45fb34711a5ad2f07a72968de`
- Runtime V12 closure commit:
  `65761d6`
- Development branch: `feature/hypervisor-core`
- Objective: require an exact, per-file-object protocol negotiation before any
  operational IOCTL can reach runtime dispatch.

The signed driver and loader have previously been transferred as separate
artifacts. A stale loader can therefore open a newer driver and submit an ABI
it does not understand. Runtime V13 makes compatibility explicit and fail
closed: file creation establishes identity and access, but not protocol
authority.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V13A Fixed protocol ABI | Complete | Additive function `0x817` carries exact 16-byte request/response records with magic, major, minor, size, and flags. Existing IOCTL values and layouts are unchanged. |
| V13B Session negotiation | Complete | Each admitted file object begins unnegotiated; malformed or incompatible negotiation fails without state change; only exact negotiation enables operational dispatch. |
| V13C Loader integration | Complete | The loader negotiates immediately after every open and closes the handle on any mismatch. Legacy owner probes use the same client adapter. |
| V13D Static authorization | Complete - GO | Source `87a58b15f6a3ca12211ecd7cf97679c0cd6fcbdc` passed a fresh WDK/MSVC/MASM build (`415/415`), focused contracts (`7/7`), and the full CTest suite (`260/260`). |
| V13E Live protocol boundary | Complete - GO | Gate 51 proved unnegotiated and wrong-version denial, exact protocol `1.0` acceptance, post-negotiation observation, independent-session recovery, and canonical unload. |

## Design contract

- Preserve all existing IOCTL numeric values and wire layouts.
- Add protocol negotiation only at function `0x817`, using
  `METHOD_BUFFERED` and the existing privileged control-session admission.
- Initialize negotiation state to false for each new WDF file object.
- Authenticate the process owner before negotiation or operational dispatch.
- Reject every non-negotiation IOCTL with `STATUS_REVISION_MISMATCH` until the
  exact protocol succeeds.
- Reject malformed sizes, magic, or flags without writing output or changing
  session state.
- Reject major or minor mismatches with `STATUS_REVISION_MISMATCH`.
- Make successful negotiation idempotent and scoped only to that file object.
- Close the loader handle immediately if negotiation or response validation
  fails.

## Gate 51 - control protocol negotiation

Gate 51 is the only necessary Runtime V13 live boundary. After a verified
LOAD, a dedicated helper opens one canonical control session and proves:

1. an observation IOCTL before negotiation is denied with Win32 error 1306;
2. a negotiation request with the wrong major version is denied with error
   1306;
3. the exact protocol `1.0` is accepted; and
4. the same session can then retrieve the exact initial observation.

The helper closes, the normal loader must independently negotiate and recover
all four exact initial observation records, and UNLOAD must remain canonical.

One run is sufficient. This qualifies a deterministic file-object protocol
state transition, not repeated VMX lifecycle behavior; accepted stress gates
already cover lifecycle repetition.

## Static authorization evidence

- Source commit: `87a58b15f6a3ca12211ecd7cf97679c0cd6fcbdc`
- Build directory:
  `build/codex-gate51-final-20260830-025449`
- Toolchain: Visual Studio 2026 Community, MSVC 19.51, WDK 10.0.28000.0,
  Ninja, and MASM x64.
- Build: `415/415` targets passed, including the WDK driver, loader,
  protocol probe, and required MASM objects.
- Focused protocol and control-session contracts: `7/7` passed.
- Full CTest suite: `260/260` passed.
- Disassembly contracts for VMX launch, persistent entry, INVEPT, and INVVPID
  were discovered and passed in the full suite.
- No driver signing, installation, loading, or live execution occurred during
  static authorization.

## Accepted live evidence

Gate 51 passed on 2026-08-30 for source commit
`87a58b15f6a3ca12211ecd7cf97679c0cd6fcbdc`.

- Evidence archive SHA-256:
  `E87AFDC490E37B699710DBA2AF72F536D57F42A30C002FFDEC08C294FE90020A`
- Closed WinDbg log SHA-256:
  `1E228A6C243AE4FED251B8B575519B1F3ACEB81A286AA386C87D0FA1F527DA56`
- Signature: `Valid`, signer `CN=hypersinv`
- Topology: exactly two groups with two active processors each
- Protocol boundary: unnegotiated operational IOCTL and wrong-major
  negotiation both denied with Win32 error 1306
- Exact negotiation: protocol `1.0` accepted; the same session recovered the
  exact initial observation
- Independent-session recovery: exact generation-1, telemetry-sequence-2,
  dispatch-sequence-2 records on all four processors
- Unload: canonical; service and driver module absent afterward
- Failure events and loader/probe stderr: none

The decision is **GO - Runtime V13 research scope complete**. Do not repeat
Gate 51 unless later changes invalidate this boundary.

## Safety and stop conditions

- Never install, load, or live-test the driver on the host machine.
- Run Gate 51 only from the clean disposable two-group/four-processor VM
  checkpoint under WinDbg.
- Stop on any signature, topology, negotiation, observation, unload, service,
  event, dump, hang, or bugcheck mismatch.
- After a failed load or uncertain runtime result, do not retry LOAD and do not
  run UNLOAD manually. Preserve the VM and debugger state.
