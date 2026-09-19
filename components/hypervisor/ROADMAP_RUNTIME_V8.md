# Runtime V8 Roadmap - Exclusive Privileged Control Sessions

## Baseline and objective

- Reviewed Runtime V7 source commit: `a1359502d0ceb79694c8236fd2aaf44a824df1f2`
- Development branch: `feature/hypervisor-core`
- Objective: prevent multiple privileged user-mode clients from opening the
  control device concurrently and interleaving otherwise valid lifecycle,
  diagnostic, and observation requests.

The existing protected SDDL admits only LocalSystem and built-in
Administrators, and the default queue is sequential. Those controls do not by
themselves make the WDF control device single-client: two privileged callers
can choose compatible `CreateFile` share masks and both obtain handles. Runtime
V8 makes session ownership explicit at the device boundary while preserving
all IOCTL numbers and wire layouts.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V8A Exclusive device boundary | Complete | WDF rejects every second simultaneous open independently of caller share masks. |
| V8B Static authorization | Complete - GO | Fresh WDK/MSVC/MASM build completed 388/388 targets; the focused mutation-backed boundary contract passed 1/1 and full CTest passed 251/251. |
| V8C Live contention and recovery | Complete - GO | Gate 46 proved one admitted shared guard handle, denial of an equally share-compatible second handle with error 5, exact recovery after close, and canonical teardown. |

## Design contract

- Device access remains restricted to LocalSystem and built-in Administrators.
- `FILE_DEVICE_SECURE_OPEN` remains enabled.
- `WdfDeviceInitSetExclusive(device_init, TRUE)` is applied before device
  creation, so WDF—not cooperative loader share flags—enforces one handle.
- The default device-control queue remains PASSIVE and sequential.
- A denied second open must not alter runtime state, service state, or the
  first handle.
- Closing the first handle must immediately permit the next privileged open.
- No IOCTL number, request/response ABI, runtime owner, or loader command is
  changed.

## Gate 46 - exclusive control-session contention

Gate 46 is the only necessary new live boundary. It loads once, opens the
device through a privileged native test seam with read/write sharing enabled,
then attempts a second equally share-compatible open. The second open must be
denied by the WDF exclusive-device policy rather than by cooperative share
flags. After the first handle closes, `observation-status` must succeed on all
four processors and canonical unload must remove the service and module.

One run is sufficient. This gate qualifies a single new WDF device-access
boundary; existing 100-cycle gates already cover runtime lifecycle stability,
and Gate 45 already qualifies the observation command used after recovery.

## Static verification evidence

- Fresh build directory:
  `build/codex-gate46-20260829-233436`
- Toolchain: Visual Studio 2026/MSVC 19.51 with WDK `10.0.28000.0` and
  x64 MASM.
- Build: `388/388` targets completed, including the WDK driver, loader, and
  required `vmx_launch.asm.obj` and `vmx_capture.asm.obj` artifacts.
- Focused authorization:
  `test_exclusive_control_device_boundary_contracts` passed `1/1`.
- Full static verification: CTest passed `251/251`.
- No driver was signed, installed, loaded, or executed during static
  verification.

## Completion decision

Gate 46 passed on 2026-08-30 for source commit
`0c0770a307f6c63f12bd6a784f5993d6d442581f`. The valid
`CN=hypersinv` signed driver admitted the first read/write shared handle and
denied an otherwise compatible second handle with Win32 error 5. Closing the
first handle immediately restored access: the exact four generation-1,
sequence-2 coherent observation records were returned before canonical
unload.

The evidence archive SHA-256 is
`6C24A056E0B1F3966A905B2FF1ABBDD281AB187CBEF71148FF3A5DEB2A6A241D`.
Both captured failure-event sets are empty, every loader stderr file is empty,
and the service is absent before and after the run. The closed WinDbg log
SHA-256 is
`441971C83FD8E29125C7960B279C59AEC2AEEF58C01B9038DD7546B8851E7F37`;
it shows private PDB symbols, normal driver unload, final module absence, no
bugcheck, and only operator-requested debugger breaks.

Runtime V8 is **GO and closed**. Do not repeat Gate 46. Future control-plane
hardening must preserve the accepted single-session boundary and the public
IOCTL numbers.

## Safety and stop conditions

- Never install, load, or live-test the driver on the host machine.
- Run Gate 46 only from the clean disposable two-group/four-processor VM
  checkpoint under WinDbg.
- Stop on any signature, topology, first-open, second-open, recovery query,
  unload, service, event, dump, hang, or bugcheck mismatch.
- After a failed load or uncertain runtime result, do not retry LOAD and do not
  run UNLOAD manually. Preserve the VM and debugger state.
