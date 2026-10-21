Set-StrictMode -Version 2.0

$script:GpuRetirementRunId = 'gpu-20261020T150318Z-59a346ed3cb8422bafe4be60268b7df1'
$script:GpuRetirementAuditRelativePath = '.analysis\gpu-baremetal-readiness\7216b61c2c11467f851d6b3192ed5dc6\readiness.json'
$script:GpuRetirementAuditSha256 = '05D4B6FCB5831CAC81A3E1C77EC7B4E852F0AA11C3FCA83C837676577C2F86C0'
$script:GpuRetirementJournalSha256 = 'F53925C27953EA695BE0DCA94105611571D08F7877AABFB95F00513AAF6F46C9'
$script:GpuRetirementArchiveSha256 = 'ABC00DBDFFC570B1EC78255426F3145CC12438DE023CFD5877D9B6248F363CA7'
$script:GpuRetirementSourceCommit = 'd79806d51187963cd81578b7287076e17558c2e2'
$script:GpuRetirementProbeSha256 = '49CCB02124E6536F4F107B83C4E79D31C5DA71993585DFD48139726F11C79D5E'
$script:GpuRetirementProbeBytes = 172544L
$script:GpuRetirementAuditToolSha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
$script:GpuRetirementAuditToolBytes = 246784L
$script:GpuRetirementAuditToolRelativePath = '.codex-build\gpu-stage-retirement-audit\readonly-v1\ibaudit.exe'
$script:GpuRetirementReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
$script:GpuRetirementReferenceLogBytes = 40838L
$script:GpuRetirementAuditBytes = 12050L
$script:GpuRetirementJournalBytes = 44878L
$script:GpuRetirementArchiveBytes = 533L
$script:GpuRetirementReviewedFirmwareRelativePath = '.analysis\gpu-stage-retirement\0851536237dd4f6a9ff5df72b0c36848\observations\stage-retirement-firmware-214f7cdb11a647beb09fd271978cca83.stdout.txt'
$script:GpuRetirementReviewedFirmwareSha256 = 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE'
$script:GpuRetirementReviewedFirmwareBytes = 4336L
$script:GpuRetirementNames = @('grubx64.efi', 'handoff-status.txt', 'manifest.sha256', 'payload-diagnostic.txt', 'payload.efi', 'shimx64.efi', 'tcglog.ib')

# A second, closed retirement profile is admitted only after the caller supplies
# a fresh readiness report explicitly. The values below describe immutable
# evidence from the completed 201140 run; the audit pin is supplied by that
# explicit readiness report and is captured again immediately before deletion.
$script:GpuRetirement201140RunId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
$script:GpuRetirement201140JournalSha256 = '7C5D643B05604954E5C03142177AF52F98044A440D3D8979632409BC2438A9ED'
$script:GpuRetirement201140JournalBytes = 52423L
$script:GpuRetirement201140ArchiveSha256 = 'A7416148825A6ABC3603681C45F6A8277AFEE432D4B503BDEC89D34F58D34FFB'
$script:GpuRetirement201140ArchiveBytes = 533L
$script:GpuRetirement201140SourceCommit = '933b4c676318f26396cdf2c66de01c3ccbf1a950'
$script:GpuRetirement201140ProbeSha256 = 'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47'
$script:GpuRetirement201140ProbeBytes = 172544L
$script:GpuRetirement201140ReferenceLogSha256 = '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'
$script:GpuRetirement201140ReferenceLogBytes = 40838L
$script:GpuRetirement201140ReferenceLogRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7\actions\20261020T201533358Z-Backup-479329a8e70a4124964be17cc597ad61\tcglog.ib'
$script:GpuRetirement201140ProbeRelativePath = '.codex-build\gb\8be3274dfe56cec7\l\ib-tcg-dump.exe'
$script:GpuRetirement201140OriginalFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7\actions\20261020T201533358Z-Backup-479329a8e70a4124964be17cc597ad61\firmware-enumeration-backup-4196e13c22604293a54688532c15cff6.stdout.txt'
$script:GpuRetirement201140ReviewReceiptRelativePath = '.analysis\gpu-final-firmware-review\8abb7b84ca644a7c84019a1f29b35bb4\result.json'
$script:GpuRetirement201140ReviewReceiptSha256 = 'BEA6DCA1FC904B13830908471D6239A906F71AA6937630B32CBA6DC3C2C023D9'
$script:GpuRetirement201140ReviewReceiptBytes = 16924L
$script:GpuRetirement201140OriginalFirmwareSha256 = 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE'
$script:GpuRetirement201140CurrentFirmwareSha256 = '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'
$script:GpuRetirement201140FirmwareBytes = 4336L
$script:GpuRetirement201140RawCaptureSha256 = 'B5D4EB51A58A2170EBCA905749E2BDAD177CFD71629A1079A14934F3F5BF63D0'
$script:GpuRetirement201140RawCaptureBytes = 259L
$script:GpuRetirement201140BootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
$script:GpuRetirement201140ReviewActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7\actions\20261020T213747018Z-FinalVerify-1c98befbf8344c0188af1178737b723a\fw-review'
$script:GpuRetirement201140ReviewedFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7\actions\20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534\firmware-enumeration-final-7a5347fd0bc4452eb06340ba1e614a92.stdout.txt'
$script:GpuRetirement201140FreshFirmwareRelativePath = $script:GpuRetirement201140ReviewActionRelativePath + '\final-firmware-review-733d15b0a2254fc2ba94a5a0178bea28.stdout.txt'
$script:GpuRetirement201140ProfileSelected = $false
$script:GpuRetirement224018RunId = 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1'
$script:GpuRetirement224018JournalSha256 = 'F06102D9C30E26E5E590FD8E3EF601DCFCE2948F457223589F5D529C22223A7C'
$script:GpuRetirement224018JournalBytes = 52411L
$script:GpuRetirement224018ArchiveSha256 = 'A996B969394113B052010129563BF3EAE35443B0B452B0C66D0B7B69FF9D75B3'
$script:GpuRetirement224018ArchiveBytes = 533L
$script:GpuRetirement224018SourceCommit = '69a3c3d94f33d1903778ef5ec5b0a1f015b1be29'
$script:GpuRetirement224018ProbeSha256 = '76828AFED037973FBB4357AD89C97FB65D85D3BCF7811C9281F199ECB65A4583'
$script:GpuRetirement224018ProbeBytes = 172544L
$script:GpuRetirement224018ProbeRelativePath = '.codex-build\gb\a68d89b7b3ecb1a1\l\ib-tcg-dump.exe'
$script:GpuRetirement224018ReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
$script:GpuRetirement224018ReferenceLogBytes = 40838L
$script:GpuRetirement224018ReferenceLogRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1\actions\20261020T224257466Z-Backup-8bf99023efa4478e91e8fe626493da70\tcglog.ib'
$script:GpuRetirement224018OriginalFirmwareSha256 = '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'
$script:GpuRetirement224018CurrentFirmwareSha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
$script:GpuRetirement224018OriginalFirmwareBytes = 4336L
$script:GpuRetirement224018CurrentFirmwareBytes = 2890L
$script:GpuRetirement224018OriginalFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1\actions\20261020T224257466Z-Backup-8bf99023efa4478e91e8fe626493da70\firmware-enumeration-backup-99b1a3488d3d47ba93a25d46ae052d2d.stdout.txt'
$script:GpuRetirement224018ReviewedFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1\actions\20261020T225933250Z-FinalVerify-cf3ab7b1e838427c964640d87f30257e\firmware-enumeration-final-bd819c45c1e94e01b00b44dafe51c163.stdout.txt'
$script:GpuRetirement224018ReviewReceiptRelativePath = '.analysis\gpu-final-firmware-review\1dccd9d33d1b4c808fd1059291a6d19b\result.json'
$script:GpuRetirement224018ReviewReceiptSha256 = '0E7107911CF43F4144F4E67AEBB647623003AF543FA38974EBEAE518F0D1642E'
$script:GpuRetirement224018ReviewReceiptBytes = 15415L
$script:GpuRetirement224018ReviewActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1\actions\20261020T232519113Z-FinalVerify-5a786491a066456facce99b21b3bf06a\fw-review'
$script:GpuRetirement224018FinalActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1\actions\20261020T232519113Z-FinalVerify-5a786491a066456facce99b21b3bf06a'
$script:GpuRetirement224018FreshFirmwareRelativePath = $script:GpuRetirement224018ReviewActionRelativePath + '\final-firmware-review-cae01cb08d3d43f8bcff47d818da7cab.stdout.txt'
$script:GpuRetirement224018FinalResultRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1\actions\20261020T232519113Z-FinalVerify-5a786491a066456facce99b21b3bf06a\result.json'
$script:GpuRetirement224018FinalResultSha256 = 'CEB070C4D863F82450B06B37DE22AEFF8D641396364A1A33719F875551DC6973'
$script:GpuRetirement224018FinalResultBytes = 9184L
$script:GpuRetirement224018RawCaptureSha256 = 'A8D2C8A2D2C7FB5EF6F12DA9362B4806A73136F9DD3801511FE2D89FAF6EF7A9'
$script:GpuRetirement224018RawCaptureBytes = 253L
$script:GpuRetirement224018BootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
$script:GpuRetirement224018InspectorSha256 = 'FBA167F5947E6722146272C9B17FE2220FDB6A1297AE764626725E62AF6BB7DD'
$script:GpuRetirement224018InspectorBytes = 33353L
$script:GpuRetirement224018StateSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
$script:GpuRetirement224018StateBytes = 19843L
$script:GpuRetirement224018HistoricalPlatformSha256 = '4276ED335283E2AD48E5D0426B8D4857063C2B9ADBFE1DC41693A0BE64BB9B51'
$script:GpuRetirement224018HistoricalPlatformBytes = 111525L
$script:GpuRetirement224018HistoricalRetirementSha256 = 'DF7F446426F63E21C5596E9CD1B35ECA0172C182B3E802B6267962B3EF8B1D75'
$script:GpuRetirement224018HistoricalRetirementBytes = 105056L
$script:GpuRetirement224018ProfileSelected = $false
$script:GpuRetirement023306RunId = 'gpu-20261021T023306Z-f33dfae1e322470ca9315bbbf8d49185'
$script:GpuRetirement023306JournalSha256 = 'AF913F6952E4614AB3A806DBC9C82667D62E5CA2CD955950030934D172DABAAC'
$script:GpuRetirement023306JournalBytes = 44878L
$script:GpuRetirement023306ArchiveSha256 = 'A988AE8937E5E785DC3EF87B64791BA3D424F5F3577089136F653DAB4E5AADC7'
$script:GpuRetirement023306ArchiveBytes = 533L
$script:GpuRetirement023306SourceCommit = '49fbf270093f5e65a3f00fd63077b113d662f301'
$script:GpuRetirement023306ProbeSha256 = 'B03B7210D63494FAE909034406172CC76B97C48E6240B9CAFE593165F23FC5B2'
$script:GpuRetirement023306ProbeBytes = 172544L
$script:GpuRetirement023306ProbeRelativePath = '.codex-build\gb\a9315bbbf8d49185\l\ib-tcg-dump.exe'
$script:GpuRetirement023306ReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
$script:GpuRetirement023306ReferenceLogBytes = 40838L
$script:GpuRetirement023306ReferenceLogRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T023306Z-f33dfae1e322470ca9315bbbf8d49185\actions\20261021T023634908Z-Backup-6667599ef49a4e44b62e049ac3f02ffa\tcglog.ib'
$script:GpuRetirement023306OriginalFirmwareSha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
$script:GpuRetirement023306OriginalFirmwareBytes = 2890L
$script:GpuRetirement023306OriginalFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T023306Z-f33dfae1e322470ca9315bbbf8d49185\actions\20261021T023634908Z-Backup-6667599ef49a4e44b62e049ac3f02ffa\firmware-enumeration-backup-f52e3a8fa4b1421ab4497119e1a3287a.stdout.txt'
$script:GpuRetirement023306FinalActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T023306Z-f33dfae1e322470ca9315bbbf8d49185\actions\20261021T024702343Z-FinalVerify-d4bc5edd8707406f8b22c2fa341b6217'
$script:GpuRetirement023306FinalResultRelativePath = $script:GpuRetirement023306FinalActionRelativePath + '\result.json'
$script:GpuRetirement023306FinalResultSha256 = '6B51CD5351AAC75605868122730C38998093F5B73226DDA48A322179CA55093F'
$script:GpuRetirement023306FinalResultBytes = 2221L
$script:GpuRetirement023306BootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
$script:GpuRetirement023306ProfileSelected = $false
$script:GpuRetirement032117RunId = 'gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1'
$script:GpuRetirement032117JournalSha256 = '152259205B75D5E2CAB6F0D036041707E0AAA9DDD3A2431BFF239C421F38699F'
$script:GpuRetirement032117JournalBytes = 44878L
$script:GpuRetirement032117ArchiveSha256 = 'F72F7BA7A21AB3DF2D075541B6F2103F57133A8199CD3DB14EC735ACB91C82A7'
$script:GpuRetirement032117ArchiveBytes = 533L
$script:GpuRetirement032117SourceCommit = 'c6a36ffcda3e7d599ceff57439b8d7d6049f40da'
$script:GpuRetirement032117ProbeSha256 = '80BD76951F16AE34F10CB3981C6D6CE71CA7E51422CF3A56615DF637D6FBF98D'
$script:GpuRetirement032117ProbeBytes = 172544L
$script:GpuRetirement032117ProbeRelativePath = '.codex-build\gb\b50c6c830b9048b1\l\ib-tcg-dump.exe'
$script:GpuRetirement032117ReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
$script:GpuRetirement032117ReferenceLogBytes = 40838L
$script:GpuRetirement032117ReferenceLogRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1\actions\20261021T032438876Z-Backup-f6bae3de7c634bc6a9086b6e9686f8a6\tcglog.ib'
$script:GpuRetirement032117FirmwareSha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
$script:GpuRetirement032117FirmwareBytes = 2890L
$script:GpuRetirement032117FirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1\actions\20261021T032438876Z-Backup-f6bae3de7c634bc6a9086b6e9686f8a6\firmware-enumeration-backup-f0f0d2e114d3489a9af255d15032ddbd.stdout.txt'
$script:GpuRetirement032117FinalActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1\actions\20261021T033825524Z-FinalVerify-75e8910ae96948b692c1a9c1135b52ec'
$script:GpuRetirement032117FinalResultRelativePath = $script:GpuRetirement032117FinalActionRelativePath + '\result.json'
$script:GpuRetirement032117FinalResultSha256 = 'ED5DA676B72B6ABA6FEFFCD22E6A1F8C3404C889B73E27FB6F8F722DCF994A21'
$script:GpuRetirement032117FinalResultBytes = 2221L
$script:GpuRetirement032117BootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
$script:GpuRetirement032117ProfileSelected = $false
$script:GpuRetirement042142RunId = 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2'
$script:GpuRetirement042142JournalSha256 = 'A4B6D6C8DB917F5A0578A9D4A6775D422CED476BD164B579C4506F73B0F9BF9F'
$script:GpuRetirement042142JournalBytes = 50987L
$script:GpuRetirement042142PreReviewJournalSha256 = '3287B5E86D018DD374748D56E16CF7BBDE0D245ECF426779EAAB80E5A2B08583'
$script:GpuRetirement042142PreReviewJournalBytes = 44133L
$script:GpuRetirement042142ArchiveSha256 = 'B4CF6844894A920912EFF85B80012ED3B0B5504D7BD01341A531316C8FC8424C'
$script:GpuRetirement042142ArchiveBytes = 533L
$script:GpuRetirement042142SourceCommit = 'a52aa25e918223abae295876800933df9b642e46'
$script:GpuRetirement042142ProbeSha256 = '86D15C1E8B29A7896FC8D4A9AA14ECD926DA23D8EED2871DBA65DDDA7002BD36'
$script:GpuRetirement042142ProbeBytes = 172544L
$script:GpuRetirement042142ProbeRelativePath = '.codex-build\gb\840ce906358bbac2\l\ib-tcg-dump.exe'
$script:GpuRetirement042142ReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
$script:GpuRetirement042142ReferenceLogBytes = 40838L
$script:GpuRetirement042142ReferenceLogRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2\actions\20261021T042510110Z-Backup-b0ad2425a4cd436fa583d76122c8afe1\tcglog.ib'
$script:GpuRetirement042142OriginalFirmwareSha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
$script:GpuRetirement042142OriginalFirmwareBytes = 2890L
$script:GpuRetirement042142OriginalFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2\actions\20261021T042510110Z-Backup-b0ad2425a4cd436fa583d76122c8afe1\firmware-enumeration-backup-297ad383796447e38ac14cc7e3ba5879.stdout.txt'
$script:GpuRetirement042142ReviewedFirmwareSha256 = '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'
$script:GpuRetirement042142ReviewedFirmwareBytes = 4336L
$script:GpuRetirement042142ReviewedFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2\actions\20261021T044758857Z-FinalVerify-a88b0158befb4af49d9643cb14da1a1c\firmware-enumeration-final-0b0629c3908d46e98c61bf9462991cd8.stdout.txt'
$script:GpuRetirement042142ReviewReceiptRelativePath = '.analysis\gpu-final-firmware-review\255f08b53f3a432cbe8cd2b386447781\result.json'
$script:GpuRetirement042142ReviewReceiptSha256 = 'AD43DF7601413CDAA13A561D499F6902E47AEC53932872014D55B009EB2EC736'
$script:GpuRetirement042142ReviewReceiptBytes = 17633L
$script:GpuRetirement042142FinalActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2\actions\20261021T052203721Z-FinalVerify-f4bb10c538e44fb79619716d9c892496'
$script:GpuRetirement042142CollectionActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2\actions\20261021T043604642Z-Collect-5e36f479feda42ffb5cc43392c340595'
$script:GpuRetirement042142FinalResultRelativePath = $script:GpuRetirement042142FinalActionRelativePath + '\result.json'
$script:GpuRetirement042142FinalResultSha256 = 'B9820EC54B4CD8CFB2E0F8E950FAD204A93E442E5FF251021BA1E65D2758F018'
$script:GpuRetirement042142FinalResultBytes = 9196L
$script:GpuRetirement042142FailedResultRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2\actions\20261021T044758857Z-FinalVerify-a88b0158befb4af49d9643cb14da1a1c\result.json'
$script:GpuRetirement042142FailedResultSha256 = '885BC6B54E13815762C85D9BF8ED60AB4C11762A3E584F143BFAAB52E6C72B69'
$script:GpuRetirement042142FailedResultBytes = 2285L
$script:GpuRetirement042142ReviewActionRelativePath = $script:GpuRetirement042142FinalActionRelativePath + '\fw-review'
$script:GpuRetirement042142RawCaptureSha256 = '69733AFE9DD782577F302F79A788DF2745BB9C9CD4BE860DF44C59A07EB4E066'
$script:GpuRetirement042142RawCaptureBytes = 259L
$script:GpuRetirement042142BootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
$script:GpuRetirement042142HistoricalPlatformSha256 = 'D30A1A38BF5B9CAD07F43C237599847A8D5ECF0825D7D3552A92F599801C543F'
$script:GpuRetirement042142HistoricalPlatformBytes = 111625L
$script:GpuRetirement042142HistoricalRetirementSha256 = '395101C30399FD96C2404BAA049054A287375060D95B7D9773FE2147647E563D'
$script:GpuRetirement042142HistoricalRetirementBytes = 183814L
$script:GpuRetirement042142CurrentPlatformSha256 = '6EEC7FAB2A3DD095215B85781A3273A87B026ED8E3DF647225C40225E332336E'
$script:GpuRetirement042142CurrentPlatformBytes = 111703L
$script:GpuRetirement042142ReviewHelperSha256 = '579B580011446FE5BC98345F30D8B44ADA73396592604912A3DDF3761ADC25B2'
$script:GpuRetirement042142ReviewHelperBytes = 67184L
$script:GpuRetirement042142InspectorSha256 = '518701A9CA9059F01B700A084736B59A9F66A382221EBD96BF436C5E67A34242'
$script:GpuRetirement042142InspectorBytes = 37603L
$script:GpuRetirement042142StateSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
$script:GpuRetirement042142StateBytes = 19843L
$script:GpuRetirement042142CurrentBootId = '2026-10-21T04:46:50.5000000Z'
$script:GpuRetirement042142EspVolume = '\\?\Volume{187bc913-7576-4e6e-8d54-18eeb5c38fcd}\'
$script:GpuRetirement042142EspPartitionGuid = '187BC913-7576-4E6E-8D54-18EEB5C38FCD'
$script:GpuRetirement042142EspDiskNumber = 0
$script:GpuRetirement042142StagePins = @(
    [pscustomobject]@{ Name='grubx64.efi'; Sha256='B7B0176261592F97E3E78EA97C6AB1DEA411ECCA67AE19BF294E2A931183C455'; Bytes=62824L },
    [pscustomobject]@{ Name='manifest.sha256'; Sha256='9BBCFDE76392D55E0AE527C892F68BBA0B74D27CDC8A5A59D1D7B178C80B19B7'; Bytes=394L },
    [pscustomobject]@{ Name='payload.efi'; Sha256='B5F277FC7F05ABE03AAE24DE8ADF4DE26B22E1FB3872743B4BB3A9A67305CF7F'; Bytes=191336L },
    [pscustomobject]@{ Name='shimx64.efi'; Sha256='6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B'; Bytes=966664L },
    [pscustomobject]@{ Name='tcglog.ib'; Sha256='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'; Bytes=40838L }
)
$script:GpuRetirement042142ProfileSelected = $false
$script:GpuRetirement062106RunId = 'gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942'
$script:GpuRetirement062106JournalSha256 = '63C7531EADC7D93F56916381CE5B8502D94412D2A4FF6C5550C2CDEBCCCAA4F8'
$script:GpuRetirement062106JournalBytes = 50987L
$script:GpuRetirement062106EntryJournalSha256 = '26B4C9D657AE65DFD007073C4E5F99A1E620720774BEFDDBB398DE7E3F8E9A80'
$script:GpuRetirement062106EntryJournalBytes = 44133L
$script:GpuRetirement062106ArchiveSha256 = '4510690FAD83991612344C958E26C334F19850F71D54B6D51669F7EF94AAB484'
$script:GpuRetirement062106ArchiveBytes = 533L
$script:GpuRetirement062106SourceCommit = 'c44bb52dcb38179f5d026b7331b848c114ed649e'
$script:GpuRetirement062106ProbeSha256 = 'E7FEA1D8436416B0ADC392F03A22A4B510D4CE5DF3308B49DD65D9D6E2432E1A'
$script:GpuRetirement062106ProbeBytes = 172544L
$script:GpuRetirement062106ReferenceLogSha256 = 'F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8'
$script:GpuRetirement062106ReferenceLogBytes = 40838L
$script:GpuRetirement062106ReferenceLogRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942\actions\20261021T062522426Z-Backup-4d82912e15464713b6f63f1ace6ad3fb\tcglog.ib'
$script:GpuRetirement062106OriginalFirmwareSha256 = '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'
$script:GpuRetirement062106OriginalFirmwareBytes = 4336L
$script:GpuRetirement062106ReviewedFirmwareSha256 = 'BC104DF2EBB63636540220FC2E085980F81AEA595B1EC77FE288116A245FA661'
$script:GpuRetirement062106ReviewedFirmwareBytes = 4336L
$script:GpuRetirement062106OriginalFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942\actions\20261021T062522426Z-Backup-4d82912e15464713b6f63f1ace6ad3fb\firmware-enumeration-backup-1648399929574818b6bfb740f9e80877.stdout.txt'
$script:GpuRetirement062106ReviewedFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942\actions\20261021T065114533Z-FinalVerify-6c332fa152e44a55925c801705f052cd\firmware-enumeration-final-cb55a93e13d648ae97e1f62bebc01a8b.stdout.txt'
$script:GpuRetirement062106FreshFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942\actions\20261021T071348678Z-FinalVerify-db71f535efb449adb46c37b873d307f3\fw-review\final-firmware-review-6ee605a8be1349cfbdd6666ee3b43ac6.stdout.txt'
$script:GpuRetirement062106ReviewReceiptRelativePath = '.analysis\gpu-final-firmware-review\faf2a0ad848b4ae38afe03ca419c978f\result.json'
$script:GpuRetirement062106ReviewReceiptSha256 = '881BBD51DDE22261EA59E69CF2DD3E143C0952F5DA06A2F88AA6307F051EC399'
$script:GpuRetirement062106ReviewReceiptBytes = 17633L
$script:GpuRetirement062106FinalActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942\actions\20261021T071348678Z-FinalVerify-db71f535efb449adb46c37b873d307f3'
$script:GpuRetirement062106FinalResultRelativePath = $script:GpuRetirement062106FinalActionRelativePath + '\result.json'
$script:GpuRetirement062106FinalResultSha256 = 'ADEFB945CD165A1098B03E796C566FEEC06FE933AB0A8DCD1893A977F9DE3720'
$script:GpuRetirement062106FinalResultBytes = 9196L
$script:GpuRetirement062106FailedActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942\actions\20261021T065114533Z-FinalVerify-6c332fa152e44a55925c801705f052cd'
$script:GpuRetirement062106FailedResultRelativePath = $script:GpuRetirement062106FailedActionRelativePath + '\result.json'
$script:GpuRetirement062106FailedResultSha256 = 'C426FEEF8E70136E527AAE40B233CAC9281681F7C3A5113DBA062DDD4F49CC49'
$script:GpuRetirement062106FailedResultBytes = 2285L
$script:GpuRetirement062106EntryJournalRelativePath = $script:GpuRetirement062106FinalActionRelativePath + '\reviewed-firmware-entry-journal.json'
$script:GpuRetirement062106RawCaptureSha256 = '485A498C520A7A32E8C0E06C20D599F060F7178C4A3630E02D755599A92163DD'
$script:GpuRetirement062106RawCaptureBytes = 259L
$script:GpuRetirement062106BootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
$script:GpuRetirement062106HistoricalPlatformSha256 = '6EEC7FAB2A3DD095215B85781A3273A87B026ED8E3DF647225C40225E332336E'
$script:GpuRetirement062106HistoricalPlatformBytes = 111703L
$script:GpuRetirement062106CurrentPlatformSha256 = '719E71388A10C72504C6DF305FB393FF1434B029E74665FA511E52A8D781E527'
$script:GpuRetirement062106CurrentPlatformBytes = 111780L
$script:GpuRetirement062106ReviewHelperSha256 = 'C71DCB2C246C1A151422733C5B29D8297D7B1CD9E498EEBA42E2952434880DCE'
$script:GpuRetirement062106ReviewHelperBytes = 72322L
$script:GpuRetirement062106StateSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
$script:GpuRetirement062106StateBytes = 19843L
$script:GpuRetirement062106HistoricalRetirementSha256 = 'D40F76D333DCAB0F3475D02CB12343F38629E0D12F12303A6FC5C85B14A931FD'
$script:GpuRetirement062106HistoricalRetirementBytes = 246612L
$script:GpuRetirement062106InspectorSha256 = '082F77481AC494E81B9A74BD5BE4596F0439FFF27E3FD0D23F9FEE597B4C95B2'
$script:GpuRetirement062106InspectorBytes = 40444L
$script:GpuRetirement062106FirmwareReviewDirectoryRelativePath = '.analysis\gpu-final-firmware-review\faf2a0ad848b4ae38afe03ca419c978f'
$script:GpuRetirement062106ReviewBeforeIntegrityReceiptLeaf = 'before-bootmgr-integrity-1a3e553899c84c17b1d6cbcc20068760.receipt.json'
$script:GpuRetirement062106ReviewAfterIntegrityReceiptLeaf = 'after-bootmgr-integrity-c549f84d121240fb96f66f07ec82b49c.receipt.json'
$script:GpuRetirement062106FinalBeforeIntegrityReceiptLeaf = 'before-bootmgr-integrity-82ccc8dfbc114645bd9cd3b0a98a7f4e.receipt.json'
$script:GpuRetirement062106FinalAfterIntegrityReceiptLeaf = 'after-bootmgr-integrity-2ba4e4c1d0974c6c8ddc32c1cdbdf8e9.receipt.json'
$script:GpuRetirement062106CollectionActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942\actions\20261021T063719827Z-Collect-9b0f9f6d4ebb43b182c4409fc68c8c39'
$script:GpuRetirement062106DisarmedBootId = '2026-10-21T06:36:00.5000000Z'
$script:GpuRetirement062106CurrentBootId = '2026-10-21T06:49:32.5000000Z'
$script:GpuRetirement062106EspVolume = '\\?\Volume{187bc913-7576-4e6e-8d54-18eeb5c38fcd}\'
$script:GpuRetirement062106EspPartitionGuid = '187BC913-7576-4E6E-8D54-18EEB5C38FCD'
$script:GpuRetirement062106EspDiskNumber = 0
$script:GpuRetirement062106EspDiskUniqueId = 'eui.0000000001000000E4D25CDF32275501'
$script:GpuRetirement062106BootManagerBytes = 3086848L
$script:GpuRetirement062106OriginalFinalResultStatus = 'FINAL_VERIFY_FAILED_GPU_UNATTESTED'
$script:GpuRetirement062106FinalResultStatus = 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED'
$script:GpuRetirement062106StagePins = @(
    [pscustomobject]@{ Name='shimx64.efi'; Sha256='6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B'; Bytes=966664L },
    [pscustomobject]@{ Name='grubx64.efi'; Sha256='A20359636F3AF22F5984163E2FBCC57B36C9C79E142CA4877F57674347B42052'; Bytes=72552L },
    [pscustomobject]@{ Name='tcglog.ib'; Sha256='F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8'; Bytes=40838L },
    [pscustomobject]@{ Name='payload.efi'; Sha256='B64E82DCB410103EE5E475F0118B2785946CCACB4ACCB2A54C38831D5F2C1C69'; Bytes=191336L },
    [pscustomobject]@{ Name='manifest.sha256'; Sha256='97DC939CA43E666EA72DE162FE909A153D2C3AB8D469BC2CA056E732FD566EE2'; Bytes=394L }
)
$script:GpuRetirement062106ProfileSelected = $false
$script:GpuRetirement160027RunId = 'gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff'
$script:GpuRetirement160027JournalSha256 = '0978A1DEC6A586D08844CDD1C52D9E8382B603363FA6E15C1581EA8127ABAE83'
$script:GpuRetirement160027JournalBytes = 50987L
$script:GpuRetirement160027EntryJournalSha256 = 'A33ABD21F2D2BA369D4160644BC6305A51E747A04CE4A0DDA1200C80AF90EB22'
$script:GpuRetirement160027EntryJournalBytes = 44133L
$script:GpuRetirement160027ArchiveSha256 = '4A0E31FA7F4773F7191A70EA151BB41FCA8363042E8F289620156337F1ABD930'
$script:GpuRetirement160027ArchiveBytes = 533L
$script:GpuRetirement160027ReadinessSha256 = '695418F3B16F6B89375237AB611DBF74DC14A0EF8CE762493E031ECC322E139F'
$script:GpuRetirement160027ReadinessBytes = 11022L
$script:GpuRetirement160027SourceCommit = '1be532253d6c6cc428ae2d4588a4c6e7aacadb3e'
$script:GpuRetirement160027ProbeSha256 = '56B204225F39A93F324AE340B985268130405845B4745723CA8DE0A8B544DC4A'
$script:GpuRetirement160027ProbeBytes = 172544L
$script:GpuRetirement160027ProbeRelativePath = '.codex-build\gb\990e6d2305c55fff\l\ib-tcg-dump.exe'
$script:GpuRetirement160027ReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
$script:GpuRetirement160027ReferenceLogBytes = 40838L
$script:GpuRetirement160027ReferenceLogRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff\actions\20261021T160724349Z-Backup-f7bd830785b14473beca1c18fc068a62\tcglog.ib'
$script:GpuRetirement160027OriginalFirmwareSha256 = 'BC104DF2EBB63636540220FC2E085980F81AEA595B1EC77FE288116A245FA661'
$script:GpuRetirement160027ReviewedFirmwareSha256 = '98E5ECE5B987B754CD1C059C34E9D87A1CFB569E758BCC21971CA21D4EDC2D80'
$script:GpuRetirement160027FirmwareBytes = 4336L
$script:GpuRetirement160027OriginalFirmwareBytes = 4336L
$script:GpuRetirement160027ReviewedFirmwareBytes = 4336L
$script:GpuRetirement160027OriginalFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff\actions\20261021T160724349Z-Backup-f7bd830785b14473beca1c18fc068a62\firmware-enumeration-backup-2010399215fd4ea7885594dcce487c3c.stdout.txt'
$script:GpuRetirement160027ReviewedFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff\actions\20261021T165416133Z-FinalVerify-33a9f87568214ffbb7342a18fcfec7cc\firmware-enumeration-final-a390b4a605494d33b673ea17303c26d5.stdout.txt'
$script:GpuRetirement160027FinalFirmwareRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff\actions\20261021T191509538Z-FinalVerify-5f519cfe082b43f1a9e6b2ffb7f96433\fw-review\final-firmware-review-a2cf89e82e3e43b3b28464b9f4a3ef02.stdout.txt'
$script:GpuRetirement160027ReviewReceiptRelativePath = '.analysis\gpu-final-firmware-review\2933898a57284570b022635a3398a3a4\result.json'
$script:GpuRetirement160027ReviewReceiptSha256 = 'EAA79DCFA55C09CEAD703A07E8A4C02CAF2A8C6992EEF006B4891BB8ADF2CBF6'
$script:GpuRetirement160027ReviewReceiptBytes = 17633L
$script:GpuRetirement160027FinalActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff\actions\20261021T191509538Z-FinalVerify-5f519cfe082b43f1a9e6b2ffb7f96433'
$script:GpuRetirement160027FinalResultRelativePath = $script:GpuRetirement160027FinalActionRelativePath + '\result.json'
$script:GpuRetirement160027FinalResultSha256 = '8A3C0241C969C1E154973A981AA3A6E9561F975375292D2D3D42C032B9159841'
$script:GpuRetirement160027FinalResultBytes = 9196L
$script:GpuRetirement160027FailedActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff\actions\20261021T165416133Z-FinalVerify-33a9f87568214ffbb7342a18fcfec7cc'
$script:GpuRetirement160027FailedResultRelativePath = $script:GpuRetirement160027FailedActionRelativePath + '\result.json'
$script:GpuRetirement160027FailedResultSha256 = '8D29D4BB908AC72C9ADB3ADC3DE9C5AC58B5ABC99AEF1073FA25AE91A7695C31'
$script:GpuRetirement160027FailedResultBytes = 2285L
$script:GpuRetirement160027EntryJournalRelativePath = $script:GpuRetirement160027FinalActionRelativePath + '\reviewed-firmware-entry-journal.json'
$script:GpuRetirement160027RawCaptureSha256 = '69733AFE9DD782577F302F79A788DF2745BB9C9CD4BE860DF44C59A07EB4E066'
$script:GpuRetirement160027RawCaptureBytes = 259L
$script:GpuRetirement160027BootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
$script:GpuRetirement160027StateSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
$script:GpuRetirement160027StateBytes = 19843L
$script:GpuRetirement160027HistoricalPlatformSha256 = '719E71388A10C72504C6DF305FB393FF1434B029E74665FA511E52A8D781E527'
$script:GpuRetirement160027HistoricalPlatformBytes = 111780L
$script:GpuRetirement160027JournalPlatformSha256 = '719E71388A10C72504C6DF305FB393FF1434B029E74665FA511E52A8D781E527'
$script:GpuRetirement160027JournalPlatformBytes = 111780L
$script:GpuRetirement160027ReceiptPlatformSha256 = 'C1DF37B21E0AB32C0E56D00D577B15463AC3705F4B08DE8E1D5EF85561BDD34E'
$script:GpuRetirement160027ReceiptPlatformBytes = 111822L
$script:GpuRetirement160027ReceiptRetirementSha256 = '78C8CE570A1DB3135BC39E3F9961D182AD9D9F346D73787414AB89639E76ADCF'
$script:GpuRetirement160027ReceiptRetirementBytes = 308879L
$script:GpuRetirement160027HistoricalRetirementSha256 = '78C8CE570A1DB3135BC39E3F9961D182AD9D9F346D73787414AB89639E76ADCF'
$script:GpuRetirement160027HistoricalRetirementBytes = 308879L
$script:GpuRetirement160027InspectorSha256 = '5D30A706D26775A7007C430EF25C1F22BDA32D521735B2780CD2233170015846'
$script:GpuRetirement160027InspectorBytes = 43642L
$script:GpuRetirement160027CurrentPlatformSha256 = '43AA4F580F6D896068D8D4DE4139A509CC19254647D400DA5803115E5957D7FE'
$script:GpuRetirement160027CurrentPlatformBytes = 111899L
$script:GpuRetirement160027ReviewHelperSha256 = '1983F6C9F8D2580E5760E32F9F0358762B87E7A98D7847C38F8AA225BDCAAC05'
$script:GpuRetirement160027ReviewHelperBytes = 77050L
$script:GpuRetirement160027ReviewDirectoryRelativePath = '.analysis\gpu-final-firmware-review\2933898a57284570b022635a3398a3a4'
$script:GpuRetirement160027FirmwareReviewDirectoryRelativePath = $script:GpuRetirement160027ReviewDirectoryRelativePath
$script:GpuRetirement160027FreshFirmwareRelativePath = $script:GpuRetirement160027FinalFirmwareRelativePath
$script:GpuRetirement160027ReviewBeforeIntegrityReceiptLeaf = 'before-bootmgr-integrity-fe15635ee2154ae5bd56bf1750c95ded.receipt.json'
$script:GpuRetirement160027ReviewAfterIntegrityReceiptLeaf = 'after-bootmgr-integrity-6f982dc6684b49d58f4076601821c495.receipt.json'
$script:GpuRetirement160027FinalBeforeIntegrityReceiptLeaf = 'before-bootmgr-integrity-0e6acde801464b57a2a9750a8bdf6081.receipt.json'
$script:GpuRetirement160027FinalAfterIntegrityReceiptLeaf = 'after-bootmgr-integrity-a31f2a8e0eac42ed871be167295a2c94.receipt.json'
$script:GpuRetirement160027CollectionActionRelativePath = '.analysis\gpu-baremetal\runs\gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff\actions\20261021T161733245Z-Collect-80aa8b0ab7e14aa5b481f6a590b9ca89'
$script:GpuRetirement160027DisarmedBootId = '2026-10-21T16:14:38.5000000Z'
$script:GpuRetirement160027CurrentBootId = '2026-10-21T16:52:47.5000000Z'
$script:GpuRetirement160027EspVolume = '\\?\Volume{187bc913-7576-4e6e-8d54-18eeb5c38fcd}\'
$script:GpuRetirement160027EspPartitionGuid = '187BC913-7576-4E6E-8D54-18EEB5C38FCD'
$script:GpuRetirement160027EspDiskNumber = 0
$script:GpuRetirement160027EspDiskUniqueId = 'eui.0000000001000000E4D25CDF32275501'
$script:GpuRetirement160027BootManagerBytes = 3086848L
$script:GpuRetirement160027OriginalFinalResultStatus = 'FINAL_VERIFY_FAILED_GPU_UNATTESTED'
$script:GpuRetirement160027FinalResultStatus = 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED'
$script:GpuRetirement160027StagePins = @(
    [pscustomobject]@{ Name='shimx64.efi'; Sha256='6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B'; Bytes=966664L },
    [pscustomobject]@{ Name='grubx64.efi'; Sha256='FBE3015D1B8643647874582855C8E4514AD5991CB61F6A248CABC41251ED821A'; Bytes=77160L },
    [pscustomobject]@{ Name='tcglog.ib'; Sha256='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'; Bytes=40838L },
    [pscustomobject]@{ Name='payload.efi'; Sha256='4C82E477A7015893644DBEA6B260ADA155A1A3A18900DF95AB58C7DB50BD65A2'; Bytes=191336L },
    [pscustomobject]@{ Name='manifest.sha256'; Sha256='647A3D5B79994437BB94C0E680A695B06340ABE030998CCD050F1EAA7F8EEB1E'; Bytes=394L }
)
$script:GpuRetirement160027ProfileSelected = $false
$script:GpuRetirementSelectedReadinessPath = $null
$script:GpuRetirementProfile = '150318'

function Get-GpuStageRetirementPin {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $attributes = [IO.File]::GetAttributes($fullPath)
    if (($attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
        ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Expected a regular non-reparse file: $fullPath"
    }
    $stream = New-Object IO.FileStream($fullPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $length = [long]$stream.Length
        $hash = ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
    return [pscustomobject]@{ Path = $fullPath; Sha256 = $hash; Bytes = $length }
}

function Assert-GpuStageRetirementPin {
    param([Parameter(Mandatory = $true)][object]$Pin)

    if ($null -eq $Pin -or $null -eq $Pin.PSObject.Properties['Path'] -or
        [string]$Pin.Sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw 'A file pin is malformed.'
    }
    $expectedBytes = 0L
    if (-not [long]::TryParse([string]$Pin.Bytes, [ref]$expectedBytes) -or $expectedBytes -lt 0) {
        throw 'A file pin has an invalid byte count.'
    }
    $actual = Get-GpuStageRetirementPin -Path ([string]$Pin.Path)
    if ($actual.Bytes -ne $expectedBytes -or
        -not [string]::Equals($actual.Sha256, [string]$Pin.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw "A reviewed evidence file changed: $($Pin.Path)"
    }
    return $actual
}

function Get-GpuRetirementProperty {
    param([AllowNull()][object]$Object, [Parameter(Mandatory = $true)][string]$Name)
    if ($null -eq $Object -or $null -eq $Object.PSObject.Properties[$Name]) { return $null }
    return $Object.PSObject.Properties[$Name].Value
}

function Assert-GpuRetirementEqual {
    param([string]$Expected, [string]$Actual, [string]$Description)
    if (-not [string]::Equals($Expected, $Actual, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description does not match the reviewed retirement target."
    }
}

function Assert-GpuRetirementSafeWorkspaceFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot
    )

    $root = [IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $full = [IO.Path]::GetFullPath($Path)
    $prefix = $root + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Reviewed evidence path is outside the workspace.'
    }
    if (-not [IO.Directory]::Exists($root)) { throw 'Workspace root is missing.' }
    $rootAttributes = [IO.File]::GetAttributes($root)
    if (($rootAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        ($rootAttributes -band [IO.FileAttributes]::Directory) -eq 0) {
        throw 'Workspace root is not a regular directory.'
    }

    $relative = $full.Substring($prefix.Length)
    $segments = @($relative -split '[\\/]')
    if ($segments.Count -lt 2 -or @($segments | Where-Object { [string]::IsNullOrWhiteSpace($_) -or $_ -in @('.', '..') }).Count -gt 0) {
        throw 'Reviewed evidence path is malformed.'
    }
    $cursor = $root
    for ($index = 0; $index -lt $segments.Count; $index++) {
        $cursor = Join-Path $cursor $segments[$index]
        if (-not [IO.File]::Exists($cursor) -and -not [IO.Directory]::Exists($cursor)) {
            throw "Reviewed evidence path is missing: $cursor"
        }
        $attributes = [IO.File]::GetAttributes($cursor)
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reviewed evidence path contains a reparse point: $cursor"
        }
        $isLast = ($index -eq ($segments.Count - 1))
        if ($isLast -and ($attributes -band [IO.FileAttributes]::Directory) -ne 0) {
            throw "Reviewed evidence is not a regular file: $cursor"
        }
        if (-not $isLast -and ($attributes -band [IO.FileAttributes]::Directory) -eq 0) {
            throw "Reviewed evidence parent is not a directory: $cursor"
        }
    }
    return $full
}

function Assert-GpuRetirementProfilePin {
    param(
        [Parameter(Mandatory = $true)][object]$Pin,
        [Parameter(Mandatory = $true)][string]$ExpectedPath,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256,
        [Parameter(Mandatory = $true)][long]$ExpectedBytes,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($null -eq $Pin -or $null -eq $Pin.PSObject.Properties['Path']) { throw "$Description pin is missing." }
    $actualPath = Assert-GpuRetirementSafeWorkspaceFile -Path ([string]$Pin.Path) -WorkspaceRoot $WorkspaceRoot
    $expectedFullPath = [IO.Path]::GetFullPath($ExpectedPath)
    Assert-GpuRetirementEqual -Expected $expectedFullPath -Actual $actualPath -Description "$Description path"
    if ([string]$Pin.Sha256 -cne $ExpectedSha256 -or [long]$Pin.Bytes -ne $ExpectedBytes) {
        throw "$Description pin differs from its fixed reviewed value."
    }
    $actual = Get-GpuStageRetirementPin -Path $actualPath
    if ($actual.Sha256 -cne $ExpectedSha256 -or $actual.Bytes -ne $ExpectedBytes) {
        throw "$Description file differs from its fixed reviewed value."
    }
    return $actual
}

function Assert-GpuStageRetirement201140Proof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $runDirectory = Join-Path (Join-Path $workspace '.analysis\gpu-baremetal\runs') $script:GpuRetirement201140RunId
    $last = $Journal.LastAction
    $verification = $Journal.FinalVerification
    $enumeration = $verification.FirmwareEnumeration
    $review = $enumeration.ReviewedDifference
    if ([string]$Journal.RunId -cne $script:GpuRetirement201140RunId -or
        [IO.Path]::GetFullPath([string]$Journal.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [string]$Journal.SourceCommit -cne $script:GpuRetirement201140SourceCommit -or
        [string]$Journal.Phase -cne 'Complete' -or $null -ne $Journal.InProgress -or
        $null -eq $last -or [string]$last.Action -cne 'FinalVerify' -or $last.Succeeded -ne $true -or
        [string]$last.Status -cne 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED') {
        throw 'The 201140 journal lacks the exact successful reviewed FinalVerify action.'
    }
    if ($null -eq $verification -or $verification.ResetBoundaryPassed -ne $true -or
        $verification.ActivationAttempted -ne $true -or $verification.SecureBoot -ne $true -or
        $verification.ActiveGpuProof -ne $false -or [string]$verification.GpuAttestation -cne 'UNATTESTED' -or
        @($verification.Errors).Count -ne 0 -or $null -eq $verification.Health -or
        $verification.Health.Passed -ne $true -or [string]$verification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$verification.Health.Aggregate -cne 'ABSENT' -or
        [int]$verification.Health.CpuCount -ne 20 -or [int]$verification.Health.ExpectedCpus -ne 20 -or
        $null -eq $verification.Esp -or $verification.Esp.MatchesBaseline -ne $true) {
        throw 'The 201140 final verification does not satisfy the reviewed reset, ABSENT health, Secure Boot, ESP, and unattested-GPU gates.'
    }
    if ($null -eq $enumeration -or $enumeration.TextEqual -ne $false -or
        $enumeration.ReviewedDifferenceAccepted -ne $true -or
        [string]$enumeration.BaselineSha256 -cne $script:GpuRetirement201140OriginalFirmwareSha256 -or
        [string]$enumeration.CurrentSha256 -cne $script:GpuRetirement201140CurrentFirmwareSha256 -or
        [string]$enumeration.CaptureContext -cne 'SystemEspMountedAtS' -or
        $null -eq $review -or $review.Accepted -ne $true -or
        $review.HistoricalRawBootOrderUnchangedProven -ne $false -or
        $review.FullBootVariableNamespaceAttested -ne $false) {
        throw 'The journal does not contain the exact accepted 201140 firmware-difference review.'
    }

    $reviewReceiptPath = Join-Path $workspace $script:GpuRetirement201140ReviewReceiptRelativePath
    $reviewReceiptPin = Assert-GpuRetirementProfilePin -Pin $review.ReceiptPin -ExpectedPath $reviewReceiptPath `
        -ExpectedSha256 $script:GpuRetirement201140ReviewReceiptSha256 -ExpectedBytes $script:GpuRetirement201140ReviewReceiptBytes `
        -WorkspaceRoot $workspace -Description 'Read-only firmware review receipt'
    $reviewReceipt = Get-Content -LiteralPath $reviewReceiptPin.Path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    if ($reviewReceipt.SchemaVersion -ne 1 -or [string]$reviewReceipt.Status -cne 'READONLY_FIRMWARE_REVIEW_CAPTURED' -or
        [string]$reviewReceipt.Profile -cne '042142' -or
        $reviewReceipt.Succeeded -ne $true -or $reviewReceipt.RecoveryCompleted -ne $false -or
        $reviewReceipt.SelectorAndJournalUnchanged -ne $true -or $reviewReceipt.EspFilesChanged -ne $false -or
        $reviewReceipt.FirmwareWritten -ne $false -or $reviewReceipt.CleanupSucceeded -ne $true -or
        $reviewReceipt.EspUnmounted -ne $true -or $reviewReceipt.RawBootSelectionStable -ne $true -or
        $reviewReceipt.FirmwareTextStable -ne $true -or
        $reviewReceipt.HistoricalRawBootOrderUnchangedProven -ne $false -or
        @($reviewReceipt.CleanupErrors).Count -ne 0 -or @($reviewReceipt.Errors).Count -ne 0 -or
        $null -eq $reviewReceipt.CurrentHost -or $reviewReceipt.CurrentHost.SecureBoot -ne $true -or
        [int]$reviewReceipt.CurrentHost.LogicalCpus -ne 20 -or
        [string]$reviewReceipt.CurrentHost.BootId -cne [string]$verification.BootId -or
        $null -eq $reviewReceipt.Esp -or
        [string]$reviewReceipt.Esp.Volume -cne [string]$Journal.Backup.EspVolume -or
        [int]$reviewReceipt.Esp.DiskNumber -ne [int]$Journal.Backup.EspDiskNumber -or
        [string]$reviewReceipt.Esp.PartitionGuid -cne [string]$Journal.Backup.EspPartitionGuid -or
        [string]$reviewReceipt.Esp.BootManagerSha256 -cne $script:GpuRetirement201140BootManagerSha256) {
        throw 'The pinned read-only firmware review receipt does not prove its required host, ESP, cleanup, and raw stability observations.'
    }
    $receiptRawCaptures = @($reviewReceipt.BootSelectionCaptures)
    if ($receiptRawCaptures.Count -ne 2 -or @($reviewReceipt.FirmwareObservations).Count -ne 2) {
        throw 'The read-only firmware review receipt must contain two raw captures and two stable firmware observations.'
    }

    $backupFirmware = $Journal.Backup.FirmwareEnumeration
    if ($null -eq $backupFirmware) { throw 'The original firmware baseline pin is missing from the 201140 journal.' }
    $expectedOriginalPath = Join-Path $workspace $script:GpuRetirement201140OriginalFirmwareRelativePath
    if ([IO.Path]::GetFullPath([string]$backupFirmware.Path) -cne [IO.Path]::GetFullPath($expectedOriginalPath)) {
        throw 'The archived original firmware baseline path differs from the fixed 201140 Backup action.'
    }
    $originalFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.OriginalBaselinePin -ExpectedPath $expectedOriginalPath `
        -ExpectedSha256 $script:GpuRetirement201140OriginalFirmwareSha256 -ExpectedBytes $script:GpuRetirement201140FirmwareBytes `
        -WorkspaceRoot $workspace -Description 'Original backup firmware baseline'
    if ([string]$backupFirmware.Sha256 -cne $script:GpuRetirement201140OriginalFirmwareSha256 -or
        [long]$backupFirmware.Bytes -ne $script:GpuRetirement201140FirmwareBytes) {
        throw 'The archived original firmware baseline no longer has its reviewed ECB8 pin.'
    }
    $reviewedFirmwarePath = Join-Path $workspace $script:GpuRetirement201140ReviewedFirmwareRelativePath
    $reviewedFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.ReviewedFirmwarePin -ExpectedPath $reviewedFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement201140CurrentFirmwareSha256 -ExpectedBytes $script:GpuRetirement201140FirmwareBytes `
        -WorkspaceRoot $workspace -Description 'First observed changed firmware capture'
    $freshFirmwarePath = Join-Path $workspace $script:GpuRetirement201140FreshFirmwareRelativePath
    $freshFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.FreshFirmwarePin -ExpectedPath $freshFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement201140CurrentFirmwareSha256 -ExpectedBytes $script:GpuRetirement201140FirmwareBytes `
        -WorkspaceRoot $workspace -Description 'Successful final-review firmware capture'
    if ([string]$Journal.Backup.BootManagerSha256 -cne $script:GpuRetirement201140BootManagerSha256 -or
        [string]$reviewReceipt.Esp.BootManagerSha256 -cne [string]$Journal.Backup.BootManagerSha256) {
        throw 'The review receipt or journal boot-manager pin differs from the fixed reviewed value.'
    }

    $referenceLog = $Journal.Backup.ReferenceLog
    if ($null -eq $referenceLog -or [string]$referenceLog.Sha256 -cne $script:GpuRetirement201140ReferenceLogSha256 -or
        [long]$referenceLog.Bytes -ne $script:GpuRetirement201140ReferenceLogBytes) {
        throw 'The 201140 journal does not preserve the fixed reviewed TPM reference log.'
    }
    $auditToolPath = Join-Path $workspace $script:GpuRetirementAuditToolRelativePath
    $auditToolPin = Get-GpuStageRetirementPin -Path (Assert-GpuRetirementSafeWorkspaceFile -Path $auditToolPath -WorkspaceRoot $workspace)
    if ($auditToolPin.Sha256 -cne $script:GpuRetirementAuditToolSha256 -or $auditToolPin.Bytes -ne $script:GpuRetirementAuditToolBytes) {
        throw 'The fixed read-only firmware audit tool has changed.'
    }

    $referenceLogPath = Join-Path $workspace $script:GpuRetirement201140ReferenceLogRelativePath
    if ([IO.Path]::GetFullPath([string]$referenceLog.Path) -cne [IO.Path]::GetFullPath($referenceLogPath)) {
        throw 'The 201140 TPM reference log path differs from the fixed Backup action.'
    }
    $referenceLogPin = Assert-GpuRetirementProfilePin -Pin $referenceLog -ExpectedPath $referenceLogPath `
        -ExpectedSha256 $script:GpuRetirement201140ReferenceLogSha256 -ExpectedBytes $script:GpuRetirement201140ReferenceLogBytes `
        -WorkspaceRoot $workspace -Description 'Pinned TPM reference log'
    $probePath = Join-Path $workspace $script:GpuRetirement201140ProbeRelativePath
    if ([IO.Path]::GetFullPath([string]$Journal.Tools.Probe.Path) -cne [IO.Path]::GetFullPath($probePath)) {
        throw 'The 201140 health probe path differs from the fixed build output.'
    }
    $probePin = Assert-GpuRetirementProfilePin -Pin $Journal.Tools.Probe -ExpectedPath $probePath `
        -ExpectedSha256 $script:GpuRetirement201140ProbeSha256 -ExpectedBytes $script:GpuRetirement201140ProbeBytes `
        -WorkspaceRoot $workspace -Description 'Pinned health probe'
    $rawCaptures = @($review.RawCaptures)
    if ($rawCaptures.Count -ne 2) { throw 'The accepted review must contain exactly two raw boot-selection captures.' }
    $reviewActionDirectory = Join-Path $workspace $script:GpuRetirement201140ReviewActionRelativePath
    $extraPins = New-Object 'System.Collections.Generic.List[object]'
    foreach ($pin in @($reviewReceiptPin,$originalFirmwarePin,$reviewedFirmwarePin,$freshFirmwarePin,$referenceLogPin,$probePin,$auditToolPin)) { $extraPins.Add($pin) }
    $reviewedFirmwareText = [IO.File]::ReadAllText($freshFirmwarePin.Path)
    if (-not [string]::Equals($reviewedFirmwareText, [IO.File]::ReadAllText($reviewedFirmwarePin.Path), [StringComparison]::Ordinal)) {
        throw 'The pinned firmware captures do not match their expected reviewed content.'
    }

    $inspectionDirectory = Join-Path (Split-Path -Parent $reviewReceiptPin.Path) 'observations'
    $firmwareObservationNames = @('before','after')
    $firmwareObservations = @($reviewReceipt.FirmwareObservations)
    for ($index = 0; $index -lt 2; $index++) {
        $observation = $firmwareObservations[$index]
        $checkpoint = $firmwareObservationNames[$index]
        if ([string]$observation.Checkpoint -cne $checkpoint -or $observation.Succeeded -ne $true -or
            -not [string]::Equals([string]$observation.Text, $reviewedFirmwareText, [StringComparison]::Ordinal)) {
            throw "The read-only $checkpoint firmware observation differs from the exact reviewed final capture."
        }
        $stdoutPin = $observation.Pin
        if ($null -eq $stdoutPin -or [string]$stdoutPin.Sha256 -cne $script:GpuRetirement201140CurrentFirmwareSha256 -or
            [long]$stdoutPin.Bytes -ne $script:GpuRetirement201140FirmwareBytes -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath([string]$stdoutPin.Path)) -cne [IO.Path]::GetFullPath($inspectionDirectory) -or
            [IO.Path]::GetFileName([string]$stdoutPin.Path) -notmatch '^stage-retirement-firmware-[0-9a-f]{32}\.stdout\.txt$') {
            throw "The read-only $checkpoint firmware output pin does not match the fixed review observation directory."
        }
        $stdoutFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path ([string]$stdoutPin.Path) -WorkspaceRoot $workspace
        $checkedStdout = Assert-GpuRetirementProfilePin -Pin $stdoutPin -ExpectedPath $stdoutFullPath `
            -ExpectedSha256 $script:GpuRetirement201140CurrentFirmwareSha256 -ExpectedBytes $script:GpuRetirement201140FirmwareBytes `
            -WorkspaceRoot $workspace -Description "$checkpoint read-only firmware stdout"
        if (-not [string]::Equals([IO.File]::ReadAllText($checkedStdout.Path), [string]$observation.Text, [StringComparison]::Ordinal)) {
            throw "The read-only $checkpoint firmware stdout differs from its pinned observation text."
        }

        $stdoutLeaf = [IO.Path]::GetFileName($checkedStdout.Path)
        $receiptLeaf = $stdoutLeaf.Substring(0, $stdoutLeaf.Length - '.stdout.txt'.Length) + '.receipt.json'
        $expectedReceiptPath = Join-Path $inspectionDirectory $receiptLeaf
        if ([IO.Path]::GetFullPath([string]$observation.ReceiptPath) -cne [IO.Path]::GetFullPath($expectedReceiptPath)) {
            throw "The read-only $checkpoint firmware receipt path differs from its pinned observation."
        }
        $firmwareReceiptPath = Assert-GpuRetirementSafeWorkspaceFile -Path $expectedReceiptPath -WorkspaceRoot $workspace
        $firmwareReceiptPin = Get-GpuStageRetirementPin -Path $firmwareReceiptPath
        $firmwareReceipt = Get-Content -LiteralPath $firmwareReceiptPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        if ($firmwareReceipt.SchemaVersion -ne 1 -or $firmwareReceipt.Succeeded -ne $true -or
            [int]$firmwareReceipt.ExitCode -ne 0 -or @($firmwareReceipt.AllowedExitCodes).Count -ne 1 -or
            [int]$firmwareReceipt.AllowedExitCodes[0] -ne 0 -or $null -ne $firmwareReceipt.LaunchError -or
            [IO.Path]::GetFileName([string]$firmwareReceipt.Program) -ine 'bcdedit.exe' -or
            @($firmwareReceipt.Arguments).Count -ne 3 -or
            [string]$firmwareReceipt.Arguments[0] -cne '/enum' -or
            [string]$firmwareReceipt.Arguments[1] -cne 'firmware' -or
            [string]$firmwareReceipt.Arguments[2] -cne '/v' -or
            [IO.Path]::GetFullPath([string]$firmwareReceipt.StdoutPath) -cne [IO.Path]::GetFullPath($checkedStdout.Path)) {
            throw "The read-only $checkpoint firmware command receipt is unsuccessful or does not match its capture."
        }
        $stderrPath = [string]$firmwareReceipt.StderrPath
        if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stderrPath)) -cne [IO.Path]::GetFullPath($inspectionDirectory) -or
            [IO.Path]::GetFileName($stderrPath) -cne ($stdoutLeaf.Substring(0, $stdoutLeaf.Length - '.stdout.txt'.Length) + '.stderr.txt')) {
            throw "The read-only $checkpoint firmware stderr path differs from its review observation."
        }
        $stderrFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stderrPath -WorkspaceRoot $workspace
        $stderrPin = Get-GpuStageRetirementPin -Path $stderrFullPath
        if ([IO.File]::ReadAllText($stderrFullPath).Length -ne 0) {
            throw "The read-only $checkpoint firmware command wrote unexpected stderr."
        }
        $extraPins.Add($checkedStdout)
        $extraPins.Add($firmwareReceiptPin)
        $extraPins.Add($stderrPin)
    }

    $expectedIntegrity = 'BootmgfwSha256=' + $script:GpuRetirement201140BootManagerSha256 + ' Findings=0x00000000'
    for ($index = 0; $index -lt 2; $index++) {
        $capture = $rawCaptures[$index]
        $checkpoint = if ($index -eq 0) { 'before' } else { 'after' }
        $rawRelativePath = $script:GpuRetirement201140ReviewActionRelativePath + '\' + $checkpoint + '-boot-selection.json'
        $rawPath = Join-Path $workspace $rawRelativePath
        $rawPin = Assert-GpuRetirementProfilePin -Pin $capture.BaselinePin -ExpectedPath $rawPath `
            -ExpectedSha256 $script:GpuRetirement201140RawCaptureSha256 -ExpectedBytes $script:GpuRetirement201140RawCaptureBytes `
            -WorkspaceRoot $workspace -Description "$checkpoint raw boot-selection capture"
        if ([IO.Path]::GetFullPath([string]$capture.BaselinePath) -cne [IO.Path]::GetFullPath($rawPath)) {
            throw "$checkpoint raw capture path does not match the closed profile."
        }
        $rawDocument = Get-Content -LiteralPath $rawPin.Path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $baseline = $capture.Baseline
        $order = @($baseline.BootOrder)
        if ($null -eq $baseline -or $null -eq $baseline.PSObject.Properties['Schema'] -or
            $null -eq $baseline.PSObject.Properties['BootNext'] -or $null -eq $baseline.PSObject.Properties['BootOrder'] -or
            $null -eq $baseline.PSObject.Properties['BootManagerSha256'] -or $null -eq $baseline.PSObject.Properties['ReferenceLogSha256'] -or
            $null -eq $baseline.PSObject.Properties['IntegrityFindings'] -or
            $null -eq $rawDocument.PSObject.Properties['boot_next'] -or $null -eq $rawDocument.PSObject.Properties['boot_order'] -or
            $null -eq $rawDocument.PSObject.Properties['bootmgfw_sha256'] -or $null -eq $rawDocument.PSObject.Properties['log_sha256'] -or
            $null -eq $rawDocument.PSObject.Properties['findings'] -or
            $baseline.Schema -ne 1 -or $baseline.BootNext -ne $null -or
            $order.Count -ne 6 -or @($rawDocument.boot_order).Count -ne 6 -or
            $null -ne $rawDocument.boot_next -or $rawDocument.schema -ne 1 -or
            [string]$rawDocument.kind -cne 'boot-selection' -or [int]$rawDocument.findings -ne 0 -or
            [string]$rawDocument.bootmgfw_sha256 -cne $script:GpuRetirement201140BootManagerSha256 -or
            [string]$rawDocument.log_sha256 -cne $script:GpuRetirement201140ReferenceLogSha256 -or
            [string]$baseline.BootManagerSha256 -cne $script:GpuRetirement201140BootManagerSha256 -or
            [string]$baseline.ReferenceLogSha256 -cne $script:GpuRetirement201140ReferenceLogSha256 -or
            [string]$baseline.IntegrityFindings -cne '00000000' -or
            -not [string]::Equals([string]$capture.IntegrityOutput, $expectedIntegrity, [StringComparison]::Ordinal)) {
            throw "$checkpoint raw boot-selection capture lacks the fixed BootNext, BootOrder, boot-manager, reference-log, or integrity proof."
        }
        for ($orderIndex = 0; $orderIndex -lt 6; $orderIndex++) {
            if ([int]$order[$orderIndex] -ne $orderIndex -or [int]$rawDocument.boot_order[$orderIndex] -ne $orderIndex) {
                throw "$checkpoint raw capture BootOrder differs from the fixed reviewed sequence 0 through 5."
            }
        }
        $extraPins.Add($rawPin)

        $receiptLeaf = if ($index -eq 0) { 'before-bootmgr-integrity-c41a875847f8486587886b4862e35f7d' } else { 'after-bootmgr-integrity-8cd4ff8e1bc64012a73dfcee197557d3' }
        $receiptPath = Join-Path $reviewActionDirectory ($receiptLeaf + '.receipt.json')
        if ([IO.Path]::GetFullPath([string]$capture.IntegrityReceipt) -cne [IO.Path]::GetFullPath($receiptPath)) {
            throw "$checkpoint integrity receipt path does not match the successful final review record."
        }
        $receiptFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $receiptPath -WorkspaceRoot $workspace
        $integrityReceiptPin = Get-GpuStageRetirementPin -Path $receiptFullPath
        $integrityReceipt = Get-Content -LiteralPath $receiptFullPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $expectedArguments = @('bootmgr-integrity','--expected-hash',$script:GpuRetirement201140BootManagerSha256,'--esp-root','S:\')
        if ($integrityReceipt.SchemaVersion -ne 1 -or $integrityReceipt.Succeeded -ne $true -or
            [int]$integrityReceipt.ExitCode -ne 0 -or @($integrityReceipt.AllowedExitCodes).Count -ne 1 -or
            [int]$integrityReceipt.AllowedExitCodes[0] -ne 0 -or $null -ne $integrityReceipt.LaunchError -or
            [string]$integrityReceipt.Program -cne [IO.Path]::GetFullPath($auditToolPath) -or
            @($integrityReceipt.Arguments).Count -ne $expectedArguments.Count) {
            throw "$checkpoint integrity command receipt is incomplete or unsuccessful."
        }
        for ($argumentIndex = 0; $argumentIndex -lt $expectedArguments.Count; $argumentIndex++) {
            if ([string]$integrityReceipt.Arguments[$argumentIndex] -cne $expectedArguments[$argumentIndex]) {
                throw "$checkpoint integrity command receipt has unexpected arguments."
            }
        }
        $stdoutPath = [string]$integrityReceipt.StdoutPath
        $stderrPath = [string]$integrityReceipt.StderrPath
        $expectedOutputLeaf = $receiptLeaf + '.stdout.txt'
        $expectedErrorLeaf = $receiptLeaf + '.stderr.txt'
        if ([IO.Path]::GetFileName($stdoutPath) -cne $expectedOutputLeaf -or
            [IO.Path]::GetFileName($stderrPath) -cne $expectedErrorLeaf -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stdoutPath)) -cne [IO.Path]::GetDirectoryName($receiptFullPath) -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stderrPath)) -cne [IO.Path]::GetDirectoryName($receiptFullPath)) {
            throw "$checkpoint integrity output paths do not match the fixed review observation directory."
        }
        $stdoutFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stdoutPath -WorkspaceRoot $workspace
        $stderrFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stderrPath -WorkspaceRoot $workspace
        $stdoutPin = Get-GpuStageRetirementPin -Path $stdoutFullPath
        $stderrPin = Get-GpuStageRetirementPin -Path $stderrFullPath
        if (-not [string]::Equals(([IO.File]::ReadAllText($stdoutFullPath)).Trim(), $expectedIntegrity, [StringComparison]::Ordinal) -or
            [IO.File]::ReadAllText($stderrFullPath).Length -ne 0) {
            throw "$checkpoint integrity stdout/stderr differ from the successful reviewed result."
        }
        $extraPins.Add($integrityReceiptPin)
        $extraPins.Add($stdoutPin)
        $extraPins.Add($stderrPin)
    }

    # The pinned read-only review receipt cites a second pair of raw captures.
    # Validate those exact files too; their directory and integrity receipts are
    # distinct from the successful FinalVerify action captures above.
    $inspectionReceiptLeaves = @('before-bootmgr-integrity-2bcf37ea725f4989ac915a7f3f125ac7','after-bootmgr-integrity-89d06c81ce664fb198fc3bef47f784eb')
    for ($index = 0; $index -lt 2; $index++) {
        $capture = $receiptRawCaptures[$index]
        $checkpoint = if ($index -eq 0) { 'before' } else { 'after' }
        $rawPath = Join-Path $inspectionDirectory ($checkpoint + '-boot-selection.json')
        $rawPin = Assert-GpuRetirementProfilePin -Pin $capture.BaselinePin -ExpectedPath $rawPath `
            -ExpectedSha256 $script:GpuRetirement201140RawCaptureSha256 -ExpectedBytes $script:GpuRetirement201140RawCaptureBytes `
            -WorkspaceRoot $workspace -Description "$checkpoint inspection raw boot-selection capture"
        if ([IO.Path]::GetFullPath([string]$capture.BaselinePath) -cne [IO.Path]::GetFullPath($rawPath)) {
            throw "$checkpoint inspection raw capture path does not match the pinned review receipt."
        }
        $rawDocument = Get-Content -LiteralPath $rawPin.Path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $baseline = $capture.Baseline
        $order = @($baseline.BootOrder)
        if ($null -eq $baseline -or $null -ne $baseline.BootNext -or $order.Count -ne 6 -or
            $null -ne $rawDocument.boot_next -or @($rawDocument.boot_order).Count -ne 6 -or
            $rawDocument.schema -ne 1 -or [string]$rawDocument.kind -cne 'boot-selection' -or
            [int]$rawDocument.findings -ne 0 -or
            [string]$baseline.BootManagerSha256 -cne $script:GpuRetirement201140BootManagerSha256 -or
            [string]$baseline.ReferenceLogSha256 -cne $script:GpuRetirement201140ReferenceLogSha256 -or
            [string]$baseline.IntegrityFindings -cne '00000000' -or
            [string]$rawDocument.bootmgfw_sha256 -cne $script:GpuRetirement201140BootManagerSha256 -or
            [string]$rawDocument.log_sha256 -cne $script:GpuRetirement201140ReferenceLogSha256 -or
            -not [string]::Equals(([string]$capture.IntegrityOutput).Trim(), $expectedIntegrity, [StringComparison]::Ordinal)) {
            throw "$checkpoint inspection raw capture differs from the exact reviewed BootNext, BootOrder, boot-manager, reference-log, or integrity state."
        }
        for ($orderIndex = 0; $orderIndex -lt 6; $orderIndex++) {
            if ([int]$order[$orderIndex] -ne $orderIndex -or [int]$rawDocument.boot_order[$orderIndex] -ne $orderIndex) {
                throw "$checkpoint inspection BootOrder differs from the fixed reviewed sequence 0 through 5."
            }
        }
        $extraPins.Add($rawPin)

        $receiptLeaf = $inspectionReceiptLeaves[$index]
        $receiptPath = Join-Path $inspectionDirectory ($receiptLeaf + '.receipt.json')
        if ([IO.Path]::GetFullPath([string]$capture.IntegrityReceipt) -cne [IO.Path]::GetFullPath($receiptPath)) {
            throw "$checkpoint inspection integrity receipt path differs from the pinned review receipt."
        }
        $receiptFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $receiptPath -WorkspaceRoot $workspace
        $integrityReceiptPin = Get-GpuStageRetirementPin -Path $receiptFullPath
        $integrityReceipt = Get-Content -LiteralPath $receiptFullPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $expectedArguments = @('bootmgr-integrity','--expected-hash',$script:GpuRetirement201140BootManagerSha256,'--esp-root','S:\')
        if ($integrityReceipt.SchemaVersion -ne 1 -or $integrityReceipt.Succeeded -ne $true -or
            [int]$integrityReceipt.ExitCode -ne 0 -or @($integrityReceipt.AllowedExitCodes).Count -ne 1 -or
            [int]$integrityReceipt.AllowedExitCodes[0] -ne 0 -or $null -ne $integrityReceipt.LaunchError -or
            [string]$integrityReceipt.Program -cne [IO.Path]::GetFullPath($auditToolPath) -or
            @($integrityReceipt.Arguments).Count -ne $expectedArguments.Count) {
            throw "$checkpoint inspection integrity command receipt is incomplete or unsuccessful."
        }
        for ($argumentIndex = 0; $argumentIndex -lt $expectedArguments.Count; $argumentIndex++) {
            if ([string]$integrityReceipt.Arguments[$argumentIndex] -cne $expectedArguments[$argumentIndex]) {
                throw "$checkpoint inspection integrity command receipt has unexpected arguments."
            }
        }
        $stdoutPath = [string]$integrityReceipt.StdoutPath
        $stderrPath = [string]$integrityReceipt.StderrPath
        if ([IO.Path]::GetFileName($stdoutPath) -cne ($receiptLeaf + '.stdout.txt') -or
            [IO.Path]::GetFileName($stderrPath) -cne ($receiptLeaf + '.stderr.txt') -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stdoutPath)) -cne [IO.Path]::GetFullPath($inspectionDirectory) -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stderrPath)) -cne [IO.Path]::GetFullPath($inspectionDirectory)) {
            throw "$checkpoint inspection integrity output paths do not match the pinned review receipt."
        }
        $stdoutFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stdoutPath -WorkspaceRoot $workspace
        $stderrFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stderrPath -WorkspaceRoot $workspace
        $stdoutPin = Get-GpuStageRetirementPin -Path $stdoutFullPath
        $stderrPin = Get-GpuStageRetirementPin -Path $stderrFullPath
        if (-not [string]::Equals(([IO.File]::ReadAllText($stdoutFullPath)).Trim(), $expectedIntegrity, [StringComparison]::Ordinal) -or
            [IO.File]::ReadAllText($stderrFullPath).Length -ne 0) {
            throw "$checkpoint inspection integrity stdout/stderr differ from the pinned review receipt."
        }
        $extraPins.Add($integrityReceiptPin)
        $extraPins.Add($stdoutPin)
        $extraPins.Add($stderrPin)
    }

    return @($extraPins.ToArray())
}

function Assert-GpuStageRetirement224018Proof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $runDirectory = Join-Path (Join-Path $workspace '.analysis\gpu-baremetal\runs') $script:GpuRetirement224018RunId
    $last = $Journal.LastAction
    $verification = $Journal.FinalVerification
    $enumeration = $verification.FirmwareEnumeration
    $review = $enumeration.ReviewedDifference
    if ([string]$Journal.RunId -cne $script:GpuRetirement224018RunId -or
        [IO.Path]::GetFullPath([string]$Journal.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [string]$Journal.SourceCommit -cne $script:GpuRetirement224018SourceCommit -or
        [string]$Journal.Phase -cne 'Complete' -or $null -ne $Journal.InProgress -or
        $null -eq $last -or [string]$last.Action -cne 'FinalVerify' -or $last.Succeeded -ne $true -or
        [string]$last.Status -cne 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED' -or
        [IO.Path]::GetFullPath([string]$last.Directory) -cne [IO.Path]::GetFullPath((Join-Path $workspace $script:GpuRetirement224018FinalActionRelativePath))) {
        throw 'The 224018 journal lacks the exact successful reviewed FinalVerify action.'
    }
    if ($null -eq $verification -or $verification.ResetBoundaryPassed -ne $true -or
        $verification.ActivationAttempted -ne $true -or $verification.SecureBoot -ne $true -or
        $verification.ActiveGpuProof -ne $false -or [string]$verification.GpuAttestation -cne 'UNATTESTED' -or
        @($verification.Errors).Count -ne 0 -or $null -eq $verification.Health -or
        $verification.Health.Passed -ne $true -or [string]$verification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$verification.Health.Aggregate -cne 'ABSENT' -or
        [int]$verification.Health.CpuCount -ne 20 -or [int]$verification.Health.ExpectedCpus -ne 20 -or
        $null -eq $verification.Esp -or $verification.Esp.MatchesBaseline -ne $true) {
        throw 'The 224018 final verification does not satisfy the reviewed reset, ABSENT health, Secure Boot, ESP, and unattested-GPU gates.'
    }
    if ($null -eq $enumeration -or $enumeration.TextEqual -ne $false -or
        $enumeration.ReviewedDifferenceAccepted -ne $true -or
        [string]$enumeration.BaselineSha256 -cne $script:GpuRetirement224018OriginalFirmwareSha256 -or
        [string]$enumeration.CurrentSha256 -cne $script:GpuRetirement224018CurrentFirmwareSha256 -or
        [string]$enumeration.CaptureContext -cne 'SystemEspMountedAtS' -or
        $null -eq $review -or $review.Accepted -ne $true -or
        $review.HistoricalRawBootOrderUnchangedProven -ne $false -or
        $review.FullBootVariableNamespaceAttested -ne $false) {
        throw 'The journal does not contain the exact accepted 224018 firmware-difference review.'
    }

    $reviewReceiptPath = Join-Path $workspace $script:GpuRetirement224018ReviewReceiptRelativePath
    $reviewReceiptPin = Assert-GpuRetirementProfilePin -Pin $review.ReceiptPin -ExpectedPath $reviewReceiptPath `
        -ExpectedSha256 $script:GpuRetirement224018ReviewReceiptSha256 -ExpectedBytes $script:GpuRetirement224018ReviewReceiptBytes `
        -WorkspaceRoot $workspace -Description 'Read-only firmware review receipt'
    $reviewReceipt = Get-Content -LiteralPath $reviewReceiptPin.Path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    $receiptPinSpecs = @(
        @{ Group='PinsBefore'; Name='Selector'; RelativePath='.analysis\gpu-baremetal\host-prepared.json'; Sha256=$script:GpuRetirement224018ArchiveSha256; Bytes=$script:GpuRetirement224018ArchiveBytes },
        @{ Group='PinsBefore'; Name='Journal'; RelativePath='.analysis\gpu-baremetal\runs\gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1\journal.json'; Sha256='A1513F12AFCD2932E4DE2D9A6069C7A2CED6F969BF1AC82E6942D6818F319E07'; Bytes=45568L },
        @{ Group='PinsBefore'; Name='FirmwareBaseline'; RelativePath=$script:GpuRetirement224018OriginalFirmwareRelativePath; Sha256=$script:GpuRetirement224018OriginalFirmwareSha256; Bytes=$script:GpuRetirement224018OriginalFirmwareBytes },
        @{ Group='PinsBefore'; Name='FirmwareCurrent'; RelativePath=$script:GpuRetirement224018ReviewedFirmwareRelativePath; Sha256=$script:GpuRetirement224018CurrentFirmwareSha256; Bytes=$script:GpuRetirement224018CurrentFirmwareBytes },
        @{ Group='PinsBefore'; Name='Ibaudit'; RelativePath=$script:GpuRetirementAuditToolRelativePath; Sha256=$script:GpuRetirementAuditToolSha256; Bytes=$script:GpuRetirementAuditToolBytes },
        @{ Group='PinsBefore'; Name='ReferenceLog'; RelativePath=$script:GpuRetirement224018ReferenceLogRelativePath; Sha256=$script:GpuRetirement224018ReferenceLogSha256; Bytes=$script:GpuRetirement224018ReferenceLogBytes },
        @{ Group='PinsBefore'; Name='Probe'; RelativePath=$script:GpuRetirement224018ProbeRelativePath; Sha256=$script:GpuRetirement224018ProbeSha256; Bytes=$script:GpuRetirement224018ProbeBytes },
        @{ Group='PinsBefore'; Name='InspectorScript'; RelativePath='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1'; Sha256=$script:GpuRetirement224018InspectorSha256; Bytes=$script:GpuRetirement224018InspectorBytes }
    )
    foreach ($spec in $receiptPinSpecs) {
        $relativePath = [string]$spec.RelativePath
        $expectedPath = [IO.Path]::GetFullPath((Join-Path $workspace $relativePath))
        $before = Get-GpuRetirementProperty -Object (Get-GpuRetirementProperty -Object $reviewReceipt -Name $spec.Group) -Name $spec.Name
        $after = Get-GpuRetirementProperty -Object (Get-GpuRetirementProperty -Object $reviewReceipt -Name 'PinsAfter') -Name $spec.Name
        foreach ($pin in @($before,$after)) {
            if ($null -eq $pin -or [IO.Path]::GetFullPath([string]$pin.Path) -cne $expectedPath -or
                [string]$pin.Sha256 -cne [string]$spec.Sha256 -or [long]$pin.Bytes -ne [long]$spec.Bytes) {
                throw "The read-only firmware review $($spec.Name) pin differs from the closed 224018 profile."
            }
        }
    }
    $modulePinSpecs = @(
        @{ Name='State'; RelativePath='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuHostState.psm1'; Sha256=$script:GpuRetirement224018StateSha256; Bytes=$script:GpuRetirement224018StateBytes },
        @{ Name='Platform'; RelativePath='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuHostPlatform.psm1'; Sha256=$script:GpuRetirement224018HistoricalPlatformSha256; Bytes=$script:GpuRetirement224018HistoricalPlatformBytes },
        @{ Name='Retirement'; RelativePath='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuStageRetirement.psm1'; Sha256=$script:GpuRetirement224018HistoricalRetirementSha256; Bytes=$script:GpuRetirement224018HistoricalRetirementBytes }
    )
    foreach ($spec in $modulePinSpecs) {
        $expectedPath = [IO.Path]::GetFullPath((Join-Path $workspace $spec.RelativePath))
        foreach ($groupName in @('PinsBefore','PinsAfter')) {
            $modules = Get-GpuRetirementProperty -Object (Get-GpuRetirementProperty -Object $reviewReceipt -Name $groupName) -Name 'Modules'
            $pin = Get-GpuRetirementProperty -Object $modules -Name $spec.Name
            if ($null -eq $pin -or [IO.Path]::GetFullPath([string]$pin.Path) -cne $expectedPath -or
                [string]$pin.Sha256 -cne [string]$spec.Sha256 -or [long]$pin.Bytes -ne [long]$spec.Bytes) {
                throw "The read-only firmware review module pin $($spec.Name) differs from the captured 224018 source."
            }
        }
    }
    if ($reviewReceipt.SchemaVersion -ne 1 -or [string]$reviewReceipt.Status -cne 'READONLY_FIRMWARE_REVIEW_CAPTURED' -or
        $reviewReceipt.Succeeded -ne $true -or $reviewReceipt.RecoveryCompleted -ne $false -or
        $reviewReceipt.SelectorAndJournalUnchanged -ne $true -or $reviewReceipt.EspFilesChanged -ne $false -or
        $reviewReceipt.FirmwareWritten -ne $false -or $reviewReceipt.CleanupSucceeded -ne $true -or
        $reviewReceipt.EspUnmounted -ne $true -or $reviewReceipt.RawBootSelectionStable -ne $true -or
        $reviewReceipt.FirmwareTextStable -ne $true -or
        $reviewReceipt.HistoricalRawBootOrderUnchangedProven -ne $false -or
        @($reviewReceipt.CleanupErrors).Count -ne 0 -or @($reviewReceipt.Errors).Count -ne 0 -or
        $null -eq $reviewReceipt.CurrentHost -or $reviewReceipt.CurrentHost.SecureBoot -ne $true -or
        [int]$reviewReceipt.CurrentHost.LogicalCpus -ne 20 -or
        [string]$reviewReceipt.CurrentHost.BootId -cne [string]$verification.BootId -or
        $null -eq $reviewReceipt.Esp -or
        [string]$reviewReceipt.Esp.Volume -cne [string]$Journal.Backup.EspVolume -or
        [int]$reviewReceipt.Esp.DiskNumber -ne [int]$Journal.Backup.EspDiskNumber -or
        [string]$reviewReceipt.Esp.PartitionGuid -cne [string]$Journal.Backup.EspPartitionGuid -or
        [string]$reviewReceipt.Esp.BootManagerSha256 -cne $script:GpuRetirement224018BootManagerSha256) {
        throw 'The pinned read-only firmware review receipt does not prove its required host, ESP, cleanup, and raw stability observations.'
    }
    $receiptRawCaptures = @($reviewReceipt.BootSelectionCaptures)
    if ($receiptRawCaptures.Count -ne 2 -or @($reviewReceipt.FirmwareObservations).Count -ne 2) {
        throw 'The read-only firmware review receipt must contain two raw captures and two stable firmware observations.'
    }

    $backupFirmware = $Journal.Backup.FirmwareEnumeration
    if ($null -eq $backupFirmware) { throw 'The original firmware baseline pin is missing from the 224018 journal.' }
    $expectedOriginalPath = Join-Path $workspace $script:GpuRetirement224018OriginalFirmwareRelativePath
    if ([IO.Path]::GetFullPath([string]$backupFirmware.Path) -cne [IO.Path]::GetFullPath($expectedOriginalPath)) {
        throw 'The archived original firmware baseline path differs from the fixed 224018 Backup action.'
    }
    $originalFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.OriginalBaselinePin -ExpectedPath $expectedOriginalPath `
        -ExpectedSha256 $script:GpuRetirement224018OriginalFirmwareSha256 -ExpectedBytes $script:GpuRetirement224018OriginalFirmwareBytes `
        -WorkspaceRoot $workspace -Description 'Original backup firmware baseline'
    if ([string]$backupFirmware.Sha256 -cne $script:GpuRetirement224018OriginalFirmwareSha256 -or
        [long]$backupFirmware.Bytes -ne $script:GpuRetirement224018OriginalFirmwareBytes) {
        throw 'The archived original firmware baseline no longer has its fixed 4A1535 pin.'
    }
    $reviewedFirmwarePath = Join-Path $workspace $script:GpuRetirement224018ReviewedFirmwareRelativePath
    $reviewedFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.ReviewedFirmwarePin -ExpectedPath $reviewedFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement224018CurrentFirmwareSha256 -ExpectedBytes $script:GpuRetirement224018CurrentFirmwareBytes `
        -WorkspaceRoot $workspace -Description 'First observed changed firmware capture'
    $freshFirmwarePath = Join-Path $workspace $script:GpuRetirement224018FreshFirmwareRelativePath
    $freshFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.FreshFirmwarePin -ExpectedPath $freshFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement224018CurrentFirmwareSha256 -ExpectedBytes $script:GpuRetirement224018CurrentFirmwareBytes `
        -WorkspaceRoot $workspace -Description 'Successful final-review firmware capture'
    if ([string]$Journal.Backup.BootManagerSha256 -cne $script:GpuRetirement224018BootManagerSha256 -or
        [string]$reviewReceipt.Esp.BootManagerSha256 -cne [string]$Journal.Backup.BootManagerSha256) {
        throw 'The review receipt or journal boot-manager pin differs from the fixed reviewed value.'
    }

    $referenceLog = $Journal.Backup.ReferenceLog
    if ($null -eq $referenceLog -or [string]$referenceLog.Sha256 -cne $script:GpuRetirement224018ReferenceLogSha256 -or
        [long]$referenceLog.Bytes -ne $script:GpuRetirement224018ReferenceLogBytes) {
        throw 'The 224018 journal does not preserve the fixed reviewed TPM reference log.'
    }
    $auditToolPath = Join-Path $workspace $script:GpuRetirementAuditToolRelativePath
    $auditToolPin = Get-GpuStageRetirementPin -Path (Assert-GpuRetirementSafeWorkspaceFile -Path $auditToolPath -WorkspaceRoot $workspace)
    if ($auditToolPin.Sha256 -cne $script:GpuRetirementAuditToolSha256 -or $auditToolPin.Bytes -ne $script:GpuRetirementAuditToolBytes) {
        throw 'The fixed read-only firmware audit tool has changed.'
    }

    $referenceLogPath = Join-Path $workspace $script:GpuRetirement224018ReferenceLogRelativePath
    if ([IO.Path]::GetFullPath([string]$referenceLog.Path) -cne [IO.Path]::GetFullPath($referenceLogPath)) {
        throw 'The 224018 TPM reference log path differs from the fixed Backup action.'
    }
    $referenceLogPin = Assert-GpuRetirementProfilePin -Pin $referenceLog -ExpectedPath $referenceLogPath `
        -ExpectedSha256 $script:GpuRetirement224018ReferenceLogSha256 -ExpectedBytes $script:GpuRetirement224018ReferenceLogBytes `
        -WorkspaceRoot $workspace -Description 'Pinned TPM reference log'
    $probePath = Join-Path $workspace $script:GpuRetirement224018ProbeRelativePath
    if ([IO.Path]::GetFullPath([string]$Journal.Tools.Probe.Path) -cne [IO.Path]::GetFullPath($probePath)) {
        throw 'The 224018 health probe path differs from the fixed build output.'
    }
    $probePin = Assert-GpuRetirementProfilePin -Pin $Journal.Tools.Probe -ExpectedPath $probePath `
        -ExpectedSha256 $script:GpuRetirement224018ProbeSha256 -ExpectedBytes $script:GpuRetirement224018ProbeBytes `
        -WorkspaceRoot $workspace -Description 'Pinned health probe'
    $rawCaptures = @($review.RawCaptures)
    if ($rawCaptures.Count -ne 2) { throw 'The accepted review must contain exactly two raw boot-selection captures.' }
    $reviewActionDirectory = Join-Path $workspace $script:GpuRetirement224018ReviewActionRelativePath
    $extraPins = New-Object 'System.Collections.Generic.List[object]'
    foreach ($pin in @($reviewReceiptPin,$originalFirmwarePin,$reviewedFirmwarePin,$freshFirmwarePin,$referenceLogPin,$probePin,$auditToolPin)) { $extraPins.Add($pin) }
    $finalResultPath = Join-Path $workspace $script:GpuRetirement224018FinalResultRelativePath
    $finalResultPin = Assert-GpuRetirementProfilePin -Pin ([pscustomobject]@{ Path=$finalResultPath; Sha256=$script:GpuRetirement224018FinalResultSha256; Bytes=$script:GpuRetirement224018FinalResultBytes }) `
        -ExpectedPath $finalResultPath -ExpectedSha256 $script:GpuRetirement224018FinalResultSha256 -ExpectedBytes $script:GpuRetirement224018FinalResultBytes `
        -WorkspaceRoot $workspace -Description 'Successful FinalVerify result'
    $finalResult = Get-Content -LiteralPath $finalResultPin.Path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    if ([string]$finalResult.Action -cne 'FinalVerify' -or [string]$finalResult.Status -cne 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED' -or
        $finalResult.Succeeded -ne $true) {
        throw 'The pinned FinalVerify result is not the exact successful reviewed firmware-difference completion.'
    }
    $extraPins.Add($finalResultPin)
    $reviewedFirmwareText = [IO.File]::ReadAllText($freshFirmwarePin.Path)
    if (-not [string]::Equals($reviewedFirmwareText, [IO.File]::ReadAllText($reviewedFirmwarePin.Path), [StringComparison]::Ordinal)) {
        throw 'The pinned firmware captures do not match their expected reviewed content.'
    }

    $inspectionDirectory = Join-Path (Split-Path -Parent $reviewReceiptPin.Path) 'observations'
    $firmwareObservationNames = @('before','after')
    $firmwareObservations = @($reviewReceipt.FirmwareObservations)
    for ($index = 0; $index -lt 2; $index++) {
        $observation = $firmwareObservations[$index]
        $checkpoint = $firmwareObservationNames[$index]
        if ([string]$observation.Checkpoint -cne $checkpoint -or $observation.Succeeded -ne $true -or
            -not [string]::Equals([string]$observation.Text, $reviewedFirmwareText, [StringComparison]::Ordinal)) {
            throw "The read-only $checkpoint firmware observation differs from the exact reviewed final capture."
        }
        $stdoutPin = $observation.Pin
        if ($null -eq $stdoutPin -or [string]$stdoutPin.Sha256 -cne $script:GpuRetirement224018CurrentFirmwareSha256 -or
            [long]$stdoutPin.Bytes -ne $script:GpuRetirement224018CurrentFirmwareBytes -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath([string]$stdoutPin.Path)) -cne [IO.Path]::GetFullPath($inspectionDirectory) -or
            [IO.Path]::GetFileName([string]$stdoutPin.Path) -notmatch '^stage-retirement-firmware-[0-9a-f]{32}\.stdout\.txt$') {
            throw "The read-only $checkpoint firmware output pin does not match the fixed review observation directory."
        }
        $stdoutFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path ([string]$stdoutPin.Path) -WorkspaceRoot $workspace
        $checkedStdout = Assert-GpuRetirementProfilePin -Pin $stdoutPin -ExpectedPath $stdoutFullPath `
            -ExpectedSha256 $script:GpuRetirement224018CurrentFirmwareSha256 -ExpectedBytes $script:GpuRetirement224018CurrentFirmwareBytes `
            -WorkspaceRoot $workspace -Description "$checkpoint read-only firmware stdout"
        if (-not [string]::Equals([IO.File]::ReadAllText($checkedStdout.Path), [string]$observation.Text, [StringComparison]::Ordinal)) {
            throw "The read-only $checkpoint firmware stdout differs from its pinned observation text."
        }

        $stdoutLeaf = [IO.Path]::GetFileName($checkedStdout.Path)
        $receiptLeaf = $stdoutLeaf.Substring(0, $stdoutLeaf.Length - '.stdout.txt'.Length) + '.receipt.json'
        $expectedReceiptPath = Join-Path $inspectionDirectory $receiptLeaf
        if ([IO.Path]::GetFullPath([string]$observation.ReceiptPath) -cne [IO.Path]::GetFullPath($expectedReceiptPath)) {
            throw "The read-only $checkpoint firmware receipt path differs from its pinned observation."
        }
        $firmwareReceiptPath = Assert-GpuRetirementSafeWorkspaceFile -Path $expectedReceiptPath -WorkspaceRoot $workspace
        $firmwareReceiptPin = Get-GpuStageRetirementPin -Path $firmwareReceiptPath
        $firmwareReceipt = Get-Content -LiteralPath $firmwareReceiptPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        if ($firmwareReceipt.SchemaVersion -ne 1 -or $firmwareReceipt.Succeeded -ne $true -or
            [int]$firmwareReceipt.ExitCode -ne 0 -or @($firmwareReceipt.AllowedExitCodes).Count -ne 1 -or
            [int]$firmwareReceipt.AllowedExitCodes[0] -ne 0 -or $null -ne $firmwareReceipt.LaunchError -or
            [IO.Path]::GetFileName([string]$firmwareReceipt.Program) -ine 'bcdedit.exe' -or
            @($firmwareReceipt.Arguments).Count -ne 3 -or
            [string]$firmwareReceipt.Arguments[0] -cne '/enum' -or
            [string]$firmwareReceipt.Arguments[1] -cne 'firmware' -or
            [string]$firmwareReceipt.Arguments[2] -cne '/v' -or
            [IO.Path]::GetFullPath([string]$firmwareReceipt.StdoutPath) -cne [IO.Path]::GetFullPath($checkedStdout.Path)) {
            throw "The read-only $checkpoint firmware command receipt is unsuccessful or does not match its capture."
        }
        $stderrPath = [string]$firmwareReceipt.StderrPath
        if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stderrPath)) -cne [IO.Path]::GetFullPath($inspectionDirectory) -or
            [IO.Path]::GetFileName($stderrPath) -cne ($stdoutLeaf.Substring(0, $stdoutLeaf.Length - '.stdout.txt'.Length) + '.stderr.txt')) {
            throw "The read-only $checkpoint firmware stderr path differs from its review observation."
        }
        $stderrFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stderrPath -WorkspaceRoot $workspace
        $stderrPin = Get-GpuStageRetirementPin -Path $stderrFullPath
        if ([IO.File]::ReadAllText($stderrFullPath).Length -ne 0) {
            throw "The read-only $checkpoint firmware command wrote unexpected stderr."
        }
        $extraPins.Add($checkedStdout)
        $extraPins.Add($firmwareReceiptPin)
        $extraPins.Add($stderrPin)
    }

    $expectedIntegrity = 'BootmgfwSha256=' + $script:GpuRetirement224018BootManagerSha256 + ' Findings=0x00000000'
    for ($index = 0; $index -lt 2; $index++) {
        $capture = $rawCaptures[$index]
        $checkpoint = if ($index -eq 0) { 'before' } else { 'after' }
        $rawRelativePath = $script:GpuRetirement224018ReviewActionRelativePath + '\' + $checkpoint + '-boot-selection.json'
        $rawPath = Join-Path $workspace $rawRelativePath
        $rawPin = Assert-GpuRetirementProfilePin -Pin $capture.BaselinePin -ExpectedPath $rawPath `
            -ExpectedSha256 $script:GpuRetirement224018RawCaptureSha256 -ExpectedBytes $script:GpuRetirement224018RawCaptureBytes `
            -WorkspaceRoot $workspace -Description "$checkpoint raw boot-selection capture"
        if ([IO.Path]::GetFullPath([string]$capture.BaselinePath) -cne [IO.Path]::GetFullPath($rawPath)) {
            throw "$checkpoint raw capture path does not match the closed profile."
        }
        $rawDocument = Get-Content -LiteralPath $rawPin.Path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $baseline = $capture.Baseline
        $order = @($baseline.BootOrder)
        if ($null -eq $baseline -or $null -eq $baseline.PSObject.Properties['Schema'] -or
            $null -eq $baseline.PSObject.Properties['BootNext'] -or $null -eq $baseline.PSObject.Properties['BootOrder'] -or
            $null -eq $baseline.PSObject.Properties['BootManagerSha256'] -or $null -eq $baseline.PSObject.Properties['ReferenceLogSha256'] -or
            $null -eq $baseline.PSObject.Properties['IntegrityFindings'] -or
            $null -eq $rawDocument.PSObject.Properties['boot_next'] -or $null -eq $rawDocument.PSObject.Properties['boot_order'] -or
            $null -eq $rawDocument.PSObject.Properties['bootmgfw_sha256'] -or $null -eq $rawDocument.PSObject.Properties['log_sha256'] -or
            $null -eq $rawDocument.PSObject.Properties['findings'] -or
            $baseline.Schema -ne 1 -or $baseline.BootNext -ne $null -or
            $order.Count -ne 3 -or @($rawDocument.boot_order).Count -ne 3 -or
            $null -ne $rawDocument.boot_next -or $rawDocument.schema -ne 1 -or
            [string]$rawDocument.kind -cne 'boot-selection' -or [int]$rawDocument.findings -ne 0 -or
            [string]$rawDocument.bootmgfw_sha256 -cne $script:GpuRetirement224018BootManagerSha256 -or
            [string]$rawDocument.log_sha256 -cne $script:GpuRetirement224018ReferenceLogSha256 -or
            [string]$baseline.BootManagerSha256 -cne $script:GpuRetirement224018BootManagerSha256 -or
            [string]$baseline.ReferenceLogSha256 -cne $script:GpuRetirement224018ReferenceLogSha256 -or
            [string]$baseline.IntegrityFindings -cne '00000000' -or
            -not [string]::Equals([string]$capture.IntegrityOutput, $expectedIntegrity, [StringComparison]::Ordinal)) {
            throw "$checkpoint raw boot-selection capture lacks the fixed BootNext, BootOrder, boot-manager, reference-log, or integrity proof."
        }
        for ($orderIndex = 0; $orderIndex -lt 3; $orderIndex++) {
            if ([int]$order[$orderIndex] -ne $orderIndex -or [int]$rawDocument.boot_order[$orderIndex] -ne $orderIndex) {
                throw "$checkpoint raw capture BootOrder differs from the fixed reviewed sequence 0 through 2."
            }
        }
        $extraPins.Add($rawPin)

        $receiptLeaf = if ($index -eq 0) { 'before-bootmgr-integrity-5fb5159e736f4f77b0aff8d2be988c17' } else { 'after-bootmgr-integrity-6d322e97c5a84818b0edec12afc5a083' }
        $receiptPath = Join-Path $reviewActionDirectory ($receiptLeaf + '.receipt.json')
        if ([IO.Path]::GetFullPath([string]$capture.IntegrityReceipt) -cne [IO.Path]::GetFullPath($receiptPath)) {
            throw "$checkpoint integrity receipt path does not match the successful final review record."
        }
        $receiptFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $receiptPath -WorkspaceRoot $workspace
        $integrityReceiptPin = Get-GpuStageRetirementPin -Path $receiptFullPath
        $integrityReceipt = Get-Content -LiteralPath $receiptFullPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $expectedArguments = @('bootmgr-integrity','--expected-hash',$script:GpuRetirement224018BootManagerSha256,'--esp-root','S:\')
        if ($integrityReceipt.SchemaVersion -ne 1 -or $integrityReceipt.Succeeded -ne $true -or
            [int]$integrityReceipt.ExitCode -ne 0 -or @($integrityReceipt.AllowedExitCodes).Count -ne 1 -or
            [int]$integrityReceipt.AllowedExitCodes[0] -ne 0 -or $null -ne $integrityReceipt.LaunchError -or
            [string]$integrityReceipt.Program -cne [IO.Path]::GetFullPath($auditToolPath) -or
            @($integrityReceipt.Arguments).Count -ne $expectedArguments.Count) {
            throw "$checkpoint integrity command receipt is incomplete or unsuccessful."
        }
        for ($argumentIndex = 0; $argumentIndex -lt $expectedArguments.Count; $argumentIndex++) {
            if ([string]$integrityReceipt.Arguments[$argumentIndex] -cne $expectedArguments[$argumentIndex]) {
                throw "$checkpoint integrity command receipt has unexpected arguments."
            }
        }
        $stdoutPath = [string]$integrityReceipt.StdoutPath
        $stderrPath = [string]$integrityReceipt.StderrPath
        $expectedOutputLeaf = $receiptLeaf + '.stdout.txt'
        $expectedErrorLeaf = $receiptLeaf + '.stderr.txt'
        if ([IO.Path]::GetFileName($stdoutPath) -cne $expectedOutputLeaf -or
            [IO.Path]::GetFileName($stderrPath) -cne $expectedErrorLeaf -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stdoutPath)) -cne [IO.Path]::GetDirectoryName($receiptFullPath) -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stderrPath)) -cne [IO.Path]::GetDirectoryName($receiptFullPath)) {
            throw "$checkpoint integrity output paths do not match the fixed review observation directory."
        }
        $stdoutFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stdoutPath -WorkspaceRoot $workspace
        $stderrFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stderrPath -WorkspaceRoot $workspace
        $stdoutPin = Get-GpuStageRetirementPin -Path $stdoutFullPath
        $stderrPin = Get-GpuStageRetirementPin -Path $stderrFullPath
        if (-not [string]::Equals(([IO.File]::ReadAllText($stdoutFullPath)).Trim(), $expectedIntegrity, [StringComparison]::Ordinal) -or
            [IO.File]::ReadAllText($stderrFullPath).Length -ne 0) {
            throw "$checkpoint integrity stdout/stderr differ from the successful reviewed result."
        }
        $extraPins.Add($integrityReceiptPin)
        $extraPins.Add($stdoutPin)
        $extraPins.Add($stderrPin)
    }

    # The pinned read-only review receipt cites a second pair of raw captures.
    # Validate those exact files too; their directory and integrity receipts are
    # distinct from the successful FinalVerify action captures above.
    $inspectionReceiptLeaves = @('before-bootmgr-integrity-4dd2ea2d166c4c36ac130f14991ea702','after-bootmgr-integrity-df0ac281c4cd4ad4b513ba55d5fd1431')
    for ($index = 0; $index -lt 2; $index++) {
        $capture = $receiptRawCaptures[$index]
        $checkpoint = if ($index -eq 0) { 'before' } else { 'after' }
        $rawPath = Join-Path $inspectionDirectory ($checkpoint + '-boot-selection.json')
        $rawPin = Assert-GpuRetirementProfilePin -Pin $capture.BaselinePin -ExpectedPath $rawPath `
            -ExpectedSha256 $script:GpuRetirement224018RawCaptureSha256 -ExpectedBytes $script:GpuRetirement224018RawCaptureBytes `
            -WorkspaceRoot $workspace -Description "$checkpoint inspection raw boot-selection capture"
        if ([IO.Path]::GetFullPath([string]$capture.BaselinePath) -cne [IO.Path]::GetFullPath($rawPath)) {
            throw "$checkpoint inspection raw capture path does not match the pinned review receipt."
        }
        $rawDocument = Get-Content -LiteralPath $rawPin.Path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $baseline = $capture.Baseline
        $order = @($baseline.BootOrder)
        if ($null -eq $baseline -or $null -ne $baseline.BootNext -or $order.Count -ne 3 -or
            $null -ne $rawDocument.boot_next -or @($rawDocument.boot_order).Count -ne 3 -or
            $rawDocument.schema -ne 1 -or [string]$rawDocument.kind -cne 'boot-selection' -or
            [int]$rawDocument.findings -ne 0 -or
            [string]$baseline.BootManagerSha256 -cne $script:GpuRetirement224018BootManagerSha256 -or
            [string]$baseline.ReferenceLogSha256 -cne $script:GpuRetirement224018ReferenceLogSha256 -or
            [string]$baseline.IntegrityFindings -cne '00000000' -or
            [string]$rawDocument.bootmgfw_sha256 -cne $script:GpuRetirement224018BootManagerSha256 -or
            [string]$rawDocument.log_sha256 -cne $script:GpuRetirement224018ReferenceLogSha256 -or
            -not [string]::Equals(([string]$capture.IntegrityOutput).Trim(), $expectedIntegrity, [StringComparison]::Ordinal)) {
            throw "$checkpoint inspection raw capture differs from the exact reviewed BootNext, BootOrder, boot-manager, reference-log, or integrity state."
        }
        for ($orderIndex = 0; $orderIndex -lt 3; $orderIndex++) {
            if ([int]$order[$orderIndex] -ne $orderIndex -or [int]$rawDocument.boot_order[$orderIndex] -ne $orderIndex) {
                throw "$checkpoint inspection BootOrder differs from the fixed reviewed sequence 0 through 2."
            }
        }
        $extraPins.Add($rawPin)

        $receiptLeaf = $inspectionReceiptLeaves[$index]
        $receiptPath = Join-Path $inspectionDirectory ($receiptLeaf + '.receipt.json')
        if ([IO.Path]::GetFullPath([string]$capture.IntegrityReceipt) -cne [IO.Path]::GetFullPath($receiptPath)) {
            throw "$checkpoint inspection integrity receipt path differs from the pinned review receipt."
        }
        $receiptFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $receiptPath -WorkspaceRoot $workspace
        $integrityReceiptPin = Get-GpuStageRetirementPin -Path $receiptFullPath
        $integrityReceipt = Get-Content -LiteralPath $receiptFullPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $expectedArguments = @('bootmgr-integrity','--expected-hash',$script:GpuRetirement224018BootManagerSha256,'--esp-root','S:\')
        if ($integrityReceipt.SchemaVersion -ne 1 -or $integrityReceipt.Succeeded -ne $true -or
            [int]$integrityReceipt.ExitCode -ne 0 -or @($integrityReceipt.AllowedExitCodes).Count -ne 1 -or
            [int]$integrityReceipt.AllowedExitCodes[0] -ne 0 -or $null -ne $integrityReceipt.LaunchError -or
            [string]$integrityReceipt.Program -cne [IO.Path]::GetFullPath($auditToolPath) -or
            @($integrityReceipt.Arguments).Count -ne $expectedArguments.Count) {
            throw "$checkpoint inspection integrity command receipt is incomplete or unsuccessful."
        }
        for ($argumentIndex = 0; $argumentIndex -lt $expectedArguments.Count; $argumentIndex++) {
            if ([string]$integrityReceipt.Arguments[$argumentIndex] -cne $expectedArguments[$argumentIndex]) {
                throw "$checkpoint inspection integrity command receipt has unexpected arguments."
            }
        }
        $stdoutPath = [string]$integrityReceipt.StdoutPath
        $stderrPath = [string]$integrityReceipt.StderrPath
        if ([IO.Path]::GetFileName($stdoutPath) -cne ($receiptLeaf + '.stdout.txt') -or
            [IO.Path]::GetFileName($stderrPath) -cne ($receiptLeaf + '.stderr.txt') -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stdoutPath)) -cne [IO.Path]::GetFullPath($inspectionDirectory) -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stderrPath)) -cne [IO.Path]::GetFullPath($inspectionDirectory)) {
            throw "$checkpoint inspection integrity output paths do not match the pinned review receipt."
        }
        $stdoutFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stdoutPath -WorkspaceRoot $workspace
        $stderrFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stderrPath -WorkspaceRoot $workspace
        $stdoutPin = Get-GpuStageRetirementPin -Path $stdoutFullPath
        $stderrPin = Get-GpuStageRetirementPin -Path $stderrFullPath
        if (-not [string]::Equals(([IO.File]::ReadAllText($stdoutFullPath)).Trim(), $expectedIntegrity, [StringComparison]::Ordinal) -or
            [IO.File]::ReadAllText($stderrFullPath).Length -ne 0) {
            throw "$checkpoint inspection integrity stdout/stderr differ from the pinned review receipt."
        }
        $extraPins.Add($integrityReceiptPin)
        $extraPins.Add($stdoutPin)
        $extraPins.Add($stderrPin)
    }

    return @($extraPins.ToArray())
}

function Assert-GpuStageRetirement023306Proof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $runDirectory = Join-Path (Join-Path $workspace '.analysis\gpu-baremetal\runs') $script:GpuRetirement023306RunId
    $last = $Journal.LastAction
    $verification = $Journal.FinalVerification
    $enumeration = if ($null -ne $verification) { $verification.FirmwareEnumeration } else { $null }
    if ([string]$Journal.RunId -cne $script:GpuRetirement023306RunId -or
        [IO.Path]::GetFullPath([string]$Journal.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [string]$Journal.SourceCommit -cne $script:GpuRetirement023306SourceCommit -or
        [string]$Journal.Phase -cne 'Complete' -or $null -ne $Journal.InProgress -or
        $null -eq $last -or [string]$last.Action -cne 'FinalVerify' -or $last.Succeeded -ne $true -or
        [string]$last.Status -cne 'FINAL_RESET_VERIFIED_GPU_UNATTESTED' -or
        [IO.Path]::GetFullPath([string]$last.Directory) -cne [IO.Path]::GetFullPath((Join-Path $workspace $script:GpuRetirement023306FinalActionRelativePath))) {
        throw 'The 023306 journal lacks the exact successful normal FinalVerify action.'
    }
    if ($null -eq $verification -or $verification.ResetBoundaryPassed -ne $true -or
        $verification.ActivationAttempted -ne $true -or $verification.SecureBoot -ne $true -or
        $verification.ActiveGpuProof -ne $false -or [string]$verification.GpuAttestation -cne 'UNATTESTED' -or
        @($verification.Errors).Count -ne 0 -or $null -eq $verification.Health -or
        $verification.Health.Passed -ne $true -or [string]$verification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$verification.Health.Aggregate -cne 'ABSENT' -or
        [int]$verification.Health.CpuCount -ne 20 -or [int]$verification.Health.ExpectedCpus -ne 20 -or
        [int]$verification.Health.CpuPassCount -ne 0 -or $null -eq $verification.Esp -or
        $verification.Esp.MatchesBaseline -ne $true -or
        [string]$verification.Esp.BootManagerSha256 -cne $script:GpuRetirement023306BootManagerSha256 -or
        [string]$verification.Esp.PartitionGuid -cne '187BC913-7576-4E6E-8D54-18EEB5C38FCD' -or
        [int]$verification.Esp.DiskNumber -ne 0) {
        throw 'The 023306 final verification does not satisfy reset, ABSENT health, Secure Boot, baseline ESP, and unattested-GPU gates.'
    }
    if ($null -eq $enumeration -or $enumeration.TextEqual -ne $true -or
        [string]$enumeration.BaselineSha256 -cne $script:GpuRetirement023306OriginalFirmwareSha256 -or
        [string]$enumeration.CurrentSha256 -cne $script:GpuRetirement023306OriginalFirmwareSha256 -or
        [string]$enumeration.CaptureContext -cne 'SystemEspMountedAtS' -or
        ($null -ne $enumeration.PSObject.Properties['ReviewedDifferenceAccepted'] -and $enumeration.ReviewedDifferenceAccepted -eq $true)) {
        throw 'The 023306 final verification does not match its pinned original firmware baseline.'
    }

    $backupFirmware = $Journal.Backup.FirmwareEnumeration
    $expectedFirmwarePath = Join-Path $workspace $script:GpuRetirement023306OriginalFirmwareRelativePath
    if ($null -eq $backupFirmware -or
        [IO.Path]::GetFullPath([string]$backupFirmware.Path) -cne [IO.Path]::GetFullPath($expectedFirmwarePath) -or
        [string]$backupFirmware.Sha256 -cne $script:GpuRetirement023306OriginalFirmwareSha256 -or
        [long]$backupFirmware.Bytes -ne $script:GpuRetirement023306OriginalFirmwareBytes) {
        throw 'The 023306 original firmware baseline path or pin differs from the fixed Backup action.'
    }
    $firmwarePin = Assert-GpuRetirementProfilePin -Pin $backupFirmware -ExpectedPath $expectedFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement023306OriginalFirmwareSha256 -ExpectedBytes $script:GpuRetirement023306OriginalFirmwareBytes `
        -WorkspaceRoot $workspace -Description 'Original 023306 firmware baseline'

    $referenceLog = $Journal.Backup.ReferenceLog
    $expectedReferencePath = Join-Path $workspace $script:GpuRetirement023306ReferenceLogRelativePath
    if ($null -eq $referenceLog -or
        [IO.Path]::GetFullPath([string]$referenceLog.Path) -cne [IO.Path]::GetFullPath($expectedReferencePath) -or
        [string]$referenceLog.Sha256 -cne $script:GpuRetirement023306ReferenceLogSha256 -or
        [long]$referenceLog.Bytes -ne $script:GpuRetirement023306ReferenceLogBytes) {
        throw 'The 023306 TPM reference log path or pin differs from the fixed Backup action.'
    }
    $referencePin = Assert-GpuRetirementProfilePin -Pin $referenceLog -ExpectedPath $expectedReferencePath `
        -ExpectedSha256 $script:GpuRetirement023306ReferenceLogSha256 -ExpectedBytes $script:GpuRetirement023306ReferenceLogBytes `
        -WorkspaceRoot $workspace -Description 'Pinned 023306 TPM reference log'

    $probePath = Join-Path $workspace $script:GpuRetirement023306ProbeRelativePath
    if ($null -eq $Journal.Tools -or $null -eq $Journal.Tools.Probe -or
        [IO.Path]::GetFullPath([string]$Journal.Tools.Probe.Path) -cne [IO.Path]::GetFullPath($probePath)) {
        throw 'The 023306 health probe path differs from the fixed build output.'
    }
    $probePin = Assert-GpuRetirementProfilePin -Pin $Journal.Tools.Probe -ExpectedPath $probePath `
        -ExpectedSha256 $script:GpuRetirement023306ProbeSha256 -ExpectedBytes $script:GpuRetirement023306ProbeBytes `
        -WorkspaceRoot $workspace -Description 'Pinned 023306 health probe'

    $finalResultPath = Join-Path $workspace $script:GpuRetirement023306FinalResultRelativePath
    $finalResultPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$finalResultPath; Sha256=$script:GpuRetirement023306FinalResultSha256; Bytes=$script:GpuRetirement023306FinalResultBytes }) `
        -ExpectedPath $finalResultPath -ExpectedSha256 $script:GpuRetirement023306FinalResultSha256 `
        -ExpectedBytes $script:GpuRetirement023306FinalResultBytes -WorkspaceRoot $workspace -Description 'Successful 023306 FinalVerify result'
    $finalResult = Get-Content -LiteralPath $finalResultPin.Path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    $resultVerification = $finalResult.Details.FinalVerification
    if ([string]$finalResult.Action -cne 'FinalVerify' -or
        [string]$finalResult.Status -cne 'FINAL_RESET_VERIFIED_GPU_UNATTESTED' -or
        $finalResult.Succeeded -ne $true -or
        [IO.Path]::GetFullPath([string]$finalResult.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [string]$finalResult.Details.Status -cne 'FINAL_RESET_VERIFIED_GPU_UNATTESTED' -or
        $finalResult.Details.Succeeded -ne $true -or $null -eq $resultVerification -or
        $resultVerification.ResetBoundaryPassed -ne $true -or $resultVerification.SecureBoot -ne $true -or
        [string]$resultVerification.GpuAttestation -cne 'UNATTESTED' -or
        $null -eq $resultVerification.Health -or $resultVerification.Health.Passed -ne $true -or
        [string]$resultVerification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$resultVerification.Health.Aggregate -cne 'ABSENT' -or
        [int]$resultVerification.Health.CpuCount -ne 20 -or [int]$resultVerification.Health.ExpectedCpus -ne 20 -or
        @($resultVerification.Errors).Count -ne 0) {
        throw 'The pinned 023306 FinalVerify result is not the exact successful normal reset verification.'
    }

    return @($firmwarePin,$referencePin,$probePin,$finalResultPin)
}

function Assert-GpuStageRetirement032117Proof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $runDirectory = Join-Path (Join-Path $workspace '.analysis\gpu-baremetal\runs') $script:GpuRetirement032117RunId
    $last = $Journal.LastAction
    $verification = $Journal.FinalVerification
    $enumeration = if ($null -ne $verification) { $verification.FirmwareEnumeration } else { $null }
    if ([string]$Journal.RunId -cne $script:GpuRetirement032117RunId -or
        [IO.Path]::GetFullPath([string]$Journal.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [string]$Journal.SourceCommit -cne $script:GpuRetirement032117SourceCommit -or
        [string]$Journal.Phase -cne 'Complete' -or $null -ne $Journal.InProgress -or
        $null -eq $last -or [string]$last.Action -cne 'FinalVerify' -or $last.Succeeded -ne $true -or
        [string]$last.Status -cne 'FINAL_RESET_VERIFIED_GPU_UNATTESTED' -or
        [IO.Path]::GetFullPath([string]$last.Directory) -cne [IO.Path]::GetFullPath((Join-Path $workspace $script:GpuRetirement032117FinalActionRelativePath))) {
        throw 'The 032117 journal lacks the exact successful normal FinalVerify action.'
    }
    if ($null -eq $verification -or $verification.ResetBoundaryPassed -ne $true -or
        $verification.ActivationAttempted -ne $true -or $verification.SecureBoot -ne $true -or
        $verification.ActiveGpuProof -ne $false -or [string]$verification.GpuAttestation -cne 'UNATTESTED' -or
        @($verification.Errors).Count -ne 0 -or $null -eq $verification.Health -or
        $verification.Health.Passed -ne $true -or [string]$verification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$verification.Health.Aggregate -cne 'ABSENT' -or
        [int]$verification.Health.CpuCount -ne 20 -or [int]$verification.Health.ExpectedCpus -ne 20 -or
        [int]$verification.Health.CpuPassCount -ne 0 -or $null -eq $verification.Esp -or
        $verification.Esp.MatchesBaseline -ne $true -or
        [string]$verification.Esp.BootManagerSha256 -cne $script:GpuRetirement032117BootManagerSha256 -or
        [string]$verification.Esp.PartitionGuid -cne '187BC913-7576-4E6E-8D54-18EEB5C38FCD' -or
        [int]$verification.Esp.DiskNumber -ne 0 -or [int]$Journal.Host.LogicalCpus -ne 20) {
        throw 'The 032117 final verification does not satisfy reset, ABSENT health, Secure Boot, baseline ESP, and unattested-GPU gates.'
    }
    if ($null -eq $enumeration -or $enumeration.TextEqual -ne $true -or
        [string]$enumeration.BaselineSha256 -cne $script:GpuRetirement032117FirmwareSha256 -or
        [string]$enumeration.CurrentSha256 -cne $script:GpuRetirement032117FirmwareSha256 -or
        [string]$enumeration.CaptureContext -cne 'SystemEspMountedAtS' -or
        ($null -ne $enumeration.PSObject.Properties['ReviewedDifferenceAccepted'] -and $enumeration.ReviewedDifferenceAccepted -eq $true)) {
        throw 'The 032117 final verification does not match its pinned original firmware baseline.'
    }

    $backupFirmware = $Journal.Backup.FirmwareEnumeration
    $expectedFirmwarePath = Join-Path $workspace $script:GpuRetirement032117FirmwareRelativePath
    if ($null -eq $backupFirmware -or
        [IO.Path]::GetFullPath([string]$backupFirmware.Path) -cne [IO.Path]::GetFullPath($expectedFirmwarePath) -or
        [string]$backupFirmware.Sha256 -cne $script:GpuRetirement032117FirmwareSha256 -or
        [long]$backupFirmware.Bytes -ne $script:GpuRetirement032117FirmwareBytes) {
        throw 'The 032117 original firmware baseline path or pin differs from the fixed Backup action.'
    }
    $firmwarePin = Assert-GpuRetirementProfilePin -Pin $backupFirmware -ExpectedPath $expectedFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement032117FirmwareSha256 -ExpectedBytes $script:GpuRetirement032117FirmwareBytes `
        -WorkspaceRoot $workspace -Description 'Original 032117 firmware baseline'

    $referenceLog = $Journal.Backup.ReferenceLog
    $expectedReferencePath = Join-Path $workspace $script:GpuRetirement032117ReferenceLogRelativePath
    if ($null -eq $referenceLog -or
        [IO.Path]::GetFullPath([string]$referenceLog.Path) -cne [IO.Path]::GetFullPath($expectedReferencePath) -or
        [string]$referenceLog.Sha256 -cne $script:GpuRetirement032117ReferenceLogSha256 -or
        [long]$referenceLog.Bytes -ne $script:GpuRetirement032117ReferenceLogBytes) {
        throw 'The 032117 TPM reference log path or pin differs from the fixed Backup action.'
    }
    $referencePin = Assert-GpuRetirementProfilePin -Pin $referenceLog -ExpectedPath $expectedReferencePath `
        -ExpectedSha256 $script:GpuRetirement032117ReferenceLogSha256 -ExpectedBytes $script:GpuRetirement032117ReferenceLogBytes `
        -WorkspaceRoot $workspace -Description 'Pinned 032117 TPM reference log'

    $probePath = Join-Path $workspace $script:GpuRetirement032117ProbeRelativePath
    if ($null -eq $Journal.Tools -or $null -eq $Journal.Tools.Probe -or
        [IO.Path]::GetFullPath([string]$Journal.Tools.Probe.Path) -cne [IO.Path]::GetFullPath($probePath)) {
        throw 'The 032117 health probe path differs from the fixed build output.'
    }
    $probePin = Assert-GpuRetirementProfilePin -Pin $Journal.Tools.Probe -ExpectedPath $probePath `
        -ExpectedSha256 $script:GpuRetirement032117ProbeSha256 -ExpectedBytes $script:GpuRetirement032117ProbeBytes `
        -WorkspaceRoot $workspace -Description 'Pinned 032117 health probe'

    $finalResultPath = Join-Path $workspace $script:GpuRetirement032117FinalResultRelativePath
    $finalResultPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$finalResultPath; Sha256=$script:GpuRetirement032117FinalResultSha256; Bytes=$script:GpuRetirement032117FinalResultBytes }) `
        -ExpectedPath $finalResultPath -ExpectedSha256 $script:GpuRetirement032117FinalResultSha256 `
        -ExpectedBytes $script:GpuRetirement032117FinalResultBytes -WorkspaceRoot $workspace -Description 'Successful 032117 FinalVerify result'
    $finalResult = [IO.File]::ReadAllText($finalResultPin.Path) | ConvertFrom-Json -ErrorAction Stop
    $resultVerification = $finalResult.Details.FinalVerification
    if ([string]$finalResult.Action -cne 'FinalVerify' -or
        [string]$finalResult.Status -cne 'FINAL_RESET_VERIFIED_GPU_UNATTESTED' -or
        $finalResult.Succeeded -ne $true -or
        [IO.Path]::GetFullPath([string]$finalResult.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [string]$finalResult.Details.Status -cne 'FINAL_RESET_VERIFIED_GPU_UNATTESTED' -or
        $finalResult.Details.Succeeded -ne $true -or $null -eq $resultVerification -or
        $resultVerification.ResetBoundaryPassed -ne $true -or $resultVerification.SecureBoot -ne $true -or
        $resultVerification.ActivationAttempted -ne $true -or
        [string]$resultVerification.GpuAttestation -cne 'UNATTESTED' -or
        $resultVerification.ActiveGpuProof -ne $false -or $null -eq $resultVerification.Health -or
        $resultVerification.Health.Passed -ne $true -or [string]$resultVerification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$resultVerification.Health.Aggregate -cne 'ABSENT' -or
        [int]$resultVerification.Health.CpuCount -ne 20 -or [int]$resultVerification.Health.ExpectedCpus -ne 20 -or
        [int]$resultVerification.Health.CpuPassCount -ne 0 -or
        $null -eq $resultVerification.Esp -or $resultVerification.Esp.MatchesBaseline -ne $true -or
        [string]$resultVerification.Esp.BootManagerSha256 -cne $script:GpuRetirement032117BootManagerSha256 -or
        [string]$resultVerification.Esp.PartitionGuid -cne '187BC913-7576-4E6E-8D54-18EEB5C38FCD' -or
        [int]$resultVerification.Esp.DiskNumber -ne 0 -or
        $null -eq $resultVerification.FirmwareEnumeration -or
        $resultVerification.FirmwareEnumeration.TextEqual -ne $true -or
        [string]$resultVerification.FirmwareEnumeration.BaselineSha256 -cne $script:GpuRetirement032117FirmwareSha256 -or
        [string]$resultVerification.FirmwareEnumeration.CurrentSha256 -cne $script:GpuRetirement032117FirmwareSha256 -or
        [string]$resultVerification.FirmwareEnumeration.CaptureContext -cne 'SystemEspMountedAtS' -or
        ($null -ne $resultVerification.FirmwareEnumeration.PSObject.Properties['ReviewedDifferenceAccepted'] -and
         $resultVerification.FirmwareEnumeration.ReviewedDifferenceAccepted -eq $true) -or
        @($resultVerification.Errors).Count -ne 0) {
        throw 'The pinned 032117 FinalVerify result is not the exact successful normal reset verification.'
    }

    return @($firmwarePin,$referencePin,$probePin,$finalResultPin)
}

function Select-GpuStageRetirement201140Profile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ReadinessPath
    )

    if ($script:GpuRetirement201140ProfileSelected -or $script:GpuRetirement224018ProfileSelected -or $script:GpuRetirement023306ProfileSelected -or $script:GpuRetirement032117ProfileSelected -or $script:GpuRetirement042142ProfileSelected -or $script:GpuRetirement062106ProfileSelected -or $script:GpuRetirement160027ProfileSelected) {
        throw 'A completed-run retirement profile is already bound in this module instance.'
    }
    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $readinessRoot = Join-Path $workspace '.analysis\gpu-baremetal-readiness'
    if (-not [IO.Directory]::Exists($readinessRoot)) { throw 'The workspace readiness evidence directory is missing.' }
    $readinessRootFull = [IO.Path]::GetFullPath($readinessRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $readinessFull = Assert-GpuRetirementSafeWorkspaceFile -Path $ReadinessPath -WorkspaceRoot $workspace
    $prefix = $readinessRootFull + [IO.Path]::DirectorySeparatorChar
    if (-not $readinessFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ReadinessPath must name a report under .analysis\gpu-baremetal-readiness.'
    }
    $relative = $readinessFull.Substring($prefix.Length)
    if ($relative -notmatch '^([0-9a-fA-F]{32})\\readiness\.json$') {
        throw 'ReadinessPath must explicitly name <guid>\readiness.json under .analysis\gpu-baremetal-readiness.'
    }
    $readinessGuid = $Matches[1].ToLowerInvariant()
    $readinessDirectory = Join-Path $readinessRootFull $readinessGuid
    if ([IO.Path]::GetFullPath([IO.Path]::GetDirectoryName($readinessFull)) -cne [IO.Path]::GetFullPath($readinessDirectory)) {
        throw 'Readiness report directory does not match its GUID path.'
    }
    $readinessPin = Get-GpuStageRetirementPin -Path $readinessFull
    $readiness = Get-Content -LiteralPath $readinessFull -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    if ($readiness.SchemaVersion -ne 1 -or [string]$readiness.Status -cne 'BLOCKED_READINESS' -or
        $readiness.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$readiness.EvidencePath) -cne $readinessFull -or
        [IO.Path]::GetFullPath([string]$readiness.EvidenceDirectory) -cne [IO.Path]::GetFullPath($readinessDirectory) -or
        $null -eq $readiness.Selector -or $readiness.Selector.Exists -ne $false -or $readiness.Selector.Valid -ne $true -or
        $null -ne $readiness.Selector.Error -or $null -eq $readiness.Cleanup -or $readiness.Cleanup.Succeeded -ne $true -or
        @($readiness.Cleanup.Errors).Count -ne 0 -or $null -eq $readiness.Esp -or
        $readiness.Esp.Inspected -ne $true -or [string]$readiness.Esp.MountStateAfterCleanup -cne 'Unmounted' -or
        $null -eq $readiness.Effects -or $readiness.Effects.EspFilesChanged -ne $false -or
        $readiness.Effects.FirmwareChanged -ne $false -or $readiness.Effects.BootStateChanged -ne $false -or
        $readiness.Effects.EspTemporarilyMounted -ne $true -or $null -eq $readiness.ExistingStage -or
        $readiness.ExistingStage.Exists -ne $true -or $readiness.ExistingStage.Complete -ne $true) {
        throw 'The explicit readiness report is not a complete, cleanly unmounted BLOCKED_READINESS capture with no host changes and no active selector.'
    }
    $script:GpuRetirement201140ProfileSelected = $true
    $script:GpuRetirementSelectedReadinessPath = $readinessFull
    $script:GpuRetirementProfile = '201140'
    $script:GpuRetirementRunId = $script:GpuRetirement201140RunId
    $script:GpuRetirementAuditRelativePath = '.analysis\gpu-baremetal-readiness\' + $readinessGuid + '\readiness.json'
    $script:GpuRetirementAuditSha256 = $readinessPin.Sha256
    $script:GpuRetirementAuditBytes = [long]$readinessPin.Bytes
    $script:GpuRetirementJournalSha256 = $script:GpuRetirement201140JournalSha256
    $script:GpuRetirementJournalBytes = $script:GpuRetirement201140JournalBytes
    $script:GpuRetirementArchiveSha256 = $script:GpuRetirement201140ArchiveSha256
    $script:GpuRetirementArchiveBytes = $script:GpuRetirement201140ArchiveBytes
    $script:GpuRetirementSourceCommit = $script:GpuRetirement201140SourceCommit
    $script:GpuRetirementProbeSha256 = $script:GpuRetirement201140ProbeSha256
    $script:GpuRetirementProbeBytes = $script:GpuRetirement201140ProbeBytes
    $script:GpuRetirementReferenceLogSha256 = $script:GpuRetirement201140ReferenceLogSha256
    $script:GpuRetirementReferenceLogBytes = $script:GpuRetirement201140ReferenceLogBytes
    $script:GpuRetirementReviewedFirmwareRelativePath = $script:GpuRetirement201140FreshFirmwareRelativePath
    $script:GpuRetirementReviewedFirmwareSha256 = $script:GpuRetirement201140CurrentFirmwareSha256
    $script:GpuRetirementReviewedFirmwareBytes = $script:GpuRetirement201140FirmwareBytes
    return [pscustomobject]@{
        Profile = '201140'
        RunId = $script:GpuRetirementRunId
        ReadinessPath = $readinessFull
        ReadinessPin = $readinessPin
        JournalSha256 = $script:GpuRetirementJournalSha256
        ArchiveSha256 = $script:GpuRetirementArchiveSha256
        SourceCommit = $script:GpuRetirementSourceCommit
    }
}

function Select-GpuStageRetirement224018Profile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ReadinessPath
    )

    if ($script:GpuRetirement201140ProfileSelected -or $script:GpuRetirement224018ProfileSelected -or $script:GpuRetirement023306ProfileSelected -or $script:GpuRetirement032117ProfileSelected -or $script:GpuRetirement042142ProfileSelected -or $script:GpuRetirement062106ProfileSelected -or $script:GpuRetirement160027ProfileSelected) {
        throw 'A completed-run retirement profile is already bound in this module instance.'
    }
    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $readinessRoot = Join-Path $workspace '.analysis\gpu-baremetal-readiness'
    if (-not [IO.Directory]::Exists($readinessRoot)) { throw 'The workspace readiness evidence directory is missing.' }
    $readinessRootFull = [IO.Path]::GetFullPath($readinessRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $readinessFull = Assert-GpuRetirementSafeWorkspaceFile -Path $ReadinessPath -WorkspaceRoot $workspace
    $prefix = $readinessRootFull + [IO.Path]::DirectorySeparatorChar
    if (-not $readinessFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ReadinessPath must name a report under .analysis\gpu-baremetal-readiness.'
    }
    $relative = $readinessFull.Substring($prefix.Length)
    if ($relative -notmatch '^([0-9a-fA-F]{32})\\readiness\.json$') {
        throw 'ReadinessPath must explicitly name <guid>\readiness.json under .analysis\gpu-baremetal-readiness.'
    }
    $readinessGuid = $Matches[1].ToLowerInvariant()
    $readinessDirectory = Join-Path $readinessRootFull $readinessGuid
    if ([IO.Path]::GetFullPath([IO.Path]::GetDirectoryName($readinessFull)) -cne [IO.Path]::GetFullPath($readinessDirectory)) {
        throw 'Readiness report directory does not match its GUID path.'
    }
    $readinessPin = Get-GpuStageRetirementPin -Path $readinessFull
    $readiness = Get-Content -LiteralPath $readinessFull -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    if ($readiness.SchemaVersion -ne 1 -or [string]$readiness.Status -cne 'BLOCKED_READINESS' -or
        $readiness.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$readiness.EvidencePath) -cne $readinessFull -or
        [IO.Path]::GetFullPath([string]$readiness.EvidenceDirectory) -cne [IO.Path]::GetFullPath($readinessDirectory) -or
        $null -eq $readiness.Selector -or $readiness.Selector.Exists -ne $false -or $readiness.Selector.Valid -ne $true -or
        $null -ne $readiness.Selector.Error -or $null -eq $readiness.Cleanup -or $readiness.Cleanup.Succeeded -ne $true -or
        @($readiness.Cleanup.Errors).Count -ne 0 -or $null -eq $readiness.Esp -or
        $readiness.Esp.Inspected -ne $true -or [string]$readiness.Esp.MountStateAfterCleanup -cne 'Unmounted' -or
        $null -eq $readiness.Effects -or $readiness.Effects.EspFilesChanged -ne $false -or
        $readiness.Effects.FirmwareChanged -ne $false -or $readiness.Effects.BootStateChanged -ne $false -or
        $readiness.Effects.EspTemporarilyMounted -ne $true -or $null -eq $readiness.ExistingStage -or
        $readiness.ExistingStage.Exists -ne $true -or $readiness.ExistingStage.Complete -ne $true) {
        throw 'The explicit readiness report is not a complete, cleanly unmounted BLOCKED_READINESS capture with no host changes and no active selector.'
    }
    $script:GpuRetirement224018ProfileSelected = $true
    $script:GpuRetirementSelectedReadinessPath = $readinessFull
    $script:GpuRetirementProfile = '224018'
    $script:GpuRetirementRunId = $script:GpuRetirement224018RunId
    $script:GpuRetirementAuditRelativePath = '.analysis\gpu-baremetal-readiness\' + $readinessGuid + '\readiness.json'
    $script:GpuRetirementAuditSha256 = $readinessPin.Sha256
    $script:GpuRetirementAuditBytes = [long]$readinessPin.Bytes
    $script:GpuRetirementJournalSha256 = $script:GpuRetirement224018JournalSha256
    $script:GpuRetirementJournalBytes = $script:GpuRetirement224018JournalBytes
    $script:GpuRetirementArchiveSha256 = $script:GpuRetirement224018ArchiveSha256
    $script:GpuRetirementArchiveBytes = $script:GpuRetirement224018ArchiveBytes
    $script:GpuRetirementSourceCommit = $script:GpuRetirement224018SourceCommit
    $script:GpuRetirementProbeSha256 = $script:GpuRetirement224018ProbeSha256
    $script:GpuRetirementProbeBytes = $script:GpuRetirement224018ProbeBytes
    $script:GpuRetirementReferenceLogSha256 = $script:GpuRetirement224018ReferenceLogSha256
    $script:GpuRetirementReferenceLogBytes = $script:GpuRetirement224018ReferenceLogBytes
    $script:GpuRetirementReviewedFirmwareRelativePath = $script:GpuRetirement224018FreshFirmwareRelativePath
    $script:GpuRetirementReviewedFirmwareSha256 = $script:GpuRetirement224018CurrentFirmwareSha256
    $script:GpuRetirementReviewedFirmwareBytes = $script:GpuRetirement224018CurrentFirmwareBytes
    return [pscustomobject]@{
        Profile = '224018'
        RunId = $script:GpuRetirementRunId
        ReadinessPath = $readinessFull
        ReadinessPin = $readinessPin
        JournalSha256 = $script:GpuRetirementJournalSha256
        ArchiveSha256 = $script:GpuRetirementArchiveSha256
        SourceCommit = $script:GpuRetirementSourceCommit
    }
}

function Select-GpuStageRetirement023306Profile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ReadinessPath
    )

    if ($script:GpuRetirement201140ProfileSelected -or $script:GpuRetirement224018ProfileSelected -or $script:GpuRetirement023306ProfileSelected -or $script:GpuRetirement032117ProfileSelected -or $script:GpuRetirement042142ProfileSelected -or $script:GpuRetirement062106ProfileSelected -or $script:GpuRetirement160027ProfileSelected) {
        throw 'A completed-run retirement profile is already bound in this module instance.'
    }
    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $readinessRoot = Join-Path $workspace '.analysis\gpu-baremetal-readiness'
    if (-not [IO.Directory]::Exists($readinessRoot)) { throw 'The workspace readiness evidence directory is missing.' }
    $readinessRootFull = [IO.Path]::GetFullPath($readinessRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $readinessFull = Assert-GpuRetirementSafeWorkspaceFile -Path $ReadinessPath -WorkspaceRoot $workspace
    $prefix = $readinessRootFull + [IO.Path]::DirectorySeparatorChar
    if (-not $readinessFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ReadinessPath must name a report under .analysis\gpu-baremetal-readiness.'
    }
    $relative = $readinessFull.Substring($prefix.Length)
    if ($relative -notmatch '^([0-9a-fA-F]{32})\\readiness\.json$') {
        throw 'ReadinessPath must explicitly name <guid>\readiness.json under .analysis\gpu-baremetal-readiness.'
    }
    $readinessGuid = $Matches[1].ToLowerInvariant()
    $readinessDirectory = Join-Path $readinessRootFull $readinessGuid
    if ([IO.Path]::GetFullPath([IO.Path]::GetDirectoryName($readinessFull)) -cne [IO.Path]::GetFullPath($readinessDirectory)) {
        throw 'Readiness report directory does not match its GUID path.'
    }
    $readinessPin = Get-GpuStageRetirementPin -Path $readinessFull
    $readiness = Get-Content -LiteralPath $readinessFull -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    if ($readiness.SchemaVersion -ne 1 -or [string]$readiness.Status -cne 'BLOCKED_READINESS' -or
        $readiness.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$readiness.EvidencePath) -cne $readinessFull -or
        [IO.Path]::GetFullPath([string]$readiness.EvidenceDirectory) -cne [IO.Path]::GetFullPath($readinessDirectory) -or
        $null -eq $readiness.Selector -or $readiness.Selector.Exists -ne $false -or $readiness.Selector.Valid -ne $true -or
        $null -ne $readiness.Selector.Error -or $null -eq $readiness.Cleanup -or $readiness.Cleanup.Succeeded -ne $true -or
        @($readiness.Cleanup.Errors).Count -ne 0 -or $null -eq $readiness.Esp -or
        $readiness.Esp.Inspected -ne $true -or [string]$readiness.Esp.MountStateAfterCleanup -cne 'Unmounted' -or
        $null -eq $readiness.Effects -or $readiness.Effects.EspFilesChanged -ne $false -or
        $readiness.Effects.FirmwareChanged -ne $false -or $readiness.Effects.BootStateChanged -ne $false -or
        $readiness.Effects.EspTemporarilyMounted -ne $true -or $null -eq $readiness.ExistingStage -or
        $readiness.ExistingStage.Exists -ne $true -or $readiness.ExistingStage.Complete -ne $true) {
        throw 'The explicit readiness report is not a complete, cleanly unmounted BLOCKED_READINESS capture with no host changes and no active selector.'
    }

    $script:GpuRetirement023306ProfileSelected = $true
    $script:GpuRetirementSelectedReadinessPath = $readinessFull
    $script:GpuRetirementProfile = '023306'
    $script:GpuRetirementRunId = $script:GpuRetirement023306RunId
    $script:GpuRetirementAuditRelativePath = '.analysis\gpu-baremetal-readiness\' + $readinessGuid + '\readiness.json'
    $script:GpuRetirementAuditSha256 = $readinessPin.Sha256
    $script:GpuRetirementAuditBytes = [long]$readinessPin.Bytes
    $script:GpuRetirementJournalSha256 = $script:GpuRetirement023306JournalSha256
    $script:GpuRetirementJournalBytes = $script:GpuRetirement023306JournalBytes
    $script:GpuRetirementArchiveSha256 = $script:GpuRetirement023306ArchiveSha256
    $script:GpuRetirementArchiveBytes = $script:GpuRetirement023306ArchiveBytes
    $script:GpuRetirementSourceCommit = $script:GpuRetirement023306SourceCommit
    $script:GpuRetirementProbeSha256 = $script:GpuRetirement023306ProbeSha256
    $script:GpuRetirementProbeBytes = $script:GpuRetirement023306ProbeBytes
    $script:GpuRetirementReferenceLogSha256 = $script:GpuRetirement023306ReferenceLogSha256
    $script:GpuRetirementReferenceLogBytes = $script:GpuRetirement023306ReferenceLogBytes
    return [pscustomobject]@{
        Profile = '023306'
        RunId = $script:GpuRetirementRunId
        ReadinessPath = $readinessFull
        ReadinessPin = $readinessPin
        JournalSha256 = $script:GpuRetirementJournalSha256
        ArchiveSha256 = $script:GpuRetirementArchiveSha256
        SourceCommit = $script:GpuRetirementSourceCommit
    }
}

function Select-GpuStageRetirement032117Profile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ReadinessPath
    )

    if ($script:GpuRetirement201140ProfileSelected -or $script:GpuRetirement224018ProfileSelected -or
        $script:GpuRetirement023306ProfileSelected -or $script:GpuRetirement032117ProfileSelected -or $script:GpuRetirement042142ProfileSelected -or
        $script:GpuRetirement062106ProfileSelected -or $script:GpuRetirement160027ProfileSelected) {
        throw 'A completed-run retirement profile is already bound in this module instance.'
    }
    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $readinessRoot = Join-Path $workspace '.analysis\gpu-baremetal-readiness'
    if (-not [IO.Directory]::Exists($readinessRoot)) { throw 'The workspace readiness evidence directory is missing.' }
    $readinessRootFull = [IO.Path]::GetFullPath($readinessRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $readinessFull = Assert-GpuRetirementSafeWorkspaceFile -Path $ReadinessPath -WorkspaceRoot $workspace
    $prefix = $readinessRootFull + [IO.Path]::DirectorySeparatorChar
    if (-not $readinessFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ReadinessPath must name a report under .analysis\gpu-baremetal-readiness.'
    }
    $relative = $readinessFull.Substring($prefix.Length)
    if ($relative -notmatch '^([0-9a-fA-F]{32})\\readiness\.json$') {
        throw 'ReadinessPath must explicitly name <guid>\readiness.json under .analysis\gpu-baremetal-readiness.'
    }
    $readinessGuid = $Matches[1].ToLowerInvariant()
    $readinessDirectory = Join-Path $readinessRootFull $readinessGuid
    if ([IO.Path]::GetFullPath([IO.Path]::GetDirectoryName($readinessFull)) -cne [IO.Path]::GetFullPath($readinessDirectory)) {
        throw 'Readiness report directory does not match its GUID path.'
    }
    $readinessPin = Get-GpuStageRetirementPin -Path $readinessFull
    $readiness = [IO.File]::ReadAllText($readinessFull) | ConvertFrom-Json -ErrorAction Stop
    if ($readiness.SchemaVersion -ne 1 -or [string]$readiness.Status -cne 'BLOCKED_READINESS' -or
        $readiness.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$readiness.EvidencePath) -cne $readinessFull -or
        [IO.Path]::GetFullPath([string]$readiness.EvidenceDirectory) -cne [IO.Path]::GetFullPath($readinessDirectory) -or
        $null -eq $readiness.Selector -or $readiness.Selector.Exists -ne $false -or $readiness.Selector.Valid -ne $true -or
        $null -ne $readiness.Selector.Error -or $null -eq $readiness.Cleanup -or $readiness.Cleanup.Succeeded -ne $true -or
        @($readiness.Cleanup.Errors).Count -ne 0 -or $null -eq $readiness.Esp -or
        $readiness.Esp.Inspected -ne $true -or [string]$readiness.Esp.MountStateAfterCleanup -cne 'Unmounted' -or
        $null -eq $readiness.Effects -or $readiness.Effects.EspFilesChanged -ne $false -or
        $readiness.Effects.FirmwareChanged -ne $false -or $readiness.Effects.BootStateChanged -ne $false -or
        $readiness.Effects.EspTemporarilyMounted -ne $true -or $null -eq $readiness.ExistingStage -or
        $readiness.ExistingStage.Exists -ne $true -or $readiness.ExistingStage.Complete -ne $true) {
        throw 'The explicit readiness report is not a complete, cleanly unmounted BLOCKED_READINESS capture with no host changes and no active selector.'
    }

    $script:GpuRetirement032117ProfileSelected = $true
    $script:GpuRetirementSelectedReadinessPath = $readinessFull
    $script:GpuRetirementProfile = '032117'
    $script:GpuRetirementRunId = $script:GpuRetirement032117RunId
    $script:GpuRetirementAuditRelativePath = '.analysis\gpu-baremetal-readiness\' + $readinessGuid + '\readiness.json'
    $script:GpuRetirementAuditSha256 = $readinessPin.Sha256
    $script:GpuRetirementAuditBytes = [long]$readinessPin.Bytes
    $script:GpuRetirementJournalSha256 = $script:GpuRetirement032117JournalSha256
    $script:GpuRetirementJournalBytes = $script:GpuRetirement032117JournalBytes
    $script:GpuRetirementArchiveSha256 = $script:GpuRetirement032117ArchiveSha256
    $script:GpuRetirementArchiveBytes = $script:GpuRetirement032117ArchiveBytes
    $script:GpuRetirementSourceCommit = $script:GpuRetirement032117SourceCommit
    $script:GpuRetirementProbeSha256 = $script:GpuRetirement032117ProbeSha256
    $script:GpuRetirementProbeBytes = $script:GpuRetirement032117ProbeBytes
    $script:GpuRetirementReferenceLogSha256 = $script:GpuRetirement032117ReferenceLogSha256
    $script:GpuRetirementReferenceLogBytes = $script:GpuRetirement032117ReferenceLogBytes
    return [pscustomobject]@{
        Profile = '032117'
        RunId = $script:GpuRetirementRunId
        ReadinessPath = $readinessFull
        ReadinessPin = $readinessPin
        JournalSha256 = $script:GpuRetirementJournalSha256
        ArchiveSha256 = $script:GpuRetirementArchiveSha256
        SourceCommit = $script:GpuRetirementSourceCommit
    }
}

function Assert-GpuStageRetirement042142StageInventory {
    param(
        [Parameter(Mandatory = $true)][object[]]$Entries,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($Entries.Count -ne $script:GpuRetirement042142StagePins.Count) {
        throw "$Description must contain exactly the five reviewed 042142 stage files."
    }
    $found = @{}
    foreach ($entry in $Entries) {
        $path = [string](Get-GpuRetirementProperty -Object $entry -Name 'RelativePath')
        if ($path -notmatch '^EFI\\insecure-boot\\([A-Za-z0-9._-]+)$') {
            throw "$Description contains a stage path outside the reviewed EFI directory."
        }
        $name = $Matches[1]
        $expected = @($script:GpuRetirement042142StagePins | Where-Object { $_.Name -ceq $name }) | Select-Object -First 1
        if ($null -eq $expected -or $found.ContainsKey($name)) {
            throw "$Description contains an unexpected or duplicate stage file: $name"
        }
        if ($null -eq $entry.PSObject.Properties['Kind'] -or [string]$entry.Kind -cne 'File') {
            throw "$Description contains a non-file stage entry: $name"
        }
        if ($null -eq $entry.PSObject.Properties['Hashed'] -or $entry.Hashed -ne $true) {
            throw "$Description contains an unhashed stage entry: $name"
        }
        if ([string]$entry.Sha256 -cne [string]$expected.Sha256 -or [long]$entry.Bytes -ne [long]$expected.Bytes) {
            throw "$Description pin differs from the fixed reviewed 042142 file: $name"
        }
        $found[$name] = $true
    }
    foreach ($expected in $script:GpuRetirement042142StagePins) {
        if (-not $found.ContainsKey([string]$expected.Name)) { throw "$Description is missing $($expected.Name)." }
    }
    return @($script:GpuRetirement042142StagePins)
}

function Assert-GpuStageRetirement042142Readiness {
    param([Parameter(Mandatory = $true)][object]$Readiness, [Parameter(Mandatory = $true)][string]$ReadinessFullPath)

    $source = $Readiness.Source
    $preflight = $Readiness.Preflight
    if ($null -eq $source -or $null -eq $preflight) {
        throw 'The explicit readiness report is missing source or host preflight evidence.'
    }
    $sourceError = $null
    $sourceErrorProperty = $source.PSObject.Properties['Error']
    if ($null -ne $sourceErrorProperty) { $sourceError = [string]$sourceErrorProperty.Value }
    $preflightErrorProperty = $preflight.PSObject.Properties['Error']
    $reportedErrors = @()
    $reportedErrorsProperty = $Readiness.PSObject.Properties['Errors']
    if ($null -ne $reportedErrorsProperty) { $reportedErrors = @($reportedErrorsProperty.Value) }
    $blockers = @($Readiness.Blockers)
    $expectedBlocker = 'EFI\\insecure-boot is retained; ownership-reviewed retirement is required before a new stage. Do not delete it generically or retry Stage.'

    if ($Readiness.SchemaVersion -ne 1 -or [string]$Readiness.Status -cne 'BLOCKED_READINESS' -or
        $Readiness.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$Readiness.EvidencePath) -cne [IO.Path]::GetFullPath($ReadinessFullPath) -or
        [string]$source.Branch -cne 'feat/smbios-boot-serial-probe' -or
        [string]$source.Commit -notmatch '^[0-9a-fA-F]{40}$' -or
        $source.Dirty -ne $false -or [int]$source.DirtyEntryCount -ne 0 -or
        -not [string]::IsNullOrWhiteSpace($sourceError) -or
        $null -eq $preflight.PSObject.Properties['Complete'] -or $preflight.Complete -ne $true -or
        $null -eq $preflight.PSObject.Properties['InventoryComplete'] -or $preflight.InventoryComplete -ne $true -or
        $null -eq $preflight.PSObject.Properties['IntelPresent'] -or $preflight.IntelPresent -ne $true -or
        $null -eq $preflight.PSObject.Properties['NvidiaPresent'] -or $preflight.NvidiaPresent -ne $true -or
        $null -eq $preflightErrorProperty -or $null -ne $preflightErrorProperty.Value -or
        $blockers.Count -ne 1 -or [string]$blockers[0] -cne $expectedBlocker -or $reportedErrors.Count -ne 0 -or
        [int]$Readiness.Host.LogicalCpus -ne 20 -or $Readiness.Host.SecureBoot -ne $true -or
        $Readiness.Recovery.Valid -ne $true -or $Readiness.Recovery.Disk.IsVirtual -ne $false -or
        $null -eq $Readiness.Selector -or $Readiness.Selector.Exists -ne $false -or $Readiness.Selector.Valid -ne $true -or
        $null -ne $Readiness.Selector.Error -or $null -eq $Readiness.Cleanup -or $Readiness.Cleanup.Succeeded -ne $true -or
        @($Readiness.Cleanup.Errors).Count -ne 0 -or $null -eq $Readiness.Esp -or
        $Readiness.Esp.Inspected -ne $true -or [string]$Readiness.Esp.MountStateAfterCleanup -cne 'Unmounted' -or
        $null -eq $Readiness.Effects -or $Readiness.Effects.EspFilesChanged -ne $false -or
        $Readiness.Effects.FirmwareChanged -ne $false -or $Readiness.Effects.BootStateChanged -ne $false -or
        $Readiness.Effects.EspTemporarilyMounted -ne $true -or $null -eq $Readiness.ExistingStage -or
        $Readiness.ExistingStage.Exists -ne $true -or $Readiness.ExistingStage.Complete -ne $true -or
        [int]$Readiness.ExistingStage.EntryCount -ne 5 -or [int]$Readiness.ExistingStage.FileCount -ne 5 -or
        @($Readiness.ExistingStage.UnknownEntries).Count -ne 0 -or
        @($Readiness.ExistingStage.UnsafeEntries).Count -ne 0 -or @($Readiness.ExistingStage.Errors).Count -ne 0 -or
        $null -eq $Readiness.ExistingResidue -or $Readiness.ExistingResidue.Clear -ne $true -or
        @($Readiness.ExistingResidue.Residues).Count -ne 0) {
        throw 'The explicit readiness report is not a clean, unmounted BLOCKED_READINESS capture for the exact retained 042142 stage.'
    }
    if ([string]$Readiness.Esp.Identity.Volume -cne $script:GpuRetirement042142EspVolume -or
        [int]$Readiness.Esp.Identity.DiskNumber -ne $script:GpuRetirement042142EspDiskNumber -or
        [string]$Readiness.Esp.Identity.PartitionGuid -ine $script:GpuRetirement042142EspPartitionGuid -or
        [string]$Readiness.Esp.Identity.DiskUniqueId -cne 'eui.0000000001000000E4D25CDF32275501' -or
        [string]$Readiness.Esp.BootManagerPin.Sha256 -cne $script:GpuRetirement042142BootManagerSha256 -or
        [long]$Readiness.Esp.BootManagerPin.Bytes -ne 3086848L) {
        throw 'The explicit readiness report identifies a different ESP or boot manager than the completed 042142 run.'
    }
    $null = Assert-GpuStageRetirement042142StageInventory -Entries @($Readiness.ExistingStage.Entries) -Description 'Fresh readiness inventory'
}

function Assert-GpuStageRetirement042142RecordedPin {
    param(
        [Parameter(Mandatory = $true)][object]$Pin,
        [Parameter(Mandatory = $true)][string]$ExpectedPath,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256,
        [Parameter(Mandatory = $true)][long]$ExpectedBytes,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($null -eq $Pin -or [IO.Path]::GetFullPath([string]$Pin.Path) -cne [IO.Path]::GetFullPath($ExpectedPath) -or
        [string]$Pin.Sha256 -cne $ExpectedSha256 -or [long]$Pin.Bytes -ne $ExpectedBytes) {
        throw "$Description metadata differs from the closed 042142 evidence pin."
    }
}

function Assert-GpuStageRetirement042142RawCapture {
    param(
        [Parameter(Mandatory = $true)][object]$Capture,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ExpectedRelativePath,
        [Parameter(Mandatory = $true)][string]$ExpectedIntegrityReceiptLeaf,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $rawPath = Join-Path $workspace $ExpectedRelativePath
    $rawPin = Assert-GpuRetirementProfilePin -Pin $Capture.BaselinePin -ExpectedPath $rawPath `
        -ExpectedSha256 $script:GpuRetirement042142RawCaptureSha256 -ExpectedBytes $script:GpuRetirement042142RawCaptureBytes `
        -WorkspaceRoot $workspace -Description "$Description raw boot-selection capture"
    if ([IO.Path]::GetFullPath([string]$Capture.BaselinePath) -cne [IO.Path]::GetFullPath($rawPath)) {
        throw "$Description raw capture path differs from the closed 042142 evidence path."
    }
    $baseline = $Capture.Baseline
    if ($null -eq $baseline) { throw "$Description raw capture is missing its parsed baseline." }
    $order = @($baseline.BootOrder)
    $rawDocument = [IO.File]::ReadAllText($rawPin.Path) | ConvertFrom-Json -ErrorAction Stop
    if ($baseline.Schema -ne 1 -or $null -ne $baseline.BootNext -or $order.Count -ne 6 -or
        [string]$baseline.BootManagerSha256 -cne $script:GpuRetirement042142BootManagerSha256 -or
        [string]$baseline.ReferenceLogSha256 -cne $script:GpuRetirement042142ReferenceLogSha256 -or
        [string]$baseline.IntegrityFindings -cne '00000000' -or
        $rawDocument.schema -ne 1 -or [string]$rawDocument.kind -cne 'boot-selection' -or
        $null -ne $rawDocument.boot_next -or @($rawDocument.boot_order).Count -ne 6 -or
        [string]$rawDocument.bootmgfw_sha256 -cne $script:GpuRetirement042142BootManagerSha256 -or
        [string]$rawDocument.log_sha256 -cne $script:GpuRetirement042142ReferenceLogSha256 -or
        [int]$rawDocument.findings -ne 0 -or
        -not [string]::Equals([string]$Capture.IntegrityOutput, ('BootmgfwSha256=' + $script:GpuRetirement042142BootManagerSha256 + ' Findings=0x00000000'), [StringComparison]::Ordinal)) {
        throw "$Description raw capture lacks the fixed BootNext, BootOrder, boot-manager, reference-log, or integrity proof."
    }
    for ($index = 0; $index -lt 6; $index++) {
        if ([int]$order[$index] -ne $index -or [int]$rawDocument.boot_order[$index] -ne $index) {
            throw "$Description raw BootOrder differs from the reviewed sequence 0 through 5."
        }
    }
    if ($null -ne $Capture.AuditToolPin) {
        Assert-GpuStageRetirement042142RecordedPin -Pin $Capture.AuditToolPin `
            -ExpectedPath (Join-Path $workspace $script:GpuRetirementAuditToolRelativePath) `
            -ExpectedSha256 $script:GpuRetirementAuditToolSha256 -ExpectedBytes $script:GpuRetirementAuditToolBytes `
            -Description "$Description audit tool"
    }
    if ($null -ne $Capture.ReferenceLogPin) {
        Assert-GpuStageRetirement042142RecordedPin -Pin $Capture.ReferenceLogPin `
            -ExpectedPath (Join-Path $workspace $script:GpuRetirement042142ReferenceLogRelativePath) `
            -ExpectedSha256 $script:GpuRetirement042142ReferenceLogSha256 -ExpectedBytes $script:GpuRetirement042142ReferenceLogBytes `
            -Description "$Description reference log"
    }

    $reviewDirectory = [IO.Path]::GetDirectoryName($rawPath)
    $receiptPath = Join-Path $reviewDirectory $ExpectedIntegrityReceiptLeaf
    if ([IO.Path]::GetFullPath([string]$Capture.IntegrityReceipt) -cne [IO.Path]::GetFullPath($receiptPath)) {
        throw "$Description integrity receipt path differs from the closed 042142 evidence path."
    }
    $receiptFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $receiptPath -WorkspaceRoot $workspace
    $integrityReceiptPin = Get-GpuStageRetirementPin -Path $receiptFullPath
    $integrityReceipt = [IO.File]::ReadAllText($receiptFullPath) | ConvertFrom-Json -ErrorAction Stop
    $auditToolPath = [IO.Path]::GetFullPath((Join-Path $workspace $script:GpuRetirementAuditToolRelativePath))
    $expectedArguments = @('bootmgr-integrity','--expected-hash',$script:GpuRetirement042142BootManagerSha256,'--esp-root','S:\')
    if ($integrityReceipt.SchemaVersion -ne 1 -or $integrityReceipt.Succeeded -ne $true -or
        [int]$integrityReceipt.ExitCode -ne 0 -or @($integrityReceipt.AllowedExitCodes).Count -ne 1 -or
        [int]$integrityReceipt.AllowedExitCodes[0] -ne 0 -or $null -ne $integrityReceipt.LaunchError -or
        [IO.Path]::GetFullPath([string]$integrityReceipt.Program) -cne $auditToolPath -or
        @($integrityReceipt.Arguments).Count -ne $expectedArguments.Count) {
        throw "$Description integrity receipt is incomplete or unsuccessful."
    }
    for ($index = 0; $index -lt $expectedArguments.Count; $index++) {
        if ([string]$integrityReceipt.Arguments[$index] -cne $expectedArguments[$index]) {
            throw "$Description integrity receipt has unexpected command arguments."
        }
    }
    $receiptStem = [IO.Path]::GetFileNameWithoutExtension([IO.Path]::GetFileNameWithoutExtension($ExpectedIntegrityReceiptLeaf))
    $stdoutPath = Join-Path $reviewDirectory ($receiptStem + '.stdout.txt')
    $stderrPath = Join-Path $reviewDirectory ($receiptStem + '.stderr.txt')
    if ([IO.Path]::GetFullPath([string]$integrityReceipt.StdoutPath) -cne [IO.Path]::GetFullPath($stdoutPath) -or
        [IO.Path]::GetFullPath([string]$integrityReceipt.StderrPath) -cne [IO.Path]::GetFullPath($stderrPath)) {
        throw "$Description integrity output paths differ from the captured receipt."
    }
    $stdoutFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stdoutPath -WorkspaceRoot $workspace
    $stderrFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stderrPath -WorkspaceRoot $workspace
    $stdoutPin = Get-GpuStageRetirementPin -Path $stdoutFullPath
    $stderrPin = Get-GpuStageRetirementPin -Path $stderrFullPath
    $expectedOutput = 'BootmgfwSha256=' + $script:GpuRetirement042142BootManagerSha256 + ' Findings=0x00000000'
    if (-not [string]::Equals(([IO.File]::ReadAllText($stdoutFullPath)).Trim(), $expectedOutput, [StringComparison]::Ordinal) -or
        [IO.File]::ReadAllText($stderrFullPath).Length -ne 0) {
        throw "$Description integrity stdout/stderr differ from the fixed reviewed result."
    }
    return @($rawPin,$integrityReceiptPin,$stdoutPin,$stderrPin)
}

function Assert-GpuStageRetirement042142FirmwareObservation {
    param(
        [Parameter(Mandatory = $true)][object]$Observation,
        [Parameter(Mandatory = $true)][string]$Checkpoint,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ExpectedText,
        [Parameter(Mandatory = $true)][string]$ExpectedDirectory,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ([string]$Observation.Checkpoint -cne $Checkpoint -or $Observation.Succeeded -ne $true -or
        -not [string]::Equals([string]$Observation.Text, $ExpectedText, [StringComparison]::Ordinal)) {
        throw "$Description firmware observation differs from the fixed 042142 reviewed text."
    }
    $stdoutPin = Assert-GpuRetirementProfilePin -Pin $Observation.Pin -ExpectedPath ([string]$Observation.Pin.Path) `
        -ExpectedSha256 $script:GpuRetirement042142ReviewedFirmwareSha256 -ExpectedBytes $script:GpuRetirement042142ReviewedFirmwareBytes `
        -WorkspaceRoot $WorkspaceRoot -Description "$Description firmware output"
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stdoutPin.Path)) -cne [IO.Path]::GetFullPath($ExpectedDirectory) -or
        [IO.Path]::GetFileName($stdoutPin.Path) -notmatch '^stage-retirement-firmware-[0-9a-f]{32}\.stdout\.txt$' -or
        -not [string]::Equals([IO.File]::ReadAllText($stdoutPin.Path), $ExpectedText, [StringComparison]::Ordinal)) {
        throw "$Description firmware stdout is outside the fixed observation directory or differs from the reviewed text."
    }
    $stdoutLeaf = [IO.Path]::GetFileName($stdoutPin.Path)
    $stem = $stdoutLeaf.Substring(0, $stdoutLeaf.Length - '.stdout.txt'.Length)
    $receiptPath = Join-Path $ExpectedDirectory ($stem + '.receipt.json')
    $stderrPath = Join-Path $ExpectedDirectory ($stem + '.stderr.txt')
    if ([IO.Path]::GetFullPath([string]$Observation.ReceiptPath) -cne [IO.Path]::GetFullPath($receiptPath)) {
        throw "$Description firmware receipt path differs from its pinned output."
    }
    $receiptFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $receiptPath -WorkspaceRoot $WorkspaceRoot
    $commandPin = Get-GpuStageRetirementPin -Path $receiptFullPath
    $command = [IO.File]::ReadAllText($receiptFullPath) | ConvertFrom-Json -ErrorAction Stop
    if ($command.SchemaVersion -ne 1 -or $command.Succeeded -ne $true -or [int]$command.ExitCode -ne 0 -or
        @($command.AllowedExitCodes).Count -ne 1 -or [int]$command.AllowedExitCodes[0] -ne 0 -or
        $null -ne $command.LaunchError -or [IO.Path]::GetFileName([string]$command.Program) -ine 'bcdedit.exe' -or
        @($command.Arguments).Count -ne 3 -or [string]$command.Arguments[0] -cne '/enum' -or
        [string]$command.Arguments[1] -cne 'firmware' -or [string]$command.Arguments[2] -cne '/v' -or
        [IO.Path]::GetFullPath([string]$command.StdoutPath) -cne [IO.Path]::GetFullPath($stdoutPin.Path) -or
        [IO.Path]::GetFullPath([string]$command.StderrPath) -cne [IO.Path]::GetFullPath($stderrPath)) {
        throw "$Description firmware command receipt is incomplete or unsuccessful."
    }
    $stderrFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stderrPath -WorkspaceRoot $WorkspaceRoot
    if ([IO.File]::ReadAllText($stderrFullPath).Length -ne 0) { throw "$Description firmware command wrote unexpected stderr." }
    return @($stdoutPin,$commandPin,(Get-GpuStageRetirementPin -Path $stderrFullPath))
}

function Assert-GpuStageRetirement042142Proof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $runDirectory = Join-Path (Join-Path $workspace '.analysis\gpu-baremetal\runs') $script:GpuRetirement042142RunId
    $journalPath = Join-Path $runDirectory 'journal.json'
    $archivePath = Join-Path (Join-Path $workspace '.analysis\gpu-baremetal\archive') ($script:GpuRetirement042142RunId + '.json')
    if ([string]$Journal.RunId -cne $script:GpuRetirement042142RunId) {
        throw 'The supplied state is not the fixed archived 042142 run.'
    }
    $journalPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$journalPath; Sha256=$script:GpuRetirement042142JournalSha256; Bytes=$script:GpuRetirement042142JournalBytes }) `
        -ExpectedPath $journalPath -ExpectedSha256 $script:GpuRetirement042142JournalSha256 `
        -ExpectedBytes $script:GpuRetirement042142JournalBytes -WorkspaceRoot $workspace -Description 'Completed 042142 journal'
    $Journal = [IO.File]::ReadAllText($journalPin.Path) | ConvertFrom-Json -ErrorAction Stop
    $archivePin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$archivePath; Sha256=$script:GpuRetirement042142ArchiveSha256; Bytes=$script:GpuRetirement042142ArchiveBytes }) `
        -ExpectedPath $archivePath -ExpectedSha256 $script:GpuRetirement042142ArchiveSha256 `
        -ExpectedBytes $script:GpuRetirement042142ArchiveBytes -WorkspaceRoot $workspace -Description 'Archived 042142 selector'
    $archivedSelector = [IO.File]::ReadAllText($archivePin.Path) | ConvertFrom-Json -ErrorAction Stop
    if ($archivedSelector.SchemaVersion -ne 1 -or [string]$archivedSelector.RunId -cne $script:GpuRetirement042142RunId -or
        [IO.Path]::GetFullPath([string]$archivedSelector.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [IO.Path]::GetFullPath([string]$archivedSelector.JournalPath) -cne [IO.Path]::GetFullPath($journalPath) -or
        [IO.Path]::GetFullPath([string]$archivedSelector.SourceRoot) -cne [IO.Path]::GetFullPath((Join-Path $workspace '.codex-build\worktrees\gpu-windows-validation'))) {
        throw 'The pinned archived selector does not identify the exact completed 042142 journal and source root.'
    }
    $extraPins = New-Object 'System.Collections.Generic.List[object]'
    $extraPins.Add($journalPin)
    $extraPins.Add($archivePin)
    $finalActionDirectory = Join-Path $workspace $script:GpuRetirement042142FinalActionRelativePath
    $verification = $Journal.FinalVerification
    $enumeration = if ($null -ne $verification) { $verification.FirmwareEnumeration } else { $null }
    $review = if ($null -ne $enumeration) { $enumeration.ReviewedDifference } else { $null }
    $last = $Journal.LastAction
    if ([string]$Journal.RunId -cne $script:GpuRetirement042142RunId -or
        [IO.Path]::GetFullPath([string]$Journal.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [IO.Path]::GetFullPath([string]$Journal.SourceRoot) -cne [IO.Path]::GetFullPath((Join-Path $workspace '.codex-build\worktrees\gpu-windows-validation')) -or
        [string]$Journal.SourceCommit -cne $script:GpuRetirement042142SourceCommit -or
        [string]$Journal.Phase -cne 'Complete' -or $null -ne $Journal.InProgress -or
        $null -eq $last -or [string]$last.Action -cne 'FinalVerify' -or $last.Succeeded -ne $true -or
        [string]$last.Status -cne 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED' -or
        [IO.Path]::GetFullPath([string]$last.Directory) -cne [IO.Path]::GetFullPath($finalActionDirectory) -or
        [string]$Journal.DisarmedBootId -cne '2026-10-21T04:30:43.5000000Z') {
        throw 'The 042142 journal lacks its exact archived run identity or successful reviewed FinalVerify completion.'
    }
    if ($null -eq $verification -or $verification.ResetBoundaryPassed -ne $true -or
        $verification.ActivationAttempted -ne $true -or $verification.SecureBoot -ne $true -or
        $verification.ActiveGpuProof -ne $false -or [string]$verification.GpuAttestation -cne 'UNATTESTED' -or
        @($verification.Errors).Count -ne 0 -or $null -eq $verification.Health -or
        $verification.Health.Passed -ne $true -or [string]$verification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$verification.Health.Aggregate -cne 'ABSENT' -or [int]$verification.Health.CpuCount -ne 20 -or
        [int]$verification.Health.ExpectedCpus -ne 20 -or [int]$verification.Health.CpuPassCount -ne 0 -or
        $null -eq $verification.Esp -or $verification.Esp.MatchesBaseline -ne $true -or
        [string]$verification.Esp.Volume -cne $script:GpuRetirement042142EspVolume -or
        [int]$verification.Esp.DiskNumber -ne $script:GpuRetirement042142EspDiskNumber -or
        [string]$verification.Esp.PartitionGuid -ine $script:GpuRetirement042142EspPartitionGuid -or
        [string]$verification.Esp.BootManagerSha256 -cne $script:GpuRetirement042142BootManagerSha256 -or
        [int]$Journal.Host.LogicalCpus -ne 20 -or [string]$verification.BootId -cne $script:GpuRetirement042142CurrentBootId) {
        throw 'The 042142 final verification does not satisfy the reset, ABSENT health, Secure Boot, ESP, and unattested-GPU gates.'
    }
    $journalSourcePins = @($Journal.SourcePins)
    $historicalStatePath = Join-Path $workspace '.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuHostState.psm1'
    $historicalPlatformPath = Join-Path $workspace '.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuHostPlatform.psm1'
    $historicalState = @($journalSourcePins | Where-Object { [IO.Path]::GetFullPath([string]$_.Path) -ceq [IO.Path]::GetFullPath($historicalStatePath) }) | Select-Object -First 1
    $historicalPlatform = @($journalSourcePins | Where-Object { [IO.Path]::GetFullPath([string]$_.Path) -ceq [IO.Path]::GetFullPath($historicalPlatformPath) }) | Select-Object -First 1
    if ($null -eq $historicalState -or [string]$historicalState.Sha256 -cne $script:GpuRetirement042142StateSha256 -or
        [long]$historicalState.Bytes -ne $script:GpuRetirement042142StateBytes -or
        $null -eq $historicalPlatform -or [string]$historicalPlatform.Sha256 -cne $script:GpuRetirement042142HistoricalPlatformSha256 -or
        [long]$historicalPlatform.Bytes -ne $script:GpuRetirement042142HistoricalPlatformBytes) {
        throw 'The 042142 journal does not bind the exact historical host state and platform sources.'
    }
    if ($null -eq $enumeration -or $enumeration.TextEqual -ne $false -or $enumeration.ReviewedDifferenceAccepted -ne $true -or
        [string]$enumeration.BaselineSha256 -cne $script:GpuRetirement042142OriginalFirmwareSha256 -or
        [string]$enumeration.CurrentSha256 -cne $script:GpuRetirement042142ReviewedFirmwareSha256 -or
        [string]$enumeration.CaptureContext -cne 'SystemEspMountedAtS' -or $null -eq $review -or
        $review.Accepted -ne $true -or $review.HistoricalRawBootOrderUnchangedProven -ne $false -or
        $review.FullBootVariableNamespaceAttested -ne $false) {
        throw 'The 042142 journal does not contain the exact reviewed firmware-difference evidence.'
    }

    $originalFirmwarePath = Join-Path $workspace $script:GpuRetirement042142OriginalFirmwareRelativePath
    $originalFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.OriginalBaselinePin -ExpectedPath $originalFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement042142OriginalFirmwareSha256 -ExpectedBytes $script:GpuRetirement042142OriginalFirmwareBytes `
        -WorkspaceRoot $workspace -Description '042142 original Backup firmware baseline'
    $extraPins.Add($originalFirmwarePin)
    $backupFirmware = $Journal.Backup.FirmwareEnumeration
    Assert-GpuStageRetirement042142RecordedPin -Pin $backupFirmware -ExpectedPath $originalFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement042142OriginalFirmwareSha256 -ExpectedBytes $script:GpuRetirement042142OriginalFirmwareBytes `
        -Description '042142 journal original firmware baseline'
    $reviewedFirmwarePath = Join-Path $workspace $script:GpuRetirement042142ReviewedFirmwareRelativePath
    $reviewedFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.ReviewedFirmwarePin -ExpectedPath $reviewedFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement042142ReviewedFirmwareSha256 -ExpectedBytes $script:GpuRetirement042142ReviewedFirmwareBytes `
        -WorkspaceRoot $workspace -Description '042142 first changed firmware observation'
    $extraPins.Add($reviewedFirmwarePin)
    $freshFirmwarePath = Join-Path $workspace $script:GpuRetirement042142ReviewActionRelativePath
    $freshFirmwareFile = Join-Path $freshFirmwarePath 'final-firmware-review-db2329af27c14bbfba70b24533c1d47a.stdout.txt'
    $freshFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.FreshFirmwarePin -ExpectedPath $freshFirmwareFile `
        -ExpectedSha256 $script:GpuRetirement042142ReviewedFirmwareSha256 -ExpectedBytes $script:GpuRetirement042142ReviewedFirmwareBytes `
        -WorkspaceRoot $workspace -Description '042142 successful FinalVerify fresh firmware capture'
    $extraPins.Add($freshFirmwarePin)
    $reviewedFirmwareText = [IO.File]::ReadAllText($reviewedFirmwarePin.Path)
    $freshFirmwareText = [IO.File]::ReadAllText($freshFirmwarePin.Path)
    if (-not [string]::Equals($reviewedFirmwareText, $freshFirmwareText, [StringComparison]::Ordinal)) {
        throw 'The 042142 fresh firmware capture differs from the reviewed capture.'
    }
    $referenceLogPath = Join-Path $workspace $script:GpuRetirement042142ReferenceLogRelativePath
    $referencePin = Assert-GpuRetirementProfilePin -Pin $Journal.Backup.ReferenceLog -ExpectedPath $referenceLogPath `
        -ExpectedSha256 $script:GpuRetirement042142ReferenceLogSha256 -ExpectedBytes $script:GpuRetirement042142ReferenceLogBytes `
        -WorkspaceRoot $workspace -Description '042142 TPM reference log'
    $extraPins.Add($referencePin)
    $probePath = Join-Path $workspace $script:GpuRetirement042142ProbeRelativePath
    $probePin = Assert-GpuRetirementProfilePin -Pin $Journal.Tools.Probe -ExpectedPath $probePath `
        -ExpectedSha256 $script:GpuRetirement042142ProbeSha256 -ExpectedBytes $script:GpuRetirement042142ProbeBytes `
        -WorkspaceRoot $workspace -Description '042142 health probe'
    $extraPins.Add($probePin)
    $auditToolPath = Join-Path $workspace $script:GpuRetirementAuditToolRelativePath
    $auditToolPin = Get-GpuStageRetirementPin -Path (Assert-GpuRetirementSafeWorkspaceFile -Path $auditToolPath -WorkspaceRoot $workspace)
    if ($auditToolPin.Sha256 -cne $script:GpuRetirementAuditToolSha256 -or $auditToolPin.Bytes -ne $script:GpuRetirementAuditToolBytes) {
        throw 'The fixed read-only firmware audit tool has changed.'
    }
    $extraPins.Add($auditToolPin)

    $reviewReceiptPath = Join-Path $workspace $script:GpuRetirement042142ReviewReceiptRelativePath
    $reviewReceiptPin = Assert-GpuRetirementProfilePin -Pin $review.ReceiptPin -ExpectedPath $reviewReceiptPath `
        -ExpectedSha256 $script:GpuRetirement042142ReviewReceiptSha256 -ExpectedBytes $script:GpuRetirement042142ReviewReceiptBytes `
        -WorkspaceRoot $workspace -Description '042142 read-only firmware review receipt'
    $extraPins.Add($reviewReceiptPin)
    $reviewReceipt = [IO.File]::ReadAllText($reviewReceiptPin.Path) | ConvertFrom-Json -ErrorAction Stop
    if ($reviewReceipt.SchemaVersion -ne 1 -or [string]$reviewReceipt.Status -cne 'READONLY_FIRMWARE_REVIEW_CAPTURED' -or
        $reviewReceipt.Succeeded -ne $true -or $reviewReceipt.RecoveryCompleted -ne $false -or
        $reviewReceipt.SelectorAndJournalUnchanged -ne $true -or $reviewReceipt.EspFilesChanged -ne $false -or
        $reviewReceipt.FirmwareWritten -ne $false -or $reviewReceipt.CleanupSucceeded -ne $true -or
        $reviewReceipt.EspUnmounted -ne $true -or $reviewReceipt.RawBootSelectionStable -ne $true -or
        $reviewReceipt.FirmwareTextStable -ne $true -or $reviewReceipt.HistoricalRawBootOrderUnchangedProven -ne $false -or
        ($null -ne $reviewReceipt.PSObject.Properties['FullBootVariableNamespaceAttested'] -and $reviewReceipt.FullBootVariableNamespaceAttested -ne $false) -or
        @($reviewReceipt.CleanupErrors).Count -ne 0 -or
        @($reviewReceipt.Errors).Count -ne 0 -or $null -eq $reviewReceipt.CurrentHost -or
        $reviewReceipt.CurrentHost.SecureBoot -ne $true -or [int]$reviewReceipt.CurrentHost.LogicalCpus -ne 20 -or
        [string]$reviewReceipt.CurrentHost.BootId -cne $script:GpuRetirement042142CurrentBootId -or
        $null -eq $reviewReceipt.Esp -or [string]$reviewReceipt.Esp.Volume -cne $script:GpuRetirement042142EspVolume -or
        [int]$reviewReceipt.Esp.DiskNumber -ne $script:GpuRetirement042142EspDiskNumber -or
        [string]$reviewReceipt.Esp.PartitionGuid -ine $script:GpuRetirement042142EspPartitionGuid -or
        [string]$reviewReceipt.Esp.BootManagerSha256 -cne $script:GpuRetirement042142BootManagerSha256) {
        throw 'The pinned 042142 review receipt does not prove the required read-only, cleanup, host, ESP, and stability gates.'
    }

    $receiptPinSpecs = @(
        @{ Name='Selector'; Path='.analysis\gpu-baremetal\host-prepared.json'; Sha256=$script:GpuRetirement042142ArchiveSha256; Bytes=$script:GpuRetirement042142ArchiveBytes },
        @{ Name='Journal'; Path='.analysis\gpu-baremetal\runs\gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2\journal.json'; Sha256=$script:GpuRetirement042142PreReviewJournalSha256; Bytes=$script:GpuRetirement042142PreReviewJournalBytes },
        @{ Name='FirmwareBaseline'; Path=$script:GpuRetirement042142OriginalFirmwareRelativePath; Sha256=$script:GpuRetirement042142OriginalFirmwareSha256; Bytes=$script:GpuRetirement042142OriginalFirmwareBytes },
        @{ Name='FirmwareCurrent'; Path=$script:GpuRetirement042142ReviewedFirmwareRelativePath; Sha256=$script:GpuRetirement042142ReviewedFirmwareSha256; Bytes=$script:GpuRetirement042142ReviewedFirmwareBytes },
        @{ Name='Ibaudit'; Path=$script:GpuRetirementAuditToolRelativePath; Sha256=$script:GpuRetirementAuditToolSha256; Bytes=$script:GpuRetirementAuditToolBytes },
        @{ Name='ReferenceLog'; Path=$script:GpuRetirement042142ReferenceLogRelativePath; Sha256=$script:GpuRetirement042142ReferenceLogSha256; Bytes=$script:GpuRetirement042142ReferenceLogBytes },
        @{ Name='Probe'; Path=$script:GpuRetirement042142ProbeRelativePath; Sha256=$script:GpuRetirement042142ProbeSha256; Bytes=$script:GpuRetirement042142ProbeBytes },
        @{ Name='FinalResult'; Path=$script:GpuRetirement042142FailedResultRelativePath; Sha256=$script:GpuRetirement042142FailedResultSha256; Bytes=$script:GpuRetirement042142FailedResultBytes },
        @{ Name='InspectorScript'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1'; Sha256=$script:GpuRetirement042142InspectorSha256; Bytes=$script:GpuRetirement042142InspectorBytes }
    )
    foreach ($spec in $receiptPinSpecs) {
        $expectedPath = Join-Path $workspace $spec.Path
        $before = Get-GpuRetirementProperty -Object $reviewReceipt.PinsBefore -Name $spec.Name
        $after = Get-GpuRetirementProperty -Object $reviewReceipt.PinsAfter -Name $spec.Name
        Assert-GpuStageRetirement042142RecordedPin -Pin $before -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes -Description "042142 review before/$($spec.Name)"
        Assert-GpuStageRetirement042142RecordedPin -Pin $after -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes -Description "042142 review after/$($spec.Name)"
        if ($spec.Name -notin @('Selector','Journal')) {
            $pin = Assert-GpuRetirementProfilePin -Pin $before -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes `
                -WorkspaceRoot $workspace -Description "042142 preserved $($spec.Name) evidence"
            $extraPins.Add($pin)
        }
    }
    $modulePinSpecs = @(
        @{ Name='State'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuHostState.psm1'; Sha256=$script:GpuRetirement042142StateSha256; Bytes=$script:GpuRetirement042142StateBytes },
        @{ Name='Platform'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuHostPlatform.psm1'; Sha256=$script:GpuRetirement042142HistoricalPlatformSha256; Bytes=$script:GpuRetirement042142HistoricalPlatformBytes },
        @{ Name='Retirement'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuStageRetirement.psm1'; Sha256=$script:GpuRetirement042142HistoricalRetirementSha256; Bytes=$script:GpuRetirement042142HistoricalRetirementBytes }
    )
    foreach ($spec in $modulePinSpecs) {
        $expectedPath = Join-Path $workspace $spec.Path
        foreach ($groupName in @('PinsBefore','PinsAfter')) {
            $pin = Get-GpuRetirementProperty -Object (Get-GpuRetirementProperty -Object $reviewReceipt.$groupName -Name 'Modules') -Name $spec.Name
            Assert-GpuStageRetirement042142RecordedPin -Pin $pin -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes -Description "042142 $groupName/$($spec.Name) module"
        }
    }

    $finalResultPath = Join-Path $workspace $script:GpuRetirement042142FinalResultRelativePath
    $finalResultPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$finalResultPath; Sha256=$script:GpuRetirement042142FinalResultSha256; Bytes=$script:GpuRetirement042142FinalResultBytes }) `
        -ExpectedPath $finalResultPath -ExpectedSha256 $script:GpuRetirement042142FinalResultSha256 `
        -ExpectedBytes $script:GpuRetirement042142FinalResultBytes -WorkspaceRoot $workspace -Description 'Successful 042142 FinalVerify result'
    $finalResult = [IO.File]::ReadAllText($finalResultPin.Path) | ConvertFrom-Json -ErrorAction Stop
    $resultVerification = $finalResult.Details.FinalVerification
    if ([string]$finalResult.Action -cne 'FinalVerify' -or
        [string]$finalResult.Status -cne 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED' -or
        $finalResult.Succeeded -ne $true -or
        [IO.Path]::GetFullPath([string]$finalResult.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [string]$finalResult.Details.Status -cne 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED' -or
        $finalResult.Details.Succeeded -ne $true -or $null -eq $resultVerification -or
        [string]$resultVerification.BootId -cne $script:GpuRetirement042142CurrentBootId -or
        $resultVerification.ResetBoundaryPassed -ne $true -or $resultVerification.SecureBoot -ne $true -or
        $resultVerification.ActivationAttempted -ne $true -or $resultVerification.ActiveGpuProof -ne $false -or
        [string]$resultVerification.GpuAttestation -cne 'UNATTESTED' -or
        $null -eq $resultVerification.Health -or $resultVerification.Health.Passed -ne $true -or
        [string]$resultVerification.Health.ExpectedResult -cne 'ABSENT' -or [string]$resultVerification.Health.Aggregate -cne 'ABSENT' -or
        [int]$resultVerification.Health.ExpectedCpus -ne 20 -or [int]$resultVerification.Health.CpuCount -ne 20 -or
        [int]$resultVerification.Health.CpuPassCount -ne 0 -or
        $null -eq $resultVerification.Esp -or $resultVerification.Esp.MatchesBaseline -ne $true -or
        [string]$resultVerification.Esp.Volume -cne $script:GpuRetirement042142EspVolume -or
        [int]$resultVerification.Esp.DiskNumber -ne $script:GpuRetirement042142EspDiskNumber -or
        [string]$resultVerification.Esp.PartitionGuid -ine $script:GpuRetirement042142EspPartitionGuid -or
        [string]$resultVerification.Esp.BootManagerSha256 -cne $script:GpuRetirement042142BootManagerSha256 -or
        $null -eq $resultVerification.FirmwareEnumeration -or $resultVerification.FirmwareEnumeration.TextEqual -ne $false -or
        $resultVerification.FirmwareEnumeration.ReviewedDifferenceAccepted -ne $true -or
        [string]$resultVerification.FirmwareEnumeration.BaselineSha256 -cne $script:GpuRetirement042142OriginalFirmwareSha256 -or
        [string]$resultVerification.FirmwareEnumeration.CurrentSha256 -cne $script:GpuRetirement042142ReviewedFirmwareSha256 -or
        @($resultVerification.Errors).Count -ne 0) {
        throw 'The pinned 042142 FinalVerify result does not prove the exact successful reset, ABSENT health, Secure Boot, ESP, and reviewed firmware state.'
    }
    $resultReview = $resultVerification.FirmwareEnumeration.ReviewedDifference
    if ($null -eq $resultReview -or $resultReview.Accepted -ne $true -or
        [IO.Path]::GetFullPath([string]$resultReview.ReceiptPin.Path) -cne [IO.Path]::GetFullPath($reviewReceiptPath) -or
        [string]$resultReview.ReceiptPin.Sha256 -cne $script:GpuRetirement042142ReviewReceiptSha256 -or
        [long]$resultReview.ReceiptPin.Bytes -ne $script:GpuRetirement042142ReviewReceiptBytes) {
        throw 'The pinned 042142 FinalVerify result does not bind the exact read-only firmware review receipt.'
    }
    $extraPins.Add($finalResultPin)

    $failedResultPath = Join-Path $workspace $script:GpuRetirement042142FailedResultRelativePath
    $failedResultPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$failedResultPath; Sha256=$script:GpuRetirement042142FailedResultSha256; Bytes=$script:GpuRetirement042142FailedResultBytes }) `
        -ExpectedPath $failedResultPath -ExpectedSha256 $script:GpuRetirement042142FailedResultSha256 `
        -ExpectedBytes $script:GpuRetirement042142FailedResultBytes -WorkspaceRoot $workspace -Description 'Original failed 042142 FinalVerify result'
    $failedResult = [IO.File]::ReadAllText($failedResultPin.Path) | ConvertFrom-Json -ErrorAction Stop
    if ([string]$failedResult.Action -cne 'FinalVerify' -or [string]$failedResult.Status -cne 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' -or
        $failedResult.Succeeded -ne $false -or [IO.Path]::GetFullPath([string]$failedResult.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory)) {
        throw 'The failed initial 042142 FinalVerify receipt is not the fixed unsuccessful action.'
    }
    $extraPins.Add($failedResultPin)

    $expectedStageByName = @{}
    foreach ($expected in $script:GpuRetirement042142StagePins) { $expectedStageByName[$expected.Name] = $expected }
    $journalStageFiles = @($Journal.Stage.Files)
    if ($journalStageFiles.Count -ne $script:GpuRetirement042142StagePins.Count) {
        throw 'The exact archived 042142 stage manifest must contain five files.'
    }
    $journalStageNames = @{}
    foreach ($file in $journalStageFiles) {
        $name = [string]$file.Name
        $expected = $expectedStageByName[$name]
        if ($null -eq $expected -or $journalStageNames.ContainsKey($name) -or
            [string]$file.RelativePath -cne ('EFI\insecure-boot\' + $name) -or
            [string]$file.Sha256 -cne $expected.Sha256 -or [long]$file.Bytes -ne $expected.Bytes) {
            throw "The 042142 installer stage manifest differs from the fixed five-file set: $name"
        }
        $journalStageNames[$name] = $true
    }

    $collectionRows = @($Journal.Collection.Esp.StageFiles)
    if ($collectionRows.Count -ne 7) { throw 'The 042142 collection must contain five present files and two explicit absent diagnostics.' }
    $collectionNames = @{}
    foreach ($row in $collectionRows) {
        $name = [string]$row.Name
        if ($collectionNames.ContainsKey($name)) { throw "The 042142 collection has a duplicate file row: $name" }
        $collectionNames[$name] = $true
        if ($name -in @('handoff-status.txt','payload-diagnostic.txt')) {
            if ($row.Present -ne $false -or $null -ne $row.Path -or $null -ne $row.Pin) {
                throw "The 042142 diagnostic artifact must be explicitly recorded absent: $name"
            }
            continue
        }
        $expected = $expectedStageByName[$name]
        $expectedCollectedPath = Join-Path (Join-Path $workspace $script:GpuRetirement042142CollectionActionRelativePath) ('esp-' + $name)
        if ($null -eq $expected -or $row.Present -ne $true -or $null -eq $row.Pin -or
            [IO.Path]::GetFullPath([string]$row.Path) -cne [IO.Path]::GetFullPath($expectedCollectedPath) -or
            [string]$row.Pin.Sha256 -cne $expected.Sha256 -or [long]$row.Pin.Bytes -ne $expected.Bytes -or
            [IO.Path]::GetFullPath([string]$row.Pin.Path) -cne [IO.Path]::GetFullPath($expectedCollectedPath)) {
            throw "The 042142 collected file snapshot differs from the fixed staged file: $name"
        }
        $collectionPin = Assert-GpuRetirementProfilePin -Pin $row.Pin -ExpectedPath $expectedCollectedPath `
            -ExpectedSha256 $expected.Sha256 -ExpectedBytes $expected.Bytes -WorkspaceRoot $workspace `
            -Description "042142 collected $name snapshot"
        $extraPins.Add($collectionPin)
    }
    foreach ($expected in $script:GpuRetirement042142StagePins) {
        if (-not $collectionNames.ContainsKey($expected.Name) -or -not $journalStageNames.ContainsKey($expected.Name)) {
            throw "The 042142 journal or collection is missing a fixed staged file: $($expected.Name)"
        }
    }
    foreach ($name in @('handoff-status.txt','payload-diagnostic.txt')) {
        if (-not $collectionNames.ContainsKey($name)) { throw "The 042142 collection is missing its explicit absent row: $name" }
    }

    $receiptRawCaptures = @($reviewReceipt.BootSelectionCaptures)
    $finalRawCaptures = @($resultReview.RawCaptures)
    if ($receiptRawCaptures.Count -ne 2 -or $finalRawCaptures.Count -ne 2 -or @($reviewReceipt.FirmwareObservations).Count -ne 2) {
        throw 'The 042142 review must contain two saved raw captures and two stable firmware observations.'
    }
    $receiptCaptureDirectory = Join-Path (Split-Path -Parent $reviewReceiptPin.Path) 'observations'
    $captureSpecs = @(
        @{ Checkpoint='before'; ReviewLeaf='before-bootmgr-integrity-be5909ea29d14695bce79a92c7d064a0.receipt.json'; FinalLeaf='before-bootmgr-integrity-998c8e335c62484d8e94ae36b1b45b17.receipt.json' },
        @{ Checkpoint='after'; ReviewLeaf='after-bootmgr-integrity-141999d6c1ca4e568602dd34e90082e6.receipt.json'; FinalLeaf='after-bootmgr-integrity-ca7e3e914c0840dfa26673cc42c202ac.receipt.json' }
    )
    for ($index = 0; $index -lt 2; $index++) {
        $spec = $captureSpecs[$index]
        $receiptCaptureRelativePath = Join-Path (Split-Path -Parent $script:GpuRetirement042142ReviewReceiptRelativePath) 'observations'
        $receiptRawRelative = Join-Path $receiptCaptureRelativePath ($spec.Checkpoint + '-boot-selection.json')
        foreach ($pin in @(Assert-GpuStageRetirement042142RawCapture -Capture $receiptRawCaptures[$index] -WorkspaceRoot $workspace `
            -ExpectedRelativePath $receiptRawRelative -ExpectedIntegrityReceiptLeaf $spec.ReviewLeaf -Description "042142 receipt $($spec.Checkpoint)")) { $extraPins.Add($pin) }
        $finalRawRelative = $script:GpuRetirement042142ReviewActionRelativePath + '\' + $spec.Checkpoint + '-boot-selection.json'
        foreach ($pin in @(Assert-GpuStageRetirement042142RawCapture -Capture $finalRawCaptures[$index] -WorkspaceRoot $workspace `
            -ExpectedRelativePath $finalRawRelative -ExpectedIntegrityReceiptLeaf $spec.FinalLeaf -Description "042142 FinalVerify $($spec.Checkpoint)")) { $extraPins.Add($pin) }
    }
    $observationNames = @('before','after')
    foreach ($index in 0..1) {
        foreach ($pin in @(Assert-GpuStageRetirement042142FirmwareObservation -Observation $reviewReceipt.FirmwareObservations[$index] `
            -Checkpoint $observationNames[$index] -WorkspaceRoot $workspace -ExpectedText $reviewedFirmwareText `
            -ExpectedDirectory $receiptCaptureDirectory -Description "042142 $($observationNames[$index]) review")) { $extraPins.Add($pin) }
    }

    $reviewSourceSpecs = @(
        @{ Name='Helper'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuFinalFirmwareReview.psm1'; Sha256=$script:GpuRetirement042142ReviewHelperSha256; Bytes=$script:GpuRetirement042142ReviewHelperBytes },
        @{ Name='Platform'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuHostPlatform.psm1'; Sha256=$script:GpuRetirement042142CurrentPlatformSha256; Bytes=$script:GpuRetirement042142CurrentPlatformBytes }
    )
    foreach ($spec in $reviewSourceSpecs) {
        $expectedPath = Join-Path $workspace $spec.Path
        $sourcePin = Get-GpuRetirementProperty -Object $resultReview.SourcePins -Name $spec.Name
        if ($null -eq $sourcePin) { throw "The successful 042142 review omitted its $($spec.Name) dependency pin." }
        foreach ($when in @('Before','After')) {
            $pin = Get-GpuRetirementProperty -Object $sourcePin -Name $when
            Assert-GpuStageRetirement042142RecordedPin -Pin $pin -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes `
                -Description "042142 $($spec.Name) dependency $when"
        }
        $currentPin = Assert-GpuRetirementProfilePin -Pin $sourcePin.Before -ExpectedPath $expectedPath `
            -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes -WorkspaceRoot $workspace -Description "042142 current $($spec.Name) dependency"
        $extraPins.Add($currentPin)
    }
    return @($extraPins.ToArray())
}

function Select-GpuStageRetirement042142Profile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ReadinessPath
    )

    if ($script:GpuRetirement201140ProfileSelected -or $script:GpuRetirement224018ProfileSelected -or
        $script:GpuRetirement023306ProfileSelected -or $script:GpuRetirement032117ProfileSelected -or
        $script:GpuRetirement042142ProfileSelected -or $script:GpuRetirement062106ProfileSelected -or
        $script:GpuRetirement160027ProfileSelected) {
        throw 'A completed-run retirement profile is already bound in this module instance.'
    }
    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $readinessRoot = Join-Path $workspace '.analysis\gpu-baremetal-readiness'
    if (-not [IO.Directory]::Exists($readinessRoot)) { throw 'The workspace readiness evidence directory is missing.' }
    $readinessRootFull = [IO.Path]::GetFullPath($readinessRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $readinessFull = Assert-GpuRetirementSafeWorkspaceFile -Path $ReadinessPath -WorkspaceRoot $workspace
    $prefix = $readinessRootFull + [IO.Path]::DirectorySeparatorChar
    if (-not $readinessFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ReadinessPath must name a report under .analysis\gpu-baremetal-readiness.'
    }
    $relative = $readinessFull.Substring($prefix.Length)
    if ($relative -notmatch '^([0-9a-fA-F]{32})\\readiness\.json$') {
        throw 'ReadinessPath must explicitly name <guid>\readiness.json under .analysis\gpu-baremetal-readiness.'
    }
    $readinessGuid = $Matches[1].ToLowerInvariant()
    $readinessDirectory = Join-Path $readinessRootFull $readinessGuid
    if ([IO.Path]::GetFullPath([IO.Path]::GetDirectoryName($readinessFull)) -cne [IO.Path]::GetFullPath($readinessDirectory)) {
        throw 'Readiness report directory does not match its GUID path.'
    }
    $readinessPin = Get-GpuStageRetirementPin -Path $readinessFull
    $readiness = [IO.File]::ReadAllText($readinessFull) | ConvertFrom-Json -ErrorAction Stop
    Assert-GpuStageRetirement042142Readiness -Readiness $readiness -ReadinessFullPath $readinessFull

    $script:GpuRetirement042142ProfileSelected = $true
    $script:GpuRetirementSelectedReadinessPath = $readinessFull
    $script:GpuRetirementProfile = '042142'
    $script:GpuRetirementRunId = $script:GpuRetirement042142RunId
    $script:GpuRetirementAuditRelativePath = '.analysis\gpu-baremetal-readiness\' + $readinessGuid + '\readiness.json'
    $script:GpuRetirementAuditSha256 = $readinessPin.Sha256
    $script:GpuRetirementAuditBytes = [long]$readinessPin.Bytes
    $script:GpuRetirementJournalSha256 = $script:GpuRetirement042142JournalSha256
    $script:GpuRetirementJournalBytes = $script:GpuRetirement042142JournalBytes
    $script:GpuRetirementArchiveSha256 = $script:GpuRetirement042142ArchiveSha256
    $script:GpuRetirementArchiveBytes = $script:GpuRetirement042142ArchiveBytes
    $script:GpuRetirementSourceCommit = $script:GpuRetirement042142SourceCommit
    $script:GpuRetirementProbeSha256 = $script:GpuRetirement042142ProbeSha256
    $script:GpuRetirementProbeBytes = $script:GpuRetirement042142ProbeBytes
    $script:GpuRetirementReferenceLogSha256 = $script:GpuRetirement042142ReferenceLogSha256
    $script:GpuRetirementReferenceLogBytes = $script:GpuRetirement042142ReferenceLogBytes
    $script:GpuRetirementReviewedFirmwareRelativePath = $script:GpuRetirement042142ReviewedFirmwareRelativePath
    $script:GpuRetirementReviewedFirmwareSha256 = $script:GpuRetirement042142ReviewedFirmwareSha256
    $script:GpuRetirementReviewedFirmwareBytes = $script:GpuRetirement042142ReviewedFirmwareBytes
    $script:GpuRetirementNames = @($script:GpuRetirement042142StagePins | ForEach-Object { $_.Name })
    return [pscustomobject]@{
        Profile = '042142'
        RunId = $script:GpuRetirementRunId
        ReadinessPath = $readinessFull
        ReadinessPin = $readinessPin
        JournalSha256 = $script:GpuRetirementJournalSha256
        ArchiveSha256 = $script:GpuRetirementArchiveSha256
        SourceCommit = $script:GpuRetirementSourceCommit
        StagePins = @($script:GpuRetirement042142StagePins)
    }
}

function Assert-GpuStageRetirement062106StageInventory {
    param(
        [Parameter(Mandatory = $true)][object[]]$Entries,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($Entries.Count -ne $script:GpuRetirement062106StagePins.Count) {
        throw "$Description must contain exactly the five pinned 062106 stage files."
    }
    $found = @{}
    foreach ($entry in $Entries) {
        $path = [string](Get-GpuRetirementProperty -Object $entry -Name 'RelativePath')
        if ($path -notmatch '^EFI\\insecure-boot\\([A-Za-z0-9._-]+)$') {
            throw "$Description contains a stage path outside the reviewed EFI directory."
        }
        $name = $Matches[1]
        $expected = @($script:GpuRetirement062106StagePins | Where-Object { $_.Name -ceq $name }) | Select-Object -First 1
        if ($null -eq $expected -or $found.ContainsKey($name)) {
            throw "$Description contains an unexpected or duplicate stage file: $name"
        }
        if ($null -eq $entry.PSObject.Properties['Kind'] -or [string]$entry.Kind -cne 'File' -or
            $null -eq $entry.PSObject.Properties['Hashed'] -or $entry.Hashed -ne $true) {
            throw "$Description contains a non-file or unhashed stage entry: $name"
        }
        if ([string]$entry.Sha256 -cne [string]$expected.Sha256 -or [long]$entry.Bytes -ne [long]$expected.Bytes) {
            throw "$Description pin differs from the fixed 062106 file: $name"
        }
        $found[$name] = $true
    }
    foreach ($expected in $script:GpuRetirement062106StagePins) {
        if (-not $found.ContainsKey([string]$expected.Name)) { throw "$Description is missing $($expected.Name)." }
    }
    return @($script:GpuRetirement062106StagePins)
}

function Assert-GpuStageRetirement062106Readiness {
    param([Parameter(Mandatory = $true)][object]$Readiness, [Parameter(Mandatory = $true)][string]$ReadinessFullPath)

    $source = $Readiness.Source
    $preflight = $Readiness.Preflight
    if ($null -eq $source -or $null -eq $preflight) { throw 'The explicit 062106 readiness report lacks source or preflight evidence.' }
    $sourceError = $null
    if ($null -ne $source.PSObject.Properties['Error']) { $sourceError = [string]$source.Error }
    $preflightError = $preflight.PSObject.Properties['Error']
    $errorsProperty = $Readiness.PSObject.Properties['Errors']
    $reportedErrors = @()
    if ($null -ne $errorsProperty) { $reportedErrors = @($errorsProperty.Value) }
    $blockers = @($Readiness.Blockers)
    $expectedBlocker = 'EFI\\insecure-boot is retained; ownership-reviewed retirement is required before a new stage. Do not delete it generically or retry Stage.'

    if ($Readiness.SchemaVersion -ne 1 -or [string]$Readiness.Status -cne 'BLOCKED_READINESS' -or
        $Readiness.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$Readiness.EvidencePath) -cne [IO.Path]::GetFullPath($ReadinessFullPath) -or
        [string]$source.Branch -cne 'feat/smbios-boot-serial-probe' -or
        [string]$source.Commit -notmatch '^[0-9a-fA-F]{40}$' -or $source.Dirty -ne $false -or
        [int]$source.DirtyEntryCount -ne 0 -or -not [string]::IsNullOrWhiteSpace($sourceError) -or
        $null -eq $preflight.PSObject.Properties['Complete'] -or $preflight.Complete -ne $true -or
        $null -eq $preflight.PSObject.Properties['InventoryComplete'] -or $preflight.InventoryComplete -ne $true -or
        $null -eq $preflight.PSObject.Properties['IntelPresent'] -or $preflight.IntelPresent -ne $true -or
        $null -eq $preflight.PSObject.Properties['NvidiaPresent'] -or $preflight.NvidiaPresent -ne $true -or
        $null -eq $preflightError -or $null -ne $preflightError.Value -or
        $blockers.Count -ne 1 -or [string]$blockers[0] -cne $expectedBlocker -or $reportedErrors.Count -ne 0 -or
        [int]$Readiness.Host.LogicalCpus -ne 20 -or $Readiness.Host.SecureBoot -ne $true -or
        $Readiness.Recovery.Valid -ne $true -or $Readiness.Recovery.Disk.IsVirtual -ne $false -or
        $null -eq $Readiness.Selector -or $Readiness.Selector.Exists -ne $false -or $Readiness.Selector.Valid -ne $true -or
        $null -ne $Readiness.Selector.Error -or $null -eq $Readiness.Cleanup -or $Readiness.Cleanup.Succeeded -ne $true -or
        @($Readiness.Cleanup.Errors).Count -ne 0 -or $null -eq $Readiness.Esp -or
        $Readiness.Esp.Inspected -ne $true -or [string]$Readiness.Esp.MountStateAfterCleanup -cne 'Unmounted' -or
        $null -eq $Readiness.Effects -or $Readiness.Effects.EspFilesChanged -ne $false -or
        $Readiness.Effects.FirmwareChanged -ne $false -or $Readiness.Effects.BootStateChanged -ne $false -or
        $Readiness.Effects.EspTemporarilyMounted -ne $true -or $null -eq $Readiness.ExistingStage -or
        $Readiness.ExistingStage.Exists -ne $true -or $Readiness.ExistingStage.Complete -ne $true -or
        [int]$Readiness.ExistingStage.EntryCount -ne 5 -or [int]$Readiness.ExistingStage.FileCount -ne 5 -or
        @($Readiness.ExistingStage.UnknownEntries).Count -ne 0 -or @($Readiness.ExistingStage.UnsafeEntries).Count -ne 0 -or
        @($Readiness.ExistingStage.Errors).Count -ne 0 -or $null -eq $Readiness.ExistingResidue -or
        $Readiness.ExistingResidue.Clear -ne $true -or @($Readiness.ExistingResidue.Residues).Count -ne 0) {
        throw 'The explicit readiness report is not a clean, unmounted BLOCKED_READINESS capture for the exact retained 062106 stage.'
    }
    if ([string]$Readiness.Esp.Identity.Volume -cne $script:GpuRetirement062106EspVolume -or
        [int]$Readiness.Esp.Identity.DiskNumber -ne $script:GpuRetirement062106EspDiskNumber -or
        [string]$Readiness.Esp.Identity.PartitionGuid -ine $script:GpuRetirement062106EspPartitionGuid -or
        [string]$Readiness.Esp.Identity.DiskUniqueId -cne $script:GpuRetirement062106EspDiskUniqueId -or
        [string]$Readiness.Esp.BootManagerPin.Sha256 -cne $script:GpuRetirement062106BootManagerSha256 -or
        [long]$Readiness.Esp.BootManagerPin.Bytes -ne $script:GpuRetirement062106BootManagerBytes) {
        throw 'The explicit readiness report identifies a different ESP or boot manager than the completed 062106 run.'
    }
    $null = Assert-GpuStageRetirement062106StageInventory -Entries @($Readiness.ExistingStage.Entries) -Description 'Fresh readiness inventory'
}

function Assert-GpuStageRetirement062106RecordedPin {
    param(
        [Parameter(Mandatory = $true)][object]$Pin,
        [Parameter(Mandatory = $true)][string]$ExpectedPath,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256,
        [Parameter(Mandatory = $true)][long]$ExpectedBytes,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($null -eq $Pin -or [IO.Path]::GetFullPath([string]$Pin.Path) -cne [IO.Path]::GetFullPath($ExpectedPath) -or
        [string]$Pin.Sha256 -cne $ExpectedSha256 -or [long]$Pin.Bytes -ne $ExpectedBytes) {
        throw "$Description metadata differs from the closed 062106 evidence pin."
    }
}

function Assert-GpuStageRetirement062106BootSelection {
    param([Parameter(Mandatory = $true)][object]$Capture, [Parameter(Mandatory = $true)][string]$Checkpoint)

    if ($null -eq $Capture -or $null -eq $Capture.Baseline) { throw "$Checkpoint raw firmware capture is missing." }
    $baseline = $Capture.Baseline
    $order = @($baseline.BootOrder)
    if ($baseline.Schema -ne 1 -or $null -ne $baseline.BootNext -or $order.Count -ne 6 -or
        [string]$baseline.BootManagerSha256 -cne $script:GpuRetirement062106BootManagerSha256 -or
        [string]$baseline.ReferenceLogSha256 -cne $script:GpuRetirement062106ReferenceLogSha256 -or
        [string]$baseline.IntegrityFindings -cne '00000000') {
        throw "$Checkpoint raw boot-selection differs from the closed 062106 reviewed state."
    }
    for ($index = 0; $index -lt 6; $index++) {
        if ($order[$index] -isnot [int] -and $order[$index] -isnot [long] -and
            $order[$index] -isnot [uint16] -and $order[$index] -isnot [uint32]) {
            throw "$Checkpoint raw BootOrder contains a nonnumeric entry."
        }
        if ([int]$order[$index] -ne $index) { throw "$Checkpoint raw BootOrder differs from the reviewed sequence 0 through 5." }
    }
}

function Assert-GpuStageRetirement062106RawCapture {
    param(
        [Parameter(Mandatory = $true)][object]$Capture,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ExpectedRelativePath,
        [Parameter(Mandatory = $true)][string]$ExpectedIntegrityReceiptLeaf,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $rawPath = Join-Path $workspace $ExpectedRelativePath
    $rawPin = Assert-GpuRetirementProfilePin -Pin $Capture.BaselinePin -ExpectedPath $rawPath `
        -ExpectedSha256 $script:GpuRetirement062106RawCaptureSha256 -ExpectedBytes $script:GpuRetirement062106RawCaptureBytes `
        -WorkspaceRoot $workspace -Description "$Description raw boot-selection capture"
    if ([IO.Path]::GetFullPath([string]$Capture.BaselinePath) -cne [IO.Path]::GetFullPath($rawPath)) {
        throw "$Description raw capture path differs from the closed 062106 evidence path."
    }
    Assert-GpuStageRetirement062106BootSelection -Capture $Capture -Checkpoint $Description
    $rawDocument = [IO.File]::ReadAllText($rawPin.Path) | ConvertFrom-Json -ErrorAction Stop
    $rawOrder = @($rawDocument.boot_order)
    if ($rawDocument.schema -ne 1 -or [string]$rawDocument.kind -cne 'boot-selection' -or
        $null -ne $rawDocument.boot_next -or $rawOrder.Count -ne 6 -or [int]$rawDocument.findings -ne 0 -or
        [string]$rawDocument.bootmgfw_sha256 -cne $script:GpuRetirement062106BootManagerSha256 -or
        [string]$rawDocument.log_sha256 -cne $script:GpuRetirement062106ReferenceLogSha256 -or
        -not [string]::Equals([string]$Capture.IntegrityOutput,
            ('BootmgfwSha256=' + $script:GpuRetirement062106BootManagerSha256 + ' Findings=0x00000000'), [StringComparison]::Ordinal)) {
        throw "$Description raw capture lacks the fixed BootNext, BootOrder, boot-manager, reference-log, or integrity proof."
    }
    for ($index = 0; $index -lt 6; $index++) {
        if ([int]$rawOrder[$index] -ne $index) { throw "$Description raw BootOrder differs from the reviewed sequence 0 through 5." }
    }

    Assert-GpuStageRetirement062106RecordedPin -Pin $Capture.AuditToolPin `
        -ExpectedPath (Join-Path $workspace $script:GpuRetirementAuditToolRelativePath) `
        -ExpectedSha256 $script:GpuRetirementAuditToolSha256 -ExpectedBytes $script:GpuRetirementAuditToolBytes `
        -Description "$Description audit tool"
    Assert-GpuStageRetirement062106RecordedPin -Pin $Capture.ReferenceLogPin `
        -ExpectedPath (Join-Path $workspace $script:GpuRetirement062106ReferenceLogRelativePath) `
        -ExpectedSha256 $script:GpuRetirement062106ReferenceLogSha256 -ExpectedBytes $script:GpuRetirement062106ReferenceLogBytes `
        -Description "$Description reference log"

    $reviewDirectory = [IO.Path]::GetDirectoryName($rawPath)
    $receiptPath = Join-Path $reviewDirectory $ExpectedIntegrityReceiptLeaf
    if ([IO.Path]::GetFullPath([string]$Capture.IntegrityReceipt) -cne [IO.Path]::GetFullPath($receiptPath)) {
        throw "$Description integrity receipt path differs from the closed 062106 evidence path."
    }
    $receiptFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $receiptPath -WorkspaceRoot $workspace
    $integrityReceiptPin = Get-GpuStageRetirementPin -Path $receiptFullPath
    $integrityReceipt = [IO.File]::ReadAllText($receiptFullPath) | ConvertFrom-Json -ErrorAction Stop
    $auditToolPath = [IO.Path]::GetFullPath((Join-Path $workspace $script:GpuRetirementAuditToolRelativePath))
    $expectedArguments = @('bootmgr-integrity','--expected-hash',$script:GpuRetirement062106BootManagerSha256,'--esp-root','S:\')
    if ($integrityReceipt.SchemaVersion -ne 1 -or $integrityReceipt.Succeeded -ne $true -or
        [int]$integrityReceipt.ExitCode -ne 0 -or @($integrityReceipt.AllowedExitCodes).Count -ne 1 -or
        [int]$integrityReceipt.AllowedExitCodes[0] -ne 0 -or $null -ne $integrityReceipt.LaunchError -or
        [IO.Path]::GetFullPath([string]$integrityReceipt.Program) -cne $auditToolPath -or
        @($integrityReceipt.Arguments).Count -ne $expectedArguments.Count) {
        throw "$Description integrity receipt is incomplete or unsuccessful."
    }
    for ($index = 0; $index -lt $expectedArguments.Count; $index++) {
        if ([string]$integrityReceipt.Arguments[$index] -cne $expectedArguments[$index]) {
            throw "$Description integrity receipt has unexpected command arguments."
        }
    }
    $receiptStem = [IO.Path]::GetFileNameWithoutExtension([IO.Path]::GetFileNameWithoutExtension($ExpectedIntegrityReceiptLeaf))
    $stdoutPath = Join-Path $reviewDirectory ($receiptStem + '.stdout.txt')
    $stderrPath = Join-Path $reviewDirectory ($receiptStem + '.stderr.txt')
    if ([IO.Path]::GetFullPath([string]$integrityReceipt.StdoutPath) -cne [IO.Path]::GetFullPath($stdoutPath) -or
        [IO.Path]::GetFullPath([string]$integrityReceipt.StderrPath) -cne [IO.Path]::GetFullPath($stderrPath)) {
        throw "$Description integrity output paths differ from the captured receipt."
    }
    $stdoutFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stdoutPath -WorkspaceRoot $workspace
    $stderrFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stderrPath -WorkspaceRoot $workspace
    $stdoutPin = Get-GpuStageRetirementPin -Path $stdoutFullPath
    $stderrPin = Get-GpuStageRetirementPin -Path $stderrFullPath
    $expectedOutput = 'BootmgfwSha256=' + $script:GpuRetirement062106BootManagerSha256 + ' Findings=0x00000000'
    if (-not [string]::Equals(([IO.File]::ReadAllText($stdoutFullPath)).Trim(), $expectedOutput, [StringComparison]::Ordinal) -or
        [IO.File]::ReadAllText($stderrFullPath).Length -ne 0) {
        throw "$Description integrity stdout/stderr differ from the fixed reviewed result."
    }
    return @($rawPin,$integrityReceiptPin,$stdoutPin,$stderrPin)
}

function Assert-GpuStageRetirement062106Proof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $runDirectory = Join-Path (Join-Path $workspace '.analysis\gpu-baremetal\runs') $script:GpuRetirement062106RunId
    $journalPath = Join-Path $runDirectory 'journal.json'
    $archivePath = Join-Path (Join-Path $workspace '.analysis\gpu-baremetal\archive') ($script:GpuRetirement062106RunId + '.json')
    if ([string]$Journal.RunId -cne $script:GpuRetirement062106RunId) { throw 'The supplied state is not the fixed archived 062106 run.' }

    $journalPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$journalPath; Sha256=$script:GpuRetirement062106JournalSha256; Bytes=$script:GpuRetirement062106JournalBytes }) `
        -ExpectedPath $journalPath -ExpectedSha256 $script:GpuRetirement062106JournalSha256 `
        -ExpectedBytes $script:GpuRetirement062106JournalBytes -WorkspaceRoot $workspace -Description 'Completed 062106 journal'
    $Journal = [IO.File]::ReadAllText($journalPin.Path) | ConvertFrom-Json -ErrorAction Stop

    # This archive copy is made by the operator's existing Archive workflow. A
    # missing or altered copy blocks retirement before any live host operation.
    $archivePin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$archivePath; Sha256=$script:GpuRetirement062106ArchiveSha256; Bytes=$script:GpuRetirement062106ArchiveBytes }) `
        -ExpectedPath $archivePath -ExpectedSha256 $script:GpuRetirement062106ArchiveSha256 `
        -ExpectedBytes $script:GpuRetirement062106ArchiveBytes -WorkspaceRoot $workspace -Description 'Archived 062106 selector copy'
    $archivedSelector = [IO.File]::ReadAllText($archivePin.Path) | ConvertFrom-Json -ErrorAction Stop
    $sourceRoot = Join-Path $workspace '.codex-build\worktrees\gpu-windows-validation'
    if ($archivedSelector.SchemaVersion -ne 1 -or [string]$archivedSelector.RunId -cne $script:GpuRetirement062106RunId -or
        [IO.Path]::GetFullPath([string]$archivedSelector.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [IO.Path]::GetFullPath([string]$archivedSelector.JournalPath) -cne [IO.Path]::GetFullPath($journalPath) -or
        [IO.Path]::GetFullPath([string]$archivedSelector.SourceRoot) -cne [IO.Path]::GetFullPath($sourceRoot)) {
        throw 'The pinned archived selector does not identify the exact completed 062106 journal and source root.'
    }

    # Reject malformed completion state before reopening the deeper review
    # chain. The journal and selector are already pinned above, so these are
    # cheap checks over the exact completed snapshot.
    $finalActionDirectory = Join-Path $workspace $script:GpuRetirement062106FinalActionRelativePath
    $failedActionDirectory = Join-Path $workspace $script:GpuRetirement062106FailedActionRelativePath
    $last = $Journal.LastAction
    $verification = $Journal.FinalVerification
    $enumeration = if ($null -ne $verification) { $verification.FirmwareEnumeration } else { $null }
    $review = if ($null -ne $enumeration) { $enumeration.ReviewedDifference } else { $null }
    if ($Journal.SchemaVersion -ne 1 -or
        [string]$Journal.RunId -cne $script:GpuRetirement062106RunId -or
        [IO.Path]::GetFullPath([string]$Journal.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [IO.Path]::GetFullPath([string]$Journal.SourceRoot) -cne [IO.Path]::GetFullPath($sourceRoot) -or
        [string]$Journal.SourceCommit -cne $script:GpuRetirement062106SourceCommit -or
        [string]$Journal.Phase -cne 'Complete' -or $null -ne $Journal.InProgress -or
        $null -eq $last -or [string]$last.Action -cne 'FinalVerify' -or $last.Succeeded -ne $true -or
        [string]$last.Status -cne $script:GpuRetirement062106FinalResultStatus -or
        [IO.Path]::GetFullPath([string]$last.Directory) -cne [IO.Path]::GetFullPath($finalActionDirectory) -or
        [string]$Journal.DisarmedBootId -cne $script:GpuRetirement062106DisarmedBootId -or
        $null -eq $verification -or $verification.ResetBoundaryPassed -ne $true -or
        $verification.ActivationAttempted -ne $true -or $verification.SecureBoot -ne $true -or
        $verification.ActiveGpuProof -ne $false -or [string]$verification.GpuAttestation -cne 'UNATTESTED' -or
        @($verification.Errors).Count -ne 0 -or $null -eq $verification.Health -or
        $verification.Health.Passed -ne $true -or [string]$verification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$verification.Health.Aggregate -cne 'ABSENT' -or [int]$verification.Health.CpuCount -ne 20 -or
        [int]$verification.Health.ExpectedCpus -ne 20 -or [int]$verification.Health.CpuPassCount -ne 0 -or
        $null -eq $verification.Esp -or $verification.Esp.MatchesBaseline -ne $true -or
        [string]$verification.Esp.Volume -cne $script:GpuRetirement062106EspVolume -or
        [int]$verification.Esp.DiskNumber -ne $script:GpuRetirement062106EspDiskNumber -or
        [string]$verification.Esp.PartitionGuid -ine $script:GpuRetirement062106EspPartitionGuid -or
        [string]$verification.Esp.BootManagerSha256 -cne $script:GpuRetirement062106BootManagerSha256 -or
        [int]$Journal.Host.LogicalCpus -ne 20 -or [string]$verification.BootId -cne $script:GpuRetirement062106CurrentBootId -or
        $null -eq $enumeration -or $enumeration.TextEqual -ne $false -or $enumeration.ReviewedDifferenceAccepted -ne $true -or
        [string]$enumeration.BaselineSha256 -cne $script:GpuRetirement062106OriginalFirmwareSha256 -or
        [string]$enumeration.CurrentSha256 -cne $script:GpuRetirement062106ReviewedFirmwareSha256 -or
        [string]$enumeration.CaptureContext -cne 'SystemEspMountedAtS' -or $null -eq $review -or
        $review.Accepted -ne $true -or $review.HistoricalRawBootOrderUnchangedProven -ne $false -or
        $review.FullBootVariableNamespaceAttested -ne $false) {
        throw 'The 062106 journal lacks its exact completed identity, error-free ABSENT health, Secure Boot, ESP, firmware-review, and reset evidence.'
    }

    $extraPins = New-Object 'System.Collections.Generic.List[object]'
    $extraPins.Add($journalPin)
    $extraPins.Add($archivePin)
    $entryJournalPath = Join-Path $workspace $script:GpuRetirement062106EntryJournalRelativePath
    $entryJournalPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$entryJournalPath; Sha256=$script:GpuRetirement062106EntryJournalSha256; Bytes=$script:GpuRetirement062106EntryJournalBytes }) `
        -ExpectedPath $entryJournalPath -ExpectedSha256 $script:GpuRetirement062106EntryJournalSha256 `
        -ExpectedBytes $script:GpuRetirement062106EntryJournalBytes -WorkspaceRoot $workspace -Description '062106 immutable pre-retry entry journal'
    $entryJournal = [IO.File]::ReadAllText($entryJournalPin.Path) | ConvertFrom-Json -ErrorAction Stop
    if ([string]$entryJournal.RunId -cne $script:GpuRetirement062106RunId -or
        [string]$entryJournal.SourceCommit -cne $script:GpuRetirement062106SourceCommit -or
        [IO.Path]::GetFullPath([string]$entryJournal.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [string]$entryJournal.Phase -cne 'Disarmed' -or $null -eq $entryJournal.InProgress -or
        [string]$entryJournal.InProgress.Action -cne 'FinalVerify' -or
        [IO.Path]::GetFullPath([string]$entryJournal.InProgress.Directory) -cne [IO.Path]::GetFullPath($failedActionDirectory) -or
        $null -eq $entryJournal.LastAction -or [string]$entryJournal.LastAction.Action -cne 'FinalVerify' -or
        $entryJournal.LastAction.Succeeded -ne $false -or
        [string]$entryJournal.LastAction.Status -cne $script:GpuRetirement062106OriginalFinalResultStatus -or
        [IO.Path]::GetFullPath([string]$entryJournal.LastAction.Directory) -cne [IO.Path]::GetFullPath($failedActionDirectory)) {
        throw 'The immutable 062106 entry-journal copy does not preserve the inspected failed FinalVerify state.'
    }
    $extraPins.Add($entryJournalPin)

    $failedResultPath = Join-Path $workspace $script:GpuRetirement062106FailedResultRelativePath
    $failedResultPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$failedResultPath; Sha256=$script:GpuRetirement062106FailedResultSha256; Bytes=$script:GpuRetirement062106FailedResultBytes }) `
        -ExpectedPath $failedResultPath -ExpectedSha256 $script:GpuRetirement062106FailedResultSha256 `
        -ExpectedBytes $script:GpuRetirement062106FailedResultBytes -WorkspaceRoot $workspace -Description 'Original failed 062106 FinalVerify result'
    $failedResult = [IO.File]::ReadAllText($failedResultPin.Path) | ConvertFrom-Json -ErrorAction Stop
    if ([string]$failedResult.Action -cne 'FinalVerify' -or
        [string]$failedResult.Status -cne $script:GpuRetirement062106OriginalFinalResultStatus -or
        $failedResult.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$failedResult.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [IO.Path]::GetFullPath([string]$failedResult.ActionDirectory) -cne [IO.Path]::GetFullPath($failedActionDirectory)) {
        throw 'The original failed 062106 FinalVerify result is not the fixed unsuccessful action.'
    }
    $extraPins.Add($failedResultPin)

    $finalResultPath = Join-Path $workspace $script:GpuRetirement062106FinalResultRelativePath
    $finalResultPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$finalResultPath; Sha256=$script:GpuRetirement062106FinalResultSha256; Bytes=$script:GpuRetirement062106FinalResultBytes }) `
        -ExpectedPath $finalResultPath -ExpectedSha256 $script:GpuRetirement062106FinalResultSha256 `
        -ExpectedBytes $script:GpuRetirement062106FinalResultBytes -WorkspaceRoot $workspace -Description 'Successful 062106 FinalVerify result'
    $finalResult = [IO.File]::ReadAllText($finalResultPin.Path) | ConvertFrom-Json -ErrorAction Stop
    $resultVerification = $finalResult.Details.FinalVerification
    if ([string]$finalResult.Action -cne 'FinalVerify' -or
        [string]$finalResult.Status -cne $script:GpuRetirement062106FinalResultStatus -or
        $finalResult.Succeeded -ne $true -or
        [IO.Path]::GetFullPath([string]$finalResult.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [IO.Path]::GetFullPath([string]$finalResult.JournalPath) -cne [IO.Path]::GetFullPath($journalPath) -or
        [IO.Path]::GetFullPath([string]$finalResult.ActionDirectory) -cne [IO.Path]::GetFullPath($finalActionDirectory) -or
        [string]$finalResult.Phase -cne 'Complete' -or
        [string]$finalResult.Details.Status -cne $script:GpuRetirement062106FinalResultStatus -or
        $finalResult.Details.Succeeded -ne $true -or
        @($finalResult.Details.FinalVerification.Errors).Count -ne 0 -or
        $finalResult.Details.FinalVerification.ActiveGpuProof -ne $false -or
        [string]$finalResult.Details.FinalVerification.GpuAttestation -cne 'UNATTESTED') {
        throw 'The pinned 062106 FinalVerify result does not prove the fixed successful, error-free, GPU-unattested completion.'
    }

    $resultEnumeration = if ($null -ne $resultVerification) { $resultVerification.FirmwareEnumeration } else { $null }
    $resultReview = if ($null -ne $resultEnumeration) { $resultEnumeration.ReviewedDifference } else { $null }
    if ($null -eq $resultVerification -or $resultVerification.ResetBoundaryPassed -ne $true -or
        $resultVerification.ActivationAttempted -ne $true -or $resultVerification.SecureBoot -ne $true -or
        $resultVerification.ActiveGpuProof -ne $false -or [string]$resultVerification.GpuAttestation -cne 'UNATTESTED' -or
        $null -eq $resultVerification.Health -or $resultVerification.Health.Passed -ne $true -or
        [string]$resultVerification.Health.ExpectedResult -cne 'ABSENT' -or [string]$resultVerification.Health.Aggregate -cne 'ABSENT' -or
        [int]$resultVerification.Health.ExpectedCpus -ne 20 -or [int]$resultVerification.Health.CpuCount -ne 20 -or
        [int]$resultVerification.Health.CpuPassCount -ne 0 -or $null -eq $resultVerification.Esp -or
        $resultVerification.Esp.MatchesBaseline -ne $true -or
        [string]$resultVerification.Esp.Volume -cne $script:GpuRetirement062106EspVolume -or
        [int]$resultVerification.Esp.DiskNumber -ne $script:GpuRetirement062106EspDiskNumber -or
        [string]$resultVerification.Esp.PartitionGuid -ine $script:GpuRetirement062106EspPartitionGuid -or
        [string]$resultVerification.Esp.BootManagerSha256 -cne $script:GpuRetirement062106BootManagerSha256 -or
        $null -eq $resultEnumeration -or $resultEnumeration.TextEqual -ne $false -or
        $resultEnumeration.ReviewedDifferenceAccepted -ne $true -or
        [string]$resultEnumeration.BaselineSha256 -cne $script:GpuRetirement062106OriginalFirmwareSha256 -or
        [string]$resultEnumeration.CurrentSha256 -cne $script:GpuRetirement062106ReviewedFirmwareSha256 -or
        @($resultVerification.Errors).Count -ne 0 -or $null -eq $resultReview -or $resultReview.Accepted -ne $true -or
        $resultReview.HistoricalRawBootOrderUnchangedProven -ne $false -or $resultReview.FullBootVariableNamespaceAttested -ne $false) {
        throw 'The pinned 062106 FinalVerify result does not prove the exact successful reviewed firmware and reset state.'
    }

    $reviewReceiptPath = Join-Path $workspace $script:GpuRetirement062106ReviewReceiptRelativePath
    $reviewReceiptPin = Assert-GpuRetirementProfilePin -Pin $review.ReceiptPin -ExpectedPath $reviewReceiptPath `
        -ExpectedSha256 $script:GpuRetirement062106ReviewReceiptSha256 -ExpectedBytes $script:GpuRetirement062106ReviewReceiptBytes `
        -WorkspaceRoot $workspace -Description '062106 read-only firmware review receipt'
    $extraPins.Add($reviewReceiptPin)
    foreach ($receiptPin in @($resultReview.ReceiptPin)) {
        Assert-GpuStageRetirement062106RecordedPin -Pin $receiptPin -ExpectedPath $reviewReceiptPath `
            -ExpectedSha256 $script:GpuRetirement062106ReviewReceiptSha256 -ExpectedBytes $script:GpuRetirement062106ReviewReceiptBytes `
            -Description 'FinalVerify firmware-review receipt reference'
    }

    $reviewReceipt = [IO.File]::ReadAllText($reviewReceiptPin.Path) | ConvertFrom-Json -ErrorAction Stop
    $reviewDirectory = Join-Path $workspace $script:GpuRetirement062106FirmwareReviewDirectoryRelativePath
    if ($reviewReceipt.SchemaVersion -ne 1 -or [string]$reviewReceipt.Profile -cne '062106' -or
        [string]$reviewReceipt.RunId -cne $script:GpuRetirement062106RunId -or
        [string]$reviewReceipt.Status -cne 'READONLY_FIRMWARE_REVIEW_CAPTURED' -or
        $reviewReceipt.Succeeded -ne $true -or $reviewReceipt.RecoveryCompleted -ne $false -or
        $reviewReceipt.SelectorAndJournalUnchanged -ne $true -or $reviewReceipt.EspFilesChanged -ne $false -or
        $reviewReceipt.FirmwareWritten -ne $false -or $reviewReceipt.CleanupSucceeded -ne $true -or
        $reviewReceipt.EspUnmounted -ne $true -or $reviewReceipt.RawBootSelectionStable -ne $true -or
        $reviewReceipt.FirmwareTextStable -ne $true -or $reviewReceipt.HistoricalRawBootOrderUnchangedProven -ne $false -or
        ($null -ne $reviewReceipt.PSObject.Properties['FullBootVariableNamespaceAttested'] -and $reviewReceipt.FullBootVariableNamespaceAttested -ne $false) -or
        @($reviewReceipt.CleanupErrors).Count -ne 0 -or @($reviewReceipt.Errors).Count -ne 0 -or
        [IO.Path]::GetFullPath([string]$reviewReceipt.EvidenceDirectory) -cne [IO.Path]::GetFullPath($reviewDirectory) -or
        $null -eq $reviewReceipt.CurrentHost -or $reviewReceipt.CurrentHost.SecureBoot -ne $true -or
        [int]$reviewReceipt.CurrentHost.LogicalCpus -ne 20 -or
        [string]$reviewReceipt.CurrentHost.BootId -cne $script:GpuRetirement062106CurrentBootId -or
        $null -eq $reviewReceipt.Esp -or [string]$reviewReceipt.Esp.Volume -cne $script:GpuRetirement062106EspVolume -or
        [int]$reviewReceipt.Esp.DiskNumber -ne $script:GpuRetirement062106EspDiskNumber -or
        [string]$reviewReceipt.Esp.PartitionGuid -ine $script:GpuRetirement062106EspPartitionGuid -or
        [string]$reviewReceipt.Esp.BootManagerSha256 -cne $script:GpuRetirement062106BootManagerSha256 -or
        $null -eq $reviewReceipt.Health -or $reviewReceipt.Health.CommandSucceeded -ne $true -or
        $null -eq $reviewReceipt.Health.Verdict -or $reviewReceipt.Health.Verdict.Passed -ne $true -or
        [string]$reviewReceipt.Health.Verdict.ExpectedResult -cne 'ABSENT' -or
        [string]$reviewReceipt.Health.Verdict.Aggregate -cne 'ABSENT' -or
        [int]$reviewReceipt.Health.Verdict.CpuCount -ne 20 -or [int]$reviewReceipt.Health.Verdict.ExpectedCpus -ne 20 -or
        [int]$reviewReceipt.Health.Verdict.CpuPassCount -ne 0) {
        throw 'The pinned 062106 review receipt does not prove read-only cleanup, ABSENT health, stable firmware text, and current ESP state.'
    }

    $historicalPlatformPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
    $historicalStatePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1'
    $retirementModulePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuStageRetirement.psm1'
    $historicalState = @($Journal.SourcePins | Where-Object { [IO.Path]::GetFullPath([string]$_.Path) -ceq [IO.Path]::GetFullPath($historicalStatePath) }) | Select-Object -First 1
    $historicalPlatform = @($Journal.SourcePins | Where-Object { [IO.Path]::GetFullPath([string]$_.Path) -ceq [IO.Path]::GetFullPath($historicalPlatformPath) }) | Select-Object -First 1
    if ($null -eq $historicalState -or [string]$historicalState.Sha256 -cne $script:GpuRetirement062106StateSha256 -or
        [long]$historicalState.Bytes -ne $script:GpuRetirement062106StateBytes -or
        $null -eq $historicalPlatform -or [string]$historicalPlatform.Sha256 -cne $script:GpuRetirement062106HistoricalPlatformSha256 -or
        [long]$historicalPlatform.Bytes -ne $script:GpuRetirement062106HistoricalPlatformBytes) {
        throw 'The 062106 completed journal does not bind the exact historical host state and platform sources.'
    }

    $originalFirmwarePath = Join-Path $workspace $script:GpuRetirement062106OriginalFirmwareRelativePath
    $originalFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.OriginalBaselinePin -ExpectedPath $originalFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement062106OriginalFirmwareSha256 -ExpectedBytes $script:GpuRetirement062106OriginalFirmwareBytes `
        -WorkspaceRoot $workspace -Description '062106 original Backup firmware baseline'
    $extraPins.Add($originalFirmwarePin)
    Assert-GpuStageRetirement062106RecordedPin -Pin $Journal.Backup.FirmwareEnumeration -ExpectedPath $originalFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement062106OriginalFirmwareSha256 -ExpectedBytes $script:GpuRetirement062106OriginalFirmwareBytes `
        -Description '062106 journal original firmware baseline'

    $reviewedFirmwarePath = Join-Path $workspace $script:GpuRetirement062106ReviewedFirmwareRelativePath
    $reviewedFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.ReviewedFirmwarePin -ExpectedPath $reviewedFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement062106ReviewedFirmwareSha256 -ExpectedBytes $script:GpuRetirement062106ReviewedFirmwareBytes `
        -WorkspaceRoot $workspace -Description '062106 original changed firmware observation'
    $extraPins.Add($reviewedFirmwarePin)
    $freshFirmwarePath = Join-Path $workspace $script:GpuRetirement062106FreshFirmwareRelativePath
    $freshFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.FreshFirmwarePin -ExpectedPath $freshFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement062106ReviewedFirmwareSha256 -ExpectedBytes $script:GpuRetirement062106ReviewedFirmwareBytes `
        -WorkspaceRoot $workspace -Description '062106 successful FinalVerify fresh firmware capture'
    $extraPins.Add($freshFirmwarePin)
    if (-not [string]::Equals([IO.File]::ReadAllText($reviewedFirmwarePin.Path), [IO.File]::ReadAllText($freshFirmwarePin.Path), [StringComparison]::Ordinal)) {
        throw 'The 062106 final-review firmware captures do not contain identical text.'
    }
    if ([string]$Journal.Backup.BootManagerSha256 -cne $script:GpuRetirement062106BootManagerSha256) {
        throw 'The archived 062106 backup does not bind the reviewed Microsoft boot manager.'
    }

    $referenceLogPath = Join-Path $workspace $script:GpuRetirement062106ReferenceLogRelativePath
    $referenceLogPin = Assert-GpuRetirementProfilePin -Pin $Journal.Backup.ReferenceLog -ExpectedPath $referenceLogPath `
        -ExpectedSha256 $script:GpuRetirement062106ReferenceLogSha256 -ExpectedBytes $script:GpuRetirement062106ReferenceLogBytes `
        -WorkspaceRoot $workspace -Description '062106 TPM reference log'
    $extraPins.Add($referenceLogPin)
    $probePath = [IO.Path]::GetFullPath((Join-Path ([string]$Journal.Paths.LoaderBuild) 'ib-tcg-dump.exe'))
    $expectedProbePath = [IO.Path]::GetFullPath((Join-Path (Join-Path $workspace '.codex-build\gb\b3b06badc7018942\l') 'ib-tcg-dump.exe'))
    if ($probePath -cne $expectedProbePath) { throw 'The 062106 health probe is outside its fixed completed-run build directory.' }
    $probePin = Assert-GpuRetirementProfilePin -Pin $Journal.Tools.Probe -ExpectedPath $expectedProbePath `
        -ExpectedSha256 $script:GpuRetirement062106ProbeSha256 -ExpectedBytes $script:GpuRetirement062106ProbeBytes `
        -WorkspaceRoot $workspace -Description '062106 health probe'
    $extraPins.Add($probePin)

    $receiptPinSpecs = @(
        @{ Name='Selector'; Path='.analysis\gpu-baremetal\host-prepared.json'; Sha256=$script:GpuRetirement062106ArchiveSha256; Bytes=$script:GpuRetirement062106ArchiveBytes },
        @{ Name='Journal'; Path='.analysis\gpu-baremetal\runs\gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942\journal.json'; Sha256=$script:GpuRetirement062106EntryJournalSha256; Bytes=$script:GpuRetirement062106EntryJournalBytes },
        @{ Name='FirmwareBaseline'; Path=$script:GpuRetirement062106OriginalFirmwareRelativePath; Sha256=$script:GpuRetirement062106OriginalFirmwareSha256; Bytes=$script:GpuRetirement062106OriginalFirmwareBytes },
        @{ Name='FirmwareCurrent'; Path=$script:GpuRetirement062106ReviewedFirmwareRelativePath; Sha256=$script:GpuRetirement062106ReviewedFirmwareSha256; Bytes=$script:GpuRetirement062106ReviewedFirmwareBytes },
        @{ Name='Ibaudit'; Path=$script:GpuRetirementAuditToolRelativePath; Sha256=$script:GpuRetirementAuditToolSha256; Bytes=$script:GpuRetirementAuditToolBytes },
        @{ Name='ReferenceLog'; Path=$script:GpuRetirement062106ReferenceLogRelativePath; Sha256=$script:GpuRetirement062106ReferenceLogSha256; Bytes=$script:GpuRetirement062106ReferenceLogBytes },
        @{ Name='Probe'; Path='.codex-build\gb\b3b06badc7018942\l\ib-tcg-dump.exe'; Sha256=$script:GpuRetirement062106ProbeSha256; Bytes=$script:GpuRetirement062106ProbeBytes },
        @{ Name='FinalResult'; Path=$script:GpuRetirement062106FailedResultRelativePath; Sha256=$script:GpuRetirement062106FailedResultSha256; Bytes=$script:GpuRetirement062106FailedResultBytes },
        @{ Name='InspectorScript'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1'; Sha256=$script:GpuRetirement062106InspectorSha256; Bytes=$script:GpuRetirement062106InspectorBytes }
    )
    foreach ($spec in $receiptPinSpecs) {
        $expectedPath = Join-Path $workspace $spec.Path
        $before = Get-GpuRetirementProperty -Object $reviewReceipt.PinsBefore -Name $spec.Name
        $after = Get-GpuRetirementProperty -Object $reviewReceipt.PinsAfter -Name $spec.Name
        Assert-GpuStageRetirement062106RecordedPin -Pin $before -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes -Description "062106 review before/$($spec.Name)"
        Assert-GpuStageRetirement062106RecordedPin -Pin $after -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes -Description "062106 review after/$($spec.Name)"
        if ($spec.Name -notin @('Selector','Journal')) {
            $pin = Assert-GpuRetirementProfilePin -Pin $before -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes `
                -WorkspaceRoot $workspace -Description "062106 preserved $($spec.Name) evidence"
            $extraPins.Add($pin)
        }
    }
    $modulePinSpecs = @(
        @{ Name='State'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuHostState.psm1'; Sha256=$script:GpuRetirement062106StateSha256; Bytes=$script:GpuRetirement062106StateBytes },
        @{ Name='Platform'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuHostPlatform.psm1'; Sha256=$script:GpuRetirement062106HistoricalPlatformSha256; Bytes=$script:GpuRetirement062106HistoricalPlatformBytes },
        @{ Name='Retirement'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuStageRetirement.psm1'; Sha256=$script:GpuRetirement062106HistoricalRetirementSha256; Bytes=$script:GpuRetirement062106HistoricalRetirementBytes }
    )
    foreach ($spec in $modulePinSpecs) {
        $expectedPath = Join-Path $workspace $spec.Path
        foreach ($groupName in @('PinsBefore','PinsAfter')) {
            $modules = Get-GpuRetirementProperty -Object $reviewReceipt.$groupName -Name 'Modules'
            $pin = Get-GpuRetirementProperty -Object $modules -Name $spec.Name
            Assert-GpuStageRetirement062106RecordedPin -Pin $pin -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes `
                -Description "062106 $groupName/$($spec.Name) module"
        }
    }

    $currentPlatformPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
    $reviewHelperPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuFinalFirmwareReview.psm1'
    foreach ($sourceSpec in @(
        @{ Name='Platform'; Path=$currentPlatformPath; Sha256=$script:GpuRetirement062106CurrentPlatformSha256; Bytes=$script:GpuRetirement062106CurrentPlatformBytes },
        @{ Name='Helper'; Path=$reviewHelperPath; Sha256=$script:GpuRetirement062106ReviewHelperSha256; Bytes=$script:GpuRetirement062106ReviewHelperBytes }
    )) {
        foreach ($sourceEvidence in @(
            [pscustomobject]@{ Name='journal'; Pins=$review.SourcePins },
            [pscustomobject]@{ Name='result'; Pins=$resultReview.SourcePins }
        )) {
            $sourcePins = Get-GpuRetirementProperty -Object $sourceEvidence.Pins -Name $sourceSpec.Name
            if ($null -eq $sourcePins) { throw "The successful 062106 $($sourceEvidence.Name) omitted its $($sourceSpec.Name) dependency pins." }
            foreach ($groupName in @('Before','After')) {
                $recorded = Get-GpuRetirementProperty -Object $sourcePins -Name $groupName
                $pin = Assert-GpuRetirementProfilePin -Pin $recorded -ExpectedPath $sourceSpec.Path `
                    -ExpectedSha256 $sourceSpec.Sha256 -ExpectedBytes $sourceSpec.Bytes -WorkspaceRoot $workspace `
                    -Description "062106 current $($sourceSpec.Name) $($sourceEvidence.Name) $groupName source"
                $extraPins.Add($pin)
            }
        }
    }

    $reviewObservations = @($reviewReceipt.FirmwareObservations)
    if ($reviewObservations.Count -ne 2 -or [string]$reviewObservations[0].Checkpoint -cne 'before' -or
        [string]$reviewObservations[1].Checkpoint -cne 'after') {
        throw 'The 062106 firmware review receipt must contain its exact before and after text observations.'
    }
    foreach ($observation in $reviewObservations) {
        $observationPath = Assert-GpuRetirementSafeWorkspaceFile -Path ([string]$observation.Pin.Path) -WorkspaceRoot $workspace
        $observationRelative = [IO.Path]::GetFullPath($observationPath).Substring([IO.Path]::GetFullPath($reviewDirectory).Length + 1)
        if (-not $observationRelative.StartsWith('observations\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The 062106 firmware text observation is outside the pinned review receipt directory.'
        }
        $observationPin = Assert-GpuRetirementProfilePin -Pin $observation.Pin -ExpectedPath $observationPath `
            -ExpectedSha256 $script:GpuRetirement062106ReviewedFirmwareSha256 -ExpectedBytes $script:GpuRetirement062106ReviewedFirmwareBytes `
            -WorkspaceRoot $workspace -Description "062106 $($observation.Checkpoint) firmware review observation"
        if (-not [string]::Equals([IO.File]::ReadAllText($observationPin.Path), [IO.File]::ReadAllText($freshFirmwarePin.Path), [StringComparison]::Ordinal)) {
            throw 'The 062106 read-only firmware observation differs from the accepted current text.'
        }
        $extraPins.Add($observationPin)
    }

    $healthReceiptPath = Assert-GpuRetirementSafeWorkspaceFile -Path ([string]$reviewReceipt.Health.ReceiptPath) -WorkspaceRoot $workspace
    $healthReceiptPin = Get-GpuStageRetirementPin -Path $healthReceiptPath
    $healthReceipt = [IO.File]::ReadAllText($healthReceiptPath) | ConvertFrom-Json -ErrorAction Stop
    if (-not $healthReceiptPath.StartsWith([IO.Path]::GetFullPath((Join-Path $reviewDirectory 'observations')) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        $healthReceipt.SchemaVersion -ne 1 -or $healthReceipt.Succeeded -ne $true -or [int]$healthReceipt.ExitCode -ne 0 -or
        @($healthReceipt.AllowedExitCodes).Count -ne 1 -or [int]$healthReceipt.AllowedExitCodes[0] -ne 0 -or
        $null -ne $healthReceipt.LaunchError -or [IO.Path]::GetFullPath([string]$healthReceipt.Program) -cne $probePath -or
        @($healthReceipt.Arguments).Count -ne 1 -or [string]$healthReceipt.Arguments[0] -cne 'health') {
        throw 'The 062106 read-only ABSENT-health receipt is incomplete or differs from the fixed probe.'
    }
    $extraPins.Add($healthReceiptPin)

    $reviewRawCaptures = @($reviewReceipt.BootSelectionCaptures)
    if ($reviewRawCaptures.Count -ne 2) { throw 'The 062106 firmware review must contain exactly two raw boot-selection captures.' }
    $reviewRawSpecs = @(
        @{ RelativePath=$script:GpuRetirement062106FirmwareReviewDirectoryRelativePath + '\observations\before-boot-selection.json'; ReceiptLeaf=$script:GpuRetirement062106ReviewBeforeIntegrityReceiptLeaf },
        @{ RelativePath=$script:GpuRetirement062106FirmwareReviewDirectoryRelativePath + '\observations\after-boot-selection.json'; ReceiptLeaf=$script:GpuRetirement062106ReviewAfterIntegrityReceiptLeaf }
    )
    for ($index = 0; $index -lt $reviewRawSpecs.Count; $index++) {
        $rawPins = @(Assert-GpuStageRetirement062106RawCapture -Capture $reviewRawCaptures[$index] -WorkspaceRoot $workspace `
            -ExpectedRelativePath $reviewRawSpecs[$index].RelativePath -ExpectedIntegrityReceiptLeaf $reviewRawSpecs[$index].ReceiptLeaf `
            -Description "062106 read-only review raw capture $index")
        foreach ($pin in $rawPins) { $extraPins.Add($pin) }
    }

    $finalRawCaptures = @($verification.FirmwareEnumeration.ReviewedDifference.RawCaptures)
    if ($finalRawCaptures.Count -ne 2) { throw 'The successful 062106 FinalVerify must contain exactly two raw boot-selection captures.' }
    $finalRawSpecs = @(
        @{ RelativePath=$script:GpuRetirement062106FinalActionRelativePath + '\fw-review\before-boot-selection.json'; ReceiptLeaf=$script:GpuRetirement062106FinalBeforeIntegrityReceiptLeaf },
        @{ RelativePath=$script:GpuRetirement062106FinalActionRelativePath + '\fw-review\after-boot-selection.json'; ReceiptLeaf=$script:GpuRetirement062106FinalAfterIntegrityReceiptLeaf }
    )
    for ($index = 0; $index -lt $finalRawSpecs.Count; $index++) {
        $rawPins = @(Assert-GpuStageRetirement062106RawCapture -Capture $finalRawCaptures[$index] -WorkspaceRoot $workspace `
            -ExpectedRelativePath $finalRawSpecs[$index].RelativePath -ExpectedIntegrityReceiptLeaf $finalRawSpecs[$index].ReceiptLeaf `
            -Description "062106 FinalVerify raw capture $index")
        foreach ($pin in $rawPins) { $extraPins.Add($pin) }
    }

    $extraPins.Add($finalResultPin)
    return $extraPins.ToArray()
}

function Select-GpuStageRetirement062106Profile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ReadinessPath
    )

    if ($script:GpuRetirement201140ProfileSelected -or $script:GpuRetirement224018ProfileSelected -or
        $script:GpuRetirement023306ProfileSelected -or $script:GpuRetirement032117ProfileSelected -or
        $script:GpuRetirement042142ProfileSelected -or $script:GpuRetirement062106ProfileSelected -or
        $script:GpuRetirement160027ProfileSelected) {
        throw 'A completed-run retirement profile is already bound in this module instance.'
    }
    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $readinessRoot = Join-Path $workspace '.analysis\gpu-baremetal-readiness'
    if (-not [IO.Directory]::Exists($readinessRoot)) { throw 'The workspace readiness evidence directory is missing.' }
    $readinessRootFull = [IO.Path]::GetFullPath($readinessRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $readinessFull = Assert-GpuRetirementSafeWorkspaceFile -Path $ReadinessPath -WorkspaceRoot $workspace
    $prefix = $readinessRootFull + [IO.Path]::DirectorySeparatorChar
    if (-not $readinessFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ReadinessPath must name a report under .analysis\gpu-baremetal-readiness.'
    }
    $relative = $readinessFull.Substring($prefix.Length)
    if ($relative -notmatch '^([0-9a-fA-F]{32})\\readiness\.json$') {
        throw 'ReadinessPath must explicitly name <guid>\readiness.json under .analysis\gpu-baremetal-readiness.'
    }
    $readinessGuid = $Matches[1].ToLowerInvariant()
    $readinessDirectory = Join-Path $readinessRootFull $readinessGuid
    if ([IO.Path]::GetFullPath([IO.Path]::GetDirectoryName($readinessFull)) -cne [IO.Path]::GetFullPath($readinessDirectory)) {
        throw 'Readiness report directory does not match its GUID path.'
    }
    $readinessPin = Get-GpuStageRetirementPin -Path $readinessFull
    $readiness = [IO.File]::ReadAllText($readinessFull) | ConvertFrom-Json -ErrorAction Stop
    Assert-GpuStageRetirement062106Readiness -Readiness $readiness -ReadinessFullPath $readinessFull

    $script:GpuRetirement062106ProfileSelected = $true
    $script:GpuRetirementSelectedReadinessPath = $readinessFull
    $script:GpuRetirementProfile = '062106'
    $script:GpuRetirementRunId = $script:GpuRetirement062106RunId
    $script:GpuRetirementAuditRelativePath = '.analysis\gpu-baremetal-readiness\' + $readinessGuid + '\readiness.json'
    $script:GpuRetirementAuditSha256 = $readinessPin.Sha256
    $script:GpuRetirementAuditBytes = [long]$readinessPin.Bytes
    $script:GpuRetirementJournalSha256 = $script:GpuRetirement062106JournalSha256
    $script:GpuRetirementJournalBytes = $script:GpuRetirement062106JournalBytes
    $script:GpuRetirementArchiveSha256 = $script:GpuRetirement062106ArchiveSha256
    $script:GpuRetirementArchiveBytes = $script:GpuRetirement062106ArchiveBytes
    $script:GpuRetirementSourceCommit = $script:GpuRetirement062106SourceCommit
    $script:GpuRetirementProbeSha256 = $script:GpuRetirement062106ProbeSha256
    $script:GpuRetirementProbeBytes = $script:GpuRetirement062106ProbeBytes
    $script:GpuRetirementReferenceLogSha256 = $script:GpuRetirement062106ReferenceLogSha256
    $script:GpuRetirementReferenceLogBytes = $script:GpuRetirement062106ReferenceLogBytes
    $script:GpuRetirementReviewedFirmwareRelativePath = $script:GpuRetirement062106FreshFirmwareRelativePath
    $script:GpuRetirementReviewedFirmwareSha256 = $script:GpuRetirement062106ReviewedFirmwareSha256
    $script:GpuRetirementReviewedFirmwareBytes = $script:GpuRetirement062106ReviewedFirmwareBytes
    $script:GpuRetirementNames = @($script:GpuRetirement062106StagePins | ForEach-Object { $_.Name })
    return [pscustomobject]@{
        Profile = '062106'; RunId = $script:GpuRetirementRunId; ReadinessPath = $readinessFull
        ReadinessPin = $readinessPin; JournalSha256 = $script:GpuRetirementJournalSha256
        ArchiveSha256 = $script:GpuRetirementArchiveSha256; SourceCommit = $script:GpuRetirementSourceCommit
        StagePins = @($script:GpuRetirement062106StagePins)
    }
}

function Assert-GpuStageRetirement160027StageInventory {
    param(
        [Parameter(Mandatory = $true)][object[]]$Entries,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($Entries.Count -ne $script:GpuRetirement160027StagePins.Count) {
        throw "$Description must contain exactly the five pinned 160027 stage files."
    }
    $found = @{}
    foreach ($entry in $Entries) {
        $path = [string](Get-GpuRetirementProperty -Object $entry -Name 'RelativePath')
        if ($path -notmatch '^EFI\\insecure-boot\\([A-Za-z0-9._-]+)$') {
            throw "$Description contains a stage path outside the reviewed EFI directory."
        }
        $name = $Matches[1]
        $expected = @($script:GpuRetirement160027StagePins | Where-Object { $_.Name -ceq $name }) | Select-Object -First 1
        if ($null -eq $expected -or $found.ContainsKey($name)) {
            throw "$Description contains an unexpected or duplicate stage file: $name"
        }
        if ($null -eq $entry.PSObject.Properties['Kind'] -or [string]$entry.Kind -cne 'File' -or
            $null -eq $entry.PSObject.Properties['Hashed'] -or $entry.Hashed -ne $true) {
            throw "$Description contains a non-file or unhashed stage entry: $name"
        }
        if ([string]$entry.Sha256 -cne [string]$expected.Sha256 -or [long]$entry.Bytes -ne [long]$expected.Bytes) {
            throw "$Description pin differs from the fixed 160027 file: $name"
        }
        $found[$name] = $true
    }
    foreach ($expected in $script:GpuRetirement160027StagePins) {
        if (-not $found.ContainsKey([string]$expected.Name)) { throw "$Description is missing $($expected.Name)." }
    }
    return @($script:GpuRetirement160027StagePins)
}

function Assert-GpuStageRetirement160027Readiness {
    param([Parameter(Mandatory = $true)][object]$Readiness, [Parameter(Mandatory = $true)][string]$ReadinessFullPath)

    $source = $Readiness.Source
    $preflight = $Readiness.Preflight
    if ($null -eq $source -or $null -eq $preflight) { throw 'The explicit 160027 readiness report lacks source or preflight evidence.' }
    $sourceError = $null
    if ($null -ne $source.PSObject.Properties['Error']) { $sourceError = [string]$source.Error }
    $preflightError = $preflight.PSObject.Properties['Error']
    $errorsProperty = $Readiness.PSObject.Properties['Errors']
    $reportedErrors = @()
    if ($null -ne $errorsProperty) { $reportedErrors = @($errorsProperty.Value) }
    $blockers = @($Readiness.Blockers)
    $expectedBlocker = 'EFI\\insecure-boot is retained; ownership-reviewed retirement is required before a new stage. Do not delete it generically or retry Stage.'

    if ($Readiness.SchemaVersion -ne 1 -or [string]$Readiness.Status -cne 'BLOCKED_READINESS' -or
        $Readiness.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$Readiness.EvidencePath) -cne [IO.Path]::GetFullPath($ReadinessFullPath) -or
        [string]$source.Branch -cne 'feat/smbios-boot-serial-probe' -or
        [string]$source.Commit -notmatch '^[0-9a-fA-F]{40}$' -or $source.Dirty -ne $false -or
        [int]$source.DirtyEntryCount -ne 0 -or -not [string]::IsNullOrWhiteSpace($sourceError) -or
        $null -eq $preflight.PSObject.Properties['Complete'] -or $preflight.Complete -ne $true -or
        $null -eq $preflight.PSObject.Properties['InventoryComplete'] -or $preflight.InventoryComplete -ne $true -or
        $null -eq $preflight.PSObject.Properties['IntelPresent'] -or $preflight.IntelPresent -ne $true -or
        $null -eq $preflight.PSObject.Properties['NvidiaPresent'] -or $preflight.NvidiaPresent -ne $true -or
        $null -eq $preflightError -or $null -ne $preflightError.Value -or
        $blockers.Count -ne 1 -or [string]$blockers[0] -cne $expectedBlocker -or $reportedErrors.Count -ne 0 -or
        [int]$Readiness.Host.LogicalCpus -ne 20 -or $Readiness.Host.SecureBoot -ne $true -or
        $Readiness.Recovery.Valid -ne $true -or $Readiness.Recovery.Disk.IsVirtual -ne $false -or
        $null -eq $Readiness.Selector -or $Readiness.Selector.Exists -ne $false -or $Readiness.Selector.Valid -ne $true -or
        $null -ne $Readiness.Selector.Error -or $null -eq $Readiness.Cleanup -or $Readiness.Cleanup.Succeeded -ne $true -or
        @($Readiness.Cleanup.Errors).Count -ne 0 -or $null -eq $Readiness.Esp -or
        $Readiness.Esp.Inspected -ne $true -or [string]$Readiness.Esp.MountStateAfterCleanup -cne 'Unmounted' -or
        $null -eq $Readiness.Effects -or $Readiness.Effects.EspFilesChanged -ne $false -or
        $Readiness.Effects.FirmwareChanged -ne $false -or $Readiness.Effects.BootStateChanged -ne $false -or
        $Readiness.Effects.EspTemporarilyMounted -ne $true -or $null -eq $Readiness.ExistingStage -or
        $Readiness.ExistingStage.Exists -ne $true -or $Readiness.ExistingStage.Complete -ne $true -or
        [int]$Readiness.ExistingStage.EntryCount -ne 5 -or [int]$Readiness.ExistingStage.FileCount -ne 5 -or
        @($Readiness.ExistingStage.UnknownEntries).Count -ne 0 -or @($Readiness.ExistingStage.UnsafeEntries).Count -ne 0 -or
        @($Readiness.ExistingStage.Errors).Count -ne 0 -or $null -eq $Readiness.ExistingResidue -or
        $Readiness.ExistingResidue.Clear -ne $true -or @($Readiness.ExistingResidue.Residues).Count -ne 0) {
        throw 'The explicit readiness report is not a clean, unmounted BLOCKED_READINESS capture for the exact retained 160027 stage.'
    }
    if ([string]$Readiness.Esp.Identity.Volume -cne $script:GpuRetirement160027EspVolume -or
        [int]$Readiness.Esp.Identity.DiskNumber -ne $script:GpuRetirement160027EspDiskNumber -or
        [string]$Readiness.Esp.Identity.PartitionGuid -ine $script:GpuRetirement160027EspPartitionGuid -or
        [string]$Readiness.Esp.Identity.DiskUniqueId -cne $script:GpuRetirement160027EspDiskUniqueId -or
        [string]$Readiness.Esp.BootManagerPin.Sha256 -cne $script:GpuRetirement160027BootManagerSha256 -or
        [long]$Readiness.Esp.BootManagerPin.Bytes -ne $script:GpuRetirement160027BootManagerBytes) {
        throw 'The explicit readiness report identifies a different ESP or boot manager than the completed 160027 run.'
    }
    $null = Assert-GpuStageRetirement160027StageInventory -Entries @($Readiness.ExistingStage.Entries) -Description 'Fresh readiness inventory'
}

function Assert-GpuStageRetirement160027RecordedPin {
    param(
        [Parameter(Mandatory = $true)][object]$Pin,
        [Parameter(Mandatory = $true)][string]$ExpectedPath,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256,
        [Parameter(Mandatory = $true)][long]$ExpectedBytes,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($null -eq $Pin -or [IO.Path]::GetFullPath([string]$Pin.Path) -cne [IO.Path]::GetFullPath($ExpectedPath) -or
        [string]$Pin.Sha256 -cne $ExpectedSha256 -or [long]$Pin.Bytes -ne $ExpectedBytes) {
        throw "$Description metadata differs from the closed 160027 evidence pin."
    }
}

function Assert-GpuStageRetirement160027HistoricalSourcePins {
    param(
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][object]$ReviewReceipt,
        [Parameter(Mandatory = $true)][string]$SourceRoot
    )

    $statePath = Join-Path $SourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1'
    $platformPath = Join-Path $SourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
    $retirementPath = Join-Path $SourceRoot 'tools\gpu_diagnostic\GpuStageRetirement.psm1'
    $journalState = @($Journal.SourcePins | Where-Object { [IO.Path]::GetFullPath([string]$_.Path) -ceq [IO.Path]::GetFullPath($statePath) }) | Select-Object -First 1
    $journalPlatform = @($Journal.SourcePins | Where-Object { [IO.Path]::GetFullPath([string]$_.Path) -ceq [IO.Path]::GetFullPath($platformPath) }) | Select-Object -First 1
    Assert-GpuStageRetirement160027RecordedPin -Pin $journalState -ExpectedPath $statePath `
        -ExpectedSha256 $script:GpuRetirement160027StateSha256 -ExpectedBytes $script:GpuRetirement160027StateBytes `
        -Description '160027 original journal host-state source'
    Assert-GpuStageRetirement160027RecordedPin -Pin $journalPlatform -ExpectedPath $platformPath `
        -ExpectedSha256 $script:GpuRetirement160027JournalPlatformSha256 -ExpectedBytes $script:GpuRetirement160027JournalPlatformBytes `
        -Description '160027 original journal platform source'

    foreach ($groupName in @('PinsBefore','PinsAfter')) {
        $modules = Get-GpuRetirementProperty -Object $ReviewReceipt.$groupName -Name 'Modules'
        foreach ($spec in @(
            @{ Name='State'; Path=$statePath; Sha256=$script:GpuRetirement160027StateSha256; Bytes=$script:GpuRetirement160027StateBytes },
            @{ Name='Platform'; Path=$platformPath; Sha256=$script:GpuRetirement160027ReceiptPlatformSha256; Bytes=$script:GpuRetirement160027ReceiptPlatformBytes },
            @{ Name='Retirement'; Path=$retirementPath; Sha256=$script:GpuRetirement160027ReceiptRetirementSha256; Bytes=$script:GpuRetirement160027ReceiptRetirementBytes }
        )) {
            $pin = Get-GpuRetirementProperty -Object $modules -Name $spec.Name
            Assert-GpuStageRetirement160027RecordedPin -Pin $pin -ExpectedPath $spec.Path -ExpectedSha256 $spec.Sha256 `
                -ExpectedBytes $spec.Bytes -Description "160027 review $groupName historical $($spec.Name) source"
        }
    }
}

function Assert-GpuStageRetirement160027BootSelection {
    param([Parameter(Mandatory = $true)][object]$Capture, [Parameter(Mandatory = $true)][string]$Checkpoint)

    if ($null -eq $Capture -or $null -eq $Capture.Baseline) { throw "$Checkpoint raw firmware capture is missing." }
    $baseline = $Capture.Baseline
    $order = @($baseline.BootOrder)
    if ($baseline.Schema -ne 1 -or $null -ne $baseline.BootNext -or $order.Count -ne 6 -or
        [string]$baseline.BootManagerSha256 -cne $script:GpuRetirement160027BootManagerSha256 -or
        [string]$baseline.ReferenceLogSha256 -cne $script:GpuRetirement160027ReferenceLogSha256 -or
        [string]$baseline.IntegrityFindings -cne '00000000') {
        throw "$Checkpoint raw boot-selection differs from the closed 160027 reviewed state."
    }
    for ($index = 0; $index -lt 6; $index++) {
        if ($order[$index] -isnot [int] -and $order[$index] -isnot [long] -and
            $order[$index] -isnot [uint16] -and $order[$index] -isnot [uint32]) {
            throw "$Checkpoint raw BootOrder contains a nonnumeric entry."
        }
        if ([int]$order[$index] -ne $index) { throw "$Checkpoint raw BootOrder differs from the reviewed sequence 0 through 5." }
    }
}

function Assert-GpuStageRetirement160027RawCapture {
    param(
        [Parameter(Mandatory = $true)][object]$Capture,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ExpectedRelativePath,
        [Parameter(Mandatory = $true)][string]$ExpectedIntegrityReceiptLeaf,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $rawPath = Join-Path $workspace $ExpectedRelativePath
    $rawPin = Assert-GpuRetirementProfilePin -Pin $Capture.BaselinePin -ExpectedPath $rawPath `
        -ExpectedSha256 $script:GpuRetirement160027RawCaptureSha256 -ExpectedBytes $script:GpuRetirement160027RawCaptureBytes `
        -WorkspaceRoot $workspace -Description "$Description raw boot-selection capture"
    if ([IO.Path]::GetFullPath([string]$Capture.BaselinePath) -cne [IO.Path]::GetFullPath($rawPath)) {
        throw "$Description raw capture path differs from the closed 160027 evidence path."
    }
    Assert-GpuStageRetirement160027BootSelection -Capture $Capture -Checkpoint $Description
    $rawDocument = [IO.File]::ReadAllText($rawPin.Path) | ConvertFrom-Json -ErrorAction Stop
    $rawOrder = @($rawDocument.boot_order)
    if ($rawDocument.schema -ne 1 -or [string]$rawDocument.kind -cne 'boot-selection' -or
        $null -ne $rawDocument.boot_next -or $rawOrder.Count -ne 6 -or [int]$rawDocument.findings -ne 0 -or
        [string]$rawDocument.bootmgfw_sha256 -cne $script:GpuRetirement160027BootManagerSha256 -or
        [string]$rawDocument.log_sha256 -cne $script:GpuRetirement160027ReferenceLogSha256 -or
        -not [string]::Equals([string]$Capture.IntegrityOutput,
            ('BootmgfwSha256=' + $script:GpuRetirement160027BootManagerSha256 + ' Findings=0x00000000'), [StringComparison]::Ordinal)) {
        throw "$Description raw capture lacks the fixed BootNext, BootOrder, boot-manager, reference-log, or integrity proof."
    }
    for ($index = 0; $index -lt 6; $index++) {
        if ([int]$rawOrder[$index] -ne $index) { throw "$Description raw BootOrder differs from the reviewed sequence 0 through 5." }
    }

    Assert-GpuStageRetirement160027RecordedPin -Pin $Capture.AuditToolPin `
        -ExpectedPath (Join-Path $workspace $script:GpuRetirementAuditToolRelativePath) `
        -ExpectedSha256 $script:GpuRetirementAuditToolSha256 -ExpectedBytes $script:GpuRetirementAuditToolBytes `
        -Description "$Description audit tool"
    Assert-GpuStageRetirement160027RecordedPin -Pin $Capture.ReferenceLogPin `
        -ExpectedPath (Join-Path $workspace $script:GpuRetirement160027ReferenceLogRelativePath) `
        -ExpectedSha256 $script:GpuRetirement160027ReferenceLogSha256 -ExpectedBytes $script:GpuRetirement160027ReferenceLogBytes `
        -Description "$Description reference log"

    $reviewDirectory = [IO.Path]::GetDirectoryName($rawPath)
    $receiptPath = Join-Path $reviewDirectory $ExpectedIntegrityReceiptLeaf
    if ([IO.Path]::GetFullPath([string]$Capture.IntegrityReceipt) -cne [IO.Path]::GetFullPath($receiptPath)) {
        throw "$Description integrity receipt path differs from the closed 160027 evidence path."
    }
    $receiptFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $receiptPath -WorkspaceRoot $workspace
    $integrityReceiptPin = Get-GpuStageRetirementPin -Path $receiptFullPath
    $integrityReceipt = [IO.File]::ReadAllText($receiptFullPath) | ConvertFrom-Json -ErrorAction Stop
    $auditToolPath = [IO.Path]::GetFullPath((Join-Path $workspace $script:GpuRetirementAuditToolRelativePath))
    $expectedArguments = @('bootmgr-integrity','--expected-hash',$script:GpuRetirement160027BootManagerSha256,'--esp-root','S:\')
    if ($integrityReceipt.SchemaVersion -ne 1 -or $integrityReceipt.Succeeded -ne $true -or
        [int]$integrityReceipt.ExitCode -ne 0 -or @($integrityReceipt.AllowedExitCodes).Count -ne 1 -or
        [int]$integrityReceipt.AllowedExitCodes[0] -ne 0 -or $null -ne $integrityReceipt.LaunchError -or
        [IO.Path]::GetFullPath([string]$integrityReceipt.Program) -cne $auditToolPath -or
        @($integrityReceipt.Arguments).Count -ne $expectedArguments.Count) {
        throw "$Description integrity receipt is incomplete or unsuccessful."
    }
    for ($index = 0; $index -lt $expectedArguments.Count; $index++) {
        if ([string]$integrityReceipt.Arguments[$index] -cne $expectedArguments[$index]) {
            throw "$Description integrity receipt has unexpected command arguments."
        }
    }
    $receiptStem = [IO.Path]::GetFileNameWithoutExtension([IO.Path]::GetFileNameWithoutExtension($ExpectedIntegrityReceiptLeaf))
    $stdoutPath = Join-Path $reviewDirectory ($receiptStem + '.stdout.txt')
    $stderrPath = Join-Path $reviewDirectory ($receiptStem + '.stderr.txt')
    if ([IO.Path]::GetFullPath([string]$integrityReceipt.StdoutPath) -cne [IO.Path]::GetFullPath($stdoutPath) -or
        [IO.Path]::GetFullPath([string]$integrityReceipt.StderrPath) -cne [IO.Path]::GetFullPath($stderrPath)) {
        throw "$Description integrity output paths differ from the captured receipt."
    }
    $stdoutFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stdoutPath -WorkspaceRoot $workspace
    $stderrFullPath = Assert-GpuRetirementSafeWorkspaceFile -Path $stderrPath -WorkspaceRoot $workspace
    $stdoutPin = Get-GpuStageRetirementPin -Path $stdoutFullPath
    $stderrPin = Get-GpuStageRetirementPin -Path $stderrFullPath
    $expectedOutput = 'BootmgfwSha256=' + $script:GpuRetirement160027BootManagerSha256 + ' Findings=0x00000000'
    if (-not [string]::Equals(([IO.File]::ReadAllText($stdoutFullPath)).Trim(), $expectedOutput, [StringComparison]::Ordinal) -or
        [IO.File]::ReadAllText($stderrFullPath).Length -ne 0) {
        throw "$Description integrity stdout/stderr differ from the fixed reviewed result."
    }
    return @($rawPin,$integrityReceiptPin,$stdoutPin,$stderrPin)
}

function Assert-GpuStageRetirement160027Proof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $runDirectory = Join-Path (Join-Path $workspace '.analysis\gpu-baremetal\runs') $script:GpuRetirement160027RunId
    $journalPath = Join-Path $runDirectory 'journal.json'
    $archivePath = Join-Path (Join-Path $workspace '.analysis\gpu-baremetal\archive') ($script:GpuRetirement160027RunId + '.json')
    if ([string]$Journal.RunId -cne $script:GpuRetirement160027RunId) { throw 'The supplied state is not the fixed archived 160027 run.' }

    $journalPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$journalPath; Sha256=$script:GpuRetirement160027JournalSha256; Bytes=$script:GpuRetirement160027JournalBytes }) `
        -ExpectedPath $journalPath -ExpectedSha256 $script:GpuRetirement160027JournalSha256 `
        -ExpectedBytes $script:GpuRetirement160027JournalBytes -WorkspaceRoot $workspace -Description 'Completed 160027 journal'
    $Journal = [IO.File]::ReadAllText($journalPin.Path) | ConvertFrom-Json -ErrorAction Stop

    # This archive copy is made by the operator's existing Archive workflow. A
    # missing or altered copy blocks retirement before any live host operation.
    $archivePin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$archivePath; Sha256=$script:GpuRetirement160027ArchiveSha256; Bytes=$script:GpuRetirement160027ArchiveBytes }) `
        -ExpectedPath $archivePath -ExpectedSha256 $script:GpuRetirement160027ArchiveSha256 `
        -ExpectedBytes $script:GpuRetirement160027ArchiveBytes -WorkspaceRoot $workspace -Description 'Archived 160027 selector copy'
    $archivedSelector = [IO.File]::ReadAllText($archivePin.Path) | ConvertFrom-Json -ErrorAction Stop
    $sourceRoot = Join-Path $workspace '.codex-build\worktrees\gpu-windows-validation'
    if ($archivedSelector.SchemaVersion -ne 1 -or [string]$archivedSelector.RunId -cne $script:GpuRetirement160027RunId -or
        [IO.Path]::GetFullPath([string]$archivedSelector.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [IO.Path]::GetFullPath([string]$archivedSelector.JournalPath) -cne [IO.Path]::GetFullPath($journalPath) -or
        [IO.Path]::GetFullPath([string]$archivedSelector.SourceRoot) -cne [IO.Path]::GetFullPath($sourceRoot)) {
        throw 'The pinned archived selector does not identify the exact completed 160027 journal and source root.'
    }

    # Reject malformed completion state before reopening the deeper review
    # chain. The journal and selector are already pinned above, so these are
    # cheap checks over the exact completed snapshot.
    $finalActionDirectory = Join-Path $workspace $script:GpuRetirement160027FinalActionRelativePath
    $failedActionDirectory = Join-Path $workspace $script:GpuRetirement160027FailedActionRelativePath
    $last = $Journal.LastAction
    $verification = $Journal.FinalVerification
    $enumeration = if ($null -ne $verification) { $verification.FirmwareEnumeration } else { $null }
    $review = if ($null -ne $enumeration) { $enumeration.ReviewedDifference } else { $null }
    if ($Journal.SchemaVersion -ne 1 -or
        [string]$Journal.RunId -cne $script:GpuRetirement160027RunId -or
        [IO.Path]::GetFullPath([string]$Journal.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [IO.Path]::GetFullPath([string]$Journal.SourceRoot) -cne [IO.Path]::GetFullPath($sourceRoot) -or
        [string]$Journal.SourceCommit -cne $script:GpuRetirement160027SourceCommit -or
        [string]$Journal.Phase -cne 'Complete' -or $null -ne $Journal.InProgress -or
        $null -eq $last -or [string]$last.Action -cne 'FinalVerify' -or $last.Succeeded -ne $true -or
        [string]$last.Status -cne $script:GpuRetirement160027FinalResultStatus -or
        [IO.Path]::GetFullPath([string]$last.Directory) -cne [IO.Path]::GetFullPath($finalActionDirectory) -or
        [string]$Journal.DisarmedBootId -cne $script:GpuRetirement160027DisarmedBootId -or
        $null -eq $verification -or $verification.ResetBoundaryPassed -ne $true -or
        $verification.ActivationAttempted -ne $true -or $verification.SecureBoot -ne $true -or
        $verification.ActiveGpuProof -ne $false -or [string]$verification.GpuAttestation -cne 'UNATTESTED' -or
        @($verification.Errors).Count -ne 0 -or $null -eq $verification.Health -or
        $verification.Health.Passed -ne $true -or [string]$verification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$verification.Health.Aggregate -cne 'ABSENT' -or [int]$verification.Health.CpuCount -ne 20 -or
        [int]$verification.Health.ExpectedCpus -ne 20 -or [int]$verification.Health.CpuPassCount -ne 0 -or
        $null -eq $verification.Esp -or $verification.Esp.MatchesBaseline -ne $true -or
        [string]$verification.Esp.Volume -cne $script:GpuRetirement160027EspVolume -or
        [int]$verification.Esp.DiskNumber -ne $script:GpuRetirement160027EspDiskNumber -or
        [string]$verification.Esp.PartitionGuid -ine $script:GpuRetirement160027EspPartitionGuid -or
        [string]$verification.Esp.BootManagerSha256 -cne $script:GpuRetirement160027BootManagerSha256 -or
        [int]$Journal.Host.LogicalCpus -ne 20 -or [string]$verification.BootId -cne $script:GpuRetirement160027CurrentBootId -or
        $null -eq $enumeration -or $enumeration.TextEqual -ne $false -or $enumeration.ReviewedDifferenceAccepted -ne $true -or
        [string]$enumeration.BaselineSha256 -cne $script:GpuRetirement160027OriginalFirmwareSha256 -or
        [string]$enumeration.CurrentSha256 -cne $script:GpuRetirement160027ReviewedFirmwareSha256 -or
        [string]$enumeration.CaptureContext -cne 'SystemEspMountedAtS' -or $null -eq $review -or
        $review.Accepted -ne $true -or $review.HistoricalRawBootOrderUnchangedProven -ne $false -or
        $review.FullBootVariableNamespaceAttested -ne $false) {
        throw 'The 160027 journal lacks its exact completed identity, error-free ABSENT health, Secure Boot, ESP, firmware-review, and reset evidence.'
    }

    $extraPins = New-Object 'System.Collections.Generic.List[object]'
    $extraPins.Add($journalPin)
    $extraPins.Add($archivePin)
    $entryJournalPath = Join-Path $workspace $script:GpuRetirement160027EntryJournalRelativePath
    $entryJournalPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$entryJournalPath; Sha256=$script:GpuRetirement160027EntryJournalSha256; Bytes=$script:GpuRetirement160027EntryJournalBytes }) `
        -ExpectedPath $entryJournalPath -ExpectedSha256 $script:GpuRetirement160027EntryJournalSha256 `
        -ExpectedBytes $script:GpuRetirement160027EntryJournalBytes -WorkspaceRoot $workspace -Description '160027 immutable pre-retry entry journal'
    $entryJournal = [IO.File]::ReadAllText($entryJournalPin.Path) | ConvertFrom-Json -ErrorAction Stop
    if ([string]$entryJournal.RunId -cne $script:GpuRetirement160027RunId -or
        [string]$entryJournal.SourceCommit -cne $script:GpuRetirement160027SourceCommit -or
        [IO.Path]::GetFullPath([string]$entryJournal.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [string]$entryJournal.Phase -cne 'Disarmed' -or $null -eq $entryJournal.InProgress -or
        [string]$entryJournal.InProgress.Action -cne 'FinalVerify' -or
        [IO.Path]::GetFullPath([string]$entryJournal.InProgress.Directory) -cne [IO.Path]::GetFullPath($failedActionDirectory) -or
        $null -eq $entryJournal.LastAction -or [string]$entryJournal.LastAction.Action -cne 'FinalVerify' -or
        $entryJournal.LastAction.Succeeded -ne $false -or
        [string]$entryJournal.LastAction.Status -cne $script:GpuRetirement160027OriginalFinalResultStatus -or
        [IO.Path]::GetFullPath([string]$entryJournal.LastAction.Directory) -cne [IO.Path]::GetFullPath($failedActionDirectory)) {
        throw 'The immutable 160027 entry-journal copy does not preserve the inspected failed FinalVerify state.'
    }
    $extraPins.Add($entryJournalPin)

    $failedResultPath = Join-Path $workspace $script:GpuRetirement160027FailedResultRelativePath
    $failedResultPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$failedResultPath; Sha256=$script:GpuRetirement160027FailedResultSha256; Bytes=$script:GpuRetirement160027FailedResultBytes }) `
        -ExpectedPath $failedResultPath -ExpectedSha256 $script:GpuRetirement160027FailedResultSha256 `
        -ExpectedBytes $script:GpuRetirement160027FailedResultBytes -WorkspaceRoot $workspace -Description 'Original failed 160027 FinalVerify result'
    $failedResult = [IO.File]::ReadAllText($failedResultPin.Path) | ConvertFrom-Json -ErrorAction Stop
    if ([string]$failedResult.Action -cne 'FinalVerify' -or
        [string]$failedResult.Status -cne $script:GpuRetirement160027OriginalFinalResultStatus -or
        $failedResult.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$failedResult.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [IO.Path]::GetFullPath([string]$failedResult.ActionDirectory) -cne [IO.Path]::GetFullPath($failedActionDirectory)) {
        throw 'The original failed 160027 FinalVerify result is not the fixed unsuccessful action.'
    }
    $extraPins.Add($failedResultPin)

    $finalResultPath = Join-Path $workspace $script:GpuRetirement160027FinalResultRelativePath
    $finalResultPin = Assert-GpuRetirementProfilePin `
        -Pin ([pscustomobject]@{ Path=$finalResultPath; Sha256=$script:GpuRetirement160027FinalResultSha256; Bytes=$script:GpuRetirement160027FinalResultBytes }) `
        -ExpectedPath $finalResultPath -ExpectedSha256 $script:GpuRetirement160027FinalResultSha256 `
        -ExpectedBytes $script:GpuRetirement160027FinalResultBytes -WorkspaceRoot $workspace -Description 'Successful 160027 FinalVerify result'
    $finalResult = [IO.File]::ReadAllText($finalResultPin.Path) | ConvertFrom-Json -ErrorAction Stop
    $resultVerification = $finalResult.Details.FinalVerification
    if ([string]$finalResult.Action -cne 'FinalVerify' -or
        [string]$finalResult.Status -cne $script:GpuRetirement160027FinalResultStatus -or
        $finalResult.Succeeded -ne $true -or
        [IO.Path]::GetFullPath([string]$finalResult.RunDirectory) -cne [IO.Path]::GetFullPath($runDirectory) -or
        [IO.Path]::GetFullPath([string]$finalResult.JournalPath) -cne [IO.Path]::GetFullPath($journalPath) -or
        [IO.Path]::GetFullPath([string]$finalResult.ActionDirectory) -cne [IO.Path]::GetFullPath($finalActionDirectory) -or
        [string]$finalResult.Phase -cne 'Complete' -or
        [string]$finalResult.Details.Status -cne $script:GpuRetirement160027FinalResultStatus -or
        $finalResult.Details.Succeeded -ne $true -or
        @($finalResult.Details.FinalVerification.Errors).Count -ne 0 -or
        $finalResult.Details.FinalVerification.ActiveGpuProof -ne $false -or
        [string]$finalResult.Details.FinalVerification.GpuAttestation -cne 'UNATTESTED') {
        throw 'The pinned 160027 FinalVerify result does not prove the fixed successful, error-free, GPU-unattested completion.'
    }

    $resultEnumeration = if ($null -ne $resultVerification) { $resultVerification.FirmwareEnumeration } else { $null }
    $resultReview = if ($null -ne $resultEnumeration) { $resultEnumeration.ReviewedDifference } else { $null }
    if ($null -eq $resultVerification -or $resultVerification.ResetBoundaryPassed -ne $true -or
        $resultVerification.ActivationAttempted -ne $true -or $resultVerification.SecureBoot -ne $true -or
        $resultVerification.ActiveGpuProof -ne $false -or [string]$resultVerification.GpuAttestation -cne 'UNATTESTED' -or
        $null -eq $resultVerification.Health -or $resultVerification.Health.Passed -ne $true -or
        [string]$resultVerification.Health.ExpectedResult -cne 'ABSENT' -or [string]$resultVerification.Health.Aggregate -cne 'ABSENT' -or
        [int]$resultVerification.Health.ExpectedCpus -ne 20 -or [int]$resultVerification.Health.CpuCount -ne 20 -or
        [int]$resultVerification.Health.CpuPassCount -ne 0 -or $null -eq $resultVerification.Esp -or
        $resultVerification.Esp.MatchesBaseline -ne $true -or
        [string]$resultVerification.Esp.Volume -cne $script:GpuRetirement160027EspVolume -or
        [int]$resultVerification.Esp.DiskNumber -ne $script:GpuRetirement160027EspDiskNumber -or
        [string]$resultVerification.Esp.PartitionGuid -ine $script:GpuRetirement160027EspPartitionGuid -or
        [string]$resultVerification.Esp.BootManagerSha256 -cne $script:GpuRetirement160027BootManagerSha256 -or
        $null -eq $resultEnumeration -or $resultEnumeration.TextEqual -ne $false -or
        $resultEnumeration.ReviewedDifferenceAccepted -ne $true -or
        [string]$resultEnumeration.BaselineSha256 -cne $script:GpuRetirement160027OriginalFirmwareSha256 -or
        [string]$resultEnumeration.CurrentSha256 -cne $script:GpuRetirement160027ReviewedFirmwareSha256 -or
        @($resultVerification.Errors).Count -ne 0 -or $null -eq $resultReview -or $resultReview.Accepted -ne $true -or
        $resultReview.HistoricalRawBootOrderUnchangedProven -ne $false -or $resultReview.FullBootVariableNamespaceAttested -ne $false) {
        throw 'The pinned 160027 FinalVerify result does not prove the exact successful reviewed firmware and reset state.'
    }

    $reviewReceiptPath = Join-Path $workspace $script:GpuRetirement160027ReviewReceiptRelativePath
    $reviewReceiptPin = Assert-GpuRetirementProfilePin -Pin $review.ReceiptPin -ExpectedPath $reviewReceiptPath `
        -ExpectedSha256 $script:GpuRetirement160027ReviewReceiptSha256 -ExpectedBytes $script:GpuRetirement160027ReviewReceiptBytes `
        -WorkspaceRoot $workspace -Description '160027 read-only firmware review receipt'
    $extraPins.Add($reviewReceiptPin)
    foreach ($receiptPin in @($resultReview.ReceiptPin)) {
        Assert-GpuStageRetirement160027RecordedPin -Pin $receiptPin -ExpectedPath $reviewReceiptPath `
            -ExpectedSha256 $script:GpuRetirement160027ReviewReceiptSha256 -ExpectedBytes $script:GpuRetirement160027ReviewReceiptBytes `
            -Description 'FinalVerify firmware-review receipt reference'
    }

    $reviewReceipt = [IO.File]::ReadAllText($reviewReceiptPin.Path) | ConvertFrom-Json -ErrorAction Stop
    Assert-GpuStageRetirement160027HistoricalSourcePins -Journal $Journal -ReviewReceipt $reviewReceipt -SourceRoot $sourceRoot
    $reviewDirectory = Join-Path $workspace $script:GpuRetirement160027FirmwareReviewDirectoryRelativePath
    if ($reviewReceipt.SchemaVersion -ne 1 -or [string]$reviewReceipt.Profile -cne '160027' -or
        [string]$reviewReceipt.RunId -cne $script:GpuRetirement160027RunId -or
        [string]$reviewReceipt.Status -cne 'READONLY_FIRMWARE_REVIEW_CAPTURED' -or
        $reviewReceipt.Succeeded -ne $true -or $reviewReceipt.RecoveryCompleted -ne $false -or
        $reviewReceipt.SelectorAndJournalUnchanged -ne $true -or $reviewReceipt.EspFilesChanged -ne $false -or
        $reviewReceipt.FirmwareWritten -ne $false -or $reviewReceipt.CleanupSucceeded -ne $true -or
        $reviewReceipt.EspUnmounted -ne $true -or $reviewReceipt.RawBootSelectionStable -ne $true -or
        $reviewReceipt.FirmwareTextStable -ne $true -or $reviewReceipt.HistoricalRawBootOrderUnchangedProven -ne $false -or
        ($null -ne $reviewReceipt.PSObject.Properties['FullBootVariableNamespaceAttested'] -and $reviewReceipt.FullBootVariableNamespaceAttested -ne $false) -or
        @($reviewReceipt.CleanupErrors).Count -ne 0 -or @($reviewReceipt.Errors).Count -ne 0 -or
        [IO.Path]::GetFullPath([string]$reviewReceipt.EvidenceDirectory) -cne [IO.Path]::GetFullPath($reviewDirectory) -or
        $null -eq $reviewReceipt.CurrentHost -or $reviewReceipt.CurrentHost.SecureBoot -ne $true -or
        [int]$reviewReceipt.CurrentHost.LogicalCpus -ne 20 -or
        [string]$reviewReceipt.CurrentHost.BootId -cne $script:GpuRetirement160027CurrentBootId -or
        $null -eq $reviewReceipt.Esp -or [string]$reviewReceipt.Esp.Volume -cne $script:GpuRetirement160027EspVolume -or
        [int]$reviewReceipt.Esp.DiskNumber -ne $script:GpuRetirement160027EspDiskNumber -or
        [string]$reviewReceipt.Esp.PartitionGuid -ine $script:GpuRetirement160027EspPartitionGuid -or
        [string]$reviewReceipt.Esp.BootManagerSha256 -cne $script:GpuRetirement160027BootManagerSha256 -or
        $null -eq $reviewReceipt.Health -or $reviewReceipt.Health.CommandSucceeded -ne $true -or
        $null -eq $reviewReceipt.Health.Verdict -or $reviewReceipt.Health.Verdict.Passed -ne $true -or
        [string]$reviewReceipt.Health.Verdict.ExpectedResult -cne 'ABSENT' -or
        [string]$reviewReceipt.Health.Verdict.Aggregate -cne 'ABSENT' -or
        [int]$reviewReceipt.Health.Verdict.CpuCount -ne 20 -or [int]$reviewReceipt.Health.Verdict.ExpectedCpus -ne 20 -or
        [int]$reviewReceipt.Health.Verdict.CpuPassCount -ne 0) {
        throw 'The pinned 160027 review receipt does not prove read-only cleanup, ABSENT health, stable firmware text, and current ESP state.'
    }

    $originalFirmwarePath = Join-Path $workspace $script:GpuRetirement160027OriginalFirmwareRelativePath
    $originalFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.OriginalBaselinePin -ExpectedPath $originalFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement160027OriginalFirmwareSha256 -ExpectedBytes $script:GpuRetirement160027OriginalFirmwareBytes `
        -WorkspaceRoot $workspace -Description '160027 original Backup firmware baseline'
    $extraPins.Add($originalFirmwarePin)
    Assert-GpuStageRetirement160027RecordedPin -Pin $Journal.Backup.FirmwareEnumeration -ExpectedPath $originalFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement160027OriginalFirmwareSha256 -ExpectedBytes $script:GpuRetirement160027OriginalFirmwareBytes `
        -Description '160027 journal original firmware baseline'

    $reviewedFirmwarePath = Join-Path $workspace $script:GpuRetirement160027ReviewedFirmwareRelativePath
    $reviewedFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.ReviewedFirmwarePin -ExpectedPath $reviewedFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement160027ReviewedFirmwareSha256 -ExpectedBytes $script:GpuRetirement160027ReviewedFirmwareBytes `
        -WorkspaceRoot $workspace -Description '160027 original changed firmware observation'
    $extraPins.Add($reviewedFirmwarePin)
    $freshFirmwarePath = Join-Path $workspace $script:GpuRetirement160027FreshFirmwareRelativePath
    $freshFirmwarePin = Assert-GpuRetirementProfilePin -Pin $review.FreshFirmwarePin -ExpectedPath $freshFirmwarePath `
        -ExpectedSha256 $script:GpuRetirement160027ReviewedFirmwareSha256 -ExpectedBytes $script:GpuRetirement160027ReviewedFirmwareBytes `
        -WorkspaceRoot $workspace -Description '160027 successful FinalVerify fresh firmware capture'
    $extraPins.Add($freshFirmwarePin)
    if (-not [string]::Equals([IO.File]::ReadAllText($reviewedFirmwarePin.Path), [IO.File]::ReadAllText($freshFirmwarePin.Path), [StringComparison]::Ordinal)) {
        throw 'The 160027 final-review firmware captures do not contain identical text.'
    }
    if ([string]$Journal.Backup.BootManagerSha256 -cne $script:GpuRetirement160027BootManagerSha256) {
        throw 'The archived 160027 backup does not bind the reviewed Microsoft boot manager.'
    }

    $referenceLogPath = Join-Path $workspace $script:GpuRetirement160027ReferenceLogRelativePath
    $referenceLogPin = Assert-GpuRetirementProfilePin -Pin $Journal.Backup.ReferenceLog -ExpectedPath $referenceLogPath `
        -ExpectedSha256 $script:GpuRetirement160027ReferenceLogSha256 -ExpectedBytes $script:GpuRetirement160027ReferenceLogBytes `
        -WorkspaceRoot $workspace -Description '160027 TPM reference log'
    $extraPins.Add($referenceLogPin)
    $probePath = [IO.Path]::GetFullPath((Join-Path ([string]$Journal.Paths.LoaderBuild) 'ib-tcg-dump.exe'))
    $expectedProbePath = [IO.Path]::GetFullPath((Join-Path $workspace $script:GpuRetirement160027ProbeRelativePath))
    if ($probePath -cne $expectedProbePath) { throw 'The 160027 health probe is outside its fixed completed-run build directory.' }
    $probePin = Assert-GpuRetirementProfilePin -Pin $Journal.Tools.Probe -ExpectedPath $expectedProbePath `
        -ExpectedSha256 $script:GpuRetirement160027ProbeSha256 -ExpectedBytes $script:GpuRetirement160027ProbeBytes `
        -WorkspaceRoot $workspace -Description '160027 health probe'
    $extraPins.Add($probePin)

    $receiptPinSpecs = @(
        @{ Name='Selector'; Path='.analysis\gpu-baremetal\host-prepared.json'; Sha256=$script:GpuRetirement160027ArchiveSha256; Bytes=$script:GpuRetirement160027ArchiveBytes },
        @{ Name='Journal'; Path='.analysis\gpu-baremetal\runs\gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff\journal.json'; Sha256=$script:GpuRetirement160027EntryJournalSha256; Bytes=$script:GpuRetirement160027EntryJournalBytes },
        @{ Name='FirmwareBaseline'; Path=$script:GpuRetirement160027OriginalFirmwareRelativePath; Sha256=$script:GpuRetirement160027OriginalFirmwareSha256; Bytes=$script:GpuRetirement160027OriginalFirmwareBytes },
        @{ Name='FirmwareCurrent'; Path=$script:GpuRetirement160027ReviewedFirmwareRelativePath; Sha256=$script:GpuRetirement160027ReviewedFirmwareSha256; Bytes=$script:GpuRetirement160027ReviewedFirmwareBytes },
        @{ Name='Ibaudit'; Path=$script:GpuRetirementAuditToolRelativePath; Sha256=$script:GpuRetirementAuditToolSha256; Bytes=$script:GpuRetirementAuditToolBytes },
        @{ Name='ReferenceLog'; Path=$script:GpuRetirement160027ReferenceLogRelativePath; Sha256=$script:GpuRetirement160027ReferenceLogSha256; Bytes=$script:GpuRetirement160027ReferenceLogBytes },
        @{ Name='Probe'; Path=$script:GpuRetirement160027ProbeRelativePath; Sha256=$script:GpuRetirement160027ProbeSha256; Bytes=$script:GpuRetirement160027ProbeBytes },
        @{ Name='FinalResult'; Path=$script:GpuRetirement160027FailedResultRelativePath; Sha256=$script:GpuRetirement160027FailedResultSha256; Bytes=$script:GpuRetirement160027FailedResultBytes },
        @{ Name='InspectorScript'; Path='.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1'; Sha256=$script:GpuRetirement160027InspectorSha256; Bytes=$script:GpuRetirement160027InspectorBytes }
    )
    foreach ($spec in $receiptPinSpecs) {
        $expectedPath = Join-Path $workspace $spec.Path
        $before = Get-GpuRetirementProperty -Object $reviewReceipt.PinsBefore -Name $spec.Name
        $after = Get-GpuRetirementProperty -Object $reviewReceipt.PinsAfter -Name $spec.Name
        Assert-GpuStageRetirement160027RecordedPin -Pin $before -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes -Description "160027 review before/$($spec.Name)"
        Assert-GpuStageRetirement160027RecordedPin -Pin $after -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes -Description "160027 review after/$($spec.Name)"
        if ($spec.Name -notin @('Selector','Journal')) {
            $pin = Assert-GpuRetirementProfilePin -Pin $before -ExpectedPath $expectedPath -ExpectedSha256 $spec.Sha256 -ExpectedBytes $spec.Bytes `
                -WorkspaceRoot $workspace -Description "160027 preserved $($spec.Name) evidence"
            $extraPins.Add($pin)
        }
    }
    $currentPlatformPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
    $reviewHelperPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuFinalFirmwareReview.psm1'
    foreach ($sourceSpec in @(
        @{ Name='Platform'; Path=$currentPlatformPath; Sha256=$script:GpuRetirement160027CurrentPlatformSha256; Bytes=$script:GpuRetirement160027CurrentPlatformBytes },
        @{ Name='Helper'; Path=$reviewHelperPath; Sha256=$script:GpuRetirement160027ReviewHelperSha256; Bytes=$script:GpuRetirement160027ReviewHelperBytes }
    )) {
        foreach ($sourceEvidence in @(
            [pscustomobject]@{ Name='journal'; Pins=$review.SourcePins },
            [pscustomobject]@{ Name='result'; Pins=$resultReview.SourcePins }
        )) {
            $sourcePins = Get-GpuRetirementProperty -Object $sourceEvidence.Pins -Name $sourceSpec.Name
            if ($null -eq $sourcePins) { throw "The successful 160027 $($sourceEvidence.Name) omitted its $($sourceSpec.Name) dependency pins." }
            foreach ($groupName in @('Before','After')) {
                $recorded = Get-GpuRetirementProperty -Object $sourcePins -Name $groupName
                $pin = Assert-GpuRetirementProfilePin -Pin $recorded -ExpectedPath $sourceSpec.Path `
                    -ExpectedSha256 $sourceSpec.Sha256 -ExpectedBytes $sourceSpec.Bytes -WorkspaceRoot $workspace `
                    -Description "160027 current $($sourceSpec.Name) $($sourceEvidence.Name) $groupName source"
                $extraPins.Add($pin)
            }
        }
    }

    $reviewObservations = @($reviewReceipt.FirmwareObservations)
    if ($reviewObservations.Count -ne 2 -or [string]$reviewObservations[0].Checkpoint -cne 'before' -or
        [string]$reviewObservations[1].Checkpoint -cne 'after') {
        throw 'The 160027 firmware review receipt must contain its exact before and after text observations.'
    }
    foreach ($observation in $reviewObservations) {
        $observationPath = Assert-GpuRetirementSafeWorkspaceFile -Path ([string]$observation.Pin.Path) -WorkspaceRoot $workspace
        $observationRelative = [IO.Path]::GetFullPath($observationPath).Substring([IO.Path]::GetFullPath($reviewDirectory).Length + 1)
        if (-not $observationRelative.StartsWith('observations\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The 160027 firmware text observation is outside the pinned review receipt directory.'
        }
        $observationPin = Assert-GpuRetirementProfilePin -Pin $observation.Pin -ExpectedPath $observationPath `
            -ExpectedSha256 $script:GpuRetirement160027ReviewedFirmwareSha256 -ExpectedBytes $script:GpuRetirement160027ReviewedFirmwareBytes `
            -WorkspaceRoot $workspace -Description "160027 $($observation.Checkpoint) firmware review observation"
        if (-not [string]::Equals([IO.File]::ReadAllText($observationPin.Path), [IO.File]::ReadAllText($freshFirmwarePin.Path), [StringComparison]::Ordinal)) {
            throw 'The 160027 read-only firmware observation differs from the accepted current text.'
        }
        $extraPins.Add($observationPin)
    }

    $healthReceiptPath = Assert-GpuRetirementSafeWorkspaceFile -Path ([string]$reviewReceipt.Health.ReceiptPath) -WorkspaceRoot $workspace
    $healthReceiptPin = Get-GpuStageRetirementPin -Path $healthReceiptPath
    $healthReceipt = [IO.File]::ReadAllText($healthReceiptPath) | ConvertFrom-Json -ErrorAction Stop
    if (-not $healthReceiptPath.StartsWith([IO.Path]::GetFullPath((Join-Path $reviewDirectory 'observations')) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        $healthReceipt.SchemaVersion -ne 1 -or $healthReceipt.Succeeded -ne $true -or [int]$healthReceipt.ExitCode -ne 0 -or
        @($healthReceipt.AllowedExitCodes).Count -ne 1 -or [int]$healthReceipt.AllowedExitCodes[0] -ne 0 -or
        $null -ne $healthReceipt.LaunchError -or [IO.Path]::GetFullPath([string]$healthReceipt.Program) -cne $probePath -or
        @($healthReceipt.Arguments).Count -ne 1 -or [string]$healthReceipt.Arguments[0] -cne 'health') {
        throw 'The 160027 read-only ABSENT-health receipt is incomplete or differs from the fixed probe.'
    }
    $extraPins.Add($healthReceiptPin)

    $reviewRawCaptures = @($reviewReceipt.BootSelectionCaptures)
    if ($reviewRawCaptures.Count -ne 2) { throw 'The 160027 firmware review must contain exactly two raw boot-selection captures.' }
    $reviewRawSpecs = @(
        @{ RelativePath=$script:GpuRetirement160027FirmwareReviewDirectoryRelativePath + '\observations\before-boot-selection.json'; ReceiptLeaf=$script:GpuRetirement160027ReviewBeforeIntegrityReceiptLeaf },
        @{ RelativePath=$script:GpuRetirement160027FirmwareReviewDirectoryRelativePath + '\observations\after-boot-selection.json'; ReceiptLeaf=$script:GpuRetirement160027ReviewAfterIntegrityReceiptLeaf }
    )
    for ($index = 0; $index -lt $reviewRawSpecs.Count; $index++) {
        $rawPins = @(Assert-GpuStageRetirement160027RawCapture -Capture $reviewRawCaptures[$index] -WorkspaceRoot $workspace `
            -ExpectedRelativePath $reviewRawSpecs[$index].RelativePath -ExpectedIntegrityReceiptLeaf $reviewRawSpecs[$index].ReceiptLeaf `
            -Description "160027 read-only review raw capture $index")
        foreach ($pin in $rawPins) { $extraPins.Add($pin) }
    }

    $finalRawCaptures = @($verification.FirmwareEnumeration.ReviewedDifference.RawCaptures)
    if ($finalRawCaptures.Count -ne 2) { throw 'The successful 160027 FinalVerify must contain exactly two raw boot-selection captures.' }
    $finalRawSpecs = @(
        @{ RelativePath=$script:GpuRetirement160027FinalActionRelativePath + '\fw-review\before-boot-selection.json'; ReceiptLeaf=$script:GpuRetirement160027FinalBeforeIntegrityReceiptLeaf },
        @{ RelativePath=$script:GpuRetirement160027FinalActionRelativePath + '\fw-review\after-boot-selection.json'; ReceiptLeaf=$script:GpuRetirement160027FinalAfterIntegrityReceiptLeaf }
    )
    for ($index = 0; $index -lt $finalRawSpecs.Count; $index++) {
        $rawPins = @(Assert-GpuStageRetirement160027RawCapture -Capture $finalRawCaptures[$index] -WorkspaceRoot $workspace `
            -ExpectedRelativePath $finalRawSpecs[$index].RelativePath -ExpectedIntegrityReceiptLeaf $finalRawSpecs[$index].ReceiptLeaf `
            -Description "160027 FinalVerify raw capture $index")
        foreach ($pin in $rawPins) { $extraPins.Add($pin) }
    }

    $extraPins.Add($finalResultPin)
    return $extraPins.ToArray()
}

function Select-GpuStageRetirement160027Profile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$ReadinessPath
    )

    if ($script:GpuRetirement201140ProfileSelected -or $script:GpuRetirement224018ProfileSelected -or
        $script:GpuRetirement023306ProfileSelected -or $script:GpuRetirement032117ProfileSelected -or
        $script:GpuRetirement042142ProfileSelected -or $script:GpuRetirement062106ProfileSelected -or
        $script:GpuRetirement160027ProfileSelected) {
        throw 'A completed-run retirement profile is already bound in this module instance.'
    }
    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $readinessRoot = Join-Path $workspace '.analysis\gpu-baremetal-readiness'
    if (-not [IO.Directory]::Exists($readinessRoot)) { throw 'The workspace readiness evidence directory is missing.' }
    $readinessRootFull = [IO.Path]::GetFullPath($readinessRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $readinessFull = Assert-GpuRetirementSafeWorkspaceFile -Path $ReadinessPath -WorkspaceRoot $workspace
    $prefix = $readinessRootFull + [IO.Path]::DirectorySeparatorChar
    if (-not $readinessFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ReadinessPath must name a report under .analysis\gpu-baremetal-readiness.'
    }
    $relative = $readinessFull.Substring($prefix.Length)
    if ($relative -notmatch '^([0-9a-fA-F]{32})\\readiness\.json$') {
        throw 'ReadinessPath must explicitly name <guid>\readiness.json under .analysis\gpu-baremetal-readiness.'
    }
    $readinessGuid = $Matches[1].ToLowerInvariant()
    $readinessDirectory = Join-Path $readinessRootFull $readinessGuid
    if ([IO.Path]::GetFullPath([IO.Path]::GetDirectoryName($readinessFull)) -cne [IO.Path]::GetFullPath($readinessDirectory)) {
        throw 'Readiness report directory does not match its GUID path.'
    }
    $readinessPin = Get-GpuStageRetirementPin -Path $readinessFull
    $readiness = [IO.File]::ReadAllText($readinessFull) | ConvertFrom-Json -ErrorAction Stop
    Assert-GpuStageRetirement160027Readiness -Readiness $readiness -ReadinessFullPath $readinessFull

    $script:GpuRetirement160027ProfileSelected = $true
    $script:GpuRetirementSelectedReadinessPath = $readinessFull
    $script:GpuRetirementProfile = '160027'
    $script:GpuRetirementRunId = $script:GpuRetirement160027RunId
    $script:GpuRetirementAuditRelativePath = '.analysis\gpu-baremetal-readiness\' + $readinessGuid + '\readiness.json'
    $script:GpuRetirementAuditSha256 = $readinessPin.Sha256
    $script:GpuRetirementAuditBytes = [long]$readinessPin.Bytes
    $script:GpuRetirementJournalSha256 = $script:GpuRetirement160027JournalSha256
    $script:GpuRetirementJournalBytes = $script:GpuRetirement160027JournalBytes
    $script:GpuRetirementArchiveSha256 = $script:GpuRetirement160027ArchiveSha256
    $script:GpuRetirementArchiveBytes = $script:GpuRetirement160027ArchiveBytes
    $script:GpuRetirementSourceCommit = $script:GpuRetirement160027SourceCommit
    $script:GpuRetirementProbeSha256 = $script:GpuRetirement160027ProbeSha256
    $script:GpuRetirementProbeBytes = $script:GpuRetirement160027ProbeBytes
    $script:GpuRetirementReferenceLogSha256 = $script:GpuRetirement160027ReferenceLogSha256
    $script:GpuRetirementReferenceLogBytes = $script:GpuRetirement160027ReferenceLogBytes
    $script:GpuRetirementReviewedFirmwareRelativePath = $script:GpuRetirement160027FreshFirmwareRelativePath
    $script:GpuRetirementReviewedFirmwareSha256 = $script:GpuRetirement160027ReviewedFirmwareSha256
    $script:GpuRetirementReviewedFirmwareBytes = $script:GpuRetirement160027ReviewedFirmwareBytes
    $script:GpuRetirementNames = @($script:GpuRetirement160027StagePins | ForEach-Object { $_.Name })
    return [pscustomobject]@{
        Profile = '160027'; RunId = $script:GpuRetirementRunId; ReadinessPath = $readinessFull
        ReadinessPin = $readinessPin; JournalSha256 = $script:GpuRetirementJournalSha256
        ArchiveSha256 = $script:GpuRetirementArchiveSha256; SourceCommit = $script:GpuRetirementSourceCommit
        StagePins = @($script:GpuRetirement160027StagePins)
    }
}

function Assert-GpuStageRetirementEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Audit,
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][object]$ArchivedSelector,
        [Parameter(Mandatory = $true)][string]$ExpectedAuditSha256,
        [Parameter(Mandatory = $true)][string]$ExpectedRunId,
        [Parameter(Mandatory = $true)][string]$ExpectedRunDirectory,
        [Parameter(Mandatory = $true)][string]$ExpectedJournalPath,
        [Parameter(Mandatory = $true)][string]$ExpectedSourceRoot,
        [string]$ExpectedWorkspaceRoot
    )

    if ([string]::IsNullOrWhiteSpace($ExpectedWorkspaceRoot)) {
        $ExpectedWorkspaceRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $ExpectedRunDirectory)))
    }
    $ExpectedWorkspaceRoot = [IO.Path]::GetFullPath($ExpectedWorkspaceRoot)

    if ($Audit.SchemaVersion -ne 1 -or $Audit.Status -cne 'BLOCKED_READINESS') {
        throw 'The reviewed readiness audit has an unsupported schema or status.'
    }
    if ($ExpectedAuditSha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw 'The expected audit pin is malformed.'
    }
    if ($Journal.SchemaVersion -ne 1 -or [string]$Journal.RunId -cne $ExpectedRunId -or
        [string]$ExpectedRunId -cne $script:GpuRetirementRunId) {
        throw 'Journal identity is outside the fixed reviewed retirement target.'
    }
    if ([string]$Journal.SourceCommit -cne $script:GpuRetirementSourceCommit) {
        throw 'Archived source commit differs from the reviewed completed run.'
    }
    Assert-GpuRetirementEqual -Expected $ExpectedRunDirectory -Actual ([string]$Journal.RunDirectory) -Description 'Journal run directory'
    Assert-GpuRetirementEqual -Expected $ExpectedSourceRoot -Actual ([string]$Journal.SourceRoot) -Description 'Journal source root'
    # Schema 1 records JournalPath on the selector; the journal records RunDirectory.
    Assert-GpuRetirementEqual -Expected ([IO.Path]::GetFullPath($ExpectedJournalPath)) -Actual ([IO.Path]::GetFullPath([string]$Journal.RunDirectory + '\journal.json')) -Description 'Journal path'
    if ([string]$Journal.Phase -cne 'Complete' -or $null -ne $Journal.InProgress) {
        throw 'The reviewed run is not complete or has an action in progress.'
    }
    if ($null -eq $Journal.LastAction -or [string]$Journal.LastAction.Action -cne 'FinalVerify' -or
        $Journal.LastAction.Succeeded -ne $true) {
        throw 'The reviewed run lacks a successful final verification as its last action.'
    }
    if ([string]::IsNullOrWhiteSpace([string]$Journal.DisarmedBootId)) { throw 'The run has no recorded disarm boot boundary.' }

    $verification = $Journal.FinalVerification
    $firmwareBaselineValid = if ($script:GpuRetirement201140ProfileSelected) {
        $null -ne $verification -and $null -ne $verification.FirmwareEnumeration -and
        $verification.FirmwareEnumeration.TextEqual -eq $false -and
        $verification.FirmwareEnumeration.ReviewedDifferenceAccepted -eq $true -and
        [string]$verification.FirmwareEnumeration.BaselineSha256 -ceq $script:GpuRetirement201140OriginalFirmwareSha256 -and
        [string]$verification.FirmwareEnumeration.CurrentSha256 -ceq $script:GpuRetirement201140CurrentFirmwareSha256
    } elseif ($script:GpuRetirement224018ProfileSelected) {
        $null -ne $verification -and $null -ne $verification.FirmwareEnumeration -and
        $verification.FirmwareEnumeration.TextEqual -eq $false -and
        $verification.FirmwareEnumeration.ReviewedDifferenceAccepted -eq $true -and
        [string]$verification.FirmwareEnumeration.BaselineSha256 -ceq $script:GpuRetirement224018OriginalFirmwareSha256 -and
        [string]$verification.FirmwareEnumeration.CurrentSha256 -ceq $script:GpuRetirement224018CurrentFirmwareSha256
    } elseif ($script:GpuRetirement042142ProfileSelected) {
        $null -ne $verification -and $null -ne $verification.FirmwareEnumeration -and
        $verification.FirmwareEnumeration.TextEqual -eq $false -and
        $verification.FirmwareEnumeration.ReviewedDifferenceAccepted -eq $true -and
        [string]$verification.FirmwareEnumeration.BaselineSha256 -ceq $script:GpuRetirement042142OriginalFirmwareSha256 -and
        [string]$verification.FirmwareEnumeration.CurrentSha256 -ceq $script:GpuRetirement042142ReviewedFirmwareSha256
    } elseif ($script:GpuRetirement062106ProfileSelected) {
        $null -ne $verification -and $null -ne $verification.FirmwareEnumeration -and
        $verification.FirmwareEnumeration.TextEqual -eq $false -and
        $verification.FirmwareEnumeration.ReviewedDifferenceAccepted -eq $true -and
        [string]$verification.FirmwareEnumeration.BaselineSha256 -ceq $script:GpuRetirement062106OriginalFirmwareSha256 -and
        [string]$verification.FirmwareEnumeration.CurrentSha256 -ceq $script:GpuRetirement062106ReviewedFirmwareSha256
    } elseif ($script:GpuRetirement160027ProfileSelected) {
        $null -ne $verification -and $null -ne $verification.FirmwareEnumeration -and
        $verification.FirmwareEnumeration.TextEqual -eq $false -and
        $verification.FirmwareEnumeration.ReviewedDifferenceAccepted -eq $true -and
        [string]$verification.FirmwareEnumeration.BaselineSha256 -ceq $script:GpuRetirement160027OriginalFirmwareSha256 -and
        [string]$verification.FirmwareEnumeration.CurrentSha256 -ceq $script:GpuRetirement160027ReviewedFirmwareSha256
    } elseif ($script:GpuRetirement023306ProfileSelected) {
        $null -ne $verification -and $null -ne $verification.FirmwareEnumeration -and
        $verification.FirmwareEnumeration.TextEqual -eq $true -and
        [string]$verification.FirmwareEnumeration.BaselineSha256 -ceq $script:GpuRetirement023306OriginalFirmwareSha256 -and
        [string]$verification.FirmwareEnumeration.CurrentSha256 -ceq $script:GpuRetirement023306OriginalFirmwareSha256 -and
        [string]$verification.FirmwareEnumeration.CaptureContext -ceq 'SystemEspMountedAtS' -and
        ($null -eq $verification.FirmwareEnumeration.PSObject.Properties['ReviewedDifferenceAccepted'] -or
         $verification.FirmwareEnumeration.ReviewedDifferenceAccepted -ne $true)
    } elseif ($script:GpuRetirement032117ProfileSelected) {
        $null -ne $verification -and $null -ne $verification.FirmwareEnumeration -and
        $verification.FirmwareEnumeration.TextEqual -eq $true -and
        [string]$verification.FirmwareEnumeration.BaselineSha256 -ceq $script:GpuRetirement032117FirmwareSha256 -and
        [string]$verification.FirmwareEnumeration.CurrentSha256 -ceq $script:GpuRetirement032117FirmwareSha256 -and
        [string]$verification.FirmwareEnumeration.CaptureContext -ceq 'SystemEspMountedAtS' -and
        ($null -eq $verification.FirmwareEnumeration.PSObject.Properties['ReviewedDifferenceAccepted'] -or
         $verification.FirmwareEnumeration.ReviewedDifferenceAccepted -ne $true)
    } else {
        $null -ne $verification -and $null -ne $verification.FirmwareEnumeration -and
        $verification.FirmwareEnumeration.TextEqual -eq $true
    }
    if ($null -eq $verification -or $verification.ResetBoundaryPassed -ne $true -or
        $verification.SecureBoot -ne $true -or $verification.ActivationAttempted -ne $true -or
        $null -eq $verification.Health -or $verification.Health.Passed -ne $true -or
        [string]$verification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$verification.Health.Aggregate -cne 'ABSENT' -or
        [int]$verification.Health.CpuCount -ne [int]$Journal.Host.LogicalCpus -or
        [int]$verification.Health.ExpectedCpus -ne [int]$Journal.Host.LogicalCpus -or
        $null -eq $verification.Esp -or $verification.Esp.MatchesBaseline -ne $true -or
        -not $firmwareBaselineValid -or
        @($verification.Errors).Count -ne 0) {
        throw 'Final verification does not prove the required reset, ABSENT health, Secure Boot, ESP, and approved firmware gates.'
    }

    if ($ArchivedSelector.SchemaVersion -ne 1 -or [string]$ArchivedSelector.RunId -cne $ExpectedRunId) {
        throw 'The archived selector is not for the fixed reviewed run.'
    }
    Assert-GpuRetirementEqual -Expected $ExpectedRunDirectory -Actual ([string]$ArchivedSelector.RunDirectory) -Description 'Archived selector run directory'
    Assert-GpuRetirementEqual -Expected $ExpectedJournalPath -Actual ([string]$ArchivedSelector.JournalPath) -Description 'Archived selector journal path'
    Assert-GpuRetirementEqual -Expected $ExpectedSourceRoot -Actual ([string]$ArchivedSelector.SourceRoot) -Description 'Archived selector source root'

    $expectedAuditCount = [int]$script:GpuRetirementNames.Count
    $expectedAuditWord = if ($expectedAuditCount -eq 5) { 'five' } else { 'seven' }
    if ($Audit.ExistingStage.Exists -ne $true -or $Audit.ExistingStage.Complete -ne $true -or
        [int]$Audit.ExistingStage.EntryCount -ne $expectedAuditCount -or [int]$Audit.ExistingStage.FileCount -ne $expectedAuditCount -or
        @($Audit.ExistingStage.UnknownEntries).Count -ne 0 -or
        @($Audit.ExistingStage.UnsafeEntries).Count -ne 0 -or @($Audit.ExistingStage.Errors).Count -ne 0 -or
        $Audit.Esp.MountStateAfterCleanup -cne 'Unmounted' -or
        $Audit.ExistingResidue.Clear -ne $true -or @($Audit.ExistingResidue.Residues).Count -ne 0) {
        throw "Readiness audit does not contain the complete, safe $expectedAuditWord-file stage inventory and clean mount/residue state."
    }

    $auditEntries = @($Audit.ExistingStage.Entries)
    if ($auditEntries.Count -ne $expectedAuditCount) { throw "Readiness audit entry count is not exactly $expectedAuditWord." }
    if ($script:GpuRetirement042142ProfileSelected) {
        $null = Assert-GpuStageRetirement042142StageInventory -Entries $auditEntries -Description 'Readiness audit inventory'
    } elseif ($script:GpuRetirement062106ProfileSelected) {
        $null = Assert-GpuStageRetirement062106StageInventory -Entries $auditEntries -Description 'Readiness audit inventory'
    } elseif ($script:GpuRetirement160027ProfileSelected) {
        $null = Assert-GpuStageRetirement160027StageInventory -Entries $auditEntries -Description 'Readiness audit inventory'
    }
    $entryMap = @{}
    foreach ($entry in $auditEntries) {
        $relativePath = [string]$entry.RelativePath
        if ($entry.Kind -cne 'File' -or $entry.Hashed -ne $true -or
            $relativePath -notmatch '^EFI\\insecure-boot\\([A-Za-z0-9._-]+)$') {
            throw 'Readiness audit contains an unknown or unsafe stage entry.'
        }
        $name = $Matches[1]
        if ($script:GpuRetirementNames -cnotcontains $name -or $entryMap.ContainsKey($name)) {
            throw "Readiness audit contains an unknown or duplicate stage entry: $relativePath"
        }
        if ([string]$entry.Sha256 -notmatch '^[0-9A-Fa-f]{64}$' -or [long]$entry.Bytes -lt 0) {
            throw "Readiness audit pin is malformed: $relativePath"
        }
        $entryMap[$name] = [pscustomobject]@{ Name=$name; RelativePath=$relativePath; Sha256=[string]$entry.Sha256; Bytes=[long]$entry.Bytes }
    }
    foreach ($name in $script:GpuRetirementNames) { if (-not $entryMap.ContainsKey($name)) { throw "Readiness audit is missing $name." } }

    $recordedStageFiles = @($Journal.Stage.Files)
    if ($recordedStageFiles.Count -ne 5) { throw 'The archived run must record exactly five installer-staged files.' }
    $recordedMap = @{}
    foreach ($file in $recordedStageFiles) {
        $path = [string]$file.RelativePath
        if ($path -notmatch '^EFI\\insecure-boot\\([A-Za-z0-9._-]+)$') { throw 'Journal stage path is malformed.' }
        $name = $Matches[1]
        if ($name -cin @('handoff-status.txt', 'payload-diagnostic.txt') -or $recordedMap.ContainsKey($name) -or -not $entryMap.ContainsKey($name)) {
            throw "Journal has an unexpected staged file: $path"
        }
        if ([string]$file.Sha256 -cne $entryMap[$name].Sha256 -or [long]$file.Bytes -ne $entryMap[$name].Bytes) {
            throw "Journal stage pin differs from the reviewed audit: $path"
        }
        $recordedMap[$name] = $true
    }
    foreach ($name in $script:GpuRetirementNames) {
        if ($name -cnotin @('handoff-status.txt', 'payload-diagnostic.txt') -and -not $recordedMap.ContainsKey($name)) { throw "Journal stage is missing $name." }
    }

    $collectionFiles = @($Journal.Collection.Esp.StageFiles)
    $collectionMap = @{}
    if ($script:GpuRetirement042142ProfileSelected) {
        if ($collectionFiles.Count -ne 7) { throw 'The 042142 collection must contain five staged files and two explicit absent diagnostic rows.' }
        $diagnosticNames = @('handoff-status.txt','payload-diagnostic.txt')
        foreach ($file in $collectionFiles) {
            $name = [string]$file.Name
            if ($collectionMap.ContainsKey($name)) { throw "The 042142 collection contains a duplicate row: $name" }
            if ($diagnosticNames -ccontains $name) {
                if ($file.Present -ne $false -or $null -ne $file.Pin -or $null -ne $file.Path) {
                    throw "The 042142 diagnostic collection row is not explicitly absent: $name"
                }
                $collectionMap[$name] = 'Absent'
                continue
            }
            $pin = $file.Pin
            if ($script:GpuRetirementNames -cnotcontains $name -or $file.Present -ne $true -or
                $null -eq $pin -or [string]$pin.Sha256 -cne $entryMap[$name].Sha256 -or
                [long]$pin.Bytes -ne $entryMap[$name].Bytes -or
                [IO.Path]::GetFullPath([string]$file.Path) -cne [IO.Path]::GetFullPath((Join-Path (Join-Path $ExpectedWorkspaceRoot $script:GpuRetirement042142CollectionActionRelativePath) ('esp-' + $name))) -or
                [IO.Path]::GetFullPath([string]$pin.Path) -cne [IO.Path]::GetFullPath([string]$file.Path)) {
                throw "Collected stage pin differs from the reviewed audit: $name"
            }
            $null = Assert-GpuRetirementProfilePin -Pin $pin -ExpectedPath ([string]$file.Path) `
                -ExpectedSha256 $entryMap[$name].Sha256 -ExpectedBytes ([long]$entryMap[$name].Bytes) `
                -WorkspaceRoot $ExpectedWorkspaceRoot -Description "042142 collected $name snapshot"
            $collectionMap[$name] = 'Present'
        }
        foreach ($name in $script:GpuRetirementNames) { if ($collectionMap[$name] -cne 'Present') { throw "Collection is missing $name." } }
        foreach ($name in $diagnosticNames) { if ($collectionMap[$name] -cne 'Absent') { throw "Collection is missing the explicit absent row for $name." } }
    } elseif ($script:GpuRetirement062106ProfileSelected) {
        if ($collectionFiles.Count -ne 7) { throw 'The 062106 collection must contain five staged files and two explicit absent diagnostic rows.' }
        $diagnosticNames = @('handoff-status.txt','payload-diagnostic.txt')
        $collectionActionDirectory = Join-Path $ExpectedWorkspaceRoot $script:GpuRetirement062106CollectionActionRelativePath
        foreach ($file in $collectionFiles) {
            $name = [string]$file.Name
            if ($collectionMap.ContainsKey($name)) { throw "The 062106 collection contains a duplicate row: $name" }
            if ($diagnosticNames -ccontains $name) {
                if ($file.Present -ne $false -or $null -ne $file.Pin -or $null -ne $file.Path) {
                    throw "The 062106 diagnostic collection row is not explicitly absent: $name"
                }
                $collectionMap[$name] = 'Absent'
                continue
            }
            $pin = $file.Pin
            $expectedPath = Join-Path $collectionActionDirectory ('esp-' + $name)
            if ($script:GpuRetirementNames -cnotcontains $name -or $file.Present -ne $true -or
                $null -eq $pin -or [string]$pin.Sha256 -cne $entryMap[$name].Sha256 -or
                [long]$pin.Bytes -ne $entryMap[$name].Bytes -or
                [IO.Path]::GetFullPath([string]$file.Path) -cne [IO.Path]::GetFullPath($expectedPath) -or
                [IO.Path]::GetFullPath([string]$pin.Path) -cne [IO.Path]::GetFullPath([string]$file.Path)) {
                throw "The 062106 collected stage pin differs from the reviewed audit: $name"
            }
            $null = Assert-GpuRetirementProfilePin -Pin $pin -ExpectedPath $expectedPath `
                -ExpectedSha256 $entryMap[$name].Sha256 -ExpectedBytes ([long]$entryMap[$name].Bytes) `
                -WorkspaceRoot $ExpectedWorkspaceRoot -Description "062106 collected $name snapshot"
            $collectionMap[$name] = 'Present'
        }
        foreach ($name in $script:GpuRetirementNames) { if ($collectionMap[$name] -cne 'Present') { throw "The 062106 collection is missing $name." } }
        foreach ($name in $diagnosticNames) { if ($collectionMap[$name] -cne 'Absent') { throw "The 062106 collection is missing the explicit absent row for $name." } }
    } elseif ($script:GpuRetirement160027ProfileSelected) {
        if ($collectionFiles.Count -ne 7) { throw 'The 160027 collection must contain five staged files and two explicit absent diagnostic rows.' }
        $diagnosticNames = @('handoff-status.txt','payload-diagnostic.txt')
        $collectionActionDirectory = Join-Path $ExpectedWorkspaceRoot $script:GpuRetirement160027CollectionActionRelativePath
        foreach ($file in $collectionFiles) {
            $name = [string]$file.Name
            if ($collectionMap.ContainsKey($name)) { throw "The 160027 collection contains a duplicate row: $name" }
            if ($diagnosticNames -ccontains $name) {
                if ($file.Present -ne $false -or $null -ne $file.Pin -or $null -ne $file.Path) {
                    throw "The 160027 diagnostic collection row is not explicitly absent: $name"
                }
                $collectionMap[$name] = 'Absent'
                continue
            }
            $pin = $file.Pin
            $expectedPath = Join-Path $collectionActionDirectory ('esp-' + $name)
            if ($script:GpuRetirementNames -cnotcontains $name -or $file.Present -ne $true -or
                $null -eq $pin -or [string]$pin.Sha256 -cne $entryMap[$name].Sha256 -or
                [long]$pin.Bytes -ne $entryMap[$name].Bytes -or
                [IO.Path]::GetFullPath([string]$file.Path) -cne [IO.Path]::GetFullPath($expectedPath) -or
                [IO.Path]::GetFullPath([string]$pin.Path) -cne [IO.Path]::GetFullPath([string]$file.Path)) {
                throw "The 160027 collected stage pin differs from the reviewed audit: $name"
            }
            $null = Assert-GpuRetirementProfilePin -Pin $pin -ExpectedPath $expectedPath `
                -ExpectedSha256 $entryMap[$name].Sha256 -ExpectedBytes ([long]$entryMap[$name].Bytes) `
                -WorkspaceRoot $ExpectedWorkspaceRoot -Description "160027 collected $name snapshot"
            $collectionMap[$name] = 'Present'
        }
        foreach ($name in $script:GpuRetirementNames) { if ($collectionMap[$name] -cne 'Present') { throw "The 160027 collection is missing $name." } }
        foreach ($name in $diagnosticNames) { if ($collectionMap[$name] -cne 'Absent') { throw "The 160027 collection is missing the explicit absent row for $name." } }
    } else {
        if ($collectionFiles.Count -ne 7) { throw 'The archived collection must record exactly seven stage files.' }
        foreach ($file in $collectionFiles) {
            $name = [string]$file.Name
            $pin = $file.Pin
            if ($script:GpuRetirementNames -cnotcontains $name -or $collectionMap.ContainsKey($name) -or
                $null -eq $pin -or [string]$pin.Sha256 -cne $entryMap[$name].Sha256 -or
                [long]$pin.Bytes -ne $entryMap[$name].Bytes) {
                throw "Collected stage pin differs from the reviewed audit: $name"
            }
            $collectionMap[$name] = $true
        }
        foreach ($name in $script:GpuRetirementNames) { if (-not $collectionMap.ContainsKey($name)) { throw "Collection is missing $name." } }
    }

    if ($null -eq $Audit.Esp.Identity -or $null -eq $Journal.Backup) { throw 'Reviewed ESP identity or backup is missing.' }
    Assert-GpuRetirementEqual -Expected ([string]$Journal.Backup.EspVolume) -Actual ([string]$Audit.Esp.Identity.Volume) -Description 'Audit ESP volume'
    Assert-GpuRetirementEqual -Expected ([string]$Journal.Backup.EspPartitionGuid) -Actual ([string]$Audit.Esp.Identity.PartitionGuid) -Description 'Audit ESP partition GUID'
    if ([int]$Journal.Backup.EspDiskNumber -ne [int]$Audit.Esp.Identity.DiskNumber -or
        -not [string]::Equals([string]$Journal.Backup.BootManagerSha256, [string]$Audit.Esp.BootManagerPin.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Audit ESP disk or boot-manager pin differs from the archived backup.'
    }

    return @($script:GpuRetirementNames | ForEach-Object { $entryMap[$_] })
}

function Get-GpuStageRetirementLiveInventory {
    param([Parameter(Mandatory = $true)][string]$StageDirectory)

    $directory = [IO.Path]::GetFullPath($StageDirectory)
    if (-not [IO.Directory]::Exists($directory)) { throw 'The reviewed stage directory is missing.' }
    $directoryAttributes = [IO.File]::GetAttributes($directory)
    if (($directoryAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'The stage directory is a reparse point.' }
    $items = @([IO.Directory]::EnumerateFileSystemEntries($directory))
    $expectedCount = [int]$script:GpuRetirementNames.Count
    $expectedWord = if ($expectedCount -eq 5) { 'five' } else { 'seven' }
    if ($items.Count -ne $expectedCount) { throw "The live stage contains $($items.Count) entries; exactly $expectedWord reviewed files are required." }
    $found = @{}
    foreach ($itemPath in $items) {
        $attributes = [IO.File]::GetAttributes($itemPath)
        if (($attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "The live stage contains a directory or reparse point: $itemPath"
        }
        $name = [IO.Path]::GetFileName($itemPath)
        if ($script:GpuRetirementNames -cnotcontains $name -or $found.ContainsKey($name)) {
            throw "The live stage contains an unknown or duplicate file: $name"
        }
        $found[$name] = Get-GpuStageRetirementPin -Path $itemPath
    }
    foreach ($name in $script:GpuRetirementNames) { if (-not $found.ContainsKey($name)) { throw "The live stage is missing $name." } }
    return $found
}

function Assert-GpuStageRetirementLivePins {
    param([hashtable]$LivePins, [object[]]$ExpectedPins)
    foreach ($expected in $ExpectedPins) {
        if (-not $LivePins.ContainsKey([string]$expected.Name)) { throw "Live stage is missing $($expected.Name)." }
        $actual = $LivePins[[string]$expected.Name]
        if ($actual.Bytes -ne [long]$expected.Bytes -or
            -not [string]::Equals($actual.Sha256, [string]$expected.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Live stage file differs from reviewed pin: $($expected.Name)"
        }
    }
}

function Assert-GpuStageRetirementBootSelection {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Baseline,
        [Parameter(Mandatory = $true)][string]$IntegrityOutput,
        [Parameter(Mandatory = $true)][string]$ExpectedBootManagerSha256,
        [Parameter(Mandatory = $true)][string]$ExpectedReferenceLogSha256,
        [Parameter(Mandatory = $true)][bool]$CommandSucceeded
    )

    if (-not $CommandSucceeded) { throw 'ibaudit failed to read the current firmware boot selection.' }
    foreach ($field in @('schema','kind','findings','boot_next','boot_order','bootmgfw_sha256','log_sha256')) {
        if ($null -eq $Baseline -or $null -eq $Baseline.PSObject.Properties[$field]) {
            throw "ibaudit capture is missing required boot-selection field: $field."
        }
    }
    if ($Baseline.schema -ne 1 -or [string]$Baseline.kind -cne 'boot-selection' -or
        ($Baseline.findings -isnot [int] -and $Baseline.findings -isnot [long] -and
         $Baseline.findings -isnot [uint16] -and $Baseline.findings -isnot [uint32]) -or
        $Baseline.findings -ne 0) {
        throw 'ibaudit returned a missing, unsupported, or failed boot-selection capture.'
    }
    if ($null -ne $Baseline.boot_next) { throw 'BootNext is set; refusing stage retirement.' }
    if (-not [string]::Equals([string]$Baseline.bootmgfw_sha256, $ExpectedBootManagerSha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ibaudit observed a different Microsoft boot-manager hash.'
    }
    if (-not [string]::Equals([string]$Baseline.log_sha256, $ExpectedReferenceLogSha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ibaudit observed a different reference TPM log hash.'
    }
    $order = @($Baseline.boot_order)
    if ($order.Count -lt 1 -or $order.Count -gt 128) { throw 'BootOrder is empty or exceeds the 128-entry retirement bound.' }
    foreach ($entry in $order) {
        if ($entry -isnot [int] -and $entry -isnot [long] -and $entry -isnot [uint16] -and $entry -isnot [uint32]) {
            throw 'BootOrder contains a non-numeric entry.'
        }
        if ([long]$entry -lt 0 -or [long]$entry -gt 65535) { throw 'BootOrder contains an out-of-range entry.' }
    }
    $integrityLine = ([string]$IntegrityOutput).Trim()
    $integrityMatch = [regex]::Match($integrityLine, '^BootmgfwSha256=([0-9A-Fa-f]{64}) Findings=0x([0-9A-Fa-f]{8})$')
    if (-not $integrityMatch.Success -or
        -not [string]::Equals($integrityMatch.Groups[1].Value, $ExpectedBootManagerSha256, [StringComparison]::OrdinalIgnoreCase) -or
        $integrityMatch.Groups[2].Value -cne '00000000') {
        throw 'bootmgr-integrity did not confirm the expected Microsoft boot manager with zero findings.'
    }
    return [pscustomobject]@{
        Schema = 1
        BootNext = $null
        BootOrder = [uint16[]]@($order | ForEach-Object { [uint16]$_ })
        BootManagerSha256 = ([string]$Baseline.bootmgfw_sha256).ToUpperInvariant()
        ReferenceLogSha256 = ([string]$Baseline.log_sha256).ToUpperInvariant()
        IntegrityFindings = '00000000'
    }
}

function Assert-GpuStageRetirementBootSelectionStable {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Before,
        [Parameter(Mandatory = $true)][object]$Current,
        [Parameter(Mandatory = $true)][string]$Checkpoint
    )
    if ($null -ne $Current.BootNext -or $null -ne $Before.BootNext -or
        @($Before.BootOrder).Count -ne @($Current.BootOrder).Count) {
        throw "Raw BootNext/BootOrder changed or became invalid $Checkpoint."
    }
    for ($i=0; $i -lt @($Before.BootOrder).Count; $i++) {
        if ([int]$Before.BootOrder[$i] -ne [int]$Current.BootOrder[$i]) {
            throw "Raw BootOrder changed $Checkpoint."
        }
    }
    if ($Before.BootManagerSha256 -cne $Current.BootManagerSha256 -or
        $Before.ReferenceLogSha256 -cne $Current.ReferenceLogSha256 -or
        $Before.IntegrityFindings -cne $Current.IntegrityFindings) {
        throw "Raw boot-selection baseline changed $Checkpoint."
    }
}

function Assert-GpuStageRetirement201140BootSelection {
    param([Parameter(Mandatory = $true)][object]$Capture, [Parameter(Mandatory = $true)][string]$Checkpoint)

    if ($null -eq $Capture -or $null -eq $Capture.Baseline) { throw "$Checkpoint raw firmware capture is missing." }
    $baseline = $Capture.Baseline
    $order = @($baseline.BootOrder)
    if ($null -ne $baseline.BootNext -or $order.Count -ne 6 -or
        [string]$baseline.BootManagerSha256 -cne $script:GpuRetirement201140BootManagerSha256 -or
        [string]$baseline.ReferenceLogSha256 -cne $script:GpuRetirement201140ReferenceLogSha256 -or
        [string]$baseline.IntegrityFindings -cne '00000000') {
        throw "$Checkpoint raw boot-selection differs from the closed 201140 reviewed state."
    }
    for ($index = 0; $index -lt 6; $index++) {
        if ([int]$order[$index] -ne $index) { throw "$Checkpoint raw BootOrder differs from the reviewed sequence 0 through 5." }
    }
}

function Assert-GpuStageRetirement224018BootSelection {
    param([Parameter(Mandatory = $true)][object]$Capture, [Parameter(Mandatory = $true)][string]$Checkpoint)

    if ($null -eq $Capture -or $null -eq $Capture.Baseline) { throw "$Checkpoint raw firmware capture is missing." }
    $baseline = $Capture.Baseline
    $order = @($baseline.BootOrder)
    if ($null -ne $baseline.BootNext -or $order.Count -ne 3 -or
        [string]$baseline.BootManagerSha256 -cne $script:GpuRetirement224018BootManagerSha256 -or
        [string]$baseline.ReferenceLogSha256 -cne $script:GpuRetirement224018ReferenceLogSha256 -or
        [string]$baseline.IntegrityFindings -cne '00000000') {
        throw "$Checkpoint raw boot-selection differs from the closed 224018 reviewed state."
    }
    for ($index = 0; $index -lt 3; $index++) {
        if ([int]$order[$index] -ne $index) { throw "$Checkpoint raw BootOrder differs from the reviewed sequence 0 through 2." }
    }
}

function Assert-GpuStageRetirement042142BootSelection {
    param([Parameter(Mandatory = $true)][object]$Capture, [Parameter(Mandatory = $true)][string]$Checkpoint)

    if ($null -eq $Capture -or $null -eq $Capture.Baseline) { throw "$Checkpoint raw firmware capture is missing." }
    $baseline = $Capture.Baseline
    $order = @($baseline.BootOrder)
    if ($baseline.Schema -ne 1 -or $null -ne $baseline.BootNext -or $order.Count -ne 6 -or
        [string]$baseline.BootManagerSha256 -cne $script:GpuRetirement042142BootManagerSha256 -or
        [string]$baseline.ReferenceLogSha256 -cne $script:GpuRetirement042142ReferenceLogSha256 -or
        [string]$baseline.IntegrityFindings -cne '00000000') {
        throw "$Checkpoint raw boot-selection differs from the closed 042142 reviewed state."
    }
    for ($index = 0; $index -lt 6; $index++) {
        if ([int]$order[$index] -ne $index) { throw "$Checkpoint raw BootOrder differs from the reviewed sequence 0 through 5." }
    }
}

function Copy-GpuStageRetirementFile {
    param([string]$Source, [string]$Destination, [object]$ExpectedPin)

    $before = Get-GpuStageRetirementPin -Path $Source
    if ($before.Bytes -ne [long]$ExpectedPin.Bytes -or $before.Sha256 -cne [string]$ExpectedPin.Sha256) {
        throw "Source changed before evidence copy: $Source"
    }
    if ([IO.File]::Exists($Destination) -or [IO.Directory]::Exists($Destination)) { throw "Evidence copy destination already exists: $Destination" }
    $input = New-Object IO.FileStream($Source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $output = $null
    try {
        if ($input.Length -gt 33554432) { throw "Stage file exceeds the 32 MiB copy limit: $Source" }
        $output = New-Object IO.FileStream($Destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $input.CopyTo($output)
        $output.Flush($true)
    }
    finally {
        if ($null -ne $output) { $output.Dispose() }
        $input.Dispose()
    }
    $copyPin = Get-GpuStageRetirementPin -Path $Destination
    $after = Get-GpuStageRetirementPin -Path $Source
    foreach ($pin in @($copyPin, $after)) {
        if ($pin.Bytes -ne [long]$ExpectedPin.Bytes -or $pin.Sha256 -cne [string]$ExpectedPin.Sha256) {
            throw "Stage file or evidence copy changed during copy: $Source"
        }
    }
    return [pscustomobject]@{ Name=[IO.Path]::GetFileName($Source); Source=$after; Copy=$copyPin }
}

function Get-GpuStageRetirementFirmwareText {
    param([Parameter(Mandatory = $true)][object]$Observation)
    if ($null -ne $Observation.PSObject.Properties['Text']) { return [string]$Observation.Text }
    return [string]$Observation
}

function Write-GpuStageRetirementJson {
    param([string]$Path, [object]$Value)
    $json = ConvertTo-Json -InputObject $Value -Depth 32 -Compress
    $bytes = (New-Object Text.UTF8Encoding($false)).GetBytes($json + "`n")
    $stream = New-Object IO.FileStream($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    return Get-GpuStageRetirementPin -Path $Path
}

function Invoke-GpuStageRetirementBootSelectionCapture {
    param(
        [object]$State,
        [string]$ActionDirectory,
        [string]$Checkpoint,
        [string]$AuditToolPath,
        [object]$PlatformModule,
        [scriptblock]$NativeCommand,
        [string]$ExpectedToolHash,
        [long]$ExpectedToolBytes,
        [string]$ExpectedLogHash,
        [long]$ExpectedLogBytes
    )

    $auditToolPath = [IO.Path]::GetFullPath($AuditToolPath)
    $auditToolPin = Get-GpuStageRetirementPin -Path $auditToolPath
    if ($auditToolPin.Sha256 -cne $ExpectedToolHash -or $auditToolPin.Bytes -ne $ExpectedToolBytes) { throw 'Pinned ibaudit.exe differs from the immutable reviewed build artifact.' }
    $referenceLog = $State.Backup.ReferenceLog
    $referencePin = Get-GpuStageRetirementPin -Path ([string]$referenceLog.Path)
    if ($referencePin.Sha256 -cne $ExpectedLogHash -or $referencePin.Bytes -ne $ExpectedLogBytes -or
        $referencePin.Sha256 -cne [string]$referenceLog.Sha256 -or $referencePin.Bytes -ne [long]$referenceLog.Bytes) { throw 'Pinned archived TPM reference log differs from the immutable reviewed file.' }
    $baselinePath = Join-Path $ActionDirectory ($Checkpoint + '-boot-selection.json')
    if ($null -ne $NativeCommand) {
        $baselineResult = & $NativeCommand $auditToolPath @('boot-selection','--log',[string]$referenceLog.Path,'--output',$baselinePath,'--esp-root','S:\','--expected-hash',[string]$State.Backup.BootManagerSha256) $ActionDirectory ($Checkpoint + '-boot-selection')
    }
    else {
        $baselineResult = & $PlatformModule { param($p,$log,$out,$hash,$dir,$label) Invoke-GpuNativeCommand -Program $p -Arguments @('boot-selection','--log',$log,'--output',$out,'--esp-root','S:\','--expected-hash',$hash) -ActionDirectory $dir -Label $label } $auditToolPath ([string]$referenceLog.Path) $baselinePath ([string]$State.Backup.BootManagerSha256) $ActionDirectory ($Checkpoint + '-boot-selection')
    }
    if (-not $baselineResult.Succeeded -or -not [IO.File]::Exists($baselinePath)) {
        throw "ibaudit boot-selection failed at $Checkpoint. Evidence: $($baselineResult.ReceiptPath); stderr: $($baselineResult.StderrPath)."
    }
    try { $baseline = Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "ibaudit boot-selection JSON is invalid: $baselinePath. Evidence: $($baselineResult.ReceiptPath); stderr: $($baselineResult.StderrPath). $($_.Exception.Message)" }
    if ($null -ne $NativeCommand) {
        $integrity = & $NativeCommand $auditToolPath @('bootmgr-integrity','--expected-hash',[string]$State.Backup.BootManagerSha256,'--esp-root','S:\') $ActionDirectory ($Checkpoint + '-bootmgr-integrity')
    }
    else {
        $integrity = & $PlatformModule { param($p,$hash,$dir,$label) Invoke-GpuNativeCommand -Program $p -Arguments @('bootmgr-integrity','--expected-hash',$hash,'--esp-root','S:\') -ActionDirectory $dir -Label $label } $auditToolPath ([string]$State.Backup.BootManagerSha256) $ActionDirectory ($Checkpoint + '-bootmgr-integrity')
    }
    if ($null -eq $integrity -or -not $integrity.Succeeded -or -not [IO.File]::Exists([string]$integrity.StdoutPath) -or
        -not [IO.File]::Exists([string]$integrity.StderrPath)) {
        throw "ibaudit boot-manager integrity failed at $Checkpoint. Evidence: $($integrity.ReceiptPath); stderr: $($integrity.StderrPath)."
    }
    $integrityText = ([IO.File]::ReadAllText($integrity.StdoutPath)).Trim()
    if (-not [string]::IsNullOrWhiteSpace([IO.File]::ReadAllText($integrity.StderrPath))) { throw "ibaudit boot-manager integrity wrote unexpected stderr at $Checkpoint. Evidence: $($integrity.ReceiptPath); stderr: $($integrity.StderrPath)." }
    try {
        $validated = Assert-GpuStageRetirementBootSelection -Baseline $baseline -IntegrityOutput $integrityText -ExpectedBootManagerSha256 ([string]$State.Backup.BootManagerSha256) -ExpectedReferenceLogSha256 ([string]$referenceLog.Sha256) -CommandSucceeded ([bool]$baselineResult.Succeeded)
    }
    catch {
        throw "$($_.Exception.Message) Capture: $baselinePath. Evidence: $($baselineResult.ReceiptPath); stderr: $($baselineResult.StderrPath). Integrity evidence: $($integrity.ReceiptPath); stderr: $($integrity.StderrPath)."
    }
    return [pscustomobject]@{ Baseline=$validated; BaselinePath=$baselinePath; BaselinePin=(Get-GpuStageRetirementPin -Path $baselinePath); IntegrityOutput=$integrityText; IntegrityReceipt=$integrity.ReceiptPath; AuditToolPin=$auditToolPin; ReferenceLogPin=$referencePin }
}

function New-GpuStageRetirementPlatformOperations {
    param(
        [string]$ToolRoot,
        [string]$AuditToolPath,
        [scriptblock]$NativeCommand,
        [string]$ExpectedAuditToolSha256 = $script:GpuRetirementAuditToolSha256,
        [long]$ExpectedAuditToolBytes = $script:GpuRetirementAuditToolBytes,
        [string]$ExpectedReferenceLogSha256 = $script:GpuRetirementReferenceLogSha256,
        [long]$ExpectedReferenceLogBytes = $script:GpuRetirementReferenceLogBytes
    )

    $statePath = Join-Path $ToolRoot 'GpuHostState.psm1'
    $platformPath = Join-Path $ToolRoot 'GpuHostPlatform.psm1'
    $stateModule = Import-Module $statePath -Force -DisableNameChecking -PassThru -ErrorAction Stop
    $platformModule = Import-Module $platformPath -Force -DisableNameChecking -PassThru -ErrorAction Stop
    $retirementModule = $ExecutionContext.SessionState.Module
    return @{
        VerifySource = { param($SourceRoot,$ReviewedCommit)
            $branch = (@(& git -C $SourceRoot branch --show-current 2>&1) -join '').Trim()
            if ($LASTEXITCODE -ne 0 -or $branch -cne 'feat/smbios-boot-serial-probe') { throw 'Run retirement from feat/smbios-boot-serial-probe.' }
            $changes = @(& git -C $SourceRoot status --porcelain --untracked-files=all 2>&1)
            if ($LASTEXITCODE -ne 0 -or $changes.Count -ne 0) { throw 'The feature worktree must be clean before retirement.' }
            # The archived commit binds the original run; the replacement read-only
            # audit tool is admitted by its exact separately reviewed image pin.
            return $true
        }.GetNewClosure()
        AssertAdministrator = { & $platformModule { Assert-GpuAdministrator } }.GetNewClosure()
        GetCurrentSnapshot = { & $platformModule { Get-GpuCurrentMachineSnapshot -IncludeSecureBoot } }.GetNewClosure()
        AssertSameMachine = { param($State,$Current) & $platformModule { param($s,$c) Assert-GpuSameMachine -State $s -Current $c -RequireCpuCount } $State $Current }.GetNewClosure()
        AssertSecureBoot = { param($Current) & $platformModule { param($c) Assert-GpuSecureBootOn -Current $c } $Current }.GetNewClosure()
        AssertBackup = { param($State) & $platformModule { param($s) Assert-GpuBackupState -State $s | Out-Null; Assert-GpuRecoveryLocation -State $s -RequireRunDirectory } $State }.GetNewClosure()
        MountEsp = { param($ActionDirectory) $identity = & $platformModule { param($d) Get-GpuEspMount -ActionDirectory $d } $ActionDirectory; return [pscustomobject]@{ Identity=$identity; Root='S:\'; ActionDirectory=$ActionDirectory; Acquired=$true } }.GetNewClosure()
        DismountEsp = { param($Mount)
            & $platformModule { param($d) Dismount-GpuEspMount -ActionDirectory $d } $Mount.ActionDirectory
            $drive = Get-PSDrive -Name S -ErrorAction SilentlyContinue
            $logicalDrives = [Environment]::GetLogicalDrives()
            if ($null -ne $drive -or (Test-Path -LiteralPath 'S:\') -or @($logicalDrives | Where-Object { $_ -ieq 'S:\' }).Count -gt 0) {
                throw 'The action-owned S: ESP mount remains visible after unmount.'
            }
        }.GetNewClosure()
        AssertMountedEsp = { param($Mount,$Backup) & $platformModule { param($m,$b) Assert-GpuMountedEspMatchesBackup -MountedEsp $m.Identity -Backup $b -Description 'Stage retirement' } $Mount $Backup }.GetNewClosure()
        CaptureFirmware = { param($ActionDirectory) $program = & $platformModule { Get-GpuApplicationPath 'bcdedit.exe' }; $result = & $platformModule { param($p,$d) Invoke-GpuNativeCommand -Program $p -Arguments @('/enum','firmware','/v') -ActionDirectory $d -Label 'stage-retirement-firmware' } $program $ActionDirectory; if (-not $result.Succeeded) { throw "Firmware enumeration failed with exit $($result.ExitCode)." }; & $retirementModule { param($r) [pscustomobject]@{ Text=[IO.File]::ReadAllText($r.StdoutPath); Pin=(Get-GpuStageRetirementPin -Path $r.StdoutPath); ReceiptPath=$r.ReceiptPath } } $result }.GetNewClosure()
        CaptureBootSelection = { param($State,$ActionDirectory,$Checkpoint) & $retirementModule { param($s,$d,$c,$path,$platform,$native,$toolHash,$toolBytes,$logHash,$logBytes) Invoke-GpuStageRetirementBootSelectionCapture -State $s -ActionDirectory $d -Checkpoint $c -AuditToolPath $path -PlatformModule $platform -NativeCommand $native -ExpectedToolHash $toolHash -ExpectedToolBytes $toolBytes -ExpectedLogHash $logHash -ExpectedLogBytes $logBytes } $State $ActionDirectory $Checkpoint $AuditToolPath $platformModule $NativeCommand $ExpectedAuditToolSha256 $ExpectedAuditToolBytes $ExpectedReferenceLogSha256 $ExpectedReferenceLogBytes }.GetNewClosure()
        CaptureHealth = { param($State,$ActionDirectory) $toolPaths = & $platformModule { param($s) Get-GpuExpectedToolPaths -State $s } $State; $probePin = $State.Tools.Probe; if ([IO.Path]::GetFullPath([string]$probePin.Path) -cne [IO.Path]::GetFullPath([string]$toolPaths.Probe)) { throw 'Pinned probe path differs from the archived run path.' }; & $stateModule { param($pin) Assert-GpuFilePin -Pin $pin } $probePin; $result = & $platformModule { param($p,$d) Invoke-GpuNativeCommand -Program $p -Arguments @('health') -ActionDirectory $d -Label 'stage-retirement-absent-health' -AllowedExitCodes @(0) } ([string]$probePin.Path) $ActionDirectory; $lines = @((Get-Content -LiteralPath $result.StdoutPath -ErrorAction Stop) + (Get-Content -LiteralPath $result.StderrPath -ErrorAction Stop)); $verdict = & $stateModule { param($l,$n) Get-GpuHealthVerdict -Lines $l -ExpectedCpus $n -ExpectedResult ABSENT } ([string[]]$lines) ([int]$State.Host.LogicalCpus); return [pscustomobject]@{ CommandSucceeded=[bool]$result.Succeeded; Verdict=$verdict; ReceiptPath=$result.ReceiptPath } }.GetNewClosure()
        AssertPathWithin = { param($Path,$Root,$MustExist) $args = @{Path=$Path;Root=$Root}; if ($MustExist) {$args.MustExist=$true}; & $stateModule { param($a) Assert-GpuPathWithin @a } $args }.GetNewClosure()
        CopyFile = { param($s,$d,$p) & $retirementModule { param($source,$destination,$pin) Copy-GpuStageRetirementFile -Source $source -Destination $destination -ExpectedPin $pin } $s $d $p }.GetNewClosure()
    }
}

function Invoke-GpuStageRetirement {
    [CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
    param(
        [Parameter(Mandatory = $true)][string]$ToolRoot,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [switch]$Apply,
        [hashtable]$Operations,
        [string]$ExpectedAuditSha256 = $script:GpuRetirementAuditSha256,
        [long]$ExpectedAuditBytes = $script:GpuRetirementAuditBytes,
        [string]$ExpectedRunId = $script:GpuRetirementRunId,
        [string]$ExpectedJournalSha256 = $script:GpuRetirementJournalSha256,
        [long]$ExpectedJournalBytes = $script:GpuRetirementJournalBytes,
        [string]$ExpectedArchiveSha256 = $script:GpuRetirementArchiveSha256,
        [long]$ExpectedArchiveBytes = $script:GpuRetirementArchiveBytes,
        [string]$ExpectedProbeSha256 = $script:GpuRetirementProbeSha256,
        [long]$ExpectedProbeBytes = $script:GpuRetirementProbeBytes,
        [string]$ExpectedAuditToolSha256 = $script:GpuRetirementAuditToolSha256,
        [long]$ExpectedAuditToolBytes = $script:GpuRetirementAuditToolBytes,
        [string]$ExpectedAuditToolRelativePath = $script:GpuRetirementAuditToolRelativePath,
        [string]$ExpectedReferenceLogSha256 = $script:GpuRetirementReferenceLogSha256,
        [long]$ExpectedReferenceLogBytes = $script:GpuRetirementReferenceLogBytes,
        [string]$ExpectedReviewedFirmwareRelativePath = $script:GpuRetirementReviewedFirmwareRelativePath,
        [string]$ExpectedReviewedFirmwareSha256 = $script:GpuRetirementReviewedFirmwareSha256,
        [long]$ExpectedReviewedFirmwareBytes = $script:GpuRetirementReviewedFirmwareBytes
    )

    $ErrorActionPreference = 'Stop'
    $ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
    $WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
    $SourceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
    $hasMockOperations = $null -ne $Operations
    if (-not $hasMockOperations) {
        if ($ExpectedAuditSha256 -cne $script:GpuRetirementAuditSha256 -or $ExpectedAuditBytes -ne $script:GpuRetirementAuditBytes -or
            $ExpectedRunId -cne $script:GpuRetirementRunId -or $ExpectedJournalSha256 -cne $script:GpuRetirementJournalSha256 -or
            $ExpectedJournalBytes -ne $script:GpuRetirementJournalBytes -or $ExpectedArchiveSha256 -cne $script:GpuRetirementArchiveSha256 -or
            $ExpectedArchiveBytes -ne $script:GpuRetirementArchiveBytes -or $ExpectedProbeSha256 -cne $script:GpuRetirementProbeSha256 -or
            $ExpectedProbeBytes -ne $script:GpuRetirementProbeBytes -or $ExpectedAuditToolSha256 -cne $script:GpuRetirementAuditToolSha256 -or
            $ExpectedAuditToolBytes -ne $script:GpuRetirementAuditToolBytes -or $ExpectedAuditToolRelativePath -cne $script:GpuRetirementAuditToolRelativePath -or $ExpectedReferenceLogSha256 -cne $script:GpuRetirementReferenceLogSha256 -or
            $ExpectedReferenceLogBytes -ne $script:GpuRetirementReferenceLogBytes -or
            $ExpectedReviewedFirmwareRelativePath -cne $script:GpuRetirementReviewedFirmwareRelativePath -or
            $ExpectedReviewedFirmwareSha256 -cne $script:GpuRetirementReviewedFirmwareSha256 -or
            $ExpectedReviewedFirmwareBytes -ne $script:GpuRetirementReviewedFirmwareBytes) {
            throw 'Production retirement evidence pins are immutable; test-only overrides require an explicit mock operations table.'
        }
        $Operations = New-GpuStageRetirementPlatformOperations -ToolRoot $ToolRoot -AuditToolPath (Join-Path $WorkspaceRoot $ExpectedAuditToolRelativePath)
    }

    $report = [ordered]@{
        SchemaVersion = 1; Status = 'STAGE_RETIREMENT_FAILED'; Succeeded = $false
        RunId = $ExpectedRunId; ApplyRequested = [bool]$Apply; ApplyApproved = $false
        RemovedFiles = @(); Copies = @(); Errors = @(); CleanupErrors = @()
        LocalReceiptDirectory = $null; ExternalReceiptDirectory = $null
        OriginalEvidencePins = $null; RetirementAuditTool = $null; ReviewedStagePins = $null; ProfileEvidencePins = @(); Esp = $null
        FirmwareBaselineEqual = $false; FirmwareBaselineSource = $null
        OriginalFirmwareBaselinePin = $null; OriginalFirmwareBaselineEqual = $false; ReviewedFirmwareBaselinePin = $null
        Health = $null; NextAction = 'blocked; preserve the stage and all evidence'
        FirmwareObservations = @(); BootSelectionCaptures = @(); HealthReceipt = $null
        EspUnmounted = $false; EspFilesChanged = $false; HostBootStateChanged = $false
    }
    $lock = $null
    $mount = $null
    $localReceipt = $null
    $externalReceipt = $null
    $receiptId = [guid]::NewGuid().ToString('N')
    $stateRoot = Join-Path $WorkspaceRoot '.analysis\gpu-baremetal'
    $runDirectory = Join-Path (Join-Path $stateRoot 'runs') $ExpectedRunId
    $journalPath = Join-Path $runDirectory 'journal.json'
    $archivePath = Join-Path (Join-Path $stateRoot 'archive') ($ExpectedRunId + '.json')
    $auditPath = Join-Path $WorkspaceRoot $script:GpuRetirementAuditRelativePath
    $selectorPath = Join-Path $stateRoot 'host-prepared.json'
    $localRoot = Join-Path $WorkspaceRoot '.analysis\gpu-stage-retirement'
    $localDirectory = Join-Path $localRoot $receiptId
    $stageDirectory = $null
    $sourcePins = @()
    $auditPin = $null; $journalPin = $null; $archivePin = $null
    $journal = $null; $audit = $null; $archive = $null
    $expectedStage = @(); $profileEvidencePins = @(); $mountedIdentity = $null
    $beforeFirmware = $null

    try {
        & $Operations['VerifySource'] $SourceRoot $script:GpuRetirementSourceCommit | Out-Null

        $sourceFiles = @(
            (Join-Path $ToolRoot 'Retire-GpuCompletedStage.ps1'),
            (Join-Path $ToolRoot 'GpuStageRetirement.psm1'),
            (Join-Path $ToolRoot 'GpuHostState.psm1'),
            (Join-Path $ToolRoot 'GpuHostPlatform.psm1'),
            (Join-Path $ToolRoot 'Get-GpuBaremetalReadiness.ps1')
        )
        foreach ($sourceFile in $sourceFiles) { & $Operations['AssertPathWithin'] $sourceFile $SourceRoot $true | Out-Null }
        foreach ($sourceFile in $sourceFiles) { $sourcePins += Get-GpuStageRetirementPin -Path $sourceFile }
        & $Operations['AssertPathWithin'] $auditPath $WorkspaceRoot $true | Out-Null
        & $Operations['AssertPathWithin'] $journalPath $stateRoot $true | Out-Null
        & $Operations['AssertPathWithin'] $archivePath (Join-Path $stateRoot 'archive') $true | Out-Null
        $auditPin = Get-GpuStageRetirementPin -Path $auditPath
        if ($auditPin.Sha256 -cne $ExpectedAuditSha256 -or $auditPin.Bytes -ne $ExpectedAuditBytes) { throw 'The reviewed readiness audit hash or length differs from the fixed approved evidence.' }
        $audit = Get-Content -LiteralPath $auditPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $journalPin = Get-GpuStageRetirementPin -Path $journalPath
        if ($journalPin.Sha256 -cne $ExpectedJournalSha256 -or $journalPin.Bytes -ne $ExpectedJournalBytes) { throw 'The archived journal hash or byte length differs from the fixed reviewed run.' }
        $journal = Get-Content -LiteralPath $journalPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $archivePin = Get-GpuStageRetirementPin -Path $archivePath
        if ($archivePin.Sha256 -cne $ExpectedArchiveSha256 -or $archivePin.Bytes -ne $ExpectedArchiveBytes) { throw 'The archived selector hash or byte length differs from the fixed reviewed run.' }
        $archive = Get-Content -LiteralPath $archivePath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $expectedStage = @(Assert-GpuStageRetirementEvidence -Audit $audit -Journal $journal -ArchivedSelector $archive `
            -ExpectedAuditSha256 $ExpectedAuditSha256 -ExpectedRunId $ExpectedRunId `
            -ExpectedRunDirectory $runDirectory -ExpectedJournalPath $journalPath -ExpectedSourceRoot $SourceRoot `
            -ExpectedWorkspaceRoot $WorkspaceRoot)
        $expectedStageWord = if ($expectedStage.Count -eq 5) { 'five' } else { 'seven' }
        if ($script:GpuRetirement201140ProfileSelected) {
            $profileEvidencePins = @(Assert-GpuStageRetirement201140Proof -Journal $journal -WorkspaceRoot $WorkspaceRoot)
        } elseif ($script:GpuRetirement224018ProfileSelected) {
            $profileEvidencePins = @(Assert-GpuStageRetirement224018Proof -Journal $journal -WorkspaceRoot $WorkspaceRoot)
        } elseif ($script:GpuRetirement023306ProfileSelected) {
            $profileEvidencePins = @(Assert-GpuStageRetirement023306Proof -Journal $journal -WorkspaceRoot $WorkspaceRoot)
        } elseif ($script:GpuRetirement032117ProfileSelected) {
            $profileEvidencePins = @(Assert-GpuStageRetirement032117Proof -Journal $journal -WorkspaceRoot $WorkspaceRoot)
        } elseif ($script:GpuRetirement042142ProfileSelected) {
            $profileEvidencePins = @(Assert-GpuStageRetirement042142Proof -Journal $journal -WorkspaceRoot $WorkspaceRoot)
        } elseif ($script:GpuRetirement062106ProfileSelected) {
            $profileEvidencePins = @(Assert-GpuStageRetirement062106Proof -Journal $journal -WorkspaceRoot $WorkspaceRoot)
        } elseif ($script:GpuRetirement160027ProfileSelected) {
            $profileEvidencePins = @(Assert-GpuStageRetirement160027Proof -Journal $journal -WorkspaceRoot $WorkspaceRoot)
        }
        $probePath = [IO.Path]::GetFullPath((Join-Path ([string]$journal.Paths.LoaderBuild) 'ib-tcg-dump.exe'))
        if ([IO.Path]::GetFullPath([string]$journal.Tools.Probe.Path) -cne $probePath -or
            [string]$journal.Tools.Probe.Sha256 -cne $ExpectedProbeSha256 -or [long]$journal.Tools.Probe.Bytes -ne $ExpectedProbeBytes) {
            throw 'The archived health probe differs from the fixed reviewed build artifact.'
        }
        $auditToolPath = [IO.Path]::GetFullPath((Join-Path $WorkspaceRoot $ExpectedAuditToolRelativePath))
        & $Operations['AssertPathWithin'] $auditToolPath $WorkspaceRoot $true | Out-Null
        $auditToolPin = Get-GpuStageRetirementPin -Path $auditToolPath
        if ($auditToolPin.Sha256 -cne $ExpectedAuditToolSha256 -or $auditToolPin.Bytes -ne $ExpectedAuditToolBytes) {
            throw 'The read-only retirement ibaudit.exe differs from the fixed reviewed build artifact.'
        }
        $referenceLogPin = Get-GpuStageRetirementPin -Path ([string]$journal.Backup.ReferenceLog.Path)
        if ($referenceLogPin.Sha256 -cne $ExpectedReferenceLogSha256 -or $referenceLogPin.Bytes -ne $ExpectedReferenceLogBytes -or
            [string]$journal.Backup.ReferenceLog.Sha256 -cne $ExpectedReferenceLogSha256 -or [long]$journal.Backup.ReferenceLog.Bytes -ne $ExpectedReferenceLogBytes) {
            throw 'The archived TPM reference log differs from the fixed reviewed evidence.'
        }
        $report.OriginalEvidencePins = [pscustomobject]@{ Audit=$auditPin; Journal=$journalPin; ArchivedSelector=$archivePin; Source=$sourcePins; ReferenceLog=$referenceLogPin; Probe=$journal.Tools.Probe; ProfileEvidence=$profileEvidencePins }
        $report.ProfileEvidencePins = $profileEvidencePins
        $report.RetirementAuditTool = $auditToolPin
        $report.ReviewedStagePins = $expectedStage

        & $Operations['AssertAdministrator']
        $current = & $Operations['GetCurrentSnapshot']
        & $Operations['AssertSameMachine'] $journal $current
        & $Operations['AssertSecureBoot'] $current
        if ([string]::Equals([string]$current.BootId, [string]$journal.DisarmedBootId, [StringComparison]::Ordinal)) {
            throw 'A full host reset after disarm has not been observed.'
        }
        $recovery = & $Operations['AssertBackup'] $journal
        if ($null -eq $recovery -or $null -eq $recovery.RunDirectory) { throw 'The external backup evidence could not be confirmed.' }
        if ([IO.Path]::GetFullPath([string]$recovery.RunDirectory) -cne [IO.Path]::GetFullPath([string]$journal.RecoveryRunDirectory)) {
            throw 'Validated recovery directory differs from the archived run.'
        }

        $stateDirectory = Join-Path $WorkspaceRoot '.analysis\gpu-baremetal'
        if (-not [IO.Directory]::Exists($stateDirectory)) { throw 'The existing host state directory is missing.' }
        & $Operations['AssertPathWithin'] $stateDirectory $WorkspaceRoot $true | Out-Null
        $lockPath = Join-Path $stateDirectory 'host-step.lock'
        & $Operations['AssertPathWithin'] $lockPath $stateDirectory $false | Out-Null
        if (Test-Path -LiteralPath $lockPath -PathType Container) { throw 'Host-step lock path is a directory.' }
        $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        if (Test-Path -LiteralPath $selectorPath) { throw 'An active host selector exists; retirement requires the archived run only.' }

        & $Operations['AssertPathWithin'] $localRoot $WorkspaceRoot $false | Out-Null
        if (-not [IO.Directory]::Exists($localRoot)) { [void][IO.Directory]::CreateDirectory($localRoot) }
        & $Operations['AssertPathWithin'] $localDirectory $localRoot $false | Out-Null
        if ([IO.Directory]::Exists($localDirectory) -or [IO.File]::Exists($localDirectory)) { throw 'The unique local receipt directory unexpectedly already exists.' }
        [void][IO.Directory]::CreateDirectory($localDirectory)
        $report.LocalReceiptDirectory = $localDirectory
        $actions = Join-Path $localDirectory 'observations'
        [void][IO.Directory]::CreateDirectory($actions)
        $mount = & $Operations['MountEsp'] $actions
        if ($null -eq $mount -or $mount.Acquired -ne $true) { throw 'The system ESP was not acquired by this retirement action.' }
        $mountedIdentity = $mount.Identity
        $stageDirectory = Join-Path ([string]$mount.Root) 'EFI\insecure-boot'
        & $Operations['AssertPathWithin'] $stageDirectory ([string]$mount.Root) $true | Out-Null
        $backup = $journal.Backup
        & $Operations['AssertMountedEsp'] $mount $backup | Out-Null
        Assert-GpuRetirementEqual -Expected ([string]$audit.Esp.Identity.Volume) -Actual ([string]$mountedIdentity.Volume) -Description 'Mounted ESP volume'
        Assert-GpuRetirementEqual -Expected ([string]$audit.Esp.Identity.DiskUniqueId) -Actual ([string]$mountedIdentity.DiskUniqueId) -Description 'Mounted ESP physical disk identity'
        if ([int]$audit.Esp.Identity.DiskNumber -ne [int]$mountedIdentity.DiskNumber -or
            -not [string]::Equals([string]$audit.Esp.Identity.PartitionGuid, [string]$mountedIdentity.PartitionGuid, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Mounted ESP disk number or partition GUID differs from the reviewed audit.'
        }
        $live = Get-GpuStageRetirementLiveInventory -StageDirectory $stageDirectory
        Assert-GpuStageRetirementLivePins -LivePins $live -ExpectedPins $expectedStage

        $firmwareBaseline = Get-GpuStageRetirementPin -Path ([string]$backup.FirmwareEnumeration.Path)
        if ($firmwareBaseline.Sha256 -cne [string]$backup.FirmwareEnumeration.Sha256 -or $firmwareBaseline.Bytes -ne [long]$backup.FirmwareEnumeration.Bytes) {
            throw 'Saved firmware enumeration baseline pin is invalid.'
        }
        $archivedBaselineText = [IO.File]::ReadAllText([string]$backup.FirmwareEnumeration.Path)
        $beforeFirmware = & $Operations['CaptureFirmware'] $actions
        $beforeFirmwareText = Get-GpuStageRetirementFirmwareText -Observation $beforeFirmware
        $report.FirmwareObservations += [pscustomobject]@{ Checkpoint='initial'; Text=$beforeFirmwareText; Pin=$beforeFirmware.Pin; ReceiptPath=$beforeFirmware.ReceiptPath }
        $report.OriginalFirmwareBaselinePin = $firmwareBaseline
        $report.OriginalFirmwareBaselineEqual = [string]::Equals([string]$beforeFirmwareText, [string]$archivedBaselineText, [StringComparison]::Ordinal)
        if ($script:GpuRetirement201140ProfileSelected -or $script:GpuRetirement224018ProfileSelected -or
            $script:GpuRetirement042142ProfileSelected -or $script:GpuRetirement062106ProfileSelected -or
            $script:GpuRetirement160027ProfileSelected) {
            $reviewedFirmwarePath = Join-Path $WorkspaceRoot $ExpectedReviewedFirmwareRelativePath
            try {
                & $Operations['AssertPathWithin'] $reviewedFirmwarePath $WorkspaceRoot $true | Out-Null
                $reviewedFirmwarePin = Get-GpuStageRetirementPin -Path $reviewedFirmwarePath
            }
            catch {
                throw "Current firmware text differs from the fixed $($script:GpuRetirementProfile) final-review capture or it could not be verified: $($_.Exception.Message)"
            }
            if ($reviewedFirmwarePin.Sha256 -cne $ExpectedReviewedFirmwareSha256 -or
                $reviewedFirmwarePin.Bytes -ne $ExpectedReviewedFirmwareBytes) {
                throw "The fixed $($script:GpuRetirementProfile) final-review firmware capture has changed."
            }
            $reviewedFirmwareText = [IO.File]::ReadAllText($reviewedFirmwarePath)
            if (-not [string]::Equals([string]$beforeFirmwareText, [string]$reviewedFirmwareText, [StringComparison]::Ordinal)) {
                throw "Current firmware enumeration differs from the successful $($script:GpuRetirementProfile) final-review capture."
            }
            $baselineText = $reviewedFirmwareText
            $report.FirmwareBaselineSource = 'REVIEWED_' + $script:GpuRetirementProfile + '_FINAL_CAPTURE'
            $report.ReviewedFirmwareBaselinePin = $reviewedFirmwarePin
        }
        elseif ($script:GpuRetirement023306ProfileSelected -or $script:GpuRetirement032117ProfileSelected) {
            if (-not $report.OriginalFirmwareBaselineEqual) {
                throw "Current firmware enumeration differs from the pinned $($script:GpuRetirementProfile) original backup baseline; no reviewed-capture override is permitted."
            }
            $baselineText = $archivedBaselineText
            $report.FirmwareBaselineSource = 'ARCHIVED_BACKUP'
        }
        elseif ($report.OriginalFirmwareBaselineEqual) {
            $baselineText = $archivedBaselineText
            $report.FirmwareBaselineSource = 'ARCHIVED_BACKUP'
        }
        else {
            $reviewedFirmwarePath = Join-Path $WorkspaceRoot $ExpectedReviewedFirmwareRelativePath
            try {
                & $Operations['AssertPathWithin'] $reviewedFirmwarePath $WorkspaceRoot $true | Out-Null
                $reviewedFirmwarePin = Get-GpuStageRetirementPin -Path $reviewedFirmwarePath
            }
            catch {
                throw "Current firmware text differs from the archived baseline and the pinned reviewed later capture could not be verified: $($_.Exception.Message)"
            }
            if ($reviewedFirmwarePin.Sha256 -cne $ExpectedReviewedFirmwareSha256 -or
                $reviewedFirmwarePin.Bytes -ne $ExpectedReviewedFirmwareBytes) {
                throw 'Current firmware text differs from the archived baseline and the pinned reviewed later capture has changed.'
            }
            $reviewedFirmwareText = [IO.File]::ReadAllText($reviewedFirmwarePath)
            if (-not [string]::Equals([string]$beforeFirmwareText, [string]$reviewedFirmwareText, [StringComparison]::Ordinal)) {
                throw 'Current firmware text differs from the archived baseline and the exact pinned reviewed later capture text does not match.'
            }
            $baselineText = $reviewedFirmwareText
            $report.FirmwareBaselineSource = 'REVIEWED_LATER_CAPTURE'
            $report.ReviewedFirmwareBaselinePin = $reviewedFirmwarePin
        }
        $liveHealth = & $Operations['CaptureHealth'] $journal $actions
        if ($liveHealth.CommandSucceeded -ne $true -or $null -eq $liveHealth.Verdict -or
            $liveHealth.Verdict.Passed -ne $true -or [string]$liveHealth.Verdict.ExpectedResult -cne 'ABSENT' -or
            [string]$liveHealth.Verdict.Aggregate -cne 'ABSENT' -or
            [int]$liveHealth.Verdict.CpuCount -ne [int]$current.LogicalCpus -or
            [int]$liveHealth.Verdict.ExpectedCpus -ne [int]$current.LogicalCpus) {
            throw 'Current pinned probe did not confirm ABSENT on every expected CPU.'
        }
        $report.Health = $liveHealth.Verdict
        $report.HealthReceipt = $liveHealth.ReceiptPath
        $bootSelectionBefore = & $Operations['CaptureBootSelection'] $journal $actions 'before-removal-checkpoint'
        if ($script:GpuRetirement201140ProfileSelected) {
            Assert-GpuStageRetirement201140BootSelection -Capture $bootSelectionBefore -Checkpoint 'Initial retirement checkpoint'
        } elseif ($script:GpuRetirement224018ProfileSelected) {
            Assert-GpuStageRetirement224018BootSelection -Capture $bootSelectionBefore -Checkpoint 'Initial retirement checkpoint'
        } elseif ($script:GpuRetirement042142ProfileSelected) {
            Assert-GpuStageRetirement042142BootSelection -Capture $bootSelectionBefore -Checkpoint 'Initial retirement checkpoint'
        } elseif ($script:GpuRetirement062106ProfileSelected) {
            Assert-GpuStageRetirement062106BootSelection -Capture $bootSelectionBefore -Checkpoint 'Initial retirement checkpoint'
        } elseif ($script:GpuRetirement160027ProfileSelected) {
            Assert-GpuStageRetirement160027BootSelection -Capture $bootSelectionBefore -Checkpoint 'Initial retirement checkpoint'
        }
        $report.BootSelectionBefore = $bootSelectionBefore.Baseline
        $report.BootSelectionCaptures += $bootSelectionBefore
        $report.FirmwareBaselineEqual = $true
        $report.Esp = [pscustomobject]@{ Identity=$mountedIdentity; BootManagerSha256=[string]$backup.BootManagerSha256 }

        $recoveryStageRoot = Join-Path ([string]$journal.RecoveryRunDirectory) 'stage-retirement'
        & $Operations['AssertPathWithin'] $recoveryStageRoot ([string]$journal.RecoveryRunDirectory) $false | Out-Null
        if (-not [IO.Directory]::Exists($recoveryStageRoot)) { [void][IO.Directory]::CreateDirectory($recoveryStageRoot) }
        $externalDirectory = Join-Path $recoveryStageRoot $receiptId
        & $Operations['AssertPathWithin'] $externalDirectory $recoveryStageRoot $false | Out-Null
        if ([IO.Directory]::Exists($externalDirectory) -or [IO.File]::Exists($externalDirectory)) { throw 'The unique external receipt directory unexpectedly already exists.' }
        [void][IO.Directory]::CreateDirectory($externalDirectory)
        & $Operations['AssertPathWithin'] $externalDirectory $recoveryStageRoot $true | Out-Null
        $report.ExternalReceiptDirectory = $externalDirectory
        $externalReceipt = $externalDirectory

        # Review mode also makes independently verified local and external copies.
        # Only explicit -Apply followed by ShouldProcess can remove the exact pinned files.
        $localCopies = Join-Path $localDirectory 'copies'
        $externalCopies = Join-Path $externalDirectory 'copies'
        & $Operations['AssertPathWithin'] $localCopies $localDirectory $false | Out-Null
        & $Operations['AssertPathWithin'] $externalCopies $externalDirectory $false | Out-Null
        [void][IO.Directory]::CreateDirectory($localCopies)
        [void][IO.Directory]::CreateDirectory($externalCopies)
        foreach ($expected in $expectedStage) {
            $source = Join-Path $stageDirectory $expected.Name
            $localCopyPath = Join-Path $localCopies $expected.Name
            $externalCopyPath = Join-Path $externalCopies $expected.Name
            & $Operations['AssertPathWithin'] $localCopyPath $localCopies $false | Out-Null
            & $Operations['AssertPathWithin'] $externalCopyPath $externalCopies $false | Out-Null
            $copyOperation = $Operations['CopyFile']
            $localCopy = & $copyOperation $source $localCopyPath $expected
            $externalCopy = & $copyOperation $source $externalCopyPath $expected
            $report.Copies += [pscustomobject]@{ Name=$expected.Name; Source=$localCopy.Source; Local=$localCopy.Copy; External=$externalCopy.Copy }
        }
        $intentKind = if ($Apply) { 'GPU_STAGE_RETIREMENT_INTENT' } else { 'GPU_STAGE_RETIREMENT_REVIEW' }
        $intent = [pscustomobject]@{
            SchemaVersion=1; Kind=$intentKind; RunId=$ExpectedRunId; ReceiptId=$receiptId
            AuditPin=$auditPin; JournalPin=$journalPin; ArchivedSelectorPin=$archivePin; SourcePins=$sourcePins
            EspIdentity=$mountedIdentity; BootManagerSha256=[string]$backup.BootManagerSha256
            FirmwareBaseline=$firmwareBaseline; FirmwareBaselineSource=$report.FirmwareBaselineSource
            OriginalFirmwareBaselineEqual=$report.OriginalFirmwareBaselineEqual
            ReviewedFirmwareBaselinePin=$report.ReviewedFirmwareBaselinePin
            StagePins=$expectedStage; Copies=$report.Copies
            Health=$liveHealth.Verdict; CreatedUtc=[DateTime]::UtcNow.ToString('o')
        }
        $recordLeaf = if ($Apply) { 'intent.json' } else { 'review.json' }
        $localIntentPath = Join-Path $localDirectory $recordLeaf
        $externalIntentPath = Join-Path $externalDirectory $recordLeaf
        $localIntentPin = Write-GpuStageRetirementJson -Path $localIntentPath -Value $intent
        $externalIntentPin = Write-GpuStageRetirementJson -Path $externalIntentPath -Value $intent
        if ($localIntentPin.Sha256 -cne $externalIntentPin.Sha256 -or $localIntentPin.Bytes -ne $externalIntentPin.Bytes) {
            throw 'Local and external retirement review/intent records differ.'
        }
        $report.ReviewOrIntentPins = [pscustomobject]@{ Local=$localIntentPin; External=$externalIntentPin }

        if (-not $Apply) {
            $report.Status = 'REVIEWED_STAGE_READY_FOR_RETIREMENT'
            $report.Succeeded = $true
            $report.NextAction = "review both verified-copy receipts, then rerun with -Apply to retire only the $expectedStageWord pinned files"
        }
        elseif (-not $PSCmdlet.ShouldProcess('S:\EFI\insecure-boot', "Retire the exact $expectedStageWord reviewed files from the completed archived stage")) {
            $report.Status = 'REVIEWED_STAGE_READY_FOR_RETIREMENT'
            $report.Succeeded = $true
            $report.NextAction = 'review the saved evidence, then rerun this command with -Apply'
        }
        else {
            $report.ApplyApproved = $true
            # Re-pin copied evidence and review/intent records immediately before removal.
            foreach ($pin in @($auditPin,$journalPin,$archivePin,$auditToolPin,$referenceLogPin) + @($sourcePins) + @($profileEvidencePins)) { Assert-GpuStageRetirementPin -Pin $pin | Out-Null }
            $probeCurrent = Get-GpuStageRetirementPin -Path ([string]$journal.Tools.Probe.Path)
            if ($probeCurrent.Sha256 -cne $ExpectedProbeSha256 -or $probeCurrent.Bytes -ne $ExpectedProbeBytes) { throw 'Pinned health probe changed before deletion.' }
            foreach ($copySet in $report.Copies) {
                foreach ($copyPin in @($copySet.Local,$copySet.External)) {
                    $verifiedCopy = Assert-GpuStageRetirementPin -Pin $copyPin
                    if ($verifiedCopy.Sha256 -cne [string]$copySet.Source.Sha256 -or $verifiedCopy.Bytes -ne [long]$copySet.Source.Bytes) {
                        throw "A verified retirement evidence copy changed before deletion: $($copySet.Name)"
                    }
                }
            }
            $localIntentCurrent = Assert-GpuStageRetirementPin -Pin $localIntentPin
            $externalIntentCurrent = Assert-GpuStageRetirementPin -Pin $externalIntentPin
            if ($localIntentCurrent.Sha256 -cne $externalIntentCurrent.Sha256 -or $localIntentCurrent.Bytes -ne $externalIntentCurrent.Bytes) {
                throw 'Retirement review/intent records changed before deletion.'
            }
            if (Test-Path -LiteralPath $selectorPath) { throw 'An active host selector appeared before removal.' }
            $recovery = & $Operations['AssertBackup'] $journal
            if ([IO.Path]::GetFullPath([string]$recovery.RunDirectory) -cne [IO.Path]::GetFullPath([string]$journal.RecoveryRunDirectory)) { throw 'Recovery evidence location changed before removal.' }
            & $Operations['AssertMountedEsp'] $mount $backup | Out-Null
            $live = Get-GpuStageRetirementLiveInventory -StageDirectory $stageDirectory
            Assert-GpuStageRetirementLivePins -LivePins $live -ExpectedPins $expectedStage
            $immediateFirmware = & $Operations['CaptureFirmware'] $actions
            $immediateFirmwareText = Get-GpuStageRetirementFirmwareText -Observation $immediateFirmware
            $report.FirmwareObservations += [pscustomobject]@{ Checkpoint='immediate-predelete'; Text=$immediateFirmwareText; Pin=$immediateFirmware.Pin; ReceiptPath=$immediateFirmware.ReceiptPath }
            if (-not [string]::Equals([string]$immediateFirmwareText, [string]$baselineText, [StringComparison]::Ordinal)) { throw 'Firmware enumeration changed immediately before stage removal.' }
            $bootSelectionPreDelete = & $Operations['CaptureBootSelection'] $journal $actions 'immediate-predelete'
            Assert-GpuStageRetirementBootSelectionStable -Before $bootSelectionBefore.Baseline -Current $bootSelectionPreDelete.Baseline -Checkpoint 'immediately before deletion'
            $report.BootSelectionPreDelete = $bootSelectionPreDelete.Baseline
            $report.BootSelectionCaptures += $bootSelectionPreDelete

            $report.Status = 'STAGE_RETIREMENT_FAILED'
            $report.Succeeded = $false
            foreach ($expected in $expectedStage) {
                $target = Join-Path $stageDirectory $expected.Name
                & $Operations['AssertPathWithin'] $target $stageDirectory $true | Out-Null
                $actual = Get-GpuStageRetirementPin -Path $target
                if ($actual.Bytes -ne [long]$expected.Bytes -or $actual.Sha256 -cne [string]$expected.Sha256) {
                    throw "Per-file pin changed before deletion: $($expected.Name)"
                }
                try {
                    if ($Operations.ContainsKey('DeleteFile')) { & $Operations['DeleteFile'] $target } else { [IO.File]::Delete($target) }
                }
                catch {
                    if (-not [IO.File]::Exists($target) -and $report.RemovedFiles -cnotcontains $expected.Name) {
                        $report.RemovedFiles += $expected.Name
                        $report.EspFilesChanged = $true
                    }
                    throw
                }
                if ([IO.File]::Exists($target)) { throw "Exact stage file remains after delete: $($expected.Name)" }
                $report.RemovedFiles += $expected.Name
                $report.EspFilesChanged = $true
            }
            if (@([IO.Directory]::EnumerateFileSystemEntries($stageDirectory)).Count -ne 0) { throw "The reviewed stage directory is not empty after the $expectedStageWord file removals." }
            & $Operations['AssertPathWithin'] $stageDirectory ([string]$mount.Root) $true | Out-Null
            [IO.Directory]::Delete($stageDirectory, $false)
            if ([IO.Directory]::Exists($stageDirectory)) { throw 'The exact stage directory remains after nonrecursive removal.' }
            $postFirmware = & $Operations['CaptureFirmware'] $actions
            $postFirmwareText = Get-GpuStageRetirementFirmwareText -Observation $postFirmware
            $report.FirmwareObservations += [pscustomobject]@{ Checkpoint='after-removal'; Text=$postFirmwareText; Pin=$postFirmware.Pin; ReceiptPath=$postFirmware.ReceiptPath }
            if (-not [string]::Equals([string]$postFirmwareText, [string]$baselineText, [StringComparison]::Ordinal)) { throw 'Firmware enumeration changed after stage removal.' }
            $bootSelectionAfter = & $Operations['CaptureBootSelection'] $journal $actions 'after-removal'
            Assert-GpuStageRetirementBootSelectionStable -Before $bootSelectionBefore.Baseline -Current $bootSelectionAfter.Baseline -Checkpoint 'after deletion'
            $report.BootSelectionAfter = $bootSelectionAfter.Baseline
            $report.BootSelectionCaptures += $bootSelectionAfter
            $bootPin = Get-GpuStageRetirementPin -Path (Join-Path ([string]$mount.Root) 'EFI\Microsoft\Boot\bootmgfw.efi')
            if ($bootPin.Sha256 -cne [string]$backup.BootManagerSha256) { throw 'Microsoft boot manager changed during stage retirement.' }
            $report.Status = 'STAGE_RETIRED'
            $report.Succeeded = $true
            $report.NextAction = 'rerun Get-GpuBaremetalReadiness.ps1 and preserve both retirement receipts'
        }
    }
    catch {
        $report.Status = 'STAGE_RETIREMENT_FAILED'
        $report.Succeeded = $false
        $report.Errors += $_.Exception.Message
        $report.NextAction = 'blocked; preserve the stage and all evidence; do not retry Apply after a partial removal'
    }
    finally {
        if ($null -ne $mount -and $mount.Acquired -eq $true) {
            try { & $Operations['DismountEsp'] $mount }
            catch {
                $report.CleanupErrors += $_.Exception.Message
                $report.Status = 'STAGE_RETIREMENT_FAILED'
                $report.Succeeded = $false
                $report.NextAction = 'blocked; the ESP unmount failed; inspect evidence before further action'
            }
            if ($report.CleanupErrors.Count -eq 0) { $report.EspUnmounted = $true }
        }
        if ($null -ne $lock) { $lock.Dispose(); $lock = $null }
    }

    if ($null -ne $report.LocalReceiptDirectory) {
        try {
            $result = [pscustomobject]$report
            $localPin = Write-GpuStageRetirementJson -Path (Join-Path $localDirectory 'result.json') -Value $result
            if ($null -ne $report.ExternalReceiptDirectory) {
                $externalPin = Write-GpuStageRetirementJson -Path (Join-Path $externalReceipt 'result.json') -Value $result
                if ($localPin.Sha256 -cne $externalPin.Sha256) { throw 'Local and external result receipts differ.' }
            }
            $report.LocalResultPin = $localPin
        }
        catch {
            $report.Status = 'STAGE_RETIREMENT_FAILED'
            $report.Succeeded = $false
            $report.Errors += ('Result receipt write failed: ' + $_.Exception.Message)
            $report.NextAction = 'blocked; preserve all current evidence and inspect receipt-write failure'
        }
    }
    return [pscustomobject]$report
}

Export-ModuleMember -Function @('Assert-GpuStageRetirementEvidence','Get-GpuStageRetirementLiveInventory','Assert-GpuStageRetirementLivePins','Invoke-GpuStageRetirement')
