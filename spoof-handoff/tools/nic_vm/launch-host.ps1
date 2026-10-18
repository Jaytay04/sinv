[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$LaunchDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..'))
$allowed = (Join-Path $repository 'spoof-handoff\reports\nic-reader-gap-20261017\host-launches').TrimEnd('\') + '\'
$launch = [IO.Path]::GetFullPath($LaunchDirectory)
if (-not $launch.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $launch -PathType Container)) { throw 'LaunchDirectory must be a prepared NIC host-launch directory.' }
if (Test-Path -LiteralPath (Join-Path $launch 'result.json')) { throw 'This launch already has a result; refusing to repeat it.' }
$result = [ordered]@{ startedUtc = [DateTime]::UtcNow.ToString('o'); status = 'FAIL'; exitCode = $null; error = $null }
$process = $null
try {
    $request = [IO.File]::ReadAllText((Join-Path $launch 'request.json')) | ConvertFrom-Json
    if ($request.schema -cne 'nic-host-launch.v1') { throw 'Host launch request schema mismatch.' }
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    $result.elevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $result.elevated) { throw 'Host execution requires a normal elevated Windows process.' }
    $verifiedPaths = @{}
    foreach ($pin in @($request.sourcePins)) {
        $path = [IO.Path]::GetFullPath((Join-Path $repository ([string]$pin.path)))
        if (-not $path.StartsWith(($repository.TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)) { throw 'A launch source pin escaped the repository.' }
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actual -cne [string]$pin.sha256) { throw ('Launch source hash mismatch: '+$path+' expected='+$pin.sha256+' actual='+$actual) }
        $verifiedPaths[$path] = $true
    }
    $entry = Join-Path $PSScriptRoot 'host-address-roundtrip.ps1'
    $required = @($entry,(Join-Path $PSScriptRoot 'adapter-address.ps1'),(Join-Path $PSScriptRoot '..\nic_vm_probe\read-host.ps1'),(Join-Path $repository 'spoof-handoff\artifacts\nic-reader-gap-20261017\native-build-vs18\Release\nic_native_read.exe'))
    foreach ($path in $required) { if (-not $verifiedPaths.ContainsKey([IO.Path]::GetFullPath($path))) { throw ('Required launch source pin is absent: '+$path) } }
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$entry,'-Action','Run')
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = Join-Path $PSHOME 'powershell.exe'
    $info.Arguments = ($arguments | ForEach-Object { '"'+$_+'"' }) -join ' '
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw 'The reviewed host script did not start.' }
    $result.childPid = $process.Id
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    # The child owns recovery. This launcher never kills an active transaction.
    $process.WaitForExit()
    [IO.File]::WriteAllText((Join-Path $launch 'stdout.txt'),$stdout.Result,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $launch 'stderr.txt'),$stderr.Result,[Text.UTF8Encoding]::new($false))
    $result.exitCode = $process.ExitCode
    $result.status = if ($process.ExitCode -eq 0) { 'PASS' } else { 'CHILD_FAILED' }
} catch { $result.error = $_.Exception.ToString(); $result.errorPosition = $_.InvocationInfo.PositionMessage }
finally {
    if ($null -ne $process) { $process.Dispose() }
    $result.completedUtc = [DateTime]::UtcNow.ToString('o')
    [IO.File]::WriteAllText((Join-Path $launch 'result.json'),($result | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
}
if ($result.status -cne 'PASS') { exit 1 }
