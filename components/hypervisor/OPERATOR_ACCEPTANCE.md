# Operator Acceptance

## Combined EFI acceptance

The UEFILoader-launched EFI path passed the pinned Windows 11 `26200.9445`
20-vCPU VMware gate as
`uefi-windows-probe-20260914-141057-66674c14fc`. Evidence proves 20/20
processor-local activation, invalid-command rejection, authenticated all-CPU
detach, replay rejection, 20/20 post-detach absence, aggregate CPUID absence,
power-request release only after `OFFLINE`, and clean companion unload. The
post-fix four-vCPU regression passed as
`uefi-windows-probe-20260914-141911-59e0da5be2`.

This acceptance is limited to the disposable nested-VM profile. Physical VT-d
remains build/model tested because project policy forbids making the primary PC
its first privileged run. Live VMX resume, general APIC/device emulation,
arbitrary Windows/hardware support, and production use remain non-goals.

The remainder of this document records the separately maintained historical
Type 2 driver acceptance line.

Status: ready for broader disposable-VM evaluation after publishing a signed
package from the current pushed commit.

## Scope

This is a research hypervisor, not production software. Automated acceptance
proves the bounded control protocol, four-processor/two-group lifecycle, EPT,
VPID, CR3 observation, diagnostic VM exits, teardown, and fail-closed software
contracts. It does not prove compatibility with arbitrary hardware, Windows
builds, third-party kernel drivers, suspend/resume, hibernation, or production
workloads.

Never install or load the driver on the development host. Use an owned,
recoverable disposable VM with crash dumps enabled and a known-clean snapshot.

## Accepted automated baseline

- Latest static build:
  `build/codex-runtime-v46-20260907-034254` - 457/457 targets and 302/302
  tests passed.
- Current accepted operator package:
  `build/codex-runtime-v46-20260907-034254/gate69-vmcs-execution-context-f452bc4.zip`.
- Package source commit:
  `f452bc4d669a69efdba8db84387e52d78ffa5bae`.
- Package SHA-256:
  `B25DB25925FABE57776EBDA6F175E5E1AF4437C669D83CDD429A4E9F7BC5F776`.
- Staged driver signer: `CN=hypersinv`.
- Automated acceptance RunId:
  `gate69-vmcs-execution-context-20260907-034955-820aa8193b`.
- Evidence archive SHA-256:
  `6AAEEA301A7280AF2229B3C632021A8E43DAF1640CCDEB29B3D9656A1BC8B23A`.
- Closed KD log SHA-256:
  `EC9F2A49BE0FE035E85D89B1824743111EC8987496E6AEA67B30DED0A2EC4F2D`.
- Guest result: seven diagnostics, authoritative status, canonical unload,
  and confirmed service removal passed with empty stderr.
- KD result: four launch and eleven resume VMCS-binding attestations, with no
  fatal, mismatch, bugcheck, or hook-add failure marker.

## Publication prerequisite

The acceptance package must be generated from one clean pushed commit. Use
`tools/New-VmcsExecutionContextPackage.ps1`; do not copy binaries from mixed
build directories or modify a produced archive. Record the source commit,
archive SHA-256, driver signer, and artifact manifest before transferring it.

## Human evaluation goals

1. Confirm the documented Windows/Intel/VMware nested-VMX environment admits
   the package and unsupported Hyper-V/VBS environments fail before SCM
   mutation.
2. Exercise the intended loader workflow and inspect whether commands, errors,
   and recovery behavior are understandable without source-code knowledge.
3. Run representative guest workloads while the runtime is resident, including
   ordinary application startup, file and network activity, and idle time.
4. Confirm canonical unload leaves the service absent and the guest usable.
5. Review Event Viewer, new dump files, guest stderr, and the closed KD log for
   any unexplained warning, failure, timeout, mismatch, or bugcheck marker.

This is exploratory acceptance, not another repetition gate. Record unexpected
behavior with the exact source/package hash, VM topology, Windows build,
operation sequence, timestamps, guest logs, KD log, events, and dumps.

## Manual operator evidence - 2026-09-08

The user manually repeated cumulative Gate 69 from the unchanged accepted
Runtime V46 package. Evidence archive
`gate69-vmcs-execution-context-20260908-152731.zip` has SHA-256
`C2039CE70AC96AF8B5E293DFF616888E4725DB71AFE832E5BB87ACC650269527`.
The staged driver, loader, PDBs, and gate script matched the accepted package
manifest exactly; the driver signature was valid for `CN=hypersinv`.

The guest reported `Result=PASS` on Windows build 26200 with two processor
groups, two active processors per group, and correct crash-dump settings. All
seven diagnostics, authoritative status, canonical unload, service removal,
empty stderr files, and an empty failure-event set passed. The summary retained
exact current-VMCS binding at `DISPATCH_LEVEL`, 15 execution attestations,
EPT/combined readiness, CR3 observation, and exact HLT control restoration.

Closed WinDbg log
`windbg-runtime-v46-operator_43c4_2026-09-08_15-26-28-669.log` has SHA-256
`5F445900A96AE501555815BD4FB1A841E830E801123F24D57265E16D2FA94B3F`.
It records four launch and eleven resume binding attestations with no fatal,
mismatch, failing status, or bugcheck marker. The final operator break was
intentional, and the unloaded driver was absent from the module list.

The separate optional WinDbg trace
`windbg-runtime-v46-operator-optional_43c4_2026-09-08_15-45-19-098.log` has
SHA-256
`2E931CC52011B22B347EB29535D2047DDB7EECB64712DA5837AFC20586984F1E`.
It records four launch and fourteen resume attestations, a successful CR3
residency transition, successful CPUID/MSR/TSC/INVLPG/CET/HLT boundaries, and
clean module absence after unload, with no fatal, mismatch, failing status, or
bugcheck marker. Because no guest-side optional-workflow transcript was
archived, this second trace supports runtime stability but does not by itself
prove the subjective usability or representative-workload goals above.

This is an operator repeat of the existing Gate 69 boundary, not a new gate or
release identity.

## Stop conditions

Stop immediately and preserve the VM and debugger state if any command hangs,
the guest becomes unresponsive, LOAD or UNLOAD cannot prove its postcondition,
a service remains unexpectedly installed, a mismatch/fatal marker appears, or
a new dump is created. Do not retry an unchanged package before the evidence is
analyzed.

## Exit criteria

Operator acceptance is complete when the intended workflow is usable, the
representative workload produces no unexplained instability, canonical unload
is repeatable, and all evidence is bound to one immutable published package.
