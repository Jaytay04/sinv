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
function New-ValidJournalFixture {
    param([string]$Root, [switch]$Run224018)
    $profile = Get-GpuFinalFirmwareInspectionProfile -Run224018:$Run224018
    $runId = $profile.RunId
    $run = Join-Path $Root ('.analysis\gpu-baremetal\runs\' + $runId)
    $action = Join-Path (Join-Path $run 'actions') $profile.FailedAction
    $selector = [pscustomobject]@{ SchemaVersion=1; RunId=$runId; RunDirectory=$run; SourceRoot=$Root }
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$runId; SourceCommit=$profile.SourceCommit
        SourceRoot=$Root; RunDirectory=$run; Phase='Disarmed'; EverArmIntent=$true; DisarmedBootId=$profile.DisarmedBootId
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$false; Directory=$action }
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
function New-MockObservationOperations {
    param([hashtable]$Behavior)
    $firmwareText = "Firmware Boot Manager`nBootOrder: {fixture}`n"
    $boot = [pscustomobject]@{ BootNext=$null; BootOrder=[uint16[]]@(1,2); BootManagerSha256='BOOT'; ReferenceLogSha256='LOG'; IntegrityFindings='00000000' }
    $health = [pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20 }
    return @{
        MountEsp = { param($d) [void]($Behavior.MountCount++); $Behavior.Mount=[pscustomobject]@{ Acquired=$true; Identity=[pscustomobject]@{ Volume='fixture-volume'; DiskNumber=0; PartitionGuid='fixture-guid' } }; return $Behavior.Mount }.GetNewClosure()
        AssertMountedEsp = { param($m,$b) return [pscustomobject]@{ Sha256='BOOT' } }
        CaptureFirmware = { param($c) [void]($Behavior.FirmwareCount++); if ($Behavior.FirmwareFails -and $c -eq 'after') { throw 'mock firmware capture failure' }; return [pscustomobject]@{ Succeeded=$true; Checkpoint=$c; Text=$(if ($Behavior.FirmwareChanges -and $c -eq 'after') { $firmwareText + 'changed' } else { $firmwareText }); Pin=[pscustomobject]@{ Sha256='FIRMWARE'; Bytes=4336 }; ReceiptPath=('firmware-' + $c + '.receipt') } }.GetNewClosure()
        CaptureBootSelection = { param($c) [void]($Behavior.BootCount++); return [pscustomobject]@{ Baseline=$boot; Checkpoint=$c } }.GetNewClosure()
        CaptureHealth = { if ($Behavior.HealthFails) { throw 'mock health failure' }; return [pscustomobject]@{ CommandSucceeded=$true; Verdict=$health; ReceiptPath='health.receipt' } }.GetNewClosure()
        AssertBootSelectionStable = { param($b,$a) if ($Behavior.BootChanges) { throw 'mock BootOrder changed' } }.GetNewClosure()
        DismountEsp = { param($m,$d) [void]($Behavior.DismountCount++); $Behavior.DismountMount=$m; if ($Behavior.DismountFails) { throw 'mock unmount failure' } }.GetNewClosure()
    }
}

if (-not (Test-Path -LiteralPath $ScriptPath -PathType Leaf)) { throw "Inspection script not found: $ScriptPath" }
$source = [IO.File]::ReadAllText($ScriptPath)
$parseTokens = $null; $parseErrors = $null
[System.Management.Automation.Language.Parser]::ParseFile($ScriptPath, [ref]$parseTokens, [ref]$parseErrors) | Out-Null
if ($parseErrors.Count -gt 0) { throw "Inspection script parse failed: $($parseErrors[0].Message)" }

Invoke-TestCase 'WhatIf guard precedes imports and evidence writes' {
    $whatIfIndex = $source.IndexOf('if ($WhatIfPreference)', [StringComparison]::Ordinal)
    $importIndex = $source.IndexOf('Import-Module', [StringComparison]::Ordinal)
    $writeIndex = $source.IndexOf('[IO.Directory]::CreateDirectory', [StringComparison]::Ordinal)
    Assert-True ($whatIfIndex -ge 0 -and $importIndex -gt $whatIfIndex -and $writeIndex -gt $whatIfIndex) 'WhatIf guard is not ahead of side effects.'
    $workspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..\..\..\..'))
    $reviewRoot = Join-Path $workspaceRoot '.analysis\gpu-final-firmware-review'
    $before = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    $preview = (& $ScriptPath -WhatIf | Out-String | ConvertFrom-Json -ErrorAction Stop)
    $preview224018 = (& $ScriptPath -Run224018 -WhatIf | Out-String | ConvertFrom-Json -ErrorAction Stop)
    Assert-True ($preview.Status -ceq 'PREVIEW_ONLY' -and $preview.Profile -ceq '201140' -and $preview.RunId -ceq 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7' -and $preview.NativeCommandsExecuted -eq $false -and $preview.FilesystemChangesPerformed -eq $false) 'The default profile WhatIf did not return its no-effects preview.'
    Assert-True ($preview224018.Status -ceq 'PREVIEW_ONLY' -and $preview224018.Profile -ceq '224018' -and $preview224018.RunId -ceq 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1' -and $preview224018.NativeCommandsExecuted -eq $false -and $preview224018.FilesystemChangesPerformed -eq $false) 'The 224018 WhatIf did not return its no-effects preview.'
    $after = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    Assert-True ($before -ceq $after) 'WhatIf changed the evidence directory contents.'
}

Invoke-TestCase 'pure profile data preserves both fixed closed-run identities' {
    . $ScriptPath -WhatIf | Out-Null
    $legacy = Get-GpuFinalFirmwareInspectionProfile
    $current = Get-GpuFinalFirmwareInspectionProfile -Run224018
    Assert-True ($legacy.Name -ceq '201140' -and $legacy.RunId -ceq 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7' -and $legacy.SourceCommit -ceq '933b4c676318f26396cdf2c66de01c3ccbf1a950') 'The default 201140 profile changed.'
    Assert-True ($current.Name -ceq '224018' -and $current.RunId -ceq 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1' -and $current.SourceCommit -ceq '69a3c3d94f33d1903778ef5ec5b0a1f015b1be29') 'The 224018 profile data is not fixed to its reviewed run.'
    Assert-True ($current.SelectorSha256 -ceq 'A996B969394113B052010129563BF3EAE35443B0B452B0C66D0B7B69FF9D75B3' -and $current.SelectorBytes -eq 533 -and $current.JournalSha256 -ceq 'A1513F12AFCD2932E4DE2D9A6069C7A2CED6F969BF1AC82E6942D6818F319E07' -and $current.JournalBytes -eq 45568 -and $current.BaselineBytes -eq 4336 -and $current.CurrentBytes -eq 2890 -and $current.CurrentBootId -ceq '2026-10-20T22:51:15.5000000Z') 'The 224018 selector, journal, firmware-length, or reset pins are incomplete.'
    $goodSelector = [pscustomobject]@{ Sha256=$current.SelectorSha256; Bytes=$current.SelectorBytes }
    $goodJournal = [pscustomobject]@{ Sha256=$current.JournalSha256; Bytes=$current.JournalBytes }
    Assert-GpuFinalFirmwareInspectionEntryPins -Profile $current -SelectorPin $goodSelector -JournalPin $goodJournal
    $badSelector = [pscustomobject]@{ Sha256='wrong'; Bytes=$current.SelectorBytes }
    Assert-Throws { Assert-GpuFinalFirmwareInspectionEntryPins -Profile $current -SelectorPin $badSelector -JournalPin $goodJournal } 'selector.*pin' 'A selector hash mismatch was accepted.'
    $badJournal = [pscustomobject]@{ Sha256=$current.JournalSha256; Bytes=1 }
    Assert-Throws { Assert-GpuFinalFirmwareInspectionEntryPins -Profile $current -SelectorPin $goodSelector -JournalPin $badJournal } 'journal.*pin' 'A failed journal length mismatch was accepted.'
}

Invoke-TestCase 'journal validator accepts only the pinned failed FinalVerify evidence' {
    . $ScriptPath -WhatIf | Out-Null
    $fixture = New-ValidJournalFixture -Root ([IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..')))
    Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $fixture.SourceRoot -ExpectedRunDirectory $fixture.RunDirectory
    Assert-Throws { $fixture.Journal.Phase='Complete'; Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $fixture.SourceRoot -ExpectedRunDirectory $fixture.RunDirectory } 'disarmed' 'Completed run phase was accepted'
    $fixture = New-ValidJournalFixture -Root ([IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..')))
    $fixture.Journal.FinalVerification.Errors=@('unrelated error')
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $fixture.SourceRoot -ExpectedRunDirectory $fixture.RunDirectory } 'single firmware-text mismatch' 'Different FinalVerify error was accepted'
}

Invoke-TestCase '224018 journal accepts the closed failure and rejects identity, source, action, reset, hash, and error drift' {
    . $ScriptPath -Run224018 -WhatIf | Out-Null
    $root = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
    $fixture = New-ValidJournalFixture -Root $root -Run224018
    Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run224018

    $fixture = New-ValidJournalFixture -Root $root -Run224018
    $fixture.Selector.RunId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run224018 } 'fixed 224018' 'A wrong selector run was accepted.'

    $fixture = New-ValidJournalFixture -Root $root -Run224018
    $fixture.Journal.RunId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run224018 } 'journal identity' 'A wrong journal run was accepted.'

    $fixture = New-ValidJournalFixture -Root $root -Run224018
    $fixture.Journal.SourceCommit = 'wrong-source'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run224018 } 'source revision' 'A wrong source commit was accepted.'

    $fixture = New-ValidJournalFixture -Root $root -Run224018
    $fixture.Journal.LastAction.Directory = Join-Path (Split-Path -Parent $fixture.Journal.LastAction.Directory) 'other-final-verify'
    $fixture.Journal.InProgress.Directory = $fixture.Journal.LastAction.Directory
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run224018 } 'fixed failed FinalVerify' 'A wrong failed action was accepted.'

    $fixture = New-ValidJournalFixture -Root $root -Run224018
    $fixture.Journal.DisarmedBootId = 'wrong-disarm-boot'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run224018 } 'disarmed state' 'A wrong disarm BootId was accepted.'

    $fixture = New-ValidJournalFixture -Root $root -Run224018
    $fixture.Journal.FinalVerification.BootId = 'wrong-current-boot'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run224018 } 'reset, health, ESP' 'A wrong current BootId was accepted.'

    $fixture = New-ValidJournalFixture -Root $root -Run224018
    $fixture.Journal.FinalVerification.Health.CpuPassCount = 1
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run224018 } 'absent-state result' 'A changed CPU preparation count was accepted.'

    $fixture = New-ValidJournalFixture -Root $root -Run224018
    $fixture.Journal.FinalVerification.FirmwareEnumeration.CurrentSha256 = 'wrong-firmware'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run224018 } 'single firmware-text mismatch' 'A changed failed firmware hash was accepted.'

    $fixture = New-ValidJournalFixture -Root $root -Run224018
    $fixture.Journal.FinalVerification.Errors += 'extra error'
    Assert-Throws { Assert-GpuFinalFirmwareInspectionJournal -Selector $fixture.Selector -Journal $fixture.Journal -ExpectedSourceRoot $root -ExpectedRunDirectory $fixture.RunDirectory -Run224018 } 'single firmware-text mismatch' 'Extra journal errors were accepted.'
}

Invoke-TestCase 'observation success captures before and after and always dismounts' {
    . $ScriptPath -WhatIf | Out-Null
    $behavior=@{ MountCount=0; FirmwareCount=0; BootCount=0; DismountCount=0; HealthFails=$false; BootChanges=$false; FirmwareFails=$false; FirmwareChanges=$false; DismountFails=$false }
    $operations=New-MockObservationOperations $behavior
    $result=Invoke-GpuFinalFirmwareObservation -State ([pscustomobject]@{ Backup=[pscustomobject]@{} }) -ActionDirectory 'fixture-actions' -Operations $operations
    Assert-True ($result.Succeeded -and $result.Status -ceq 'READONLY_FIRMWARE_REVIEW_CAPTURED') 'Mock observation did not succeed.'
    Assert-True ($result -isnot [array]) 'Observation helper emitted more than one result object.'
    Assert-True ($result.EspUnmounted -and $result.CleanupSucceeded -and $behavior.DismountCount -eq 1) 'Owned ESP mount was not cleaned up.'
    Assert-True ([object]::ReferenceEquals($behavior.Mount,$behavior.DismountMount)) 'Dismount did not receive the exact owned mount object.'
    Assert-True ($behavior.FirmwareCount -eq 2 -and $behavior.BootCount -eq 2 -and $result.RawBootSelectionStable) 'Expected before/after captures were not made.'
}

Invoke-TestCase 'observation failure preserves failure and still dismounts' {
    . $ScriptPath -WhatIf | Out-Null
    $behavior=@{ MountCount=0; FirmwareCount=0; BootCount=0; DismountCount=0; HealthFails=$true; BootChanges=$false; FirmwareFails=$false; FirmwareChanges=$false; DismountFails=$false }
    $operations=New-MockObservationOperations $behavior
    $result=Invoke-GpuFinalFirmwareObservation -State ([pscustomobject]@{ Backup=[pscustomobject]@{} }) -ActionDirectory 'fixture-actions' -Operations $operations
    Assert-True (-not $result.Succeeded -and $result.Errors.Count -eq 1 -and $result.Errors[0] -match 'health failure') 'Injected health failure was lost.'
    Assert-True ($result.EspUnmounted -and $result.CleanupSucceeded -and $behavior.DismountCount -eq 1) 'Mount cleanup was skipped after capture failure.'
}

Invoke-TestCase 'cleanup failure is reported and prevents success' {
    . $ScriptPath -WhatIf | Out-Null
    $behavior=@{ MountCount=0; FirmwareCount=0; BootCount=0; DismountCount=0; HealthFails=$false; BootChanges=$false; FirmwareFails=$false; FirmwareChanges=$false; DismountFails=$true }
    $operations=New-MockObservationOperations $behavior
    $result=Invoke-GpuFinalFirmwareObservation -State ([pscustomobject]@{ Backup=[pscustomobject]@{} }) -ActionDirectory 'fixture-actions' -Operations $operations
    Assert-True (-not $result.Succeeded -and -not $result.CleanupSucceeded -and -not $result.EspUnmounted) 'Unmount failure did not block success.'
    Assert-True ($result.CleanupErrors.Count -eq 1 -and $result.CleanupErrors[0] -match 'unmount failure') 'Unmount failure evidence was lost.'
}

Invoke-TestCase 'raw boot-selection mismatch still dismounts the owned ESP' {
    . $ScriptPath -WhatIf | Out-Null
    $behavior=@{ MountCount=0; FirmwareCount=0; BootCount=0; DismountCount=0; HealthFails=$false; BootChanges=$true; FirmwareFails=$false; FirmwareChanges=$false; DismountFails=$false }
    $operations=New-MockObservationOperations $behavior
    $result=Invoke-GpuFinalFirmwareObservation -State ([pscustomobject]@{ Backup=[pscustomobject]@{} }) -ActionDirectory 'fixture-actions' -Operations $operations
    Assert-True (-not $result.Succeeded -and $result.Errors[0] -match 'BootOrder changed') 'Boot-selection mismatch was not retained.'
    Assert-True ($result.EspUnmounted -and $behavior.DismountCount -eq 1 -and [object]::ReferenceEquals($behavior.Mount,$behavior.DismountMount)) 'Raw-capture failure did not dismount the exact owned mount.'
}

Invoke-TestCase 'second firmware capture failure still dismounts the owned ESP' {
    . $ScriptPath -WhatIf | Out-Null
    $behavior=@{ MountCount=0; FirmwareCount=0; BootCount=0; DismountCount=0; HealthFails=$false; BootChanges=$false; FirmwareFails=$true; FirmwareChanges=$false; DismountFails=$false }
    $operations=New-MockObservationOperations $behavior
    $result=Invoke-GpuFinalFirmwareObservation -State ([pscustomobject]@{ Backup=[pscustomobject]@{} }) -ActionDirectory 'fixture-actions' -Operations $operations
    Assert-True (-not $result.Succeeded -and $result.Errors[0] -match 'firmware capture failure') 'Second firmware capture failure was not retained.'
    Assert-True ($result.EspUnmounted -and $behavior.DismountCount -eq 1 -and [object]::ReferenceEquals($behavior.Mount,$behavior.DismountMount)) 'Firmware capture failure did not dismount the exact owned mount.'
}

Invoke-TestCase 'firmware text drift between observations fails and dismounts the owned ESP' {
    . $ScriptPath -WhatIf | Out-Null
    $behavior=@{ MountCount=0; FirmwareCount=0; BootCount=0; DismountCount=0; HealthFails=$false; BootChanges=$false; FirmwareFails=$false; FirmwareChanges=$true; DismountFails=$false }
    $operations=New-MockObservationOperations $behavior
    $result=Invoke-GpuFinalFirmwareObservation -State ([pscustomobject]@{ Backup=[pscustomobject]@{} }) -ActionDirectory 'fixture-actions' -Operations $operations
    Assert-True (-not $result.Succeeded -and $result.Errors[0] -match 'firmware text changed') 'Firmware text drift did not fail the observation.'
    Assert-True ($result.EspUnmounted -and $behavior.DismountCount -eq 1) 'Firmware text drift did not clean up the owned ESP mount.'
}

$summary = [pscustomobject]@{ PassedCases=$script:PassedCases; FailedCases=$script:FailedCases.ToArray(); Assertions=$script:Assertions }
$summary | ConvertTo-Json -Depth 5
if ($script:FailedCases.Count -gt 0) { exit 1 }
