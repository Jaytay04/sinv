param([Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$resolvedFixture=[IO.Path]::GetFullPath($FixtureRoot)
if(-not $resolvedFixture.Contains('\spoof-handoff\')){throw 'Test fixtures must remain inside spoof-handoff.'}
New-Item -ItemType Directory -Path $resolvedFixture -Force|Out-Null
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '..\run.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw ('Workflow PowerShell parse failed: '+($parseErrors|ForEach-Object Message -join '; '))}
$definition=$ast.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Read-WriteProbeReport'},$true)
if(-not $definition){throw 'Strict report parser function was not found.'}
. ([scriptblock]::Create($definition.Extent.Text))
$worktreeRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$mainRepo=[IO.Path]::GetFullPath((Join-Path $worktreeRoot '..\..\..'))
$rootGuardScript=Join-Path $mainRepo 'tools\efi_inventory\path-b-collector.ps1'
if(-not(Test-Path -LiteralPath $rootGuardScript -PathType Leaf)){throw ('The real path root guard is missing: '+$rootGuardScript)}
. $rootGuardScript -LibraryOnly
if(-not(Get-Command Assert-NoReparseAncestors -ErrorAction SilentlyContinue)){throw 'The real path root guard did not import.'}
$writerDefinition=$ast.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Write-WriteFlag'},$true)
$removeDefinition=$ast.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Remove-WriteFlagIfUnchanged'},$true)
if(-not $writerDefinition -or -not $removeDefinition){throw 'The flag writer or exact owned-flag remover was not found.'}
. ([scriptblock]::Create($writerDefinition.Extent.Text))
. ([scriptblock]::Create($removeDefinition.Extent.Text))
$id='0123456789ABCDEF0123456789ABCDEF'
$flagRoot=Join-Path $resolvedFixture 'actual-flag-root'
New-Item -ItemType Directory -Path $flagRoot -Force|Out-Null
$flagRecord=Write-WriteFlag $flagRoot $id
$flagPath=Join-Path $flagRoot 'HypersinvSecureBootWrite.flag'
$expectedFlag=[Text.Encoding]::ASCII.GetBytes("HSBWRITE001`r`nbuild_id=$id`r`nmode=probe-write-restore`r`nEND`r`n")
$actualFlag=[IO.File]::ReadAllBytes($flagPath)
if($flagRecord.length -ne $expectedFlag.Length -or [Convert]::ToBase64String($actualFlag) -cne [Convert]::ToBase64String($expectedFlag) -or $flagRecord.sha256 -cne (Get-Sha256 $flagPath)){throw 'Actual one-shot flag creation did not round-trip the expected bytes and SHA-256.'}
$removedFlag=Remove-WriteFlagIfUnchanged $flagRoot $flagRecord.sha256 $id
if($removedFlag -cne 'removed_unchanged_owned_flag' -or (Test-Path -LiteralPath $flagPath)){throw 'Actual one-shot flag removal did not verify.'}
$valid=@(
    'HSBWRITE001',('build_id='+$id),'mode=probe-write-restore','name=SecureBoot','guid=8be4df61-93ca-11d2-aa0d-00e098032b8c',
    'timestamp_status=0x0000000000000000','timestamp=2026-10-16T19:38:09','timestamp_timezone=2047','timestamp_daylight=0',
    'flag_consumed=yes','flag_status=0x0000000000000000','flag_verify_status=0x800000000000000E','flag_validation=valid',
    'preflight_called=yes','preflight_status=0x0000000000000000','preflight_size=1','preflight_attributes=0x00000006','preflight_data=01',
    'intent_flushed=yes','intent_status=0x0000000000000000','set_called=yes','set_status=0x0000000000000000',
    'after_called=yes','after_status=0x0000000000000000','after_size=1','after_attributes=0x00000006','after_data=00',
    'restore_called=yes','restore_status=0x0000000000000000','final_called=yes','final_status=0x0000000000000000','final_size=1','final_attributes=0x00000006','final_data=01',
    'consistency=none','verdict=TOGGLE-WORKS','recovered=yes','recovery_action=no_recovery_needed','result=pass','END'
)
$report=Join-Path $resolvedFixture 'synthetic-report.txt'
$text=($valid -join "`r`n")+"`r`n"
[IO.File]::WriteAllText($report,$text,[Text.Encoding]::ASCII)
$parsed=Read-WriteProbeReport $report $id '01' 6
if($parsed.verdict -cne 'TOGGLE-WORKS'){throw 'Valid toggle report did not parse.'}
$refused=$text.Replace('set_status=0x0000000000000000','set_status=0x8000000000000008').Replace('after_data=00','after_data=01').Replace('verdict=TOGGLE-WORKS','verdict=TOGGLE-REFUSED')
[IO.File]::WriteAllText($report,$refused,[Text.Encoding]::ASCII)
if((Read-WriteProbeReport $report $id '01' 6).verdict -cne 'TOGGLE-REFUSED'){throw 'A verified refusal was not accepted as a completed, restored experiment.'}
$refusedObservedOff=$text.Replace('set_status=0x0000000000000000','set_status=0x8000000000000008').Replace('consistency=none','consistency=set_error_but_off_observed').Replace('verdict=TOGGLE-WORKS','verdict=TOGGLE-REFUSED')
[IO.File]::WriteAllText($report,$refusedObservedOff,[Text.Encoding]::ASCII)
if((Read-WriteProbeReport $report $id '01' 6).after_data -cne '00'){throw 'A failed SetVariable status with observed off bytes was not retained as a recovered refusal.'}
$refusedRestoreDenied=$refused.Replace('restore_status=0x0000000000000000','restore_status=0x8000000000000008')
[IO.File]::WriteAllText($report,$refusedRestoreDenied,[Text.Encoding]::ASCII)
if((Read-WriteProbeReport $report $id '01' 6).restore_status -cne '0x8000000000000008'){throw 'A denied restore call with final original bytes did not retain its raw status.'}
$noop=$text.Replace('after_data=00','after_data=01').Replace('verdict=TOGGLE-WORKS','verdict=VARIABLE-WRITE-SILENT-NOOP')
[IO.File]::WriteAllText($report,$noop.Replace('consistency=none','consistency=set_success_but_original_observed').Replace('restore_status=0x0000000000000000','restore_status=0x8000000000000008'),[Text.Encoding]::ASCII)
if((Read-WriteProbeReport $report $id '01' 6).verdict -cne 'VARIABLE-WRITE-SILENT-NOOP'){throw 'A verified silent no-op was not accepted as a completed, restored experiment.'}
$unverified=$text.Replace('restore_status=0x0000000000000000','restore_status=0x8000000000000008').Replace('consistency=none','consistency=restore_error_but_original_observed').Replace('verdict=TOGGLE-WORKS','verdict=WRITE-OUTCOME-UNVERIFIED-RECOVERED')
[IO.File]::WriteAllText($report,$unverified,[Text.Encoding]::ASCII)
if((Read-WriteProbeReport $report $id '01' 6).verdict -cne 'WRITE-OUTCOME-UNVERIFIED-RECOVERED'){throw 'Final original bytes were not accepted when the raw restore status was ambiguous.'}
$failedSetUnreadable=$text.Replace('set_status=0x0000000000000000','set_status=0x8000000000000008').Replace('after_status=0x0000000000000000','after_status=0x8000000000000008').Replace('after_size=1','after_size=not_available').Replace('after_attributes=0x00000006','after_attributes=not_available').Replace('after_data=00','after_data=not_available').Replace('consistency=none','consistency=after_unreadable').Replace('verdict=TOGGLE-WORKS','verdict=WRITE-OUTCOME-UNVERIFIED-RECOVERED')
[IO.File]::WriteAllText($report,$failedSetUnreadable,[Text.Encoding]::ASCII)
if((Read-WriteProbeReport $report $id '01' 6).set_status -cne '0x8000000000000008'){throw 'A failed SetVariable with unreadable post-write data was not preserved with a final-original proof.'}
$failedSetUnexpected=$text.Replace('set_status=0x0000000000000000','set_status=0x8000000000000008').Replace('after_data=00','after_data=02').Replace('consistency=none','consistency=after_unexpected_value').Replace('verdict=TOGGLE-WORKS','verdict=WRITE-OUTCOME-UNVERIFIED-RECOVERED')
[IO.File]::WriteAllText($report,$failedSetUnexpected,[Text.Encoding]::ASCII)
if((Read-WriteProbeReport $report $id '01' 6).after_data -cne '02'){throw 'A failed SetVariable with an unexpected post-write byte was not preserved.'}
$failedSetAndRestoreUnexpected=$failedSetUnexpected.Replace('restore_status=0x0000000000000000','restore_status=0x8000000000000008').Replace('consistency=after_unexpected_value','consistency=restore_error_but_original_observed')
[IO.File]::WriteAllText($report,$failedSetAndRestoreUnexpected,[Text.Encoding]::ASCII)
if((Read-WriteProbeReport $report $id '01' 6).set_status -cne '0x8000000000000008'){throw 'A failed SetVariable plus failed restore call was not retained when the final read proved original bytes.'}
$notAttempted=@(
    'HSBWRITE001',('build_id='+$id),'mode=probe-write-restore','name=SecureBoot','guid=8be4df61-93ca-11d2-aa0d-00e098032b8c',
    'timestamp_status=0x0000000000000000','timestamp=2026-10-16T19:38:09','timestamp_timezone=2047','timestamp_daylight=0',
    'flag_consumed=no','flag_status=0x0000000000000000','flag_verify_status=not_called','flag_validation=content_mismatch',
    'preflight_called=no','preflight_status=not_called','preflight_size=not_available','preflight_attributes=not_available','preflight_data=not_available',
    'intent_flushed=not_called','intent_status=not_called','set_called=no','set_status=not_called','after_called=no','after_status=not_called','after_size=not_available','after_attributes=not_available','after_data=not_available',
    'restore_called=no','restore_status=not_called','final_called=no','final_status=not_called','final_size=not_available','final_attributes=not_available','final_data=not_available',
    'consistency=none','verdict=NOT-ATTEMPTED','recovered=not_verified','recovery_action=no_recovery_needed','result=not_attempted','END'
)
$notAttemptedText=($notAttempted -join "`r`n")+"`r`n"
[IO.File]::WriteAllText($report,$notAttemptedText,[Text.Encoding]::ASCII)
if((Read-WriteProbeReport $report $id '01' 6).verdict -cne 'NOT-ATTEMPTED'){throw 'A malformed-flag no-SET report was not accepted as evidence.'}
$cases=@(
    @{name='wrong-build';text=$text.Replace($id,('F'*32))},
    @{name='wrong-guid';text=$text.Replace('8be4df61-93ca-11d2-aa0d-00e098032b8c','00000000-0000-0000-0000-000000000000')},
    @{name='missing-flag-consumption';text=$text.Replace('flag_consumed=yes','flag_consumed=no')},
    @{name='unknown-flag-validation';text=$text.Replace('flag_validation=valid','flag_validation=unknown')},
    @{name='unflushed-intent';text=$text.Replace('intent_status=0x0000000000000000','intent_status=0x8000000000000008')},
    @{name='set-success-with-original-after';text=$noop.Replace('verdict=VARIABLE-WRITE-SILENT-NOOP','verdict=TOGGLE-WORKS')},
    @{name='refusal-off-without-consistency';text=$refusedObservedOff.Replace('consistency=set_error_but_off_observed','consistency=none')},
    @{name='final-mismatch';text=$text.Replace('final_data=01','final_data=00')},
    @{name='restore-failure';text=$text.Replace('restore_status=0x0000000000000000','restore_status=0x8000000000000008').Replace('final_data=01','final_data=00').Replace('recovered=yes','recovered=no').Replace('recovery_action=no_recovery_needed','recovery_action=cold_reset_requested').Replace('result=pass','result=fail').Replace('verdict=TOGGLE-WORKS','verdict=RESTORE-FAILED')},
    @{name='uncalled-after-has-status';text=$notAttemptedText.Replace('after_status=not_called','after_status=0x0000000000000000')},
    @{name='called-set-has-no-status';text=$text.Replace('set_status=0x0000000000000000','set_status=not_called')},
    @{name='no-set-has-consistency';text=$notAttemptedText.Replace('consistency=none','consistency=other_recovered_inconsistency')},
    @{name='wrong-order';text=$text.Replace('flag_validation=valid'+"`r`n"+'preflight_called=yes','preflight_called=yes'+"`r`n"+'flag_validation=valid')},
    @{name='lf-only';text=$text.Replace("`r`n","`n")},
    @{name='truncated';text=$text.Replace("END`r`n",'')}
)
foreach($case in $cases){
    [IO.File]::WriteAllText($report,$case.text,[Text.Encoding]::ASCII)
    $rejected=$false;$message=''
    try{$null=Read-WriteProbeReport $report $id '01' 6}catch{$rejected=$true;$message=$_.Exception.Message}
    if(-not $rejected){throw ('Invalid write-probe report accepted: '+$case.name)}
    if($case.name -eq 'restore-failure' -and ($message -notmatch 'expected=01' -or $message -notmatch 'final=00' -or $message -notmatch 'restore_status=0x8000000000000008')){throw ('Restore failure did not preserve exact expected/actual/status: '+$message)}
}
'PASS: 25 strict synthetic parser cases and actual guarded flag create/flush/hash/remove round-trip; no firmware, ESP, driver, or elevation access.'
