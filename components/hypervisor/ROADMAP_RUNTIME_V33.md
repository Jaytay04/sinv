# Runtime V33 - Disjoint VMCS Plan Publication

Status: statically accepted. No new live gate is planned.

## Problem

The persistent VMCS-plan builder stages the complete result locally and
publishes it only after validation, but it did not reject an output span that
overlapped the persistent context, launch context, or control plan. A caller
could therefore receive a successful result while publication corrupted one
of the authenticated inputs used to construct the plan.

Exact-pointer checks would not cover partial overlap, and checking after
publication would be too late to preserve the input state.

## Decision

Validate every fixed-size input/output span before dereferencing the build
inputs. Require the output plan to be disjoint from the persistent context,
launch context, and controls across their complete byte ranges. Reject exact
and partial overlap before construction and preserve every input byte.

Keep the local staging and single final publication model. Valid plan fields,
counts, order, values, public ABI, and hardware sequencing remain unchanged.

## Checkpoints

1. **V33A - Span and overlap preflight**
   - validate non-null, non-wrapping fixed-size spans;
   - reject exact and partial output overlap with each authenticated input;
   - run preflight before any input dereference or plan construction.
2. **V33B - Preservation proof**
   - retain local construction and one final output assignment;
   - prove rejected aliases preserve the complete fixture and launch context;
   - mutation-test removal or narrowing of every separation check.
3. **V33C - Static qualification**
   - pass focused unit and mutation contracts;
   - pass the complete WDK/MSVC/MASM build and CTest suite.

## Acceptance criteria

1. Invalid or wrapping fixed-size spans are rejected before input use.
2. Output overlap with context, launch, or controls is rejected across the
   complete byte range.
3. Rejected exact and partial aliases preserve all caller-owned bytes.
4. Valid plans retain their exact accepted representation and publication.
5. Mutation-backed contracts and the complete static suite pass.

## Live-test decision

No live gate is required. Runtime V33 changes only invalid caller aliasing at
a pure construction boundary. Production already supplies independent local
storage, so valid VMCS data and every hardware operation remain unchanged.
The overlap classes are deterministically covered by unit and mutation tests.

## Accepted verification

- Accepted implementation commit:
  `949bda56e4cf50aaf9b2da400e3e51e5fbb6de85`.
- Fresh build directory:
  `build/codex-runtime-v33-20260906-205050`.
- Complete WDK/MSVC/MASM build: `457/457` targets built.
- Focused persistent-plan, V31, V32, and V33 tests: `4/4` passed.
- Complete CTest suite: `294/294` passed, with zero failures.
- Unit tests rejected exact and partial output overlap with context, launch,
  and controls while preserving every caller-owned byte.
- Unit tests rejected wrapping context, launch, controls, and output spans
  before dereference or publication.
- Mutation-backed contracts rejected span-overflow bypass and removal of each
  output separation check.

No live artifact was produced because valid publication and hardware behavior
are unchanged.
