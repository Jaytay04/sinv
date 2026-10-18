$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$reportsRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\reports\prepare-build-fix-20261018\tests'))
$outputRoot = Join-Path $reportsRoot ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($outputRoot)
$script:checks = New-Object System.Collections.Generic.List[string]
$failureMessage = $null

function Check([bool]$Condition,[string]$Name) {
    if (-not $Condition) { throw "FAIL: $Name" }
    $script:checks.Add($Name)
}

function Get-AstFunction([string]$Path,[string]$Name) {
    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$parseErrors)
    if ($parseErrors.Count -gt 0) { throw "Could not parse $Path`: $($parseErrors[0].Message)" }
    $definitions = @($ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $Name },$true))
    if ($definitions.Count -ne 1) { throw "Expected one $Name definition in $Path; found $($definitions.Count)." }
    return [scriptblock]::Create($definitions[0].Extent.Text)
}

function Get-AstAssignment([System.Management.Automation.Language.Ast]$Ast,[string]$VariableName) {
    $left = '$' + $VariableName
    $assignments = @($Ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq $left },$true))
    if ($assignments.Count -ne 1) { throw "Expected one assignment to $left; found $($assignments.Count)." }
    return $assignments[0]
}

function Get-HostPayloadSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Assert-Contains([string]$Value,[string]$Expected,[string]$Name) {
    Check ($Value.Contains($Expected)) $Name
}

function Write-TestFile([string]$Path,[string]$Content) {
    [IO.File]::WriteAllText($Path,$Content,[Text.UTF8Encoding]::new($false))
}

try {
    $builderPath = Join-Path $PSScriptRoot 'build-host-payload.ps1'
    $runnerPath = Join-Path $PSScriptRoot 'run.ps1'
    . (Get-AstFunction $builderPath 'Invoke-LoggedBuildCommand')
    . (Get-AstFunction $runnerPath 'Invoke-CombinedPowerShellChild')

    $builderTokens = $null
    $builderParseErrors = $null
    $builderAst = [System.Management.Automation.Language.Parser]::ParseFile($builderPath,[ref]$builderTokens,[ref]$builderParseErrors)
    if ($builderParseErrors.Count -gt 0) { throw "Could not parse builder path allocation: $($builderParseErrors[0].Message)" }
    $builderSource = [IO.File]::ReadAllText($builderPath)
    $workspaceIdAssignment = Get-AstAssignment $builderAst 'buildWorkspaceId'
    $workspaceAssignment = Get-AstAssignment $builderAst 'buildWorkspace'
    $buildRootAssignment = Get-AstAssignment $builderAst 'buildRoot'
    $temporaryRootAssignment = Get-AstAssignment $builderAst 'temporaryRoot'
    Check ($workspaceIdAssignment.Right.Extent.Text -match "Guid\]::NewGuid\(\)\.ToString\('N'\)\.Substring\(0,\s*16\)") 'build workspace ID is a fresh 16-hex GUID prefix'
    Check ($workspaceAssignment.Right.Extent.Text -match 'Join-Path\s+\$buildArtifactsRoot\s+\$buildWorkspaceId') 'unique build workspace is joined beneath artifacts/chb'
    Check ($buildRootAssignment.Right.Extent.Text -match 'Join-Path\s+\$buildWorkspace\s+''b''') 'CMake build root is nested beneath its unique workspace'
    Check ($temporaryRootAssignment.Right.Extent.Text -match 'Join-Path\s+\$buildRoot\s+''t''') 'temporary root is nested beneath the short build root'
    Check ($builderSource -match 'Test-Path\s+-LiteralPath\s+\$buildWorkspace' -and $builderSource -match 'refusing to reuse it') 'builder refuses to reuse a prior short workspace'
    Check ($builderSource -match '\$logsRoot\s*=\s*Join-Path\s+\$output\s+''l''' -and $builderSource -match '\$payloadPath\s*=\s*Join-Path\s+\$output\s+''inventory\.efi''' -and $builderSource -match 'outputDirectory\s*=\s*\$output') 'final output, payload, and log locations stay under the requested output directory'
    $worktreeForPaths = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
    $newBuildRoot = Join-Path (Join-Path (Join-Path $worktreeForPaths 'spoof-handoff\artifacts\chb') '0123456789abcdef') 'b'
    $newTemporaryRoot = Join-Path $newBuildRoot 't'
    $shortBoundary = [IO.Path]::GetFullPath((Join-Path $worktreeForPaths 'spoof-handoff')).TrimEnd('\') + '\'
    Check ($newBuildRoot.StartsWith($shortBoundary,[StringComparison]::OrdinalIgnoreCase) -and $newTemporaryRoot.StartsWith(($newBuildRoot.TrimEnd('\') + '\'),[StringComparison]::OrdinalIgnoreCase)) 'sample build and TEMP paths remain under worktree spoof-handoff'
    $previousOutput = Join-Path (Join-Path $worktreeForPaths 'spoof-handoff\artifacts\chp') '20261018T190344997Z.497a89a191114373a675eb03c7db18b8'
    $previousBuildRoot = Join-Path $previousOutput 'b'
    Check (($previousBuildRoot.Length - $newBuildRoot.Length) -ge 30) 'short build path removes at least 30 characters from the failed Prepare layout'

    $powerShell = Join-Path $PSHOME 'powershell.exe'
    if (-not (Test-Path -LiteralPath $powerShell -PathType Leaf)) { throw "Windows PowerShell executable is missing: $powerShell" }

    $probeDirectory = Join-Path $outputRoot 'helper scripts with spaces'
    [void][IO.Directory]::CreateDirectory($probeDirectory)
    $probeScript = Join-Path $probeDirectory 'stream and args helper.ps1'
    Write-TestFile $probeScript @'
param([string]$Value,[string]$Trailing,[string]$Mode)
[Console]::Out.WriteLine("OUT<$Value><$Trailing>")
[Console]::Error.WriteLine("ERR<$Value><$Trailing>")
if ($Mode -eq 'fail') { exit 23 }
'@

    $value = 'value with spaces'
    $trailing = 'C:\path with spaces\'
    $expectedLine = 'OUT<' + $value + '><' + $trailing + '>'
    $successLog = Join-Path $outputRoot 'native stderr success.log'
    $success = Invoke-LoggedBuildCommand $powerShell @('-NoProfile','-ExecutionPolicy','Bypass','-File',$probeScript,$value,$trailing,'ok') $successLog
    Check ($success.exitCode -eq 0) 'native stderr with exit zero is accepted'
    Check (Test-Path -LiteralPath $success.logPath -PathType Leaf) 'native success log exists'
    Check ((Get-HostPayloadSha256 $success.logPath) -ceq $success.logSha256) 'native success log hash matches bytes'
    Assert-Contains $success.output $expectedLine 'native arguments preserve spaces and a trailing backslash'
    Assert-Contains $success.output ('ERR<' + $value + '><' + $trailing + '>') 'native stderr is returned on exit zero'
    $successLogText = [IO.File]::ReadAllText($success.logPath)
    Assert-Contains $successLogText $expectedLine 'native stdout is fully logged'
    Assert-Contains $successLogText ('ERR<' + $value + '><' + $trailing + '>') 'native stderr is fully logged'

    $failureScript = Join-Path $probeDirectory 'nonzero helper.ps1'
    Write-TestFile $failureScript "[Console]::Out.WriteLine('EXPECTED_NONZERO_STDOUT')`r`n[Console]::Error.WriteLine('EXPECTED_NONZERO_STDERR')`r`nexit 23`r`n"
    $failureLog = Join-Path $outputRoot 'native stderr failure.log'
    $nativeFailure = $null
    try {
        Invoke-LoggedBuildCommand $powerShell @('-NoProfile','-ExecutionPolicy','Bypass','-File',$failureScript) $failureLog | Out-Null
    } catch { $nativeFailure = $_.Exception.Message }
    Check ($null -ne $nativeFailure) 'native nonzero exit fails'
    Assert-Contains $nativeFailure 'exit 23' 'native failure preserves exact root exit code'
    Assert-Contains $nativeFailure 'EXPECTED_NONZERO_STDERR' 'native failure preserves exact stderr text'
    Assert-Contains ([IO.File]::ReadAllText($failureLog)) 'EXPECTED_NONZERO_STDERR' 'native failure log retains stderr'

    $childLogDirectory = Join-Path $outputRoot 'combined child logs'
    $childSuccess = Invoke-CombinedPowerShellChild -ScriptPath $probeScript -Arguments @($value,$trailing,'ok') -LogDirectory $childLogDirectory -Name 'stderr success'
    Check ($childSuccess.exitCode -eq 0) 'combined child stderr with exit zero is accepted'
    Assert-Contains $childSuccess.stdout $expectedLine 'combined child stdout preserves quoted arguments'
    Assert-Contains $childSuccess.stderr ('ERR<' + $value + '><' + $trailing + '>') 'combined child stderr is retained separately'
    Check (Test-Path -LiteralPath $childSuccess.stdoutPath -PathType Leaf) 'combined child stdout file is retained'
    Check (Test-Path -LiteralPath $childSuccess.stderrPath -PathType Leaf) 'combined child stderr file is retained'

    $childFailure = $null
    try {
        Invoke-CombinedPowerShellChild -ScriptPath $probeScript -Arguments @($value,$trailing,'fail') -LogDirectory $childLogDirectory -Name 'stderr failure' | Out-Null
    } catch { $childFailure = $_.Exception.Message }
    Check ($null -ne $childFailure) 'combined child nonzero exit fails'
    Assert-Contains $childFailure 'exit=23' 'combined child failure preserves exact root exit code'
    Assert-Contains $childFailure ('ERR<' + $value + '><' + $trailing + '>') 'combined child failure preserves exact stderr text'

    $detachedParentScript = Join-Path $probeDirectory 'detached parent.ps1'
    Write-TestFile $detachedParentScript @'
param([string]$PowerShellPath,[string]$PidFile,[string]$ChildStdout,[string]$ChildStderr)
$child = Start-Process -FilePath $PowerShellPath -ArgumentList '-NoProfile -Command "Start-Sleep -Seconds 12"' -PassThru -WindowStyle Hidden -RedirectStandardOutput $ChildStdout -RedirectStandardError $ChildStderr
[IO.File]::WriteAllText($PidFile,[string]$child.Id)
[Console]::Out.WriteLine('PARENT_ROOT_EXIT')
'@

    foreach ($mode in @('native','combined')) {
        $detachedRoot = Join-Path $outputRoot ('detached-' + $mode + ' with spaces')
        [void][IO.Directory]::CreateDirectory($detachedRoot)
        $pidPath = Join-Path $detachedRoot 'child pid.txt'
        $childOut = Join-Path $detachedRoot 'child stdout.txt'
        $childErr = Join-Path $detachedRoot 'child stderr.txt'
        $watch = [Diagnostics.Stopwatch]::StartNew()
        try {
            if ($mode -eq 'native') {
                $detachedResult = Invoke-LoggedBuildCommand $powerShell @('-NoProfile','-ExecutionPolicy','Bypass','-File',$detachedParentScript,$powerShell,$pidPath,$childOut,$childErr) (Join-Path $detachedRoot 'root.log')
            } else {
                $detachedResult = Invoke-CombinedPowerShellChild -ScriptPath $detachedParentScript -Arguments @($powerShell,$pidPath,$childOut,$childErr) -LogDirectory $detachedRoot -Name 'root'
            }
            $watch.Stop()
            Check ($watch.Elapsed.TotalSeconds -lt 8) "$mode root process returns without waiting for its detached child"
            if ($mode -eq 'native') { Assert-Contains ([string]$detachedResult.output) 'PARENT_ROOT_EXIT' 'native root process output is captured' }
            else { Assert-Contains ([string]$detachedResult.stdout) 'PARENT_ROOT_EXIT' 'combined root process output is captured' }
            $childPid = [int]([IO.File]::ReadAllText($pidPath).Trim())
            $liveChild = Get-Process -Id $childPid -ErrorAction SilentlyContinue
            Check ($null -ne $liveChild) "$mode detached child remains live after the root exits"
        } finally {
            if (Test-Path -LiteralPath $pidPath -PathType Leaf) {
                $cleanupPid = 0
                [void][int]::TryParse(([IO.File]::ReadAllText($pidPath).Trim()),[ref]$cleanupPid)
                if ($cleanupPid -gt 0) {
                    Stop-Process -Id $cleanupPid -Force -ErrorAction SilentlyContinue
                    try { Wait-Process -Id $cleanupPid -Timeout 5 -ErrorAction SilentlyContinue } catch { }
                }
            }
        }
    }
} catch {
    $failureMessage = $_.Exception.Message
}

$result = [ordered]@{
    schema = 'combined-host-build-process-tests.v1'
    status = if ($null -eq $failureMessage) { 'PASS' } else { 'FAIL' }
    checks = @($script:checks)
    checkCount = $script:checks.Count
    failure = $failureMessage
    outputRoot = $outputRoot
    files = @(Get-ChildItem -LiteralPath $outputRoot -File -Recurse | ForEach-Object {
        [ordered]@{path=$_.FullName;length=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
    })
}
$resultPath = Join-Path $outputRoot 'result.json'
[IO.File]::WriteAllText($resultPath,($result | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$result | ConvertTo-Json -Depth 8
if ($null -ne $failureMessage) { throw $failureMessage }
