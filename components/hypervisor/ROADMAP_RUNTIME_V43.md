# Runtime V43 - EPT Root Coordinator Admission Transaction

Status: statically accepted. No new live gate is required.

## Problem

Root-coordinator initialization checked output zero state and validated the EPT
domain before proving that the variable identity and record arrays and the
fixed domain and coordinator spans were non-wrapping and pairwise separate. It
also validated caller-owned identities and later reread them for publication.

A malformed wrapping pointer could therefore be dereferenced before admission,
and caller mutation could separate identity validation from the retained record
value.

## Decision

Treat the domain, identity array, record array, and coordinator as one complete
span-admission set. Calculate bounded array sizes, require every span to be
non-wrapping and pairwise disjoint before dereference, then validate zero state,
domain state, and domain-storage separation.

Use the required zero record array as transaction staging storage: snapshot each
identity once, validate that snapshot against its expected index and previously
staged identities, and publish the coordinator only after the complete record
set authenticates. Zero all records on any post-staging rejection.

## Acceptance criteria

1. Wrapping domain or identity-array spans fail before dereference.
2. Every domain, identity-array, record-array, and coordinator span is pairwise
   separate before output or domain inspection.
3. Identity validation and retained publication use the same per-record
   snapshot.
4. Every failure after staging restores the record array to zero and preserves
   the zero coordinator.
5. Valid first-enter/last-release state, ABI, and production hardware behavior
   remain unchanged.
6. Focused unit and mutation contracts and a fresh complete WDK/MSVC/MASM build
   and CTest pass.

## Live-test decision

No new live gate is planned. Runtime V43 changes malformed pointer, aliasing,
and concurrent caller-mutation behavior only. The valid root ownership state
machine, EPTP, VMCS operations, and hardware sequencing are unchanged, and the
Runtime V41 cumulative Gate 69 already exercised the retained valid route.

## Verification

Fresh WDK/MSVC/MASM verification in
`build/codex-runtime-v43-20260907-030409` built all `457/457` targets. The
focused root-coordinator unit and mutation contracts passed `2/2`, and the
complete CTest suite passed `302/302` with zero failures. Driver, loader, VMX
launch, and VMX capture artifacts were produced by the fresh build.
