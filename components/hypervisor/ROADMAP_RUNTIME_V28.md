# Runtime V28 - VMX Preemption Watchdog

Status: accepted. Gate 67 passed once on the uniformly unsupported VMware
nested-VMX path; no repetition gate is required.

## Problem

Persistent diagnostic transactions currently have no hardware deadline after
VM entry. A malformed guest instruction stream or lost completion route can
therefore leave a logical processor resident in non-root mode indefinitely,
turning one failed diagnostic into a VM-wide hard hang.

## Decision

Use the VMX-preemption timer as a fail-closed watchdog when the complete active
topology exposes it. Admission requires every logical processor to agree on
timer availability. Uniform support additionally requires agreement on the
timer-rate field in `IA32_VMX_MISC[4:0]`; uniform absence is admitted as an
explicit unsupported mode, while a mixed topology is rejected.

On a supported topology, the persistent control decorator enables the
pin-based timer, deliberately keeps save-timer-on-exit disabled, and assigns
the maximum 32-bit initial value. The initial VMCS plan writes that value.
Before every externally initiated resident diagnostic or unload transaction,
composition rewrites the same value and reads it back exactly before VMRESUME.
A timer expiry is an explicit terminal dispatch result and never advances
guest RIP or continues the diagnostic.

On a uniformly unsupported topology, control construction and the VMCS plan
omit the timer without weakening any existing runtime control. Composition
reads the live pin-control field and records a typed BYPASSED marker rather
than touching an unsupported VMCS field. This preserves VMware compatibility
without claiming a watchdog that the nested hypervisor does not provide.

With save-on-exit disabled, hardware leaves the programmed VMCS value intact.
Internal VMRESUME operations therefore receive a fresh bounded interval. This
is intentional: it prevents any one non-root interval from hanging while
avoiding a dependency on the optional save-on-exit control that VMware's
nested-VMX surface does not expose.

## Checkpoints

1. **V28A - Pure control policy**
   - add a narrow persistent-only timer decorator;
   - reject unavailable controls, malformed base plans, and prior timer state;
   - preserve the generic planner's timer-disabled default.
2. **V28B - Topology admission**
   - sample `IA32_VMX_MISC` plus true/legacy pin controls on every
     active processor;
   - admit uniform support or uniform absence, reject mixed capability or a
     nonuniform supported timer rate;
   - publish the admitted availability and rate in the preentry marker.
3. **V28C - VMCS residency and rearm**
   - emit the timer guest field only when activation is supported;
   - rearm and read back the exact maximum value before every external guest
     transaction on supported hardware;
   - expose typed READY/ARMED or UNAVAILABLE/BYPASSED KD markers.
4. **V28D - Fail-closed dispatch**
   - recognize basic exit reason 52 explicitly;
   - return a dedicated terminal failure before reading instruction length or
     advancing RIP.
5. **V28E - Gate 67**
   - run EPT, CPUID, MSR-read/write, TSC, INVLPG, CET-VMCS, and HLT diagnostics
     once under one resident LOAD;
   - prove one exact, topology-consistent timer mode: four READY markers plus
     exact ARMED readbacks, or four UNAVAILABLE markers plus exact BYPASSED
     transactions; the seven diagnostics execute on their selected owner and
     unload executes once on each of the four processors;
   - prove no timer-expiry/fatal marker and canonical unload.

## Acceptance criteria

1. Timer availability is uniform across the full two-group/four-CPU topology
   before runtime configuration; a supported topology also has a uniform rate.
2. Supported VMCSs contain a nonzero timer value and activation control;
   unsupported VMCSs contain neither. Timer-save-on-exit remains disabled.
3. Every external transaction either rearms and reads back `0xFFFFFFFF` or
   records an exact BYPASSED marker from the live disabled pin control.
4. Exit reason 52 is explicit and fail closed.
5. Pure, unit, mutation-backed contract, WDK/MSVC/MASM, and complete CTest
   verification pass.
6. Gate 67 completes all seven existing diagnostics once, unloads canonically,
   and produces no crash, dump, failure event, stderr, fatal marker, or timer
   expiry.

## Live-test boundary

Gate 67 is necessary because static verification cannot establish the actual
nested-VMX capability surface or prove that the selected supported/unsupported
path completes a real lifecycle. One composite lifecycle is proportionate.
The gate intentionally does not execute an infinite guest: expiry behavior is
fail-closed in pure dispatch tests, while a deliberate hang probe would add
destructive risk without qualifying the normal runtime path. No 10/100-cycle
repetition gate is planned unless evidence shows an intermittent defect.

## Preserved first-run finding

The first Gate 67 package, source commit `8a19ea7`, is rejected evidence and
must not be retried. RunId
`gate67-preemption-watchdog-20260906-164734-92bfc67073` failed before VMX
mutation with readiness reason 7 on processor 0. LOAD rolled back canonically
and removed the service. The first diagnosis incorrectly attributed the
failure to save-on-exit.

The second package, source commit `7a4d035`, removed that false requirement but
failed at the same preentry boundary. Its closed KD evidence proves CPU 0's
pin-control allowed-1 mask omits timer activation entirely. VMware therefore
does not expose the VMX-preemption timer to this nested guest.

The final design treats the timer as a topology-qualified optional hardening
feature. It retains strict activation, exact VMCS residency/rearm, and reason
52 fail-closed behavior when supported, while admitting a uniformly
unsupported topology with explicit evidence. A fresh signed package and RunId
are required after static verification and clean-snapshot restoration.

## Accepted evidence

Implementation commit `ac97b7937d10249dd21e33d22d4a13a993e3ea33`
passed the complete WDK/MSVC/MASM build and all `289/289` static tests. Signed
package SHA-256:
`AFFA5C1DB9EC029D7731B3DBFB7D0F3C3460E1E2B0705F1F51BF0D1FD487694C`.

Gate 67 RunId
`gate67-preemption-watchdog-20260906-180725-dd8b55955c` passed one complete
resident lifecycle. KD admitted four processors with
`preemption-timer=0 preemption-rate=0`, recorded four identical UNAVAILABLE
markers, one BYPASSED marker for each diagnostic state 1 through 13, and four
BYPASSED state-0 markers for per-processor unload. It recorded no READY/ARMED,
timer-expiry, fatal-system-error, bugcheck, or fail-closed teardown marker.
The guest completed all seven diagnostics, unloaded canonically, confirmed
service absence, produced empty stderr, zero failure events, and no new dump.

Evidence archive SHA-256:
`2A196C6B08B5F33E06587AF68D18E5B2F7D8E368CAFBD06BF6EBF63E1F5E44C9`.
Closed KD log SHA-256:
`BBEC113AF5992118C5AC316C7C587922642B4C864652EDF65C727DA6B0F67758`.
The broker powered off and restored `Gate6-HostRunner-ReducedMemory-Clean`,
and all shared VM leases were released.
