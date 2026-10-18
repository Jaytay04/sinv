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

$loaded=Get-CombinedHostJournal -Repository $repository -Worktree $worktree -RunDirectory $RunDirectory
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

function Get-CombinedRecoveryStoragePhase {
    param([Parameter(Mandatory=$true)][object]$StorageResult,[Parameter(Mandatory=$true)][string]$Action)
    $phase=[string](Get-CombinedValue $StorageResult 'phase')
    if ([string]::IsNullOrWhiteSpace($phase)) { throw "Storage $Action status did not return an authoritative phase." }
    return $phase
}

function Get-CombinedRecoveryLifecyclePhase {
    param([Parameter(Mandatory=$true)][string]$StoragePhase)
    $known=@(
        'Prepared','EnablingTestMode','PolicyPendingReboot',
        'TransparentInstallInProgress','TransparentInstallPendingReboot','TransparentVerified',
        'TransparentRemovalInProgress','TransparentRemovalPendingReboot','TransparentRemoved',
        'ReplacementInstallInProgress','ReplacementInstallPendingReboot','ReplacementVerified',
        'ReplacementRemovalInProgress','ReplacementRemovalPendingReboot','ReplacementRemoved',
        'RestoringPolicy','PolicyRestorePendingReboot','RecoveryReconciled','Complete'
    )
    if ($StoragePhase -cnotin $known) { throw "Storage recovery returned an unsupported phase '$StoragePhase'; preserve the journal for review." }
    return $StoragePhase
}

function Test-CombinedSimultaneousReadbackEvidence {
    param([Parameter(Mandatory=$true)][object]$Journal)
    foreach ($name in @('pathBArmEvidence','pathBCollectEvidence','pathBStageRestore','smbiosBootProof','nicReadback','nicTransaction')) {
        if ($null -eq (Get-CombinedValue $Journal $name)) { return $false }
    }
    $nicTransaction=Get-CombinedValue $Journal 'nicTransaction'
    return [bool](Get-CombinedValue $nicTransaction 'restorationComplete')
}

function Test-CombinedFinalVerificationEvidence {
    param([Parameter(Mandatory=$true)][object]$Journal,[Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$Worktree)
    try {
        $storage=Get-CombinedValue (Get-CombinedValue $Journal 'finalStorageVerification') 'result'
        $trust=Get-CombinedValue $storage 'trustStores'
        if ([string](Get-CombinedValue $storage 'status') -cne 'FINAL_VERIFY_PASS' -or (Get-CombinedValue $storage 'secureBoot') -ne $true -or (Get-CombinedValue $storage 'testSigning') -ne $false -or (Get-CombinedValue $storage 'filterPresent') -ne $false -or (Get-CombinedValue $trust 'Root') -ne $false -or (Get-CombinedValue $trust 'TrustedPublisher') -ne $false) { return $false }

        $readback=Get-CombinedValue $Journal 'finalReadback'
        if ([string](Get-CombinedValue $readback 'status') -cne 'PASS' -or [string](Get-CombinedValue $readback 'expectedMode') -cne 'Original' -or [int](Get-CombinedValue $readback 'bindingCount') -ne 12) { return $false }
        $readbackPath=[IO.Path]::GetFullPath([string](Get-CombinedValue $readback 'reportPath'))
        $readbackHash=[string](Get-CombinedValue $readback 'sha256')
        $reportRoots=@((Join-Path $Repository 'spoof-handoff\reports'),(Join-Path $Worktree 'spoof-handoff\reports')) | ForEach-Object { [IO.Path]::GetFullPath($_).TrimEnd('\')+[IO.Path]::DirectorySeparatorChar }
        if ($readbackHash -notmatch '\A[A-Fa-f0-9]{64}\z' -or -not (@($reportRoots | Where-Object { $readbackPath.StartsWith($_,[StringComparison]::OrdinalIgnoreCase) }).Count -eq 1) -or -not (Test-Path -LiteralPath $readbackPath -PathType Leaf) -or (Get-CombinedHostSha256 $readbackPath) -cne $readbackHash.ToUpperInvariant()) { return $false }
        $readbackReport=Get-Content -LiteralPath $readbackPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        if ([string](Get-CombinedValue $readbackReport 'schema') -cne 'combined-host-readback.v1' -or [string](Get-CombinedValue $readbackReport 'status') -cne 'PASS' -or [string](Get-CombinedValue $readbackReport 'expectedMode') -cne 'Original' -or (Get-CombinedValue $readbackReport 'simultaneousReadbackPass') -ne $true -or (Get-CombinedValue $readbackReport 'stableAcrossPolls') -ne $true -or @((Get-CombinedValue $readbackReport 'polls')).Count -ne 2) { return $false }

        $bcd=Get-CombinedValue $Journal 'finalBcdSnapshot'
        $baselineBcd=Get-CombinedValue $Journal 'bcdSnapshot'
        foreach ($name in @('current','all','firmware','exportedAll')) {
            $capture=Get-CombinedValue $bcd $name
            if ((Get-CombinedValue $capture 'exitCode') -ne 0 -or [string]::IsNullOrWhiteSpace([string](Get-CombinedValue $capture 'stdout'))) { return $false }
            if ($name -ne 'exportedAll' -and [string](Get-CombinedValue $capture 'stdout') -cne [string](Get-CombinedValue (Get-CombinedValue $baselineBcd $name) 'stdout')) { return $false }
        }
        $export=Get-CombinedValue $bcd 'export'
        $exportPath=[IO.Path]::GetFullPath([string](Get-CombinedValue $export 'path'))
        $exportHash=[string](Get-CombinedValue $export 'sha256')
        if ($exportHash -notmatch '\A[A-Fa-f0-9]{64}\z' -or -not (@($reportRoots | Where-Object { $exportPath.StartsWith($_,[StringComparison]::OrdinalIgnoreCase) }).Count -eq 1) -or -not (Test-Path -LiteralPath $exportPath -PathType Leaf) -or (Get-CombinedHostSha256 $exportPath) -cne $exportHash.ToUpperInvariant()) { return $false }

        $esp=Get-CombinedValue $Journal 'finalEspSnapshot'
        if (@(Get-CombinedValue $esp 'partitions').Count -eq 0 -or (Get-CombinedValue (Get-CombinedValue $Journal 'finalEspComparison') 'match') -ne $true -or (Get-CombinedValue $Journal 'finalFirmwareUnchanged') -ne $true) { return $false }
        return $true
    } catch {
        return $false
    }
}

try {
    $journal.operationInProgress=$true
    Set-CombinedHostField -Object $journal -Name 'recoveryUpdatedUtc' -Value ([DateTime]::UtcNow.ToString('o'))
    $null=Save-CombinedHostJournal $journal

    $nicPath=$null
    if ($null -ne $journal.nicTransaction) { $nicPath=[string]$journal.nicTransaction.path }
    if ($nicPath -and (Test-Path -LiteralPath $nicPath -PathType Leaf)) {
        $nicJournal=Get-Content -LiteralPath $nicPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        if ([string]$nicJournal.schema -cne 'combined-host-nic-transaction.v1') { throw "Unsupported NIC recovery journal schema: $([string]$nicJournal.schema)" }
        if ([bool]$nicJournal.mutationStarted -or -not [bool]$nicJournal.restorationComplete) {
            $null=Import-CombinedNicHelperBundle -RepositoryRoot $repository
            $restore=Restore-CombinedNicTransaction -Journal $nicJournal
            Set-CombinedHostField -Object $journal.nicTransaction -Name 'restorationComplete' -Value $true
            Set-CombinedHostField -Object $journal.nicTransaction -Name 'sha256' -Value (Get-CombinedHostSha256 $nicPath)
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
            Set-CombinedHostField -Object $journal -Name 'pathBStageRestore' -Value $stageRestore
            Add-RecoveryStep 'Path-B stage restore' 'PASS' $stageRestore
        } else {
            $recoveryEspDirectory=Join-Path $journal.runDirectory ('recovery-esp-check.'+[Guid]::NewGuid().ToString('N'))
            [void][IO.Directory]::CreateDirectory($recoveryEspDirectory)
            $recoveryEsp=Get-CombinedEspSnapshot -EvidenceDirectory $recoveryEspDirectory -BcdSnapshot $journal.bcdSnapshot
            $recoveryEspComparison=Assert-CombinedEspMatchesBaseline -Baseline $journal.espSnapshot -Current $recoveryEsp
            if ((Get-CombinedValue $recoveryEspComparison 'match') -ne $true) { throw 'Path-B has no stage receipt and the current ESP comparison did not prove the recorded baseline.' }
            Set-CombinedHostField -Object $journal -Name 'recoveryEspSnapshot' -Value $recoveryEsp
            Set-CombinedHostField -Object $journal -Name 'recoveryEspComparison' -Value $recoveryEspComparison
            Add-RecoveryStep 'Path-B no-receipt ESP baseline check' 'PASS' ([ordered]@{snapshotDirectory=$recoveryEspDirectory;comparison=$recoveryEspComparison})
        }
    }

    $storageStatus=Invoke-CombinedStorageAction -Journal $journal -Action 'Status'
    $storagePhase=Get-CombinedRecoveryStoragePhase -StorageResult $storageStatus.result -Action 'Status'
    switch ($storagePhase) {
        {$_ -in @('TransparentInstallPendingReboot','TransparentInstallInProgress','TransparentVerified','TransparentRemovalInProgress','ReplacementInstallPendingReboot','ReplacementInstallInProgress','ReplacementVerified','ReplacementRemovalInProgress')} {
            $remove=Invoke-CombinedStorageAction -Journal $journal -Action 'Remove'
            $journal.lastStorageAction=$remove
            $storageStatus=Invoke-CombinedStorageAction -Journal $journal -Action 'Status'
            $storagePhase=Get-CombinedRecoveryStoragePhase -StorageResult $storageStatus.result -Action 'Remove follow-up'
            if ($storagePhase -notin @('TransparentRemovalPendingReboot','ReplacementRemovalPendingReboot')) { throw "Storage Remove returned status '$([string](Get-CombinedValue $remove.result 'status'))' but follow-up Status reported unexpected phase '$storagePhase'." }
            $pending.Add('Reboot Windows with Secure Boot still OFF and test-signing still ON, then run combined-host-rollback.ps1 again to verify original disk state.')
            Add-RecoveryStep 'Storage package removal' 'PENDING_REBOOT' $remove.result
            break
        }
        {$_ -in @('TransparentRemovalPendingReboot','ReplacementRemovalPendingReboot')} {
            $verify=Invoke-CombinedStorageAction -Journal $journal -Action 'VerifyOriginal'
            if ([string](Get-CombinedValue $verify.result 'status') -cne 'ORIGINAL_VERIFIED') { throw "Storage VerifyOriginal did not report ORIGINAL_VERIFIED; actual='$([string](Get-CombinedValue $verify.result 'status'))'." }
            $journal.lastStorageAction=$verify
            $storageStatus=Invoke-CombinedStorageAction -Journal $journal -Action 'Status'
            $storagePhase=Get-CombinedRecoveryStoragePhase -StorageResult $storageStatus.result -Action 'VerifyOriginal follow-up'
            if ($storagePhase -notin @('TransparentRemoved','ReplacementRemoved')) { throw "Storage VerifyOriginal reported ORIGINAL_VERIFIED but follow-up Status reported unexpected phase '$storagePhase'." }
            Add-RecoveryStep 'Storage original readback' 'PASS' $verify.result
            break
        }
        default { }
    }

    if ($storagePhase -in @('TransparentRemovalPendingReboot','ReplacementRemovalPendingReboot')) {
        if ($steps.Count -eq 0 -or $steps[$steps.Count-1].name -ne 'Storage package removal') { $pending.Add('Reboot Windows with Secure Boot still OFF and test-signing still ON, then run combined-host-rollback.ps1 again to verify original disk state.') }
    }
    elseif ($storagePhase -in @('Prepared','EnablingTestMode','PolicyPendingReboot','TransparentRemoved','ReplacementRemoved','RecoveryReconciled','RestoringPolicy')) {
        $policy=Invoke-CombinedStorageAction -Journal $journal -Action 'RestorePolicy'
        $policyStatus=[string](Get-CombinedValue $policy.result 'status')
        if ($policyStatus -notin @('POLICY_RESTORED_PENDING_REBOOT','POLICY_ALREADY_BASELINE')) { throw "Storage RestorePolicy returned unexpected status '$policyStatus'." }
        $journal.lastStorageAction=$policy
        $storageStatus=Invoke-CombinedStorageAction -Journal $journal -Action 'Status'
        $storagePhase=Get-CombinedRecoveryStoragePhase -StorageResult $storageStatus.result -Action 'RestorePolicy follow-up'
        if ($storagePhase -cne 'PolicyRestorePendingReboot') { throw "Storage RestorePolicy returned '$policyStatus' but follow-up Status reported unexpected phase '$storagePhase'." }
        if ($policy.result.rebootRequired -or $policy.result.secureBootRestoreRequired) {
            $pending.Add('In firmware, enable Secure Boot at Settings > Security > Secure Boot > Enabled; save, boot Windows, then run combined-host-rollback.ps1 again for final verification.')
        }
        Add-RecoveryStep 'Policy restore' $policyStatus $policy.result
    }
    elseif ($storagePhase -ceq 'PolicyRestorePendingReboot') {
        $pending.Add('Enable Secure Boot in firmware at Settings > Security > Secure Boot > Enabled, save and boot Windows, then run combined-host-rollback.ps1 again for final verification.')
        Add-RecoveryStep 'Policy restore' 'PENDING_NORMAL_BOOT' $storageStatus.result
    }
    elseif ($storagePhase -ceq 'Complete') { Add-RecoveryStep 'Storage baseline verification' 'PASS' $storageStatus.result }

    $updatedStorage=Invoke-CombinedStorageAction -Journal $journal -Action 'Status'
    $storagePhase=Get-CombinedRecoveryStoragePhase -StorageResult $updatedStorage.result -Action 'final status'
    Set-CombinedHostField -Object $journal -Name 'storagePhase' -Value $storagePhase
    $simultaneousEvidence=Test-CombinedSimultaneousReadbackEvidence -Journal $journal
    if ($storagePhase -ceq 'Complete' -and -not $simultaneousEvidence) {
        $journal.phase='AbortedRecovered'
        $journal.operationInProgress=$false
        Set-CombinedHostField -Object $journal -Name 'recoveryUpdatedUtc' -Value ([DateTime]::UtcNow.ToString('o'))
        $null=Save-CombinedHostJournal $journal
        Add-RecoveryStep 'Combined simultaneous proof' 'NOT_PERFORMED_NO_AUTHENTICATED_ACTIVE_READBACK' ([ordered]@{phase=$journal.phase;reason='Recovery reached the storage baseline without complete Path-B arm/collect, SMBIOS-seven, NIC readback, and NIC restoration evidence.'})
    } elseif ($storagePhase -ceq 'Complete' -and $simultaneousEvidence -and -not (Test-CombinedFinalVerificationEvidence -Journal $journal -Repository $repository -Worktree $worktree)) {
        throw 'Storage reports Complete, but the combined journal lacks its final readback/BCD/ESP/firmware evidence. The combined final proof cannot be reconstructed from storage status alone.'
    } else {
        $journal.phase=Get-CombinedRecoveryLifecyclePhase -StoragePhase $storagePhase
        $journal.operationInProgress=$false
        Set-CombinedHostField -Object $journal -Name 'recoveryUpdatedUtc' -Value ([DateTime]::UtcNow.ToString('o'))
        $null=Save-CombinedHostJournal $journal
    }

    $secureBoot=$null
    try { $secureBoot=[bool](Confirm-SecureBootUEFI) } catch { $pending.Add('Secure Boot state could not be verified; exact error: '+$_.Exception.ToString()) }
    if ($storagePhase -ceq 'Complete' -and $secureBoot -ne $true) {
        $journal.phase='RecoveryPendingSecureBoot'
        $journal.operationInProgress=$false
        Set-CombinedHostField -Object $journal -Name 'recoveryUpdatedUtc' -Value ([DateTime]::UtcNow.ToString('o'))
        $null=Save-CombinedHostJournal $journal
        if ($secureBoot -eq $false) { $pending.Add('Enable Secure Boot in firmware, boot Windows, and rerun combined-host-rollback.ps1 to verify the restored state.') }
    }
    if ($storagePhase -ceq 'PolicyRestorePendingReboot' -and $secureBoot -eq $true -and $simultaneousEvidence) {
        $final=Invoke-CombinedLifecycleAction -Repository $repository -Worktree $worktree -RunDirectory $journal.runDirectory -Action 'FinalVerify'
        if ([string](Get-CombinedValue $final.result 'status') -cne 'FINAL_VERIFY_PASS') { throw "Combined FinalVerify returned unexpected status '$([string](Get-CombinedValue $final.result 'status'))'." }
        $finalJournal=Get-CombinedHostJournal -Repository $repository -Worktree $worktree -RunDirectory $journal.runDirectory
        if ([string](Get-CombinedValue $finalJournal.journal 'phase') -cne 'Complete' -or -not (Test-CombinedFinalVerificationEvidence -Journal $finalJournal.journal -Repository $repository -Worktree $worktree)) { throw 'Combined FinalVerify returned success without a persisted Complete phase and valid final readback, BCD, ESP, firmware, and storage evidence.' }
        $journal=$finalJournal.journal
        $storagePhase='Complete'
        Add-RecoveryStep 'Final baseline verification' 'FINAL_VERIFY_PASS' $final.result
        $pending.Clear()
    } elseif ($storagePhase -ceq 'PolicyRestorePendingReboot' -and $secureBoot -eq $true -and -not $simultaneousEvidence) {
        $storageFinal=Invoke-CombinedStorageAction -Journal $journal -Action 'FinalVerify'
        if ([string](Get-CombinedValue $storageFinal.result 'status') -cne 'FINAL_VERIFY_PASS') { throw "Storage-only FinalVerify returned unexpected status '$([string](Get-CombinedValue $storageFinal.result 'status'))'." }
        $updatedStorage=Invoke-CombinedStorageAction -Journal $journal -Action 'Status'
        $storagePhase=Get-CombinedRecoveryStoragePhase -StorageResult $updatedStorage.result -Action 'storage final verification'
        if ($storagePhase -cne 'Complete') { throw "Storage-only FinalVerify returned success but follow-up Status reported '$storagePhase'." }
        Set-CombinedHostField -Object $journal -Name 'storagePhase' -Value $storagePhase
        $journal.phase='AbortedRecovered'
        $journal.operationInProgress=$false
        Set-CombinedHostField -Object $journal -Name 'recoveryUpdatedUtc' -Value ([DateTime]::UtcNow.ToString('o'))
        $null=Save-CombinedHostJournal $journal
        Add-RecoveryStep 'Storage baseline verification' 'FINAL_VERIFY_PASS' $storageFinal.result
        Add-RecoveryStep 'Combined simultaneous proof' 'NOT_PERFORMED_NO_AUTHENTICATED_ACTIVE_READBACK' ([ordered]@{phase=$journal.phase;reason='The disk baseline is verified, but this run has no complete Path-B and all-reader active proof.'})
        $pending.Clear()
    } elseif ($storagePhase -ceq 'Complete' -and $simultaneousEvidence -and [string]$journal.phase -ceq 'Complete' -and (Test-CombinedFinalVerificationEvidence -Journal $journal -Repository $repository -Worktree $worktree)) {
        Add-RecoveryStep 'Final baseline verification' 'ALREADY_COMPLETE_WITH_PERSISTED_EVIDENCE' ([ordered]@{secureBoot=$secureBoot;phase=$journal.phase})
    } elseif ($storagePhase -ceq 'Complete' -and -not $simultaneousEvidence) {
        Add-RecoveryStep 'Storage baseline verification' 'PASS' $updatedStorage.result
        Add-RecoveryStep 'Combined simultaneous proof' 'NOT_PERFORMED_NO_AUTHENTICATED_ACTIVE_READBACK' ([ordered]@{phase=$journal.phase;reason='Storage baseline completion does not establish an all-fields simultaneous readback.'})
    }
}
catch {
    $failure=$_.Exception.ToString()+[Environment]::NewLine+$_.InvocationInfo.PositionMessage
    try {
        Set-CombinedHostField -Object $journal -Name 'lastRecoveryError' -Value $failure
        $journal.operationInProgress=$true
        Set-CombinedHostField -Object $journal -Name 'recoveryUpdatedUtc' -Value ([DateTime]::UtcNow.ToString('o'))
        $null=Save-CombinedHostJournal $journal
    } catch { }
    [Console]::Error.WriteLine("COMBINED RECOVERY INCOMPLETE: $failure")
    [Console]::Error.WriteLine("Preserve journal: $($loaded.path)")
    [Console]::Error.WriteLine('Do not continue the combined test until rollback verification succeeds.')
    exit 1
}

$status=if($pending.Count -gt 0){'RECOVERY_PENDING_USER_REBOOT_OR_FIRMWARE'}elseif([string]$journal.phase -ceq 'AbortedRecovered'){'ABORTED_RECOVERED_NO_SIMULTANEOUS_PROOF'}else{'RECOVERY_VERIFIED'}
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
