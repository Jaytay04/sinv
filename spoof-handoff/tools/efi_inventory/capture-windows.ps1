param(
    [ValidateSet('Build','Capture')][string]$Mode = 'Capture',
    [string]$BuildDir = '.codex-build/efi_inventory_windows',
    [string]$VariableReport,
    [string]$IdentifierReport,
    [string]$SmbiosReport
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Resolve-LocalPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $script:RepoRoot $Path))
}
$script:InventoryBuild = Resolve-LocalPath $BuildDir
$buildBoundary = [IO.Path]::GetFullPath((Join-Path $script:RepoRoot '.codex-build')).TrimEnd([char]92) + [IO.Path]::DirectorySeparatorChar
if (-not $script:InventoryBuild.StartsWith($buildBoundary,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDir must be inside this worktree under .codex-build.'
}
# Used only by the shared toolchain-discovery loop, not to build a loader.
$script:LoaderBuild = $script:InventoryBuild

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    if (-not $Executable) { throw 'Required CMake/CTest executable was not found.' }
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed (exit $LASTEXITCODE)." }
}

function Assert-CMakeCache([string]$BuildDirectory, [hashtable]$ExpectedValues, [string[]]$PathValues) {
    $cache = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) { throw "CMake did not create its cache: $cache" }
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $cache) {
        $equals = $line.IndexOf('=')
        $colon = $line.IndexOf(':')
        if ($equals -le 0 -or $colon -le 0 -or $colon -ge $equals) { continue }
        $values[$line.Substring(0,$colon)] = $line.Substring($equals + 1)
    }
    foreach ($name in $ExpectedValues.Keys) {
        if (-not $values.ContainsKey($name)) { throw "CMake cache is missing required setting $name in $cache" }
        $expected = [string]$ExpectedValues[$name]
        $actual = [string]$values[$name]
        if ($PathValues -contains $name) {
            $expected = [IO.Path]::GetFullPath($expected)
            $actual = [IO.Path]::GetFullPath($actual)
            if (-not [string]::Equals($expected,$actual,[StringComparison]::OrdinalIgnoreCase)) {
                throw "CMake cache $name mismatch in $cache. Expected '$expected'; found '$actual'."
            }
        } elseif ($actual -cne $expected) {
            throw "CMake cache $name mismatch in $cache. Expected '$expected'; found '$actual'."
        }
    }
}


function Invoke-Build {
    $systemRoot = [Environment]::GetEnvironmentVariable('SystemRoot')
    if (-not $systemRoot) { $systemRoot = [Environment]::GetEnvironmentVariable('windir') }
    $systemDriveRoot = if ($systemRoot) { [IO.Path]::GetPathRoot($systemRoot) } else { $null }
    $programFilesRoots = @(
        [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
        [Environment]::GetEnvironmentVariable('ProgramFiles')
        [Environment]::GetEnvironmentVariable('ProgramW6432')
        [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
        [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)
        if ($systemDriveRoot) { Join-Path $systemDriveRoot 'Program Files (x86)' }
        if ($systemDriveRoot) { Join-Path $systemDriveRoot 'Program Files' }
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } |
        ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique

    $vswhere = $null
    foreach ($programFilesRoot in $programFilesRoots) {
        $candidate = Join-Path $programFilesRoot 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $vswhere = $candidate
            break
        }
    }
    $installations = @()
    if ($vswhere) {
        $identified = @(& $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
        if ($LASTEXITCODE -ne 0 -or $identified.Count -eq 0) {
            $identified = @(& $vswhere -products '*' -property installationPath 2>$null)
        }
        $installations += $identified
    }
    # vswhere can omit installations in restricted/partial environments. Probe
    # only known Visual Studio version/edition directory levels as a fallback.
    foreach ($programFilesRoot in $programFilesRoots) {
        $vsRoot = Join-Path $programFilesRoot 'Microsoft Visual Studio'
        if (-not (Test-Path -LiteralPath $vsRoot -PathType Container)) { continue }
        foreach ($version in (Get-ChildItem -LiteralPath $vsRoot -Directory -ErrorAction SilentlyContinue)) {
            $installations += @(Get-ChildItem -LiteralPath $version.FullName -Directory -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty FullName)
        }
    }
    $installations = @($installations | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } |
        ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique)
    $vcvars = $null
    foreach ($installation in $installations) {
        if (-not $installation) { continue }
        $candidate = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path -LiteralPath $candidate) {
            $vcvars = $candidate
            break
        }
    }
    if (-not $vcvars) {
        throw 'No x64 MSVC installation with vcvars64.bat was found.'
    }

    $ninja = $null
    foreach ($buildDirectory in @($script:InventoryBuild,$script:LoaderBuild)) {
        $cache = Join-Path $buildDirectory 'CMakeCache.txt'
        if (Test-Path -LiteralPath $cache) {
            $cacheLine = Get-Content -LiteralPath $cache |
                Where-Object { $_ -match '^CMAKE_MAKE_PROGRAM:FILEPATH=' } |
                Select-Object -First 1
            if ($cacheLine) {
                $candidate = $cacheLine.Substring($cacheLine.IndexOf('=') + 1)
                if (Test-Path -LiteralPath $candidate) { $ninja = $candidate; break }
            }
        }
    }
    if (-not $ninja) {
        $command = Get-Command ninja.exe -ErrorAction SilentlyContinue
        if ($command) { $ninja = $command.Source }
    }
    if (-not $ninja) {
        foreach ($installation in $installations) {
            if (-not $installation) { continue }
            $candidate = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
            if (Test-Path -LiteralPath $candidate) {
                $ninja = $candidate
                break
            }
        }
    }
    if (-not $ninja) {
        throw 'Ninja was not found on PATH, in the existing inventory build cache, or in Visual Studio.'
    }

    $commandProcessor = [Environment]::GetEnvironmentVariable('ComSpec')
    if (-not $commandProcessor) {
        $commandProcessor = Join-Path ([Environment]::SystemDirectory) 'cmd.exe'
    }
    if (-not (Test-Path -LiteralPath $commandProcessor -PathType Leaf)) {
        throw "Windows command processor was not found: $commandProcessor"
    }
    $envLines = & $commandProcessor /d /s /c ('call "' + $vcvars + '" >nul && set')
    if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed (exit $LASTEXITCODE)." }
    foreach ($line in $envLines) {
        $equals = $line.IndexOf('=')
        if ($equals -le 0) { continue }
        $name = $line.Substring(0, $equals)
        if ($name.StartsWith('=')) { continue }
        [Environment]::SetEnvironmentVariable($name, $line.Substring($equals + 1), 'Process')
    }
    $ninjaDir = Split-Path -Parent $ninja
    if (($env:PATH -split ';') -notcontains $ninjaDir) {
        $env:PATH = $ninjaDir + ';' + $env:PATH
    }
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    $ctest = Get-Command ctest.exe -ErrorAction SilentlyContinue
    if (-not $cmake -or -not $ctest) { throw 'CMake or CTest was not found on PATH.' }
    $versionOutput = & $cmake.Source --version
    $versionText = $versionOutput -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0 -or $versionText -notmatch 'cmake version\s+(\d+)\.(\d+)\.(\d+)') {
        throw 'Could not determine the installed CMake version.'
    }
    $cmakeMajor = [int]$Matches[1]
    $cmakeMinor = [int]$Matches[2]
    if ($cmakeMajor -lt 3 -or ($cmakeMajor -eq 3 -and $cmakeMinor -lt 24)) {
        throw 'Build requires CMake 3.24 or newer for isolated --fresh configuration.'
    }
    $compilerCommand = Get-Command cl.exe -ErrorAction SilentlyContinue
    if (-not $compilerCommand -or -not $compilerCommand.Source) { throw 'MSVC cl.exe was not found after loading vcvars64.bat.' }
    $compiler = [IO.Path]::GetFullPath($compilerCommand.Source)


    New-Item -ItemType Directory -Path $script:InventoryBuild -Force | Out-Null
    Invoke-Checked $cmake.Source @('--fresh','-S',(Join-Path $script:RepoRoot 'tools/efi_inventory'),
        '-B',$script:InventoryBuild,'-G','Ninja',('-DCMAKE_MAKE_PROGRAM:FILEPATH=' + $ninja),
        ('-DCMAKE_CXX_COMPILER:FILEPATH=' + $compiler),'-DCMAKE_BUILD_TYPE:STRING=Release',
        '-DBUILD_TESTING:BOOL=ON','-DEFI_INVENTORY_BUILD_COLLECTOR:BOOL=OFF')
    Assert-CMakeCache $script:InventoryBuild @{
        CMAKE_BUILD_TYPE = 'Release'
        EFI_INVENTORY_BUILD_COLLECTOR = 'OFF'
        BUILD_TESTING = 'ON'
        CMAKE_HOME_DIRECTORY = (Join-Path $script:RepoRoot 'tools/efi_inventory')
    } @('CMAKE_HOME_DIRECTORY')
    Invoke-Checked $cmake.Source @('--build',$script:InventoryBuild)
    Invoke-Checked $ctest.Source @('--test-dir',$script:InventoryBuild,'--output-on-failure')
    Write-Output ('WINDOWS-ONLY BUILD READY: ' + (Join-Path $script:InventoryBuild 'efi_inventory.exe'))
}


if ($Mode -eq 'Build') { Invoke-Build; exit 0 }
$tool = Join-Path $script:InventoryBuild 'efi_inventory.exe'
if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw 'Run capture-windows.ps1 Build first.' }
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
if (-not $VariableReport -or -not $IdentifierReport) {
    $captureDir = Join-Path $script:InventoryBuild 'captures'
    New-Item -ItemType Directory -Path $captureDir -Force | Out-Null
    if (-not $VariableReport) { $VariableReport = Join-Path $captureDir ('host-vars.' + $stamp + '.txt') }
    if (-not $IdentifierReport) { $IdentifierReport = Join-Path $captureDir ('host-identifiers.' + $stamp + '.txt') }
}
$arguments = @('capture-windows',(Resolve-LocalPath $VariableReport),(Resolve-LocalPath $IdentifierReport))
if ($SmbiosReport) { $arguments += Resolve-LocalPath $SmbiosReport }
& $tool @arguments
if ($LASTEXITCODE -ne 0) { throw "Windows capture failed (exit $LASTEXITCODE). No identifiers were changed." }
