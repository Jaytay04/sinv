# Hypervisor VM host runner

Additional operator documentation:

- [`BUILD_AND_CONFIGURATION.md`](BUILD_AND_CONFIGURATION.md) explains how to
  copy, configure, and validate the runner for a different disposable VMware
  test VM.
- [`AGENT_OPERATIONS.md`](AGENT_OPERATIONS.md) defines the shared-agent lease,
  Codex TUI Computer Use setup, and the single-owner live-test workflow.
- [`CONCURRENT_VM_TESTING.md`](CONCURRENT_VM_TESTING.md) defines the opt-in,
  provider-neutral isolation and two-level lease contract required before two
  different local test VMs may run concurrently.

This runner removes terminal and WinDbg GUI automation from the live-gate loop. It uses:

- a host PowerShell controller;
- a VMware shared-folder mailbox;
- a fixed, elevated guest scheduled task; and
- one headless KD process attached directly to the VMware serial pipe.

**Computer Use is not required.** The complete gate lifecycle is driven through
the PowerShell entry point, the per-boot broker, `vmrun`, the shared-folder
mailbox, the guest scheduled task, and headless `kd.exe`. Computer Use may help
an operator inspect ordinary application UI, but it is not part of the runner's
execution, debugger, or evidence contract.

The runner can retrieve the encrypted VM password from Windows Credential
Manager, revert the one configured clean snapshot, and start the configured VM
headlessly. Its optional per-boot broker accepts one UAC prompt, remains alive
only for the current Windows logon/boot, and runs later prepared gates without
another elevation prompt. It does not keep a password in repository files,
create or delete snapshots, sign binaries, or retry a failed or hung gate.

The machine-wide helper requires resource-scoped leases. Free RAM alone does
not authorize another live VM. Concurrent testing is permitted only when every
participating runner satisfies `CONCURRENT_VM_TESTING.md`; runners that omit
`-Resource` fail closed.

## Safety contract

- Run only in the disposable test VM from the configured clean checkpoint.
- `Submit` requires `-AuthorizeLiveVmRun` and refuses to run unless the exact VMX is already running.
- The guest accepts only a canonical `gate<number>-<name>.ps1` from a SHA-256-pinned ZIP.
- Mailbox messages cannot supply an executable or arbitrary arguments.
- ZIP extraction rejects rooted paths, traversal, drive-qualified paths, and duplicate files.
- The staged `SHA256SUMS.txt` and the `CN=hypersinv` driver signature must validate.
- A timeout is reported as `HUNG`; the runner does not terminate the gate, retry, reset, or revert the VM.
- A timeout also creates `C:\V3H\host-runner\QUARANTINED.json`; subsequent runner starts refuse work until the VM is inspected or reverted.
- Snapshot revert requires `-AuthorizeSnapshotRevert`, accepts no caller-supplied
  VM or snapshot path, and refuses to run while the configured VM is powered on.
- VM start requires `-AuthorizeLiveVmRun` and uses the configured VMX only.
- VM start also requires the host runner itself to be launched from an elevated
  Administrator PowerShell process. Microsoft requires elevation for kernel
  debugging through a VM serial pipe; the runner now fails before `vmrun` when
  the host token is not elevated.
- The optional broker is a manually started, hidden elevated PowerShell
  process. It creates no service, scheduled task, logon trigger, or startup
  entry and does not survive a reboot. Start it once after each boot; stop it
  explicitly with `StopBroker` or let Windows terminate it during shutdown.
- At startup the broker copies the fixed runner, modules, and configuration to
  a random `C:\ProgramData\HypersinvHostRunner\Sessions` directory, removes
  inherited permissions, grants only SYSTEM/Administrators full control and
  the initiating user read/execute, then assigns SYSTEM as owner. Later
  standard-token edits to the worktree cannot replace code executed by the
  already elevated broker.
- The broker accepts two exact request schemas. A gate request contains the
  current per-boot nonce, canonical prepared `RunId`, initiating user SID, and
  both explicit live authorizations. A recovery request contains only the
  nonce, SID, and explicit power-off authorization. Neither schema has an
  executable, command, argument, package, symbol, VMX, snapshot, or debugger
  field. The broker independently revalidates gate state before touching the
  VM, and recovery can target only the VMX path pinned in its secured config.
- A broker failure or timeout preserves the VM and debugger and never retries.
  Only after a reported `PASS` does the broker close KD, hard-stop the exact
  configured disposable VM, verify that it stopped, and restore the configured
  clean snapshot.
- After failure evidence has been preserved, `PowerOffPreservedVm` provides a
  separate stop-only disposition. It closes only the owned KD process,
  hard-stops only the exact configured VM, verifies it is absent from
  `vmrun list`, and deliberately does not restore a snapshot or rerun a gate.
- `StartVm` requires the prepared `RunId`, runs all read-only safety checks
  first, starts the VM headlessly, and immediately starts one owned direct KD
  client before any VM-visibility or mailbox polling. It waits synchronously for both
  the exact `Connected to Windows ... target` marker and the exact per-run
  marker before checking the guest mailbox. It never returns a disconnected
  debugger.
- The KD startup script is deliberately symbol-independent: it emits the exact
  run marker and immediately resumes the booting guest. It does not resolve or
  write `nt!Kd_*_Mask` before `g`; that previously held the guest at its initial
  kernel break while kernel symbols were unavailable. The current driver uses
  `DPFLTR_ERROR_LEVEL`, whose bit remains visible through Windows' default
  `WIN2000` mask, so those startup writes are unnecessary.
- KD uses `-b` to break into the kernel when the transport connection is
  established, allowing the pinned command file to emit its marker and
  immediately resume the guest. `-bonc` is intentionally not used: it only
  requests a break after the session has started and did not produce an initial
  prompt for this boot-time VMware serial connection. The
  runner retains `-noshell`. It does not use KD secure mode because this KD
  version suppresses `-logo` file creation in secure mode; the runner exposes
  no interactive or caller-controlled debugger commands, and its two fixed
  commands are `.echo <canonical-marker>` and `g`.
- KD receives that startup script through `-cfr`, not `-cf`. The runner starts
  KD while VMware is still booting; `-cfr` replays the two symbol-independent
  commands when the target restart/connection arrives instead of consuming a
  one-shot startup script before a debug target exists.
- Standalone `StartDebugger` is disabled because delaying attachment until
  after Windows has booted misses this VM's boot-time serial KD handshake.
  `WaitDebugger` is a
  connected-state assertion kept for compatibility.
- KD failure state is sticky for the process lifetime: a delayed pipe failure
  remains fatal even after later output advances the log tail. Any startup
  failure or timeout terminates only the owned KD process; the running VM is
  never stopped or reset.
- A per-run `*** Fatal System Error:` marker is also fatal. The supervised
  wait stops its owned KD process and returns immediately instead of waiting
  for the guest-result timeout. The VM remains preserved until the operator or
  agent records the failure evidence and explicitly decides its disposition.
- Before VM start, a read-only resource gate compares VMX `memsize` with
  available host RAM plus `Debugger.HostMemoryReserveMb`, rejects any other
  `vmware-vmx` or unexpected `kd.exe`, and requires every configured VM NIC to
  be absent/disconnected. It also requires 3D acceleration to be disabled and
  both `svga.graphicsMemoryKB` and the snapshot-restored
  `vmotion.svga.graphicsMemoryKB` apertures to be no larger than 256 MB. The configured serial
  device must be present, use the exact configured local named pipe, declare
  `serial0.pipe.endPoint = "server"`, and set
  `serial0.tryNoRxLoss = "FALSE"`. The runner never rewrites the VMX.
- KD uses `com:pipe,port=<pipe>,baud=115200` without `resets=0` or `reconnect`.
  Microsoft explicitly excludes VMware from those two options because its
  virtual serial pipe preserves bytes differently across reset/restart.
- After immediate post-start KD attachment has connected,
  `StartVm` atomically publishes one unique,
  canonical schema-zero readiness probe containing only `SchemaVersion` and
  `RunId`. It waits for the guest mailbox task to move that probe to its
  `.completed` path and publish the exact per-RunId `REJECTED` /
  `Unsupported request schema: 0` response, rejecting stale state and malformed
  request/response pairs. It then enforces the configured 15-second
  stabilization interval before returning. This protocol-level check is used
  instead of VMware's coarse Tools
  status output, whose `installed` result does not prove that the elevated
  mailbox task is processing requests. Free-host-memory checks run throughout readiness,
  stabilization, and the bounded KD startup loop. KD startup logs are scanned
  with fixed-size reads and a captured-length ceiling, so an actively growing
  diagnostic log cannot consume unbounded host memory. During KD/VM boot only,
  a soft free-memory-floor breach must persist for the configured two-second
  grace interval before cleanup; falling below half that floor (never less than
  512 MB) remains immediately fatal. Steady-state debugger and mailbox checks
  continue to enforce the full floor without grace.
- Live start is refused while a cFosSpeed kernel driver/service is running.
  Update or uninstall/disable that host component manually after investigating
  it; the runner never changes host network or security settings.
- A new debugger start archives the previous stdout/stderr under
  `debugger\history`; the bounded `kd-resource-samples.csv` records KD working
  set/private memory, runner working set/private memory, aggregate VMware
  working set/private memory, physical availability, system commit/cache,
  paged and nonpaged kernel pools, and system object counts during startup.
  These system values come from Windows `GetPerformanceInfo`, not repeated WMI
  queries. When mapped Poolmon diagnostics are configured, the runner captures
  exactly one `pool-nonpaged-threshold.log` after kernel nonpaged pool first
  reaches `Debugger.PoolSnapshotTriggerNonpagedMb` (2 GB by default). It writes
  an attempt marker before launching Poolmon, passes the configured pool-tag
  map, skips capture when available memory is already below the safety floor,
  and records a bounded error instead of retrying. The snapshot and its
  marker are archived with the next debugger start. Diagnostic runs can set
  `Debugger.StopAfterPoolSnapshot` to terminate the owned KD immediately after
  the capture attempt; this never stops or resets the VM. Failure evidence is
  never swallowed into an unbounded exception or overwritten. The debugger
  process has a bounded working-set limit and every health check enforces a
  minimum amount of free host RAM. A minor startup-only floor breach must
  persist for the configured grace interval, the critical floor is still
  enforced immediately.
- The VM password is stored as a generic Windows Credential Manager credential
  protected for the current Windows user. It is retrieved only for `vmrun` and
  is never written to configuration, run state, mailbox messages, or logs.

## One-time host setup

1. Copy `host-runner.config.example.json` to `host-runner.config.json` and verify every path.
2. Create the configured `Mailbox.HostRoot` directory.
3. In VMware Workstation, expose that directory to the disposable VM as a writable shared folder named `HypersinvRunner`.
4. Run the static checks:

```powershell
Set-Location <ACTIVE_WORKTREE>
& .\tools\host-runner\tests\Test-HostRunnerContracts.ps1
& .\tools\host-runner\Invoke-HypervisorHostRunner.ps1 -Action Doctor
```

`Doctor` is read-only. It verifies local tools, VM identity, snapshot metadata, and reports whether the VM is running. It does not revert or start anything.

## One-time encrypted-VM credential setup

Run the host runner under the same Windows account that will perform VM
operations. The command opens the Windows credential prompt; enter the
encrypted VMware VM password. The prefilled username is only a label and is not
used by VMware.

```powershell
$Runner = '.\tools\host-runner\Invoke-HypervisorHostRunner.ps1'

& $Runner -Action StoreVmCredential
& $Runner -Action CredentialStatus
```

Credential Manager protects the stored value using Windows/DPAPI. VMware's
`vmrun` interface still requires `-vp`, so the password is briefly present in
the short-lived `vmrun.exe` process command line and could be observed by a
sufficiently privileged local process. It is redacted from runner output.

To remove the stored credential deliberately:

```powershell
& $Runner -Action RemoveVmCredential -AuthorizeCredentialRemoval
```

## One-time guest setup

From an elevated Windows PowerShell window inside the disposable VM, copy the `guest` directory into the VM and run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
& C:\Path\To\guest\Install-HypersinvGuestMailbox.ps1
Get-ScheduledTask -TaskName HypersinvGuestMailbox |
    Format-List TaskName, State
```

Create a new clean VMware checkpoint only after verifying that the task is running and the driver service is absent. This mutates only the disposable VM.

## Restore and start the disposable VM

Snapshot revert requires explicit authorization and discards the VM state
created after that snapshot. It also cleans up only a previously owned KD
process, so no untracked debugger survives the VM transition.

```powershell
$Runner = '.\tools\host-runner\Invoke-HypervisorHostRunner.ps1'
$RunId = '<RUN-ID-FROM-PREPARE>'

& $Runner -Action RevertSnapshot -AuthorizeSnapshotRevert
& $Runner -Action StartVm -RunId $RunId -AuthorizeLiveVmRun
& $Runner -Action Doctor
```

`RevertSnapshot` performs exactly one revert to `Vm.SnapshotName` and never
starts the VM. `StartVm` performs exactly one headless VM start, immediately
launches one direct KD client, waits for a bounded synchronous connection gate,
and only then verifies that the VM is visible to `vmrun list`. On KD, VM-start,
or mailbox-readiness failure it terminates only the owned KD process and never
stops or resets the VM.

The ordering is enforced: `StartVm` performs the cFosSpeed, NIC, memory, and
unexpected-process preflight before its first `vmrun` call, verifies the exact
VM path, starts the VM, immediately creates the single KD client, proves the
debugger connection before VM-visibility polling, then publishes and validates
the schema-zero mailbox probe and completes stabilization. Both debugger output streams are monitored
with a 256-KiB startup ceiling. `Submit` additionally
requires the per-run debug-log handshake and validates the owned KD process and
per-run log (bounded at 8 MiB) immediately before and after publishing the guest
request. `Wait`
revalidates process, log, and host-resource health on every result-poll interval.

## One UAC prompt per boot

Start the broker manually after each Windows boot. This is the only UAC prompt
needed for later gates during that boot:

```powershell
$Runner = '.\tools\host-runner\Invoke-HypervisorHostRunner.ps1'

& $Runner -Action StartBroker
& $Runner -Action BrokerStatus
```

`StartBroker` is idempotent while the exact broker process is healthy. It does
not register persistence. The elevated process runs under the same Windows
account, so it can retrieve that account's DPAPI-protected VMware credential.

After a normal non-elevated `Prepare`, submit the complete fixed lifecycle to
the broker with both explicit authorizations:

```powershell
& $Runner -Action RunPreparedGate `
    -RunId $RunId `
    -AuthorizeLiveVmRun `
    -AuthorizeSnapshotRevert
```

The command waits for the broker result. On `PASS`, KD is closed and the VM is
powered off and restored to the clean snapshot. On any other result, leave the
VM and debugger untouched until the failure evidence is recorded. If the
chosen disposition is to retain the failed disk state but release host
resources, use the current broker session's stop-only action:

```powershell
& $Runner -Action PowerOffPreservedVm -AuthorizeVmPowerOff
```

This command accepts no VM path and never restores the snapshot. When no more
gates will run during the current boot:

```powershell
& $Runner -Action StopBroker
```

## Gate sequence

Prepare the run without touching the VM:

```powershell
$Runner = '.\tools\host-runner\Invoke-HypervisorHostRunner.ps1'
$Package = 'C:\path\to\signed-gate-stage.zip'
$PackageSha256 = '<64-HEX-DIGIT-SHA256>'
$Symbols = 'C:\path\to\directory-containing-hypervisor_driver.pdb'
$Gate = 'gate53-example.ps1'

& $Runner -Action Prepare `
    -PackagePath $Package `
    -ExpectedPackageSha256 $PackageSha256 `
    -GateScript $Gate `
    -SymbolPath $Symbols
```

Record the returned `RunId`. Restore and start the VM as shown above. Once the
guest mailbox task is ready:

```powershell
& $Runner -Action BeginDebugLog -RunId $RunId
& $Runner -Action Submit -RunId $RunId -AuthorizeLiveVmRun
& $Runner -Action Wait -RunId $RunId
& $Runner -Action EndDebugLog -RunId $RunId
```

The evidence and debugger log are written under:

```text
<Mailbox.HostRoot>\responses\<RunId>\
```

If `Wait` reports `FAIL`, `REJECTED`, `HUNG`, or times out, preserve the VM and debugger state. Do not submit the same run again.

## Why this is lighter than GUI automation

The steady-state host processes are `kd.exe` plus PowerShell. VMware still runs normally, but neither WinDbg Preview nor terminal-window image recognition is required for each command. The mailbox protocol also makes each live run reproducible and auditable rather than timing-dependent.
