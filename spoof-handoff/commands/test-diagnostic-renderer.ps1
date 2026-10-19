param()
$ErrorActionPreference = 'Stop'
$worktree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build = Join-Path $worktree 'spoof-handoff\b\or'
$reports = Join-Path $worktree 'spoof-handoff\reports\diagnostic-renderer'
New-Item -ItemType Directory -Force -Path $reports | Out-Null
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Run from a Visual Studio x64 developer PowerShell or vcvars64 environment.'
}

function Invoke-LoggedTool([string]$Name, [string]$Tool, [string[]]$Arguments) {
    $toolPath = (Get-Command $Tool -ErrorAction Stop).Source
    # Quote every argument explicitly for the native Windows command line.
    $quoted = ($Arguments | ForEach-Object {
        if ($_ -match '"') { throw 'Unexpected quote in build argument.' }
        '"' + $_ + '"'
    }) -join ' '
    $stdout = Join-Path $reports ($Name + '.stdout.txt')
    $stderr = Join-Path $reports ($Name + '.stderr.txt')
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = $toolPath
    $process.StartInfo.Arguments = $quoted
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    if (-not $process.Start()) { throw ('Could not start ' + $Name) }
    $outputRead = $process.StandardOutput.ReadToEndAsync()
    $errorRead = $process.StandardError.ReadToEndAsync()
    # Wait only for this owned process, while draining both pipes concurrently.
    if (-not $process.WaitForExit(120000)) {
        $process.Kill()
        throw ($Name + ' timed out after 120 seconds; evidence=' + $reports)
    }
    if (-not $outputRead.Wait(5000) -or -not $errorRead.Wait(5000)) {
        throw ($Name + ' output capture timed out; evidence=' + $reports)
    }
    [IO.File]::WriteAllText($stdout, $outputRead.Result)
    [IO.File]::WriteAllText($stderr, $errorRead.Result)
    $process.ExitCode | Set-Content -LiteralPath (Join-Path $reports ($Name + '.exitcode.txt'))
    Get-Content -LiteralPath $stdout
    if ($process.ExitCode -ne 0) {
        Get-Content -LiteralPath $stderr
        throw ($Name + ' failed, exit=' + $process.ExitCode + '; evidence=' + $reports)
    }
}

Invoke-LoggedTool 'configure' 'cmake.exe' @(
    '-S', (Join-Path $worktree 'components\hypervisor'), '-B', $build,
    '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
    '-DHYPERSINV_BUILD_CORE=OFF', '-DHYPERSINV_BUILD_DRIVER=OFF',
    '-DHYPERSINV_BUILD_LOADER=OFF', '-DHYPERSINV_BUILD_UEFI=ON',
    '-DHYPERSINV_BUILD_EFI_RUNTIME_COMPANION=OFF',
    '-DHYPERSINV_BUILD_EFI_RUNTIME_SERVICE=OFF')
Invoke-LoggedTool 'build' 'cmake.exe' @('--build', $build, '--parallel', '4')
$dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
Invoke-LoggedTool 'resident-audit' 'powershell.exe' @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    (Join-Path $worktree 'components\hypervisor\src\efi\tests\Test-EfiRuntimeScannerImage.ps1'),
    '-ImagePath', (Join-Path $build 'src\efi\uefi_hypervisor.efi'),
    '-DumpbinPath', $dumpbin, '-MaximumPages', '16')
Invoke-LoggedTool 'ctest' 'ctest.exe' @('--test-dir', $build, '--output-on-failure')
Write-Output ('STATIC_RENDERER_VALIDATION_PASS; evidence=' + $reports)
