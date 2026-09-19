# Concurrent VM testing contract

This document defines the **opt-in** contract for running at most two live test
VMs concurrently on one physical host. It is intentionally provider-neutral:
one runner may use VMware and Windows KD while another uses QEMU, a Linux
guest, or a different debugger.

Concurrency is not inferred from free RAM alone. Two runners may overlap only
after every isolation and coordination requirement below is implemented by
both runners. Otherwise the unqualified runner must fail closed before VM
mutation.

Computer Use, Codex app-server listeners, and visible VM windows are not part
of this protocol. A correctly configured host runner needs only shell access
to its own broker and coordination mailbox. Separate Computer Use ports do not
create VM isolation.

## Current implementation status

The machine-wide `Invoke-AgentVmLease.ps1` helper requires exact `-Resource`
scoping for every operation except `List`, retains per-resource history, and
provides a serialized `host-vm-startup` lease. Its isolated functional and
acquisition-race contracts pass.

The Hypervisor VMware runner still rejects unexpected `vmware-vmx` or `kd.exe`
processes during startup. It may overlap an otherwise isolated QEMU/Linux
runner after both agents adopt this lease protocol and pass capacity checks,
but it must remain serial with another VMware/KD runner until that broad
preflight is replaced with exact owned-process attribution and mutation tests.
Do not bypass or duplicate the machine-wide lease helper.

## Required isolation

Each VM runner must own a stable, unique value for every item in this table.

| Resource | Requirement |
| --- | --- |
| VM identity | Exact VM definition, disk/snapshot tree, and clean checkpoint. No linked or writable disk may be shared. |
| VM lease | Stable resource ID such as `vm-hypervisor-v3h-gate4` or `vm-uefi-linux`. |
| Mailbox | Unique host root and unique guest mount/share. |
| Credentials | Unique Credential Manager target or provider-specific secret identifier. |
| Debug transport | Unique named pipe, socket, TCP port, or device. |
| Debugger state | Unique process ownership record, command file, log root, and evidence root. |
| Broker state | Unique session root, request pipe/socket, nonce, and process-ownership record. |
| Gate identity | Unique RunId namespace and immutable package hash. |

Resource IDs must be lowercase ASCII and match
`^[a-z0-9][a-z0-9.-]{2,63}$`. Never derive ownership from a display name or a
process name alone.

## Two-level lease model

Concurrent runners require two different leases in the machine-wide agent
mailbox.

### 1. Per-VM lease

Acquire the VM's stable resource ID before any revert, start, debugger attach,
guest submission, stop, or restore. Hold it for the entire live lifecycle,
including failure investigation. Only the token owner may renew or release it.

The shared lease helper supports an explicit `-Resource <resource-id>` and
stores each active resource in a separate atomic state file. Omitting
`-Resource` fails closed. The `host-vm-startup` lease may coexist with per-VM
leases but remains exclusive with another `host-vm-startup` owner.

The per-VM acquisition has this shape:

```powershell
$Mailbox = 'C:\Users\jayli\OneDrive\Documents\Projects\AgentSharedMailbox'
$LeaseCommand = Join-Path $Mailbox 'Invoke-AgentVmLease.ps1'
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

### 2. Host-startup lease

Acquire the short-lived `host-vm-startup` lease immediately before the final
capacity check and VM start. This closes the race where two agents both see
enough memory and start simultaneously.

```powershell
$StartupLease = & $LeaseCommand `
    -Action Acquire `
    -Resource 'host-vm-startup' `
    -Agent '<agent-or-thread-id>' `
    -Project '<absolute-project-path>' `
    -RunId '<prepared-run-id>' `
    -LeaseMinutes 10

$StartupLeaseToken = $StartupLease.LeaseToken
```

While holding it, the runner must:

1. enumerate all active VM and debugger processes;
2. verify that none conflicts with its exact configured resources;
3. recheck free physical memory, system commit, and configured reserve;
4. enforce a machine-wide maximum of two live test VMs;
5. start its VM and debugger; and
6. wait until VM, debugger, and mailbox readiness are stable.

Release `host-vm-startup` only after readiness is stable or startup has failed
closed. Continue holding the per-VM lease until final disposition.

```powershell
& $LeaseCommand `
    -Action Release `
    -Resource 'host-vm-startup' `
    -LeaseToken $StartupLeaseToken
```

## Capacity policy

- Every runner must account for the *actual* memory already consumed by the
  first VM; configured guest RAM is not a substitute for a live host check.
- The second runner must prove that its VM allocation plus its configured host
  reserve fits before start.
- Keep the existing critical and steady-state memory floors. Never lower a
  floor merely to permit concurrency.
- Monitor free physical memory and commit throughout startup and the gate.
- A runner may stop only its exact owned VM/debugger processes. It must never
  use global kill-by-name cleanup.
- CPU and storage contention are not memory failures. Timeouts must preserve
  evidence and must not trigger automatic retries.

## Process preflight rule

Preflight must reject **resource collisions**, not every process from the same
hypervisor or debugger family. For example, a VMware runner may coexist with a
QEMU runner when their VM images, transports, mailboxes, credentials, brokers,
and evidence roots are distinct and host capacity passes.

If two runners use the same provider, each must identify its owned process by
the exact configured VM definition and recorded process identity. Seeing an
unrecognized process is a fail-closed condition unless the runner can prove it
belongs to another active, non-conflicting resource lease.

The Hypervisor runner's current broad `vmware-vmx` / `kd.exe` rejection must be
changed and mutation-tested before it may overlap another VMware/KD runner. It
does not need to be weakened merely to coexist with an isolated QEMU/Linux
runner, but the shared lease helper still must support resource-scoped leases.

## Required live sequence

1. Complete builds, packaging, and static tests without a live lease.
2. Acquire the exact per-VM lease.
3. Run the runner's read-only doctor/preflight.
4. Acquire `host-vm-startup`.
5. Perform the final capacity and collision checks.
6. Revert/start only the exact configured VM and establish debugger/mailbox
   readiness.
7. Release `host-vm-startup`.
8. Submit exactly one immutable prepared RunId.
9. On `PASS`, stop and restore only the owned VM.
10. On any other result, preserve only that VM and its debugger/evidence. Do
    not disturb another runner.
11. Release the per-VM lease only when disposition is unambiguous.

```powershell
& $LeaseCommand `
    -Action Release `
    -Resource $VmResource `
    -LeaseToken $VmLeaseToken
```

If any lease expires, inspect the recorded VM, debugger, project, and RunId
before takeover. Expiry is evidence, not permission to reset a resource.

## Shared lease helper requirements

Any machine-wide mailbox implementation used for concurrent mode must provide
and test all of the following:

- `Acquire`, `Status`, `Renew`, and `Release` scoped by exact `-Resource`;
- atomic create-new acquisition per resource;
- safe resource-to-filename validation (no path separators or traversal);
- owner-token validation within the selected resource only;
- independent history records per resource;
- status/listing of all active resources for capacity diagnostics;
- mandatory explicit resource selection with no global fallback; and
- no command that deletes or rewrites another resource's lease.

The installed helper passes these contracts. A participating runner that has
not completed the remaining isolation checklist must fail closed and must not
run concurrently.

## Adoption checklist for another runner

- [ ] Assign and document a stable per-VM resource ID.
- [ ] Confirm VM disks and snapshots are not shared with another runner.
- [ ] Configure unique mailbox, credential, broker, transport, and log roots.
- [ ] Add exact owned-process tracking and resource-collision checks.
- [ ] Add final memory/commit checks under `host-vm-startup`.
- [ ] Fail closed when another process cannot be attributed safely.
- [ ] Prove failure cleanup cannot stop, revert, or overwrite another VM.
- [ ] Add static tests for all namespace and ownership boundaries.
- [ ] Reject startup when the runner has no exact `vm-*` resource configured.
