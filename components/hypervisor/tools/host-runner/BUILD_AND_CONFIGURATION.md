# Host runner build and configuration guide

This guide explains how to adapt the PowerShell host runner to another
disposable VMware Workstation VM. The runner has no compilation step: its
"build" is a pinned copy of the complete `tools/host-runner` directory followed
by static contract verification.

Computer Use is not a prerequisite. Host and guest work is performed through
PowerShell, the fixed elevated broker, `vmrun`, a shared-folder mailbox, an
elevated guest scheduled task, and headless `kd.exe`. This is deliberate: live
gate correctness must not depend on GUI focus, screenshots, or UI automation.

For two different VMs on one host, also follow
[`CONCURRENT_VM_TESTING.md`](CONCURRENT_VM_TESTING.md). That contract is
provider-neutral and applies equally to VMware/Windows, QEMU/Linux, or mixed
runner pairs. The shared lease helper supports the required resource scopes;
each participating runner must still implement and verify the remaining
isolation and capacity contract.

The runner is intentionally narrow. It executes signed `gate<number>-<name>.ps1`
packages in one Windows guest, attaches one headless KD client through a VMware
named serial pipe, and preserves the VM on any non-PASS result. Do not generalize
it into an arbitrary remote-command service.

## 1. Required files

Keep these files together:

```text
tools/host-runner/
  Invoke-HypervisorHostRunner.ps1
  Invoke-HypervisorHostBroker.ps1
  HostRunner.Broker.psm1
  HostRunner.Core.psm1
  HostRunner.CredentialManager.psm1
  HostRunner.Debugger.psm1
  HostRunner.Vmware.psm1
  host-runner.config.example.json
  guest/
  tests/
```

Do not copy `host-runner.config.json` between machines. It contains local paths
and machine-specific policy, even though it contains no password.

## 2. Host prerequisites

- Windows host with VMware Workstation and `vmrun.exe`.
- Windows Debugging Tools with x64 `kd.exe`.
- Windows PowerShell 5.1 or a compatible PowerShell host.
- A disposable Windows test VM with a known-good powered-off snapshot.
- A VMware shared folder dedicated to this runner.
- A VMware named-pipe serial device dedicated to kernel debugging.
- Enough free host RAM for the VM plus the configured reserve.

The current runner also requires the test VM to have no connected/present NIC,
3D acceleration disabled, and both active and snapshot-restored graphics
apertures no larger than 256 MB. These are enforced safety checks, not advice.

## 3. Prepare the guest

Configure the disposable Windows guest for kernel debugging over COM1 at
115200 baud. Configure VMware serial device 0 to use a local named pipe with
the VM as the pipe server. The VMX must contain equivalent settings to:

```text
serial0.present = "TRUE"
serial0.fileType = "pipe"
serial0.fileName = "\\\\.\\pipe\\<unique-pipe-name>"
serial0.pipe.endPoint = "server"
serial0.tryNoRxLoss = "FALSE"
```

Create a host mailbox directory and expose it to the VM as a writable VMware
shared folder. The installer defaults to the share name `HypersinvRunner`.
For another share name, pass the complete guest UNC path explicitly.

From an elevated PowerShell window inside the disposable VM:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force

& C:\Path\To\guest\Install-HypersinvGuestMailbox.ps1 `
    -MailboxRoot '\\vmware-host\Shared Folders\HypersinvRunner'

Get-ScheduledTask -TaskName HypersinvGuestMailbox |
    Format-List TaskName, State

sc.exe query hypersinv
```

The task must be running and `sc.exe` must report service error 1060 before the
snapshot is accepted. Power off the VM and create exactly one clean snapshot
after guest configuration is verified.

## 4. Configure the host

Copy the example configuration:

```powershell
$RunnerRoot = 'C:\path\to\tools\host-runner'
Copy-Item `
    -LiteralPath "$RunnerRoot\host-runner.config.example.json" `
    -Destination "$RunnerRoot\host-runner.config.json"
```

Edit only `host-runner.config.json` and set:

| Field | Required value |
| --- | --- |
| `Vm.VmxPath` | Absolute path to the one disposable VMX. |
| `Vm.DisplayName` | Exact VMX `displayName`. |
| `Vm.SnapshotName` | Exact known-good, powered-off snapshot name. |
| `Vm.DebuggerPipe` | Exact local pipe used by `serial0.fileName`. |
| `Tools.VmrunPath` | Absolute path to VMware `vmrun.exe`. |
| `Tools.KdPath` | Absolute path to x64 `kd.exe`. |
| `Tools.PoolmonPath` | Absolute path to Poolmon, when diagnostics are enabled. |
| `Tools.PoolTagPath` | Absolute path to the Poolmon tag map. |
| `Mailbox.HostRoot` | Dedicated host directory exposed to the guest. |
| `Policy.MaximumRuntimeSeconds` | Gate timeout, from 30 through 7200 seconds. |
| `Credential.TargetName` | Unique `HypersinvHostRunner/<vm-name>` identifier. |

Use `vm-hypervisor-v3h-gate4` as this VM's machine-wide lease resource unless
the VM identity is deliberately replaced and the documentation is updated.

Use a unique mailbox directory, credential target, and debugger pipe for every
VM. Never point two runner configurations at the same mailbox.

Concurrent-capable configurations additionally require a stable unique VM
resource ID, broker state root, debugger state/log root, evidence root, and
RunId namespace. They must use a resource-scoped per-VM lease plus the short
machine-wide `host-vm-startup` lease described in
`CONCURRENT_VM_TESTING.md`. Separate app-server or debugger ports alone are not
sufficient isolation.

The debugger limits are host-capacity policy. Begin conservatively:

- `HostMemoryReserveMb`: RAM that must remain available in addition to VM RAM.
- `MinimumHostFreeMemoryMb`: hard steady-state free-memory floor.
- `MaximumDebuggerWorkingSetMb`: upper bound for the owned KD process.
- `MaximumStartupLogBytes` and `MaximumRunLogBytes`: bounded log sizes.

Do not reduce these limits merely to force a failing preflight to pass. Reduce
guest RAM or other host workloads first and document the accepted topology.

## 5. Store the VM credential

Run this once under the Windows account that will operate the runner:

```powershell
$Runner = 'C:\path\to\tools\host-runner\Invoke-HypervisorHostRunner.ps1'

& $Runner -Action StoreVmCredential
& $Runner -Action CredentialStatus
```

The secret is stored as a generic Windows Credential Manager credential under
the configured target. It is protected for the current Windows user through
Windows/DPAPI and is not written to the repository or mailbox.

## 6. Validate the installation

These checks are host-only and do not start the VM:

```powershell
Set-Location C:\path\to\project-worktree

& .\tools\host-runner\tests\Test-HostRunnerContracts.ps1
& .\tools\host-runner\Invoke-HypervisorHostRunner.ps1 -Action Doctor
```

Do not continue until the contract test passes and `Doctor` reports the exact
VM, snapshot, tools, mailbox, and stored credential expected for this machine.

## 7. Package contract

The guest deliberately accepts only a SHA-256-pinned ZIP whose selected script
has a canonical `gate<number>-<name>.ps1` name. The package must contain:

- the gate script;
- `SHA256SUMS.txt` covering the staged files;
- the signed driver and matching runtime artifacts required by that gate.

The current guest policy requires a valid driver signature whose signer subject
is exactly `CN=hypersinv`. Forks must change that policy deliberately, update
the static tests, and retain an equally strict signer check.

## 8. Per-boot and per-gate workflow

Start the elevated broker once per Windows boot:

```powershell
& $Runner -Action StartBroker
& $Runner -Action BrokerStatus
```

Prepare is non-live:

```powershell
$Prepared = & $Runner -Action Prepare `
    -PackagePath 'C:\path\to\signed-gate.zip' `
    -ExpectedPackageSha256 '<64-HEX-SHA256>' `
    -GateScript 'gate63-release-candidate.ps1' `
    -SymbolPath 'C:\path\to\matching-symbol-directory'

$RunId = $Prepared.RunId
```

After acquiring the shared VM-test lease described in `AGENT_OPERATIONS.md`,
run the fixed lifecycle:

```powershell
& $Runner -Action RunPreparedGate `
    -RunId $RunId `
    -AuthorizeLiveVmRun `
    -AuthorizeSnapshotRevert
```

Only PASS permits automatic KD close, VM power-off, and snapshot restoration.
For FAIL, REJECTED, HUNG, timeout, or debugger failure, preserve the VM and all
evidence. Never retry automatically. After preserving evidence, an explicit
stop-only disposition can close the owned debugger and power off the exact
configured VM without restoring its snapshot:

```powershell
& $Runner -Action PowerOffPreservedVm -AuthorizeVmPowerOff
```

The recovery request contains no VMX path or command surface; the per-boot
broker uses only its protected configuration. Reverting the failed disk state
remains a separate, explicit decision.

## 9. Porting checklist

- [ ] Runner directory copied as one versioned unit.
- [ ] Disposable VM and single clean snapshot identified.
- [ ] Guest kernel-debug boot configuration verified.
- [ ] Unique VMware serial pipe configured exactly.
- [ ] NIC absent/disconnected and 3D disabled.
- [ ] Dedicated shared mailbox visible from host and guest.
- [ ] Elevated guest mailbox task running.
- [ ] Driver service absent at the clean snapshot.
- [ ] Local configuration contains exact absolute paths.
- [ ] Unique Credential Manager target stored and verified.
- [ ] Static runner contracts pass.
- [ ] `Doctor` passes while the VM remains powered off.
- [ ] Shared VM-test lease acquired before any live action.
- [ ] If concurrency is enabled, every item in
      `CONCURRENT_VM_TESTING.md` is implemented and statically verified by
      both runners; otherwise the runner fails closed before VM mutation.

## 10. What not to customize

Do not add arbitrary executable paths, command lines, debugger commands, VMX
paths, snapshot names, or package paths to broker requests. Those omissions are
the security boundary that prevents a standard process from turning the
elevated per-boot broker into a general privileged execution service.
