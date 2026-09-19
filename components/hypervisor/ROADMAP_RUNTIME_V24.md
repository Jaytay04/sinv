# Runtime V24 - Topology-Wide VMX Preentry Readiness

Status: complete. Gate 62 accepted on 2026-09-05.

## Problem

Runtime V23 proves one allowed virtualization substrate across every active
processor before runtime configuration. The configuration builder still reads
CPUID VMX support, CR4.CET, `IA32_FEATURE_CONTROL`, and `IA32_VMX_BASIC` only
on the IOCTL-dispatch processor, then uses that processor's VMX basic value to
stamp every VMXON and VMCS region. Per-processor activation revalidates local
preentry state, but a nonuniform topology can therefore allocate the runtime
and advance into staged activation before discovering that one processor has a
different VMX region contract.

## Decision

After substrate consensus and before runtime configuration or allocation,
sample the VMX preentry prerequisites on every active system processor. Require
CPUID VMX support, CR4.CET clear, locked `IA32_FEATURE_CONTROL` with VMXON
outside SMX enabled, and a valid identical `IA32_VMX_BASIC` value across the
whole topology.

Keep group enumeration, affinity, CR4, CPUID, and MSR access in a WDK adapter.
Keep sample validation, ordered consensus, exact VMX-basic agreement, and
completion in a dependency-free policy. Build the runtime configuration from
the admitted consensus VMX-basic value rather than resampling an arbitrary
processor. Existing per-processor checks remain defense in depth immediately
before VMX mutation. No public IOCTL, protocol, capability, or wire layout
changes.

## Checkpoints

1. **V24A - Pure readiness policy**
   - validate one reserved-zero preentry sample;
   - reject missing VMX, active CET, unsafe feature control, or malformed
     VMX-basic state;
   - require ordered, complete, exact VMX-basic agreement.
2. **V24B - Group-aware readiness adapter**
   - enumerate `ALL_PROCESSOR_GROUPS` by system index at `PASSIVE_LEVEL`;
   - map, pin, and verify each group/number identity;
   - capture CR4, CPUID VMX, feature control, and VMX basic locally;
   - restore affinity before consuming each sample and allocate no memory.
3. **V24C - Authoritative configuration handoff**
   - run readiness after substrate consensus and before configuration;
   - construct configuration from the admitted VMX-basic consensus value;
   - retain local preentry checks immediately before each processor's mutation.
4. **V24D - Gate 62 qualification**
   - prove exact readiness across the accepted two-group/four-processor VMware
     topology;
   - require an exact processor-count and VMX-revision KD marker;
   - preserve capability verification, persistent/EPT readiness, canonical
     unload, service absence, clean events, empty stderr, and no dump.

## Acceptance criteria

1. Zero, skipped, duplicate, extra, incomplete, malformed, or reserved samples
   cannot produce admission.
2. Any missing VMX, active CET, unsafe feature-control state, or invalid
   VMX-basic value fails before runtime configuration or allocation.
3. Every processor must report the exact same raw VMX-basic value used to stamp
   runtime regions.
4. Every successful affinity transition has exactly one restore before the
   sample is consumed or the next processor is visited.
5. Static mutation tests reject bypassed checks, weakened equality, resampling
   during configuration, missing restoration, and incomplete KD evidence.
6. Fresh WDK/MSVC/MASM build and full CTest pass before live execution.
7. Gate 62 runs once through the existing disposable-VM broker without a new
   Windows UAC prompt or automatic retry.

## Live-test boundary

Gate 62 is necessary because topology-wide privileged CR4/MSR sampling and the
consensus-to-configuration handoff are new live kernel behavior. One lifecycle
on the established two-group/four-processor VMware snapshot is proportionate.
Pure tests cover rejected and nonuniform samples. A true failure is preserved
for investigation and is not automatically retried.

## Accepted evidence

Gate 62 passed once through the existing unattended host-runner broker for
source commit `55d1b7379114a5683e0d064556a9bbe083af175d`.

- Fresh WDK/MSVC/MASM build: all configured targets completed.
- Full CTest: `280/280` tests.
- Package SHA-256:
  `606E79BB019502D34079954E694534D380EEC6CB5FCED6508EFCC029D0F158C6`.
- Evidence archive SHA-256:
  `CFB641B08A0D97D40E27A6387D56F5653F3A0B1C9DE4B96A98D8B716A65E23B8`.
- Closed KD log SHA-256:
  `F3C0079504449C623D9432DB8567112D7E79A697C7BBD9CB10FF3CBA029B8F73`.
- Kernel admission: revision `1`, four processors, and identical raw
  VMX-basic value `0x1d8100000000001` before runtime configuration.
- Runtime: persistent `LOADED`, EPT `READY`, combined `READY`.
- Teardown: canonical unload, service absent, all loader and mailbox stderr
  empty, no failure event, no new dump, and no hypersinv bugcheck, VMfail,
  fatal, or denial signature. The two unrelated baseline DriverEntry failures
  were unchanged from accepted Gate 60 and Gate 61 evidence.
- Host runner: VM powered off and
  `Gate6-HostRunner-ReducedMemory-Clean` restored after PASS; broker returned
  to `READY` without a Windows UAC prompt.

Runtime V24 is closed. Gate 62 must not be repeated.
