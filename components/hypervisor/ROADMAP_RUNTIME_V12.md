# Runtime V12 Roadmap - Stable Control-Session Owner Identity

## Baseline and objective

- Accepted Runtime V11 source commit:
  `0fd7f09242d50f873a9e76bd800075bd64f30a3f`
- Runtime V11 closure commit:
  `56d863b9c56d7669d25d56841b621a23a1606d1e`
- Development branch: `feature/hypervisor-core`
- Objective: replace reusable numeric process IDs with referenced process-object
  identity for control-session authorization.

Runtime V11 rejects inherited-handle IOCTLs from a different current PID. A
numeric PID is not a lifetime-stable identity, however: Windows can reuse it
after the opener exits while an inherited handle still keeps the file object
alive. Runtime V12 retains the opener's process object and compares each IRP's
requestor process object directly.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V12A Stable owner identity | Complete | File objects retain the admitted `PEPROCESS`; authorization compares nonzero object identity rather than PID values. |
| V12B Reference-lifetime authorization | Complete - GO | Fresh WDK/MSVC/MASM build `404/404`, focused owner-lifetime checks `3/3`, and full CTest `258/258` passed. |
| V12C Live owner-exit boundary | Complete - GO | Gate 50 proved owner admission, confirmed owner exit while the inherited file object remained alive, post-exit child denial, exact recovery after final close, and canonical unload. |

## Design contract

- Obtain the IRP requestor with `IoGetRequestorProcess` after existing access
  and namespace authorization succeeds.
- Reference the admitted process object once before completing file create.
- Store the referenced `PEPROCESS` in the WDF file-object context.
- Require exact nonzero process-object identity and user requestor mode before
  every IOCTL dispatch.
- Dereference the retained process object exactly once from WDF object-context
  cleanup, after requests can no longer use the file context.
- Preserve exclusivity, read/write access, root-only namespace, administrator/
  System SDDL, sequential PASSIVE queueing, and all 23 public IOCTL values and
  layouts.

## Gate 50 - owner exit and final inherited-handle close

Gate 50 is the only necessary Runtime V12 live boundary. A coordinator starts
an owner process that opens the canonical device, verifies the exact initial
observation, and creates a child with the inherited device handle plus a
synchronization handle to the owner process. The owner exits. Only after that
exit is signaled may the child issue the inherited-handle IOCTL, which must be
denied with Win32 error 5. The child closes the final inherited handle before
publishing its result.

The normal loader must then reopen the device, recover the exact four initial
observation records, and unload canonically. This exercises the new referenced
owner lifetime and cleanup boundary that Gate 49 could not cover because its
owner remained alive during the child attempt.

One run is sufficient. The changed risk is deterministic file-object/process-
object lifetime, not repeated VMX lifecycle behavior; existing stress gates
already qualify runtime repetition.

## Static verification evidence

- Fresh build directory: `build/codex-gate50-20260830-022528`
- Toolchain: Visual Studio 2026 / MSVC 19.51, WDK 10.0.28000.0, x64 MASM
- Build targets: `404/404` passed
- Focused owner-lifetime CTest: `3/3` passed
- Full CTest: `258/258` passed
- Required driver, loader, owner-lifetime helper, PDB, and VMX assembly object
  artifacts were present.
- Static verification did not sign, install, load, or execute the driver or
  either control-session helper.

## Accepted live evidence

Gate 50 passed on 2026-08-30 for source commit
`df2910d42e51dab45fb34711a5ad2f07a72968de`.

- Evidence archive SHA-256:
  `7E168042CD9B6B7C3587D74AA9FE620C094CFCBA4D2447CD33D0C140F18B8841`
- Closed WinDbg log SHA-256:
  `CE925881AC8D6F999B009A1C1439725EE57ACE04A0D2391D75CEB5938ADFC82E`
- Signature: `Valid`, signer `CN=hypersinv`
- Topology: exactly two groups with two active processors each
- Owner boundary: admitted before exit; owner exit confirmed; inherited child
  IOCTL denied with Win32 error 5
- Final-close recovery: exact generation-1, telemetry-sequence-2,
  dispatch-sequence-2 observation records on all four processors
- Unload: canonical; service and driver module absent afterward
- Failure events and loader stderr: none

The decision is **GO - Runtime V12 research scope complete**. Do not repeat
Gate 50 unless later changes invalidate this boundary.

## Safety and stop conditions

- Never install, load, or live-test the driver on the host machine.
- Run Gate 50 only from the clean disposable two-group/four-processor VM
  checkpoint under WinDbg.
- Stop on any signature, topology, owner admission, owner-exit, child denial,
  final-close recovery, unload, service, event, dump, hang, or bugcheck
  mismatch.
- After a failed load or uncertain runtime result, do not retry LOAD and do not
  run UNLOAD manually. Preserve the VM and debugger state.
