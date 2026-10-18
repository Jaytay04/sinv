[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Prepare','EnableTestMode','InstallTransparent','VerifyTransparent','Remove','VerifyOriginal','InstallReplacement','VerifyReplacement','ArmCombined','CollectCombined','RestorePolicy','FinalVerify','Status')][string]$Action,
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$WorktreeRoot,
    [string]$VmGatePath,
    [string]$SmbiosReaderPath,
    [string]$ReleaseDirectory,
    [string]$RecoveryDirectory='E:\CodexBackup\Recovery\hsinv-storage-test',
    [string]$RunDirectory,
    [string]$NicReaderPath,
    [string]$NicNativeReaderPath,
    [switch]$LibraryOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$script:CombinedHostSchema = 'combined-host-preparation.v1'
$script:CombinedPathBSourceManifestRelative = 'spoof-handoff/reports/combined-9field-vm-20261018/pathb-source-tree-manifest.json'
$script:CombinedRequiredSourcePins = @(
    'spoof-handoff/reports/client-originals.tsv',
    'tools/combined_host/model.psm1',
    'tools/combined_host/readback.ps1',
    'tools/combined_host/run.ps1',
    'tools/combined_host/build-host-payload.ps1',
    'tools/smbios_boot/boot_report.h',
    'tools/smbios_boot/efi_smbios_diagnostic.h',
    'tools/smbios_boot/CMakeLists.txt',
    'tools/smbios_boot/smbios_table.c',
    'tools/smbios_boot/smbios_table.h',
    'tools/smbios_boot/smbios_values.h',
    'tools/combined_vm/build-smbios-fixture.ps1',
    'tools/combined_host/nic-transaction.ps1',
    'tools/storage_host/run.ps1',
    'tools/storage_host/read.ps1',
    'tools/nic_vm_probe/read-host.ps1',
    'tools/nic_vm/host-address-roundtrip.ps1',
    'tools/smbios_boot/boot_variant.c',
    'tools/smbios_boot/efi_smbios.c',
    'tools/smbios_boot/windows_readback.cpp',
    'tools/device_vm/invoke-mailbox.ps1',
    'tools/efi_inventory/path-b-collector.ps1',
    'spoof-handoff/reports/combined-9field-vm-20261018/pathb-source-tree-manifest.json'
)

Import-Module (Join-Path $PSScriptRoot 'model.psm1') -Force
. (Join-Path $PSScriptRoot 'readback.ps1')

function Get-CombinedHostSha256 {
    param([Parameter(Mandatory=$true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Get-CombinedHostPayloadSourcePins {
    param([Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$Worktree)
    $worktreeRoot = [IO.Path]::GetFullPath($Worktree).TrimEnd('\') + '\'
    $repositoryRoot = [IO.Path]::GetFullPath($Repository).TrimEnd('\') + '\'
    $sourceRoot = [IO.Path]::GetFullPath((Join-Path $Worktree 'spoof-handoff\tools\smbios_boot'))
    $records = [Collections.Generic.List[object]]::new()
    $extensions = @('.c','.h','.cpp','.hpp','.ps1','.cmake','.in')
    foreach ($item in @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force -File -ErrorAction Stop)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point found in host payload source tree: $($item.FullName)" }
        if ($item.Name -cne 'CMakeLists.txt' -and $extensions -notcontains [IO.Path]::GetExtension($item.Name).ToLowerInvariant()) { continue }
        $relative = $item.FullName.Substring($worktreeRoot.Length) -replace '\\','/'
        $records.Add([pscustomobject][ordered]@{root='worktree';relative=$relative;path=[IO.Path]::GetFullPath($item.FullName);bytes=[long]$item.Length;sha256=(Get-CombinedHostSha256 $item.FullName)})
    }
    foreach ($source in @(
        [pscustomobject]@{root='worktree';path=(Join-Path $Worktree 'spoof-handoff\tools\combined_host\build-host-payload.ps1')},
        [pscustomobject]@{root='repository';path=(Join-Path $Repository 'spoof-handoff\reports\client-originals.tsv')},
        [pscustomobject]@{root='repository';path=(Join-Path $Repository $script:CombinedPathBSourceManifestRelative.Replace('/','\'))}
    )) {
        $fullPath = [IO.Path]::GetFullPath($source.path)
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { throw "Host payload source is missing: $fullPath" }
        if (@($records | Where-Object { [IO.Path]::GetFullPath([string]$_.path) -ceq $fullPath }).Count -gt 0) { continue }
        $base = if ($source.root -ceq 'worktree') { $worktreeRoot } else { $repositoryRoot }
        if (-not $fullPath.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)) { throw "Host payload source escaped its pinned root: $fullPath" }
        $relative = $fullPath.Substring($base.Length) -replace '\\','/'
        $records.Add([pscustomobject][ordered]@{root=$source.root;relative=$relative;path=$fullPath;bytes=[long](Get-Item -LiteralPath $fullPath).Length;sha256=(Get-CombinedHostSha256 $fullPath)})
    }
    return @($records.ToArray() | Sort-Object -Property root,relative -CaseSensitive)
}

function Get-CombinedHostPayloadSourceSetHash {
    param([Parameter(Mandatory=$true)][object[]]$Pins)
    $lines = @($Pins | ForEach-Object { ([string]$_.root) + '/' + ([string]$_.relative) + "`t" + ([long]$_.bytes) + "`t" + ([string]$_.sha256) })
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($lines -join "`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','') } finally { $sha.Dispose() }
}

function Assert-CombinedHostEfiApplication {
    param([Parameter(Mandatory=$true)][string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw "Host SMBIOS collector lacks a DOS/PE header: $Path" }
    $offset = [int][BitConverter]::ToUInt32($bytes,0x3C)
    if ($offset -gt ($bytes.Length - 24) -or $bytes[$offset] -ne 0x50 -or $bytes[$offset + 1] -ne 0x45 -or $bytes[$offset + 2] -ne 0 -or $bytes[$offset + 3] -ne 0) { throw "Host SMBIOS collector has a malformed PE header: $Path" }
    $machine = [BitConverter]::ToUInt16($bytes,$offset + 4)
    $optionalSize = [BitConverter]::ToUInt16($bytes,$offset + 20)
    $optionalOffset = $offset + 24
    if ($machine -ne 0x8664 -or $optionalSize -lt 70 -or ($optionalOffset + $optionalSize) -gt $bytes.Length -or [BitConverter]::ToUInt16($bytes,$optionalOffset) -ne 0x20B -or [BitConverter]::ToUInt16($bytes,$optionalOffset + 68) -ne 10) { throw "Host SMBIOS collector is not an x64 EFI_APPLICATION image: $Path" }
}

function Assert-CombinedHostPayload {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][object]$Reference,[Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$Worktree)
    $verdictPath = [IO.Path]::GetFullPath([string](Get-CombinedValue $Reference 'verdictPath'))
    $payloadPath = [IO.Path]::GetFullPath([string](Get-CombinedValue $Reference 'payloadPath'))
    $artifactRoot = [IO.Path]::GetFullPath((Join-Path $Repository 'spoof-handoff\artifacts\chp')).TrimEnd('\') + '\'
    if (-not $verdictPath.StartsWith($artifactRoot,[StringComparison]::OrdinalIgnoreCase) -or -not $payloadPath.StartsWith($artifactRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Prepared host payload verdict and binary must remain under spoof-handoff\artifacts\chp.' }
    if (-not (Test-Path -LiteralPath $verdictPath -PathType Leaf) -or -not (Test-Path -LiteralPath $payloadPath -PathType Leaf)) { throw 'Prepared fresh host payload verdict or binary is missing.' }
    $verdictHash = Get-CombinedHostSha256 $verdictPath
    $payloadHash = Get-CombinedHostSha256 $payloadPath
    if ($verdictHash -cne ([string](Get-CombinedValue $Reference 'verdictSha256')).ToUpperInvariant()) { throw "Fresh host payload verdict changed after Prepare: expected=$([string](Get-CombinedValue $Reference 'verdictSha256')) actual=$verdictHash" }
    if ($payloadHash -cne ([string](Get-CombinedValue $Reference 'payloadSha256')).ToUpperInvariant()) { throw "Fresh host payload binary changed after Prepare: expected=$([string](Get-CombinedValue $Reference 'payloadSha256')) actual=$payloadHash" }
    $verdict = Get-Content -LiteralPath $verdictPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ([string]$verdict.schema -cne 'combined-host-smbios-payload.v1' -or [string]$verdict.result -cne 'PASS' -or [bool]$verdict.vmOnlyProfileEnabled -or [int]$verdict.activeBootMask -ne 127 -or [string]$verdict.buildId -cne [string](Get-CombinedValue $Reference 'buildId')) { throw 'Fresh host payload verdict schema/result/profile/mask/build ID is invalid.' }
    if ([bool]$verdict.liveBootProven -or [bool]$verdict.signingPerformed -or [bool]$verdict.stageOrArmPerformed) { throw 'Host payload build verdict falsely claims a live boot, signing, staging, or arm operation.' }
    $expectedMap = [IO.Path]::GetFullPath((Join-Path $Repository 'spoof-handoff\reports\client-originals.tsv'))
    $mapPath = [IO.Path]::GetFullPath([string]$verdict.lockedMap.path)
    if ($mapPath -cne $expectedMap -or [string]$verdict.lockedMap.sha256 -cne (Get-CombinedHostSha256 $expectedMap) -or [int]$verdict.lockedMap.bindingCount -ne 12 -or [int]$verdict.lockedMap.activeSmbiosFields -ne 7 -or -not [bool]$verdict.lockedMap.usbDiskTargetsExcluded) { throw 'Fresh host payload does not pin the current 12-row locked map and seven-field-only profile.' }
    $configuration = $verdict.configuration
    if ([string]$configuration.vmOnlyProfile -cne 'OFF' -or [string]$configuration.vmValuesHeader -cne '' -or [string]$configuration.buildType -cne 'Release' -or -not [bool]$configuration.buildTesting -or [int]$configuration.activeBootMask -ne 127 -or [bool]$configuration.diskSerialEnabled -or [bool]$configuration.adapterMacEnabled -or [bool]$configuration.cpuSerialEnabled -or [bool]$configuration.gpuSerialEnabled) { throw 'Fresh host payload CMake configuration is not the required host-only seven-field profile.' }
    if ([int]$verdict.ctest.count -ne 2 -or [int]$verdict.ctest.passed -ne 2 -or [int]$verdict.ctest.failed -ne 0 -or [int]$verdict.ctest.exitCode -ne 0 -or -not (Test-Path -LiteralPath ([string]$verdict.ctest.logPath) -PathType Leaf) -or (Get-CombinedHostSha256 ([string]$verdict.ctest.logPath)) -cne [string]$verdict.ctest.logSha256) { throw 'Fresh host payload must have two passing, hash-pinned SMBIOS tests.' }
    $ctestText = [IO.File]::ReadAllText([string]$verdict.ctest.logPath)
    if ($ctestText -notmatch '(?m)100% tests passed, 0 tests failed out of 2') { throw 'Fresh host payload CTest log does not show exactly two passing tests.' }
    $sourceManifestPath = [IO.Path]::GetFullPath((Join-Path $Repository $script:CombinedPathBSourceManifestRelative.Replace('/','\')))
    if ([IO.Path]::GetFullPath([string]$verdict.sourceManifest.path) -cne $sourceManifestPath -or (Get-CombinedHostSha256 $sourceManifestPath) -cne [string]$verdict.sourceManifest.sha256) { throw 'Fresh host payload does not pin the current Path-B source manifest.' }
    $null = Assert-CombinedPathBSourceManifest -Repository $Repository -Path $sourceManifestPath -ExpectedFileSha256 ([string]$verdict.sourceManifest.sha256)
    $currentPins = @(Get-CombinedHostPayloadSourcePins -Repository $Repository -Worktree $Worktree)
    $currentSourceSetHash = Get-CombinedHostPayloadSourceSetHash -Pins $currentPins
    if ($currentPins.Count -ne [int]$verdict.sourceSet.fileCount -or $currentSourceSetHash -cne [string]$verdict.sourceSet.sha256 -or $currentSourceSetHash -cne [string](Get-CombinedValue $Reference 'sourceSetSha256')) { throw 'Fresh host payload source set changed after its host build.' }
    $pinnedFiles = @(Get-CombinedValue $verdict.sourceSet 'pinnedFiles')
    if ($pinnedFiles.Count -ne $currentPins.Count) { throw 'Fresh host payload pinned source list length mismatch.' }
    for ($index=0; $index -lt $currentPins.Count; $index++) {
        foreach ($field in @('root','relative','bytes','sha256')) {
            if ([string](Get-CombinedValue $pinnedFiles[$index] $field) -cne [string](Get-CombinedValue $currentPins[$index] $field)) { throw "Fresh host payload source pin changed at index=$index field=$field." }
        }
    }
    $payload = $verdict.payload
    if ([IO.Path]::GetFullPath([string]$payload.path) -cne $payloadPath -or [string]$payload.sha256 -cne $payloadHash -or [string]$payload.sha256 -cne [string](Get-CombinedValue $Reference 'payloadSha256') -or [string]$payload.efiMachine -cne 'x64' -or [string]$payload.efiSubsystem -cne 'EFI_APPLICATION') { throw 'Fresh host payload metadata does not match its prepared binary.' }
    Assert-CombinedHostEfiApplication $payloadPath
    return [pscustomobject][ordered]@{verdictPath=$verdictPath;verdictSha256=$verdictHash;payloadPath=$payloadPath;payloadSha256=$payloadHash;buildId=[string]$verdict.buildId;sourceSetSha256=$currentSourceSetHash;activeBootMask=127;ctestPassed=2}
}

function Get-CombinedPathBSourceManifestData {
    param([Parameter(Mandatory=$true)][string]$Repository,[string[]]$SourceRoots=@('components/uefi-loader','tools/efi_inventory'))
    $repositoryPath = [IO.Path]::GetFullPath($Repository).TrimEnd('\') + '\'
    $sourceRoots = @($SourceRoots)
    $allowedExtensions = @('.c','.h','.cpp','.hpp','.cmake','.asm','.def','.rc','.in')
    $excludedDirectoryNames = @('.git','.codex-build','build','out','target','CMakeFiles','Testing','.cache','node_modules','__pycache__','artifacts','keys','key','private','secrets','certs','certificates')
    $records = [Collections.Generic.List[object]]::new()
    foreach ($relativeRoot in $sourceRoots) {
        $root = [IO.Path]::GetFullPath((Join-Path $Repository $relativeRoot.Replace('/','\')))
        if (-not $root.StartsWith($repositoryPath,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $root -PathType Container)) { throw "Path-B build source root is missing or outside the repository: $root" }
        $pending = [Collections.Generic.Queue[string]]::new()
        $pending.Enqueue($root)
        while ($pending.Count -gt 0) {
            $directory = $pending.Dequeue()
            foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)) {
                if ($item.PSIsContainer -and $excludedDirectoryNames -icontains $item.Name) { continue }
                if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point found in Path-B source tree: $($item.FullName)" }
                if ($item.PSIsContainer) { $pending.Enqueue($item.FullName); continue }
                $extension = [IO.Path]::GetExtension($item.Name).ToLowerInvariant()
                if ($item.Name -cne 'CMakeLists.txt' -and $allowedExtensions -notcontains $extension) { continue }
                $relative = $item.FullName.Substring($repositoryPath.Length) -replace '\\','/'
                $records.Add([pscustomobject][ordered]@{path=$relative;bytes=[long]$item.Length;sha256=(Get-CombinedHostSha256 $item.FullName)})
            }
        }
    }
    $files = @($records.ToArray() | Sort-Object -Property path -CaseSensitive)
    if ($files.Count -eq 0) { throw 'Path-B source tree manifest found no eligible C/CMake inputs.' }
    $canonicalLines = @($files | ForEach-Object { ([string]$_.path) + "`t" + ([long]$_.bytes) + "`t" + ([string]$_.sha256) })
    $canonicalBytes = [Text.UTF8Encoding]::new($false).GetBytes(($canonicalLines -join "`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $fileSetHash = [BitConverter]::ToString($sha.ComputeHash($canonicalBytes)).Replace('-','') } finally { $sha.Dispose() }
    return [pscustomobject][ordered]@{schema='combined-pathb-source-tree.v1';roots=$sourceRoots;fileCount=$files.Count;totalBytes=[long](($files | Measure-Object -Property bytes -Sum).Sum);filesSha256=$fileSetHash;files=$files}
}

function Write-CombinedPathBSourceManifest {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Repository,[string]$OutputPath,[string[]]$SourceRoots=@('components/uefi-loader','tools/efi_inventory'))
    if ([string]::IsNullOrWhiteSpace($OutputPath)) { $OutputPath = Join-Path $Repository $script:CombinedPathBSourceManifestRelative.Replace('/','\') }
    $fullOutput = [IO.Path]::GetFullPath($OutputPath)
    $reportsRoot = [IO.Path]::GetFullPath((Join-Path $Repository 'spoof-handoff\reports')).TrimEnd('\') + '\'
    if (-not $fullOutput.StartsWith($reportsRoot,[StringComparison]::OrdinalIgnoreCase)) { throw "Path-B source manifest must be written under spoof-handoff\reports: $fullOutput" }
    $manifest = Get-CombinedPathBSourceManifestData -Repository $Repository -SourceRoots $SourceRoots
    Write-CombinedHostAtomicJson -Path $fullOutput -Value $manifest
    return [pscustomobject][ordered]@{path=$fullOutput;sha256=(Get-CombinedHostSha256 $fullOutput);filesSha256=$manifest.filesSha256;fileCount=$manifest.fileCount;totalBytes=$manifest.totalBytes}
}

function Assert-CombinedPathBSourceManifest {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)][string]$ExpectedFileSha256,[string[]]$SourceRoots=@('components/uefi-loader','tools/efi_inventory'))
    $expectedPath = if ($SourceRoots.Count -eq 2 -and $SourceRoots[0] -ceq 'components/uefi-loader' -and $SourceRoots[1] -ceq 'tools/efi_inventory') { [IO.Path]::GetFullPath((Join-Path $Repository $script:CombinedPathBSourceManifestRelative.Replace('/','\'))) } else { [IO.Path]::GetFullPath($Path) }
    $fullPath = [IO.Path]::GetFullPath($Path)
    if ($fullPath -cne $expectedPath) { throw "Path-B source manifest path is not the required report: expected=$expectedPath actual=$fullPath" }
    if ($ExpectedFileSha256 -notmatch '\A[A-Fa-f0-9]{64}\z' -or -not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { throw "Path-B source manifest pin is missing or malformed: $fullPath" }
    $actualFileSha = Get-CombinedHostSha256 $fullPath
    if ($actualFileSha -cne $ExpectedFileSha256.ToUpperInvariant()) { throw "Path-B source manifest file changed: expected=$ExpectedFileSha256 actual=$actualFileSha" }
    $pinned = Get-Content -LiteralPath $fullPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $fresh = Get-CombinedPathBSourceManifestData -Repository $Repository -SourceRoots $SourceRoots
    if ([string]$pinned.schema -cne [string]$fresh.schema -or [string]$pinned.filesSha256 -cne [string]$fresh.filesSha256 -or [int]$pinned.fileCount -ne [int]$fresh.fileCount -or [long]$pinned.totalBytes -ne [long]$fresh.totalBytes -or (@($pinned.roots) -join '|') -cne (@($fresh.roots) -join '|')) {
        throw "Path-B native source set changed after its manifest was pinned: expectedFiles=$([int]$pinned.fileCount) actualFiles=$([int]$fresh.fileCount) expectedSetSha=$([string]$pinned.filesSha256) actualSetSha=$([string]$fresh.filesSha256)"
    }
    $pinnedFiles = @(Get-CombinedValue $pinned 'files')
    if ($pinnedFiles.Count -ne $fresh.files.Count) { throw "Path-B source file list length changed: expected=$($pinnedFiles.Count) actual=$($fresh.files.Count)" }
    for ($index=0; $index -lt $fresh.files.Count; $index++) {
        if ([string]$pinnedFiles[$index].path -cne [string]$fresh.files[$index].path -or [long]$pinnedFiles[$index].bytes -ne [long]$fresh.files[$index].bytes -or [string]$pinnedFiles[$index].sha256 -cne [string]$fresh.files[$index].sha256) {
            throw "Path-B native source manifest entry changed at index=$index expected=$([string]$pinnedFiles[$index].path):$([long]$pinnedFiles[$index].bytes):$([string]$pinnedFiles[$index].sha256) actual=$([string]$fresh.files[$index].path):$([long]$fresh.files[$index].bytes):$([string]$fresh.files[$index].sha256)"
        }
    }
    return [pscustomobject][ordered]@{path=$fullPath;sha256=$actualFileSha;filesSha256=$fresh.filesSha256;fileCount=$fresh.fileCount;totalBytes=$fresh.totalBytes}
}

function Write-CombinedHostAtomicJson {
    param([Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)][object]$Value)
    $parent = Split-Path -Parent $Path
    [void][IO.Directory]::CreateDirectory($parent)
    $tempPath = Join-Path $parent ([IO.Path]::GetFileName($Path) + '.' + [Guid]::NewGuid().ToString('N') + '.tmp')
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 32))
    $stream = [IO.FileStream]::new($tempPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None,4096,[IO.FileOptions]::WriteThrough)
    try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        # A named backup avoids the Windows PowerShell null-overload binding bug.
        $backupPath = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.bak'
        [IO.File]::Replace($tempPath,$Path,$backupPath)
    }
    else { [IO.File]::Move($tempPath,$Path) }
}

function Get-CombinedHostPath {
    param([Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $Repository $Path))
}

function ConvertTo-CombinedRelativePath {
    param([Parameter(Mandatory=$true)][string]$Path)
    return ($Path -replace '\\','/')
}

function Resolve-CombinedPinnedSource {
    param([Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$Worktree,[Parameter(Mandatory=$true)][string]$Path)
    $repositoryRoot = [IO.Path]::GetFullPath($Repository).TrimEnd('\') + '\'
    $worktreeRoot = [IO.Path]::GetFullPath($Worktree).TrimEnd('\') + '\'
    if ([IO.Path]::IsPathRooted($Path)) {
        $resolved = [IO.Path]::GetFullPath($Path)
        if ($resolved.StartsWith($worktreeRoot,[StringComparison]::OrdinalIgnoreCase)) {
            $relative = ConvertTo-CombinedRelativePath $resolved.Substring($worktreeRoot.Length)
            if ($relative.StartsWith('spoof-handoff/')) { $relative = $relative.Substring('spoof-handoff/'.Length) }
            return [pscustomobject]@{ path=$resolved; relative=$relative; root='worktree' }
        }
        if ($resolved.StartsWith($repositoryRoot,[StringComparison]::OrdinalIgnoreCase)) {
            return [pscustomobject]@{ path=$resolved; relative=(ConvertTo-CombinedRelativePath $resolved.Substring($repositoryRoot.Length)); root='repository' }
        }
        throw "Pinned source is outside the repository and worktree: $resolved"
    }
    $relative = ConvertTo-CombinedRelativePath $Path
    if ($relative.StartsWith('../') -or $relative -match '(^|/)\.\.(?:/|$)') { throw "Pinned source path escapes its root: $Path" }
    if ($relative.StartsWith('spoof-handoff/reports/')) {
        return [pscustomobject]@{ path=(Get-CombinedHostPath $Repository $Path); relative=$relative; root='repository' }
    }
    if ($relative.StartsWith('spoof-handoff/artifacts/')) {
        return [pscustomobject]@{ path=(Get-CombinedHostPath $Repository $Path); relative=$relative; root='repository' }
    }
    if ($relative.StartsWith('spoof-handoff/tools/')) { $relative = $relative.Substring('spoof-handoff/'.Length) }
    if ($relative.StartsWith('tools/')) {
        return [pscustomobject]@{ path=(Get-CombinedHostPath $Worktree ('spoof-handoff/' + $relative)); relative=$relative; root='worktree' }
    }
    return [pscustomobject]@{ path=(Get-CombinedHostPath $Worktree $Path); relative=$relative; root='worktree' }
}

function Assert-CombinedHostEvidenceFile {
    param([Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][object]$Reference,[Parameter(Mandatory=$true)][string]$Label)
    $pathText = [string](Get-CombinedValue $Reference 'path')
    $expectedHash = [string](Get-CombinedValue $Reference 'sha256')
    if ([string]::IsNullOrWhiteSpace($pathText) -or $expectedHash -notmatch '\A[A-Fa-f0-9]{64}\z') { throw "$Label must provide a path and SHA-256." }
    $path = Get-CombinedHostPath $Repository $pathText
    $reportsRoot = [IO.Path]::GetFullPath((Join-Path $Repository 'spoof-handoff\reports')).TrimEnd('\') + '\'
    if (-not $path.StartsWith($reportsRoot,[StringComparison]::OrdinalIgnoreCase)) { throw "$Label must resolve under spoof-handoff\reports: $path" }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "$Label evidence is missing: $path" }
    $actualHash = Get-CombinedHostSha256 $path
    if ($actualHash -cne $expectedHash.ToUpperInvariant()) { throw "$Label SHA-256 mismatch: expected=$expectedHash actual=$actualHash path=$path" }
    return [pscustomobject]@{ path=$path; sha256=$actualHash }
}

function Assert-CombinedVmReplacementSet {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][object]$HostBindingMap,[Parameter(Mandatory=$true)][object]$VmBindingMap)
    foreach ($hostBinding in @($HostBindingMap.bindings | Where-Object { $_.category -cne 'NIC' })) {
        $vmMatches = @($VmBindingMap.bindings | Where-Object { $_.category -ceq $hostBinding.category -and $_.member -ceq $hostBinding.member })
        if ($vmMatches.Count -ne 1) { throw "VM fixture map must contain exactly one $($hostBinding.category) binding for $($hostBinding.member); actual=$($vmMatches.Count)." }
        if ([string]$vmMatches[0].replacement -cne [string]$hostBinding.replacement) { throw "VM fixture replacement differs from the canonical locked value for $($hostBinding.category):$($hostBinding.member): expected=$($hostBinding.replacement) actual=$($vmMatches[0].replacement)." }
    }
    $hostNics = @($HostBindingMap.bindings | Where-Object { $_.category -ceq 'NIC' })
    $vmNics = @($VmBindingMap.bindings | Where-Object { $_.category -ceq 'NIC' })
    if ($hostNics.Count -ne 4 -or $vmNics.Count -ne 4) { throw "Canonical and VM fixture maps must each contain four NIC bindings; host=$($hostNics.Count) VM=$($vmNics.Count)." }
    $hostByOriginal = @{}
    foreach ($hostNic in $hostNics) {
        $key = ConvertTo-CombinedComparableValue -Category 'NIC' -Field 'mac' -Value $hostNic.original
        if ($null -eq $key -or $hostByOriginal.ContainsKey([string]$key)) { throw "Canonical NIC map has an invalid or duplicate original MAC: $([string]$hostNic.original)" }
        $hostByOriginal[[string]$key] = [string]$hostNic.replacement
    }
    $vmOriginals = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($vmNic in $vmNics) {
        $key = ConvertTo-CombinedComparableValue -Category 'NIC' -Field 'mac' -Value $vmNic.original
        if ($null -eq $key -or -not $hostByOriginal.ContainsKey([string]$key)) { throw "VM fixture NIC original has no canonical adapter mapping: $([string]$vmNic.original)" }
        if (-not $vmOriginals.Add([string]$key)) { throw "VM fixture repeats a NIC original MAC: $([string]$vmNic.original)" }
        $expectedReplacement = [string]$hostByOriginal[[string]$key]
        if ([string]$vmNic.replacement -cne $expectedReplacement) { throw "VM fixture NIC replacement is assigned to the wrong original adapter: original=$([string]$vmNic.original) expected=$expectedReplacement actual=$([string]$vmNic.replacement)." }
    }
    if ($vmOriginals.Count -ne $hostByOriginal.Count) { throw "VM fixture NIC originals must cover the four canonical adapters; expected=$($hostByOriginal.Count) actual=$($vmOriginals.Count)." }
    return $true
}

function Assert-CombinedReadbackMapReference {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][object]$Readback,[Parameter(Mandatory=$true)][object]$MapReference)
    $readbackMap = Get-CombinedValue $Readback 'lockedMap'
    $readbackHash = [string](Get-CombinedValue $readbackMap 'sha256')
    $readbackCount = Get-CombinedValue $readbackMap 'bindingCount'
    if ($readbackHash -notmatch '\A[A-Fa-f0-9]{64}\z' -or $readbackHash.ToUpperInvariant() -cne [string]$MapReference.sha256 -or [int]$readbackCount -ne 12) { throw "Combined readback does not use the hash-pinned 12-binding VM fixture map: expected=$($MapReference.sha256) actual=$readbackHash bindingCount=$readbackCount." }
    return $true
}

function Get-CombinedVmFixtureBindingMap {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][object]$Reference,[Parameter(Mandatory=$true)][object]$HostBindingMap)
    $pathText = [string](Get-CombinedValue $Reference 'path')
    $expectedHash = [string](Get-CombinedValue $Reference 'sha256')
    if ([string]::IsNullOrWhiteSpace($pathText) -or $expectedHash -notmatch '\A[A-Fa-f0-9]{64}\z') { throw 'VM fixture map reference must include path and SHA-256.' }
    $fixturePath = Get-CombinedHostPath $Repository $pathText
    $reportsRoot = [IO.Path]::GetFullPath((Join-Path $Repository 'spoof-handoff\reports')).TrimEnd('\') + '\'
    $artifactsRoot = [IO.Path]::GetFullPath((Join-Path $Repository 'spoof-handoff\artifacts')).TrimEnd('\') + '\'
    if (-not ($fixturePath.StartsWith($reportsRoot,[StringComparison]::OrdinalIgnoreCase) -or $fixturePath.StartsWith($artifactsRoot,[StringComparison]::OrdinalIgnoreCase))) { throw "VM fixture map must be under spoof-handoff reports or artifacts: $fixturePath" }
    if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) { throw "VM fixture map does not exist: $fixturePath" }
    $actualHash = Get-CombinedHostSha256 $fixturePath
    if ($actualHash -cne $expectedHash.ToUpperInvariant()) { throw "VM fixture map SHA-256 mismatch: expected=$expectedHash actual=$actualHash path=$fixturePath" }
    if ([IO.Path]::GetExtension($fixturePath) -cne '.tsv') { throw "VM fixture map must be a TSV consumed by New-CombinedBindingMap: $fixturePath" }
    $rows = Get-CombinedMapRows $fixturePath
    $diskRows = @($rows | Where-Object { [string]$_.member -ceq 'FakeDiskSerial' })
    if ($diskRows.Count -ne 1 -or [string]$diskRows[0].device -notmatch '(?i)PHYSICALDRIVE0\s*$') { throw "VM fixture map must contain exactly the locked PHYSICALDRIVE0 target and no other disk rows; actual=$($diskRows.Count)." }
    $vmBindingMap = New-CombinedBindingMap $rows
    Assert-CombinedVmReplacementSet -HostBindingMap $HostBindingMap -VmBindingMap $vmBindingMap | Out-Null
    return [pscustomobject][ordered]@{ path=$fixturePath; sha256=$actualHash; bindingMap=$vmBindingMap }
}

function Assert-CombinedRequiredVmSourcePins {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][object[]]$NormalizedPins)
    foreach ($relative in $script:CombinedRequiredSourcePins) {
        $matches = @($NormalizedPins | Where-Object { [string](Get-CombinedValue $_ 'relative') -ceq $relative })
        if ($matches.Count -ne 1) { throw "VM gate must pin source '$relative' exactly once; actual=$($matches.Count)." }
    }
    return $true
}

function Assert-CombinedVmGateEvidence {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$Worktree,[Parameter(Mandatory=$true)][string]$GatePath)
    $resolvedGate = [IO.Path]::GetFullPath($GatePath)
    $reportsRoot = [IO.Path]::GetFullPath((Join-Path $Repository 'spoof-handoff\reports')).TrimEnd('\') + '\'
    if (-not $resolvedGate.StartsWith($reportsRoot,[StringComparison]::OrdinalIgnoreCase)) { throw "VM gate must be under spoof-handoff\reports: $resolvedGate" }
    if (-not (Test-Path -LiteralPath $resolvedGate -PathType Leaf)) { throw "Combined VM gate is missing: $resolvedGate" }
    $gate = Get-Content -LiteralPath $resolvedGate -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ([string](Get-CombinedValue $gate 'schema') -cne 'combined-vm-gate.v1') { throw "VM gate schema mismatch; expected=combined-vm-gate.v1 actual=$([string](Get-CombinedValue $gate 'schema'))." }
    if ([string](Get-CombinedValue $gate 'status') -cne 'PASS') { throw "Combined VM gate did not pass: status=$([string](Get-CombinedValue $gate 'status'))." }

    $rows = Get-CombinedMapRows (Join-Path $Repository 'spoof-handoff\reports\client-originals.tsv')
    $hostBindings = New-CombinedBindingMap $rows
    $mapHash = Get-CombinedHostSha256 (Join-Path $Repository 'spoof-handoff\reports\client-originals.tsv')
    $vmFixtureMap = Get-CombinedVmFixtureBindingMap -Repository $Repository -Reference (Get-CombinedValue $gate 'vmFixtureMap') -HostBindingMap $hostBindings
    $sourcePins = @(Get-CombinedValue $gate 'sourcePins')
    if ($sourcePins.Count -eq 0) { throw 'VM gate contains no source pins.' }
    $normalizedPins = [Collections.Generic.List[object]]::new()
    foreach ($pin in $sourcePins) {
        $pathText = [string](Get-CombinedValue $pin 'path')
        $resolvedPin = Resolve-CombinedPinnedSource -Repository $Repository -Worktree $Worktree -Path $pathText
        $pinHash = [string](Get-CombinedValue $pin 'sha256')
        if ($pinHash -notmatch '\A[A-Fa-f0-9]{64}\z') { throw "VM gate source pin has invalid SHA-256: $pathText" }
        if (-not (Test-Path -LiteralPath $resolvedPin.path -PathType Leaf)) { throw "Pinned source does not exist: $($resolvedPin.path)" }
        $actualSourceHash = Get-CombinedHostSha256 $resolvedPin.path
        if ($pinHash.ToUpperInvariant() -cne $actualSourceHash) { throw "VM gate source pin differs from current source: path=$($resolvedPin.relative) expected=$pinHash actual=$actualSourceHash" }
        $normalizedPins.Add([pscustomobject]@{relative=$resolvedPin.relative;path=$resolvedPin.path;root=$resolvedPin.root;sha256=$actualSourceHash})
    }
    Assert-CombinedRequiredVmSourcePins -NormalizedPins @($normalizedPins.ToArray()) | Out-Null
    $pathBSourcePin = @($normalizedPins | Where-Object { $_.relative -ceq 'tools/efi_inventory/path-b-collector.ps1' })
    $expectedPathBScript = [IO.Path]::GetFullPath((Join-Path $Repository 'tools\efi_inventory\path-b-collector.ps1'))
    if ($pathBSourcePin.Count -ne 1 -or $pathBSourcePin[0].root -cne 'repository' -or [IO.Path]::GetFullPath($pathBSourcePin[0].path) -cne $expectedPathBScript) {
        throw "VM gate must pin the actual main-repository Path-B child script by absolute path: $expectedPathBScript"
    }
    $manifestPin = @($normalizedPins | Where-Object { $_.relative -ceq $script:CombinedPathBSourceManifestRelative })
    $expectedManifestPath = [IO.Path]::GetFullPath((Join-Path $Repository $script:CombinedPathBSourceManifestRelative.Replace('/','\')))
    if ($manifestPin.Count -ne 1 -or $manifestPin[0].root -cne 'repository' -or [IO.Path]::GetFullPath($manifestPin[0].path) -cne $expectedManifestPath) {
        throw "VM gate must pin the prepared Path-B source tree manifest at: $expectedManifestPath"
    }
    $sourceTreeManifest = Assert-CombinedPathBSourceManifest -Repository $Repository -Path $manifestPin[0].path -ExpectedFileSha256 $manifestPin[0].sha256
    $efiPins = @($normalizedPins | Where-Object { $_.relative -match '(?i)\.efi\z' })
    $driverPins = @($normalizedPins | Where-Object { $_.relative -match '(?i)\.sys\z' })
    $wrongDriverPins = @($driverPins | Where-Object { [IO.Path]::GetFileName($_.relative) -cne 'client_storage_filter.sys' })
    $readerPins = @($normalizedPins | Where-Object { $_.relative -match '(?i)\.exe\z' })
    if ($efiPins.Count -lt 1 -or $driverPins.Count -lt 1 -or $wrongDriverPins.Count -gt 0 -or $readerPins.Count -lt 2) { throw "VM gate must pin the EFI payload, the VM-used storage filter binary only, and the native reader executables; efi=$($efiPins.Count) storageSys=$($driverPins.Count) unexpectedSys=$($wrongDriverPins.Count) nativeExe=$($readerPins.Count)." }
    $inventoryLoaderPins = @($efiPins | Where-Object { [IO.Path]::GetFileName($_.relative) -ceq 'inventory_loader.efi' })
    $installerPins = @($readerPins | Where-Object { [IO.Path]::GetFileName($_.relative) -ceq 'ib-install.exe' })
    if ($inventoryLoaderPins.Count -ne 1 -or $installerPins.Count -ne 1) { throw "VM gate must pin the actual Path-B inventory_loader.efi and ib-install.exe artifacts exactly once; loader=$($inventoryLoaderPins.Count) installer=$($installerPins.Count)." }
    $mapPins = @($normalizedPins | Where-Object { $_.relative -ceq 'spoof-handoff/reports/client-originals.tsv' })
    if ($mapPins.Count -ne 1 -or $mapPins[0].sha256 -cne $mapHash) { throw "VM gate locked-map hash differs from current exact TSV: expected=$mapHash actual=$($mapPins[0].sha256)." }

    $phases = Get-CombinedValue $gate 'phases'
    $phaseNames = @('original','active','restored')
    foreach ($phaseName in $phaseNames) {
        $phase = Get-CombinedValue $phases $phaseName
        if ($null -eq $phase) { throw "VM gate is missing phase '$phaseName'." }
        $readbackRef = Get-CombinedValue $phase 'combinedReadback'
        $readbackFile = Assert-CombinedHostEvidenceFile -Repository $Repository -Reference $readbackRef -Label "$phaseName combined readback"
        $readback = Get-Content -LiteralPath $readbackFile.path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        Assert-CombinedReadbackMapReference -Readback $readback -MapReference $vmFixtureMap | Out-Null
        Assert-CombinedReadbackPhase -BindingMap $vmFixtureMap.bindingMap -Readback $readback -Phase $phaseName | Out-Null
        $captureRef = Get-CombinedValue $phase 'guestCapture'
        $captureFile = Assert-CombinedHostEvidenceFile -Repository $Repository -Reference $captureRef -Label "$phaseName authenticated guest capture"
        $capture = Get-Content -LiteralPath $captureFile.path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        $authentication = Get-CombinedValue $capture 'mailboxAuthentication'
        $sessionId = [string](Get-CombinedValue $authentication 'sessionId')
        $responseHmac = [string](Get-CombinedValue $authentication 'responseHmacSha256')
        $evidenceDigest = [string](Get-CombinedValue $authentication 'evidenceSha256')
        if ([string](Get-CombinedValue $authentication 'method') -cne 'HMAC-SHA256' -or (Get-CombinedValue $authentication 'verified') -ne $true -or [string]::IsNullOrWhiteSpace($sessionId) -or $responseHmac -notmatch '\A[A-Fa-f0-9]{64}\z' -or $evidenceDigest -notmatch '\A[A-Fa-f0-9]{64}\z') { throw "$phaseName guest capture does not contain a complete verified mailbox receipt reference." }
        $receiptRef = Get-CombinedValue $phase 'mailboxReceipt'
        if ($null -eq $receiptRef) { $receiptRef = Get-CombinedValue $capture 'mailboxReceipt' }
        $receiptFile = Assert-CombinedHostEvidenceFile -Repository $Repository -Reference $receiptRef -Label "$phaseName authenticated mailbox response metadata"
        $receipt = Get-Content -LiteralPath $receiptFile.path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        if ([string](Get-CombinedValue $receipt 'RunId') -cne $sessionId -or [string](Get-CombinedValue $receipt 'ResponseHmacSha256') -cne $responseHmac -or [string](Get-CombinedValue $receipt 'EvidenceSha256') -cne $evidenceDigest -or [string](Get-CombinedValue $receipt 'Status') -cne 'PASS') { throw "$phaseName mailbox metadata does not match the capture's HMAC/session/evidence references." }
        $archiveRef = Get-CombinedValue $capture 'archive'
        if ($null -eq $archiveRef) { $archiveRef = Get-CombinedValue $phase 'archive' }
        $archiveFile = Assert-CombinedHostEvidenceFile -Repository $Repository -Reference $archiveRef -Label "$phaseName authenticated evidence archive"
        if ($archiveFile.sha256 -cne $evidenceDigest.ToUpperInvariant() -or [string](Get-CombinedValue $receipt 'EvidenceArchive') -cne (Split-Path -Leaf $archiveFile.path)) { throw "$phaseName authenticated archive does not match response metadata: expected=$evidenceDigest actual=$($archiveFile.sha256)." }
        $responseRef = Get-CombinedValue $phase 'mailboxResponse'
        if ($null -ne $responseRef) {
            $responseFile = Assert-CombinedHostEvidenceFile -Repository $Repository -Reference $responseRef -Label "$phaseName mailbox response"
            $response = Get-Content -LiteralPath $responseFile.path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
            if ([string](Get-CombinedValue $response 'RunId') -cne $sessionId -or [string](Get-CombinedValue $response 'ResponseHmacSha256') -cne $responseHmac -or [string](Get-CombinedValue $response 'EvidenceSha256') -cne $evidenceDigest) { throw "$phaseName mailbox response does not match the authenticated receipt." }
        }
        $transportRef = Get-CombinedValue $phase 'transportMetadata'
        if ($null -ne $transportRef) {
            $transportFile = Assert-CombinedHostEvidenceFile -Repository $Repository -Reference $transportRef -Label "$phaseName mailbox transport metadata"
            $transport = Get-Content -LiteralPath $transportFile.path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
            if ([string](Get-CombinedValue $transport 'RunId') -cne $sessionId) { throw "$phaseName transport metadata session does not match authenticated mailbox RunId." }
        }
        $rawRefs = @(Get-CombinedValue $phase 'rawEvidence')
        if ($rawRefs.Count -lt 1) { throw "$phaseName phase must pin raw reader evidence." }
        foreach ($rawRef in $rawRefs) { $null = Assert-CombinedHostEvidenceFile -Repository $Repository -Reference $rawRef -Label "$phaseName raw evidence" }
    }
    $active = Get-CombinedValue $phases 'active'
    $activeReadbackFile = Assert-CombinedHostEvidenceFile -Repository $Repository -Reference (Get-CombinedValue $active 'combinedReadback') -Label 'active combined readback'
    $activeReadback = Get-Content -LiteralPath $activeReadbackFile.path -Raw | ConvertFrom-Json
    if ([string](Get-CombinedValue $activeReadback 'status') -cne 'PASS' -or (Get-CombinedValue $activeReadback 'simultaneousReadbackPass') -ne $true) { throw 'The active VM evidence does not prove a simultaneous all-field pass.' }
    $activeBoot = Get-CombinedValue (Get-CombinedValue $activeReadback 'bootIdentity') 'windowsLastBootUpTimeUtc'
    $activePhaseBoot = Get-CombinedValue (Get-CombinedValue $active 'bootIdentity') 'lastBootUpTimeUtc'
    if ([string]$activeBoot -cne [string]$activePhaseBoot -or [string]::IsNullOrWhiteSpace([string]$activeBoot)) { throw "Active VM boot identity mismatch: readback=$activeBoot phase=$activePhaseBoot" }
    $efi = Get-CombinedValue $active 'efi'
    if ($null -eq $efi -or [string](Get-CombinedValue $efi 'status') -notmatch '\A(?:EFI_SUCCESS|0x0{16})\z') { throw "Active EFI load status must be EFI_SUCCESS/0; actual=$([string](Get-CombinedValue $efi 'status'))." }
    $deviceState = Get-CombinedValue (Get-CombinedValue $phases 'restored') 'deviceState'
    if ((Get-CombinedValue $deviceState 'storageFilterPresent') -ne $false -or (Get-CombinedValue $deviceState 'nicRegistryRestored') -ne $true) { throw 'Restored VM phase does not prove typed NIC restoration and storage filter absence.' }
    $restoredVm = Get-CombinedValue $phases 'restored'
    if ((Get-CombinedValue $restoredVm 'coldSnapshotBoot') -ne $true) { throw 'Restored VM phase must prove a cold boot from its verified snapshot.' }
    $gateHash = Get-CombinedHostSha256 $resolvedGate
    return [pscustomobject][ordered]@{ schema=[string](Get-CombinedValue $gate 'schema'); status='PASS'; path=$resolvedGate; sha256=$gateHash; lockedMapSha256=$mapHash; vmFixtureMap=[ordered]@{path=$vmFixtureMap.path;sha256=$vmFixtureMap.sha256}; pathBSourcePin=[ordered]@{path=$pathBSourcePin[0].path;sha256=$pathBSourcePin[0].sha256}; pathBSourceManifest=$sourceTreeManifest; pathBArtifacts=@([pscustomobject]@{name='inventory_loader.efi';path=$inventoryLoaderPins[0].path;sha256=$inventoryLoaderPins[0].sha256},[pscustomobject]@{name='ib-install.exe';path=$installerPins[0].path;sha256=$installerPins[0].sha256}); bindingCount=$vmFixtureMap.bindingMap.bindings.Count; activeBootId=[string](Get-CombinedValue (Get-CombinedValue $active 'bootIdentity') 'bootId') }
}

function Assert-CombinedReadbackPhase {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][object]$BindingMap,[Parameter(Mandatory=$true)][object]$Readback,[Parameter(Mandatory=$true)][ValidateSet('original','active','restored')][string]$Phase)
    if ([string](Get-CombinedValue $Readback 'schema') -cne 'combined-host-readback.v1') { throw "$Phase readback schema is invalid." }
    $expectedMode = if ($Phase -ceq 'active') { 'Replacement' } else { 'Original' }
    if ([string](Get-CombinedValue $Readback 'expectedMode') -cne $expectedMode -or [string](Get-CombinedValue $Readback 'status') -cne 'PASS') { throw "$Phase readback must pass in mode $expectedMode; actual status=$([string](Get-CombinedValue $Readback 'status')) mode=$([string](Get-CombinedValue $Readback 'expectedMode'))." }
    if ((Get-CombinedValue $Readback 'simultaneousReadbackPass') -ne $true -or (Get-CombinedValue $Readback 'stableAcrossPolls') -ne $true) { throw "$Phase readback is not a stable simultaneous full-reader capture." }
    $polls = @(Get-CombinedValue $Readback 'polls')
    if ($polls.Count -ne 2) { throw "$Phase readback must contain exactly two bracket polls; actual=$($polls.Count)." }
    foreach ($poll in $polls) {
        $pollBindings = @(Get-CombinedValue $poll 'bindings')
        if ($pollBindings.Count -ne 12) { throw "$Phase poll must contain 12 bindings; actual=$($pollBindings.Count)." }
        foreach ($binding in $BindingMap.bindings) {
            $matches = @($pollBindings | Where-Object { [string](Get-CombinedValue $_ 'id') -ceq [string]$binding.id })
            if ($matches.Count -ne 1) { throw "$Phase poll binding '$($binding.id)' cardinality mismatch: actual=$($matches.Count)." }
            $expected = if ($expectedMode -ceq 'Replacement') { [string]$binding.replacement } else { [string]$binding.original }
            $readerResults = @(Get-CombinedValue $matches[0] 'readers')
            foreach ($readerName in $binding.readers) {
                $reader = @($readerResults | Where-Object { [string](Get-CombinedValue $_ 'reader') -ceq [string]$readerName })
                if ($reader.Count -ne 1) { throw "$Phase binding '$($binding.id)' reader '$readerName' cardinality mismatch: actual=$($reader.Count)." }
                $actual = [string](Get-CombinedValue $reader[0] 'actual')
                $normalizedExpected = ConvertTo-CombinedComparableValue -Category ([string]$binding.category) -Field ([string]$binding.field) -Value $expected
                $normalizedActual = ConvertTo-CombinedComparableValue -Category ([string]$binding.category) -Field ([string]$binding.field) -Value $actual
                if ((Get-CombinedValue $reader[0] 'match') -ne $true -or [string](Get-CombinedValue $reader[0] 'expected') -cne $expected -or $null -eq $normalizedActual -or $normalizedActual -cne $normalizedExpected -or ((Get-CombinedValue $reader[0] 'cardinality') -ne $null -and [int](Get-CombinedValue $reader[0] 'cardinality') -ne 1)) { throw "$Phase binding '$($binding.id)' reader '$readerName' did not match the locked value. Expected=$expected Actual=$actual." }
            }
        }
    }
    return $true
}

function Invoke-CombinedBcdCapture {
    param([Parameter(Mandatory=$true)][string]$Name,[Parameter(Mandatory=$true)][string[]]$Arguments,[Parameter(Mandatory=$true)][string]$Directory)
    $bcdedit = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
    $stdoutPath = Join-Path $Directory ($Name + '.stdout.txt')
    $stderrPath = Join-Path $Directory ($Name + '.stderr.txt')
    $quoted = @($Arguments | ForEach-Object { '"' + ([string]$_).Replace('"','\"') + '"' })
    $process = Start-Process -FilePath $bcdedit -ArgumentList $quoted -PassThru -Wait -WindowStyle Hidden -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $stdout = if (Test-Path -LiteralPath $stdoutPath -PathType Leaf) { [IO.File]::ReadAllText($stdoutPath) } else { '' }
    $stderr = if (Test-Path -LiteralPath $stderrPath -PathType Leaf) { [IO.File]::ReadAllText($stderrPath) } else { '' }
    return [pscustomobject][ordered]@{ name=$Name; arguments=$Arguments; exitCode=[int]$process.ExitCode; stdoutPath=$stdoutPath; stderrPath=$stderrPath; stdout=$stdout; stderr=$stderr }
}

function Get-CombinedBcdSnapshot {
    param([Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$EvidenceDirectory)
    $directory = Join-Path $EvidenceDirectory 'bcd'
    [void][IO.Directory]::CreateDirectory($directory)
    $current = Invoke-CombinedBcdCapture 'current' @('/enum','{current}','/v') $directory
    $all = Invoke-CombinedBcdCapture 'all' @('/enum','all','/v') $directory
    $firmware = Invoke-CombinedBcdCapture 'firmware' @('/enum','firmware','/v') $directory
    foreach ($result in @($current,$all,$firmware)) {
        if ($result.exitCode -ne 0 -or [string]::IsNullOrWhiteSpace([string]$result.stdout)) { throw "BCD $($result.name) snapshot failed exit=$($result.exitCode) stdout=$($result.stdout) stderr=$($result.stderr)" }
    }
    $exportPath = Join-Path $directory 'boot-store-export.bcd'
    $export = Invoke-CombinedBcdCapture 'export' @('/export',$exportPath) $directory
    if ($export.exitCode -ne 0 -or -not (Test-Path -LiteralPath $exportPath -PathType Leaf)) { throw "BCD export failed exit=$($export.exitCode) stdout=$($export.stdout) stderr=$($export.stderr)" }
    $exportedAll = Invoke-CombinedBcdCapture 'exported-all' @('/store',$exportPath,'/enum','all','/v') $directory
    if ($exportedAll.exitCode -ne 0 -or [string]::IsNullOrWhiteSpace([string]$exportedAll.stdout)) { throw "Exported BCD enumeration failed exit=$($exportedAll.exitCode) stdout=$($exportedAll.stdout) stderr=$($exportedAll.stderr)" }
    $exportSha = Get-CombinedHostSha256 $exportPath
    return [pscustomobject][ordered]@{ capturedUtc=[DateTime]::UtcNow.ToString('o'); current=$current; all=$all; firmware=$firmware; export=[pscustomobject]@{path=$exportPath;sha256=$exportSha;bytes=(Get-Item -LiteralPath $exportPath).Length;exitCode=$export.exitCode;stdout=$export.stdout;stderr=$export.stderr}; exportedAll=$exportedAll; rawLiveBcdHashClaimed=$false }
}

function Get-CombinedEspSnapshot {
    param([Parameter(Mandatory=$true)][string]$EvidenceDirectory,[Parameter(Mandatory=$true)][object]$BcdSnapshot)
    $espType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
    $partitions = @(Get-Partition -ErrorAction Stop | Where-Object { [string]$_.GptType -ieq $espType })
    if ($partitions.Count -eq 0) { throw 'No EFI System Partition was found by its GPT type.' }
    $results = [Collections.Generic.List[object]]::new()
    foreach ($partition in $partitions) {
        $systemPartition = [bool]$partition.IsSystem
        $accessPaths = @($partition.AccessPaths | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) })
        $root = $null
        foreach ($candidate in $accessPaths) { if (Test-Path -LiteralPath ([string]$candidate) -PathType Container) { $root = [string]$candidate; break } }
        if (-not $root) { throw "EFI partition is not reachable through an existing access path; no mount or drive letter was added. Disk=$($partition.DiskNumber) Partition=$($partition.PartitionNumber) AccessPaths=$($accessPaths -join ';')" }
        $files = [Collections.Generic.List[object]]::new()
        $pending = [Collections.Generic.Queue[string]]::new()
        $pending.Enqueue($root)
        while ($pending.Count -gt 0) {
            $directory = $pending.Dequeue()
            foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)) {
                if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Unexpected reparse point in ESP: $($item.FullName)" }
                $relative = $item.FullName.Substring($root.Length).TrimStart('\') -replace '\\','/'
                if ($item.PSIsContainer) {
                    $files.Add([pscustomobject]@{path=$relative;directory=$true;bytes=0;sha256=$null;verification='directory';rawBytesCaptured=$false})
                    $pending.Enqueue($item.FullName)
                    continue
                }
                if ($systemPartition -and $relative -match '(?i)\AEFI/Microsoft/Boot/BCD(?:\.LOG[0-9]*)?\z') {
                    $files.Add([pscustomobject]@{path=$relative;directory=$false;bytes=[long]$item.Length;sha256=$null;verification='supported-bcdedit-export';exportPath=$BcdSnapshot.export.path;exportSha256=$BcdSnapshot.export.sha256;rawBytesCaptured=$false})
                }
                elseif ([string]::Equals($relative,'EFI/Microsoft/Boot/BOOTSTAT.DAT',[StringComparison]::OrdinalIgnoreCase)) {
                    $files.Add([pscustomobject]@{path=$relative;directory=$false;bytes=[long]$item.Length;sha256=$null;verification='mutable-windows-boot-status';rawBytesCaptured=$false})
                }
                else { $files.Add([pscustomobject]@{path=$relative;directory=$false;bytes=[long]$item.Length;sha256=(Get-CombinedHostSha256 $item.FullName);verification='file-sha256';rawBytesCaptured=$true}) }
            }
        }
        $results.Add([pscustomobject][ordered]@{diskNumber=[int]$partition.DiskNumber;partitionNumber=[int]$partition.PartitionNumber;partitionGuid=[string]$partition.Guid;offset=[long]$partition.Offset;size=[long]$partition.Size;isSystem=$systemPartition;accessPath=$root;files=@($files.ToArray() | Sort-Object path)})
    }
    $report = [pscustomobject][ordered]@{schema='combined-host-esp-snapshot.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');partitions=@($results.ToArray());bcdSnapshot=$BcdSnapshot;note='BOOTSTAT.DAT is mutable Windows boot status. Live BCD bytes are not hashed; supported BCD enumerations and the exported store are recorded.'}
    $path = Join-Path $EvidenceDirectory 'esp-snapshot.json'
    Write-CombinedHostAtomicJson $path $report
    $entries=@($results.ToArray() | ForEach-Object { @($_.files) })
    return [pscustomobject][ordered]@{path=$path;sha256=(Get-CombinedHostSha256 $path);fileCount=@($entries | Where-Object { -not [bool]$_.directory }).Count;directoryCount=@($entries | Where-Object { [bool]$_.directory }).Count;entryCount=$entries.Count;partitions=@($results.ToArray())}
}

function Get-CombinedSecureBootVariableSnapshot {
    param([Parameter(Mandatory=$true)][string]$Worktree,[bool]$ExpectedEnabled=$true)
    $readScript = Join-Path $Worktree 'spoof-handoff\tools\storage_host\read.ps1'
    if (-not (Test-Path -LiteralPath $readScript -PathType Leaf)) { throw "Pinned Secure Boot reader source is missing: $readScript" }
    . $readScript
    $mode = $null
    $modeError = $null
    try { $mode = [bool](Confirm-SecureBootUEFI -ErrorAction Stop) } catch { $modeError = $_.Exception.ToString() }
    $variable = [StorageHostReadNative]::ReadSecureBootVariable()
    if (-not [bool]$variable.supported) { throw "SecureBoot variable snapshot failed Win32Error=$($variable.win32Error) $($variable.win32ErrorText) privilegeError=$($variable.privilegeError)" }
    if ($null -eq $mode -or [bool]$mode -ne $ExpectedEnabled) { throw "Secure Boot mode mismatch: expected=$ExpectedEnabled actual=$mode error=$modeError" }
    $expectedValue = if ($ExpectedEnabled) { '01' } else { '00' }
    if ([string]$variable.value -cne $expectedValue -or [long]$variable.size -ne 1) { throw "SecureBoot variable mismatch: expected one byte $expectedValue; actual value=$($variable.value) size=$($variable.size) attributes=$($variable.attributes)" }
    return [pscustomobject][ordered]@{ secureBootMode=$mode; secureBootModeError=$modeError; variable=$variable; variableGuid='8be4df61-93ca-11d2-aa0d-00e098032b8c'; readOnly=$true }
}

function Get-CombinedFirmwareSnapshot {
    param([Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$Worktree,[Parameter(Mandatory=$true)][string]$OutputDirectory)
    $snapshotLibrary = Join-Path $Worktree 'spoof-handoff\tools\smbios_boot\snapshot-reference.ps1'
    if (-not (Test-Path -LiteralPath $snapshotLibrary -PathType Leaf)) { throw "Firmware snapshot reader is missing: $snapshotLibrary" }
    . $snapshotLibrary -RepoRoot $Repository -OutputBoundary (Join-Path $Repository 'spoof-handoff') -LibraryOnly
    Initialize-ReadyFirmwareReader
    [ReadyToArmFirmwareRead]::Enable()
    $order = Read-VariableRecord 'BootOrder' $globalGuid
    $next = Read-VariableRecord 'BootNext' $globalGuid
    $current = Read-VariableRecord 'BootCurrent' $globalGuid
    $bootEntries = @(Read-ReadyBootEntries $order $next $current $globalGuid)
    foreach ($name in @('SecureBoot','SetupMode','AuditMode','DeployedMode','PK','KEK')) { $null = Read-VariableRecord $name $globalGuid }
    foreach ($name in @('db','dbx')) { $null = Read-VariableRecord $name $imageGuid }
    foreach ($name in @('MokListRT','MokListXRT','MokSBStateRT','SbatLevelRT','SbatLevel','SbatPolicy','MokIgnoreDB')) { $null = Read-VariableRecord $name $shimGuid }
    $records = @($firmwareRecords | ForEach-Object { [pscustomobject][ordered]@{name=$_.name;guid=$_.guid;attributes=$_.attributes;present=$_.present;win32Error=$_.win32Error;size=$_.size;sha256=$_.sha256;base64=$_.base64} })
    $directory = [IO.Path]::GetFullPath($OutputDirectory)
    [void][IO.Directory]::CreateDirectory($directory)
    $path = Join-Path $directory 'firmware-records.json'
    Write-CombinedHostAtomicJson $path ([ordered]@{schema='combined-host-firmware-snapshot.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');records=$records;bootEntries=$bootEntries;writeAttempted=$false;secureBootMode=[bool](Confirm-SecureBootUEFI)})
    return [pscustomobject][ordered]@{path=$path;sha256=(Get-CombinedHostSha256 $path);records=$records;bootEntries=$bootEntries;secureBootMode=[bool](Confirm-SecureBootUEFI)}
}

function Assert-CombinedFirmwareUnchanged {
    param([Parameter(Mandatory=$true)][object]$Snapshot)
    . (Join-Path $WorktreeRoot 'spoof-handoff\tools\smbios_boot\snapshot-reference.ps1') -RepoRoot $RepositoryRoot -OutputBoundary (Join-Path $RepositoryRoot 'spoof-handoff') -LibraryOnly
    Initialize-ReadyFirmwareReader
    [ReadyToArmFirmwareRead]::Enable()
    foreach ($record in @($Snapshot.records)) {
        [uint32]$attributes=0; [int]$errorCode=0
        $bytes=[ReadyToArmFirmwareRead]::Read([string]$record.name,[string]$record.guid,[ref]$attributes,[ref]$errorCode)
        $encoded=if($null -ne $bytes){[Convert]::ToBase64String($bytes)}else{$null}
        if ($errorCode -ne [int]$record.win32Error -or $attributes -ne [uint32]$record.attributes -or $encoded -cne [string]$record.base64) {
            throw "Firmware record changed: $([string]$record.name) expectedError=$([int]$record.win32Error) actualError=$errorCode expectedAttributes=$([uint32]$record.attributes) actualAttributes=$attributes expectedBytes=$([string]$record.base64) actualBytes=$encoded"
        }
    }
    if ([bool](Confirm-SecureBootUEFI) -ne [bool]$Snapshot.secureBootMode) { throw "Secure Boot mode changed: expected=$([bool]$Snapshot.secureBootMode) actual=$([bool](Confirm-SecureBootUEFI))." }
    return $true
}

function Invoke-CombinedOriginalPrepare {
    param([string]$Repository,[string]$Worktree,[string]$VmGate,[string]$SmbiosReader,[string]$Release,[string]$Recovery,[string]$NicReader,[string]$NicNativeReader)
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Prepare requires an elevated PowerShell session to read the live boot and EFI state.' }
    $reportsRoot = Join-Path $Repository 'spoof-handoff\reports\combined-host-20261018'
    [void][IO.Directory]::CreateDirectory($reportsRoot)
    $runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
    $runDirectory = Join-Path $reportsRoot $runId
    [void][IO.Directory]::CreateDirectory($runDirectory)
    $mapPath = Join-Path $Repository 'spoof-handoff\reports\client-originals.tsv'
    $map = New-CombinedBindingMap (Get-CombinedMapRows $mapPath)
    $gateEvidence = $null
    if (-not [string]::IsNullOrWhiteSpace($VmGate)) { $gateEvidence = Assert-CombinedVmGateEvidence -Repository $Repository -Worktree $Worktree -GatePath $VmGate }
    if ([string]::IsNullOrWhiteSpace($SmbiosReader) -or -not (Test-Path -LiteralPath $SmbiosReader -PathType Leaf)) { throw 'Prepare requires -SmbiosReaderPath pointing to the pinned seven-field Windows RSMB/WMI readback executable.' }
    $SmbiosReader = [IO.Path]::GetFullPath($SmbiosReader)
    $nicReaderResolved = if ($NicReader) { [IO.Path]::GetFullPath($NicReader) } else { Join-Path $Worktree 'spoof-handoff\tools\nic_vm_probe\read-host.ps1' }
    $nicNativeResolved = if ($NicNativeReader) { [IO.Path]::GetFullPath($NicNativeReader) } else { Join-Path $Repository 'spoof-handoff\artifacts\nic-reader-gap-20261017\native-build-vs18\Release\nic_native_read.exe' }
    foreach ($readerPath in @($nicReaderResolved,$nicNativeResolved)) { if (-not (Test-Path -LiteralPath $readerPath -PathType Leaf)) { throw "Combined host reader is missing: $readerPath" } }
    $original = Invoke-CombinedReadback -RepositoryRoot $Repository -OutputDirectory 'spoof-handoff\reports\combined-host-20261018\o' -ExpectedMode Original -SmbiosReaderPath $SmbiosReader -NicReaderPath $nicReaderResolved -NicNativeExePath $nicNativeResolved -LockedMapPath $mapPath
    $bcdSnapshot = Get-CombinedBcdSnapshot -Repository $Repository -EvidenceDirectory (Join-Path $runDirectory 'boot-snapshot')
    $espSnapshot = Get-CombinedEspSnapshot -EvidenceDirectory (Join-Path $runDirectory 'boot-snapshot') -BcdSnapshot $bcdSnapshot
    $firmwareVariable = Get-CombinedSecureBootVariableSnapshot -Worktree $Worktree
    $firmwareSnapshot = Get-CombinedFirmwareSnapshot -Repository $Repository -Worktree $Worktree -OutputDirectory (Join-Path $runDirectory 'boot-snapshot\firmware')
    $gateReady = $null -ne $gateEvidence
    $executionSourcePins = @(
        foreach ($source in @(
            [pscustomobject]@{path=(Join-Path $Worktree 'spoof-handoff\tools\combined_host\run.ps1');name='combined-host-runner'},
            [pscustomobject]@{path=(Join-Path $Worktree 'spoof-handoff\tools\combined_host\nic-transaction.ps1');name='combined-nic-transaction'},
            [pscustomobject]@{path=(Join-Path $Worktree 'spoof-handoff\tools\combined_host\build-host-payload.ps1');name='combined-host-payload-builder'},
            [pscustomobject]@{path=(Join-Path $Repository 'tools\efi_inventory\path-b-collector.ps1');name='path-b-child'},
            [pscustomobject]@{path=(Join-Path $Repository $script:CombinedPathBSourceManifestRelative.Replace('/','\'));name='path-b-source-manifest'}
        )) {
            if (-not (Test-Path -LiteralPath $source.path -PathType Leaf)) { throw "Combined execution source is missing: $($source.path)" }
            [pscustomobject][ordered]@{name=$source.name;path=[IO.Path]::GetFullPath($source.path);sha256=(Get-CombinedHostSha256 $source.path)}
        }
    )
    if ($gateReady) {
        $gatePathBPin = Get-CombinedValue $gateEvidence 'pathBSourcePin'
        $preparedPathBPin = @($executionSourcePins | Where-Object { $_.name -ceq 'path-b-child' })
        if ($preparedPathBPin.Count -ne 1 -or $null -eq $gatePathBPin -or [IO.Path]::GetFullPath([string](Get-CombinedValue $gatePathBPin 'path')) -cne [IO.Path]::GetFullPath([string]$preparedPathBPin[0].path) -or [string](Get-CombinedValue $gatePathBPin 'sha256') -cne [string]$preparedPathBPin[0].sha256) {
            throw 'Prepared Path-B execution source does not match the source pinned by the validated VM gate.'
        }
        $gateManifestPin = Get-CombinedValue $gateEvidence 'pathBSourceManifest'
        $preparedManifestPin = @($executionSourcePins | Where-Object { $_.name -ceq 'path-b-source-manifest' })
        if ($preparedManifestPin.Count -ne 1 -or $null -eq $gateManifestPin -or [IO.Path]::GetFullPath([string](Get-CombinedValue $gateManifestPin 'path')) -cne [IO.Path]::GetFullPath([string]$preparedManifestPin[0].path) -or [string](Get-CombinedValue $gateManifestPin 'sha256') -cne [string]$preparedManifestPin[0].sha256) {
            throw 'Prepared Path-B source manifest does not match the source set pinned by the validated VM gate.'
        }
    }
    $hostPayload = $null
    if ($gateReady) {
        $hostPayloadDirectory = Join-Path $Repository ('spoof-handoff\artifacts\chp\' + $runId)
        $hostPayloadBuilder = Join-Path $Worktree 'spoof-handoff\tools\combined_host\build-host-payload.ps1'
        $hostPayloadBuild = Invoke-CombinedPowerShellChild -ScriptPath $hostPayloadBuilder -Arguments @('-RepositoryRoot',$Repository,'-WorktreeRoot',$Worktree,'-OutputDirectory',$hostPayloadDirectory,'-PathBSourceManifestPath',(Join-Path $Repository $script:CombinedPathBSourceManifestRelative.Replace('/','\'))) -LogDirectory (Join-Path $runDirectory 'actions') -Name 'build-host-smbios-payload'
        $hostPayloadSummary = $hostPayloadBuild.stdout | ConvertFrom-Json -ErrorAction Stop
        if ([string]$hostPayloadSummary.status -cne 'HOST_PAYLOAD_BUILD_PASS' -or [int]$hostPayloadSummary.activeBootMask -ne 127 -or [int]$hostPayloadSummary.ctestPassed -ne 2) { throw "Fresh host payload builder did not return a seven-field/two-test PASS: $($hostPayloadBuild.stdout)" }
        $hostPayload = [ordered]@{
            verdictPath=[string]$hostPayloadSummary.verdictPath
            verdictSha256=[string]$hostPayloadSummary.verdictSha256
            payloadPath=[string]$hostPayloadSummary.payloadPath
            payloadSha256=[string]$hostPayloadSummary.payloadSha256
            buildId=[string]$hostPayloadSummary.buildId
            sourceSetSha256=[string]$hostPayloadSummary.sourceSetSha256
            activeBootMask=[int]$hostPayloadSummary.activeBootMask
            ctestPassed=[int]$hostPayloadSummary.ctestPassed
            buildEvidence=$hostPayloadBuild
        }
        $validatedHostPayload = Assert-CombinedHostPayload -Reference ([pscustomobject]$hostPayload) -Repository $Repository -Worktree $Worktree
        if ($validatedHostPayload.payloadSha256 -cne $hostPayload.payloadSha256) { throw 'Fresh host payload failed immediate post-build validation.' }
    }
    $storagePrepare = $null
    if ($gateReady) {
        if ([string]::IsNullOrWhiteSpace($Release)) { throw 'A passing combined VM gate requires an explicit pinned storage ReleaseDirectory before external recovery preparation.' }
        $storageRunner = Join-Path $Worktree 'spoof-handoff\tools\storage_host\run.ps1'
        $storageArgs = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$storageRunner,'-Action','Prepare','-RepositoryRoot',$Repository,'-ReleaseDirectory',$Release,'-RecoveryDirectory',$Recovery)
        $storageLines = @(& (Join-Path $PSHOME 'powershell.exe') @storageArgs 2>&1 | ForEach-Object { [string]$_ })
        $storageExit = $LASTEXITCODE
        if ($storageExit -ne 0) { throw "Storage recovery snapshot Prepare failed exit=$storageExit output=$($storageLines -join ' | ')" }
        $storageJson = ($storageLines -join "`n") | ConvertFrom-Json -ErrorAction Stop
        if ([string]$storageJson.status -cne 'PREPARED') { throw "Storage recovery snapshot did not return PREPARED: $($storageLines -join ' | ')" }
        $storagePrepare = $storageJson
    }
    $journal = [ordered]@{
        schema = $script:CombinedHostSchema
        status = if ($gateReady) { 'PREPARED_READONLY_GATE_PASS' } else { 'PREPARED_READONLY_VM_GATE_REQUIRED' }
        createdUtc = [DateTime]::UtcNow.ToString('o')
        repositoryRoot = $Repository
        worktreeRoot = $Worktree
        runDirectory = $runDirectory
        lockedMap = [ordered]@{ path=$mapPath; sha256=(Get-CombinedHostSha256 $mapPath); bindingCount=$map.bindings.Count; excludedDiskTargets=$map.excludedDiskTargets }
        originals = [ordered]@{ path=$original.reportPath; sha256=$original.sha256; status=$original.status }
        vmGate = $gateEvidence
        storageSnapshot = $storagePrepare
        hostPayload = $hostPayload
        storageRelease = if($Release){[ordered]@{directory=[IO.Path]::GetFullPath($Release)}}else{$null}
        bcdSnapshot = $bcdSnapshot
        espSnapshot = $espSnapshot
        secureBootVariable = $firmwareVariable
        firmwareSnapshot = [ordered]@{path=$firmwareSnapshot.path;sha256=$firmwareSnapshot.sha256;records=$firmwareSnapshot.records;bootEntries=$firmwareSnapshot.bootEntries;secureBootMode=$firmwareSnapshot.secureBootMode}
        smbiosReader = [ordered]@{path=$SmbiosReader;sha256=(Get-CombinedHostSha256 $SmbiosReader)}
        nicReader = [ordered]@{path=$nicReaderResolved;sha256=(Get-CombinedHostSha256 $nicReaderResolved)}
        nicNativeReader = [ordered]@{path=$nicNativeResolved;sha256=(Get-CombinedHostSha256 $nicNativeResolved)}
        executionSourcePins = $executionSourcePins
        hostMutationPerformed = $false
        activationAvailable = ($gateReady -and $null -ne $hostPayload -and $null -ne $storagePrepare)
        phase = if ($gateReady -and $null -ne $hostPayload -and $null -ne $storagePrepare) { 'Prepared' } else { 'VmGateRequired' }
        operationInProgress = $false
        lastAction = 'Prepare'
        nicTransaction = $null
        pathBBuild = $null
        pathBBackupPath = $null
        pathBSmbiosReport = $null
        smbiosBootProof = $null
        blockedBoundary = if ($gateReady -and $null -ne $hostPayload -and $null -ne $storagePrepare) { $null } else { 'A current authenticated combined VM gate, fresh host seven-field payload build, and external storage recovery snapshot are required before host mutation.' }
    }
    $journalPath = Join-Path $runDirectory 'journal.json'
    $journal.journalPath = [IO.Path]::GetFullPath($journalPath)
    $journal.updatedUtc = [DateTime]::UtcNow.ToString('o')
    Write-CombinedHostAtomicJson $journalPath $journal
    $summary = [ordered]@{ status=$journal.status; runDirectory=$runDirectory; journalPath=$journalPath; journalSha256=(Get-CombinedHostSha256 $journalPath); originalReadback=$original; vmGate=if($gateReady){$gateEvidence}else{'NOT_PROVIDED'}; storageSnapshot=if($storagePrepare){$storagePrepare.runDirectory}else{$null}; hostPayload=if($hostPayload){[pscustomobject]$hostPayload}else{$null}; hostMutationPerformed=$false; activationAvailable=[bool]$journal.activationAvailable }
    Write-CombinedHostAtomicJson (Join-Path $runDirectory 'prepare-summary.json') $summary
    return $summary
}

function Get-CombinedHostJournal {
    param([string]$Repository,[string]$RunDirectory)
    if ([string]::IsNullOrWhiteSpace($RunDirectory)) { throw 'Status requires -RunDirectory from a prior Prepare.' }
    $root = [IO.Path]::GetFullPath((Join-Path $Repository 'spoof-handoff\reports\combined-host-20261018')).TrimEnd('\') + '\'
    $path = [IO.Path]::GetFullPath($RunDirectory)
    if (-not $path.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)) { throw "RunDirectory must be under combined-host reports: $path" }
    $journalPath = Join-Path $path 'journal.json'
    if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf)) { throw "Combined host journal not found: $journalPath" }
    $journal = ConvertTo-CombinedMutableData (Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json)
    if ([string]$journal.schema -cne $script:CombinedHostSchema) { throw 'Combined host journal schema mismatch.' }
    if ([string]::IsNullOrWhiteSpace([string](Get-CombinedValue $journal 'runDirectory')) -or [IO.Path]::GetFullPath([string]$journal.runDirectory) -cne [IO.Path]::GetFullPath($path)) {
        throw "Combined host journal runDirectory does not match its authoritative location: declared=$([string](Get-CombinedValue $journal 'runDirectory')) actual=$path"
    }
    $declaredJournalPath = [string](Get-CombinedValue $journal 'journalPath')
    if (-not [string]::IsNullOrWhiteSpace($declaredJournalPath) -and [IO.Path]::GetFullPath($declaredJournalPath) -cne [IO.Path]::GetFullPath($journalPath)) {
        throw "Combined host journalPath does not match its authoritative location: declared=$declaredJournalPath actual=$journalPath"
    }
    Set-CombinedHostField -Object $journal -Name 'journalPath' -Value $journalPath
    return [pscustomobject]@{ path=$journalPath; sha256=(Get-CombinedHostSha256 $journalPath); journal=$journal }
}

function Save-CombinedHostJournal {
    param([Parameter(Mandatory=$true)][object]$Journal)
    $runDirectory = [string](Get-CombinedValue $Journal 'runDirectory')
    if ([string]::IsNullOrWhiteSpace($runDirectory)) { throw 'Cannot save a combined host journal without runDirectory.' }
    $authoritativePath = Join-Path ([IO.Path]::GetFullPath($runDirectory)) 'journal.json'
    $declaredPath = [string](Get-CombinedValue $Journal 'journalPath')
    if (-not [string]::IsNullOrWhiteSpace($declaredPath) -and [IO.Path]::GetFullPath($declaredPath) -cne [IO.Path]::GetFullPath($authoritativePath)) {
        throw "Refusing to save the combined journal outside its run directory: declared=$declaredPath expected=$authoritativePath"
    }
    Set-CombinedHostField -Object $Journal -Name 'journalPath' -Value $authoritativePath
    Set-CombinedHostField -Object $Journal -Name 'updatedUtc' -Value ([DateTime]::UtcNow.ToString('o'))
    Write-CombinedHostAtomicJson $authoritativePath $Journal
    return (Get-CombinedHostSha256 $authoritativePath)
}

function Set-CombinedHostField {
    param([Parameter(Mandatory=$true)][object]$Object,[Parameter(Mandatory=$true)][string]$Name,[AllowNull()][object]$Value)
    if ($Object -is [System.Collections.IDictionary]) {
        $Object[$Name] = $Value
        return
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { $Object | Add-Member -MemberType NoteProperty -Name $Name -Value $Value -Force }
    else { $property.Value = $Value }
}

function ConvertTo-CombinedMutableData {
    param([AllowNull()][object]$Value)
    if ($null -eq $Value) { return $null }
    if ($Value -is [System.Collections.IDictionary]) {
        $dictionary = [ordered]@{}
        foreach ($key in $Value.Keys) { $dictionary[[string]$key] = ConvertTo-CombinedMutableData $Value[$key] }
        return ,$dictionary
    }
    if ($Value -is [System.Management.Automation.PSCustomObject]) {
        $dictionary = [ordered]@{}
        foreach ($property in $Value.PSObject.Properties) { $dictionary[[string]$property.Name] = ConvertTo-CombinedMutableData $property.Value }
        return ,$dictionary
    }
    if ($Value -is [Array]) {
        $items = [Collections.Generic.List[object]]::new()
        foreach ($item in $Value) { $items.Add((ConvertTo-CombinedMutableData $item)) }
        return ,$items.ToArray()
    }
    return $Value
}

function Assert-CombinedHostActivationJournal {
    param([Parameter(Mandatory=$true)][object]$Journal,[Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$Worktree)
    if ([bool]$Journal.activationAvailable -ne $true -or $null -eq $Journal.vmGate -or [string]$Journal.vmGate.status -cne 'PASS') {
        throw 'Combined host mutation is blocked: Prepare must validate a complete authenticated PASS VM gate first.'
    }
    if ([string]$Journal.schema -cne $script:CombinedHostSchema -or [string]::IsNullOrWhiteSpace([string]$Journal.storageSnapshot.runDirectory)) {
        throw 'Combined host journal is missing its schema or external storage recovery snapshot.'
    }
    if ($null -eq (Get-CombinedValue $Journal 'hostPayload')) { throw 'Combined host mutation is blocked: Prepare did not build and pin a fresh host-values SMBIOS-seven EFI payload.' }
    $null = Assert-CombinedHostPayload -Reference (Get-CombinedValue $Journal 'hostPayload') -Repository $Repository -Worktree $Worktree
    $gateHash = Get-CombinedHostSha256 ([string]$Journal.vmGate.path)
    if ($gateHash -cne [string]$Journal.vmGate.sha256) { throw 'Validated VM gate evidence changed after Prepare.' }
    $freshGate = Assert-CombinedVmGateEvidence -Repository $Repository -Worktree $Worktree -GatePath ([string]$Journal.vmGate.path)
    if ([string]$freshGate.sha256 -cne [string]$Journal.vmGate.sha256) { throw 'Revalidated VM gate differs from the evidence pinned during Prepare.' }
    $mapPath = [string]$Journal.lockedMap.path
    if ((Get-CombinedHostSha256 $mapPath) -cne [string]$Journal.lockedMap.sha256) { throw 'Locked field map changed after Prepare.' }
    foreach ($readerName in @('smbiosReader','nicReader','nicNativeReader')) {
        $reader = Get-CombinedValue $Journal $readerName
        $path = [string](Get-CombinedValue $reader 'path')
        $expected = [string](Get-CombinedValue $reader 'sha256')
        if ([string]::IsNullOrWhiteSpace($path) -or $expected -notmatch '\A[A-Fa-f0-9]{64}\z' -or -not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Prepared $readerName path or SHA-256 is missing." }
        $actual = Get-CombinedHostSha256 $path
        if ($actual -cne $expected.ToUpperInvariant()) { throw "Prepared $readerName changed after Prepare: expected=$expected actual=$actual path=$path" }
    }
    Assert-CombinedExecutionSourcePins -Journal $Journal -Repository $Repository -Worktree $Worktree | Out-Null
}

function Assert-CombinedExecutionSourcePins {
    param([Parameter(Mandatory=$true)][object]$Journal,[Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$Worktree)
    $expectedSources = @(
        [pscustomobject]@{name='combined-host-runner';path=[IO.Path]::GetFullPath((Join-Path $Worktree 'spoof-handoff\tools\combined_host\run.ps1'))},
        [pscustomobject]@{name='combined-nic-transaction';path=[IO.Path]::GetFullPath((Join-Path $Worktree 'spoof-handoff\tools\combined_host\nic-transaction.ps1'))},
        [pscustomobject]@{name='combined-host-payload-builder';path=[IO.Path]::GetFullPath((Join-Path $Worktree 'spoof-handoff\tools\combined_host\build-host-payload.ps1'))},
        [pscustomobject]@{name='path-b-child';path=[IO.Path]::GetFullPath((Join-Path $Repository 'tools\efi_inventory\path-b-collector.ps1'))},
        [pscustomobject]@{name='path-b-source-manifest';path=[IO.Path]::GetFullPath((Join-Path $Repository $script:CombinedPathBSourceManifestRelative.Replace('/','\')))}
    )
    $pins = @(Get-CombinedValue $Journal 'executionSourcePins')
    if ($pins.Count -ne $expectedSources.Count) { throw "Prepared execution source pin count mismatch: expected=$($expectedSources.Count) actual=$($pins.Count)." }
    foreach ($source in $expectedSources) {
        $matches = @($pins | Where-Object { [string](Get-CombinedValue $_ 'name') -ceq $source.name })
        if ($matches.Count -ne 1) { throw "Prepared execution source pin is missing or duplicated: $($source.name)." }
        $path = [string](Get-CombinedValue $matches[0] 'path')
        $expected = [string](Get-CombinedValue $matches[0] 'sha256')
        if ([IO.Path]::GetFullPath($path) -cne $source.path -or $expected -notmatch '\A[A-Fa-f0-9]{64}\z' -or -not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Prepared execution source pin is missing or malformed: $($source.name) path=$path" }
        $actual = Get-CombinedHostSha256 $path
        if ($actual -cne $expected.ToUpperInvariant()) { throw "Prepared execution source changed after Prepare: name=$($source.name) expected=$expected actual=$actual path=$path" }
        if ($source.name -ceq 'path-b-child') {
            $gatePin = Get-CombinedValue (Get-CombinedValue $Journal 'vmGate') 'pathBSourcePin'
            if ($null -eq $gatePin -or [IO.Path]::GetFullPath([string](Get-CombinedValue $gatePin 'path')) -cne $source.path -or [string](Get-CombinedValue $gatePin 'sha256') -cne $actual) {
                throw "Prepared Path-B script does not match the validated VM gate source pin: $($source.path)"
            }
        }
        if ($source.name -ceq 'path-b-source-manifest') {
            $null = Assert-CombinedPathBSourceManifest -Repository $Repository -Path $source.path -ExpectedFileSha256 $actual
            $gateManifest = Get-CombinedValue (Get-CombinedValue $Journal 'vmGate') 'pathBSourceManifest'
            if ([bool](Get-CombinedValue $Journal 'activationAvailable') -and ($null -eq $gateManifest -or [IO.Path]::GetFullPath([string](Get-CombinedValue $gateManifest 'path')) -cne $source.path -or [string](Get-CombinedValue $gateManifest 'sha256') -cne $actual)) {
                throw "Prepared Path-B source manifest differs from the validated VM gate: $($source.path)"
            }
        }
    }
    if ([bool](Get-CombinedValue $Journal 'activationAvailable')) {
        $freshGate = Assert-CombinedVmGateEvidence -Repository $Repository -Worktree $Worktree -GatePath ([string](Get-CombinedValue (Get-CombinedValue $Journal 'vmGate') 'path'))
        if ([string]$freshGate.sha256 -cne [string](Get-CombinedValue (Get-CombinedValue $Journal 'vmGate') 'sha256')) { throw 'VM gate changed after Prepare while rechecking Path-B execution pins.' }
    }
    return $true
}

function Assert-CombinedLifecyclePhase {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Action,[Parameter(Mandatory=$true)][string]$Phase)
    $allowed = @{
        'EnableTestMode'=@('Prepared'); 'InstallTransparent'=@('PolicyPendingReboot'); 'VerifyTransparent'=@('TransparentInstallPendingReboot')
        'Remove'=@('TransparentVerified','CombinedReadbackComplete'); 'VerifyOriginal'=@('TransparentRemovalPendingReboot','ReplacementRemovalPendingReboot')
        'InstallReplacement'=@('TransparentRemoved'); 'VerifyReplacement'=@('ReplacementInstallPendingReboot'); 'ArmCombined'=@('ReplacementVerified')
        'CollectCombined'=@('CombinedArmPendingReboot'); 'RestorePolicy'=@('ReplacementRemoved'); 'FinalVerify'=@('PolicyRestorePendingReboot')
    }
    if (-not $allowed.ContainsKey($Action)) { throw "No combined lifecycle transition is defined for action $Action." }
    if ($allowed[$Action] -notcontains $Phase) { throw "Combined lifecycle action $Action is not allowed from phase $Phase." }
    return $true
}

function Invoke-CombinedPowerShellChild {
    param([Parameter(Mandatory=$true)][string]$ScriptPath,[Parameter(Mandatory=$true)][string[]]$Arguments,[Parameter(Mandatory=$true)][string]$LogDirectory,[Parameter(Mandatory=$true)][string]$Name)
    $powershell = Join-Path $PSHOME 'powershell.exe'
    if (-not (Test-Path -LiteralPath $ScriptPath -PathType Leaf) -or -not (Test-Path -LiteralPath $powershell -PathType Leaf)) { throw "Required child script or PowerShell is missing: $ScriptPath" }
    [void][IO.Directory]::CreateDirectory($LogDirectory)
    $stdoutPath = Join-Path $LogDirectory ($Name + '.stdout.txt')
    $stderrPath = Join-Path $LogDirectory ($Name + '.stderr.txt')
    $argumentLine = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$ScriptPath) + $Arguments | ForEach-Object { '"' + ([string]$_).Replace('"','\"') + '"' }
    $process = Start-Process -FilePath $powershell -ArgumentList $argumentLine -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $stdout = if (Test-Path -LiteralPath $stdoutPath -PathType Leaf) { [IO.File]::ReadAllText($stdoutPath) } else { '' }
    $stderr = if (Test-Path -LiteralPath $stderrPath -PathType Leaf) { [IO.File]::ReadAllText($stderrPath) } else { '' }
    $evidence = [pscustomobject][ordered]@{name=$Name;script=$ScriptPath;arguments=$Arguments;exitCode=[int]$process.ExitCode;stdoutPath=$stdoutPath;stderrPath=$stderrPath;stdout=$stdout;stderr=$stderr}
    if ($process.ExitCode -ne 0) { throw "Child action '$Name' failed exit=$($process.ExitCode) stdout=$stdout stderr=$stderr" }
    return $evidence
}

function Invoke-CombinedStorageAction {
    param([Parameter(Mandatory=$true)][object]$Journal,[Parameter(Mandatory=$true)][string]$Action)
    $runner = Join-Path $WorktreeRoot 'spoof-handoff\tools\storage_host\run.ps1'
    $arguments = @('-Action',$Action,'-RepositoryRoot',$RepositoryRoot,'-RunDirectory',[string]$Journal.storageSnapshot.runDirectory,'-ReleaseDirectory',[string]$Journal.storageRelease.directory)
    $evidence = Invoke-CombinedPowerShellChild -ScriptPath $runner -Arguments $arguments -LogDirectory (Join-Path $Journal.runDirectory 'actions') -Name ('storage-' + $Action.ToLowerInvariant())
    $result = $evidence.stdout | ConvertFrom-Json -ErrorAction Stop
    if ($null -eq $result) { throw "Storage action $Action emitted no result JSON." }
    return [pscustomobject][ordered]@{result=$result;evidence=$evidence}
}

function Get-CombinedCurrentBootIdentity {
    $rows = @(Get-CimInstance -ClassName Win32_OperatingSystem -OperationTimeoutSec 15 -ErrorAction Stop)
    if ($rows.Count -ne 1 -or $null -eq $rows[0].LastBootUpTime) { throw 'Cannot establish current Windows boot identity.' }
    return $rows[0].LastBootUpTime.ToUniversalTime().ToString('o')
}

function Assert-CombinedTestSigning {
    param([bool]$Expected)
    $directory = Join-Path $env:TEMP ('combined-bcd-' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($directory)
    try { $capture = Invoke-CombinedBcdCapture 'current' @('/enum','{current}','/v') $directory }
    finally { if (Test-Path -LiteralPath $directory -PathType Container) { Remove-Item -LiteralPath $directory -Recurse -Force } }
    if ($capture.exitCode -ne 0) { throw "Could not verify effective BCD testsigning state: exit=$($capture.exitCode) stdout=$($capture.stdout) stderr=$($capture.stderr)" }
    $line = @($capture.stdout -split "`r?`n" | Where-Object { $_ -match '^\s*testsigning\s+' })
    $actual = $line.Count -eq 1 -and $line[0] -match '(?i)\byes\b'
    if ([bool]$actual -ne $Expected) { throw "BCD testsigning mismatch: expected=$Expected actual=$actual line=$($line -join '; ')" }
    return [pscustomobject]@{expected=$Expected;actual=[bool]$actual;line=if($line.Count){$line[0].Trim()}else{$null}}
}

function Invoke-CombinedPathB {
    param([Parameter(Mandatory=$true)][object]$Journal,[Parameter(Mandatory=$true)][string]$Mode)
    Assert-CombinedExecutionSourcePins -Journal $Journal -Repository $RepositoryRoot -Worktree $WorktreeRoot | Out-Null
    $scriptPath = Join-Path $RepositoryRoot 'tools\efi_inventory\path-b-collector.ps1'
    $buildRoot = Join-Path $RepositoryRoot ('spoof-handoff\artifacts\combined-host\' + [IO.Path]::GetFileName([string]$Journal.runDirectory))
    $loaderBuild = Join-Path $buildRoot 'loader'
    $inventoryBuild = Join-Path $buildRoot 'inventory'
    $arguments = @('-Mode',$Mode,'-LoaderBuildDir',$loaderBuild,'-InventoryBuildDir',$inventoryBuild,'-MountLetter','S')
    $result = Invoke-CombinedPowerShellChild -ScriptPath $scriptPath -Arguments $arguments -LogDirectory (Join-Path $Journal.runDirectory 'actions') -Name ('pathb-' + $Mode.ToLowerInvariant())
    return [pscustomobject][ordered]@{evidence=$result;buildRoot=$buildRoot;loaderBuild=$loaderBuild;inventoryBuild=$inventoryBuild}
}

function Install-CombinedVmPinnedPathBArtifacts {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][object]$Journal,[Parameter(Mandatory=$true)][string]$Repository,[Parameter(Mandatory=$true)][string]$BuildRoot)
    $artifactRoot=[IO.Path]::GetFullPath((Join-Path $Repository 'spoof-handoff\artifacts')).TrimEnd('\')+'\'
    $runLeaf=[IO.Path]::GetFileName([string](Get-CombinedValue $Journal 'runDirectory'))
    $expectedBuildRoot=[IO.Path]::GetFullPath((Join-Path $Repository ('spoof-handoff\artifacts\combined-host\'+$runLeaf)))
    $resolvedBuildRoot=[IO.Path]::GetFullPath($BuildRoot)
    if (-not $resolvedBuildRoot.StartsWith($artifactRoot,[StringComparison]::OrdinalIgnoreCase) -or $resolvedBuildRoot -cne $expectedBuildRoot) { throw "Path-B build output is outside its owned run directory: expected=$expectedBuildRoot actual=$resolvedBuildRoot" }
    $loaderBuild=[IO.Path]::GetFullPath((Join-Path $resolvedBuildRoot 'loader'))
    if (-not $loaderBuild.StartsWith($resolvedBuildRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $loaderBuild -PathType Container)) { throw "Path-B loader build directory is missing or outside its owned run directory: $loaderBuild" }
    $references=@(Get-CombinedValue (Get-CombinedValue $Journal 'vmGate') 'pathBArtifacts')
    $required=@('inventory_loader.efi','ib-install.exe')
    if ($references.Count -ne $required.Count) { throw "VM gate must provide exactly two tested Path-B vehicle artifacts; actual=$($references.Count)." }
    $installed=[Collections.Generic.List[object]]::new()
    foreach ($name in $required) {
        $matches=@($references | Where-Object { [string](Get-CombinedValue $_ 'name') -ceq $name })
        if ($matches.Count -ne 1) { throw "VM gate artifact is missing or duplicated: $name count=$($matches.Count)" }
        $source=[IO.Path]::GetFullPath([string](Get-CombinedValue $matches[0] 'path'))
        $expected=[string](Get-CombinedValue $matches[0] 'sha256')
        if (-not $source.StartsWith($artifactRoot,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $source -PathType Leaf) -or $expected -notmatch '\A[A-Fa-f0-9]{64}\z') { throw "VM-pinned Path-B artifact has an invalid path or SHA-256: name=$name path=$source" }
        $sourceHash=Get-CombinedHostSha256 $source
        if ($sourceHash -cne $expected.ToUpperInvariant()) { throw "VM-pinned Path-B artifact changed: name=$name expected=$expected actual=$sourceHash path=$source" }
        $destination=Join-Path $loaderBuild $name
        if ([IO.Path]::GetFullPath($destination) -ceq $source) { throw "VM-pinned Path-B source and host build destination unexpectedly alias: $source" }
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) { throw "Native Path-B build did not produce expected artifact before VM-pinned replacement: $destination" }
        $rebuiltHash=Get-CombinedHostSha256 $destination
        $backupPath=$null
        if ($rebuiltHash -cne $sourceHash) {
            $backupPath=$destination+'.rebuilt.'+$rebuiltHash.ToLowerInvariant()
            if (Test-Path -LiteralPath $backupPath) { throw "Refusing to overwrite an existing native build evidence copy: $backupPath" }
            [IO.File]::Copy($destination,$backupPath,$false)
            if ((Get-CombinedHostSha256 $backupPath) -cne $rebuiltHash) { throw "Native build evidence copy failed hash verification: $backupPath" }
        }
        $temporaryPath=$destination+'.vm-pinned.'+[Guid]::NewGuid().ToString('N')+'.tmp'
        try {
            [IO.File]::Copy($source,$temporaryPath,$false)
            if ((Get-CombinedHostSha256 $temporaryPath) -cne $sourceHash) { throw "Temporary VM-pinned artifact copy failed verification: name=$name path=$temporaryPath" }
            if ($rebuiltHash -cne $sourceHash) {
                $replaceBackup=$destination+'.replace-backup.'+[Guid]::NewGuid().ToString('N')
                [IO.File]::Replace($temporaryPath,$destination,$replaceBackup)
                if ((Get-CombinedHostSha256 $replaceBackup) -cne $rebuiltHash) { throw "Atomic replacement backup differs from native output: $replaceBackup" }
                Remove-Item -LiteralPath $replaceBackup -Force
            } else {
                Remove-Item -LiteralPath $temporaryPath -Force
            }
            $installedHash=Get-CombinedHostSha256 $destination
            if ($installedHash -cne $sourceHash) { throw "VM-pinned artifact copy failed verification: name=$name expected=$sourceHash actual=$installedHash path=$destination" }
        } catch {
            $copyFailure=$_.Exception.ToString()
            if ($backupPath) {
                try {
                    [IO.File]::Copy($backupPath,$destination,$true)
                    if ((Get-CombinedHostSha256 $destination) -cne $rebuiltHash) { throw "Restored native build artifact differs from its recorded hash: $destination" }
                } catch { throw "Could not install VM-pinned artifact ($copyFailure) or restore native build output ($($_.Exception.ToString())); output=$destination backup=$backupPath" }
            } elseif ($rebuiltHash -ceq $sourceHash) {
                try {
                    [IO.File]::Copy($source,$destination,$true)
                    if ((Get-CombinedHostSha256 $destination) -cne $rebuiltHash) { throw "Restored identical native build artifact differs from its recorded hash: $destination" }
                } catch { throw "Could not verify or restore identical VM-pinned artifact after failure ($copyFailure): $($_.Exception.ToString()); output=$destination" }
            }
            throw $copyFailure
        } finally {
            if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) { Remove-Item -LiteralPath $temporaryPath -Force }
        }
        $installed.Add([pscustomobject][ordered]@{name=$name;source=$source;sourceSha256=$sourceHash;destination=$destination;nativeBuildSha256=$rebuiltHash;installedSha256=(Get-CombinedHostSha256 $destination);nativeBuildBackup=$backupPath})
    }
    return @($installed.ToArray())
}

function Assert-CombinedSmbiosBootReport {
    param([Parameter(Mandatory=$true)][string]$ReportPath,[Parameter(Mandatory=$true)][string]$ExpectedBuildId,[Parameter(Mandatory=$true)][string]$LockedMapPath)
    $lines = [IO.File]::ReadAllLines($ReportPath,[Text.Encoding]::ASCII)
    if ($lines.Count -lt 12 -or $lines[0] -cne 'HSMBIOS001' -or $lines[1] -cne 'mask=127' -or $lines[2] -cne ('build_id=' + $ExpectedBuildId) -or $lines[-2] -cne 'result=applied' -or $lines[-1] -cne 'END') { throw 'SMBIOS boot report is incomplete, failed, or from an unpinned build.' }
    if ($lines[3] -notmatch '^entries=([12])$') { throw 'SMBIOS boot report entry count is malformed.' }
    $entryCount = [int]$Matches[1]
    $mapRows = Get-CombinedMapRows $LockedMapPath
    $fieldMap = @(
        @('system_serial','FakeSMBIOSSerial'),@('system_vendor','FakeSMBIOSManufacturer'),@('system_product','FakeSMBIOSProduct'),
        @('board_serial','FakeMotherboardSerial'),@('board_vendor','FakeMotherboardManufacturer'),@('board_product','FakeMotherboardProduct'),@('system_uuid','FakeSMBIOSUUID')
    )
    if ($lines.Count -ne 6 + 8 * $entryCount) { throw 'SMBIOS boot report has an unexpected record count.' }
    for ($entry=0; $entry -lt $entryCount; $entry++) {
        $offset=4 + 8*$entry
        if ($lines[$offset] -notmatch ('^entry\t' + $entry + '\t[23]\tEfiRuntimeServicesData$')) { throw "SMBIOS boot report entry provenance mismatch at entry=$entry." }
        for ($field=0; $field -lt $fieldMap.Count; $field++) {
            $rows=@($mapRows | Where-Object { [string]$_.member -ceq $fieldMap[$field][1] })
            if ($rows.Count -ne 1) { throw "Locked SMBIOS field is absent or ambiguous: $($fieldMap[$field][1])" }
            $expected=@('field',[string]$entry,$fieldMap[$field][0],[string]$rows[0].original,[string]$rows[0].replacement) -join "`t"
            if ($lines[$offset+1+$field] -cne $expected) { throw "SMBIOS boot field mismatch: expected=[$expected] actual=[$($lines[$offset+1+$field])]" }
        }
    }
    return [pscustomobject][ordered]@{path=$ReportPath;sha256=(Get-CombinedHostSha256 $ReportPath);buildId=$ExpectedBuildId;entries=$entryCount;mask=127;result='applied'}
}

function Assert-CombinedEspMatchesBaseline {
    param([Parameter(Mandatory=$true)][object]$Baseline,[Parameter(Mandatory=$true)][object]$Current)
    $differences=[Collections.Generic.List[string]]::new()
    foreach ($currentPartition in @($Current.partitions)) {
        $oldPartition=@($Baseline.partitions | Where-Object { [int]$_.diskNumber -eq [int]$currentPartition.diskNumber -and [int]$_.partitionNumber -eq [int]$currentPartition.partitionNumber })
        if ($oldPartition.Count -ne 1) { $differences.Add("Unexpected ESP partition is present: disk=$([int]$currentPartition.diskNumber) partition=$([int]$currentPartition.partitionNumber)") }
    }
    foreach ($oldPartition in @($Baseline.partitions)) {
        $part=@($Current.partitions | Where-Object { [int]$_.diskNumber -eq [int]$oldPartition.diskNumber -and [int]$_.partitionNumber -eq [int]$oldPartition.partitionNumber })
        if ($part.Count -ne 1) { $differences.Add("ESP partition identity missing: disk=$($oldPartition.diskNumber) partition=$($oldPartition.partitionNumber)"); continue }
        foreach ($oldFile in @($oldPartition.files | Where-Object { $_.path -notmatch '(?i)EFI/Microsoft/Boot/BCD\.LOG[0-9]*$' })) {
            $newFile=@($part[0].files | Where-Object { [string]::Equals([string]$_.path,[string]$oldFile.path,[StringComparison]::OrdinalIgnoreCase) })
            if ($newFile.Count -ne 1) { $differences.Add("ESP entry missing or duplicated: $([string]$oldFile.path) expectedCount=1 actualCount=$($newFile.Count)"); continue }
            if ([bool]$newFile[0].directory -ne [bool]$oldFile.directory) { $differences.Add("ESP entry type changed: $([string]$oldFile.path) expectedDirectory=$([bool]$oldFile.directory) actualDirectory=$([bool]$newFile[0].directory)"); continue }
            if ($oldFile.verification -ceq 'file-sha256' -and [string]$newFile[0].sha256 -cne [string]$oldFile.sha256) { $differences.Add("ESP file changed: $([string]$oldFile.path) expected=$([string]$oldFile.sha256) actual=$([string]$newFile[0].sha256)") }
        }
        $oldPaths=@($oldPartition.files | ForEach-Object { [string]$_.path } | Where-Object { $_ -notmatch '(?i)EFI/Microsoft/Boot/BCD\.LOG[0-9]*$' })
        $newPaths=@($part[0].files | ForEach-Object { [string]$_.path } | Where-Object { $_ -notmatch '(?i)EFI/Microsoft/Boot/BCD\.LOG[0-9]*$' })
        foreach ($extra in @($newPaths | Where-Object { $oldPaths -notcontains $_ })) { $differences.Add("Unexpected ESP path remains: $extra") }
    }
    if ($differences.Count -gt 0) { throw ('ESP snapshot comparison failed: ' + ($differences -join '; ')) }
    return [pscustomobject]@{match=$true;mutableBootStatus='EFI/Microsoft/Boot/BOOTSTAT.DAT excluded as mutable';bcd='Compared via supported bcdedit enumerations/export, not raw live hive bytes.'}
}

function Restore-CombinedPathBStage {
    param([Parameter(Mandatory=$true)][object]$Journal)
    Assert-CombinedExecutionSourcePins -Journal $Journal -Repository $RepositoryRoot -Worktree $WorktreeRoot | Out-Null
    $scriptPath = Join-Path $RepositoryRoot 'tools\efi_inventory\path-b-collector.ps1'
    $build = $Journal.pathBBuild
    . $scriptPath -LoaderBuildDir ([string]$build.loaderBuild) -InventoryBuildDir ([string]$build.inventoryBuild) -MountLetter 'S' -LibraryOnly
    $mount = Mount-Esp
    try {
        $stageRoot = Join-Path $mount.EspRoot 'EFI\HypersinvInventory'
        $receipt = Read-Receipt
        $expectedStage = [ordered]@{'loader.efi'=[string]$receipt.loaderSha256;'inventory.efi'=[string]$receipt.inventorySha256}
        $manifestPath = Join-Path $stageRoot 'manifest.sha256'
        if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Owned Path-B manifest is missing during stage cleanup.' }
        $manifestExpected = "schema=1`r`nloader.efi size=$([long]$receipt.loaderSize) sha256=$([string]$receipt.loaderSha256)`r`ninventory.efi size=$([long]$receipt.inventorySize) sha256=$([string]$receipt.inventorySha256)`r`n"
        if ([IO.File]::ReadAllText($manifestPath,[Text.Encoding]::ASCII) -cne $manifestExpected) { throw 'Path-B stage manifest changed before owned cleanup.' }
        foreach ($name in @('loader.efi','inventory.efi')) {
            $path=Join-Path $stageRoot $name
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-CombinedHostSha256 $path) -cne [string]$expectedStage[$name]) { throw "Path-B staged file changed before owned cleanup: $name" }
        }
        $backupPath=[string]$Journal.pathBBackupPath
        $baselineFiles=@($Journal.espSnapshot.partitions | Where-Object { [bool]$_.isSystem } | ForEach-Object { @($_.files) } | Where-Object { $_.path -match '(?i)^EFI/HypersinvInventory/' })
        if ($baselineFiles.Count -gt 0) {
            if ($baselineFiles.Count -ne 3 -or [string]::IsNullOrWhiteSpace($backupPath) -or -not (Test-Path -LiteralPath $backupPath -PathType Container)) { throw 'Pre-run Path-B stage exists, but its complete preservation copy is unavailable; current ESP stage was left untouched.' }
            $backupFiles=@(Get-ChildItem -LiteralPath $backupPath -Force -File)
            if ($backupFiles.Count -ne 3 -or @($backupFiles | Where-Object { @('loader.efi','inventory.efi','manifest.sha256') -cnotcontains $_.Name }).Count -gt 0) { throw 'Pre-run Path-B preservation copy has unexpected or missing files; current ESP stage was left untouched.' }
            foreach ($file in $baselineFiles) {
                $name=[IO.Path]::GetFileName([string]$file.path)
                $source=Join-Path $backupPath $name
                if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or (Get-CombinedHostSha256 $source) -cne [string]$file.sha256) { throw "Pre-run Path-B preservation copy does not match snapshot: $([string]$file.path); current ESP stage was left untouched." }
            }
        } elseif (-not [string]::IsNullOrWhiteSpace($backupPath)) {
            throw 'A Path-B backup was recorded even though the snapshot contained no prior stage; refusing ambiguous cleanup.'
        }
        if (Test-Path -LiteralPath $stageRoot -PathType Container) { Remove-OwnedStage $stageRoot }
        if ($backupPath) {
            $originalRoot=Join-Path $backupPath ''
            [void][IO.Directory]::CreateDirectory($stageRoot)
            foreach ($name in @('loader.efi','inventory.efi','manifest.sha256')) {
                $source=Join-Path $originalRoot $name
                if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Path-B preservation copy lacks ${name}: $source" }
                Copy-Item -LiteralPath $source -Destination (Join-Path $stageRoot $name)
            }
            foreach ($name in @('loader.efi','inventory.efi','manifest.sha256')) {
                $relative=('EFI/HypersinvInventory/' + $name)
                $baseline=@($baselineFiles | Where-Object { [string]::Equals([string]$_.path,$relative,[StringComparison]::OrdinalIgnoreCase) })
                $restored=Join-Path $stageRoot $name
                if ($baseline.Count -ne 1 -or (Get-CombinedHostSha256 $restored) -cne [string]$baseline[0].sha256) { throw "Restored original Path-B stage file differs from the Prepare snapshot: $relative" }
            }
        }
        $bootManager=Join-Path $mount.EspRoot 'EFI\Microsoft\Boot\bootmgfw.efi'
        if ((Get-CombinedHostSha256 $bootManager) -cne [string]$receipt.bootManagerSha256) { throw 'Windows Boot Manager hash changed during Path-B stage restore.' }
        $patch=Join-Path $mount.EspRoot 'HypersinvSmbiosPatch.txt'
        if (Test-Path -LiteralPath $patch -PathType Leaf) {
            $patchEvidence=Get-CombinedValue $Journal 'pathBSmbiosReport'
            $expectedPatch=[string](Get-CombinedValue $patchEvidence 'sha256')
            $actualPatch=Get-CombinedHostSha256 $patch
            if ([string]::IsNullOrWhiteSpace($expectedPatch)) {
                $validated=Assert-CombinedSmbiosBootReport -ReportPath $patch -ExpectedBuildId ([string]$Journal.pathBBuild.buildId) -LockedMapPath ([string]$Journal.lockedMap.path)
                $expectedPatch=[string]$validated.sha256
            }
            if ($actualPatch -cne $expectedPatch) { throw "SMBIOS report on ESP differs from the collected receipt; expected=$expectedPatch actual=$actualPatch" }
            Remove-Item -LiteralPath $patch -Force
        }
        return [pscustomobject][ordered]@{restored=$true;originalStagePresent=[bool]$backupPath;backupPath=$backupPath;bootManagerSha256=(Get-CombinedHostSha256 $bootManager)}
    } finally { Dismount-Esp $mount }
}

function Assert-CombinedSmbiosReportAbsent {
    param([Parameter(Mandatory=$true)][object]$Journal)
    $baseline=@($Journal.espSnapshot.partitions | ForEach-Object { @($_.files) } | Where-Object { [string]::Equals([string]$_.path,'HypersinvSmbiosPatch.txt',[StringComparison]::OrdinalIgnoreCase) })
    if ($baseline.Count -ne 0) { throw 'The SMBIOS EFI report path existed before this run; preserve and review it before arming.' }
    Assert-CombinedExecutionSourcePins -Journal $Journal -Repository $RepositoryRoot -Worktree $WorktreeRoot | Out-Null
    $pathScript=Join-Path $RepositoryRoot 'tools\efi_inventory\path-b-collector.ps1'
    . $pathScript -LoaderBuildDir ([string]$Journal.pathBBuild.loaderBuild) -InventoryBuildDir ([string]$Journal.pathBBuild.inventoryBuild) -MountLetter 'S' -LibraryOnly
    $mount=Mount-Esp
    try {
        $report=Join-Path $mount.EspRoot 'HypersinvSmbiosPatch.txt'
        if (Test-Path -LiteralPath $report) { throw "SMBIOS EFI report path is already occupied: $report" }
    } finally { Dismount-Esp $mount }
}

function Copy-CombinedEspSmbiosReport {
    param([Parameter(Mandatory=$true)][object]$Journal)
    Assert-CombinedExecutionSourcePins -Journal $Journal -Repository $RepositoryRoot -Worktree $WorktreeRoot | Out-Null
    $pathScript=Join-Path $RepositoryRoot 'tools\efi_inventory\path-b-collector.ps1'
    . $pathScript -LoaderBuildDir ([string]$Journal.pathBBuild.loaderBuild) -InventoryBuildDir ([string]$Journal.pathBBuild.inventoryBuild) -MountLetter 'S' -LibraryOnly
    $mount=Mount-Esp
    try {
        $source=Join-Path $mount.EspRoot 'HypersinvSmbiosPatch.txt'
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw 'SMBIOS EFI readback report is absent from the ESP.' }
        $destination=Join-Path $Journal.runDirectory 'HypersinvSmbiosPatch.txt'
        Copy-Item -LiteralPath $source -Destination $destination -Force
        $before=Get-CombinedHostSha256 $source; $copyHash=Get-CombinedHostSha256 $destination; $after=Get-CombinedHostSha256 $source
        if ($before -cne $copyHash -or $before -cne $after) { throw "SMBIOS EFI report changed during copy: before=$before copied=$copyHash after=$after" }
        return [pscustomobject][ordered]@{path=$destination;sha256=$copyHash;espPath=$source}
    } finally { Dismount-Esp $mount }
}

function Invoke-CombinedLifecycleAction {
    param([string]$Repository,[string]$Worktree,[string]$RunDirectory,[string]$Action)
    . (Join-Path $PSScriptRoot 'nic-transaction.ps1')
    $loaded=Get-CombinedHostJournal -Repository $Repository -RunDirectory $RunDirectory
    $journal=$loaded.journal
    Assert-CombinedHostActivationJournal -Journal $journal -Repository $Repository -Worktree $Worktree
    if ([bool]$journal.operationInProgress) { throw "Previous combined action is incomplete; use rollback before continuing. phase=$([string]$journal.phase)" }
    $phase=[string]$journal.phase
    Assert-CombinedLifecyclePhase -Action $Action -Phase $phase | Out-Null
    $result=$null
    switch ($Action) {
        'EnableTestMode' {
            if ($phase -cne 'Prepared') { throw "EnableTestMode requires phase Prepared; actual=$phase" }
            $sb=Get-CombinedSecureBootVariableSnapshot -Worktree $Worktree -ExpectedEnabled $false
            $journal.operationInProgress=$true; $journal.lastAction='EnableTestMode'; $null=Save-CombinedHostJournal $journal
            $result=Invoke-CombinedStorageAction $journal 'EnableTestMode'
            $journal.phase='PolicyPendingReboot'; $journal.operationInProgress=$false; $journal.lastStorageAction=$result; $journal.hostMutationPerformed=$true
            $journal.secureBootOffObservation=$sb
            $journal.policyMutationBootIdentity=Get-CombinedCurrentBootIdentity
        }
        'InstallTransparent' {
            if ($phase -cne 'PolicyPendingReboot') { throw "InstallTransparent requires a completed reboot after EnableTestMode; actual=$phase" }
            if ((Get-CombinedCurrentBootIdentity) -ceq [string]$journal.policyMutationBootIdentity) { throw 'InstallTransparent requires a new Windows boot after test-signing was enabled.' }
            $null=Assert-CombinedTestSigning $true
            $journal.operationInProgress=$true; $null=Save-CombinedHostJournal $journal
            $result=Invoke-CombinedStorageAction $journal 'InstallTransparent'
            $journal.phase='TransparentInstallPendingReboot'; $journal.operationInProgress=$false; $journal.lastStorageAction=$result
        }
        'VerifyTransparent' {
            if ($phase -cne 'TransparentInstallPendingReboot') { throw "VerifyTransparent requires a reboot after transparent installation; actual=$phase" }
            $result=Invoke-CombinedStorageAction $journal 'VerifyTransparent'
            $journal.phase='TransparentVerified'; $journal.operationInProgress=$false; $journal.lastStorageAction=$result
        }
        'Remove' {
            if ($phase -notin @('TransparentVerified','CombinedReadbackComplete')) { throw "Remove requires verified transparent or combined replacement state; actual=$phase" }
            $storageAction=if($phase -ceq 'TransparentVerified'){'Remove'}else{'Remove'}
            $journal.operationInProgress=$true; $journal.lastAction='Remove'; $null=Save-CombinedHostJournal $journal
            $result=Invoke-CombinedStorageAction $journal $storageAction
            $journal.phase=if($phase -ceq 'TransparentVerified'){'TransparentRemovalPendingReboot'}else{'ReplacementRemovalPendingReboot'}
            $journal.operationInProgress=$false; $journal.lastStorageAction=$result
        }
        'VerifyOriginal' {
            if ($phase -notin @('TransparentRemovalPendingReboot','ReplacementRemovalPendingReboot')) { throw "VerifyOriginal requires a reboot after filter removal; actual=$phase" }
            $result=Invoke-CombinedStorageAction $journal 'VerifyOriginal'
            $journal.phase=if($phase -ceq 'TransparentRemovalPendingReboot'){'TransparentRemoved'}else{'ReplacementRemoved'}
            $journal.operationInProgress=$false; $journal.lastStorageAction=$result
        }
        'InstallReplacement' {
            if ($phase -cne 'TransparentRemoved') { throw "InstallReplacement requires a verified transparent removal; actual=$phase" }
            $journal.operationInProgress=$true; $null=Save-CombinedHostJournal $journal
            $result=Invoke-CombinedStorageAction $journal 'InstallReplacement'
            $journal.phase='ReplacementInstallPendingReboot'; $journal.operationInProgress=$false; $journal.lastStorageAction=$result
        }
        'VerifyReplacement' {
            if ($phase -cne 'ReplacementInstallPendingReboot') { throw "VerifyReplacement requires a reboot after replacement install; actual=$phase" }
            $result=Invoke-CombinedStorageAction $journal 'VerifyReplacement'
            $journal.phase='ReplacementVerified'; $journal.operationInProgress=$false; $journal.lastStorageAction=$result
            $journal.replacementVerifiedBootIdentity=Get-CombinedCurrentBootIdentity
        }
        'ArmCombined' {
            if ($phase -cne 'ReplacementVerified') { throw "ArmCombined requires a verified storage replacement; actual=$phase" }
            $null=Get-CombinedSecureBootVariableSnapshot -Worktree $Worktree -ExpectedEnabled $false
            $null=Assert-CombinedTestSigning $true
            $bootBefore=Get-CombinedCurrentBootIdentity
            $journal.phase='CombinedArmIntent'; $journal.operationInProgress=$true; $journal.armBootIdentity=$bootBefore; $null=Save-CombinedHostJournal $journal
            $pathBuild=Invoke-CombinedPathB -Journal $journal -Mode 'Build'
            Assert-CombinedExecutionSourcePins -Journal $journal -Repository $Repository -Worktree $Worktree | Out-Null
            $pinnedVehicleArtifacts=Install-CombinedVmPinnedPathBArtifacts -Journal $journal -Repository $Repository -BuildRoot $pathBuild.buildRoot
            $hostPayloadValidation=Assert-CombinedHostPayload -Reference $journal.hostPayload -Repository $Repository -Worktree $Worktree
            $buildVerdictPath=[string]$journal.hostPayload.verdictPath
            $buildVerdict=Get-Content -LiteralPath $buildVerdictPath -Raw | ConvertFrom-Json
            $payload=[string]$journal.hostPayload.payloadPath
            $builtCollector=Join-Path $pathBuild.inventoryBuild 'efi\inventory.efi'
            if ([string]$buildVerdict.result -cne 'PASS' -or [int]$buildVerdict.activeBootMask -ne 127 -or [string]$buildVerdict.buildId -cne [string]$journal.hostPayload.buildId -or -not (Test-Path -LiteralPath $payload -PathType Leaf) -or -not (Test-Path -LiteralPath $builtCollector -PathType Leaf)) { throw 'Fresh host SMBIOS-seven payload verdict or Path-B collector destination is missing or inconsistent.' }
            Copy-Item -LiteralPath $payload -Destination $builtCollector -Force
            if ((Get-CombinedHostSha256 $builtCollector) -cne [string]$journal.hostPayload.payloadSha256) { throw 'The collector copied into Path-B differs from the fresh prepared host payload.' }
            $journal.pathBBuild=[ordered]@{loaderBuild=$pathBuild.loaderBuild;inventoryBuild=$pathBuild.inventoryBuild;loaderSha256=(Get-CombinedHostSha256 (Join-Path $pathBuild.loaderBuild 'inventory_loader.efi'));collectorSha256=(Get-CombinedHostSha256 $builtCollector);payloadSha256=(Get-CombinedHostSha256 $payload);pinnedVehicleArtifacts=$pinnedVehicleArtifacts;buildId=[string]$buildVerdict.buildId;activeBootMask=[int]$buildVerdict.activeBootMask;buildVerdictPath=$buildVerdictPath;buildVerdictSha256=(Get-CombinedHostSha256 $buildVerdictPath);hostPayloadValidation=$hostPayloadValidation}
            $null=Save-CombinedHostJournal $journal
            Assert-CombinedSmbiosReportAbsent -Journal $journal
            $stage=Invoke-CombinedPathB -Journal $journal -Mode 'Stage'
            $preserved=@($stage.evidence.stdout -split "`r?`n" | Where-Object { $_ -match '^PRESERVED previous stage at ' })
            $journal.pathBBackupPath=if($preserved.Count -eq 1){($preserved[0] -replace '^PRESERVED previous stage at ','').Trim()}else{$null}
            $journal.pathBStageEvidence=$stage.evidence
            $null=Save-CombinedHostJournal $journal
            $arm=Invoke-CombinedPathB -Journal $journal -Mode 'Arm'
            $journal.phase='CombinedArmPendingReboot'; $journal.operationInProgress=$false; $journal.hostMutationPerformed=$true; $journal.pathBArmEvidence=$arm.evidence
            $journal.armedUtc=[DateTime]::UtcNow.ToString('o')
            $result=[ordered]@{status='COMBINED_ARMED_NO_REBOOT';stage=$stage.evidence.stdout;arm=$arm.evidence.stdout;rebootRequiredByUser=$true;runDirectory=$journal.runDirectory}
        }
        'CollectCombined' {
            if ($phase -cne 'CombinedArmPendingReboot') { throw "CollectCombined requires the armed Path-B reboot; actual=$phase" }
            $bootNow=Get-CombinedCurrentBootIdentity
            if ($bootNow -ceq [string]$journal.armBootIdentity) { throw 'CollectCombined requires a new Windows boot after ArmCombined.' }
            $null=Assert-CombinedTestSigning $true
            $journal.operationInProgress=$true; $journal.lastAction='CollectCombined'; $journal.collectBootIdentity=$bootNow; $null=Save-CombinedHostJournal $journal
            $storageStatus=Invoke-CombinedStorageAction $journal 'Status'
            if ([string]$storageStatus.result.phase -cne 'ReplacementVerified') { throw "Storage runner is not in ReplacementVerified phase after combined boot; actual=$([string]$storageStatus.result.phase)" }
            if ($bootNow -ceq [string]$journal.replacementVerifiedBootIdentity) { throw 'Combined boot identity did not change after the replacement filter verification checkpoint.' }
            $storageVerified=[pscustomobject]@{phase=$storageStatus.result.phase;bootIdentity=$bootNow;previousVerifiedBootIdentity=[string]$journal.replacementVerifiedBootIdentity}
            $pathBuild=$journal.pathBBuild
            $patchEvidence=Copy-CombinedEspSmbiosReport -Journal $journal
            $journal.pathBSmbiosReport=$patchEvidence; $null=Save-CombinedHostJournal $journal
            $smbiosProof=Assert-CombinedSmbiosBootReport -ReportPath $patchEvidence.path -ExpectedBuildId ([string]$pathBuild.buildId) -LockedMapPath ([string]$journal.lockedMap.path)
            $journal.smbiosBootProof=$smbiosProof; $null=Save-CombinedHostJournal $journal
            $collect=Invoke-CombinedPathB -Journal $journal -Mode 'Collect'
            $stageRestore=Restore-CombinedPathBStage -Journal $journal
            $nicSupport=Import-CombinedNicHelperBundle -RepositoryRoot $Repository
            $mapRows=Get-CombinedMapRows ([string]$journal.lockedMap.path)
            $bindings=@((New-CombinedBindingMap $mapRows).bindings | Where-Object { $_.category -ceq 'NIC' })
            $nicJournalPath=Join-Path $journal.runDirectory 'nic-transaction.json'
            $nicJournal=New-CombinedNicTransactionJournal -Bindings $bindings -JournalPath $nicJournalPath -SourceEvidence $nicSupport
            $journal.nicTransaction=[ordered]@{path=$nicJournalPath;helperBundle=$nicSupport;mutationIntentRecorded=$true}
            $journal.phase='NicMutationIntent'; $journal.operationInProgress=$true; $null=Save-CombinedHostJournal $journal
            $readback=$null; $failure=$null; $restoreFailure=$null
            try {
                $applyResults=Invoke-CombinedNicTransactionApply -Journal $nicJournal
                $readback=Invoke-CombinedReadback -RepositoryRoot $Repository -OutputDirectory 'spoof-handoff\reports\combined-host-20261018\a' -ExpectedMode Replacement -SmbiosReaderPath ([string]$journal.smbiosReader.path) -NicReaderPath ([string]$journal.nicReader.path) -NicNativeExePath ([string]$journal.nicNativeReader.path) -LockedMapPath ([string]$journal.lockedMap.path)
                Assert-CombinedReadbackPhase -BindingMap (New-CombinedBindingMap $mapRows) -Readback (Get-Content -LiteralPath $readback.reportPath -Raw | ConvertFrom-Json) -Phase active | Out-Null
            } catch { $failure=$_.Exception.ToString() }
            finally { try { $null=Restore-CombinedNicTransaction -Journal $nicJournal } catch { $restoreFailure=$_.Exception.ToString() } }
            if ($restoreFailure) { throw "Combined read failed=$failure; NIC restoration failed=$restoreFailure; recovery journal=$nicJournalPath" }
            if ($failure) { throw "Combined all-fields read failed after NIC restore. Exact error=$failure; NIC journal=$nicJournalPath" }
            $journal.phase='CombinedReadbackComplete'; $journal.operationInProgress=$false; $journal.bootIdentityAfterArm=$bootNow; $journal.storageReplacementVerification=$storageVerified; $journal.pathBCollectEvidence=$collect.evidence; $journal.pathBStageRestore=$stageRestore; $journal.smbiosBootProof=$smbiosProof; $journal.nicReadback=$readback; $journal.nicTransaction=[ordered]@{path=$nicJournalPath;sha256=(Get-CombinedHostSha256 $nicJournalPath);restorationComplete=[bool]$nicJournal.restorationComplete}; $result=[ordered]@{status='COMBINED_SIMULTANEOUS_READBACK_PASS_NIC_RESTORED';smbios=$smbiosProof;allReaders=$readback;nicJournal=$nicJournalPath;pathBStageRestore=$stageRestore;rebootRequiredByUser=$true;next='Remove the replacement storage package, reboot, and VerifyOriginal.'}
        }
        'RestorePolicy' {
            if ($phase -cne 'ReplacementRemoved') { throw "RestorePolicy requires storage replacement removal verified after reboot; actual=$phase" }
            $result=Invoke-CombinedStorageAction $journal 'RestorePolicy'
            $journal.phase='PolicyRestorePendingReboot'; $journal.operationInProgress=$false; $journal.lastStorageAction=$result
        }
        'FinalVerify' {
            if ($phase -cne 'PolicyRestorePendingReboot') { throw "FinalVerify requires a normal boot after test-signing restoration and Secure Boot re-enable; actual=$phase" }
            $null=Get-CombinedSecureBootVariableSnapshot -Worktree $Worktree -ExpectedEnabled $true
            $null=Assert-CombinedTestSigning $false
            $storageFinal=Invoke-CombinedStorageAction $journal 'FinalVerify'
            $original=Invoke-CombinedReadback -RepositoryRoot $Repository -OutputDirectory 'spoof-handoff\reports\combined-host-20261018\r' -ExpectedMode Original -SmbiosReaderPath ([string]$journal.smbiosReader.path) -NicReaderPath ([string]$journal.nicReader.path) -NicNativeExePath ([string]$journal.nicNativeReader.path) -LockedMapPath ([string]$journal.lockedMap.path)
            $originalReport=Get-Content -LiteralPath $original.reportPath -Raw | ConvertFrom-Json
            Assert-CombinedReadbackPhase -BindingMap (New-CombinedBindingMap (Get-CombinedMapRows ([string]$journal.lockedMap.path))) -Readback $originalReport -Phase restored | Out-Null
            $finalSnapshotDirectory=Join-Path $journal.runDirectory ('final-boot-snapshot.'+[Guid]::NewGuid().ToString('N'))
            $bcd=Get-CombinedBcdSnapshot -Repository $Repository -EvidenceDirectory $finalSnapshotDirectory
            foreach ($name in @('current','all','firmware')) {
                if ([string](Get-CombinedValue (Get-CombinedValue $bcd $name) 'stdout') -cne [string](Get-CombinedValue (Get-CombinedValue $journal.bcdSnapshot $name) 'stdout')) { throw "Supported BCD enumeration changed after rollback: $name" }
            }
            $esp=Get-CombinedEspSnapshot -EvidenceDirectory $finalSnapshotDirectory -BcdSnapshot $bcd
            $espCheck=Assert-CombinedEspMatchesBaseline -Baseline $journal.espSnapshot -Current $esp
            $firmware=Assert-CombinedFirmwareUnchanged -Snapshot $journal.firmwareSnapshot
            $journal.phase='Complete'; $journal.operationInProgress=$false; $journal.finalStorageVerification=$storageFinal; $journal.finalReadback=$original; $journal.finalBcdSnapshot=$bcd; $journal.finalEspSnapshot=$esp; $journal.finalFirmwareUnchanged=$firmware; $journal.finalEspComparison=$espCheck
            $result=[ordered]@{status='FINAL_VERIFY_PASS';simultaneousRun=$true;originalReadback=$original;secureBoot=$true;testSigning=$false;storage=$storageFinal.result;esp=$espCheck;firmwareRecordsUnchanged=$firmware;rebootedByUser=$true}
        }
    }
    if ($null -eq $result) { throw "Action '$Action' has no lifecycle implementation." }
    $journal.lastAction=$Action; $journal.lastActionUtc=[DateTime]::UtcNow.ToString('o'); $null=Save-CombinedHostJournal $journal
    return [pscustomobject][ordered]@{result=$result;runDirectory=$journal.runDirectory;journalPath=$loaded.path;journalSha256=(Get-CombinedHostSha256 $loaded.path);phase=$journal.phase}
}

if (-not $LibraryOnly) {
    $repository = [IO.Path]::GetFullPath($RepositoryRoot)
    $worktree = [IO.Path]::GetFullPath($WorktreeRoot)
    if (-not (Test-Path -LiteralPath (Join-Path $repository 'spoof-handoff') -PathType Container)) { throw "RepositoryRoot is not the main repository: $repository" }
    if (-not (Test-Path -LiteralPath (Join-Path $worktree 'spoof-handoff\tools\combined_host\readback.ps1') -PathType Leaf)) { throw "WorktreeRoot lacks the combined readback implementation: $worktree" }
    if ($Action -ceq 'Prepare') {
        Invoke-CombinedOriginalPrepare -Repository $repository -Worktree $worktree -VmGate $VmGatePath -SmbiosReader $SmbiosReaderPath -Release $ReleaseDirectory -Recovery $RecoveryDirectory -NicReader $NicReaderPath -NicNativeReader $NicNativeReaderPath | ConvertTo-Json -Depth 16
    } elseif ($Action -ceq 'Status') {
        $result = Get-CombinedHostJournal -Repository $repository -RunDirectory $RunDirectory
        [pscustomobject]@{ status='STATUS'; runDirectory=$result.journal.runDirectory; preparationStatus=$result.journal.status; phase=$result.journal.phase; hostMutationPerformed=$result.journal.hostMutationPerformed; activationAvailable=$result.journal.activationAvailable; blockedBoundary=$result.journal.blockedBoundary; journalPath=$result.path; journalSha256=$result.sha256 } | ConvertTo-Json -Depth 12
    } else {
        Invoke-CombinedLifecycleAction -Repository $repository -Worktree $worktree -RunDirectory $RunDirectory -Action $Action | ConvertTo-Json -Depth 24
    }
}
