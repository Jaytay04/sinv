# Runtime V19 - Generation-Matched Process Target Release

Status: complete. Gate 57 accepted on 2026-09-05.

## Problem

Runtime V18 intentionally exposes an unconditional detached release operation.
That is sufficient for an interactive operator, but a delayed command cannot
distinguish the target it originally observed from a later target installed on
the same device. The sequential WDF queue prevents simultaneous dispatch; it
does not give asynchronous clients compare-and-release semantics.

## Decision

Add one optional, additive generation-matched release operation. Its exact
request carries the observed PID, process start key, and target generation.
The owner releases only when all three values still identify the current
CONFIGURED or EXPIRED target. A mismatch returns a redacted STALE result and
does not change state, identity, CR3 metadata, or reference ownership.

Target generation is a device-lifetime binding epoch, not the process start
key. It increments only after a successful configuration and is retained
outside the canonical-empty owner. Releasing and rebinding even the same
process therefore produces a different generation and closes the same-process
ABA case.

The Runtime V18 unconditional operation remains ABI-compatible. Clients that
need stale-command protection use the matched operation; this roadmap does not
misrepresent the legacy operation as generation guarded.

## Checkpoints

1. **V19A - Exact matched-release ABI and policy**
   - fixed 40-byte request and unchanged 48-byte response;
   - exact version, size, flags, reserved, and nonzero identity validation;
   - additive IOCTL with exact request/response framing.
2. **V19B - Compare-and-release ownership**
   - publish a monotonic device-lifetime generation for each successful bind;
   - compare PID, start key, and generation before the state transition;
   - retry only the CONFIGURED-to-EXPIRED race;
   - mismatch preserves the complete owner and reference.
3. **V19C - Loader proof transaction**
   - configure self and capture the returned identity;
   - prove a stale generation is denied and the same CONFIGURED identity remains;
   - release with the exact identity, observe canonical EMPTY, and reconfigure;
   - prove matched release is still denied while ACTIVE.
4. **V19D - Gate 57 qualification**
   - prove non-mutating STALE denial, exact matched release, rebind, ACTIVE
     denial, target expiry, fail-closed routing, and canonical teardown.

## Acceptance criteria

1. Malformed, short, oversized, zero-identity, or nonzero-reserved requests are
   rejected before owner mutation.
2. Matched release is admitted only while the persistent runtime is detached.
3. Any PID, start-key, or generation mismatch returns STALE and leaves the
   exact prior status unchanged.
4. An exact CONFIGURED or EXPIRED identity dereferences once and publishes the
   complete canonical EMPTY response.
5. ACTIVE matched release returns BUSY without changing target or route state.
6. The Runtime V18 release ABI and existing System/default behavior remain
   unchanged.
7. Full WDK/MSVC/MASM build, mutation-backed contracts, and full CTest pass
   before live execution.

## Live-test boundary

Gate 57 is one deterministic disposable-VM run. It is justified because the
new public mutation changes the kernel process-reference release predicate and
must prove that a stale identity cannot release a current target. Repetition is
not required: Gate 56 already qualifies the underlying release/rebind lifecycle
and accepted stress gates cover unchanged VMX startup and teardown. A true live
failure is preserved and is not automatically retried.

## Acceptance evidence

Gate 57 passed for implementation commit
`ace5b15e950f61fb7ab6dc2dbefb96842d2c0231`. The immutable signed package
SHA-256 is
`0312C3C1C5FF04BC298D565C62774822139CD03CF6B8CE01837E5A9C4A988DB7`.
The evidence archive SHA-256 is
`FA6978BE8DDB1D285463F8C594BFA6DF203429A10F2590F613C89652A4562F62`;
the closed KD log SHA-256 is
`6CBFCADF4AE0B5FF0280320CA9F06E4C38FD362CC81F1ECCD40BCF29B3765AEF`.

The accepted run proved non-mutating STALE denial for generation 1, exact
matched release to canonical EMPTY, same-process rebind at generation 2,
ACTIVE BUSY denial, self-target residency `1/0/1`, sticky EXPIRED identity,
fail-closed NOT_ACTIVE routing, canonical unload, service absence, empty
failure events, and empty target-holder and unload stderr. The broker then
powered off the disposable VM and restored the clean snapshot. Runtime V19 is
closed; do not repeat Gate 57.
