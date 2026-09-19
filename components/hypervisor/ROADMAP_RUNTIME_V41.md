# Runtime V41 - Frozen EPT Storage Initialization

Status: accepted. The cumulative valid hardware route passed once; no
repetition run is required.

## Problem

EPT storage initialization validated caller-owned configuration and allocator
objects, but invoked allocation callbacks through the original allocator table
and later reused the original capability, identity, and population objects.
An injected allocator callback could therefore mutate those objects after
initial planning and redirect later allocation, unwind, or domain construction.
The fixed-size configuration objects were also checked only against the output
owner rather than against one another.

## Decision

Require complete pairwise separation of every fixed-size input and output
object before dereference. Snapshot capabilities, identity, population policy,
and allocator before the first callback, and use only those frozen values for
planning, allocation, physical-address discovery, unwind, domain construction,
and retained owner state.

Keep valid allocation sizes and order, physical-contiguity proof, identity-map
contents, failure mapping, public ABI, native adapter, and hardware behavior
unchanged.

## Checkpoints

1. **V41A - Initialization admission**
   - reject wrapping or pairwise-overlapping caller objects;
   - preserve the zero output owner and reach no allocator callback on reject.
2. **V41B - Frozen construction inputs**
   - snapshot all configuration objects and allocator callbacks;
   - route allocation, unwind, physical lookup, and domain construction through
     the frozen values.
3. **V41C - Static qualification**
   - mutate every caller input from the first allocation callback;
   - prove construction, authentication, and destruction use admitted values;
   - pass focused tests and a fresh complete WDK/MSVC/MASM build and CTest.

## Acceptance criteria

1. Invalid, wrapping, or overlapping caller objects preserve the output owner
   and reach no callback.
2. Callback-driven mutation cannot redirect later allocator operations or
   domain configuration.
3. Failure unwind and successful destroy use the admitted allocator snapshot.
4. Valid allocation order, page metadata, identity map, and lifecycle remain
   unchanged.
5. Public structures and production adapters remain unchanged.
6. Mutation-backed contracts and the complete static suite pass.

## Live-test decision

No new live gate is planned. Production construction inputs are independent
stack objects and the kernel allocator table is immutable. Runtime V41 changes
only malformed aliasing and post-entry mutation of injected caller storage;
Gate 69 already validated the unchanged EPT construction and lifecycle path.

## Static acceptance

- Fresh build directory:
  `build/codex-runtime-v41-20260907-021012`.
- Complete WDK/MSVC/MASM build: `457/457` targets built.
- Focused storage-owner and domain contracts: `3/3` passed.
- Complete CTest suite: `301/301` passed with zero failures.
- Required driver, loader, VMX launch, and VMX capture artifacts were present.
- `git diff --check` passed.

## Cumulative live acceptance

- Package:
  `gate69-vmcs-execution-context-bc2ef2f.zip`.
- Package SHA-256:
  `C37601965FDCF0ACC7ADF03F6441948366CACEED154EA0C452BBDBD593B29DBF`.
- RunId:
  `gate69-vmcs-execution-context-20260907-023242-2c6300a8bb`.
- Guest completed all seven diagnostics, authoritative status, canonical
  unload, and confirmed service removal with empty stderr.
- KD recorded four launch and eleven resume VMCS-binding attestations with no
  mismatch or fatal marker.
- Evidence archive SHA-256:
  `CCBA08B795C08899FE18B562251350B0A0B78C67A09A9EE9B32ED4C6BA023D3E`.
- Closed KD log SHA-256:
  `72C588F4F3BD2840BF87669B9A502BBC81C4F75085557DF624BA3990EB32E884`.
- The broker powered off and restored the clean snapshot; all VM leases are
  free.
