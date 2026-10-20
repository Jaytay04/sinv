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
    param([string]$Root)
    $runId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
    $run = Join-Path $Root ('.analysis\gpu-baremetal\runs\' + $runId)
    $action = Join-Path (Join-Path $run 'actions') '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'
    $selector = [pscustomobject]@{ SchemaVersion=1; RunId=$runId; RunDirectory=$run; SourceRoot=$Root }
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$runId; SourceCommit='933b4c676318f26396cdf2c66de01c3ccbf1a950'
        SourceRoot=$Root; RunDirectory=$run; Phase='Disarmed'; EverArmIntent=$true; DisarmedBootId='old-boot'
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$false; Directory=$action }
        InProgress=[pscustomobject]@{ Action='FinalVerify'; Directory=$action }
        FinalVerification=[pscustomobject]@{
            ResetBoundaryPassed=$true; SecureBoot=$true; ActivationAttempted=$true
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; ExpectedCpus=20; CpuCount=20 }
            Esp=[pscustomobject]@{ MatchesBaseline=$true; BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511' }
            FirmwareEnumeration=[pscustomobject]@{
                TextEqual=$false; BaselineSha256='ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE'
                CurrentSha256='4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'; CaptureContext='SystemEspMountedAtS'
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
    Assert-True ($preview.Status -ceq 'PREVIEW_ONLY' -and $preview.NativeCommandsExecuted -eq $false -and $preview.FilesystemChangesPerformed -eq $false) 'WhatIf did not return the no-effects preview.'
    $after = if (Test-Path -LiteralPath $reviewRoot) { @(Get-ChildItem -LiteralPath $reviewRoot -Force | ForEach-Object Name | Sort-Object) -join ';' } else { '<missing>' }
    Assert-True ($before -ceq $after) 'WhatIf changed the evidence directory contents.'
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

$summary = [pscustomobject]@{ PassedCases=$script:PassedCases; FailedCases=$script:FailedCases.ToArray(); Assertions=$script:Assertions }
$summary | ConvertTo-Json -Depth 5
if ($script:FailedCases.Count -gt 0) { exit 1 }
