# Runtime V4 Roadmap - VPID and Translation-Context Lifecycle

## Baseline and objective

- Immutable Runtime V3 evidence commit:
  `418e9a163657899cfb48d5091e286a6465e35571`
- Development branch: `feature/hypervisor-core`
- Initial state: VPID remains disabled in production VMCS controls;
  legacy assignment helpers exist, but kernel INVVPID helpers are stubs.
- Objective: add optional, capability-authenticated VPID support with explicit
  ownership, invalidation-before-reuse, safe fallback to the accepted no-VPID
  runtime, and proportionate disposable-VM evidence.

VPID is a performance and translation-context feature, not a reason to weaken
correctness. Runtime V4 must remain correct when VPID is unsupported or
disabled. No VMCS control may be enabled before its capabilities, backing
state, invalidation operations, and teardown obligations are all proven.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V4A Capability model | Complete | Pure decoding/validation of the VPID and INVVPID capability surface; production wiring remains forbidden. |
| V4B Identity ownership | Complete | Dynamic, group-safe nonzero VPID allocation with no alias or reuse before invalidation. |
| V4C Native invalidation adapter | Complete | Narrow WDK/MASM adapter for supported INVVPID types with injectable pure contracts and exact result mapping. |
| V4D VMCS/lifecycle composition | Complete | Transactional control/VPID writes, capability fallback, activation, rollback, invalidation, teardown, and truthful status. |
| V4E Static authorization | Complete - GO | Independent ABI, ownership, capability, invalidation, rollback, unload, and Gate 36 authorization passed. |
| V4F Live activation | Complete | Gate 36 proved exact live VPID activation, per-CPU VMCS identity, invalidation lifecycle, canonical unload, and service absence. |
| V4G Lifecycle qualification | Complete | Gate 37 completed 100 exact accepted Gate 36 transactions with clean lifecycle and debugger evidence. |
| V4H Interoperability and release closure | Complete | Gate 38 interoperability and Gate 39 clean-commit package provenance are accepted. Runtime V4 is closed. |

## V4A - capability model

Create a WDK-free model for `IA32_VMX_EPT_VPID_CAP` that distinguishes the
INVVPID instruction from its individual-address, single-context, all-context,
and single-context-retaining-globals types. Runtime V4 requires INVVPID,
single-context, and all-context support before activation. Optional types must
be represented without becoming accidental requirements.

Exit when exact bit decoding, raw/decoded consistency, reserved-field
validation, missing-capability rejection, mutation-backed contracts, and pure
unit tests pass. The model must remain unwired and production VPID controls
must remain clear throughout V4A.

V4A completed with exact decoding of bits 32 and 40-43, an explicit required
set of INVVPID plus single-context and all-context invalidation, optional type
preservation, malformed-state rejection, pure unit tests, and a mutation-backed
unwired contract. The fresh WDK/MSVC/MASM build completed `363/363` targets and
the full CTest suite passed `230/230`. No intrinsic, VMCS VPID field, secondary
VPID control, driver path, loader path, or live test was added.
The V4A boundary was explicit: **Do not add an intrinsic** or production
wiring until the later native-adapter and lifecycle milestones.

## V4B - identity ownership

Define a dynamic VPID owner keyed by the existing topology identity rather than
group-local processor numbers. VPID zero is always invalid. Allocation must
reject overflow and duplicate ownership. A retired identifier cannot return to
the free set until its required invalidation has completed successfully.

Exit when multi-group identity allocation, exhaustion, rollback, destroy,
retry, and invalidation-before-reuse are covered without fixed-64 or group-0
assumptions.

V4B completed with caller-owned dynamic storage keyed by the exact frozen
group/number/system-index identity. Deterministic nonzero identifiers cannot
alias, retirements remain pending until explicit invalidation acknowledgement,
and destroy fails while any identifier is owned or pending. Pure unit and
mutation-backed unwired contracts cover multi-group allocation, overflow,
duplicate identity, output aliasing, retry, reuse, corruption, and teardown.

## V4C - native invalidation adapter

Add one narrow WDK-only adapter that executes only policy-approved INVVPID
requests. Capability selection, descriptor construction, ownership, and retry
decisions remain in pure modules. The adapter validates IRQL, current VMX-root
state, request type, nonzero VPID where required, canonical linear addresses,
and exact instruction completion.

Exit when native results map to explicit statuses, failure preserves pending
ownership, unsupported types never execute, and mutation-backed tests prove the
adapter cannot manufacture success.

V4C completed with a pure descriptor/execution boundary and one WDK/MASM
adapter that admits only single-context and all-context INVVPID requests.
Current processor identity, DISPATCH_LEVEL, root ownership, descriptor shape,
and exact VM-success/VMfail-valid/VMfail-invalid outcomes are authenticated.
The emitted INVVPID object is covered by a disassembly contract.

## V4D - VMCS and lifecycle composition

Compose secondary-control bit 5 and the 16-bit VMCS VPID field only after V4A
through V4C succeed. Activation is transactional across every processor. Any
partial write, launch, invalidation, or teardown failure rolls back or remains
explicitly retryable without releasing the identifier.

The accepted no-VPID path remains the fallback. Status must report VPID as
`DISABLED`, `READY`, `ACTIVE`, or `TEARDOWN_PENDING`; it must not infer live
activation from processor capabilities or a nonzero VMCS field alone.

V4D completed with dynamic group-safe storage, a capability preflight that
falls back to the accepted no-VPID runtime when the required INVVPID types are
unsupported, initial invalidation before activation, transactional VMCS field
and secondary-control writes, exact rollback verification, and reverse-order
teardown. Identifier retirement remains pending until its final INVVPID
succeeds. The versioned `0x813` status query authenticates the selected
processor, root ownership, current VMCS, live control bit, and exact VMCS VPID
before emitting `ACTIVE`; it otherwise reports only the lifecycle state it can
prove.

## V4E - static authorization

Run a focused independent review of public ABI preservation, capability
authentication, topology identity, allocation ownership, instruction
descriptors, VMCS ordering, invalidation-before-reuse, unload retry, and status
truthfulness. Any ambiguity is a NO-GO for live activation.

V4E issued GO on 2026-08-29. A fresh WDK 10.0.28000.0 / MSVC / MASM build
completed all 379 targets, including the linked driver, loader, and emitted
INVVPID object. The full CTest suite passed 238/238. Mutation-backed contracts
cover capability fallback, exact topology identity, nonzero ownership,
invalidation-before-reuse, native result mapping, VMCS write/verify/rollback
ordering, unload retry state, pointer-free status ABI, sequential PASSIVE
dispatch, exact loader verification, and the fail-closed Gate 36 harness. No
driver was signed, installed, loaded, or live-tested on the host.

## V4F-V4H - live evidence and release

Gate 36 performs one supervised activation/invalidation roundtrip on the
disposable two-group/four-processor VM. It must authenticate the live secondary
controls, per-processor nonzero identifiers, invalidation completion, fallback
state, canonical unload, and service absence.

Gate 36 evidence `gate36-vpid-activation-20260829-124541.zip` has SHA-256
`88F44030664E7554B329B1193B12E9620CBA4505551E990A1D8C253E4075EDBA` and is
accepted. It proved exact ACTIVE records for system indices 0 through 3 with
group-aware identities, VPIDs 1 through 4, observed VMCS values 1 through 4,
required flags and capabilities `0x1f`, secondary controls `0x22`, canonical
unload, and service absence. The signed driver SHA-256 was
`75E811D2AC4AF2F2A7338B329A195D81F432E5CF72F915F1A03507613513C12C`.
WinDbg log `windbg-gate36-identity-fix-rerun_6e10_2026-08-29_12-45-11-990.log`
has SHA-256
`B6756C26B4C3D581CEC8480E2329C592937BD23AA26013265209C085749919F1` and records
only the operator-requested break and normal log closure, with no bugcheck or
fatal debugger event.

Only after Gate 36 passes, Gate 37 performs one 100-cycle lifecycle run. The
changed translation-cache and teardown semantics justify this stress boundary;
a separate 10-cycle run would add little coverage and is intentionally omitted.
Gate 37 stops on the first uncertain state and never retries in place.

Gate 37 evidence `gate37-vpid-lifecycle-100cycles-20260829-130503.zip`
has SHA-256
`413F8598130E3A8C9164A72B8D3525892919F862656A8DB1B80BF34B767E20FA` and is
accepted. It completed all 100 cycles, produced 100 exact PASS summaries and
100 nested Gate 36 archives, preserved deterministic VPIDs 1 through 4 on
every cycle, unloaded canonically, removed the service, and recorded no crash,
WHEA, or loader/PowerShell Application Error event. WinDbg log
`windbg-gate37-vpid-lifecycle-100cycles_6e10_2026-08-29_13-04-39-290.log`
has SHA-256
`BE9AA1CA1B5D6C6FCEE0290E3F437E6A038FEBA881FD62BEAD5D4939E82A7EEE` and
contains no bugcheck, fatal event, VPID failure, or fail-closed result.

Gate 38 is one bounded interoperability run using the unchanged accepted
signed runtime. It proves that an exact CR3 target ENTER/LEAVE residency
roundtrip does not alter, alias, or disable any live per-processor VPID. This
cross-feature boundary is not covered by the isolated Gate 17 CR3 proof or the
VPID-only Gate 36/37 evidence. Repetition is unnecessary because the accepted
100-cycle gates already cover both lifecycle families independently.

After Gate 38 passes, V4H packages the reviewed commit and records exact build,
test, signature, manifest, Gate 36, Gate 37, and Gate 38 hashes. Live evidence
remains external to runtime status.

## Safety and stop conditions

- Never live-load on a primary or irreplaceable machine; disposable VMs only.
- Never bypass Driver Signature Enforcement or use BYOVD techniques.
- Keep VPID disabled until V4E issues GO.
- Preserve the Runtime V2 public ABI and the accepted Runtime V3 no-VPID path.
- Stop on capability mismatch, VPID aliasing, reuse before invalidation,
  partial topology, ambiguous instruction completion, rollback failure,
  truthful-status regression, unexplained test regression, hang, or bugcheck.
- Do not add a live gate for a pure milestone. Batch related behavior into the
  smallest gate that changes hardware or lifecycle semantics.

## Immediate checkpoint

Runtime V4 is closed. Continue successor work in
[ROADMAP_RUNTIME_V5.md](ROADMAP_RUNTIME_V5.md); no further Runtime V4 live gate
is required.

Fresh host-only verification in `build/codex-gate38-20260829-133659`
completed all 379 WDK/MSVC/MASM targets, passed focused Gate 36-38 contracts
3/3, and passed full CTest 240/240. The immutable VM package
`gate38-vm-stage-accepted-gate36.zip` has SHA-256
`955B995CB33AB6870A3532396A007E84B08F5B538E9CA026B97D538547C616A0`.
It contains the exact accepted signed Gate 36/37 runtime artifacts; no runtime
binary was rebuilt or re-signed for Gate 38.

The first Gate 38 attempt was rejected by the harness before the CR3 probe.
The runtime's exact four-processor VPID status was correct, but the script's
expected strings omitted the public owner, flags, capabilities, and secondary
control suffix. Evidence ZIP SHA-256 is
`619948EB1AB5C5A048AB63EBFC690DDEC948AEF36B6E8C691635B4717463DF12`; WinDbg
log SHA-256 is
`44A34BD4968DB0169845EFED3378019CC932D90300B8E1B70D81C9DA5F30D424`.
The corrected rerun uses the same accepted runtime binaries and authenticates
the complete VPID records; the VM must first be reverted to its clean
checkpoint because the fail-closed harness intentionally preserved the loaded
service.

Corrected rerun package
`gate38-vm-stage-accepted-gate36-record-fix.zip` has SHA-256
`36D16909348F73D0DEAAFF4840E714C66F34927BFEEB3037908ED1713B11299F`.
The corrected script has SHA-256
`990E7B94EC925A3B6A956F6CFD42CA31BE77205BD030F7F2AD253B73B3338F36`;
all accepted runtime binary hashes remain unchanged.

The corrected Gate 38 run is accepted. Evidence archive
`gate38-vpid-cr3-residency-interoperability-20260829-135244.zip` has SHA-256
`31426B484F5C126F58A4C15086742738D7FB9E10447F7C56C55C99D5B799197E`.
It proved unchanged exact VPIDs 1 through 4 across the CR3 residency
`0/1/0` ENTER/LEAVE transition, exact matched indices and write counts,
strict transitions `1/2/3`, final nonresidency, canonical unload, service
absence, no failure events, and no new dump. Closed WinDbg log
`windbg-gate38-vpid-cr3-record-fix_6e10_2026-08-29_13-52-17-209.log` has
SHA-256
`C8E0777D4FDB4F8393A10A0A935D4A28126775309CA8BF487D36E6079E9780CB`.
It contains one successful residency completion, two successful CR3 status
queries, no bugcheck, fatal event, VPID failure, fail-closed result, or retained
driver module.

Gate 39 is the final necessary Runtime V4 live boundary. It packages a fresh
full build from a completely clean reviewed commit, signs only the staged
driver copy, records exact Gate 36-38 evidence hashes, excludes private signing
material, and performs one package-provenance lifecycle with exact ACTIVE VPID
status. No repetition is justified because Gate 37 and the earlier accepted
stress gates already qualify lifecycle stability.

Fresh host-only Gate 39 authorization in `build/codex-gate39-20260829-140303`
completed all 379 WDK/MSVC/MASM targets, passed focused Gate 38/39 contracts
2/2, and passed full CTest 241/241. The release packager and VM gate parse and
their mutation-backed provenance, evidence, signing, exact VPID, teardown,
event, dump, and private-key-exclusion contracts pass. No driver was signed,
installed, loaded, or live-tested during authorization. That authorization
was later consumed by the accepted clean-commit Gate 39 package.

Gate 39 passed on 2026-08-29 for source commit
`3bd85015967c7bd12f06e92abf98772c3a59d771`. Its release manifest SHA-256 is
`9F2551137027E9FC1820ED77EE724ABDF8372ABC700A1BB205EDC9828D871591`,
the evidence archive SHA-256 is
`50482275082187953CEC856BB5D71423B66EB47AD6C0D4060646081BCD2E424A`,
and the closed WinDbg log SHA-256 is
`BEB4753D566541E8BFE1D7F3FAD18F32BC39B7059D80EDB12EA349222511ACEC`.
The VM proved the valid `CN=hypersinv` signature, exact VPID identities
1 through 4, authoritative runtime readiness, canonical unload, service
absence, and private-key exclusion. Runtime V4 is complete.
