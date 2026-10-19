# Path B: Secure Boot shim/MOK inventory test

Keep firmware Secure Boot enabled for this path. It uses the vendored Microsoft-signed shim, the production loader, and a collector signed by the existing project certificate. The previous direct unsigned inventory-loader procedure is archived in [EFI_INVENTORY_PATH_B_UNSIGNED.md](EFI_INVENTORY_PATH_B_UNSIGNED.md).

## What this tests

```text
UEFI firmware (Secure Boot ON)
  -> EFI\HypersinvShimInventory\shimx64.efi (Microsoft-signed Ubuntu shim)
  -> grubx64.efi (signed copy of the actual production loader.efi)
  -> inventory.efi (same MOK signer, verified through shim)
  -> EFI\Microsoft\Boot\bootmgfw.efi
```

The production loader recognizes its exact diagnostic directory before entering its normal replay code. This branch has fixed child paths and never enters TPM replay, Windows Boot Manager restoration, generic payload execution, or VMX. It uses the same child verification and LoadImage/StartImage code used by the production loader. The collector reads firmware variables and writes a report. Shim itself performs normal measured-boot operations and can update its shim/SBAT variables; MOK enrollment adds persistent trust.

A successful capture proves this signed loader/collector chain and Windows handoff on this machine. It does not prove the hypervisor binary starts, that VMX works, or that TPM replay works. Hypervisor execution remains a disposable-VM activity under AGENTS.md.

## Before the first boot

Have your Windows recovery USB, full backup, and the 48-digit BitLocker recovery key available. A different measured boot path can trigger recovery even with Secure Boot enabled. Do not publish the recovery key or include it in logs. Remove the previously armed collector USB (G:) so it cannot select the unsigned Path A by accident. If you previously armed the old unsigned ESP profile, run `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\path-b-collector.ps1 Disarm` from elevated PowerShell before arming this new profile. The signed profile refuses to replace an unrelated existing BootNext.

The firmware must trust the Microsoft third-party UEFI CA used by this shim, and its dbx/SBAT policy must permit this version. Some firmware has a separate third-party UEFI CA toggle. If the firmware rejects the shim, return to Windows and investigate the trust/revocation state; do not clear Secure Boot keys, reset SBAT revocations, or disable shim validation to force a pass.

The vendored artifact is Ubuntu noble shim 15.8, SHA-256:

```text
6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B
```

It was signed under Microsoft UEFI CA 2011. Certificate expiry by itself does not revoke an existing shim; acceptance depends on the actual firmware trust database and revocations. See [Microsoft transition guidance](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/linux-distribution-guidance-handling-2023keys?view=windows-11).

## Build and sign

Run from the repository root in normal PowerShell. Requires Visual Studio x64 C++ tools, Windows SDK signtool, CMake 3.24+, Ninja, and tar.exe. The existing project signing key must be available in the current user certificate store; this workflow does not export it or import certificates into Windows trust stores.

```powershell
Set-Location C:\Users\jayli\OneDrive\Documents\Projects\hsinv
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\shim-chain-collector.ps1 Build
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\shim-chain-collector.ps1 Sign
```

Build configures isolated Release/static host artifacts under `.codex-build/efi_shim_loader` and the collector under `.codex-build/efi_inventory`, then runs host tests. Raw QEMU debug/serial port writes are disabled. Sign copies and signs the production `loader.efi` and collector with SHA-256 Authenticode, pins the signer to the public project certificate, and tests that tampered copies are rejected. Signed files and receipts remain in ignored build storage.

The matching MokManager comes from the same hash-pinned Ubuntu package as the vendored shim. The package and extracted MokManager are hash checked before use. It is not a newly built unsigned MokManager.

## Stage and verify

Run these commands in elevated PowerShell:

```powershell
Set-Location C:\Users\jayli\OneDrive\Documents\Projects\hsinv
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\shim-chain-collector.ps1 Stage
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\shim-chain-collector.ps1 Status
```

Stage mounts the current system ESP temporarily and installs only these files under `EFI\HypersinvShimInventory`:

| File | Purpose |
| --- | --- |
| `shimx64.efi` | Firmware-trusted first stage |
| `grubx64.efi` | Signed production `loader.efi`, renamed to shim's expected second-stage name |
| `inventory.efi` | Signed inventory collector |
| `mmx64.efi` | Matching MokManager for interactive enrollment |
| `MOK.cer` | Public project certificate to enroll |
| `manifest.sha256` | Exact sizes and hashes for the five input files |

No hypervisor payload or replay log is placed in this folder. Stage checks signatures, verifies read-back hashes, saves a local backup before replacing an existing owned stage, verifies Windows Boot Manager is unchanged, and unmounts the ESP. It does not arm BootNext or reboot. Status verifies the stage; it does not prove the firmware will accept the chain or that BootNext is armed.

## Arm the shim, enroll MOK, and run

1. Keep Secure Boot ON. In elevated PowerShell run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\shim-chain-collector.ps1 Arm
   ```

   This arms the one-time entry pointing to `shimx64.efi`. You do not run `loader.efi` as a Windows program. BootNext selects shim; shim verifies and starts the signed loader. There is no automatic reboot.

2. Reboot when ready. If this certificate is not enrolled, shim will reject its second stage and offer MokManager. Enter MokManager when prompted, choose **Enroll key from disk**, locate the system ESP, and select `EFI\HypersinvShimInventory\MOK.cer`. Check the certificate identity and fingerprint before confirming. Use the certificate enrollment action, leaving shim validation enabled. Do not choose **Enroll hash from disk** for this test, since the intended trust mechanism is the MOK certificate.

   ```text
   Subject: CN=hypersinv
   SHA-1 certificate fingerprint: 59DBD6780CFE60C020B3F4511211FEC22C397B85
   SHA-256 of DER certificate: B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE
   ```

   Enrollment persistently trusts EFI images signed by this certificate through shim. The private key stays in your Windows signing store. Nothing writes MokList directly from Windows. The menu may reboot; if so, the first BootNext has already been consumed. Boot Windows normally, run Arm again, then reboot for the actual collector pass. If already enrolled, shim can proceed directly.

3. Expect `shim inventory diagnostic: START`, then `EFI variable inventory: report ready.` The report is written to the root of the system ESP, not G:. A separate `chain-status.txt` records that the loader observed Secure Boot and shim validation enabled and that the collector returned successfully. Windows Boot Manager then starts.

   The loader also writes `EFI\HypersinvShimInventory\chain-diagnostic.txt` before Windows handoff. This optional runtime record contains the collector launch phase, its raw 64-bit EFI status, and the last shim load gate:

   ```text
   schema=2
   collector_launch=load
   collector_status=0x800000000000001A
   shim_load_phase=security2_lookup
   shim_load_status=0x800000000000000E
   ```

   This example shows the Security2 protocol lookup returning `EFI_NOT_FOUND`, with the load wrapper refusing the child as `EFI_SECURITY_VIOLATION`; it is not a diagnosis of this host. The actual file uses CRLF line endings. Launch phases distinguish source reading, device-path construction, image loading, and child execution. Shim load gates distinguish trust-state checks, shim verification, Security2 lookup/interface validation, and firmware image loading. The trace preserves the underlying API status when available; boolean policy guards use the refusal status. Failures before the load wrapper use `not_applicable` and `EFI_NOT_STARTED`. The existing four-line `chain-status.txt` contract, signed five-file manifest, and EFI handoff remain unchanged.

4. After returning to Windows, collect from elevated PowerShell:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\shim-chain-collector.ps1 Collect
   Confirm-SecureBootUEFI
   ```

   Collect saves and verifies the raw collector report and chain status, runs the Windows scanner to merge host inventory, clears only the owned one-time firmware entry, and removes the unchanged ESP reports after successful local capture. It preserves the signed stage. `Confirm-SecureBootUEFI` should return `True`. Capture files are printed under `.codex-build/efi_inventory/captures`.

   When present, Collect validates and captures the diagnostic as `HypersinvShimChainDiagnostic.<timestamp>.<id>.txt`, and prints its path and exact launch/status and shim load trace before attempting firmware cleanup. Legacy runs without that record, and the earlier three-line diagnostic schema 1, remain supported. Malformed, internally contradictory, or changed diagnostic evidence stops output removal. Stage and Arm refuse any uncollected diagnostic. If the legacy status write failed, Collect preserves the evidence; Status can display the diagnostic for review, and Disarm can cancel the owned entry independently of output validity.

If the collector returns failure and writes chain status, Collect saves the failure status and any raw report, clears the owned one-time entry and unchanged ESP output, then exits with an error. It does not run the merge scanner or report a passing chain. You can inspect the saved failure capture before retrying Arm. If the status is missing, invalid, or files have changed, Collect preserves the ESP evidence; use Disarm to cancel the entry and inspect the failure before restaging. The loader attempts Windows handoff when the collector returns failure. If the collector or firmware hangs, there is no guaranteed application timeout; power off and use the recovery procedure below.

## Cancel or recover

Before reboot, cancel the owned one-time test entry from elevated PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\shim-chain-collector.ps1 Disarm
```

If boot stalls or a signature check fails, power off, disconnect the test USB, and use the firmware one-time boot menu to choose **Windows Boot Manager** on the original Windows disk. BootNext is normally consumed before the one-time image starts, so the following normal boot should return to BootOrder. BootOrder and the Microsoft boot files are not modified by this workflow. If BitLocker asks for recovery, use the recovery key for the original Windows installation.

After Windows starts, run Disarm. Leave Secure Boot enabled. Disarm does not require a valid stage receipt or report. Do not run it from the spare-drive clone: it resolves the current system ESP, which may be a different disk.

If Windows Boot Manager does not boot, start your Windows recovery USB in UEFI mode and use **Repair your computer -> Troubleshoot -> Advanced options -> Startup Repair**. Identify and unlock the original Windows disk if prompted. Since this workflow does not replace Windows Boot Manager or BCD, inspect the selected disk/boot entry before attempting manual boot repair. Use your full spare-drive backup if the original installation cannot be recovered. Avoid formatting an ESP or blindly running bcdboot against an unidentified partition.

Disarm removes the owned BootNext/Boot#### state, not the enrolled MOK. To withdraw that persistent trust later, use a trusted Linux environment with mokutil to request deletion of this specific public certificate (`mokutil --delete MOK.cer`), then confirm the matching certificate in MokManager on reboot. Do not reset all MOKs; other systems or drivers may depend on them. Keep the public certificate for identification/removal. See [upstream mokutil](https://github.com/lcp/mokutil).

## Validation limits

Host tests and signing checks validate code paths, signature integrity, file ownership, and staging transactions. Firmware signature acceptance and the complete boot result require an actual boot test; a staged or armed message alone is not success. The test deliberately uses a collector in place of the hypervisor while sharing the signed production loader and its downstream authentication code.

Shim behavior is described in [upstream shim](https://github.com/rhboot/shim/tree/15.8), including its MOK verifier and MokManager.

## Verified staging record (2026-10-10 local time)

The final build passed 28 loader CTest checks, 2 inventory CTest checks, 12 unsigned-profile PowerShell host checks, and 9 signed-profile PowerShell host checks. Signing verified both images against the pinned public certificate and rejected tampered copies. The actual signed files also passed a local test-directory stage and a live system-ESP Stage/Status pass.

The UEFI runtime-services header was corrected so GetNextVariableName, SetVariable, and subsequent slots match the specification. Numeric offset assertions now protect that ABI. Both signed EFI images contain SBAT metadata and relocation tables. The native shim-call fixture checks the System V to Microsoft x64 calling-convention boundary and preserved registers.

The live staging receipt records UTC time `2026-10-11T05:36:03.7783864Z` and ESP volume `\\?\Volume{187bc913-7576-4e6e-8d54-18eeb5c38fcd}\`. The dedicated directory is `EFI\HypersinvShimInventory`. The temporary S: mount was removed, Secure Boot read back as True, and Windows Boot Manager remained unchanged. Stage/Status did not invoke Arm, change BootNext, enroll MOK, or reboot. The previous unsigned `EFI\HypersinvInventory` stage was preserved.

| Artifact | SHA-256 |
| --- | --- |
| Signed production loader, `grubx64.efi` | `69F59451A2518BB75D1FF9A7D3D4B0F1A62ABED537FB242D8DB9339603A3A6F7` |
| Signed `inventory.efi` | `A3EC5EC4AF0BD6C2C99E74D4BB4840C5916E346E7EBF6D822C51EDCB2C012AB7` |
| `mmx64.efi` | `D2FA8E52FDDC99DAD94A0009FEE23CB2478C28373B777D50B2F784EB4E96F88E` |
| Stage manifest | `35C4214CBAF712295EE7D26059862281ED8B65AF77A57ABD8216EFB253758276` |
| Unchanged Windows Boot Manager | `25D8869E3C79083AE62C698A9DB9030748E3267BD53593357447F3F72761D1F2` |

Local evidence is under `.codex-build/efi_inventory`: `shim-chain-build.log`, `path-b-collector.tests.ps1.log`, `shim-chain-collector.tests.ps1.log`, `shim-signed/signed-source.json`, `shim-chain-stage.json`, and `shim-chain-live-stage.log`. These files are ignored build artifacts.

This is a historical staging record for the 2026-10-10 revision. Use the current signing/staging receipts and recovery snapshot when preparing a later retry.

## Security2 diagnostic retry (2026-10-12)

The captured diagnostic reported `collector_launch=load` and `collector_status=0x800000000000001A` (`EFI_SECURITY_VIOLATION`). Source review found the Security2 protocol GUID encoded with `8e` instead of the canonical `0e` byte. The fixture repeated that typo. Correcting the fixture first caused the bridge test to fail; correcting production lookup made it pass. The GUID now matches [EDK2's Security2 definition](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/Security2.h).

The corrected loader adds the schema 2 trace described above. All 28 component, 16 signed-workflow, and 12 base-workflow host checks passed. The loader was signed with the existing MOK certificate and staged at `2026-10-12T21:50:10Z`; its SHA-256 is `12301BAE8348C22EE9940EA866AC589D02109A4F380A58DE5F9BEFC456EF2A96`. The collector, shim, MokManager, and certificate bytes are unchanged. Independent PE/signature checks, ESP/capture write probes, and the actual recovery comparison passed. Secure Boot remained ON, BootNext was absent, and Windows Boot Manager/BootOrder/keys were unchanged. No Arm, reboot, or enrollment was performed during this preparation.

The current runbook is `.codex-build/efi_inventory/READY_TO_ARM_RUNBOOK.txt`; the complete recovery snapshot is `.codex-build/efi_inventory/diagnostic-retry.20261012T215007Z/recovery`. This fixes a verified source defect consistent with the captured failure. The old three-line capture cannot prove the exact failing security gate.

## Verified firmware run (2026-10-12)

The user completed the retry and returned to Windows. Collect saved capture ID `20261012T220522238Z.8467c7fd73424c02ab2856ec7b8af0f2`. The exact schema 2 diagnostic reports `collector_launch=ok`, `collector_status=0x0000000000000000`, `shim_load_phase=firmware_load`, and `shim_load_status=0x0000000000000000`. The exact legacy chain status reports `secure_boot=1`, `shim_validation=1`, and `collector_success=1`. Both records passed the production parser. The raw EFI capture contains 288 variable records, matching the merged report's `EFI variables: 288` count.

The user's Collect output confirms the owned one-time BootNext/Boot#### state was cleared and BootOrder was not changed. The four captures remain under `.codex-build/efi_inventory/captures`; their sizes/hashes and verified result are recorded in `.codex-build/efi_inventory/shim-chain-success.20261012T220522Z.json`. The signed diagnostic chain and Windows handoff succeeded on this host. This result does not validate VMX, hypervisor execution, or TPM replay. No further Arm or reboot is needed for this completed inventory test.
