# Firmware comparison for the completed seven-file stage

The first retirement review for
`gpu-20261020T150318Z-59a346ed3cb8422bafe4be60268b7df1` stopped because its
current `bcdedit /enum firmware /v` text differed from the archived backup.
It removed no files, created no stage copies, and unmounted its owned ESP
mount successfully. No boot selection or firmware variables were written.

The saved texts differ only by three added firmware application blocks named
`UEFI:CD/DVD Drive`, `UEFI:Removable Device`, and `UEFI:Network Device`, and
their added firmware display-order references. The Windows Boot Manager and
both existing USB blocks are unchanged. The captures do not establish what
created the entries. Their descriptions alone do not prove that retiring the
stage is safe.

## Bounded comparison update

The retirement script retains the original backup and accepts its exact text.
If that differs from the live capture, it can instead use one fixed later
capture, verified by its path, SHA-256, and byte length. Live text must match
that reviewed capture exactly. It does not normalize enumeration output,
ignore arbitrary entries, change firmware, or overwrite the old journal.

The selected text becomes the fixed baseline for the rest of that action.
Every later text comparison must match it. Raw BootNext must still be absent;
the pinned audit tool must successfully inspect BootOrder and referenced
entries with zero findings, including no staged boot target. Raw boot order
and integrity must remain stable. The same-machine, reset, Secure Boot,
ABSENT health, seven-file ownership, verified local/external copies, removal
checkpoints, and owned-unmount checks remain required.

The archived backup has no raw pre-stage BootOrder capture. The first live
retirement capture establishes a baseline for stability during that action;
it does not prove raw BootOrder still equals its pre-stage value. The saved
text comparison is not a raw firmware-variable attestation.

When the later capture is selected, reports and intents identify
`FirmwareBaselineSource=REVIEWED_LATER_CAPTURE`, preserve the original pin,
record `OriginalFirmwareBaselineEqual=false`, and include the reviewed capture
pin. `FirmwareBaselineEqual` describes the selected baseline after the initial
health and raw boot checks pass.

This comparison update is prepared from saved evidence. The failed review
stopped before health and raw boot-selection capture; those live checks must
still pass in a new default review before Apply. No live retirement or ESP
file removal was performed while preparing the change.

The focused mocked suite passes 26 cases and 111 assertions. It covers both
baseline choices, missing or tampered evidence, unexpected live text, a return
to the old text before deletion, and continued health/raw-boot failure gates.

## Preserved evidence

Paths are relative to the main workspace.

- Failed review: `.analysis/gpu-stage-retirement/0851536237dd4f6a9ff5df72b0c36848/`.
- Saved comparison receipt: `.analysis/gpu-stage-retirement-150318-preparation/firmware-delta-review.json`.

| Evidence | Bytes | SHA-256 |
| --- | ---: | --- |
| Original firmware text | 2,890 | `66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007` |
| Reviewed later text | 4,336 | `ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE` |
| Failed review result | 7,731 | `D25D98ACA94E4390FDE57A48AFD0E8040F6B9AF9C920436C1A60ED4E0426079D` |

Use the default review and conditional Apply sequence in the
[bare-metal runbook](GPU_BAREMETAL_MANUAL_RUNBOOK.md). A successful retirement
still requires a fresh readiness result before Prepare.
