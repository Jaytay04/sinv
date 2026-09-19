# Runtime V21 - Authenticated Control Capability Manifest

Status: complete. Gate 59 accepted on 2026-09-05.

## Problem

Protocol negotiation proves only the fixed control ABI version. It does not
let a loader distinguish an older driver that implements protocol `1.0` but
lacks later additive operations. A new loader can therefore discover a stale
driver only after issuing an unsupported operational IOCTL.

## Decision

Add one authenticated, read-only capability query after protocol negotiation.
Its exact response publishes a fixed bitmap of implemented control-surface
families and the exact operational IOCTL inventory count. The bitmap describes
ABI support, not current hardware availability or runtime readiness.

The loader queries and verifies the exact manifest immediately after every
successful negotiation. A mismatch closes the device and fails before any
runtime mutation. Existing IOCTL values and the protocol `1.0` wire layout
remain unchanged.

## Checkpoints

1. **V21A - Exact capability ABI**
   - additive function `0x81D`;
   - exact 16-byte request and 32-byte response;
   - ten explicit control-family bits and exact operational count.
2. **V21B - Read-only driver publication**
   - authenticate owner and protocol through the existing common boundary;
   - validate exact request and response framing;
   - return only compile-time public capability metadata.
3. **V21C - Loader fail-closed handshake**
   - query immediately after every successful protocol negotiation;
   - require exact version, size, status, flags, bitmap, count, and reserved
     fields before returning an open device;
   - close the handle on any mismatch.
4. **V21D - Gate 59 qualification**
   - prove the exact manifest on independent LOAD, status, and UNLOAD sessions;
   - preserve canonical lifecycle, service absence, clean events, and no dump.

## Acceptance criteria

1. Malformed, short, oversized, or nonzero-reserved requests are rejected
   before publication.
2. Unnegotiated or non-owner access remains denied by the existing boundary.
3. The response contains no pointers, addresses, process identity, CR3 value,
   hardware-derived state, or mutable runtime state.
4. The loader fails closed before operational mutation if the manifest is
   absent, malformed, incomplete, or unexpected.
5. The bitmap and operational IOCTL count are mutation-backed against every
   public additive control family.
6. Existing protocol negotiation and operational wire layouts remain
   unchanged.
7. A fresh WDK/MSVC/MASM build and full CTest pass before live execution.

## Live-test boundary

Gate 59 is one deterministic disposable-VM lifecycle. It is necessary because
the new handshake runs on every loader device open and now gates LOAD, status,
and UNLOAD. The gate proves three independent negotiated sessions receive the
same exact manifest without changing the accepted VMX lifecycle. Repetition is
not required. A true live failure is preserved and is not automatically
retried.

## Acceptance evidence

Gate 59 passed for implementation commit
`1545845c583ef76a9874a247b4e259cce66ae5fd`. The immutable signed package
SHA-256 is
`C105CBC196A8B486332D696394C8783863072526061C71936486FBD8D3FA196C`.
The evidence archive SHA-256 is
`3F8FBE4973EDF289A270E820A074536910280F3A45FAD69F56ABB3DCC57FA297`;
the closed KD log SHA-256 is
`B507BB62049BBCC591865D7119B0B3297FA920EC452E185DB38C99286A6985B7`.

The accepted run proved the exact version-1 bitmap
`0x00000000000003FF` and 29-operation inventory on independent LOAD, status,
and UNLOAD sessions. Persistent/EPT readiness, authoritative status, canonical
unload, service absence, empty loader stderr, and empty failure events remained
intact. The broker powered off the disposable VM and restored the clean
snapshot. Runtime V21 is closed; do not repeat Gate 59.
