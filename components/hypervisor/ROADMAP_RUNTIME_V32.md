# Runtime V32 - Preflighted VMCS Plan Application

Status: statically accepted. No new live gate is planned.

## Problem

Runtime V31 makes every public plan builder transactional, but the reusable
application boundary still trusted any caller-provided `VMX_VMCS_PLAN`.
Malformed counts, a corrupted sentinel, zero field encodings, or duplicate
fields could therefore reach the writer. Because VMWRITE changes hardware
state immediately, discovering a software-plan defect after the first write
would leave a partially populated VMCS.

The adapter also propagated arbitrary writer return values even though its
public result contract defines only success, fail-valid, and fail-invalid
after an attempted write.

## Decision

Preflight the complete immutable plan before the first writer call. Require a
zero sentinel, at least one write, a bounded count, nonzero field encodings,
and unique fields. Reject malformed plans as fail-invalid without invoking
the writer or VM-instruction-error reader.

Normalize every writer result other than success or fail-valid to
fail-invalid while retaining the exact attempted field. Preserve the existing
fail-valid instruction-error read gate and the valid plan write order.

## Checkpoints

1. **V32A - Complete application preflight**
   - validate count and sentinel before indexing the plan;
   - validate every field and duplicate pair before the first write;
   - reject empty and malformed plans without hardware callbacks.
2. **V32B - Closed outcome domain**
   - retain success and fail-valid exactly;
   - normalize not-attempted and unknown writer results to fail-invalid;
   - retain the exact attempted field and suppress instruction-error reads.
3. **V32C - Static qualification**
   - cover every malformed-plan class with zero writer/read callback counts;
   - mutation-test preflight ordering and each validation class;
   - pass the complete WDK/MSVC/MASM build and CTest suite.

## Acceptance criteria

1. A malformed plan cannot invoke the writer or error reader.
2. Count bounds are proven before indexing any plan entry.
3. The sentinel is exactly zero and every applied field is nonzero and unique.
4. Valid writes retain their exact order and stop on the first hardware
   failure.
5. Writer outcomes exposed by the adapter remain inside the documented
   result domain.
6. Mutation-backed contracts and the complete static suite pass.

## Live-test decision

No new live gate is required. Runtime V32 does not alter any valid plan,
VMWRITE order, VMCS field, control, public ABI, or lifecycle path. Its new
behavior is limited to malformed software plans and unexpected injected
writer results, all of which are deterministically observable with callback
counters and mutation-backed unit tests. Gate 69 remains valid-path evidence
only and is not Runtime V32 acceptance evidence.

## Accepted verification

- Accepted implementation commit:
  `59a79b2abd8786378fbc7ff389202e6e7d5c0d39`.
- Fresh build directory:
  `build/codex-runtime-v32-20260906-203953`.
- Complete WDK/MSVC/MASM build: `457/457` targets built.
- Focused plan, transactional-builder, and apply-preflight tests: `3/3`
  passed.
- Complete CTest suite: `293/293` passed, with zero failures.
- Unit tests rejected zero, sentinel-only, over-capacity, corrupt-sentinel,
  zero-field, and duplicate-field plans without invoking either callback.
- Mutation-backed contracts rejected removal, inversion, or bypass of every
  preflight class and rejected widening of the writer outcome domain.

No live artifact was produced because the accepted valid hardware path is
unchanged.
