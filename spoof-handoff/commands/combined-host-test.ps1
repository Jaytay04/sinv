[CmdletBinding()]
param(
    [Parameter(Position=0)][ValidateSet('Prepare','EnableTestMode','InstallTransparent','VerifyTransparent','Remove','VerifyOriginal','InstallReplacement','VerifyReplacement','ArmCombined','CollectCombined','RestorePolicy','FinalVerify','Status')][string]$Action='Prepare',
    [string]$VmGatePath,
    [string]$SmbiosReaderPath,
    [string]$ReleaseDirectory,
    [string]$RecoveryDirectory='E:\CodexBackup\Recovery\hsinv-storage-test',
    [string]$RunDirectory,
    [string]$NicReaderPath,
    [string]$NicNativeReaderPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$locationRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$locationLeaf = [IO.Path]::GetFileName($locationRoot.TrimEnd('\'))
$parentDirectory = Split-Path -Parent $locationRoot
$parentLeaf = [IO.Path]::GetFileName($parentDirectory.TrimEnd('\'))
$codexBuildDirectory = Split-Path -Parent $parentDirectory
$codexBuildLeaf = [IO.Path]::GetFileName($codexBuildDirectory.TrimEnd('\'))
if ($locationLeaf -ieq 'smbios-boot-serial-probe' -and $parentLeaf -ieq 'worktrees' -and $codexBuildLeaf -ieq '.codex-build') {
    $worktreeRoot = $locationRoot
    $repositoryRoot = Split-Path -Parent $codexBuildDirectory
}
else {
    $repositoryRoot = $locationRoot
    $worktreeRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot '.codex-build\worktrees\smbios-boot-serial-probe'))
}
$implementation = Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\run.ps1'
if (-not (Test-Path -LiteralPath $implementation -PathType Leaf)) { throw "Combined host orchestration implementation is missing: $implementation" }
Import-Module (Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\step-support.psm1') -Force -DisableNameChecking -ErrorAction Stop
$defaults = Get-CombinedStepDefaults -RepositoryRoot $repositoryRoot -WorktreeRoot $worktreeRoot
if ($Action -cne 'Status') {
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Open elevated Windows PowerShell before running this combined host step.' }
}
$lock = Enter-CombinedStepLock -Path $defaults.LockPath
try {
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$implementation,'-Action',$Action,'-RepositoryRoot',$repositoryRoot,'-WorktreeRoot',$worktreeRoot)
    if ($Action -ceq 'Prepare') {
        if ($RunDirectory) { throw 'Prepare creates a new run; do not provide -RunDirectory.' }
        if (Test-Path -LiteralPath $defaults.PrepareAttemptPath) { throw "A previous Prepare attempt is unresolved: $($defaults.PrepareAttemptPath). Preserve it and its command evidence; do not retry Prepare or delete the guard. Review the exact combined/storage journals first." }
        if (Test-Path -LiteralPath $defaults.PreparedPath) { throw "A saved Prepare already exists: $($defaults.PreparedPath). Use Status or rollback for that run; do not overwrite its recovery record." }
        if (-not (Confirm-SecureBootUEFI)) { throw 'Secure Boot must be enabled for Prepare. Do not change firmware settings before the snapshot.' }
        if (-not $VmGatePath) { $VmGatePath = $defaults.VmGatePath }
        if (-not $ReleaseDirectory) { $ReleaseDirectory = $defaults.ReleaseDirectory }
        if (-not $SmbiosReaderPath) { $SmbiosReaderPath = $defaults.SmbiosReaderPath }
        if (-not $NicReaderPath) { $NicReaderPath = $defaults.NicReaderPath }
        if (-not $NicNativeReaderPath) { $NicNativeReaderPath = $defaults.NicNativeReaderPath }
        Assert-CombinedStepFileHash -Path $VmGatePath -Expected $defaults.VmGateSha256
        Assert-CombinedStepFileHash -Path (Join-Path $ReleaseDirectory 'manifest.json') -Expected $defaults.ReleaseManifestSha256
        $null = Assert-CombinedStepReadbackPaths -WorktreeRoot $worktreeRoot
        foreach ($path in @($SmbiosReaderPath,$NicReaderPath,$NicNativeReaderPath)) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required reader is missing: $path" }
        }
        foreach ($pair in @(@('VmGatePath',$VmGatePath),@('ReleaseDirectory',$ReleaseDirectory),@('SmbiosReaderPath',$SmbiosReaderPath),@('NicReaderPath',$NicReaderPath),@('NicNativeReaderPath',$NicNativeReaderPath),@('RecoveryDirectory',$RecoveryDirectory))) {
            $arguments += @(('-'+$pair[0]),[IO.Path]::GetFullPath($pair[1]))
        }
        $attempt = New-CombinedStepPrepareAttempt -Path $defaults.PrepareAttemptPath -RunRoot $defaults.RunRoot -EvidenceRoot $defaults.EvidenceRoot
    } else {
        foreach ($name in @('VmGatePath','SmbiosReaderPath','ReleaseDirectory','RecoveryDirectory','NicReaderPath','NicNativeReaderPath')) {
            if ($PSBoundParameters.ContainsKey($name)) { throw "-$name is a Prepare-only option. Later actions use the recorded run." }
        }
        if (-not $RunDirectory) { $RunDirectory = (Read-CombinedStepPrepared -Path $defaults.PreparedPath -RunRoot $defaults.RunRoot).runDirectory }
        $RunDirectory = Assert-CombinedStepRunDirectory -RunDirectory $RunDirectory -RunRoot $defaults.RunRoot
        $arguments += @('-RunDirectory',$RunDirectory)
    }
    $child = Invoke-CombinedStepChild -Program (Join-Path $PSHOME 'powershell.exe') -Arguments $arguments -EvidenceRoot $defaults.EvidenceRoot -Action $Action
    if ($Action -ceq 'Prepare') {
        try { Save-CombinedStepPrepared -Path $defaults.PreparedPath -Json $child.stdout.Trim() -RunRoot $defaults.RunRoot }
        catch { throw "Prepare returned but its selector record could not be saved. Preserve the returned JSON and use its exact -RunDirectory for Status or rollback. Do not repeat Prepare. Evidence: $($child.evidenceDirectory)`nReturned JSON:`n$($child.stdout)`nExact error: $($_.Exception.Message)" }
        Complete-CombinedStepPrepareAttempt -Path $defaults.PrepareAttemptPath -AttemptId $attempt.id
    }
    if ($child.stderr) { [Console]::Error.Write($child.stderr) }
    Write-Output $child.stdout.TrimEnd()
} finally { $lock.Dispose() }
