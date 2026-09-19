# Runtime V38 - Frozen VPID Execution Boundaries

Status: accepted. Static qualification and cumulative valid-path regression
are complete; no repetition gate is planned.

## Problem

The pure VPID invalidation executor and VMCS coordinator validated injected
callback tables in caller-owned storage, then reread those tables across
multiple privileged-operation steps. Their fixed-size input and output objects
also lacked a complete pairwise overlap preflight. A malformed test/integration
caller could therefore alias publication with authenticated input or mutate an
admitted callback table between operations.

## Decision

Preflight every complete fixed-size span and require pairwise separation before
dereference or report publication. Copy capabilities, requests, descriptors,
and callback platforms into local snapshots, validate those snapshots, and use
only the frozen values for the admitted operation.

Keep valid VPID allocation, INVVPID, VMCS field order, rollback, public ABI,
and hardware sequencing unchanged.

## Checkpoints

1. **V38A - INVVPID executor admission**
   - reject wrapping or overlapping fixed-size inputs and report storage;
   - snapshot all inputs before validation and callback execution;
   - prove callback-table mutation cannot redirect later execution.
2. **V38B - VPID VMCS coordinator admission**
   - preflight activation and deactivation inputs before dereference;
   - snapshot capabilities and VMCS callbacks;
   - preserve activation rollback and ordered idempotent teardown.
3. **V38C - Static qualification**
   - add preservation, callback-poison, and mutation-backed contracts;
   - pass focused tests and the complete WDK/MSVC/MASM build and CTest suite.

## Acceptance criteria

1. Invalid, wrapping, or overlapping fixed-size inputs reach no callback and
   preserve caller-owned output.
2. Post-entry mutation cannot redirect INVVPID or VMCS operations.
3. Valid instruction result mapping, rollback, and teardown order are unchanged.
4. Public structures and production hardware adapters remain unchanged.
5. Mutation-backed contracts and the complete static suite pass.

## Live-test decision

No new live gate is required. Production VPID inputs use independent storage
and immutable callback tables. Runtime V38 changes malformed injected inputs
and post-entry mutation only; it does not change any valid VMCS value,
instruction, order, ABI, or hardware adapter.

## Accepted verification

- Accepted implementation commits: `a2a54e5`, `5fa33ea`
- Fresh build: `build/codex-runtime-v38-20260906-231906`
- Build targets: 457/457 passed
- Focused VPID tests and contracts: 4/4 passed
- Complete CTest suite: 299/299 passed
- Overlap and wrapping preservation, input snapshotting, callback-poison,
  activation rollback, and ordered teardown checks passed.
- The retained Runtime V4 lifecycle contract was updated to attest the frozen
  VMCS platform path and passed in the complete suite.
- Cumulative valid-path regression package SHA-256:
  `4003B60EAC58E1F11982512B7D6ED613AC278A97FE9F9EEFD6BC21C2BA2E602A`.
- Accepted RunId:
  `gate69-vmcs-execution-context-20260907-002536-0bffe261b6`.
- The guest completed all seven diagnostics under one resident lifecycle,
  reported 15 exact VMCS execution-context attestations, unloaded
  canonically, removed the service, emitted no stderr, and returned PASS.
- KD recorded four launch and eleven resume `VMCS_BINDING_ATTESTED` markers
  with no binding mismatch, fatal marker, or bugcheck.
- Evidence archive SHA-256:
  `31AE9EB8920750E03B18E0B3B06732917B8BA7E6AD050B8F6E61E14B9A674B96`.
- Closed KD log SHA-256:
  `235AB11A4B29D7B696290597FDB97D445DE7455DBF27340239BB15FF96AFDCFE`.
- The broker powered off the VM, restored
  `Gate6-HostRunner-ReducedMemory-Clean`, and the resource-scoped lease was
  released. No repetition run is required.
