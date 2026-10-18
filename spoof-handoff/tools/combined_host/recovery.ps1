[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$WorktreeRoot,
    [Parameter(Mandatory=$true)][string]$RunDirectory
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0

$repository=[IO.Path]::GetFullPath($RepositoryRoot)
$worktree=[IO.Path]::GetFullPath($WorktreeRoot)
$recoveryRunDirectory=[IO.Path]::GetFullPath($RunDirectory)
$implementation=Join-Path $worktree 'spoof-handoff\tools\combined_host\run.ps1'
if (-not (Test-Path -LiteralPath $implementation -PathType Leaf)) { throw "Combined host recovery dependencies are missing: $implementation" }
. $implementation -Action Status -RepositoryRoot $repository -WorktreeRoot $worktree -LibraryOnly
$RunDirectory=$recoveryRunDirectory
. (Join-Path $PSScriptRoot 'nic-transaction.ps1')

$loaded=Get-CombinedHostJournal -Repository $repository -RunDirectory $RunDirectory
$journal=$loaded.journal
$steps=[Collections.Generic.List[object]]::new()
$pending=[Collections.Generic.List[string]]::new()

$hostMutationPerformed = [bool](Get-CombinedValue $journal 'hostMutationPerformed')
$operationInProgress = [bool](Get-CombinedValue $journal 'operationInProgress')
$journalPhase = [string](Get-CombinedValue $journal 'phase')
$readOnlyPreparation = -not $hostMutationPerformed -and -not $operationInProgress -and $journalPhase -in @('Prepared','VmGateRequired')
if ($readOnlyPreparation) {
    [pscustomobject][ordered]@{
        schema='combined-host-recovery.v1'
        status='NO_MUTATION_TO_ROLL_BACK'
        runDirectory=(Get-CombinedValue $journal 'runDirectory')
        journalPath=$loaded.path
        journalSha256=(Get-CombinedHostSha256 $loaded.path)
        steps=@()
        pendingUserSteps=@()
        hostMutationPerformed=$false
        automaticRecoveryClaim=$false
    } | ConvertTo-Json -Depth 12
    return
}

$storageSnapshot = Get-CombinedValue $journal 'storageSnapshot'
$storageRunDirectory = [string](Get-CombinedValue $storageSnapshot 'runDirectory')
if ($hostMutationPerformed -and [string]::IsNullOrWhiteSpace($storageRunDirectory)) {
    throw 'cannot roll back an armed or mutated combined run without its verified external storage recovery snapshot; preserve the journal and use the pre-existing full-image recovery path.'
}

function Add-RecoveryStep {
    param([string]$Name,[string]$Status,[AllowNull()][object]$Evidence)
    $steps.Add([pscustomobject][ordered]@{name=$Name;status=$Status;evidence=$Evidence})
}

try {
    $nicPath=$null
    if ($null -ne $journal.nicTransaction) { $nicPath=[string]$journal.nicTransaction.path }
    if ($nicPath -and (Test-Path -LiteralPath $nicPath -PathType Leaf)) {
        $nicJournal=Get-Content -LiteralPath $nicPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        if ([string]$nicJournal.schema -cne 'combined-host-nic-transaction.v1') { throw "Unsupported NIC recovery journal schema: $([string]$nicJournal.schema)" }
        if ([bool]$nicJournal.mutationStarted -or -not [bool]$nicJournal.restorationComplete) {
            $null=Import-CombinedNicHelperBundle -RepositoryRoot $repository
            $restore=Restore-CombinedNicTransaction -Journal $nicJournal
            $journal.nicTransaction.restorationComplete=$true
            $journal.nicTransaction.sha256=(Get-CombinedHostSha256 $nicPath)
            Add-RecoveryStep 'NIC address restore' 'PASS' ([ordered]@{journal=$nicPath;sha256=(Get-CombinedHostSha256 $nicPath);adapters=@($restore.adapterProgress).Count})
        } else { Add-RecoveryStep 'NIC address restore' 'ALREADY_RESTORED' ([ordered]@{journal=$nicPath;sha256=(Get-CombinedHostSha256 $nicPath)}) }
    }

    if ($null -ne $journal.pathBBuild) {
        $pathbReceipt=Join-Path ([string]$journal.pathBBuild.inventoryBuild) 'path-b-stage.json'
        if (Test-Path -LiteralPath $pathbReceipt -PathType Leaf) {
            if ([string]::IsNullOrWhiteSpace([string]$journal.pathBBackupPath)) {
                $stageLog=Join-Path $journal.runDirectory 'actions\pathb-stage.stdout.txt'
                if (Test-Path -LiteralPath $stageLog -PathType Leaf) {
                    $preserved=@([IO.File]::ReadAllLines($stageLog) | Where-Object { $_ -match '^PRESERVED previous stage at ' })
                    if ($preserved.Count -eq 1) {
                        $journal.pathBBackupPath=($preserved[0] -replace '^PRESERVED previous stage at ','').Trim()
                        $null=Save-CombinedHostJournal $journal
                    }
                }
            }
            $disarm=Invoke-CombinedPathB -Journal $journal -Mode 'Disarm'
            Add-RecoveryStep 'Path-B one-shot cleanup' 'PASS' $disarm.evidence
            $stageRestore=Restore-CombinedPathBStage -Journal $journal
            $journal.pathBStageRestore=$stageRestore
            Add-RecoveryStep 'Path-B stage restore' 'PASS' $stageRestore
        } else { Add-RecoveryStep 'Path-B stage restore' 'NO_STAGE_RECEIPT' ([ordered]@{expected=$pathbReceipt}) }
    }

    $storageStatus=Invoke-CombinedStorageAction -Journal $journal -Action 'Status'
    $storagePhase=[string]$storageStatus.result.phase
    switch ($storagePhase) {
        {$_ -in @('TransparentInstallPendingReboot','TransparentInstallInProgress','TransparentVerified','ReplacementInstallPendingReboot','ReplacementInstallInProgress','ReplacementVerified')} {
            $remove=Invoke-CombinedStorageAction -Journal $journal -Action 'Remove'
            $pending.Add('Reboot Windows with Secure Boot still OFF and test-signing still ON, then run combined-host-rollback.ps1 again to verify original disk state.')
            Add-RecoveryStep 'Storage package removal' 'PENDING_REBOOT' $remove.result
            $storagePhase=[string]$remove.result.phase
            break
        }
        {$_ -in @('TransparentRemovalPendingReboot','ReplacementRemovalPendingReboot')} {
            $verify=Invoke-CombinedStorageAction -Journal $journal -Action 'VerifyOriginal'
            $storagePhase=[string]$verify.result.phase
            Add-RecoveryStep 'Storage original readback' 'PASS' $verify.result
            break
        }
        default { }
    }

    if ($storagePhase -in @('TransparentRemovalPendingReboot','ReplacementRemovalPendingReboot')) {
        if ($steps.Count -eq 0 -or $steps[$steps.Count-1].name -ne 'Storage package removal') { $pending.Add('Reboot Windows with Secure Boot still OFF and test-signing still ON, then run combined-host-rollback.ps1 again to verify original disk state.') }
    }
    elseif ($storagePhase -in @('Prepared','PolicyPendingReboot','TransparentRemoved','ReplacementRemoved','RecoveryReconciled','RestoringPolicy')) {
        $policy=Invoke-CombinedStorageAction -Journal $journal -Action 'RestorePolicy'
        $storagePhase=[string]$policy.result.status
        if ($policy.result.rebootRequired -or $policy.result.secureBootRestoreRequired) {
            $pending.Add('In firmware, enable Secure Boot at Settings > Security > Secure Boot > Enabled; save, boot Windows, then run combined-host-rollback.ps1 again for final verification.')
        }
        Add-RecoveryStep 'Policy restore' ([string]$policy.result.status) $policy.result
    }
    elseif ($storagePhase -ceq 'PolicyRestorePendingReboot') {
        $pending.Add('Enable Secure Boot in firmware at Settings > Security > Secure Boot > Enabled, save and boot Windows, then run combined-host-rollback.ps1 again for final verification.')
        Add-RecoveryStep 'Policy restore' 'PENDING_NORMAL_BOOT' $storageStatus.result
    }
    elseif ($storagePhase -ceq 'Complete') { Add-RecoveryStep 'Storage baseline verification' 'PASS' $storageStatus.result }

    $updatedStorage=Invoke-CombinedStorageAction -Journal $journal -Action 'Status'
    $journal.storagePhase=[string]$updatedStorage.result.phase
    $journal.operationInProgress=$false
    $journal.recoveryUpdatedUtc=[DateTime]::UtcNow.ToString('o')
    $null=Save-CombinedHostJournal $journal

    $secureBoot=$null
    try { $secureBoot=[bool](Confirm-SecureBootUEFI) } catch { $pending.Add('Secure Boot state could not be verified; exact error: '+$_.Exception.ToString()) }
    if ($updatedStorage.result.phase -ceq 'PolicyRestorePendingReboot' -and $secureBoot -eq $true) {
        $final=Invoke-CombinedLifecycleAction -Repository $repository -Worktree $worktree -RunDirectory $journal.runDirectory -Action 'FinalVerify'
        Add-RecoveryStep 'Final baseline verification' ([string]$final.result.status) $final.result
        $pending.Clear()
    } elseif ($updatedStorage.result.phase -ceq 'Complete') {
        Add-RecoveryStep 'Final baseline verification' 'ALREADY_COMPLETE' ([ordered]@{secureBoot=$secureBoot})
    }
}
catch {
    $failure=$_.Exception.ToString()
    try {
        $journal.lastRecoveryError=$failure
        $journal.operationInProgress=$true
        $journal.recoveryUpdatedUtc=[DateTime]::UtcNow.ToString('o')
        $null=Save-CombinedHostJournal $journal
    } catch { }
    [Console]::Error.WriteLine("COMBINED RECOVERY INCOMPLETE: $failure")
    [Console]::Error.WriteLine("Preserve journal: $($loaded.path)")
    [Console]::Error.WriteLine('Do not continue the combined test until rollback verification succeeds.')
    exit 1
}

$status=if($pending.Count -gt 0){'RECOVERY_PENDING_USER_REBOOT_OR_FIRMWARE'}else{'RECOVERY_VERIFIED'}
[pscustomobject][ordered]@{
    schema='combined-host-recovery.v1'
    status=$status
    runDirectory=$journal.runDirectory
    journalPath=$loaded.path
    journalSha256=(Get-CombinedHostSha256 $loaded.path)
    steps=@($steps.ToArray())
    pendingUserSteps=@($pending.ToArray())
    secureBoot=$secureBoot
    rebootIssued=$false
    automaticRecoveryClaim=$false
} | ConvertTo-Json -Depth 24
