# uefi2hsinv

`uefi2hsinv` is a Windows/x86-64 VM research composition: UEFILoader owns
the UEFI boot handoff, and Hypervisor supplies a small EFI VMX payload. The
root project is a static-first superbuild; the original projects remain
independently buildable under `components/`.

## MVP boot contract

The intended disposable-VM sequence is:

1. UEFILoader is started by firmware and loads the configured `payload.efi`.
2. The EFI Hypervisor payload validates its VMX prerequisites, allocates
   firmware-backed state, virtualizes the bootstrap processor (BSP), and
   returns to the loader in VMX non-root mode.
3. UEFILoader performs its normal handoff and starts Windows Boot Manager
   (`bootmgfw.efi`).

The hypervisor payload is not a Windows `.sys` driver and is not installed by
the Windows Service Control Manager. The combined build does not live-load a
driver.

The first health check is deliberately narrow: a boot-manager-compatible EFI
probe executes the repository-defined magic CPUID request after the payload
handoff. A successful response proves that the VMX transition and the
non-root return path are alive; it is not a claim that the full Runtime V3/V4
driver feature set is available. The request magic, leaf, version, and
completion values are defined by the Hypervisor EFI ABI header.

The checked-in host smoke substitutes that probe for Windows Boot Manager so
the result is deterministic and observable through QEMU debugcon. The nested
and direct Windows actions exercise a real Windows handoff in disposable VMs.
`RunWindowsDirectHypervisorE2E` always stages freshly built loader, hypervisor,
and static Windows probe binaries, expands the disposable VM to 20 vCPUs for
the run, and restores its snapshot and original VMX settings afterward.

A second, bounded smoke replaces Windows Boot Manager with a runtime-resident
probe. It calls `ExitBootServices`, executes the same intercepted health CPUID,
and exits nested QEMU through a dedicated debug-exit device. This validates the
resident BSP VM-exit path after boot services end; it is not a complete Windows
boot or runtime-services compatibility claim.

A third bounded probe executes `UD2` after `ExitBootServices`. Before VMX
entry, the payload rewrites the copied guest IDT gates and guest TSS stack
state to a resident terminal guard. The probe therefore checks that an early
post-firmware guest exception reaches resident code without calling a stale
firmware handler. It remains a quarantine test, not general interrupt or
exception emulation.

On Windows, `ib-tcg-dump probe` reports one
`UEFI_HYPERVISOR_WINDOWS_CPU:<group>:<processor>:<status>` line for every
active processor, followed by one aggregate
`UEFI_HYPERVISOR_WINDOWS_CPUID:<status>` line. Each query is bounded to one
CPUID instruction under temporary thread affinity. The direct Windows E2E gate
requires 20 distinct per-CPU `PASS` results plus the aggregate `PASS`.

## Implemented and verified state

As of 2026-09-13, the direct VMware Windows profile is validated with 20 vCPUs.
Before handoff, the payload enumerates firmware topology, performs a bounded
VMX readiness preflight on every enabled AP, allocates an independent runtime
ownership package for each processor, and launches the BSP plus all 19 APs in
VMX non-root mode. Activation is fail-closed: preparation failures release all
owned pages, while a failed AP launch executes VMXOFF before BSP launch is
allowed.

Each processor owns a context, VMXON and VMCS regions, private stacks and page
tables, MSR bitmap storage, and private guest/host descriptor state. Each AP
also owns a 514-page EPT hierarchy that identity maps the full 39-bit
(512-GiB) guest-physical address space. UEFI-derived memory typing covers that
entire range: a 2-MiB policy leaf is WB only when the whole leaf is covered by
WB-capable RAM, and a complete 1-GiB range is coalesced only when all 512
policy leaves are WB. Uncovered, reserved, and MMIO ranges remain UC.
Each VMCS also receives a unique nonzero VPID so handled exits retain that
processor's guest translations instead of forcing a full translation flush.

EPT and unrestricted guest are enabled for the AP's architectural reset and
`INIT -> wait-for-SIPI -> SIPI vector` startup sequence. The resident VM-exit
path places the AP into wait-for-SIPI before doing debug I/O or the complete
architectural reset. The reset VMCS writes are deferred until SIPI has been
captured, closing the nested-VMX delivery window that could strand the AP.
The path also implements CR0/CR4 fixed-bit virtualization, EFER.LMA
transitions, and duplicate-SIPI handling. EPT and unrestricted guest remain
enabled across reset, protected-mode, and long-mode transitions. CPUID leaf 1
hides VMX and SMX while preserving the containing VM's hypervisor identity and
paravirtual leaves; the repository-private health leaf remains available. This
keeps virtualization-aware Windows services on their compatible code paths. A
later INIT reuses the same complete identity hierarchy. The pre-handoff debug
evidence reports the number of WB 2-MiB EPT policy leaves; the pinned 3-GiB
guest reports `0x000005FE`.

Static/component gates and the bounded EFI/post-`ExitBootServices` VM smokes
pass. Direct live run
`uefi-windows-probe-20260913-092227-7aedf3d6cf` passed the full 20-vCPU
profile: loader/payload launch, Windows handoff, 20 distinct processor-local
health responses, and aggregate CPUID health. A one-shot `LocalSystem` startup
probe publishes its bounded result through a temporary COM2 named pipe. The
result is HMAC-authenticated, and the runner restores both the clean snapshot
and the pre-run VMX after success or failure.

### Read-only live-memory scanner module

The EFI payload includes a freestanding C++ signature-scanner module with a
C-compatible ABI. Because VMX-root accesses use the private host CR3 rather
than EPT, each processor's host page tables reserve a temporary 2-MiB virtual
window at `0x40000000`. The window uses 4-KiB leaf PTEs and refuses to operate
unless CR0.WP and EFER.NXE are active. Every mapped leaf is read-only,
non-executable, and contained by a caller-supplied page-aligned RAM allowlist;
map and unmap operations invalidate every affected host virtual page. Guest
EPT permissions are never changed.

`scanner.h` exposes reusable pattern compilation, wildcard-aware
Boyer-Moore-Horspool search, chunked physical-range scanning, checked
base-plus-RVA calculation, and ordered multi-signature resolution. Signature
records carry an ID, pattern, signed match-relative offset, and target data
size. The scanner contains no write, hook, guest-execution, allocation, or
runtime-control API.

The module is statically tested and linked into `uefi_hypervisor.efi`, but no
runtime command invokes it yet. Its caller must provide validated RAM ranges
and a contiguous physical module base/size; virtual-to-physical discovery and
non-contiguous image walking are outside this module's contract. Privileged
scanner execution remains VM-only.

### Lifecycle, control, interrupts, and isolation

The EFI runtime now has an authenticated QUERY/PREPARE/COMMIT detach protocol. A
per-boot 256-bit session key, generation, nonce, monotonic sequence, capability
mask, and HMAC-SHA256 bind each fixed-size HVR1 mailbox request. The optional
WDM companion exposes one exclusive `SYSTEM`-only device and uses
`KeIpiGenericCall` to query and detach every active processor. PREPARE and
COMMIT are fused in one interrupts-disabled native transaction because PREPARE
changes the next VM-exit return target. A shared prepare barrier prevents any
processor from committing early, and a detached barrier prevents any callback
from returning to Windows until every processor has executed `VMCLEAR`,
`VMXOFF`, cleared `CR4.VMXE`, and acknowledged the same generation. The
companion is a separate opt-in target; the combined default still installs and
loads no driver.

The companion's optional LocalSystem service provides bounded `status` and
`shutdown` commands, records lifecycle state plus a volatile boot identity in
the registry, and refuses unsafe reuse after an incomplete same-boot detach.
After a real reboot, the changed boot identity permits a fresh session because
processor VMX state cannot survive the reset. Shutdown, preshutdown, and power
notifications request the same detach path. While root mode is active, the
service holds system-required and execution-required power requests to block
unattended sleep and releases them only after every processor is `OFFLINE`.
Windows still offers no absolute veto for every manually initiated suspend;
that path detaches synchronously on the service notification and resumes
without re-enabling this hypervisor.

The disposable direct-Windows gate additionally has a fixed
`selftest-shutdown` path. It must prove invalid-HMAC rejection without a state
change, authenticated detach acknowledgements from all 20 processors,
consumed-sequence replay rejection, disappearance of the CPUID health
contract, and clean driver unload. Test-signing material is generated and
destroyed inside the disposable VM; no private key is stored in this
repository or created on the physical host.

The accepted exact-profile run is
`uefi-windows-probe-20260914-141057-66674c14fc`: Windows build `26200.9445`
reported 20/20 processor-local health checks, authenticated detach and replay
rejection, 20/20 post-detach `ABSENT` checks, aggregate CPUID absence, and clean
companion unload. A post-fix four-vCPU diagnostic regression also passed as
`uefi-windows-probe-20260914-141911-59e0da5be2`.

Interrupts and devices use a direct 1:1 pass-through policy instead of device
emulation. External-interrupt and NMI exiting, virtual NMI, and posted
interrupts are not enabled. This preserves the host OS's native APIC/device
stack while the EFI runtime handles only its explicitly admitted exits.

CPU access isolation is enforced by removing every resident hypervisor page
from guest EPT: code, contexts, VMXON/VMCS regions, host stacks and page tables,
descriptor state, EPT storage, runtime records, control mailboxes, and scanner
mapping windows are guest-inaccessible. On the exact physical Intel profile,
the payload additionally parses DMAR and MCFG, requires include-all DRHD
coverage for every admitted PCI segment, hides DMAR from Windows through
read-only guest ACPI shadows, quiesces PCI bus masters, and installs a private
legacy VT-d identity domain with all hypervisor ranges removed and RMRRs
preserved. Enable, audit, invalidation, detach rollback, and PCI command
restoration are transactional. Missing/malformed tables, pre-enabled or
unsupported VT-d state, uncovered segments, more than 512 active bus masters,
or the wrong CPU/topology fail before VMX launch. Nested-VM profiles leave
physical DMA isolation to the outer hypervisor. The physical VT-d path has
build and pure-model coverage but has not been executed on the primary host;
the safety policy forbids using that machine as the first privileged test.

## Build layout

The root CMake project uses `ExternalProject_Add` and never merges the
component target graphs. A normal build creates:

```text
build/combined/<config>/
└── components/
    ├── uefi-loader/   # UEFILoader cache, binaries, and CTest registry
    └── hypervisor/    # Hypervisor cache, EFI binary, and CTest registry
```

The Hypervisor configuration receives these explicit paths:

- `HYPERSINV_UEFI_SOURCE_DIR` — `components/uefi-loader`;
- `HYPERSINV_UEFI_SUPPORT_DIR` — `components/uefi-loader/src/vendor`.

That support directory is the source of the freestanding UEFI entry, allocator,
CRT shims, and vendor headers used by the Hypervisor EFI target. The root
target `combined_hypervisor_efi` explicitly builds the component target named
`uefi_hypervisor`, producing `uefi_hypervisor.efi`.

With CMake and Visual Studio 2026 available:

```powershell
cmake --preset combined-windows
cmake --build --preset combined-windows
cmake --build --preset combined-windows-tests
```

The equivalent one-command wrapper is:

```powershell
.\tools\Build-Combined.ps1 -Configuration Release
```

For a source-only check on a machine without a Windows compiler or VM:

```powershell
cmake --preset combined-contract-only
ctest --preset combined-contract
```

`IB_STATIC_RUNTIME=ON` and the Hypervisor static-runtime option are enabled by
the superbuild defaults. The KMDF driver and the historical user-mode SCM
loader are disabled for this path. A WDK is therefore not required for the
EFI MVP configuration, although the component's separate driver build still
has its original WDK requirements.

## VM-only safety boundary

VMX code changes processor privilege and boot ownership. Build and run the EFI
payload only in a disposable VM with nested virtualization explicitly enabled,
a known-good snapshot, and a controlled test network. Keep Secure Boot,
firmware variables, and test signing policy under the VM test plan. Never
install or execute this experimental payload on a primary host or on hardware
where recovery is not immediate.

The root build and test scripts are intentionally non-operational with respect
to deployment: they configure, compile, and run source/component tests only.
VM launch, disk preparation, firmware variable changes, and guest assertions
belong to an explicit VM runbook and must be reviewed separately.

## Validated scope and non-goals

- x86-64 Intel VMX is the only target; AMD SVM and ARM are out of scope.
- The direct validated profile is exactly 20 vCPUs on the pinned Windows 11
  VMware image. Compatibility evidence is build-specific; arbitrary Windows
  builds and virtual hardware remain outside the claim.
- The boot path is a proof of VMX entry/return plus Windows handoff, not a
  production hypervisor or a compatibility guarantee for every Windows build.
- Each AP uses a full 39-bit (512-GiB) identity EPT for boot compatibility,
  with resident pages denied to the guest. The admitted physical profile also
  uses VT-d to deny DMA to those pages. This is hypervisor self-isolation, not
  general multi-VM memory partitioning or a virtual device framework.
- The BSP VM-exit code, context, stacks, VMX regions, private host page tables,
  and host/guest descriptor state use low runtime allocations. Bounded probes
  have validated intercepted CPUID and exception-guard behavior after
  `ExitBootServices`; live pinned-image evidence exercises all 20 CPUs through
  Windows boot and completes the final postboot health exchange. Broader
  Windows/hardware compatibility outside the exact admission profile and
  virtual-address-map changes remain outside the validated boot-handoff
  profile. Clean authenticated teardown is implemented. Suspend is handled by
  detach-before-suspend policy, and crash recovery is fail-closed quarantine
  plus reset rather than transparent continuation.
- General APIC emulation, interrupt remapping models, and virtual device models
  are non-goals; the implemented policy is direct 1:1 pass-through. Likewise,
  hot-add CPUs, processor groups beyond the exact 20-thread topology, and live
  topology changes are not supported.
- The CPUID smoke response is a health signal only. It does not authorize
  memory inspection, stealth, persistence, or a driver load.
- No signing private key, VM disk, generated build state, or live-load helper
  is part of this repository.

See `SOURCE_PROVENANCE.md` for the copied source identities and explicit
exclusions.
