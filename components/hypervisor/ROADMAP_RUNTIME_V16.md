# Runtime V16 - Exact Fixed-Response Framing

Status: accepted by Gate 54 on 2026-09-05 for source commit `db3f206`.

## Problem

Runtime V14 assigns an exact input length to every negotiated operational
IOCTL, but most fixed-width operations still accept oversized output buffers.
That leaves half of the METHOD_BUFFERED transport contract implicit and lets a
caller use non-canonical response framing even though the driver always emits
one fixed response shape.

The persistent and CR3 aggregate status queries are deliberately excluded:
their response lengths are topology-dependent and remain validated by their
typed serializers and handlers.

## Decision

Add one WDK-free response-frame policy covering every fixed-width operational
IOCTL. The policy returns one of four classifications:

- exact fixed-width response accepted;
- fixed-width response length mismatch;
- topology-dependent response delegated to its typed handler; or
- unknown/non-operational IOCTL delegated to normal dispatch.

Apply the policy after owner, negotiation, and exact request-frame checks but
before `IoctlMainDispatch`. A fixed-width mismatch completes with
`STATUS_INFO_LENGTH_MISMATCH`, zero information, and no handler invocation.
No IOCTL number, request layout, response layout, or protocol version changes.

## Acceptance criteria

1. A dependency-free table covers all 23 operational IOCTLs exactly once: 21
   fixed-width responses and the two topology-dependent aggregate responses.
2. Each fixed-width operation accepts only its exact response size and rejects
   both one-byte-short and one-byte-long buffers.
3. Persistent-status and CR3-status output lengths remain handler-owned; the
   new policy must not invent a fixed topology.
4. Protocol negotiation and unknown IOCTLs remain outside operational response
   framing.
5. The KMDF boundary rejects a fixed response mismatch before
   `IoctlMainDispatch` and returns `STATUS_INFO_LENGTH_MISMATCH` with zero
   information.
6. Mutation-backed contracts reject removal of the boundary, weakening exact
   equality, converting either dynamic response to fixed width, or omitting an
   operational IOCTL.
7. Gate 54 uses one negotiated session to deny short and oversized legacy
   STATUS and observation-status output buffers, accept both exact frames,
   recover the exact four-processor observation baseline from a fresh session,
   and unload canonically.

## Verification boundary

- Complete the host-only WDK/MSVC/MASM build and full CTest suite first.
- Run exactly one Gate 54 session in the pinned disposable two-group VM.
- Repetition is not required: this is a deterministic dispatch-boundary
  contract, while accepted lifecycle stress gates already cover repeated VMX
  startup and teardown.
- On any live failure, preserve the VM, KD process, and mailbox evidence. Do
  not retry or restore the snapshot until the failure is investigated.

## Accepted evidence

- Gate result: `PASS`
- Pinned package SHA-256:
  `E82CEBB050ABE119A28289BFE42BFDA8A9980739FFC569991FA61F045A3524A6`
- Evidence archive SHA-256:
  `D1302436A2B6167F8A98BF81A13092607004DFD05A7FA902B7E5D3BCBC92E716`
- Closed KD log SHA-256:
  `EAE48FEFAEA0A6A0CC5D2574ECE22468D2D13F790DE35219FDF970EEECAE3AC9`
- Exact fixed-response outcomes: short and oversized STATUS and observation
  buffers denied with error 24; exact 16-byte and 240-byte responses accepted.
- Fresh-session observation: four exact generation-1 sequence-2 records.
- Cleanup: canonical unload, service absent, empty stderr, clean failure-event
  captures, VM powered off, and clean snapshot restored.
