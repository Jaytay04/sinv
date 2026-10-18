Set-StrictMode -Version 2.0

function Get-CombinedStepDefaults {
    param([string]$RepositoryRoot,[string]$WorktreeRoot)
    [pscustomobject]@{
        PreparedPath = Join-Path $WorktreeRoot 'spoof-handoff\reports\f12-release\host-prepared.json'
        PrepareAttemptPath = Join-Path $WorktreeRoot 'spoof-handoff\reports\f12-release\host-prepare-attempt.json'
        LockPath = Join-Path $WorktreeRoot 'spoof-handoff\reports\f12-release\host-command.lock'
        EvidenceRoot = Join-Path $WorktreeRoot 'spoof-handoff\reports\combined-host-command-steps-20261018\actions'
        RunRoot = Join-Path $WorktreeRoot 'spoof-handoff\reports\combined-host-20261018'
        VmGatePath = Join-Path $WorktreeRoot 'spoof-handoff\reports\activation-repair-20261018\gate\combined-vm-gate.json'
        VmGateSha256 = 'AF9024D7EFBF725D464825E5A7217F39CADD0538FD56C8155C24C3D9EBDBF483'
        ReleaseDirectory = Join-Path $WorktreeRoot 'spoof-handoff\artifacts\storage-host-20261017\20261017T061421831Z.2c4616dcb3ab4d81a4d328e6b8b1afa8'
        ReleaseManifestSha256 = 'E35C2AACD47E59F07A0AE12AE6973C751FD0D13267645385471FA516048221C5'
        SmbiosReaderPath = Join-Path $RepositoryRoot 'spoof-handoff\artifacts\combined-vm\capture-tool-20261018T033000Z\build\smbios_windows_readback.exe'
        NicReaderPath = Join-Path $WorktreeRoot 'spoof-handoff\tools\nic_vm_probe\read-host.ps1'
        NicNativeReaderPath = Join-Path $RepositoryRoot 'spoof-handoff\artifacts\nic-reader-gap-20261017\native-build-vs18\Release\nic_native_read.exe'
    }
}

function Enter-CombinedStepLock {
    param([string]$Path)
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    try { return [IO.File]::Open($Path,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
    catch { throw "Cannot acquire the combined command lock: $Path. Another command may still be running. Exact error: $($_.Exception.Message)" }
}

function New-CombinedStepPrepareAttempt {
    param([string]$Path,[string]$RunRoot,[string]$EvidenceRoot)
    $attempt = [ordered]@{schema='combined-host-prepare-attempt.v1';status='PREPARE_STARTED_COMPLETION_UNCONFIRMED';id=[Guid]::NewGuid().ToString('N');startedUtc=[DateTime]::UtcNow.ToString('o');runRoot=$RunRoot;evidenceRoot=$EvidenceRoot;existingRunDirectories=@(if(Test-Path -LiteralPath $RunRoot -PathType Container){Get-ChildItem -LiteralPath $RunRoot -Directory|ForEach-Object FullName});nextStep='If this record remains, preserve it and the command evidence. Do not repeat Prepare, select the newest directory, or delete the guard to retry. Review the exact combined/storage journals before another run.'}
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($attempt|ConvertTo-Json -Depth 8))
    $stream = [IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
    return [pscustomobject]$attempt
}

function Complete-CombinedStepPrepareAttempt {
    param([string]$Path,[string]$AttemptId)
    $attempt = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($attempt.schema -cne 'combined-host-prepare-attempt.v1' -or $attempt.id -cne $AttemptId) { throw 'The Prepare guard changed during this command; preserve it for review.' }
    Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
}

function Assert-CombinedStepFileHash {
    param([string]$Path,[string]$Expected)
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash
    if ($actual -cne $Expected) { throw "Input SHA-256 mismatch: $Path expected=$Expected actual=$actual" }
}

function Assert-CombinedStepReadbackPaths {
    param([string]$WorktreeRoot)
    $corePath = Join-Path $WorktreeRoot 'spoof-handoff\tools\combined_host\run.ps1'
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($corePath,[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw 'Cannot preflight output paths: the combined host runner does not parse.' }
    $calls=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Invoke-CombinedReadback'},$true))
    if ($calls.Count -ne 3) { throw 'Expected the original, active, and final combined readback calls before Prepare.' }
    . (Join-Path $WorktreeRoot 'spoof-handoff\tools\combined_host\readback.ps1')
    foreach ($call in $calls) {
        $parameters=@($call.CommandElements|Where-Object{$_ -is [Management.Automation.Language.CommandParameterAst] -and $_.ParameterName -ieq 'OutputDirectory'})
        if ($parameters.Count -ne 1) { throw 'A combined readback call has no unique OutputDirectory.' }
        $index=[Array]::IndexOf($call.CommandElements,$parameters[0])
        $value=$call.CommandElements[$index+1]
        if ($value -isnot [Management.Automation.Language.StringConstantExpressionAst]) { throw 'Combined readback output preflight requires a literal output path.' }
        $capture=Join-Path (Join-Path $WorktreeRoot $value.Value) ('c.'+('0'*32))
        Assert-CombinedNativePathPreflight -RunDirectory $capture
    }
}

function Assert-CombinedStepRunDirectory {
    param([string]$RunDirectory,[string]$RunRoot)
    if ([string]::IsNullOrWhiteSpace($RunDirectory)) { throw 'The saved Prepare result has no runDirectory.' }
    $path = [IO.Path]::GetFullPath($RunDirectory)
    $prefix = [IO.Path]::GetFullPath($RunRoot).TrimEnd('\') + '\'
    if (-not $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw "RunDirectory must remain under $RunRoot; actual=$path" }
    $journalPath = Join-Path $path 'journal.json'
    $journal = Get-Content -LiteralPath $journalPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($journal.schema -cne 'combined-host-preparation.v1' -or [IO.Path]::GetFullPath([string]$journal.runDirectory) -cne $path) { throw "The combined journal does not match its run directory: $journalPath" }
    return $path
}

function Read-CombinedStepPrepared {
    param([string]$Path,[string]$RunRoot)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "No saved combined Prepare result: $Path. Run Prepare first, or provide the exact -RunDirectory printed by a successful Prepare for Status or rollback. No directory is selected by date." }
    $prepared = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($prepared.status -cne 'PREPARED_READONLY_GATE_PASS' -or $prepared.activationAvailable -isnot [bool] -or -not $prepared.activationAvailable) { throw "The saved result is not an activation-ready Prepare: $Path" }
    $null = Assert-CombinedStepRunDirectory -RunDirectory ([string]$prepared.runDirectory) -RunRoot $RunRoot
    return $prepared
}

function Save-CombinedStepPrepared {
    param([string]$Path,[string]$Json,[string]$RunRoot)
    $prepared = $Json | ConvertFrom-Json -ErrorAction Stop
    if ($prepared.status -cne 'PREPARED_READONLY_GATE_PASS' -or $prepared.activationAvailable -isnot [bool] -or -not $prepared.activationAvailable) { throw 'Prepare did not return PREPARED_READONLY_GATE_PASS with activationAvailable=true. No saved run was created.' }
    $run = Assert-CombinedStepRunDirectory -RunDirectory ([string]$prepared.runDirectory) -RunRoot $RunRoot
    if ([IO.Path]::GetFullPath([string]$prepared.journalPath) -cne (Join-Path $run 'journal.json')) { throw 'Prepare returned a journalPath outside its exact run directory.' }
    Assert-CombinedStepFileHash -Path $prepared.journalPath -Expected $prepared.journalSha256
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    $temporary = $Path + '.pending.' + [Guid]::NewGuid().ToString('N')
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Json)
        $stream = [IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        # Move on the same volume; a previous Prepare record must never be overwritten.
        [IO.File]::Move($temporary,$Path)
    } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force } }
}

function Invoke-CombinedStepChild {
    param([string]$Program,[string[]]$Arguments,[string]$EvidenceRoot,[string]$Action,[ValidateRange(1,3600)][int]$ProgressIntervalSeconds=30)
    $directory = Join-Path $EvidenceRoot ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($directory)
    $encoded = foreach ($argument in $Arguments) {
        if ($argument.Contains('"')) { throw 'Unexpected quote in a child-process argument.' }
        '"' + [regex]::Replace($argument,'(\\+)$','$1$1') + '"'
    }
    $started = [DateTime]::UtcNow.ToString('o')
    $stdoutPath = Join-Path $directory 'stdout.txt'
    $stderrPath = Join-Path $directory 'stderr.txt'
    $liveStdoutPath = Join-Path $directory 'live.o'
    $liveStderrPath = Join-Path $directory 'live.e'
    $childSource = $null
    $fileIndex = [Array]::IndexOf($Arguments,'-File')
    if ($fileIndex -ge 0 -and ($fileIndex+1) -lt $Arguments.Length) {
        $sourcePath = $Arguments[$fileIndex+1]
        $childSource = [ordered]@{path=[IO.Path]::GetFullPath($sourcePath);sha256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256 -ErrorAction Stop).Hash}
    }
    $launch = [ordered]@{schema='combined-host-command-launch.v1';action=$Action;program=$Program;arguments=$Arguments;startedUtc=$started;childSource=$childSource;stdoutPath=$liveStdoutPath;stderrPath=$liveStderrPath;finalStdoutPath=$stdoutPath;finalStderrPath=$stderrPath;automaticReboot=$false}
    [IO.File]::WriteAllText((Join-Path $directory 'launch.json'),($launch|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    [Console]::Error.WriteLine("Combined $Action started; evidence: $directory")
    $process = $null
    $completed = $false
    try {
        # Wait for the requested process, not detached compiler helpers in its job.
        # Direct file capture preserves available output if the caller is interrupted.
        $process = Start-Process -FilePath $Program -ArgumentList ($encoded -join ' ') -PassThru -WindowStyle Hidden -RedirectStandardOutput $liveStdoutPath -RedirectStandardError $liveStderrPath -ErrorAction Stop
        $null = $process.Handle
        [IO.File]::WriteAllText((Join-Path $directory 'process.json'),([ordered]@{schema='combined-host-command-process.v1';processId=$process.Id;startTimeUtc=$process.StartTime.ToUniversalTime().ToString('o');launchPath=(Join-Path $directory 'launch.json')}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
        $elapsed = [Diagnostics.Stopwatch]::StartNew()
        $nextProgress = $ProgressIntervalSeconds
        while (-not $process.WaitForExit(1000)) {
            if ($elapsed.Elapsed.TotalSeconds -ge $nextProgress) {
                [Console]::Error.WriteLine("Combined $Action still running ($([int]$elapsed.Elapsed.TotalSeconds)s); evidence: $directory. Do not start another action or reboot.")
                $nextProgress += $ProgressIntervalSeconds
            }
        }
        $process.WaitForExit()
        if ($null -eq $process.ExitCode) { throw 'The completed child has no exit code; success cannot be inferred.' }
        $exitCode = $process.ExitCode
        $captures = @()
        foreach ($path in @($liveStdoutPath,$liveStderrPath)) {
            $stream = [IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
            $reader = [IO.StreamReader]::new($stream,[Text.Encoding]::UTF8,$true)
            try { $captures += $reader.ReadToEnd() } finally { $reader.Dispose() }
        }
        $stdout = $captures[0]
        $stderr = $captures[1]
        [IO.File]::WriteAllText($stdoutPath,$stdout,[Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($stderrPath,$stderr,[Text.UTF8Encoding]::new($false))
        $completed = $true
    } finally {
        if (-not $completed) {
            $interrupted = [ordered]@{schema='combined-host-command-interrupted.v1';action=$Action;recordedUtc=[DateTime]::UtcNow.ToString('o');processId=$(if($null -ne $process){$process.Id}else{$null});processTerminationConfirmed=$false;nextStep='Preserve the launch, process and output files; do not retry or start rollback until this exact process and run state are reviewed.'}
            [IO.File]::WriteAllText((Join-Path $directory 'interrupted.json'),($interrupted|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
        }
        if ($null -ne $process) { $process.Dispose() }
    }
    $receipt = [ordered]@{schema='combined-host-command-step.v1';action=$Action;program=$Program;arguments=$Arguments;startedUtc=$started;completedUtc=[DateTime]::UtcNow.ToString('o');exitCode=$exitCode;stdout=[ordered]@{path=$stdoutPath;sha256=(Get-FileHash $stdoutPath).Hash};stderr=[ordered]@{path=$stderrPath;sha256=(Get-FileHash $stderrPath).Hash};automaticReboot=$false}
    [IO.File]::WriteAllText((Join-Path $directory 'command.json'),($receipt|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    if ($exitCode -ne 0) { throw "Combined $Action failed (exit $exitCode). No next action or reboot was issued. Evidence: $directory`nExact stdout:`n$stdout`nExact stderr:`n$stderr" }
    return [pscustomobject]@{stdout=$stdout;stderr=$stderr;evidenceDirectory=$directory}
}

Export-ModuleMember -Function Get-CombinedStepDefaults,Enter-CombinedStepLock,New-CombinedStepPrepareAttempt,Complete-CombinedStepPrepareAttempt,Assert-CombinedStepFileHash,Assert-CombinedStepReadbackPaths,Assert-CombinedStepRunDirectory,Read-CombinedStepPrepared,Save-CombinedStepPrepared,Invoke-CombinedStepChild
