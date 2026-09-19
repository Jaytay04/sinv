# Hypervisor Core - Completed Runtime V3 Roadmap

**Baseline**

- Planning baseline: `c54b14c`
- Branch: `feature/hypervisor-core`
- Scope: Persistent Runtime V3 integration and the path to research-project completion, while preserving the verified Runtime V2 ABI.

## Status at a glance

Persistent Runtime V3, production EPT/hooks, CR3 routing, transparent
CPUID/MSR/TSC/INVLPG behavior, CET suppression, truthful operator status,
bounded status-queue pressure, and reviewed-package provenance now have
accepted disposable-VM evidence. Gate 35 authenticated the immutable package
for commit `418e9a163657899cfb48d5091e286a6465e35571` and completed the defined
research-project scope on 2026-08-29.

| Milestone | State | Exit evidence / boundary |
| --- | --- | --- |
| V3A-V3D | Complete | Topology, lifecycle, context, persistent ABI/protocol/dispatcher, and MASM foundations implemented and tested. |
| V3E1-E6b | Complete | Pure routing/coordinator path implemented; WDK/MSVC/MASM `129/129`, CTest `40/40`. |
| V3E6c | Complete | Dynamic topology, owned resources, serialization, and all-or-nothing teardown are implemented and tested. |
| V3F | Complete | Supported legacy paths are excluded; authoritative loader/status and unload ownership are integrated. |
| V3G | Complete | Static ABI, ownership, topology, rollback, and safety contracts gate the live path. |
| V3H | Complete | Accepted 1/10/100-cycle and two-group/four-processor disposable-VM evidence. |
| Production EPT | Complete | Identity map, EPTP ownership, invalidation, and lifecycle are live-verified. |
| Production hooks | Complete | Hook roundtrip and production EPT-violation routing are live-verified with clean teardown. |
| Production routing | Complete | CR3 residency, CPUID/MSR/TSC/INVLPG transparency, and CET suppression are live-verified. |
| Operator/release | Complete | Gate 34 pressure and Gate 35 reviewed-package lifecycle evidence are accepted. |

## Completion decision

Gate 35 returned `Result=PASS` for the reviewed release candidate. The package
manifest hash is
`0FF8877DD76DDA9CA47334D1806D0DB087DA2BEDAEA09737A32C9D78E9E00465`.
The accepted evidence archive SHA-256 is
`42344FE95A5B636789ABF4CE5601E305EBB03C19EC039E34077884BBADCCDAE2`
and the closed WinDbg log SHA-256 is
`9FBF157E57ABD0F634A07BEB53F84018BC49D816846C996260C5C3F797185DA7`.
The run proved a valid `CN=hypersinv` test signature, exact Runtime `LOADED`,
EPT `READY`, combined `READY`, CR3 `INACTIVE`, external live-gate evidence,
canonical unload, service absence, one successful authoritative-status query,
and no retained module, bugcheck, fatal VM-exit, or fail-closed result.

The decision is **GO — Runtime V3 research scope complete**. This is not a
production-readiness claim. The accepted boundary is the supervised disposable
VM configuration, including two processor groups with two processors each,
test signing, and Hyper-V/VBS disabled. Processor hot-add/remove, Hyper-V/VBS
coexistence, and VPID activation are outside the completed scope.

## Critical path

1. **E6c - kernel-owned dynamic resources and serializer.** Establish kernel ownership, allocation, serialization, and teardown contracts before replacing legacy entry points.
2. **V3F - integration seam replacement.** Remove the legacy persistent `core_mgmt`, LOAD/STATUS/UNLOAD IOCTL, and `DriverEvtUnload` paths while preserving the Runtime V2 transient probe. Add status and loader behavior that reports only capabilities actually present and verified.
3. **V3G - independent static gate.** Validate ABI preservation, topology, ownership, unload/rollback safety, and the absence of fixed-size or group-0 assumptions. A failing or incomplete gate blocks VMX work.
4. **V3H - disposable-VM gate.** Run controlled 1/10/100-iteration and multi-group scenarios. Capture reproducible evidence for startup, operation, teardown, and failure rollback.
5. **Production integration.** Only after an independent V3G GO and a passing V3H baseline, integrate EPT, hooks, CR3 selection, VM-exit routing/transparency, CET, and operator surfaces in that order.
6. **Release qualification.** Complete stress, performance, security, and release gates with live disposable-VM evidence and truthful status output.

## Milestones and exit criteria

### V3E6c - kernel-owned dynamic resources/serializer (next)

Implement the kernel-owned resource model and serializer for persistent Runtime V3.
Define ownership, lifetime, synchronization, failure cleanup, and serialization boundaries in code and tests.
Exit when all supported topology sizes and groups are represented dynamically, no fixed-64 or group-0 assumption remains, malformed/partial state is rejected safely, and teardown is all-or-nothing.

### V3F - replace legacy control and unload paths

Replace the legacy persistent `core_mgmt`, LOAD/STATUS/UNLOAD IOCTL, and `DriverEvtUnload` integration with the persistent Runtime V3 coordinator/router seam; preserve the Runtime V2 transient probe ABI.
Add a status surface and loader that distinguish compiled, initialized, available, and live-verified capabilities.
Exit when legacy paths are unreachable in the supported build, status cannot overclaim VMX or production support, and unload/error paths preserve complete rollback.

### V3G - independent static gate

Run an independent static review/gate over ABI, module ownership, resource lifetime, topology, error handling, teardown, and safety boundaries.
Exit only with an explicit GO artifact covering the reviewed commit and test evidence.
Any unresolved contradiction, stale legacy path, fixed-size topology, or unverifiable status is a NO-GO.

### V3H - disposable-VM gates

Use disposable VMs only.
Execute 1-, 10-, and 100-iteration gates, then multi-group variants, covering load, initialization, operation, unload, restart, failure injection, and rollback.
Exit when results are repeatable, no orphaned resources or partial topology remain, and failures are classified with actionable evidence.

### Production EPT integration

Integrate identity mapping, explicit ownership, EPTP construction/selection, and INVEPT sequencing.
Exit when map ownership and invalidation ordering are proven in tests and live-verified on disposable VMs under normal and teardown paths.

### Production EPT violation and transactional hooks

Integrate EPT-violation handling and transactional hook/unhook with prepare/commit/rollback semantics.
Exit when partial hook application cannot escape, unhook restores the prior state, violations route through the production coordinator, and disposable-VM evidence covers retries and teardown.

### CR3 target selection

Integrate target-process/address-space selection with explicit validation and safe fallback.
Exit when target selection is deterministic, rejects stale or invalid CR3 state, and is covered by multi-group disposable-VM scenarios.

### Production VM-exit routing, transparency, and CET

Replace pure routing with production VM-exit handling while preserving transparency requirements and CET behavior.
Exit when all supported exits have explicit ownership, unhandled exits follow the documented safe path, CET state is preserved/restored, and live disposable-VM evidence shows no unintended interception.

### Operator/status UX

Expose loader and status information that accurately separates build support, initialization, runtime readiness, live verification, and safety-gate state.
Exit when operators can identify the exact gate/commit/evidence state without inferring capability from presence of a binary or device.

### Stress, performance, security, and release gates

Run repeatable stress and performance characterization, security review, and release packaging checks.
Exit when resource bounds, latency/throughput behavior, rollback under pressure, attack-surface review, signing/loader constraints, and documentation are all recorded for the reviewed commit.

## Foundation versus production claims

The following distinction is mandatory in status, reviews, and release notes:

| Claim | Allowed evidence | Not sufficient |
| --- | --- | --- |
| Pure foundation | Coordinator/router tests, static analysis, and the recorded `129/129` plus `40/40` results. | A successful compile by itself. |
| Kernel integration | Resource ownership, serializer, loader, status, and teardown tests on the supported kernel path. | A pure mock or an unexercised code path. |
| Live VMX behavior | Disposable-VM evidence for the exact reviewed commit, after V3G GO. | Pure tests, static checks, or a driver artifact. |
| Research completion | Passing V3H and all integration/release gates with truthful operator status. | A single successful boot or a manually observed run. |

No document or status command may collapse these categories. If evidence is missing, the state is `unverified`, not `ready`.

## Dependency and ownership rules

- E6c owns dynamic resource representation, serialization, lifetime, and cleanup contracts.
- V3F owns the integration seam, loader, status model, and removal of legacy control/unload paths.
- V3G owns independent static review and the VMX authorization decision.
- V3H owns disposable-VM orchestration, repetition counts, multi-group coverage, and rollback evidence.
- EPT work owns map ownership, EPTP selection, and INVEPT ordering; it depends on V3H.
- Hook work owns transactional prepare/commit/rollback and violation dispatch; it depends on EPT ownership and invalidation.
- CR3 work owns target selection and validation; it depends on stable runtime topology and routing contracts.
- VM-exit work owns production dispatch, transparency, and CET preservation; it depends on CR3 and hook contracts.
- Operator work owns truthful status presentation; it consumes gate and runtime evidence but cannot manufacture it.
- Release work owns stress, performance, security, packaging, and final evidence retention.

Each module must expose a narrow interface, identify its owner, and provide an explicit failure/teardown path. Cross-module changes must preserve the Runtime V2 ABI and keep responsibilities separable for tests and replacement.

## Gate evidence expectations

Every milestone exit record should identify:

- the reviewed commit and branch;
- the scope and topology exercised;
- the tests or static checks run and their pass/fail result;
- ownership and teardown/rollback observations;
- known exclusions, including any behavior not live-verified;
- the decision (`GO`, `NO-GO`, or `unverified`) and the next dependency.

V3G must be independently produced and must not be self-approved by the implementation author alone.
V3H evidence must include 1, 10, and 100 iterations and at least one multi-group scenario before production integration is authorized.
Production gates must preserve the same evidence shape so later claims remain auditable.

## Stop conditions and recovery

Stop the critical path on any ABI drift, stale legacy entry point, fixed-64 or group-0 assumption, ownership ambiguity, partial topology, failed rollback, unverifiable status, or unexplained test regression.
Record the failure at the owning milestone, restore the smallest safe boundary, and rerun only the invalidated checks after correction.
Do not advance by weakening assertions, hiding failures, or substituting mock evidence for live evidence.

If a disposable-VM run leaves uncertain state, discard the VM and classify the result as failed or unverified.
Recreate a clean disposable VM for the next attempt.
No experiment should depend on a potentially contaminated VM or on bypassing platform security controls.

## Completion checklist

- Runtime V2 ABI is unchanged and documented at the integration seam.
- Pure V3A-V3D and V3E1-E6b evidence remains green.
- E6c resource ownership and serializer contracts are covered for dynamic topology and groups.
- V3F has removed the supported legacy paths and reports truthful status.
- V3G has an independent GO artifact for the reviewed commit.
- V3H has repeatable 1/10/100 and multi-group disposable-VM evidence.
- EPT identity map, EPTP, ownership, and INVEPT are live-verified.
- EPT violations and transactional hook/unhook are live-verified with rollback.
- CR3 target selection is validated under supported topology and failure cases.
- VM-exit routing, transparency, and CET behavior are live-verified.
- Operator/status UX distinguishes compiled, initialized, available, and verified states.
- Stress, performance, security, packaging, and release checks pass.
- No BYOVD/DSE bypass, fixed-64, group-0-only, or partial-topology behavior remains.

## Safety boundaries

- No live VMX execution before an independent V3G GO; pure tests and static evidence do not authorize VMX.
- Disposable VMs only for live verification; never use production or irreplaceable workloads.
- No BYOVD, DSE bypass, or equivalent security-control circumvention.
- Topology changes are all-or-nothing: reject partial initialization and leave no orphaned resources.
- Support dynamic topology and groups; do not encode fixed-64 limits or group-0-only behavior.
- Preserve the Runtime V2 ABI while replacing implementation seams.
- Status and loader output must remain truthful; absence of live evidence is reported as unverified.
- Every hook, map, route, and allocation must have an explicit owner and rollback/teardown path.

## Research-project completion definition

The research project is complete when the Runtime V2 ABI and modular ownership model are preserved; V3E foundations remain passing; V3E6c through V3H gates have independent, reproducible evidence; production EPT, hooks, CR3 selection, VM-exit routing/transparency, and CET are integrated and live-verified on disposable VMs; operator status is truthful; and stress, performance, security, and release gates pass for a reviewed commit.
"Complete" does not mean merely compiling or passing pure tests: live behavior, teardown, rollback, and safety evidence are required.

## Successor roadmap

Runtime V3 is frozen at the reviewed source commit. New implementation work
belongs to [ROADMAP_VNEXT.md](ROADMAP_VNEXT.md), beginning with a WDK-free VPID
capability model while VPID remains disabled in the supported runtime. No
Runtime V4 work may retroactively change the accepted Runtime V3 evidence or
release manifest.
