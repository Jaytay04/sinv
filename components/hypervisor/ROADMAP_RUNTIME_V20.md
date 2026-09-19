# Runtime V20 - Session-Owned Process Target Configuration

Status: complete. Gate 58 accepted on 2026-09-05.

## Problem

The Runtime V17 configure operation accepts a numeric PID and then resolves it
to a referenced process. The retained process object and start key prevent PID
reuse from retargeting an established route, but a self-targeting client still
has to publish a reusable numeric identifier even though the authenticated
control session already owns a referenced process identity.

## Decision

Add one optional configure-self operation with an exact empty request. It
derives the target from the authenticated file object's referenced owner
process, takes a separate target-lifetime reference, captures the CR3 root, and
publishes the existing redacted CONFIGURED response. No PID or process handle
is accepted from user mode.

The existing arbitrary-PID configure operation remains ABI-compatible for
administrative targeting. The new operation is the preferred self-target path
because it binds authorization and target selection to the same stable process
object.

## Checkpoints

1. **V20A - Exact configure-self ABI and framing**
   - fixed 16-byte request with exact version, size, flags, and reserved fields;
   - additive IOCTL and unchanged 48-byte response;
   - request/response inventory and malformed-frame coverage.
2. **V20B - Referenced session-owner transfer**
   - resolve the file context only after the common owner/protocol boundary;
   - take an independent target reference from the session owner;
   - share one capture/publication path with the existing PID operation.
3. **V20C - Loader proof transaction**
   - configure from the current negotiated session without sending a PID;
   - verify the returned PID is the current process and identity is nonzero;
   - load, prove ACTIVE self residency, expiry, and fail-closed routing.
4. **V20D - Gate 58 qualification**
   - prove session-owned CONFIGURED to ACTIVE to EXPIRED lifecycle;
   - prove canonical unload, service absence, and clean failure evidence.

## Acceptance criteria

1. Malformed, short, oversized, or nonzero-reserved requests are rejected
   before target-owner mutation.
2. Configure-self is admitted only for a negotiated, owner-authorized session
   while the persistent runtime is detached.
3. The request contains no caller-selected PID, handle, pointer, or CR3 value.
4. The target owner holds a reference independent of the file-context
   reference and releases it exactly once through the existing lifecycle.
5. Failed configuration neither consumes a binding generation nor changes the
   canonical-empty owner.
6. Existing configure-by-PID, release, matched release, and System/default
   behavior remain unchanged.
7. Full WDK/MSVC/MASM build, mutation-backed contracts, and full CTest pass
   before live execution.

## Live-test boundary

Gate 58 is one deterministic disposable-VM run. It is justified because the
new operation changes the kernel process-reference acquisition path and must
prove that session authorization and target identity refer to the same live
process. Repetition is not required: Gates 55 through 57 qualify the unchanged
target activation, expiry, release, and teardown machinery. A true live failure
is preserved and is not automatically retried.

## Acceptance evidence

Gate 58 passed for implementation commit
`c861d053645168361be28c749af8016feea67dca`. The immutable signed package
SHA-256 is
`9DEF0816549C71D1A5B385BCFC4EAC3E48A452E423EDCD8192E734F2E001182B`.
The evidence archive SHA-256 is
`7D1D0CD58E407A15C2A548C9E3162F38B349B5C977780C3D875D23EB8C60A44C`;
the closed KD log SHA-256 is
`4757E16C0C99E6D1E0DE08AB7014C6127D6752C8B4B94C4D0F8ABBE213158AF8`.

The accepted run proved configuration from the authenticated session owner
without caller-selected PID input, exact returned PID equality, ACTIVE
self-target residency `1/0/1`, sticky EXPIRED identity, fail-closed NOT_ACTIVE
routing after owner exit, canonical unload, service absence, empty failure
events, and empty target-holder and unload stderr. The broker then powered off
the disposable VM and restored the clean snapshot. Runtime V20 is closed; do
not repeat Gate 58.
