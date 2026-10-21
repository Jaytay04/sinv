[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$ScriptPath = Join-Path $ToolRoot 'Inspect-GpuFinalFirmware.ps1'
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

if (-not (Test-Path -LiteralPath $ScriptPath -PathType Leaf)) { throw "Inspection script not found: $ScriptPath" }
$tokens = $null; $parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($ScriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw "Inspection script parse failed: $($parseErrors[0].Message)" }
$requiredFunctions = @(
    'Get-GpuFinalFirmwareInspectionProfile',
    'Assert-GpuFinalFirmwareInspectionEntryPins',
    'Assert-GpuFinalFirmwareInspectionJournal'
)
$functionAsts = $ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $requiredFunctions -contains $node.Name }, $true)
foreach ($name in $requiredFunctions) {
    $functionAst = @($functionAsts | Where-Object Name -CEQ $name | Select-Object -First 1)
    if ($functionAst.Count -ne 1) { throw "Could not isolate inspection helper $name." }
    . ([scriptblock]::Create($functionAst[0].Extent.Text))
}

function New-193833JournalFixture {
    param([Parameter(Mandatory = $true)][string]$Root)
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run193833
    $run = Join-Path $Root ('.analysis\gpu-baremetal\runs\' + $profile.RunId)
    $action = Join-Path (Join-Path $run 'actions') $profile.FailedAction
    $selector = [pscustomobject]@{ SchemaVersion=1; RunId=$profile.RunId; RunDirectory=$run; SourceRoot=$Root }
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$profile.RunId; SourceCommit=$profile.SourceCommit
        SourceRoot=$Root; RunDirectory=$run; Phase='Disarmed'; EverArmIntent=$true; DisarmedBootId=$profile.DisarmedBootId
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Status=$profile.FailedActionStatus; Succeeded=$false; Directory=$action }
        InProgress=[pscustomobject]@{ Action='FinalVerify'; Directory=$action }
        FinalVerification=[pscustomobject]@{
            BootId=$profile.CurrentBootId; ResetBoundaryPassed=$true; SecureBoot=$true; ActivationAttempted=$true
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; ExpectedCpus=20; CpuCount=20; CpuPassCount=0; Reason='PASS' }
            Esp=[pscustomobject]@{ MatchesBaseline=$true; BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511' }
            FirmwareEnumeration=[pscustomobject]@{
                TextEqual=$false; BaselineSha256=$profile.BaselineSha256
                CurrentSha256=$profile.CurrentSha256; CaptureContext='SystemEspMountedAtS'
            }
            Errors=@('Firmware enumeration differs from the saved textual baseline.')
        }
    }
    return @{ Selector=$selector; Journal=$journal; SourceRoot=$Root; RunDirectory=$run }
}

function Get-LineMultiplicityDifference {
    param([string[]]$Left, [string[]]$Right)
    $leftCounts = [Collections.Generic.Dictionary[string, int]]::new([StringComparer]::Ordinal)
    $rightCounts = [Collections.Generic.Dictionary[string, int]]::new([StringComparer]::Ordinal)
    foreach ($line in $Left) {
        if (-not $leftCounts.ContainsKey($line)) { $leftCounts[$line] = 0 }
        $leftCounts[$line]++
    }
    foreach ($line in $Right) {
        if (-not $rightCounts.ContainsKey($line)) { $rightCounts[$line] = 0 }
        $rightCounts[$line]++
    }
    $difference = [Collections.Generic.List[string]]::new()
    foreach ($line in $leftCounts.Keys) {
        $rightCount = if ($rightCounts.ContainsKey($line)) { $rightCounts[$line] } else { 0 }
        for ($index = $rightCount; $index -lt $leftCounts[$line]; $index++) { $difference.Add($line) }
    }
    return @($difference.ToArray() | Sort-Object -CaseSensitive)
}

function Assert-193833GenericApplicationsRemovedOnly {
    param([string[]]$Before, [string[]]$After, [string[]]$RemovedGuids)
    if ($RemovedGuids.Count -ne 3) { throw 'The reviewed profile must contain exactly three removed generic application GUIDs.' }

    $expectedRemoved = [Collections.Generic.List[string]]::new()
    $descriptions = @('UEFI:CD/DVD Drive', 'UEFI:Removable Device', 'UEFI:Network Device')
    for ($index = 0; $index -lt $RemovedGuids.Count; $index++) {
        $guid = '{' + $RemovedGuids[$index] + '}'
        $expectedRemoved.Add("                        $guid")
        $expectedRemoved.Add('Firmware Application (101fffff)')
        $expectedRemoved.Add('-------------------------------')
        $expectedRemoved.Add("identifier              $guid")
        $expectedRemoved.Add("description             $($descriptions[$index])")
    }

    $beforeOnly = @(Get-LineMultiplicityDifference -Left $Before -Right $After)
    $afterOnly = @(Get-LineMultiplicityDifference -Left $After -Right $Before)
    if (($beforeOnly -join "`n") -cne (@($expectedRemoved.ToArray() | Sort-Object -CaseSensitive) -join "`n") -or $afterOnly.Count -ne 0) {
        throw 'The firmware text delta contains a change other than removal of the three generic application blocks and display-order references.'
    }
}

Invoke-TestCase '193833 WhatIf has no effects and all profile selectors are exclusive' {
    $source = [IO.File]::ReadAllText($ScriptPath)
    $whatIfIndex = $source.IndexOf('if ($WhatIfPreference)', [StringComparison]::Ordinal)
    $importIndex = $source.IndexOf('Import-Module', [StringComparison]::Ordinal)
    $writeIndex = $source.IndexOf('[IO.Directory]::CreateDirectory', [StringComparison]::Ordinal)
    Assert-True ($whatIfIndex -ge 0 -and $importIndex -gt $whatIfIndex -and $writeIndex -gt $whatIfIndex) 'WhatIf guard is not ahead of module imports and evidence writes.'
    $workspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..\..\..\..'))
    $reviewRoot = Join-Path $workspaceRoot '.analysis\gpu-final-firmware-review'
    $before = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    $preview = (& $ScriptPath -Run193833 -WhatIf | Out-String | ConvertFrom-Json -ErrorAction Stop)
    Assert-True ($preview.Status -ceq 'PREVIEW_ONLY' -and $preview.Profile -ceq '193833' -and
        $preview.RunId -ceq 'gpu-20261021T193833Z-c48461667089493988074edfaf008d56' -and
        $preview.NativeCommandsExecuted -eq $false -and $preview.FilesystemChangesPerformed -eq $false -and
        $preview.RecoveryCompleted -eq $false) '193833 WhatIf preview has unexpected identity or effects.'
    foreach ($other in @('Run224018','Run042142','Run062106','Run160027')) {
        $selectors = @{ Run193833=$true; WhatIf=$true }
        $selectors[$other] = $true
        Assert-Throws { & $ScriptPath @selectors } 'mutually exclusive' "The entry point accepted Run193833 together with $other."
        $helperSelectors = @{ Run193833=$true }
        $helperSelectors[$other] = $true
        Assert-Throws { Get-GpuFinalFirmwareInspectionProfile @helperSelectors } 'mutually exclusive' "The profile helper accepted Run193833 together with $other."
    }
    $after = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    Assert-True ($before -ceq $after) 'The 193833 WhatIf preview changed review evidence files.'
}

Invoke-TestCase '193833 profile pins the exact run evidence and source modules' {
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run193833
    Assert-True ($profile.Name -ceq '193833' -and $profile.RunId -ceq 'gpu-20261021T193833Z-c48461667089493988074edfaf008d56' -and
        $profile.SourceCommit -ceq '15842e7d789eddaea401754734ce9540a10b69e7' -and
        $profile.FailedAction -ceq '20261021T200510255Z-FinalVerify-38149ac12747467cbb64e46552b7abae' -and
        $profile.FailedActionStatus -ceq 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' -and
        $profile.BackupAction -ceq '20261021T194754121Z-Backup-ba3d1ef07fe64145916eea9506cca303') 'The 193833 source or action identity is not fixed.'
    Assert-True ($profile.SelectorSha256 -ceq 'E0611ECB3A3F1DC94A064D40783E68A495C5FFED761BE01626C4BC95E63916A3' -and $profile.SelectorBytes -eq 533 -and
        $profile.JournalSha256 -ceq '557AB91F1760F4FEA7EE4E54135E6E0B7B45B0ADA16CE91D09A2B508ADA44E18' -and $profile.JournalBytes -eq 45269) 'The exact 193833 selector or failed journal pin is incomplete.'
    Assert-True ($profile.BaselineLeaf -ceq 'firmware-enumeration-backup-890dee938db04defbe3b879ede88023a.stdout.txt' -and
        $profile.BaselineSha256 -ceq '98E5ECE5B987B754CD1C059C34E9D87A1CFB569E758BCC21971CA21D4EDC2D80' -and $profile.BaselineBytes -eq 4336 -and
        $profile.CurrentLeaf -ceq 'firmware-enumeration-final-6a3e5467babe4872aec6689e6c95f51b.stdout.txt' -and
        $profile.CurrentSha256 -ceq '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007' -and $profile.CurrentBytes -eq 2890 -and
        $profile.FinalResultLeaf -ceq 'result.json' -and $profile.FinalResultSha256 -ceq '23B01D517A650AE90D58180086E2BC9D2BB06899476E29F740FDE3304CCBED35' -and
        $profile.FinalResultBytes -eq 2285) 'The 193833 baseline, current capture, or failed FinalVerify result pin is incomplete.'
    Assert-True ($profile.DisarmedBootId -ceq '2026-10-21T19:51:47.5000000Z' -and $profile.CurrentBootId -ceq '2026-10-21T20:03:49.5000000Z' -and
        $profile.ReferenceLogSha256 -ceq 'F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8' -and $profile.ReferenceLogBytes -eq 40838 -and
        $profile.ProbeRelativePath -ceq '.codex-build\gb\88074edfaf008d56\l\ib-tcg-dump.exe' -and
        $profile.ProbeSha256 -ceq 'F2CACE0FA3BF07140593B64E48529C6CA5AF0EA036E4B500F2F98C3046212333' -and $profile.ProbeBytes -eq 172544 -and
        $profile.AuditSha256 -ceq 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' -and $profile.AuditBytes -eq 246784) 'The 193833 boot boundary, reference, probe, or audit pins are incomplete.'
    Assert-True ($profile.StateModuleSha256 -ceq '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC' -and $profile.StateModuleBytes -eq 19843 -and
        $profile.PlatformModuleSha256 -ceq '66A3B826C01A3F3A3D962DB5798C81FA0FE758325BC0734D93778691409125B2' -and $profile.PlatformModuleBytes -eq 133220 -and
        $profile.RetirementModuleSha256 -ceq '24CDEFAE98EFFCFF664CB269E3642C35E4920EB7560F1492010C766486143E59' -and $profile.RetirementModuleBytes -eq 372336) 'The 193833 current source-module pins are incomplete.'

    $expectedRemoved = @(
        'd81f168e-cd6f-11f1-8b27-806e6f6e6963',
        'd81f168f-cd6f-11f1-8b27-806e6f6e6963',
        'd81f1690-cd6f-11f1-8b27-806e6f6e6963'
    )
    Assert-True (@($profile.BaselineGenericGuids) -join ';' -ceq ($expectedRemoved -join ';') -and @($profile.CurrentGenericGuids).Count -eq 0) 'The 193833 generic firmware application removal set is wrong.'

    $before = @(
        'Firmware Boot Manager'
        'identifier {a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}'
        'displayorder {9dea862c-5cdd-4e70-acc1-f32b344d4795}'
        '                        {6cc7730c-c9c3-11f1-847b-806e6f6e6963}'
        '                        {6cc7730d-c9c3-11f1-847b-806e6f6e6963}'
        '                        {d81f168e-cd6f-11f1-8b27-806e6f6e6963}'
        '                        {d81f168f-cd6f-11f1-8b27-806e6f6e6963}'
        '                        {d81f1690-cd6f-11f1-8b27-806e6f6e6963}'
        'Windows Boot Manager'
        'identifier {9dea862c-5cdd-4e70-acc1-f32b344d4795}'
        'Firmware Application (101fffff)'
        '-------------------------------'
        'identifier              {d81f168e-cd6f-11f1-8b27-806e6f6e6963}'
        'description             UEFI:CD/DVD Drive'
        'Firmware Application (101fffff)'
        '-------------------------------'
        'identifier              {d81f168f-cd6f-11f1-8b27-806e6f6e6963}'
        'description             UEFI:Removable Device'
        'Firmware Application (101fffff)'
        '-------------------------------'
        'identifier              {d81f1690-cd6f-11f1-8b27-806e6f6e6963}'
        'description             UEFI:Network Device'
        'Firmware Application (101fffff)'
        '-------------------------------'
        'identifier              {6cc7730c-c9c3-11f1-847b-806e6f6e6963}'
        'description             UEFI: Memorex USB Flash Drive PMAP, Partition 1'
        'Firmware Application (101fffff)'
        '-------------------------------'
        'identifier              {6cc7730d-c9c3-11f1-847b-806e6f6e6963}'
        'description             UEFI: VendorCoProductCode 2.00, Partition 1'
    )
    $after = @(
        'Firmware Boot Manager'
        'identifier {a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}'
        'displayorder {9dea862c-5cdd-4e70-acc1-f32b344d4795}'
        '                        {6cc7730c-c9c3-11f1-847b-806e6f6e6963}'
        '                        {6cc7730d-c9c3-11f1-847b-806e6f6e6963}'
        'Windows Boot Manager'
        'identifier {9dea862c-5cdd-4e70-acc1-f32b344d4795}'
        'Firmware Application (101fffff)'
        '-------------------------------'
        'identifier              {6cc7730c-c9c3-11f1-847b-806e6f6e6963}'
        'description             UEFI: Memorex USB Flash Drive PMAP, Partition 1'
        'Firmware Application (101fffff)'
        '-------------------------------'
        'identifier              {6cc7730d-c9c3-11f1-847b-806e6f6e6963}'
        'description             UEFI: VendorCoProductCode 2.00, Partition 1'
    )
    Assert-193833GenericApplicationsRemovedOnly -Before $before -After $after -RemovedGuids $profile.BaselineGenericGuids
    $unexpected = @($after) + @('timeout 99')
    Assert-Throws { Assert-193833GenericApplicationsRemovedOnly -Before $before -After $unexpected -RemovedGuids $profile.BaselineGenericGuids } 'other than removal' 'The 193833 firmware text delta accepted an unrelated current-only line.'
    $unexpected = @($before) + @('unexpected baseline line')
    Assert-Throws { Assert-193833GenericApplicationsRemovedOnly -Before $unexpected -After $after -RemovedGuids $profile.BaselineGenericGuids } 'other than removal' 'The 193833 firmware text delta accepted an unrelated baseline-only line.'

    $goodSelector = [pscustomobject]@{ Sha256=$profile.SelectorSha256; Bytes=$profile.SelectorBytes }
    $goodJournal = [pscustomobject]@{ Sha256=$profile.JournalSha256; Bytes=$profile.JournalBytes }
    Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin $goodSelector -JournalPin $goodJournal
    Assert-Throws { Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin ([pscustomobject]@{ Sha256='bad'; Bytes=$profile.SelectorBytes }) -JournalPin $goodJournal } '193833 selector.*pin' 'A selector hash drift was accepted.'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin $goodSelector -JournalPin ([pscustomobject]@{ Sha256=$profile.JournalSha256; Bytes=1 }) } '193833 failed journal.*pin' 'A journal length drift was accepted.'
}

Invoke-TestCase '193833 journal gate rejects identity, failure, reset, health, ESP, and firmware drift' {
    $root = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run193833
    $fixture = New-193833JournalFixture -Root $root
    Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Selector.RunId = 'gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'fixed 193833' 'A changed selector identity was accepted.'

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Journal.SourceCommit = 'later-source'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'source revision' 'A changed source commit was accepted.'

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Journal.LastAction.Status = 'FINAL_RESET_VERIFIED_GPU_UNATTESTED'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'status or directory' 'A changed failed-action status was accepted.'

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Journal.LastAction.Directory = Join-Path (Split-Path -Parent $fixture.Journal.LastAction.Directory) 'unexpected'
    $fixture.Journal.InProgress.Directory = $fixture.Journal.LastAction.Directory
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'fixed failed FinalVerify' 'A changed failed-action directory was accepted.'

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Journal.DisarmedBootId = $profile.CurrentBootId
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'disarmed state' 'A changed disarm BootId was accepted.'

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Journal.FinalVerification.ResetBoundaryPassed = $false
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'reset, health, ESP' 'A failed reset gate was accepted.'

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Journal.FinalVerification.BootId = $profile.DisarmedBootId
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'reset, health, ESP' 'A changed current BootId was accepted.'

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Health.CpuCount = 19
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'reset, health, ESP' 'A changed CPU count was accepted.'

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Health.CpuPassCount = 1
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'absent-state result' 'A changed CPU pass count was accepted.'

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Journal.FinalVerification.FirmwareEnumeration.CurrentSha256 = 'changed'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'reset, health, ESP' 'A changed firmware capture hash was accepted.'

    $fixture = New-193833JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Errors += 'additional error'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run193833 } 'reset, health, ESP' 'An additional verification error was accepted.'
}

$summary = [pscustomobject]@{ PassedCases=$script:PassedCases; FailedCases=$script:FailedCases.ToArray(); Assertions=$script:Assertions }
$summary | ConvertTo-Json -Depth 5
if ($script:FailedCases.Count -gt 0) { exit 1 }
