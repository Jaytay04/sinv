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
function Set-ReviewTestPins {
    param([object]$Fixture)
    $before = $Fixture.Receipt.PinsBefore
    $after = $Fixture.Receipt.PinsAfter
    foreach ($pins in @($before, $after)) {
        if ($null -eq $pins.PSObject.Properties['FinalResult']) {
            $pins | Add-Member -MemberType NoteProperty -Name FinalResult -Value $null
        }
    }
    foreach ($pins in @($before, $after)) {
        $pins.Selector.Sha256 = 'B4CF6844894A920912EFF85B80012ED3B0B5504D7BD01341A531316C8FC8424C'
        $pins.Journal.Sha256 = $Fixture.EntryJournalPin.Sha256
        $pins.Journal.Bytes = $Fixture.EntryJournalPin.Bytes
        $pins.FirmwareBaseline.Sha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
        $pins.FirmwareBaseline.Bytes = 2890
        $pins.FirmwareCurrent.Sha256 = '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'
        $pins.FirmwareCurrent.Bytes = 4336
        $pins.ReferenceLog.Sha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
        $pins.Probe.Sha256 = '86D15C1E8B29A7896FC8D4A9AA14ECD926DA23D8EED2871DBA65DDDA7002BD36'
        $pins.InspectorScript.Sha256 = '518701A9CA9059F01B700A084736B59A9F66A382221EBD96BF436C5E67A34242'
        $pins.InspectorScript.Bytes = 37603
        $pins.Modules.Platform.Sha256 = 'D30A1A38BF5B9CAD07F43C237599847A8D5ECF0825D7D3552A92F599801C543F'
        $pins.Modules.Platform.Bytes = 111625
        $pins.Modules.Retirement.Sha256 = '395101C30399FD96C2404BAA049054A287375060D95B7D9773FE2147647E563D'
        $pins.Modules.Retirement.Bytes = 183814
        $pins.FinalResult = New-Pin $Fixture.FailedFinalResultPath $Fixture.FailedFinalResultPin.Sha256 $Fixture.FailedFinalResultPin.Bytes
    }
    foreach ($pin in @($Fixture.Receipt.FirmwareObservations | ForEach-Object { $_.Pin })) {
        $pin.Sha256 = '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'
        $pin.Bytes = 4336
    }
    foreach ($capture in $Fixture.Receipt.BootSelectionCaptures) {
        $capture.Baseline.ReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
        $capture.BaselinePin.Sha256 = '69733AFE9DD782577F302F79A788DF2745BB9C9CD4BE860DF44C59A07EB4E066'
        $capture.BaselinePin.Bytes = 259
        $capture.ReferenceLogPin.Sha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
    }
}

function New-042142Fixture {
    param([string]$WorkspaceRoot)
    $tempRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('gpu-final-review-042142-' + [guid]::NewGuid().ToString('N'))))
    [void][IO.Directory]::CreateDirectory($tempRoot)
    $fixture = New-ValidFinalReviewFixture $tempRoot
    $fixture | Add-Member -NotePropertyName TempRoot -NotePropertyValue $tempRoot -Force
    $runId = 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2'
    $oldRunId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
    $replacements = [ordered]@{
        'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'=$runId
        '8abb7b84ca644a7c84019a1f29b35bb4'='255f08b53f3a432cbe8cd2b386447781'
        '20261020T201533358Z-Backup-479329a8e70a4124964be17cc597ad61'='20261021T042510110Z-Backup-b0ad2425a4cd436fa583d76122c8afe1'
        '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'='20261021T044758857Z-FinalVerify-a88b0158befb4af49d9643cb14da1a1c'
        'firmware-enumeration-backup-4196e13c22604293a54688532c15cff6.stdout.txt'='firmware-enumeration-backup-297ad383796447e38ac14cc7e3ba5879.stdout.txt'
        'firmware-enumeration-final-7a5347fd0bc4452eb06340ba1e614a92.stdout.txt'='firmware-enumeration-final-0b0629c3908d46e98c61bf9462991cd8.stdout.txt'
        'stage-retirement-firmware-3ba00749b6d94f82a0ef932ab7fb214d.stdout.txt'='stage-retirement-firmware-6eb1ad5ae3e9488bab017a053b4516e1.stdout.txt'
        'stage-retirement-firmware-1f63298289ce49ed81012994adc954ba.stdout.txt'='stage-retirement-firmware-ca807ac9cc6442d6a6fb8bc464698791.stdout.txt'
        '8be3274dfe56cec7'='840ce906358bbac2'
        '933b4c676318f26396cdf2c66de01c3ccbf1a950'='a52aa25e918223abae295876800933df9b642e46'
        '2026-10-20T20:19:07.5000000Z'='2026-10-21T04:30:43.5000000Z'
        '2026-10-20T20:48:51.5000000Z'='2026-10-21T04:46:50.5000000Z'
        'A7416148825A6ABC3603681C45F6A8277AFEE432D4B503BDEC89D34F58D34FFB'='B4CF6844894A920912EFF85B80012ED3B0B5504D7BD01341A531316C8FC8424C'
        'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE'='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
        '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'='7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'
        '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
        'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47'='86D15C1E8B29A7896FC8D4A9AA14ECD926DA23D8EED2871DBA65DDDA7002BD36'
        'B5D4EB51A58A2170EBCA905749E2BDAD177CFD71629A1079A14934F3F5BF63D0'='69733AFE9DD782577F302F79A788DF2745BB9C9CD4BE860DF44C59A07EB4E066'
        '9EFE53FCC2CEA79BF0F2DF7E8E550AE122D381259AB7C176D7FA64A76A7A6275'='518701A9CA9059F01B700A084736B59A9F66A382221EBD96BF436C5E67A34242'
        '4816FC0E640D7AEB38DF301E4D53A4982D5CABAA780650FEA8BDEE5D7C416899'='D30A1A38BF5B9CAD07F43C237599847A8D5ECF0825D7D3552A92F599801C543F'
        'CCFA78B48A18AB90AE865ADEB14CA952D927955523B9E72F0C30A762F4FDB553'='395101C30399FD96C2404BAA049054A287375060D95B7D9773FE2147647E563D'
        'BEA6DCA1FC904B13830908471D6239A906F71AA6937630B32CBA6DC3C2C023D9'='AD43DF7601413CDAA13A561D499F6902E47AEC53932872014D55B009EB2EC736'
    }
    $fixture = Update-FixtureStrings $fixture $replacements
    $probeRewrittenOldRunId = 'gpu-20261020T201140Z-6e6e86230a2549b2840ce906358bbac2'
    $fixture = Update-FixtureStrings $fixture ([ordered]@{ $probeRewrittenOldRunId=$runId })
    $fixture.State.RunId = $runId
    $fixture.State.SourceCommit = 'a52aa25e918223abae295876800933df9b642e46'
    $fixture.State.DisarmedBootId = '2026-10-21T04:30:43.5000000Z'
    $fixture.CurrentHost.BootId = '2026-10-21T04:46:50.5000000Z'
    $fixture.State.Backup.FirmwareEnumeration.Bytes = 2890
    $fixture.State.FinalVerification = [pscustomobject]@{
        BootId='2026-10-21T04:46:50.5000000Z'; SecureBoot=$true; ResetBoundaryPassed=$true; ActivationAttempted=$true
        Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; ExpectedCpus=20; CpuCount=20; CpuPassCount=0; Aggregate='ABSENT'; Reason='PASS' }
        FirmwareEnumeration=[pscustomobject]@{ TextEqual=$false; BaselineSha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'; CurrentSha256='7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'; CaptureContext='SystemEspMountedAtS' }
        ActiveGpuProof=$false; GpuAttestation='UNATTESTED'; Errors=@('Firmware enumeration differs from the saved textual baseline.')
    }
    $fixture.Receipt | Add-Member -MemberType NoteProperty -Name Profile -Value '042142' -Force
    $fixture.Receipt.RunId = $runId
    $fixture.Receipt.CurrentHost.BootId = '2026-10-21T04:46:50.5000000Z'
    $fixture.Receipt.Health.Verdict.ExpectedCpus = 20
    $fixture.Receipt.Health.Verdict.CpuCount = 20
    $fixture.Receipt.Health.Verdict.Aggregate = 'ABSENT'
    $fixture.Receipt.Esp.Volume = '\\?\Volume{187bc913-7576-4e6e-8d54-18eeb5c38fcd}\'
    $fixture.Receipt.Esp.DiskNumber = 0
    $fixture.Receipt.Esp.PartitionGuid = '187BC913-7576-4E6E-8D54-18EEB5C38FCD'
    $fixture.State.Backup.EspVolume = $fixture.Receipt.Esp.Volume
    $fixture.State.Backup.EspPartitionGuid = $fixture.Receipt.Esp.PartitionGuid
    $fixture.Receipt.BootSelectionCaptures[0].Baseline.BootOrder = [uint16[]]@(0,1,2,3,4,5)
    $fixture.Receipt.BootSelectionCaptures[1].Baseline.BootOrder = [uint16[]]@(0,1,2,3,4,5)
    $fixture.State.FinalVerification.FirmwareEnumeration.BaselineSha256 = $fixture.State.Backup.FirmwareEnumeration.Sha256

    $currentAction = Join-Path (Join-Path $fixture.State.RunDirectory 'actions') '20261021T050000000Z-FinalVerify-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
    $fixture.ActionDirectory = $currentAction
    $fixture.State.InProgress.Directory = $currentAction
    $fixture.CurrentFirmwarePin = New-Pin (Join-Path $currentAction 'firmware-enumeration-final-fresh.stdout.txt') '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4' 4336
    $oldAction = Join-Path (Join-Path $fixture.State.RunDirectory 'actions') '20261021T044758857Z-FinalVerify-a88b0158befb4af49d9643cb14da1a1c'
    $fixture.FailedFinalResultPath = Join-Path $oldAction 'result.json'
    $fixture.EntryJournalPath = Join-Path $currentAction 'reviewed-firmware-entry-journal.json'
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$runId; WorkspaceRoot=$fixture.State.WorkspaceRoot; SourceRoot=$fixture.State.SourceRoot
        SourceCommit='a52aa25e918223abae295876800933df9b642e46'; RunDirectory=$fixture.State.RunDirectory
        Phase='Disarmed'; DisarmedBootId='2026-10-21T04:30:43.5000000Z'; EverArmIntent=$true
        InProgress=[pscustomobject]@{ Action='FinalVerify'; Directory=$oldAction }
        LastFailure='FINAL_VERIFY_FAILED_GPU_UNATTESTED'
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Status='FINAL_VERIFY_FAILED_GPU_UNATTESTED'; Succeeded=$false; Directory=$oldAction }
        FinalVerification=[pscustomobject]@{
            BootId='2026-10-21T04:46:50.5000000Z'; SecureBoot=$true; ResetBoundaryPassed=$true; ActivationAttempted=$true
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; ExpectedCpus=20; CpuCount=20; CpuPassCount=0; Aggregate='ABSENT'; Reason='PASS' }
            FirmwareEnumeration=[pscustomobject]@{ TextEqual=$false; BaselineSha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'; CurrentSha256='7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'; CaptureContext='SystemEspMountedAtS' }
            Errors=@('Firmware enumeration differs from the saved textual baseline.')
        }
    }
    $failed = [pscustomobject]@{
        Action='FinalVerify'; Status='FINAL_VERIFY_FAILED_GPU_UNATTESTED'; Succeeded=$false; Phase='Disarmed'
        RunDirectory=$fixture.State.RunDirectory; ActionDirectory=$oldAction
        Details=[pscustomobject]@{
            Status='FINAL_VERIFY_FAILED_GPU_UNATTESTED'; Succeeded=$false
            FinalVerification=$journal.FinalVerification
        }
    }
    foreach ($file in @(@($fixture.EntryJournalPath, $journal), @($fixture.FailedFinalResultPath, $failed))) {
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
    Set-ReviewTestPins $fixture
    $fixture.Receipt.PinsAfter = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $fixture.Receipt.PinsBefore -Depth 12 -Compress)
    return $fixture
}

function Remove-042142Fixture {
    param([AllowNull()][object]$Fixture)
    if ($null -eq $Fixture) { return }
    $root = [IO.Path]::GetFullPath([string]$Fixture.TempRoot)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $root.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFileName($root).StartsWith('gpu-final-review-042142-', [StringComparison]::OrdinalIgnoreCase)) {
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
$module = Import-Module -Name $ModulePath -PassThru -DisableNameChecking -ErrorAction Stop | Select-Object -First 1
& $module { param($runId) Select-GpuFinalFirmwareReviewProfile -RunId $runId } 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2'
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..\..\..\..'))
$invokeShape = { param($f) & $module { param($receipt,$state,$host,$firmware,$action) Assert-GpuFinalFirmwareReviewReceiptShape -Receipt $receipt -State $state -CurrentHost $host -CurrentFirmwarePin $firmware -ActionDirectory $action } $f.Receipt $f.State $f.CurrentHost $f.CurrentFirmwarePin $f.ActionDirectory }

Invoke-TestCase '042142 binds only the fixed run and keeps historical/current platform pins distinct' {
    $profile = & $module { [pscustomobject]@{
        RunId=$script:GpuFinalReviewRunId; Profile=$script:GpuFinalReviewProfileId
        Disarmed=$script:GpuFinalReviewDisarmedBootId; Current=$script:GpuFinalReviewCurrentBootId
        Journal=$script:GpuFinalReviewJournalSha256; JournalBytes=$script:GpuFinalReviewJournalBytes
        PlatformHistorical=$script:GpuFinalReviewPlatformHistoricalSha256; PlatformHistoricalBytes=$script:GpuFinalReviewPlatformHistoricalBytes
        PlatformCurrent=$script:GpuFinalReviewPlatformCurrentSha256; PlatformCurrentBytes=$script:GpuFinalReviewPlatformCurrentBytes
        BootOrder=@($script:GpuFinalReviewExpectedBootOrder)
    } }
    Assert-True ($profile.RunId -ceq 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2' -and $profile.Profile -ceq '042142') 'The fixed 042142 profile is not selected.'
    Assert-True ($profile.Disarmed -ceq '2026-10-21T04:30:43.5000000Z' -and $profile.Current -ceq '2026-10-21T04:46:50.5000000Z') 'The fixed BootIds changed.'
    Assert-True ($profile.Journal -ceq '3287B5E86D018DD374748D56E16CF7BBDE0D245ECF426779EAAB80E5A2B08583' -and $profile.JournalBytes -eq 44133) 'The original journal pin changed.'
    Assert-True ($profile.PlatformHistorical -ceq 'D30A1A38BF5B9CAD07F43C237599847A8D5ECF0825D7D3552A92F599801C543F' -and $profile.PlatformHistoricalBytes -eq 111625) 'The historical platform pin changed.'
    Assert-True ($profile.PlatformCurrent -ceq '6EEC7FAB2A3DD095215B85781A3273A87B026ED8E3DF647225C40225E332336E' -and $profile.PlatformCurrentBytes -eq 111703) 'The current platform pin changed.'
    Assert-True ([string](@($profile.BootOrder) -join ',') -ceq '0,1,2,3,4,5') 'The fixed raw boot order changed.'
    Assert-Throws { & $module { Select-GpuFinalFirmwareReviewProfile -RunId 'gpu-20261021T042143Z-unreviewed' } } 'cannot switch between fixed run profiles' 'A module instance switched away from the bound run.'
}

Invoke-TestCase '042142 accepts the exact failed-result and immutable entry-journal fixture' {
    $fixture = New-042142Fixture $workspaceRoot
    try {
        $root = & $invokeShape $fixture
        Assert-True ($root.EntryJournalPath -ceq [IO.Path]::GetFullPath($fixture.EntryJournalPath)) 'The action-local immutable entry journal was not returned.'
        Assert-True ($root.FinalResultPath -ceq [IO.Path]::GetFullPath($fixture.FailedFinalResultPath)) 'The original failed result was not pinned.'
        Assert-True ($fixture.Receipt.PinsBefore.FinalResult.Sha256 -ceq $fixture.Receipt.PinsAfter.FinalResult.Sha256) 'The failed-result before/after pins differ.'
    }
    finally { Remove-042142Fixture $fixture }
}

Invoke-TestCase '042142 rejects receipt profile/run drift, source pins, and either failed-result pin drift' {
    foreach ($mutation in @(
        { param($f) $f.Receipt.Profile='224018' },
        { param($f) $f.Receipt.RunId='gpu-other' },
        { param($f) $f.Receipt.PinsAfter.Modules.Platform.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA' },
        { param($f) $f.Receipt.PinsBefore.FinalResult.Bytes++ },
        { param($f) $f.Receipt.PinsAfter.FinalResult.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA' }
    )) {
        $fixture = New-042142Fixture $workspaceRoot
        try { & $mutation $fixture; Assert-Throws { $null = & $invokeShape $fixture } 'profile|run ID|Platform pin|FinalResult pin|FinalVerify result' 'A mutated fixed receipt pin was accepted.' }
        finally { Remove-042142Fixture $fixture }
    }
}

Invoke-TestCase '042142 rejects wrong boot identities, state errors, health, and raw boot order' {
    foreach ($mutation in @(
        { param($f) $f.CurrentHost.BootId='different-boot' },
        { param($f) $f.State.FinalVerification.BootId='different-boot' },
        { param($f) $f.State.FinalVerification.Health.Passed=$false },
        { param($f) $f.State.FinalVerification.Errors=@('Firmware enumeration differs from the saved textual baseline.','unexpected') },
        { param($f) $f.Receipt.BootSelectionCaptures[1].Baseline.BootOrder[5]=9 },
        { param($f) $f.Receipt.BootSelectionCaptures[0].Baseline.BootNext=1 },
        { param($f) $f.Receipt.BootSelectionCaptures[0].Baseline.IntegrityFindings='00000001' }
    )) {
        $fixture = New-042142Fixture $workspaceRoot
        try { & $mutation $fixture; Assert-Throws { $null = & $invokeShape $fixture } 'Current host boot identity|firmware-text mismatch|failure beyond|BootOrder|BootNext|findings' 'A mutated current state or raw capture was accepted.' }
        finally { Remove-042142Fixture $fixture }
    }
}

Invoke-TestCase '042142 rejects a copied journal with changed bytes, context, or path' {
    $fixture = New-042142Fixture $workspaceRoot
    try {
        [IO.File]::AppendAllText($fixture.EntryJournalPath, ' ')
        Assert-Throws { $null = & $invokeShape $fixture } 'entry-journal copy|reviewed pre-retry journal' 'Changed journal bytes were accepted.'
    }
    finally { Remove-042142Fixture $fixture }
    $fixture = New-042142Fixture $workspaceRoot
    try {
        $bad = Get-Content -LiteralPath $fixture.EntryJournalPath -Raw | ConvertFrom-Json
        $bad.DisarmedBootId='different-boot'
        [IO.File]::WriteAllText($fixture.EntryJournalPath, (ConvertTo-Json -InputObject $bad -Depth 16 -Compress), [Text.UTF8Encoding]::new($false))
        & $module { param($hash,$bytes) $script:GpuFinalReviewJournalSha256=$hash; $script:GpuFinalReviewJournalBytes=[long]$bytes } (Get-FileHash $fixture.EntryJournalPath).Hash (Get-Item $fixture.EntryJournalPath).Length
        $fixture.Receipt.PinsBefore.Journal.Sha256=(Get-FileHash $fixture.EntryJournalPath).Hash
        $fixture.Receipt.PinsAfter.Journal.Sha256=$fixture.Receipt.PinsBefore.Journal.Sha256
        $fixture.Receipt.PinsBefore.Journal.Bytes=(Get-Item $fixture.EntryJournalPath).Length
        $fixture.Receipt.PinsAfter.Journal.Bytes=$fixture.Receipt.PinsBefore.Journal.Bytes
        Assert-Throws { $null = & $invokeShape $fixture } 'pre-retry Disarmed context' 'A semantically altered copied journal was accepted.'
    }
    finally { Remove-042142Fixture $fixture }
    $fixture = New-042142Fixture $workspaceRoot
    try {
        $outside = Join-Path $fixture.TempRoot 'outside-entry-journal.json'
        Assert-Throws { & $module { param($path,$state,$action) Assert-GpuFinalReviewEntryJournal -Path $path -State $state -ActionDirectory $action } $outside $fixture.State $fixture.ActionDirectory } 'missing from this FinalVerify action' 'An out-of-action journal path was accepted.'
    }
    finally { Remove-042142Fixture $fixture }
}

Invoke-TestCase '042142 fixes the executing platform source while retaining runtime rehash support' {
    $path = Join-Path $workspaceRoot 'platform-fixture.psm1'
    $pin = New-Pin $path '6EEC7FAB2A3DD095215B85781A3273A87B026ED8E3DF647225C40225E332336E' 111703
    & $module { param($value) Assert-GpuFinalReviewCurrentPlatformPin -Pin $value } $pin
    $pin.Sha256='D30A1A38BF5B9CAD07F43C237599847A8D5ECF0825D7D3552A92F599801C543F'
    Assert-Throws { & $module { param($value) Assert-GpuFinalReviewCurrentPlatformPin -Pin $value } $pin } 'Current reviewed GPU host platform module pin differs' 'Historical platform source was accepted as the current executable source.'
}

$summary = [pscustomobject]@{ PassedCases=$script:PassedCases; FailedCases=$script:FailedCases.ToArray(); Assertions=$script:Assertions }
$summary | ConvertTo-Json -Depth 5
if ($script:FailedCases.Count -gt 0) { exit 1 }
