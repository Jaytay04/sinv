# VMnet probe source bundle

Plain-text sources for a disposable-VM VMnet initialization probe, the final four-adapter round-trip, and independent raw-capture validation. They do not authorize physical-host changes.

| Source | SHA-256 | Sealed report copy |
|---|---|---|
| `vmnet-initialize-probe.template.ps1` | `F1C07550A5D9ED02846EA002F96CC9197E11285F913D203145953EAA3C561E4C` | `spoof-handoff/reports/nic-vmnet-miniport-20261018/vmnet-miniport-20261018T004517Z.3b91e91f/install-probe.template.ps1` |
| `vmnet-four-target-roundtrip.template.ps1` | `6BCA2AAE29AEF3EC31B7EE6C1C6BCDB3DCD7652E9541D88CD95577741DF63F84` | same fixture, `four-target-roundtrip.template.ps1` |
| `validate-four-target-roundtrip.ps1` | `A77D106129D871294017B4205BF4F6BB14A59C9D404392CB62E33312C1A04E4C` | same fixture, `validate-four-target-roundtrip.ps1` |

No generated payload, vendor binary, private key, credential, VMX, or snapshot is included. The templates contain only base64 substitution tokens. Rendering is fixture-local: `render-install-probe.ps1` needs the signed VMware package and DevCon; `render-four-target-roundtrip.ps1` needs the native reader and pinned production helper bundle. These inputs and renderers remain in the report fixture, so this directory alone is not a runnable package.

Sealed fixture provenance: `input-manifest.json` SHA-256 `A7EBFEAEABC881840EF1AF7B8393FDDD8C15BB9D1D0C3A604B3E6E5EDA0FCBDC`; locked `client-originals.tsv` `F628CB25D18F549A877C1529A82986B524BFD3D8B9801A776405F969FC4F42C3`; native reader `BF553F39E4E7024C75272C24215D995858627AEDD5E946A4B1289A8033DA1642`; production helper bundle `E7B1B841372B890C0D1638E6CCB11D41B5F257A64FBF491B3FEF811B29A212D8`. The report-local renderers hash to `FA78E902CC824A84AD16412BE76AA6052BD62917DCC872D272E6C868D8D90F43` and `21AF1F4606A016E90BDD8EA66521E094B6CF1538C80F02D17FE0B5E53FBF34A8`; their generated payload hashes are `4A203A895552DA0813B152D88766D35207D0B642EDB4030E85E100E9673A142D` and `81FBE466B667F371431E01CACF00B9266DD48E2FA9C36A0DA2CB71FDCD9D2BCE` respectively. Generated payloads remain only in the report fixture.

The recorded VM run passed 84/84 raw checks across four adapters, seven readers, and original/replacement/restored phases. Independent validation recomputed 84 checks and rejected three negative cases. Evidence: round-trip metadata `spoof-handoff/reports/nic-vm-20261017/uefi-windows-probe-20261018-011305-e1ffeb9e48/evidence/kernel-metadata.json` SHA-256 `067674BBA247E1AD31FCA99826D4F3A0A67655DF989E1D129B8493117C518695`; independent result `spoof-handoff/reports/nic-vmnet-miniport-20261018/vmnet-miniport-20261018T004517Z.3b91e91f/independent-validation/20261018T011552924Z.a02b0f0b/roundtrip-validation.json` SHA-256 `89BD0DF8E306A74A254D6019F820D050B78E38FDC52E220139BE1E6060EBC14E`; recovery record `spoof-handoff/reports/nic-vmnet-miniport-20261018/vmnet-miniport-20261018T004517Z.3b91e91f/vm-recovery.20261018T0115Z.json` SHA-256 `A2C5E205096A6D239CB1EB5CAB142208F0480009EB193D82ED1361275715E62E`.

The clone was cold-reverted and powered off. Its VMX matches the recorded backup hash `BD68D9110A26EC274E0D9AD7FAADFF94F93DEAD53DEE31F4B732F76D660B31F3`. This VM result does not establish physical-host NIC behavior.

The original input manifest mislabeled another disposable clone's hash as the protected source VMX. The preserved `preflight-vmx-path-correction.json` identifies both paths; the protected source remained `537BBE6D91DFA716D698A5E8301F5C2C59FC98CC65308BC905A9938A3129F0C9`.
