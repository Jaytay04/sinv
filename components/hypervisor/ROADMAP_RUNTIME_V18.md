# Runtime V18 - Explicit Process Target Rebinding

Status: complete. Gate 56 accepted on 2026-09-05.

## Problem

Runtime V17 can configure one referenced process target, but the public control
surface cannot release that target while the driver remains loaded and the VMX
runtime is detached. A configuration mistake or an exited target therefore
forces a full driver-service teardown before another target can be selected.

## Decision

Add one exact, versioned process-target release operation. Release is admitted
only while the persistent runtime is detached and only from CONFIGURED or
EXPIRED state. It drops exactly one process-object reference, securely clears
all target identity and CR3 metadata, and returns the existing redacted status
response in canonical EMPTY state.

The operation is deliberately not a replace shortcut. Operators must observe a
successful EMPTY response before configuring the next target. ACTIVE release
remains denied, and runtime teardown keeps its existing ownership path.

## Checkpoints

1. **V18A - Exact release ABI and policy**
   - fixed 16-byte request and unchanged 48-byte response;
   - exact version, size, flags, reserved, request, and response framing;
   - state policy preserves ACTIVE denial and sticky EXPIRED semantics.
2. **V18B - Detached release dispatch**
   - negotiated IOCTL dispatch checks detached phase before owner mutation;
   - CONFIGURED and EXPIRED release publish canonical EMPTY;
   - owner and teardown retain one shared release implementation.
3. **V18C - Loader rebind transaction**
   - configure the current process, release it, configure it again, and then
     enter the existing target-aware LOAD transaction on one device handle;
   - verify exact target identity and EMPTY response at each boundary.
4. **V18D - Gate 56 qualification**
   - prove CONFIGURED to EMPTY to CONFIGURED to ACTIVE to EXPIRED;
   - prove the active self-target CR3 boundary, fail-closed expiry behavior,
     canonical unload, service absence, and no new crash evidence.

## Acceptance criteria

1. Malformed, truncated, oversized, or nonzero-reserved release requests are
   rejected before target-owner mutation.
2. Release is denied unless the persistent runtime is exactly detached.
3. ACTIVE release cannot remove a live route or process reference.
4. CONFIGURED and EXPIRED release each dereference exactly once and leave the
   complete owner storage all-zero.
5. A successful response is exactly EMPTY with zero PID, start key,
   generation, flags, and reserved fields.
6. Reconfiguration is possible only after successful EMPTY publication.
7. The existing System default and V17 target-aware LOAD paths are unchanged.
8. Full WDK/MSVC/MASM build, mutation-backed contracts, and full CTest pass
   before live execution.

## Live-test boundary

Gate 56 is one deterministic disposable-VM run because the new behavior changes
kernel process-reference lifetime and public device mutation. Repetition is not
required: Gate 55 already proves process-exit invalidation, and the accepted
lifecycle stress gates cover unchanged VMX startup and teardown. Any true live
failure is preserved and is not automatically retried.

## Acceptance evidence

Gate 56 passed for implementation commit
`f0420a4e5a7126122a8546be21f22437e9abe797` with the fail-closed gate harness
at `bb8a7afcf96864d2ba1b039ccd8fd7ebd5d9b257`. The immutable signed package
SHA-256 is
`C844DF322FD1D8806277919DFAE51BC3025FB5309FE7284077EE3A6BB0380F19`.
The evidence archive SHA-256 is
`B662684C91F16C166246A9DC2C26AACD95B84513AC6793BA4ADA8764D139B86A`;
the closed KD log SHA-256 is
`35C89FD709CA1EFC25745D7082904B5AB6B35BB95BE3CCBE5D2580F5DA30B4CA`.

The accepted run proved detached CONFIGURED to canonical EMPTY release,
reconfiguration of the same retained process, ACTIVE publication, BUSY denial
of an active release, exact self-target residency `1/0/1`, sticky EXPIRED
identity after process termination, fail-closed NOT_ACTIVE routing, canonical
unload, service absence, empty failure events, and no guest stderr.

The first attempt did not reach the new release path because the PowerShell
poller called `Contains` before redirected stdout contained its first byte.
Commit `bb8a7af` normalizes the transient null read to an empty string and adds
a mutation-backed contract. The corrected run reused the unchanged signed
driver and loader binaries. Runtime V18 is closed; do not repeat Gate 56.
