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
    'Assert-GpuFinalFirmwareInspectionJournal',
    'Invoke-GpuFinalFirmwareObservation'
)
$functionAsts = $ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $requiredFunctions -contains $node.Name }, $true)
foreach ($name in $requiredFunctions) {
    $functionAst = @($functionAsts | Where-Object Name -CEQ $name | Select-Object -First 1)
    if ($functionAst.Count -ne 1) { throw "Could not isolate inspection helper $name." }
    . ([scriptblock]::Create($functionAst[0].Extent.Text))
}

function New-062106JournalFixture {
    param([Parameter(Mandatory = $true)][string]$Root)
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run062106
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

Invoke-TestCase '062106 WhatIf previews without effects and all profile selectors are exclusive' {
    $source = [IO.File]::ReadAllText($ScriptPath)
    $whatIfIndex = $source.IndexOf('if ($WhatIfPreference)', [StringComparison]::Ordinal)
    $importIndex = $source.IndexOf('Import-Module', [StringComparison]::Ordinal)
    $writeIndex = $source.IndexOf('[IO.Directory]::CreateDirectory', [StringComparison]::Ordinal)
    Assert-True ($whatIfIndex -ge 0 -and $importIndex -gt $whatIfIndex -and $writeIndex -gt $whatIfIndex) 'WhatIf guard is not ahead of module imports and evidence writes.'
    $workspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..\..\..\..'))
    $reviewRoot = Join-Path $workspaceRoot '.analysis\gpu-final-firmware-review'
    $before = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    $preview = (& $ScriptPath -Run062106 -WhatIf | Out-String | ConvertFrom-Json -ErrorAction Stop)
    Assert-True ($preview.Status -ceq 'PREVIEW_ONLY' -and $preview.Profile -ceq '062106' -and
        $preview.RunId -ceq 'gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942' -and
        $preview.NativeCommandsExecuted -eq $false -and $preview.FilesystemChangesPerformed -eq $false -and
        $preview.RecoveryCompleted -eq $false) '062106 WhatIf preview has unexpected identity or effects.'
    Assert-Throws { & $ScriptPath -Run062106 -Run042142 -WhatIf } 'mutually exclusive' 'The entry point accepted two closed-run selectors.'
    Assert-Throws { Get-GpuFinalFirmwareInspectionProfile -Run062106 -Run224018 } 'mutually exclusive' 'The profile helper accepted two closed-run selectors.'
    $after = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    Assert-True ($before -ceq $after) 'The 062106 WhatIf preview changed review evidence files.'
}

Invoke-TestCase '062106 profile binds every reviewed artifact and module pin' {
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run062106
    Assert-True ($profile.Name -ceq '062106' -and $profile.RunId -ceq 'gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942' -and
        $profile.SourceCommit -ceq 'c44bb52dcb38179f5d026b7331b848c114ed649e' -and
        $profile.FailedAction -ceq '20261021T065114533Z-FinalVerify-6c332fa152e44a55925c801705f052cd' -and
        $profile.FailedActionStatus -ceq 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' -and
        $profile.BackupAction -ceq '20261021T062522426Z-Backup-4d82912e15464713b6f63f1ace6ad3fb') 'The 062106 source or action identity is not closed.'
    Assert-True ($profile.SelectorSha256 -ceq '4510690FAD83991612344C958E26C334F19850F71D54B6D51669F7EF94AAB484' -and $profile.SelectorBytes -eq 533 -and
        $profile.JournalSha256 -ceq '26B4C9D657AE65DFD007073C4E5F99A1E620720774BEFDDBB398DE7E3F8E9A80' -and $profile.JournalBytes -eq 44133) 'The exact 062106 selector or journal pin is incomplete.'
    Assert-True ($profile.BaselineLeaf -ceq 'firmware-enumeration-backup-1648399929574818b6bfb740f9e80877.stdout.txt' -and
        $profile.BaselineSha256 -ceq '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4' -and $profile.BaselineBytes -eq 4336 -and
        $profile.CurrentLeaf -ceq 'firmware-enumeration-final-cb55a93e13d648ae97e1f62bebc01a8b.stdout.txt' -and
        $profile.CurrentSha256 -ceq 'BC104DF2EBB63636540220FC2E085980F81AEA595B1EC77FE288116A245FA661' -and $profile.CurrentBytes -eq 4336 -and
        $profile.FinalResultLeaf -ceq 'result.json' -and $profile.FinalResultSha256 -ceq 'C426FEEF8E70136E527AAE40B233CAC9281681F7C3A5113DBA062DDD4F49CC49' -and
        $profile.FinalResultBytes -eq 2285) 'The 062106 baseline, current capture, or failed result pin is incomplete.'
    Assert-True ($profile.DisarmedBootId -ceq '2026-10-21T06:36:00.5000000Z' -and $profile.CurrentBootId -ceq '2026-10-21T06:49:32.5000000Z' -and
        $profile.ReferenceLogSha256 -ceq 'F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8' -and $profile.ReferenceLogBytes -eq 40838 -and
        $profile.ProbeRelativePath -ceq '.codex-build\gb\b3b06badc7018942\l\ib-tcg-dump.exe' -and
        $profile.ProbeSha256 -ceq 'E7FEA1D8436416B0ADC392F03A22A4B510D4CE5DF3308B49DD65D9D6E2432E1A' -and $profile.ProbeBytes -eq 172544 -and
        $profile.AuditSha256 -ceq 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' -and $profile.AuditBytes -eq 246784) 'The 062106 boot boundary, reference, probe, or audit pins are incomplete.'
    Assert-True ($profile.StateModuleSha256 -ceq '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC' -and $profile.StateModuleBytes -eq 19843 -and
        $profile.PlatformModuleSha256 -ceq '6EEC7FAB2A3DD095215B85781A3273A87B026ED8E3DF647225C40225E332336E' -and $profile.PlatformModuleBytes -eq 111703 -and
        $profile.RetirementModuleSha256 -ceq 'D40F76D333DCAB0F3475D02CB12343F38629E0D12F12303A6FC5C85B14A931FD' -and $profile.RetirementModuleBytes -eq 246612) 'The 062106 source module pins are incomplete.'

    $goodSelector = [pscustomobject]@{ Sha256=$profile.SelectorSha256; Bytes=$profile.SelectorBytes }
    $goodJournal = [pscustomobject]@{ Sha256=$profile.JournalSha256; Bytes=$profile.JournalBytes }
    Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin $goodSelector -JournalPin $goodJournal
    Assert-Throws { Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin ([pscustomobject]@{ Sha256='bad'; Bytes=$profile.SelectorBytes }) -JournalPin $goodJournal } '062106 selector.*pin' 'A selector hash drift was accepted.'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin $goodSelector -JournalPin ([pscustomobject]@{ Sha256=$profile.JournalSha256; Bytes=1 }) } '062106 failed journal.*pin' 'A journal length drift was accepted.'
}

Invoke-TestCase '062106 journal gate rejects identity, status, reset, health, and error drift' {
    $root = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
    $fixture = New-062106JournalFixture -Root $root
    Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Selector.RunId = 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'fixed 062106' 'A changed selector identity was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.SourceCommit = 'future-source'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'source revision' 'A changed source commit was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.LastAction.Status = 'FINAL_RESET_VERIFIED_GPU_UNATTESTED'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'status or directory' 'A changed failed action status was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.LastAction.Directory = Join-Path (Split-Path -Parent $fixture.Journal.LastAction.Directory) 'unexpected'
    $fixture.Journal.InProgress.Directory = $fixture.Journal.LastAction.Directory
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'fixed failed FinalVerify' 'A changed failed action directory was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.InProgress.Directory = Join-Path $fixture.RunDirectory 'actions\unexpected'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'failed FinalVerify intent' 'A changed in-progress action directory was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.DisarmedBootId = '2026-10-21T06:49:32.5000000Z'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'disarmed state' 'A changed disarm BootId was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.FinalVerification.ResetBoundaryPassed = $false
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'reset, health, ESP' 'A failed reset gate was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.FinalVerification.SecureBoot = $false
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'reset, health, ESP' 'Secure Boot drift was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.FinalVerification.BootId = 'future-boot'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'reset, health, ESP' 'A changed current BootId was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Health.CpuCount = 19
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'reset, health, ESP' 'A changed CPU count was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Health.CpuPassCount = 1
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'absent-state result' 'A changed CPU pass count was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.FinalVerification.FirmwareEnumeration.CurrentSha256 = 'modified'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'reset, health, ESP' 'A changed failed firmware hash was accepted.'

    $fixture = New-062106JournalFixture -Root $root
    $fixture.Journal.FinalVerification.Errors += 'additional error'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run062106 } 'reset, health, ESP' 'An additional verification error was accepted.'
}

Invoke-TestCase '062106 observation rejects unhealthy data and still dismounts its owned ESP' {
    $mount = [pscustomobject]@{ Acquired=$true; Identity=[pscustomobject]@{ Volume='fixture-volume'; DiskNumber=0; PartitionGuid='fixture-guid' } }
    $behavior = @{ DismountCount=0 }
    $firmware = [pscustomobject]@{ Succeeded=$true; Text='stable firmware listing' }
    $boot = [pscustomobject]@{ Baseline=[pscustomobject]@{ BootOrder=[uint16[]]@(1,2) } }
    $operations = @{
        MountEsp = { param($d) return $mount }.GetNewClosure()
        AssertMountedEsp = { param($m,$b) return [pscustomobject]@{ Sha256='BOOT' } }
        CaptureFirmware = { param($c) return $firmware }.GetNewClosure()
        CaptureBootSelection = { param($c) return $boot }
        CaptureHealth = { return [pscustomobject]@{ CommandSucceeded=$true; Verdict=[pscustomobject]@{ Passed=$false; CpuCount=20; ExpectedCpus=20; Aggregate='ABSENT'; ExpectedResult='ABSENT' } } }
        AssertBootSelectionStable = { param($before,$after) }
        DismountEsp = { param($m,$d) [void]($behavior.DismountCount++); $behavior.DismountMount=$m }.GetNewClosure()
    }
    $result = Invoke-GpuFinalFirmwareObservation -State ([pscustomobject]@{ Backup=[pscustomobject]@{} }) -ActionDirectory 'fixture-actions' -Operations $operations
    Assert-True (-not $result.Succeeded -and $result.Status -ceq 'READONLY_FIRMWARE_REVIEW_FAILED' -and $result.Errors.Count -eq 1 -and
        $result.Errors[0] -match 'Fresh post-reset health') 'Unhealthy observation data did not fail closed.'
    Assert-True ($result.EspUnmounted -and $result.CleanupSucceeded -and $behavior.DismountCount -eq 1 -and
        [object]::ReferenceEquals($mount,$behavior.DismountMount)) 'The failed observation did not dismount its exact owned ESP mount.'
}

$summary = [pscustomobject]@{ PassedCases=$script:PassedCases; FailedCases=$script:FailedCases.ToArray(); Assertions=$script:Assertions }
$summary | ConvertTo-Json -Depth 5
if ($script:FailedCases.Count -gt 0) { exit 1 }
