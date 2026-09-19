# Runtime V23 - Topology-Wide VMX Substrate Consensus

Status: complete. Gate 61 accepted on 2026-09-05.

## Problem

Runtime V22 classifies the loader CPU and the kernel IOCTL-dispatch CPU before
mutation. That is necessary but not sufficient on a multi-group system: the
persistent runtime owns every active logical processor, while substrate
identity has been sampled on only one processor. A migrated caller or a
nonuniform virtual topology could therefore reach runtime configuration
without proving that every target reports the same admitted substrate.

## Decision

Require an exact topology-wide substrate consensus in the kernel after
detached-owner proof and before runtime configuration. Enumerate every active
system processor, map its group/number identity, pin the caller temporarily,
verify the current identity, classify CPUID locally, restore the prior affinity,
and feed the observation to one WDK-free consensus policy.

All processors must report the same admitted class. Any rejected class,
mixed allowed classes, mapping failure, affinity-identity mismatch, incomplete
enumeration, or affinity-restore uncertainty fails closed. The loader retains
its early current-CPU check before SCM mutation for operator feedback; the
kernel consensus remains authoritative. No public IOCTL, protocol, capability
manifest, or wire layout changes.

## Checkpoints

1. **V23A - Pure consensus policy**
   - accept exact ordered observations for one nonzero processor count;
   - require one identical admitted classification across the whole topology;
   - reject disallowed, mixed, duplicate, skipped, incomplete, or reserved
     state without manufacturing success.
2. **V23B - Group-aware kernel adapter**
   - enumerate `ALL_PROCESSOR_GROUPS` by system index;
   - map and verify every group/number pair while pinned at `PASSIVE_LEVEL`;
   - restore the opaque prior affinity after every successful pin;
   - allocate no memory and execute no VMX or control-register mutation.
3. **V23C - Authoritative LOAD boundary**
   - replace the kernel single-CPU check with topology consensus after exact
     detached-owner proof and before runtime configuration;
   - publish exact class/count KD evidence on success and actionable processor
     detail on rejection;
   - preserve Runtime V22 loader admission and all public contracts.
4. **V23D - Gate 61 qualification**
   - prove exact VMware class `2` consensus across the accepted two-group,
     four-processor topology;
   - preserve capability-manifest verification, persistent/EPT readiness,
     canonical unload, service absence, clean events, and no dump;
   - require the exact four-processor consensus marker in the closed KD log.

## Acceptance criteria

1. Consensus cannot complete with zero, fewer, extra, reordered, or skipped
   observations.
2. Native and VMware classes are never mixed, even though each is independently
   admitted by Runtime V22.
3. Every successful affinity transition has exactly one restore attempt using
   the opaque returned token before the next processor is visited.
4. Kernel admission occurs before runtime configuration, allocation owned by
   the persistent runtime, CR mutation, VMXON, or lifecycle publication.
5. Static mutation tests reject bypassed consensus, weakened identity checks,
   missing restoration, and an admission marker that omits class or count.
6. A fresh WDK/MSVC/MASM build and full CTest pass before live execution.
7. Gate 61 is run once through the existing disposable-VM host-runner broker;
   no Windows UAC prompt, manual GUI step, or automatic retry is permitted.

## Live-test boundary

Gate 61 is necessary because group-aware affinity traversal and per-processor
CPUID observation are new live kernel behavior. One deterministic lifecycle on
the established two-group/four-processor VMware snapshot is proportionate.
Pure tests cover rejected and mixed classifications; the live VM is not
reconfigured into unsupported substrates. A true failure is preserved for
investigation and is not automatically retried.

## Accepted evidence

Gate 61 passed once through the existing unattended host-runner broker for
source commit `abd5ea866b7c64f03ac9a8eb043d42caca598353`.

- Fresh WDK/MSVC/MASM build: `449/449` targets.
- Full CTest: `278/278` tests.
- Package SHA-256:
  `A79F73F4B28093251C47A3FF316009769B38863AF72A6680BF8B8E524178902D`.
- Evidence archive SHA-256:
  `418F74E4CF57BD3D46C424985E7FA8E381DB6C13176D96F86DCAE433CB8A8B79`.
- Closed KD log SHA-256:
  `113CA685CE33E26F2FF614B19B8DAABEAB00257F8DF1D02CAB73B17998EDB6E0`.
- Loader admission: exact `VMWARE_NESTED` classification before SCM mutation.
- Kernel admission: exact class `2`, four-processor consensus before runtime
  configuration.
- Runtime: persistent `LOADED`, EPT `READY`, combined `READY`.
- Teardown: canonical unload, service absent, empty loader and mailbox stderr,
  no failure event, no new dump, and no hypersinv bugcheck, VMfail, fatal, or
  denial signature. The two unrelated baseline DriverEntry failures were
  unchanged from accepted Gate 60 evidence.
- Host runner: VM powered off and
  `Gate6-HostRunner-ReducedMemory-Clean` restored after PASS; broker returned
  to `READY` without a Windows UAC prompt.

Runtime V23 is closed. Gate 61 must not be repeated.
