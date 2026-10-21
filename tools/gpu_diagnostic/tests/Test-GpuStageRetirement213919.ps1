[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
}
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$modulePath = Join-Path $ToolRoot 'GpuStageRetirement.psm1'
$wrapperPath = Join-Path $ToolRoot 'Retire-GpuCompletedStage.ps1'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot '..\..\..'))
$script:Assertions = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}

function Assert-Throws {
    param([scriptblock]$Operation, [string]$MessagePattern, [string]$Message)
    $observed = $null
    try { $null = & $Operation } catch { $observed = [string]$_.Exception.Message }
    Assert-True ($null -ne $observed -and $observed -match $MessagePattern) "$Message (observed: $observed)"
}

$module = Import-Module -Name $modulePath -Force -PassThru -ErrorAction Stop | Select-Object -First 1
$state = & $module {
    $values = @{}
    foreach ($variable in Get-Variable -Scope Script) {
        if ($variable.Name -like 'GpuRetirement213919*') { $values[$variable.Name] = $variable.Value }
    }
    return ,$values
}
$readinessPath = Join-Path $workspaceRoot ([string]$state.GpuRetirement213919ReadinessRelativePath)

# WhatIf validates the wrapper contract and returns before module selection, probes,
# host commands, ESP access, or evidence writes.
$preview = (& $wrapperPath -ToolRoot $ToolRoot -CompletedRun213919 -ReadinessPath $readinessPath -WhatIf | Out-String) | ConvertFrom-Json
Assert-True ($preview.Preview -eq $true -and $preview.FilesystemChangesPerformed -eq $false -and
    $preview.NativeCommandsExecuted -eq $false -and $preview.EspMounted -eq $false -and
    $preview.CompletedRun213919 -eq $true) 'The 213919 WhatIf preview did not report a no-effect profile preview.'
Assert-Throws { & $wrapperPath -ToolRoot $ToolRoot -CompletedRun213919 -WhatIf } 'explicit EvidencePath' 'The wrapper accepted a profile without readiness evidence.'
Assert-Throws { & $wrapperPath -ToolRoot $ToolRoot -CompletedRun213919 -CompletedRun193833 -ReadinessPath $readinessPath -WhatIf } 'mutually exclusive' 'The wrapper accepted conflicting immutable profiles.'
Assert-Throws { & $wrapperPath -ToolRoot $ToolRoot -ReadinessPath $readinessPath -WhatIf } 'only with an explicit completed-run profile' 'The wrapper accepted a readiness path without a profile.'

# The selector accepts only the captured readiness pathname. Use an existing regular
# source file so the safe-path check completes before the immutable-profile check.
Assert-Throws {
    & $module { param($root,$path) Select-GpuStageRetirement213919Profile -WorkspaceRoot $root -ReadinessPath $path } $sourceRoot $modulePath
} 'fixed 213919 ownership-review report' 'The profile selector accepted a different readiness report.'

function New-JournalEnvelope {
    param([string]$Failure = '')
    $runDirectory = Join-Path (Join-Path $sourceRoot '.analysis\gpu-baremetal\runs') $state.GpuRetirement213919RunId
    $finalDirectory = Join-Path $sourceRoot $state.GpuRetirement213919FinalActionRelativePath
    $status = if ($Failure -ceq 'status') { 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED' } else { 'FINAL_RESET_VERIFIED_GPU_UNATTESTED' }
    $directory = if ($Failure -ceq 'directory') { Join-Path $sourceRoot '.analysis\wrong-final-action' } else { $finalDirectory }
    return [pscustomobject]@{
        SchemaVersion=1; RunId=$state.GpuRetirement213919RunId; SourceCommit=$state.GpuRetirement213919SourceCommit
        RunDirectory=$runDirectory; Phase='Complete'; InProgress=$null; LastFailure=$null
        DisarmedBootId=$state.GpuRetirement213919DisarmedBootId
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Status=$status; Succeeded=$true; Directory=$directory }
    }
}

$validEnvelope = New-JournalEnvelope
$envelopeResult = & $module { param($journal,$root) Assert-GpuStageRetirement213919JournalEnvelope -Journal $journal -WorkspaceRoot $root } $validEnvelope $sourceRoot
Assert-True ([IO.Path]::GetFullPath($envelopeResult.FinalActionDirectory) -ceq [IO.Path]::GetFullPath((Join-Path $sourceRoot $state.GpuRetirement213919FinalActionRelativePath))) 'The valid exact final-action envelope was rejected or rebound.'
foreach ($failure in @('status','directory')) {
    $tampered = New-JournalEnvelope -Failure $failure
    Assert-Throws {
        & $module { param($journal,$root) Assert-GpuStageRetirement213919JournalEnvelope -Journal $journal -WorkspaceRoot $root } $tampered $sourceRoot
    } 'exact closed, successfully reset run' "A journal with a tampered LastAction $failure was accepted."
    Assert-Throws {
        & $module { param($journal,$root) Assert-GpuStageRetirement213919Proof -Journal $journal -WorkspaceRoot $root } $tampered $sourceRoot
    } 'exact closed, successfully reset run' "The full proof accepted a journal with a tampered LastAction $failure."
}

$validCapture = [pscustomobject]@{ Baseline=[pscustomobject]@{
    Schema=1; BootNext=$null; BootOrder=@(0,1,2); BootManagerSha256=$state.GpuRetirement213919BootManagerSha256
    ReferenceLogSha256=$state.GpuRetirement213919ReferenceLogSha256; IntegrityFindings='00000000'
} }
$null = & $module { param($capture) Assert-GpuStageRetirement213919BootSelection -Capture $capture -Checkpoint 'fixture' } $validCapture
foreach ($mutant in @(
    [pscustomobject]@{ Name='BootNext'; Capture=[pscustomobject]@{ Baseline=[pscustomobject]@{ Schema=1; BootNext=2; BootOrder=@(0,1,2); BootManagerSha256=$state.GpuRetirement213919BootManagerSha256; ReferenceLogSha256=$state.GpuRetirement213919ReferenceLogSha256; IntegrityFindings='00000000' } } },
    [pscustomobject]@{ Name='BootOrder'; Capture=[pscustomobject]@{ Baseline=[pscustomobject]@{ Schema=1; BootNext=$null; BootOrder=@(0,2,1); BootManagerSha256=$state.GpuRetirement213919BootManagerSha256; ReferenceLogSha256=$state.GpuRetirement213919ReferenceLogSha256; IntegrityFindings='00000000' } } },
    [pscustomobject]@{ Name='findings'; Capture=[pscustomobject]@{ Baseline=[pscustomobject]@{ Schema=1; BootNext=$null; BootOrder=@(0,1,2); BootManagerSha256=$state.GpuRetirement213919BootManagerSha256; ReferenceLogSha256=$state.GpuRetirement213919ReferenceLogSha256; IntegrityFindings='00000001' } } }
)) {
    Assert-Throws {
        & $module { param($capture) Assert-GpuStageRetirement213919BootSelection -Capture $capture -Checkpoint 'fixture' } $mutant.Capture
    } 'raw boot-selection|BootOrder|BootNext' "A tampered current raw $($mutant.Name) value was accepted."
}

Write-Output "PASS: 213919 retirement selection guards, exact proof envelope, raw boot checks, and no-effect preview ($script:Assertions assertions)"
