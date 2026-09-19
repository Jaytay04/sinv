# Runtime V5 Roadmap - Authenticated VM-Exit Observability

## Baseline and objective

- Reviewed Runtime V4 source commit:
  `3bd85015967c7bd12f06e92abf98772c3a59d771`
- Development branch: `feature/hypervisor-core`
- Objective: expose coherent, pointer-free, per-processor VM-exit telemetry
  without adding work to unrelated driver paths or weakening the accepted
  Runtime V4 lifecycle.

Runtime V5 treats observability as runtime state, not as external gate
evidence. Counters are owned by each persistent launch context, updated only
by its resident VM-exit dispatcher, and queried through the existing
serialized owner/runtime boundary. The public response authenticates processor
identity and owner generation but never exposes kernel pointers or guest CR3
values.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V5A Pure telemetry model | Complete | WDK-free reason classification, single-writer sequence protocol, coherent snapshot validation, and exact counter invariants. |
| V5B Runtime ownership | Complete | Append-only launch-context storage, VM-exit instrumentation, fail-closed accounting, and serialized per-processor snapshot query. |
| V5C Public query path | Complete | Versioned pointer-free ABI, exact IOCTL validation/dispatch, timeout-safe loader command, and authoritative identity/generation checks. |
| V5D Static authorization | Complete - GO | Fresh WDK/MSVC/MASM build, unit tests, mutation-backed contracts, launch disassembly, and script parsing pass. |
| V5E CPUID telemetry qualification | Complete | Gate 40 proved exact per-processor CPUID/VMCALL deltas, canonical unload, service/module absence, and closed debugger evidence. |
| V5F Route-matrix qualification | Complete | Gate 41 proved exact MSR-read, MSR-write, CR-access, EPT-violation, and VMCALL attribution in one disposable-VM lifecycle. |
| V5G Reviewed release closure | Complete | Gate 42 bound the clean pushed commit, fresh build, signed package, accepted Gate 40/41 evidence, exact telemetry/VPID status, and canonical unload. |

## Design contract

- Every persistent processor context owns one telemetry block.
- The VM-exit dispatcher is the only writer for that block.
- Sequence values are odd while a write is in progress and even when stable.
- A snapshot is accepted only when both sequence reads match, the sequence is
  even, and classified exit counts sum exactly to the total.
- Fail-closed events are counted independently from classified exits.
- Runtime queries require an attached owner, the loaded persistent lifecycle,
  exact system-index identity, a valid launch-context header, and a nonzero
  owner generation.
- The IOCTL accepts exactly one 16-byte request and emits one fixed 112-byte
  response. No pointers or variable-length payloads cross the ABI.

## Gate 40 - exact CPUID telemetry delta

Gate 40 is the next necessary live boundary because host-only tests cannot
prove which resident processor records real VM exits. It performs one load,
captures all four telemetry baselines, runs the already-qualified authenticated
CPUID probe on system index 0, captures all four telemetry snapshots again,
and unloads canonically.

PASS requires exactly four additional exits on system index 0: three CPUID
exits and one VMCALL exit. CR-access, MSR-read, MSR-write, EPT-violation,
other, and fail-closed deltas must remain zero. All counters on system indices
1 through 3 must remain unchanged. Processor identity and owner generation
must remain exact throughout the run, and the service must be absent after
unload.

No 10-cycle or 100-cycle repetition is justified for this milestone. The
underlying CPUID path and persistent runtime lifecycle already have accepted
stress evidence; Gate 40 adds one new observability boundary only.

Fresh host-only authorization in `build/codex-gate40-20260829-185820` built
all 382 WDK/MSVC/MASM targets, including the linked driver, loader, and MASM
objects. Focused telemetry/layout/disassembly verification passed 4/4 and the
full CTest suite passed 243/243 after updating closed Runtime V4 contracts to
their accepted state. No driver was signed, installed, loaded, or live-tested
during authorization.

The first Gate 40 attempt on 2026-08-29 is a harness-only rejection, not a
runtime failure. The driver loaded cleanly and returned four coherent ACTIVE
telemetry records with exact group-aware identities, generation 1, sequence 2,
total 1, VMCALL 1, and zero fail-closed events. Windows PowerShell 5.1 did not
match the hashtable's `UInt32` key with the script's untyped `Int32` zero, so
the harness rejected the valid system-index-0 record before running the CPUID
probe and intentionally preserved the loaded service. Evidence archive
SHA-256 is
`7E44CF778D8F21CA7812092D6008AF9EA2085AA220D3345097BAA79DACFF3631`;
the closed WinDbg log SHA-256 is
`5ACB9C9F6972D201736707AA92C26071862020993F495020701B5CC529C6C9A4`.
The corrected harness uses an explicit `UInt32` lookup and a valid
provider-scoped event query. Revert the VM to the clean checkpoint before the
single corrected rerun; do not continue from the preserved loaded state.

The corrected Gate 40 rerun on 2026-08-29 passed. Its evidence archive SHA-256
is `476AFDC89A38DC0398EAF25E469F88EAC9055D4C5C93382876AE03B0625140C2`.
System index 0 recorded exactly three CPUID exits and one VMCALL; the other
three processors were unchanged, fail-closed remained zero, unload completed,
the service was absent, and no failure event or dump was produced. The WinDbg
closed with the driver module absent. Its SHA-256 is
`2419938514A22FB8E3EFFBF20201ED8DB20051854619EF99846D451DF998B846`.
Gate 40 is accepted.

## Gate 41 - exact route telemetry matrix

Gate 41 is the final necessary Runtime V5 live boundary. It deliberately
batches the remaining deterministic routes into one load/unload lifecycle
instead of creating separate repetition gates:

- MSR transparency: total +5, RDMSR +3, WRMSR +1, VMCALL +1.
- CR3 probe: total +2, CR-access +1, VMCALL +1.
- EPT violation roundtrip: total +3, CR-access +1, EPT violation +1,
  VMCALL +1.

Each delta must occur only on system index 0. CPUID, other, and fail-closed
deltas must remain zero, non-target processors must remain unchanged, and
owner generation and group-aware processor identity must remain exact. The
cumulative target delta is ten exits: CR-access +2, RDMSR +3, WRMSR +1,
EPT-violation +1, and VMCALL +3.

No 10-cycle or 100-cycle Gate 41 repetition is justified. Each underlying
runtime route and the persistent lifecycle already has accepted live and
stress evidence; this gate qualifies only the new cross-route telemetry
attribution.

Fresh host-only authorization in `build/codex-gate41-20260829-193704` built
all 382 WDK/MSVC/MASM targets, including the driver, loader, and persistent
launch object. Focused telemetry, Gate 41, and disassembly verification passed
4/4; the full CTest suite passed 244/244. No driver was installed, loaded, or
live-tested on the host.

Gate 41 passed on 2026-08-29. Its evidence archive SHA-256 is
`CA26D6B328CF5745F27DA29C0C11B8F16061692B29FC95635E65A398438F24AA`;
the closed WinDbg log SHA-256 is
`CC45CA32BF1F355F9C5B3F982757B5FE99E0238481091EF8B5B5453A0239256E`.
System index 0 recorded the exact cumulative ten-exit matrix: CR-access +2,
RDMSR +3, WRMSR +1, EPT-violation +1, and VMCALL +3. The other processors
were unchanged, other/fail-closed remained zero, the service and driver module
were absent after canonical unload, stderr files were empty, and no failure
event or dump was recorded. Gate 41 is accepted.

## Gate 42 - Runtime V5 reviewed release candidate

Gate 42 is the final Runtime V5 live boundary. A host-side packager refuses a
dirty, unpushed, or wrong-branch source tree; copies only the fresh driver,
PDBs, loader, and Gate 42 script; signs only the staged driver copy; records
the accepted Gate 40/41 archive and debugger hashes; hashes the reviewed
documentation; and excludes private signing material.

The disposable-VM gate authenticates the manifest and every artifact, requires
the valid `CN=hypersinv` signature, verifies exact ACTIVE VPID and initial
per-processor telemetry records, checks truthful authoritative status, and
unloads canonically. One run is sufficient: Gates 40 and 41 already qualify
the new telemetry behavior and the older stress gates qualify lifecycle
stability. Gate 42 adds package provenance only; repetition would not add a
new hardware or lifecycle boundary.

Fresh host-only authorization completed on 2026-08-29 in
`build/codex-gate42-20260829-200157`: the WDK/MSVC/MASM build completed
382/382 targets, the focused release/telemetry/disassembly checks passed 4/4,
and the full CTest suite passed 245/245. No driver was signed, installed,
loaded, or live-tested during host authorization.

Gate 42 passed on 2026-08-29 for source commit
`88e957674b9323ea4aa32769265ebcef5fb42a57`. The release manifest SHA-256
is `2E52A110971373A69D9EB4E1A2B6C9A69349CD72F0C67D2C9F64C28DAACDF194`,
the accepted evidence archive SHA-256 is
`C2505BE97028A72191A7372475C9F02668D3FF3CCA444E60E3DE47AF1AED3DC0`,
and the closed WinDbg log SHA-256 is
`C4161C847E979039A9E0275E145513382F588BFC5611AD0C44DCCDB1D6D1B4D7`.
The run proved the valid `CN=hypersinv` signature, exact ACTIVE VPIDs 1
through 4, exact initial telemetry records with one VMCALL and zero
fail-closed events per processor, truthful external live-evidence status,
canonical unload, service and module absence, empty failure-event evidence,
and no crash dump. Runtime V5 is accepted and closed.

## Safety and stop conditions

- Never sign, install, load, or live-test the driver on the host machine.
- Run live gates only from the clean disposable two-group/four-processor VM
  checkpoint under WinDbg.
- Stop on any malformed snapshot, identity/generation mismatch, unexpected
  counter delta, fail-closed increment, hang, bugcheck, unload failure, retained
  service, failure event, or new dump.
- After a failed load or uncertain runtime state, do not retry LOAD and do not
  run UNLOAD manually. Preserve the VM and debugger state for investigation.

## Completion decision

The decision is **GO — Runtime V5 research scope complete**. Future work must
use a successor roadmap and must not alter or overclaim the accepted Runtime
V5 release identity or evidence.
