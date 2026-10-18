param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$WorktreeRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$PathBSourceManifestPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$repository = [IO.Path]::GetFullPath($RepositoryRoot)
$worktree = [IO.Path]::GetFullPath($WorktreeRoot)
$output = [IO.Path]::GetFullPath($OutputDirectory)
$artifactBoundary = [IO.Path]::GetFullPath((Join-Path $repository 'spoof-handoff\artifacts\chp')).TrimEnd('\') + '\'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $worktree 'spoof-handoff\tools\smbios_boot'))
$mapPath = [IO.Path]::GetFullPath((Join-Path $repository 'spoof-handoff\reports\client-originals.tsv'))
$builderPath = [IO.Path]::GetFullPath($PSCommandPath)
$manifestPath = if ([string]::IsNullOrWhiteSpace($PathBSourceManifestPath)) {
    [IO.Path]::GetFullPath((Join-Path $repository 'spoof-handoff\reports\combined-9field-vm-20261018\pathb-source-tree-manifest.json'))
} else { [IO.Path]::GetFullPath($PathBSourceManifestPath) }

function Get-HostPayloadSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Get-HostPayloadSourcePins {
    param([string]$Root,[string]$WorktreePath,[string]$Builder,[string]$Map,[string]$SourceManifest)
    $records = [Collections.Generic.List[object]]::new()
    $allowedExtensions = @('.c','.h','.cpp','.hpp','.ps1','.cmake','.in')
    foreach ($item in @(Get-ChildItem -LiteralPath $Root -Recurse -Force -File -ErrorAction Stop)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point found in host SMBIOS payload source: $($item.FullName)" }
        if ($item.Name -cne 'CMakeLists.txt' -and $allowedExtensions -notcontains [IO.Path]::GetExtension($item.Name).ToLowerInvariant()) { continue }
        $relative = $item.FullName.Substring($WorktreePath.TrimEnd('\').Length + 1) -replace '\\','/'
        $records.Add([pscustomobject][ordered]@{root='worktree';relative=$relative;path=[IO.Path]::GetFullPath($item.FullName);bytes=[long]$item.Length;sha256=(Get-HostPayloadSha256 $item.FullName)})
    }
    foreach ($required in @(
        [pscustomobject]@{root='worktree';path=$Builder},
        [pscustomobject]@{root='repository';path=$Map},
        [pscustomobject]@{root='repository';path=$SourceManifest}
    )) {
        if (-not (Test-Path -LiteralPath $required.path -PathType Leaf)) { throw "Required host payload source pin is missing: $($required.path)" }
        if (@($records | Where-Object { [IO.Path]::GetFullPath([string]$_.path) -ceq [IO.Path]::GetFullPath($required.path) }).Count -gt 0) { continue }
        $base = if ($required.root -ceq 'worktree') { $WorktreePath } else { $repository }
        $relative = $required.path.Substring($base.TrimEnd('\').Length + 1) -replace '\\','/'
        $records.Add([pscustomobject][ordered]@{root=$required.root;relative=$relative;path=[IO.Path]::GetFullPath($required.path);bytes=[long](Get-Item -LiteralPath $required.path).Length;sha256=(Get-HostPayloadSha256 $required.path)})
    }
    return @($records.ToArray() | Sort-Object -Property root,relative -CaseSensitive)
}

function Get-HostPayloadSourceSetHash([object[]]$Pins) {
    $lines = @($Pins | ForEach-Object { ([string]$_.root) + '/' + ([string]$_.relative) + "`t" + ([long]$_.bytes) + "`t" + ([string]$_.sha256) })
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($lines -join "`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','') } finally { $sha.Dispose() }
}

function Assert-HostPayloadMap([string]$Map,[string]$ValuesHeader,[string]$TableHeader) {
    $rows = @(Import-Csv -LiteralPath $Map -Delimiter "`t" -ErrorAction Stop)
    $diskRows = @($rows | Where-Object { [string]$_.member -ceq 'FakeDiskSerial' -and [string]$_.device -ceq 'disk:\\.\PHYSICALDRIVE0' })
    $nicRows = @($rows | Where-Object { [string]$_.member -ceq 'FakeMacAddress' })
    if ($rows.Count -ne 12 -or $diskRows.Count -ne 1 -or $nicRows.Count -ne 4) { throw "Locked TSV scope differs from seven SMBIOS fields, one system disk, and four NICs: rows=$($rows.Count) disks=$($diskRows.Count) nics=$($nicRows.Count)" }
    $members = @('FakeSMBIOSSerial','FakeSMBIOSManufacturer','FakeSMBIOSProduct','FakeMotherboardSerial','FakeMotherboardManufacturer','FakeMotherboardProduct','FakeSMBIOSUUID')
    $source = [IO.File]::ReadAllText($ValuesHeader)
    $source = [regex]::Replace($source,'(?m)^\s*#include\s+"[^"]+"\s*$','')
    $source = [regex]::Replace($source,'(?s)/\*.*?\*/','')
    $tokens = @([regex]::Matches($source,'"([^"\r\n]*)"') | ForEach-Object { $_.Groups[1].Value })
    if ($tokens.Count -ne 21) { throw "Host SMBIOS values header has an unexpected quoted-value count: expected=21 actual=$($tokens.Count)" }
    $fieldNames = @('system_serial','system_vendor','system_product','board_serial','board_vendor','board_product','system_uuid')
    for ($index = 0; $index -lt $members.Count; $index++) {
        $matches = @($rows | Where-Object { [string]$_.member -ceq $members[$index] })
        if ($matches.Count -ne 1) { throw "Locked host map row is missing or duplicated: $($members[$index]) count=$($matches.Count)" }
        if ([string]$matches[0].original -cne [string]$tokens[$index]) { throw "Host SMBIOS original differs from locked TSV: field=$($fieldNames[$index]) expected=$([string]$matches[0].original) actual=$([string]$tokens[$index])" }
        if ([string]$matches[0].replacement -cne [string]$tokens[$index + 7]) { throw "Host SMBIOS replacement differs from locked TSV: field=$($fieldNames[$index]) expected=$([string]$matches[0].replacement) actual=$([string]$tokens[$index + 7])" }
        if ([string]$tokens[$index + 14] -cne $fieldNames[$index]) { throw "Host SMBIOS field-name order differs from the expected seven pins at index=$index actual=$([string]$tokens[$index + 14])" }
    }
    $macros = [IO.File]::ReadAllText($ValuesHeader)
    foreach ($line in @('#define SM_DISK_SERIAL_ENABLED 0','#define SM_ADAPTER_MAC_ENABLED 0','#define SM_CPU_SERIAL_ENABLED 0','#define SM_GPU_SERIAL_ENABLED 0')) {
        if ($macros -notmatch ('(?m)^' + [regex]::Escape($line) + '\s*$')) { throw "Host payload has an unexpected enabled device field or unavailable identifier setting: expected=$line" }
    }
    if ($macros -notmatch '(?m)^#define SM_ACTIVE_BOOT_MASK SM_MASK_ALL\s*$' -or
        [IO.File]::ReadAllText($TableHeader) -notmatch '(?m)^#define SM_MASK_ALL UINT32_C\(0x0000007f\)\s*$') {
        throw 'Host SMBIOS payload must select exactly the seven confirmed SMBIOS fields (mask 127).'
    }
    return [pscustomobject]@{bindingCount=$rows.Count;activeSmbiosFields=7;activeBootMask=127;deviceFieldsDisabled=$true;unavailableIdentifiersDisabled=$true;mapSha256=(Get-HostPayloadSha256 $Map)}
}

function Find-HostBuildTools {
    $systemRoot = [Environment]::GetEnvironmentVariable('SystemRoot')
    if (-not $systemRoot) { $systemRoot = [Environment]::GetEnvironmentVariable('windir') }
    $systemDrive = if ($systemRoot) { [IO.Path]::GetPathRoot($systemRoot) } else { $null }
    $programFilesRoots = @(
        [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
        [Environment]::GetEnvironmentVariable('ProgramFiles')
        [Environment]::GetEnvironmentVariable('ProgramW6432')
        [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
        [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)
        if ($systemDrive) { Join-Path $systemDrive 'Program Files (x86)' }
        if ($systemDrive) { Join-Path $systemDrive 'Program Files' }
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } | ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique
    $installations = @()
    foreach ($root in $programFilesRoots) {
        $vswhere = Join-Path $root 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
            $installations += @(& $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
            if ($LASTEXITCODE -ne 0 -or $installations.Count -eq 0) { $installations += @(& $vswhere -products '*' -property installationPath 2>$null) }
        }
        $vsRoot = Join-Path $root 'Microsoft Visual Studio'
        if (Test-Path -LiteralPath $vsRoot -PathType Container) {
            foreach ($version in @(Get-ChildItem -LiteralPath $vsRoot -Directory -ErrorAction SilentlyContinue)) { $installations += @(Get-ChildItem -LiteralPath $version.FullName -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName) }
        }
    }
    $installations = @($installations | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } | ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique)
    $vcvars = $null
    foreach ($installation in $installations) { $candidate = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'; if (Test-Path -LiteralPath $candidate -PathType Leaf) { $vcvars = $candidate; break } }
    if (-not $vcvars) { throw 'No x64 MSVC installation with vcvars64.bat was found.' }
    $commandProcessor = [Environment]::GetEnvironmentVariable('ComSpec')
    if (-not $commandProcessor) { $commandProcessor = Join-Path ([Environment]::SystemDirectory) 'cmd.exe' }
    $environmentLines = @(& $commandProcessor /d /s /c ('call "' + $vcvars + '" >nul && set'))
    if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed with exit $LASTEXITCODE." }
    foreach ($line in $environmentLines) {
        $equals = $line.IndexOf('=')
        if ($equals -le 0) { continue }
        $name = $line.Substring(0,$equals)
        if (-not $name.StartsWith('=')) { [Environment]::SetEnvironmentVariable($name,$line.Substring($equals + 1),'Process') }
    }
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    $ctest = Get-Command ctest.exe -ErrorAction SilentlyContinue
    $compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
    $ninja = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if (-not $cmake -or -not $ctest -or -not $compiler -or -not $ninja) { throw "Required build tool missing after MSVC initialization: cmake=$([bool]$cmake) ctest=$([bool]$ctest) cl=$([bool]$compiler) ninja=$([bool]$ninja)" }
    return [pscustomobject]@{cmake=[IO.Path]::GetFullPath($cmake.Source);ctest=[IO.Path]::GetFullPath($ctest.Source);compiler=[IO.Path]::GetFullPath($compiler.Source);ninja=[IO.Path]::GetFullPath($ninja.Source);vcvars=$vcvars}
}

function Invoke-LoggedBuildCommand([string]$Program,[string[]]$Arguments,[string]$LogPath) {
    $outputLines = @(& $Program @Arguments 2>&1 | ForEach-Object { [string]$_ })
    $exitCode = $LASTEXITCODE
    $outputText = $outputLines -join [Environment]::NewLine
    [IO.File]::WriteAllText($LogPath,$outputText + [Environment]::NewLine,[Text.UTF8Encoding]::new($false))
    if ($exitCode -ne 0) { throw "Build command failed with exit ${exitCode}: $Program $($Arguments -join ' '); output=$outputText; log=$LogPath" }
    return [pscustomobject]@{exitCode=$exitCode;logPath=$LogPath;logSha256=(Get-HostPayloadSha256 $LogPath);output=$outputText}
}

function Assert-HostEfiApplication([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw "Built payload does not have a DOS/PE header: $Path" }
    $offset = [BitConverter]::ToUInt32($bytes,0x3C)
    if ($offset -gt ($bytes.Length - 24) -or $bytes[$offset] -ne 0x50 -or $bytes[$offset + 1] -ne 0x45 -or $bytes[$offset + 2] -ne 0 -or $bytes[$offset + 3] -ne 0) { throw "Built payload has a malformed PE header: $Path" }
    $machine = [BitConverter]::ToUInt16($bytes,[int]$offset + 4)
    $optionalSize = [BitConverter]::ToUInt16($bytes,[int]$offset + 20)
    $optionalOffset = [int]$offset + 24
    if ($machine -ne 0x8664 -or $optionalSize -lt 70 -or ($optionalOffset + $optionalSize) -gt $bytes.Length -or [BitConverter]::ToUInt16($bytes,$optionalOffset) -ne 0x20B -or [BitConverter]::ToUInt16($bytes,$optionalOffset + 68) -ne 10) { throw "Built payload is not an x64 EFI_APPLICATION image: $Path" }
}

if (-not $output.StartsWith($artifactBoundary,[StringComparison]::OrdinalIgnoreCase)) { throw "Host payload output must remain under spoof-handoff\artifacts\chp: $output" }
if (Test-Path -LiteralPath $output) { throw "Host payload output directory already exists; choose a new unique directory: $output" }
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container) -or -not (Test-Path -LiteralPath $mapPath -PathType Leaf) -or -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Required host source, locked map, or Path-B source manifest is missing.' }

$valuesHeader = Join-Path $sourceRoot 'smbios_values.h'
$tableHeader = Join-Path $sourceRoot 'smbios_table.h'
$hostMap = Assert-HostPayloadMap $mapPath $valuesHeader $tableHeader
$initialPins = @(Get-HostPayloadSourcePins -Root $sourceRoot -WorktreePath $worktree -Builder $builderPath -Map $mapPath -SourceManifest $manifestPath)
$initialSetHash = Get-HostPayloadSourceSetHash $initialPins
[void][IO.Directory]::CreateDirectory($output)
$buildRoot = Join-Path $output 'b'
$logsRoot = Join-Path $output 'l'
[void][IO.Directory]::CreateDirectory($logsRoot)
$previousTemp = $env:TEMP
$previousTmp = $env:TMP
$temporaryRoot = Join-Path $output 't'
[void][IO.Directory]::CreateDirectory($temporaryRoot)
$env:TEMP = $temporaryRoot
$env:TMP = $temporaryRoot

try {
    $tools = Find-HostBuildTools
    $version = @(& $tools.cmake --version 2>&1 | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0 -or $version -notmatch 'cmake version\s+(\d+)\.(\d+)\.(\d+)') { throw "Could not read CMake version: $version" }
    $cmakeVersion = $Matches[1] + '.' + $Matches[2] + '.' + $Matches[3]
    $versionNumbers = @([int]$Matches[1],[int]$Matches[2],[int]$Matches[3])
    if ($versionNumbers[0] -lt 3 -or ($versionNumbers[0] -eq 3 -and $versionNumbers[1] -lt 24)) { throw "CMake 3.24 or newer is required; actual=$($versionNumbers -join '.')" }
    $cmakeCacheArguments = @('--fresh','-S',$sourceRoot,'-B',$buildRoot,'-G','Ninja',('-DCMAKE_MAKE_PROGRAM:FILEPATH='+$tools.ninja),('-DREFERENCE_REPO:PATH='+$repository),'-DHYPERSINV_VM_ONLY_SMBIOS_PROFILE:BOOL=OFF','-DHYPERSINV_VM_SMBIOS_VALUES_HEADER:FILEPATH=','-DBUILD_TESTING:BOOL=ON',('-DCMAKE_C_COMPILER:FILEPATH='+$tools.compiler),('-DCMAKE_CXX_COMPILER:FILEPATH='+$tools.compiler),'-DCMAKE_BUILD_TYPE:STRING=Release')
    $configure = Invoke-LoggedBuildCommand $tools.cmake $cmakeCacheArguments (Join-Path $logsRoot 'configure.log')
    $cachePath = Join-Path $buildRoot 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) { throw "CMake did not write its cache: $cachePath" }
    $cacheText = [IO.File]::ReadAllText($cachePath)
    foreach ($expectedLine in @('HYPERSINV_VM_ONLY_SMBIOS_PROFILE:BOOL=OFF','HYPERSINV_VM_SMBIOS_VALUES_HEADER:FILEPATH=','BUILD_TESTING:BOOL=ON','CMAKE_BUILD_TYPE:STRING=Release')) {
        if ($cacheText -notmatch ('(?m)^'+[regex]::Escape($expectedLine)+'\s*$')) { throw "Host payload CMake option mismatch or missing cache entry: $expectedLine" }
    }
    $build = Invoke-LoggedBuildCommand $tools.cmake @('--build',$buildRoot) (Join-Path $logsRoot 'build.log')
    $ctest = Invoke-LoggedBuildCommand $tools.ctest @('--test-dir',$buildRoot,'--output-on-failure') (Join-Path $logsRoot 'ctest.log')
    if ($ctest.output -notmatch '(?m)100% tests passed, 0 tests failed out of 2') { throw "Host SMBIOS CTest did not report two passing tests: $($ctest.output)" }
    $builtPayload = Join-Path $buildRoot 'efi\inventory.efi'
    if (-not (Test-Path -LiteralPath $builtPayload -PathType Leaf)) { throw "Fresh host SMBIOS EFI target is missing: $builtPayload" }
    Assert-HostEfiApplication $builtPayload
    $payloadPath = Join-Path $output 'inventory.efi'
    [IO.File]::Copy($builtPayload,$payloadPath,$false)
    Assert-HostEfiApplication $payloadPath
    $buildIdPath = Join-Path $buildRoot 'build-id.txt'
    $buildId = if (Test-Path -LiteralPath $buildIdPath -PathType Leaf) { [IO.File]::ReadAllText($buildIdPath).Trim() } else { '' }
    if ($buildId -notmatch '\A[A-Fa-f0-9]{32}\z') { throw "Fresh host payload build ID is malformed: [$buildId]" }
    $finalPins = @(Get-HostPayloadSourcePins -Root $sourceRoot -WorktreePath $worktree -Builder $builderPath -Map $mapPath -SourceManifest $manifestPath)
    $finalSetHash = Get-HostPayloadSourceSetHash $finalPins
    if ($initialSetHash -cne $finalSetHash -or $initialPins.Count -ne $finalPins.Count) { throw "Host payload source set changed during build: before=$initialSetHash after=$finalSetHash" }
    for ($index = 0; $index -lt $initialPins.Count; $index++) {
        if ($initialPins[$index].root -cne $finalPins[$index].root -or $initialPins[$index].relative -cne $finalPins[$index].relative -or $initialPins[$index].sha256 -cne $finalPins[$index].sha256) { throw "Host payload source changed during build at index=${index}: before=$($initialPins[$index].relative):$($initialPins[$index].sha256) after=$($finalPins[$index].relative):$($finalPins[$index].sha256)" }
    }
    $verdict = [pscustomobject][ordered]@{
        schema = 'combined-host-smbios-payload.v1'
        result = 'PASS'
        hostValuesProfile = 'checked-in-host-values'
        vmOnlyProfileEnabled = $false
        activeBootMask = [int]$hostMap.activeBootMask
        buildId = $buildId.ToUpperInvariant()
        outputDirectory = $output
        buildDirectory = $buildRoot
        payload = [ordered]@{path=$payloadPath;bytes=[long](Get-Item -LiteralPath $payloadPath).Length;sha256=(Get-HostPayloadSha256 $payloadPath);efiMachine='x64';efiSubsystem='EFI_APPLICATION'}
        lockedMap = [ordered]@{path=$mapPath;sha256=$hostMap.mapSha256;bindingCount=$hostMap.bindingCount;activeSmbiosFields=$hostMap.activeSmbiosFields;usbDiskTargetsExcluded=$true}
        sourceManifest = [ordered]@{path=$manifestPath;sha256=(Get-HostPayloadSha256 $manifestPath)}
        sourceSet = [ordered]@{sha256=$finalSetHash;fileCount=$finalPins.Count;pinnedFiles=$finalPins}
        configuration = [ordered]@{vmOnlyProfile='OFF';vmValuesHeader='';buildType='Release';buildTesting=$true;activeBootMask=127;diskSerialEnabled=$false;adapterMacEnabled=$false;cpuSerialEnabled=$false;gpuSerialEnabled=$false}
        ctest = [ordered]@{count=2;passed=2;failed=0;exitCode=$ctest.exitCode;logPath=$ctest.logPath;logSha256=$ctest.logSha256}
        buildCommands = @([ordered]@{name='configure';exitCode=$configure.exitCode;logPath=$configure.logPath;logSha256=$configure.logSha256},[ordered]@{name='build';exitCode=$build.exitCode;logPath=$build.logPath;logSha256=$build.logSha256})
        toolchain = [ordered]@{cmakeVersion=$cmakeVersion;cmakePath=$tools.cmake;ctestPath=$tools.ctest;compilerPath=$tools.compiler;ninjaPath=$tools.ninja;vcvarsPath=$tools.vcvars}
        liveBootProven = $false
        signingPerformed = $false
        stageOrArmPerformed = $false
        builtUtc = [DateTime]::UtcNow.ToString('o')
    }
    $verdictPath = Join-Path $output 'build-verdict.json'
    [IO.File]::WriteAllText($verdictPath,($verdict | ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
    [pscustomobject][ordered]@{status='HOST_PAYLOAD_BUILD_PASS';verdictPath=$verdictPath;verdictSha256=(Get-HostPayloadSha256 $verdictPath);payloadPath=$payloadPath;payloadSha256=$verdict.payload.sha256;buildId=$verdict.buildId;activeBootMask=$verdict.activeBootMask;ctestPassed=$verdict.ctest.passed;sourceSetSha256=$finalSetHash} | ConvertTo-Json -Depth 8 -Compress
}
catch {
    $failure = $_.Exception.ToString()
    try {
        $failurePath = Join-Path $output 'build-failure.txt'
        [IO.File]::WriteAllText($failurePath,$failure,[Text.UTF8Encoding]::new($false))
    } catch { }
    [Console]::Error.WriteLine("HOST SMBIOS PAYLOAD BUILD FAILED: $failure")
    throw
}
finally {
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
}
