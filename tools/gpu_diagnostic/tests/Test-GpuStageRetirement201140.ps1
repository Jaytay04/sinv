[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$modulePath = Join-Path $ToolRoot 'GpuStageRetirement.psm1'
$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = [Collections.Generic.List[string]]::new()
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent (Split-Path -Parent $ToolRoot)) ('.codex-build\gpu-stage-retirement-201140-tests-' + [guid]::NewGuid().ToString('N'))))
$workspaceParent = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent (Split-Path -Parent $ToolRoot)) '.codex-build'))

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
    $guid = 'a1234567890b4cdef1234567890abcde'
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

function New-FinalReviewJournalFixture {
    return [pscustomobject]@{
        SchemaVersion=1
        RunId='gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
        RunDirectory=(Join-Path (Join-Path $workspaceRoot '.analysis\gpu-baremetal\runs') 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7')
        SourceCommit='933b4c676318f26396cdf2c66de01c3ccbf1a950'
        Phase='Complete'; InProgress=$null
        LastAction=[pscustomobject]@{
            Action='FinalVerify'; Succeeded=$true
            Status='FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED'
        }
        FinalVerification=[pscustomobject]@{
            ResetBoundaryPassed=$true; ActivationAttempted=$true; SecureBoot=$true
            ActiveGpuProof=$false; GpuAttestation='UNATTESTED'; BootId='fixture-boot-id'; Errors=@()
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20 }
            Esp=[pscustomobject]@{ MatchesBaseline=$true }
            FirmwareEnumeration=[pscustomobject]@{
                TextEqual=$false; ReviewedDifferenceAccepted=$true
                BaselineSha256='ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE'
                CurrentSha256='4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'
                CaptureContext='SystemEspMountedAtS'
                ReviewedDifference=[pscustomobject]@{
                    Accepted=$true; HistoricalRawBootOrderUnchangedProven=$false; FullBootVariableNamespaceAttested=$false
                    ReceiptPin=[pscustomobject]@{
                        Path='C:\fixture\review\result.json'; Sha256='BEA6DCA1FC904B13830908471D6239A906F71AA6937630B32CBA6DC3C2C023D9'; Bytes=16924
                    }
                }
            }
        }
    }
}

try {
    if (-not [string]::Equals([IO.Path]::GetFullPath((Split-Path -Parent $workspaceRoot)), $workspaceParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Temporary test fixture escaped the dedicated .codex-build parent.'
    }
    [void][IO.Directory]::CreateDirectory($workspaceRoot)

    Invoke-TestCase 'explicit readiness capture selects only the fixed 201140 profile and needs no synthetic self-pin' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $module = Import-TestModule
        $selected = & $module { param($workspace,$readiness) Select-GpuStageRetirement201140Profile -WorkspaceRoot $workspace -ReadinessPath $readiness } $workspaceRoot $fixture.Path
        Assert-True ($selected.Profile -ceq '201140' -and $selected.RunId -ceq 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7') 'Explicit readiness selected the wrong completed-run profile.'
        Assert-True ($selected.ReadinessPin.Path -ceq [IO.Path]::GetFullPath($fixture.Path) -and $selected.ReadinessPin.Bytes -gt 0) 'Selected readiness was not captured by an immutable entry pin.'
        $selectedProfile = & $module { [pscustomobject]@{ RunId=$script:GpuRetirementRunId; AuditPath=$script:GpuRetirementAuditRelativePath; AuditPin=$script:GpuRetirementAuditSha256; Selected=$script:GpuRetirement201140ProfileSelected } }
        Assert-True ($selectedProfile.Selected -and $selectedProfile.RunId -ceq $selected.RunId -and $selectedProfile.AuditPath -match [regex]::Escape($fixture.Guid) -and $selectedProfile.AuditPin -ceq $selected.ReadinessPin.Sha256) 'Module profile pins do not bind the explicit readiness evidence.'
    }

    Invoke-TestCase 'missing readiness evidence is rejected without discovery or substitution' {
        $module = Import-TestModule
        $missing = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness\ffffffffffffffffffffffffffffffff\readiness.json'
        Assert-Throws { & $module { param($workspace,$path) Select-GpuStageRetirement201140Profile -WorkspaceRoot $workspace -ReadinessPath $path } $workspaceRoot $missing } 'missing|not found' 'Missing explicit readiness evidence was accepted.'
    }

    Invoke-TestCase 'readiness outside the GUID capture directory is rejected' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $outside = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness\readiness.json'
        Copy-Item -LiteralPath $fixture.Path -Destination $outside
        $module = Import-TestModule
        Assert-Throws { & $module { param($workspace,$path) Select-GpuStageRetirement201140Profile -WorkspaceRoot $workspace -ReadinessPath $path } $workspaceRoot $outside } 'GUID|<guid>' 'A non-GUID readiness location was accepted.'
    }

    Invoke-TestCase 'readiness with active selector, mounted ESP, or host-side changes is rejected' {
        foreach ($case in @(
            @{ Name='active selector'; Changes=@{ Selector=[pscustomobject]@{ Exists=$true; Valid=$true; Error=$null } } },
            @{ Name='ESP still mounted'; Changes=@{ Esp=[pscustomobject]@{ Inspected=$true; MountStateAfterCleanup='Mounted' } } },
            @{ Name='boot state changed'; Changes=@{ Effects=[pscustomobject]@{ EspFilesChanged=$false; FirmwareChanged=$false; BootStateChanged=$true; EspTemporarilyMounted=$true } } }
        )) {
            $fixture = New-ReadinessFixture -Root $workspaceRoot -Changes $case.Changes
            $module = Import-TestModule
            Assert-Throws { & $module { param($workspace,$path) Select-GpuStageRetirement201140Profile -WorkspaceRoot $workspace -ReadinessPath $path } $workspaceRoot $fixture.Path } 'readiness report|unmounted' "Unsafe $($case.Name) readiness state was accepted."
        }
    }

    Invoke-TestCase 'proof validator rejects another run and a forged review acceptance' {
        $module = Import-TestModule
        $journal = New-FinalReviewJournalFixture
        $journal.RunId = 'gpu-20261020T150318Z-59a346ed3cb8422bafe4be60268b7df1'
        Assert-Throws { & $module { param($value,$workspace) Assert-GpuStageRetirement201140Proof -Journal $value -WorkspaceRoot $workspace } $journal $workspaceRoot } 'exact successful reviewed FinalVerify' 'The validator accepted an unrelated completed run.'

        $journal = New-FinalReviewJournalFixture
        $journal.FinalVerification.FirmwareEnumeration.ReviewedDifference.Accepted = $false
        Assert-Throws { & $module { param($value,$workspace) Assert-GpuStageRetirement201140Proof -Journal $value -WorkspaceRoot $workspace } $journal $workspaceRoot } 'exact accepted 201140 firmware-difference review' 'The validator accepted a forged review decision.'
    }

    Invoke-TestCase 'proof validator rejects textual firmware drift and unapproved difference status' {
        $module = Import-TestModule
        foreach ($case in @(
            @{ Name='changed current hash'; Mutate={ param($j) $j.FinalVerification.FirmwareEnumeration.CurrentSha256='A'*64 } },
            @{ Name='text marked equal'; Mutate={ param($j) $j.FinalVerification.FirmwareEnumeration.TextEqual=$true } },
            @{ Name='review flag missing'; Mutate={ param($j) $j.FinalVerification.FirmwareEnumeration.ReviewedDifferenceAccepted=$false } }
        )) {
            $journal = New-FinalReviewJournalFixture
            & $case.Mutate $journal
            Assert-Throws { & $module { param($value,$workspace) Assert-GpuStageRetirement201140Proof -Journal $value -WorkspaceRoot $workspace } $journal $workspaceRoot } 'exact accepted 201140 firmware-difference review' "The validator accepted $($case.Name)."
        }
    }

    Invoke-TestCase '201140 raw boot-selection gate requires absent BootNext and fixed order zero through five' {
        $module = Import-TestModule
        $valid = [pscustomobject]@{ Baseline=[pscustomobject]@{ BootNext=$null; BootOrder=[uint16[]]@(0,1,2,3,4,5); BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'; ReferenceLogSha256='8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'; IntegrityFindings='00000000' } }
        $null = & $module { param($capture) Assert-GpuStageRetirement201140BootSelection -Capture $capture -Checkpoint 'fixture-before' } $valid

        $wrongOrder = [pscustomobject]@{ Baseline=[pscustomobject]@{ BootNext=$null; BootOrder=[uint16[]]@(0,1,2,3,5,4); BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'; ReferenceLogSha256='8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'; IntegrityFindings='00000000' } }
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement201140BootSelection -Capture $capture -Checkpoint 'fixture-before' } $wrongOrder } 'sequence 0 through 5' 'A changed BootOrder was accepted.'
        $bootNext = [pscustomobject]@{ Baseline=[pscustomobject]@{ BootNext=[uint16]1; BootOrder=[uint16[]]@(0,1,2,3,4,5); BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'; ReferenceLogSha256='8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'; IntegrityFindings='00000000' } }
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement201140BootSelection -Capture $capture -Checkpoint 'fixture-before' } $bootNext } 'BootNext|raw boot-selection' 'A set BootNext was accepted.'
    }

    Write-Output "Assertions: $script:Assertions"
    Write-Output "Passed cases: $script:PassedCases"
    if ($script:FailedCases.Count -gt 0) {
        Write-Output 'Failed cases:'
        foreach ($failure in $script:FailedCases) { Write-Output "- $failure" }
        exit 1
    }
    Write-Output 'All 201140 retirement contract tests passed.'
}
finally {
    $resolvedParent = [IO.Path]::GetFullPath((Split-Path -Parent $workspaceRoot))
    if ([string]::Equals($resolvedParent, $workspaceParent, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Directory]::Exists($workspaceRoot) -and
        ([IO.File]::GetAttributes($workspaceRoot) -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        Remove-Item -LiteralPath $workspaceRoot -Recurse -Force
    }
}
