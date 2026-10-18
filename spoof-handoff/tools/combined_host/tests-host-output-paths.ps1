$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
$worktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
Import-Module (Join-Path $PSScriptRoot 'step-support.psm1') -Force -DisableNameChecking
. (Join-Path $PSScriptRoot 'readback.ps1')
$checks=@()
$oldCapture=Join-Path $worktree ('spoof-handoff\reports\combined-host-20261018\o\c.'+('0'*32))
$old=Get-CombinedNativePathPreflight -RunDirectory $oldCapture
if($old.maximumPathLength -ne 254 -or $old.withinLimit){throw 'The regression fixture no longer reproduces the actual 254-character failure.'}
$checks+='actual worktree reproduces old 254-character rejection'
$rejected=$false
try{Assert-CombinedNativePathPreflight -RunDirectory $oldCapture|Out-Null}catch{$rejected=$_.Exception.Message.Contains('limit=240')}
if(-not $rejected){throw 'The unchanged native guard did not reject the old output root.'}
$checks+='original 240-character guard still rejects old output root'
$plan=@(Assert-CombinedStepReadbackPaths -WorktreeRoot $worktree)
if($plan.Count -ne 3){throw 'All three real host lifecycle calls must be checked.'}
$checks+='actual original, active and final lifecycle output literals are preflighted'
foreach($item in $plan){
    if(-not $item.withinLimit -or $item.limit -ne 240 -or $item.maximumPathLength -ne 234){throw ('Unexpected output-path measurement: '+($item|ConvertTo-Json -Compress))}
    $checks+=('actual host native output fits: '+$item.longestPath)
}
$temporary=Join-Path $worktree ('spoof-handoff\reports\host-path-fix-20261018\tests\'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($temporary)
$extended=$worktree+('\nested'*6)
$longCapture=Join-Path $extended ('spoof-handoff\reports\ch\o\c.'+('0'*32))
$rejected=$false
try{Assert-CombinedNativePathPreflight -RunDirectory $longCapture|Out-Null}catch{$rejected=$true}
if(-not $rejected){throw 'A still-longer root must remain rejected.'}
$checks+='deeper output roots still fail closed'
$report=[ordered]@{schema='combined-host-output-path-tests.v1';status='PASS';testCount=$checks.Count;old=$old;current=$plan;checks=$checks;scope='Pure AST/path checks only; no reader, Prepare, driver, boot, firmware, or VM action.'}
$path=Join-Path $temporary 'result.json'
[IO.File]::WriteAllText($path,($report|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
[pscustomobject]@{status='PASS';tests=$checks.Count;oldLength=$old.maximumPathLength;newLengths=@($plan.maximumPathLength);limit=240;report=$path;sha256=(Get-FileHash $path).Hash}|ConvertTo-Json
