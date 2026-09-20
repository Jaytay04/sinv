# hypersinv

## Combined EFI path

The repository root does not boot the historical Type 2 driver described
later in this file. Its combined target builds `src/efi/uefi_hypervisor.efi`,
which the UEFILoader component launches before Windows. That EFI path owns
separate VMX contexts and tests, uses direct 1:1 interrupt/device pass-through,
denies all resident root pages through EPT, and on the exact admitted physical
Intel profile prepares VT-d identity mappings with the root ranges removed.

Clean detach is implemented by the optional standalone WDM companion under
`src/driver/efi_runtime_companion`: an exclusive LocalSystem service submits a
boot-keyed HMAC-SHA256 request, every processor is queried, and PREPARE/COMMIT
run as one interrupts-disabled native transaction. Prepare and detached
barriers prevent early VMXOFF and early return to Windows respectively. Every
native pivot executes VMCLEAR/VMXOFF before the driver becomes unloadable. The
service holds Windows power requests until detach, quarantines an incomplete
lifecycle within the same boot, and permits recovery only after a reset changes
its volatile boot identity. These EFI claims are independent of the legacy
runtime-gate history below.

The pinned Windows `26200.9445` VMware profile passed the complete 20-vCPU EFI
activation/authenticated-detach gate as
`uefi-windows-probe-20260914-141057-66674c14fc`. The same post-fix path passed a
four-vCPU diagnostic regression as
`uefi-windows-probe-20260914-141911-59e0da5be2`.

The EFI path also contains build-pinned, read-only Windows process enumeration
and authenticated process-bound alternate EPT views. Alternate EPTs preserve
the identity view's HPA mappings and may only remove write permission from
explicit allowlisted RAM ranges. They do not make VMX-root reads pass through
EPT and are not invisible: a guest write to a protected range causes a VM exit
and a fail-safe return to identity EPT.

Privileged EFI and companion execution is VM-only. The physical VT-d path is
currently build/model tested and must not be first-run on the primary host.

> **⚠️ Research Project — Not for Production Use**
>
> This is an educational/research implementation of a Type 2 Intel VT-x hypervisor for Windows 11 x86_64. It is designed for studying low-level virtualization, EPT memory management, and VM-Exit handling. **Do not use on production systems.** VM testing uses a test-signed research driver and can cause system instability. Never bypass Driver Signature Enforcement or use BYOVD techniques.

Type 2 Intel VT-x hypervisor for Windows 11 x86_64 — advanced memory debugging engine that intercepts and modifies guest memory by operating below Ring 0 at VMX root (Ring -1).

## Architecture

Three-layer design with clean separation between kernel driver, hypervisor core, and user-mode loader:

```
Ring 3  →  loader.exe        (Win32, SCM driver loading + IOCTL CLI)
Ring 0  →  hypervisor_driver.sys  (KMDF, per-core VMX init via DPC broadcast)
Ring -1 →  hypervisor_core        (C11 static lib, VM-Exit dispatch, EPT, stealth)
```

### Capabilities

- **VM-Exit dispatch** — CPUID, HLT, RDTSC, MSR read/write, CR access, EPT violation, VMCALL
- **EPT memory hooks** — identity map with 2MB large pages, 2MB→4KB split for per-page permission toggling, MTRR-aware memory types
- **Transparency** — CPUID leaf 1 bit 31 masking, Hyper-V MSR range hiding, VMX capability concealment
- **CET suppression** — guest CET disabled via VMCS fields, MSR bitmap, and CR4 shadow
- **Reviewed Runtime V3 release** — Gate 35 binds the accepted signed package to exact source, artifact, and evidence hashes
- **Reviewed Runtime V4 release** — optional capability-authenticated VPID activation, invalidation-before-reuse, CR3 interoperability, and package provenance are accepted through Gate 39
- **Reviewed Runtime V5 release** — authenticated per-processor VM-exit telemetry and package provenance are accepted through Gate 42
- **Reviewed Runtime V6 diagnostics** — authenticated, redacted, sequence-stable per-processor last-dispatch status and dynamic CPUID coherence are accepted through Gate 44
- **Reviewed Runtime V7 coherent observations** — one authenticated read bundles telemetry and redacted dispatch state without a cross-command skew window; Gate 45 is accepted
- **Reviewed Runtime V8 control-session hardening** — the privileged control device admits exactly one simultaneous handle regardless of cooperative share flags; Gate 46 is accepted
- **Reviewed Runtime V9 explicit session access** — admitted control handles must carry both read and write rights while public IOCTL values remain unchanged; Gate 47 is accepted
- **Reviewed Runtime V10 root-only control namespace** — named device opens fail closed while the canonical root path remains available; Gate 48 is accepted
- **Reviewed Runtime V11 process-bound control sessions** — inherited handles cannot authorize IOCTLs from a process other than the admitted opener; Gate 49 is accepted
- **Reviewed Runtime V12 stable owner identity** — retained process-object authorization survives opener exit and rejects inherited-handle use after PID lifetime ends; Gate 50 is accepted
- **Reviewed Runtime V13 protocol negotiation** — every control-session file object must negotiate an exact ABI before operational IOCTL dispatch; Gate 51 is accepted
- **Reviewed Runtime V14 exact request framing** — every negotiated operational IOCTL has one canonical zero- or 16-byte input length enforced before runtime dispatch; Gate 52 is accepted
- **Reviewed Runtime V15 immutable negotiation** — successful protocol negotiation is a one-shot file transition; Gate 53 is accepted
- **Reviewed Runtime V16 exact response framing** — every fixed-width operational IOCTL requires its exact output length before runtime dispatch; Gate 54 is accepted
- **Reviewed Runtime V17-V20 process targeting** — referenced process identity, explicit and generation-matched release, and authenticated session-owned self-target configuration are accepted through Gate 58
- **Reviewed Runtime V21 capability manifest** — every loader session verifies the exact authenticated control-surface bitmap before operational mutation; Gate 59 is accepted
- **Reviewed Runtime V22 substrate admission** — native VMX and exact VMware nested VMX are admitted while Hyper-V, unknown, and malformed substrates fail before SCM or VMX mutation; Gate 60 is accepted
- **Reviewed Runtime V23 topology consensus** — every active processor across all groups must report one identical admitted VMX substrate before runtime configuration; Gate 61 is accepted
- **Reviewed Runtime V24 VMX preentry readiness** — every active processor must report compatible preentry state and one identical VMX region contract before runtime allocation; Gate 62 is accepted
- **Reviewed Runtime V25-V27 diagnostic hardening** — bounded HLT exit proof, shared diagnostic-state admission, and split owned/direct diagnostic payload targets are accepted through Gate 66
- **Reviewed Runtime V28 capability-qualified watchdog** — uniform VMX-preemption-timer support activates exact VMCS rearm and expiry failure, while uniform hardware absence is admitted and reported without touching unsupported fields; Gate 67 is accepted on VMware's unsupported path
- **Reviewed Runtime V29 current-VMCS attestation** — persistent composition verifies VMPTRST against the exact tracked per-processor VMCS before every dependent VMX boundary; Gate 68 is accepted
- **Reviewed Runtime V30 execution context** — every current-VMCS attestation requires exact non-migratable `DISPATCH_LEVEL`; Gate 69 is accepted
- **Runtime V31-V46 hardening closure** — VMCS planning, transient execution, VPID, EPT construction, session configuration, and hook ownership use transactional admission and frozen inputs; the cumulative valid path is accepted through Gate 69 from the Runtime V46 package
- **CR3 interception** — target process detection on context switch
- **Authenticated dynamic analysis** — bounded masked signature scans, user page-map discovery, safe structure traversal, and typed read-only extraction

## Project Structure

```
src/
├── hypervisor/          Pure C11 static library — no WDK dependency, unit-testable
│   ├── vmx_init.c           VMXON, VMCS setup, VMLAUNCH
│   ├── vmx_vmexit.c         VM-Exit dispatch, fast-path handlers
│   ├── ept_manager.c        EPT walker, identity map, PT pool, page split
│   ├── ept_violation.c      EPT violation handler
│   ├── cpuid_handler.c      CPUID stealth masking
│   ├── msr_handler.c        MSR dispatch table, bitmap manipulation
│   ├── cr3_handler.c        CR access qualification decode
│   ├── cet_suppress.c       CET MSR list, VMCS field constants
│   ├── cpu_features.c       CPUID/MSR feature detection, MTRR parser
│   ├── hook_manager.c       Hook table CRUD (256 slots)
│   └── ioctl_validate.c     IOCTL validation helpers
├── driver/              KMDF kernel driver — requires WDK
│   ├── driver_main.c        DriverEntry, device creation, IOCTL dispatch
│   ├── core_mgmt.c          Legacy fixed-array implementation (not compiled)
│   ├── ioctl.c              LOAD/UNLOAD/HOOK/UNHOOK/STATUS handlers
│   └── hook_table.c         Kernel spinlock wrappers for hook table
├── loader/              User-mode loader — no WDK, standard Win32
│   └── loader_main.c        SCM driver load/unload + IOCTL CLI
├── tests/               CTest unit and mutation-backed contract suite
└── include/             Public headers
    ├── vmx.h                ~200 VMX field encodings, control bits
    ├── ept.h                EPT types, memory types, walker API
    └── driver_interface.h   IOCTL codes, request/response structs
```

## Prerequisites

| Component | Version | Notes |
|-----------|---------|-------|
| Windows | 11 Build 22000+ | 25H2 Insider tested |
| CPU | Intel VT-x with EPT | VPID is optional and remains disabled in Runtime V3 |
| MSVC | 14.44+ (VS 2026) | Required for driver target |
| WDK | 10.0.28000.0+ | Kernel-mode headers and libs |
| CMake | 3.15+ | Ninja generator recommended |

**System requirements before loading the driver:**
- Hyper-V/VBS disabled in the disposable test VM so this runtime owns nested VMX
- Test Mode enabled (`bcdedit /set testsigning on`)
- Driver signed with test certificate
- **Disposable test VM required** — never load the driver on a primary machine

## Legal & Ethics

This project is for **educational and security research purposes only**. The memory manipulation techniques demonstrated here are applicable to reverse engineering, vulnerability research, and defensive security tooling. Use only on systems you own or have explicit authorization to test.

## Build

Open **Developer Command Prompt for VS 2026** (or run `vcvars64.bat` to set up the MSVC environment), then:

```cmd
cmake -B build -G Ninja
cmake --build build
```

To sign the driver with the test certificate:

```cmd
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe" sign /fd SHA256 /f hypersinv.pfx /p test build\src\driver\hypervisor_driver.sys
```

To run tests:

```cmd
ctest --test-dir build -C Debug
```

## Runtime V3 research release

Gate 35 is the final package-provenance lifecycle check. Its manifest binds the
reviewed commit to exact driver, PDB, loader, and gate hashes while explicitly
excluding the signing private key. Passing unit tests or possessing a signed
binary is not equivalent to passing the supervised disposable-VM gate; live
evidence remains external to runtime status.

Gate 35 passed for source commit
`418e9a163657899cfb48d5091e286a6465e35571`. The accepted evidence archive
SHA-256 is
`42344FE95A5B636789ABF4CE5601E305EBB03C19EC039E34077884BBADCCDAE2`
and the closed WinDbg log SHA-256 is
`9FBF157E57ABD0F634A07BEB53F84018BC49D816846C996260C5C3F797185DA7`.
See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the reviewed identity and explicit
limitations.

Runtime V4 passed Gate 39 for source commit
`3bd85015967c7bd12f06e92abf98772c3a59d771`. The accepted evidence archive
SHA-256 is
`50482275082187953CEC856BB5D71423B66EB47AD6C0D4060646081BCD2E424A`
and the closed WinDbg log SHA-256 is
`BEB4753D566541E8BFE1D7F3FAD18F32BC39B7059D80EDB12EA349222511ACEC`.
See [ROADMAP_VNEXT.md](ROADMAP_VNEXT.md) for the closed Runtime V4 scope and
[ROADMAP_RUNTIME_V5.md](ROADMAP_RUNTIME_V5.md) for the VM-exit observability
successor milestone.

Runtime V5 passed Gate 42 for source commit
`88e957674b9323ea4aa32769265ebcef5fb42a57`. The accepted evidence archive
SHA-256 is
`C2505BE97028A72191A7372475C9F02668D3FF3CCA444E60E3DE47AF1AED3DC0`
and the closed WinDbg log SHA-256 is
`C4161C847E979039A9E0275E145513382F588BFC5611AD0C44DCCDB1D6D1B4D7`.
See [ROADMAP_RUNTIME_V5.md](ROADMAP_RUNTIME_V5.md) for the closed scope.
New dispatch-diagnostic work is tracked in
[ROADMAP_RUNTIME_V6.md](ROADMAP_RUNTIME_V6.md) and does not alter the accepted
Runtime V5 release identity.

Runtime V6 passed Gate 44 for source commit
`17c085eab8389dbad81dbd3d5bc17f4092385bab`. The accepted evidence archive
SHA-256 is
`32DD19D2C337C10DE0AE6E72917E776A611D6BB7CC72FF0381CC7D2097AA3EBA`
and the closed WinDbg log SHA-256 is
`851E565D9C16AE04066A5023B6DE7EF33D060E81CE6E6670F8E8F18B23FA0CBB`.
See [ROADMAP_RUNTIME_V6.md](ROADMAP_RUNTIME_V6.md) for the closed scope.
Successor observation-bundle work is tracked in
[ROADMAP_RUNTIME_V7.md](ROADMAP_RUNTIME_V7.md).

Runtime V7 passed Gate 45 for source commit
`a1359502d0ceb79694c8236fd2aaf44a824df1f2`. The accepted evidence archive
SHA-256 is
`62689C8F1D6ADDD1C60C27F907DB921CDEE929F9C65E19FA3448CFBEDF74D7E5`
and the closed WinDbg log SHA-256 is
`3FE7A7E8DD00FF43001943B5814F93ED8CBB084837AF17EFDE9A363DE01819CD`.
See [ROADMAP_RUNTIME_V7.md](ROADMAP_RUNTIME_V7.md) for the closed scope.
Exclusive privileged control-session work is tracked in
[ROADMAP_RUNTIME_V8.md](ROADMAP_RUNTIME_V8.md).
Gate 46 closed Runtime V8 with evidence archive SHA-256
`6C24A056E0B1F3966A905B2FF1ABBDD281AB187CBEF71148FF3A5DEB2A6A241D`
and WinDbg log SHA-256
`441971C83FD8E29125C7960B279C59AEC2AEEF58C01B9038DD7546B8851E7F37`.
Explicit control-session access work is tracked in
[ROADMAP_RUNTIME_V9.md](ROADMAP_RUNTIME_V9.md).
Gate 47 closed Runtime V9 with evidence archive SHA-256
`FEDB560EFD3F139340496AAF7AE33AB53DACCC663D30B1650DC2FE6E4E747713`
and WinDbg log SHA-256
`AB718AEFFC92F890BE0293BA6AD0F20F64108470198187232B17A17C383434E6`.
Root-only control namespace work is tracked in
[ROADMAP_RUNTIME_V10.md](ROADMAP_RUNTIME_V10.md).
Gate 48 closed Runtime V10 with evidence archive SHA-256
`CDD0600139DEB61051DAF8C08E9F6580823A3E632C686A5A3E42F54D97702E1A`
and WinDbg log SHA-256
`5A6C7DD231D2B4304C98CF340EDC4AAE2231F911F7101348B913675FE2E2FE4B`.
Process-bound control-session work is tracked in
[ROADMAP_RUNTIME_V11.md](ROADMAP_RUNTIME_V11.md).
Gate 49 closed Runtime V11 with evidence archive SHA-256
`0A084E4787AD140DFFDD1757C2955097B6C21F6C888D78F18C374180519F663C`
and WinDbg log SHA-256
`C48F4D10E0A859D4D947C54A81A37ED26FC6C3C319C5EB17F1690AD281FDF5E1`.
Stable process-object owner work is tracked in
[ROADMAP_RUNTIME_V12.md](ROADMAP_RUNTIME_V12.md).
Gate 50 closed Runtime V12 with evidence archive SHA-256
`7E168042CD9B6B7C3587D74AA9FE620C094CFCBA4D2447CD33D0C140F18B8841`
and WinDbg log SHA-256
`CE925881AC8D6F999B009A1C1439725EE57ACE04A0D2391D75CEB5938ADFC82E`.
Additive fail-closed control-protocol negotiation is tracked in
[ROADMAP_RUNTIME_V13.md](ROADMAP_RUNTIME_V13.md).
Gate 51 closed Runtime V13 with evidence archive SHA-256
`E87AFDC490E37B699710DBA2AF72F536D57F42A30C002FFDEC08C294FE90020A`
and WinDbg log SHA-256
`1E228A6C243AE4FED251B8B575519B1F3ACEB81A286AA386C87D0FA1F527DA56`.

Referenced process-target lifecycle and explicit detached rebinding are tracked
in [ROADMAP_RUNTIME_V17.md](ROADMAP_RUNTIME_V17.md) and
[ROADMAP_RUNTIME_V18.md](ROADMAP_RUNTIME_V18.md). Gate 56 closed Runtime V18
with immutable package SHA-256
`C844DF322FD1D8806277919DFAE51BC3025FB5309FE7284077EE3A6BB0380F19`,
evidence archive SHA-256
`B662684C91F16C166246A9DC2C26AACD95B84513AC6793BA4ADA8764D139B86A`,
and closed KD log SHA-256
`35C89FD709CA1EFC25745D7082904B5AB6B35BB95BE3CCBE5D2580F5DA30B4CA`.

Generation-matched process-target release is tracked in
[ROADMAP_RUNTIME_V19.md](ROADMAP_RUNTIME_V19.md). Gate 57 closed Runtime V19
for source commit `ace5b15e950f61fb7ab6dc2dbefb96842d2c0231` with immutable
package SHA-256
`0312C3C1C5FF04BC298D565C62774822139CD03CF6B8CE01837E5A9C4A988DB7`,
evidence archive SHA-256
`FA6978BE8DDB1D285463F8C594BFA6DF203429A10F2590F613C89652A4562F62`,
and closed KD log SHA-256
`6CBFCADF4AE0B5FF0280320CA9F06E4C38FD362CC81F1ECCD40BCF29B3765AEF`.

Session-owned process-target configuration is tracked in
[ROADMAP_RUNTIME_V20.md](ROADMAP_RUNTIME_V20.md). Gate 58 closed Runtime V20
for source commit `c861d053645168361be28c749af8016feea67dca` with immutable
package SHA-256
`9DEF0816549C71D1A5B385BCFC4EAC3E48A452E423EDCD8192E734F2E001182B`,
evidence archive SHA-256
`7D1D0CD58E407A15C2A548C9E3162F38B349B5C977780C3D875D23EB8C60A44C`,
and closed KD log SHA-256
`4757E16C0C99E6D1E0DE08AB7014C6127D6752C8B4B94C4D0F8ABBE213158AF8`.

Authenticated control capability discovery is tracked in
[ROADMAP_RUNTIME_V21.md](ROADMAP_RUNTIME_V21.md). Gate 59 closed Runtime V21
for source commit `1545845c583ef76a9874a247b4e259cce66ae5fd` with immutable
package SHA-256
`C105CBC196A8B486332D696394C8783863072526061C71936486FBD8D3FA196C`,
evidence archive SHA-256
`3F8FBE4973EDF289A270E820A074536910280F3A45FAD69F56ABB3DCC57FA297`,
and closed KD log SHA-256
`B507BB62049BBCC591865D7119B0B3297FA920EC452E185DB38C99286A6985B7`.

Fail-closed VMX substrate admission is tracked in
[ROADMAP_RUNTIME_V22.md](ROADMAP_RUNTIME_V22.md). Gate 60 closed Runtime V22
for source commit `722073bd3d06c99c646f37a41e8da90b690b100e` with immutable
package SHA-256
`D49020C6EF09907B23DC904F547979883EF11AB912371FC1C947F04F19CC0BD1`,
evidence archive SHA-256
`173F014CA706CEC124F4644FE4EEEECDAE754E87CFE4B20ABDC929DA40117887`,
and closed KD log SHA-256
`A0FB3FB6A7AEAE5561D3433F2D77D9399BDE68C4CFB057172613A7009AA1BEBA`.

Topology-wide VMX substrate consensus is tracked in
[ROADMAP_RUNTIME_V23.md](ROADMAP_RUNTIME_V23.md). Gate 61 closed Runtime V23
for source commit `abd5ea866b7c64f03ac9a8eb043d42caca598353` with immutable
package SHA-256
`A79F73F4B28093251C47A3FF316009769B38863AF72A6680BF8B8E524178902D`,
evidence archive SHA-256
`418F74E4CF57BD3D46C424985E7FA8E381DB6C13176D96F86DCAE433CB8A8B79`,
and closed KD log SHA-256
`113CA685CE33E26F2FF614B19B8DAABEAB00257F8DF1D02CAB73B17998EDB6E0`.

Topology-wide VMX preentry readiness is tracked in
[ROADMAP_RUNTIME_V24.md](ROADMAP_RUNTIME_V24.md). Gate 62 closed Runtime V24
for source commit `55d1b7379114a5683e0d064556a9bbe083af175d` with immutable
package SHA-256
`606E79BB019502D34079954E694534D380EEC6CB5FCED6508EFCC029D0F158C6`,
evidence archive SHA-256
`CFB641B08A0D97D40E27A6387D56F5653F3A0B1C9DE4B96A98D8B716A65E23B8`,
and closed KD log SHA-256
`F3C0079504449C623D9432DB8567112D7E79A697C7BBD9CB10FF3CBA029B8F73`.
