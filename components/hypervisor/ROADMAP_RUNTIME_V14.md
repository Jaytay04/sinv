# Runtime V14 Roadmap - Exact Operational Request Framing

## Baseline and objective

- Accepted Runtime V13 source commit:
  `87a58b15f6a3ca12211ecd7cf97679c0cd6fcbdc`
- Runtime V13 closure commit:
  `9c8a725`
- Development branch: `feature/hypervisor-core`
- Objective: reject every negotiated operational IOCTL whose input length is
  not the one canonical length assigned to that operation.

Protocol negotiation prevents an incompatible client from reaching runtime
dispatch, but it does not by itself make each subsequent request
unambiguous. Five legacy commands accept no input, while every request-bearing
operation has a fixed 16-byte input. Runtime V14 centralizes that framing rule
before operational dispatch and keeps handler-level content validation as
defense in depth.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V14A Pure framing policy | Complete | One dependency-free policy maps all 23 operational IOCTLs to their canonical zero- or 16-byte input length and distinguishes unknown codes. |
| V14B Dispatch enforcement | Complete | After owner authorization and protocol negotiation, a known non-canonical input length completes with `STATUS_INFO_LENGTH_MISMATCH` before `IoctlMainDispatch`. |
| V14C Handler consistency | Complete | HOOK and UNHOOK validators reject both short and overlong fixed requests; existing exact validators remain defense in depth. |
| V14D Static authorization | Complete - GO | Corrected source `c67d87f056e8eefbb6d6d2a577e7b4d2b56935e1` passed a fresh WDK/MSVC/MASM build (`422/422`), focused contracts (`7/7`), and the full CTest suite (`262/262`). |
| V14E Live framing boundary | Complete - PASS | Gate 52 rejected one-byte legacy STATUS and LOAD requests with Win32 error 24, accepted exact zero-byte STATUS with its complete 16-byte response, preserved exact authoritative observation state, and unloaded canonically. |

## Design contract

- Preserve all 24 public IOCTL values and every existing wire layout.
- Keep protocol negotiation at function `0x817` and handle it before the
  operational framing policy.
- Treat functions `0x800` through `0x816` as the complete operational
  inventory.
- Require zero input bytes for LOAD, UNLOAD, STATUS, PROBE, and LAUNCH_PROBE.
- Require exactly 16 input bytes for every other operational request.
- Perform framing only after file-object owner authorization and successful
  protocol negotiation, but before runtime dispatch or state mutation.
- Complete a known length mismatch with `STATUS_INFO_LENGTH_MISMATCH`, which
  maps to Win32 `ERROR_BAD_LENGTH` (24).
- Leave unknown IOCTL codes to the existing invalid-device-request dispatch.
- Preserve handler-level version, flags, padding, alignment, and content
  validation.

## Gate 52 - exact operational request framing

Gate 52 is the only necessary Runtime V14 live boundary. After one verified
LOAD, a dedicated helper opens and negotiates one canonical control session.
It sends a one-byte input to legacy STATUS and LOAD; both must fail with Win32
error 24 before runtime dispatch. It then sends exact zero-input STATUS on the
same handle and must receive the complete 16-byte legacy response. Runtime
state is qualified separately through the authoritative observation command.

The helper closes, the normal loader must independently negotiate and recover
all four exact initial observation records, and UNLOAD must remain canonical.

One run is sufficient. The changed boundary is deterministic WDF input-length
framing; accepted stress gates already cover repeated VMX lifecycle behavior.

The first Gate 52 attempt on 2026-08-30 correctly denied both malformed
requests with error 24 and produced no failure events, but the helper then
conflated transport acceptance with unrelated legacy status-field assertions.
Its error message also reported stale last-error state from the preceding
expected denial. That attempt is not acceptance evidence. The corrected helper
requires successful transport plus the exact 16-byte response and records the
legacy fields diagnostically; the independent authoritative observation still
enforces exact four-processor runtime state.

## Static authorization evidence

- Source commit: `c67d87f056e8eefbb6d6d2a577e7b4d2b56935e1`
- Build directory:
  `build/codex-gate52-rerun-final-20260830-040630`
- Toolchain: Visual Studio 2026 Community, MSVC 19.51, WDK 10.0.28000.0,
  Ninja, and MASM x64.
- Build: `422/422` targets passed, including the WDK driver, loader,
  request-frame probe, and required MASM objects.
- Focused VMX launch/runtime/protocol/dispatch/diagnostic and request-frame
  contracts: `7/7` passed.
- Full CTest suite: `262/262` passed.
- Disassembly contracts for VMX launch, persistent entry, INVEPT, and INVVPID
  were discovered and passed in the full suite.
- Required SYS, PDB, EXE, and MASM object artifacts exist.
- No driver signing, installation, loading, or live execution occurred during
  static authorization.

## Live acceptance evidence

- Gate 52 passed once on 2026-09-05 from the corrected powered-off
  `Gate6-HostRunner-ReducedMemory-Clean` snapshot.
- RunId:
  `gate52-control-session-request-framing-20260905-002222-fe4c6dbf95`.
- Pinned package SHA-256:
  `F1341947950BF627B328713F2EE7C0602F2D746A5024A7400931B47D13B80AF5`.
- Evidence archive SHA-256:
  `A760CBFFFA1C420A31A681D6F7CA275E2353692431C22A2E0607D30FCE8B30AB`.
- Closed debugger log SHA-256:
  `D03C61775ECF37309F76ABF7BB44B352CE729B009903C08DB74EDCEF88401863`.
- The result was PASS with exit code 0, four exact generation-1 sequence-2
  observation records, canonical unload, service absence, empty stderr, and
  no fatal debugger-pattern matches.
- Runtime V14 is closed. Do not repeat Gate 52.

## Safety and stop conditions

- Never install, load, or live-test the driver on the host machine.
- Run Gate 52 only from the clean disposable two-group/four-processor VM
  checkpoint under WinDbg.
- Stop on any signature, topology, framing, status, observation, unload,
  service, event, dump, hang, or bugcheck mismatch.
- After a failed load or uncertain runtime result, do not retry LOAD and do not
  run UNLOAD manually. Preserve the VM and debugger state.
