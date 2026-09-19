# Runtime V27 - Diagnostic Target Ownership Boundary

Status: accepted. Gate 66 passed on the corrected implementation.

## Problem

The persistent runtime originally published caller-owned diagnostic buffers
while the processor crossed into non-root mode and back. Runtime V27 moved
borrowed diagnostic payloads into launch-context-owned storage. Gate 66 then
exposed one important exception: the EPT trigger byte must remain the exact
driver lifecycle page that is hooked in EPT. Copying that byte into launch
storage makes the guest write miss the hook entirely.

## Decision

Use two explicit authenticated target modes. Borrowed generic and structured
diagnostic payloads use the fixed 80-byte launch-context buffer: activation
copies in before VM entry, validates completion against the owned copy, copies
back only after leaving instruction level, and scrubs the storage on every
terminal path. The EPT lifecycle diagnostic alone publishes its exact retained
driver page using `DRIVER_LIFECYCLE_PAGE`; it performs no payload copy and is
admitted only by the dedicated EPT path.

The private C/MASM ABI is version 4. The former reserved fields at offsets 920
and 924 authenticate the target mode and its complement; the existing 80-byte
storage remains append-only at offset 928. Operation-specific payload
validation and routing remain explicit.

## Checkpoints

1. **V27A - Append-only private ABI**
   - add an 80-byte owned payload at the end of the launch context;
   - advance the private ABI version and C/MASM size contract;
   - prove existing offsets remain unchanged and the context stays within one
     page.
2. **V27B - Owned activation transaction**
   - validate the external request before pinning;
   - require owned storage to be zero before arming;
   - copy in, publish only the owned address, and execute the diagnostic;
   - validate and copy out only after successful completion;
   - clear the pointer, state, and owned bytes on every terminal path.
3. **V27C - Exact EPT target exception**
   - authenticate a dedicated direct-target mode and complement;
   - admit it only for the one-byte EPT lifecycle diagnostic;
   - publish the exact retained driver lifecycle page without copy-in/out;
   - keep all generic and structured diagnostics on owned storage.
4. **V27D - Mutation-backed verification**
   - reject direct publication of the external pointer;
   - reject missing copy-in, copy-out, exact-owned-pointer, and zeroization
     checks;
   - reject a copied EPT target, a generic EPT owner call, or a weakened mode
     contract;
   - cover success, retry, validation failure, and assembly teardown paths.
5. **V27E - Gate 66**
   - run all seven diagnostics once in one resident lifecycle;
   - prove structured payloads use launch ownership and EPT uses the exact
     driver lifecycle page;
   - unload canonically and preserve negative crash/event evidence.

## Acceptance criteria

1. No borrowed caller-owned diagnostic pointer is retained in persistent
   launch state.
2. Generic and structured diagnostics receive the exact owned storage address;
   the EPT diagnostic receives only the exact retained driver lifecycle page.
3. Failed diagnostics never copy potentially partial results to the caller.
4. Owned bytes and diagnostic pointer/state fields are zero after every
   terminal path.
5. The private ABI remains append-only, 16-byte aligned, below one page, and
   authenticates the target mode with a complement.
6. A fresh WDK/MSVC/MASM build and the complete CTest suite pass.
7. Gate 66 proves all seven operations in one composite lifecycle.

## Live-test boundary

Gate 66 is necessary because the correction changes the exact address consumed
by the EPT guest write while preserving owned storage for every borrowed
payload. Static tests can prove routing, copy ordering, target mode, and pointer
contracts, but not that the hardware write reaches the hooked driver page.
One composite run is proportionate. No 10/100-cycle gate is planned unless
evidence identifies an intermittent defect.

## Accepted live evidence

Gate 66 passed once on commit
`6c126f662ce34f15158e0055aed3772b18b7a846` using RunId
`gate66-diagnostic-payload-ownership-20260906-160136-605f3c5294`.
The gate completed all seven authenticated diagnostics in one resident
lifecycle, reported
`StructuredPayloadOwner=PERSISTENT_LAUNCH_CONTEXT` and
`EptTargetOwner=DRIVER_LIFECYCLE_PAGE`, unloaded canonically, and confirmed
service absence with SCM result 1060. Guest stderr was empty and the closed KD
log contained no fatal-system-error or bugcheck marker.

Signed package SHA-256:
`5B3595F6997D38A1CD3155C057BA6DD20B8596312EA6E6505BE647B40EFBAE11`.
Evidence archive SHA-256:
`21A83B6C410E04EC3632F4AE6A90E611ACFF46197FB9856D471133A0E6F94C9C`.
Closed KD log SHA-256:
`2642FA6EC4E63C81617C8D51DADB37A3E79A6E9FD6178D8C5D6EAF6337F4716F`.

The broker powered off and restored the VM to
`Gate6-HostRunner-ReducedMemory-Clean`, and both Hypervisor leases were
released. No repetition gate is required for this deterministic address-bound
correction.
