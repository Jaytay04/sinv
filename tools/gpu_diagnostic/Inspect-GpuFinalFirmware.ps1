[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Low')]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Get-GpuFinalFirmwarePin {
    param([Parameter(Mandatory = $true)][string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw "Expected a regular, non-reparse file: $Path"
    }
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
    return [pscustomobject]@{ Path = [IO.Path]::GetFullPath($Path); Sha256 = $hash; Bytes = [long]$item.Length }
}

function Assert-GpuFinalFirmwarePin {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Sha256,
          [Parameter(Mandatory = $true)][long]$Bytes, [Parameter(Mandatory = $true)][string]$Description)
    $actual = Get-GpuFinalFirmwarePin -Path $Path
    if ($actual.Bytes -ne $Bytes -or $actual.Sha256 -cne $Sha256) {
        throw "$Description differs from its reviewed SHA-256/length pin."
    }
    return $actual
}

function Assert-GpuFinalFirmwareInspectionJournal {
    param([Parameter(Mandatory = $true)][object]$Selector,
          [Parameter(Mandatory = $true)][object]$Journal,
          [Parameter(Mandatory = $true)][string]$ExpectedSourceRoot,
          [Parameter(Mandatory = $true)][string]$ExpectedRunDirectory)

    $expectedRunId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
    $expectedAction = '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'
    if ($Selector.SchemaVersion -ne 1 -or [string]$Selector.RunId -cne $expectedRunId -or
        [IO.Path]::GetFullPath([string]$Selector.RunDirectory) -cne [IO.Path]::GetFullPath($ExpectedRunDirectory) -or
        [IO.Path]::GetFullPath([string]$Selector.SourceRoot) -cne [IO.Path]::GetFullPath($ExpectedSourceRoot)) {
        throw 'The active selector does not identify the fixed failed run and reviewed feature worktree.'
    }
    if ($Journal.SchemaVersion -ne 1 -or [string]$Journal.RunId -cne $expectedRunId -or
        [string]$Journal.SourceCommit -cne '933b4c676318f26396cdf2c66de01c3ccbf1a950' -or
        [IO.Path]::GetFullPath([string]$Journal.SourceRoot) -cne [IO.Path]::GetFullPath($ExpectedSourceRoot) -or
        [IO.Path]::GetFullPath([string]$Journal.RunDirectory) -cne [IO.Path]::GetFullPath($ExpectedRunDirectory)) {
        throw 'The journal identity or source revision differs from the reviewed failed run.'
    }
    if ([string]$Journal.Phase -cne 'Disarmed' -or $Journal.EverArmIntent -ne $true -or
        [string]::IsNullOrWhiteSpace([string]$Journal.DisarmedBootId)) {
        throw 'The run is not in the reviewed disarmed state.'
    }
    if ($null -eq $Journal.LastAction -or [string]$Journal.LastAction.Action -cne 'FinalVerify' -or
        $Journal.LastAction.Succeeded -ne $false -or
        [IO.Path]::GetFileName([string]$Journal.LastAction.Directory) -cne $expectedAction) {
        throw 'The latest action is not the fixed failed FinalVerify capture.'
    }
    if ($null -eq $Journal.InProgress -or [string]$Journal.InProgress.Action -cne 'FinalVerify' -or
        [IO.Path]::GetFullPath([string]$Journal.InProgress.Directory) -cne [IO.Path]::GetFullPath([string]$Journal.LastAction.Directory)) {
        throw 'The journal does not retain the failed FinalVerify intent for this exact action directory.'
    }

    $verification = $Journal.FinalVerification
    $health = $verification.Health
    $esp = $verification.Esp
    $firmware = $verification.FirmwareEnumeration
    $expectedError = 'Firmware enumeration differs from the saved textual baseline.'
    if ($verification.ResetBoundaryPassed -ne $true -or $verification.SecureBoot -ne $true -or
        $verification.ActivationAttempted -ne $true -or $null -eq $health -or $health.Passed -ne $true -or
        [string]$health.ExpectedResult -cne 'ABSENT' -or [string]$health.Aggregate -cne 'ABSENT' -or
        [int]$health.ExpectedCpus -ne 20 -or [int]$health.CpuCount -ne 20 -or
        $null -eq $esp -or $esp.MatchesBaseline -ne $true -or
        [string]$esp.BootManagerSha256 -cne '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511' -or
        $null -eq $firmware -or $firmware.TextEqual -ne $false -or
        [string]$firmware.BaselineSha256 -cne 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE' -or
        [string]$firmware.CurrentSha256 -cne '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE' -or
        [string]$firmware.CaptureContext -cne 'SystemEspMountedAtS' -or
        @($verification.Errors).Count -ne 1 -or [string]$verification.Errors[0] -cne $expectedError) {
        throw 'The failed FinalVerify does not match the reviewed reset, health, ESP, and single firmware-text mismatch gates.'
    }
}

function Write-GpuFinalFirmwareJson {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][object]$Value)
    $json = ConvertTo-Json -InputObject $Value -Depth 40 -Compress -ErrorAction Stop
    $bytes = (New-Object Text.UTF8Encoding($false)).GetBytes($json + "`n")
    $stream = New-Object IO.FileStream($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    return Get-GpuFinalFirmwarePin -Path $Path
}

function Invoke-GpuFinalFirmwareObservation {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][object]$State,
          [Parameter(Mandatory = $true)][string]$ActionDirectory,
          [Parameter(Mandatory = $true)][hashtable]$Operations)

    $result = [ordered]@{
        Succeeded = $false; Status = 'READONLY_FIRMWARE_REVIEW_FAILED'; Esp = $null
        EspUnmounted = $true; CleanupSucceeded = $true; FirmwareObservations = @()
        BootSelectionCaptures = @(); Health = $null; Errors = @(); CleanupErrors = @()
        FirmwareTextStable = $false; RawBootSelectionStable = $false
    }
    $mount = $null
    try {
        $mount = & $Operations.MountEsp $ActionDirectory
        if ($null -eq $mount -or $mount.Acquired -ne $true) { throw 'The system ESP was not acquired by this observation.' }
        $result.EspUnmounted = $false
        $espCheck = & $Operations.AssertMountedEsp $mount $State.Backup
        $result.Esp = [pscustomobject]@{
            Volume=[string]$mount.Identity.Volume; DiskNumber=[int]$mount.Identity.DiskNumber
            PartitionGuid=[string]$mount.Identity.PartitionGuid; BootManagerSha256=[string]$espCheck.Sha256
        }

        $firmwareBefore = & $Operations.CaptureFirmware 'before'
        if ($null -eq $firmwareBefore -or $firmwareBefore.Succeeded -ne $true) { throw 'Mounted firmware text capture failed before the raw capture.' }
        $result.FirmwareObservations += $firmwareBefore
        $bootBefore = & $Operations.CaptureBootSelection 'before'
        if ($null -eq $bootBefore -or $null -eq $bootBefore.Baseline) { throw 'Raw boot-selection capture failed before health.' }
        $result.BootSelectionCaptures += $bootBefore

        $health = & $Operations.CaptureHealth
        if ($null -eq $health -or $health.CommandSucceeded -ne $true -or $null -eq $health.Verdict -or
            $health.Verdict.Passed -ne $true -or [int]$health.Verdict.CpuCount -ne 20 -or
            [int]$health.Verdict.ExpectedCpus -ne 20 -or [string]$health.Verdict.Aggregate -cne 'ABSENT' -or
            [string]$health.Verdict.ExpectedResult -cne 'ABSENT') {
            throw 'Fresh post-reset health did not report 20 ABSENT CPUs.'
        }
        $result.Health = $health

        $bootAfter = & $Operations.CaptureBootSelection 'after'
        if ($null -eq $bootAfter -or $null -eq $bootAfter.Baseline) { throw 'Raw boot-selection capture failed after health.' }
        & $Operations.AssertBootSelectionStable $bootBefore.Baseline $bootAfter.Baseline
        $result.BootSelectionCaptures += $bootAfter
        $result.RawBootSelectionStable = $true

        $firmwareAfter = & $Operations.CaptureFirmware 'after'
        if ($null -eq $firmwareAfter -or $firmwareAfter.Succeeded -ne $true) { throw 'Mounted firmware text capture failed after the raw capture.' }
        $result.FirmwareObservations += $firmwareAfter
        $result.FirmwareTextStable = [string]::Equals([string]$firmwareBefore.Text, [string]$firmwareAfter.Text, [StringComparison]::Ordinal)
        if (-not $result.FirmwareTextStable) { throw 'Mounted bcdedit firmware text changed during the observation window.' }
        $result.Status = 'READONLY_FIRMWARE_REVIEW_CAPTURED'
        $result.Succeeded = $true
    }
    catch { $result.Errors = @($result.Errors) + @([string]$_.Exception.Message) }
    finally {
        if ($null -ne $mount -and $mount.Acquired -eq $true) {
            try { & $Operations.DismountEsp $mount $ActionDirectory; $result.EspUnmounted = $true }
            catch {
                $result.CleanupErrors = @($result.CleanupErrors) + @([string]$_.Exception.Message)
                $result.CleanupSucceeded = $false; $result.EspUnmounted = $false; $result.Succeeded = $false
                $result.Status = 'READONLY_FIRMWARE_REVIEW_FAILED'
            }
        }
        if (-not $result.Succeeded) { $result.Status = 'READONLY_FIRMWARE_REVIEW_FAILED' }
    }
    return [pscustomobject]$result
}

# WhatIf is intentionally handled before module imports, path reads, native
# commands, or evidence-directory creation. Function declarations above are inert.
if ($WhatIfPreference) {
    [pscustomobject]@{
        Status = 'PREVIEW_ONLY'; Preview = $true; FilesystemChangesPerformed = $false
        NativeCommandsExecuted = $false; RecoveryCompleted = $false
    } | ConvertTo-Json -Depth 4
    return
}

$toolRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $toolRoot '..\..'))
$workspaceRoot = [IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $sourceRoot))))
if ((Split-Path -Leaf (Split-Path -Parent $sourceRoot)) -ine 'worktrees' -or
    (Split-Path -Leaf (Split-Path -Parent (Split-Path -Parent $sourceRoot))) -ine '.codex-build') {
    $workspaceRoot = $sourceRoot
}
$stateRoot = Join-Path $workspaceRoot '.analysis\gpu-baremetal'
$runId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
$runDirectory = Join-Path (Join-Path $stateRoot 'runs') $runId
$actionDirectory = Join-Path (Join-Path $runDirectory 'actions') '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'
$expectedBackupAction = Join-Path (Join-Path $runDirectory 'actions') '20261020T201533358Z-Backup-479329a8e70a4124964be17cc597ad61'
$expectedBaselinePath = Join-Path $expectedBackupAction 'firmware-enumeration-backup-4196e13c22604293a54688532c15cff6.stdout.txt'
$baselinePath = $null
$currentFirmwarePath = Join-Path $actionDirectory 'firmware-enumeration-final-7a5347fd0bc4452eb06340ba1e614a92.stdout.txt'
$auditToolPath = Join-Path $workspaceRoot '.codex-build\gpu-stage-retirement-audit\readonly-v1\ibaudit.exe'
$referenceLogExpected = '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'
$reviewRoot = Join-Path $workspaceRoot '.analysis\gpu-final-firmware-review'
$evidenceDirectory = Join-Path $reviewRoot ([guid]::NewGuid().ToString('N'))
$resultPath = Join-Path $evidenceDirectory 'result.json'
$stateModule = $null
$platformModule = $null
$retirementModule = $null
$sourceModulePaths = [ordered]@{
    State = Join-Path $toolRoot 'GpuHostState.psm1'
    Platform = Join-Path $toolRoot 'GpuHostPlatform.psm1'
    Retirement = Join-Path $toolRoot 'GpuStageRetirement.psm1'
}
$inspectorPinBefore = $null
$sourceModulePinsBefore = $null
$selectorPinBefore = $null
$journalPinBefore = $null
$lock = $null
$report = [ordered]@{
    SchemaVersion = 1; Status = 'READONLY_FIRMWARE_REVIEW_FAILED'; Succeeded = $false
    RunId = $runId; RecoveryCompleted = $false; HistoricalRawBootOrderUnchangedProven = $false
    SelectorAndJournalUnchanged = $false; EspUnmounted = $true; CleanupSucceeded = $true; EspFilesChanged = $false; FirmwareWritten = $false
    EvidenceDirectory = $evidenceDirectory; Errors = @(); CleanupErrors = @(); PinsBefore = $null; PinsAfter = $null
    FirmwareObservations = @(); BootSelectionCaptures = @(); Health = $null
    Note = 'Read-only evidence capture only. This does not complete recovery or prove that historical BootOrder never changed.'
}

if (-not $PSCmdlet.ShouldProcess($runId, 'Capture bounded post-recovery firmware evidence')) {
    [pscustomobject]@{
        SchemaVersion = 1; Status = 'CAPTURE_DECLINED'; Succeeded = $false; RunId = $runId
        RecoveryCompleted = $false; FilesystemChangesPerformed = $false; NativeCommandsExecuted = $false
    } | ConvertTo-Json -Depth 4
    return
}

try {
    $inspectorPinBefore = Get-GpuFinalFirmwarePin -Path $PSCommandPath
    $sourceModulePinsBefore = [ordered]@{
        State = Assert-GpuFinalFirmwarePin -Path $sourceModulePaths.State -Sha256 '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC' -Bytes 19843 -Description 'Pinned GPU host state module'
        Platform = Assert-GpuFinalFirmwarePin -Path $sourceModulePaths.Platform -Sha256 '4816FC0E640D7AEB38DF301E4D53A4982D5CABAA780650FEA8BDEE5D7C416899' -Bytes 107733 -Description 'Pinned GPU host platform module'
        Retirement = Assert-GpuFinalFirmwarePin -Path $sourceModulePaths.Retirement -Sha256 'CCFA78B48A18AB90AE865ADEB14CA952D927955523B9E72F0C30A762F4FDB553' -Bytes 61875 -Description 'Pinned GPU stage-retirement module'
    }
    $report.PinsBefore = [pscustomobject]@{ InspectorScript=$inspectorPinBefore; Modules=[pscustomobject]$sourceModulePinsBefore }
    $stateModule = Import-Module $sourceModulePaths.State -Force -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    $platformModule = Import-Module $sourceModulePaths.Platform -Force -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    $retirementModule = Import-Module $sourceModulePaths.Retirement -Force -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r } $reviewRoot $workspaceRoot
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } $sourceRoot $workspaceRoot
    [void][IO.Directory]::CreateDirectory($evidenceDirectory)
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } $evidenceDirectory $workspaceRoot

    if (-not [IO.Directory]::Exists($stateRoot)) { throw 'The existing GPU host state directory is missing.' }
    $lockPath = Join-Path $stateRoot 'host-step.lock'
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } $lockPath $stateRoot
    if ([IO.Directory]::Exists($lockPath)) { throw 'The shared host-step lock path is a directory.' }
    try { $lock = [IO.File]::Open($lockPath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
    catch { throw 'Another GPU host action is running, or the existing shared host-step lock is unavailable.' }

    $selectorPath = Join-Path $stateRoot 'host-prepared.json'
    $journalPath = Join-Path $runDirectory 'journal.json'
    $selectorPinBefore = Get-GpuFinalFirmwarePin $selectorPath
    $journalPinBefore = Get-GpuFinalFirmwarePin $journalPath
    $report.PinsBefore = [pscustomobject]@{
        InspectorScript=$inspectorPinBefore; Modules=[pscustomobject]$sourceModulePinsBefore
        Selector=$selectorPinBefore; Journal=$journalPinBefore
    }
    $selector = Get-Content -LiteralPath $selectorPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    $journal = Get-Content -LiteralPath $journalPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    Assert-GpuFinalFirmwareInspectionJournal -Selector $selector -Journal $journal -ExpectedSourceRoot $sourceRoot -ExpectedRunDirectory $runDirectory

    $baselinePath = [IO.Path]::GetFullPath([string]$journal.Backup.FirmwareEnumeration.Path)
    if ($baselinePath -cne [IO.Path]::GetFullPath($expectedBaselinePath)) { throw 'The journal baseline path differs from the fixed current-run Backup capture.' }
    $baselinePinBefore = Assert-GpuFinalFirmwarePin -Path $baselinePath -Sha256 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE' -Bytes 4336 -Description 'Current-run firmware baseline capture'
    $currentPinBefore = Assert-GpuFinalFirmwarePin -Path $currentFirmwarePath -Sha256 '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE' -Bytes 4336 -Description 'Failed FinalVerify firmware capture'
    $auditToolPinBefore = Assert-GpuFinalFirmwarePin -Path $auditToolPath -Sha256 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' -Bytes 246784 -Description 'Pinned ibaudit executable'
    $referenceLog = $journal.Backup.ReferenceLog
    $referenceLogPinBefore = Assert-GpuFinalFirmwarePin -Path ([string]$referenceLog.Path) -Sha256 $referenceLogExpected -Bytes 40838 -Description 'Current run TPM reference log'
    if ([string]$referenceLog.Sha256 -cne $referenceLogExpected -or [long]$referenceLog.Bytes -ne 40838) { throw 'The journal reference-log pin differs from the reviewed pin.' }
    $expectedToolPaths = & $platformModule { param($s) Get-GpuExpectedToolPaths -State $s } $journal
    $probePath = [IO.Path]::GetFullPath([string]$expectedToolPaths.Probe)
    if ([IO.Path]::GetFullPath([string]$journal.Tools.Probe.Path) -cne $probePath) { throw 'The probe path differs from the current run build directory.' }
    if ([string]$journal.Tools.Probe.Sha256 -cne 'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47' -or [long]$journal.Tools.Probe.Bytes -ne 172544) {
        throw 'The current run probe journal pin differs from the reviewed build artifact.'
    }
    $probePinBefore = Assert-GpuFinalFirmwarePin -Path $probePath -Sha256 'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47' -Bytes 172544 -Description 'Current run health probe'
    $report.PinsBefore = [pscustomobject]@{
        Selector=$selectorPinBefore; Journal=$journalPinBefore; FirmwareBaseline=$baselinePinBefore; FirmwareCurrent=$currentPinBefore
        Ibaudit=$auditToolPinBefore; ReferenceLog=$referenceLogPinBefore; Probe=$probePinBefore
        InspectorScript=$inspectorPinBefore; Modules=[pscustomobject]$sourceModulePinsBefore
    }

    & $platformModule { Assert-GpuAdministrator }
    $current = & $platformModule { Get-GpuCurrentMachineSnapshot -IncludeSecureBoot }
    & $platformModule { param($s,$c) Assert-GpuSameMachine -State $s -Current $c -RequireCpuCount } $journal $current
    & $platformModule { param($c) Assert-GpuSecureBootOn -Current $c } $current
    if ([int]$current.LogicalCpus -ne 20) { throw 'Current host logical CPU count differs from the reviewed 20 CPUs.' }
    if ([string]::Equals([string]$current.BootId, [string]$journal.DisarmedBootId, [StringComparison]::Ordinal)) {
        throw 'A full host reset after disarm has not been observed.'
    }
    $report.CurrentHost = [pscustomobject]@{ BootId=[string]$current.BootId; LogicalCpus=[int]$current.LogicalCpus; SecureBoot=[bool]$current.SecureBoot }

    $observationsDirectory = Join-Path $evidenceDirectory 'observations'
    [void][IO.Directory]::CreateDirectory($observationsDirectory)
    $nativeOperations = & $retirementModule {
        param($root,$auditPath,$auditHash,$auditBytes,$logHash,$logBytes)
        New-GpuStageRetirementPlatformOperations -ToolRoot $root -AuditToolPath $auditPath -ExpectedAuditToolSha256 $auditHash -ExpectedAuditToolBytes $auditBytes -ExpectedReferenceLogSha256 $logHash -ExpectedReferenceLogBytes $logBytes
    } $toolRoot $auditToolPath 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' 246784 $referenceLogExpected 40838
    $captureOperations = @{}
    $captureOperations.MountEsp = $nativeOperations.MountEsp
    $captureOperations.AssertMountedEsp = $nativeOperations.AssertMountedEsp
    $captureOperations.DismountEsp = { param($m,$d) & $nativeOperations.DismountEsp $m }.GetNewClosure()
    $captureOperations.CaptureFirmware = {
        param([string]$Checkpoint)
        $capture = & $nativeOperations.CaptureFirmware $observationsDirectory
        if ($null -eq $capture -or [long]$capture.Pin.Bytes -ne 4336 -or
            [string]$capture.Pin.Sha256 -cne '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE') {
            throw "Mounted bcdedit /enum firmware /v differs from the reviewed current capture at $Checkpoint."
        }
        return [pscustomobject]@{ Checkpoint=$Checkpoint; Succeeded=$true; Text=[string]$capture.Text; Pin=$capture.Pin; ReceiptPath=$capture.ReceiptPath }
    }.GetNewClosure()
    $captureOperations.CaptureBootSelection = { param([string]$Checkpoint) & $nativeOperations.CaptureBootSelection $journal $observationsDirectory $Checkpoint }.GetNewClosure()
    $captureOperations.CaptureHealth = {
        $capture = & $nativeOperations.CaptureHealth $journal $observationsDirectory
        return [pscustomobject]@{ CommandSucceeded=[bool]$capture.CommandSucceeded; Verdict=$capture.Verdict; ReceiptPath=$capture.ReceiptPath }
    }.GetNewClosure()
    $captureOperations.AssertBootSelectionStable = { param($before,$after) & $retirementModule { param($b,$a) Assert-GpuStageRetirementBootSelectionStable -Before $b -Current $a -Checkpoint 'during firmware inspection' } $before $after }.GetNewClosure()
    $observation = Invoke-GpuFinalFirmwareObservation -State $journal -ActionDirectory $observationsDirectory -Operations $captureOperations
    $report.Esp = $observation.Esp
    $report.EspUnmounted = $observation.EspUnmounted
    $report.CleanupSucceeded = $observation.CleanupSucceeded
    $report.CleanupErrors = @($observation.CleanupErrors)
    $report.FirmwareObservations = @($observation.FirmwareObservations)
    $report.BootSelectionCaptures = @($observation.BootSelectionCaptures)
    $report.Health = $observation.Health
    $report.FirmwareTextStable = $observation.FirmwareTextStable
    $report.RawBootSelectionStable = $observation.RawBootSelectionStable
    if (-not $observation.Succeeded) {
        $captureErrors = @(@($observation.Errors) + @($observation.CleanupErrors))
        throw ($captureErrors -join ' ')
    }
    $report.Status = 'READONLY_FIRMWARE_REVIEW_CAPTURED'
    $report.Succeeded = $true
}
catch {
    $report.Errors = @($report.Errors) + @([string]$_.Exception.Message)
}
finally {
    if ($null -ne $lock) {
        try {
            $selectorAfter = Get-GpuFinalFirmwarePin -Path (Join-Path $stateRoot 'host-prepared.json')
            $journalAfter = Get-GpuFinalFirmwarePin -Path (Join-Path $runDirectory 'journal.json')
            $pinsAfter = [ordered]@{ Selector=$selectorAfter; Journal=$journalAfter }
            $report.SelectorAndJournalUnchanged = ($null -ne $selectorPinBefore -and $null -ne $journalPinBefore -and
                $selectorAfter.Sha256 -ceq $selectorPinBefore.Sha256 -and $selectorAfter.Bytes -eq $selectorPinBefore.Bytes -and
                $journalAfter.Sha256 -ceq $journalPinBefore.Sha256 -and $journalAfter.Bytes -eq $journalPinBefore.Bytes)
            if (-not $report.SelectorAndJournalUnchanged) { throw 'Selector or journal changed during the read-only evidence capture.' }
            if ($report.PinsBefore) {
                $pinsAfter.FirmwareBaseline = Assert-GpuFinalFirmwarePin -Path $baselinePath -Sha256 $report.PinsBefore.FirmwareBaseline.Sha256 -Bytes $report.PinsBefore.FirmwareBaseline.Bytes -Description 'Current-run firmware baseline after observation'
                $pinsAfter.FirmwareCurrent = Assert-GpuFinalFirmwarePin -Path $currentFirmwarePath -Sha256 $report.PinsBefore.FirmwareCurrent.Sha256 -Bytes $report.PinsBefore.FirmwareCurrent.Bytes -Description 'Failed FinalVerify firmware capture after observation'
                $pinsAfter.Ibaudit = Assert-GpuFinalFirmwarePin -Path $auditToolPath -Sha256 $report.PinsBefore.Ibaudit.Sha256 -Bytes $report.PinsBefore.Ibaudit.Bytes -Description 'Pinned ibaudit after observation'
                $pinsAfter.ReferenceLog = Assert-GpuFinalFirmwarePin -Path ([string]$report.PinsBefore.ReferenceLog.Path) -Sha256 $report.PinsBefore.ReferenceLog.Sha256 -Bytes $report.PinsBefore.ReferenceLog.Bytes -Description 'Reference log after observation'
                $pinsAfter.Probe = Assert-GpuFinalFirmwarePin -Path ([string]$report.PinsBefore.Probe.Path) -Sha256 $report.PinsBefore.Probe.Sha256 -Bytes $report.PinsBefore.Probe.Bytes -Description 'Health probe after observation'
            }
            $pinsAfter.InspectorScript = Assert-GpuFinalFirmwarePin -Path $inspectorPinBefore.Path -Sha256 $inspectorPinBefore.Sha256 -Bytes $inspectorPinBefore.Bytes -Description 'Inspector script after observation'
            $pinsAfter.Modules = [pscustomobject]@{
                State = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.State.Path -Sha256 $sourceModulePinsBefore.State.Sha256 -Bytes $sourceModulePinsBefore.State.Bytes -Description 'GPU host state module after observation'
                Platform = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.Platform.Path -Sha256 $sourceModulePinsBefore.Platform.Sha256 -Bytes $sourceModulePinsBefore.Platform.Bytes -Description 'GPU host platform module after observation'
                Retirement = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.Retirement.Path -Sha256 $sourceModulePinsBefore.Retirement.Sha256 -Bytes $sourceModulePinsBefore.Retirement.Bytes -Description 'GPU stage-retirement module after observation'
            }
            $report.PinsAfter = [pscustomobject]$pinsAfter
        }
        catch { $report.Errors = @($report.Errors) + @([string]$_.Exception.Message); $report.Succeeded = $false; $report.Status = 'READONLY_FIRMWARE_REVIEW_FAILED' }
        finally { $lock.Dispose(); $lock = $null }
    }
    elseif ($null -ne $inspectorPinBefore -and $null -ne $sourceModulePinsBefore) {
        try {
            $report.PinsAfter = [pscustomobject]@{
                InspectorScript = Assert-GpuFinalFirmwarePin -Path $inspectorPinBefore.Path -Sha256 $inspectorPinBefore.Sha256 -Bytes $inspectorPinBefore.Bytes -Description 'Inspector script after preflight'
                Modules = [pscustomobject]@{
                    State = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.State.Path -Sha256 $sourceModulePinsBefore.State.Sha256 -Bytes $sourceModulePinsBefore.State.Bytes -Description 'GPU host state module after preflight'
                    Platform = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.Platform.Path -Sha256 $sourceModulePinsBefore.Platform.Sha256 -Bytes $sourceModulePinsBefore.Platform.Bytes -Description 'GPU host platform module after preflight'
                    Retirement = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.Retirement.Path -Sha256 $sourceModulePinsBefore.Retirement.Sha256 -Bytes $sourceModulePinsBefore.Retirement.Bytes -Description 'GPU stage-retirement module after preflight'
                }
            }
        }
        catch { $report.Errors = @($report.Errors) + @([string]$_.Exception.Message); $report.Succeeded = $false; $report.Status = 'READONLY_FIRMWARE_REVIEW_FAILED' }
    }
    if (-not $report.Succeeded) { $report.Status = 'READONLY_FIRMWARE_REVIEW_FAILED' }
    try { $report.ResultPin = Write-GpuFinalFirmwareJson -Path $resultPath -Value ([pscustomobject]$report) }
    catch { $report.Errors = @($report.Errors) + @("Could not write result.json: $($_.Exception.Message)"); $report.Succeeded = $false; $report.Status = 'READONLY_FIRMWARE_REVIEW_FAILED' }
}

$report | ConvertTo-Json -Depth 40
if (-not $report.Succeeded) { exit 1 }
