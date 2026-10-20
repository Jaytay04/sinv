[CmdletBinding(SupportsShouldProcess=$true)]
param(
    [string]$WorkspaceRoot,
    [string]$RunDirectory,
    [string]$CredentialStateRoot,
    [string]$VmrunPath = 'C:\Program Files\VMware\VMware Workstation\vmrun.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($WhatIfPreference) {
    [pscustomobject]@{ Action='NewPayloadVm'; Preview=$true; HostBootChanges=$false; VmChanges=$false } | ConvertTo-Json
    return
}
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sourceCommit = (@(& git -C $sourceRoot rev-parse HEAD) -join '').Trim()
if ($LASTEXITCODE -ne 0 -or $sourceCommit -cnotmatch '^[0-9a-f]{40}$') { throw 'Cannot pin the clone preparation source revision.' }
$sourceBranch = (@(& git -C $sourceRoot branch --show-current) -join '').Trim()
if ($LASTEXITCODE -ne 0 -or $sourceBranch -cne 'feat/smbios-boot-serial-probe') { throw 'Clone preparation requires the reviewed feature branch.' }
$sourceChanges = @(& git -C $sourceRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or $sourceChanges.Count -ne 0) { throw 'Clone preparation requires the clean reviewed feature worktree.' }
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot '..\..\..'))
}
Import-Module (Join-Path $PSScriptRoot 'GpuHostState.psm1') -Force -DisableNameChecking
$WorkspaceRoot = Assert-GpuPathWithin -Path $WorkspaceRoot -Root $WorkspaceRoot -AllowRoot -MustExist
$null = Assert-GpuPathWithin -Path $sourceRoot -Root $WorkspaceRoot -MustExist
$buildRoot = Join-Path $WorkspaceRoot '.codex-build'
$vmReceiptPath = Join-Path $WorkspaceRoot '.analysis\gpu-payload-diagnostic\vm-prepared.json'
if (Test-Path -LiteralPath $vmReceiptPath) { throw 'A prepared diagnostic VM receipt already exists; preserve it and review that exact VM before creating another.' }
$metadataPath = Join-Path $WorkspaceRoot '.analysis\host-runner\default-windows-vm.json'
$metadata = Read-GpuJson -Path $metadataPath
$sourceVmx = Assert-GpuPathWithin -Path $metadata.VmxPath -Root $buildRoot -MustExist
$reviewedVmx = Join-Path $WorkspaceRoot '.codex-build\gpu_windows_vm\run-01\standalone\hsinv-Windows-25H2.vmx'
$reviewedVmxHash = '242C46795FEDA50B24D681AD502DAE926D756E0375809CDA8456096B554EAC16'
if (-not [string]::Equals($sourceVmx,$reviewedVmx,[StringComparison]::OrdinalIgnoreCase) -or
    [string]$metadata.VmxSha256 -cne $reviewedVmxHash) { throw 'Inventory does not select the exact reviewed retained VM.' }
if ((Get-GpuFilePin -Path $sourceVmx).Sha256 -cne [string]$metadata.VmxSha256) {
    throw 'Retained VMX differs from its reviewed metadata; preserve it and stop.'
}
if ([string]$metadata.DefaultSnapshot -cne 'hsinv-default-25h2-26200.9457' -or
    [int]$metadata.WindowsBuild -ne 26200 -or [int]$metadata.Ubr -ne 9457 -or
    [int]$metadata.LogicalCpus -ne 4 -or [int]$metadata.MemoryMiB -ne 8192 -or
    $metadata.NestedVirtualization -ne $true -or $metadata.Network -cne 'disabled' -or
    $metadata.PhysicalPassthrough -cne 'none') { throw 'Retained VM metadata is not the reviewed offline nested VM.' }
$certificate = Join-Path $sourceRoot 'components\hypervisor\hypersinv.cer'
if ((Get-GpuFilePin -Path $certificate).Sha256 -cne 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE') {
    throw 'Public signing certificate differs from its reviewed pin.'
}
if ([string]::IsNullOrWhiteSpace($CredentialStateRoot)) { $CredentialStateRoot = Join-Path $WorkspaceRoot '.analysis\host-runner' }
$CredentialStateRoot = Assert-GpuPathWithin -Path $CredentialStateRoot -Root (Join-Path $WorkspaceRoot '.analysis') -MustExist
if (-not [string]::Equals($CredentialStateRoot,(Join-Path $WorkspaceRoot '.analysis\host-runner'),[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Clone preparation uses only the existing protected Host Runner credential directory.'
}
if ([string]::IsNullOrWhiteSpace($RunDirectory)) {
    $RunDirectory = Join-Path $buildRoot ('gpu_payload_vm\vm-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
}
$RunDirectory = Assert-GpuPathWithin -Path $RunDirectory -Root $buildRoot
if (Test-Path -LiteralPath $RunDirectory) { throw 'Clone RunDirectory must be fresh; existing VM state is never overwritten.' }
if (-not $PSCmdlet.ShouldProcess($RunDirectory, 'Create independent disposable VM clone with test-only firmware db defaults')) { return }

# Reuse the protected credential and redacted VMware process helpers. No
# credential values are emitted or copied into this run's files.
Import-Module (Join-Path $sourceRoot 'tools\host-runner\HostRunner.Core.psm1') -Force
$Config = [pscustomobject]@{
    ProjectRoot=$WorkspaceRoot; StateRoot=$CredentialStateRoot
    Tools=[pscustomobject]@{VmrunPath=$VmrunPath}
    Policy=[pscustomobject]@{OperationTimeoutSeconds=60}
}
$Selected = [pscustomobject]@{VmxPath=$sourceVmx;VmCredentialFile='windows-vm.xml'}
$tokens=$null; $parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $sourceRoot 'tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'),[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw 'Existing Host Runner did not parse.' }
foreach ($name in @('Get-PlainCredential','Invoke-ConfiguredVmrun')) {
    $functions=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$true) | Where-Object {$_.Name -ceq $name})
    if ($functions.Count -ne 1) { throw "Required Host Runner function is missing: $name" }
    . ([scriptblock]::Create($functions[0].Extent.Text))
}

function Get-GpuVmTreeLayout {
    param([Parameter(Mandatory=$true)][string]$Root)
    $fullRoot=[IO.Path]::GetFullPath($Root)
    $rootItem=Get-Item -LiteralPath $fullRoot -Force -ErrorAction Stop
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'VM source or destination root may not be a reparse point.'
    }
    $prefix=$fullRoot.TrimEnd('\') + '\'
    $pending=[Collections.Generic.Stack[string]]::new()
    $pending.Push($fullRoot)
    $directories=[Collections.Generic.List[object]]::new()
    $files=[Collections.Generic.List[object]]::new()
    while ($pending.Count -gt 0) {
        $current=$pending.Pop()
        foreach ($item in Get-ChildItem -LiteralPath $current -Force -ErrorAction Stop) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'VM trees containing reparse points are refused.'
            }
            if ([string]$item.Name -match '(?i)\.lck$') {
                throw 'VM trees containing VMware lock entries are refused.'
            }
            $relative=$item.FullName.Substring($prefix.Length)
            if ($item.PSIsContainer) {
                $directories.Add([pscustomobject]@{ RelativePath=$relative })
                $pending.Push($item.FullName)
            } else {
                $files.Add([pscustomobject]@{ RelativePath=$relative; Length=[long]$item.Length })
            }
        }
    }
    $orderedFiles=@($files | Sort-Object -Property RelativePath)
    $orderedDirectories=@($directories | Sort-Object -Property RelativePath)
    $total=[long]0
    foreach ($file in $orderedFiles) { $total += [long]$file.Length }
    return [pscustomobject]@{
        Root=$fullRoot; Files=$orderedFiles; Directories=$orderedDirectories
        FileCount=$orderedFiles.Count; DirectoryCount=$orderedDirectories.Count; TotalBytes=$total
    }
}

function Get-GpuVmSnapshotSelection {
    param(
        [Parameter(Mandatory=$true)][string]$VmsdPath,
        [Parameter(Mandatory=$true)][string]$ExpectedName
    )
    if (-not (Test-Path -LiteralPath $VmsdPath -PathType Leaf)) { throw 'Required snapshot metadata file is missing.' }
    $values=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in [IO.File]::ReadAllLines($VmsdPath)) {
        if ($line -match '^\s*([^=\s]+)\s*=\s*"?([^"\r\n]*)"?\s*$') {
            $values[$Matches[1]]=[string]$Matches[2]
        }
    }
    if (-not $values.ContainsKey('snapshot.current')) { throw 'Snapshot metadata has no current snapshot identifier.' }
    $current=[string]$values['snapshot.current']
    $selected=[Collections.Generic.List[object]]::new()
    $named=[Collections.Generic.List[object]]::new()
    foreach ($key in $values.Keys) {
        if ($key -match '^snapshot(\d+)\.uid$') {
            $index=[string]$Matches[1]
            $uid=[string]$values[$key]
            $displayKey='snapshot' + $index + '.displayName'
            $display=if ($values.ContainsKey($displayKey)) { [string]$values[$displayKey] } else { '' }
            $entry=[pscustomobject]@{ Index=$index; Uid=$uid; DisplayName=$display }
            if ($uid -ceq $current) { $selected.Add($entry) }
            if ($display -ceq $ExpectedName) { $named.Add($entry) }
        }
    }
    if ($selected.Count -ne 1 -or $named.Count -ne 1 -or
        [string]$selected[0].Uid -cne [string]$named[0].Uid -or
        [string]$selected[0].DisplayName -cne $ExpectedName) {
        throw 'The retained VM is not already at the uniquely identified reviewed default snapshot.'
    }
    return [pscustomobject]@{
        CurrentUid=$current; SnapshotIndex=[string]$selected[0].Index
        DisplayName=[string]$selected[0].DisplayName; UniqueCurrentMatch=$true
    }
}

function Assert-GpuVmStopped {
    param([Parameter(Mandatory=$true)][string]$VmxPath)
    $listing=Invoke-ConfiguredVmrun -Selected ([pscustomobject]@{VmxPath=$VmxPath;VmCredentialFile=''}) `
        -Operation @('list') -TimeoutSeconds 30
    foreach ($line in ($listing.Stdout -split '\r?\n')) {
        $candidate=$line.Trim()
        if ([IO.Path]::GetExtension($candidate) -ieq '.vmx' -and
            [string]::Equals([IO.Path]::GetFullPath($candidate),[IO.Path]::GetFullPath($VmxPath),[StringComparison]::OrdinalIgnoreCase)) {
            throw 'A VM expected to remain stopped is running.'
        }
    }
}

function Assert-GpuVmVmdkClosure {
    param([Parameter(Mandatory=$true)][string]$VmxPath,[Parameter(Mandatory=$true)][string]$CloneRoot)
    $root=[IO.Path]::GetFullPath($CloneRoot)
    $lines=[IO.File]::ReadAllLines($VmxPath)
    $disks=[Collections.Generic.Queue[string]]::new()
    foreach ($line in $lines) {
        if ($line -match '^\s*(?:nvme|scsi|sata|ide)\d+:\d+\.fileName\s*=\s*"([^"]+)"\s*$') {
            $value=[string]$Matches[1]
            if ($value -ceq 'none') { continue }
            $disk=if ([IO.Path]::IsPathRooted($value)) { [IO.Path]::GetFullPath($value) } else { [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $VmxPath) $value)) }
            $null=Assert-GpuPathWithin -Path $disk -Root $root -MustExist
            if ([IO.Path]::GetExtension($disk) -ine '.vmdk') { throw 'A configured virtual disk has an unexpected file type.' }
            $disks.Enqueue($disk)
        }
    }
    if ($disks.Count -eq 0) { throw 'The VMX contains no independently copied virtual disk.' }
    $visited=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($disks.Count -gt 0) {
        $disk=$disks.Dequeue()
        if (-not $visited.Add($disk)) { continue }
        $stream=[IO.File]::Open($disk,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        try {
            $count=[int][Math]::Min([long]1048576,$stream.Length)
            $buffer=New-Object byte[] $count
            $read=0
            while ($read -lt $count) {
                $chunk=$stream.Read($buffer,$read,$count-$read)
                if ($chunk -le 0) { break }
                $read += $chunk
            }
        } finally { $stream.Dispose() }
        $header=[Text.Encoding]::ASCII.GetString($buffer,0,$read)
        $matches=[regex]::Matches($header,'(?im)^\s*parentFileNameHint\s*=\s*"([^"]*)"\s*$')
        if ($matches.Count -gt 1) { throw 'A virtual disk contains ambiguous parent-chain metadata.' }
        if ($matches.Count -eq 1 -and -not [string]::IsNullOrWhiteSpace([string]$matches[0].Groups[1].Value)) {
            $parentName=[string]$matches[0].Groups[1].Value
            $parent=if ([IO.Path]::IsPathRooted($parentName)) { [IO.Path]::GetFullPath($parentName) } else { [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $disk) $parentName)) }
            $null=Assert-GpuPathWithin -Path $parent -Root $root -MustExist
            if ([IO.Path]::GetExtension($parent) -ine '.vmdk') { throw 'A virtual disk parent has an unexpected file type.' }
            $disks.Enqueue($parent)
        }
    }
    return [pscustomobject]@{ DiskCount=$visited.Count; AllParentsWithinClone=$true }
}

$sourceVmxDirectory=Split-Path -Parent $sourceVmx
$sourceVmxLeaf=Split-Path -Leaf $sourceVmx
$sourceVmsd=[IO.Path]::ChangeExtension($sourceVmx,'.vmsd')
$sourceVmxInitialPin=Get-GpuFilePin -Path $sourceVmx
if (-not [string]::Equals([string]$sourceVmxInitialPin.Sha256,[string]$metadata.VmxSha256,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Retained VMX differs from its reviewed metadata; preserve it and stop.'
}
$sourceVmsdInitialPin=Get-GpuFilePin -Path $sourceVmsd
$snapshotProof=Get-GpuVmSnapshotSelection -VmsdPath $sourceVmsd -ExpectedName ([string]$metadata.DefaultSnapshot)
$sourceLayout=Get-GpuVmTreeLayout -Root $sourceVmxDirectory
$running=Invoke-ConfiguredVmrun -Selected $Selected -Operation @('list') -TimeoutSeconds 30
foreach ($line in ($running.Stdout -split '\r?\n')) {
    if ([string]::Equals($line.Trim(),$sourceVmx,[StringComparison]::OrdinalIgnoreCase)) { throw 'Retained VM is running; it will not be copied in use.' }
}
$snapshots=Invoke-ConfiguredVmrun -Selected $Selected -Operation @('listSnapshots',$sourceVmx) -TimeoutSeconds 30
if (@($snapshots.Stdout -split '\r?\n' | Where-Object {$_.Trim() -ceq [string]$metadata.DefaultSnapshot}).Count -ne 1) { throw 'Reviewed default snapshot was not found exactly once.' }

if ([string]::IsNullOrWhiteSpace($RunDirectory)) {
    $RunDirectory=Join-Path $buildRoot ('gpu_payload_vm\vm-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
}
$RunDirectory=[IO.Path]::GetFullPath((Assert-GpuPathWithin -Path $RunDirectory -Root $buildRoot))
$expectedVmRunRoot=[IO.Path]::GetFullPath((Join-Path $buildRoot 'gpu_payload_vm'))
if (-not [string]::Equals((Split-Path -Parent $RunDirectory),$expectedVmRunRoot,[StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $RunDirectory) -cnotmatch '^vm-\d{8}T\d{6}Z-[0-9a-fA-F]{8}$') {
    throw 'RunDirectory must use the pinned .codex-build\gpu_payload_vm\vm-<UTC>-<id> layout.'
}
if (Test-Path -LiteralPath $RunDirectory) { throw 'Clone RunDirectory must be fresh; existing VM state is never overwritten.' }
$cloneRoot=Join-Path $RunDirectory 'clone'
$cloneVmx=Join-Path $cloneRoot $sourceVmxLeaf
$volumeRoot=[IO.Path]::GetPathRoot($RunDirectory)
$drive=[IO.DriveInfo]::new($volumeRoot)
$reserve=[long][Math]::Max([long]1073741824,[long][Math]::Ceiling($sourceLayout.TotalBytes * 0.05))
$required=[long]($sourceLayout.TotalBytes + $reserve)
if ($drive.AvailableFreeSpace -lt $required) { throw 'Insufficient free space for an independent full VM copy plus safety reserve.' }
if (-not $PSCmdlet.ShouldProcess($RunDirectory, 'Create independent offline full-copy VM and prepare diagnostic Secure Boot settings')) { return }

$intent=[ordered]@{
    SchemaVersion=1;Status='CLONE_IN_PROGRESS';SourceCommit=$sourceCommit
    SourceVmx=$sourceVmx;SourceVmxSha256=[string]$sourceVmxInitialPin.Sha256
    SourceVmsdSha256=[string]$sourceVmsdInitialPin.Sha256
    Snapshot=[string]$metadata.DefaultSnapshot;SnapshotSelectionMethod='COPIED_REVIEWED_DEFAULT_CURRENT_SNAPSHOT'
    VmxPath=$cloneVmx;RetainedVmMoved=$false;HostBootChanges=$false
    CopyMethod='OFFLINE_FULL_DIRECTORY_COPY';TrustMechanism='VM firmware db append, not MOK enrollment';TrustValidated=$false
}
[void](New-Item -ItemType Directory -Path $RunDirectory -ErrorAction Stop)
Write-GpuJson -Path (Join-Path $RunDirectory 'clone-intent.json') -Value $intent -NoOverwrite
try {
    [void](New-Item -ItemType Directory -Path $cloneRoot -ErrorAction Stop)
    foreach ($directory in $sourceLayout.Directories) {
        $destinationDirectory=Join-Path $cloneRoot ([string]$directory.RelativePath)
        [void](New-Item -ItemType Directory -Path $destinationDirectory -Force -ErrorAction Stop)
    }
    $copyRecords=[Collections.Generic.List[object]]::new()
    $completedBytes=[long]0
    $copyIndex=0
    foreach ($file in $sourceLayout.Files) {
        $sourceFile=Join-Path $sourceLayout.Root ([string]$file.RelativePath)
        $destinationFile=Join-Path $cloneRoot ([string]$file.RelativePath)
        $sourceBefore=Get-GpuFilePin -Path $sourceFile
        if ([long]$sourceBefore.Bytes -ne [long]$file.Length) { throw 'Source VM files changed during preflight.' }
        [IO.File]::Copy($sourceFile,$destinationFile,$false)
        $destinationPin=Get-GpuFilePin -Path $destinationFile
        $sourceAfter=Get-GpuFilePin -Path $sourceFile
        if ([string]$sourceBefore.Sha256 -cne [string]$sourceAfter.Sha256 -or
            [long]$sourceBefore.Bytes -ne [long]$sourceAfter.Bytes -or
            [string]$destinationPin.Sha256 -cne [string]$sourceBefore.Sha256 -or
            [long]$destinationPin.Bytes -ne [long]$sourceBefore.Bytes) {
            throw 'Copied VM file failed source-stability or SHA-256 readback verification.'
        }
        $copyRecords.Add([pscustomobject]@{
            RelativePath=[string]$file.RelativePath;Bytes=[long]$destinationPin.Bytes;Sha256=[string]$destinationPin.Sha256
        })
        $completedBytes += [long]$destinationPin.Bytes
        $copyIndex++
        if (($copyIndex % 8) -eq 0 -or $copyIndex -eq $sourceLayout.FileCount) {
            Write-GpuJson -Path (Join-Path $RunDirectory 'copy-progress.json') -Value @{
                Status='COPYING';CompletedFiles=$copyIndex;TotalFiles=$sourceLayout.FileCount
                CompletedBytes=$completedBytes;TotalBytes=$sourceLayout.TotalBytes;HostBootChanges=$false
            }
        }
    }
    $manifestPath=Join-Path $RunDirectory 'copy-manifest.json'
    Write-GpuJson -Path $manifestPath -Value ([ordered]@{
        SchemaVersion=1;CopyMethod='OFFLINE_FULL_DIRECTORY_COPY'
        FileCount=$copyRecords.Count;DirectoryCount=$sourceLayout.DirectoryCount;TotalBytes=$completedBytes
        Files=@($copyRecords)
    }) -NoOverwrite
    $manifestPin=Get-GpuFilePin -Path $manifestPath
    $copyProgressPath=Join-Path $RunDirectory 'copy-progress.json'
    Write-GpuJson -Path $copyProgressPath -Value @{
        Status='COPY_VERIFIED';CompletedFiles=$copyIndex;TotalFiles=$sourceLayout.FileCount
        CompletedBytes=$completedBytes;TotalBytes=$sourceLayout.TotalBytes;ManifestSha256=$manifestPin.Sha256
        HostBootChanges=$false
    }

    $cloneLayout=Get-GpuVmTreeLayout -Root $cloneRoot
    if ($cloneLayout.FileCount -ne $sourceLayout.FileCount -or $cloneLayout.DirectoryCount -ne $sourceLayout.DirectoryCount -or
        $cloneLayout.TotalBytes -ne $sourceLayout.TotalBytes) { throw 'Independent VM directory layout does not match the source.' }
    for ($i=0; $i -lt $sourceLayout.Files.Count; $i++) {
        if ([string]$cloneLayout.Files[$i].RelativePath -cne [string]$sourceLayout.Files[$i].RelativePath -or
            [long]$cloneLayout.Files[$i].Length -ne [long]$sourceLayout.Files[$i].Length) {
            throw 'Independent VM file paths or lengths do not match the source tree.'
        }
    }
    for ($i=0; $i -lt $sourceLayout.Directories.Count; $i++) {
        if ([string]$cloneLayout.Directories[$i].RelativePath -cne [string]$sourceLayout.Directories[$i].RelativePath) {
            throw 'Independent VM directory paths do not match the source tree.'
        }
    }
    $null=Assert-GpuPathWithin -Path $cloneVmx -Root $cloneRoot -MustExist
    $cloneVmsd=[IO.Path]::ChangeExtension($cloneVmx,'.vmsd')
    $cloneVmsdPin=Get-GpuFilePin -Path $cloneVmsd
    if ([string]$cloneVmsdPin.Sha256 -cne [string]$sourceVmsdInitialPin.Sha256 -or
        [long]$cloneVmsdPin.Bytes -ne [long]$sourceVmsdInitialPin.Bytes) {
        throw 'Copied snapshot metadata is not byte-identical to the reviewed source.'
    }
    $cloneSnapshotProof=Get-GpuVmSnapshotSelection -VmsdPath $cloneVmsd -ExpectedName ([string]$metadata.DefaultSnapshot)
    if ([string]$cloneSnapshotProof.CurrentUid -cne [string]$snapshotProof.CurrentUid) {
        throw 'Copied VM does not select the reviewed default snapshot.'
    }
    $sourceVmxAfterCopy=Get-GpuFilePin -Path $sourceVmx
    $sourceVmsdAfterCopy=Get-GpuFilePin -Path $sourceVmsd
    if ([string]$sourceVmxAfterCopy.Sha256 -cne [string]$sourceVmxInitialPin.Sha256 -or
        [string]$sourceVmsdAfterCopy.Sha256 -cne [string]$sourceVmsdInitialPin.Sha256) {
        throw 'Retained VMX or snapshot metadata changed during the copy.'
    }
    Assert-GpuVmStopped -VmxPath $sourceVmx
    Assert-GpuVmStopped -VmxPath $cloneVmx

    $lines=[Collections.Generic.List[string]]::new([IO.File]::ReadAllLines($cloneVmx))
    if (@($lines | Where-Object {$_ -match '^\s*uefi\.secureBoot\.(?:dbDefault|allowAuthBypass)'}).Count -ne 0) {
        throw 'Copied source already has custom Secure Boot policy; refusing to replace it.'
    }
    $publicCopy=Join-Path $cloneRoot 'hypersinv-public.cer'
    [IO.File]::Copy($certificate,$publicCopy,$false)
    if ((Get-GpuFilePin -Path $publicCopy).Sha256 -cne (Get-GpuFilePin -Path $certificate).Sha256) {
        throw 'Public certificate copy verification failed.'
    }
    $newNvram=Join-Path $cloneRoot 'gpu-payload-test.nvram'
    if (Test-Path -LiteralPath $newNvram) { throw 'New test NVRAM path already exists.' }
    $settings=[ordered]@{
        'nvram'='gpu-payload-test.nvram'
        'uefi.secureBoot.enabled'='TRUE'
        'uefi.secureBoot.dbDefault.file0'='hypersinv-public.cer'
        'uefi.secureBoot.dbDefault.append'='TRUE'
    }
    foreach ($key in $settings.Keys) {
        $pattern='^\s*' + [regex]::Escape($key) + '\s*='
        $matches=@(for($i=0;$i -lt $lines.Count;$i++){if($lines[$i] -match $pattern){$i}})
        if($matches.Count -gt 1){throw "Repeated VM setting: $key"}
        $replacement=$key + ' = "' + $settings[$key] + '"'
        if($matches.Count -eq 1){$lines[$matches[0]]=$replacement}else{$lines.Add($replacement)}
    }
    $ethernetKeys=@(for($i=0;$i -lt $lines.Count;$i++){if($lines[$i] -match '^\s*ethernet\d+\.present\s*='){ $i }})
    if ($ethernetKeys.Count -eq 0) { $lines.Add('ethernet0.present = "FALSE"') }
    else { foreach ($index in $ethernetKeys) { $lines[$index]=([regex]::Match($lines[$index],'^\s*ethernet\d+\.present')).Value + ' = "FALSE"' } }
    $cloneVmxItem=Get-Item -LiteralPath $cloneVmx -Force
    if (($cloneVmxItem.Attributes -band [IO.FileAttributes]::ReadOnly) -ne 0) {
        $cloneVmxItem.Attributes=$cloneVmxItem.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly)
    }
    [IO.File]::WriteAllLines($cloneVmx,$lines,[Text.UTF8Encoding]::new($false))
    $vmdkProof=Assert-GpuVmVmdkClosure -VmxPath $cloneVmx -CloneRoot $cloneRoot
    Assert-GpuVmStopped -VmxPath $sourceVmx
    Assert-GpuVmStopped -VmxPath $cloneVmx
    $sourceVmxFinalPin=Get-GpuFilePin -Path $sourceVmx
    $sourceVmsdFinalPin=Get-GpuFilePin -Path $sourceVmsd
    if ([string]$sourceVmxFinalPin.Sha256 -cne [string]$sourceVmxInitialPin.Sha256 -or
        [string]$sourceVmsdFinalPin.Sha256 -cne [string]$sourceVmsdInitialPin.Sha256) {
        throw 'Retained source VMX or snapshot metadata changed during preparation.'
    }
    $intent.Status='CLONE_PREPARED_TRUST_UNVERIFIED'
    $intent.VmxSha256=(Get-GpuFilePin -Path $cloneVmx).Sha256
    $intent.PublicCertificateSha256=(Get-GpuFilePin -Path $publicCopy).Sha256
    $intent.CopyFileCount=$copyRecords.Count
    $intent.CopyDirectoryCount=$sourceLayout.DirectoryCount
    $intent.CopyBytes=$completedBytes
    $intent.CopyManifestPath=$manifestPath
    $intent.CopyManifestSha256=$manifestPin.Sha256
    $intent.SnapshotProof=[pscustomobject]@{
        CurrentUid=$cloneSnapshotProof.CurrentUid;SnapshotIndex=$cloneSnapshotProof.SnapshotIndex
        DisplayName=$cloneSnapshotProof.DisplayName;VmsdSha256=$cloneVmsdPin.Sha256
        MatchesRetainedSource=$true;RevertRequired=$false
    }
    $intent.VmdkClosure=$vmdkProof
    $intent.WorkstationSupport='Unverified until VMware log and guest Secure Boot/db readback confirm this firmware db append option.'
    Write-GpuJson -Path (Join-Path $RunDirectory 'result.json') -Value $intent -NoOverwrite
    $receiptParent=Split-Path -Parent $vmReceiptPath
    if (-not (Test-Path -LiteralPath $receiptParent -PathType Container)) { [void](New-Item -ItemType Directory -Path $receiptParent) }
    Write-GpuJson -Path $vmReceiptPath -Value $intent -NoOverwrite
    [pscustomobject]$intent | ConvertTo-Json -Depth 8
} catch {
    $failureMessage=$_.Exception.Message
    $sourceUnchanged=$false
    try {
        $sourceUnchanged=([string](Get-GpuFilePin -Path $sourceVmx).Sha256 -ceq [string]$sourceVmxInitialPin.Sha256) -and
            ([string](Get-GpuFilePin -Path $sourceVmsd).Sha256 -ceq [string]$sourceVmsdInitialPin.Sha256)
    } catch { }
    Write-GpuJson -Path (Join-Path $RunDirectory 'failure.json') -Value @{
        Failure=$failureMessage;SourceVmxAndVmsdUnchanged=$sourceUnchanged
        CopyMethod='OFFLINE_FULL_DIRECTORY_COPY';PartialVmPreserved=$true
        HostBootChanges=$false;RetainedVmMoved=$false
    } -NoOverwrite
    if (-not $sourceUnchanged) { throw ($failureMessage + ' Retained VMX or snapshot metadata could not be verified unchanged; preserve all state.') }
    throw $failureMessage
}
