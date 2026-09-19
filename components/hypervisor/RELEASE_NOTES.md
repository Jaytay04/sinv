# Runtime V3 Research Release

## Combined EFI boundary milestone - 2026-09-14

This milestone is separate from the historical Type 2 Runtime V3-V46 release
identities below. The UEFILoader-launched EFI target now has boot-keyed
authenticated runtime control, fused and barriered all-CPU detach, clean
companion unload, detach-before-suspend policy, fail-closed crash recovery,
direct interrupt/device pass-through, guest-EPT self-isolation, and a
fail-closed physical VT-d policy for the exact admitted Intel profile.

Disposable VMware run
`uefi-windows-probe-20260914-141057-66674c14fc` passed on Windows build
`26200.9445` with 20/20 processor-local activation, authenticated shutdown,
replay rejection, 20/20 post-detach absence, aggregate CPUID absence, and clean
driver unload. Run `uefi-windows-probe-20260914-141911-59e0da5be2` passed the
post-fix four-vCPU regression.

This evidence does not claim arbitrary hardware/Windows compatibility, live
VMX resume, general device emulation, or physical VT-d execution on the primary
host. Those exclusions are intentional and documented in
`ROADMAP_EFI_BOUNDARY_CLOSURE.md`.

> Current development: Runtime V46 closes the Runtime V31-V46 hardening series.
> The cumulative valid runtime path is accepted at Gate 69 from source commit
> `f452bc4d669a69efdba8db84387e52d78ffa5bae`.
> This successor work does not alter earlier reviewed release identities.

Runtime V30 binds every current-VMCS attestation to exact `DISPATCH_LEVEL`, so
the processor cannot migrate between VMPTRST and the dependent VMX operation.
Gate 69 accepted four launch and eleven resume boundaries under one complete
seven-diagnostic lifecycle.

Runtime V31 through V46 harden pure software construction and injected-callback
boundaries: transactional VMCS plan composition/application, disjoint plan and
launch-probe publication, exact transient planner batches, frozen transient,
launch-probe, VPID, EPT invalidation, EPT domain, root-coordinator, session
configuration, and hook-owner inputs, plus stable processor identity. These
milestones reject malformed aliases and post-entry mutation without changing
valid hardware values or instruction order. Runtime V46 admits the one valid
production overlap: the authenticated allocator embedded in its EPT session.

The cumulative Runtime V46 Gate 69 package SHA-256 is
`B25DB25925FABE57776EBDA6F175E5E1AF4437C669D83CDD429A4E9F7BC5F776`.
Accepted RunId:
`gate69-vmcs-execution-context-20260907-034955-820aa8193b`.
Evidence archive SHA-256:
`6AAEEA301A7280AF2229B3C632021A8E43DAF1640CCDEB29B3D9656A1BC8B23A`.
Closed KD log SHA-256:
`EC9F2A49BE0FE035E85D89B1824743111EC8987496E6AEA67B30DED0A2EC4F2D`.
The guest completed all seven diagnostics, canonical unload, and service
removal with no stderr; KD recorded four launch and eleven resume attestations
with no binding mismatch, hook-add failure, bugcheck, or fatal marker.

Runtime V46 implementation commit `f452bc4` passed a fresh 457-target
WDK/MSVC/MASM build and the complete 302-test suite. Gate 69 was repeated
because Runtime V46 corrected the valid production hook-owner allocator route;
that cumulative run is the accepted Runtime V31-V46 closure evidence.

Runtime V29 requires hardware's VMPTRST result to equal the tracked
per-processor VMCS before every persistent composition boundary that depends
on a current VMCS. Gate 68 accepted all fifteen external boundaries: four
launches, seven diagnostic resumes, and four unload resumes. Accepted source
commit: `67b30aa560e1aee4c1d8c4c7a2b703f112e0a253`. Package SHA-256:
`4D855ACA802CD83C3902E58E23B71F072F6FEB22D16AAF134251A42D999CE96D`.
Evidence archive SHA-256:
`8BCEDBB8F6B57BD7E295F94C68D395CB4FBD615C792D7F0F0B780493D9270AF1`.
Closed KD log SHA-256:
`7EA09455E07F373840B261FF2160BA137CBA532C0C9CF88F4A0F6A303905120E`.
No 10/100-cycle repetition gate is required.

Runtime V27 separates launch-owned copied diagnostic payloads from the one
authenticated direct driver-owned EPT lifecycle page. Gate 66 accepted that
ownership boundary without changing the public control ABI.

Runtime V28 adds a topology-qualified optional VMX-preemption watchdog.
Uniform timer support enables exact VMCS residency, per-transaction rearm, and
explicit reason-52 fail-closed dispatch. Uniform absence admits an explicit
unsupported path; mixed availability or a nonuniform supported rate is denied
before runtime configuration. Gate 67 accepted the VMware nested-VMX path,
which reports uniform timer absence. Accepted source commit:
`ac97b7937d10249dd21e33d22d4a13a993e3ea33`. Package SHA-256:
`AFFA5C1DB9EC029D7731B3DBFB7D0F3C3460E1E2B0705F1F51BF0D1FD487694C`.
Evidence archive SHA-256:
`2A196C6B08B5F33E06587AF68D18E5B2F7D8E368CAFBD06BF6EBF63E1F5E44C9`.
Closed KD log SHA-256:
`BBEC113AF5992118C5AC316C7C587922642B4C864652EDF65C727DA6B0F67758`.
No 10/100-cycle repetition gate is required.

Runtime V25 is accepted at Gate 64. Runtime V26 replaces seven independently
maintained C/MASM diagnostic-state lists with one bounded odd/even private-ABI
contract, exact complement validation, and adjacent armed/complete pair
validation. Operation-specific routing remains explicit. Gate 65 passed all
seven existing diagnostic pairs once in one resident lifecycle; no repetition
gate is planned. Accepted source commit:
`3ce937d773c5e0c52df6a461fd6f3305061b295a`. Package SHA-256:
`21DEED9A4A87927D21B354659B5D74CBD7FE77AC718A8EFC83CB368E8046A248`.
Evidence archive SHA-256:
`6FF8B5CF25CEB477843F5ADCB47731385AFED968D2F2B6213D225EB9E7AE3CE4`.
Closed KD log SHA-256:
`A162EC10AC41B356689C7260F495F195E6590D91067E3D771BFEBCF4EC6536F7`.
The remaining C dispatcher completion list was subsequently converged onto
the same shared predicate. This semantics-equivalent cleanup changes no
hardware control or public ABI, passed the full 286-test static suite, and
does not require a repetition-only live gate.

Runtime V7 Gate 45 is accepted for source commit
`a1359502d0ceb79694c8236fd2aaf44a824df1f2`. Evidence archive SHA-256:
`62689C8F1D6ADDD1C60C27F907DB921CDEE929F9C65E19FA3448CFBEDF74D7E5`.
Closed WinDbg log SHA-256:
`3FE7A7E8DD00FF43001943B5814F93ED8CBB084837AF17EFDE9A363DE01819CD`.
The gate proved exact coherent observation bundles before and after one
authenticated CPUID route, canonical unload, service/module absence, and no
failure events or stderr output.

Runtime V8 development makes the already administrator/System-only WDF
control device exclusive. This prevents two privileged callers from holding
simultaneous handles even when both request compatible share masks. It does
not change any IOCTL number or wire layout.

Gate 46 accepted that boundary for source commit
`0c0770a307f6c63f12bd6a784f5993d6d442581f`. It proved first-open success,
second-open denial with Win32 error 5, immediate observation access after the
first handle closed, exact four-processor state, and canonical unload. The
evidence archive SHA-256 is
`6C24A056E0B1F3966A905B2FF1ABBDD281AB187CBEF71148FF3A5DEB2A6A241D`;
the closed WinDbg log SHA-256 is
`441971C83FD8E29125C7960B279C59AEC2AEEF58C01B9038DD7546B8851E7F37`.
Runtime V8 is closed.

Runtime V9 development adds explicit file-create authorization without
renumbering the accepted `FILE_ANY_ACCESS` IOCTL ABI. Zero-access, read-only,
write-only, malformed, and incomplete control sessions fail closed; only a
session with effective read and write rights reaches IOCTL dispatch.

Gate 47 accepted that boundary for source commit
`c2b19f024c71e6f6fd2ca9ad2358767c9ae8ef6e`. It denied zero-access,
read-only, and write-only opens with Win32 error 5, admitted read/write,
recovered the exact four-processor coherent observation baseline after close,
and unloaded canonically. The evidence archive SHA-256 is
`FEDB560EFD3F139340496AAF7AE33AB53DACCC663D30B1650DC2FE6E4E747713`;
the closed WinDbg log SHA-256 is
`AB718AEFFC92F890BE0293BA6AD0F20F64108470198187232B17A17C383434E6`.
Runtime V9 is closed.

Runtime V10 development makes the control-device namespace explicit. Only the
canonical root path is admitted; any trailing name fails closed before IOCTL
dispatch while the accepted access, exclusivity, and IOCTL ABI remain intact.

Gate 48 accepted that boundary for source commit
`e63fca1e47e6e095f71a7f218be7589baa54ac8c`. It denied the named
read/write open with Win32 error 5, admitted the canonical root, recovered the
exact four-processor coherent observation baseline after close, and unloaded
canonically. The evidence archive SHA-256 is
`CDD0600139DEB61051DAF8C08E9F6580823A3E632C686A5A3E42F54D97702E1A`;
the closed WinDbg log SHA-256 is
`5A6C7DD231D2B4304C98CF340EDC4AAE2231F911F7101348B913675FE2E2FE4B`.
Runtime V10 is closed.

Runtime V11 development binds each admitted file object to its creating
user-mode process. IOCTLs issued through an inherited handle by another process
fail closed before runtime dispatch, without changing the public IOCTL ABI.

Gate 49 accepted that boundary for source commit
`0fd7f09242d50f873a9e76bd800075bd64f30a3f`. It admitted exact owner
observations before and after the inherited child attempt, denied the child
IOCTL with Win32 error 5, recovered exact four-processor state, and unloaded
canonically. The evidence archive SHA-256 is
`0A084E4787AD140DFFDD1757C2955097B6C21F6C888D78F18C374180519F663C`;
the closed WinDbg log SHA-256 is
`C48F4D10E0A859D4D947C54A81A37ED26FC6C3C319C5EB17F1690AD281FDF5E1`.
Runtime V11 is closed.

Runtime V12 development replaces reusable numeric owner PIDs with referenced
process-object identity. The WDF file object retains the admitted `PEPROCESS`,
compares each IRP's requestor object before dispatch, and releases the reference
from object-context cleanup after the final inherited handle closes. The public
IOCTL ABI is unchanged.

Gate 50 accepted that boundary for source commit
`df2910d42e51dab45fb34711a5ad2f07a72968de`. It admitted the owner, confirmed
that owner exited while the inherited file object remained alive, denied the
post-exit child IOCTL with Win32 error 5, recovered exact four-processor state
after the final inherited handle closed, and unloaded canonically. The evidence
archive SHA-256 is
`7E168042CD9B6B7C3587D74AA9FE620C094CFCBA4D2447CD33D0C140F18B8841`;
the closed WinDbg log SHA-256 is
`CE925881AC8D6F999B009A1C1439725EE57ACE04A0D2391D75CEB5938ADFC82E`.
Runtime V12 is closed.

Runtime V13 development adds a fixed 16-byte protocol request/response and
requires exact version `1.0` negotiation for every admitted file object before
operational IOCTL dispatch. Existing IOCTL values and layouts remain unchanged;
the negotiation function is additive at `0x817`. The loader closes the device
handle on any negotiation or response mismatch.

Gate 51 accepted that boundary for source commit
`87a58b15f6a3ca12211ecd7cf97679c0cd6fcbdc`. It denied an unnegotiated
operational IOCTL and a wrong-major negotiation with Win32 error 1306, accepted
exact protocol `1.0`, recovered exact four-processor observation state from a
new negotiated session, and unloaded canonically. The evidence archive SHA-256
is `E87AFDC490E37B699710DBA2AF72F536D57F42A30C002FFDEC08C294FE90020A`;
the closed WinDbg log SHA-256 is
`1E228A6C243AE4FED251B8B575519B1F3ACEB81A286AA386C87D0FA1F527DA56`.
Runtime V13 is closed.

Runtime V14 development assigns one canonical input length to every negotiated
operational IOCTL. Five legacy commands require zero input bytes; all fixed
request-bearing operations require exactly 16 bytes. Known mismatches fail
with `STATUS_INFO_LENGTH_MISMATCH` before runtime dispatch, while existing
handler validators remain defense in depth. Public IOCTL values and wire
layouts are unchanged. Gate 52 passed and Runtime V14 is closed.

Runtime V15 makes successful protocol negotiation an immutable, one-shot
per-file transition. Gate 53 denied exact and incompatible replays with error
5, preserved the original session authority for an exact observation query,
recovered all four initial records, and unloaded canonically. Evidence archive
SHA-256:
`F71F25AF564AC4E3B72AA9209BD6EF0771ECF919167B8010DB3DA7B802B19246`;
closed KD log SHA-256:
`DB987376F728EC6AA27734D509ECF86574AB96DB49AC275BB76410557A380B85`.
Runtime V15 is closed.

Runtime V16 assigns one canonical output length to each of the 21 fixed-width
operational IOCTLs. One-byte-short and one-byte-long buffers fail with
`STATUS_INFO_LENGTH_MISMATCH` before runtime dispatch. The persistent-status
and CR3-status aggregate responses remain topology-dependent and handler-owned;
public IOCTL values and wire layouts are unchanged. Gate 54 passed for source
commit `db3f206`, with short and oversized fixed responses denied, exact
responses accepted, fresh-session observation recovery, and canonical unload.
Evidence archive SHA-256:
`D1302436A2B6167F8A98BF81A13092607004DFD05A7FA902B7E5D3BCBC92E716`;
closed KD log SHA-256:
`EAE48FEFAEA0A6A0CC5D2574ECE22468D2D13F790DE35219FDF970EEECAE3AC9`.
Runtime V16 is closed.

## Reviewed identity

- Source commit: `418e9a163657899cfb48d5091e286a6465e35571`
- Branch: `feature/hypervisor-core`
- WDK: `10.0.28000.0`
- Build result: `360/360`
- CTest result: `228/228`
- Release-candidate archive SHA-256:
  `10C09F2E98540BF83686BC3A197B424FF00EA63DCDBDE5F0A68976A0CE9E4FF1`
- Release manifest SHA-256:
  `0FF8877DD76DDA9CA47334D1806D0DB087DA2BEDAEA09737A32C9D78E9E00465`

## Accepted release evidence

Gate 35 completed on 2026-08-29 in the supervised disposable VM.

- Result: `PASS`
- Probe: `RELEASE_CANDIDATE_LIFECYCLE`
- Evidence archive SHA-256:
  `42344FE95A5B636789ABF4CE5601E305EBB03C19EC039E34077884BBADCCDAE2`
- WinDbg log SHA-256:
  `9FBF157E57ABD0F634A07BEB53F84018BC49D816846C996260C5C3F797185DA7`
- Driver signature: `Valid`, signer `CN=hypersinv`
- Runtime/EPT/combined status: `LOADED` / `READY` / `READY`
- CR3 observation: `INACTIVE`
- Live-gate evidence: `EXTERNAL`
- Unload: canonical; service and driver module absent afterward
- WinDbg: one successful authoritative query; no bugcheck, fatal VM-exit,
  fail-closed result, or retained module
- Private signing material: excluded from the package

## Scope and limitations

This is a test-signed research release, not production software. Accepted live
evidence covers the documented disposable VMware Windows 11 environment with
two processor groups and two processors per group, Hyper-V/VBS disabled, and
the configured test root trusted. It does not claim Hyper-V/VBS coexistence,
processor hot-add/remove, general hardware compatibility, or production
support. VPID remains disabled in this release.

Runtime status describes authenticated current state; it does not embed or
manufacture external live-gate evidence. Reproduce live tests only in a clean
disposable VM and never bypass platform security controls.

## Runtime V4 reviewed release

Runtime V4 adds optional capability-authenticated VPID support with dynamic
group-safe identities, invalidation before activation and reuse, transactional
VMCS composition, safe no-VPID fallback, and truthful per-processor status.
Accepted Gate 36 evidence proves exact live VPIDs 1 through 4 and canonical
retirement. Gate 37 qualifies 100 complete lifecycles. Gate 38 proves that the
exact VPID identities remain unchanged across a CR3 target-residency
ENTER/LEAVE roundtrip.

Gate 38 evidence archive SHA-256 is
`31426B484F5C126F58A4C15086742738D7FB9E10447F7C56C55C99D5B799197E`; its
closed WinDbg log SHA-256 is
`C8E0777D4FDB4F8393A10A0A935D4A28126775309CA8BF487D36E6079E9780CB`.

Gate 39 completed on 2026-08-29 and closes Runtime V4 as a reviewed research
release.

- Source commit: `3bd85015967c7bd12f06e92abf98772c3a59d771`
- Release manifest SHA-256:
  `9F2551137027E9FC1820ED77EE724ABDF8372ABC700A1BB205EDC9828D871591`
- Evidence archive SHA-256:
  `50482275082187953CEC856BB5D71423B66EB47AD6C0D4060646081BCD2E424A`
- WinDbg log SHA-256:
  `BEB4753D566541E8BFE1D7F3FAD18F32BC39B7059D80EDB12EA349222511ACEC`
- Signature: `Valid`, signer `CN=hypersinv`
- Runtime/EPT/combined status: `LOADED` / `READY` / `READY`
- CR3 observation/live evidence: `INACTIVE` / `EXTERNAL`
- VPID identity: exact `1,2,3,4`
- Unload: canonical; service absent afterward
- Private signing material: excluded

## Runtime V17 through V20 process-target qualification

Runtime V17 adds a referenced process-target lifecycle with stable process
identity, guarded CR3 routing, sticky exit invalidation, and redacted status.
Gate 55 accepted that boundary with evidence archive SHA-256
`BC6011360050E88596306CA76FDA91365DCF7C887E45C9F10B521433FD559676`.

Runtime V18 adds exact detached target release and explicit rebind while
preserving ACTIVE denial. Gate 56 passed on 2026-09-05. The immutable package
SHA-256 is
`C844DF322FD1D8806277919DFAE51BC3025FB5309FE7284077EE3A6BB0380F19`,
the evidence archive SHA-256 is
`B662684C91F16C166246A9DC2C26AACD95B84513AC6793BA4ADA8764D139B86A`,
and the closed KD log SHA-256 is
`35C89FD709CA1EFC25745D7082904B5AB6B35BB95BE3CCBE5D2580F5DA30B4CA`.
The run proved canonical EMPTY publication, reconfiguration, ACTIVE release
denial, sticky expiry, fail-closed routing, and canonical teardown.

Runtime V19 adds device-lifetime binding generations and an additive matched
release request carrying PID, process start key, and observed generation. Gate
57 passed on 2026-09-05 for source commit
`ace5b15e950f61fb7ab6dc2dbefb96842d2c0231`. It proved that a stale generation
cannot mutate or release the current target, an exact match releases to
canonical EMPTY, rebinding the same process advances generation 1 to 2, and an
ACTIVE matched release remains BUSY. The immutable package SHA-256 is
`0312C3C1C5FF04BC298D565C62774822139CD03CF6B8CE01837E5A9C4A988DB7`,
the evidence archive SHA-256 is
`FA6978BE8DDB1D285463F8C594BFA6DF203429A10F2590F613C89652A4562F62`,
and the closed KD log SHA-256 is
`6CBFCADF4AE0B5FF0280320CA9F06E4C38FD362CC81F1ECCD40BCF29B3765AEF`.
Runtime V19 is closed.

Runtime V20 adds an exact configure-self request that contains no PID, process
handle, pointer, or CR3 value. The driver derives the target from the
authenticated control-session owner's referenced process object and takes an
independent target-lifetime reference. Gate 58 passed on 2026-09-05 for source
commit `c861d053645168361be28c749af8016feea67dca`. It proved session-owned
CONFIGURED identity, exact current-process equality, ACTIVE self-target
residency `1/0/1`, sticky expiry, fail-closed routing, and canonical teardown.
The immutable package SHA-256 is
`9DEF0816549C71D1A5B385BCFC4EAC3E48A452E423EDCD8192E734F2E001182B`,
the evidence archive SHA-256 is
`7D1D0CD58E407A15C2A548C9E3162F38B349B5C977780C3D875D23EB8C60A44C`,
and the closed KD log SHA-256 is
`4757E16C0C99E6D1E0DE08AB7014C6127D6752C8B4B94C4D0F8ABBE213158AF8`.
Runtime V20 is closed.

## Runtime V21 authenticated capability manifest

Runtime V21 adds an authenticated read-only manifest at function `0x81D`.
Every loader device open now verifies the exact version-1 capability bitmap
and 29-operation inventory immediately after protocol negotiation and before
operational mutation. Gate 59 passed on 2026-09-05 for source commit
`1545845c583ef76a9874a247b4e259cce66ae5fd`. It proved identical manifests on
independent LOAD, status, and UNLOAD sessions while preserving canonical
lifecycle behavior. The immutable package SHA-256 is
`C105CBC196A8B486332D696394C8783863072526061C71936486FBD8D3FA196C`,
the evidence archive SHA-256 is
`3F8FBE4973EDF289A270E820A074536910280F3A45FAD69F56ABB3DCC57FA297`,
and the closed KD log SHA-256 is
`B507BB62049BBCC591865D7119B0B3297FA920EC452E185DB38C99286A6985B7`.
Runtime V21 is closed.

## Runtime V22 fail-closed VMX substrate admission

Runtime V22 classifies the virtualization substrate before mutation. Native
execution with no hypervisor present and exact VMware nested VMX are admitted;
Microsoft Hyper-V, unknown vendors, and malformed identities fail closed. The
loader checks before SCM mutation, and the kernel independently checks after
detached-owner proof and before runtime configuration. No public IOCTL,
protocol, capability manifest, or wire layout changed.

Gate 60 passed on 2026-09-05 for source commit
`722073bd3d06c99c646f37a41e8da90b690b100e`. The immutable package SHA-256 is
`D49020C6EF09907B23DC904F547979883EF11AB912371FC1C947F04F19CC0BD1`,
the evidence archive SHA-256 is
`173F014CA706CEC124F4644FE4EEEECDAE754E87CFE4B20ABDC929DA40117887`,
and the closed KD log SHA-256 is
`A0FB3FB6A7AEAE5561D3433F2D77D9399BDE68C4CFB057172613A7009AA1BEBA`.
The VM was powered off and the clean snapshot restored after PASS. Runtime V22
is closed.

## Runtime V23 topology-wide VMX substrate consensus

Runtime V23 replaces the kernel's single-CPU substrate sample with one
fail-closed consensus over every active system processor across all processor
groups. The kernel maps each system index, temporarily pins and verifies the
group/number identity, samples CPUID locally, restores affinity, and requires
one identical admitted class before runtime configuration. Mixed allowed
classes, rejected identities, mapping or identity failures, and incomplete
enumeration are denied. Public IOCTL, protocol, capability, and wire contracts
are unchanged.

Gate 61 passed on 2026-09-05 for source commit
`abd5ea866b7c64f03ac9a8eb043d42caca598353`. The immutable package SHA-256 is
`A79F73F4B28093251C47A3FF316009769B38863AF72A6680BF8B8E524178902D`,
the evidence archive SHA-256 is
`418F74E4CF57BD3D46C424985E7FA8E381DB6C13176D96F86DCAE433CB8A8B79`,
and the closed KD log SHA-256 is
`113CA685CE33E26F2FF614B19B8DAABEAB00257F8DF1D02CAB73B17998EDB6E0`.
The exact class-2/four-processor marker preceded runtime configuration;
canonical unload, service absence, empty stderr, clean failure events, and no
new dump were preserved. The VM was powered off and the clean snapshot restored
after PASS. Runtime V23 is closed.

## Runtime V24 topology-wide VMX preentry readiness

Runtime V24 samples CPUID VMX support, CR4.CET, `IA32_FEATURE_CONTROL`, and
`IA32_VMX_BASIC` on every active processor after substrate consensus and before
runtime configuration or allocation. One dependency-free policy requires
ordered, complete, valid samples and exact raw VMX-basic agreement. The
admitted VMX-basic value is handed directly to configuration construction;
per-processor preentry checks remain active immediately before mutation.
Public IOCTL, protocol, capability, and wire contracts are unchanged.

Gate 62 passed on 2026-09-05 for source commit
`55d1b7379114a5683e0d064556a9bbe083af175d`. The immutable package SHA-256 is
`606E79BB019502D34079954E694534D380EEC6CB5FCED6508EFCC029D0F158C6`,
the evidence archive SHA-256 is
`CFB641B08A0D97D40E27A6387D56F5653F3A0B1C9DE4B96A98D8B716A65E23B8`,
and the closed KD log SHA-256 is
`F3C0079504449C623D9432DB8567112D7E79A697C7BBD9CB10FF3CBA029B8F73`.
The exact revision-1/four-processor marker published raw VMX-basic value
`0x1d8100000000001` before runtime configuration. Canonical unload, service
absence, empty stderr, clean failure events, and no new dump were preserved.
The VM was powered off and the clean snapshot restored after PASS. Runtime V24
is closed.

## Runtime V6 qualification

Runtime V6 adds an authenticated, owner-serialized, fixed-width redacted view
of the most recent per-processor VM-exit dispatch. Guest RIP, exit
qualification, guest CR3, diagnostic targets, kernel pointers, and raw exit
reason metadata are excluded from the public response.

Gate 43 passed on 2026-08-29 and accepts the exact initial four-processor
dispatch baseline:

- Source commit: `1468c1da4c0442a8aff85a18cb8fd5862b53f058`
- Evidence archive SHA-256:
  `CDE0D45C0A65F22623A3C81C137D23E998B24378FE926EB88C6F77EF6846EE04`
- WinDbg log SHA-256:
  `7CAE0A8A3813174B2EA8ACDA6AB40C78513EA38F177A7A2F17113A0D20A0B1CA`
- Signature: `Valid`, signer `CN=hypersinv`
- Initial records: generation 1, telemetry/dispatch sequence 2, VMCALL reason
  18, instruction length 3, resident action/protocol, successful VMCS access,
  and zero error/fail-closed state on all four processors
- Unload: canonical; service and driver module absent afterward

Gate 44 passed on 2026-08-29 and closes Runtime V6:

- Source commit: `17c085eab8389dbad81dbd3d5bc17f4092385bab`
- Evidence archive SHA-256:
  `32DD19D2C337C10DE0AE6E72917E776A611D6BB7CC72FF0381CC7D2097AA3EBA`
- WinDbg log SHA-256:
  `851E565D9C16AE04066A5023B6DE7EF33D060E81CE6E6670F8E8F18B23FA0CBB`
- Target telemetry/dispatch sequences: exact `2 -> 10`
- Target total/CPUID/VMCALL deltas: exact `4/3/1`
- Non-target records: unchanged at sequence 2
- Final target dispatch: action 5, protocol 7, validation flags `0x33`, no
  VMCS error or fail-closed state
- Unload: canonical; service and driver module absent afterward
- Failure events: none

The decision is **GO - Runtime V6 research scope complete**.

Runtime V5 work is tracked separately in
[ROADMAP_RUNTIME_V5.md](ROADMAP_RUNTIME_V5.md). It does not retroactively
change the accepted Runtime V4 release identity.

## Runtime V5 release candidate

Runtime V5 adds coherent, authenticated, per-processor VM-exit telemetry with
exact group-aware identity, owner generation, classified counters, and
independent fail-closed accounting. Gate 40 and Gate 41 are accepted:

- Gate 40 evidence archive:
  `476AFDC89A38DC0398EAF25E469F88EAC9055D4C5C93382876AE03B0625140C2`
- Gate 40 WinDbg log:
  `2419938514A22FB8E3EFFBF20201ED8DB20051854619EF99846D451DF998B846`
- Gate 41 evidence archive:
  `CA26D6B328CF5745F27DA29C0C11B8F16061692B29FC95635E65A398438F24AA`
- Gate 41 WinDbg log:
  `CC45CA32BF1F355F9C5B3F982757B5FE99E0238481091EF8B5B5453A0239256E`

Gate 42 completed on 2026-08-29 and closes Runtime V5 as a reviewed research
release.

- Source commit: `88e957674b9323ea4aa32769265ebcef5fb42a57`
- Release manifest SHA-256:
  `2E52A110971373A69D9EB4E1A2B6C9A69349CD72F0C67D2C9F64C28DAACDF194`
- Evidence archive SHA-256:
  `C2505BE97028A72191A7372475C9F02668D3FF3CCA444E60E3DE47AF1AED3DC0`
- WinDbg log SHA-256:
  `C4161C847E979039A9E0275E145513382F588BFC5611AD0C44DCCDB1D6D1B4D7`
- Signature: `Valid`, signer `CN=hypersinv`
- Runtime/EPT/combined status: `LOADED` / `READY` / `READY`
- CR3 observation/live evidence: `INACTIVE` / `EXTERNAL`
- VPID identity: exact `1,2,3,4`
- Initial telemetry: generation 1, sequence 2, total/VMCALL 1, all other
  classified counters and fail-closed events zero on every processor
- Unload: canonical; service and driver module absent afterward
- Private signing material: excluded
