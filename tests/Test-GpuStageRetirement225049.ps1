Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Assert-True {
    param([Parameter(Mandatory = $true)][bool]$Condition, [Parameter(Mandatory = $true)][string]$Message)
    if (-not $Condition) { throw $Message }
}

function ConvertTo-IndependentObject {
    param([Parameter(Mandatory = $true)][object]$Value)
    return (ConvertTo-Json -InputObject $Value -Depth 64 -Compress | ConvertFrom-Json -ErrorAction Stop)
}

function Assert-Rejected {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [string]$ExpectedMessage
    )
    try {
        & $Action
    }
    catch {
        if (-not [string]::IsNullOrWhiteSpace($ExpectedMessage) -and $_.Exception.Message -notmatch $ExpectedMessage) {
            throw "$Name was rejected for an unexpected reason: $($_.Exception.Message)"
        }
        $script:rejectedMutations++
        Write-Output "Rejected tamper: $Name"
        return
    }
    throw "Tampered evidence unexpectedly passed: $Name"
}

$script:rejectedMutations = 0
$sourceRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot '..\..\..'))
$toolRoot = Join-Path $sourceRoot 'tools\gpu_diagnostic'
$readinessPath = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness\d34e0d3f9e9d45679c79529c3fc9545b\readiness.json'
$runId = 'gpu-20261021T225049Z-9ac846fd923441cf9b7f0a75a2e0d246'
$runDirectory = Join-Path $workspaceRoot ".analysis\gpu-baremetal\runs\$runId"
$journalPath = Join-Path $runDirectory 'journal.json'
$archivePath = Join-Path $workspaceRoot ".analysis\gpu-baremetal\archive\$runId.json"
$modulePath = Join-Path $toolRoot 'GpuStageRetirement.psm1'
$wrapperPath = Join-Path $toolRoot 'Retire-GpuCompletedStage.ps1'

foreach ($path in @($readinessPath,$journalPath,$archivePath,$modulePath,$wrapperPath)) {
    if (-not [IO.File]::Exists($path)) { throw "Required 225049 proof input is missing: $path" }
}

# Load the module source as a script module without invoking host helpers or relying on
# machine execution-policy settings. The selected proof and every fixture remain read-only.
$retirementModule = New-Module -Name GpuStageRetirement225049Tests `
    -ScriptBlock ([scriptblock]::Create([IO.File]::ReadAllText($modulePath)))
$readiness = [IO.File]::ReadAllText($readinessPath) | ConvertFrom-Json -ErrorAction Stop
$journal = [IO.File]::ReadAllText($journalPath) | ConvertFrom-Json -ErrorAction Stop
$archive = [IO.File]::ReadAllText($archivePath) | ConvertFrom-Json -ErrorAction Stop

$selection = & $retirementModule {
    param($root,$report)
    Select-GpuStageRetirement225049Profile -WorkspaceRoot $root -ReadinessPath $report
} $workspaceRoot $readinessPath
Assert-True ($selection.Profile -ceq '225049') 'The fixed readiness report did not select profile 225049.'
Assert-True (@($selection.StagePins).Count -eq 5) 'The profile did not bind exactly five stage pins.'
Assert-True ($selection.ReadinessPin.Sha256 -ceq 'D3E076C8A2CF92A24A141DD2436F1432A6F8A7C3DE757784629DBAA668E9111D') 'The readiness evidence pin changed.'
Assert-True ($selection.JournalSha256 -ceq 'B03B24FD49A04E51D5CE66F10AE43968AF39D17031F28DFE088FD341BFFEB66C') 'The selected immutable journal hash changed.'
Assert-True ($selection.JournalBytes -eq 58405) 'The selected immutable journal byte count changed.'
Assert-True ($selection.ArchiveSha256 -ceq '7BB8B49C1BDDB0A702EAD9EB48FEA88CBD37530FFB7D1EDDAA62CA3FEEB7E707') 'The selected immutable archive hash changed.'
Assert-True ($selection.ArchiveBytes -eq 533) 'The selected immutable archive byte count changed.'

$unselectedModule = New-Module -Name GpuStageRetirement225049PathGateTests `
    -ScriptBlock ([scriptblock]::Create([IO.File]::ReadAllText($modulePath)))
Assert-Rejected 'profile readiness path binding' {
    & $unselectedModule {
        param($root,$path)
        Select-GpuStageRetirement225049Profile -WorkspaceRoot $root -ReadinessPath $path
    } $workspaceRoot $modulePath
} 'fixed 225049 ownership-review report'

Assert-Rejected 'module profile mutual exclusion' {
    & $retirementModule {
        param($root,$path)
        Select-GpuStageRetirement213919Profile -WorkspaceRoot $root -ReadinessPath $path
    } $workspaceRoot $readinessPath
} 'already bound'

$journalPin = & $retirementModule {
    param($path)
    Get-GpuStageRetirementPin -Path $path
} $journalPath
Assert-True ($journalPin.Sha256 -ceq $selection.JournalSha256 -and $journalPin.Bytes -eq $selection.JournalBytes) 'The immutable journal file differs from the selected profile pin.'
$archivePin = & $retirementModule {
    param($path)
    Get-GpuStageRetirementPin -Path $path
} $archivePath
Assert-True ($archivePin.Sha256 -ceq $selection.ArchiveSha256 -and $archivePin.Bytes -eq $selection.ArchiveBytes) 'The archived selector file differs from the selected profile pin.'

$expectedStage = & $retirementModule {
    param($audit,$run,$selector,$root,$runPath,$journalFile,$source,$auditHash)
    Assert-GpuStageRetirementEvidence -Audit $audit -Journal $run -ArchivedSelector $selector `
        -ExpectedAuditSha256 $auditHash -ExpectedRunId 'gpu-20261021T225049Z-9ac846fd923441cf9b7f0a75a2e0d246' `
        -ExpectedRunDirectory $runPath -ExpectedJournalPath $journalFile -ExpectedSourceRoot $source `
        -ExpectedWorkspaceRoot $root
} $readiness $journal $archive $workspaceRoot $runDirectory $journalPath $sourceRoot `
    'D3E076C8A2CF92A24A141DD2436F1432A6F8A7C3DE757784629DBAA668E9111D'
Assert-True (@($expectedStage).Count -eq 5) 'The generic evidence gate did not accept exactly five reviewed stage files.'

$proofPins = @(& $retirementModule {
    param($run,$root)
    Assert-GpuStageRetirement225049Proof -Journal $run -WorkspaceRoot $root
} $journal $workspaceRoot)
Assert-True ($proofPins.Count -eq 33) "Expected 33 actual evidence pin checks; received $($proofPins.Count)."
$expectedEvidence = @{
    'result.json' = '8FDDC52382B01FE2E73E44C8F39DB6854656FE8282081858FE352CA9F43B740F'
    'firmware-enumeration-final-0aa6df99947f4aefbfc0a53f90c3972d.stdout.txt' = '50B7333C35660951D297DFEEFBFDA38DCE53F2E9CED02AB2857C47F8085BA4AB'
    'firmware-enumeration-final-0aa6df99947f4aefbfc0a53f90c3972d.receipt.json' = 'E35A41DD1A6D8FB45E52DC3C213321AA468DAE6FD6A77045C4E78E1B913DF7BD'
    'firmware-final-before-boot-selection.json' = '5719D30E529C6B2E8AB659F4FE2E710028BBBF039897B04EB37F7D43766FA2C5'
    'firmware-final-after-boot-selection.json' = '5719D30E529C6B2E8AB659F4FE2E710028BBBF039897B04EB37F7D43766FA2C5'
    'esp-grubx64.efi' = 'B2F4640D508CF7BF3244E381EB650DEA1EA8A535589A8088DA3928F6CC8B98A3'
    'esp-manifest.sha256' = '9BA0943C040642780703AE6B24F95ADFDB966704261FC0B27D18FDF226AD86AC'
    'esp-payload.efi' = '25A5FEBF01756DD1A0D941FB7B3392B90278B022B87688FC46255313E4AA6E81'
    'esp-shimx64.efi' = '6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B'
    'esp-tcglog.ib' = '4A872424148E1D0DD13A2475FAD63C2FB5DB721C8812E5A9F05FF9EB4F9BD88E'
}
foreach ($leaf in $expectedEvidence.Keys) {
    $matches = @($proofPins | Where-Object { [IO.Path]::GetFileName([string]$_.Path) -ceq $leaf })
    Assert-True ($matches.Count -ge 1) "The actual proof omitted required evidence file $leaf."
    foreach ($pin in $matches) { Assert-True ($pin.Sha256 -ceq $expectedEvidence[$leaf]) "The proof returned a changed pin for $leaf." }
}

$mutated = ConvertTo-IndependentObject $journal
$mutated.FinalVerification.FirmwareEnumeration.ComparisonPolicy.Accepted = $false
Assert-Rejected 'semantic firmware-policy acceptance' {
    & $retirementModule { param($run,$root) Assert-GpuStageRetirement225049Proof -Journal $run -WorkspaceRoot $root } $mutated $workspaceRoot
}

$mutated = ConvertTo-IndependentObject $journal
$mutated.SourcePins[0].Sha256 = ('0' * 64)
Assert-Rejected 'archived source pin' {
    & $retirementModule { param($run,$root) Assert-GpuStageRetirement225049Proof -Journal $run -WorkspaceRoot $root } $mutated $workspaceRoot
}

$mutated = ConvertTo-IndependentObject $journal
$stageRow = $mutated.Collection.Esp.StageFiles | Where-Object { $_.Name -ceq 'payload.efi' }
$stageRow.Pin.Sha256 = ('0' * 64)
Assert-Rejected 'collected stage-file snapshot' {
    & $retirementModule { param($run,$root) Assert-GpuStageRetirement225049Proof -Journal $run -WorkspaceRoot $root } $mutated $workspaceRoot
}

$mutated = ConvertTo-IndependentObject $journal
$diagnosticRow = $mutated.Collection.Esp.StageFiles | Where-Object { $_.Name -ceq 'handoff-status.txt' }
$diagnosticRow.Present = $true
Assert-Rejected 'explicitly absent diagnostic row' {
    & $retirementModule { param($run,$root) Assert-GpuStageRetirement225049Proof -Journal $run -WorkspaceRoot $root } $mutated $workspaceRoot
}

$mutated = ConvertTo-IndependentObject $journal
$mutated.FinalVerification.FirmwareEnumeration.ComparisonPolicy.RawAfter.Snapshot.BootOrder[0] = 1
Assert-Rejected 'raw after-capture BootOrder' {
    & $retirementModule { param($run,$root) Assert-GpuStageRetirement225049Proof -Journal $run -WorkspaceRoot $root } $mutated $workspaceRoot
}

$mutated = ConvertTo-IndependentObject $journal
$mutated.FinalVerification.FirmwareEnumeration.CurrentSha256 = ('0' * 64)
Assert-Rejected 'current firmware capture binding' {
    & $retirementModule { param($run,$root) Assert-GpuStageRetirement225049Proof -Journal $run -WorkspaceRoot $root } $mutated $workspaceRoot
}

$mutatedReadiness = ConvertTo-IndependentObject $readiness
$mutatedReadiness.Source.Commit = '0000000000000000000000000000000000000000'
Assert-Rejected 'readiness source ownership commit' {
    & $retirementModule {
        param($report,$path,$root)
        Assert-GpuStageRetirement225049Readiness -Readiness $report -ReadinessFullPath $path -WorkspaceRoot $root
    } $mutatedReadiness $readinessPath $workspaceRoot
}

$mutatedReadiness = ConvertTo-IndependentObject $readiness
$mutatedReadiness.Source.Pins[0].Sha256 = ('0' * 64)
Assert-Rejected 'readiness source-file pin' {
    & $retirementModule {
        param($report,$path,$root)
        Assert-GpuStageRetirement225049Readiness -Readiness $report -ReadinessFullPath $path -WorkspaceRoot $root
    } $mutatedReadiness $readinessPath $workspaceRoot
}

$mutatedReadiness = ConvertTo-IndependentObject $readiness
$mutatedReadiness.Recovery.Disk.UniqueId = 'USBSTOR\\TAMPERED'
Assert-Rejected 'external recovery-device ownership identity' {
    & $retirementModule {
        param($report,$path,$root)
        Assert-GpuStageRetirement225049Readiness -Readiness $report -ReadinessFullPath $path -WorkspaceRoot $root
    } $mutatedReadiness $readinessPath $workspaceRoot
}

$mutatedReadiness = ConvertTo-IndependentObject $readiness
$mutatedReadiness.Esp.MountStateAfterCleanup = 'Mounted'
Assert-Rejected 'ESP cleanup and unmounted ownership gate' {
    & $retirementModule {
        param($report,$path,$root)
        Assert-GpuStageRetirement225049Readiness -Readiness $report -ReadinessFullPath $path -WorkspaceRoot $root
    } $mutatedReadiness $readinessPath $workspaceRoot
}

$wrapper = [scriptblock]::Create([IO.File]::ReadAllText($wrapperPath))
$previewJson = & $wrapper -WhatIf -CompletedRun225049 -ReadinessPath $readinessPath -ToolRoot $toolRoot
$preview = [string]$previewJson | ConvertFrom-Json -ErrorAction Stop
Assert-True ($preview.Preview -eq $true -and $preview.CompletedRun225049 -eq $true) 'The 225049 WhatIf preview did not identify the selected profile.'
Assert-True ($preview.FilesystemChangesPerformed -eq $false -and $preview.NativeCommandsExecuted -eq $false -and $preview.EspMounted -eq $false) 'The 225049 WhatIf preview advertised a side effect.'
Assert-True ([string]$preview.Note -match 'five pinned files') 'The 225049 WhatIf preview did not describe the five-file profile.'

Assert-Rejected 'wrapper profile mutual exclusion' {
    & $wrapper -WhatIf -CompletedRun213919 -CompletedRun225049 -ReadinessPath $readinessPath -ToolRoot $toolRoot | Out-Null
}

Assert-Rejected 'wrapper explicit readiness requirement' {
    & $wrapper -WhatIf -CompletedRun225049 -ToolRoot $toolRoot | Out-Null
}

Write-Output "225049 evidence proof passed: $($proofPins.Count) returned file pins; $(@($expectedStage).Count) stage rows; $($script:rejectedMutations) tampered inputs rejected; WhatIf and exclusivity gates passed."
