[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$ModulePath = Join-Path $ToolRoot 'GpuFinalFirmwareReview.psm1'
$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = [Collections.Generic.List[string]]::new()

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}
function Assert-Throws {
    param([scriptblock]$Operation, [string]$Pattern, [string]$Message)
    $observed = $null
    try { $null = & $Operation } catch { $observed = [string]$_.Exception.Message }
    Assert-True ($null -ne $observed -and $observed -match $Pattern) "$Message (observed: $observed)"
}
function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)
    try { & $Body; $script:PassedCases++; Write-Output "PASS: $Name" }
    catch { $script:FailedCases.Add("$Name`: $($_.Exception.Message)"); Write-Output "FAIL: $Name`: $($_.Exception.Message)" }
}
function New-Pin {
    param([string]$Path, [string]$Sha256, [long]$Bytes)
    return [pscustomobject]@{ Path=[IO.Path]::GetFullPath($Path); Sha256=$Sha256; Bytes=$Bytes }
}
function New-ValidFinalReviewFixture {
    param([string]$WorkspaceRoot)
    $sourceRoot = Join-Path $WorkspaceRoot '.codex-build\worktrees\gpu-windows-validation'
    $runId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
    $runDirectory = Join-Path $WorkspaceRoot ('.analysis\gpu-baremetal\runs\' + $runId)
    $actionDirectory = Join-Path (Join-Path $runDirectory 'actions') '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'
    $backupAction = Join-Path (Join-Path $runDirectory 'actions') '20261020T201533358Z-Backup-479329a8e70a4124964be17cc597ad61'
    $oldFinalAction = Join-Path (Join-Path $runDirectory 'actions') '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'
    $reviewDirectory = Join-Path (Join-Path $WorkspaceRoot '.analysis\gpu-final-firmware-review') '8abb7b84ca644a7c84019a1f29b35bb4'
    $observations = Join-Path $reviewDirectory 'observations'
    $statePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1'
    $platformPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
    $retirementPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuStageRetirement.psm1'
    $selectorPath = Join-Path $WorkspaceRoot '.analysis\gpu-baremetal\host-prepared.json'
    $journalPath = Join-Path $runDirectory 'journal.json'
    $baselinePath = Join-Path $backupAction 'firmware-enumeration-backup-4196e13c22604293a54688532c15cff6.stdout.txt'
    $originalCurrentPath = Join-Path $oldFinalAction 'firmware-enumeration-final-7a5347fd0bc4452eb06340ba1e614a92.stdout.txt'
    $auditToolPath = Join-Path $WorkspaceRoot '.codex-build\gpu-stage-retirement-audit\readonly-v1\ibaudit.exe'
    $referencePath = Join-Path $backupAction 'tcglog.ib'
    $probePath = Join-Path $WorkspaceRoot '.codex-build\gb\8be3274dfe56cec7\l\ib-tcg-dump.exe'
    $inspectorPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1'
    $state = [pscustomobject]@{
        WorkspaceRoot=$WorkspaceRoot; SourceRoot=$sourceRoot; SourceCommit='933b4c676318f26396cdf2c66de01c3ccbf1a950'
        RunId=$runId; RunDirectory=$runDirectory; Phase='Disarmed'; EverArmIntent=$true
        DisarmedBootId='2026-10-20T20:19:07.5000000Z'; InProgress=[pscustomobject]@{ Action='FinalVerify'; Directory=$actionDirectory }
        Host=[pscustomobject]@{ LogicalCpus=20 }
        Backup=[pscustomobject]@{
            EspVolume='\\?\Volume{187bc913-7576-4e6e-8d54-18eeb5c38fcd}\'; EspDiskNumber=0
            EspPartitionGuid='187BC913-7576-4E6E-8D54-18EEB5C38FCD'
            BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
            FirmwareEnumeration=(New-Pin $baselinePath 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE' 4336)
            ReferenceLog=(New-Pin $referencePath '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0' 40838)
        }
        Tools=[pscustomobject]@{ Probe=(New-Pin $probePath 'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47' 172544) }
        FinalVerification=[pscustomobject]@{ FirmwareEnumeration=[pscustomobject]@{
            TextEqual=$false; BaselineSha256='ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE'
            CurrentSha256='4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'; CaptureContext='SystemEspMountedAtS'
        } }
    }
    $currentHost = [pscustomobject]@{ BootId='2026-10-20T20:48:51.5000000Z'; LogicalCpus=20; SecureBoot=$true }
    $currentFirmwarePin = New-Pin (Join-Path $actionDirectory 'firmware-enumeration-final-fresh.stdout.txt') '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE' 4336
    $firmwareText = 'fixture mounted firmware text'
    $baseline = [pscustomobject]@{ Schema=1; BootNext=$null; BootOrder=[uint16[]]@(0,1,2,3,4,5); BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'; ReferenceLogSha256='8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'; IntegrityFindings='00000000' }
    $beforeRawPath = Join-Path $observations 'before-boot-selection.json'
    $afterRawPath = Join-Path $observations 'after-boot-selection.json'
    $rawPin = { param($path) New-Pin $path 'B5D4EB51A58A2170EBCA905749E2BDAD177CFD71629A1079A14934F3F5BF63D0' 259 }
    $rawCapture = {
        param($path)
        [pscustomobject]@{
            Baseline=$baseline; BaselinePath=$path; BaselinePin=(& $rawPin $path)
            IntegrityOutput='BootmgfwSha256=3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511 Findings=0x00000000'
            IntegrityReceipt=(Join-Path $observations (([IO.Path]::GetFileNameWithoutExtension($path)) + '-integrity.receipt.json'))
            AuditToolPin=(New-Pin $auditToolPath 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' 246784)
            ReferenceLogPin=(New-Pin $referencePath '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0' 40838)
        }
    }
    $receipt = [pscustomobject]@{
        SchemaVersion=1; Status='READONLY_FIRMWARE_REVIEW_CAPTURED'; Succeeded=$true; RunId=$runId
        RecoveryCompleted=$false; HistoricalRawBootOrderUnchangedProven=$false; SelectorAndJournalUnchanged=$true
        EspUnmounted=$true; CleanupSucceeded=$true; EspFilesChanged=$false; FirmwareWritten=$false; Errors=@(); CleanupErrors=@()
        EvidenceDirectory=$reviewDirectory
        PinsBefore=[pscustomobject]@{
            Selector=(New-Pin $selectorPath 'A7416148825A6ABC3603681C45F6A8277AFEE432D4B503BDEC89D34F58D34FFB' 533)
            Journal=(New-Pin $journalPath '631E2811091F4085445170A500985A30A296762EA15EEC0FD70C70422C55101D' 45269)
            FirmwareBaseline=(New-Pin $baselinePath 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE' 4336)
            FirmwareCurrent=(New-Pin $originalCurrentPath '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE' 4336)
            Ibaudit=(New-Pin $auditToolPath 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' 246784)
            ReferenceLog=(New-Pin $referencePath '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0' 40838)
            Probe=(New-Pin $probePath 'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47' 172544)
            InspectorScript=(New-Pin $inspectorPath '9EFE53FCC2CEA79BF0F2DF7E8E550AE122D381259AB7C176D7FA64A76A7A6275' 27895)
            Modules=[pscustomobject]@{
                State=(New-Pin $statePath '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC' 19843)
                Platform=(New-Pin $platformPath '4816FC0E640D7AEB38DF301E4D53A4982D5CABAA780650FEA8BDEE5D7C416899' 107733)
                Retirement=(New-Pin $retirementPath 'CCFA78B48A18AB90AE865ADEB14CA952D927955523B9E72F0C30A762F4FDB553' 61875)
            }
        }
        PinsAfter=$null
        CurrentHost=$currentHost
        Health=[pscustomobject]@{
            CommandSucceeded=$true; Verdict=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; ExpectedCpus=20; CpuCount=20; Aggregate='ABSENT' }
            ReceiptPath=(Join-Path $observations 'health.receipt.json')
        }
        Esp=[pscustomobject]@{ Volume=$state.Backup.EspVolume; DiskNumber=0; PartitionGuid=$state.Backup.EspPartitionGuid; BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511' }
        FirmwareTextStable=$true; RawBootSelectionStable=$true
        FirmwareObservations=@(
            [pscustomobject]@{ Checkpoint='before'; Succeeded=$true; Text=$firmwareText; Pin=(New-Pin (Join-Path $observations 'stage-retirement-firmware-3ba00749b6d94f82a0ef932ab7fb214d.stdout.txt') '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE' 4336); ReceiptPath=(Join-Path $observations 'firmware-before.receipt.json') }
            [pscustomobject]@{ Checkpoint='after'; Succeeded=$true; Text=$firmwareText; Pin=(New-Pin (Join-Path $observations 'stage-retirement-firmware-1f63298289ce49ed81012994adc954ba.stdout.txt') '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE' 4336); ReceiptPath=(Join-Path $observations 'firmware-after.receipt.json') }
        )
        BootSelectionCaptures=@((& $rawCapture $beforeRawPath), (& $rawCapture $afterRawPath))
    }
    $receipt.PinsAfter = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $receipt.PinsBefore -Depth 8 -Compress)
    return @{ State=$state; CurrentHost=$currentHost; CurrentFirmwarePin=$currentFirmwarePin; ActionDirectory=$actionDirectory; Receipt=$receipt }
}

function Update-FixtureStrings {
    param([AllowNull()][object]$Value, [hashtable]$Replacements)
    if ($null -eq $Value) { return $null }
    if ($Value -is [string]) {
        $updated = $Value
        foreach ($key in $Replacements.Keys) { $updated = $updated.Replace([string]$key, [string]$Replacements[$key]) }
        return $updated
    }
    if ($Value -is [Collections.IDictionary]) {
        foreach ($key in @($Value.Keys)) { $Value[$key] = Update-FixtureStrings $Value[$key] $Replacements }
        return $Value
    }
    if ($Value -is [Array]) {
        for ($index = 0; $index -lt $Value.Length; $index++) { $Value[$index] = Update-FixtureStrings $Value[$index] $Replacements }
        return ,$Value
    }
    if ($Value -is [pscustomobject]) {
        foreach ($property in @($Value.PSObject.Properties)) { $property.Value = Update-FixtureStrings $property.Value $Replacements }
    }
    return $Value
}

function Set-224018FixtureProfile {
    param([Parameter(Mandatory = $true)][object]$Fixture, [Parameter(Mandatory = $true)][string]$OriginalWorkspaceRoot, [Parameter(Mandatory = $true)][string]$TargetWorkspaceRoot)
    $oldRunId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
    $newRunId = 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1'
    $replacements = [ordered]@{
        $oldRunId=$newRunId
        $OriginalWorkspaceRoot=$TargetWorkspaceRoot
        '8abb7b84ca644a7c84019a1f29b35bb4'='1dccd9d33d1b4c808fd1059291a6d19b'
        '20261020T201533358Z-Backup-479329a8e70a4124964be17cc597ad61'='20261020T224257466Z-Backup-8bf99023efa4478e91e8fe626493da70'
        '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'='20261020T225933250Z-FinalVerify-cf3ab7b1e838427c964640d87f30257e'
        'firmware-enumeration-backup-4196e13c22604293a54688532c15cff6.stdout.txt'='firmware-enumeration-backup-99b1a3488d3d47ba93a25d46ae052d2d.stdout.txt'
        'firmware-enumeration-final-7a5347fd0bc4452eb06340ba1e614a92.stdout.txt'='firmware-enumeration-final-bd819c45c1e94e01b00b44dafe51c163.stdout.txt'
        'stage-retirement-firmware-3ba00749b6d94f82a0ef932ab7fb214d.stdout.txt'='stage-retirement-firmware-669f29ce055a488c8d9dda9d434e8879.stdout.txt'
        'stage-retirement-firmware-1f63298289ce49ed81012994adc954ba.stdout.txt'='stage-retirement-firmware-ce39fde67ee142e3810fbdc2e7c95c40.stdout.txt'
        '8be3274dfe56cec7'='a68d89b7b3ecb1a1'
    }
    $Fixture = Update-FixtureStrings $Fixture $replacements

    $newCurrentHash = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
    $newBaselineHash = '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'
    $newLogHash = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
    $newProbeHash = '76828AFED037973FBB4357AD89C97FB65D85D3BCF7811C9281F199ECB65A4583'
    $newRawHash = 'A8D2C8A2D2C7FB5EF6F12DA9362B4806A73136F9DD3801511FE2D89FAF6EF7A9'
    $newPlatformHash = '4276ED335283E2AD48E5D0426B8D4857063C2B9ADBFE1DC41693A0BE64BB9B51'
    $newRetirementHash = 'DF7F446426F63E21C5596E9CD1B35ECA0172C182B3E802B6267962B3EF8B1D75'
    $newInspectorHash = 'FBA167F5947E6722146272C9B17FE2220FDB6A1297AE764626725E62AF6BB7DD'
    $newJournalHash = 'A1513F12AFCD2932E4DE2D9A6069C7A2CED6F969BF1AC82E6942D6818F319E07'
    $newSelectorHash = 'A996B969394113B052010129563BF3EAE35443B0B452B0C66D0B7B69FF9D75B3'

    $Fixture.State.RunId = 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1'
    $Fixture.State.SourceCommit = '69a3c3d94f33d1903778ef5ec5b0a1f015b1be29'
    $Fixture.State.DisarmedBootId = '2026-10-20T22:45:43.5000000Z'
    $Fixture.CurrentHost.BootId = '2026-10-20T22:51:15.5000000Z'
    $Fixture.State.Backup.FirmwareEnumeration.Sha256 = $newBaselineHash
    $Fixture.State.Backup.FirmwareEnumeration.Bytes = 4336
    $Fixture.State.Backup.ReferenceLog.Sha256 = $newLogHash
    $Fixture.State.Tools.Probe.Sha256 = $newProbeHash
    $Fixture.State.FinalVerification.FirmwareEnumeration.BaselineSha256 = $newBaselineHash
    $Fixture.State.FinalVerification.FirmwareEnumeration.CurrentSha256 = $newCurrentHash
    $Fixture.CurrentFirmwarePin.Sha256 = $newCurrentHash
    $Fixture.CurrentFirmwarePin.Bytes = 2890
    $Fixture.Receipt | Add-Member -MemberType NoteProperty -Name Profile -Value '224018' -Force
    $Fixture.Receipt.RunId = $Fixture.State.RunId
    $Fixture.Receipt.CurrentHost.BootId = $Fixture.CurrentHost.BootId
    $Fixture.Receipt.PinsBefore.Selector.Sha256 = $newSelectorHash
    $Fixture.Receipt.PinsAfter.Selector.Sha256 = $newSelectorHash
    $Fixture.Receipt.PinsBefore.Journal.Sha256 = $newJournalHash
    $Fixture.Receipt.PinsBefore.Journal.Bytes = 45568
    $Fixture.Receipt.PinsAfter.Journal.Sha256 = $newJournalHash
    $Fixture.Receipt.PinsAfter.Journal.Bytes = 45568
    $Fixture.Receipt.PinsBefore.FirmwareBaseline.Sha256 = $newBaselineHash
    $Fixture.Receipt.PinsBefore.FirmwareBaseline.Bytes = 4336
    $Fixture.Receipt.PinsAfter.FirmwareBaseline.Sha256 = $newBaselineHash
    $Fixture.Receipt.PinsAfter.FirmwareBaseline.Bytes = 4336
    $Fixture.Receipt.PinsBefore.FirmwareCurrent.Sha256 = $newCurrentHash
    $Fixture.Receipt.PinsBefore.FirmwareCurrent.Bytes = 2890
    $Fixture.Receipt.PinsAfter.FirmwareCurrent.Sha256 = $newCurrentHash
    $Fixture.Receipt.PinsAfter.FirmwareCurrent.Bytes = 2890
    $Fixture.Receipt.PinsBefore.ReferenceLog.Sha256 = $newLogHash
    $Fixture.Receipt.PinsAfter.ReferenceLog.Sha256 = $newLogHash
    $Fixture.Receipt.PinsBefore.Probe.Sha256 = $newProbeHash
    $Fixture.Receipt.PinsAfter.Probe.Sha256 = $newProbeHash
    $Fixture.Receipt.PinsBefore.InspectorScript.Sha256 = $newInspectorHash
    $Fixture.Receipt.PinsBefore.InspectorScript.Bytes = 33353
    $Fixture.Receipt.PinsAfter.InspectorScript.Sha256 = $newInspectorHash
    $Fixture.Receipt.PinsAfter.InspectorScript.Bytes = 33353
    $Fixture.Receipt.PinsBefore.Modules.Platform.Sha256 = $newPlatformHash
    $Fixture.Receipt.PinsBefore.Modules.Platform.Bytes = 111525
    $Fixture.Receipt.PinsAfter.Modules.Platform.Sha256 = $newPlatformHash
    $Fixture.Receipt.PinsAfter.Modules.Platform.Bytes = 111525
    $Fixture.Receipt.PinsBefore.Modules.Retirement.Sha256 = $newRetirementHash
    $Fixture.Receipt.PinsBefore.Modules.Retirement.Bytes = 105056
    $Fixture.Receipt.PinsAfter.Modules.Retirement.Sha256 = $newRetirementHash
    $Fixture.Receipt.PinsAfter.Modules.Retirement.Bytes = 105056
    foreach ($pin in @($Fixture.Receipt.FirmwareObservations | ForEach-Object { $_.Pin })) {
        $pin.Sha256 = $newCurrentHash
        $pin.Bytes = 2890
    }
    foreach ($capture in $Fixture.Receipt.BootSelectionCaptures) {
        $capture.Baseline.BootOrder = [uint16[]]@(0, 1, 2)
        $capture.Baseline.ReferenceLogSha256 = $newLogHash
        $capture.BaselinePin.Sha256 = $newRawHash
        $capture.BaselinePin.Bytes = 253
        $capture.AuditToolPin.Sha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
        $capture.ReferenceLogPin.Sha256 = $newLogHash
    }
    $Fixture.Receipt.PinsAfter = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $Fixture.Receipt.PinsBefore -Depth 8 -Compress)
    $Fixture.Receipt.PinsAfter.Selector.Sha256 = $newSelectorHash
    $Fixture.Receipt.PinsAfter.Journal.Sha256 = $newJournalHash
    $Fixture.Receipt.PinsAfter.Journal.Bytes = 45568
    $Fixture.Receipt.PinsAfter.FirmwareBaseline.Sha256 = $newBaselineHash
    $Fixture.Receipt.PinsAfter.FirmwareBaseline.Bytes = 4336
    $Fixture.Receipt.PinsAfter.FirmwareCurrent.Sha256 = $newCurrentHash
    $Fixture.Receipt.PinsAfter.FirmwareCurrent.Bytes = 2890
    $Fixture.Receipt.PinsAfter.ReferenceLog.Sha256 = $newLogHash
    $Fixture.Receipt.PinsAfter.Probe.Sha256 = $newProbeHash
    $Fixture.Receipt.PinsAfter.InspectorScript.Sha256 = $newInspectorHash
    $Fixture.Receipt.PinsAfter.InspectorScript.Bytes = 33353
    $Fixture.Receipt.PinsAfter.Modules.Platform.Sha256 = $newPlatformHash
    $Fixture.Receipt.PinsAfter.Modules.Platform.Bytes = 111525
    $Fixture.Receipt.PinsAfter.Modules.Retirement.Sha256 = $newRetirementHash
    $Fixture.Receipt.PinsAfter.Modules.Retirement.Bytes = 105056
    $Fixture.Receipt.PinsBefore.Modules.Platform.Sha256 = $newPlatformHash
    $Fixture.Receipt.PinsBefore.Modules.Platform.Bytes = 111525
    $Fixture.Receipt.PinsBefore.Modules.Retirement.Sha256 = $newRetirementHash
    $Fixture.Receipt.PinsBefore.Modules.Retirement.Bytes = 105056
    $Fixture.Receipt.BootSelectionCaptures[0].Baseline.BootOrder = [uint16[]]@(0,1,2)
    $Fixture.Receipt.BootSelectionCaptures[1].Baseline.BootOrder = [uint16[]]@(0,1,2)
    return $Fixture
}

function New-224018CoreFixture {
    param([Parameter(Mandatory = $true)][string]$OriginalWorkspaceRoot)
    $tempRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('gpu-final-review-224018-' + [guid]::NewGuid().ToString('N'))))
    [void][IO.Directory]::CreateDirectory($tempRoot)
    $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $OriginalWorkspaceRoot) $OriginalWorkspaceRoot $tempRoot
    $fixtureFiles = @(
        (Join-Path $tempRoot '.analysis\gpu-baremetal\host-prepared.json'),
        (Join-Path $fixture.State.RunDirectory 'journal.json'),
        (Join-Path $tempRoot '.analysis\gpu-final-firmware-review\1dccd9d33d1b4c808fd1059291a6d19b\result.json'),
        (Join-Path $tempRoot '.codex-build\gpu-stage-retirement-audit\readonly-v1\ibaudit.exe'),
        (Join-Path $tempRoot '.codex-build\gb\a68d89b7b3ecb1a1\l\ib-tcg-dump.exe'),
        (Join-Path $fixture.State.RunDirectory 'actions\20261020T224257466Z-Backup-8bf99023efa4478e91e8fe626493da70\tcglog.ib'),
        (Join-Path $fixture.State.RunDirectory 'actions\20261020T224257466Z-Backup-8bf99023efa4478e91e8fe626493da70\firmware-enumeration-backup-99b1a3488d3d47ba93a25d46ae052d2d.stdout.txt'),
        (Join-Path $fixture.State.RunDirectory 'actions\20261020T225933250Z-FinalVerify-cf3ab7b1e838427c964640d87f30257e\firmware-enumeration-final-bd819c45c1e94e01b00b44dafe51c163.stdout.txt'),
        (Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1'),
        (Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'),
        (Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\GpuStageRetirement.psm1'),
        (Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1'),
        (Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\GpuFinalFirmwareReview.psm1')
    )
    foreach ($path in $fixtureFiles) {
        $parent = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($path))
        [void][IO.Directory]::CreateDirectory($parent)
        [IO.File]::WriteAllText($path, 'fixed 224018 review fixture')
    }
    [byte[]]$firmwareBytes = New-Object byte[] 2890
    for ($index = 0; $index -lt $firmwareBytes.Length; $index++) { $firmwareBytes[$index] = [byte](($index * 37 + 11) % 256) }
    $firmwareParent = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($fixture.CurrentFirmwarePin.Path))
    [void][IO.Directory]::CreateDirectory($firmwareParent)
    [IO.File]::WriteAllBytes($fixture.CurrentFirmwarePin.Path, $firmwareBytes)

    $stateModulePath = Join-Path $tempRoot 'fixture-state\TestGpuFinalReviewState.psm1'
    $stateModuleSource = @'
function Get-GpuFilePin {
    param([Parameter(Mandatory = $true)][string]$Path)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $leaf = [IO.Path]::GetFileName($fullPath)
    if ($leaf -ceq 'host-prepared.json') { return [pscustomobject]@{ Path=$fullPath; Sha256='A996B969394113B052010129563BF3EAE35443B0B452B0C66D0B7B69FF9D75B3'; Bytes=533L } }
    if ($leaf -ceq 'journal.json') { return [pscustomobject]@{ Path=$fullPath; Sha256='A1513F12AFCD2932E4DE2D9A6069C7A2CED6F969BF1AC82E6942D6818F319E07'; Bytes=45568L } }
    if ($leaf -ceq 'firmware-enumeration-final-fresh.stdout.txt' -or $leaf -ceq 'firmware-enumeration-fresh-fixture.txt') {
        return [pscustomobject]@{ Path=$fullPath; Sha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'; Bytes=2890L }
    }
    $item = Get-Item -LiteralPath $fullPath -ErrorAction Stop
    return [pscustomobject]@{ Path=$fullPath; Sha256=(Get-FileHash -LiteralPath $fullPath -Algorithm SHA256 -ErrorAction Stop).Hash; Bytes=[long]$item.Length }
}
function Assert-GpuPathWithin {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Root, [switch]$MustExist)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path is outside the requested fixture root.' }
    if ($MustExist -and -not (Test-Path -LiteralPath $fullPath)) { throw "Path does not exist: $fullPath" }
}
Export-ModuleMember -Function Get-GpuFilePin, Assert-GpuPathWithin
'@
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($stateModulePath))
    [IO.File]::WriteAllText($stateModulePath, $stateModuleSource)
    $stateModule = Import-Module -Name $stateModulePath -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    $platformPath = Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
    $helperPath = Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\GpuFinalFirmwareReview.psm1'
    $platformItem = Get-Item -LiteralPath $platformPath -ErrorAction Stop
    $helperItem = Get-Item -LiteralPath $helperPath -ErrorAction Stop
    $platformPin = New-Pin $platformPath (Get-FileHash -LiteralPath $platformPath -Algorithm SHA256).Hash $platformItem.Length
    $helperPin = New-Pin $helperPath (Get-FileHash -LiteralPath $helperPath -Algorithm SHA256).Hash $helperItem.Length
    $fixture | Add-Member -NotePropertyName TempRoot -NotePropertyValue $tempRoot -Force
    $fixture | Add-Member -NotePropertyName StateModule -NotePropertyValue $stateModule -Force
    $fixture | Add-Member -NotePropertyName PlatformPin -NotePropertyValue $platformPin -Force
    $fixture | Add-Member -NotePropertyName HelperPin -NotePropertyValue $helperPin -Force
    return $fixture
}

function Get-TestFilePin {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Description)
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    return [pscustomobject]@{
        Path=[IO.Path]::GetFullPath($Path)
        Sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash
        Bytes=[long]$item.Length
        Description=$Description
    }
}

function New-224018CoreOperations {
    param([Parameter(Mandatory = $true)][object]$Fixture, [switch]$MutatePlatformDuringCapture)
    $root = $Fixture.TempRoot
    $reviewResultPath = Join-Path $root '.analysis\gpu-final-firmware-review\1dccd9d33d1b4c808fd1059291a6d19b\result.json'
    $auditPath = Join-Path $root '.codex-build\gpu-stage-retirement-audit\readonly-v1\ibaudit.exe'
    $referencePath = Join-Path $Fixture.State.RunDirectory 'actions\20261020T224257466Z-Backup-8bf99023efa4478e91e8fe626493da70\tcglog.ib'
    $probePath = Join-Path $root '.codex-build\gb\a68d89b7b3ecb1a1\l\ib-tcg-dump.exe'
    $statePath = Join-Path $Fixture.State.SourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1'
    $retirementPath = Join-Path $Fixture.State.SourceRoot 'tools\gpu_diagnostic\GpuStageRetirement.psm1'
    $platformPath = $Fixture.PlatformPin.Path
    $helperPath = $Fixture.HelperPin.Path
    $immutablePins = @(
        (Get-TestFilePin $reviewResultPath 'Pinned successful firmware review receipt'),
        (Get-TestFilePin $auditPath 'Pinned read-only audit tool'),
        (Get-TestFilePin $referencePath 'Current-run reference TPM log'),
        (Get-TestFilePin $probePath 'Current-run health probe'),
        (Get-TestFilePin $statePath 'GPU host state module'),
        (Get-TestFilePin $retirementPath 'GPU stage-retirement module'),
        (Get-TestFilePin $helperPath 'Current GPU final firmware review helper'),
        (Get-TestFilePin $platformPath 'Current GPU host platform module')
    )
    $stateModule = $Fixture.StateModule
    $currentFirmwarePath = $Fixture.CurrentFirmwarePin.Path
    $firstBaseline = $Fixture.Receipt.BootSelectionCaptures[0].Baseline
    $secondBaseline = $Fixture.Receipt.BootSelectionCaptures[1].Baseline
    $platformPathForMutation = $platformPath
    $operations = @{
        Receipt=$Fixture.Receipt
        ReceiptPin=(Get-TestFilePin $reviewResultPath 'Pinned successful firmware review receipt')
        StateModule=$stateModule
        PlatformModule=$null
        HelperPin=$Fixture.HelperPin
        PlatformPin=$Fixture.PlatformPin
        ImmutablePins=$immutablePins
        AssertImmutablePin={
            param($pin)
            $actual = & $stateModule { param($path) Get-GpuFilePin -Path $path } ([string]$pin.Path)
            if ($actual.Sha256 -cne [string]$pin.Sha256 -or [long]$actual.Bytes -ne [long]$pin.Bytes) {
                throw "$($pin.Description) changed during final firmware review."
            }
        }.GetNewClosure()
        CaptureBootSelection={
            param($runState, $directory, $checkpoint)
            if ($checkpoint -eq 'before') { return [pscustomobject]@{ Baseline=$firstBaseline } }
            if ($checkpoint -eq 'after') { return [pscustomobject]@{ Baseline=$secondBaseline } }
            throw "Unexpected boot-selection checkpoint: $checkpoint"
        }.GetNewClosure()
        CaptureFirmware={
            param($directory)
            $path = Join-Path $directory 'firmware-enumeration-fresh-fixture.txt'
            Copy-Item -LiteralPath $currentFirmwarePath -Destination $path -ErrorAction Stop
            if ($MutatePlatformDuringCapture) { [IO.File]::AppendAllText($platformPathForMutation, "`n# drift injected by test") }
            $pin = & $stateModule { param($p) Get-GpuFilePin -Path $p } $path
            return [pscustomobject]@{ Text=[IO.File]::ReadAllText($path); Pin=$pin }
        }.GetNewClosure()
        AssertBootSelectionStable={
            param($before, $after)
            if ($null -ne $before.BootNext -or $null -ne $after.BootNext -or
                [string](@($before.BootOrder) -join ',') -cne [string](@($after.BootOrder) -join ',') -or
                [string]$before.IntegrityFindings -cne [string]$after.IntegrityFindings) {
                throw 'Fresh raw boot-selection captures changed during the review.'
            }
        }.GetNewClosure()
    }
    return $operations
}

function Remove-224018CoreFixture {
    param([AllowNull()][object]$Fixture)
    if ($null -eq $Fixture) { return }
    if ($null -ne $Fixture.StateModule) {
        Remove-Module -ModuleInfo $Fixture.StateModule -Force -ErrorAction SilentlyContinue
    }
    $tempRoot = [IO.Path]::GetFullPath([string]$Fixture.TempRoot)
    $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $tempRoot.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFileName($tempRoot).StartsWith('gpu-final-review-224018-', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove an out-of-scope firmware review fixture directory: $tempRoot"
    }
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction Stop
}

if (-not (Test-Path -LiteralPath $ModulePath -PathType Leaf)) { throw "Review module not found: $ModulePath" }
$module = Import-Module -Name $ModulePath -Force -PassThru -DisableNameChecking -ErrorAction Stop | Select-Object -First 1
$null = & $module {
    Set-Item -Path Function:Assert-GpuFinalReviewReceiptFiles -Value { param($StateModule, $Receipt, $Paths) }
    Set-Item -Path Function:Get-GpuFinalReviewMountedEsp -Value { param($PlatformModule, $ActionDirectory, $Backup) }
}
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..\..\..\..'))
$invokeShape = { param($f) & $module { param($receipt,$state,$host,$firmware,$action) Assert-GpuFinalFirmwareReviewReceiptShape -Receipt $receipt -State $state -CurrentHost $host -CurrentFirmwarePin $firmware -ActionDirectory $action } $f.Receipt $f.State $f.CurrentHost $f.CurrentFirmwarePin $f.ActionDirectory }

Invoke-TestCase 'fixed current-run review receipt passes strict evidence-shape checks' {
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $null = & $invokeShape $fixture
    Assert-True ($module.ExportedFunctions.ContainsKey('Invoke-GpuReviewedFinalFirmware')) 'Public review helper is not exported.'
    Assert-True ($module.ExportedFunctions.Count -eq 1) 'Internal review overrides leaked into the public module surface.'
}

Invoke-TestCase 'wrong run, source commit, host boot, or action directory is rejected' {
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.RunId='gpu-elsewhere'
    Assert-Throws { $null = & $invokeShape $fixture } 'run ID' 'Wrong receipt run was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.State.SourceCommit='different-commit'
    Assert-Throws { $null = & $invokeShape $fixture } 'source commit' 'Wrong source commit was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.CurrentHost.BootId='different-boot'
    Assert-Throws { $null = & $invokeShape $fixture } 'Current host boot identity' 'Wrong boot ID was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.ActionDirectory=Join-Path $workspaceRoot 'outside-action'
    Assert-Throws { $null = & $invokeShape $fixture } 'FinalVerify intent|action directory|outside' 'Outside action directory was accepted'
}

Invoke-TestCase 'recovery, cleanup, selector, or journal changes prevent acceptance' {
    foreach ($mutation in @(
        { param($f) $f.Receipt.Status='FAILED' },
        { param($f) $f.Receipt.RecoveryCompleted=$true },
        { param($f) $f.Receipt.CleanupSucceeded=$false },
        { param($f) $f.Receipt.SelectorAndJournalUnchanged=$false },
        { param($f) $f.Receipt.PinsAfter.Journal.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA' }
    )) {
        $fixture = New-ValidFinalReviewFixture $workspaceRoot
        & $mutation $fixture
        Assert-Throws { $null = & $invokeShape $fixture } 'receipt|Journal pin|clean|changed' 'Mutated receipt evidence was accepted'
    }
}

Invoke-TestCase 'firmware text, health result, and saved pin changes prevent acceptance' {
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.CurrentFirmwarePin.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    Assert-Throws { $null = & $invokeShape $fixture } 'Current firmware text pin' 'Wrong current firmware pin was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.State.FinalVerification.FirmwareEnumeration.BaselineSha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    Assert-Throws { $null = & $invokeShape $fixture } 'mounted-ESP firmware-text mismatch' 'Wrong baseline was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.Health.Verdict.ExpectedResult='PASS'
    Assert-Throws { $null = & $invokeShape $fixture } '20 ABSENT CPUs' 'Wrong saved health result was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.PinsAfter.FirmwareBaseline.Bytes=4337
    Assert-Throws { $null = & $invokeShape $fixture } 'After FirmwareBaseline pin' 'Changed immutable pin was accepted'
}

Invoke-TestCase 'raw BootNext, BootOrder, integrity findings, and evidence path are rejected' {
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.BootSelectionCaptures[0].Baseline.BootNext=4
    Assert-Throws { $null = & $invokeShape $fixture } 'BootSelection|BootNext|raw' 'Set BootNext was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.BootSelectionCaptures[1].Baseline.BootOrder[4]=7
    Assert-Throws { $null = & $invokeShape $fixture } 'BootOrder|raw' 'Changed BootOrder was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.BootSelectionCaptures[0].Baseline.IntegrityFindings='00000001'
    Assert-Throws { $null = & $invokeShape $fixture } 'findings' 'Audit finding was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.BootSelectionCaptures[0].BaselinePath=Join-Path $workspaceRoot 'outside.json'
    Assert-Throws { $null = & $invokeShape $fixture } 'outside its pinned inspection evidence|pin differs' 'Raw JSON outside evidence directory was accepted'
}

Invoke-TestCase '224018 exact profile accepts its separate baseline/current pins and three-entry raw order' {
    $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $workspaceRoot) $workspaceRoot $workspaceRoot
    & $module { param($runId) Select-GpuFinalFirmwareReviewProfile -RunId $runId } $fixture.State.RunId
    $null = & $invokeShape $fixture
    Assert-True ($fixture.State.Backup.FirmwareEnumeration.Bytes -eq 4336 -and $fixture.CurrentFirmwarePin.Bytes -eq 2890) 'Profile fixture collapsed distinct baseline/current lengths.'
    Assert-True (@($fixture.Receipt.BootSelectionCaptures[0].Baseline.BootOrder).Count -eq 3) '224018 raw order fixture is not the reviewed three-entry order.'
}

Invoke-TestCase '224018 rejects mismatched profile, run, pins, and current firmware lengths' {
    $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $workspaceRoot) $workspaceRoot $workspaceRoot
    $fixture.Receipt.Profile='201140'
    Assert-Throws { $null = & $invokeShape $fixture } 'does not identify the fixed 224018 profile' 'Wrong profile label was accepted'
    $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $workspaceRoot) $workspaceRoot $workspaceRoot
    $fixture.State.RunId='gpu-unreviewed'
    Assert-Throws { $null = & $invokeShape $fixture } 'Current run ID' 'Wrong state run was accepted'
    $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $workspaceRoot) $workspaceRoot $workspaceRoot
    $fixture.Receipt.RunId='gpu-unreviewed'
    Assert-Throws { $null = & $invokeShape $fixture } 'run ID' 'Wrong receipt run was accepted'
    $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $workspaceRoot) $workspaceRoot $workspaceRoot
    $fixture.Receipt.RunId=$fixture.Receipt.RunId.ToUpperInvariant()
    Assert-Throws { $null = & $invokeShape $fixture } 'exactly match the fixed 224018 run' 'Case-modified receipt run ID was accepted'
    $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $workspaceRoot) $workspaceRoot $workspaceRoot
    $fixture.Receipt.PinsAfter.FirmwareBaseline.Bytes=2890
    Assert-Throws { $null = & $invokeShape $fixture } 'After FirmwareBaseline pin' 'Wrong baseline length was accepted'
    $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $workspaceRoot) $workspaceRoot $workspaceRoot
    $fixture.CurrentFirmwarePin.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    Assert-Throws { $null = & $invokeShape $fixture } 'Current firmware text pin' 'Wrong current firmware hash was accepted'
    $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $workspaceRoot) $workspaceRoot $workspaceRoot
    $fixture.Receipt.FirmwareObservations[0].Pin.Bytes=4336
    Assert-Throws { $null = & $invokeShape $fixture } 'stable current capture' 'Wrong current firmware length was accepted'
    $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $workspaceRoot) $workspaceRoot $workspaceRoot
    $fixture.Receipt.Errors=@('review reported a failure')
    Assert-Throws { $null = & $invokeShape $fixture } 'receipt is incomplete, failed' 'A receipt with an extra error was accepted'
}

Invoke-TestCase '224018 rejects BootNext, changed order, findings, or source pin drift' {
    foreach ($mutation in @(
        { param($f) $f.Receipt.BootSelectionCaptures[0].Baseline.BootNext=2 },
        { param($f) $f.Receipt.BootSelectionCaptures[1].Baseline.BootOrder[2]=7 },
        { param($f) $f.Receipt.BootSelectionCaptures[0].Baseline.IntegrityFindings='00000001' },
        { param($f) $f.Receipt.PinsAfter.Modules.Platform.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA' }
    )) {
        $fixture = Set-224018FixtureProfile (New-ValidFinalReviewFixture $workspaceRoot) $workspaceRoot $workspaceRoot
        & $mutation $fixture
        Assert-Throws { $null = & $invokeShape $fixture } 'BootNext|BootOrder|findings|After Platform pin' 'Mutated raw/source evidence was accepted'
    }
}

Invoke-TestCase '224018 core accepts fresh read-only captures without native commands or ESP access' {
    $fixture = $null
    try {
        $fixture = New-224018CoreFixture $workspaceRoot
        & $module { param($runId) Select-GpuFinalFirmwareReviewProfile -RunId $runId } $fixture.State.RunId
        $operations = New-224018CoreOperations $fixture
        $result = & $module {
            param($state, $host, $firmware, $action, $ops)
            Invoke-GpuFinalFirmwareReviewCore -State $state -CurrentHost $host -CurrentFirmwarePin $firmware -ActionDirectory $action -Operations $ops
        } $fixture.State $fixture.CurrentHost $fixture.CurrentFirmwarePin $fixture.ActionDirectory $operations
        Assert-True ($result.Accepted -eq $true) 'The fixed 224018 core review was not accepted.'
        Assert-True (@($result.RawCaptures).Count -eq 2) 'The core review did not perform both fresh raw captures.'
        Assert-True ([string]$result.FreshFirmwarePin.Sha256 -ceq '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007' -and [long]$result.FreshFirmwarePin.Bytes -eq 2890) 'The fresh firmware pin differs from the reviewed 224018 capture.'
        Assert-True ($result.HistoricalRawBootOrderUnchangedProven -eq $false -and $result.FullBootVariableNamespaceAttested -eq $false) 'The core review overstated the raw firmware evidence scope.'
        Assert-True ([IO.Directory]::Exists((Join-Path $fixture.ActionDirectory 'fw-review'))) 'The fresh review evidence directory was not created.'
    }
    finally { Remove-224018CoreFixture $fixture }
}

Invoke-TestCase '224018 core rejects immutable source drift during fresh firmware review' {
    $fixture = $null
    try {
        $fixture = New-224018CoreFixture $workspaceRoot
        & $module { param($runId) Select-GpuFinalFirmwareReviewProfile -RunId $runId } $fixture.State.RunId
        $operations = New-224018CoreOperations $fixture -MutatePlatformDuringCapture
        Assert-Throws {
            & $module {
                param($state, $host, $firmware, $action, $ops)
                Invoke-GpuFinalFirmwareReviewCore -State $state -CurrentHost $host -CurrentFirmwarePin $firmware -ActionDirectory $action -Operations $ops
            } $fixture.State $fixture.CurrentHost $fixture.CurrentFirmwarePin $fixture.ActionDirectory $operations
        } 'GPU host platform module changed during final firmware review' 'The core review accepted immutable platform-source drift.'
    }
    finally { Remove-224018CoreFixture $fixture }
}

Invoke-TestCase '224018 profile binding rejects switching a module instance to a different run' {
    Assert-Throws { & $module { Select-GpuFinalFirmwareReviewProfile -RunId 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7' } } 'cannot switch between fixed run profiles' 'A bound module switched from 224018 to 201140'
}

$summary = [pscustomobject]@{ PassedCases=$script:PassedCases; FailedCases=$script:FailedCases.ToArray(); Assertions=$script:Assertions }
$summary | ConvertTo-Json -Depth 5
if ($script:FailedCases.Count -gt 0) { exit 1 }
