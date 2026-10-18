[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$VmxPath,
    [Parameter(Mandatory = $true)][string]$GuestScriptPath,
    [Parameter(Mandatory = $true)][string]$CompanionExePath,
    [string]$OutputDirectory = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\spoof-handoff\reports\combined-9field-vm-20261018',
    [ValidateRange(30, 280)][int]$GuestTimeoutSeconds = 240,
    [ValidateRange(30, 300)][int]$WaitTimeoutSeconds = 300,
    [ValidateRange(15, 180)][int]$WorkerReadyTimeoutSeconds = 90,
    [switch]$PreflightOnly,
    [string]$ConfigPath = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\tools\host-runner\host-runner.config.json'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$vmRoot = Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018'
$expectedVmx = Join-Path $vmRoot 'DeviceFilter-Win11-Combined\DeviceFilter-Win11-Combined.vmx'
$expectedOutput = Join-Path $mainRoot 'spoof-handoff\reports\combined-9field-vm-20261018'
$sourceMailbox = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\device_vm\invoke-mailbox.ps1'))

function Assert-ExactPath([string]$Actual, [string]$Expected, [string]$Label) {
    $actualFull = [IO.Path]::GetFullPath($Actual)
    $expectedFull = [IO.Path]::GetFullPath($Expected)
    if (-not $actualFull.Equals($expectedFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be the exact authorized path: $expectedFull"
    }
}

Assert-ExactPath $VmxPath $expectedVmx 'VMX'
Assert-ExactPath $OutputDirectory $expectedOutput 'OutputDirectory'
foreach ($input in @(
    [pscustomobject]@{ Path = $GuestScriptPath; Name = 'GuestScriptPath' },
    [pscustomobject]@{ Path = $CompanionExePath; Name = 'CompanionExePath' },
    [pscustomobject]@{ Path = $ConfigPath; Name = 'ConfigPath' }
)) {
    $full = [IO.Path]::GetFullPath($input.Path)
    $root = [IO.Path]::GetFullPath($mainRoot).TrimEnd('\') + '\'
    if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$($input.Name) must remain inside the main repository."
    }
}
if ([IO.Path]::GetFileName($CompanionExePath) -cne 'ib-tcg-dump.exe') {
    throw 'CompanionExePath must identify the supplied ib-tcg-dump.exe.'
}
foreach ($path in @($expectedVmx, $sourceMailbox, $GuestScriptPath, $CompanionExePath, $ConfigPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required input is missing: $path" }
}

$sourceText = ([IO.File]::ReadAllText($sourceMailbox)).Replace("`r`n", "`n")
$sourceHash = (Get-FileHash -LiteralPath $sourceMailbox -Algorithm SHA256).Hash.ToUpperInvariant()
$runtimeText = $sourceText
$patches = [Collections.Generic.List[object]]::new()

function Replace-ExactlyOnce([string]$Old, [string]$New, [string]$Label) {
    $script:runtimeText = $script:runtimeText.Replace($Old, $New)
    $count = ([regex]::Matches($script:sourceText, [regex]::Escape($Old))).Count
    if ($count -ne 1) { throw "Pinned mailbox source drift for $Label; expected one exact match, found $count." }
    $script:patches.Add([pscustomobject]@{ label = $Label; old = $Old; new = $New })
}

$oldNamespace = '$script:EvidenceNamespace = if ($TestProfile -ceq ''Nic'') { ''nic-vm-20261017'' } else { ''device-vm-20261017'' }'
$newNamespace = '$script:EvidenceNamespace = ''combined-9field-vm-20261018'''
Replace-ExactlyOnce $oldNamespace $newNamespace 'report namespace'
$oldRoot = @'
Join-Path $script:MainRepoRoot `
    ('spoof-handoff\artifacts\' + $script:EvidenceNamespace)
'@
$newRoot = @'
Join-Path $script:MainRepoRoot `
    'spoof-handoff\artifacts\combined-vm-20261018'
'@
Replace-ExactlyOnce $oldRoot.TrimEnd("`r", "`n") $newRoot.TrimEnd("`r", "`n") 'VM root'
$oldFolderGuard = '$VmDirectoryName -cnotmatch ''^vm-[A-Za-z0-9._-]{1,80}$'''
$newFolderGuard = '$VmDirectoryName -cne ''DeviceFilter-Win11-Combined'''
Replace-ExactlyOnce $oldFolderGuard $newFolderGuard 'VM folder identity'

$runtimeRoot = Join-Path $expectedOutput 'runtime'
[void][IO.Directory]::CreateDirectory($runtimeRoot)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$runtimePath = Join-Path $runtimeRoot "invoke-mailbox.$stamp.$([Guid]::NewGuid().ToString('N')).ps1"
[IO.File]::WriteAllText($runtimePath, $runtimeText, [Text.UTF8Encoding]::new($false))
$runtimeHash = (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash.ToUpperInvariant()
$tokens = $null
$parseErrors = $null
[void][Management.Automation.Language.Parser]::ParseFile($runtimePath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw ('Generated mailbox has syntax errors: ' + ($parseErrors.Message -join '; ')) }

if ($PreflightOnly) {
    $preflightPath = Join-Path $runtimeRoot "mailbox-preflight.$stamp.json"
    [ordered]@{
        schema = 'combined-vm-mailbox-preflight.v1'
        sourcePath = $sourceMailbox
        sourceSha256 = $sourceHash
        runtimePath = $runtimePath
        runtimeSha256 = $runtimeHash
        patches = $patches.ToArray()
        vmxPath = $expectedVmx
        vmxSha256 = (Get-FileHash -LiteralPath $expectedVmx -Algorithm SHA256).Hash.ToUpperInvariant()
        outputDirectory = $expectedOutput
        guestScriptPath = [IO.Path]::GetFullPath($GuestScriptPath)
        companionPath = [IO.Path]::GetFullPath($CompanionExePath)
        preflightOnly = $true
        recordedUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $preflightPath -Encoding utf8
    Write-Output "MAILBOX PREFLIGHT PASS: $preflightPath"
    return
}

function Invoke-WorkerStatusRead([string]$VmrunPath, [string]$Vmx) {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $VmrunPath
    $startInfo.Arguments = '-T ws readVariable "' + $Vmx.Replace('"', '\"') + '" guestVar uefiloader.worker.status'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'VMware worker status query did not start.' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(10000)) {
            $process.Kill()
            $process.WaitForExit()
            return [pscustomobject]@{ exitCode=-1; stdout=''; stderr='VMware worker status query timed out after 10 seconds.' }
        }
        return [pscustomobject]@{
            exitCode = [int]$process.ExitCode
            stdout = [string]$stdoutTask.Result
            stderr = [string]$stderrTask.Result
        }
    }
    finally { $process.Dispose() }
}

function Wait-ForAuthenticatedWorkerSignal([string]$VmrunPath, [string]$Vmx, [int]$TimeoutSeconds, [string]$RuntimeDirectory) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $checks = [Collections.Generic.List[object]]::new()
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        $started = [DateTime]::UtcNow
        $probe = Invoke-WorkerStatusRead $VmrunPath $Vmx
        $record = [ordered]@{
            queriedUtc = $started.ToString('o')
            completedUtc = [DateTime]::UtcNow.ToString('o')
            exitCode = [int]$probe.exitCode
            workerState = $null
            workerSid = $null
            updatedUtc = $null
            statusParseError = $null
            error = if ($probe.exitCode -ne 0) { [string]$probe.stderr } else { $null }
        }
        $encoded = ([string]$probe.stdout).Trim()
        if ($probe.exitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($encoded) -and $encoded -cne 'MA==') {
            try {
                $statusBytes = [Convert]::FromBase64String($encoded)
                try {
                    $status = [Text.Encoding]::UTF8.GetString($statusBytes) | ConvertFrom-Json -ErrorAction Stop
                    $record.workerState = [string]$status.State
                    $record.workerSid = [string]$status.WorkerSid
                    $record.updatedUtc = [string]$status.UpdatedUtc
                    $ready = [int]$status.SchemaVersion -eq 1 -and
                        [string]$status.State -ceq 'READY' -and
                        [string]$status.WorkerSid -ceq 'S-1-5-18'
                }
                finally { [Array]::Clear($statusBytes, 0, $statusBytes.Length) }
            }
            catch { $record.statusParseError = $_.Exception.Message }
        }
        $checks.Add([pscustomobject]$record)
        if ($ready) { break }
        Start-Sleep -Seconds 2
    }

    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
    $path = Join-Path $RuntimeDirectory "worker-ready.$stamp.$([Guid]::NewGuid().ToString('N')).json"
    $evidence = [ordered]@{
        schema = 'combined-vm-worker-readiness.v1'
        vmxPath = $Vmx
        statusVariable = 'uefiloader.worker.status'
        ready = [bool]$ready
        timeoutSeconds = $TimeoutSeconds
        startedUtc = if ($checks.Count -gt 0) { [string]$checks[0].queriedUtc } else { [DateTime]::UtcNow.ToString('o') }
        completedUtc = [DateTime]::UtcNow.ToString('o')
        checks = @($checks.ToArray())
        hmacValueRecorded = $false
    }
    $evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding utf8
    if (-not $ready) { throw "Authenticated guest worker did not report READY within $TimeoutSeconds seconds; readiness evidence=$path" }
    return $path
}

$workerVmrun = [string]((Get-Content -LiteralPath $ConfigPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop).Tools.VmrunPath)
if ([string]::IsNullOrWhiteSpace($workerVmrun) -or -not (Test-Path -LiteralPath $workerVmrun -PathType Leaf)) {
    throw 'VMware vmrun path from the host-runner configuration is missing or invalid.'
}
$workerReadyEvidence = Wait-ForAuthenticatedWorkerSignal -VmrunPath $workerVmrun -Vmx $expectedVmx -TimeoutSeconds $WorkerReadyTimeoutSeconds -RuntimeDirectory $runtimeRoot
Write-Output "GUEST WORKER READY: $workerReadyEvidence"

$childArgs = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runtimePath,
    '-VmxPath', $expectedVmx,
    '-GuestScriptPath', [IO.Path]::GetFullPath($GuestScriptPath),
    '-CompanionExePath', [IO.Path]::GetFullPath($CompanionExePath),
    '-OutputDirectory', $expectedOutput,
    '-TestProfile', 'Nic', '-Action', 'Probe',
    '-ExpectedProbeVerdict', 'NOT_MET',
    '-GuestTimeoutSeconds', [string]$GuestTimeoutSeconds,
    '-WaitTimeoutSeconds', [string]$WaitTimeoutSeconds,
    '-ConfigPath', [IO.Path]::GetFullPath($ConfigPath)
)
$childOutput = @(& (Join-Path $PSHOME 'powershell.exe') @childArgs 2>&1 | ForEach-Object { [string]$_ })
$childExit = $LASTEXITCODE
$childOutput | ForEach-Object { Write-Output $_ }

$runDirectory = $null
foreach ($line in $childOutput) {
    if ($line -match '^Evidence:\s*(.+)$') { $runDirectory = [IO.Path]::GetFullPath($Matches[1].Trim()) }
}
if (-not $runDirectory -and (Test-Path -LiteralPath $expectedOutput -PathType Container)) {
    $runDirectory = Get-ChildItem -LiteralPath $expectedOutput -Directory |
        Where-Object { $_.Name -match '^uefi-windows-probe-' } |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
$metadata = [ordered]@{
    schema = 'combined-vm-mailbox-wrapper.v1'
    sourcePath = $sourceMailbox
    sourceSha256 = $sourceHash
    runtimePath = $runtimePath
    runtimeSha256 = $runtimeHash
    patches = $patches.ToArray()
    vmxPath = $expectedVmx
    vmxSha256 = (Get-FileHash -LiteralPath $expectedVmx -Algorithm SHA256).Hash.ToUpperInvariant()
    guestScriptPath = [IO.Path]::GetFullPath($GuestScriptPath)
    guestScriptSha256 = (Get-FileHash -LiteralPath $GuestScriptPath -Algorithm SHA256).Hash.ToUpperInvariant()
    companionPath = [IO.Path]::GetFullPath($CompanionExePath)
    companionSha256 = (Get-FileHash -LiteralPath $CompanionExePath -Algorithm SHA256).Hash.ToUpperInvariant()
    childExitCode = [int]$childExit
    childOutput = $childOutput
    runDirectory = $runDirectory
    completedUtc = [DateTime]::UtcNow.ToString('o')
}
$metadataRoot = if ($runDirectory -and (Test-Path -LiteralPath $runDirectory -PathType Container)) { $runDirectory } else { $expectedOutput }
$metadataPath = Join-Path $metadataRoot "mailbox-wrapper.$stamp.json"
$metadata | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $metadataPath -Encoding utf8
if ($childExit -ne 0) { throw "Combined VM mailbox invocation failed ($childExit); evidence=$metadataPath" }
Write-Output "MAILBOX WRAPPER: $metadataPath"
