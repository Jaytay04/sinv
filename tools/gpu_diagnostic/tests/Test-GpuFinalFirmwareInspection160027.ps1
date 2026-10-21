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

function New-160027JournalFixture {
    param([Parameter(Mandatory = $true)][string]$Root)
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run160027
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

function Assert-160027GenericGuidOnlyDiff {
    param([string[]]$Before, [string[]]$After, [string[]]$OldGuids, [string[]]$NewGuids)
    if ($OldGuids.Count -ne 3 -or $NewGuids.Count -ne 3) { throw 'The profile must contain exactly three old and new generic application GUIDs.' }
    $expectedBefore = [Collections.Generic.List[string]]::new()
    $expectedAfter = [Collections.Generic.List[string]]::new()
    for ($index = 0; $index -lt 3; $index++) {
        $old = '{' + $OldGuids[$index] + '}'
        $new = '{' + $NewGuids[$index] + '}'
        $expectedBefore.Add("                        $old")
        $expectedBefore.Add("identifier              $old")
        $expectedAfter.Add("                        $new")
        $expectedAfter.Add("identifier              $new")
    }
    $beforeOnly = @(Compare-Object -ReferenceObject $Before -DifferenceObject $After | Where-Object SideIndicator -CEQ '<=' | ForEach-Object InputObject | Sort-Object)
    $afterOnly = @(Compare-Object -ReferenceObject $Before -DifferenceObject $After | Where-Object SideIndicator -CEQ '=>' | ForEach-Object InputObject | Sort-Object)
    if (($beforeOnly -join "`n") -cne (@($expectedBefore | Sort-Object) -join "`n") -or
        ($afterOnly -join "`n") -cne (@($expectedAfter | Sort-Object) -join "`n")) {
        throw 'The profile text difference includes changes outside the three generic application GUID references.'
    }
}

Invoke-TestCase '160027 WhatIf has no effects and all profile selectors are exclusive' {
    $source = [IO.File]::ReadAllText($ScriptPath)
    $whatIfIndex = $source.IndexOf('if ($WhatIfPreference)', [StringComparison]::Ordinal)
    $importIndex = $source.IndexOf('Import-Module', [StringComparison]::Ordinal)
    $writeIndex = $source.IndexOf('[IO.Directory]::CreateDirectory', [StringComparison]::Ordinal)
    Assert-True ($whatIfIndex -ge 0 -and $importIndex -gt $whatIfIndex -and $writeIndex -gt $whatIfIndex) 'WhatIf guard is not ahead of module imports and evidence writes.'
    $workspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..\..\..\..'))
    $reviewRoot = Join-Path $workspaceRoot '.analysis\gpu-final-firmware-review'
    $before = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    $preview = (& $ScriptPath -Run160027 -WhatIf | Out-String | ConvertFrom-Json -ErrorAction Stop)
    Assert-True ($preview.Status -ceq 'PREVIEW_ONLY' -and $preview.Profile -ceq '160027' -and
        $preview.RunId -ceq 'gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff' -and
        $preview.NativeCommandsExecuted -eq $false -and $preview.FilesystemChangesPerformed -eq $false -and
        $preview.RecoveryCompleted -eq $false) '160027 WhatIf preview has unexpected identity or effects.'
    foreach ($other in @('Run224018','Run042142','Run062106')) {
        $selectors = @{ Run160027=$true; WhatIf=$true }
        $selectors[$other] = $true
        Assert-Throws { & $ScriptPath @selectors } 'mutually exclusive' "The entry point accepted Run160027 together with $other."
    }
    Assert-Throws { Get-GpuFinalFirmwareInspectionProfile -Run160027 -Run062106 } 'mutually exclusive' 'The profile helper accepted two closed-run selectors.'
    $after = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    Assert-True ($before -ceq $after) 'The 160027 WhatIf preview changed review evidence files.'
}

Invoke-TestCase '160027 profile binds run artifacts, source modules, and generic GUID delta' {
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run160027
    Assert-True ($profile.Name -ceq '160027' -and $profile.RunId -ceq 'gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff' -and
        $profile.SourceCommit -ceq '1be532253d6c6cc428ae2d4588a4c6e7aacadb3e' -and
        $profile.FailedAction -ceq '20261021T165416133Z-FinalVerify-33a9f87568214ffbb7342a18fcfec7cc' -and
        $profile.FailedActionStatus -ceq 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' -and
        $profile.BackupAction -ceq '20261021T160724349Z-Backup-f7bd830785b14473beca1c18fc068a62') 'The 160027 source or action identity is not fixed.'
    Assert-True ($profile.SelectorSha256 -ceq '4A0E31FA7F4773F7191A70EA151BB41FCA8363042E8F289620156337F1ABD930' -and $profile.SelectorBytes -eq 533 -and
        $profile.JournalSha256 -ceq 'A33ABD21F2D2BA369D4160644BC6305A51E747A04CE4A0DDA1200C80AF90EB22' -and $profile.JournalBytes -eq 44133) 'The exact 160027 selector or journal pin is incomplete.'
    Assert-True ($profile.BaselineLeaf -ceq 'firmware-enumeration-backup-2010399215fd4ea7885594dcce487c3c.stdout.txt' -and
        $profile.BaselineSha256 -ceq 'BC104DF2EBB63636540220FC2E085980F81AEA595B1EC77FE288116A245FA661' -and $profile.BaselineBytes -eq 4336 -and
        $profile.CurrentLeaf -ceq 'firmware-enumeration-final-a390b4a605494d33b673ea17303c26d5.stdout.txt' -and
        $profile.CurrentSha256 -ceq '98E5ECE5B987B754CD1C059C34E9D87A1CFB569E758BCC21971CA21D4EDC2D80' -and $profile.CurrentBytes -eq 4336 -and
        $profile.FinalResultLeaf -ceq 'result.json' -and $profile.FinalResultSha256 -ceq '8D29D4BB908AC72C9ADB3ADC3DE9C5AC58B5ABC99AEF1073FA25AE91A7695C31' -and
        $profile.FinalResultBytes -eq 2285) 'The 160027 baseline, current capture, or FinalVerify result pin is incomplete.'
    Assert-True ($profile.DisarmedBootId -ceq '2026-10-21T16:14:38.5000000Z' -and $profile.CurrentBootId -ceq '2026-10-21T16:52:47.5000000Z' -and
        $profile.ReferenceLogSha256 -ceq '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447' -and $profile.ReferenceLogBytes -eq 40838 -and
        $profile.ProbeRelativePath -ceq '.codex-build\gb\990e6d2305c55fff\l\ib-tcg-dump.exe' -and
        $profile.ProbeSha256 -ceq '56B204225F39A93F324AE340B985268130405845B4745723CA8DE0A8B544DC4A' -and $profile.ProbeBytes -eq 172544 -and
        $profile.AuditSha256 -ceq 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' -and $profile.AuditBytes -eq 246784) 'The 160027 boot boundary, reference, probe, or audit pins are incomplete.'
    Assert-True ($profile.StateModuleSha256 -ceq '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC' -and $profile.StateModuleBytes -eq 19843 -and
        $profile.PlatformModuleSha256 -ceq 'C1DF37B21E0AB32C0E56D00D577B15463AC3705F4B08DE8E1D5EF85561BDD34E' -and $profile.PlatformModuleBytes -eq 111822 -and
        $profile.RetirementModuleSha256 -ceq '78C8CE570A1DB3135BC39E3F9961D182AD9D9F346D73787414AB89639E76ADCF' -and $profile.RetirementModuleBytes -eq 308879) 'The 160027 source-module pins are incomplete.'

    $before = @(
        'Firmware Boot Manager';
        'displayorder {71152aeb-45e9-11ed-8140-a26d57e4c014}';
        '                        {923c8061-cd1b-11f1-8b25-806e6f6e6963}';
        '                        {923c8062-cd1b-11f1-8b25-806e6f6e6963}';
        '                        {923c8063-cd1b-11f1-8b25-806e6f6e6963}';
        'Windows Boot Manager';
        'identifier {9dea862c-5cdd-4e70-acc1-f32b344d4795}';
        'Firmware Application';
        'identifier              {923c8061-cd1b-11f1-8b25-806e6f6e6963}';
        'Firmware Application';
        'identifier              {923c8062-cd1b-11f1-8b25-806e6f6e6963}';
        'Firmware Application';
        'identifier              {923c8063-cd1b-11f1-8b25-806e6f6e6963}'
    )
    $after = @(
        'Firmware Boot Manager';
        'displayorder {71152aeb-45e9-11ed-8140-a26d57e4c014}';
        '                        {d81f168e-cd6f-11f1-8b27-806e6f6e6963}';
        '                        {d81f168f-cd6f-11f1-8b27-806e6f6e6963}';
        '                        {d81f1690-cd6f-11f1-8b27-806e6f6e6963}';
        'Windows Boot Manager';
        'identifier {9dea862c-5cdd-4e70-acc1-f32b344d4795}';
        'Firmware Application';
        'identifier              {d81f168e-cd6f-11f1-8b27-806e6f6e6963}';
        'Firmware Application';
        'identifier              {d81f168f-cd6f-11f1-8b27-806e6f6e6963}';
        'Firmware Application';
        'identifier              {d81f1690-cd6f-11f1-8b27-806e6f6e6963}'
    )
    Assert-160027GenericGuidOnlyDiff -Before $before -After $after -OldGuids $profile.BaselineGenericGuids -NewGuids $profile.CurrentGenericGuids
    $unexpected = @($after) + @('timeout 99')
    Assert-Throws { Assert-160027GenericGuidOnlyDiff -Before $before -After $unexpected -OldGuids $profile.BaselineGenericGuids -NewGuids $profile.CurrentGenericGuids } 'outside the three generic' 'The profile text-diff gate accepted an unrelated firmware change.'

    $goodSelector = [pscustomobject]@{ Sha256=$profile.SelectorSha256; Bytes=$profile.SelectorBytes }
    $goodJournal = [pscustomobject]@{ Sha256=$profile.JournalSha256; Bytes=$profile.JournalBytes }
    Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin $goodSelector -JournalPin $goodJournal
    Assert-Throws { Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin ([pscustomobject]@{ Sha256='bad'; Bytes=$profile.SelectorBytes }) -JournalPin $goodJournal } '160027 selector.*pin' 'A selector hash drift was accepted.'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin $goodSelector -JournalPin ([pscustomobject]@{ Sha256=$profile.JournalSha256; Bytes=1 }) } '160027 failed journal.*pin' 'A journal length drift was accepted.'
}

Invoke-TestCase '160027 journal gate rejects identity, failure, reset, health, ESP, and firmware drift' {
    $root = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run160027
    $fixture = New-160027JournalFixture -Root $root
    Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Selector.RunId = 'gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'fixed 160027' 'A changed selector identity was accepted.'

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Journal.SourceCommit = 'later-source'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'source revision' 'A changed source commit was accepted.'

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Journal.LastAction.Status = 'FINAL_RESET_VERIFIED_GPU_UNATTESTED'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'status or directory' 'A changed failed-action status was accepted.'

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Journal.LastAction.Directory = Join-Path (Split-Path -Parent $fixture.Journal.LastAction.Directory) 'unexpected'
    $fixture.Journal.InProgress.Directory = $fixture.Journal.LastAction.Directory
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'fixed failed FinalVerify' 'A changed failed-action directory was accepted.'

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Journal.DisarmedBootId = $profile.CurrentBootId
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'disarmed state' 'A changed disarm BootId was accepted.'

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Journal.FinalVerification.ResetBoundaryPassed = $false
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'reset, health, ESP' 'A failed reset gate was accepted.'

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Journal.FinalVerification.BootId = $profile.DisarmedBootId
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'reset, health, ESP' 'A changed current BootId was accepted.'

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Health.CpuCount = 19
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'reset, health, ESP' 'A changed CPU count was accepted.'

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Health.CpuPassCount = 1
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'absent-state result' 'A changed CPU pass count was accepted.'

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Journal.FinalVerification.FirmwareEnumeration.CurrentSha256 = 'changed'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'reset, health, ESP' 'A changed firmware capture hash was accepted.'

    $fixture = New-160027JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Errors += 'additional error'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run160027 } 'reset, health, ESP' 'An additional verification error was accepted.'
}

$summary = [pscustomobject]@{ PassedCases=$script:PassedCases; FailedCases=$script:FailedCases.ToArray(); Assertions=$script:Assertions }
$summary | ConvertTo-Json -Depth 5
if ($script:FailedCases.Count -gt 0) { exit 1 }
