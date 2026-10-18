# R5 EFI-only SMBIOS diagnostic and recovery

Recorded after the Clean4 restore and final VM power-off. This iteration only diagnosed the SMBIOS boot payload in the disposable VMware guest. It did not run the storage/NIC driver cycle, modify the physical host, or establish a simultaneous active spoof proof. USB storage was excluded.

The final guest `vmrun list` check was executed at `2026-10-18T04:29:58.2127253Z` with exit code 0 and output `Total running VMs: 0`. The authorized VMX was `spoof-handoff/artifacts/combined-vm-20261018/DeviceFilter-Win11-Combined/DeviceFilter-Win11-Combined.vmx` (guest UUID `F0114D56-CF8A-8A36-2772-AAE2FA2464D1`).

## R5 fixture and build evidence

- Fixture manifest: `spoof-handoff/artifacts/combined-vm-20261018/smbios-failure-diagnostic-r5-20261018T042000Z/fixture-build.json`, SHA-256 `6D23EABF4C9711DDF4F8B234292AE06CDE070D2DBC662BE10F7F08FE009F4F09`.
- Compile source-set SHA-256: `CEC01E03E7B215929B88645FC998D0433860692A3A06B6580EAEEA841F647862`.
- Collector `build/efi/inventory.efi`, SHA-256 `BD86CA73EFACE5B2195DDF0AEEA4BE9BBB2BCD2DF778D77F22DF2D9B82AB60F9`; SMBIOS reader `build/smbios_windows_readback.exe`, SHA-256 `C0EF07AC4698644758F2384BC8524CB87FB9946FE04972F47C3F6BD30A80A74C`.
- `ctest.txt`, SHA-256 `1A42DC53ABF4ED95DDE9A5B057591D84BDDDA8585C9887AD41179EC669382E96`: 2/2 tests passed.
- Independent source audit: `spoof-handoff/reports/combined-9field-vm-20261018/independent-audit/efi-diagnostic-r5-source-audit-20261018.md`, SHA-256 `C59E6068654D8F393D0A25BBCEF2D10D2346097CA0F1448F066A4DC45BA7D9FA`.

## EFI-only run result

Authenticated stage-and-arm completed successfully without changing BootOrder. It staged `loader.efi`, the r5 `inventory.efi`, and `manifest.sha256`, then set BootNext to `0600` (Boot0006). The staged EFI and loader hashes matched their receipts. One diagnostic boot followed.

The payload did not patch SMBIOS. It failed in `prepare` at `smbios2_maximum_structure_size`: the SMBIOS 2.7 entry point reported a 12,842-byte table, 165 structures, and maximum structure size 111, while the complete parsed table reports maximum size 112. The EFI status was exactly `0x8000000000000021`. The report says `prepare_replacement_metadata=0`, `result=failed`, and no rollback or free was attempted. Fresh RSMB and WMI readback remained at the original values. Do not treat this run as an active spoof proof.

The complete patch diagnostic is in `HypersinvSmbiosPatch.txt` (1015 bytes), SHA-256 `7D18B3A827DF0F090D0EE961EEA17BDB85E9C7283676C864003422C487FACDB5`, embedded in the diagnostic kernel metadata below.

## Clean4 restore and same-boot original verification

After restoring the Clean4 snapshot, one cold boot was used for three read-only guest actions. CaptureOriginal and the combined Original phase recorded the same exact Windows LastBootUpTime, `2026-10-18T04:24:24.5000000Z`. The SMBIOS diagnostic reports an uptime-derived approximate boot time of `2026-10-18T04:24:24.183Z` (317 ms earlier).

- `CaptureOriginal` passed. Secure Boot remained at the VM baseline OFF (`SecureBoot` variable bytes `00`); BootOrder was `050000000100020003000400`; BootNext was absent (Win32 error 203); test-signing BCD value was absent; effective Code Integrity options were 5; Root and TrustedPublisher membership added by the test was absent.
- The r5 diagnostic passed and read Boot0006 as absent (Win32 error 203). It also observed the seven original SMBIOS values through fresh RSMB and WMI. No project SMBIOS files or inventory stage directory remained on the ESP.
- The combined Original phase passed for 12 bindings in two polls: 44 successful reader assertions in each poll, 88/88 total. All original values matched and the same-boot checkpoints held. Reader kinds included RSMB, WMI, storage IOCTL, NDIS OID, `GetAdaptersAddresses`, `GetAdaptersInfo`, `GetIfTable2`, and `Get-NetAdapter` paths. USB disks were excluded. The file integrity probe also passed and removed its owned temporary file.

The pre-stage Clean4 CaptureOriginal evidence is `spoof-handoff/reports/combined-9field-vm-20261018/uefi-windows-probe-20261018-041639-5972c76467/evidence/kernel-metadata.json` (SHA-256 `66B251DD1F64539618E05325EF08CB791ABB31A8C649362DC4A55FFD2A2CCC87`). The Clean4 baseline and post-restore capture agree on BootOrder, current BCD enumeration, firmware enumeration, Windows Boot Manager SHA-256 (`25D8869E3C79083AE62C698A9DB9030748E3267BD53593357447F3F72761D1F2`), and all 143 ESP entries with stable SHA-256 values. Five mutable/Windows-managed ESP entries have no hash comparison. The raw BCD export hashes differ (`33DE1EB318702F9EFF7E61AC772D01B17A3834149CFD3B7CD88BED1BC963784F` before staging; `AD2EDDE0836DEF54E559F0FD5107DC523F4E12F4CF51E8E60D0638008AD9DD94` after restore; the independent audit measured a 708-byte difference). Therefore this report does not claim byte-identical BCD export restoration.

## Hash-pinned authenticated run evidence

For each run, `response.json`, `response-metadata.json`, `guest-evidence.zip`, and `evidence/kernel-metadata.json` are preserved together under the listed run directory. The metadata hashes below bind the detailed reports; response and archive hashes preserve the authenticated mailbox evidence.

| Phase | Run directory | response.json SHA-256 | response-metadata.json SHA-256 | guest-evidence.zip SHA-256 | kernel-metadata.json SHA-256 |
|---|---|---|---|---|---|
| Stage and arm | `uefi-windows-probe-20261018-041850-9958a7a427` | `41BAAA80101BF2D74BF0AE042C82CBF771F1B7AE71B240DE5ACCF2902EE84D85` | `FD2DEA1D44F0D14380099A9095EEF1DCCE59EC232C32EB0613E73E38F56DF7B1` | `B984DB65BC4E7A97B23A146CBF2C474B2812F0C07D78CC460B0DD6C99E065EB8` | `435951FE616A645360BD979FF12989B66FBC72FB55ADB6EF454A630A7E2BB263` |
| EFI-only r5 failure | `uefi-windows-probe-20261018-042120-51e4efcbe6` | `B6B2166E872333BFBC2BD99A6563A23267A63867E6CF3B4597640D31A289CBB1` | `27518A2EA798C15CE647AA7FDAFF7FB76A18ADE854EC09062D1F3AE676CBD3A1` | `303E703C2CD3EDA6A3A212B987B76DBCD6FAF1B2F114CAD75D98C65506A9720D` | `87163E0C5A7281B8DA3D8BF8CB3F42F635F43B345AD10C363CD3C55D085C9E74` |
| Post-restore CaptureOriginal | `uefi-windows-probe-20261018-042437-e26d414746` | `7EAF396397ED609922E73B94C15B4D5D4AD1B70AA846038EE1FFA9B9B559E91B` | `9F7340B31FCC6AE4C676455B70787ECC5A77297CE5FD499FE1DAB48E3BC8F685` | `3A584D05C9AEA633F69649D07F1DA36DB3D3949E421291F5522023F3F9E1A548` | `A7E7243267CBFDB766D112D643E90D9CCD43419CB977203904028A56C157169C` |
| Post-restore Boot0006 diagnostic | `uefi-windows-probe-20261018-042523-f8a8b447b6` | `2BC982FF8B7ABFC60616FB09B95F42088FC921897A11B33AC6C4BB6A8CCE9379` | `5E3E2950F53EC541FF350982BCC750625F3AAF12D722D85109903C1148BF4C6E` | `04926D5DB499FF3097776E548A2AD2B729A9CDE8C3477AE4CA4858D5BE4C72D0` | `080A097264C2BE1120B76F9DC7C5BEB788F0C3EC2DB5E02329A8BF600496A4F2` |
| Post-restore 88-read Original phase | `uefi-windows-probe-20261018-042612-496b5c9b9d` | `D2012BCB281A2961EA8B6C8E3079635AB69AEF0CBB9CC58D8354650481ED2A4A` | `98F903924238DF5760B3D2BFF58B4598CDC585DDBE9C6D9BA062EF8397D91A6E` | `F7F67B36062B6CC84CDAF3D81E334509EED9286E7693F55CA5C90C91525AD4EF` | `625F94FDD5B62C84228D9730CE31BE783FC5E714A7267CFDD63F7FB40168E712` |

The Original phase embeds the raw combined-readback report in its authenticated evidence: guest report SHA-256 `3C4257D2942395ECA1341BC81AF66634D5E6E33AA3540F0A213921B14DA5602E`, 101,352 bytes, archive leaf `f.0001.json`; nested raw-evidence bundle SHA-256 `E1124BC48F05D86187D639DC6EA6C4039B897AE62E463E281B59C3A7D070E0D0`, 25,173 bytes.

## Source and rendered script hashes

| File | SHA-256 |
|---|---|
| `.codex-build/worktrees/smbios-boot-serial-probe/spoof-handoff/tools/combined_vm/guest-smbios-pathb-stage-arm.template.ps1` | `71AA43B0A79D4370B0423C191C0601671EC404E5F9C1A27631D96F1BAE2F40DF` |
| `.codex-build/worktrees/smbios-boot-serial-probe/spoof-handoff/tools/combined_vm/render-smbios-pathb-stage-arm.ps1` | `2FA07D6302C95C312D7FDDB2971FC204CACB0D7D1593EF5ED575D4316DBE8CF2` |
| `.codex-build/worktrees/smbios-boot-serial-probe/spoof-handoff/tools/combined_vm/guest-smbios-diagnostic.template.ps1` | `E66DE6E27EC6C936EE7B9D16F246428E063173D025B8F232EE355B4E51A03CB4` |
| `.codex-build/worktrees/smbios-boot-serial-probe/spoof-handoff/tools/combined_vm/render-smbios-diagnostic.ps1` | `CD452CB307A6BEBE6765FA08891BFA18DCFB24180BC9A678A4A1923F7A1699D4` |
| Rendered stage-and-arm script `runtime/guest-smbios-pathb-stage-arm.20261018T041824Z.db680416a72e4a06b4b7c3e2b41f4c61.ps1` | `5E8CFF41E459EBBD5AB34DCA5D1E0870669BB6C3967CD0B7057355FE8A67A423` |
| Stage-and-arm render receipt `runtime/guest-smbios-pathb-stage-arm.20261018T041824Z.db680416a72e4a06b4b7c3e2b41f4c61.render.json` | `574F6C74D276FB13DB2DA5309BC809AEEAADADA919FC87A141110FB9F835201C` |
| Rendered r5 diagnostic `runtime/guest-smbios-diagnostic.20261018T042113Z.9d91c1867fb8492f8fa0937a3d3dff21.ps1` | `6B62C04DC2F7A7091081939909E87AE2A9D5233363FA8E17AAA798ADF4F76591` |
| r5 diagnostic render receipt `runtime/guest-smbios-diagnostic.20261018T042113Z.9d91c1867fb8492f8fa0937a3d3dff21.render.json` | `4F6D5C4E71C578F24F1FAFA3C5517A892DCF4E265E2DCA7280CB2283F27BF5B7` |
| Rendered post-restore CaptureOriginal `lifecycle/guest-lifecycle.CaptureOriginal.20261018T042300Z.105396b4daa94d1f85c7f89e5365cbda.ps1` | `6680EF46040E2EBF107545A48D5ADC02D5AAB2536F557F3F96D00E140D031912` |
| CaptureOriginal rendered config lifecycle/guest-lifecycle.CaptureOriginal.20261018T042300Z.105396b4daa94d1f85c7f89e5365cbda.config.json | 98142A262BEB1A30FD3DA97169D5C3B96ACB1B2CE2085142D5D8872012A49DB5 |
| CaptureOriginal render receipt lifecycle/guest-lifecycle.CaptureOriginal.20261018T042300Z.105396b4daa94d1f85c7f89e5365cbda.render.json | 42085AE5D18FC21FC48AC3D29087C981CD5B4C46E05A600C8942011F08581C48 |
| Rendered post-restore Original matrix `runtime/guest-original.20261018T042603Z.ca209437070348c6b6648974d9afde7e.ps1` | `8F25EB39C0E80B1130CC9BE49D7F8D8A7FADF74089827D0C1E65B1DF8954833E` |
| Original matrix render receipt `runtime/guest-original.20261018T042603Z.ca209437070348c6b6648974d9afde7e.render.json` | `095C236E1DC0BD5A33837086636E079AD9A02463C4331553112E26BD7312D2F4` |

The physical Windows host was not used for activation. The VM remains off. No full active 9-field combined proof is claimed; the next work must address the 111-versus-112 SMBIOS 2.7 maximum-size discrepancy without weakening the payload guard.