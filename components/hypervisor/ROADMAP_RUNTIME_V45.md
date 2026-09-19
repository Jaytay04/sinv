# Runtime V45 - Frozen EPT Hook-Owner Allocator

Status: statically accepted. No new live gate is required.

## Problem

Hook-owner initialization validated and copied the caller allocator, but invoked
slot allocation and failure cleanup through the original allocator object. An
injected allocation callback could mutate that object and redirect unwind.
Initialization also inspected the output and session before proving complete
fixed-object separation.

## Decision

Require non-wrapping pairwise separation of the session owner, allocator, and
hook-owner output before dereference, then prove the allocator and output are
also separate from the session's owned graph and table/metadata storage.
Snapshot the allocator once after fixed-span admission, validate the snapshot,
and use only it for allocation, failure cleanup, retained allocator contracts,
and later destruction.

Keep valid slot layout, hook transaction state, EPT mutation/invalidation
ordering, public ABI, and native behavior unchanged.

## Acceptance criteria

1. Session, allocator, and output spans are admitted before dereference, and
   external objects cannot overlap any session-owned storage.
2. Overlapping fixed objects fail without changing caller storage.
3. AllocatePool mutation cannot redirect cleanup or retained ownership.
4. Every post-allocation failure unwinds through the frozen allocator.
5. Valid hook lifecycle behavior and ABI remain unchanged.
6. Focused unit and mutation contracts and a fresh complete WDK/MSVC/MASM build
   and CTest pass.

## Live-test decision

No new live gate is planned. Runtime V45 affects rejected aliasing and injected
allocator-mutation behavior only. It does not change valid EPT bytes, VMCS
operations, ABI, or hardware sequencing; cumulative Gate 69 already validates
the retained valid path.

## Verification

Fresh WDK/MSVC/MASM verification in
`build/codex-runtime-v45-20260907-031908` built all `457/457` targets. The
focused hook-owner unit and mutation contracts passed `2/2`, and the complete
CTest suite passed `302/302` with zero failures. Required driver, loader, VMX
launch, and VMX capture artifacts were produced.
