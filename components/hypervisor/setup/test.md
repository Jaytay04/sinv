# Testing hypersinv

## Persistent Runtime + EPT supervised Gate 5

This is the next live boundary after the pure/static EPT integration gates. Run
it only in the dedicated snapshotted VM. The host build and test machine must
not load the driver. Use one fresh VM checkpoint and one signed artifact set;
do not retry after any mismatch.

### Host preparation

1. Build the exact branch revision with the mandatory WDK configuration and
   confirm the complete CTest suite passes.
2. Copy `hypervisor_driver.sys`, `hypervisor_driver.pdb`,
   `hypervisor_loader.exe`, `hypervisor_loader.pdb`, and
   `setup/gate5-ept-lifecycle.ps1` into a clean staging directory.
3. Sign only the staged driver with the established test certificate. Record
   SHA-256 hashes after signing. Do not modify the built artifact in place.

### VM preconditions

1. Revert the dedicated VM to its clean checkpoint. Verify VT-x/EPT exposure,
   VBS/Hyper-V disabled, test-signing policy, crash-dump configuration, and the
   trusted test certificate using the established Gate 4 procedure.
2. Create a new checkpoint before the run.
3. Attach WinDbg and open a timestamped log. Add the directory containing the
   matching `hypervisor_driver.pdb` to `.sympath`, then run `.reload /f` and
   `g`. Keep the debugger attached for the complete LOAD/UNLOAD cycle.
4. Copy the five staged files to `C:\V3H`. Confirm `sc.exe query hypersinv`
   returns exit code `1060` before running the script.

### Execute exactly one cycle

From elevated Windows PowerShell in the VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate5-ept-lifecycle.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
```

The script stops on the first failure and must not be rerun in that VM state.
It requires both authoritative load proofs—Runtime `LOADED` from `0x807` and
EPT `READY` from `0x808`—before issuing UNLOAD. SCM cleanup is accepted only
after the loader proves Runtime/EPT `DETACHED` and `sc.exe` confirms service
absence (`1060`). A test-certificate `UnknownError` from
`Get-AuthenticodeSignature` is recorded but is not itself the kernel trust
verdict; a driver-load failure is still fatal.

### Evidence and stop rule

On completion, copy the timestamped archive from `C:\V3H\evidence` and the
host WinDbg log off the VM. Preserve the checkpoint until both are reviewed.
On any failure, do not retry LOAD, do not run UNLOAD manually, do not replace
the image, and do not reboot/reset until the evidence is copied and reviewed.
The loader's fail-closed rollback may intentionally leave the service installed
when canonical detached proof is unavailable.


## Gate 6 and Gate 7 repetition evidence

The signed d4e02c9 image passed the complete Runtime+EPT lifecycle escalation
on the dedicated two-group, four-processor VM:

- Gate 5: one supervised Runtime LOADED / EPT READY / dual DETACHED cycle.
- Gate 6: 10/10 consecutive cycles through gate6-ept-10cycles.ps1.
- Gate 7: 100/100 consecutive cycles through gate7-ept-100cycles.ps1.

Every repetition required service absence before LOAD, reused the verified
single-cycle Gate 5 script, stopped on the first mismatch, required exact child
PASS evidence, and confirmed service absence after UNLOAD. The Gate 7 archive
contained 100 Runtime load proofs, 100 EPT READY proofs, 100 unload proofs, zero
nonempty stderr files, and 100 final service-absence results. WinDbg recorded
100 driver unloads and the final module query was empty. The unrelated
informational TiWorker and session-transition events do not indicate a driver
failure.

These repetition scripts are operator-only artifacts. They do not sign, build,
install, or invoke the driver during CTest; their static verifier checks exact
cycle counts, Gate 5 composition, failure gates, and evidence archival.

## Gate 8: kernel-owned no-access EPT hook roundtrip

Gate 8 is the next live boundary. It proves one complete private-page EPT hook
transaction after canonical Runtime LOADED and EPT READY: split/add,
all-processor invalidation, remove/rollback, page release, Runtime/EPT unload,
and service removal. The diagnostic does not deliberately access the trapped page while the hook
exists, so it does not prove EPT-violation VM-exit routing.
That remains a later, separately reviewed milestone.

Run Gate 8 only in the dedicated snapshotted VM. Codex must not execute it on
the host development machine.

### Host staging and signing

1. Use the final build reported in agent_docs/latest_session_work.md.
2. Copy these files into a clean host staging directory:
   hypervisor_driver.sys, hypervisor_driver.pdb,
   hypervisor_loader.exe, hypervisor_loader.pdb, and
   setup/gate8-ept-hook-roundtrip.ps1.
3. Sign only the staged hypervisor_driver.sys with the established test
   certificate. Record SHA-256 hashes after signing.
4. Copy the five files to C:\V3H in the clean test VM.

### WinDbg host log

Before running the VM script, attach the kernel debugger and enter:

~~~text
.symfix
.sympath+ <HOST_DIRECTORY_CONTAINING_hypervisor_driver.pdb>
.reload /f
ed nt!Kd_IHVDRIVER_Mask 0xFFFFFFFF
.logopen /t <HOST_LOG_DIRECTORY>\windbg-gate8-ept-hook-roundtrip.log
g
~~~

The IHVDRIVER mask is required so fail-closed Gate 8 hook-add diagnostics are captured. Keep WinDbg attached for the entire run. Do not set software breakpoints in the
VM-exit path. After the VM script finishes, break in once, run:

~~~text
lm m hypervisor_driver
.logclose
g
~~~

Copy the closed WinDbg log with the VM evidence archive.

### VM execution

Revert to the known-safe VM checkpoint, create a fresh checkpoint, and verify
sc.exe query hypersinv returns exit code 1060. Then run from elevated Windows
PowerShell:

~~~powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate8-ept-hook-roundtrip.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
~~~

PASS requires canonical Runtime LOADED, EPT READY, the exact
SUCCESS: Kernel-owned no-access EPT hook roundtrip verified. line, canonical
Runtime/EPT detached proof, service absence (1060), and no new crash dump.

On any failure, do not retry LOAD, do not run UNLOAD manually, do not replace
artifacts, and do not reboot or reset until the timestamped evidence directory
and WinDbg log have been copied off the VM. Preserve the checkpoint for review.

### Accepted Gate 8 evidence

The supervised `gate8-ept-hook-roundtrip-20260819-154002` run passed on the
two-group/four-processor VM. It proved canonical Runtime LOADED, EPT READY,
split/add with 4/4 invalidations, remove with 4/4 invalidations, generation 4,
canonical unload, and service absence. The matching WinDbg log contains only
the operator-requested break after unload and no bugcheck or fail-closed driver
diagnostic.

## Gate 9: 10-cycle EPT hook roundtrip stress

Gate 9 repeats the complete accepted Gate 8 transaction exactly ten times.
Every cycle begins and ends with `hypersinv` absent, runs the unmodified Gate 8
script in a child PowerShell process, requires both `Result=PASS` and
`Roundtrip=PASS`, and retains every inner evidence directory and archive.

Gate 9 still does not prove EPT-violation VM-exit routing because the retained
private page is never deliberately accessed while the no-access hook exists.

Stage the same signed driver, PDBs, and loader used for Gate 8 plus both
`gate8-ept-hook-roundtrip.ps1` and
`gate9-ept-hook-roundtrip-10cycles.ps1` in `C:\V3H`. With WinDbg attached and
logging, run from elevated Windows PowerShell:

~~~powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate9-ept-hook-roundtrip-10cycles.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
~~~

PASS requires `10/10`, `PassedCycles=10`, service absence after every cycle,
and the final `PASS: all 10 Runtime/EPT hook roundtrip cycles completed.` line.
On any failure, do not retry LOAD, do not run UNLOAD manually, and do not
reboot or reset until the outer evidence archive and closed WinDbg log are
copied off the VM.


## Gate 10: 100-cycle EPT hook roundtrip stress

Gate 10 repeats the complete accepted Gate 8 transaction exactly one hundred
times. Every cycle begins and ends with `hypersinv` absent, runs the unmodified
Gate 8 script in a child PowerShell process, requires both `Result=PASS` and
`Roundtrip=PASS`, and retains every inner evidence directory and archive.

This is the final repetition gate for the management-plane EPT hook roundtrip.
It still does not prove EPT-violation VM-exit routing because the retained
private page is never deliberately accessed while the no-access hook exists.

Use the same signed Gate 9 driver, PDBs, loader, and Gate 8 script. Add
`gate10-ept-hook-roundtrip-100cycles.ps1` to `C:\V3H`. With WinDbg attached
and logging, run from elevated Windows PowerShell:

~~~powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate10-ept-hook-roundtrip-100cycles.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
~~~

PASS requires `100/100`, `PassedCycles=100`, service absence after every cycle,
and the final `PASS: all 100 Runtime/EPT hook roundtrip cycles completed.` line.
On any failure, do not retry LOAD, do not run UNLOAD manually, and do not
reboot or reset until the outer evidence archive and closed WinDbg log are
copied off the VM.
## Gate 11: supervised one-shot EPT-violation VM-exit route

Gate 11 is the next live boundary after the accepted Gate 10 stress run. It
runs exactly once on the dedicated snapshotted VM configured with two
processor groups and two active processors per group. The diagnostic installs
a write trap on one private kernel page, deliberately writes one byte, requires
VM-exit reason 48, captures the guest physical address and EPT pointer, restores
the exact leaf entry, executes one single-context INVEPT, resumes the same
guest RIP without advancing it, removes the hook across all processors, frees
the page, and proves canonical unload/service removal.

Codex must not run this gate, sign the driver, install the service, or execute
VMX on the development machine. The operator performs it only after reverting
the test VM to a clean checkpoint.

### Stage the exact build

Copy the following files from the final build reported in
`agent_docs/latest_session_work.md` to `C:\V3H` inside the VM:

- `hypervisor_driver.sys`
- `hypervisor_driver.pdb`
- `hypervisor_loader.exe`
- `hypervisor_loader.pdb`
- `gate11-ept-violation-roundtrip.ps1`

Sign only the staged `hypervisor_driver.sys` using the established test
certificate, then record hashes of all five staged files. Do not mix artifacts
from different builds.

### WinDbg logging

Attach kernel WinDbg before running the script. In the debugger command window:

~~~text
.symfix
.sympath+ C:\V3H
.reload /f hypervisor_driver.sys
ed nt!Kd_IHVDRIVER_Mask 0xf
.logopen /t C:\V3H\evidence\windbg-gate11-ept-violation.log
g
~~~

Keep WinDbg attached for the entire run. Do not set software breakpoints in the
VM-exit, EPT, INVEPT, or teardown paths. After the VM script completes, break
in once and run:

~~~text
lm m hypervisor_driver
.logclose
g
~~~

### Run in the VM

From elevated Windows PowerShell:

~~~powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate11-ept-violation-roundtrip.ps1' `
    -Stage 'C:\V3H' `
    -EvidenceRoot 'C:\V3H\evidence'
~~~

PASS requires all of the following:

- exactly two processor groups with two processors each;
- canonical Runtime LOADED and EPT READY proof;
- `SUCCESS: One-shot EPT violation VM-exit roundtrip verified.`;
- route state `3`, dispatcher failure `0`, write access, INVEPT outcome `1`;
- byte evidence `A5/5A`, nonzero aligned guest physical address, nonzero EPT
  pointer and guest RIP;
- add and remove invalidation successes equal the four-processor count;
- canonical Runtime/EPT unload and `sc.exe` service absence (`1060`);
- no new loader dump, kernel dump, hang, bugcheck, or fail-closed diagnostic.

On any mismatch, stop immediately. Do not retry LOAD and do not run UNLOAD
manually. Preserve the timestamped evidence directory, its zip, and the closed
WinDbg log before reverting or restarting the VM.

## Gate 12: 10-cycle EPT-violation VM-exit stress

Gate 12 repeats the complete accepted Gate 11 transaction exactly ten times.
Each cycle starts only when `hypersinv` is absent, runs the unchanged Gate 11
script in a child PowerShell process, requires both `Result=PASS` and
`ViolationRoundtrip=PASS`, and proves service absence again before the next
cycle. One accepted run is not release-quality stress evidence.

Stage the same signed driver, matching PDBs, loader, and Gate 11 script used by
the accepted one-shot run, plus `gate12-ept-violation-roundtrip-10cycles.ps1`.
Do not mix build artifacts. Attach WinDbg and open a new log before running:

~~~text
.symfix
.sympath+ C:\V3H
.reload /f hypervisor_driver.sys
ed nt!Kd_IHVDRIVER_Mask 0xf
.logopen /t C:\V3H\evidence\windbg-gate12-ept-violation-10cycles.log
g
~~~

From elevated Windows PowerShell in the clean, snapshotted test VM:

~~~powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate12-ept-violation-roundtrip-10cycles.ps1' `
    -Stage 'C:\V3H' `
    -EvidenceRoot 'C:\V3H\evidence'
~~~

PASS requires `10/10`, `PassedCycles=10`, a complete Gate 11 proof inside every
cycle directory, service absence before and after every cycle, and the final
`PASS: all 10 EPT-violation VM-exit roundtrip cycles completed.` line. On the
first mismatch, do not retry LOAD, do not run UNLOAD manually, and do not reset
or reboot until the evidence archive and closed WinDbg log are copied off the
VM.

## Gate 13: 100-cycle EPT-violation VM-exit stress

Gate 13 is the release-quality repetition gate after the accepted Gate 12
`10/10` run. It repeats the unchanged Gate 11 transaction exactly one hundred
times. Each cycle requires service absence before LOAD, a successful child
process, exact `Result=PASS` plus `ViolationRoundtrip=PASS`, and service absence
after canonical unload.

Stage the same accepted signed driver, matching PDBs, loader, and Gate 11
script, plus `gate13-ept-violation-roundtrip-100cycles.ps1`. Do not mix build
artifacts. Attach WinDbg and open a fresh log:

~~~text
.symfix
.sympath+ C:\V3H
.reload /f
ed nt!Kd_IHVDRIVER_Mask 0xf
.logopen /t C:\V3H\evidence\windbg-gate13-ept-violation-100cycles.log
g
~~~

From elevated Windows PowerShell in the clean, snapshotted test VM:

~~~powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate13-ept-violation-roundtrip-100cycles.ps1' `
    -Stage 'C:\V3H' `
    -EvidenceRoot 'C:\V3H\evidence'
~~~

PASS requires `100/100`, `PassedCycles=100`, a complete Gate 11 proof in every
cycle directory, service absence before and after every cycle, and the final
`PASS: all 100 EPT-violation VM-exit roundtrip cycles completed.` line. On the
first mismatch, do not retry LOAD, do not run UNLOAD manually, and do not reset
or reboot until the evidence archive and closed WinDbg log are copied off the
VM.
## System Prerequisites

All of these must be satisfied before loading the driver — **no exceptions**:

1. **Hyper-V/VBS must be OFF** (reboot required):
   ```cmd
   bcdedit /set hypervisorlaunchtype off
   ```
   Verify with `bcdedit /enum | find "hypervisorlaunchtype"` — should show `off`.

2. **Test signing must be ON** (reboot required):
   ```cmd
   bcdedit /set testsigning on
   ```

3. **Driver Signature Enforcement must be OFF** (one-time per boot, or permanent via bcdedit):
   ```cmd
   bcdedit /set nointegritychecks on
   ```
   Alternatively, hold Shift while clicking Restart → Troubleshoot → Advanced Options → Startup Settings → Disable driver signature enforcement.

4. **Confirm Intel VT-x availability and VBS state**: CPU/VM must expose VT-x with EPT and VPID, and VBS must be disabled.
   ```powershell
   Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard | Select-Object VirtualizationBasedSecurityStatus,SecurityServicesConfigured,SecurityServicesRunning
   ```
   Use the DeviceGuard/VBS status as the readiness check. VMware may still report a hypervisor even with VBS disabled; `systeminfo` hypervisor text is informational only, not a readiness verdict.

## One-Time Setup

### Generate Test Certificate (if not already done)

If `hypersinv.pfx` and `hypersinv.cer` don't exist yet:

```cmd
makecert -r -pe -ss PrivateCertStore -n "CN=hypersinv" hypersinv.cer
pvk2pfx -pvk hypersinv.pvk -spc hypersinv.cer -pfx hypersinv.pfx -po test
```

Note: `makecert` and `pvk2pfx` are deprecated but still available in the WDK. Modern alternative:
```cmd
New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=hypersinv" -KeyUsage DigitalSignature -CertStoreLocation Cert:\CurrentUser\My
```

### Install Test Certificate

```cmd
certutil -addstore Root hypersinv.cer
certutil -addstore TrustedPublisher hypersinv.cer
```

## Legacy probe/load workflow (separate from Runtime V2)

### 1. Build

From a **Developer Command Prompt for VS 2026**:

```cmd
cmake -B build -G Ninja
cmake --build build
```

### 2. Sign the Driver

```cmd
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe" sign /fd SHA256 /f hypersinv.pfx /p test build\src\driver\hypervisor_driver.sys
```

Verify the signature:
```cmd
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe" verify /pa build\src\driver\hypervisor_driver.sys
```

### 3. Probe (smoke test)

Runs a transient VMXON/VMXOFF cycle on the current processor. Does NOT leave VMX root operation active — this is a diagnostic probe only.

```cmd
build\src\loader\Debug\hypervisor_loader.exe probe
```

**Expected output**: `SUCCESS: Processor group 0, number 0 entered and exited VMX operation.` with `VMXOFF Completed: yes` and `CR State Restored: yes`.

If this fails, stop here — do not proceed to load.

### 4. Load the Hypervisor

```cmd
build\src\loader\Debug\hypervisor_loader.exe load
```

This broadcasts VMX init across all cores via DPC. The driver service is created, started, and the IOCTL_LOAD handler runs per-core VMXON/VMLAUNCH.

### 5. Status Check

```cmd
build\src\loader\Debug\hypervisor_loader.exe status
```

Should show active cores and hook count.

### 6. Unload

```cmd
build\src\loader\Debug\hypervisor_loader.exe unload
```

This sends IOCTL_UNLOAD which broadcasts VMXOFF across all active cores, tears down EPT, and stops the driver service.

## Legacy Probe Output Reference

A healthy probe looks like this:

```
C:\Users\jayli\Documents\Hyper>hypervisor_loader.exe probe
Hypervisor Core Loader v1.0
=== PROBE ===
Using driver at: C:\Users\jayli\Documents\Hyper\hypervisor_driver.sys
Service created successfully.
Starting driver...
Driver started successfully.
Device opened: \.\hypersinv
Sending IOCTL_PROBE...
VMX Probe Diagnostics:
  Domain Status:       0x00000000 (success)
  NTSTATUS:            0x00000000
  Runtime Stage:       14 (complete)
  VMX Status:          0 (success)
  VMXOFF Completed:    yes
  CR State Restored:   yes
  Processor:           group 0, number 0
  VMX Revision:        0x00000001
  VMX Region Size:     4096 bytes
  FEATURE_CONTROL:     0x0000000000000005
  VMX_BASIC:           0x01D8100000000001
  VMXON PA (freed):    0x000000023FFEE000
  CR0:                 0x0000000080050033 -> 0x0000000080050033
  CR4:                 0x0000000000B50EF8 -> 0x0000000000B52EF8
SUCCESS: Processor group 0, number 0 entered and exited VMX operation.
This was a transient VMXON/VMXOFF probe only. Persistent active cores: 0.
VMLAUNCH was not attempted.
```

Key indicators:
- `VMX Status: 0` — VMXON succeeded
- `VMXOFF Completed: yes` — clean teardown
- `CR State Restored: yes` — CR0/CR4 fixed bits validated
- `FEATURE_CONTROL: 0x5` — lock bit (bit 0) + VMXON outside SMX (bit 2) are set

## Running Unit Tests

```cmd
ctest --test-dir build -C Debug
```

Requires 2,255 assertions across 6 test executables to pass.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `VMXON failed: 1` | FEATURE_CONTROL lock bit not set, or VMX already in use | Reboot. Verify Hyper-V is fully off. |
| `VMXON failed: 2` | CR0/CR4 fixed bits mismatch | Check `bcdedit /enum` — Hyper-V may be setting reserved bits. |
| Driver won't load (signing error) | Test certificate not trusted | Run `certutil -addstore Root hypersinv.cer`. |
| `SCM error 1275` | Driver blocked by HVCI/Memory Integrity | Disable Memory Integrity in Windows Security → Device Security → Core Isolation. |
| BSOD `SYSTEM_SERVICE_EXCEPTION` on probe | VMCS field encoding mismatch or unaligned VMXON region | Check VMX revision from CPUID — must match probe output. |

The preceding probe/load/status/unload steps and output are legacy guidance; they are separate from Runtime V2 and do not validate the launch-probe protocol.

## Runtime V2 launch-probe operator guide (R17)

This section covers the transient launch/shutdown protocol, not the persistent
hypervisor load path. Run it only on a disposable, snapshotted test VM.

### Established commands

From the directory containing the signed, deployed loader/driver pair, run one probe and then clean up the
retained service registration:

```cmd
hypervisor_loader.exe launch-probe
hypervisor_loader.exe unload
```

`launch-probe` creates/starts the configured `hypersinv` service and intentionally
leaves it installed (and potentially running). The service may remain running after the launch-probe IOCTL; this is expected,
abort only if a stale, unverified, or otherwise unexpected service was already
running before IOCTL. Always use `unload` before replacing the image or
starting another probe.
Do not use a failed probe as a reason to load the persistent hypervisor.

### CET-on rejection is a pass

When CR4.CET is active (not merely CPUID-exposed), the expected, safe result is
a rejection before any VMX instruction. CPUID exposure alone is not a rejection
condition:

| Field | Required value |
| --- | --- |
| Domain status | `REJECTED` (`2`) |
| NTSTATUS | `0xC00000BB` (`STATUS_NOT_SUPPORTED`) |
| Runtime stage | `CET_PREFLIGHT` (`4`) |
| VMX instruction attempted | No |
| Cleanup requirement | `NONE` |
| Process exit code | `0` |

Record the complete diagnostic response. Do not reinterpret this expected
rejection as a transport failure, and do not automatically disable CET or retry.

### CET-off testing boundary

Only test with CET masked/off inside the disposable, snapshotted VM. Revert the
snapshot after the test. Never disable CET on a production host, and never make
the loader auto-disable CET or auto-retry after the CET preflight rejection.

### WinDbg-supervised checkpoints

For the required supervised one-run gate, capture each checkpoint and stop on
the first mismatch:

1. Probe `VMCALL`: exit reason `0x12`; signed registers are `RAX =
   0x4853505253564E56`, `RCX = 1`, and `RDX = ExpectedCookie`.
2. Shutdown `VMCALL`: the same signature, reason `0x12`, `RCX = 2`, and the
   identical cookie in `RDX`.
3. Teardown order: final `VMCLEAR` completes before `VMXOFF`; no `VMXOFF` or
   page release may precede that final clear.
4. On return, `CR0` and `CR4` equal their captured originals. Confirm
   `IRQL == PASSIVE_LEVEL`, the pinned processor/affinity is restored, and the
   resource ownership is fully released (`RESOURCES_RELEASED` flag set). The public `ResourceMask` is a high-water acquisition mask
   and may remain `0x3f` after release; do not require it to be zero.

Treat the signed response (`ProbeExit`/`ShutdownExit`) and the WinDbg trace as
one evidence set; a pretty-printed success line alone is insufficient.

### Escalation and abort rules

Escalate only after a clean gate: one WinDbg-supervised run, then 10 runs, then
100 runs, using the same signed image and the explicit `launch-probe` -> `unload`
pair. Do not retry automatically; abort the sequence immediately for a wrong
classification/stage/status, any VMX instruction failure, protocol mismatch,
missing final `VMCLEAR`, `CR0`/`CR4` mismatch, IRQL or affinity drift, resource ownership not released or `RESOURCES_RELEASED` absent, stale/unverified
or otherwise unexpected pre-existing running service before IOCTL, hang, crash, or bugcheck. Preserve the
first failing response and debugger evidence before any manual recovery.

### Service-image and unload caveat

The loader refreshes a stopped kernel-driver service image and refuses an
unverified already-running image. `unload` may continue SCM cleanup only after
confirmed VMX teardown. `0x80000001` is the project-defined
`ERR_DRIVER_NOT_LOADED`; it is benign **only when the loader classifies the
response as `NOT_LOADED`**. A raw `0x80000001`, transport failure, or unconfirmed
teardown is not permission to stop/delete the service; inspect the failure and
leave the service running when required.

### Evidence status

One signed, non-WinDbg run completed and remained stable for at least ten
minutes. The WinDbg-supervised one-run gate and the 10-run/100-run repetition
gates remain unverified.

## Gate 14: supervised redacted CR3 routing observation

Gate 14 is the next live boundary after the accepted Gate 13 EPT-violation
stress run. It is a **single supervised run** in the dedicated snapshotted VM;
do not turn it into a repetition gate yet. The guest must expose exactly two
processor groups with two active processors in each group.

Stage the matching signed artifacts and script in `C:\V3H`:

- `hypervisor_driver.sys` and `hypervisor_driver.pdb`
- `hypervisor_loader.exe` and `hypervisor_loader.pdb`
- `gate14-cr3-status-observation.ps1`

Configure WinDbg on the debugger host before running the guest script:

```text
.symfix
.sympath+ C:\V3H
.reload /f hypervisor_driver.sys
.logopen /t C:\V3H\evidence\windbg-gate14-cr3-status.log
ed nt!Kd_DEFAULT_Mask 0xF
g
```

Run the gate from elevated Windows PowerShell in the test VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate14-cr3-status-observation.ps1'
```

The script requires an absent `hypersinv` service, proves Runtime LOADED and EPT
READY, then performs up to 40 read-only `cr3-status` queries over 10 seconds.
The pointer-free wire must remain redacted, contain all four group-aware CPU
identities exactly once, and report internally consistent resident and
transition aggregates. PASS requires the `OBSERVED` class with at least one
CR3 transition, followed by canonical unload and confirmed service absence.
An `INACTIVE` snapshot is valid while polling but is not the Gate 14 pass.

After the script returns, stop the debugger log:

```text
.logclose
```

Preserve both the generated `gate14-cr3-status-observation-*.zip` and the WinDbg
log. On any mismatch, hang, crash, bugcheck, malformed/short status, exposed CR3
root, unload failure, or residual service, **do not retry**. Keep the VM at the
failure state until the evidence is copied. Do not run this gate on a host or a
non-disposable machine.

### Gate 14 one-shot CR3 delivery diagnostic

Repeated Gate 14 observations proved that all four VMCS instances received
primary processor controls with CPU_BASED_CR3_LOAD_EXITING set and an explicit
CR3 target count of zero, while passive process churn still produced no
authenticated transitions. The next bounded diagnostic is therefore an
operator-triggered same-value CR3 write, not another polling-only repetition.

The first one-shot attempt returned stage NONE before its initial snapshot because
the probe incorrectly consulted the retired legacy VMX state. Persistent LOAD does
not update that legacy state. The probe now treats the persistent owner snapshot as
the sole V3 runtime-state authority; repeat the same single-shot gate with a newly
built and signed matched artifact set.

The corrected state-authority attempt reached stage AFTER_CAPTURED, but its
transition count remained zero. That result proved the direct IOCTL CR3 write ran in
the resident VMX-root control path, not in VMX non-root guest execution. The probe
now reuses the authenticated pinned diagnostic resume boundary: the guest
continuation performs the CR3 self-write before its harmless completion write,
and the redacted before/after snapshots verify the routed exit.

The first guest-continuation build completed LOAD but hard-stalled immediately
after the operator script printed `CR3 STATUS BEFORE PROBE`. WinDbg could not
break in, its log ended after `g`, and the VMware monitor recorded guest-tools
heartbeat loss without a VMware panic or assertion. This proves a hard guest
execution failure, but it does not prove whether the request reached the CR3
status IOCTL. The suspended encrypted VM state could not be converted by the
installed `vmss2core` and therefore supplied no trustworthy stack.

The follow-up diagnostic makes the outer persistent-owner push lock fail BUSY
instead of waiting indefinitely and emits redacted `CR3_DIAG` boundaries around
the status IOCTL, owner lock, Runtime `OperationBusy`, and CR3 route snapshot.
No marker includes a CR3 value, root, address, or private pointer. The next run
must enable the IHVDRIVER debugger mask and preserve the last emitted boundary;
it is still a one-shot diagnostic, not a repetition gate.

The fail-safe rerun proved both the external status query and the probe's internal
before-snapshot completed. The hard stall began only after diagnostic guest resume.
The VM-exit GPR capture then overwrote the borrowed target stored in `GuestGprs.Rax`
with the CR3 self-write source before the guest used it. The corrected private ABI
uses a dedicated complemented `DiagnosticTarget` field, validates it in guest mode,
and no longer aliases diagnostic payload state with the mutable VM-exit GPR frame.
The private launch ABI is version 2 and 768 bytes; all matched artifacts must be
rebuilt together before the next one-shot rerun.

Stage the matching signed driver/loader pair and
gate14-cr3-probe-diagnostic.ps1 in C:\V3H. The script performs exactly one
cr3-probe between redacted before/after status snapshots, then unloads and
archives evidence. The fixed 0x80C response publishes only processor identity
and aggregate transition counts; it never publishes a CR3 value.

Run only from elevated PowerShell in the disposable snapshotted VM with:

    Set-ExecutionPolicy -Scope Process Bypass -Force
    & 'C:\V3H\gate14-cr3-probe-diagnostic.ps1'

PASS requires the probe to return stage COMPLETE, an increasing aggregate
transition count, an OBSERVED post-probe status, canonical unload, and service
absence. On a hang, crash, bugcheck, non-increasing count, unload failure, or
residual service, do not retry or reset before preserving the evidence and
WinDbg log.

The target-stability rerun on 2026-08-21 is the accepted Gate 14 result. Its
summary reported `Result=PASS`, `ProbeStage=COMPLETE`, transitions increasing
from 0 to 1, and `Delta=1`. The post-probe status was `OBSERVED`; Runtime/EPT
LOAD, canonical UNLOAD, and final service absence all completed without a
bugcheck, crash, or hang.

## Gate 15: 10-cycle CR3-load VM-exit probe

Gate 15 is the next live boundary after the accepted Gate 14 one-shot. It
repeats the unchanged Gate 14 transaction exactly ten times in child PowerShell
processes. Every cycle requires service absence before LOAD, a successful Gate
14 process, exact `Result=PASS` and `ProbeStage=COMPLETE` evidence, a positive
and internally consistent transition delta, canonical unload, and service
absence afterward.

Use the same matched signed driver, PDBs, loader, and Gate 14 script. Add
`gate15-cr3-probe-10cycles.ps1` to `C:\V3H`. Attach WinDbg and open a fresh log:

```text
.symfix
.sympath+ C:\V3H
.reload /f hypervisor_driver.sys
ed nt!Kd_IHVDRIVER_Mask 0xf
.logopen /t C:\V3H\evidence\windbg-gate15-cr3-probe-10cycles.log
g
```

From elevated Windows PowerShell in the clean, checkpointed test VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate15-cr3-probe-10cycles.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
```

PASS requires `10/10`, `PassedCycles=10`, ten complete transition proofs, final
service absence, and the final
`PASS: all 10 CR3-load VM-exit probe cycles completed.` line. On the first
mismatch, hang, crash, bugcheck, unload failure, or residual service, do not
retry LOAD, do not run UNLOAD manually, and do not reset or reboot until the
outer evidence archive and closed WinDbg log are copied off the VM.

The 2026-08-21 Gate 15 archive is accepted 10-cycle evidence. All ten cycles
reported `Result=PASS`, `ProbeStage=COMPLETE`, and the exact transition sequence
`Before=0;After=1;Delta=1`. The archive contained ten successful LOAD proofs,
ten successful CR3-probe proofs, ten canonical UNLOAD proofs, twenty wrapper
service-absence checks, and zero nonempty stderr files. WinDbg recorded twenty
successful 192-byte CR3 status completions, no bugcheck or BUSY status, an empty
final driver-module query, and a normally closed log.

## Gate 16: 100-cycle CR3-load VM-exit probe

Gate 16 is the release-quality repetition boundary after the accepted Gate 15
`10/10` result. It repeats the unchanged Gate 14 transaction exactly one hundred
times. Every cycle requires service absence before LOAD, a successful Gate 14
child process, exact `Result=PASS` and `ProbeStage=COMPLETE` evidence, a positive
and internally consistent transition delta, canonical unload, and service
absence afterward.

Reuse the accepted signed Gate 15 driver, matching PDBs, loader, and Gate 14
script. Add `gate16-cr3-probe-100cycles.ps1` to `C:\V3H`. Attach WinDbg and open
a fresh log:

```text
.symfix
.sympath+ C:\V3H
.reload /f hypervisor_driver.sys
ed nt!Kd_IHVDRIVER_Mask 0xf
.logopen /t C:\V3H\evidence\windbg-gate16-cr3-probe-100cycles.log
g
```

From elevated Windows PowerShell in the clean, checkpointed test VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate16-cr3-probe-100cycles.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
```

PASS requires `100/100`, `PassedCycles=100`, one hundred complete transition
proofs, final service absence, and the final
`PASS: all 100 CR3-load VM-exit probe cycles completed.` line. On the first
mismatch, hang, crash, bugcheck, unload failure, or residual service, do not
retry LOAD, do not run UNLOAD manually, and do not reset or reboot until the
outer evidence archive and closed WinDbg log are copied off the VM.

The 2026-08-21 Gate 16 diagnostic archive is accepted 100-cycle evidence. All
one hundred cycles reported `Result=PASS`, `ProbeStage=COMPLETE`, and the exact
per-cycle transition sequence `Before=0;After=1;Delta=1`. WinDbg recorded one
hundred successful guest CR3 self-writes, one hundred successful resume
boundaries, and one hundred zero-status route boundaries with no bugcheck,
fatal VM-exit, BUSY status, or retained driver module. This proves delivery and
the configured target's **no-match** route only; it does not prove positive
target residency.

## Gate 17: positive CR3 target-residency roundtrip

Gate 17 is the next live boundary after accepted Gate 16. It is one supervised
run, not a repetition gate. The fixed `0x80D` diagnostic first establishes a
caller-process nonresident baseline on system index zero. A System-process
worker then executes the authenticated guest CR3 boundary under an outer CPU-zero
group-affinity guard and captures the redacted state before restoring affinity or
signaling completion. This prevents a scheduler switch from erasing the positive
observation. A final caller-process boundary
must prove the leave transition.

Stage one newly built, matched artifact set in `C:\V3H`:

- signed `hypervisor_driver.sys` and matching `hypervisor_driver.pdb`
- `hypervisor_loader.exe` and matching `hypervisor_loader.pdb`
- `gate17-cr3-residency-roundtrip.ps1`

On the debugger host, attach WinDbg to the clean checkpointed VM and run:

```text
.symfix
.sympath+ C:\V3H
.reload /f hypervisor_driver.sys
ed nt!Kd_IHVDRIVER_Mask 0xf
.logopen /t C:\V3H\evidence\windbg-gate17-cr3-residency.log
g
```

From elevated Windows PowerShell inside the VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate17-cr3-residency-roundtrip.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
```

PASS requires:

- an exact pre-probe `INACTIVE` baseline with one configured target, zero
  residents, and zero transitions
- stage `COMPLETE` (`7`) and status `0x00000000`
- strictly increasing CPU-zero transition counters
- exact residency `0/1/0`
- exact matched indices `4294967295/0/4294967295`
- exact authenticated completion writes `1/1/1`
- a final redacted status with `targets=1` and `residents=0`
- canonical unload and confirmed service absence

After the script returns, break into WinDbg, run `lm m hypervisor_driver`, then
close the log with `.logclose`. Preserve the generated
`gate17-cr3-residency-roundtrip-*.zip` and WinDbg log. On any hang, crash,
bugcheck, mismatch, unload failure, or residual service, do not retry, reset, or
reboot before copying both evidence files.

The 2026-08-22 rearm rerun is the accepted Gate 17 result. It proved exact
transitions 1/2/3, residency 0/1/0, matched indices
4294967295/0/4294967295, completion writes 1/1/1, final OBSERVED status
with zero residents, canonical unload, and service absence. WinDbg recorded
three successful diagnostic resumes, the exact final residency route, no
bugcheck or fatal VM-exit, and no retained driver module.

## Gate 18: 10-cycle CR3 target-residency roundtrip

Gate 18 is the next live boundary after the accepted Gate 17 one-shot. It
repeats the unchanged Gate 17 positive target-residency transaction exactly ten
times in child Windows PowerShell processes. Each cycle independently requires
the 2-group/4-processor topology, valid kernel dump configuration, service
absence before LOAD, the complete exact Gate 17 proof, canonical unload,
service absence afterward, no new crash/application-failure event, and no new
memory dump.

Reuse the accepted signed e1ceafe driver and loader with their matching PDBs.
Place both gate17-cr3-residency-roundtrip.ps1 and
gate18-cr3-residency-roundtrip-10cycles.ps1 in C:\V3H.

On the debugger host, attach WinDbg and open a fresh log:

    .symfix
    .sympath+ C:\V3H
    .reload /f hypervisor_driver.sys
    ed nt!Kd_IHVDRIVER_Mask 0xf
    .logopen /t C:\V3H\evidence\windbg-gate18-cr3-residency-10cycles.log
    g

Do not set software breakpoints in the VMX/guest interval. From elevated
Windows PowerShell in the clean checkpointed VM:

    Set-ExecutionPolicy -Scope Process Bypass -Force
    & 'C:\V3H\gate18-cr3-residency-roundtrip-10cycles.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'

PASS requires Result=PASS, PassedCycles=10, ten exact transition proofs
1/2/3, ten residency proofs 0/1/0, ten matched-index proofs
4294967295/0/4294967295, ten completion-write proofs 1/1/1, and the final
PASS: all 10 CR3 target-residency roundtrip cycles completed. line.

On the first mismatch, hang, crash, bugcheck, unload failure, or residual
service, do not retry LOAD, do not run UNLOAD manually, and do not reset or
reboot before copying the outer evidence archive and closed WinDbg log.

The 2026-08-22 Gate 18 archive is accepted ten-cycle evidence. Every cycle used
the exact accepted signed e1ceafe artifact set and proved transitions 1/2/3,
residency 0/1/0, matched indices 4294967295/0/4294967295, completion writes
1/1/1, canonical unload, and service absence. The outer evidence contained no
failure events or nonempty runner stderr. WinDbg recorded ten exact stage-7
routes and thirty authenticated resumes with no bugcheck, fatal exit, BUSY
status, or retained driver module.

## Gate 19: 100-cycle CR3 target-residency roundtrip

Gate 19 is the release-quality repetition boundary after accepted Gate 18. It
repeats the unchanged accepted Gate 17 positive-residency transaction exactly
one hundred times. Each cycle requires the same 2-group/4-processor topology,
kernel dump configuration, service-absence, exact transition/residency/match/
write proof, event, memory-dump, transcript, and archive gates as Gate 18.

Reuse the accepted signed e1ceafe driver and loader with their matching PDBs.
Place gate17-cr3-residency-roundtrip.ps1 and
gate19-cr3-residency-roundtrip-100cycles.ps1 in C:\V3H.

Attach WinDbg and open a fresh log:

    .symfix
    .sympath+ C:\V3H
    .reload /f hypervisor_driver.sys
    ed nt!Kd_IHVDRIVER_Mask 0xf
    .logopen /t C:\V3H\evidence\windbg-gate19-cr3-residency-100cycles.log
    g

Do not set software breakpoints in the VMX/guest interval. From elevated
Windows PowerShell in the clean checkpointed VM:

    Set-ExecutionPolicy -Scope Process Bypass -Force
    & 'C:\V3H\gate19-cr3-residency-roundtrip-100cycles.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'

PASS requires Result=PASS, PassedCycles=100, one hundred exact transition
proofs 1/2/3, residency proofs 0/1/0, matched-index proofs
4294967295/0/4294967295, completion-write proofs 1/1/1, and the final
PASS: all 100 CR3 target-residency roundtrip cycles completed. line.

The initial 2026-08-22 Gate 19 run stopped after 27 completed cycles because
the outer wrapper overclassified informational Kernel-Power session events and
Windows Error Reporting submissions for older failures. Cycle 28 itself
completed the exact Gate 17 proof and detached cleanly. The corrected wrapper
rejects current Kernel-Power bugchecks, serious WHEA events, and loader or
PowerShell Application Error crashes while ignoring unrelated informational
queue activity.

The 2026-08-22 corrected Gate 19 rerun is accepted release-quality evidence.
All one hundred cycles proved the exact transition sequence 1/2/3, residency
0/1/0, matched indices 4294967295/0/4294967295, completion writes 1/1/1,
canonical unload, service absence, no failure events, and no new dump. WinDbg
recorded three hundred exact CR3 probe completions, one hundred stage-7
residency routes, no bugcheck, fatal exit, or BUSY result, and no retained
driver module.

On the first mismatch, hang, crash, bugcheck, unload failure, or residual
service, do not retry LOAD, do not run UNLOAD manually, and do not reset or
reboot before copying the outer evidence archive and closed WinDbg log.

## Gate 20: persistent CPUID transparency

Gate 20 is the next live boundary after accepted Gate 19. It proves that the
production persistent dispatcher handles the unconditional CPUID VM exit,
advances guest RIP transactionally, resumes the guest, and applies the existing
transparency policy. This is one supervised run, not a repetition gate.

The first Gate 20 attempt on 2026-08-22 is not acceptance evidence. LOAD and
UNLOAD were clean, but `cpuid-probe` executed CPUID directly in user mode after
the synthetic guest had returned to the native VMX-root host. Leaf 1 therefore
retained ECX bit 31 and the probe correctly failed without exercising exit
reason 10. The corrected command uses IOCTL `0x80E` to execute all three CPUID
instructions inside an authenticated VMX non-root diagnostic continuation and
returns only the committed dispatcher results.

The first corrected non-root rerun on 2026-08-22 is also not acceptance
evidence. LOAD and UNLOAD remained clean, but the command returned public
`NOT_READY` before VMRESUME. WinDbg recorded `compose ready=0 state=3` and a
zero completion value. State `3` is the authenticated CPUID-armed state; the
composition resume gate still admitted only the older CR3-armed state. The
corrected gate now authenticates both exact state/contract pairs, preserves the
shared target checks, and classifies either armed state as a diagnostic resume.
A fresh 328-target WDK/MSVC/MASM build and full CTest 200/200 passed. The next
run remains one supervised Gate 20 rerun; no repetition gate is authorized yet.

Stage one newly built, matched artifact set in `C:\V3H`:

- signed `hypervisor_driver.sys` and matching `hypervisor_driver.pdb`
- `hypervisor_loader.exe` and matching `hypervisor_loader.pdb`
- `gate20-cpuid-transparency.ps1`

On the debugger host, attach WinDbg to the clean checkpointed VM and run:

```text
.symfix
.sympath+ C:\V3H
.reload /f hypervisor_driver.sys
ed nt!Kd_IHVDRIVER_Mask 0xf
.logopen /t C:\V3H\evidence\windbg-gate20-cpuid-transparency.log
g
```

Do not set software breakpoints in the VMX/guest interval. From elevated
Windows PowerShell inside the VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate20-cpuid-transparency.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
```

PASS requires loader output `INFO: Authenticated non-root CPUID guest
roundtrip completed.`, the exact summary values `Leaf1Hypervisor=0`,
`HypervisorLeaf=0/0/0/0`, and `Leaf7Cet=0/0`, plus canonical unload,
`ServiceAfterUnload=ABSENT`, no current serious Kernel-Power/WHEA or loader/
PowerShell Application Error event, and no new memory dump. WinDbg should also
record `hypersinv: CPUID_PROBE boundary status=0 completion=0x43505549`.

After the script returns, break into WinDbg, run `lm m hypervisor_driver`, then
close the log with `.logclose`. Preserve the generated
`gate20-cpuid-transparency-*.zip` and the closed WinDbg log. On any hang,
crash, bugcheck, mismatch, unload failure, or residual service, do not retry,
reset, or reboot before copying both evidence files.

The 2026-08-22 23:50 corrected Gate 20 rerun is accepted single-cycle evidence.
The summary proves `Leaf1Hypervisor=0`, `HypervisorLeaf=0/0/0/0`,
`Leaf7Cet=0/0`, and `ServiceAfterUnload=ABSENT`. The loader recorded exact
stage `2` and the authenticated non-root completion marker. WinDbg recorded
`DIAGNOSTIC compose ready=1 state=3 refresh=3`, completion
`0x43505549`, no retained driver module, and only the operator-requested break.

## Gate 21: 10-cycle persistent CPUID transparency

Gate 21 is the next live boundary after accepted Gate 20. It repeats the
unchanged accepted Gate 20 transaction exactly ten times in child Windows
PowerShell processes. Each cycle independently requires the two-group/four-
processor topology, valid kernel dump configuration, service absence before
LOAD, exact authenticated CPUID stage/result proof, canonical unload, service
absence afterward, no current serious Kernel-Power/WHEA or loader/PowerShell
Application Error event, and no new memory dump.

The corrected 2026-08-23 Gate 21 rerun is accepted. All ten cycles proved
`Leaf1Hypervisor=0`, `HypervisorLeaf=0/0/0/0`, `Leaf7Cet=0/0`, authenticated
stage `2`, completion `0x43505549`, canonical unload, and service absence.
The accepted signed driver is from `d25055a`, with its matching PDB and loader
artifacts.

On the debugger host, attach WinDbg and open a fresh log:

```text
.symfix
.sympath+ C:\V3H
.reload /f hypervisor_driver.sys
ed nt!Kd_IHVDRIVER_Mask 0xf
.logopen /t C:\V3H\evidence\windbg-gate21-cpuid-transparency-10cycles.log
g
```

Do not set software breakpoints in the VMX/guest interval. From elevated
Windows PowerShell in the clean checkpointed VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate21-cpuid-transparency-10cycles.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
```

PASS requires `Result=PASS`, `PassedCycles=10`, ten exact per-cycle summary
proofs for `Leaf1Hypervisor=0`, `HypervisorLeaf=0/0/0/0`, and
`Leaf7Cet=0/0`, ten exact `status=0x00000000 stage=2` probe results, ten
canonical unloads, and the final `PASS: all 10 persistent CPUID transparency
cycles completed.` line. WinDbg must record ten completion values
`0x43505549`, with no bugcheck, fatal VM-exit, fail-closed compose result, or
retained module.

On the first mismatch, hang, crash, bugcheck, unload failure, or residual
service, do not retry LOAD, do not run UNLOAD manually, and do not reset or
reboot before copying the outer evidence archive and closed WinDbg log.
## Gate 22: 100-cycle persistent CPUID transparency

Gate 22 is the next live boundary after accepted Gate 21. It repeats the
unchanged accepted Gate 20 transaction exactly one hundred times, stopping at
the first mismatch. Reuse the accepted signed `d25055a` driver, matching PDB
and loader artifacts. Place `gate20-cpuid-transparency.ps1` and
`gate22-cpuid-transparency-100cycles.ps1` in `C:\V3H`.

On the debugger host, attach WinDbg, enable the default and IHV driver print
masks, open a fresh log, and continue the target. Do not set software
breakpoints in the VMX/guest interval.

From elevated Windows PowerShell in the clean checkpointed VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate22-cpuid-transparency-100cycles.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
```

PASS requires `Result=PASS`, `PassedCycles=100`, one hundred exact CPUID
transparency proofs, one hundred canonical unloads, final service absence, and
the final `PASS: all 100 persistent CPUID transparency cycles completed.`
line. WinDbg must record one hundred completion values `0x43505549` with no
bugcheck, fatal VM-exit, fail-closed compose result, or retained module.

On the first mismatch, hang, crash, bugcheck, unload failure, or residual
service, do not retry LOAD, do not run UNLOAD manually, and do not reset or
reboot before copying the outer evidence archive and closed WinDbg log.

The 2026-08-23 Gate 22 run is accepted. The archive SHA-256 is
`EED170C8EF75E28C48CE4A7C40895207349E16EAD01150B21A952BCBC25D7EAE`;
the closed WinDbg log SHA-256 is
`C1A3D348AD42D4409619F58F3D7CCC9AD95425D8D9467B6A7E0A5BE67739DB7B`.
All one hundred cycles proved the exact transparency result, canonical unload,
and service absence. WinDbg recorded one hundred exact completion values
`0x43505549`, no bugcheck, fatal VM-exit, or BUSY result, and no retained
driver module.

## Gate 23: persistent policy-owned RDMSR transparency

Gate 23 is the next live boundary after accepted Gate 22. It is one supervised
run. The authenticated synthetic non-root continuation executes exactly three
RDMSR instructions. The persistent dispatcher must return the policy-owned
values IA32_FEATURE_CONTROL=1, IA32_VMX_BASIC=0, and IA32_U_CET=0, advance
guest RIP for each exit reason 31, and resume the guest. Unsupported/native
pass-through MSRs remain fail-closed and are not part of this live probe.

Stage one newly built matched artifact set in `C:\V3H`:

- signed `hypervisor_driver.sys` and matching `hypervisor_driver.pdb`
- `hypervisor_loader.exe` and matching `hypervisor_loader.pdb`
- `gate23-msr-read-transparency.ps1`

On the debugger host, attach WinDbg to the clean checkpointed VM and run:

```text
.symfix
.sympath+ C:\V3H
.reload /f hypervisor_driver.sys
ed nt!Kd_IHVDRIVER_Mask 0xf
.logopen /t C:\V3H\evidence\windbg-gate23-msr-read-transparency.log
g
```

Do not set software breakpoints in the VMX/guest interval. From elevated
Windows PowerShell inside the VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate23-msr-read-transparency.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
```

PASS requires loader output `MSR Read Probe: status=0x00000000 stage=2`,
`INFO: Authenticated non-root RDMSR guest roundtrip completed.`, exact summary
values `FeatureControl=1`, `VmxBasic=0`, and `UserCet=0`, canonical
unload, `ServiceAfterUnload=ABSENT`, no current serious Kernel-Power/WHEA or
loader/PowerShell Application Error event, and no new memory dump. WinDbg must
record `hypersinv: MSR_READ_PROBE boundary status=0 completion=0x4D535231`.

After the script returns, break into WinDbg, run `lm m hypervisor_driver`,
then close the log with `.logclose`. Preserve the generated
`gate23-msr-read-transparency-*.zip` and the closed WinDbg log. On any hang,
crash, bugcheck, mismatch, unload failure, or residual service, do not retry,
reset, or reboot before copying both evidence files.

The 2026-08-23 Gate 23 run is accepted single-cycle evidence. The archive
SHA-256 is
`FC08B356D9CD1B39A2182EC252A3FCD4A2B103056EBF449A5794417370D70367`;
the closed WinDbg log SHA-256 is
`6CCF9C7953D04AD3B8E9F4DBA9627CAA206DE6A0EBD44FAF1B1A3FF7677E07F9`.
The summary proved `FeatureControl=1`, `VmxBasic=0`, `UserCet=0`, and
`ServiceAfterUnload=ABSENT`. WinDbg recorded authenticated diagnostic state
5, exact completion `0x4D535231`, no bugcheck, fatal VM-exit, fail-closed
compose result, or retained driver module.

## Gate 24: 10-cycle persistent MSR read transparency

Gate 24 is the next live boundary after accepted Gate 23. It repeats the
unchanged accepted Gate 23 transaction exactly ten times in child Windows
PowerShell processes. Each cycle independently requires the two-group/four-
processor topology, valid kernel dump configuration, service absence before
LOAD, exact authenticated RDMSR stage/result proof, canonical unload, service
absence afterward, no current serious Kernel-Power/WHEA or loader/PowerShell
Application Error event, and no new memory dump.

Reuse the accepted Gate 23 signed driver, matching PDB, loader artifacts, and
`gate23-msr-read-transparency.ps1`. Place
`gate24-msr-read-transparency-10cycles.ps1` in `C:\V3H`.

On the debugger host, attach WinDbg and open a fresh log:

```text
.symfix
.sympath+ C:\V3H
.reload /f hypervisor_driver.sys
ed nt!Kd_DEFAULT_Mask 0xf
ed nt!Kd_IHVDRIVER_Mask 0xf
.logopen /t C:\V3H\evidence\windbg-gate24-msr-read-transparency-10cycles.log
g
```

Do not set software breakpoints in the VMX/guest interval. From elevated
Windows PowerShell in the clean checkpointed VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& 'C:\V3H\gate24-msr-read-transparency-10cycles.ps1' -Stage 'C:\V3H' -EvidenceRoot 'C:\V3H\evidence'
```

PASS requires `Result=PASS`, `PassedCycles=10`, ten exact per-cycle summary
proofs for `FeatureControl=1`, `VmxBasic=0`, and `UserCet=0`, ten exact
`status=0x00000000 stage=2` probe results, ten canonical unloads, and the
final `PASS: all 10 persistent MSR read transparency cycles completed.` line.
WinDbg must record ten completion values `0x4D535231`, with no bugcheck, fatal
VM-exit, fail-closed compose result, or retained module.

On the first mismatch, hang, crash, bugcheck, unload failure, or residual
service, do not retry LOAD, do not run UNLOAD manually, and do not reset or
reboot before copying the outer evidence archive and closed WinDbg log.

## Gate 25: 100-cycle persistent MSR read transparency

The 2026-08-27 Gate 25 run is accepted. The archive SHA-256 is
`2EF254C5AF3511B9ACBD1AC24EF41A3D56789C41D9B0E41AA6FB3D0E09F99511`;
the closed WinDbg log SHA-256 is
`2E27C0CD0E8A8BBBD0780FC7F576881C06014CBEAEC8CA0267A0D0343D73AB8C`.
All one hundred cycles proved exact policy-owned RDMSR values, canonical
unload, and service absence. WinDbg recorded one hundred exact completion
values `0x4D535231` with no bugcheck or fatal VM-exit.

## Gate 26: supervised MSR read/write transparency

Gate 26 is the next live boundary because it introduces a genuinely new
VM-exit path: exit reason 32 (WRMSR). The authenticated guest writes the
nonzero sentinel `1` to IA32_U_CET. The persistent dispatcher must discard
the write, advance guest RIP, preserve every guest GPR, and then return zero
for the existing IA32_U_CET RDMSR observation. At the accepted Gate 26
revision, the control and VMCS plans failed closed if MSR bitmaps were enabled,
so the physical WRMSR could not execute.

Run Gate 26 only once under WinDbg. Gate 25 already proved the unchanged
load/probe/unload lifecycle over one hundred cycles; repeating Gate 26 at
10 and 100 cycles would not cover another boundary and is therefore not
required unless this supervised run exposes instability.

Stage one newly built matched artifact set in `C:\V3H`:

- signed `hypervisor_driver.sys` and matching `hypervisor_driver.pdb`
- `hypervisor_loader.exe` and matching `hypervisor_loader.pdb`
- `gate26-msr-transparency.ps1`

PASS requires `Result=PASS`, `Probe=MSR_READ_WRITE_TRANSPARENCY`,
`FeatureControl=1`, `VmxBasic=0`, `UserCet=0`, `WriteDiscard=1`, canonical
unload, and `ServiceAfterUnload=ABSENT`. WinDbg must record exactly one
`hypersinv: MSR_TRANSPARENCY_PROBE boundary status=0
completion=0x4D535231` line, with no bugcheck, fatal VM-exit, or retained
driver module.

The 2026-08-28 Gate 26 run is accepted single-cycle evidence. The archive
SHA-256 is
`E5811FB293E1C0927B1C63B8293ED8303E59EBD9D277D80F69EEF9AE045B2AB8`;
the closed WinDbg log SHA-256 is
`F7F05F7108EE7494303F0C92D13DD22591CA2919E6456104E284A5C51AEBC234`.
The summary proved `FeatureControl=1`, `VmxBasic=0`, `UserCet=0`,
`WriteDiscard=1`, and `ServiceAfterUnload=ABSENT`. WinDbg recorded exactly
one authenticated completion `0x4D535231`, no bugcheck, fatal VM-exit, or
fail-closed result, and no retained driver module. Gate 25 already supplies
the accepted 100-cycle lifecycle evidence, so no Gate 26 repetition is
required.

## Gate 27: supervised selective MSR-bitmap residency

Gate 27 is the next live boundary because the persistent runtime now allocates
one aligned MSR-bitmap page per processor, enables `CPU_BASED_USE_MSR_BITMAPS`,
and writes each page's physical address to `VMCS_CTRL_MSR_BITMAP_ADDR`.
Policy-owned Feature Control, VMX-capability, and CET reads/writes must still
VM-exit, while ordinary addressable MSRs now execute natively without forcing
every guest RDMSR/WRMSR through the diagnostic dispatcher.

Run Gate 27 only once under WinDbg. Gate 25 already supplies accepted
100-cycle MSR lifecycle evidence. Repetition is not required unless this new
control/backing boundary exposes instability.

Stage one matched artifact set in `C:\V3H`:

- signed `hypervisor_driver.sys` and matching `hypervisor_driver.pdb`
- `hypervisor_loader.exe` and matching `hypervisor_loader.pdb`
- `gate27-msr-bitmap-residency.ps1`

Do not set software breakpoints in the VMX/guest interval. PASS requires
`Result=PASS`, `Probe=MSR_BITMAP_RESIDENCY`, `ContextResources=63`,
`FeatureControl=1`, `VmxBasic=0`, `UserCet=0`, `WriteDiscard=1`, canonical
unload, and `ServiceAfterUnload=ABSENT`. On any mismatch, hang, bugcheck,
unload failure, or retained service, do not retry LOAD or run UNLOAD manually;
preserve the VM and WinDbg state for investigation.

The 2026-08-28 Gate 27 run is accepted single-cycle evidence. The archive
SHA-256 is
`9D2908234C2C99AEA011A4EB4B32C93583596DE83E2E861C3DD3E70148ADA338`;
the closed WinDbg log SHA-256 is
`6B8741336E896B87ABC14F706C010DA00FAC3626EB1C2020156595D8F175081C`.
The summary proved `ContextResources=63`, exact policy-owned read values,
WRMSR discard, canonical unload, and `ServiceAfterUnload=ABSENT`. WinDbg
recorded the authenticated compose/resume state and exact completion
`0x4D535231`, with no bugcheck, fatal VM-exit, or retained driver module.
Gate 25 already supplies accepted 100-cycle MSR lifecycle evidence, so Gate 27
does not require repetition.

## Gate 28: supervised native MSR pass-through

Gate 28 is the next necessary live boundary because Gate 27 proved that
policy-owned MSRs still exit through the selective bitmap, but it did not prove
that an ordinary addressable MSR executes natively. The authenticated guest now
reads `IA32_EFER` after the policy-owned MSR sequence. Its high-range bitmap bit
must remain clear. If the access exits unexpectedly, the dispatcher rejects the
unsupported route and the transaction fails closed; successful completion
therefore proves the intended no-exit path.

Run Gate 28 only once under WinDbg. Existing Gate 25 stress evidence already
covers the unchanged lifecycle. Stage one newly built matched artifact set and
`gate28-msr-native-pass-through.ps1` in `C:\V3H`. Do not set software
breakpoints in the VMX/guest interval.

PASS requires `Result=PASS`, `Probe=MSR_NATIVE_PASS_THROUGH`,
`ContextResources=63`, exact policy-owned values, `WriteDiscard=1`, a captured
`NativeEfer` whose LME and LMA bits are both set, `NativeEferLongMode=1`,
canonical unload, and `ServiceAfterUnload=ABSENT`. On any mismatch, hang,
bugcheck, unload failure, or retained service, do not retry LOAD or run UNLOAD
manually; preserve the VM and WinDbg state for investigation.

The 2026-08-28 Gate 28 run is accepted single-cycle evidence for signed commit
`2c99a21`. The archive SHA-256 is
`FB43B1A4FE55C846D0CE1AC72470B633EB1F5AC4A9FFE4DC70119CA59F719655`;
the closed WinDbg log SHA-256 is
`6B203F7D9C7BFAFE75420E4119327FF18FC0B312803DF5720EF799BBBA33B5FE`.
The staged driver hash was
`B4A45B7EA1A8CD502D19DB71890043FC7DC9D25EEAC9BA5A7221E5F8D48BFDEA`.
The summary proved `NativeEfer=0x0000000000000D01`, LME and LMA set,
the exact policy-owned values, canonical unload, and
`ServiceAfterUnload=ABSENT`. WinDbg recorded exactly one authenticated
completion `0x4D535231`, no bugcheck or fail-closed result, and an empty
`lm m hypervisor_driver` result after unload. Gate 28 does not require
repetition.

## Gate 29: supervised native MSR write pass-through

Gate 29 is the next necessary live boundary because the MSR bitmap has
independent read and write quadrants. Gate 28 proved the high-range read bit for
`IA32_EFER` is clear, but it cannot prove the corresponding write bit is clear.
The authenticated guest now reads `IA32_EFER`, writes back that exact value,
then reads it again. Any unexpected exit reaches an unsupported dispatcher
route and fails closed; any architectural change fails the before/after check.

Run Gate 29 only once under WinDbg. The same-value IA32_EFER write introduces
the only new live behavior; Gate 25 already supplies 100-cycle lifecycle
evidence. Stage one newly built matched artifact set and
`gate29-msr-native-write-pass-through.ps1` in `C:\V3H`. Do not set software
breakpoints in the VMX/guest interval.

PASS requires `Result=PASS`, `Probe=MSR_NATIVE_WRITE_PASS_THROUGH`,
`ContextResources=63`, exact policy-owned values, `WriteDiscard=1`, equal
`NativeEferBefore` and `NativeEferAfter` values with LME and LMA set,
`NativeEferLongMode=1`, `NativeEferStable=1`, canonical unload, and
`ServiceAfterUnload=ABSENT`. On any mismatch, hang, bugcheck, unload failure,
or retained service, do not retry LOAD or run UNLOAD manually; preserve the VM
and WinDbg state for investigation.

The 2026-08-28 Gate 29 run is accepted single-cycle evidence for signed commit
`1bddc0e`. The archive SHA-256 is
`544E9ED1082F9CB64F79A950EC75D84A50E7F24D0E995559C615A34CCF5016EF`;
the closed WinDbg log SHA-256 is
`673608770FDC8C85B91315DC734BAC9C97DCE4133E92A4A372B20396B638AEA8`.
The staged signed-driver SHA-256 was
`3B22934153C425A8902B6DCEE1AEC3848DD82BDA221682FC866987C46F8E7655`.
The summary proved equal `NativeEferBefore` and `NativeEferAfter` values of
`0x0000000000000D01`, long-mode bits set, clean unload, and service absence.
WinDbg recorded exactly one authenticated completion `0x4D535231`, no
bugcheck or fail-closed result, and no retained driver module. Gate 29 does
not require repetition.

## Gate 30: supervised native RDTSC transparency

Gate 30 is the next necessary live boundary because the persistent control
policy statically forbids RDTSC exiting, TSC offsetting, and TSC scaling, but
no live authenticated guest has yet proved that configuration. The driver
executes one guest `RDTSC`, reads back the live primary and secondary VMCS
controls plus the TSC-offset field, and requires all three timestamp
virtualization mechanisms to be disabled. An unexpected RDTSC exit is
unsupported by the persistent dispatcher and fails closed.

Root-mode timestamps still bracket the guest read, but that comparison is
diagnostic rather than an acceptance boundary. Under nested virtualization,
the outer hypervisor owns the relationship between the VMX-root and non-root
TSC observations. Live VMCS readback directly proves the controls owned by
this runtime without assuming both nested levels expose one clock domain.

Run Gate 30 only once under WinDbg. Earlier 100-cycle gates already cover the
unchanged lifecycle; repeating this diagnostic would not cross another
hardware boundary. Stage one newly built matched artifact set and
`gate30-tsc-native-pass-through.ps1` in `C:\V3H`. Do not set software
breakpoints in the VMX/guest interval.

PASS requires `Result=PASS`, `Probe=TSC_NATIVE_PASS_THROUGH`, a completed guest
roundtrip, nonzero root/guest observations, `RdtscExit=0`, `TscOffsetting=0`,
`TscScaling=0`, a live zero `TscOffset`, canonical unload, and
`ServiceAfterUnload=ABSENT`. `RootWindowClassification` and
`GuestWithinRootWindow` are retained as diagnostic observations. WinDbg must
record exactly one live `TSC_VMCS_OBSERVATION` and one
`hypersinv: TSC_TRANSPARENCY_PROBE boundary status=0 completion=0x54534331`
line, with no bugcheck, fatal VM-exit, fail-closed result, or retained driver
module. On any mismatch, hang, bugcheck, unload failure, or retained service,
do not retry LOAD or run UNLOAD manually; preserve the VM and WinDbg state.

The initial 2026-08-28 Gate 30 run for signed commit `b88b8a2` completed the
guest path (`completion=0x54534331`) and unloaded cleanly, but returned
`status=-3` because the original root-window acceptance predicate rejected the
unpublished timestamp relationship. Its archive SHA-256 is
`D9212D571D9779B8E72DDF98AE80A4239E10AEF2FE8DBBB5D9B2B6E538C55F6A`; the
closed WinDbg log SHA-256 is
`5B8C789141E49AED696934571F4F787066C86FF512124C59871C95331A23EEFA`.
The rerun replaces that ambiguous predicate with authenticated live VMCS
control and offset evidence; it does not weaken the native-RDTSC boundary.

The 2026-08-28 live-VMCS rerun for signed commit `65fa564` then proved
`primary=0x9400E172`, `secondary=0x00000002`, zero TSC offset, guest completion
`0x54534331`, and clean unload, but still returned `status=-3`. The closed
WinDbg log SHA-256 is
`4D4421CD73F6E0783F90CA85B3C81084EC98B9347A495620E638DB4A096D43F0`.
Investigation found that the MASM guest wrote `0x0ABACBCE` instead of the exact
32-bit complement `~0x54534331 == 0xABACBCCE`; the authenticated completion
contract therefore rejected an otherwise successful roundtrip. The corrected
literal is covered by a mutation test so this one-character defect cannot
silently recur.

The accepted 2026-08-28 completion-contract rerun for signed commit `7b26ddb`
proved `primary=0x9400E172`, `secondary=0x00000002`, zero TSC offset,
completion `0x54534331`, a successful guest roundtrip, clean unload, and
service absence. Its evidence archive SHA-256 is
`EB10A1A9535A247D9A147F0277E647EC87D9DC43BA88301EAEEE5431E6340CE2`;
the closed WinDbg log SHA-256 is
`090DE9FDFEBD474008D7011458C6D6B78B032E823C09FF9BF901524ED771E2E9`.
Gate 30 is accepted and does not require repetition.

## Gate 31: supervised native INVLPG transparency

Gate 31 is the next necessary live boundary because the persistent control
policy forbids INVLPG exiting, but no authenticated resident guest has yet
executed INVLPG and proved the corresponding live primary VMCS control. The
driver reads back the live control field, requires bit 9 clear, executes
exactly one guest `INVLPG` against its authenticated private payload address,
and verifies the exact completion contract. If INVLPG unexpectedly exits, the
persistent dispatcher has no route for exit reason 14 and fails closed.

Run Gate 31 only once under WinDbg. Existing 100-cycle gates already cover the
unchanged LOAD/UNLOAD lifecycle, so repetition would not cross a new hardware
boundary. Stage one newly built matched artifact set and
`gate31-invlpg-native-pass-through.ps1` in `C:\V3H`. Do not set software
breakpoints in the VMX/guest interval.

PASS requires `Result=PASS`, `Probe=INVLPG_NATIVE_PASS_THROUGH`,
`PrimaryControls` with bit 9 clear, `InvlpgCompletion=0x494E564C`,
`InvlpgExit=0`, one completed guest roundtrip, canonical unload, and
`ServiceAfterUnload=ABSENT`. WinDbg must record exactly one
`INVLPG_VMCS_OBSERVATION` and one
`hypersinv: INVLPG_TRANSPARENCY_PROBE boundary status=0 completion=0x494E564C`
line, with no bugcheck, fatal VM-exit, fail-closed result, or retained driver
module. On any mismatch, hang, bugcheck, unload failure, or retained service,
do not retry LOAD or run UNLOAD manually; preserve the VM and WinDbg state.

The first supervised Gate 31 run on 2026-08-28 failed only after the
authenticated guest boundary completed. WinDbg recorded live primary controls
`0x9400E172` (INVLPG exiting clear), exact completion `0x494E564C`, and
successful guest resume, while the loader returned `0x80000001`.
Investigation found that the persistent dispatcher completion allowlist and
both assembly diagnostic-state gates stopped at the earlier TSC states and
omitted the new INVLPG states. The rerun build admits `INVLPG_ARMED` and
`INVLPG_COMPLETE` at those existing fail-closed boundaries and adds
mutation-backed regression checks. This is a software completion-plumbing
rerun; it does not repeat the hardware boundary for statistical confidence.

The supervised Gate 31 rerun on 2026-08-28 is accepted. The evidence archive
SHA-256 is
`3A4B0D00BD15FE43BF58F59103D4B8E9958AB5BABF1F1FA6F4657695A88138AD`
and the WinDbg log SHA-256 is
`3DC5630376E6F3C7FE3A97A3C65BB2DC3CB732F497932AE4D005B2AD4852BFE4`.
The matched signed artifacts identify commit `483f797`; the run recorded
`PrimaryControls=0x9400E172`, `InvlpgCompletion=0x494E564C`, one completed
guest roundtrip, canonical unload, and an absent service. WinDbg recorded the
same live primary controls and exact completion with no bugcheck, fatal
VM-exit, fail-closed result, or retained module. Gate 31 requires no further
repetition.

## Gate 32: supervised live CET/HLT VMCS suppression

Gate 32 is the next necessary live boundary. Earlier gates proved the public
CPUID/MSR CET policy, while static contracts proved the planned VMCS policy;
no authenticated resident guest has yet bound the **live** VM-exit controls,
VM-entry controls, guest CR4, host CR4, and current root CR4 into one exact
observation. The new `cet-vmcs-probe` reads those fields from the current
persistent VMCS immediately before VM entry. The synthetic guest independently
checks the observation and returns the exact completion contract.

This gate deliberately does **not** execute HLT or a CET instruction. The
synthetic guest runs with interrupts disabled, so native HLT would create an
interrupt-dependent hard-hang path and is not a valid diagnostic. Instead the
same authenticated live observation requires primary-control HLT exiting bit
7 clear, VM-exit `LOAD_CET_STATE` bit 28 clear, VM-entry `LOAD_CET_STATE` bit
20 clear, and CR4.CET bit 23 clear in guest, host, and root state.

Run Gate 32 once under WinDbg from a clean VM checkpoint. Existing 100-cycle
gates already stress the unchanged LOAD/UNLOAD lifecycle; repeating this
read-only state boundary would not add coverage. Stage one newly built matched
artifact set and `gate32-cet-vmcs-suppression.ps1` in `C:\V3H`. Do not set
software breakpoints in the VMX/guest interval.

PASS requires `Result=PASS`, `Probe=CET_VMCS_SUPPRESSION`, primary bit 7
clear, exit bit 28 clear, entry bit 20 clear, guest/host/root CR4 bit 23 clear,
`CetVmcsCompletion=0x43455431`, one completed guest roundtrip, canonical
unload, and `ServiceAfterUnload=ABSENT`. WinDbg must record exactly one
`CET_VMCS_OBSERVATION` and one
`hypersinv: CET_SUPPRESSION_PROBE boundary status=0 completion=0x43455431`
line, with no bugcheck, fatal VM-exit, fail-closed result, or retained driver
module. On any mismatch, hang, bugcheck, unload failure, or retained service,
do not retry LOAD or run UNLOAD manually; preserve the VM and WinDbg state.

The supervised Gate 32 run on 2026-08-28 is accepted. The evidence archive
SHA-256 is
`8ACE919A4A1D60A7B57643F037678BE1161E7FF832D731FE7E1FB4143BFB305D`
and the closed WinDbg log SHA-256 is
`A6BEBD424CB75B0A55D962200D6CFAF98B16D6B6574DD50A1B0DA41C1DF883EE`.
The matched signed artifacts identify commit `b38c6a9`. The run recorded
primary controls `0x9400E172`, exit controls `0x003F6FFF`, entry controls
`0x0000D3FF`, guest/host/root CR4 `0x0000000000352EF8`, exact completion
`0x43455431`, canonical unload, and an absent service. Every forbidden CET/HLT
bit was clear. WinDbg recorded the same live observation and exact completion
with no bugcheck, fatal VM-exit, fail-closed result, or retained module. Gate 32
requires no repetition.

## Gate 33: authoritative operator-status lifecycle

Gate 33 is the next necessary live boundary because the authoritative `status`
operator command and its composed Runtime/EPT/CR3 classification have extensive
pure and mutation-backed coverage but have not been authenticated together on
the live persistent runtime. This gate does not introduce another VMX
instruction probe. It verifies that the command reports Runtime `LOADED`, EPT
`READY`, combined readiness `READY`, and redacted CR3 `INACTIVE`, while
explicitly keeping live-gate evidence `EXTERNAL` rather than manufacturing a
verification claim from runtime state.

Run Gate 33 once under WinDbg from a clean two-group/four-processor VM
checkpoint. Existing 100-cycle gates already stress the unchanged lifecycle,
so repeating this read-only operator boundary would add no coverage. Reuse the
accepted signed Gate 32 driver and matching PDB/loader artifacts; stage
`gate33-authoritative-status-lifecycle.ps1` in `C:\V3H`. Do not set software
breakpoints in the VMX/guest interval.

PASS requires `Result=PASS`, `Probe=AUTHORITATIVE_OPERATOR_STATUS`,
`PersistentRuntime=LOADED`, `EptRuntime=READY`,
`CombinedReadiness=READY`, `Cr3Observation=INACTIVE`,
`LiveGateEvidence=EXTERNAL`, `StatusExit=0`, canonical unload, and
`ServiceAfterUnload=ABSENT`. The captured status output must contain the exact
five operator classifications and must not claim `VMX root active` or
`Live Gate Evidence: VERIFIED`. On any mismatch, hang, bugcheck, unload
failure, or retained service, do not retry LOAD or run UNLOAD manually;
preserve the VM and WinDbg state.

The supervised Gate 33 run on 2026-08-28 is accepted. The evidence archive
SHA-256 is
`4DDC194A9FF5139492E615A3786BF0CEC6CD5F4B4565F71977E45E23B7958FCD`
and the closed WinDbg log SHA-256 is
`7C9FEF0EB11378F93E3DAED3F83659096CC2EF68CAA75C8492465BF0965541B0`.
The matched signed artifacts identify commit `b38c6a9`. The run returned the
exact Runtime `LOADED`, EPT `READY`, combined `READY`, CR3 `INACTIVE`, and
live-evidence `EXTERNAL` classifications, then proved canonical unload and an
absent service. WinDbg recorded successful CR3 query completion, no bugcheck or
fail-closed result, and no retained driver module. Gate 33 requires no further
repetition.

## Gate 34: status-queue release qualification

Gate 34 is the next necessary live release boundary. Gate 33 authenticated one
operator-status command, but it did not exercise request serialization,
backpressure, or bounded completion when several administrative clients query
the sequential passive KMDF queue. The release security contract preserves the
administrator/System-only device ACL, `FILE_DEVICE_SECURE_OPEN`,
`METHOD_BUFFERED` IOCTLs, sequential dispatch, passive execution, and the
loader's cancellable 30-second query deadlines without changing the published
IOCTL numeric ABI.

Run `gate34-status-queue-qualification.ps1` once under WinDbg from a clean
two-group/four-processor VM checkpoint. The script loads once and starts four
concurrent processes across 25 rounds, producing 100 authoritative status
commands while never exceeding four simultaneous clients. Every command must
return the exact Gate 33 classifications, write no stderr, and finish within
the 120-second per-batch watchdog. The script then performs one canonical
unload and proves service absence.

PASS requires `Result=PASS`, `Probe=STATUS_QUEUE_QUALIFICATION`, `Workers=4`,
`Rounds=25`, `TotalQueries=100`, `PassedQueries=100`, the exact Runtime/EPT/
combined/CR3/live-evidence classifications, and
`ServiceAfterUnload=ABSENT`. `AverageMs`, `P50Ms`, `P95Ms`, `MaxMs`, wall time,
and throughput are characterization, not a production benchmark or a fragile
hardware-independent performance threshold. WinDbg must record exactly 100
successful `CR3_DIAG ioctl_query complete` lines, no bugcheck, fatal VM-exit,
fail-closed result, or retained driver module. Existing accepted 100-cycle
lifecycle gates already cover repeated LOAD/UNLOAD, so Gate 34 itself requires
only this one bounded 100-query run.

On any mismatch, watchdog expiry, hang, bugcheck, unload failure, or retained
service, do not retry LOAD or run UNLOAD manually; preserve the VM and WinDbg
state for investigation.

The initial 2026-08-28 Gate 34 run did not test the 100-query boundary. All
four round-one clients returned exact READY/INACTIVE/EXTERNAL output and
WinDbg recorded four successful CR3 query completions, but Windows PowerShell
5.1 exposed blank `ExitCode`, `StartTime`, and `ExitTime` properties on the
`Start-Process -PassThru` objects used with redirected files. The harness
therefore rejected the first successful client before recording results, then
unloaded canonically. The evidence archive SHA-256 is
`DD0415975CCB79947544605012222FAA791C444B4CFE8CD9416D1898F2A4EB91`;
the closed WinDbg log SHA-256 is
`BEC307147E8B2FBF9EB7CFF901DE15EA61C949D3DBCD754A84FA914EE7CCD6BA`.
The rerun harness uses `System.Diagnostics.Process` directly, retains the same
four-client bound and watchdog, and records redirected output plus reliable
exit and timing metadata. This is a harness-correction rerun, not a runtime
failure or a repeated hardware boundary.

The corrected Gate 34 rerun on 2026-08-28 is accepted. The evidence archive
SHA-256 is
`224FB2E9247212EF6C21E52A8AAD1C78E8A08E4D9C0766A841930603C3413D8B`
and the closed WinDbg log SHA-256 is
`4E5D572C5D43F71B416F2879477C104C4AAECE58D549A8729BA52EC3A3F6115B`.
All 100 clients returned exit code zero and exact LOADED/READY/INACTIVE/
EXTERNAL classifications. Average latency was 411.090 ms, p50 386.133 ms,
p95 505.381 ms, maximum 511.131 ms, and observed throughput 7.782 commands
per second. The run unloaded canonically, removed the service, and produced
exactly 100 successful CR3 completion records with no bugcheck, fatal VM-exit,
fail-closed result, or retained module. Gate 34 is accepted and requires no
repetition.

## Gate 35: reviewed release-candidate lifecycle

Gate 35 is the next and final necessary live release boundary. Earlier gates
authenticated individual runtime behaviors, lifecycle stress, truthful status,
and bounded query pressure. They did not prove that one immutable package for
the reviewed source commit contains only the intended binaries and gate,
excludes private signing material, carries a verifiable manifest, has a valid
test signature in the VM, and still completes the canonical lifecycle.

`New-ResearchReleaseCandidate.ps1` creates the package only from a clean
tracked worktree and a full WDK/MSVC/MASM build. It signs a staged driver copy,
records the exact source commit, branch, WDK version, build/test counts,
artifact sizes and SHA-256 hashes, and the accepted Gate 34 evidence hashes.
The package deliberately excludes `.pfx`, `.p12`, `.key`, and `.cer` files.

Run `gate35-release-candidate-lifecycle.ps1` once under WinDbg from a clean
two-group/four-processor VM checkpoint. PASS requires an intact manifest and
manifest hash, exactly five declared artifacts, `SourceCommit` equal to the
reviewed commit, a `Valid` driver signature from `CN=hypersinv`, no private key
or certificate material, exact Runtime `LOADED`, EPT `READY`, combined
`READY`, CR3 `INACTIVE`, live evidence `EXTERNAL`, canonical unload, and
`ServiceAfterUnload=ABSENT`. WinDbg must record exactly one successful CR3
status completion, no bugcheck, fatal VM-exit, fail-closed result, or retained
driver module.

This one final package-provenance lifecycle is necessary; another 10/100-cycle
series is not. Gate 34 already supplies pressure characterization and earlier
accepted gates supply repeated lifecycle evidence. On any mismatch, hang,
bugcheck, unload failure, or retained service, do not retry LOAD or run UNLOAD
manually; preserve the VM and WinDbg state for investigation.

The supervised Gate 35 run on 2026-08-29 is accepted. The reviewed package
identifies source commit `418e9a163657899cfb48d5091e286a6465e35571`; its
manifest SHA-256 is
`0FF8877DD76DDA9CA47334D1806D0DB087DA2BEDAEA09737A32C9D78E9E00465`.
The evidence archive SHA-256 is
`42344FE95A5B636789ABF4CE5601E305EBB03C19EC039E34077884BBADCCDAE2`
and the closed WinDbg log SHA-256 is
`9FBF157E57ABD0F634A07BEB53F84018BC49D816846C996260C5C3F797185DA7`.
The VM validated the `CN=hypersinv` signature, exact LOADED/READY/READY and
INACTIVE/EXTERNAL status, canonical unload, service absence, and private-key
exclusion. WinDbg recorded exactly one successful CR3 status completion and no
bugcheck, fatal VM-exit, fail-closed result, or retained module. Gate 35 is accepted;
the Runtime V3 research scope is complete, and no further Runtime V3 live test
is required.

## Gate 36: supervised VPID activation and invalidation

Gate 36 is the first Runtime V4 live boundary. Static authorization has proven
the capability model, group-safe identifier owner, native INVVPID adapter,
transactional VMCS composition, unsupported-capability fallback, teardown
retry state, and truthful per-processor status bridge. Hardware execution is
still necessary because no host-side test can authenticate the outer
hypervisor's INVVPID behavior or the live current VMCS on the disposable VM.

Use a clean two-group/four-processor checkpoint with kernel debugging already
configured. Stage one matched signed driver/PDB and loader/PDB set plus
`gate36-vpid-activation.ps1` in `C:\V3H`. Open WinDbg before running the gate;
set both `nt!Kd_IHVDRIVER_Mask` and `nt!Kd_DEFAULT_Mask` to `0xffffffff`
before continuing the target, and do not set software breakpoints inside the
VMX/guest interval. A failed VPID activation or retirement emits a
`VPID_FAILURE` record with the exact phase, native/coordinator status,
instruction result, failed VMCS field, and rollback state.

The script loads once, runs `vpid-status`, requires four exact ACTIVE records,
requires deterministic nonzero VPIDs 1 through 4 with no alias, authenticates
the live VPID secondary-control bit and VMCS VPID on each pinned processor,
then performs one canonical unload. The unload path must clear the VPID VMCS
state and complete single-context INVVPID before acknowledging identifier
retirement or releasing runtime/EPT ownership.

The capability preflight requires both the INVVPID forms reported by
`IA32_VMX_EPT_VPID_CAP` and the VPID allowed-1 bit in
`IA32_VMX_PROCBASED_CTLS2`. If the outer hypervisor exposes INVVPID but masks
the VMCS VPID control surface, Runtime falls back without touching the
unsupported VMCS field. Gate 36 then performs canonical unload, reports
`ENVIRONMENT_UNSUPPORTED`, and must be repeated on a VPID-capable environment;
that result is not a Gate 36 PASS.

PASS requires `Result=PASS`, `Probe=VPID_ACTIVATION`, `Processors=4`,
`Groups=2`, `Vpids=1,2,3,4`, `LiveVmcsVpid=VERIFIED`, both invalidation fields
`VERIFIED_BY_*`, and `ServiceAfterUnload=ABSENT`. A DBG driver additionally
prints `VPID_ACTIVATE`, `VPID_STATUS classification=3`, and
`VPID_RETIRE_INVALIDATED` records. Those success markers use `KdPrint` and are
compiled out of a non-DBG release driver; their absence is not a release-build
failure. The pointer-free public status, exact loader output, service evidence,
event capture, and dump check remain authoritative. WinDbg must contain no
bugcheck, fatal VM-exit, fail-closed result, or unexplained break.

On any mismatch, hang, bugcheck, unload failure, or retained service, do not
retry LOAD or run UNLOAD manually. Preserve the VM, WinDbg state, evidence
folder/archive, and debugger log. A successful Gate 36 advances directly to
Gate 37's justified 100-cycle lifecycle run; there is intentionally no
separate 10-cycle VPID gate.

The supervised Gate 36 run on 2026-08-29 is accepted. Evidence archive
`gate36-vpid-activation-20260829-124541.zip` has SHA-256
`88F44030664E7554B329B1193B12E9620CBA4505551E990A1D8C253E4075EDBA`.
The signed driver SHA-256 is
`75E811D2AC4AF2F2A7338B329A195D81F432E5CF72F915F1A03507613513C12C`.
All four processor records reported exact group-aware identity, VPIDs and
observed VMCS values 1 through 4, owner state 1, flags/capabilities `0x1f`, and
secondary controls `0x22`. Unload was canonical, the service was absent, and
no failure event or new dump was produced. Closed WinDbg log
`windbg-gate36-identity-fix-rerun_6e10_2026-08-29_12-45-11-990.log` has
SHA-256
`B6756C26B4C3D581CEC8480E2329C592937BD23AA26013265209C085749919F1`;
it records only the operator-requested break and normal log closure.

## Gate 37: 100-cycle VPID lifecycle qualification

Gate 37 is the only repetition gate justified for Runtime V4. It uses the
accepted Gate 36 signed driver, matching PDB/loader pair, and unchanged Gate 36
inner transaction. Do not rebuild or re-sign those accepted runtime artifacts.
Stage `gate37-vpid-lifecycle-100cycles.ps1` beside them in `C:\V3H`.

The wrapper launches Gate 36 in a fresh PowerShell process 100 times. Before
and after every cycle it requires service query exit `1060`. Every inner run
must exit zero, emit no stderr, create exactly one summary and evidence archive,
and reproduce all exact Gate 36 proofs. The wrapper separately parses the
four `vpid-status-stdout.txt` records and requires the deterministic mapping
`0/0/0 -> 1`, `1/0/1 -> 2`, `2/1/0 -> 3`, and `3/1/1 -> 4`, with matching
observed VMCS values, owner state, flags, capabilities, and secondary controls.
Each cycle also rejects critical Kernel-Power, WHEA, loader/PowerShell
Application Error events, and a new `C:\Windows\MEMORY.DMP`.

PASS requires outer `Result=PASS`, `PassedCycles=100`, exactly 100
`CycleNNN=PASS` records, 100 nested Gate 36 archives, and final service absence.
Run under WinDbg with both print masks enabled and no software breakpoint in a
VMX/guest interval. On the first failure, hang, bugcheck, retained service, or
uncertain unload, do not retry LOAD and do not run UNLOAD manually. Preserve
the VM, outer evidence directory/archive, and WinDbg log.

Fresh host-only verification in
`build/codex-gate37-20260829-125544` completed all 379 WDK/MSVC/MASM targets,
passed focused Gate 36/37 contracts 2/2, and passed full CTest 239/239. The VM
package
`gate37-vm-stage-accepted-gate36.zip` has SHA-256
`E9E79C3FB16CF4DC7172C5BFF0A61CAD425C565B44F0B601C72BE364D48163B2`.
It carries the exact accepted signed Gate 36 driver and matching PDB/loader
artifacts; no runtime binary was rebuilt or re-signed for Gate 37.

The supervised Gate 37 run on 2026-08-29 is accepted. Evidence archive
`gate37-vpid-lifecycle-100cycles-20260829-130503.zip` has SHA-256
`413F8598130E3A8C9164A72B8D3525892919F862656A8DB1B80BF34B767E20FA`.
The wrapper completed exactly 100 PASS cycles with 100 inner Gate 36 summaries
and archives, deterministic VPIDs 1 through 4, canonical unload, service
absence, no qualifying failure events, and no new dump in every cycle. Closed
WinDbg log
`windbg-gate37-vpid-lifecycle-100cycles_6e10_2026-08-29_13-04-39-290.log`
has SHA-256
`BE9AA1CA1B5D6C6FCEE0290E3F437E6A038FEBA881FD62BEAD5D4939E82A7EEE`;
it contains no bugcheck, fatal event, `VPID_FAILURE`, or fail-closed result.
Gate 37 is accepted and must not be repeated.

## Gate 38: VPID/CR3 residency interoperability

Gate 38 is one necessary cross-feature boundary, not another lifecycle stress
gate. Gate 17 proved the CR3 target ENTER/LEAVE state machine before VPID was
introduced. Gates 36 and 37 proved VPID activation and lifecycle without
executing a CR3 target-residency transition. Because VPID tags translation
contexts while the CR3 target controls guest entry/exit residency, one combined
run is required to prove that the accepted features compose without VPID
identity drift, aliasing, or loss of live VMCS state.

Use the exact accepted signed Gate 36/37 runtime artifacts. Do not rebuild or
re-sign the driver. Stage `gate38-vpid-cr3-residency-interoperability.ps1`
beside the driver/PDB and loader/PDB in `C:\V3H`. This single integration run
needs no 10-cycle or 100-cycle repetition because the accepted
Gate 19 and Gate 37 evidence already qualifies the two lifecycle families.

The script requires a clean service baseline, exactly two processor groups
with two processors each, correct crash-dump configuration, and the expected
`CN=hypersinv` test signature. It loads once and requires exact ACTIVE VPID
records for indices 0 through 3 with group-aware identities and VPID/observed
values 1 through 4. It then requires the exact CR3 INACTIVE baseline, exact
stage-7 ENTER/LEAVE response (`residency=0/1/0`,
`matched=4294967295/0/4294967295`, `writes=1/1/1`, strictly increasing
transitions), and final CR3 OBSERVED/nonresident state. A second `vpid-status`
must reproduce the four pre-probe records byte-for-byte before canonical
unload and service removal.

PASS requires `Result=PASS`,
`Probe=VPID_CR3_RESIDENCY_INTEROPERABILITY`, `VpidsBefore=1,2,3,4`,
`VpidsAfter=1,2,3,4`, `VpidIdentityStable=VERIFIED`,
`Cr3Residency=0/1/0`, `Cr3Matched=4294967295/0/4294967295`,
`Cr3Writes=1/1/1`, and `ServiceAfterUnload=ABSENT`. The evidence must contain
no qualifying crash, WHEA, or loader/PowerShell Application Error event and no
new `C:\Windows\MEMORY.DMP`.

Fresh host-only verification in `build/codex-gate38-20260829-133659`
completed all 379 WDK/MSVC/MASM targets, passed focused Gate 36-38 contracts
3/3, and passed full CTest 240/240. The immutable VM package
`gate38-vm-stage-accepted-gate36.zip` has SHA-256
`955B995CB33AB6870A3532396A007E84B08F5B538E9CA026B97D538547C616A0`.
Its manifest authenticates the gate plus the exact accepted signed driver,
matching PDB, loader, and loader PDB; no runtime binary was rebuilt or
re-signed for this gate.

Run under WinDbg with both debug-print masks set to `0xffffffff`. Do not set a
software breakpoint inside the VMX/guest interval. On any mismatch, watchdog
expiry, hang, bugcheck, unload failure, or retained service, do not retry LOAD
and do not run UNLOAD manually. Preserve the VM, evidence directory/archive,
and WinDbg log for investigation.

The first Gate 38 attempt on 2026-08-29 is a harness-only rejection, not a
runtime failure. The runtime loaded successfully, reported exact ACTIVE VPID
records for all four processors, and emitted no failure event or debugger
fault. The harness compared each complete public VPID record against an
incomplete expected prefix that omitted the authoritative owner, flags,
capabilities, and secondary-controls fields. It therefore stopped before the
CR3 probe and intentionally preserved the running service. Evidence archive
`gate38-vpid-cr3-residency-interoperability-20260829-134410.zip` has SHA-256
`619948EB1AB5C5A048AB63EBFC690DDEC948AEF36B6E8C691635B4717463DF12`;
WinDbg log
`windbg-gate38-vpid-cr3-interoperability_6e10_2026-08-29_13-43-42-348.log`
has SHA-256
`44A34BD4968DB0169845EFED3378019CC932D90300B8E1B70D81C9DA5F30D424`.
The corrected harness requires the complete exact record, including
`owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022`.
Revert to the clean checkpoint before the single corrected rerun; do not try
to continue from the preserved loaded state.

The corrected immutable rerun package
`gate38-vm-stage-accepted-gate36-record-fix.zip` has SHA-256
`36D16909348F73D0DEAAFF4840E714C66F34927BFEEB3037908ED1713B11299F`.
Its Gate 38 script has SHA-256
`990E7B94EC925A3B6A956F6CFD42CA31BE77205BD030F7F2AD253B73B3338F36`.
The four accepted runtime artifact hashes are unchanged. PowerShell parsing,
the mutation-backed Gate 38 verifier, and the registered focused CTest all
pass after the correction.

The corrected Gate 38 run on 2026-08-29 is accepted. Evidence archive
`gate38-vpid-cr3-residency-interoperability-20260829-135244.zip` has SHA-256
`31426B484F5C126F58A4C15086742738D7FB9E10447F7C56C55C99D5B799197E`.
It proved exact unchanged VPID records before and after the CR3 residency
roundtrip, transitions `1/2/3`, residency `0/1/0`, matched indices
`4294967295/0/4294967295`, writes `1/1/1`, final CR3 nonresidency, canonical
unload, service absence, zero failure events, and no new dump. Closed WinDbg
log `windbg-gate38-vpid-cr3-record-fix_6e10_2026-08-29_13-52-17-209.log`
has SHA-256
`C8E0777D4FDB4F8393A10A0A935D4A28126775309CA8BF487D36E6079E9780CB`;
it contains no bugcheck, fatal event, `VPID_FAILURE`, fail-closed result, or
retained module. Gate 38 requires no repetition.

## Gate 39: Runtime V4 reviewed release-candidate lifecycle

Gate 39 is the final necessary Runtime V4 live boundary. Gates 36-38 prove the
runtime behavior, repetition, invalidation lifecycle, and CR3 interoperability,
but their accepted package is not bound to a clean pushed source commit and a
Runtime V4 release manifest. Gate 39 supplies that missing provenance boundary.

`New-RuntimeV4ResearchReleaseCandidate.ps1` refuses any tracked or untracked
working-tree change, requires `feature/hypervisor-core` HEAD to equal its
upstream, copies only the fresh full-build driver/PDB, loader/PDB, and Gate 39
script, signs only the staged driver copy, and records exact artifact hashes,
sizes, WDK/build/test counts, documentation hashes, and accepted Gate 36-38
evidence hashes. It excludes `.pfx`, `.p12`, `.key`, and `.cer` files.
The VM entry point is
`gate39-runtime-v4-release-candidate-lifecycle.ps1`.

Run one final package-provenance lifecycle on the clean two-group/four-
processor disposable VM. The gate requires schema version 2, the exact Runtime
V4 candidate type, a pushed 40-character source commit, WDK 10.0.28000.0,
at least 379 build targets and 240 CTests, exactly five declared artifacts, a
valid `CN=hypersinv` signature, exact authoritative LOADED/READY/READY and
INACTIVE/EXTERNAL status, all four exact ACTIVE VPID records, canonical unload,
service absence, no failure event, and no new dump.

PASS requires `Result=PASS`,
`Probe=RUNTIME_V4_RELEASE_CANDIDATE_LIFECYCLE`, the exact source commit and
manifest hash, `SignatureStatus=Valid`, `Signer=CN=hypersinv`,
`Vpids=1,2,3,4`, `VpidIdentity=EXACT_ACTIVE`,
`LiveGateEvidence=EXTERNAL`, `ServiceAfterUnload=ABSENT`, and
`PrivateKeyIncluded=FALSE`.

This is one final package-provenance lifecycle; no repetition is necessary.
Gate 37 already supplies 100-cycle VPID lifecycle evidence and Gate 38 supplies
the combined CR3 boundary. On any mismatch, hang, bugcheck, unload failure, or
retained service, do not retry LOAD and do not run UNLOAD manually. Preserve
the VM, evidence archive, release package, manifest, and closed WinDbg log.

Fresh host-only authorization in `build/codex-gate39-20260829-140303`
completed all 379 WDK/MSVC/MASM targets, passed focused Gate 38/39 contracts
2/2, and passed full CTest 241/241. No driver was signed, installed, loaded, or
live-tested. The packager intentionally refuses to create the candidate until
the reviewed Runtime V4 changes are committed, the worktree is completely
clean, and HEAD equals the pushed upstream commit.

The supervised Gate 39 run on 2026-08-29 is accepted. Source commit
`3bd85015967c7bd12f06e92abf98772c3a59d771` produced release manifest SHA-256
`9F2551137027E9FC1820ED77EE724ABDF8372ABC700A1BB205EDC9828D871591`.
Evidence archive SHA-256 is
`50482275082187953CEC856BB5D71423B66EB47AD6C0D4060646081BCD2E424A`;
closed WinDbg log SHA-256 is
`BEB4753D566541E8BFE1D7F3FAD18F32BC39B7059D80EDB12EA349222511ACEC`.
The VM proved exact LOADED/READY/READY and INACTIVE/EXTERNAL status, VPIDs
1 through 4, a valid `CN=hypersinv` signature, canonical unload, service
absence, and private-key exclusion. Runtime V4 is closed.

## Gate 40: exact per-processor VM-exit telemetry delta

Gate 40 is the first Runtime V5 live boundary. Static verification covers the
counter model, launch-context layout, coherent snapshot protocol, public ABI,
IOCTL validation, runtime ownership, loader validation, and fail-closed gate
harness. One disposable-VM run is still necessary to prove that real VM exits
are attributed to the correct resident processor.

Run `gate40-vmexit-telemetry-cpuid-delta.ps1` once under WinDbg from the clean
two-group/four-processor checkpoint. The gate loads once, captures all four
telemetry baselines, runs the already-qualified authenticated CPUID probe on
system index 0, captures all four snapshots again, and unloads canonically.

PASS requires `Result=PASS`, `Probe=VMEXIT_TELEMETRY_CPUID_DELTA`,
`Processors=4`, `TargetSystemIndex=0`, `TotalDelta=4`, `CpuidDelta=3`,
`VmcallDelta=1`, `NonTargetDelta=0`, `FailClosedDelta=0`, and
`ServiceAfterUnload=ABSENT`. CR-access, MSR-read, MSR-write, EPT-violation,
other, and fail-closed deltas must be zero. System indices 1 through 3 must not
change. Identity, generation, even sequence values, and exact category sums
must remain valid.

No 10-cycle or 100-cycle repetition is necessary. CPUID and persistent
lifecycle stress are already accepted; this gate qualifies only the new
telemetry attribution boundary. On any mismatch, hang, bugcheck, unload
failure, retained service, failure event, or new dump, do not retry LOAD and do
not run UNLOAD manually. Preserve the VM, evidence folder/archive, and WinDbg
state.

Fresh host-only authorization in `build/codex-gate40-20260829-185820` built
all 382 WDK/MSVC/MASM targets, passed focused telemetry/layout/disassembly
verification 4/4, and passed full CTest 243/243. No driver was signed,
installed, loaded, or live-tested during authorization.

The first Gate 40 attempt on 2026-08-29 stopped after the valid baseline
telemetry query because Windows PowerShell 5.1 treats hashtable `UInt32` and
`Int32` keys as distinct. All four returned records were coherent and exact,
but the harness used an untyped zero for its generation lookup. The CPUID probe
did not run and the fail-closed harness preserved the running service.
Evidence archive SHA-256 is
`7E44CF778D8F21CA7812092D6008AF9EA2085AA220D3345097BAA79DACFF3631`;
closed WinDbg log SHA-256 is
`5ACB9C9F6972D201736707AA92C26071862020993F495020701B5CC529C6C9A4`.
Revert to the clean checkpoint and use the corrected typed-key harness for one
rerun. Do not continue from the preserved loaded state.

## Gate 41: VM-exit telemetry route matrix

Run `gate41-vmexit-telemetry-route-matrix.ps1` once under WinDbg from the clean
two-group/four-processor checkpoint. This is one batched qualification, not a
new stress series. It loads once, captures a telemetry baseline, runs the
already-qualified MSR transparency, CR3, and EPT-violation probes on system
index 0, captures telemetry after each route, and unloads canonically.

PASS requires exact incremental target deltas of `5/3/1/1` for
total/RDMSR/WRMSR/VMCALL, then `2/1/1` for total/CR-access/VMCALL, then
`3/1/1/1` for total/CR-access/EPT-violation/VMCALL. All non-target records
must remain unchanged; CPUID, other, and fail-closed deltas must remain zero.
The service must be absent after unload, with no new dump or failure event.

Stop on any load, probe, telemetry, identity, generation, delta, unload,
service, event, dump, hang, or bugcheck failure. Do not retry LOAD or manually
UNLOAD after an uncertain runtime result; preserve the VM and WinDbg state.

## Gate 42: Runtime V5 reviewed release-candidate lifecycle

`gate42-runtime-v5-release-candidate-lifecycle.ps1` is the final Runtime V5
live boundary. Run it once from the clean two-group/four-processor checkpoint
under WinDbg. It must consume the package produced by
`New-RuntimeV5ResearchReleaseCandidate.ps1`; loose or manually substituted
artifacts are not acceptable.

The gate authenticates the clean pushed source identity, manifest hash, exact
artifact hashes/sizes, accepted Gate 40/41 evidence hashes, valid
`CN=hypersinv` signature, private-key exclusion, authoritative Runtime/EPT
status, exact VPIDs 1 through 4, exact initial telemetry records, canonical
unload, service absence, events, and dump state. This is one final
package-provenance lifecycle with no repetition.

Stop on any manifest, signature, identity, telemetry, VPID, lifecycle, event,
dump, hang, or debugger failure. Do not retry LOAD or manually UNLOAD after an
uncertain result; preserve the VM and debugger state.

## Gate 43: exact redacted dispatch-status baseline

`gate43-dispatch-status-baseline.ps1` is the first Runtime V6 live boundary.
Run it once from the clean two-group/four-processor checkpoint under WinDbg
using one matched signed stage. It loads the persistent runtime, queries the
new `dispatch-status` command, requires four exact initial VMCALL records, and
unloads canonically.

The public response intentionally excludes guest RIP, exit qualification,
guest CR3, diagnostic targets, and all kernel pointers. PASS requires exact
processor identities, owner generation 1, dispatch sequence 2, telemetry sequence 2, VMCALL reason
18, instruction length 3, resident protocol/action state, successful VMCS
read/write outcomes, zero errors/fail-closed state, and service absence after
unload. One run is sufficient because this gate qualifies a new read-only ABI,
not another lifecycle-stress boundary.

Stop on any mismatch, timeout, hang, bugcheck, event, dump, or uncertain
unload. Do not retry LOAD or manually UNLOAD; preserve the VM and debugger
state for investigation.

Gate 43 passed on 2026-08-29. Accepted evidence archive SHA-256:
`CDE0D45C0A65F22623A3C81C137D23E998B24378FE926EB88C6F77EF6846EE04`.
Accepted closed WinDbg log SHA-256:
`7CAE0A8A3813174B2EA8ACDA6AB40C78513EA38F177A7A2F17113A0D20A0B1CA`.

## Gate 44: CPUID dispatch-status coherence

`gate44-dispatch-status-cpuid-coherence.ps1` is the final necessary Runtime V6
live boundary. Run it once from the clean two-group/four-processor checkpoint
under WinDbg using its matched signed stage. The script records exact telemetry
and redacted dispatch baselines, executes one authenticated `cpuid-probe`, and
queries both surfaces again.

PASS requires system index 0 to advance both publication sequences from 2 to
10, with telemetry deltas total 4, CPUID 3, and VMCALL 1. Its last redacted
dispatch must be the diagnostic-completion VMCALL with reason 18, length 3,
action 5, protocol step 7, validation mask `0x00000FFF`, successful VMCS
operations, validation flags `0x00000033` (header, ready, resume armed, and
resume valid), and no error or fail-closed state. Indices 1 through 3 must remain
at their exact initial sequence-2 VMCALL records with action 1 and
dispatch-time READY_EXIT protocol step 2. Canonical unload, service
absence, no retained driver module, and clean event/dump checks are mandatory.

Do not repeat this gate for 10 or 100 cycles. Earlier gates already qualify
CPUID and lifecycle repetition. Stop on any mismatch, timeout, hang, bugcheck,
event, dump, or uncertain result. Do not retry LOAD or manually UNLOAD.

## Gate 45: coherent observation-bundle CPUID coherence

`gate45-observation-bundle-cpuid-coherence.ps1` is the only necessary Runtime
V7 live boundary. Run it once from the clean two-group/four-processor
disposable VM checkpoint under WinDbg using its matched signed stage. It loads
once, captures all four coherent `observation-status` bundles, executes one
authenticated CPUID probe on system index 0, captures all four bundles again,
and unloads canonically.

PASS requires matching nested processor identity, owner generation, and
telemetry sequence in every 240-byte bundle. Initial telemetry and dispatch
sequences must be 2. The target must advance both to 10 with exact
total/CPUID/VMCALL deltas 4/3/1 and the accepted completion dispatch; all
non-target bundles must remain unchanged. Service/module absence, clean
failure events, and no new dump remain mandatory.

One run is sufficient because earlier gates already qualify CPUID and
lifecycle repetition. Stop on any mismatch, timeout, hang, bugcheck, event,
dump, or uncertain unload. Do not retry LOAD or manually UNLOAD; preserve the
VM and debugger state.

Gate 45 passed on 2026-08-29 for source commit
`a1359502d0ceb79694c8236fd2aaf44a824df1f2`. Accepted evidence archive
SHA-256: `62689C8F1D6ADDD1C60C27F907DB921CDEE929F9C65E19FA3448CFBEDF74D7E5`.
Accepted closed WinDbg log SHA-256:
`3FE7A7E8DD00FF43001943B5814F93ED8CBB084837AF17EFDE9A363DE01819CD`.
Runtime V7 is closed; do not repeat Gate 45.

## Gate 46: exclusive privileged control session

`gate46-exclusive-control-session.ps1` is the only necessary Runtime V8 live
boundary. Run it once from the clean two-group/four-processor disposable VM
checkpoint under WinDbg using its matched signed stage. It loads once, opens
one privileged device handle with read/write sharing explicitly enabled, and
attempts a second handle with the same compatible sharing. WDF must deny the
second open with Win32 error 5 because the control device itself is exclusive.

After the first handle closes, `observation-status` must immediately succeed
with the exact four generation-1 sequence-2 records. The gate then unloads
canonically and requires service/module absence, clean failure events, and no
new dump.

One run is sufficient because this qualifies one new device-access boundary;
earlier gates already cover runtime lifecycle repetition and the recovery
observation command. Stop on any mismatch, timeout, hang, bugcheck, event,
dump, or uncertain unload. Do not retry LOAD or manually UNLOAD; preserve the
VM and debugger state.

Gate 46 passed on 2026-08-30 for source commit
`0c0770a307f6c63f12bd6a784f5993d6d442581f`. The evidence archive SHA-256 is
`6C24A056E0B1F3966A905B2FF1ABBDD281AB187CBEF71148FF3A5DEB2A6A241D`.
The closed WinDbg log SHA-256 is
`441971C83FD8E29125C7960B279C59AEC2AEEF58C01B9038DD7546B8851E7F37`.
Runtime V8 is closed; do not repeat Gate 46.

## Gate 47: explicit control-session access matrix

`gate47-control-session-access-matrix.ps1` is the only necessary Runtime V9
live boundary. Run it once from the clean two-group/four-processor disposable
VM checkpoint under WinDbg using its matched signed stage. After one verified
LOAD, it attempts zero-access, read-only, and write-only native opens with
read/write sharing enabled. Each incomplete access request must be denied with
Win32 error 5. A read/write request must then be admitted.

After the admitted handle closes, `observation-status` must return the exact
four generation-1 sequence-2 records. The gate then unloads canonically and
requires service/module absence, clean failure events, empty loader stderr,
and no new dump.

One run is sufficient because this qualifies one deterministic file-create
authorization boundary. Gate 46 already covers exclusivity and existing
stress gates cover lifecycle repetition. Stop on any mismatch, timeout, hang,
bugcheck, event, dump, or uncertain unload. Do not retry LOAD or manually
UNLOAD; preserve the VM and debugger state.

Gate 47 passed on 2026-08-30 for source commit
`c2b19f024c71e6f6fd2ca9ad2358767c9ae8ef6e`. The evidence archive SHA-256 is
`FEDB560EFD3F139340496AAF7AE33AB53DACCC663D30B1650DC2FE6E4E747713`.
The closed WinDbg log SHA-256 is
`AB718AEFFC92F890BE0293BA6AD0F20F64108470198187232B17A17C383434E6`.
Runtime V9 is closed; do not repeat Gate 47.

## Gate 48: root-only control-device namespace

`gate48-control-session-namespace.ps1` is the only necessary Runtime V10 live
boundary. Run it once from the clean two-group/four-processor disposable VM
checkpoint under WinDbg using its matched signed stage. After one verified
LOAD, it attempts a read/write shared open of
`\\.\hypersinv\unexpected`. The named open must be denied with Win32 error 5.
The same access and sharing against canonical `\\.\hypersinv` must succeed.

After the canonical handle closes, `observation-status` must return the exact
four generation-1 sequence-2 records. The gate then unloads canonically and
requires service/module absence, clean failure events, empty loader stderr,
and no new dump.

One run is sufficient because this qualifies one deterministic namespace
boundary. Gates 46 and 47 already cover exclusivity and access rights, and
existing stress gates cover lifecycle repetition. Stop on any mismatch,
timeout, hang, bugcheck, event, dump, or uncertain unload. Do not retry LOAD
or manually UNLOAD; preserve the VM and debugger state.

Gate 48 passed on 2026-08-30 for source commit
`e63fca1e47e6e095f71a7f218be7589baa54ac8c`. The evidence archive SHA-256 is
`CDD0600139DEB61051DAF8C08E9F6580823A3E632C686A5A3E42F54D97702E1A`.
The closed WinDbg log SHA-256 is
`5A6C7DD231D2B4304C98CF340EDC4AAE2231F911F7101348B913675FE2E2FE4B`.
Runtime V10 is closed; do not repeat Gate 48.

## Gate 49: process-bound control session

`gate49-control-session-owner.ps1` is the only necessary Runtime V11 live
boundary. Run it once from the clean two-group/four-processor disposable VM
checkpoint under WinDbg using its matched signed stage. After one verified
LOAD, `control_session_owner_probe.exe` opens an inheritable canonical device
handle. Its owner process must complete the exact observation query before and
after a child process attempts the same IOCTL through the inherited handle.
The child request must fail with Win32 error 5.

After the helper closes, the normal loader must return the exact four
generation-1 sequence-2 observation records. The gate then unloads canonically
and requires service/module absence, clean failure events, empty stderr, and no
new dump.

One run is sufficient because this qualifies one deterministic request-owner
boundary. Gates 46 through 48 already cover the adjacent device-open policies,
and existing stress gates cover lifecycle repetition. Stop on any mismatch,
timeout, hang, bugcheck, event, dump, or uncertain unload. Do not retry LOAD
or manually UNLOAD; preserve the VM and debugger state.

Gate 49 passed on 2026-08-30 for source commit
`0fd7f09242d50f873a9e76bd800075bd64f30a3f`. It admitted the owner before
and after the child attempt, denied the inherited child IOCTL with Win32 error
5, recovered the exact generation-1 sequence-2 observation records, and
unloaded canonically. The evidence archive SHA-256 is
`0A084E4787AD140DFFDD1757C2955097B6C21F6C888D78F18C374180519F663C`.
The closed WinDbg log SHA-256 is
`C48F4D10E0A859D4D947C54A81A37ED26FC6C3C319C5EB17F1690AD281FDF5E1`.
Runtime V11 is closed; do not repeat Gate 49.

## Gate 50: stable owner identity after opener exit

`gate50-control-session-owner-lifetime.ps1` is the only necessary Runtime V12
live boundary. Run it once from the clean two-group/four-processor disposable
VM checkpoint under WinDbg using its matched signed stage. After one verified
LOAD, `control_session_owner_lifetime_probe.exe` starts a dedicated owner that
opens the canonical device and verifies the exact initial observation. The
owner gives a child the inherited file object and exits. The child waits for
confirmed owner termination before issuing the IOCTL, which must fail with
Win32 error 5, then closes the final inherited handle.

After the helper returns, the normal loader must reopen the device and return
the exact four generation-1 sequence-2 observation records. The gate then
unloads canonically and requires service/module absence, clean failure events,
empty stderr, and no new dump.

One run is sufficient because this qualifies the new retained process-object
and final file-context cleanup lifetime. It does not change VMX lifecycle
behavior. Stop on any mismatch, timeout, hang, bugcheck, event, dump, or
uncertain unload. Do not retry LOAD or manually UNLOAD; preserve the VM and
debugger state.

Gate 50 passed on 2026-08-30 for source commit
`df2910d42e51dab45fb34711a5ad2f07a72968de`. It admitted the owner, confirmed
owner exit before the child attempt, denied the inherited child IOCTL with
Win32 error 5, recovered the exact generation-1 sequence-2 observation records
after final close, and unloaded canonically. The evidence archive SHA-256 is
`7E168042CD9B6B7C3587D74AA9FE620C094CFCBA4D2447CD33D0C140F18B8841`.
The closed WinDbg log SHA-256 is
`CE925881AC8D6F999B009A1C1439725EE57ACE04A0D2391D75CEB5938ADFC82E`.
Runtime V12 is closed; do not repeat Gate 50.

## Gate 51: control-protocol negotiation

`gate51-control-session-protocol.ps1` is the only necessary Runtime V13 live
boundary. Run it once from the clean two-group/four-processor disposable VM
checkpoint under WinDbg using its matched signed stage. The normal loader must
report exact protocol `1.0` negotiation during LOAD.

After LOAD, `control_session_protocol_probe.exe` opens one canonical session.
An observation request before negotiation and a wrong-major negotiation must
both fail with Win32 error 1306. Exact protocol `1.0` must then be accepted,
and the same session must retrieve the exact initial observation.

After the helper closes, the normal loader must negotiate a new session and
return all four exact generation-1 sequence-2 observation records. The gate
then unloads canonically and requires service/module absence, clean failure
events, empty stderr, and no new dump.

One run is sufficient because this qualifies one deterministic protocol-state
boundary. Existing stress gates cover VMX lifecycle repetition. Stop on any
mismatch, timeout, hang, bugcheck, event, dump, or uncertain unload. Do not
retry LOAD or manually UNLOAD; preserve the VM and debugger state.

Gate 51 passed on 2026-08-30 for source commit
`87a58b15f6a3ca12211ecd7cf97679c0cd6fcbdc`. The evidence archive SHA-256 is
`E87AFDC490E37B699710DBA2AF72F536D57F42A30C002FFDEC08C294FE90020A`;
the closed WinDbg log SHA-256 is
`1E228A6C243AE4FED251B8B575519B1F3ACEB81A286AA386C87D0FA1F527DA56`.
Runtime V13 is closed; do not repeat Gate 51.

## Gate 52: exact operational request framing

`gate52-control-session-request-framing.ps1` is the only necessary Runtime V14
live boundary. Run it once from the clean two-group/four-processor disposable
VM checkpoint under WinDbg using its matched signed stage. After one verified
LOAD, `control_session_request_frame_probe.exe` opens and negotiates one
canonical control session.

The helper sends a one-byte input to legacy STATUS and LOAD. Both must be
denied with Win32 error 24 before runtime dispatch. An exact zero-input STATUS
on the same session must then succeed and return its complete 16-byte response;
the fields are diagnostic because authoritative runtime state is checked next.

After the helper closes, the normal loader must negotiate a new session and
return all four exact generation-1 sequence-2 observation records. The gate
then unloads canonically and requires service/module absence, clean failure
events, empty stderr, and no new dump.

One run is sufficient because this qualifies one deterministic WDF request-
framing boundary. Existing stress gates cover VMX lifecycle repetition. Stop
on any mismatch, timeout, hang, bugcheck, event, dump, or uncertain unload. Do
not retry LOAD or manually UNLOAD; preserve the VM and debugger state.

Gate 52 passed on 2026-09-05 for source commit `c67d87f`. It denied both
oversized operational requests with Win32 error 24, accepted the exact
zero-input legacy status request, recovered the exact four generation-1
sequence-2 observation records, and unloaded canonically. The evidence archive
SHA-256 is
`A760CBFFFA1C420A31A681D6F7CA275E2353692431C22A2E0607D30FCE8B30AB`.
The closed debugger log SHA-256 is
`D03C61775ECF37309F76ABF7BB44B352CE729B009903C08DB74EDCEF88401863`.
Runtime V14 is closed; do not repeat Gate 52.

## Gate 53: immutable protocol negotiation

Gate 53 qualifies Runtime V15's one-shot control-session negotiation boundary.
The helper negotiates exact protocol 1.0, verifies that both exact and
incompatible replays fail with Win32 error 5, and then proves the original
session remains authorized with an exact observation query. The normal loader
must recover all four initial observations and unload canonically after the
helper closes.

Run `gate53-control-session-protocol-replay.ps1` only from the corrected clean
two-group/four-processor VM checkpoint through the host runner and headless KD.
One run is sufficient. Stop on any mismatch, timeout, hang, bugcheck, event,
dump, or uncertain unload; do not retry or manually unload.

Gate 53 passed on 2026-09-05. Exact and incompatible negotiation replays were
both denied with error 5, the original session remained authorized for an
exact observation query, all four initial observations were recovered, and
unload removed the service. Evidence archive SHA-256:
`F71F25AF564AC4E3B72AA9209BD6EF0771ECF919167B8010DB3DA7B802B19246`.
Closed KD log SHA-256:
`DB987376F728EC6AA27734D509ECF86574AB96DB49AC275BB76410557A380B85`.
Runtime V15 is closed; do not repeat Gate 53.

## Gate 54: exact fixed-response framing

`gate54-control-session-response-framing.ps1` is the only necessary Runtime
V16 live boundary. Run it once from the clean two-group/four-processor
disposable VM checkpoint through the host runner and headless KD using its
matched signed stage. After one verified LOAD,
`control_session_response_frame_probe.exe` opens and negotiates one canonical
control session.

The helper sends one-byte-short and one-byte-long output buffers to legacy
STATUS and observation status. All four non-canonical calls must fail with
Win32 error 24 and zero returned bytes. The exact 16-byte legacy STATUS and
240-byte observation responses must then succeed on the same session.

After the helper closes, the normal loader opens a fresh negotiated session
and must recover all four exact generation-1 sequence-2 observation records.
The gate then unloads canonically and requires service absence, clean failure
events, empty stderr, and no new dump.

One run is sufficient because this is a deterministic WDF response-framing
boundary; accepted lifecycle stress gates already cover repeated VMX startup
and teardown. Stop on any mismatch, timeout, hang, bugcheck, event, dump, or
uncertain unload. Do not retry LOAD or manually UNLOAD; preserve the VM,
debugger, and mailbox evidence.

Gate 54 passed on 2026-09-05 for source commit `db3f206`. It denied short and
oversized legacy STATUS and observation responses with Win32 error 24,
accepted the exact 16-byte and 240-byte frames, recovered all four exact
generation-1 sequence-2 observation records from a fresh session, and
unloaded canonically. The evidence archive SHA-256 is
`D1302436A2B6167F8A98BF81A13092607004DFD05A7FA902B7E5D3BCBC92E716`;
the closed KD log SHA-256 is
`EAE48FEFAEA0A6A0CC5D2574ECE22468D2D13F790DE35219FDF970EEECAE3AC9`.
Runtime V16 is closed; do not repeat Gate 54.

## Gate 55: referenced process-target lifecycle

`gate55-process-target-lifecycle.ps1` is the only necessary Runtime V17 live
boundary. Run it once from the clean two-group/four-processor disposable VM
checkpoint through the host runner and headless KD using its matched signed
stage.

The gate starts `hypervisor_loader.exe load-target-self`. That single process
configures its own referenced kernel identity while detached, loads the
runtime, verifies the exact redacted ACTIVE state, and completes the self-target
CR3 boundary with residency `1/0/1`. The gate then terminates that process and
requires the retained PID, start key, and target generation to transition to
EXPIRED with the route-active flag cleared.

A fresh negotiated session must prove that a CR3 residency request now fails
closed with public NOT_ACTIVE status before any boundary execution. The gate
then unloads canonically and requires service absence, clean failure events,
and no new dump.

One run is sufficient because process identity and exit notification are the
new deterministic boundary; accepted lifecycle stress gates already cover the
unchanged VMX startup and teardown. Stop on any mismatch, timeout, hang,
bugcheck, event, dump, or uncertain unload. Do not automatically retry.

Gate 55 passed on 2026-09-05 for implementation source commit `484e32a`.
It proved CONFIGURED then ACTIVE target state, exact self-target residency
`1/0/1`, the same nonzero start key and generation after process exit, EXPIRED
state with flags `0x00000003`, a fail-closed `0x80000001` CR3 probe, canonical
unload, and service absence. The accepted package SHA-256 is
`CC4BCD04D8150476456645C865C1E516B465282B153F7D580DFB754D4FD4769D`;
the evidence archive SHA-256 is
`BC6011360050E88596306CA76FDA91365DCF7C887E45C9F10B521433FD559676`;
the closed KD log SHA-256 is
`0866629C8AF18A2306E0D62E35C6C66A2DA29A62139C5CC153B7052DA3EF26E8`.
Runtime V17 is closed; do not repeat Gate 55.

## Gate 56: explicit process-target rebind

`gate56-process-target-rebind.ps1` is the only necessary Runtime V18 live
boundary. Run it once from the clean two-group/four-processor disposable VM
checkpoint through the host runner and headless KD using its matched signed
stage.

The gate starts `hypervisor_loader.exe load-target-self-rebind`. On one
negotiated device session, the loader configures its referenced process target,
releases it while detached, verifies the canonical EMPTY response, configures
the same live process again, and loads the runtime. It then proves the exact
self-target CR3 boundary and verifies that an ACTIVE release request is denied
with BUSY before publishing READY.

The gate terminates the target process, requires the exact EXPIRED identity and
fail-closed CR3 behavior already established by Gate 55, then unloads
canonically. Service absence, clean failure events, and no new dump remain
mandatory.

One run is sufficient because the new boundary is deterministic process-object
release and reacquisition. Accepted lifecycle stress gates cover unchanged VMX
startup and teardown. Stop on any mismatch, timeout, hang, bugcheck, event,
dump, or uncertain unload. Do not automatically retry a true live failure.

Gate 56 passed on 2026-09-05 for implementation source commit `f0420a4` and
the corrected gate harness at `bb8a7af`. It proved CONFIGURED to EMPTY to
CONFIGURED to ACTIVE to EXPIRED, exact self-target residency `1/0/1`, BUSY
denial of active release, fail-closed `0x80000001` routing after target exit,
canonical unload, and service absence. The accepted package SHA-256 is
`C844DF322FD1D8806277919DFAE51BC3025FB5309FE7284077EE3A6BB0380F19`;
the evidence archive SHA-256 is
`B662684C91F16C166246A9DC2C26AACD95B84513AC6793BA4ADA8764D139B86A`;
the closed KD log SHA-256 is
`35C89FD709CA1EFC25745D7082904B5AB6B35BB95BE3CCBE5D2580F5DA30B4CA`.
Runtime V18 is closed; do not repeat Gate 56.

## Gate 57: generation-matched process-target release

`gate57-process-target-matched-release.ps1` is the only necessary Runtime V19
live boundary. Run it once from the clean two-group/four-processor disposable
VM checkpoint through the host runner and headless KD using its matched signed
stage.

The gate starts
`hypervisor_loader.exe load-target-self-matched-rebind`. On one negotiated
device session, the loader configures itself and retains the exact PID, process
start key, and device-lifetime target generation. It proves that a stale
generation returns STALE without changing the CONFIGURED record, releases the
target with the exact identity to canonical EMPTY, and reconfigures the same
process with a strictly different generation. After LOAD, the exact matched
release is denied with BUSY while ACTIVE.

The remaining target-expiry, fail-closed CR3 route, canonical unload, service
absence, failure-event, and dump checks are unchanged from Gate 56. One run is
sufficient because this gate changes only the deterministic compare-and-release
predicate. Stop on any mismatch, timeout, hang, bugcheck, event, dump, or
uncertain unload. Do not automatically retry a true live failure.

Gate 57 passed on 2026-09-05 for implementation source commit `ace5b15`. It
proved a preserved CONFIGURED record after STALE generation denial, canonical
EMPTY after an exact matched release, same-process rebind from generation 1 to
2, ACTIVE BUSY denial, self-target residency `1/0/1`, sticky EXPIRED state,
fail-closed `0x80000001` routing, canonical unload, and service absence. The
accepted package SHA-256 is
`0312C3C1C5FF04BC298D565C62774822139CD03CF6B8CE01837E5A9C4A988DB7`;
the evidence archive SHA-256 is
`FA6978BE8DDB1D285463F8C594BFA6DF203429A10F2590F613C89652A4562F62`;
the closed KD log SHA-256 is
`6CBFCADF4AE0B5FF0280320CA9F06E4C38FD362CC81F1ECCD40BCF29B3765AEF`.
Runtime V19 is closed; do not repeat Gate 57.

## Gate 58: session-owned process target

`gate58-session-owned-process-target.ps1` is the only necessary Runtime V20
live boundary. Run it once from the clean two-group/four-processor disposable
VM checkpoint through the host runner and headless KD using its matched signed
stage.

The gate starts `hypervisor_loader.exe load-target-session-owner`. The loader
sends an exact configure-self request containing no PID, process handle,
pointer, or CR3 value. The driver derives the target from the authenticated
control-session owner's referenced process object, takes an independent target
reference, and returns the existing redacted CONFIGURED identity. The loader
requires that returned PID to equal its current process before LOAD.

The gate then proves the unchanged ACTIVE self-target CR3 residency boundary,
terminates the target process, requires sticky EXPIRED identity and fail-closed
routing, and unloads canonically. Service absence, clean failure events, and no
new dump remain mandatory. One run is sufficient because only the deterministic
process-reference acquisition path changed. Stop and preserve evidence on any
true live failure; do not retry automatically.

Gate 58 passed on 2026-09-05 for implementation source commit `c861d05`.
It proved the no-PID session-owner configuration path, exact returned target
identity, ACTIVE self-target residency `1/0/1`, sticky EXPIRED state,
fail-closed `0x80000001` routing, canonical unload, and service absence. The
accepted package SHA-256 is
`9DEF0816549C71D1A5B385BCFC4EAC3E48A452E423EDCD8192E734F2E001182B`;
the evidence archive SHA-256 is
`7D1D0CD58E407A15C2A548C9E3162F38B349B5C977780C3D875D23EB8C60A44C`;
the closed KD log SHA-256 is
`4757E16C0C99E6D1E0DE08AB7014C6127D6752C8B4B94C4D0F8ABBE213158AF8`.
Runtime V20 is closed; do not repeat Gate 58.

## Gate 59: authenticated control capability manifest

`gate59-control-capability-manifest.ps1` is the only necessary Runtime V21
live boundary. Run it once through the host runner from the clean
two-group/four-processor disposable VM snapshot with its matched signed stage.

The loader negotiates protocol `1.0` and immediately queries the exact
capability manifest on every device open. The gate requires version 1, bitmap
`0x00000000000003FF`, 29 operational IOCTLs, and the exact success proof on
three independent LOAD, status, and UNLOAD sessions. It then requires the
unchanged persistent/EPT readiness, authoritative status, canonical unload,
service absence, clean failure events, and no new dump.

One run is sufficient because the manifest is immutable compile-time metadata
and the only live risk is its new fail-closed position in every loader open.
Stop and preserve evidence on any true mismatch, hang, bugcheck, event, dump,
or uncertain unload. Do not retry automatically.

Gate 59 passed on 2026-09-05 for implementation source commit `1545845`.
It proved exact capability version 1, bitmap `0x00000000000003FF`, and 29
operational IOCTLs on independent LOAD, status, and UNLOAD sessions, followed
by canonical unload and service absence. The accepted package SHA-256 is
`C105CBC196A8B486332D696394C8783863072526061C71936486FBD8D3FA196C`;
the evidence archive SHA-256 is
`3F8FBE4973EDF289A270E820A074536910280F3A45FAD69F56ABB3DCC57FA297`;
the closed KD log SHA-256 is
`B507BB62049BBCC591865D7119B0B3297FA920EC452E185DB38C99286A6985B7`.
Runtime V21 is closed; do not repeat Gate 59.

## Gate 60: fail-closed VMX substrate admission

`gate60-vmx-substrate-admission.ps1` is the only necessary Runtime V22 live
boundary. Run it once through the host runner from the clean
two-group/four-processor disposable VMware snapshot with its matched signed
stage.

Before SCM mutation, the loader must report `VMX substrate: VMWARE_NESTED` and
the exact admission proof. The authenticated kernel LOAD path independently
repeats the same pure classification after detached-owner proof and before
runtime configuration. The closed KD log must contain the exact class-2
admission marker before normal persistent start output.

The gate preserves the Runtime V21 manifest checks on independent LOAD,
status, and UNLOAD sessions, followed by exact persistent/EPT readiness,
authoritative status, canonical unload, service absence, clean failure events,
and no new dump. Pure tests provide Microsoft Hyper-V, unknown-vendor, and
malformed-input rejection evidence; do not reconfigure the VM into an
unsupported substrate merely to repeat those negative cases.

One run is sufficient because only the deterministic pre-mutation admission
boundary changed. Stop and preserve evidence on any mismatch, hang, bugcheck,
event, dump, or uncertain unload. Do not retry automatically.

Gate 60 passed on 2026-09-05 for source commit `722073b`. The loader admitted
exact `VMWARE_NESTED` before SCM mutation, and the closed KD log recorded
`hypersinv: VMX substrate admitted class=2 before runtime configuration`.
Persistent/EPT readiness, the exact Runtime V21 capability manifest on LOAD,
status, and UNLOAD, canonical teardown, service absence, empty stderr, clean
failure events, and no new dump were preserved. The accepted package SHA-256
is `D49020C6EF09907B23DC904F547979883EF11AB912371FC1C947F04F19CC0BD1`;
the evidence archive SHA-256 is
`173F014CA706CEC124F4644FE4EEEECDAE754E87CFE4B20ABDC929DA40117887`;
the closed KD log SHA-256 is
`A0FB3FB6A7AEAE5561D3433F2D77D9399BDE68C4CFB057172613A7009AA1BEBA`.
Runtime V22 is closed; do not repeat Gate 60.

## Gate 61: topology-wide VMX substrate consensus

`gate61-vmx-substrate-consensus.ps1` is the only necessary Runtime V23 live
boundary. Run it once through the existing host-runner broker from the clean
two-group/four-processor disposable VMware snapshot with its matched signed
stage. No new Windows UAC prompt or GUI step is required.

The loader must retain the exact Runtime V22 `VMWARE_NESTED` admission before
SCM mutation. The kernel must then visit all four system processors across both
groups, restore affinity after every observation, and complete one identical
class-2 consensus before runtime configuration. The closed KD log must contain:

```text
hypersinv: VMX substrate consensus admitted class=2 processors=4 before runtime configuration
```

The gate preserves the exact capability manifest on LOAD, status, and UNLOAD,
persistent/EPT readiness, canonical unload, service absence, clean failure
events, empty stderr, and no new dump. Pure and mutation tests cover rejected,
mixed, malformed, incomplete, and weakened-consensus cases.

One run is sufficient because the new live boundary is the deterministic
group-aware affinity traversal. Stop and preserve the VM, debugger, and logs on
any mismatch, hang, bugcheck, event, dump, or uncertain unload. Do not retry
automatically.

Gate 61 passed on 2026-09-05 for source commit `abd5ea8`. The loader retained
exact `VMWARE_NESTED` admission before SCM mutation, and the closed KD log
recorded exact class-2 consensus across all four processors before runtime
configuration. Persistent/EPT readiness, the exact Runtime V21 capability
manifest on LOAD, status, and UNLOAD, canonical teardown, service absence,
empty stderr, clean failure events, and no new dump were preserved. The
accepted package SHA-256 is
`A79F73F4B28093251C47A3FF316009769B38863AF72A6680BF8B8E524178902D`;
the evidence archive SHA-256 is
`418F74E4CF57BD3D46C424985E7FA8E381DB6C13176D96F86DCAE433CB8A8B79`;
the closed KD log SHA-256 is
`113CA685CE33E26F2FF614B19B8DAABEAB00257F8DF1D02CAB73B17998EDB6E0`.
Runtime V23 is closed; do not repeat Gate 61.

## Gate 62: topology-wide VMX preentry readiness

`gate62-vmx-preentry-readiness.ps1` is the only necessary Runtime V24 live
boundary. Run it once through the existing host-runner broker from the clean
two-group/four-processor disposable VMware snapshot with its matched signed
stage. No new Windows UAC prompt or GUI step is required.

After the accepted substrate consensus and before runtime configuration, the
kernel must sample all four processors and admit identical VMX preentry state.
The closed KD log must contain one line matching this exact field order:

```text
hypersinv: VMX preentry consensus admitted revision=<decimal> processors=4 vmx_basic=0x<hex> before runtime configuration
```

The raw VMX-basic value in that marker must be the value handed directly to the
runtime configuration builder. Per-processor preentry validation remains active
immediately before VMX mutation. The gate preserves loader substrate admission,
the exact capability manifest on LOAD/status/UNLOAD, persistent/EPT readiness,
canonical unload, service absence, clean failure events, empty stderr, and no
new dump.

One run is sufficient because the new live boundary is deterministic
group-aware CR4/CPUID/MSR sampling and its configuration handoff. Pure and
mutation tests cover rejected, nonuniform, malformed, incomplete, and bypassed
cases. Stop and preserve the VM, debugger, and logs on any true failure; do not
retry automatically.

Gate 62 passed on 2026-09-05 for source commit `55d1b73`. The closed KD log
recorded revision `1`, all four processors, and identical raw VMX-basic value
`0x1d8100000000001` before runtime configuration. Loader substrate admission,
persistent/EPT readiness, the exact capability manifest on LOAD, status, and
UNLOAD, canonical teardown, service absence, empty stderr, clean failure
events, and no new dump were preserved. The accepted package SHA-256 is
`606E79BB019502D34079954E694534D380EEC6CB5FCED6508EFCC029D0F158C6`;
the evidence archive SHA-256 is
`CFB641B08A0D97D40E27A6387D56F5653F3A0B1C9DE4B96A98D8B716A65E23B8`;
the closed KD log SHA-256 is
`F3C0079504449C623D9432DB8567112D7E79A697C7BBD9CB10FF3CBA029B8F73`.
Runtime V24 is closed; do not repeat Gate 62.

## Gate 63: production-boundary release candidate

`gate63-production-boundary-release-candidate.ps1` is the single final live
check after quarantining the historical VM-exit model and deleting unsupported
CPU/MTRR hardware wrappers from the production core. The package must carry a
hash-pinned release manifest for a clean pushed commit and a driver signed by
`CN=hypersinv` without private-key material.

Run Gate 63 once through the existing host-runner broker on the clean
two-group/four-processor disposable VMware snapshot. It must preserve exact
loader substrate admission, the capability manifest, persistent/EPT readiness,
authoritative status, canonical unload, service absence, clean events, empty
stderr, and no new dump. The closed KD log must retain both accepted markers:

```text
hypersinv: VMX substrate consensus admitted class=2 processors=4 before runtime configuration
hypersinv: VMX preentry consensus admitted revision=<decimal> processors=4 vmx_basic=0x<hex> before runtime configuration
```

One run is sufficient. Do not retry LOAD or repeat the gate after a failure;
preserve the VM, debugger, mailbox result, evidence archive, and KD log.

## Gate 64: authenticated HLT VM-exit roundtrip

`gate64-hlt-exit-roundtrip.ps1` is the only necessary Runtime V25 live
boundary. Run it once through the existing host-runner broker from the clean
two-group/four-processor disposable VMware snapshot with its matched signed
stage. No new Windows UAC prompt or GUI step is required.

After LOAD, the loader runs one `hlt-probe`. It must prove HLT exiting was
initially clear, was the only primary-control bit armed for the probe, and was
restored to the exact original control value after the guest resumed. The
closed KD log must contain one exact observation in this field order:

```text
hypersinv: HLT_VMEXIT_OBSERVATION reason=12 length=1 primary-before=0x<8 hex> primary-armed=0x<8 hex> primary-after=0x<8 hex> completion=0x484C5431
```

The armed value must equal `before | 0x80`; the after value must equal before.
The log must also contain a successful `HLT_EXIT_PROBE boundary` marker. The
gate preserves the current `0x7FF`/30-operation capability manifest,
persistent/EPT readiness, authoritative status, canonical unload, service
absence, empty stderr, clean failure events, and no new dump.

One run is sufficient because the new live boundary is the temporary VMCS
control transition, real HLT exit, dispatcher resume, and exact restoration.
Do not retry LOAD after a failure; preserve all VM, debugger, mailbox, and
evidence state for investigation.

The first Gate 64 run, RunId
`gate64-hlt-exit-roundtrip-20260906-010439-6bc9eb0b2f`, is retained as failure
evidence and must never be reused. It exposed a missing
`VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE` admission in the MASM diagnostic
return gate. Use only a fresh signed package and fresh RunId containing the
corrected resident-return gate after the preserved failed VM has been safely
reverted to the clean snapshot.

The corrected package run
`gate64-hlt-exit-roundtrip-20260906-085448-e93a816374` is also retained as
failure evidence and must never be reused. It proved the corresponding
`VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED` admission was missing from the MASM
VMRESUME gate. The next package must contain both the HLT-armed resume
admission and HLT-complete resident-return admission, and it must use another
fresh RunId after reverting the preserved VM.

Run `gate64-hlt-exit-roundtrip-20260906-090644-172664f1b0` proved the HLT
runtime transaction and KD boundary but is not final acceptance evidence. Its
PowerShell harness rejected valid `PRIMARY_CONTROLS_*` output because the
captured Windows lines ended in CRLF and the multiline regexes required LF.
The corrected harness permits the optional carriage return. Use another fresh
package and RunId after reverting the VM; the rerun is required to reach and
prove canonical unload, service absence, clean events, and no new dump.

Gate 64 was accepted by RunId
`gate64-hlt-exit-roundtrip-20260906-093922-e27dda76db`. Do not repeat it.

## Gate 65: diagnostic admission matrix

`gate65-diagnostic-admission-matrix.ps1` requalifies the shared C/MASM
diagnostic-state admission contract. Run it once through the host-runner broker
from the clean two-group/four-processor disposable VMware snapshot.

One LOAD must remain resident while the gate executes the existing EPT,
CPUID, MSR-read, TSC, INVLPG, CET-VMCS, and HLT diagnostics in that order. The
guest script verifies each loader completion, authoritative status, canonical
unload, service absence, empty stderr, clean events, and no new dump. The
closed KD log must contain one typed completion marker for each operation and
must not contain a fatal exit, bugcheck, or fail-closed teardown marker.

This is one composite lifecycle, not a repetition gate. Its purpose is to prove
that all seven authenticated odd/even state pairs still cross the live MASM
resume and return boundaries after the list-to-range refactor. Do not retry a
failed RunId; preserve the VM, debugger, mailbox, and evidence for diagnosis.

Gate 65 was accepted under RunId
`gate65-diagnostic-admission-matrix-rerun-20260906-131352-a9c609baa5` for
source commit `3ce937d773c5e0c52df6a461fd6f3305061b295a`. Package SHA-256:
`21DEED9A4A87927D21B354659B5D74CBD7FE77AC718A8EFC83CB368E8046A248`.
Evidence archive SHA-256:
`6FF8B5CF25CEB477843F5ADCB47731385AFED968D2F2B6213D225EB9E7AE3CE4`.
Closed KD log SHA-256:
`A162EC10AC41B356689C7260F495F195E6590D91067E3D771BFEBCF4EC6536F7`.

The accepted run recorded each armed state 1, 3, 5, 7, 9, 11, and 13 and each
matching complete state 2, 4, 6, 8, 10, 12, and 14 exactly once. The first
package also completed all runtime operations and unloaded cleanly, but the
harness incorrectly expected CR3 observation to remain INACTIVE after the EPT
roundtrip. Commit `3ce937d` changed that exact expectation to OBSERVED and
added mutation coverage; the rerun then passed with empty stderr, no fatal KD
marker, no failure event or dump, and canonical service removal.

## Gate 66: diagnostic target ownership boundary

`gate66-diagnostic-payload-ownership.ps1` qualifies Runtime V27 once from the
clean two-group/four-processor disposable VMware snapshot. It runs the same
seven-operation composite lifecycle as Gate 65. Generic and structured
diagnostics copy borrowed payloads into the persistent launch context's owned
80-byte storage before entering non-root mode. The EPT diagnostic is the sole
direct-target exception: it authenticates and writes the exact retained driver
lifecycle page (`DRIVER_LIFECYCLE_PAGE`) so the write reaches the installed EPT
hook.

The gate must complete EPT, CPUID, MSR-read, TSC, INVLPG, CET-VMCS, and HLT in
one LOAD; verify authoritative status; unload canonically; and preserve empty
stderr, service absence, clean events, and no new dump. Qualify the closed KD
log for one typed completion marker per operation and no fatal, bugcheck, or
fail-closed teardown marker.

The PASS summary must report
`StructuredPayloadOwner=PERSISTENT_LAUNCH_CONTEXT` and
`EptTargetOwner=DRIVER_LIFECYCLE_PAGE`. This is a target-ownership boundary
test, not a repetition gate. Do not retry a failed RunId. Preserve the VM,
debugger, mailbox, and evidence for diagnosis.

## Gate 67: VMX preemption watchdog

`gate67-preemption-watchdog.ps1` is the single Runtime V28 live boundary. Run
it once through the host-runner broker from the clean two-group/four-processor
disposable VMware snapshot using the matched signed stage.

The gate runs EPT, CPUID, MSR-read/write, TSC, INVLPG, CET-VMCS, and HLT under
one resident LOAD, then verifies authoritative status and canonical unload.
The closed KD log must prove one topology-wide timer mode. A supported surface
uses:

```text
hypersinv: VMX preentry consensus admitted revision=<decimal> processors=4 vmx_basic=0x<hex> preemption-timer=1 preemption-rate=<decimal> before runtime configuration
hypersinv: VMX_PREEMPTION_TIMER_READY rate=<decimal> value=0xffffffff pin=0x<hex> exit=0x<hex>
hypersinv: VMX_PREEMPTION_TIMER_ARMED state=<decimal> value=0xffffffff
```

An unsupported nested surface uses:

```text
hypersinv: VMX preentry consensus admitted revision=<decimal> processors=4 vmx_basic=0x<hex> preemption-timer=0 preemption-rate=0 before runtime configuration
hypersinv: VMX_PREEMPTION_TIMER_UNAVAILABLE pin-allowed=0x<hex>
hypersinv: VMX_PREEMPTION_TIMER_BYPASSED state=<decimal> pin=0x<hex>
```

For the supported mode, require four READY markers with one common rate and
the save-on-exit bit clear, plus eleven ARMED markers. For the unsupported
mode, require four UNAVAILABLE markers and eleven BYPASSED markers. In either
mode, transaction states 1, 3, 5, 7, 9, 11, and 13 occur once for the seven
diagnostics and state 0 occurs four times because unload executes once on each
processor. Reject mixed timer modes, exit-reason-52 failure (`fail=0x10a`),
fatal-system-error, bugcheck, or fail-closed teardown markers.

The PASS summary must report `Probe=VMX_PREEMPTION_WATCHDOG`,
`PreemptionTimer=CAPABILITY_QUALIFIED`, `PreemptionTimerValue=EXTERNAL_KD`,
seven completed operations, and `ServiceAfterUnload=ABSENT`. One run is
sufficient. This gate does not deliberately hang a guest, and no 10/100-cycle
repetition is planned
unless the first evidence reveals an intermittent defect.

## Gate 68: exact current-VMCS binding attestation

`gate68-vmcs-binding-attestation.ps1` is the single Runtime V29 live boundary.
Run it once through the host-runner broker from the clean
two-group/four-processor disposable VMware snapshot using its matched signed
stage.

The gate runs EPT, CPUID, MSR-read/write, TSC, INVLPG, CET-VMCS, and HLT under
one resident LOAD, then verifies authoritative status and canonical unload.
Persistent composition must execute VMPTRST and compare hardware's current
VMCS with the exact tracked per-processor VMCS before every dependent boundary.
The closed KD log must contain these marker forms:

```text
hypersinv: VMCS_BINDING_ATTESTED phase=launch state=0
hypersinv: VMCS_BINDING_ATTESTED phase=resume state=<decimal>
```

Require four launch/state-0 markers, one resume marker for each diagnostic
state 1, 3, 5, 7, 9, 11, and 13, and four resume/state-0 unload markers. That
is fifteen attestations in total. Reject any occurrence of:

```text
hypersinv: VMCS_BINDING_MISMATCH
```

Also reject fatal-system-error, bugcheck, or fail-closed teardown markers. The
PASS summary must report `Probe=VMCS_BINDING_ATTESTATION`,
`VmcsBinding=EXACT_CURRENT`, `VmcsBindingAttestations=15`,
`KernelVmcsEvidence=EXTERNAL_KD`, seven completed operations, and
`ServiceAfterUnload=ABSENT`.

One run is sufficient because the new invariant is checked deterministically
at every external VM-entry boundary in the composite lifecycle. No 10/100-cycle
repetition gate is planned unless Gate 68 evidence reveals an intermittent
defect. Preserve the VM, debugger, mailbox result, evidence archive, and KD log
on any mismatch, hang, crash, dump, event, stderr, or uncertain unload; do not
retry the same RunId.

Gate 68 was accepted for source commit
`67b30aa560e1aee4c1d8c4c7a2b703f112e0a253` under RunId
`gate68-vmcs-binding-attestation-20260906-185511-cb77ea61f3`. The closed KD
log contained exactly four launch attestations, one resume attestation for
each diagnostic state 1, 3, 5, 7, 9, 11, and 13, four state-0 unload-resume
attestations, and no mismatch or fatal marker. The guest completed all seven
operations with empty stderr, canonical unload, service absence, clean events,
and no new dump. Evidence archive SHA-256:
`8BCEDBB8F6B57BD7E295F94C68D395CB4FBD615C792D7F0F0B780493D9270AF1`.
Closed KD log SHA-256:
`7EA09455E07F373840B261FF2160BA137CBA532C0C9CF88F4A0F6A303905120E`.
Runtime V29 is closed; do not repeat Gate 68.

## Gate 69: non-migratable VMCS execution context

`gate69-vmcs-execution-context.ps1` is the single Runtime V30 live boundary.
Run it once through the host-runner broker from the clean
two-group/four-processor disposable VMware snapshot using its matched signed
stage.

Before the shared composition boundary executes VMPTRST, it must prove the
current IRQL is exactly DISPATCH_LEVEL. This makes the hardware VMCS
attestation non-migratable until its dependent VMX instruction. The closed KD
log must retain exactly four launch and eleven resume
`VMCS_BINDING_ATTESTED` markers: diagnostic states 1, 3, 5, 7, 9, 11, and 13
once each, plus state 0 four times during unload. Reject either failure marker:

```text
hypersinv: VMCS_EXECUTION_CONTEXT_MISMATCH
hypersinv: VMCS_BINDING_MISMATCH
```

Also reject fatal-system-error, bugcheck, watchdog-expiry, or fail-closed
teardown markers. The PASS summary must report
`Probe=VMCS_EXECUTION_CONTEXT`, `VmcsBinding=EXACT_CURRENT`,
`VmcsExecutionAttestations=15`, `VmcsExecutionLevel=DISPATCH_LEVEL`,
`KernelVmcsContextEvidence=EXTERNAL_KD`, seven completed operations, and
`ServiceAfterUnload=ABSENT`.

One run is sufficient. Gate 69 proves all fifteen legitimate external
boundaries satisfy the new local execution-level invariant; 10/100-cycle
repetition would not qualify a distinct behavior. Preserve the VM, debugger,
mailbox result, evidence archive, and KD log on any mismatch, hang, crash,
dump, event, stderr, or uncertain unload; do not retry the same RunId.
