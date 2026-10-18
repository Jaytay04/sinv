$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$worktree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$repository = [IO.Path]::GetFullPath((Join-Path $worktree '..\..\..'))
Import-Module (Join-Path $PSScriptRoot 'step-support.psm1') -Force -DisableNameChecking
$root = Join-Path $worktree ('spoof-handoff\reports\combined-host-command-steps-20261018\tests\'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($root)
$script:checks = @()
function Check([bool]$Condition,[string]$Name) {
    if (-not $Condition) { throw "FAIL: $Name" }
    $script:checks += $Name
}
function Reject([scriptblock]$Body,[string]$Name) {
    $failed = $false
    try { & $Body | Out-Null } catch { $failed = $true }
    Check $failed $Name
}
function Write-Json([string]$Path,$Value) { [IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false)) }
$defaults = Get-CombinedStepDefaults -RepositoryRoot $repository -WorktreeRoot $worktree
Check ($defaults.SmbiosReaderPath.StartsWith($repository+'\spoof-handoff\')) 'physical SMBIOS reader resolves in main repository'
Check ($defaults.NicNativeReaderPath.StartsWith($repository+'\spoof-handoff\')) 'physical native NIC reader resolves in main repository'
Check ($defaults.NicReaderPath.StartsWith($worktree+'\spoof-handoff\')) 'NIC script resolves in feature worktree'
Check ($defaults.PreparedPath -ceq (Join-Path $worktree 'spoof-handoff\reports\f12-release\host-prepared.json')) 'selector resolves in assigned feature reports'
Check ($defaults.VmGateSha256 -ceq 'AF9024D7EFBF725D464825E5A7217F39CADD0538FD56C8155C24C3D9EBDBF483') 'accepted gate remains pinned'
Check ($defaults.VmGatePath -ceq (Join-Path $worktree 'spoof-handoff\reports\activation-repair-20261018\gate\combined-vm-gate.json')) 'accepted gate resolves to the current repaired-source VM evidence'
Check ((Get-FileHash -LiteralPath $defaults.VmGatePath).Hash -ceq $defaults.VmGateSha256) 'launcher gate digest matches the actual accepted gate bytes'
Check ($defaults.ReleaseManifestSha256 -ceq 'E35C2AACD47E59F07A0AE12AE6973C751FD0D13267645385471FA516048221C5') 'physical storage release remains pinned'
$runRoot = Join-Path $root 'runs'
$run = Join-Path $runRoot 'captured-run'
[void][IO.Directory]::CreateDirectory($run)
$journalPath = Join-Path $run 'journal.json'
$journal = [ordered]@{schema='combined-host-preparation.v1';runDirectory=$run;phase='Prepared'}
Write-Json $journalPath $journal
$summary = [ordered]@{status='PREPARED_READONLY_GATE_PASS';activationAvailable=$true;runDirectory=$run;journalPath=$journalPath;journalSha256=(Get-FileHash $journalPath).Hash}
$json = $summary|ConvertTo-Json -Depth 12
$pointer = Join-Path $root 'host-prepared.json'
Reject { Read-CombinedStepPrepared -Path $pointer -RunRoot $runRoot } 'missing selector refuses to guess a run'
Save-CombinedStepPrepared -Path $pointer -Json $json -RunRoot $runRoot
Check (([IO.File]::ReadAllText($pointer)) -ceq $json) 'saved summary is the exact successful child JSON'
Check ((Read-CombinedStepPrepared -Path $pointer -RunRoot $runRoot).runDirectory -ceq $run) 'subsequent commands select the prepared run'
$before = (Get-FileHash $pointer).Hash
Reject { Save-CombinedStepPrepared -Path $pointer -Json $json -RunRoot $runRoot } 'second Prepare cannot overwrite saved recovery selection'
Check ((Get-FileHash $pointer).Hash -ceq $before) 'failed overwrite leaves selector bytes unchanged'
Check (@(Get-ChildItem -LiteralPath $root -Filter '*.pending.*').Count -eq 0) 'temporary summary is removed after failed move'
[void][IO.Directory]::CreateDirectory((Join-Path $runRoot 'newer-unrelated-run'))
Check ((Read-CombinedStepPrepared -Path $pointer -RunRoot $runRoot).runDirectory -ceq $run) 'newer unrelated directory does not change selection'
$journal.phase = 'PolicyPendingReboot'
Write-Json $journalPath $journal
Check ((Read-CombinedStepPrepared -Path $pointer -RunRoot $runRoot).runDirectory -ceq $run) 'legitimate journal updates do not compare against stale initial journal hash'
Reject { Save-CombinedStepPrepared -Path (Join-Path $root 'stale.json') -Json $json -RunRoot $runRoot } 'initial summary journal digest is checked before persistence'
Check ((Assert-CombinedStepRunDirectory -RunDirectory $run -RunRoot $runRoot) -ceq $run) 'explicit recovery run works independently of saved selector'
Reject { Assert-CombinedStepRunDirectory -RunDirectory (Join-Path $root 'outside') -RunRoot $runRoot } 'foreign run directory is rejected'
Reject { Assert-CombinedStepRunDirectory -RunDirectory (Join-Path $runRoot '..\outside') -RunRoot $runRoot } 'directory traversal is rejected'
Reject { Assert-CombinedStepRunDirectory -RunDirectory (Join-Path $runRoot 'partial-no-journal') -RunRoot $runRoot } 'partial Prepare without a combined journal is not selected'
Reject { Assert-CombinedStepRunDirectory -RunDirectory '' -RunRoot $runRoot } 'empty run directory is rejected'
$journal.runDirectory = Join-Path $runRoot 'wrong-run'
Write-Json $journalPath $journal
Reject { Read-CombinedStepPrepared -Path $pointer -RunRoot $runRoot } 'journal location mismatch is rejected'
$journal.runDirectory = $run
Write-Json $journalPath $journal
$summary.journalSha256 = (Get-FileHash $journalPath).Hash
$summary.activationAvailable = 'true'
Reject { Save-CombinedStepPrepared -Path (Join-Path $root 'string-bool.json') -Json ($summary|ConvertTo-Json) -RunRoot $runRoot } 'string activation flag cannot pass boolean validation'
$summary.activationAvailable = $false
Reject { Save-CombinedStepPrepared -Path (Join-Path $root 'disabled.json') -Json ($summary|ConvertTo-Json) -RunRoot $runRoot } 'disabled activation is rejected'
$summary.activationAvailable = $true
$summary.status = 'PREPARED_READONLY_VM_GATE_REQUIRED'
Reject { Save-CombinedStepPrepared -Path (Join-Path $root 'unready.json') -Json ($summary|ConvertTo-Json) -RunRoot $runRoot } 'missing gate status is rejected'
Reject { Save-CombinedStepPrepared -Path (Join-Path $root 'malformed.json') -Json 'invalid-json' -RunRoot $runRoot } 'malformed child output is rejected'
Assert-CombinedStepFileHash -Path $journalPath -Expected $summary.journalSha256
Check $true 'input hash match is accepted'
Reject { Assert-CombinedStepFileHash -Path $journalPath -Expected ('0'*64) } 'input hash mismatch is rejected'
$lockPath = Join-Path $root 'command.lock'
$lock = Enter-CombinedStepLock -Path $lockPath
try { Reject { $other = Enter-CombinedStepLock -Path $lockPath; $other.Dispose() } 'concurrent step lock is rejected' } finally { $lock.Dispose() }
$lock = Enter-CombinedStepLock -Path $lockPath
$lock.Dispose()
Check $true 'released lock can be reacquired without deleting its file'
$attemptPath=Join-Path $root 'prepare-attempt.json'
$attempt=New-CombinedStepPrepareAttempt -Path $attemptPath -RunRoot $runRoot -EvidenceRoot (Join-Path $root 'mock-evidence')
Check ($attempt.status -ceq 'PREPARE_STARTED_COMPLETION_UNCONFIRMED') 'Prepare intent is durable before child launch'
Check (@($attempt.existingRunDirectories).Count -eq 2) 'Prepare intent records pre-existing directories without selecting them'
Reject { New-CombinedStepPrepareAttempt -Path $attemptPath -RunRoot $runRoot -EvidenceRoot $root } 'another Prepare cannot overwrite an unresolved attempt'
Check ((Get-Content -LiteralPath $attemptPath -Raw|ConvertFrom-Json).id -ceq $attempt.id) 'failed retry preserves original intent identity'
Reject { Complete-CombinedStepPrepareAttempt -Path $attemptPath -AttemptId 'wrong-id' } 'another attempt cannot clear the durable guard'
Check (Test-Path -LiteralPath $attemptPath) 'failed completion leaves guard present'
Complete-CombinedStepPrepareAttempt -Path $attemptPath -AttemptId $attempt.id
Check (-not(Test-Path -LiteralPath $attemptPath)) 'successful owned completion clears the guard'
$mock = Join-Path $root 'mock child with spaces.ps1'
[IO.File]::WriteAllText($mock,'param([string]$Value,[string]$Trailing) [pscustomobject]@{value=$Value;trailing=$Trailing;mock=$true}|ConvertTo-Json -Compress',[Text.UTF8Encoding]::new($false))
$evidence = Join-Path $root 'mock-child-evidence'
$program = Join-Path $PSHOME 'powershell.exe'
$child = Invoke-CombinedStepChild -Program $program -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File',$mock,'-Value','value with spaces','-Trailing','C:\a path\') -EvidenceRoot $evidence -Action 'MockSuccess'
$decoded = $child.stdout | ConvertFrom-Json
Check ($decoded.value -ceq 'value with spaces' -and $decoded.trailing -ceq 'C:\a path\') 'child arguments preserve spaces and trailing backslash'
$receipt = Get-Content -LiteralPath (Join-Path $child.evidenceDirectory 'command.json') -Raw | ConvertFrom-Json
Check ($receipt.exitCode -eq 0 -and $receipt.automaticReboot -eq $false) 'successful child receipt records no automatic reboot'
Check ((Get-FileHash $receipt.stdout.path).Hash -ceq $receipt.stdout.sha256) 'stdout hash binds the captured output'
$launch = Get-Content -LiteralPath (Join-Path $child.evidenceDirectory 'launch.json') -Raw | ConvertFrom-Json
$processRecord = Get-Content -LiteralPath (Join-Path $child.evidenceDirectory 'process.json') -Raw | ConvertFrom-Json
Check ($launch.childSource.sha256 -ceq (Get-FileHash -LiteralPath $mock).Hash) 'launch receipt pins child source before execution'
Check ($processRecord.processId -gt 0 -and $processRecord.startTimeUtc) 'process identity is durable independently of completion receipt'
[IO.File]::WriteAllText($mock,"[Console]::Out.WriteLine('EXPECTED_MOCK_STDOUT'); [Console]::Error.WriteLine('EXPECTED_MOCK_STDERR'); exit 7",[Text.UTF8Encoding]::new($false))
$failure = $null
try { Invoke-CombinedStepChild -Program $program -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File',$mock) -EvidenceRoot $evidence -Action 'MockFailure' | Out-Null } catch { $failure = $_.Exception.Message }
Check ($null -ne $failure -and $failure.Contains('exit 7') -and $failure.Contains('EXPECTED_MOCK_STDOUT') -and $failure.Contains('EXPECTED_MOCK_STDERR')) 'child failure preserves exact exit code and both streams'
$failedReceipt = @(Get-ChildItem -LiteralPath $evidence -Filter command.json -Recurse | ForEach-Object {Get-Content -LiteralPath $_.FullName -Raw|ConvertFrom-Json} | Where-Object action -CEQ 'MockFailure')
Check ($failedReceipt.Count -eq 1 -and $failedReceipt[0].exitCode -eq 7 -and $failedReceipt[0].automaticReboot -eq $false) 'failed action remains evidenced without a next step'
$heartbeatMock=Join-Path $root 'heartbeat stderr success.ps1'
[IO.File]::WriteAllText($heartbeatMock,"[Console]::Error.WriteLine('EXPECTED_NONFATAL_STDERR'); Start-Sleep -Milliseconds 2200; [Console]::Out.WriteLine('EXPECTED_SUCCESS_STDOUT'); exit 0",[Text.UTF8Encoding]::new($false))
$oldError=[Console]::Error
$progress=[IO.StringWriter]::new()
try {
    [Console]::SetError($progress)
    $heartbeat=Invoke-CombinedStepChild -Program $program -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File',$heartbeatMock) -EvidenceRoot $evidence -Action 'MockHeartbeat' -ProgressIntervalSeconds 1
} finally { [Console]::SetError($oldError) }
Check ($heartbeat.stdout.Contains('EXPECTED_SUCCESS_STDOUT') -and $heartbeat.stderr.Contains('EXPECTED_NONFATAL_STDERR')) 'stderr with exit zero is captured without terminating the launcher'
Check ($progress.ToString().Contains('MockHeartbeat still running')) 'waiting launcher prints elapsed progress without polluting child JSON'
Check (Test-Path -LiteralPath (Join-Path $heartbeat.evidenceDirectory 'launch.json')) 'launch receipt survives independently of captured command result'
Check (-not(Test-Path -LiteralPath (Join-Path $heartbeat.evidenceDirectory 'interrupted.json'))) 'successful completion is not labeled interrupted'
$progress.Dispose()
$detachedMock=Join-Path $root 'detached parent.ps1'
$detachedPid=Join-Path $root 'detached.pid'
[IO.File]::WriteAllText($detachedMock,@'
param([string]$PidPath,[string]$ChildOut,[string]$ChildErr)
$child=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList '-NoProfile -Command "Start-Sleep -Seconds 12"' -PassThru -WindowStyle Hidden -RedirectStandardOutput $ChildOut -RedirectStandardError $ChildErr
[IO.File]::WriteAllText($PidPath,[string]$child.Id)
[Console]::Out.WriteLine('ROOT_COMPLETED')
'@,[Text.UTF8Encoding]::new($false))
$watch=[Diagnostics.Stopwatch]::StartNew()
try {
    $detached=Invoke-CombinedStepChild -Program $program -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File',$detachedMock,'-PidPath',$detachedPid,'-ChildOut',(Join-Path $root 'detached.out'),'-ChildErr',(Join-Path $root 'detached.err')) -EvidenceRoot $evidence -Action 'MockDetached'
    Check ($watch.Elapsed.TotalSeconds -lt 8) 'launcher returns when the root exits despite a live detached child'
    Check ($detached.stdout.Contains('ROOT_COMPLETED')) 'launcher reads root output despite open descendant handles'
    $childPid=[int]([IO.File]::ReadAllText($detachedPid))
    Check ($null -ne (Get-Process -Id $childPid -ErrorAction SilentlyContinue)) 'detached child was still live when the launcher returned'
} finally {
    if(Test-Path -LiteralPath $detachedPid){$cleanupPid=[int]([IO.File]::ReadAllText($detachedPid)); Stop-Process -Id $cleanupPid -Force -ErrorAction SilentlyContinue}
}
# Exercise the actual Status launcher inside an isolated mock repository.
$fakeRepository=Join-Path $root 'mock main with spaces'
$fakeWorktree=Join-Path $fakeRepository '.codex-build\worktrees\smbios-boot-serial-probe'
$fakeCommands=Join-Path $fakeWorktree 'spoof-handoff\commands'
$fakeTools=Join-Path $fakeWorktree 'spoof-handoff\tools\combined_host'
foreach($directory in @($fakeCommands,$fakeTools)){[void][IO.Directory]::CreateDirectory($directory)}
Copy-Item -LiteralPath (Join-Path $worktree 'spoof-handoff\commands\combined-host-test.ps1') -Destination $fakeCommands
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'step-support.psm1') -Destination $fakeTools
[IO.File]::WriteAllText((Join-Path $fakeTools 'run.ps1'),'param([string]$Action,[string]$RepositoryRoot,[string]$WorktreeRoot,[string]$RunDirectory) [pscustomobject]@{mock=$true;action=$Action;repository=$RepositoryRoot;worktree=$WorktreeRoot;run=$RunDirectory}|ConvertTo-Json -Compress',[Text.UTF8Encoding]::new($false))
$fakeDefaults=Get-CombinedStepDefaults -RepositoryRoot $fakeRepository -WorktreeRoot $fakeWorktree
$fakeRun=Join-Path $fakeDefaults.RunRoot 'mock-captured-run'
[void][IO.Directory]::CreateDirectory($fakeRun)
$fakeJournal=Join-Path $fakeRun 'journal.json'
Write-Json $fakeJournal ([ordered]@{schema='combined-host-preparation.v1';runDirectory=$fakeRun;phase='Prepared'})
$fakeSummary=[ordered]@{status='PREPARED_READONLY_GATE_PASS';activationAvailable=$true;runDirectory=$fakeRun;journalPath=$fakeJournal;journalSha256=(Get-FileHash $fakeJournal).Hash}
Save-CombinedStepPrepared -Path $fakeDefaults.PreparedPath -Json ($fakeSummary|ConvertTo-Json) -RunRoot $fakeDefaults.RunRoot
$launched=Invoke-CombinedStepChild -Program $program -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $fakeCommands 'combined-host-test.ps1'),'Status') -EvidenceRoot $evidence -Action 'MockStatusLauncher'
$mockStatus=$launched.stdout|ConvertFrom-Json
Check ($mockStatus.mock -eq $true -and $mockStatus.action -ceq 'Status') 'actual launcher dispatches the requested action to the mock runner'
Check ($mockStatus.repository -ceq $fakeRepository -and $mockStatus.worktree -ceq $fakeWorktree) 'actual launcher resolves repository and worktree without current-directory setup'
Check ($mockStatus.run -ceq $fakeRun) 'actual launcher forwards the saved run without -RunDirectory'
foreach ($path in @((Join-Path $PSScriptRoot 'step-support.psm1'),(Join-Path $worktree 'spoof-handoff\commands\combined-host-test.ps1'),(Join-Path $worktree 'spoof-handoff\commands\combined-host-rollback.ps1'))) {
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    Check ($errors.Count -eq 0) ('PowerShell parses: '+[IO.Path]::GetFileName($path))
    $mutators=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -in @('Restart-Computer','shutdown','Set-SecureBootUEFI','bcdedit','pnputil')},$true))
    Check ($mutators.Count -eq 0) ('launcher adds no direct reboot, firmware or driver actions: '+[IO.Path]::GetFileName($path))
}
$report=[ordered]@{schema='combined-host-command-step-tests.v1';status='PASS';testCount=$script:checks.Count;scope='Pure file fixtures and mock child processes only; no host preparation, driver, firmware, network, or VM actions.';checks=$script:checks;fixtureDirectory=$root}
$reportPath=Join-Path $root 'result.json'
Write-Json $reportPath $report
[pscustomobject]@{status='PASS';tests=$script:checks.Count;report=$reportPath;sha256=(Get-FileHash $reportPath).Hash}|ConvertTo-Json
