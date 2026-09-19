# Runtime V42 - Frozen EPT Domain Initialization

Status: statically accepted. No new live gate is required.

## Problem

EPT domain initialization checked that the output domain did not overlap its
fixed-size inputs, but did not require those inputs to be mutually disjoint or
separate from every bound-storage span. It also validated and populated the
identity map through the original caller-owned configuration objects.

Malformed aliases could therefore couple logically independent configuration,
metadata, and page-table storage. A wrapping fixed-size span also needed to be
rejected before any caller object was dereferenced.

## Decision

Treat capabilities, identity, plan, population policy, storage binding, and
output domain as one fixed-size admission set. Require complete non-wrapping,
pairwise separation before dereference. Snapshot all five inputs, prove that
every original caller object is separate from the frozen binding's virtual and
metadata spans, and use only the snapshots for validation, EPTP construction,
identity-map population, and publication.

Keep valid identity-map bytes, EPTP encoding, generation state, public ABI, and
production ownership unchanged.

## Checkpoints

1. **V42A - Fixed-input admission**
   - reject wrapping or overlapping fixed-size caller spans before dereference;
   - preserve the output domain and table storage on reject.
2. **V42B - Storage-span admission**
   - reject configuration or binding objects that overlap virtual tables,
     ordinal metadata, or physical-index metadata;
   - retain the existing pairwise storage-span validation.
3. **V42C - Frozen construction**
   - snapshot every metadata input once;
   - route validation, EPTP construction, population, and publication through
     the snapshots.
4. **V42D - Static qualification**
   - add overlap, wrapping-pointer, output-preservation, and mutation-backed
     contract coverage;
   - pass focused tests and a fresh complete WDK/MSVC/MASM build and CTest.

## Acceptance criteria

1. Invalid fixed-size spans fail before caller-object dereference.
2. Every fixed-size input/output object and bound-storage span is independent.
3. Rejected admission preserves both the zero output domain and table bytes.
4. Valid construction uses one coherent snapshot set from validation through
   publication.
5. Valid table contents, EPTP, lifecycle state, public ABI, and production
   adapters remain unchanged.
6. Mutation-backed contracts and the complete static suite pass.

## Live-test decision

No new live gate is planned. The production storage owner already supplies
independent allocation and metadata spans, and Runtime V42 does not change
valid EPT bytes, EPTP fields, VMCS operations, or hardware sequencing. The
Runtime V41 cumulative Gate 69 already validated the unchanged valid route.

## Verification

Fresh WDK/MSVC/MASM verification in
`build/codex-runtime-v42-final-20260907-024800` built all `457/457` targets.
The focused EPT-domain, domain-contract, fresh-build-ordering, and persistent
ABI-mirror tests passed `4/4`; the complete CTest suite passed `302/302`.

The fresh configure also exposed and fixed a build-graph defect: tests were
previously registered before the driver enabled ASM_MASM, so the guarded
persistent ABI-mirror test was omitted on an initial configure and appeared
only after reconfiguration. Production targets now register before the test
graph, and a structural contract protects that ordering.
