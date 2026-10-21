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

function New-042142JournalFixture {
    param([Parameter(Mandatory = $true)][string]$Root)
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run042142
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

Invoke-TestCase '042142 WhatIf is a no-effects preview and profile switches are exclusive' {
    $source = [IO.File]::ReadAllText($ScriptPath)
    $whatIfIndex = $source.IndexOf('if ($WhatIfPreference)', [StringComparison]::Ordinal)
    $importIndex = $source.IndexOf('Import-Module', [StringComparison]::Ordinal)
    $writeIndex = $source.IndexOf('[IO.Directory]::CreateDirectory', [StringComparison]::Ordinal)
    Assert-True ($whatIfIndex -ge 0 -and $importIndex -gt $whatIfIndex -and $writeIndex -gt $whatIfIndex) 'WhatIf guard is not ahead of module imports and evidence writes.'
    $workspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..\..\..\..'))
    $reviewRoot = Join-Path $workspaceRoot '.analysis\gpu-final-firmware-review'
    $before = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    $preview = (& $ScriptPath -Run042142 -WhatIf | Out-String | ConvertFrom-Json -ErrorAction Stop)
    Assert-True ($preview.Status -ceq 'PREVIEW_ONLY' -and $preview.Profile -ceq '042142' -and
        $preview.RunId -ceq 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2' -and
        $preview.NativeCommandsExecuted -eq $false -and $preview.FilesystemChangesPerformed -eq $false -and
        $preview.RecoveryCompleted -eq $false) '042142 WhatIf preview has unexpected identity or effects.'
    Assert-Throws { & $ScriptPath -Run042142 -Run224018 -WhatIf } 'mutually exclusive' 'The entry point accepted two closed-run selectors.'
    Assert-Throws { Get-GpuFinalFirmwareInspectionProfile -Run042142 -Run224018 } 'mutually exclusive' 'The profile helper accepted two closed-run selectors.'
    $after = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    Assert-True ($before -ceq $after) 'The 042142 WhatIf preview changed review evidence files.'
}

Invoke-TestCase '042142 profile pins the exact selector, journal, captures, modules, probe, and boot boundary' {
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run042142
    Assert-True ($profile.Name -ceq '042142' -and $profile.RunId -ceq 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2' -and
        $profile.SourceCommit -ceq 'a52aa25e918223abae295876800933df9b642e46' -and
        $profile.FailedAction -ceq '20261021T044758857Z-FinalVerify-a88b0158befb4af49d9643cb14da1a1c' -and
        $profile.BackupAction -ceq '20261021T042510110Z-Backup-b0ad2425a4cd436fa583d76122c8afe1') 'The 042142 source or action identity is not closed.'
    Assert-True ($profile.SelectorSha256 -ceq 'B4CF6844894A920912EFF85B80012ED3B0B5504D7BD01341A531316C8FC8424C' -and $profile.SelectorBytes -eq 533 -and
        $profile.JournalSha256 -ceq '3287B5E86D018DD374748D56E16CF7BBDE0D245ECF426779EAAB80E5A2B08583' -and $profile.JournalBytes -eq 44133) 'The exact 042142 selector or journal pin is incomplete.'
    Assert-True ($profile.BaselineSha256 -ceq '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007' -and $profile.BaselineBytes -eq 2890 -and
        $profile.CurrentSha256 -ceq '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4' -and $profile.CurrentBytes -eq 4336 -and
        $profile.FinalResultSha256 -ceq '885BC6B54E13815762C85D9BF8ED60AB4C11762A3E584F143BFAAB52E6C72B69' -and $profile.FinalResultBytes -eq 2285) 'The firmware capture or failed action result pins are incomplete.'
    Assert-True ($profile.DisarmedBootId -ceq '2026-10-21T04:30:43.5000000Z' -and $profile.CurrentBootId -ceq '2026-10-21T04:46:50.5000000Z' -and
        $profile.ProbeRelativePath -ceq '.codex-build\gb\840ce906358bbac2\l\ib-tcg-dump.exe' -and
        $profile.ProbeSha256 -ceq '86D15C1E8B29A7896FC8D4A9AA14ECD926DA23D8EED2871DBA65DDDA7002BD36' -and $profile.ProbeBytes -eq 172544) 'The 042142 reset or probe pins are incomplete.'
    Assert-True ($profile.ReferenceLogSha256 -ceq '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447' -and $profile.ReferenceLogBytes -eq 40838 -and
        $profile.StateModuleSha256 -ceq '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC' -and
        $profile.PlatformModuleSha256 -ceq 'D30A1A38BF5B9CAD07F43C237599847A8D5ECF0825D7D3552A92F599801C543F' -and
        $profile.RetirementModuleSha256 -ceq '395101C30399FD96C2404BAA049054A287375060D95B7D9773FE2147647E563D') 'The reference log or reviewed source module pins are incomplete.'

    $goodSelector = [pscustomobject]@{ Sha256=$profile.SelectorSha256; Bytes=$profile.SelectorBytes }
    $goodJournal = [pscustomobject]@{ Sha256=$profile.JournalSha256; Bytes=$profile.JournalBytes }
    Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin $goodSelector -JournalPin $goodJournal
    Assert-Throws { Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin ([pscustomobject]@{ Sha256='bad'; Bytes=$profile.SelectorBytes }) -JournalPin $goodJournal } '042142 selector.*pin' 'A mismatched 042142 selector pin was accepted.'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin $goodSelector -JournalPin ([pscustomobject]@{ Sha256=$profile.JournalSha256; Bytes=1 }) } '042142 failed journal.*pin' 'A mismatched 042142 journal pin was accepted.'
}

Invoke-TestCase '042142 journal gate rejects unpinned identity, reset, health, ESP, and firmware changes' {
    $root = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
    $fixture = New-042142JournalFixture -Root $root
    Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Selector.RunId = 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'fixed 042142' 'A wrong selector run was accepted.'

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Journal.SourceCommit = 'future-source'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'source revision' 'A changed source commit was accepted.'

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Journal.LastAction.Status = 'FINAL_RESET_VERIFIED_GPU_UNATTESTED'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'status or directory' 'A different failed action status was accepted.'

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Journal.InProgress.Directory = Join-Path $fixture.RunDirectory 'actions\unexpected'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'failed FinalVerify intent' 'A different in-progress action directory was accepted.'

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Journal.DisarmedBootId = '2026-10-21T04:46:50.5000000Z'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'disarmed state' 'A changed disarm boot ID was accepted.'

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Journal.FinalVerification.BootId = 'future-boot'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'reset, health, ESP' 'A changed current boot ID was accepted.'

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Health.CpuCount = 19
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'reset, health, ESP' 'A changed CPU count was accepted.'

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Esp.BootManagerSha256 = 'modified'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'reset, health, ESP' 'A changed ESP baseline was accepted.'

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Journal.FinalVerification.FirmwareEnumeration.CurrentSha256 = 'modified'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'reset, health, ESP' 'A changed failed firmware hash was accepted.'

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Errors += 'additional error'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'reset, health, ESP' 'An additional final verification error was accepted.'

    $fixture = New-042142JournalFixture -Root $root
    $fixture.Journal.Phase = 'Complete'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run042142 } 'disarmed state' 'A completed or future journal was accepted.'
}

$summary = [pscustomobject]@{ PassedCases=$script:PassedCases; FailedCases=$script:FailedCases.ToArray(); Assertions=$script:Assertions }
$summary | ConvertTo-Json -Depth 5
if ($script:FailedCases.Count -gt 0) { exit 1 }
