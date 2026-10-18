[CmdletBinding()]
param()

$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0

function Assert-RecoveryTest {
    param([bool]$Condition,[string]$Message)
    if (-not $Condition) { throw "Recovery test failed: $Message" }
}

function Write-RecoveryTestState {
    param([string]$Path,[object]$State)
    $json=ConvertTo-Json -InputObject $State -Depth 32
    [IO.File]::WriteAllText($Path,$json,[Text.UTF8Encoding]::new($false))
}

function New-RecoveryTestState {
    param([string]$StoragePhase,[bool]$WithSimultaneousEvidence,[bool]$SecureBoot,[string]$FailAction='')
    $journal=[ordered]@{
        schema='combined-host-preparation.v1'
        runDirectory='mock-combined-run'
        phase=if($StoragePhase -ceq 'ReplacementVerified'){'CombinedArmIntent'}else{$StoragePhase}
        hostMutationPerformed=$true
        operationInProgress=($StoragePhase -ceq 'ReplacementVerified')
        lastStorageAction=$null
        storageSnapshot=[ordered]@{runDirectory='mock-storage-run'}
        storageRelease=[ordered]@{directory='mock-release'}
        bcdSnapshot=[ordered]@{current=[ordered]@{exitCode=0;stdout='baseline-current'};all=[ordered]@{exitCode=0;stdout='baseline-all'};firmware=[ordered]@{exitCode=0;stdout='baseline-firmware'}}
        espSnapshot=[ordered]@{partitions=@([ordered]@{diskNumber=0;partitionNumber=1;files=@()})}
        nicTransaction=$null
        pathBBuild=$null
        pathBArmEvidence=$null
        pathBCollectEvidence=$null
        pathBStageRestore=$null
        smbiosBootProof=$null
        nicReadback=$null
        finalStorageVerification=$null
        finalReadback=$null
        finalBcdSnapshot=$null
        finalEspSnapshot=$null
        finalFirmwareUnchanged=$null
        finalEspComparison=$null
    }
    if ($WithSimultaneousEvidence) {
        $journal.pathBArmEvidence=[ordered]@{status='ARMED'}
        $journal.pathBCollectEvidence=[ordered]@{status='COLLECTED'}
        $journal.pathBStageRestore=[ordered]@{status='RESTORED'}
        $journal.smbiosBootProof=[ordered]@{status='PASS'}
        $journal.nicReadback=[ordered]@{status='PASS'}
        $journal.nicTransaction=[ordered]@{path='mock-nic-transaction.json';restorationComplete=$true}
    }
    return [ordered]@{
        journal=$journal
        storagePhase=$StoragePhase
        secureBoot=$SecureBoot
        currentEsp=[ordered]@{partitions=@([ordered]@{diskNumber=0;partitionNumber=1;files=@()})}
        espMismatch=$false
        espReadFails=$false
        failAction=$FailAction
        omitStatusPhase=$false
        failMessage='mocked action failure'
        actions=@()
        lifecycleActions=@()
    }
}

function ConvertTo-QuotedProcessArgument {
    param([string]$Value)
    return '"'+$Value.Replace('"','\"')+'"'
}

function Invoke-RecoveryScenario {
    param([string]$Root,[string]$Name,[object]$InitialState)
    $scenarioDirectory=Join-Path $Root $Name
    [void][IO.Directory]::CreateDirectory($scenarioDirectory)
    $worktree=Join-Path $scenarioDirectory 'worktree'
    $hostTools=Join-Path $worktree 'spoof-handoff\tools\combined_host'
    [void][IO.Directory]::CreateDirectory($hostTools)
    $runDirectory=Join-Path $worktree 'spoof-handoff\reports\combined-host-test\run'
    [void][IO.Directory]::CreateDirectory($runDirectory)
    $InitialState.journal.runDirectory=$runDirectory
    if ($InitialState.journal.pathBBuild -and [string]$InitialState.journal.pathBBuild.inventoryBuild -ceq 'mock-missing-stage-receipt') {
        $InitialState.journal.pathBBuild.inventoryBuild=Join-Path $worktree 'missing-stage-receipt'
    }
    $statePath=Join-Path $scenarioDirectory 'state.json'
    $statePathInChild=Join-Path $scenarioDirectory 'state-child.json'
    $stdoutPath=Join-Path $scenarioDirectory 'stdout.txt'
    $stderrPath=Join-Path $scenarioDirectory 'stderr.txt'
    Write-RecoveryTestState -Path $statePath -State $InitialState
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'recovery.ps1') -Destination (Join-Path $hostTools 'recovery.ps1') -Force
    [IO.File]::WriteAllText((Join-Path $hostTools 'nic-transaction.ps1'),'',[Text.UTF8Encoding]::new($false))

    $mockRunner=@'
param([string]$Action,[string]$RepositoryRoot,[string]$WorktreeRoot,[switch]$LibraryOnly)
Set-StrictMode -Version 2.0
function Get-RecoveryMockState { Get-Content -LiteralPath $env:COMBINED_RECOVERY_TEST_STATE -Raw | ConvertFrom-Json }
function Save-RecoveryMockState { param($State) [IO.File]::WriteAllText($env:COMBINED_RECOVERY_TEST_STATE,(ConvertTo-Json -InputObject $State -Depth 32),[Text.UTF8Encoding]::new($false)) }
function Get-CombinedValue { param($Object,[string]$Name) if($null -eq $Object){return $null}; if($Object -is [System.Collections.IDictionary]){if($Object.Contains($Name)){return $Object[$Name]};return $null};$property=$Object.PSObject.Properties[$Name];if($null -ne $property){return $property.Value};return $null }
function Set-CombinedHostField { param($Object,[string]$Name,$Value) if($Object -is [System.Collections.IDictionary]){$Object[$Name]=$Value;return};$property=$Object.PSObject.Properties[$Name];if($null -eq $property){$Object|Add-Member -MemberType NoteProperty -Name $Name -Value $Value -Force}else{$property.Value=$Value} }
function Get-CombinedHostJournal { param($Repository,$Worktree,$RunDirectory) $state=Get-RecoveryMockState; return [pscustomobject]@{journal=$state.journal;path=(Join-Path $RunDirectory 'journal.json')} }
function Get-CombinedHostSha256 { param([string]$Path) if(Test-Path -LiteralPath $Path -PathType Leaf){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()};return ('A'*64) }
function Get-CombinedEspSnapshot { param([string]$EvidenceDirectory,$BcdSnapshot) $state=Get-RecoveryMockState;if([bool]$state.espReadFails){throw 'mocked ESP snapshot read failure'};return $state.currentEsp }
function Assert-CombinedEspMatchesBaseline { param($Baseline,$Current) $state=Get-RecoveryMockState;if([bool]$state.espMismatch){return [ordered]@{match=$false;reason='mocked ESP baseline mismatch'}};return [ordered]@{match=$true} }
function Save-CombinedHostJournal { param($Journal) $state=Get-RecoveryMockState; $state.journal=$Journal; Save-RecoveryMockState $state; return (Join-Path $Journal.runDirectory 'journal.json') }
function Invoke-CombinedStorageAction { param($Journal,[string]$Action) $state=Get-RecoveryMockState; $state.actions=@($state.actions)+@($Action); Save-RecoveryMockState $state; if([string]$state.failAction -ceq $Action){throw ([string]$state.failMessage)}; switch($Action){ 'Status' { if([bool]$state.omitStatusPhase){$result=[ordered]@{status='STATUS'}}else{$result=[ordered]@{phase=[string]$state.storagePhase}} } 'Remove' { $state.storagePhase=if([string]$state.storagePhase -like 'Transparent*'){'TransparentRemovalPendingReboot'}else{'ReplacementRemovalPendingReboot'};Save-RecoveryMockState $state;$result=[ordered]@{status=if([string]$state.storagePhase -like 'Transparent*'){'TRANSPARENT_REMOVAL_PENDING_REBOOT'}else{'REPLACEMENT_REMOVAL_PENDING_REBOOT'};rebootIssued=$false} } 'VerifyOriginal' { $state.storagePhase=if([string]$state.storagePhase -like 'Transparent*'){'TransparentRemoved'}else{'ReplacementRemoved'};Save-RecoveryMockState $state;$result=[ordered]@{status='ORIGINAL_VERIFIED'} } 'RestorePolicy' { $state.storagePhase='PolicyRestorePendingReboot';Save-RecoveryMockState $state;$result=[ordered]@{status='POLICY_RESTORED_PENDING_REBOOT';rebootRequired=$true;secureBootRestoreRequired=$true} } 'FinalVerify' { $state.storagePhase='Complete';Save-RecoveryMockState $state;$result=[ordered]@{status='FINAL_VERIFY_PASS'} } default { throw "Unexpected storage action $Action" } };return [pscustomobject]@{result=$result;evidence=[ordered]@{action=$Action}} }
function Invoke-CombinedLifecycleAction { param($Repository,$Worktree,$RunDirectory,[string]$Action) $state=Get-RecoveryMockState;$state.lifecycleActions=@($state.lifecycleActions)+@($Action);Save-RecoveryMockState $state;if([string]$state.failAction -ceq $Action){throw ([string]$state.failMessage)};$reportPath=Join-Path $RunDirectory 'final-readback.json';$report=[ordered]@{schema='combined-host-readback.v1';status='PASS';expectedMode='Original';simultaneousReadbackPass=$true;stableAcrossPolls=$true;polls=@(@{poll=1},@{poll=2})};[IO.File]::WriteAllText($reportPath,(ConvertTo-Json -InputObject $report -Depth 8),[Text.UTF8Encoding]::new($false));$bcdExportPath=Join-Path $RunDirectory 'final-boot-store-export.bcd';[IO.File]::WriteAllBytes($bcdExportPath,[byte[]]@(1,2,3));$exportHash=(Get-FileHash -LiteralPath $bcdExportPath -Algorithm SHA256).Hash.ToUpperInvariant();$captureCurrent=[ordered]@{exitCode=0;stdout='baseline-current'};$captureAll=[ordered]@{exitCode=0;stdout='baseline-all'};$captureFirmware=[ordered]@{exitCode=0;stdout='baseline-firmware'};$captureExportedAll=[ordered]@{exitCode=0;stdout='exported-all'};$finalBcd=[ordered]@{current=$captureCurrent;all=$captureAll;firmware=$captureFirmware;exportedAll=$captureExportedAll;export=[ordered]@{path=$bcdExportPath;sha256=$exportHash}};Set-CombinedHostField $state.journal 'phase' 'Complete';Set-CombinedHostField $state.journal 'operationInProgress' $false;Set-CombinedHostField $state.journal 'finalStorageVerification' ([ordered]@{result=[ordered]@{status='FINAL_VERIFY_PASS';secureBoot=$true;testSigning=$false;filterPresent=$false;trustStores=[ordered]@{Root=$false;TrustedPublisher=$false}}});Set-CombinedHostField $state.journal 'finalReadback' ([ordered]@{status='PASS';expectedMode='Original';bindingCount=12;reportPath=$reportPath;sha256=(Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToUpperInvariant()});Set-CombinedHostField $state.journal 'finalBcdSnapshot' $finalBcd;Set-CombinedHostField $state.journal 'finalEspSnapshot' ([ordered]@{partitions=@([ordered]@{diskNumber=0;partitionNumber=1;files=@()})});Set-CombinedHostField $state.journal 'finalFirmwareUnchanged' $true;Set-CombinedHostField $state.journal 'finalEspComparison' ([ordered]@{match=$true});Save-RecoveryMockState $state;return [pscustomobject]@{result=[ordered]@{status='FINAL_VERIFY_PASS'}} }
function Confirm-SecureBootUEFI { $state=Get-RecoveryMockState; return [bool]$state.secureBoot }
'@
    [IO.File]::WriteAllText((Join-Path $hostTools 'run.ps1'),$mockRunner,[Text.UTF8Encoding]::new($false))

    $previousState=$env:COMBINED_RECOVERY_TEST_STATE
    $env:COMBINED_RECOVERY_TEST_STATE=$statePath
    try {
        $powershell=Join-Path $PSHOME 'powershell.exe'
        $scriptPath=Join-Path $hostTools 'recovery.ps1'
        $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$scriptPath,'-RepositoryRoot',$scenarioDirectory,'-WorktreeRoot',$worktree,'-RunDirectory',$runDirectory)
        $argumentLine=($arguments | ForEach-Object { ConvertTo-QuotedProcessArgument ([string]$_) }) -join ' '
        $process=Start-Process -FilePath $powershell -ArgumentList $argumentLine -PassThru -Wait -WindowStyle Hidden -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    } finally {
        if ($null -eq $previousState) { Remove-Item Env:\COMBINED_RECOVERY_TEST_STATE -ErrorAction SilentlyContinue } else { $env:COMBINED_RECOVERY_TEST_STATE=$previousState }
    }
    $state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    $stdout=if(Test-Path -LiteralPath $stdoutPath){Get-Content -LiteralPath $stdoutPath -Raw}else{''}
    $stderr=if(Test-Path -LiteralPath $stderrPath){Get-Content -LiteralPath $stderrPath -Raw}else{''}
    $result=$null
    if (-not [string]::IsNullOrWhiteSpace($stdout)) { try { $result=$stdout | ConvertFrom-Json -ErrorAction Stop } catch { } }
    return [pscustomobject]@{name=$Name;exitCode=[int]$process.ExitCode;state=$state;result=$result;stdout=$stdout;stderr=$stderr}
}

$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('.recovery-test.'+[Guid]::NewGuid().ToString('N'))))
$toolsRoot=[IO.Path]::GetFullPath($PSScriptRoot)
if (-not $root.StartsWith($toolsRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw "Recovery test root escaped its tools directory: $root" }
[void][IO.Directory]::CreateDirectory($root)
$results=[Collections.Generic.List[object]]::new()
try {
    $full=Invoke-RecoveryScenario -Root $root -Name 'full-round-trip' -InitialState (New-RecoveryTestState -StoragePhase 'ReplacementVerified' -WithSimultaneousEvidence $true -SecureBoot $false)
    Assert-RecoveryTest ($full.exitCode -eq 0) "active replacement removal should return pending reboot; exit=$($full.exitCode), stderr=$($full.stderr)"
    Assert-RecoveryTest ($full.result.status -ceq 'RECOVERY_PENDING_USER_REBOOT_OR_FIRMWARE') 'first pass should request the removal reboot'
    Assert-RecoveryTest ($full.state.storagePhase -ceq 'ReplacementRemovalPendingReboot' -and $full.state.journal.phase -ceq 'ReplacementRemovalPendingReboot' -and -not [bool]$full.state.journal.operationInProgress) 'Remove result without phase must be reconciled from Status and clear the completed recovery intent'
    Assert-RecoveryTest (@($full.state.actions) -contains 'Remove') 'first pass should invoke Remove'
    $results.Add([pscustomobject]@{scenario='active replacement removal';status='PASS';detail='Remove returned status-only JSON; follow-up Status supplied ReplacementRemovalPendingReboot.'})

    $full.state.secureBoot=$false
    Write-RecoveryTestState -Path (Join-Path $root 'full-round-trip\state.json') -State $full.state
    $full=Invoke-RecoveryScenario -Root $root -Name 'full-round-trip' -InitialState $full.state
    Assert-RecoveryTest ($full.exitCode -eq 0) "original verification and policy restore should request the normal reboot; exit=$($full.exitCode), stderr=$($full.stderr)"
    Assert-RecoveryTest (@($full.state.actions) -contains 'VerifyOriginal' -and @($full.state.actions) -contains 'RestorePolicy') 'second pass should verify originals and restore policy'
    Assert-RecoveryTest ($full.state.storagePhase -ceq 'PolicyRestorePendingReboot' -and $full.state.journal.phase -ceq 'PolicyRestorePendingReboot' -and -not [bool]$full.state.journal.operationInProgress) 'status-only VerifyOriginal and RestorePolicy results must reconcile to the authoritative policy phase'
    Assert-RecoveryTest ($full.result.status -ceq 'RECOVERY_PENDING_USER_REBOOT_OR_FIRMWARE') 'second pass should retain the required normal reboot/Secure Boot step'
    $results.Add([pscustomobject]@{scenario='original verification and policy restore';status='PASS';detail='VerifyOriginal/RestorePolicy statuses were followed by authoritative Status phase reads.'})

    $full.state.secureBoot=$true
    Write-RecoveryTestState -Path (Join-Path $root 'full-round-trip\state.json') -State $full.state
    $full=Invoke-RecoveryScenario -Root $root -Name 'full-round-trip' -InitialState $full.state
    Assert-RecoveryTest ($full.exitCode -eq 0) "proof-complete recovery should run combined FinalVerify; exit=$($full.exitCode), stderr=$($full.stderr)"
    Assert-RecoveryTest (@($full.state.lifecycleActions) -contains 'FinalVerify') 'complete active proof evidence should authorize the existing combined FinalVerify lifecycle'
    Assert-RecoveryTest ($full.result.status -ceq 'RECOVERY_VERIFIED' -and $full.state.journal.phase -ceq 'Complete' -and -not [bool]$full.state.journal.operationInProgress) 'combined FinalVerify must persist Complete with all final evidence references before recovery reports verified'
    $results.Add([pscustomobject]@{scenario='combined final proof';status='PASS';detail='Existing FinalVerify ran only with complete arm/collect/SMBIOS/NIC restoration evidence and persisted all final proof references.'})

    foreach ($invalidFinalField in @('finalFirmwareUnchanged','finalEspComparison')) {
        $invalidFinalState=$full.state
        $invalidFinalState.storagePhase='Complete'
        if ($invalidFinalField -ceq 'finalFirmwareUnchanged') { $invalidFinalState.journal.finalFirmwareUnchanged=$false } else { $invalidFinalState.journal.finalEspComparison.match=$false }
        $invalidFinal=Invoke-RecoveryScenario -Root $root -Name ('failure-invalid-'+$invalidFinalField.ToLowerInvariant()) -InitialState $invalidFinalState
        Assert-RecoveryTest ($invalidFinal.exitCode -ne 0 -and [bool]$invalidFinal.state.journal.operationInProgress) "persisted Complete with $invalidFinalField false must fail closed; exit=$($invalidFinal.exitCode), phase=$($invalidFinal.state.journal.phase), storage=$($invalidFinal.state.storagePhase), field=$($invalidFinal.state.journal.$invalidFinalField), inProgress=$($invalidFinal.state.journal.operationInProgress), stderr=$($invalidFinal.stderr)"
        $results.Add([pscustomobject]@{scenario=('invalid persisted '+$invalidFinalField);status='PASS';detail='A false final verification result cannot be promoted to RECOVERY_VERIFIED.'})
    }

    $missingFinalState=New-RecoveryTestState -StoragePhase 'Complete' -WithSimultaneousEvidence $true -SecureBoot $true
    $missingFinalState.journal.phase='Complete'
    $missingFinal=Invoke-RecoveryScenario -Root $root -Name 'failure-complete-missing-evidence' -InitialState $missingFinalState
    Assert-RecoveryTest ($missingFinal.exitCode -ne 0 -and [bool]$missingFinal.state.journal.operationInProgress -and [string]::IsNullOrWhiteSpace([string]$missingFinal.state.journal.finalReadback)) 'Complete storage and combined phase without persisted final evidence must fail closed'
    $results.Add([pscustomobject]@{scenario='Complete with missing final evidence';status='PASS';detail='A Complete marker without final proof references is never reported as verified.'})

    $aborted=Invoke-RecoveryScenario -Root $root -Name 'aborted-no-simultaneous-proof' -InitialState (New-RecoveryTestState -StoragePhase 'ReplacementVerified' -WithSimultaneousEvidence $false -SecureBoot $false)
    Assert-RecoveryTest ($aborted.exitCode -eq 0) 'aborted recovery should be handled without a combined proof'
    Assert-RecoveryTest (@($aborted.state.lifecycleActions).Count -eq 0) 'aborted recovery must not invoke combined FinalVerify'
    Assert-RecoveryTest ($aborted.result.status -ceq 'RECOVERY_PENDING_USER_REBOOT_OR_FIRMWARE') 'aborted recovery first pass should request removal reboot'
    $aborted.state.secureBoot=$false
    Write-RecoveryTestState -Path (Join-Path $root 'aborted-no-simultaneous-proof\state.json') -State $aborted.state
    $aborted=Invoke-RecoveryScenario -Root $root -Name 'aborted-no-simultaneous-proof' -InitialState $aborted.state
    Assert-RecoveryTest ($aborted.exitCode -eq 0 -and @($aborted.state.lifecycleActions).Count -eq 0) 'aborted recovery should restore policy without promoting the run to simultaneous proof'
    $aborted.state.secureBoot=$true
    Write-RecoveryTestState -Path (Join-Path $root 'aborted-no-simultaneous-proof\state.json') -State $aborted.state
    $aborted=Invoke-RecoveryScenario -Root $root -Name 'aborted-no-simultaneous-proof' -InitialState $aborted.state
    Assert-RecoveryTest ($aborted.exitCode -eq 0) "storage-only final verification should complete aborted cleanup; exit=$($aborted.exitCode), stderr=$($aborted.stderr)"
    Assert-RecoveryTest ($aborted.state.storagePhase -ceq 'Complete' -and $aborted.state.journal.phase -ceq 'AbortedRecovered' -and $aborted.result.status -ceq 'ABORTED_RECOVERED_NO_SIMULTANEOUS_PROOF') 'storage-only completion must record an aborted recovery without claiming a simultaneous proof'
    Assert-RecoveryTest (@($aborted.state.lifecycleActions).Count -eq 0) 'storage-only final verification must not call the combined proof lifecycle'
    $results.Add([pscustomobject]@{scenario='aborted before active readback';status='PASS';detail='Storage cleanup completed as AbortedRecovered; no combined FinalVerify or simultaneous-proof claim.'})

    foreach ($failureAction in @('Remove','VerifyOriginal','RestorePolicy','FinalVerify')) {
        $phase=switch($failureAction){'Remove'{'ReplacementVerified'}'VerifyOriginal'{'ReplacementRemovalPendingReboot'}'RestorePolicy'{'ReplacementRemoved'}'FinalVerify'{'PolicyRestorePendingReboot'}}
        $failureState=New-RecoveryTestState -StoragePhase $phase -WithSimultaneousEvidence ($failureAction -ceq 'FinalVerify') -SecureBoot ($failureAction -ceq 'FinalVerify') -FailAction $failureAction
        $failure=Invoke-RecoveryScenario -Root $root -Name ('failure-'+$failureAction.ToLowerInvariant()) -InitialState $failureState
        Assert-RecoveryTest ($failure.exitCode -ne 0) "$failureAction failure should exit nonzero"
        Assert-RecoveryTest ([bool]$failure.state.journal.operationInProgress -and -not [string]::IsNullOrWhiteSpace([string]$failure.state.journal.lastRecoveryError)) "$failureAction failure should preserve an in-progress recovery intent and exact error"
        Assert-RecoveryTest (@($failure.state.lifecycleActions).Count -eq 0 -or $failureAction -ceq 'FinalVerify') "$failureAction failure must not be followed by combined FinalVerify"
        $results.Add([pscustomobject]@{scenario=('failure '+$failureAction);status='PASS';detail='Failure remained non-green and the journal retained recovery intent plus exact error.'})
    }

    $missingPhaseState=New-RecoveryTestState -StoragePhase 'ReplacementVerified' -WithSimultaneousEvidence $false -SecureBoot $false
    $missingPhaseState.omitStatusPhase=$true
    $missingPhase=Invoke-RecoveryScenario -Root $root -Name 'failure-missing-status-phase' -InitialState $missingPhaseState
    Assert-RecoveryTest ($missingPhase.exitCode -ne 0 -and [bool]$missingPhase.state.journal.operationInProgress) 'missing authoritative Status phase should fail closed and preserve recovery intent'
    $results.Add([pscustomobject]@{scenario='missing status phase';status='PASS';detail='Missing Status.phase fails closed and leaves the recovery journal in progress.'})

    $missingReceiptState=New-RecoveryTestState -StoragePhase 'ReplacementVerified' -WithSimultaneousEvidence $false -SecureBoot $false
    $missingReceiptState.journal.pathBBuild=[ordered]@{inventoryBuild='mock-missing-stage-receipt'}
    $missingReceipt=Invoke-RecoveryScenario -Root $root -Name 'missing-stage-receipt-baseline-match' -InitialState $missingReceiptState
    Assert-RecoveryTest ($missingReceipt.exitCode -eq 0) "missing stage receipt with a matching ESP baseline should continue recovery; exit=$($missingReceipt.exitCode), stderr=$($missingReceipt.stderr)"
    Assert-RecoveryTest ($missingReceipt.state.journal.recoveryEspComparison.match -eq $true -and @($missingReceipt.state.actions) -contains 'Remove') 'missing receipt must be accompanied by a recorded matching ESP comparison before storage cleanup continues'
    $results.Add([pscustomobject]@{scenario='missing receipt with matching ESP';status='PASS';detail='Recovery continued only after a read-only current ESP snapshot matched the recorded baseline.'})

    foreach ($espFailure in @('mismatch','unreadable')) {
        $espFailureState=New-RecoveryTestState -StoragePhase 'ReplacementVerified' -WithSimultaneousEvidence $false -SecureBoot $false
        $espFailureState.journal.pathBBuild=[ordered]@{inventoryBuild='mock-missing-stage-receipt'}
        if ($espFailure -ceq 'mismatch') { $espFailureState.espMismatch=$true } else { $espFailureState.espReadFails=$true }
        $espFailed=Invoke-RecoveryScenario -Root $root -Name ('failure-missing-stage-receipt-'+$espFailure) -InitialState $espFailureState
        Assert-RecoveryTest ($espFailed.exitCode -ne 0 -and [bool]$espFailed.state.journal.operationInProgress -and @($espFailed.state.actions).Count -eq 0) "missing receipt with $espFailure ESP evidence must fail closed before storage mutation"
        $results.Add([pscustomobject]@{scenario=('missing receipt with '+$espFailure+' ESP');status='PASS';detail='No storage cleanup action ran when the current ESP could not be proven equal to baseline.'})
    }

    [pscustomobject][ordered]@{schema='combined-host-recovery-tests.v1';status='PASS';testCount=$results.Count;results=@($results.ToArray())} | ConvertTo-Json -Depth 12
} finally {
    if (Test-Path -LiteralPath $root -PathType Container) {
        $resolved=[IO.Path]::GetFullPath($root)
        if (-not $resolved.StartsWith($toolsRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw "Refusing to remove recovery test output outside its tools directory: $resolved" }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
