# Runtime V17 - Referenced Process Target Lifecycle

Status: complete. Gate 55 accepted on 2026-09-05.

## Problem

The CR3 route is permanently bound to the System process. That proves the
mechanics of CR3 observation, but it cannot identify a caller-selected process
and it gives the runtime no durable identity with which to reject PID reuse or
process exit.

## Decision

Add an explicit, versioned process-target control surface and a device-owned
target lifecycle. A configuration request names a PID, but the kernel resolves
that PID to a referenced `PEPROCESS`, records its process start key, and captures
the CR3 root through the supported process-attach boundary. Raw process
pointers and CR3 values never cross the public ABI.

The target is configured only while the persistent runtime is detached. LOAD
activates the configured target; UNLOAD deactivates it before the reference is
released. Process exit invalidates a guarded route before the referenced object
can be reused. Reconfiguration while VMX is active is denied.

The existing System target remains the default when no explicit target was
configured. This preserves every accepted gate while making target selection
an additive capability.

## Checkpoints

1. **V17A - ABI and state policy**
   - fixed-width configure/query messages;
   - WDK-free request validation and lifecycle transitions;
   - C and C++ layout tests.
2. **V17B - Referenced target owner**
   - PID lookup, `PEPROCESS` reference ownership, start-key identity, and
     generic CR3 capture;
   - exact exit detection and idempotent release without raw-value publication.
3. **V17C - Guarded CR3 route integration**
   - stable device-owned route guard;
   - process-exit invalidation visible to every route plan/commit;
   - bind, rollback, teardown, and driver-unload ownership.
4. **V17D - Control and loader integration**
   - negotiated exact-frame configure/query IOCTLs;
   - one loader transaction that configures a target and loads the runtime;
   - redacted status and deterministic error mapping.
5. **V17E - Gate 55 qualification**
   - configure a long-lived guest process, load, prove its CR3 boundary,
     observe active redacted identity, expire the process, prove fail-closed
     route status, and unload canonically.

## Acceptance criteria

1. PID zero, malformed versions/sizes/flags, truncated frames, and oversized
   frames are rejected before process lookup.
2. A configured target owns exactly one process-object reference and records a
   nonzero start key plus nonzero target generation.
3. PID reuse cannot retarget an existing runtime because identity is the
   referenced process object and start key, not the numeric PID alone.
4. LOAD consumes either the explicit configured target or the unchanged System
   default. Reconfiguration is denied unless the runtime is detached.
5. Process exit invalidates the CR3 route without blocking in the process-notify
   callback. A route cannot publish a new target-resident state after expiry.
6. UNLOAD and driver unload release the process reference only after CR3 route
   teardown. Rollback leaves one diagnosable, retryable state.
7. Public status exposes only state, PID, start key, generation, and flags; no
   kernel pointer or CR3 root is returned.
8. Full WDK/MSVC/MASM build, mutation-backed contracts, and full CTest pass
   before live execution.

## Live-test boundary

Gate 55 is one deterministic disposable-VM run. Ten- and one-hundred-cycle
stress are not required initially because the existing accepted lifecycle
stress gates already cover unchanged VMX startup/teardown. Any failure preserves
the VM, debugger, and evidence without an automatic retry.

## Acceptance evidence

Gate 55 passed for implementation source commit
`484e32a98b5c075a9949c13c9b7b9522de40ca6b`. The exact signed package
SHA-256 is
`CC4BCD04D8150476456645C865C1E516B465282B153F7D580DFB754D4FD4769D`.
The evidence archive SHA-256 is
`BC6011360050E88596306CA76FDA91365DCF7C887E45C9F10B521433FD559676`;
the closed KD log SHA-256 is
`0866629C8AF18A2306E0D62E35C6C66A2DA29A62139C5CC153B7052DA3EF26E8`.

The accepted run proved the exact CONFIGURED to ACTIVE to EXPIRED lifecycle,
retained PID/start-key/generation identity, self-target residency `1/0/1`,
route-active flag removal, fail-closed public NOT_ACTIVE behavior after process
exit, canonical unload, service absence, clean failure events, and no new dump.
Runtime V17 is closed; do not repeat Gate 55.
