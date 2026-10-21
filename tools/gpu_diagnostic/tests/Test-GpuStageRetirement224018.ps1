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
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $fixtureParent ('gpu-stage-retirement-224018-tests-' + [guid]::NewGuid().ToString('N'))))
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
    $guid = 'a224018fe1204ff2a2af7a8f8a9a5a10'
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
    $runId = 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1'
    $runDirectory = Join-Path (Join-Path $Root '.analysis\gpu-baremetal\runs') $runId
    $finalAction = Join-Path $runDirectory 'actions\20261020T232519113Z-FinalVerify-5a786491a066456facce99b21b3bf06a'
    $journal = [pscustomobject]@{
        RunId=$runId; RunDirectory=$runDirectory; SourceCommit='69a3c3d94f33d1903778ef5ec5b0a1f015b1be29'
        Phase='Complete'; InProgress=$null
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$true; Status='FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED'; Directory=$finalAction }
        FinalVerification=[pscustomobject]@{
            ResetBoundaryPassed=$true; ActivationAttempted=$true; SecureBoot=$true; ActiveGpuProof=$false; GpuAttestation='UNATTESTED'
            Errors=@(); BootId='fixture-boot-id'
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20 }
            Esp=[pscustomobject]@{ MatchesBaseline=$true }
            FirmwareEnumeration=[pscustomobject]@{
                TextEqual=$false; ReviewedDifferenceAccepted=$true
                BaselineSha256='4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'
                CurrentSha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
                CaptureContext='SystemEspMountedAtS'
                ReviewedDifference=[pscustomobject]@{
                    Accepted=$true; HistoricalRawBootOrderUnchangedProven=$false; FullBootVariableNamespaceAttested=$false
                    ReceiptPin=[pscustomobject]@{ Path=(Join-Path $Root '.analysis\gpu-final-firmware-review\1dccd9d33d1b4c808fd1059291a6d19b\result.json'); Sha256='0E7107911CF43F4144F4E67AEBB647623003AF543FA38974EBEAE518F0D1642E'; Bytes=15415 }
                }
            }
        }
    }
    foreach ($key in $Changes.Keys) { $journal.$key = $Changes[$key] }
    return $journal
}

function New-BootCapture {
    param([object[]]$Order = @(0,1,2), [object]$BootNext = $null, [string]$Findings = '00000000')
    return [pscustomobject]@{ Baseline=[pscustomobject]@{
        Schema=1; BootNext=$BootNext; BootOrder=[uint16[]]$Order
        BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
        ReferenceLogSha256='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
        IntegrityFindings=$Findings
    } }
}

try {
    if (-not [string]::Equals([IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot)), $fixtureParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Temporary test fixture escaped its dedicated worktree .codex-build parent.'
    }
    [void][IO.Directory]::CreateDirectory($fixtureRoot)
    $workspaceRoot = Join-Path $fixtureRoot 'workspace'
    [void][IO.Directory]::CreateDirectory($workspaceRoot)

    Invoke-TestCase 'WhatIf for the fixed profile reports no probes or filesystem effects' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $output = & $scriptPath -CompletedRun224018 -ReadinessPath $fixture.Path -WhatIf | Out-String
        $preview = $output | ConvertFrom-Json
        Assert-True ($preview.Preview -and -not $preview.FilesystemChangesPerformed -and -not $preview.NativeCommandsExecuted -and -not $preview.EspMounted -and $preview.CompletedRun224018) 'WhatIf did not remain a side-effect-free preview.'
    }

    Invoke-TestCase 'the two fixed completed-run switches are mutually exclusive before preview' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        Assert-Throws { & $scriptPath -CompletedRun201140 -CompletedRun224018 -ReadinessPath $fixture.Path -WhatIf } 'mutually exclusive' 'The fixed profiles were allowed together.'
    }

    Invoke-TestCase '224018 selection binds only the fixed run, source, and explicit readiness pin' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $module = Import-TestModule
        $selected = & $module { param($root,$path) Select-GpuStageRetirement224018Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $fixture.Path
        $state = & $module { [pscustomobject]@{ RunId=$script:GpuRetirementRunId; SourceCommit=$script:GpuRetirementSourceCommit; Profile=$script:GpuRetirementProfile; Selected=$script:GpuRetirement224018ProfileSelected; AuditSha256=$script:GpuRetirementAuditSha256; AuditBytes=$script:GpuRetirementAuditBytes } }
        Assert-True ($selected.Profile -ceq '224018' -and $selected.RunId -ceq 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1') 'The profile selected a different completed run.'
        Assert-True ($selected.ReadinessPath -ceq [IO.Path]::GetFullPath($fixture.Path) -and $selected.ReadinessPin.Bytes -gt 0) 'Selection did not pin the explicit fresh readiness report.'
        Assert-True ($state.Selected -and $state.Profile -ceq '224018' -and $state.SourceCommit -ceq '69a3c3d94f33d1903778ef5ec5b0a1f015b1be29' -and $state.AuditBytes -gt 0) 'The module instance did not remain bound to the closed 224018 profile.'
        Assert-Throws { & $module { param($root,$path) Select-GpuStageRetirement201140Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $fixture.Path } 'already bound' 'The selected profile was rebound to 201140.'
        $lengths = & $module { [pscustomobject]@{ Original=$script:GpuRetirement224018OriginalFirmwareBytes; Current=$script:GpuRetirement224018CurrentFirmwareBytes } }
        Assert-True ($lengths.Original -eq 4336 -and $lengths.Current -eq 2890 -and $lengths.Original -ne $lengths.Current) 'The profile conflated the 4336-byte original baseline and 2890-byte reviewed capture.'
    }

    Invoke-TestCase '224018 selection rejects missing or non-GUID readiness locations without substitution' {
        $module = Import-TestModule
        $missing = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness\ffffffffffffffffffffffffffffffff\readiness.json'
        Assert-Throws { & $module { param($root,$path) Select-GpuStageRetirement224018Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $missing } 'missing|not found' 'Missing explicit readiness was accepted.'
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $outside = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness\readiness.json'
        Copy-Item -LiteralPath $fixture.Path -Destination $outside
        $module = Import-TestModule
        Assert-Throws { & $module { param($root,$path) Select-GpuStageRetirement224018Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $outside } 'GUID' 'A readiness report outside its GUID directory was accepted.'
    }

    Invoke-TestCase '224018 proof rejects a different run, altered firmware hash, and altered review receipt pin' {
        $module = Import-TestModule
        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.RunId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement224018Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'exact successful reviewed FinalVerify' 'An unrelated run passed the closed profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.FinalVerification.FirmwareEnumeration.CurrentSha256 = '0' * 64
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement224018Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'exact accepted 224018 firmware-difference review' 'A changed current firmware hash passed the proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $reviewReceiptPath = $journal.FinalVerification.FirmwareEnumeration.ReviewedDifference.ReceiptPin.Path
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $reviewReceiptPath))
        [IO.File]::WriteAllText($reviewReceiptPath, '{}', [Text.UTF8Encoding]::new($false))
        $journal.FinalVerification.FirmwareEnumeration.ReviewedDifference.ReceiptPin.Bytes = 15414
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement224018Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'fixed reviewed value|pin differs' 'A wrong review receipt length passed the proof.'
    }

    Invoke-TestCase '224018 raw boot-selection requires BootNext absent, exact order 0/1/2, and zero findings' {
        $module = Import-TestModule
        $null = & $module { param($capture) Assert-GpuStageRetirement224018BootSelection -Capture $capture -Checkpoint 'fixture' } (New-BootCapture)
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement224018BootSelection -Capture $capture -Checkpoint 'fixture' } (New-BootCapture -Order @(0,2,1)) } 'sequence 0 through 2' 'An altered BootOrder passed the raw gate.'
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement224018BootSelection -Capture $capture -Checkpoint 'fixture' } (New-BootCapture -BootNext 1) } 'BootNext|raw boot-selection' 'A set BootNext passed the raw gate.'
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement224018BootSelection -Capture $capture -Checkpoint 'fixture' } (New-BootCapture -Findings '00000001') } 'raw boot-selection' 'Nonzero integrity findings passed the raw gate.'
    }

    Write-Output "Assertions: $script:Assertions"
    Write-Output "Passed cases: $script:PassedCases"
    if ($script:FailedCases.Count -gt 0) {
        Write-Output 'Failed cases:'
        foreach ($failure in $script:FailedCases) { Write-Output "- $failure" }
        exit 1
    }
    Write-Output 'All 224018 retirement contract tests passed.'
}
finally {
    $resolvedParent = [IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot))
    if ([string]::Equals($resolvedParent, $fixtureParent, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Directory]::Exists($fixtureRoot) -and
        ([IO.File]::GetAttributes($fixtureRoot) -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
