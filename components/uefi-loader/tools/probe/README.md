# M0 Hard-Gate Probe — Procedure

This is the M0 empty-bank precondition probe: prove that PCR[0–7] are at reset
values (empty) and that the TBS event log is crypto-agile before any replay work
begins. It is a PRECURSOR — the formal `ib-tcg-dump probe` command lands in
task 1.6 (PR P3). Until then, the empirical check is performed manually and the
result recorded in `tools/probe/M0-VERDICT.md`.

## What the gate requires (from spec `boot-chain-recorder#Probe`)

1. TPM device present and version 2.0.
2. Active PCR banks enumerated.
3. PCR[0–7] read and compared to reset values.
4. TBS log header is `Spec ID Event03` (crypto-agile), NOT `Spec ID Event00`
   (SHA-1 / TCG 1.2 — a downgrade).
5. Verdict: empty-bank precondition MET or NOT MET (non-zero exit when NOT MET).

## Environment A — OVMF + swtpm (measurement disabled)

1. Start swtpm (see `tools/PROVISIONING.md` path A).
2. Boot the guest once and enter the OVMF firmware menu (F2).
3. In "Device Manager" → "TCG2 Configuration", disable measured boot:
   set TPM 2.0 UEFI Spec Version to TCG 1.2, or disable the active SHA-256 bank,
   so firmware extends nothing into PCR[0–7].
4. Boot Windows to the desktop.
5. Confirm TPM presence/version:
   `Get-Tpm` (PowerShell) or `wmic /namespace:\root\cimv2\security\microsofttpm path Win32_Tpm get SpecVersion,ManufacturerVersion /value`
6. Read PCR[0–7] and the TBS log header. Until `ib-tcg-dump probe` (P3) exists,
   use the smallest available TBS reader on the guest. Record the raw values and
   the `Spec ID Event03` magic into `M0-VERDICT.md`.
7. Expected: all PCR[0–7] = reset values; log header = `Spec ID Event03`.

## Environment B — VMware Workstation guest

1. Snapshot the VM.
2. Reconfigure the VM firmware: enable/trust TPM 2.0, then set TPM UEFI Spec
   Version to TCG 1.2 (or disable the SHA-256 bank) so the firmware leaves
   PCR[0–7] empty.
3. Boot Windows to the desktop.
4. Repeat steps 5–6 from Environment A.
5. Expected: identical reset-value PCR[0–7] and `Spec ID Event03` header. A
   default VMware measured boot (TCG 2.0) will show non-zero PCR[0–7] and
   verdict NOT MET — this is the negative control.

## Recording the verdict

Copy the evidence into `tools/probe/M0-VERDICT.md`. The gate is MET only when
BOTH environments show empty banks AND `Spec ID Event03`. Do not mark the gate
passed without real, recorded evidence.
