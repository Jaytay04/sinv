Set-StrictMode -Version 2.0

$script:GpuFinalReviewModulePath = [IO.Path]::GetFullPath($PSCommandPath)
$script:GpuFinalReviewRunId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
$script:GpuFinalReviewBoundRunId = $null
$script:GpuFinalReviewReviewDirectoryName = '8abb7b84ca644a7c84019a1f29b35bb4'
$script:GpuFinalReviewBackupActionName = '20261020T201533358Z-Backup-479329a8e70a4124964be17cc597ad61'
$script:GpuFinalReviewOriginalFinalActionName = '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'
$script:GpuFinalReviewBaselineLeaf = 'firmware-enumeration-backup-4196e13c22604293a54688532c15cff6.stdout.txt'
$script:GpuFinalReviewOriginalCurrentLeaf = 'firmware-enumeration-final-7a5347fd0bc4452eb06340ba1e614a92.stdout.txt'
$script:GpuFinalReviewFirmwareObservationLeaves = @('stage-retirement-firmware-3ba00749b6d94f82a0ef932ab7fb214d.stdout.txt', 'stage-retirement-firmware-1f63298289ce49ed81012994adc954ba.stdout.txt')
$script:GpuFinalReviewRawObservationLeaves = @('before-boot-selection.json', 'after-boot-selection.json')
$script:GpuFinalReviewProbeBuildId = '8be3274dfe56cec7'
$script:GpuFinalReviewSourceCommit = '933b4c676318f26396cdf2c66de01c3ccbf1a950'
$script:GpuFinalReviewDisarmedBootId = '2026-10-20T20:19:07.5000000Z'
$script:GpuFinalReviewCurrentBootId = '2026-10-20T20:48:51.5000000Z'
$script:GpuFinalReviewJournalSha256 = '631E2811091F4085445170A500985A30A296762EA15EEC0FD70C70422C55101D'
$script:GpuFinalReviewJournalBytes = 45269L
$script:GpuFinalReviewSelectorSha256 = 'A7416148825A6ABC3603681C45F6A8277AFEE432D4B503BDEC89D34F58D34FFB'
$script:GpuFinalReviewSelectorBytes = 533L
$script:GpuFinalReviewBaselineSha256 = 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE'
$script:GpuFinalReviewCurrentFirmwareSha256 = '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'
$script:GpuFinalReviewBaselineFirmwareBytes = 4336L
$script:GpuFinalReviewCurrentFirmwareBytes = 4336L
$script:GpuFinalReviewIbauditSha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
$script:GpuFinalReviewIbauditBytes = 246784L
$script:GpuFinalReviewReferenceLogSha256 = '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'
$script:GpuFinalReviewReferenceLogBytes = 40838L
$script:GpuFinalReviewProbeSha256 = 'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47'
$script:GpuFinalReviewProbeBytes = 172544L
$script:GpuFinalReviewAuditResultSha256 = 'BEA6DCA1FC904B13830908471D6239A906F71AA6937630B32CBA6DC3C2C023D9'
$script:GpuFinalReviewAuditResultBytes = 16924L
$script:GpuFinalReviewRawSha256 = 'B5D4EB51A58A2170EBCA905749E2BDAD177CFD71629A1079A14934F3F5BF63D0'
$script:GpuFinalReviewRawBytes = 259L
$script:GpuFinalReviewBootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
$script:GpuFinalReviewInspectorSha256 = '9EFE53FCC2CEA79BF0F2DF7E8E550AE122D381259AB7C176D7FA64A76A7A6275'
$script:GpuFinalReviewInspectorBytes = 27895L
$script:GpuFinalReviewStateSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
$script:GpuFinalReviewStateBytes = 19843L
$script:GpuFinalReviewPlatformHistoricalSha256 = '4816FC0E640D7AEB38DF301E4D53A4982D5CABAA780650FEA8BDEE5D7C416899'
$script:GpuFinalReviewPlatformHistoricalBytes = 107733L
$script:GpuFinalReviewPlatformCurrentSha256 = $null
$script:GpuFinalReviewPlatformCurrentBytes = 0L
$script:GpuFinalReviewRetirementSha256 = 'CCFA78B48A18AB90AE865ADEB14CA952D927955523B9E72F0C30A762F4FDB553'
$script:GpuFinalReviewRetirementBytes = 61875L
$script:GpuFinalReviewFinalResultSha256 = $null
$script:GpuFinalReviewFinalResultBytes = 0L
$script:GpuFinalReviewHealthStdoutSha256 = $null
$script:GpuFinalReviewHealthStdoutBytes = 0L
$script:GpuFinalReviewHealthStderrSha256 = $null
$script:GpuFinalReviewHealthStderrBytes = 0L

function Select-GpuFinalFirmwareReviewProfile {
    param([Parameter(Mandatory = $true)][string]$RunId)

    if ($null -ne $script:GpuFinalReviewBoundRunId -and
        -not [string]::Equals([string]$script:GpuFinalReviewBoundRunId, $RunId, [StringComparison]::Ordinal)) {
        throw 'A final firmware review module instance cannot switch between fixed run profiles; start a fresh PowerShell process.'
    }
    if ([string]::Equals($RunId, 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7', [StringComparison]::Ordinal)) {
        $script:GpuFinalReviewBoundRunId = $RunId
        return
    }
    if ([string]::Equals($RunId, 'gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff', [StringComparison]::Ordinal)) {
        $script:GpuFinalReviewRunId = 'gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff'
        $script:GpuFinalReviewReviewDirectoryName = '2933898a57284570b022635a3398a3a4'
        $script:GpuFinalReviewBackupActionName = '20261021T160724349Z-Backup-f7bd830785b14473beca1c18fc068a62'
        $script:GpuFinalReviewOriginalFinalActionName = '20261021T165416133Z-FinalVerify-33a9f87568214ffbb7342a18fcfec7cc'
        $script:GpuFinalReviewBaselineLeaf = 'firmware-enumeration-backup-2010399215fd4ea7885594dcce487c3c.stdout.txt'
        $script:GpuFinalReviewOriginalCurrentLeaf = 'firmware-enumeration-final-a390b4a605494d33b673ea17303c26d5.stdout.txt'
        $script:GpuFinalReviewFirmwareObservationLeaves = @('stage-retirement-firmware-4630ba0a24be4418a7dc29f22b30c904.stdout.txt', 'stage-retirement-firmware-dd0653ac165c4eeea0cbe791fc0f5d7b.stdout.txt')
        $script:GpuFinalReviewRawObservationLeaves = @('before-boot-selection.json', 'after-boot-selection.json')
        $script:GpuFinalReviewProbeBuildId = '990e6d2305c55fff'
        $script:GpuFinalReviewSourceCommit = '1be532253d6c6cc428ae2d4588a4c6e7aacadb3e'
        $script:GpuFinalReviewDisarmedBootId = '2026-10-21T16:14:38.5000000Z'
        $script:GpuFinalReviewCurrentBootId = '2026-10-21T16:52:47.5000000Z'
        $script:GpuFinalReviewJournalSha256 = 'A33ABD21F2D2BA369D4160644BC6305A51E747A04CE4A0DDA1200C80AF90EB22'
        $script:GpuFinalReviewJournalBytes = 44133L
        $script:GpuFinalReviewSelectorSha256 = '4A0E31FA7F4773F7191A70EA151BB41FCA8363042E8F289620156337F1ABD930'
        $script:GpuFinalReviewSelectorBytes = 533L
        $script:GpuFinalReviewBaselineSha256 = 'BC104DF2EBB63636540220FC2E085980F81AEA595B1EC77FE288116A245FA661'
        $script:GpuFinalReviewCurrentFirmwareSha256 = '98E5ECE5B987B754CD1C059C34E9D87A1CFB569E758BCC21971CA21D4EDC2D80'
        $script:GpuFinalReviewBaselineFirmwareBytes = 4336L
        $script:GpuFinalReviewCurrentFirmwareBytes = 4336L
        $script:GpuFinalReviewIbauditSha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
        $script:GpuFinalReviewIbauditBytes = 246784L
        $script:GpuFinalReviewReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
        $script:GpuFinalReviewReferenceLogBytes = 40838L
        $script:GpuFinalReviewProbeSha256 = '56B204225F39A93F324AE340B985268130405845B4745723CA8DE0A8B544DC4A'
        $script:GpuFinalReviewProbeBytes = 172544L
        $script:GpuFinalReviewAuditResultSha256 = 'EAA79DCFA55C09CEAD703A07E8A4C02CAF2A8C6992EEF006B4891BB8ADF2CBF6'
        $script:GpuFinalReviewAuditResultBytes = 17633L
        $script:GpuFinalReviewRawSha256 = '69733AFE9DD782577F302F79A788DF2745BB9C9CD4BE860DF44C59A07EB4E066'
        $script:GpuFinalReviewRawBytes = 259L
        $script:GpuFinalReviewBootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
        $script:GpuFinalReviewInspectorSha256 = '5D30A706D26775A7007C430EF25C1F22BDA32D521735B2780CD2233170015846'
        $script:GpuFinalReviewInspectorBytes = 43642L
        $script:GpuFinalReviewStateSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
        $script:GpuFinalReviewStateBytes = 19843L
        $script:GpuFinalReviewPlatformHistoricalSha256 = 'C1DF37B21E0AB32C0E56D00D577B15463AC3705F4B08DE8E1D5EF85561BDD34E'
        $script:GpuFinalReviewPlatformHistoricalBytes = 111822L
        $script:GpuFinalReviewPlatformCurrentSha256 = '43AA4F580F6D896068D8D4DE4139A509CC19254647D400DA5803115E5957D7FE'
        $script:GpuFinalReviewPlatformCurrentBytes = 111899L
        $script:GpuFinalReviewRetirementSha256 = '78C8CE570A1DB3135BC39E3F9961D182AD9D9F346D73787414AB89639E76ADCF'
        $script:GpuFinalReviewRetirementBytes = 308879L
        $script:GpuFinalReviewFinalResultSha256 = '8D29D4BB908AC72C9ADB3ADC3DE9C5AC58B5ABC99AEF1073FA25AE91A7695C31'
        $script:GpuFinalReviewFinalResultBytes = 2285L
        $script:GpuFinalReviewHealthStdoutSha256 = 'D7CD426CF81128F690B80686D1C8FCF3FC03FB4BFADD4A8A612359E769153EA9'
        $script:GpuFinalReviewHealthStdoutBytes = 1698L
        $script:GpuFinalReviewHealthStderrSha256 = 'E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855'
        $script:GpuFinalReviewHealthStderrBytes = 0L
        $script:GpuFinalReviewExpectedBootOrder = @(0, 1, 2, 3, 4, 5)
        $script:GpuFinalReviewProfileId = '160027'
        $script:GpuFinalReviewBoundRunId = $RunId
        return
    }
    if ([string]::Equals($RunId, 'gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942', [StringComparison]::Ordinal)) {
        $script:GpuFinalReviewRunId = 'gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942'
        $script:GpuFinalReviewReviewDirectoryName = 'faf2a0ad848b4ae38afe03ca419c978f'
        $script:GpuFinalReviewBackupActionName = '20261021T062522426Z-Backup-4d82912e15464713b6f63f1ace6ad3fb'
        $script:GpuFinalReviewOriginalFinalActionName = '20261021T065114533Z-FinalVerify-6c332fa152e44a55925c801705f052cd'
        $script:GpuFinalReviewBaselineLeaf = 'firmware-enumeration-backup-1648399929574818b6bfb740f9e80877.stdout.txt'
        $script:GpuFinalReviewOriginalCurrentLeaf = 'firmware-enumeration-final-cb55a93e13d648ae97e1f62bebc01a8b.stdout.txt'
        $script:GpuFinalReviewFirmwareObservationLeaves = @('stage-retirement-firmware-7f37a824cd3a406e8ee62936cfbe2808.stdout.txt', 'stage-retirement-firmware-9dc3367d199e4f228f0995ed188f1578.stdout.txt')
        $script:GpuFinalReviewRawObservationLeaves = @('before-boot-selection.json', 'after-boot-selection.json')
        $script:GpuFinalReviewProbeBuildId = 'b3b06badc7018942'
        $script:GpuFinalReviewSourceCommit = 'c44bb52dcb38179f5d026b7331b848c114ed649e'
        $script:GpuFinalReviewDisarmedBootId = '2026-10-21T06:36:00.5000000Z'
        $script:GpuFinalReviewCurrentBootId = '2026-10-21T06:49:32.5000000Z'
        $script:GpuFinalReviewJournalSha256 = '26B4C9D657AE65DFD007073C4E5F99A1E620720774BEFDDBB398DE7E3F8E9A80'
        $script:GpuFinalReviewJournalBytes = 44133L
        $script:GpuFinalReviewSelectorSha256 = '4510690FAD83991612344C958E26C334F19850F71D54B6D51669F7EF94AAB484'
        $script:GpuFinalReviewSelectorBytes = 533L
        $script:GpuFinalReviewBaselineSha256 = '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'
        $script:GpuFinalReviewCurrentFirmwareSha256 = 'BC104DF2EBB63636540220FC2E085980F81AEA595B1EC77FE288116A245FA661'
        $script:GpuFinalReviewBaselineFirmwareBytes = 4336L
        $script:GpuFinalReviewCurrentFirmwareBytes = 4336L
        $script:GpuFinalReviewIbauditSha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
        $script:GpuFinalReviewIbauditBytes = 246784L
        $script:GpuFinalReviewReferenceLogSha256 = 'F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8'
        $script:GpuFinalReviewReferenceLogBytes = 40838L
        $script:GpuFinalReviewProbeSha256 = 'E7FEA1D8436416B0ADC392F03A22A4B510D4CE5DF3308B49DD65D9D6E2432E1A'
        $script:GpuFinalReviewProbeBytes = 172544L
        $script:GpuFinalReviewAuditResultSha256 = '881BBD51DDE22261EA59E69CF2DD3E143C0952F5DA06A2F88AA6307F051EC399'
        $script:GpuFinalReviewAuditResultBytes = 17633L
        $script:GpuFinalReviewRawSha256 = '485A498C520A7A32E8C0E06C20D599F060F7178C4A3630E02D755599A92163DD'
        $script:GpuFinalReviewRawBytes = 259L
        $script:GpuFinalReviewBootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
        $script:GpuFinalReviewInspectorSha256 = '082F77481AC494E81B9A74BD5BE4596F0439FFF27E3FD0D23F9FEE597B4C95B2'
        $script:GpuFinalReviewInspectorBytes = 40444L
        $script:GpuFinalReviewStateSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
        $script:GpuFinalReviewStateBytes = 19843L
        $script:GpuFinalReviewPlatformHistoricalSha256 = '6EEC7FAB2A3DD095215B85781A3273A87B026ED8E3DF647225C40225E332336E'
        $script:GpuFinalReviewPlatformHistoricalBytes = 111703L
        $script:GpuFinalReviewPlatformCurrentSha256 = '719E71388A10C72504C6DF305FB393FF1434B029E74665FA511E52A8D781E527'
        $script:GpuFinalReviewPlatformCurrentBytes = 111780L
        $script:GpuFinalReviewRetirementSha256 = 'D40F76D333DCAB0F3475D02CB12343F38629E0D12F12303A6FC5C85B14A931FD'
        $script:GpuFinalReviewRetirementBytes = 246612L
        $script:GpuFinalReviewFinalResultSha256 = 'C426FEEF8E70136E527AAE40B233CAC9281681F7C3A5113DBA062DDD4F49CC49'
        $script:GpuFinalReviewFinalResultBytes = 2285L
        $script:GpuFinalReviewHealthStdoutSha256 = 'D7CD426CF81128F690B80686D1C8FCF3FC03FB4BFADD4A8A612359E769153EA9'
        $script:GpuFinalReviewHealthStdoutBytes = 1698L
        $script:GpuFinalReviewHealthStderrSha256 = 'E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855'
        $script:GpuFinalReviewHealthStderrBytes = 0L
        $script:GpuFinalReviewExpectedBootOrder = @(0, 1, 2, 3, 4, 5)
        $script:GpuFinalReviewProfileId = '062106'
        $script:GpuFinalReviewBoundRunId = $RunId
        return
    }
    if (-not [string]::Equals($RunId, 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1', [StringComparison]::Ordinal) -and
        -not [string]::Equals($RunId, 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2', [StringComparison]::Ordinal)) {
        throw 'The final firmware review supports only four fixed reviewed run profiles; 160027 is separately fixed.'
    }

    $script:GpuFinalReviewRunId = 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1'
    $script:GpuFinalReviewReviewDirectoryName = '1dccd9d33d1b4c808fd1059291a6d19b'
    $script:GpuFinalReviewBackupActionName = '20261020T224257466Z-Backup-8bf99023efa4478e91e8fe626493da70'
    $script:GpuFinalReviewOriginalFinalActionName = '20261020T225933250Z-FinalVerify-cf3ab7b1e838427c964640d87f30257e'
    $script:GpuFinalReviewBaselineLeaf = 'firmware-enumeration-backup-99b1a3488d3d47ba93a25d46ae052d2d.stdout.txt'
    $script:GpuFinalReviewOriginalCurrentLeaf = 'firmware-enumeration-final-bd819c45c1e94e01b00b44dafe51c163.stdout.txt'
    $script:GpuFinalReviewFirmwareObservationLeaves = @('stage-retirement-firmware-669f29ce055a488c8d9dda9d434e8879.stdout.txt', 'stage-retirement-firmware-ce39fde67ee142e3810fbdc2e7c95c40.stdout.txt')
    $script:GpuFinalReviewRawObservationLeaves = @('before-boot-selection.json', 'after-boot-selection.json')
    $script:GpuFinalReviewProbeBuildId = 'a68d89b7b3ecb1a1'
    $script:GpuFinalReviewSourceCommit = '69a3c3d94f33d1903778ef5ec5b0a1f015b1be29'
    $script:GpuFinalReviewDisarmedBootId = '2026-10-20T22:45:43.5000000Z'
    $script:GpuFinalReviewCurrentBootId = '2026-10-20T22:51:15.5000000Z'
    $script:GpuFinalReviewJournalSha256 = 'A1513F12AFCD2932E4DE2D9A6069C7A2CED6F969BF1AC82E6942D6818F319E07'
    $script:GpuFinalReviewJournalBytes = 45568L
    $script:GpuFinalReviewSelectorSha256 = 'A996B969394113B052010129563BF3EAE35443B0B452B0C66D0B7B69FF9D75B3'
    $script:GpuFinalReviewSelectorBytes = 533L
    $script:GpuFinalReviewBaselineSha256 = '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'
    $script:GpuFinalReviewCurrentFirmwareSha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
    $script:GpuFinalReviewBaselineFirmwareBytes = 4336L
    $script:GpuFinalReviewCurrentFirmwareBytes = 2890L
    $script:GpuFinalReviewReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
    $script:GpuFinalReviewProbeSha256 = '76828AFED037973FBB4357AD89C97FB65D85D3BCF7811C9281F199ECB65A4583'
    $script:GpuFinalReviewAuditResultSha256 = '0E7107911CF43F4144F4E67AEBB647623003AF543FA38974EBEAE518F0D1642E'
    $script:GpuFinalReviewAuditResultBytes = 15415L
    $script:GpuFinalReviewRawSha256 = 'A8D2C8A2D2C7FB5EF6F12DA9362B4806A73136F9DD3801511FE2D89FAF6EF7A9'
    $script:GpuFinalReviewRawBytes = 253L
    $script:GpuFinalReviewInspectorSha256 = 'FBA167F5947E6722146272C9B17FE2220FDB6A1297AE764626725E62AF6BB7DD'
    $script:GpuFinalReviewInspectorBytes = 33353L
    $script:GpuFinalReviewPlatformHistoricalSha256 = '4276ED335283E2AD48E5D0426B8D4857063C2B9ADBFE1DC41693A0BE64BB9B51'
    $script:GpuFinalReviewPlatformHistoricalBytes = 111525L
    $script:GpuFinalReviewRetirementSha256 = 'DF7F446426F63E21C5596E9CD1B35ECA0172C182B3E802B6267962B3EF8B1D75'
    $script:GpuFinalReviewRetirementBytes = 105056L
    $script:GpuFinalReviewExpectedBootOrder = @(0, 1, 2)
    $script:GpuFinalReviewProfileId = '224018'

    if ([string]::Equals($RunId, 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2', [StringComparison]::Ordinal)) {
        $script:GpuFinalReviewRunId = 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2'
        $script:GpuFinalReviewReviewDirectoryName = '255f08b53f3a432cbe8cd2b386447781'
        $script:GpuFinalReviewBackupActionName = '20261021T042510110Z-Backup-b0ad2425a4cd436fa583d76122c8afe1'
        $script:GpuFinalReviewOriginalFinalActionName = '20261021T044758857Z-FinalVerify-a88b0158befb4af49d9643cb14da1a1c'
        $script:GpuFinalReviewBaselineLeaf = 'firmware-enumeration-backup-297ad383796447e38ac14cc7e3ba5879.stdout.txt'
        $script:GpuFinalReviewOriginalCurrentLeaf = 'firmware-enumeration-final-0b0629c3908d46e98c61bf9462991cd8.stdout.txt'
        $script:GpuFinalReviewFirmwareObservationLeaves = @('stage-retirement-firmware-6eb1ad5ae3e9488bab017a053b4516e1.stdout.txt', 'stage-retirement-firmware-ca807ac9cc6442d6a6fb8bc464698791.stdout.txt')
        $script:GpuFinalReviewRawObservationLeaves = @('before-boot-selection.json', 'after-boot-selection.json')
        $script:GpuFinalReviewProbeBuildId = '840ce906358bbac2'
        $script:GpuFinalReviewSourceCommit = 'a52aa25e918223abae295876800933df9b642e46'
        $script:GpuFinalReviewDisarmedBootId = '2026-10-21T04:30:43.5000000Z'
        $script:GpuFinalReviewCurrentBootId = '2026-10-21T04:46:50.5000000Z'
        $script:GpuFinalReviewJournalSha256 = '3287B5E86D018DD374748D56E16CF7BBDE0D245ECF426779EAAB80E5A2B08583'
        $script:GpuFinalReviewJournalBytes = 44133L
        $script:GpuFinalReviewSelectorSha256 = 'B4CF6844894A920912EFF85B80012ED3B0B5504D7BD01341A531316C8FC8424C'
        $script:GpuFinalReviewSelectorBytes = 533L
        $script:GpuFinalReviewBaselineSha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
        $script:GpuFinalReviewCurrentFirmwareSha256 = '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'
        $script:GpuFinalReviewBaselineFirmwareBytes = 2890L
        $script:GpuFinalReviewCurrentFirmwareBytes = 4336L
        $script:GpuFinalReviewIbauditSha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
        $script:GpuFinalReviewIbauditBytes = 246784L
        $script:GpuFinalReviewReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
        $script:GpuFinalReviewReferenceLogBytes = 40838L
        $script:GpuFinalReviewProbeSha256 = '86D15C1E8B29A7896FC8D4A9AA14ECD926DA23D8EED2871DBA65DDDA7002BD36'
        $script:GpuFinalReviewAuditResultSha256 = 'AD43DF7601413CDAA13A561D499F6902E47AEC53932872014D55B009EB2EC736'
        $script:GpuFinalReviewAuditResultBytes = 17633L
        $script:GpuFinalReviewRawSha256 = '69733AFE9DD782577F302F79A788DF2745BB9C9CD4BE860DF44C59A07EB4E066'
        $script:GpuFinalReviewRawBytes = 259L
        $script:GpuFinalReviewBootManagerSha256 = '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
        $script:GpuFinalReviewInspectorSha256 = '518701A9CA9059F01B700A084736B59A9F66A382221EBD96BF436C5E67A34242'
        $script:GpuFinalReviewInspectorBytes = 37603L
        $script:GpuFinalReviewStateSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
        $script:GpuFinalReviewStateBytes = 19843L
        $script:GpuFinalReviewPlatformHistoricalSha256 = 'D30A1A38BF5B9CAD07F43C237599847A8D5ECF0825D7D3552A92F599801C543F'
        $script:GpuFinalReviewPlatformHistoricalBytes = 111625L
        $script:GpuFinalReviewPlatformCurrentSha256 = '6EEC7FAB2A3DD095215B85781A3273A87B026ED8E3DF647225C40225E332336E'
        $script:GpuFinalReviewPlatformCurrentBytes = 111703L
        $script:GpuFinalReviewRetirementSha256 = '395101C30399FD96C2404BAA049054A287375060D95B7D9773FE2147647E563D'
        $script:GpuFinalReviewRetirementBytes = 183814L
        $script:GpuFinalReviewFinalResultSha256 = '885BC6B54E13815762C85D9BF8ED60AB4C11762A3E584F143BFAAB52E6C72B69'
        $script:GpuFinalReviewFinalResultBytes = 2285L
        $script:GpuFinalReviewHealthStdoutSha256 = 'D7CD426CF81128F690B80686D1C8FCF3FC03FB4BFADD4A8A612359E769153EA9'
        $script:GpuFinalReviewHealthStdoutBytes = 1698L
        $script:GpuFinalReviewHealthStderrSha256 = 'E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855'
        $script:GpuFinalReviewHealthStderrBytes = 0L
        $script:GpuFinalReviewExpectedBootOrder = @(0, 1, 2, 3, 4, 5)
        $script:GpuFinalReviewProfileId = '042142'
    }
    $script:GpuFinalReviewBoundRunId = $RunId
}

$script:GpuFinalReviewExpectedBootOrder = @(0, 1, 2, 3, 4, 5)
$script:GpuFinalReviewProfileId = '201140'

function Get-GpuFinalReviewProperty {
    param([AllowNull()][object]$Object, [Parameter(Mandatory = $true)][string]$Name)
    if ($null -eq $Object -or $null -eq $Object.PSObject.Properties[$Name]) { return $null }
    return $Object.PSObject.Properties[$Name].Value
}

function Assert-GpuFinalReviewEqual {
    param([string]$Expected, [string]$Actual, [string]$Description)
    if (-not [string]::Equals($Expected, $Actual, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description differs from the fixed reviewed GPU run."
    }
}

function Assert-GpuFinalReviewPinRecord {
    param(
        [Parameter(Mandatory = $true)][object]$Pin,
        [Parameter(Mandatory = $true)][string]$ExpectedPath,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256,
        [Parameter(Mandatory = $true)][long]$ExpectedBytes,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($null -eq $Pin -or $null -eq $Pin.PSObject.Properties['Path'] -or
        $null -eq $Pin.PSObject.Properties['Sha256'] -or $null -eq $Pin.PSObject.Properties['Bytes']) {
        throw "$Description pin is incomplete."
    }
    $pinBytes = 0L
    if (-not [long]::TryParse([string]$Pin.Bytes, [ref]$pinBytes) -or $pinBytes -lt 0) {
        throw "$Description pin has an invalid byte count."
    }
    $expectedCanonical = [IO.Path]::GetFullPath($ExpectedPath)
    $actualCanonical = [IO.Path]::GetFullPath([string]$Pin.Path)
    if (-not [string]::Equals($expectedCanonical, $actualCanonical, [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals([string]$Pin.Sha256, $ExpectedSha256, [StringComparison]::OrdinalIgnoreCase) -or
        $pinBytes -ne $ExpectedBytes) {
        throw "$Description pin differs from its reviewed path, SHA-256, or length."
    }
}

function Assert-GpuFinalReviewPinPair {
    param([object]$Before, [object]$After, [string]$Name, [string]$Path, [string]$Hash, [long]$Bytes)
    Assert-GpuFinalReviewPinRecord -Pin (Get-GpuFinalReviewProperty $Before $Name) -ExpectedPath $Path -ExpectedSha256 $Hash -ExpectedBytes $Bytes -Description "Before $Name"
    Assert-GpuFinalReviewPinRecord -Pin (Get-GpuFinalReviewProperty $After $Name) -ExpectedPath $Path -ExpectedSha256 $Hash -ExpectedBytes $Bytes -Description "After $Name"
}

function Assert-GpuFinalReviewRawBaseline {
    param([Parameter(Mandatory = $true)][object]$Baseline, [Parameter(Mandatory = $true)][string]$Description)
    $expectedOrder = @($script:GpuFinalReviewExpectedBootOrder)
    if ($null -eq $Baseline) { throw "$Description raw boot-selection baseline is missing." }
    $order = @($Baseline.BootOrder)
    if ([int]$Baseline.Schema -ne 1 -or $null -ne $Baseline.BootNext -or
        $order.Count -ne $expectedOrder.Count -or [string]$Baseline.BootManagerSha256 -cne $script:GpuFinalReviewBootManagerSha256 -or
        [string]$Baseline.ReferenceLogSha256 -cne $script:GpuFinalReviewReferenceLogSha256 -or
        [string]$Baseline.IntegrityFindings -cne '00000000') {
        throw "$Description raw boot-selection baseline is incomplete or has findings."
    }
    for ($index = 0; $index -lt $expectedOrder.Count; $index++) {
        if ([int]$order[$index] -ne $expectedOrder[$index]) { throw "$Description BootOrder differs from the reviewed capture." }
    }
}

function Assert-GpuFinalReviewEntryJournal {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object]$State,
        [Parameter(Mandatory = $true)][string]$ActionDirectory
    )

    $expectedPath = [IO.Path]::GetFullPath((Join-Path $ActionDirectory 'reviewed-firmware-entry-journal.json'))
    if (-not [string]::Equals([IO.Path]::GetFullPath($Path), $expectedPath, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.File]::Exists($expectedPath)) {
        throw 'The immutable entry-journal copy is missing from this FinalVerify action.'
    }
    $item = Get-Item -LiteralPath $expectedPath -ErrorAction Stop
    if ($item.PSIsContainer -or [long]$item.Length -ne $script:GpuFinalReviewJournalBytes) {
        throw 'The immutable entry-journal copy has the wrong file type or length.'
    }
    $hash = (Get-FileHash -LiteralPath $expectedPath -Algorithm SHA256 -ErrorAction Stop).Hash
    if ($hash -ine $script:GpuFinalReviewJournalSha256) {
        throw 'The immutable entry-journal copy differs from the reviewed pre-retry journal.'
    }

    $journal = [IO.File]::ReadAllText($expectedPath) | ConvertFrom-Json -ErrorAction Stop
    $runDirectory = [IO.Path]::GetFullPath([string]$State.RunDirectory)
    $oldFinalAction = [IO.Path]::GetFullPath((Join-Path (Join-Path $runDirectory 'actions') $script:GpuFinalReviewOriginalFinalActionName))
    $expectedErrors = @('Firmware enumeration differs from the saved textual baseline.')
    if ($journal.SchemaVersion -ne 1 -or [string]$journal.RunId -cne $script:GpuFinalReviewRunId -or
        [IO.Path]::GetFullPath([string]$journal.WorkspaceRoot) -ine [IO.Path]::GetFullPath([string]$State.WorkspaceRoot) -or
        [IO.Path]::GetFullPath([string]$journal.SourceRoot) -ine [IO.Path]::GetFullPath([string]$State.SourceRoot) -or
        [string]$journal.SourceCommit -cne $script:GpuFinalReviewSourceCommit -or
        [IO.Path]::GetFullPath([string]$journal.RunDirectory) -ine $runDirectory -or
        [string]$journal.Phase -cne 'Disarmed' -or [string]$journal.DisarmedBootId -cne $script:GpuFinalReviewDisarmedBootId -or
        $journal.EverArmIntent -ne $true -or $null -eq $journal.InProgress -or
        [string]$journal.InProgress.Action -cne 'FinalVerify' -or
        [IO.Path]::GetFullPath([string]$journal.InProgress.Directory) -ine $oldFinalAction -or
        [string]$journal.LastFailure -cne 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' -or
        $null -eq $journal.LastAction -or [string]$journal.LastAction.Action -cne 'FinalVerify' -or
        [string]$journal.LastAction.Status -cne 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' -or
        $journal.LastAction.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$journal.LastAction.Directory) -ine $oldFinalAction) {
        throw 'The immutable entry journal does not contain the fixed pre-retry Disarmed context.'
    }
    $final = $journal.FinalVerification
    if ($null -eq $final -or [string]$final.BootId -cne $script:GpuFinalReviewCurrentBootId -or
        $final.SecureBoot -ne $true -or $final.ResetBoundaryPassed -ne $true -or $final.ActivationAttempted -ne $true -or
        $null -eq $final.Health -or $final.Health.Passed -ne $true -or
        [string]$final.Health.ExpectedResult -cne 'ABSENT' -or [string]$final.Health.Aggregate -cne 'ABSENT' -or
        [int]$final.Health.ExpectedCpus -ne 20 -or [int]$final.Health.CpuCount -ne 20 -or
        [int]$final.Health.CpuPassCount -ne 0 -or
        $null -eq $final.FirmwareEnumeration -or $final.FirmwareEnumeration.TextEqual -ne $false -or
        [string]$final.FirmwareEnumeration.BaselineSha256 -cne $script:GpuFinalReviewBaselineSha256 -or
        [string]$final.FirmwareEnumeration.CurrentSha256 -cne $script:GpuFinalReviewCurrentFirmwareSha256 -or
        [string]$final.FirmwareEnumeration.CaptureContext -cne 'SystemEspMountedAtS' -or
        @($final.Errors).Count -ne $expectedErrors.Count -or [string]$final.Errors[0] -cne $expectedErrors[0]) {
        throw 'The immutable entry journal does not contain the reviewed reset, health, and firmware mismatch result.'
    }
    return [pscustomobject]@{ Path=$expectedPath; Sha256=$hash; Bytes=[long]$item.Length; Journal=$journal }
}

function Assert-GpuFinalReviewFailedFinalResult {
    param([Parameter(Mandatory = $true)][string]$Path)
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash
    if ($item.PSIsContainer -or [long]$item.Length -ne $script:GpuFinalReviewFinalResultBytes -or
        $hash -ine $script:GpuFinalReviewFinalResultSha256) {
        throw 'The pinned failed FinalVerify result changed or has the wrong length.'
    }
    $result = [IO.File]::ReadAllText($Path) | ConvertFrom-Json -ErrorAction Stop
    $runDirectory = [IO.Path]::GetFullPath((Join-Path (Join-Path ([string]$result.RunDirectory) 'actions') $script:GpuFinalReviewOriginalFinalActionName))
    if ([string]$result.Action -cne 'FinalVerify' -or
        [string]$result.Status -cne 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' -or $result.Succeeded -ne $false -or
        [string]$result.Phase -cne 'Disarmed' -or
        [string]$result.Details.Status -cne 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' -or $result.Details.Succeeded -ne $false -or
        [IO.Path]::GetFullPath([string]$result.ActionDirectory) -ine $runDirectory -or
        [IO.Path]::GetFullPath([string]$result.RunDirectory) -ine [IO.Path]::GetFullPath((Split-Path (Split-Path (Split-Path $Path -Parent) -Parent) -Parent))) {
        throw 'The pinned FinalVerify result does not identify the reviewed failed action.'
    }
    $final = $result.Details.FinalVerification
    $expectedError = 'Firmware enumeration differs from the saved textual baseline.'
    if ([string]$final.BootId -cne $script:GpuFinalReviewCurrentBootId -or
        $final.SecureBoot -ne $true -or $final.ResetBoundaryPassed -ne $true -or $final.ActivationAttempted -ne $true -or
        $null -eq $final.Health -or $final.Health.Passed -ne $true -or
        [string]$final.Health.ExpectedResult -cne 'ABSENT' -or [string]$final.Health.Aggregate -cne 'ABSENT' -or
        [int]$final.Health.ExpectedCpus -ne 20 -or [int]$final.Health.CpuCount -ne 20 -or
        [int]$final.Health.CpuPassCount -ne 0 -or
        $null -eq $final.FirmwareEnumeration -or $final.FirmwareEnumeration.TextEqual -ne $false -or
        [string]$final.FirmwareEnumeration.BaselineSha256 -cne $script:GpuFinalReviewBaselineSha256 -or
        [string]$final.FirmwareEnumeration.CurrentSha256 -cne $script:GpuFinalReviewCurrentFirmwareSha256 -or
        [string]$final.FirmwareEnumeration.CaptureContext -cne 'SystemEspMountedAtS' -or
        @($final.Errors).Count -ne 1 -or [string]$final.Errors[0] -cne $expectedError) {
        throw 'The pinned failed FinalVerify result does not contain the reviewed firmware-text-only failure.'
    }
}

function Assert-GpuFinalFirmwareReviewReceiptShape {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Receipt,
        [Parameter(Mandatory = $true)][object]$State,
        [Parameter(Mandatory = $true)][object]$CurrentHost,
        [Parameter(Mandatory = $true)][object]$CurrentFirmwarePin,
        [Parameter(Mandatory = $true)][string]$ActionDirectory
    )

    $workspaceRoot = [IO.Path]::GetFullPath([string]$State.WorkspaceRoot)
    $sourceRoot = [IO.Path]::GetFullPath([string]$State.SourceRoot)
    $runDirectory = [IO.Path]::GetFullPath((Join-Path (Join-Path $workspaceRoot '.analysis\gpu-baremetal\runs') $script:GpuFinalReviewRunId))
    $backupAction = Join-Path (Join-Path $runDirectory 'actions') $script:GpuFinalReviewBackupActionName
    $originalFinalAction = Join-Path (Join-Path $runDirectory 'actions') $script:GpuFinalReviewOriginalFinalActionName
    $stateRoot = Join-Path $workspaceRoot '.analysis\gpu-baremetal'
    $reviewDirectory = Join-Path (Join-Path $workspaceRoot '.analysis\gpu-final-firmware-review') $script:GpuFinalReviewReviewDirectoryName
    $receiptPath = Join-Path $reviewDirectory 'result.json'
    $expectedCurrentPath = [IO.Path]::GetFullPath([string]$CurrentFirmwarePin.Path)

    if ($Receipt.SchemaVersion -ne 1 -or [string]$Receipt.Status -cne 'READONLY_FIRMWARE_REVIEW_CAPTURED' -or
        $Receipt.Succeeded -ne $true -or $Receipt.RecoveryCompleted -ne $false -or
        $Receipt.HistoricalRawBootOrderUnchangedProven -ne $false -or
        $Receipt.SelectorAndJournalUnchanged -ne $true -or $Receipt.EspUnmounted -ne $true -or
        $Receipt.CleanupSucceeded -ne $true -or $Receipt.EspFilesChanged -ne $false -or $Receipt.FirmwareWritten -ne $false -or
        @($Receipt.Errors).Count -ne 0 -or @($Receipt.CleanupErrors).Count -ne 0) {
        throw 'The saved firmware review receipt is incomplete, failed, or reports host changes.'
    }
    if ($script:GpuFinalReviewProfileId -in @('224018', '042142', '062106', '160027') -and [string]$Receipt.Profile -cne $script:GpuFinalReviewProfileId) {
        throw "The saved firmware review receipt does not identify the fixed $($script:GpuFinalReviewProfileId) profile."
    }
    if ($script:GpuFinalReviewProfileId -in @('224018', '042142', '062106', '160027') -and [string]$Receipt.RunId -cne $script:GpuFinalReviewRunId) {
        throw "The saved firmware review receipt run ID does not exactly match the fixed $($script:GpuFinalReviewProfileId) run."
    }
    Assert-GpuFinalReviewEqual $reviewDirectory ([string]$Receipt.EvidenceDirectory) 'Saved firmware review evidence directory'
    Assert-GpuFinalReviewEqual $script:GpuFinalReviewRunId ([string]$Receipt.RunId) 'Firmware review run ID'
    Assert-GpuFinalReviewEqual $script:GpuFinalReviewRunId ([string]$State.RunId) 'Current run ID'
    Assert-GpuFinalReviewEqual $script:GpuFinalReviewSourceCommit ([string]$State.SourceCommit) 'Recorded source commit'
    Assert-GpuFinalReviewEqual $sourceRoot ([IO.Path]::GetFullPath((Join-Path $workspaceRoot '.codex-build\worktrees\gpu-windows-validation'))) 'Run source root'
    Assert-GpuFinalReviewEqual $runDirectory ([string]$State.RunDirectory) 'Run directory'
    Assert-GpuFinalReviewEqual $runDirectory ([string]$Receipt.PinsBefore.Journal.Path.Substring(0, [string]$Receipt.PinsBefore.Journal.Path.IndexOf('\journal.json', [StringComparison]::OrdinalIgnoreCase))) 'Receipt journal run directory'
    if ([string]$State.Phase -cne 'Disarmed' -or $State.EverArmIntent -ne $true -or
        [string]$State.DisarmedBootId -cne $script:GpuFinalReviewDisarmedBootId) {
        throw 'The current journal is not the reviewed armed and disarmed run.'
    }
    if ($null -eq $State.InProgress -or [string]$State.InProgress.Action -cne 'FinalVerify' -or
        [IO.Path]::GetFullPath([string]$State.InProgress.Directory) -cne [IO.Path]::GetFullPath($ActionDirectory)) {
        throw 'The caller does not own the current FinalVerify intent.'
    }
    $null = [IO.Path]::GetFullPath($ActionDirectory)
    if (-not [IO.Path]::GetFullPath($ActionDirectory).StartsWith([IO.Path]::GetFullPath((Join-Path $runDirectory 'actions')) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($ActionDirectory) -notmatch '^[0-9]{8}T[0-9]{9}Z-FinalVerify-[0-9a-f]{32}$') {
        throw 'The current FinalVerify action directory is outside the reviewed run.'
    }
    if ([string]$CurrentHost.BootId -cne $script:GpuFinalReviewCurrentBootId -or
        [int]$CurrentHost.LogicalCpus -ne 20 -or $CurrentHost.SecureBoot -ne $true) {
        throw 'Current host boot identity, CPU count, or Secure Boot state differs from the reviewed observation.'
    }
    if ([int]$State.Host.LogicalCpus -ne 20 -or
        [string]$State.Backup.EspVolume -cne '\\?\Volume{187bc913-7576-4e6e-8d54-18eeb5c38fcd}\' -or
        [int]$State.Backup.EspDiskNumber -ne 0 -or
        [string]$State.Backup.EspPartitionGuid -cne '187BC913-7576-4E6E-8D54-18EEB5C38FCD' -or
        [string]$State.Backup.BootManagerSha256 -cne $script:GpuFinalReviewBootManagerSha256) {
        throw 'Recorded host or ESP identity differs from the fixed reviewed run.'
    }
    if ([string]$CurrentFirmwarePin.Sha256 -cne $script:GpuFinalReviewCurrentFirmwareSha256 -or
        [long]$CurrentFirmwarePin.Bytes -ne $script:GpuFinalReviewCurrentFirmwareBytes -or
        -not ([IO.Path]::GetFullPath($expectedCurrentPath).StartsWith([IO.Path]::GetFullPath($ActionDirectory) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))) {
        throw 'Current firmware text pin is outside this FinalVerify action or differs from the reviewed capture.'
    }
    $firmware = Get-GpuFinalReviewProperty (Get-GpuFinalReviewProperty $State 'FinalVerification') 'FirmwareEnumeration'
    if ($null -eq $firmware -or $firmware.TextEqual -ne $false -or
        [string]$firmware.BaselineSha256 -cne $script:GpuFinalReviewBaselineSha256 -or
        [string]$firmware.CurrentSha256 -cne $script:GpuFinalReviewCurrentFirmwareSha256 -or
        [string]$firmware.CaptureContext -cne 'SystemEspMountedAtS') {
        throw 'FinalVerify did not observe the reviewed mounted-ESP firmware-text mismatch.'
    }
    if ($script:GpuFinalReviewProfileId -in @('042142', '062106', '160027')) {
        $verification = $State.FinalVerification
        $expectedError = 'Firmware enumeration differs from the saved textual baseline.'
        if ([string]$verification.BootId -cne $script:GpuFinalReviewCurrentBootId -or
            $verification.SecureBoot -ne $true -or $verification.ResetBoundaryPassed -ne $true -or
            $verification.ActivationAttempted -ne $true -or $null -eq $verification.Health -or
            $verification.Health.Passed -ne $true -or [string]$verification.Health.ExpectedResult -cne 'ABSENT' -or
            [string]$verification.Health.Aggregate -cne 'ABSENT' -or
            [int]$verification.Health.ExpectedCpus -ne 20 -or [int]$verification.Health.CpuCount -ne 20 -or
            [int]$verification.Health.CpuPassCount -ne 0 -or
            $verification.ActiveGpuProof -ne $false -or [string]$verification.GpuAttestation -cne 'UNATTESTED' -or
            @($verification.Errors).Count -ne 1 -or [string]$verification.Errors[0] -cne $expectedError) {
            throw 'The current FinalVerify state contains a failure beyond the reviewed firmware-text mismatch.'
        }
    }

    $selectorPath = Join-Path $stateRoot 'host-prepared.json'
    $journalPath = Join-Path $runDirectory 'journal.json'
    $baselinePath = Join-Path $backupAction $script:GpuFinalReviewBaselineLeaf
    $oldCurrentPath = Join-Path $originalFinalAction $script:GpuFinalReviewOriginalCurrentLeaf
    $finalResultPath = Join-Path $originalFinalAction 'result.json'
    $auditToolPath = Join-Path $workspaceRoot '.codex-build\gpu-stage-retirement-audit\readonly-v1\ibaudit.exe'
    $referenceLogPath = Join-Path $backupAction 'tcglog.ib'
    $probePath = Join-Path $workspaceRoot ('.codex-build\gb\' + $script:GpuFinalReviewProbeBuildId + '\l\ib-tcg-dump.exe')
    $inspectorPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1'
    $stateModulePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1'
    $platformModulePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
    $retirementModulePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuStageRetirement.psm1'
    $beforePins = $Receipt.PinsBefore
    $afterPins = $Receipt.PinsAfter
    if ($null -eq $beforePins -or $null -eq $afterPins) { throw 'Firmware review receipt is missing before/after immutable pins.' }
    Assert-GpuFinalReviewPinPair $beforePins $afterPins 'Selector' $selectorPath $script:GpuFinalReviewSelectorSha256 $script:GpuFinalReviewSelectorBytes
    Assert-GpuFinalReviewPinPair $beforePins $afterPins 'Journal' $journalPath $script:GpuFinalReviewJournalSha256 $script:GpuFinalReviewJournalBytes
    Assert-GpuFinalReviewPinPair $beforePins $afterPins 'FirmwareBaseline' $baselinePath $script:GpuFinalReviewBaselineSha256 $script:GpuFinalReviewBaselineFirmwareBytes
    Assert-GpuFinalReviewPinPair $beforePins $afterPins 'FirmwareCurrent' $oldCurrentPath $script:GpuFinalReviewCurrentFirmwareSha256 $script:GpuFinalReviewCurrentFirmwareBytes
    Assert-GpuFinalReviewPinPair $beforePins $afterPins 'Ibaudit' $auditToolPath $script:GpuFinalReviewIbauditSha256 $script:GpuFinalReviewIbauditBytes
    Assert-GpuFinalReviewPinPair $beforePins $afterPins 'ReferenceLog' $referenceLogPath $script:GpuFinalReviewReferenceLogSha256 $script:GpuFinalReviewReferenceLogBytes
    Assert-GpuFinalReviewPinPair $beforePins $afterPins 'Probe' $probePath $script:GpuFinalReviewProbeSha256 $script:GpuFinalReviewProbeBytes
    Assert-GpuFinalReviewPinPair $beforePins $afterPins 'InspectorScript' $inspectorPath $script:GpuFinalReviewInspectorSha256 $script:GpuFinalReviewInspectorBytes
    if ($null -eq $beforePins.Modules -or $null -eq $afterPins.Modules) { throw 'Firmware review receipt is missing source module pins.' }
    Assert-GpuFinalReviewPinPair $beforePins.Modules $afterPins.Modules 'State' $stateModulePath $script:GpuFinalReviewStateSha256 $script:GpuFinalReviewStateBytes
    Assert-GpuFinalReviewPinPair $beforePins.Modules $afterPins.Modules 'Platform' $platformModulePath $script:GpuFinalReviewPlatformHistoricalSha256 $script:GpuFinalReviewPlatformHistoricalBytes
    Assert-GpuFinalReviewPinPair $beforePins.Modules $afterPins.Modules 'Retirement' $retirementModulePath $script:GpuFinalReviewRetirementSha256 $script:GpuFinalReviewRetirementBytes
    if ($script:GpuFinalReviewProfileId -in @('042142', '062106', '160027')) {
        Assert-GpuFinalReviewPinPair $beforePins $afterPins 'FinalResult' $finalResultPath $script:GpuFinalReviewFinalResultSha256 $script:GpuFinalReviewFinalResultBytes
        Assert-GpuFinalReviewFailedFinalResult -Path $finalResultPath
        $entryJournalPath = Join-Path $ActionDirectory 'reviewed-firmware-entry-journal.json'
        $null = Assert-GpuFinalReviewEntryJournal -Path $entryJournalPath -State $State -ActionDirectory $ActionDirectory
    } else {
        $entryJournalPath = $null
    }
    if ($null -eq $State.Backup.FirmwareEnumeration -or
        [IO.Path]::GetFullPath([string]$State.Backup.FirmwareEnumeration.Path) -ine [IO.Path]::GetFullPath($baselinePath) -or
        [string]$State.Backup.FirmwareEnumeration.Sha256 -ine $script:GpuFinalReviewBaselineSha256 -or
        [long]$State.Backup.FirmwareEnumeration.Bytes -ne $script:GpuFinalReviewBaselineFirmwareBytes -or
        [IO.Path]::GetFullPath([string]$State.Backup.ReferenceLog.Path) -ine [IO.Path]::GetFullPath($referenceLogPath) -or
        [string]$State.Backup.ReferenceLog.Sha256 -ine $script:GpuFinalReviewReferenceLogSha256 -or
        [long]$State.Backup.ReferenceLog.Bytes -ne $script:GpuFinalReviewReferenceLogBytes -or
        [IO.Path]::GetFullPath([string]$State.Tools.Probe.Path) -ine [IO.Path]::GetFullPath($probePath) -or
        [string]$State.Tools.Probe.Sha256 -ine $script:GpuFinalReviewProbeSha256 -or
        [long]$State.Tools.Probe.Bytes -ne $script:GpuFinalReviewProbeBytes) {
        throw 'Recorded baseline, reference log, or health probe differs from the fixed current-run pins.'
    }

    if ($null -eq $Receipt.CurrentHost -or [string]$Receipt.CurrentHost.BootId -cne [string]$CurrentHost.BootId -or
        [int]$Receipt.CurrentHost.LogicalCpus -ne 20 -or $Receipt.CurrentHost.SecureBoot -ne $true) {
        throw 'The saved inspection host identity differs from the current host.'
    }
    $healthReceipt = Get-GpuFinalReviewProperty $Receipt 'Health'
    $healthVerdictReceipt = Get-GpuFinalReviewProperty $healthReceipt 'Verdict'
    $healthCpuPassCount = Get-GpuFinalReviewProperty $healthVerdictReceipt 'CpuPassCount'
    $healthCpuPassCountInvalid = $script:GpuFinalReviewProfileId -in @('062106', '160027') -and
        ($null -eq $healthCpuPassCount -or [int]$healthCpuPassCount -ne 0)
    if ($null -eq $Receipt.Health -or $Receipt.Health.CommandSucceeded -ne $true -or
        $null -eq $Receipt.Health.Verdict -or $Receipt.Health.Verdict.Passed -ne $true -or
        [string]$Receipt.Health.Verdict.ExpectedResult -cne 'ABSENT' -or
        [string]$Receipt.Health.Verdict.Aggregate -cne 'ABSENT' -or
        [int]$Receipt.Health.Verdict.ExpectedCpus -ne 20 -or [int]$Receipt.Health.Verdict.CpuCount -ne 20 -or
        $healthCpuPassCountInvalid) {
        throw 'Saved fresh health evidence does not report 20 ABSENT CPUs.'
    }
    if ($null -eq $Receipt.Esp -or [string]$Receipt.Esp.Volume -cne [string]$State.Backup.EspVolume -or
        [int]$Receipt.Esp.DiskNumber -ne [int]$State.Backup.EspDiskNumber -or
        [string]$Receipt.Esp.PartitionGuid -cne [string]$State.Backup.EspPartitionGuid -or
        [string]$Receipt.Esp.BootManagerSha256 -cne $script:GpuFinalReviewBootManagerSha256) {
        throw 'Saved ESP evidence differs from the current run backup.'
    }
    if (@($Receipt.FirmwareObservations).Count -ne 2 -or @($Receipt.BootSelectionCaptures).Count -ne 2) {
        throw 'Saved inspection must contain exactly two firmware and two raw boot-selection observations.'
    }
    $observationCheckpoints = @('before', 'after')
    for ($index = 0; $index -lt 2; $index++) {
        $firmwareObservation = $Receipt.FirmwareObservations[$index]
        $expectedFirmwareName = $script:GpuFinalReviewFirmwareObservationLeaves[$index]
        $expectedFirmwarePath = Join-Path $reviewDirectory ('observations\' + $expectedFirmwareName)
        if ([string]$firmwareObservation.Checkpoint -cne $observationCheckpoints[$index] -or
            $firmwareObservation.Succeeded -ne $true -or
            [string]$firmwareObservation.Pin.Sha256 -cne $script:GpuFinalReviewCurrentFirmwareSha256 -or
            [long]$firmwareObservation.Pin.Bytes -ne $script:GpuFinalReviewCurrentFirmwareBytes -or
            [IO.Path]::GetFullPath([string]$firmwareObservation.Pin.Path) -ine [IO.Path]::GetFullPath($expectedFirmwarePath) -or
            [string]$firmwareObservation.Text -cne [string]$Receipt.FirmwareObservations[0].Text) {
            throw 'Saved mounted firmware observations do not match the reviewed stable current capture.'
        }
        $bootCapture = $Receipt.BootSelectionCaptures[$index]
        $expectedRawPath = Join-Path $reviewDirectory ('observations\' + $script:GpuFinalReviewRawObservationLeaves[$index])
        if ($null -eq $bootCapture.Baseline -or $null -eq $bootCapture.BaselinePin -or
            [string]$bootCapture.BaselinePin.Sha256 -cne $script:GpuFinalReviewRawSha256 -or
            [long]$bootCapture.BaselinePin.Bytes -ne $script:GpuFinalReviewRawBytes -or
            [IO.Path]::GetFullPath([string]$bootCapture.BaselinePath) -ine [IO.Path]::GetFullPath($expectedRawPath) -or
            [IO.Path]::GetFullPath([string]$bootCapture.BaselinePin.Path) -ine [IO.Path]::GetFullPath($expectedRawPath)) {
            throw 'Saved raw boot-selection file is outside its pinned inspection evidence or changed.'
        }
        Assert-GpuFinalReviewRawBaseline -Baseline $bootCapture.Baseline -Description "Saved $($observationCheckpoints[$index])"
        Assert-GpuFinalReviewPinRecord -Pin $bootCapture.AuditToolPin -ExpectedPath $auditToolPath -ExpectedSha256 $script:GpuFinalReviewIbauditSha256 -ExpectedBytes $script:GpuFinalReviewIbauditBytes -Description 'Saved audit tool'
        Assert-GpuFinalReviewPinRecord -Pin $bootCapture.ReferenceLogPin -ExpectedPath $referenceLogPath -ExpectedSha256 $script:GpuFinalReviewReferenceLogSha256 -ExpectedBytes $script:GpuFinalReviewReferenceLogBytes -Description 'Saved reference log'
        Assert-GpuFinalReviewEqual $script:GpuFinalReviewBootManagerSha256 ([string]$bootCapture.Baseline.BootManagerSha256) 'Saved boot-manager hash'
        Assert-GpuFinalReviewEqual 'BootmgfwSha256=3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511 Findings=0x00000000' ([string]$bootCapture.IntegrityOutput) 'Saved boot-manager audit output'
    }
    if ([string]$Receipt.FirmwareObservations[0].Text -cne [string]$Receipt.FirmwareObservations[1].Text -or
        $Receipt.FirmwareTextStable -ne $true -or $Receipt.RawBootSelectionStable -ne $true) {
        throw 'Saved firmware or raw boot-selection observations were not stable.'
    }

    return [pscustomobject]@{
        WorkspaceRoot=$workspaceRoot; SourceRoot=$sourceRoot; RunDirectory=$runDirectory; State=$State
        ReviewDirectory=$reviewDirectory; ReceiptPath=$receiptPath; SelectorPath=$selectorPath; JournalPath=$journalPath
        ActionDirectory=[IO.Path]::GetFullPath($ActionDirectory)
        BaselinePath=$baselinePath; OriginalCurrentPath=$oldCurrentPath; FinalResultPath=$finalResultPath
        EntryJournalPath=$entryJournalPath; AuditToolPath=$auditToolPath
        ReferenceLogPath=$referenceLogPath; ProbePath=$probePath; StateModulePath=$stateModulePath
        InspectorPath=$inspectorPath
        PlatformModulePath=$platformModulePath; RetirementModulePath=$retirementModulePath
    }
}

function Get-GpuFinalReviewStatePin {
    param([object]$StateModule, [string]$Path)
    return & $StateModule { param($p) Get-GpuFilePin -Path $p } $Path
}

function Assert-GpuFinalReviewLivePin {
    param([object]$StateModule, [object]$Pin, [string]$ExpectedPath, [string]$ExpectedSha256, [long]$ExpectedBytes, [string]$Description)
    $actual = Get-GpuFinalReviewStatePin -StateModule $StateModule -Path $ExpectedPath
    Assert-GpuFinalReviewPinRecord -Pin $actual -ExpectedPath $ExpectedPath -ExpectedSha256 $ExpectedSha256 -ExpectedBytes $ExpectedBytes -Description $Description
    if ([IO.Path]::GetFullPath([string]$Pin.Path) -ine [IO.Path]::GetFullPath($ExpectedPath) -or
        [string]$Pin.Sha256 -ine [string]$actual.Sha256 -or [long]$Pin.Bytes -ne [long]$actual.Bytes) {
        throw "$Description changed after the reviewed receipt was produced."
    }
    return $actual
}

function Assert-GpuFinalReviewCurrentPlatformPin {
    param([Parameter(Mandatory = $true)][object]$Pin)
    if ($script:GpuFinalReviewProfileId -in @('042142', '062106', '160027')) {
        Assert-GpuFinalReviewPinRecord -Pin $Pin -ExpectedPath ([string]$Pin.Path) `
            -ExpectedSha256 $script:GpuFinalReviewPlatformCurrentSha256 `
            -ExpectedBytes $script:GpuFinalReviewPlatformCurrentBytes `
            -Description 'Current reviewed GPU host platform module'
    }
}

function Assert-GpuFinalReviewCommandReceipt {
    param([object]$StateModule, [string]$ReceiptPath, [string]$ExpectedProgram, [string[]]$ExpectedArguments, [string]$EvidenceRoot, [string]$Description)
    $null = & $StateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } $ReceiptPath $EvidenceRoot
    $receipt = & $StateModule { param($p) Read-GpuJson -Path $p } $ReceiptPath
    if ($receipt.SchemaVersion -ne 1 -or $receipt.Succeeded -ne $true -or $receipt.ExitCode -ne 0 -or
        $null -ne $receipt.LaunchError -or [IO.Path]::GetFullPath([string]$receipt.Program) -ine [IO.Path]::GetFullPath($ExpectedProgram) -or
        @($receipt.Arguments).Count -ne $ExpectedArguments.Count) {
        throw "$Description command receipt is malformed or unsuccessful."
    }
    for ($index = 0; $index -lt $ExpectedArguments.Count; $index++) {
        if ([string]$receipt.Arguments[$index] -cne $ExpectedArguments[$index]) { throw "$Description command arguments differ from the reviewed read-only operation." }
    }
    foreach ($outputPath in @([string]$receipt.StdoutPath, [string]$receipt.StderrPath)) {
        $null = & $StateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } $outputPath $EvidenceRoot
    }
    if (-not [string]::IsNullOrEmpty([IO.File]::ReadAllText([string]$receipt.StderrPath))) { throw "$Description command wrote unexpected stderr." }
    return $receipt
}

function Assert-GpuFinalReviewReceiptFiles {
    param([object]$StateModule, [object]$Receipt, [object]$Paths)
    $null = Assert-GpuFinalReviewLivePin $StateModule $Receipt.PinsBefore.Selector $Paths.SelectorPath $script:GpuFinalReviewSelectorSha256 $script:GpuFinalReviewSelectorBytes 'Active selector'
    $null = Assert-GpuFinalReviewLivePin $StateModule $Receipt.PinsBefore.FirmwareBaseline $Paths.BaselinePath $script:GpuFinalReviewBaselineSha256 $script:GpuFinalReviewBaselineFirmwareBytes 'Firmware baseline'
    $null = Assert-GpuFinalReviewLivePin $StateModule $Receipt.PinsBefore.FirmwareCurrent $Paths.OriginalCurrentPath $script:GpuFinalReviewCurrentFirmwareSha256 $script:GpuFinalReviewCurrentFirmwareBytes 'Original FinalVerify firmware capture'
    $null = Assert-GpuFinalReviewLivePin $StateModule $Receipt.PinsBefore.Ibaudit $Paths.AuditToolPath $script:GpuFinalReviewIbauditSha256 $script:GpuFinalReviewIbauditBytes 'Read-only audit tool'
    $null = Assert-GpuFinalReviewLivePin $StateModule $Receipt.PinsBefore.ReferenceLog $Paths.ReferenceLogPath $script:GpuFinalReviewReferenceLogSha256 $script:GpuFinalReviewReferenceLogBytes 'Reference TPM log'
    $null = Assert-GpuFinalReviewLivePin $StateModule $Receipt.PinsBefore.Probe $Paths.ProbePath $script:GpuFinalReviewProbeSha256 $script:GpuFinalReviewProbeBytes 'Health probe'
    $null = Assert-GpuFinalReviewLivePin $StateModule $Receipt.PinsBefore.InspectorScript $Paths.InspectorPath $script:GpuFinalReviewInspectorSha256 $script:GpuFinalReviewInspectorBytes 'Firmware review inspector'
    $modulePin = Get-GpuFinalReviewStatePin -StateModule $StateModule -Path $Paths.StateModulePath
    Assert-GpuFinalReviewPinRecord $modulePin $Paths.StateModulePath $script:GpuFinalReviewStateSha256 $script:GpuFinalReviewStateBytes 'Current host state module'
    $retirementPin = Get-GpuFinalReviewStatePin -StateModule $StateModule -Path $Paths.RetirementModulePath
    Assert-GpuFinalReviewPinRecord $retirementPin $Paths.RetirementModulePath $script:GpuFinalReviewRetirementSha256 $script:GpuFinalReviewRetirementBytes 'Current retirement module'
    if ($script:GpuFinalReviewProfileId -in @('042142', '062106', '160027')) {
        $null = Assert-GpuFinalReviewLivePin $StateModule $Receipt.PinsBefore.FinalResult $Paths.FinalResultPath $script:GpuFinalReviewFinalResultSha256 $script:GpuFinalReviewFinalResultBytes 'Failed FinalVerify result'
        Assert-GpuFinalReviewFailedFinalResult -Path $Paths.FinalResultPath
        $null = Assert-GpuFinalReviewEntryJournal -Path $Paths.EntryJournalPath -State $Paths.State -ActionDirectory $Paths.ActionDirectory
    }

    $evidenceRoot = $Paths.ReviewDirectory
    foreach ($observation in @($Receipt.FirmwareObservations)) {
        $null = & $StateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } ([string]$observation.Pin.Path) $evidenceRoot
        $actual = Get-GpuFinalReviewStatePin -StateModule $StateModule -Path ([string]$observation.Pin.Path)
        Assert-GpuFinalReviewPinRecord $actual ([string]$observation.Pin.Path) $script:GpuFinalReviewCurrentFirmwareSha256 $script:GpuFinalReviewCurrentFirmwareBytes 'Saved firmware observation'
        if ([IO.File]::ReadAllText([string]$observation.Pin.Path) -cne [string]$observation.Text) { throw 'Saved firmware observation text differs from its pinned stdout.' }
        $command = Assert-GpuFinalReviewCommandReceipt $StateModule ([string]$observation.ReceiptPath) 'C:\windows\system32\bcdedit.exe' @('/enum','firmware','/v') $evidenceRoot 'Saved firmware enumeration'
        if ([IO.Path]::GetFullPath([string]$command.StdoutPath) -ine [IO.Path]::GetFullPath([string]$observation.Pin.Path)) { throw 'Saved firmware receipt stdout differs from the pinned observation.' }
    }
    foreach ($capture in @($Receipt.BootSelectionCaptures)) {
        $null = & $StateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } ([string]$capture.BaselinePath) $evidenceRoot
        $rawPin = Get-GpuFinalReviewStatePin -StateModule $StateModule -Path ([string]$capture.BaselinePath)
        Assert-GpuFinalReviewPinRecord $rawPin ([string]$capture.BaselinePath) $script:GpuFinalReviewRawSha256 $script:GpuFinalReviewRawBytes 'Saved raw boot-selection JSON'
        $raw = & $StateModule { param($p) Read-GpuJson -Path $p } ([string]$capture.BaselinePath)
        if ($raw.schema -ne 1 -or [string]$raw.kind -cne 'boot-selection' -or $raw.findings -ne 0 -or
            $null -ne $raw.boot_next -or [string]$raw.bootmgfw_sha256 -cne $script:GpuFinalReviewBootManagerSha256 -or
            [string]$raw.log_sha256 -cne $script:GpuFinalReviewReferenceLogSha256 -or @($raw.boot_order).Count -ne @($script:GpuFinalReviewExpectedBootOrder).Count) {
            throw 'Saved raw boot-selection JSON is not the reviewed zero-finding baseline.'
        }
        $expectedOrder = @($script:GpuFinalReviewExpectedBootOrder)
        for ($orderIndex = 0; $orderIndex -lt $expectedOrder.Count; $orderIndex++) {
            if ([int]$raw.boot_order[$orderIndex] -ne [int]$expectedOrder[$orderIndex] -or [int]$capture.Baseline.BootOrder[$orderIndex] -ne [int]$expectedOrder[$orderIndex]) {
                throw 'Saved raw BootOrder differs from the reviewed stable order.'
            }
        }
        $integrity = Assert-GpuFinalReviewCommandReceipt $StateModule ([string]$capture.IntegrityReceipt) $Paths.AuditToolPath @('bootmgr-integrity','--expected-hash',$script:GpuFinalReviewBootManagerSha256,'--esp-root','S:\') $evidenceRoot 'Saved boot-manager integrity'
        $integrityText = ([IO.File]::ReadAllText([string]$integrity.StdoutPath)).Trim()
        if ($integrityText -cne [string]$capture.IntegrityOutput) { throw 'Saved boot-manager integrity output differs from the review record.' }
    }
    $health = Assert-GpuFinalReviewCommandReceipt $StateModule ([string]$Receipt.Health.ReceiptPath) $Paths.ProbePath @('health') $evidenceRoot 'Saved health observation'
    if ($script:GpuFinalReviewProfileId -in @('042142', '062106', '160027')) {
        $healthStdoutPin = Get-GpuFinalReviewStatePin -StateModule $StateModule -Path ([string]$health.StdoutPath)
        Assert-GpuFinalReviewPinRecord $healthStdoutPin ([string]$health.StdoutPath) $script:GpuFinalReviewHealthStdoutSha256 $script:GpuFinalReviewHealthStdoutBytes 'Pinned health stdout'
        $healthStderrPin = Get-GpuFinalReviewStatePin -StateModule $StateModule -Path ([string]$health.StderrPath)
        Assert-GpuFinalReviewPinRecord $healthStderrPin ([string]$health.StderrPath) $script:GpuFinalReviewHealthStderrSha256 $script:GpuFinalReviewHealthStderrBytes 'Pinned health stderr'
    }
    $healthLines = @((Get-Content -LiteralPath ([string]$health.StdoutPath) -ErrorAction Stop) + (Get-Content -LiteralPath ([string]$health.StderrPath) -ErrorAction Stop))
    $healthVerdict = & $StateModule { param($lines) Get-GpuHealthVerdict -Lines $lines -ExpectedCpus 20 -ExpectedResult ABSENT } ([string[]]$healthLines)
    if ($healthVerdict.Passed -ne $true) { throw 'Pinned health command output no longer reports 20 ABSENT CPUs.' }
}

function Get-GpuFinalReviewMountedEsp {
    param([object]$PlatformModule, [string]$ActionDirectory, [object]$Backup)
    if (-not (Test-Path -LiteralPath 'S:\')) { throw 'The caller-owned system ESP mount at S: is not present.' }
    $mountvol = & $PlatformModule { Get-GpuApplicationPath -Name 'mountvol.exe' }
    $receipt = & $PlatformModule {
        param($program, $directory)
        Invoke-GpuNativeCommand -Program $program -Arguments @('S:', '/L') -ActionDirectory $directory -Label 'final-firmware-review-mounted-volume'
    } $mountvol $ActionDirectory
    if ($null -eq $receipt -or $receipt.Succeeded -ne $true -or [string]::IsNullOrWhiteSpace([string]$receipt.StdoutPath)) {
        throw 'Could not read the current S: ESP volume identity.'
    }
    $volume = ([IO.File]::ReadAllText([string]$receipt.StdoutPath)).Trim()
    $partition = & $PlatformModule { param($v) Resolve-GpuEspPartition -Volume $v } $volume
    $identity = [pscustomobject]@{
        Volume=$volume; DiskNumber=[int]$partition.DiskNumber
        PartitionNumber=[int]$partition.PartitionNumber; PartitionGuid=([string]$partition.Guid).Trim('{}').ToUpperInvariant()
    }
    return & $PlatformModule { param($m,$b) Assert-GpuMountedEspMatchesBackup -MountedEsp $m -Backup $b -Description 'Final firmware review' } $identity $Backup
}

function Invoke-GpuFinalFirmwareReviewCore {
    param([object]$State, [object]$CurrentHost, [object]$CurrentFirmwarePin, [string]$ActionDirectory, [hashtable]$Operations)
    $root = Assert-GpuFinalFirmwareReviewReceiptShape -Receipt $Operations.Receipt -State $State -CurrentHost $CurrentHost -CurrentFirmwarePin $CurrentFirmwarePin -ActionDirectory $ActionDirectory
    $stateModule = $Operations.StateModule
    $selectorBefore = Get-GpuFinalReviewStatePin -StateModule $stateModule -Path $root.SelectorPath
    Assert-GpuFinalReviewPinRecord $selectorBefore $root.SelectorPath $script:GpuFinalReviewSelectorSha256 $script:GpuFinalReviewSelectorBytes 'Active selector'
    $journalPath = $root.JournalPath
    $journalBefore = Get-GpuFinalReviewStatePin -StateModule $stateModule -Path $journalPath
    if ([string]$journalBefore.Sha256 -notmatch '^[0-9A-Fa-f]{64}$') { throw 'Current journal pin is malformed.' }
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } ([string]$CurrentFirmwarePin.Path) $ActionDirectory
    $inputCurrentBefore = Get-GpuFinalReviewStatePin -StateModule $stateModule -Path ([string]$CurrentFirmwarePin.Path)
    if ($inputCurrentBefore.Sha256 -cne [string]$CurrentFirmwarePin.Sha256 -or $inputCurrentBefore.Bytes -ne [long]$CurrentFirmwarePin.Bytes -or
        $inputCurrentBefore.Sha256 -cne $script:GpuFinalReviewCurrentFirmwareSha256 -or $inputCurrentBefore.Bytes -ne $script:GpuFinalReviewCurrentFirmwareBytes) {
        throw 'Failed FinalVerify firmware capture changed or differs from its reviewed pin.'
    }
    foreach ($pin in @($Operations.ImmutablePins)) { & $Operations.AssertImmutablePin $pin }
    Assert-GpuFinalReviewReceiptFiles -StateModule $stateModule -Receipt $Operations.Receipt -Paths $root
    $null = Get-GpuFinalReviewMountedEsp -PlatformModule $Operations.PlatformModule -ActionDirectory $ActionDirectory -Backup $State.Backup

    # The pinned native auditor uses CreateFileW without an extended path.
    # Keep its explicit JSON output below MAX_PATH for this fixed workspace.
    $reviewActionDirectory = Join-Path $ActionDirectory 'fw-review'
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r } $reviewActionDirectory $ActionDirectory
    if ([IO.Directory]::Exists($reviewActionDirectory) -or [IO.File]::Exists($reviewActionDirectory)) { throw 'Final firmware review evidence directory already exists.' }
    [void][IO.Directory]::CreateDirectory($reviewActionDirectory)

    $captureBefore = & $Operations.CaptureBootSelection $State $reviewActionDirectory 'before'
    if ($null -eq $captureBefore -or $null -eq $captureBefore.Baseline) { throw 'Fresh raw boot-selection capture failed before firmware text.' }
    Assert-GpuFinalReviewRawBaseline -Baseline $captureBefore.Baseline -Description 'Fresh before'
    $firmwareCapture = & $Operations.CaptureFirmware $reviewActionDirectory
    if ($null -eq $firmwareCapture -or $null -eq $firmwareCapture.Pin) { throw 'Fresh mounted firmware text capture failed.' }
    Assert-GpuFinalReviewPinRecord -Pin $firmwareCapture.Pin -ExpectedPath ([string]$firmwareCapture.Pin.Path) -ExpectedSha256 $script:GpuFinalReviewCurrentFirmwareSha256 -ExpectedBytes $script:GpuFinalReviewCurrentFirmwareBytes -Description 'Fresh mounted firmware text'
    if ([string]$firmwareCapture.Text -cne [IO.File]::ReadAllText([string]$CurrentFirmwarePin.Path)) { throw 'Fresh mounted firmware text differs from the failed FinalVerify capture.' }
    $inputCurrentAfter = Get-GpuFinalReviewStatePin -StateModule $stateModule -Path ([string]$CurrentFirmwarePin.Path)
    if ($inputCurrentAfter.Sha256 -cne [string]$CurrentFirmwarePin.Sha256 -or $inputCurrentAfter.Bytes -ne [long]$CurrentFirmwarePin.Bytes) { throw 'Failed FinalVerify firmware capture changed during review.' }
    $captureAfter = & $Operations.CaptureBootSelection $State $reviewActionDirectory 'after'
    if ($null -eq $captureAfter -or $null -eq $captureAfter.Baseline) { throw 'Fresh raw boot-selection capture failed after firmware text.' }
    Assert-GpuFinalReviewRawBaseline -Baseline $captureAfter.Baseline -Description 'Fresh after'
    & $Operations.AssertBootSelectionStable $captureBefore.Baseline $captureAfter.Baseline

    foreach ($pin in @($Operations.ImmutablePins)) { & $Operations.AssertImmutablePin $pin }
    Assert-GpuFinalReviewReceiptFiles -StateModule $stateModule -Receipt $Operations.Receipt -Paths $root
    $selectorAfter = Get-GpuFinalReviewStatePin -StateModule $stateModule -Path $root.SelectorPath
    $journalAfter = Get-GpuFinalReviewStatePin -StateModule $stateModule -Path $journalPath
    if ($selectorAfter.Sha256 -cne $selectorBefore.Sha256 -or $selectorAfter.Bytes -ne $selectorBefore.Bytes -or
        $journalAfter.Sha256 -cne $journalBefore.Sha256 -or $journalAfter.Bytes -ne $journalBefore.Bytes) {
        throw 'Selector or journal changed during the read-only firmware review.'
    }
    $freshFirmwareAfter = Get-GpuFinalReviewStatePin -StateModule $stateModule -Path ([string]$firmwareCapture.Pin.Path)
    if ($freshFirmwareAfter.Sha256 -cne [string]$firmwareCapture.Pin.Sha256 -or $freshFirmwareAfter.Bytes -ne [long]$firmwareCapture.Pin.Bytes) { throw 'Fresh firmware evidence changed during review.' }
    $helperAfter = Get-GpuFinalReviewStatePin -StateModule $stateModule -Path ([string]$Operations.HelperPin.Path)
    $platformAfter = Get-GpuFinalReviewStatePin -StateModule $stateModule -Path ([string]$Operations.PlatformPin.Path)
    if ($helperAfter.Sha256 -cne [string]$Operations.HelperPin.Sha256 -or $helperAfter.Bytes -ne [long]$Operations.HelperPin.Bytes -or
        $platformAfter.Sha256 -cne [string]$Operations.PlatformPin.Sha256 -or $platformAfter.Bytes -ne [long]$Operations.PlatformPin.Bytes) {
        throw 'Current helper or platform source changed during the firmware review.'
    }

    return [pscustomobject]@{
        Accepted=$true
        ReceiptPin=$Operations.ReceiptPin
        OriginalBaselinePin=$Operations.Receipt.PinsBefore.FirmwareBaseline
        ReviewedFirmwarePin=$Operations.Receipt.PinsBefore.FirmwareCurrent
        FreshFirmwarePin=$freshFirmwareAfter
        RawCaptures=@($captureBefore, $captureAfter)
        SourcePins=[pscustomobject]@{
            Helper=[pscustomobject]@{ Before=$Operations.HelperPin; After=$helperAfter }
            Platform=[pscustomobject]@{ Before=$Operations.PlatformPin; After=$platformAfter }
        }
        HistoricalRawBootOrderUnchangedProven=$false
        FullBootVariableNamespaceAttested=$false
        Scope='Read-only mounted-ESP firmware text and current raw BootNext/BootOrder stability review; historical raw BootOrder and the full firmware-variable namespace are not attested.'
    }
}

function Invoke-GpuReviewedFinalFirmware {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$State,
        [Parameter(Mandatory = $true)][object]$CurrentHost,
        [Parameter(Mandatory = $true)][object]$CurrentFirmwarePin,
        [Parameter(Mandatory = $true)][string]$ActionDirectory
    )

    Select-GpuFinalFirmwareReviewProfile -RunId ([string]$State.RunId)

    $sourceRoot = [IO.Path]::GetFullPath([string]$State.SourceRoot)
    $workspaceRoot = [IO.Path]::GetFullPath([string]$State.WorkspaceRoot)
    $expectedSourceRoot = [IO.Path]::GetFullPath((Join-Path $workspaceRoot '.codex-build\worktrees\gpu-windows-validation'))
    if (-not [string]::Equals($sourceRoot, $expectedSourceRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The fixed GPU source worktree differs from the current run workspace.'
    }
    $stateModulePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1'
    $platformModulePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
    $retirementModulePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuStageRetirement.psm1'
    $helperPath = $script:GpuFinalReviewModulePath
    if ([IO.Path]::GetFullPath($helperPath) -ine [IO.Path]::GetFullPath((Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuFinalFirmwareReview.psm1'))) {
        throw 'The active final-review helper path differs from the fixed source worktree.'
    }

    # Verify the fixed helper modules before importing code from the worktree.
    # The platform module is maintained by the caller's current integration;
    # pin its current file identity here and recheck after the read-only query.
    foreach ($modulePin in @(
        [pscustomobject]@{ Path=$stateModulePath; Sha256=$script:GpuFinalReviewStateSha256; Bytes=$script:GpuFinalReviewStateBytes; Name='GPU host state module' },
        [pscustomobject]@{ Path=$retirementModulePath; Sha256=$script:GpuFinalReviewRetirementSha256; Bytes=$script:GpuFinalReviewRetirementBytes; Name='GPU stage-retirement module' }
    )) {
        $moduleItem = Get-Item -LiteralPath $modulePin.Path -ErrorAction Stop
        if ($moduleItem.PSIsContainer) { throw "$($modulePin.Name) is not a regular file." }
        $moduleHash = (Get-FileHash -LiteralPath $modulePin.Path -Algorithm SHA256 -ErrorAction Stop).Hash
        if ([long]$moduleItem.Length -ne [long]$modulePin.Bytes -or $moduleHash -ine [string]$modulePin.Sha256) {
            throw "$($modulePin.Name) differs from its fixed reviewed pin."
        }
    }
    $platformItem = Get-Item -LiteralPath $platformModulePath -ErrorAction Stop
    if ($platformItem.PSIsContainer) { throw 'GPU host platform module is not a regular file.' }
    $platformPin = [pscustomobject]@{
        Path=[IO.Path]::GetFullPath($platformModulePath)
        Sha256=(Get-FileHash -LiteralPath $platformModulePath -Algorithm SHA256 -ErrorAction Stop).Hash
        Bytes=[long]$platformItem.Length
        Description='Current GPU host platform module'
    }
    Assert-GpuFinalReviewCurrentPlatformPin -Pin $platformPin
    $helperItem = Get-Item -LiteralPath $helperPath -ErrorAction Stop
    if ($helperItem.PSIsContainer) { throw 'GPU final firmware review helper is not a regular file.' }
    $helperPin = [pscustomobject]@{
        Path=[IO.Path]::GetFullPath($helperPath)
        Sha256=(Get-FileHash -LiteralPath $helperPath -Algorithm SHA256 -ErrorAction Stop).Hash
        Bytes=[long]$helperItem.Length
        Description='Current GPU final firmware review helper'
    }

    # Reuse dependencies already loaded by the dispatcher. Force-reloading them
    # from this nested module removes their exports from the caller's scope.
    $stateModule = Import-Module -Name $stateModulePath -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    $platformModule = Import-Module -Name $platformModulePath -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    $retirementModule = Import-Module -Name $retirementModulePath -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    $reviewRoot = Join-Path (Join-Path $workspaceRoot '.analysis\gpu-final-firmware-review') $script:GpuFinalReviewReviewDirectoryName
    $receiptPath = Join-Path $reviewRoot 'result.json'
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } $receiptPath $workspaceRoot
    $receiptPin = & $stateModule { param($p) Get-GpuFilePin -Path $p } $receiptPath
    Assert-GpuFinalReviewPinRecord $receiptPin $receiptPath $script:GpuFinalReviewAuditResultSha256 $script:GpuFinalReviewAuditResultBytes 'Pinned successful firmware review receipt'
    $reviewReceipt = & $stateModule { param($p) Read-GpuJson -Path $p } $receiptPath
    $auditToolPath = Join-Path $workspaceRoot '.codex-build\gpu-stage-retirement-audit\readonly-v1\ibaudit.exe'
    $auditToolHash = $script:GpuFinalReviewIbauditSha256
    $auditToolBytes = $script:GpuFinalReviewIbauditBytes
    $logHash = $script:GpuFinalReviewReferenceLogSha256
    $logBytes = $script:GpuFinalReviewReferenceLogBytes
    $operations = @{
        Receipt=$reviewReceipt; ReceiptPin=$receiptPin; StateModule=$stateModule; PlatformModule=$platformModule
        HelperPin=$helperPin; PlatformPin=$platformPin
        ImmutablePins=@(
            [pscustomobject]@{Path=$receiptPath;Sha256=$script:GpuFinalReviewAuditResultSha256;Bytes=$script:GpuFinalReviewAuditResultBytes;Description='Pinned successful firmware review receipt'}
            [pscustomobject]@{Path=$auditToolPath;Sha256=$script:GpuFinalReviewIbauditSha256;Bytes=$script:GpuFinalReviewIbauditBytes;Description='Pinned read-only audit tool'}
            [pscustomobject]@{Path=(Join-Path (Join-Path (Join-Path $State.RunDirectory 'actions') $script:GpuFinalReviewBackupActionName) 'tcglog.ib');Sha256=$script:GpuFinalReviewReferenceLogSha256;Bytes=$script:GpuFinalReviewReferenceLogBytes;Description='Current-run reference TPM log'}
            [pscustomobject]@{Path=(Join-Path $workspaceRoot ('.codex-build\gb\' + $script:GpuFinalReviewProbeBuildId + '\l\ib-tcg-dump.exe'));Sha256=$script:GpuFinalReviewProbeSha256;Bytes=$script:GpuFinalReviewProbeBytes;Description='Current-run health probe'}
            [pscustomobject]@{Path=$stateModulePath;Sha256=$script:GpuFinalReviewStateSha256;Bytes=$script:GpuFinalReviewStateBytes;Description='GPU host state module'}
            [pscustomobject]@{Path=$retirementModulePath;Sha256=$script:GpuFinalReviewRetirementSha256;Bytes=$script:GpuFinalReviewRetirementBytes;Description='GPU stage-retirement module'}
            $helperPin
            $platformPin
        )
        AssertImmutablePin={ param($pin) $actual=& $stateModule { param($p) Get-GpuFilePin -Path $p } ([string]$pin.Path); if($actual.Sha256 -cne [string]$pin.Sha256 -or $actual.Bytes -ne [long]$pin.Bytes){throw "$($pin.Description) changed during final firmware review."} }.GetNewClosure()
        CaptureBootSelection={
            param($runState,$directory,$checkpoint)
            & $retirementModule {
                param($s,$d,$c,$tool,$platform,$toolHash,$toolBytes,$referenceHash,$referenceBytes)
                Invoke-GpuStageRetirementBootSelectionCapture -State $s -ActionDirectory $d -Checkpoint $c `
                    -AuditToolPath $tool -PlatformModule $platform -ExpectedToolHash $toolHash `
                    -ExpectedToolBytes $toolBytes -ExpectedLogHash $referenceHash -ExpectedLogBytes $referenceBytes
            } $runState $directory $checkpoint $auditToolPath $platformModule $auditToolHash $auditToolBytes $logHash $logBytes
        }.GetNewClosure()
        CaptureFirmware={
            param($directory)
            $capture = & $platformModule {
                param($d)
                $program = Get-GpuApplicationPath 'bcdedit.exe'
                Invoke-GpuNativeCommand -Program $program -Arguments @('/enum','firmware','/v') `
                    -ActionDirectory $d -Label 'final-firmware-review'
            } $directory
            if (-not $capture.Succeeded) { throw "Firmware enumeration failed with exit $($capture.ExitCode)." }
            $pin = & $stateModule { param($p) Get-GpuFilePin -Path $p } $capture.StdoutPath
            [pscustomobject]@{ Text=[IO.File]::ReadAllText($capture.StdoutPath); Pin=$pin; ReceiptPath=$capture.ReceiptPath }
        }.GetNewClosure()
        AssertBootSelectionStable={ param($before,$after) & $retirementModule { param($b,$a) Assert-GpuStageRetirementBootSelectionStable -Before $b -Current $a -Checkpoint 'during final firmware review' } $before $after }.GetNewClosure()
    }
    return Invoke-GpuFinalFirmwareReviewCore -State $State -CurrentHost $CurrentHost -CurrentFirmwarePin $CurrentFirmwarePin -ActionDirectory $ActionDirectory -Operations $operations
}

Export-ModuleMember -Function @('Invoke-GpuReviewedFinalFirmware')
