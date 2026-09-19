# Runtime V11 Roadmap - Process-Bound Control Sessions

## Baseline and objective

- Accepted Runtime V10 source commit:
  `e63fca1e47e6e095f71a7f218be7589baa54ac8c`
- Development branch: `feature/hypervisor-core`
- Objective: prevent an admitted control-device handle from becoming an
  alternate authorization path when inherited by another process.

Runtime V8 through V10 constrain handle count, effective rights, and device
namespace. Windows handle inheritance can still give another process access
to the same file object without a second create request. Runtime V11 records
the creating user process on the WDF file object and rejects every IOCTL whose
IRP requestor does not match that owner.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V11A Pure owner decision and KMDF integration | Complete | A dedicated pure decision requires a nonzero matching user-mode requestor; each file object records its admitted creator. |
| V11B Static authorization | Complete - GO | Fresh WDK/MSVC/MASM build `402/402`, focused owner checks `2/2`, and full CTest `257/257` passed. |
| V11C Live inherited-handle boundary | Complete - GO | Gate 49 admitted owner IOCTLs before and after a child attempt, denied the inherited child IOCTL with error 5, recovered exact observations, and unloaded canonically. |

## Design contract

- Allocate one nonpaged owner context per WDF file object.
- Record the nonzero user-mode requestor process ID only after namespace and
  read/write authorization succeeds.
- Before dispatching any IOCTL, require the same file object, a valid IRP, a
  user-mode requestor, and exact process-ID equality.
- Reject foreign, zero, kernel-mode, or context-free requests with
  `STATUS_ACCESS_DENIED` before touching runtime state.
- Preserve the accepted Runtime V8 exclusivity, Runtime V9 access matrix,
  Runtime V10 root namespace, administrator/System SDDL, sequential PASSIVE
  queue, and all 23 public IOCTL values and layouts.

## Gate 49 - inherited-handle process boundary

Gate 49 is the only necessary Runtime V11 live boundary. After one verified
LOAD, a dedicated user-mode helper opens the canonical device with an
inheritable read/write handle. The owner process performs an exact observation
query successfully, launches a child that inherits the same file object, and
requires the child's observation IOCTL to fail with Win32 error 5. The owner
must then perform the same query successfully again. After the helper closes,
the normal loader must recover all four exact initial observation records and
unload canonically.

One run is sufficient. This is a deterministic control-session authorization
boundary, not a VMX lifecycle change. Existing stress gates already qualify
runtime repetition, while Gates 46 through 48 qualify the adjacent device-open
boundaries.

## Static verification evidence

- Fresh build directory: `build/codex-gate49-final-20260830-011642`
- Toolchain: Visual Studio 2026 / MSVC 19.51, WDK 10.0.28000.0, x64 MASM
- Build targets: `402/402` passed
- Focused owner-boundary CTest: `2/2` passed
- Full CTest: `257/257` passed
- Required driver, loader, helper, PDB, and VMX assembly object artifacts were
  present.
- Static verification did not sign, install, load, or execute the driver.

## Accepted live evidence

Gate 49 passed on 2026-08-30 for source commit
`0fd7f09242d50f873a9e76bd800075bd64f30a3f`.

- Result: `PASS`
- Probe: `CONTROL_SESSION_OWNER`
- Topology: two groups, two processors per group
- Parent before child: `ADMITTED`
- Inherited child IOCTL: `DENIED_5`
- Parent after child: `ADMITTED`
- Recovered observation: exact generation `1`, telemetry sequence `2`, and
  dispatch sequence `2` on all four processors
- Signature: `Valid`, signer `CN=hypersinv`
- Service: absent before LOAD and after canonical UNLOAD
- Failure events: zero; stderr: empty
- WinDbg: private symbols loaded, normal unload observed, final module absent,
  and log closed normally
- Evidence archive SHA-256:
  `0A084E4787AD140DFFDD1757C2955097B6C21F6C888D78F18C374180519F663C`
- Closed WinDbg log SHA-256:
  `C48F4D10E0A859D4D947C54A81A37ED26FC6C3C319C5EB17F1690AD281FDF5E1`

Runtime V11 is closed. Do not repeat Gate 49 unless later source changes
invalidate this boundary.

## Safety and stop conditions

- Never install, load, or live-test the driver on the host machine.
- Run Gate 49 only from the clean disposable two-group/four-processor VM
  checkpoint under WinDbg.
- Stop on any signature, topology, parent admission, child denial, recovery,
  unload, service, event, dump, hang, or bugcheck mismatch.
- After a failed load or uncertain runtime result, do not retry LOAD and do not
  run UNLOAD manually. Preserve the VM and debugger state.
