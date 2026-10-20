[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Low')]
param(
    [string]$WorkspaceRoot,
    [string]$RecoveryDirectory = 'E:\CodexBackup\Recovery\hsinv-gpu-test'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# Keep preview ahead of module imports, host queries, mount operations, and all
# filesystem writes. This makes -WhatIf safe even on a machine with no GPU
# tooling or elevated access.
if ($WhatIfPreference) {
    [pscustomobject]@{
        Action = 'InspectBaremetalReadiness'
        Preview = $true
        FilesystemChangesPerformed = $false
        NativeCommandsExecuted = $false
        EspMounted = $false
        Note = 'No commands or filesystem operations were executed. The normal inspection saves local evidence, may create the workspace-local shared GPU state directory/lock, and temporarily mounts the system ESP for reads; it does not change ESP files or firmware.'
    } | ConvertTo-Json -Depth 4
    return
}

$SourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorktreeParent = Split-Path -Parent $SourceRoot
    $BuildParent = Split-Path -Parent $WorktreeParent
    if ((Split-Path -Leaf $WorktreeParent) -ieq 'worktrees' -and
        (Split-Path -Leaf $BuildParent) -ieq '.codex-build') {
        $WorkspaceRoot = Split-Path -Parent $BuildParent
    } else {
        $WorkspaceRoot = $SourceRoot
    }
}
$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
if (-not $PSCmdlet.ShouldProcess($WorkspaceRoot, 'Inspect bare-metal readiness and write a local evidence report')) {
    [pscustomobject]@{
        Action = 'InspectBaremetalReadiness'
        Status = 'DECLINED'
        FilesystemChangesPerformed = $false
        NativeCommandsExecuted = $false
    } | ConvertTo-Json -Depth 3
    return
}

$StateRoot = Join-Path $WorkspaceRoot '.analysis\gpu-baremetal'
$SelectorPath = Join-Path $StateRoot 'host-prepared.json'
$LockPath = Join-Path $StateRoot 'host-step.lock'
$EvidenceRoot = Join-Path $WorkspaceRoot '.analysis\gpu-baremetal-readiness'
$EvidenceDirectory = Join-Path $EvidenceRoot ([guid]::NewGuid().ToString('N'))
$EvidencePath = Join-Path $EvidenceDirectory 'readiness.json'
$ReadinessErrors = New-Object 'System.Collections.Generic.List[string]'
$CleanupErrors = New-Object 'System.Collections.Generic.List[string]'
$Lock = $null
$PlatformModule = $null
$EspMount = $null
$HostSnapshot = $null
$SourceFacts = [pscustomobject]@{ Branch = $null; Commit = $null; Dirty = $null; DirtyEntryCount = $null; Error = $null; Pins = @() }
$SelectorFacts = [pscustomobject]@{ Exists = $false; Valid = $true; Phase = $null; RunId = $null; JournalPath = $null; Pin = $null; Error = $null }
$RecoveryFacts = [pscustomobject]@{ Valid = $false; Directory = $RecoveryDirectory; Disk = $null; Error = $null }
$PreflightFacts = [pscustomobject]@{ Complete = $false; Path = $null; Pin = $null; InventoryComplete = $false; IntelPresent = $false; NvidiaPresent = $false; ConnectedMonitorCount = $null; Error = $null }
$EspFacts = [pscustomobject]@{ Inspected = $false; MountedDuringInspection = $false; MountCleanupState = 'NotMounted'; Identity = $null; BootManager = $null; BootManagerPin = $null; StageDirectory = 'EFI\insecure-boot'; Error = $null }
$StageFacts = [pscustomobject]@{ Exists = $false; Complete = $true; EntryCount = 0; FileCount = 0; BytesHashed = [long]0; Entries = @(); UnknownEntries = @(); UnsafeEntries = @(); Errors = @() }
$ResidueFacts = [pscustomobject]@{ Clear = $false; Residues = @(); ValidationError = $null }
$LockFacts = [pscustomobject]@{ Acquired = $false; Path = $LockPath; Error = $null }
$AdministratorCheck = [pscustomobject]@{ Passed = $false; Error = $null }

function Add-ReadinessError {
    param([Parameter(Mandatory = $true)][string]$Message)
    $ReadinessErrors.Add($Message)
}

function Get-GpuBaremetalStageInventory {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$StageDirectory)

    $knownNames = @('shimx64.efi', 'grubx64.efi', 'tcglog.ib', 'payload.efi', 'manifest.sha256', 'handoff-status.txt', 'payload-diagnostic.txt')
    $maxEntries = 64
    $maxFileBytes = 33554432L
    $maxTotalBytes = 67108864L
    $result = [ordered]@{
        Exists = [bool](Test-Path -LiteralPath $StageDirectory)
        Complete = $true
        EntryCount = 0
        FileCount = 0
        BytesHashed = [long]0
        Entries = @()
        UnknownEntries = @()
        UnsafeEntries = @()
        Errors = @()
        Limits = [pscustomobject]@{ MaxEntries = $maxEntries; MaxFileBytes = $maxFileBytes; MaxTotalBytes = $maxTotalBytes; Recursive = $false }
    }
    if (-not $result.Exists) { return [pscustomobject]$result }

    try {
        $directoryAttributes = [IO.File]::GetAttributes($StageDirectory)
        if (($directoryAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            $result.Complete = $false
            $result.UnsafeEntries = @('EFI\insecure-boot (reparse point)')
            $result.Errors = @('Stage directory is a reparse point; contents were not traversed.')
            return [pscustomobject]$result
        }
        $items = @(Get-ChildItem -LiteralPath $StageDirectory -Force -ErrorAction Stop | Sort-Object -Property Name)
        $result.EntryCount = $items.Count
        if ($items.Count -gt $maxEntries) {
            $result.Complete = $false
            $result.Errors += "Top-level entry count exceeds the bound of $maxEntries; no entries were hashed."
            return [pscustomobject]$result
        }
        $entries = New-Object 'System.Collections.Generic.List[object]'
        $unknown = New-Object 'System.Collections.Generic.List[string]'
        $unsafe = New-Object 'System.Collections.Generic.List[string]'
        $errors = New-Object 'System.Collections.Generic.List[string]'
        foreach ($item in $items) {
            $relative = 'EFI\insecure-boot\' + [string]$item.Name
            $attributes = [IO.File]::GetAttributes($item.FullName)
            $isReparse = (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
            $isDirectory = (($attributes -band [IO.FileAttributes]::Directory) -ne 0)
            if ($isReparse -or $isDirectory) {
                if ($isReparse) { $unsafe.Add($relative + ' (reparse point)') }
                if ($isDirectory) { $unsafe.Add($relative + ' (child directory; not traversed)') }
                $entries.Add([pscustomobject]@{ RelativePath = $relative; Kind = if ($isDirectory) { 'Directory' } else { 'ReparsePoint' }; Bytes = $null; Sha256 = $null; Hashed = $false })
                continue
            }
            if ($knownNames -notcontains ([string]$item.Name).ToLowerInvariant()) { $unknown.Add($relative) }
            $length = [long]$item.Length
            if ($length -gt $maxFileBytes -or ($result.BytesHashed + $length) -gt $maxTotalBytes) {
                $result.Complete = $false
                $errors.Add("Hash bound exceeded for $relative ($length bytes); no file content was read.")
                $entries.Add([pscustomobject]@{ RelativePath = $relative; Kind = 'File'; Bytes = $length; Sha256 = $null; Hashed = $false })
                continue
            }
            $pin = Get-GpuFilePin -Path $item.FullName
            $result.BytesHashed += [long]$pin.Bytes
            $result.FileCount++
            $entries.Add([pscustomobject]@{ RelativePath = $relative; Kind = 'File'; Bytes = [long]$pin.Bytes; Sha256 = [string]$pin.Sha256; Hashed = $true })
        }
        $result.Entries = @($entries.ToArray())
        $result.UnknownEntries = @($unknown.ToArray())
        $result.UnsafeEntries = @($unsafe.ToArray())
        $result.Errors = @($errors.ToArray())
    }
    catch {
        $result.Complete = $false
        $result.Errors += $_.Exception.Message
    }
    return [pscustomobject]$result
}

function Get-GpuBaremetalReadinessDecision {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][object]$Facts)

    $blockers = New-Object 'System.Collections.Generic.List[string]'
    if (-not [bool]$Facts.Administrator) { $blockers.Add('Run from an elevated PowerShell session.') }
    if ($null -ne $Facts.Source.Error) { $blockers.Add([string]$Facts.Source.Error) }
    if ([string]$Facts.Source.Branch -cne 'feat/smbios-boot-serial-probe') { $blockers.Add('Source branch must be feat/smbios-boot-serial-probe.') }
    if ([string]$Facts.Source.Commit -notmatch '^[0-9a-f]{40}$') { $blockers.Add('A full source commit could not be identified.') }
    if ($Facts.Source.Dirty -ne $false) { $blockers.Add('Source worktree must be clean before fresh preparation.') }
    if ($Facts.Selector.Exists) {
        if (-not [bool]$Facts.Selector.Valid) {
            $blockers.Add('An existing selector is malformed or its journal could not be read; preserve it and inspect its exact path.')
        } elseif ([string]$Facts.Selector.Phase -ceq 'Complete') {
            $blockers.Add('A completed selector remains; run Archive on that exact completed run before a fresh Prepare.')
        } else {
            $blockers.Add('An existing selector or interrupted run remains; inspect its exact run with Status and complete rollback before a fresh Prepare.')
        }
    }
    if (-not [bool]$Facts.HostSnapshotValid) { $blockers.Add('Host identity or Secure Boot could not be read completely.') }
    if ($Facts.HostSecureBoot -ne $true) { $blockers.Add('Secure Boot must be enabled for the documented bare-metal workflow.') }
    if (-not [bool]$Facts.Preflight.Complete) { $blockers.Add('Display and monitor preflight did not complete or did not identify both required adapters.') }
    if (-not [bool]$Facts.Recovery.Valid) { $blockers.Add('The recovery directory is not confirmed on a separate physical disk.') }
    if (-not [bool]$Facts.Esp.Inspected -or $null -eq $Facts.Esp.BootManagerPin) { $blockers.Add('The system ESP or its Microsoft boot manager could not be safely inspected and pinned.') }
    if (-not [bool]$Facts.Stage.Complete) { $blockers.Add('The existing diagnostic stage could not be completely inventoried within the configured bounds.') }
    if ([bool]$Facts.Stage.Exists) { $blockers.Add('EFI\\insecure-boot is retained; ownership-reviewed retirement is required before a new stage. Do not delete it generically or retry Stage.') }
    if (@($Facts.Stage.UnknownEntries).Count -gt 0) { $blockers.Add('The retained diagnostic directory contains unknown top-level files; preserve and review them.') }
    if (@($Facts.Stage.UnsafeEntries).Count -gt 0) { $blockers.Add('The retained diagnostic directory contains child directories or reparse points; preserve and review them.') }
    if (-not [bool]$Facts.Residue.Clear) { $blockers.Add('Existing boot replacement/restore residue blocks a fresh stage; preserve it for ownership-reviewed retirement.') }
    if (@($Facts.CleanupErrors).Count -gt 0) { $blockers.Add('Temporary ESP cleanup failed; do not continue until the owned S: mount is resolved.') }
    if (@($Facts.Errors).Count -gt 0) { foreach ($errorText in @($Facts.Errors)) { $blockers.Add([string]$errorText) } }

    $status = if ($blockers.Count -eq 0) { 'READY_FOR_FRESH_PREPARE' } else { 'BLOCKED_READINESS' }
    $nextStep = if ($blockers.Count -eq 0) {
        'Run gpu-host-test.ps1 Prepare -PhysicalOutputConfirmed -RecoveryReady after manually confirming the Intel-connected diagnostic display and recovery readiness.'
    } else {
        'Resolve each blocker using the existing Status/rollback/archive workflow or an ownership-reviewed ESP retirement, then run this inspection again. Do not delete retained EFI files generically or retry an interrupted action.'
    }
    return [pscustomobject]@{ Status = $status; Succeeded = ($blockers.Count -eq 0); Blockers = @($blockers.ToArray()); NextStep = $nextStep }
}

function Get-GpuBaremetalEspLifecycleFacts {
    [CmdletBinding()]
    param([bool]$MountAcquired, [bool]$UnmountSucceeded)

    return [pscustomobject]@{
        Inspected = [bool]$MountAcquired
        MountedDuringInspection = [bool]$MountAcquired
        MountStateAfterCleanup = if (-not [bool]$MountAcquired) { 'NotMounted' } elseif ([bool]$UnmountSucceeded) { 'Unmounted' } else { 'Unknown' }
    }
}

function New-GpuBaremetalSourceFacts {
    [CmdletBinding()]
    param(
        [string]$Branch,
        [string]$Commit,
        [int]$BranchExitCode,
        [int]$StatusExitCode,
        [int]$CommitExitCode,
        [AllowEmptyCollection()][string[]]$StatusEntries = @()
    )

    $statusKnown = ($StatusExitCode -eq 0)
    $errorText = $null
    if ($BranchExitCode -ne 0 -or -not $statusKnown -or $CommitExitCode -ne 0) {
        $errorText = 'Git source status could not be read completely.'
    }
    return [pscustomobject]@{
        Branch = $Branch
        Commit = $Commit
        Dirty = if ($statusKnown) { ($StatusEntries.Count -gt 0) } else { $null }
        DirtyEntryCount = if ($statusKnown) { $StatusEntries.Count } else { $null }
        Error = $errorText
        Pins = @()
    }
}

function New-GpuBaremetalReadinessFacts {
    [CmdletBinding()]
    param(
        [bool]$Administrator,
        [Parameter(Mandatory = $true)][object]$Source,
        [Parameter(Mandatory = $true)][object]$Selector,
        [AllowNull()][object]$HostSnapshot,
        [Parameter(Mandatory = $true)][object]$Preflight,
        [Parameter(Mandatory = $true)][object]$Recovery,
        [bool]$EspMountAcquired,
        [bool]$EspUnmountSucceeded,
        [string]$EspMountStateAfterCleanup,
        [AllowNull()][object]$BootManagerPin,
        [Parameter(Mandatory = $true)][object]$Stage,
        [Parameter(Mandatory = $true)][object]$Residue,
        [AllowEmptyCollection()][string[]]$CleanupErrors = @(),
        [AllowEmptyCollection()][string[]]$Errors = @()
    )

    $lifecycle = Get-GpuBaremetalEspLifecycleFacts -MountAcquired $EspMountAcquired -UnmountSucceeded $EspUnmountSucceeded
    if (-not [string]::IsNullOrWhiteSpace($EspMountStateAfterCleanup)) { $lifecycle.MountStateAfterCleanup = $EspMountStateAfterCleanup }
    return [pscustomobject]@{
        Administrator = $Administrator
        Source = $Source
        Selector = $Selector
        HostSnapshotValid = ($null -ne $HostSnapshot)
        HostSecureBoot = if ($null -ne $HostSnapshot) { $HostSnapshot.SecureBoot } else { $null }
        Preflight = $Preflight
        Recovery = $Recovery
        Esp = [pscustomobject]@{
            Inspected = $lifecycle.Inspected
            MountedDuringInspection = $lifecycle.MountedDuringInspection
            MountStateAfterCleanup = $lifecycle.MountStateAfterCleanup
            BootManagerPin = $BootManagerPin
        }
        Stage = $Stage
        Residue = $Residue
        CleanupErrors = @($CleanupErrors)
        Errors = @($Errors)
    }
}

function New-GpuBaremetalRecoveryFacts {
    [CmdletBinding()]
    param([string]$Directory, [AllowNull()][object]$Validation, [AllowNull()][string]$ErrorMessage)

    return [pscustomobject]@{
        Valid = ($null -ne $Validation -and [string]::IsNullOrWhiteSpace($ErrorMessage))
        Directory = $Directory
        Disk = if ($null -ne $Validation -and $null -ne $Validation.PSObject.Properties['Disk']) { $Validation.Disk } else { $null }
        Error = if ([string]::IsNullOrWhiteSpace($ErrorMessage)) { $null } else { $ErrorMessage }
    }
}

function Set-GpuBaremetalReportSaveFailure {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][object]$Report, [Parameter(Mandatory = $true)][object]$Decision)

    $Decision.Status = 'BLOCKED_READINESS'
    $Decision.Succeeded = $false
    $Decision.Blockers = @($Decision.Blockers) + @('The local readiness report could not be saved.')
    $Decision.NextStep = 'Preserve the collected logs, repair the local evidence-write issue, and rerun this readiness inspection. Do not start Prepare without a saved READY_FOR_FRESH_PREPARE report.'
    $Report.Status = $Decision.Status
    $Report.Succeeded = $false
    $Report.Blockers = $Decision.Blockers
    $Report.NextStep = $Decision.NextStep
    return $Report
}

$EvidenceCreated = $false
$StateRootCreated = $false
$LockFileCreated = $false
try {
    Import-Module (Join-Path $PSScriptRoot 'GpuHostState.psm1') -DisableNameChecking -ErrorAction Stop
    $PlatformModule = Import-Module (Join-Path $PSScriptRoot 'GpuHostPlatform.psm1') -PassThru -DisableNameChecking -ErrorAction Stop

    & $PlatformModule { Assert-GpuAdministrator }
    $AdministratorCheck.Passed = $true

    $null = Assert-GpuPathWithin -Path $SourceRoot -Root $WorkspaceRoot -AllowRoot -MustExist
    $null = Assert-GpuPathWithin -Path $StateRoot -Root $WorkspaceRoot
    $null = Assert-GpuPathWithin -Path $EvidenceRoot -Root $WorkspaceRoot

    & $PlatformModule { param($Path) Assert-GpuNoReparseAncestors -Path $Path } (Split-Path -Parent $EvidenceRoot)
    if (-not (Test-Path -LiteralPath $EvidenceRoot -PathType Container)) {
        $null = New-Item -ItemType Directory -Path $EvidenceRoot -Force -ErrorAction Stop
    }
    $null = Assert-GpuPathWithin -Path $EvidenceRoot -Root $WorkspaceRoot -MustExist
    $null = New-Item -ItemType Directory -Path $EvidenceDirectory -ErrorAction Stop
    $EvidenceCreated = $true

    if (-not (Test-Path -LiteralPath $StateRoot -PathType Container)) {
        & $PlatformModule { param($Path) Assert-GpuNoReparseAncestors -Path $Path } ([IO.Path]::GetDirectoryName($StateRoot))
        $null = New-Item -ItemType Directory -Path $StateRoot -Force -ErrorAction Stop
        $StateRootCreated = $true
    }
    $null = Assert-GpuPathWithin -Path $LockPath -Root $StateRoot
    $LockFileCreated = -not (Test-Path -LiteralPath $LockPath)
    try {
        $Lock = [IO.File]::Open($LockPath, [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $LockFacts.Acquired = $true
    } catch {
        $LockFacts.Error = 'Another GPU host action is running, or the shared host-step lock is unavailable. Wait; do not inspect or recover concurrently.'
        throw $LockFacts.Error
    }

    $branchOutput = @(& git -C $SourceRoot branch --show-current 2>&1)
    $branchExit = $LASTEXITCODE
    $statusOutput = @(& git -C $SourceRoot status --porcelain --untracked-files=all 2>&1)
    $statusExit = $LASTEXITCODE
    $commitOutput = @(& git -C $SourceRoot rev-parse HEAD 2>&1)
    $commitExit = $LASTEXITCODE
    $SourceFacts = New-GpuBaremetalSourceFacts `
        -Branch (($branchOutput -join "`n").Trim()) `
        -Commit (($commitOutput -join '').Trim()) `
        -BranchExitCode $branchExit -StatusExitCode $statusExit -CommitExitCode $commitExit `
        -StatusEntries ([string[]]$statusOutput)
    if ($null -ne $SourceFacts.Error) { Add-ReadinessError $SourceFacts.Error }
    try {
        $sourcePinRows = New-Object 'System.Collections.Generic.List[object]'
        foreach ($relativePath in @(
            'tools/gpu_diagnostic/Get-GpuBaremetalReadiness.ps1',
            'tools/gpu_diagnostic/GpuHostState.psm1',
            'tools/gpu_diagnostic/GpuHostPlatform.psm1',
            'components/hypervisor/setup/Get-GpuDiagnosticPreflight.ps1'
        )) {
            $pinPath = Join-Path $WorkspaceRoot ($relativePath.Replace('/', '\'))
            $pin = Get-GpuFilePin -Path $pinPath
            $sourcePinRows.Add([pscustomobject]@{ RelativePath = $relativePath; Sha256 = $pin.Sha256; Bytes = $pin.Bytes })
        }
        $SourceFacts.Pins = @($sourcePinRows.ToArray())
    } catch {
        $SourceFacts.Error = 'Required readiness source pins could not be read: ' + $_.Exception.Message
        Add-ReadinessError $SourceFacts.Error
    }

    if (Test-Path -LiteralPath $SelectorPath) {
        $SelectorFacts.Exists = $true
        try {
            $SelectorFacts.Pin = Get-GpuFilePin -Path $SelectorPath
            $selector = Read-GpuJson -Path $SelectorPath
            if ($selector.SchemaVersion -ne 1 -or [string]$selector.RunId -cnotmatch '^gpu-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{32}$') {
                throw 'Existing selector has an unsupported schema or run identity.'
            }
            $expectedRun = Join-Path (Join-Path $StateRoot 'runs') ([string]$selector.RunId)
            $expectedJournal = Join-Path $expectedRun 'journal.json'
            $null = Assert-GpuPathWithin -Path ([string]$selector.RunDirectory) -Root (Join-Path $StateRoot 'runs') -MustExist
            if (-not [string]::Equals([IO.Path]::GetFullPath([string]$selector.RunDirectory), [IO.Path]::GetFullPath($expectedRun), [StringComparison]::OrdinalIgnoreCase) -or
                -not [string]::Equals([IO.Path]::GetFullPath([string]$selector.JournalPath), [IO.Path]::GetFullPath($expectedJournal), [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Existing selector paths do not match the recorded run identity.'
            }
            $journal = Read-GpuJson -Path $expectedJournal
            $SelectorFacts.Phase = [string]$journal.Phase
            $SelectorFacts.RunId = [string]$selector.RunId
            $SelectorFacts.JournalPath = $expectedJournal
        } catch {
            $SelectorFacts.Valid = $false
            $SelectorFacts.Error = $_.Exception.Message
            Add-ReadinessError ('Existing selector/journal read failed: ' + $SelectorFacts.Error)
        }
    }

    try {
        $HostSnapshot = & $PlatformModule { Get-GpuCurrentMachineSnapshot -IncludeSecureBoot }
    } catch { Add-ReadinessError ('Host snapshot failed: ' + $_.Exception.Message) }

    if (Test-Path -LiteralPath $RecoveryDirectory -PathType Container) {
        try {
            $null = & $PlatformModule { param($Path) Assert-GpuNoReparseAncestors -Path $Path } ([IO.Path]::GetFullPath($RecoveryDirectory))
            if ($null -eq $HostSnapshot) { throw 'Host system-disk identity is unavailable; recovery separation cannot be confirmed.' }
            $recoveryState = [pscustomobject]@{
                RecoveryBase = [IO.Path]::GetFullPath($RecoveryDirectory)
                RecoveryRunDirectory = $null
                Host = if ($null -ne $HostSnapshot) { [pscustomobject]@{ SystemDiskNumber = [int]$HostSnapshot.SystemDiskNumber } } else { $null }
            }
            $recoveryValidation = & $PlatformModule { param($State) Assert-GpuRecoveryLocation -State $State } $recoveryState
            $RecoveryFacts = New-GpuBaremetalRecoveryFacts -Directory ([IO.Path]::GetFullPath($RecoveryDirectory)) -Validation $recoveryValidation -ErrorMessage $null
        } catch {
            $RecoveryFacts.Error = $_.Exception.Message
            Add-ReadinessError ('Recovery validation failed: ' + $RecoveryFacts.Error)
        }
    } else {
        $RecoveryFacts.Error = "Recovery directory does not exist: $RecoveryDirectory"
        Add-ReadinessError ('Recovery validation failed: ' + $RecoveryFacts.Error)
    }

    try {
        $preflightState = [pscustomobject]@{ SourceRoot = $SourceRoot }
        $preflightResult = & $PlatformModule { param($State, $Directory) Get-GpuPreflight -State $State -ActionDirectory $Directory -Name 'gpu-preflight-readiness' } $preflightState $EvidenceDirectory
        $PreflightFacts.Path = [string]$preflightResult.Path
        $PreflightFacts.Pin = $preflightResult.Pin
        $PreflightFacts.InventoryComplete = ($preflightResult.Report.InventoryComplete -eq $true)
        $PreflightFacts.IntelPresent = ($null -ne $preflightResult.Report.DiagnosticCandidate -and $preflightResult.Report.DiagnosticCandidate.Present -eq $true -and [string]$preflightResult.Report.DiagnosticCandidate.PciId -eq 'PCI\VEN_8086&DEV_4680')
        $PreflightFacts.NvidiaPresent = ($null -ne $preflightResult.Report.Nvidia -and $preflightResult.Report.Nvidia.Present -eq $true -and [string]$preflightResult.Report.Nvidia.PciId -match '^PCI\\VEN_10DE')
        $PreflightFacts.ConnectedMonitorCount = $preflightResult.Report.PhysicalOutputEvidence.ConnectedMonitorCount
        & $PlatformModule { param($Report) Assert-GpuPreflightAdapters -Report $Report } $preflightResult.Report
        $PreflightFacts.Complete = ($PreflightFacts.InventoryComplete -and $PreflightFacts.IntelPresent -and $PreflightFacts.NvidiaPresent)
        if (-not $PreflightFacts.Complete) { throw 'Display/monitor preflight did not identify the expected adapters.' }
    } catch {
        $PreflightFacts.Error = $_.Exception.Message
        Add-ReadinessError ('Preflight failed: ' + $PreflightFacts.Error)
    }

    $espMountAttempted = $false
    try {
        $espMountAttempted = $true
        $EspFacts.Identity = & $PlatformModule { param($Directory) Get-GpuEspMount -ActionDirectory $Directory } $EvidenceDirectory
        $EspMount = $EspFacts.Identity
        $EspFacts.Identity = [pscustomobject]@{
            Volume = $EspMount.Volume
            DiskNumber = $EspMount.DiskNumber
            DiskUniqueId = $EspMount.DiskUniqueId
            PartitionNumber = $EspMount.PartitionNumber
            PartitionGuid = $EspMount.PartitionGuid
            GptType = $EspMount.GptType
        }
        $EspFacts.Inspected = $true
        $EspFacts.MountedDuringInspection = $true
        $EspFacts.MountCleanupState = 'PendingCleanup'
        $null = Assert-GpuPathWithin -Path 'S:\EFI' -Root 'S:\' -MustExist
        try {
            $null = & $PlatformModule { Assert-GpuNoPreviousStage -EspRoot 'S:\' }
        } catch {
            # The helper also rejects a retained stage directory, but that must
            # not prevent recording the current Microsoft boot-manager pin.
            if ([string]::IsNullOrWhiteSpace($EspFacts.Error)) { $EspFacts.Error = $_.Exception.Message }
        }
        $EspFacts.BootManager = 'EFI\Microsoft\Boot\bootmgfw.efi'
        $bootManagerPath = 'S:\EFI\Microsoft\Boot\bootmgfw.efi'
        $null = Assert-GpuPathWithin -Path $bootManagerPath -Root 'S:\' -MustExist
        $bootManagerPin = Get-GpuFilePin -Path $bootManagerPath
        $EspFacts.BootManagerPin = [pscustomobject]@{ RelativePath = 'EFI\Microsoft\Boot\bootmgfw.efi'; Sha256 = $bootManagerPin.Sha256; Bytes = $bootManagerPin.Bytes }
    } catch {
        $EspFacts.Error = $_.Exception.Message
        Add-ReadinessError ('ESP inspection failed: ' + $EspFacts.Error)
        if ($espMountAttempted -and $null -eq $EspMount) {
            # Get-GpuEspMount cleans up failures after its mount command. If S:
            # still resolves after an acquisition failure, its ownership is
            # ambiguous; retain the mapping and fail closed instead of guessing.
            if ((Get-PSDrive -Name S -ErrorAction SilentlyContinue) -or (Test-Path -LiteralPath 'S:\')) {
                $EspFacts.MountCleanupState = 'Unknown'
                $CleanupErrors.Add('S: still resolves after ESP mount acquisition failed; ownership is ambiguous and no unmount was attempted.')
            }
        }
    }

    if ($EspFacts.MountedDuringInspection) {
        $stagePath = 'S:\EFI\insecure-boot'
        try {
            if (Test-Path -LiteralPath 'S:\EFI' -PathType Container) {
                $null = Assert-GpuPathWithin -Path $stagePath -Root 'S:\' -AllowRoot
            }
            $StageFacts = Get-GpuBaremetalStageInventory -StageDirectory $stagePath
        } catch {
            $StageFacts.Complete = $false
            $StageFacts.Errors = @($_.Exception.Message)
            Add-ReadinessError ('Stage inventory failed: ' + $_.Exception.Message)
        }
        if ($StageFacts.Errors.Count -gt 0) { foreach ($message in $StageFacts.Errors) { Add-ReadinessError ('Stage inventory: ' + $message) } }

        $bootDirectory = 'S:\EFI\Microsoft\Boot'
        $residueNames = @('bootmgfw.efi.bak', 'bootmgfw.efi.ibnew', 'bootmgfw.efi.ibold', 'bootmgfw.sha256', 'grubx64.efi')
        $foundResidues = New-Object 'System.Collections.Generic.List[string]'
        foreach ($name in $residueNames) {
            $candidate = Join-Path $bootDirectory $name
            if (Test-Path -LiteralPath $candidate) { $foundResidues.Add('EFI\Microsoft\Boot\' + $name) }
        }
        $ResidueFacts.Residues = @($foundResidues.ToArray())
        try {
            $null = & $PlatformModule { param($Root) Assert-GpuNoPreviousStage -EspRoot $Root } 'S:\'
            $ResidueFacts.Clear = $true
        } catch {
            $ResidueFacts.ValidationError = $_.Exception.Message
            $stageOnlyFailure = ([string]$_.Exception.Message -like '*EFI\insecure-boot already exists*')
            $ResidueFacts.Clear = ($stageOnlyFailure -and $ResidueFacts.Residues.Count -eq 0)
            if (-not $stageOnlyFailure -and [string]::IsNullOrWhiteSpace($EspFacts.Error)) { $EspFacts.Error = $_.Exception.Message }
        }
    }
} catch {
    Add-ReadinessError $_.Exception.Message
} finally {
    if ($null -ne $EspMount) {
        try {
            & $PlatformModule { param($Directory) Dismount-GpuEspMount -ActionDirectory $Directory } $EvidenceDirectory
            $EspFacts.MountCleanupState = 'Unmounted'
        } catch {
            $EspFacts.MountCleanupState = 'Unknown'
            $CleanupErrors.Add($_.Exception.Message)
            Add-ReadinessError ('ESP unmount failed: ' + $_.Exception.Message)
        }
    }
    if ($null -ne $Lock) { $Lock.Dispose(); $Lock = $null }
}

$facts = New-GpuBaremetalReadinessFacts `
    -Administrator $AdministratorCheck.Passed -Source $SourceFacts -Selector $SelectorFacts `
    -HostSnapshot $HostSnapshot -Preflight $PreflightFacts -Recovery $RecoveryFacts `
    -EspMountAcquired $EspFacts.MountedDuringInspection -EspUnmountSucceeded ($EspFacts.MountCleanupState -eq 'Unmounted') -EspMountStateAfterCleanup $EspFacts.MountCleanupState `
    -BootManagerPin $EspFacts.BootManagerPin -Stage $StageFacts -Residue $ResidueFacts `
    -CleanupErrors ([string[]]$CleanupErrors.ToArray()) -Errors ([string[]]$ReadinessErrors.ToArray())
$decision = Get-GpuBaremetalReadinessDecision -Facts $facts
$report = [pscustomobject][ordered]@{
    SchemaVersion = 1
    Status = $decision.Status
    Succeeded = [bool]$decision.Succeeded
    GeneratedUtc = [DateTime]::UtcNow.ToString('o')
    Source = [pscustomobject]@{
        Branch = $SourceFacts.Branch
        Commit = $SourceFacts.Commit
        Dirty = $SourceFacts.Dirty
        DirtyEntryCount = $SourceFacts.DirtyEntryCount
        Pins = $SourceFacts.Pins
    }
    Host = if ($null -ne $HostSnapshot) { [pscustomobject]@{ WindowsVersion = $HostSnapshot.WindowsVersion; LogicalCpus = $HostSnapshot.LogicalCpus; SystemDrive = $HostSnapshot.SystemDrive; SystemDiskNumber = $HostSnapshot.SystemDiskNumber; SecureBoot = $HostSnapshot.SecureBoot } } else { $null }
    Recovery = [pscustomobject]@{ Valid = $RecoveryFacts.Valid; Directory = $RecoveryFacts.Directory; Disk = $RecoveryFacts.Disk; Error = $RecoveryFacts.Error }
    Selector = $SelectorFacts
    Preflight = $PreflightFacts
    Esp = [pscustomobject]@{ Inspected = $facts.Esp.Inspected; MountedDuringInspection = $facts.Esp.MountedDuringInspection; MountStateAfterCleanup = $facts.Esp.MountStateAfterCleanup; Identity = $EspFacts.Identity; BootManagerRelativePath = $EspFacts.BootManager; BootManagerPin = $EspFacts.BootManagerPin; Error = $EspFacts.Error }
    ExistingStage = $StageFacts
    ExistingResidue = $ResidueFacts
    SharedLock = [pscustomobject]@{ Path = '.analysis/gpu-baremetal/host-step.lock'; Acquired = $LockFacts.Acquired; Error = $LockFacts.Error }
    Cleanup = [pscustomobject]@{ Succeeded = ($CleanupErrors.Count -eq 0); Errors = @($CleanupErrors.ToArray()) }
    Effects = [pscustomobject]@{
        LocalEvidenceDirectoryCreated = $EvidenceCreated
        SharedStateDirectoryCreated = $StateRootCreated
        SharedLockFileCreated = $LockFileCreated
        EspTemporarilyMounted = $EspFacts.MountedDuringInspection
        EspFilesChanged = $false
        FirmwareChanged = $false
        BootStateChanged = $false
    }
    Blockers = $decision.Blockers
    NextStep = $decision.NextStep
    Scope = 'Readiness evidence only. This report does not prove Intel GPU ownership, MOK enrollment, GOP output routing, guest exclusion, DMA isolation, or successful native scanout. A connected monitor inventory entry does not prove which physical connector carries its signal.'
    EvidenceDirectory = $EvidenceDirectory
    EvidencePath = $EvidencePath
}

if ($EvidenceCreated) {
    try {
        $json = ConvertTo-Json -InputObject $report -Depth 16
        [IO.File]::WriteAllText($EvidencePath, $json + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))
        $pin = Get-GpuFilePin -Path $EvidencePath
        $report | Add-Member -NotePropertyName EvidencePin -NotePropertyValue ([pscustomobject]@{ RelativePath = '.analysis/gpu-baremetal-readiness/<run>/readiness.json'; Sha256 = $pin.Sha256; Bytes = $pin.Bytes }) -Force
    } catch {
        [Console]::Error.WriteLine('Could not save readiness evidence: ' + $_.Exception.Message)
        $report = Set-GpuBaremetalReportSaveFailure -Report $report -Decision $decision
    }
}

$report | ConvertTo-Json -Depth 16
if (-not $decision.Succeeded) { exit 1 }
