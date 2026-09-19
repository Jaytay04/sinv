# Runtime V10 Roadmap - Root-Only Control Namespace

## Baseline and objective

- Accepted Runtime V9 source commit:
  `c2b19f024c71e6f6fd2ca9ad2358767c9ae8ef6e`
- Development branch: `feature/hypervisor-core`
- Objective: admit control sessions only through the canonical root device path
  while preserving the accepted access, exclusivity, and public IOCTL ABI.

The secure-open characteristic applies the device security descriptor to named
opens, but it does not define which names the control device accepts. Runtime
V10 makes that namespace boundary explicit: the canonical
`\\.\hypersinv` root has an empty WDF file name and remains valid; any trailing
name is denied before request dispatch.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V10A Pure namespace decision and KMDF integration | Complete | A dedicated pure decision admits only an empty WDF file name; missing file objects, missing names, and named opens fail closed. |
| V10B Static authorization | Complete - GO | Fresh WDK/MSVC/MASM build completed 396/396 targets; focused pure/integration authorization passed 2/2 and full CTest passed 255/255. |
| V10C Live namespace boundary | Complete - GO | Gate 48 denied the named read/write open with error 5, admitted the canonical root, recovered exact observations, and unloaded canonically. |

## Design contract

- Accept only an empty WDF file name at `EvtDeviceFileCreate`.
- Reject missing file objects, unavailable file names, and every non-empty name
  with `STATUS_ACCESS_DENIED`.
- Preserve Runtime V9's effective read/write requirement and Runtime V8's
  exclusive device policy.
- Preserve administrator/System SDDL, `FILE_DEVICE_SECURE_OPEN`, sequential
  PASSIVE queueing, all 23 IOCTL values, `METHOD_BUFFERED`, and
  `FILE_ANY_ACCESS`.
- Do not bind runtime lifetime to a user-mode handle.

## Gate 48 - control-session namespace boundary

Gate 48 is the only necessary Runtime V10 live boundary. After one verified
LOAD, it attempts a read/write shared open of
`\\.\hypersinv\unexpected`; the driver must deny it with Win32 error 5. It
then opens the canonical `\\.\hypersinv` root with the same rights and sharing,
which must succeed. After that handle closes, the exact four initial coherent
observation records must remain available and canonical UNLOAD must remove the
service and module.

One run is sufficient. This is a deterministic file-create namespace boundary,
not a runtime-stability change. Gates 46 and 47 already qualify the adjacent
exclusive and access-rights boundaries, and existing 100-cycle gates qualify
lifecycle repetition.

## Static verification evidence

- Fresh build directory: `build/codex-gate48-20260830-003456`.
- Toolchain: Visual Studio 2026/MSVC 19.51 with WDK `10.0.28000.0` and
  x64 MASM.
- Build: `396/396` targets completed, including the WDK driver, loader,
  dedicated namespace-decision object, and required MASM objects.
- Focused authorization: `test_control_session_namespace` and
  `test_control_session_namespace_boundary_contracts` passed `2/2`.
- Full static verification: CTest passed `255/255`.
- No driver was signed, installed, loaded, or executed during static
  verification.

## Completion decision

Gate 48 passed on 2026-08-30 for source commit
`e63fca1e47e6e095f71a7f218be7589baa54ac8c`. The valid
`CN=hypersinv` signed driver denied `\\.\hypersinv\unexpected` with Win32
error 5 while admitting the canonical `\\.\hypersinv` root. After the
canonical handle closed, the exact four generation-1, sequence-2 coherent
observation records were returned before canonical unload.

The evidence archive SHA-256 is
`CDD0600139DEB61051DAF8C08E9F6580823A3E632C686A5A3E42F54D97702E1A`.
Both captured failure-event sets are empty, every loader stderr file is empty,
and the service is absent before and after the run. The closed WinDbg log
SHA-256 is
`5A6C7DD231D2B4304C98CF340EDC4AAE2231F911F7101348B913675FE2E2FE4B`;
it shows private PDB symbols, normal driver unload, final module absence, no
bugcheck, and only operator-requested debugger breaks.

Runtime V10 is **GO and closed**. Do not repeat Gate 48. Future control-plane
hardening must preserve the accepted root namespace, access matrix,
single-session boundary, and public IOCTL numbers.

## Safety and stop conditions

- Never install, load, or live-test the driver on the host machine.
- Run Gate 48 only from the clean disposable two-group/four-processor VM
  checkpoint under WinDbg.
- Stop on any signature, topology, named-open, canonical-open, recovery query,
  unload, service, event, dump, hang, or bugcheck mismatch.
- After a failed load or uncertain runtime result, do not retry LOAD and do not
  run UNLOAD manually. Preserve the VM and debugger state.
