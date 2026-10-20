[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
}
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$ReadinessScript = Join-Path $ToolRoot 'Get-GpuBaremetalReadiness.ps1'
$StateModulePath = Join-Path $ToolRoot 'GpuHostState.psm1'
foreach ($requiredPath in @($ReadinessScript, $StateModulePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) { throw "Readiness test input is missing: $requiredPath" }
}
Import-Module $StateModulePath -Force -ErrorAction Stop

$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = New-Object 'System.Collections.Generic.List[string]'
function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}
function Assert-Equal {
    param($Expected, $Actual, [string]$Message)
    $script:Assertions++
    if ($Expected -is [string] -or $Actual -is [string]) {
        if ([string]$Expected -cne [string]$Actual) { throw "$Message Expected '$Expected', got '$Actual'." }
    } elseif ($Expected -ne $Actual) { throw "$Message Expected '$Expected', got '$Actual'." }
}
function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)
    try {
        & $Body
        $script:PassedCases++
        Write-Output "PASS: $Name"
    } catch {
        $script:FailedCases.Add("$Name`: $($_.Exception.Message)")
        Write-Output "FAIL: $Name`: $($_.Exception.Message)"
    }
}

$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($ReadinessScript, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw ('Readiness script parse failed: ' + ($parseErrors -join '; ')) }
$decisionFunction = @($ast.FindAll({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-GpuBaremetalReadinessDecision'
}, $true))
$inventoryFunction = @($ast.FindAll({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-GpuBaremetalStageInventory'
}, $true))
foreach ($functionName in @('Get-GpuBaremetalEspLifecycleFacts','New-GpuBaremetalSourceFacts','Get-GpuBaremetalSourcePins','New-GpuBaremetalReadinessFacts','New-GpuBaremetalRecoveryFacts','Set-GpuBaremetalReportSaveFailure')) {
    $functionAst = @($ast.FindAll({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $functionName
    }, $true))
    if ($functionAst.Count -ne 1) { throw "Expected pure helper was not found exactly once: $functionName" }
    . ([scriptblock]::Create($functionAst[0].Extent.Text))
}
if ($decisionFunction.Count -ne 1 -or $inventoryFunction.Count -ne 1) { throw 'Expected pure decision and bounded inventory functions were not found exactly once.' }
. ([scriptblock]::Create($decisionFunction[0].Extent.Text))
. ([scriptblock]::Create($inventoryFunction[0].Extent.Text))

function New-ReadyFacts {
    [pscustomobject]@{
        Administrator = $true
        Source = [pscustomobject]@{ Branch = 'feat/smbios-boot-serial-probe'; Commit = ('a' * 40); Dirty = $false; Error = $null }
        Selector = [pscustomobject]@{ Exists = $false; Valid = $true; Phase = $null }
        HostSnapshotValid = $true
        HostSecureBoot = $true
        Preflight = [pscustomobject]@{ Complete = $true }
        Recovery = [pscustomobject]@{ Valid = $true }
        Esp = [pscustomobject]@{ Inspected = $true; MountedDuringInspection = $true; MountStillPresent = $false; BootManagerPin = [pscustomobject]@{ Sha256 = ('b' * 64); Bytes = 1024 } }
        Stage = [pscustomobject]@{ Exists = $false; Complete = $true; UnknownEntries = @(); UnsafeEntries = @() }
        Residue = [pscustomobject]@{ Clear = $true }
        CleanupErrors = @()
        Errors = @()
    }
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('gpu-baremetal-readiness-' + [guid]::NewGuid().ToString('N'))
$fixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$tempPrefix = $tempRoot + [IO.Path]::DirectorySeparatorChar
if (-not $fixtureRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Test fixture escaped the intended temporary directory: $fixtureRoot"
}
function Remove-ReadinessFixturePath {
    param([Parameter(Mandatory = $true)][string]$Path, [switch]$AllowRoot)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $rootPrefix = $fixtureRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $isFixtureRoot = [string]::Equals($fullPath.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar), $fixtureRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)
    if (-not $isFixtureRoot -and -not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to recursively remove a path outside the test fixture: $fullPath"
    }
    if ($isFixtureRoot -and -not $AllowRoot) { throw 'Refusing to remove the entire test fixture from a child-path cleanup.' }
    if (Test-Path -LiteralPath $fullPath) {
        $attributes = [IO.File]::GetAttributes($fullPath)
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing recursive cleanup of a reparse point: $fullPath" }
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}
try {
    Invoke-TestCase 'clean fact set admits a fresh Prepare' {
        $decision = Get-GpuBaremetalReadinessDecision -Facts (New-ReadyFacts)
        Assert-Equal 'READY_FOR_FRESH_PREPARE' $decision.Status 'Clean readiness status differs.'
        Assert-True $decision.Succeeded 'Clean readiness was rejected.'
        Assert-Equal 0 $decision.Blockers.Count 'Clean readiness returned blockers.'
    }

    Invoke-TestCase 'duplicate blocker evidence is reported once' {
        $facts = New-ReadyFacts
        $facts.Source.Error = 'Repeated source evidence failure.'
        $facts.Errors = @('Repeated source evidence failure.', 'Repeated source evidence failure.')
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        $matching = @($decision.Blockers | Where-Object { $_ -ceq 'Repeated source evidence failure.' })
        Assert-Equal 1 $matching.Count 'Identical blocker evidence was duplicated.'
    }

    Invoke-TestCase 'source pins come from the nested feature root and fail closed when a pin is missing' {
        $mainRoot = Join-Path $fixtureRoot 'main'
        $featureRoot = Join-Path $mainRoot '.codex-build\worktrees\gpu-windows-validation'
        $relativePaths = @(
            'tools/gpu_diagnostic/Get-GpuBaremetalReadiness.ps1',
            'tools/gpu_diagnostic/GpuHostState.psm1',
            'tools/gpu_diagnostic/GpuHostPlatform.psm1',
            'components/hypervisor/setup/Get-GpuDiagnosticPreflight.ps1'
        )
        foreach ($relativePath in $relativePaths) {
            $featureFile = Join-Path $featureRoot ($relativePath.Replace('/', '\'))
            $mainFile = Join-Path $mainRoot ($relativePath.Replace('/', '\'))
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $featureFile) -Force
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $mainFile) -Force
            [IO.File]::WriteAllText($featureFile, ('feature:' + $relativePath))
            [IO.File]::WriteAllText($mainFile, ('main-decoy:' + $relativePath))
        }

        $pins = @(Get-GpuBaremetalSourcePins -SourceRoot $featureRoot)
        Assert-Equal $relativePaths.Count $pins.Count 'Feature source pin count differs.'
        for ($index = 0; $index -lt $relativePaths.Count; $index++) {
            $relativePath = $relativePaths[$index]
            $featureFile = Join-Path $featureRoot ($relativePath.Replace('/', '\'))
            $mainFile = Join-Path $mainRoot ($relativePath.Replace('/', '\'))
            $featureHash = (Get-FileHash -LiteralPath $featureFile -Algorithm SHA256).Hash
            $mainHash = (Get-FileHash -LiteralPath $mainFile -Algorithm SHA256).Hash
            Assert-Equal $relativePath $pins[$index].RelativePath 'Source pin relative path differs.'
            Assert-Equal $featureHash $pins[$index].Sha256 'Source pin did not hash the feature worktree file.'
            Assert-True ($pins[$index].Sha256 -cne $mainHash) 'Source pin unexpectedly matched the main-workspace decoy.'
        }

        $missingRelativePath = $relativePaths[2]
        $missingFeatureFile = [IO.Path]::GetFullPath((Join-Path $featureRoot ($missingRelativePath.Replace('/', '\'))))
        $fixturePrefix = [IO.Path]::GetFullPath($fixtureRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
        if (-not $missingFeatureFile.StartsWith($fixturePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to delete a source pin outside the test fixture: $missingFeatureFile"
        }
        Remove-Item -LiteralPath $missingFeatureFile -Force
        $missingRejected = $false
        try { $null = Get-GpuBaremetalSourcePins -SourceRoot $featureRoot }
        catch { $missingRejected = ($_.Exception.Message -match [regex]::Escape($missingRelativePath)) }
        Assert-True $missingRejected 'Missing feature source pin did not fail with its relative path.'
    }

    Invoke-TestCase 'completed selector requires Archive first' {
        $facts = New-ReadyFacts
        $facts.Selector = [pscustomobject]@{ Exists = $true; Valid = $true; Phase = 'Complete' }
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        Assert-Equal 'BLOCKED_READINESS' $decision.Status 'Completed selector was admitted.'
        Assert-True (($decision.Blockers -join ' ') -match 'Archive') 'Completed selector did not direct the operator to Archive.'
    }

    Invoke-TestCase 'active or malformed selector blocks fresh preparation' {
        $facts = New-ReadyFacts
        $facts.Selector = [pscustomobject]@{ Exists = $true; Valid = $true; Phase = 'Staged' }
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        Assert-True (($decision.Blockers -join ' ') -match 'rollback') 'Active selector did not preserve the rollback gate.'
        $facts.Selector = [pscustomobject]@{ Exists = $true; Valid = $false; Phase = $null }
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        Assert-True (($decision.Blockers -join ' ') -match 'malformed') 'Malformed selector was not fail-closed.'
    }

    Invoke-TestCase 'retained stage blocks and requires ownership-reviewed retirement' {
        $facts = New-ReadyFacts
        $facts.Stage = [pscustomobject]@{ Exists = $true; Complete = $true; UnknownEntries = @(); UnsafeEntries = @() }
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        Assert-Equal 'BLOCKED_READINESS' $decision.Status 'Retained stage was admitted.'
        Assert-True (($decision.Blockers -join ' ') -match 'ownership-reviewed retirement') 'Retained stage did not require reviewed retirement.'
        Assert-True (($decision.NextStep) -notmatch 'Remove-Item|rm ') 'Next step suggested generic deletion.'
    }

    Invoke-TestCase 'unknown, unsafe, and incomplete stage inventories block' {
        $facts = New-ReadyFacts
        $facts.Stage = [pscustomobject]@{ Exists = $false; Complete = $false; UnknownEntries = @('EFI\insecure-boot\mystery.bin'); UnsafeEntries = @('EFI\insecure-boot\child (child directory)') }
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        $combined = $decision.Blockers -join ' '
        Assert-True ($combined -match 'completely inventoried') 'Incomplete inventory was admitted.'
        Assert-True ($combined -match 'unknown top-level files') 'Unknown entry did not block.'
        Assert-True ($combined -match 'child directories or reparse points') 'Unsafe entry did not block.'
    }

    Invoke-TestCase 'boot replacement residue blocks preparation' {
        $facts = New-ReadyFacts
        $facts.Residue = [pscustomobject]@{ Clear = $false }
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        Assert-True (($decision.Blockers -join ' ') -match 'replacement/restore residue') 'Residue did not block preparation.'
    }

    Invoke-TestCase 'mount cleanup errors block preparation' {
        $facts = New-ReadyFacts
        $facts.CleanupErrors = @('Unable to unmount owned S:')
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        Assert-True (($decision.Blockers -join ' ') -match 'cleanup failed') 'Cleanup failure did not block preparation.'
    }

    Invoke-TestCase 'successful ESP unmount preserves inspection proof without claiming a live mount' {
        $lifecycle = Get-GpuBaremetalEspLifecycleFacts -MountAcquired $true -UnmountSucceeded $true
        Assert-True $lifecycle.Inspected 'Successful mount inspection was lost after unmount.'
        Assert-True $lifecycle.MountedDuringInspection 'Report omitted that the ESP had been temporarily mounted.'
        Assert-Equal 'Unmounted' $lifecycle.MountStateAfterCleanup 'Successful unmount state was misreported.'
        $failedCleanup = Get-GpuBaremetalEspLifecycleFacts -MountAcquired $true -UnmountSucceeded $false
        Assert-Equal 'Unknown' $failedCleanup.MountStateAfterCleanup 'Failed unmount claimed the current mount state.'
        $facts = New-ReadyFacts
        $facts.Esp = [pscustomobject]@{ Inspected = $lifecycle.Inspected; MountedDuringInspection = $lifecycle.MountedDuringInspection; MountStateAfterCleanup = $lifecycle.MountStateAfterCleanup; BootManagerPin = [pscustomobject]@{ Sha256 = ('b' * 64); Bytes = 1024 } }
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        Assert-Equal 'READY_FOR_FRESH_PREPARE' $decision.Status 'Successful ESP cleanup incorrectly blocked readiness.'
    }

    Invoke-TestCase 'mocked inspection orchestration maps clean and dirty Git plus ESP cleanup accurately' {
        $cleanSource = New-GpuBaremetalSourceFacts -Branch 'feat/smbios-boot-serial-probe' -Commit ('a' * 40) -BranchExitCode 0 -StatusExitCode 0 -CommitExitCode 0 -StatusEntries ([string[]]@())
        Assert-True (-not $cleanSource.Dirty) 'Clean Git status was marked dirty.'
        $dirtySource = New-GpuBaremetalSourceFacts -Branch 'feat/smbios-boot-serial-probe' -Commit ('a' * 40) -BranchExitCode 0 -StatusExitCode 0 -CommitExitCode 0 -StatusEntries ([string[]]@(' M tools/example.ps1'))
        Assert-True $dirtySource.Dirty 'Changed Git status was marked clean.'
        $unknownSource = New-GpuBaremetalSourceFacts -Branch 'feat/smbios-boot-serial-probe' -Commit ('a' * 40) -BranchExitCode 0 -StatusExitCode 128 -CommitExitCode 0 -StatusEntries ([string[]]@())
        Assert-True ($null -eq $unknownSource.Dirty) 'Failed Git status was misreported as clean or dirty.'
        Assert-True ($null -ne $unknownSource.Error) 'Failed Git status omitted its error.'

        $hostSnapshot = [pscustomobject]@{ SecureBoot = $true }
        $preflight = [pscustomobject]@{ Complete = $true }
        $recovery = New-GpuBaremetalRecoveryFacts -Directory 'E:\Recovery' -Validation ([pscustomobject]@{ Disk = [pscustomobject]@{ Number = 2 } }) -ErrorMessage $null
        $selector = [pscustomobject]@{ Exists = $false; Valid = $true; Phase = $null }
        $stage = [pscustomobject]@{ Exists = $false; Complete = $true; UnknownEntries = @(); UnsafeEntries = @() }
        $residue = [pscustomobject]@{ Clear = $true }
        $pin = [pscustomobject]@{ Sha256 = ('c' * 64); Bytes = 2048 }
        $facts = New-GpuBaremetalReadinessFacts -Administrator $true -Source $cleanSource -Selector $selector -HostSnapshot $hostSnapshot -Preflight $preflight -Recovery $recovery -EspMountAcquired $true -EspUnmountSucceeded $true -BootManagerPin $pin -Stage $stage -Residue $residue
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        Assert-Equal 'READY_FOR_FRESH_PREPARE' $decision.Status 'Mocked normal inspection did not admit its clean facts.'
        Assert-True $facts.Esp.Inspected 'Mocked inspection lost ESP proof after unmount.'
        Assert-Equal 'Unmounted' $facts.Esp.MountStateAfterCleanup 'Mocked inspection misreported cleanup.'
        $facts = New-GpuBaremetalReadinessFacts -Administrator $true -Source $dirtySource -Selector $selector -HostSnapshot $hostSnapshot -Preflight $preflight -Recovery $recovery -EspMountAcquired $true -EspUnmountSucceeded $false -BootManagerPin $pin -Stage $stage -Residue $residue -CleanupErrors ([string[]]@('unmount failed'))
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        Assert-Equal 'BLOCKED_READINESS' $decision.Status 'Dirty source and failed cleanup were admitted.'
        Assert-Equal 'Unknown' $facts.Esp.MountStateAfterCleanup 'Failed unmount was claimed absent.'
        Assert-True (($decision.Blockers -join ' ') -match 'worktree must be clean') 'Mocked dirty source blocker is missing.'
        Assert-True (($decision.Blockers -join ' ') -match 'cleanup failed') 'Mocked unmount failure blocker is missing.'
    }

    Invoke-TestCase 'recovery summary always includes validity, disk, directory, and error fields' {
        $validation = [pscustomobject]@{ Base = 'E:\Recovery'; RunDirectory = $null; Disk = [pscustomobject]@{ Number = 2; BusType = 'USB' } }
        $summary = New-GpuBaremetalRecoveryFacts -Directory 'E:\Recovery' -Validation $validation -ErrorMessage $null
        Assert-True $summary.Valid 'Valid recovery disk was not admitted.'
        Assert-Equal 2 $summary.Disk.Number 'Recovery disk identity was lost.'
        Assert-Equal 'E:\Recovery' $summary.Directory 'Recovery path was lost.'
        Assert-True ($null -eq $summary.Error) 'Valid recovery facts should have no error.'
        $failed = New-GpuBaremetalRecoveryFacts -Directory 'E:\Recovery' -Validation $null -ErrorMessage 'Disk could not be inspected.'
        Assert-True (-not $failed.Valid) 'Failed recovery inspection was admitted.'
        Assert-Equal 'Disk could not be inspected.' $failed.Error 'Recovery failure detail was lost.'
    }

    Invoke-TestCase 'report-save failure updates both printed and decision status' {
        $decision = [pscustomobject]@{ Status = 'READY_FOR_FRESH_PREPARE'; Succeeded = $true; Blockers = @(); NextStep = 'Run Prepare.' }
        $report = [pscustomobject]@{ Status = 'READY_FOR_FRESH_PREPARE'; Succeeded = $true; Blockers = @(); NextStep = 'Run Prepare.' }
        $updated = Set-GpuBaremetalReportSaveFailure -Report $report -Decision $decision
        Assert-Equal 'BLOCKED_READINESS' $decision.Status 'Decision retained the pre-save status.'
        Assert-Equal 'BLOCKED_READINESS' $updated.Status 'Printed report retained the pre-save status.'
        Assert-True (-not $updated.Succeeded) 'Report-save failure did not clear success.'
        Assert-True (($updated.Blockers -join ' ') -match 'could not be saved') 'Report-save failure is not explained.'
        Assert-True ($updated.NextStep -match 'rerun this readiness inspection') 'Report-save failure did not direct the operator back to readiness.'
        Assert-True ($updated.NextStep -match 'Do not start Prepare') 'Report-save failure failed to hold Prepare.'
        Assert-Equal $updated.NextStep $decision.NextStep 'Decision and printed next step differ after a save failure.'
    }

    Invoke-TestCase 'source, Secure Boot, preflight, recovery, and probe errors fail closed' {
        $facts = New-ReadyFacts
        $facts.Source.Dirty = $true
        $facts.HostSecureBoot = $false
        $facts.Preflight.Complete = $false
        $facts.Recovery.Valid = $false
        $facts.Errors = @('shared lock unavailable')
        $decision = Get-GpuBaremetalReadinessDecision -Facts $facts
        $combined = $decision.Blockers -join ' '
        Assert-Equal 'BLOCKED_READINESS' $decision.Status 'Invalid host facts were admitted.'
        Assert-True ($combined -match 'worktree must be clean') 'Dirty source was not detected.'
        Assert-True ($combined -match 'Secure Boot must be enabled') 'Secure Boot failure was not detected.'
        Assert-True ($combined -match 'preflight') 'Failed preflight was not detected.'
        Assert-True ($combined -match 'separate physical disk') 'Recovery disk failure was not detected.'
        Assert-True ($combined -match 'shared lock unavailable') 'Operational probe error was not retained.'
    }

    Invoke-TestCase 'stage inventory hashes only bounded top-level regular files' {
        $stage = Join-Path $fixtureRoot 'EFI\insecure-boot'
        $null = New-Item -ItemType Directory -Path $stage -Force
        $filePath = Join-Path $stage 'payload.efi'
        [IO.File]::WriteAllBytes($filePath, [byte[]](1, 2, 3, 4))
        $result = Get-GpuBaremetalStageInventory -StageDirectory $stage
        Assert-True $result.Exists 'Existing stage directory was not reported.'
        Assert-True $result.Complete 'Small stage inventory was marked incomplete.'
        Assert-Equal 1 $result.FileCount 'Top-level file count differs.'
        Assert-Equal 4 $result.BytesHashed 'Hashed byte count differs.'
        Assert-True ($result.Entries[0].Hashed) 'Top-level regular file was not hashed.'
        Assert-Equal ((Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash) $result.Entries[0].Sha256 'Stage file hash differs.'
    }

    Invoke-TestCase 'stage inventory flags unknown files and never recurses into child directories' {
        $stage = Join-Path $fixtureRoot 'EFI\insecure-boot'
        Remove-ReadinessFixturePath -Path $stage
        $null = New-Item -ItemType Directory -Path (Join-Path $stage 'nested') -Force
        [IO.File]::WriteAllBytes((Join-Path $stage 'unexpected.dat'), [byte[]](5, 6))
        [IO.File]::WriteAllBytes((Join-Path $stage 'nested\ignored.bin'), [byte[]](7, 8))
        $result = Get-GpuBaremetalStageInventory -StageDirectory $stage
        Assert-Equal 2 $result.EntryCount 'Inventory traversed a child directory.'
        Assert-Equal 1 $result.UnknownEntries.Count 'Unknown top-level file was not flagged.'
        Assert-Equal 1 $result.UnsafeEntries.Count 'Child directory was not flagged.'
        Assert-Equal 1 $result.FileCount 'Nested file was unexpectedly hashed.'
    }

    Invoke-TestCase '-WhatIf returns before host or filesystem effects' {
        $previewRoot = Join-Path $fixtureRoot 'must-not-be-created'
        $previewOutput = @(& $ReadinessScript -WorkspaceRoot $previewRoot -WhatIf | ConvertFrom-Json)
        Assert-Equal 1 $previewOutput.Count 'Preview did not return exactly one result.'
        Assert-True $previewOutput[0].Preview 'Preview flag is missing.'
        Assert-True (-not $previewOutput[0].FilesystemChangesPerformed) 'Preview reported filesystem effects.'
        Assert-True (-not $previewOutput[0].NativeCommandsExecuted) 'Preview reported native commands.'
        Assert-True (-not (Test-Path -LiteralPath $previewRoot)) 'Preview created the supplied workspace root.'
    }
} finally {
    Remove-ReadinessFixturePath -Path $fixtureRoot -AllowRoot
}

if ($script:FailedCases.Count -gt 0) {
    throw ("Bare-metal readiness tests failed: {0} failed case(s), {1} assertions. {2}" -f $script:FailedCases.Count, $script:Assertions, ($script:FailedCases -join ' | '))
}
Write-Output ("Bare-metal readiness tests passed: {0} case(s), {1} assertions." -f $script:PassedCases, $script:Assertions)
