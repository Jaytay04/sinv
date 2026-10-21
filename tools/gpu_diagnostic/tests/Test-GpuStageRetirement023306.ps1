[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$modulePath = Join-Path $ToolRoot 'GpuStageRetirement.psm1'
$scriptPath = Join-Path $ToolRoot 'Retire-GpuCompletedStage.ps1'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
$fixtureParent = [IO.Path]::GetFullPath((Join-Path $sourceRoot '.codex-build'))
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $fixtureParent ('gpu-stage-retirement-023306-tests-' + [guid]::NewGuid().ToString('N'))))
$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = [Collections.Generic.List[string]]::new()

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

function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)
    try { & $Body; $script:PassedCases++; Write-Output "PASS: $Name" }
    catch { $script:FailedCases.Add("$Name`: $($_.Exception.Message)"); Write-Output "FAIL: $Name`: $($_.Exception.Message)" }
}

function Import-TestModule {
    return Import-Module $modulePath -Force -PassThru -ErrorAction Stop | Select-Object -First 1
}

function Write-TestJson {
    param([string]$Path, [object]$Value)
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 24 -Compress) + "`n", [Text.UTF8Encoding]::new($false))
}

function New-ReadinessFixture {
    param([string]$Root, [hashtable]$Changes = @{})
    $guid = 'a023306fe1204ff2a2af7a8f8a9a5a10'
    $directory = Join-Path $Root ('.analysis\gpu-baremetal-readiness\' + $guid)
    $path = Join-Path $directory 'readiness.json'
    $report = [pscustomobject]@{
        SchemaVersion=1; Status='BLOCKED_READINESS'; Succeeded=$false
        EvidencePath=$path; EvidenceDirectory=$directory
        Selector=[pscustomobject]@{ Exists=$false; Valid=$true; Error=$null }
        Cleanup=[pscustomobject]@{ Succeeded=$true; Errors=@() }
        Esp=[pscustomobject]@{ Inspected=$true; MountStateAfterCleanup='Unmounted' }
        Effects=[pscustomobject]@{ EspFilesChanged=$false; FirmwareChanged=$false; BootStateChanged=$false; EspTemporarilyMounted=$true }
        ExistingStage=[pscustomobject]@{ Exists=$true; Complete=$true }
    }
    foreach ($key in $Changes.Keys) { $report.$key = $Changes[$key] }
    Write-TestJson $path $report
    return @{ Path=$path; Report=$report; Guid=$guid }
}

function New-ProofJournalFixture {
    param([string]$Root, [hashtable]$Changes = @{})
    $runId = 'gpu-20261021T023306Z-f33dfae1e322470ca9315bbbf8d49185'
    $runDirectory = Join-Path (Join-Path $Root '.analysis\gpu-baremetal\runs') $runId
    $finalAction = Join-Path $runDirectory 'actions\20261021T024702343Z-FinalVerify-d4bc5edd8707406f8b22c2fa341b6217'
    $backupDirectory = Join-Path $runDirectory 'actions\20261021T023634908Z-Backup-6667599ef49a4e44b62e049ac3f02ffa'
    $journal = [pscustomobject]@{
        RunId=$runId; RunDirectory=$runDirectory; SourceCommit='49fbf270093f5e65a3f00fd63077b113d662f301'
        Phase='Complete'; InProgress=$null; DisarmedBootId='fixture-disarm-boot'
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$true; Status='FINAL_RESET_VERIFIED_GPU_UNATTESTED'; Directory=$finalAction }
        Host=[pscustomobject]@{ LogicalCpus=20 }
        Backup=[pscustomobject]@{
            BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
            FirmwareEnumeration=[pscustomobject]@{
                Path=(Join-Path $backupDirectory 'firmware-enumeration-backup-f52e3a8fa4b1421ab4497119e1a3287a.stdout.txt')
                Sha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'; Bytes=2890
            }
            ReferenceLog=[pscustomobject]@{
                Path=(Join-Path $backupDirectory 'tcglog.ib')
                Sha256='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'; Bytes=40838
            }
        }
        Tools=[pscustomobject]@{ Probe=[pscustomobject]@{
            Path=(Join-Path $Root '.codex-build\gb\a9315bbbf8d49185\l\ib-tcg-dump.exe')
            Sha256='B03B7210D63494FAE909034406172CC76B97C48E6240B9CAFE593165F23FC5B2'; Bytes=172544
        } }
        FinalVerification=[pscustomobject]@{
            ResetBoundaryPassed=$true; ActivationAttempted=$true; SecureBoot=$true
            ActiveGpuProof=$false; GpuAttestation='UNATTESTED'; Errors=@()
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20; CpuPassCount=0 }
            Esp=[pscustomobject]@{ MatchesBaseline=$true; BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'; PartitionGuid='187BC913-7576-4E6E-8D54-18EEB5C38FCD'; DiskNumber=0 }
            FirmwareEnumeration=[pscustomobject]@{
                TextEqual=$true
                BaselineSha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
                CurrentSha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
                CaptureContext='SystemEspMountedAtS'
            }
        }
    }
    foreach ($key in $Changes.Keys) { $journal.$key = $Changes[$key] }
    return $journal
}

try {
    if (-not [string]::Equals([IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot)), $fixtureParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Temporary test fixture escaped its dedicated worktree .codex-build parent.'
    }
    [void][IO.Directory]::CreateDirectory($fixtureRoot)
    $workspaceRoot = Join-Path $fixtureRoot 'workspace'
    [void][IO.Directory]::CreateDirectory($workspaceRoot)

    Invoke-TestCase '023306 WhatIf remains a filesystem-free preview' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $before = @(Get-ChildItem -LiteralPath $workspaceRoot -Force -Recurse -ErrorAction SilentlyContinue).Count
        $fixtureToolRoot = Join-Path $workspaceRoot 'tools\gpu_diagnostic'
        $output = & $scriptPath -ToolRoot $fixtureToolRoot -CompletedRun023306 -ReadinessPath $fixture.Path -WhatIf | Out-String
        $preview = $output | ConvertFrom-Json
        $after = @(Get-ChildItem -LiteralPath $workspaceRoot -Force -Recurse -ErrorAction SilentlyContinue).Count
        Assert-True ($preview.Preview -and -not $preview.FilesystemChangesPerformed -and -not $preview.NativeCommandsExecuted -and -not $preview.EspMounted -and $preview.CompletedRun023306 -and $before -eq $after) 'WhatIf did not remain a side-effect-free preview.'
    }

    Invoke-TestCase '023306 switch is mutually exclusive with either older closed profile and requires fresh readiness' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        Assert-Throws { & $scriptPath -CompletedRun201140 -CompletedRun023306 -ReadinessPath $fixture.Path -WhatIf } 'mutually exclusive' '201140 and 023306 were allowed together.'
        Assert-Throws { & $scriptPath -CompletedRun224018 -CompletedRun023306 -ReadinessPath $fixture.Path -WhatIf } 'mutually exclusive' '224018 and 023306 were allowed together.'
        Assert-Throws { & $scriptPath -CompletedRun023306 -WhatIf } 'explicit EvidencePath' '023306 proceeded without a fresh explicit readiness path.'
    }

    Invoke-TestCase '023306 selection binds the fixed run and prevents cross-profile rebind' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $module = Import-TestModule
        $selected = & $module { param($root,$path) Select-GpuStageRetirement023306Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $fixture.Path
        $state = & $module { [pscustomobject]@{
            RunId=$script:GpuRetirementRunId; SourceCommit=$script:GpuRetirementSourceCommit; Profile=$script:GpuRetirementProfile
            Selected=$script:GpuRetirement023306ProfileSelected; JournalSha256=$script:GpuRetirementJournalSha256
            ArchiveSha256=$script:GpuRetirementArchiveSha256; ProbeSha256=$script:GpuRetirementProbeSha256
            FirmwareBytes=$script:GpuRetirement023306OriginalFirmwareBytes
        } }
        Assert-True ($selected.Profile -ceq '023306' -and $selected.RunId -ceq 'gpu-20261021T023306Z-f33dfae1e322470ca9315bbbf8d49185') 'Selection chose a different fixed run.'
        Assert-True ($selected.ReadinessPath -ceq [IO.Path]::GetFullPath($fixture.Path) -and $selected.ReadinessPin.Bytes -gt 0) 'Selection did not pin the explicit readiness report.'
        Assert-True ($state.Selected -and $state.Profile -ceq '023306' -and $state.SourceCommit -ceq '49fbf270093f5e65a3f00fd63077b113d662f301' -and $state.JournalSha256 -ceq 'AF913F6952E4614AB3A806DBC9C82667D62E5CA2CD955950030934D172DABAAC' -and $state.ArchiveSha256 -ceq 'A988AE8937E5E785DC3EF87B64791BA3D424F5F3577089136F653DAB4E5AADC7' -and $state.ProbeSha256 -ceq 'B03B7210D63494FAE909034406172CC76B97C48E6240B9CAFE593165F23FC5B2' -and $state.FirmwareBytes -eq 2890) 'Profile constants did not bind to the fixed archived evidence pins.'
        Assert-Throws { & $module { param($root,$path) Select-GpuStageRetirement201140Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $fixture.Path } 'already bound' 'The selected profile was rebound to 201140.'
        Assert-Throws { & $module { param($root,$path) Select-GpuStageRetirement224018Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $fixture.Path } 'already bound' 'The selected profile was rebound to 224018.'
    }

    Invoke-TestCase '023306 proof rejects wrong run, action, firmware equality, hash, bytes, and review override' {
        $module = Import-TestModule
        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.RunId = 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1'
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement023306Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'exact successful normal FinalVerify' 'A different completed run passed the profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.LastAction.Status = 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED'
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement023306Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'exact successful normal FinalVerify' 'A reviewed-difference completion passed the normal profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.FinalVerification.FirmwareEnumeration.CurrentSha256 = '0' * 64
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement023306Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'pinned original firmware baseline' 'A changed current firmware hash passed the profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.FinalVerification.FirmwareEnumeration.TextEqual = $false
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement023306Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'pinned original firmware baseline' 'A firmware text difference passed the profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.Backup.FirmwareEnumeration.Bytes = 2891
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement023306Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'path or pin differs' 'An incorrect original-baseline byte length passed the profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.FinalVerification.FirmwareEnumeration | Add-Member -NotePropertyName ReviewedDifferenceAccepted -NotePropertyValue $true
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement023306Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'pinned original firmware baseline' 'A reviewed-difference override passed the normal profile proof.'
    }

    Write-Output "Assertions: $script:Assertions"
    Write-Output "Passed cases: $script:PassedCases"
    if ($script:FailedCases.Count -gt 0) {
        Write-Output 'Failed cases:'
        foreach ($failure in $script:FailedCases) { Write-Output "- $failure" }
        exit 1
    }
    Write-Output 'All 023306 retirement contract tests passed.'
}
finally {
    $resolvedParent = [IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot))
    if ([string]::Equals($resolvedParent, $fixtureParent, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Directory]::Exists($fixtureRoot) -and
        ([IO.File]::GetAttributes($fixtureRoot) -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
