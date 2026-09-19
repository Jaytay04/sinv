# Runtime V44 - Frozen EPT Session Configuration

Status: statically accepted. No new live gate is required.

## Problem

Session configuration build and validation proved fixed input/output span
separation but repeatedly read caller-owned objects while deriving capabilities,
address bounds, mapping policy, and the expected configuration. A caller change
during the operation could therefore separate admission from later derivation
or exact comparison.

## Decision

After non-wrapping input/output separation, copy the build input once and use
only that snapshot for all policy derivation. Validation snapshots both the
input and supplied configuration, builds the expected value from the frozen
input, and compares only the frozen configuration.

Keep the public ABI, conservative UC/RWX 2 MiB mapping policy, capability
requirements, address bounds, and output failure-atomicity unchanged.

## Acceptance criteria

1. Build snapshots the admitted input before inspecting any field.
2. Every build decision uses the same frozen input.
3. Validation snapshots both admitted objects before construction or
   comparison.
4. Rejected build and validation preserve caller-owned output bytes.
5. Valid configuration bytes and public ABI remain unchanged.
6. Focused mutation contracts and a fresh complete WDK/MSVC/MASM build and
   CTest pass.

## Live-test decision

No new live gate is planned. Runtime V44 changes only caller-mutation behavior
inside a WDK-free policy helper. It does not change valid EPT bytes, VMCS
operations, ABI, or hardware sequencing; Runtime V41 cumulative Gate 69 already
validated the retained valid path.

## Verification

Fresh WDK/MSVC/MASM verification in
`build/codex-runtime-v44-20260907-031113` built all `457/457` targets. The
focused session-config unit, mutation, and adapter contracts passed `3/3`, and
the complete CTest suite passed `302/302` with zero failures. Required driver,
loader, VMX launch, and VMX capture artifacts were produced.
