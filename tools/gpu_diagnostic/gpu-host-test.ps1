[CmdletBinding(SupportsShouldProcess=$true, ConfirmImpact='Medium')]
param(
    [Parameter(Position=0, Mandatory=$true)]
    [ValidateSet('Prepare','Build','Sign','Backup','Stage','Arm','Collect','Disarm','FinalVerify','Status','Rollback','Archive')]
    [string]$Action,
    [string]$WorkspaceRoot,
    [string]$RunDirectory,
    [string]$RecoveryDirectory = 'E:\CodexBackup\Recovery\hsinv-gpu-test',
    [switch]$PhysicalOutputConfirmed,
    [switch]$RecoveryReady,
    [switch]$ReviewedFirmwareDifference
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ($ReviewedFirmwareDifference -and $Action -ne 'FinalVerify') {
    throw 'ReviewedFirmwareDifference is supported only with FinalVerify.'
}

# Preview never imports platform code, probes the host, creates a selector,
# mounts an ESP, signs an image, or invokes a native command.
if ($WhatIfPreference) {
    [pscustomobject]@{
        Action = $Action
        Preview = $true
        HostChangesPerformed = $false
        AutomaticReboot = $false
        ReviewedFirmwareDifferenceRequested = [bool]$ReviewedFirmwareDifference
        Note = 'No commands were executed. Prepare creates local/external evidence; Stage writes ESP files; Arm/Disarm change only owned one-shot boot state.'
    } | ConvertTo-Json -Depth 5
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
Import-Module (Join-Path $PSScriptRoot 'GpuHostState.psm1') -Force -DisableNameChecking
$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
$null = Assert-GpuPathWithin -Path $SourceRoot -Root $WorkspaceRoot -AllowRoot -MustExist
$StateRoot = Join-Path $WorkspaceRoot '.analysis\gpu-baremetal'
$RunsRoot = Join-Path $StateRoot 'runs'
$SelectorPath = Join-Path $StateRoot 'host-prepared.json'

function Get-GpuSourceCommit {
    $Branch = @(& git -C $SourceRoot branch --show-current)
    if ($LASTEXITCODE -ne 0 -or ($Branch -join '').Trim() -cne 'feat/smbios-boot-serial-probe') {
        throw 'Run the reviewed feat/smbios-boot-serial-probe worktree.'
    }
    $Changes = @(& git -C $SourceRoot status --porcelain)
    if ($LASTEXITCODE -ne 0 -or $Changes.Count -ne 0) {
        throw 'Source worktree must be clean. Preserve edits and use a reviewed commit.'
    }
    $Commit = (@(& git -C $SourceRoot rev-parse HEAD) -join '').Trim()
    if ($LASTEXITCODE -ne 0 -or $Commit -cnotmatch '^[0-9a-f]{40}$') {
        throw 'Cannot identify the source revision.'
    }
    return $Commit
}

function Get-GpuExpectedPaths([string]$Directory) {
    $RunLeaf = Split-Path -Leaf $Directory
    if ($RunLeaf -cnotmatch '^gpu-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{32}$') { throw 'Invalid run directory name.' }
    $ShortBuildRoot = Join-Path (Join-Path $WorkspaceRoot '.codex-build\gb') $RunLeaf.Substring($RunLeaf.Length - 16)
    [pscustomobject]@{
        HypervisorBuild = Join-Path $ShortBuildRoot 'h'
        LoaderBuild = Join-Path $ShortBuildRoot 'l'
        SignedDirectory = Join-Path $Directory 'signed'
    }
}

function Assert-GpuSamePath([string]$Actual, [string]$Expected) {
    if ([string]::IsNullOrWhiteSpace($Actual) -or
        -not [string]::Equals([IO.Path]::GetFullPath($Actual),
            [IO.Path]::GetFullPath($Expected), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Recorded path differs from its derived location: $Expected"
    }
}

function Save-Gpu042142ReviewEntryJournal {
    param([object]$State, [string]$ActionDirectory)

    $expectedRun = Join-Path ([string]$State.WorkspaceRoot) '.analysis\gpu-baremetal\runs\gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2'
    if ([string]$State.RunId -cne 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2') {
        throw 'The entry-journal checkpoint supports only the fixed 042142 run.'
    }
    Assert-GpuSamePath ([string]$State.RunDirectory) $expectedRun
    $null = Assert-GpuPathWithin -Path $ActionDirectory -Root (Join-Path $expectedRun 'actions') -MustExist
    if ([IO.Path]::GetFileName($ActionDirectory) -cnotmatch '^[0-9]{8}T[0-9]{9}Z-FinalVerify-[0-9a-f]{32}$') {
        throw 'The entry-journal checkpoint requires a FinalVerify action directory.'
    }
    $journalPath = Join-Path $expectedRun 'journal.json'
    $originalPin = Get-GpuFilePin -Path $journalPath
    if ($originalPin.Sha256 -cne '3287B5E86D018DD374748D56E16CF7BBDE0D245ECF426779EAAB80E5A2B08583' -or
        $originalPin.Bytes -ne 44133L) {
        throw 'The 042142 journal differs from the exact pre-retry inspection pin; preserve the existing intent.'
    }
    $copyPath = Join-Path $ActionDirectory 'reviewed-firmware-entry-journal.json'
    $null = Assert-GpuPathWithin -Path $copyPath -Root $ActionDirectory
    [IO.File]::Copy($journalPath, $copyPath, $false)
    $copyPin = Get-GpuFilePin -Path $copyPath
    if ($copyPin.Sha256 -cne $originalPin.Sha256 -or $copyPin.Bytes -ne $originalPin.Bytes) {
        throw 'The 042142 pre-retry journal copy failed readback; preserve the original journal.'
    }
    Assert-GpuFilePin -Pin $originalPin
    return $copyPin
}

function Assert-GpuRecordedState($State, [string]$Directory) {
    foreach ($Name in @('SchemaVersion','RunId','WorkspaceRoot','SourceRoot','SourceCommit',
        'RunDirectory','RecoveryBase','RecoveryRunDirectory','Phase','InProgress','SourcePins',
        'Host','Paths','Tools','BuildArtifacts','SignedArtifacts','Backup','Stage','ArmBootId',
        'EverArmIntent','DisarmedBootId','Collection','FinalVerification')) {
        if ($null -eq $State.PSObject.Properties[$Name]) { throw "Run journal lacks $Name." }
    }
    if ($State.SchemaVersion -ne 1 -or $State.RunId -cnotmatch '^gpu-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{32}$') {
        throw 'Unsupported run journal identity/schema.'
    }
    Assert-GpuSamePath $State.WorkspaceRoot $WorkspaceRoot
    Assert-GpuSamePath $State.SourceRoot $SourceRoot
    Assert-GpuSamePath $State.RunDirectory $Directory
    Assert-GpuSamePath $Directory (Join-Path $RunsRoot $State.RunId)
    $Expected = Get-GpuExpectedPaths $Directory
    foreach ($Name in @('HypervisorBuild','LoaderBuild','SignedDirectory')) {
        Assert-GpuSamePath $State.Paths.$Name $Expected.$Name
        $null = Assert-GpuPathWithin -Path $State.Paths.$Name -Root $WorkspaceRoot
    }
    Assert-GpuSamePath $State.RecoveryRunDirectory (Join-Path $State.RecoveryBase $State.RunId)
    if ($State.RecoveryBase -notmatch '^[A-Za-z]:\\') { throw 'Recovery base must be a local drive directory.' }
    # Read-only status and owned-entry cleanup must remain available when the
    # external recovery disk is absent. Platform activation checks it live.
    Assert-GpuActionAllowed -Action Status -Phase $State.Phase
    $ExpectedPinPaths = @{
        Tools = @{
            Installer = Join-Path $Expected.LoaderBuild 'ib-install.exe'
            Probe = Join-Path $Expected.LoaderBuild 'ib-tcg-dump.exe'
            Verifier = Join-Path $Expected.LoaderBuild 'install_authenticode_test.exe'
            Shim = Join-Path $SourceRoot 'components\uefi-loader\src\vendor\shimx64.efi'
            Certificate = Join-Path $SourceRoot 'components\hypervisor\hypersinv.cer'
        }
        BuildArtifacts = @{
            EFI = Join-Path $Expected.HypervisorBuild 'src\efi\uefi_hypervisor.efi'
            Loader = Join-Path $Expected.LoaderBuild 'loader.efi'
        }
        SignedArtifacts = @{
            Loader = Join-Path $Expected.SignedDirectory 'loader.efi'
            Payload = Join-Path $Expected.SignedDirectory 'payload.efi'
        }
    }
    foreach ($Group in @('Tools','BuildArtifacts','SignedArtifacts')) {
        if ($null -eq $State.$Group) { continue }
        foreach ($Name in $ExpectedPinPaths[$Group].Keys) {
            if ($null -eq $State.$Group.PSObject.Properties[$Name]) { throw "Missing $Group pin: $Name" }
            Assert-GpuSamePath $State.$Group.$Name.Path $ExpectedPinPaths[$Group][$Name]
        }
    }
}

function Save-GpuJournal($State) {
    Write-GpuJson -Path (Join-Path $State.RunDirectory 'journal.json') -Value $State
}

function Copy-GpuRecoveryJournal($State) {
    # Recovery media may be unavailable during cleanup. Preserve the local
    # journal regardless; absence of this optional mirror never hides an arm.
    if (-not [IO.Directory]::Exists($State.RecoveryRunDirectory)) { return }
    try {
        $null = Assert-GpuPathWithin -Path $State.RecoveryRunDirectory -Root $State.RecoveryBase -MustExist
        Write-GpuJson -Path (Join-Path $State.RecoveryRunDirectory 'gpu-run-journal.json') -Value $State
    } catch {
        Write-Warning "Local journal saved; external mirror failed: $($_.Exception.Message)"
    }
}

function Get-GpuStatus($State) {
    [pscustomobject]@{
        Status = 'RECORDED_STATE_ONLY'
        RunDirectory = $State.RunDirectory
        Phase = $State.Phase
        InProgress = $State.InProgress
        LastFailure = $State.LastFailure
        EverArmIntent = $State.EverArmIntent
        RecoveryDirectory = $State.RecoveryRunDirectory
        Collection = $State.Collection
        FinalVerification = $State.FinalVerification
        BootNextLiveAttested = $false
        NativeGpuIsolationAttested = $false
        Note = 'Recorded phase is not a live BootNext or GPU ownership check. Never guess a run from directory timestamps.'
    }
}

if ($Action -ne 'Prepare') {
    foreach ($Name in @('RecoveryDirectory','PhysicalOutputConfirmed','RecoveryReady')) {
        if ($PSBoundParameters.ContainsKey($Name)) { throw "-$Name is a Prepare-only input." }
    }
}
if ($Action -eq 'Status' -and -not $RunDirectory -and -not (Test-Path -LiteralPath $SelectorPath)) {
    [pscustomobject]@{ Status='NOT_PREPARED'; Selector=$SelectorPath } | ConvertTo-Json
    return
}
if (-not $PSCmdlet.ShouldProcess($StateRoot, "GPU diagnostic action: $Action")) { return }
$null = Assert-GpuPathWithin -Path $StateRoot -Root $WorkspaceRoot
if (-not [IO.Directory]::Exists($StateRoot)) {
    if ($Action -ne 'Prepare') { throw "No saved run: $SelectorPath" }
    $null = New-Item -ItemType Directory -Path $StateRoot
}
$LockPath = Join-Path $StateRoot 'host-step.lock'
$null = Assert-GpuPathWithin -Path $LockPath -Root $StateRoot
try {
    $Lock = [IO.File]::Open($LockPath, [IO.FileMode]::OpenOrCreate,
        [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
} catch {
    throw 'Another GPU host action is running, or its action lock is unavailable. Wait for it; do not run recovery concurrently.'
}
$State = $null
$ActionDirectory = $null
$EntryJournalCheckpointPending = $false
try {
    if ($Action -eq 'Prepare') {
        if ($RunDirectory) { throw 'Prepare creates a new run; do not supply -RunDirectory.' }
        if (Test-Path -LiteralPath $SelectorPath) {
            throw "A recorded or interrupted Prepare already exists: $SelectorPath. Use Status or rollback; do not overwrite or choose a newer run."
        }
        if (-not $PhysicalOutputConfirmed -or -not $RecoveryReady) {
            throw 'Prepare requires -PhysicalOutputConfirmed and -RecoveryReady after the documented manual prerequisites.'
        }
        $Commit = Get-GpuSourceCommit
        $SourcePins = @(foreach ($Name in @('gpu-host-test.ps1','gpu-host-rollback.ps1',
            'GpuHostState.psm1','GpuHostPlatform.psm1')) {
            Get-GpuFilePin -Path (Join-Path $PSScriptRoot $Name)
        })
        $RecoveryBase = [IO.Path]::GetFullPath($RecoveryDirectory)
        if ($RecoveryBase -notmatch '^[A-Za-z]:\\') { throw 'RecoveryDirectory must be on a local drive.' }
        if ([string]::Equals($RecoveryBase.TrimEnd('\'),
            [IO.Path]::GetPathRoot($RecoveryBase).TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
            throw 'RecoveryDirectory must be a dedicated directory, not a drive root.'
        }
        $RunId = 'gpu-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N')
        $RunDirectory = Join-Path $RunsRoot $RunId
        $null = Assert-GpuPathWithin -Path $RunDirectory -Root $StateRoot
        $null = New-Item -ItemType Directory -Path $RunDirectory
        $State = [pscustomobject][ordered]@{
            SchemaVersion=1; RunId=$RunId; WorkspaceRoot=$WorkspaceRoot
            SourceRoot=$SourceRoot; SourceCommit=$Commit; RunDirectory=$RunDirectory
            RecoveryBase=$RecoveryBase; RecoveryRunDirectory=(Join-Path $RecoveryBase $RunId)
            Phase='Preparing'; InProgress=$null; SourcePins=$SourcePins; RecoveryHistory=@()
            Host=$null; Paths=(Get-GpuExpectedPaths $RunDirectory); Tools=$null
            BuildArtifacts=$null; SignedArtifacts=$null; Backup=$null; Stage=$null
            ArmBootId=$null; EverArmIntent=$false; DisarmedBootId=$null
            Collection=$null; FinalVerification=$null; LastFailure=$null; LastAction=$null
            CreatedUtc=[DateTime]::UtcNow.ToString('o'); UpdatedUtc=[DateTime]::UtcNow.ToString('o')
        }
        $ActionId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-Prepare-' + [guid]::NewGuid().ToString('N')
        $ActionDirectory = Join-Path (Join-Path $RunDirectory 'actions') $ActionId
        $null = Assert-GpuPathWithin -Path $ActionDirectory -Root $RunDirectory
        $null = New-Item -ItemType Directory -Path $ActionDirectory
        Save-GpuJournal $State
        # Publish the exact initial run before probing. If Prepare fails or is
        # interrupted, this selector remains and blocks accidental retries.
        Write-GpuJson -Path $SelectorPath -NoOverwrite -Value ([ordered]@{
            SchemaVersion=1; RunId=$RunId; RunDirectory=$RunDirectory
            JournalPath=(Join-Path $RunDirectory 'journal.json'); SourceRoot=$SourceRoot
        })
    } else {
        if (-not $RunDirectory) {
            if (-not (Test-Path -LiteralPath $SelectorPath -PathType Leaf)) {
                throw "No saved selector. Use the exact recorded -RunDirectory, never the newest directory: $SelectorPath"
            }
            $Selector = Read-GpuJson -Path $SelectorPath
            if ($Selector.SchemaVersion -ne 1 -or
                $Selector.RunId -cnotmatch '^gpu-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{32}$') {
                throw 'Unsupported selector schema or identity.'
            }
            Assert-GpuSamePath $Selector.SourceRoot $SourceRoot
            Assert-GpuSamePath $Selector.RunDirectory (Join-Path $RunsRoot $Selector.RunId)
            Assert-GpuSamePath $Selector.JournalPath (Join-Path $Selector.RunDirectory 'journal.json')
            $RunDirectory = $Selector.RunDirectory
        }
        $RunDirectory = Assert-GpuPathWithin -Path $RunDirectory -Root $RunsRoot -MustExist
        $State = Read-GpuJson -Path (Join-Path $RunDirectory 'journal.json')
        Assert-GpuRecordedState $State $RunDirectory
        if ($null -ne $State.InProgress -and $Action -notin @('Status','Disarm','Rollback') -and
            -not (Test-GpuFinalVerifyRetry -Action $Action -Phase $State.Phase -InProgress $State.InProgress)) {
            throw "An earlier action is incomplete: $($State.InProgress.Action). Preserve its evidence; use Status or rollback."
        }
        if ($Action -eq 'Archive') {
            if ($State.Phase -cne 'Complete' -or $null -eq $State.FinalVerification -or
                $null -eq $State.LastAction -or $State.LastAction.Action -cne 'FinalVerify' -or
                -not $State.LastAction.Succeeded) {
                throw 'Archive requires a successful FinalVerify and a Complete journal.'
            }
            $SavedSelector = Read-GpuJson -Path $SelectorPath
            Assert-GpuSamePath $SavedSelector.RunDirectory $State.RunDirectory
            if ($SavedSelector.RunId -cne $State.RunId) { throw 'Selector belongs to another run.' }
            $ArchiveRoot = Join-Path $StateRoot 'archive'
            $null = Assert-GpuPathWithin -Path $ArchiveRoot -Root $StateRoot
            if (-not [IO.Directory]::Exists($ArchiveRoot)) { $null = New-Item -ItemType Directory -Path $ArchiveRoot }
            $ArchivePath = Join-Path $ArchiveRoot ($State.RunId + '.json')
            $null = Assert-GpuPathWithin -Path $ArchivePath -Root $ArchiveRoot
            $OriginalPin = Get-GpuFilePin -Path $SelectorPath
            [IO.File]::Copy($SelectorPath, $ArchivePath, $false)
            $ArchivedPin = Get-GpuFilePin -Path $ArchivePath
            if ($OriginalPin.Sha256 -cne $ArchivedPin.Sha256 -or $OriginalPin.Bytes -ne $ArchivedPin.Bytes) {
                throw 'Archived selector readback failed; original selector retained.'
            }
            Assert-GpuFilePin -Pin $OriginalPin
            Remove-Item -LiteralPath $SelectorPath -Force
            [pscustomobject]@{ Status='SELECTOR_ARCHIVED'; RunDirectory=$RunDirectory;
                ArchivePath=$ArchivePath; JournalPreserved=$true; HostChangesPerformed=$false } |
                ConvertTo-Json -Depth 6
            return
        }
        Assert-GpuActionAllowed -Action $Action -Phase $State.Phase -InProgress $State.InProgress
        if ($Action -eq 'Status') {
            Get-GpuStatus $State | ConvertTo-Json -Depth 12
            return
        }
        if ($State.Phase -eq 'Complete' -and $Action -in @('Disarm','Rollback')) {
            [pscustomobject]@{ Status='ALREADY_COMPLETE'; RunDirectory=$RunDirectory;
                FinalVerification=$State.FinalVerification; HostChangesPerformed=$false } |
                ConvertTo-Json -Depth 12
            return
        }
        if ($Action -in @('Build','Sign','Backup','Stage','Arm')) {
            if ((Get-GpuSourceCommit) -cne $State.SourceCommit) { throw 'Source revision changed after Prepare.' }
            foreach ($Pin in $State.SourcePins) { Assert-GpuFilePin -Pin $Pin }
        }
    }
    if ($null -eq $ActionDirectory) {
        $ActionId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + $Action + '-' + [guid]::NewGuid().ToString('N')
        $ActionDirectory = Join-Path (Join-Path $RunDirectory 'actions') $ActionId
        $null = Assert-GpuPathWithin -Path $ActionDirectory -Root $RunDirectory
        $null = New-Item -ItemType Directory -Path $ActionDirectory
    }
    if ($Action -eq 'FinalVerify' -and $ReviewedFirmwareDifference -and
        [string]$State.RunId -ceq 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2') {
        # Preserve the exact inspected journal before replacing its retry intent.
        $EntryJournalCheckpointPending = $true
        $null = Save-Gpu042142ReviewEntryJournal -State $State -ActionDirectory $ActionDirectory
        $EntryJournalCheckpointPending = $false
    }
    if ($null -ne $State.InProgress) {
        if ($null -eq $State.PSObject.Properties['RecoveryHistory']) {
            $State | Add-Member -NotePropertyName RecoveryHistory -NotePropertyValue @()
        }
        $State.RecoveryHistory = @($State.RecoveryHistory) + @($State.InProgress)
    }
    $State.InProgress = [pscustomobject]@{ Action=$Action; Directory=$ActionDirectory; StartedUtc=[DateTime]::UtcNow.ToString('o') }
    $State.LastFailure = $null
    if ($Action -eq 'Arm') { $State.EverArmIntent = $true }
    $State.UpdatedUtc = [DateTime]::UtcNow.ToString('o')
    Save-GpuJournal $State
    Import-Module (Join-Path $PSScriptRoot 'GpuHostPlatform.psm1') -Force -DisableNameChecking
    $PlatformAction = $Action
    if ($Action -eq 'Rollback') { $PlatformAction = 'Disarm' }
    $Result = Invoke-GpuHostPlatformAction -Action $PlatformAction -State $State `
        -ActionDirectory $ActionDirectory -PhysicalOutputConfirmed:$PhysicalOutputConfirmed `
        -RecoveryReady:$RecoveryReady -ReviewedFirmwareDifference:$ReviewedFirmwareDifference
    if ($Result.Succeeded -and $Action -in @('Build','Sign','Backup','Stage','Arm')) {
        if ((Get-GpuSourceCommit) -cne $State.SourceCommit) { throw 'Source revision changed during the action.' }
        foreach ($Pin in $State.SourcePins) { Assert-GpuFilePin -Pin $Pin }
    }
    $NextPhase = @{
        Prepare='Prepared'; Build='Built'; Sign='Signed'; Backup='BackedUp'
        Stage='Staged'; Arm='Armed'; Collect='Collected'; Disarm='Disarmed'
        Rollback='Disarmed'; FinalVerify='Complete'
    }
    if ($Result.Succeeded -or $Action -eq 'Collect') { $State.Phase = $NextPhase[$Action] }
    $State.LastAction = [pscustomobject]@{ Action=$Action; Status=$Result.Status; Succeeded=[bool]$Result.Succeeded; Directory=$ActionDirectory }
    if ($Result.Succeeded -or $Action -eq 'Collect') { $State.InProgress = $null }
    $State.UpdatedUtc = [DateTime]::UtcNow.ToString('o')
    if (-not $Result.Succeeded) { $State.LastFailure = $Result.Status }
    Save-GpuJournal $State
    Copy-GpuRecoveryJournal $State
    $Receipt = [pscustomobject]@{
        Action=$Action; Status=$Result.Status; Succeeded=[bool]$Result.Succeeded
        RunDirectory=$RunDirectory; JournalPath=(Join-Path $RunDirectory 'journal.json')
        Phase=$State.Phase; ActionDirectory=$ActionDirectory
        Details=$Result; AutomaticReboot=$false; NativeGpuIsolationAttested=$false
    }
    $ResetAlreadyObserved = ($null -ne $Result.PSObject.Properties['ResetBoundaryPassed'] -and [bool]$Result.ResetBoundaryPassed)
    if ($Action -eq 'Rollback' -and $State.EverArmIntent -and -not $ResetAlreadyObserved) {
        $Receipt.Status = 'RECOVERY_PENDING_USER_POWER_CYCLE'
    }
    Write-GpuJson -Path (Join-Path $ActionDirectory 'result.json') -Value $Receipt -NoOverwrite
    $Receipt | ConvertTo-Json -Depth 16
    if (-not $Result.Succeeded) { exit 1 }
} catch {
    $Failure = $_.Exception.Message
    if ($null -ne $State -and $null -ne $ActionDirectory) {
        if (-not $EntryJournalCheckpointPending) {
            $State.LastFailure = $Failure
            $State.UpdatedUtc = [DateTime]::UtcNow.ToString('o')
            try { Save-GpuJournal $State } catch { Write-Warning "Journal save failed: $($_.Exception.Message)" }
        }
        try {
            Write-GpuJson -Path (Join-Path $ActionDirectory 'failure.json') -NoOverwrite -Value ([ordered]@{
                Action=$Action; Failure=$Failure; RunDirectory=$RunDirectory
                InProgress=$State.InProgress; FailedUtc=[DateTime]::UtcNow.ToString('o')
            })
        } catch { Write-Warning "Failure receipt save failed: $($_.Exception.Message)" }
    }
    throw "GPU action $Action failed: $Failure. Preserve the selector/journal; use Status or rollback without retrying the interrupted action."
} finally {
    $Lock.Dispose()
}
