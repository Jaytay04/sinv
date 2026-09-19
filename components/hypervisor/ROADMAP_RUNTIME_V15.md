# Runtime V15 Roadmap - Immutable Protocol Negotiation

## Baseline and objective

- Accepted Runtime V14 source commit:
  `c67d87f056e8eefbb6d6d2a577e7b4d2b56935e1`
- Runtime V14 closure commit: `68edc3e`.
- Development branch: `feature/hypervisor-core`
- Objective: make successful control-session protocol negotiation a one-shot,
  immutable authorization transition.

Runtime V13 requires negotiation before operational dispatch and Runtime V14
enforces exact operational input framing. The negotiated file-object state was
still accepted as an input to another negotiation request. Runtime V15 closes
that ambiguous transition: once a session has negotiated protocol 1.0, every
later negotiation request is rejected before request-buffer retrieval, while
the established protocol authority remains valid for operational IOCTLs.

## Status at a glance

| Milestone | State | Exit boundary |
| --- | --- | --- |
| V15A Pure attempt policy | Complete | A dependency-free decision admits only state zero, rejects state one as repeated, and rejects every other byte as invalid state. |
| V15B KMDF enforcement | Complete | The adapter applies the attempt policy before framing or buffer retrieval and returns `STATUS_ACCESS_DENIED` for every noninitial attempt. |
| V15C Static authorization | Complete - GO | Source `6ac5144` plus Gate 53 authorization `68edc3e` passed a fresh `425/425` WDK/MSVC/MASM build, focused `8/8`, and full CTest `263/263`. |
| V15D Live immutable-session boundary | Complete - PASS | Gate 53 denied exact and incompatible negotiation replays with error 5, preserved the original negotiated authority, recovered exact observations, and unloaded canonically. |

## Design contract

- Preserve protocol ABI 1.0, IOCTL function `0x817`, all operational IOCTL
  values, and every existing request/response layout.
- Admit negotiation only when the per-file `ProtocolNegotiated` state is
  exactly false.
- Classify exactly true as a repeated attempt and every other byte value as an
  invalid state.
- Reject repeated or invalid-state attempts with `STATUS_ACCESS_DENIED` before
  checking lengths or retrieving either WDF buffer.
- Do not clear or replace a successful negotiation after a replay attempt.
- Preserve the accepted owner-process authorization before the negotiation
  state decision.
- Preserve Runtime V14 framing before operational dispatch.

## Gate 53 - immutable protocol negotiation

Gate 53 performs one Runtime lifecycle. A dedicated helper opens one canonical
read/write control session, negotiates exact protocol 1.0, then attempts both
an exact replay and an incompatible-version replay. Both must fail with Win32
`ERROR_ACCESS_DENIED` (5). An exact authoritative observation query on the same
handle must still succeed, proving the original authorization is immutable
rather than revoked or replaced.

After the helper closes, the normal loader must independently negotiate,
recover all four exact initial observation records, and unload canonically.
One run is sufficient because this is a deterministic per-file state-machine
boundary; the accepted lifecycle stress gates already cover repetition.

## Static authorization evidence

- Implementation commit: `6ac514420f7da312f2cdbc6036a65fdabcf70768`.
- Gate and contract authorization commit:
  `68edc3eb2f4d2097e9cb6ac6eeb837edbc6888a0`.
- Build directory: `build/codex-gate53-20260905-011339`.
- WDK/MSVC/MASM build completed all `425/425` targets.
- Focused protocol and VMX contracts passed `8/8`; full CTest passed
  `263/263`.
- The driver, loader, replay helper, PDBs, and all required MASM objects exist;
  `git diff --check` passed with line-ending warnings only.
- Signed-copy VM package SHA-256:
  `287648F91B32C1B7266AD7E71A266663E34C8B8152A9A30B4ACAE1ED831BABB4`.
- No driver was installed, loaded, or executed on the host.

## Live acceptance evidence

- Gate 53 passed once on 2026-09-05 from
  `Gate6-HostRunner-ReducedMemory-Clean` through the headless host runner.
- RunId:
  `gate53-control-session-protocol-replay-20260905-012106-9c3acc511d`.
- Pinned package SHA-256:
  `287648F91B32C1B7266AD7E71A266663E34C8B8152A9A30B4ACAE1ED831BABB4`.
- Evidence archive SHA-256:
  `F71F25AF564AC4E3B72AA9209BD6EF0771ECF919167B8010DB3DA7B802B19246`.
- Closed KD log SHA-256:
  `DB987376F728EC6AA27734D509ECF86574AB96DB49AC275BB76410557A380B85`.
- The exact and incompatible replays both returned error 5. The same session
  then returned the exact active generation-1 sequence-2 observation, and the
  independent loader recovered all four exact records.
- The driver signature was valid in the guest, stderr and fatal debugger
  matches were empty, canonical unload removed the service, and the VM was
  powered off and restored to the clean checkpoint.
- Runtime V15 is closed. Do not repeat Gate 53.

## Safety and stop conditions

- Never install, load, or live-test the driver on the host machine.
- Run Gate 53 only from the powered-off corrected disposable two-group/
  four-processor checkpoint through the host runner and headless KD.
- Stop on any signature, topology, negotiation, observation, unload, service,
  event, dump, hang, debugger, or host-resource mismatch.
- Do not retry a failed or uncertain live run. Preserve the VM and debugger
  state for investigation.
