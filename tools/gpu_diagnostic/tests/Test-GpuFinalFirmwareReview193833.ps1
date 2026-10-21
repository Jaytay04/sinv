[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$ModulePath = Join-Path $ToolRoot 'GpuFinalFirmwareReview.psm1'
$SharedFixturePath = Join-Path $PSScriptRoot 'Test-GpuFinalFirmwareReview.ps1'
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
function Set-193833ReceiptPins {
    param([Parameter(Mandatory = $true)][object]$Fixture)

    foreach ($pins in @($Fixture.Receipt.PinsBefore, $Fixture.Receipt.PinsAfter)) {
        if ($null -eq $pins.PSObject.Properties['FinalResult']) {
            $pins | Add-Member -MemberType NoteProperty -Name FinalResult -Value $null
        }
        $pins.Selector.Sha256 = 'E0611ECB3A3F1DC94A064D40783E68A495C5FFED761BE01626C4BC95E63916A3'
        $pins.Journal.Sha256 = $Fixture.EntryJournalPin.Sha256
        $pins.Journal.Bytes = $Fixture.EntryJournalPin.Bytes
        $pins.FirmwareBaseline.Sha256 = '98E5ECE5B987B754CD1C059C34E9D87A1CFB569E758BCC21971CA21D4EDC2D80'
        $pins.FirmwareBaseline.Bytes = 4336
        $pins.FirmwareCurrent.Sha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
        $pins.FirmwareCurrent.Bytes = 2890
        $pins.ReferenceLog.Sha256 = 'F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8'
        $pins.ReferenceLog.Bytes = 40838
        $pins.Probe.Sha256 = 'F2CACE0FA3BF07140593B64E48529C6CA5AF0EA036E4B500F2F98C3046212333'
        $pins.Probe.Bytes = 172544
        $pins.InspectorScript.Sha256 = '0F127D49BC45C8F597CD0AE0B7D1CF2016E5FAE41509EBB276779483C87A0E75'
        $pins.InspectorScript.Bytes = 46304
        $pins.Modules.State.Sha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
        $pins.Modules.State.Bytes = 19843
        $pins.Modules.Platform.Sha256 = '66A3B826C01A3F3A3D962DB5798C81FA0FE758325BC0734D93778691409125B2'
        $pins.Modules.Platform.Bytes = 133220
        $pins.Modules.Retirement.Sha256 = '24CDEFAE98EFFCFF664CB269E3642C35E4920EB7560F1492010C766486143E59'
        $pins.Modules.Retirement.Bytes = 372336
        $pins.FinalResult = New-Pin $Fixture.FailedFinalResultPath $Fixture.FailedFinalResultPin.Sha256 $Fixture.FailedFinalResultPin.Bytes
    }
    foreach ($pin in @($Fixture.Receipt.FirmwareObservations | ForEach-Object { $_.Pin })) {
        $pin.Sha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
        $pin.Bytes = 2890
    }
    foreach ($capture in $Fixture.Receipt.BootSelectionCaptures) {
        $capture.Baseline.ReferenceLogSha256 = 'F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8'
        $capture.BaselinePin.Sha256 = '450434A4447DC11F57FBC9859C5369960DBAA7D42B1700CAA8BC74B849099B9F'
        $capture.BaselinePin.Bytes = 253
        $capture.AuditToolPin.Sha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
        $capture.AuditToolPin.Bytes = 246784
        $capture.ReferenceLogPin.Sha256 = 'F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8'
        $capture.ReferenceLogPin.Bytes = 40838
    }
    $Fixture.Receipt.PinsAfter = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $Fixture.Receipt.PinsBefore -Depth 12 -Compress)
}

function New-193833Fixture {
    param([string]$OriginalWorkspaceRoot)

    $tempRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('gpu-final-review-193833-' + [guid]::NewGuid().ToString('N'))))
    [void][IO.Directory]::CreateDirectory($tempRoot)
    $fixture = New-ValidFinalReviewFixture $OriginalWorkspaceRoot
    $runId = 'gpu-20261021T193833Z-c48461667089493988074edfaf008d56'
    $replacements = [ordered]@{
        $OriginalWorkspaceRoot=$tempRoot
        'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'=$runId
        '8abb7b84ca644a7c84019a1f29b35bb4'='8b8a27d744ae4fcd806c4017156da6af'
        '20261020T201533358Z-Backup-479329a8e70a4124964be17cc597ad61'='20261021T194754121Z-Backup-ba3d1ef07fe64145916eea9506cca303'
        '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'='20261021T200510255Z-FinalVerify-38149ac12747467cbb64e46552b7abae'
        'firmware-enumeration-backup-4196e13c22604293a54688532c15cff6.stdout.txt'='firmware-enumeration-backup-890dee938db04defbe3b879ede88023a.stdout.txt'
        'firmware-enumeration-final-7a5347fd0bc4452eb06340ba1e614a92.stdout.txt'='firmware-enumeration-final-6a3e5467babe4872aec6689e6c95f51b.stdout.txt'
        'stage-retirement-firmware-3ba00749b6d94f82a0ef932ab7fb214d.stdout.txt'='stage-retirement-firmware-17db98e0171b4c938bf1199b734a248f.stdout.txt'
        'stage-retirement-firmware-1f63298289ce49ed81012994adc954ba.stdout.txt'='stage-retirement-firmware-fd8f201a2d5c4bfaad282e8903113ad6.stdout.txt'
        '933b4c676318f26396cdf2c66de01c3ccbf1a950'='15842e7d789eddaea401754734ce9540a10b69e7'
        '2026-10-20T20:19:07.5000000Z'='2026-10-21T19:51:47.5000000Z'
        '2026-10-20T20:48:51.5000000Z'='2026-10-21T20:03:49.5000000Z'
        '631E2811091F4085445170A500985A30A296762EA15EEC0FD70C70422C55101D'='557AB91F1760F4FEA7EE4E54135E6E0B7B45B0ADA16CE91D09A2B508ADA44E18'
        'A7416148825A6ABC3603681C45F6A8277AFEE432D4B503BDEC89D34F58D34FFB'='E0611ECB3A3F1DC94A064D40783E68A495C5FFED761BE01626C4BC95E63916A3'
        'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE'='98E5ECE5B987B754CD1C059C34E9D87A1CFB569E758BCC21971CA21D4EDC2D80'
        '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
        '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'='F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8'
        'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47'='F2CACE0FA3BF07140593B64E48529C6CA5AF0EA036E4B500F2F98C3046212333'
        'BEA6DCA1FC904B13830908471D6239A906F71AA6937630B32CBA6DC3C2C023D9'='82B73A584E7DF37F223D0E5F9B8E5E69B4E68A854F1905E17F9E9AA66B1346EF'
        'B5D4EB51A58A2170EBCA905749E2BDAD177CFD71629A1079A14934F3F5BF63D0'='450434A4447DC11F57FBC9859C5369960DBAA7D42B1700CAA8BC74B849099B9F'
        '9EFE53FCC2CEA79BF0F2DF7E8E550AE122D381259AB7C176D7FA64A76A7A6275'='0F127D49BC45C8F597CD0AE0B7D1CF2016E5FAE41509EBB276779483C87A0E75'
        '4816FC0E640D7AEB38DF301E4D53A4982D5CABAA780650FEA8BDEE5D7C416899'='66A3B826C01A3F3A3D962DB5798C81FA0FE758325BC0734D93778691409125B2'
        'CCFA78B48A18AB90AE865ADEB14CA952D927955523B9E72F0C30A762F4FDB553'='24CDEFAE98EFFCFF664CB269E3642C35E4920EB7560F1492010C766486143E59'
    }
    $fixture = Update-FixtureStrings $fixture $replacements
    # Rewrite the probe build ID only after the full old run ID is gone; that
    # build ID is a suffix of the old run ID and an unordered hashtable could
    # otherwise rewrite it first and prevent the exact run-ID substitution.
    $fixture = Update-FixtureStrings $fixture ([ordered]@{ '8be3274dfe56cec7'='88074edfaf008d56' })
    $fixture | Add-Member -NotePropertyName TempRoot -NotePropertyValue $tempRoot -Force

    $fixture.State.RunId = $runId
    $fixture.State.SourceCommit = '15842e7d789eddaea401754734ce9540a10b69e7'
    $fixture.State.DisarmedBootId = '2026-10-21T19:51:47.5000000Z'
    $fixture.CurrentHost.BootId = '2026-10-21T20:03:49.5000000Z'
    $fixture.Receipt | Add-Member -MemberType NoteProperty -Name Profile -Value '193833' -Force
    $fixture.Receipt.RunId = $runId
    $fixture.Receipt.CurrentHost.BootId = '2026-10-21T20:03:49.5000000Z'
    if ($null -eq $fixture.Receipt.Health.Verdict.PSObject.Properties['CpuPassCount']) {
        $fixture.Receipt.Health.Verdict | Add-Member -MemberType NoteProperty -Name CpuPassCount -Value 0
    } else { $fixture.Receipt.Health.Verdict.CpuPassCount = 0 }
    if ($null -eq $fixture.Receipt.Health.Verdict.PSObject.Properties['Reason']) {
        $fixture.Receipt.Health.Verdict | Add-Member -MemberType NoteProperty -Name Reason -Value 'PASS'
    } else { $fixture.Receipt.Health.Verdict.Reason = 'PASS' }
    $fixture.Receipt.Esp.Volume = '\\?\Volume{187bc913-7576-4e6e-8d54-18eeb5c38fcd}\'
    $fixture.Receipt.Esp.DiskNumber = 0
    $fixture.Receipt.Esp.PartitionGuid = '187BC913-7576-4E6E-8D54-18EEB5C38FCD'
    $fixture.State.Backup.EspVolume = $fixture.Receipt.Esp.Volume
    $fixture.State.Backup.EspPartitionGuid = $fixture.Receipt.Esp.PartitionGuid
    $fixture.Receipt.BootSelectionCaptures[0].Baseline.BootOrder = [uint16[]]@(0,1,2)
    $fixture.Receipt.BootSelectionCaptures[1].Baseline.BootOrder = [uint16[]]@(0,1,2)

    $currentAction = Join-Path (Join-Path $fixture.State.RunDirectory 'actions') '20261021T071000000Z-FinalVerify-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
    $fixture.ActionDirectory = $currentAction
    $fixture.State.InProgress.Directory = $currentAction
    $fixture.CurrentFirmwarePin = New-Pin (Join-Path $currentAction 'firmware-enumeration-final-fresh.stdout.txt') '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007' 2890
    $fixture.State.FinalVerification = [pscustomobject]@{
        BootId='2026-10-21T20:03:49.5000000Z'; SecureBoot=$true; ResetBoundaryPassed=$true; ActivationAttempted=$true
        Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; ExpectedCpus=20; CpuCount=20; CpuPassCount=0; Aggregate='ABSENT'; Reason='PASS' }
        FirmwareEnumeration=[pscustomobject]@{ TextEqual=$false; BaselineSha256='98E5ECE5B987B754CD1C059C34E9D87A1CFB569E758BCC21971CA21D4EDC2D80'; CurrentSha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'; CaptureContext='SystemEspMountedAtS' }
        ActiveGpuProof=$false; GpuAttestation='UNATTESTED'; Errors=@('Firmware enumeration differs from the saved textual baseline.')
    }
    $sourcePins = @(
        (New-Pin (Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\gpu-host-test.ps1') 'BAF051E7021F74DC84E858C068A67C8BB6515E92FB3814E8AC9A3CF378DD0681' 22842),
        (New-Pin (Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\gpu-host-rollback.ps1') '609493557319303F9192D984C623B40814AFD937F45EBC63C5AC08FD060A98FF' 509),
        (New-Pin (Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1') '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC' 19843),
        (New-Pin (Join-Path $fixture.State.SourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1') '43AA4F580F6D896068D8D4DE4139A509CC19254647D400DA5803115E5957D7FE' 111899)
    )
    $fixture.State | Add-Member -MemberType NoteProperty -Name SourcePins -Value $sourcePins -Force

    $oldAction = Join-Path (Join-Path $fixture.State.RunDirectory 'actions') '20261021T200510255Z-FinalVerify-38149ac12747467cbb64e46552b7abae'
    $fixture.FailedFinalResultPath = Join-Path $oldAction 'result.json'
    $fixture.EntryJournalPath = Join-Path $currentAction 'reviewed-firmware-entry-journal.json'
    $finalVerification = [pscustomobject]@{
        BootId='2026-10-21T20:03:49.5000000Z'; SecureBoot=$true; ResetBoundaryPassed=$true; ActivationAttempted=$true
        Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; ExpectedCpus=20; CpuCount=20; CpuPassCount=0; Aggregate='ABSENT'; Reason='PASS' }
        FirmwareEnumeration=[pscustomobject]@{ TextEqual=$false; BaselineSha256='98E5ECE5B987B754CD1C059C34E9D87A1CFB569E758BCC21971CA21D4EDC2D80'; CurrentSha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'; CaptureContext='SystemEspMountedAtS' }
        Errors=@('Firmware enumeration differs from the saved textual baseline.')
    }
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$runId; WorkspaceRoot=$fixture.State.WorkspaceRoot; SourceRoot=$fixture.State.SourceRoot
        SourceCommit='15842e7d789eddaea401754734ce9540a10b69e7'; RunDirectory=$fixture.State.RunDirectory
        Phase='Disarmed'; DisarmedBootId='2026-10-21T19:51:47.5000000Z'; EverArmIntent=$true
        InProgress=[pscustomobject]@{ Action='FinalVerify'; Directory=$oldAction }
        LastFailure='FINAL_VERIFY_FAILED_GPU_UNATTESTED'
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Status='FINAL_VERIFY_FAILED_GPU_UNATTESTED'; Succeeded=$false; Directory=$oldAction }
        FinalVerification=$finalVerification
    }
    $failedResult = [pscustomobject]@{
        Action='FinalVerify'; Status='FINAL_VERIFY_FAILED_GPU_UNATTESTED'; Succeeded=$false; Phase='Disarmed'
        RunDirectory=$fixture.State.RunDirectory; ActionDirectory=$oldAction
        Details=[pscustomobject]@{ Status='FINAL_VERIFY_FAILED_GPU_UNATTESTED'; Succeeded=$false; FinalVerification=$finalVerification }
    }
    foreach ($file in @(@($fixture.EntryJournalPath, $journal), @($fixture.FailedFinalResultPath, $failedResult))) {
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([string]$file[0]))
        [IO.File]::WriteAllText([string]$file[0], (ConvertTo-Json -InputObject $file[1] -Depth 16 -Compress), [Text.UTF8Encoding]::new($false))
    }
    $fixture.EntryJournalPin = New-Pin $fixture.EntryJournalPath (Get-FileHash -LiteralPath $fixture.EntryJournalPath -Algorithm SHA256).Hash (Get-Item $fixture.EntryJournalPath).Length
    $fixture.FailedFinalResultPin = New-Pin $fixture.FailedFinalResultPath (Get-FileHash -LiteralPath $fixture.FailedFinalResultPath -Algorithm SHA256).Hash (Get-Item $fixture.FailedFinalResultPath).Length
    & $module {
        param($journalHash, $journalBytes, $resultHash, $resultBytes)
        $script:GpuFinalReviewJournalSha256 = $journalHash
        $script:GpuFinalReviewJournalBytes = [long]$journalBytes
        $script:GpuFinalReviewFinalResultSha256 = $resultHash
        $script:GpuFinalReviewFinalResultBytes = [long]$resultBytes
    } $fixture.EntryJournalPin.Sha256 $fixture.EntryJournalPin.Bytes $fixture.FailedFinalResultPin.Sha256 $fixture.FailedFinalResultPin.Bytes
    Set-193833ReceiptPins $fixture
    return $fixture
}

function Remove-193833Fixture {
    param([AllowNull()][object]$Fixture)
    if ($null -eq $Fixture) { return }
    $root = [IO.Path]::GetFullPath([string]$Fixture.TempRoot)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $root.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFileName($root).StartsWith('gpu-final-review-193833-', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove an out-of-scope firmware review fixture directory: $root"
    }
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction Stop
}

if (-not (Test-Path -LiteralPath $ModulePath -PathType Leaf)) { throw "Review module not found: $ModulePath" }
if (-not (Test-Path -LiteralPath $SharedFixturePath -PathType Leaf)) { throw "Shared review fixture source not found: $SharedFixturePath" }
$sharedAst = [System.Management.Automation.Language.Parser]::ParseFile($SharedFixturePath, [ref]$null, [ref]$null)
foreach ($functionName in @('New-ValidFinalReviewFixture', 'Update-FixtureStrings')) {
    $definition = $sharedAst.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $functionName }, $true) | Select-Object -First 1
    if ($null -eq $definition) { throw "Shared fixture helper '$functionName' is missing." }
    . ([scriptblock]::Create($definition.Extent.Text))
}

$unknownModule = Import-Module -Name $ModulePath -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
Assert-Throws { & $unknownModule { Select-GpuFinalFirmwareReviewProfile -RunId 'gpu-20261021T193834Z-unreviewed' } } 'four fixed reviewed run profiles' 'An unknown run was accepted.'
Remove-Module -ModuleInfo $unknownModule -Force
$module = Import-Module -Name $ModulePath -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
& $module { param($runId) Select-GpuFinalFirmwareReviewProfile -RunId $runId } 'gpu-20261021T193833Z-c48461667089493988074edfaf008d56'
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..\..\..\..'))
$invokeShape = { param($f) & $module { param($receipt,$state,$host,$firmware,$action) Assert-GpuFinalFirmwareReviewReceiptShape -Receipt $receipt -State $state -CurrentHost $host -CurrentFirmwarePin $firmware -ActionDirectory $action } $f.Receipt $f.State $f.CurrentHost $f.CurrentFirmwarePin $f.ActionDirectory }

Invoke-TestCase '193833 selects exact pins and separates historical from current platform source' {
    $profile = & $module { [pscustomobject]@{
        RunId=$script:GpuFinalReviewRunId; Profile=$script:GpuFinalReviewProfileId; SourceCommit=$script:GpuFinalReviewSourceCommit
        Disarmed=$script:GpuFinalReviewDisarmedBootId; Current=$script:GpuFinalReviewCurrentBootId
        Journal=$script:GpuFinalReviewJournalSha256; JournalBytes=$script:GpuFinalReviewJournalBytes
        Review=$script:GpuFinalReviewAuditResultSha256; ReviewBytes=$script:GpuFinalReviewAuditResultBytes
        FinalResult=$script:GpuFinalReviewFinalResultSha256; FinalResultBytes=$script:GpuFinalReviewFinalResultBytes
        Inspector=$script:GpuFinalReviewInspectorSha256; InspectorBytes=$script:GpuFinalReviewInspectorBytes
        PlatformHistorical=$script:GpuFinalReviewPlatformHistoricalSha256; PlatformHistoricalBytes=$script:GpuFinalReviewPlatformHistoricalBytes
        PlatformCurrent=$script:GpuFinalReviewPlatformCurrentSha256; PlatformCurrentBytes=$script:GpuFinalReviewPlatformCurrentBytes
        Retirement=$script:GpuFinalReviewRetirementSha256; RetirementBytes=$script:GpuFinalReviewRetirementBytes
        HealthStdout=$script:GpuFinalReviewHealthStdoutSha256; HealthStdoutBytes=$script:GpuFinalReviewHealthStdoutBytes
        HealthStderr=$script:GpuFinalReviewHealthStderrSha256; HealthStderrBytes=$script:GpuFinalReviewHealthStderrBytes
        Raw=$script:GpuFinalReviewRawSha256; RawBytes=$script:GpuFinalReviewRawBytes
        ReferenceLog=$script:GpuFinalReviewReferenceLogSha256; Probe=$script:GpuFinalReviewProbeSha256
        ExpectedBootOrder=@($script:GpuFinalReviewExpectedBootOrder)
    } }
    Assert-True ($profile.RunId -ceq 'gpu-20261021T193833Z-c48461667089493988074edfaf008d56' -and $profile.Profile -ceq '193833') 'The fixed 193833 profile was not selected.'
    Assert-True ($profile.SourceCommit -ceq '15842e7d789eddaea401754734ce9540a10b69e7') 'The reviewed source commit changed.'
    Assert-True ($profile.Disarmed -ceq '2026-10-21T19:51:47.5000000Z' -and $profile.Current -ceq '2026-10-21T20:03:49.5000000Z') 'The reviewed boot IDs changed.'
    Assert-True ($profile.Journal -ceq '557AB91F1760F4FEA7EE4E54135E6E0B7B45B0ADA16CE91D09A2B508ADA44E18' -and $profile.JournalBytes -eq 45269) 'The immutable journal pin changed.'
    Assert-True ($profile.Review -ceq '82B73A584E7DF37F223D0E5F9B8E5E69B4E68A854F1905E17F9E9AA66B1346EF' -and $profile.ReviewBytes -eq 16103) 'The successful inspector receipt pin changed.'
    Assert-True ($profile.FinalResult -ceq '23B01D517A650AE90D58180086E2BC9D2BB06899476E29F740FDE3304CCBED35' -and $profile.FinalResultBytes -eq 2285) 'The failed FinalVerify result pin changed.'
    Assert-True ($profile.Inspector -ceq '0F127D49BC45C8F597CD0AE0B7D1CF2016E5FAE41509EBB276779483C87A0E75' -and $profile.InspectorBytes -eq 46304) 'The updated inspector source pin changed.'
    Assert-True ($profile.PlatformHistorical -ceq '66A3B826C01A3F3A3D962DB5798C81FA0FE758325BC0734D93778691409125B2' -and $profile.PlatformHistoricalBytes -eq 133220) 'The historical platform pin changed.'
    Assert-True ($profile.PlatformCurrent -ceq '0881A2EBE0C292107DDD7D83E165830FEBE617A31B0CA1A8FF731DB723EDFC4C' -and $profile.PlatformCurrentBytes -eq 133298) 'The current platform source pin changed.'
    Assert-True ($profile.PlatformHistorical -cne $profile.PlatformCurrent) 'Historical receipt evidence was conflated with the current executable platform module.'
    Assert-True ($profile.Retirement -ceq '24CDEFAE98EFFCFF664CB269E3642C35E4920EB7560F1492010C766486143E59' -and $profile.RetirementBytes -eq 372336) 'The historical retirement module pin changed.'
    Assert-True ($profile.HealthStdout -ceq 'D7CD426CF81128F690B80686D1C8FCF3FC03FB4BFADD4A8A612359E769153EA9' -and $profile.HealthStdoutBytes -eq 1698 -and $profile.HealthStderr -ceq 'E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855' -and $profile.HealthStderrBytes -eq 0) 'The actual health receipt output pins changed.'
    Assert-True ($profile.Raw -ceq '450434A4447DC11F57FBC9859C5369960DBAA7D42B1700CAA8BC74B849099B9F' -and $profile.RawBytes -eq 253 -and [string](@($profile.ExpectedBootOrder) -join ',') -ceq '0,1,2') 'The raw boot-selection evidence changed.'
    Assert-True ($profile.ReferenceLog -ceq 'F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8' -and $profile.Probe -ceq 'F2CACE0FA3BF07140593B64E48529C6CA5AF0EA036E4B500F2F98C3046212333') 'The dependency pins changed.'

    $pin = New-Pin (Join-Path $workspaceRoot 'current-platform-fixture.psm1') $profile.PlatformCurrent $profile.PlatformCurrentBytes
    & $module { param($value) Assert-GpuFinalReviewCurrentPlatformPin -Pin $value } $pin
    $pin.Sha256 = $profile.PlatformHistorical
    $pin.Bytes = $profile.PlatformHistoricalBytes
    Assert-Throws { & $module { param($value) Assert-GpuFinalReviewCurrentPlatformPin -Pin $value } $pin } 'Current reviewed GPU host platform module pin differs' 'Historical platform source was accepted as current executable code.'
}

Invoke-TestCase '193833 accepts the closed receipt, failed result, entry proof, and historical source pins' {
    $fixture = New-193833Fixture $workspaceRoot
    try {
        $root = & $invokeShape $fixture
        Assert-True ($root.EntryJournalPath -ceq [IO.Path]::GetFullPath($fixture.EntryJournalPath)) 'The immutable action-local entry journal was not returned.'
        Assert-True ($root.FinalResultPath -ceq [IO.Path]::GetFullPath($fixture.FailedFinalResultPath)) 'The prior failed FinalVerify result was not pinned.'
        Assert-True ($fixture.Receipt.PinsBefore.FinalResult.Sha256 -ceq $fixture.Receipt.PinsAfter.FinalResult.Sha256) 'Failed-result before/after pins differ.'
        Assert-True ($fixture.Receipt.PinsBefore.Modules.Platform.Sha256 -ceq '66A3B826C01A3F3A3D962DB5798C81FA0FE758325BC0734D93778691409125B2') 'The receipt lost its historical platform pin.'
        Assert-True ($fixture.State.SourcePins[3].Sha256 -ceq '43AA4F580F6D896068D8D4DE4139A509CC19254647D400DA5803115E5957D7FE' -and $fixture.State.SourcePins[3].Bytes -eq 111899 -and $fixture.State.SourcePins[3].Sha256 -cne $fixture.Receipt.PinsBefore.Modules.Platform.Sha256) 'The original journal platform source pin was conflated with the receipt historical platform pin.'
        $currentPlatformHash = & $module { $script:GpuFinalReviewPlatformCurrentSha256 }
        Assert-True ($currentPlatformHash -cne $fixture.Receipt.PinsBefore.Modules.Platform.Sha256) 'The current platform hash was incorrectly required to match historical receipt module pins.'
    }
    finally { Remove-193833Fixture $fixture }
}

Invoke-TestCase '193833 rejects unrelated identities and unstable evidence copies' {
    foreach ($mutation in @(
        { param($f) $f.State.RunId = 'gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942' },
        { param($f) $f.Receipt.RunId = 'gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942' },
        { param($f) $f.Receipt.Profile = '062106' },
        { param($f) $f.Receipt.HistoricalRawBootOrderUnchangedProven = $true },
        { param($f) $f.Receipt.FirmwareTextStable = $false },
        { param($f) $f.Receipt.RawBootSelectionStable = $false },
        { param($f) $f.Receipt.PinsAfter.Journal.Sha256 = 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA' },
        { param($f) $f.Receipt.PinsAfter.FinalResult.Bytes++ },
        { param($f) $f.Receipt.BootSelectionCaptures[0].Baseline.BootNext = 4 },
        { param($f) $f.Receipt.BootSelectionCaptures[0].Baseline.BootOrder[0] = 5 }
    )) {
        $fixture = New-193833Fixture $workspaceRoot
        try {
            & $mutation $fixture
            Assert-Throws { $null = & $invokeShape $fixture } 'fixed|reviewed|historical|stable|pin|BootOrder|run ID|profile|incomplete' 'Unrelated or unstable 193833 evidence was accepted.'
        }
        finally { Remove-193833Fixture $fixture }
    }
}

Invoke-TestCase '193833 rejects cleanup, health, and dependency drift' {
    foreach ($mutation in @(
        { param($f) $f.Receipt.CleanupSucceeded=$false },
        { param($f) $f.Receipt.EspUnmounted=$false },
        { param($f) $f.Receipt.FirmwareWritten=$true },
        { param($f) $f.Receipt.CleanupErrors=@('unmount failed') },
        { param($f) $f.Receipt.Health.Verdict.Passed=$false },
        { param($f) $f.Receipt.Health.Verdict.CpuPassCount=1 },
        { param($f) $f.State.FinalVerification.Health.Passed=$false },
        { param($f) $f.Receipt.PinsAfter.Modules.Platform.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA' },
        { param($f) $f.Receipt.PinsBefore.Modules.Retirement.Bytes++ },
        { param($f) $f.Receipt.PinsAfter.InspectorScript.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA' },
        { param($f) $f.Receipt.PinsBefore.Probe.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA' }
    )) {
        $fixture = New-193833Fixture $workspaceRoot
        try {
            & $mutation $fixture
            Assert-Throws { $null = & $invokeShape $fixture } 'incomplete|health|Health|pin|evidence|Current FinalVerify|FinalVerify result|entry journal' 'Mutated closed evidence was accepted.'
        }
        finally { Remove-193833Fixture $fixture }
    }
}

Invoke-TestCase '193833 entry-journal proof rejects changed bytes and refreshed semantic drift' {
    $fixture = New-193833Fixture $workspaceRoot
    try {
        [IO.File]::AppendAllText($fixture.EntryJournalPath, ' ')
        Assert-Throws { $null = & $invokeShape $fixture } 'entry-journal copy|reviewed pre-retry journal' 'Changed entry-journal bytes were accepted.'
    }
    finally { Remove-193833Fixture $fixture }

    $fixture = New-193833Fixture $workspaceRoot
    try {
        $journal = Get-Content -LiteralPath $fixture.EntryJournalPath -Raw | ConvertFrom-Json
        $journal.DisarmedBootId = '2026-10-21T19:51:48.5000000Z'
        [IO.File]::WriteAllText($fixture.EntryJournalPath, (ConvertTo-Json -InputObject $journal -Depth 16 -Compress), [Text.UTF8Encoding]::new($false))
        $changedHash = (Get-FileHash -LiteralPath $fixture.EntryJournalPath -Algorithm SHA256).Hash
        $changedBytes = (Get-Item -LiteralPath $fixture.EntryJournalPath).Length
        & $module { param($hash,$bytes) $script:GpuFinalReviewJournalSha256=$hash; $script:GpuFinalReviewJournalBytes=[long]$bytes } $changedHash $changedBytes
        foreach ($pins in @($fixture.Receipt.PinsBefore, $fixture.Receipt.PinsAfter)) {
            $pins.Journal.Sha256 = $changedHash
            $pins.Journal.Bytes = $changedBytes
        }
        Assert-Throws { $null = & $invokeShape $fixture } 'pre-retry Disarmed context' 'Semantically changed entry-journal context was accepted.'
    }
    finally { Remove-193833Fixture $fixture }
}

$summary = [pscustomobject]@{ PassedCases=$script:PassedCases; FailedCases=$script:FailedCases.ToArray(); Assertions=$script:Assertions }
$summary | ConvertTo-Json -Depth 5
if ($script:FailedCases.Count -gt 0) { exit 1 }
