# Runtime V35 - Disjoint Transient Execution Inputs

Status: statically accepted. No new live gate is planned.

## Problem

The retained transient launch-probe entry point mutates its launch context and
zeroes its optional report before using the injected callback table. It did
not reject exact or partial overlap among those caller-owned objects. A report
overlap could erase physical addresses or callbacks before validation, while a
launch-context overlap could corrupt callbacks during execution.

## Decision

Validate complete non-wrapping spans and require pairwise separation of the
platform, launch context, and optional report before dereferencing or mutating
any of them. Validate the complete callback table before initializing the
report so rejected platforms preserve every caller-owned byte.

Keep valid callbacks, VMCS fields, reports, lifecycle stages, public ABI, and
hardware sequencing unchanged.

## Checkpoints

1. **V35A - Entry-span preflight**
   - reject wrapping platform, launch-context, and report spans;
   - reject exact and partial overlap across every applicable pair;
   - run the preflight before callback validation and report initialization.
2. **V35B - Rejected-input preservation**
   - prove overlapping inputs remain byte-identical;
   - prove an invalid callback table preserves the optional report;
   - prove rejection invokes no platform callback or VMX operation.
3. **V35C - Static qualification**
   - mutation-test span arithmetic, pairwise separation, and ordering;
   - pass focused tests and the complete WDK/MSVC/MASM build and CTest suite.

## Acceptance criteria

1. No overlapping or wrapping input reaches a callback or mutation.
2. Rejected input bytes, including an optional report, are preserved.
3. Null reports remain supported on the valid path.
4. Valid output and hardware operation order remain unchanged.
5. Mutation-backed contracts and the complete static suite pass.

## Live-test decision

No new live gate is required. Runtime V35 changes only malformed caller
aliasing and invalid callback-table behavior in the retained transient
diagnostic path. Production supplies independent objects and the accepted
valid hardware path is unchanged. Unit byte-preservation and mutation-backed
ordering tests directly exercise the new boundary.

## Accepted verification

- Accepted implementation commit:
  `210a3a2feb0f3c8644473039a3b6d65512634a56`.
- Fresh build directory:
  `build/codex-runtime-v35-20260906-225225`.
- Complete WDK/MSVC/MASM build: `457/457` targets built.
- Focused VMCS-plan, transient, V34, and V35 tests: `4/4` passed.
- Complete CTest suite: `296/296` passed, with zero failures.
- Unit tests rejected exact and partial overlap across all three applicable
  input pairs while preserving every byte.
- Unit tests rejected wrapping platform, launch-context, and report spans
  without invoking a callback.
- An invalid callback table preserved the optional report.
- Mutation-backed contracts rejected weakened span arithmetic, removal of any
  pairwise separation, and bypass of preflight ordering.

No live artifact was produced because valid inputs and hardware sequencing are
unchanged.
