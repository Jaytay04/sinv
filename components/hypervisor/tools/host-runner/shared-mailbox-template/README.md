# Agent shared VM-test mailbox

This machine-wide mailbox coordinates concurrent live VM testing across
agents and projects on one physical host. It supports resource-scoped leases
only.

## Resources

- `vm-<stable-id>`: one long-lived lease for one exact VM and its complete
  live-test lifecycle.
- `host-vm-startup`: short exclusive lease around final capacity checks, VM
  start, debugger start, and readiness.

`-Resource` is mandatory. Omitting it fails closed; there is no global fallback.
Resource names are lowercase ASCII, 3-64 characters, and may contain letters,
digits, dots, and hyphens. VM resources must start with `vm-`.

Different VM resources may coexist. Only one agent may hold
`host-vm-startup`, so memory/capacity checks and startup remain serialized.
After readiness is stable, release the startup lease while retaining the exact
VM lease through stop and restore.

Concurrent testing is allowed only when every runner isolates its VM
storage/snapshots, mailbox, credentials, broker state, debug transport,
debugger state/logs, evidence, and process ownership. The helper does not
replace runner capacity checks or authorize more than the documented host
limit.

## Inspect leases

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force

$Mailbox = 'C:\Users\jayli\OneDrive\Documents\Projects\AgentSharedMailbox'
$LeaseCommand = Join-Path $Mailbox 'Invoke-AgentVmLease.ps1'

& $LeaseCommand -Action Status -Resource 'vm-hypervisor-v3h-gate4'
& $LeaseCommand -Action List
```

## Acquire one VM

Acquire the VM resource before revert, start, debugger attachment, guest
submission, stop, or restore. Keep its token only in the owning session.

```powershell
$VmResource = 'vm-hypervisor-v3h-gate4'
$VmLease = & $LeaseCommand `
    -Action Acquire `
    -Resource $VmResource `
    -Agent '<agent-or-thread-id>' `
    -Project '<absolute-project-path>' `
    -RunId '<prepared-run-id>' `
    -LeaseMinutes 120

$VmLeaseToken = $VmLease.LeaseToken
```

## Serialize startup

Immediately before final memory and process-collision checks, acquire the
startup lease. Release it as soon as the exact VM and debugger are ready or
startup fails closed.

```powershell
$StartupLease = & $LeaseCommand `
    -Action Acquire `
    -Resource 'host-vm-startup' `
    -Agent '<agent-or-thread-id>' `
    -Project '<absolute-project-path>' `
    -RunId '<prepared-run-id>' `
    -LeaseMinutes 10

$StartupLeaseToken = $StartupLease.LeaseToken

# Perform final capacity/collision checks and establish readiness here.

& $LeaseCommand `
    -Action Release `
    -Resource 'host-vm-startup' `
    -LeaseToken $StartupLeaseToken
```

## Renew and release the VM resource

```powershell
& $LeaseCommand `
    -Action Renew `
    -Resource $VmResource `
    -LeaseToken $VmLeaseToken `
    -LeaseMinutes 120

# Release only after the VM is restored/powered off or its preserved failure
# state is documented and ownership is unambiguous.
& $LeaseCommand `
    -Action Release `
    -Resource $VmResource `
    -LeaseToken $VmLeaseToken
```

## Expired leases

Expiry is evidence, not permission to reset a VM. Inspect the recorded project,
RunId, VM, debugger, and owner first. Take over only the exact expired resource:

```powershell
& $LeaseCommand `
    -Action Acquire `
    -Resource $VmResource `
    -Agent '<agent-or-thread-id>' `
    -Project '<absolute-project-path>' `
    -RunId '<prepared-run-id>' `
    -LeaseMinutes 120 `
    -TakeOverExpiredLease
```

Never delete or hand-edit active state files to bypass ownership. State files
contain only token hashes; released and expired records are retained under the
resource-specific `history` directory.

## Static verification

The test uses an isolated temporary mailbox and does not touch real leases or
VMs:

```powershell
& "$Mailbox\tests\Test-AgentVmLease.ps1"
```