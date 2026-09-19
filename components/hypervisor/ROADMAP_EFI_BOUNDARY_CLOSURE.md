# EFI Runtime Boundary Closure

This roadmap expands the EFI target beyond its boot-handoff MVP. A boundary is
closed only when its production path and its disposable-VM evidence satisfy the
exit criteria below. Pure-model tests and successful boot are necessary, but
they are not substitutes for the corresponding runtime gate.

## Current closure status - 2026-09-14

- **B1 authenticated control:** implemented and exercised by invalid-HMAC,
  valid-HMAC, monotonic replay, exclusive LocalSystem access, and authenticated
  response checks in the disposable Windows VM.
- **B2 clean shutdown/unload:** implemented with a fused per-CPU
  PREPARE/COMMIT transaction plus prepare/detached barriers. Exact-profile run
  `uefi-windows-probe-20260914-141057-66674c14fc` proved 20/20 detach,
  post-detach CPUID absence, and companion unload. Diagnostic 1- and 4-vCPU
  runs also passed; the post-fix four-vCPU run is
  `uefi-windows-probe-20260914-141911-59e0da5be2`.
- **B3 suspend and crash policy:** implemented as detach-before-suspend with
  system/execution power requests, same-boot quarantine on incomplete state,
  and reset-qualified recovery. Live VMX suspend/resume and transparent root
  crash continuation are deliberate non-goals.
- **B4 interrupt/device policy:** closed for the target as direct 1:1
  pass-through. General APIC, interrupt, and device emulation remain non-goals.
- **B5 isolation:** resident pages are removed from guest EPT. The physical
  target has a fail-closed DMAR/MCFG/VT-d policy with pure-model/build coverage;
  it has not been executed on the primary host. In nested VMware, DMA isolation
  remains the outer hypervisor's responsibility.
- **B6 compatibility:** the pinned Windows 11 build `26200.9445`, 20-vCPU
  VMware profile passed. Arbitrary hardware, Windows builds, hot-add, and live
  topology changes remain outside the claim.

These statuses describe the requested target policy, not production readiness.
The physical PC is source-admitted for the exact profile but intentionally has
not been used as the first privileged execution environment.

## Safety and architecture

- Privileged execution is VM-only. The EFI payload and any companion driver
  must never be loaded on the development host.
- The existing combined build remains driver-free by default. A Windows
  companion driver, if enabled, is a separately built, signed, VM-only control
  component rather than a return to the historical SCM loader.
- The guest OS is the only VM. Ordinary RAM and assigned devices remain
  identity addressed, but hypervisor-owned memory is removed from guest EPT and
  from every DMA domain before the first guest instruction can access it.
- No lifecycle command is accepted solely because it contains a magic number.
  Runtime commands require a per-boot key, server nonce, monotonic sequence,
  explicit framing, and a verified HMAC-SHA-256 tag.
- Failure is fail-closed. An incomplete shutdown, stale command, lost processor,
  invalid interrupt state, or incomplete IOMMU policy keeps resources reserved
  and makes the VM runner retain evidence and quarantine the VM.

## B1 - Authenticated runtime control

Implementation requirements:

1. A freestanding EFI verifier owns the authenticated session state and never
   exposes the per-boot key through CPUID or another guest-readable response.
2. A Windows kernel broker obtains the boot-scoped key through an OS-restricted
   handoff, applies a SYSTEM/Administrators device ACL, binds one exclusive
   owner process, and zeroizes the key on close.
3. Every request authenticates protocol version, session nonce, sequence,
   opcode, payload length, and payload. Replays, non-increasing sequences,
   duplicate commits, and unknown opcodes are rejected without changing
   hypervisor state. Monotonic gaps are permitted so a lost transport request
   cannot deadlock a session.
4. Every response carries an HMAC-SHA-256 tag over its canonical header and
   payload. The close response is authenticated before the retained session
   key is zeroized.
5. Status is read-only. Shutdown, suspend preparation, and resume recovery have
   separate capabilities and explicit state preconditions.

Exit evidence: protocol vectors, malformed-frame and replay tests, negative
caller/ACL tests, key-zeroization proof, and an authenticated status exchange in
the disposable Windows VM.

## B2 - Clean shutdown and unload

Implementation requirements:

1. Authenticated shutdown starts a new generation and quiesces all enabled
   processors through their stable firmware processor indices.
2. Each processor stops accepting operational commands, drains its VM-exit
   path, restores the current guest architectural state, executes VMXOFF on the
   owning logical processor, and acknowledges exactly once.
3. No runtime allocation or shared state is released until every enabled
   processor proves VMXOFF. After `ExitBootServices`, firmware runtime pages may
   remain reserved, but they must be inert, non-executable by the guest, and
   contain no live secret.
4. Timeout or partial failure never reports success and never frees possibly
   active VMX state.

Exit evidence: 1-, 2-, and 20-vCPU shutdown gates, repeated status/shutdown
negative cases, continued Windows workload after detach, and VM snapshot
recovery on injected partial-acknowledgement failure.

## B3 - Suspend, resume, and crash recovery

Implementation requirements:

1. The Windows broker receives system-power callbacks. It completes B2 before
   S3/S4 or vetoes the transition when clean detachment cannot be proven.
2. Resume performs fresh platform admission and a new authenticated session;
   stale pre-suspend generations and keys cannot be reused.
3. Fatal VM exits use a bounded recovery policy. Recoverable guest faults are
   injected architecturally; unrecoverable root faults attempt processor-local
   detach only when current guest state is complete. Otherwise the VM is
   deliberately stopped and quarantined rather than continuing corrupted.
4. Crash evidence records the last exit, processor, lifecycle generation, and
   recovery decision without writing guest memory.

Exit evidence: suspend/resume cycles, hibernate/resume cycles when supported by
the test image, injected recoverable and terminal exits, and deterministic
external snapshot recovery for the terminal cases.

## B4 - Interrupt and device handling

Implementation requirements:

1. The control builder proves whether external-interrupt exiting is disabled or
   configures acknowledge-on-exit plus a bounded pending-interrupt queue.
2. Intercepted external interrupts are reinjected only when guest
   interruptibility permits it; otherwise interrupt-window exiting is armed.
   NMI delivery uses the equivalent NMI-window rules.
3. APIC/x2APIC MSRs, INIT/SIPI, timer interrupts, MSI/MSI-X, and the VMware test
   devices have explicit policies. Unknown exits and unsupported devices fail
   admission instead of reaching a runtime halt path.
4. Device assignment and MMIO/I/O exposure come from an immutable boot policy,
   not an all-access fallback.

Exit evidence: interrupt-storm and timer gates, all-CPU affinity coverage,
network/storage workload, MSI/MSI-X coverage, INIT/SIPI restart coverage, and
negative unsupported-device admission tests.

## B5 - Memory and DMA isolation

Implementation requirements:

1. Guest EPT uses 4-KiB splits wherever a 2-MiB range contains a protected
   page. Every context, VMXON/VMCS page, host stack, descriptor page, private
   page table, EPT table, resident code/data page, control secret, and scanner
   mapping page is absent from guest EPT.
2. Large EPT leaves are used only when all 512 constituent pages have identical
   permissions and cache policy. Uncovered and conflicting ranges fail
   admission.
3. ACPI DMAR is parsed and VT-d translation is enabled before bus-master DMA
   can target hypervisor-owned pages. Each device is assigned to an explicit
   domain; reserved regions and interrupt-remapping requirements are honored.
4. EPT and IOMMU policies are validated as one transaction. CPU isolation
   without DMA isolation is reported as incomplete, never as protected.

Exit evidence: exhaustive policy validation, guest read/write/execute denial
for every protected range, DMA attempts from the virtual test device, IOMMU
fault evidence, and rollback tests for partial EPT/IOMMU construction.

## B6 - Exact compatibility profile

The target host profile is Windows 11 25H2 build `26200.9445`, Intel Family 6
Model 151 Stepping 2, with 20 logical processors. Validation uses a separate
disposable VMware configuration so the accepted `26200.9168` image and snapshot
remain immutable.

The runner must fail closed unless authenticated guest evidence reports the
configured Windows build and UBR. The matching VM uses EFI firmware, vTPM,
nested VMX during the gate, 20 vCPUs, a controlled network, a dedicated serial
result channel, an independent lease, and a clean snapshot. A linked clone is
preferred so host free space remains above 20 GiB.

Exit evidence: exact build/UBR match, all 20 processor-local health results,
authenticated status and clean detach, suspend/resume, interrupt/device
workloads, isolation probes, and successful snapshot restoration.
