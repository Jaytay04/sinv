# Runtime V9 Roadmap - Explicit Control-Session Access

## Baseline and objective

- Accepted Runtime V8 source commit:
  `0c0770a307f6c63f12bd6a784f5993d6d442581f`
- Development branch: `feature/hypervisor-core`
- Objective: require every admitted control-device handle to possess both read
  and write rights without changing any public IOCTL number or wire layout.

The device SDDL and Runtime V8 exclusivity restrict who can open the device and
how many handles can coexist. The public IOCTL codes intentionally retain the
accepted Runtime V2 `FILE_ANY_ACCESS` numeric ABI, however, so the I/O manager
does not reject IOCTLs based on handle rights. Runtime V9 closes that gap at
the file-create boundary: zero-access, read-only, and write-only sessions are
denied before they can issue a request.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V9A Pure access decision and KMDF integration | Complete | A dedicated pure decision requires effective read and write rights; malformed or incomplete create requests fail closed. |
| V9B Static authorization | Complete - GO | Fresh WDK/MSVC/MASM build completed 392/392 targets; focused pure/integration authorization passed 2/2 and full CTest passed 253/253. |
| V9C Live access matrix | Complete - GO | Gate 47 denied zero/read-only/write-only opens with error 5, admitted read/write, recovered exact observations, and unloaded canonically. |

## Design contract

- Preserve every accepted IOCTL value, `METHOD_BUFFERED` transfer method, and
  `FILE_ANY_ACCESS` field; Runtime V9 adds enforcement without ABI drift.
- Accept either generic or file-specific representations of read/write rights
  because Windows can map generic rights before the create callback runs.
- Require effective read and write rights. Additional granted rights do not
  weaken the requirement.
- Reject missing security context, non-create request shape, zero access,
  read-only access, and write-only access with `STATUS_ACCESS_DENIED`.
- Keep the administrator/System SDDL, secure-open characteristic, Runtime V8
  exclusivity, sequential queue, and PASSIVE execution level unchanged.
- Do not bind runtime lifetime to a user-mode handle. Closing an admitted
  handle releases the control session but does not silently stop VMX.

## Gate 47 - control-session access matrix

Gate 47 is the only necessary live Runtime V9 boundary. After one verified
LOAD, it uses equally share-compatible native opens to prove that zero-access,
read-only, and write-only handles are each denied with Win32 error 5. A
read/write handle must then be admitted. After that handle closes, the exact
four initial coherent observation records must remain available and canonical
UNLOAD must remove the service and module.

One run is sufficient. This is a deterministic file-create authorization
boundary, not a runtime-stability change. Gate 46 already qualifies exclusive
ownership, Gate 45 qualifies the recovery observation, and existing
100-cycle gates qualify lifecycle repetition.

## Static verification evidence

- Fresh build directory: `build/codex-gate47-20260830-001430`.
- Toolchain: Visual Studio 2026/MSVC 19.51 with WDK `10.0.28000.0` and
  x64 MASM.
- Build: `392/392` targets completed, including the WDK driver, loader,
  dedicated access-decision object, and required MASM objects.
- Focused authorization: `test_control_session_access` and
  `test_control_session_access_boundary_contracts` passed `2/2`.
- Full static verification: CTest passed `253/253`.
- No driver was signed, installed, loaded, or executed during static
  verification.

## Completion decision

Gate 47 passed on 2026-08-30 for source commit
`c2b19f024c71e6f6fd2ca9ad2358767c9ae8ef6e`. The valid
`CN=hypersinv` signed driver denied zero-access, read-only, and write-only
opens with Win32 error 5 while admitting the read/write session. After that
handle closed, the exact four generation-1, sequence-2 coherent observation
records were returned before canonical unload.

The evidence archive SHA-256 is
`FEDB560EFD3F139340496AAF7AE33AB53DACCC663D30B1650DC2FE6E4E747713`.
Both captured failure-event sets are empty, every loader stderr file is empty,
and the service is absent before and after the run. The closed WinDbg log
SHA-256 is
`AB718AEFFC92F890BE0293BA6AD0F20F64108470198187232B17A17C383434E6`;
it shows private PDB symbols, normal driver unload, final module absence, no
bugcheck, and only operator-requested debugger breaks.

Runtime V9 is **GO and closed**. Do not repeat Gate 47. Future control-plane
hardening must preserve the accepted access matrix, single-session boundary,
and public IOCTL numbers.

## Safety and stop conditions

- Never install, load, or live-test the driver on the host machine.
- Run Gate 47 only from the clean disposable two-group/four-processor VM
  checkpoint under WinDbg.
- Stop on any signature, topology, access-result, recovery query, unload,
  service, event, dump, hang, or bugcheck mismatch.
- After a failed load or uncertain runtime result, do not retry LOAD and do not
  run UNLOAD manually. Preserve the VM and debugger state.
