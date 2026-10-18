[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$MetadataPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
Add-Type -AssemblyName System.IO.Compression
$mainRoot='C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$handoff=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$worktreeRoot=Split-Path -Parent $handoff
$mainReportRoot=Join-Path $mainRoot 'spoof-handoff\reports'
$worktreeReportRoot=Join-Path $handoff 'reports'
$runtimeRoot=Join-Path $worktreeReportRoot 'f6\runtime'
function Assert-PhaseInputPath([string]$Path,[string[]]$AllowedRoots) {
    $full=[IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw ('Missing VM evidence input: '+$full) }
    $matchedRoot=$null
    foreach($boundary in $AllowedRoots) {
        $rootFull=[IO.Path]::GetFullPath($boundary).TrimEnd('\')
        if ($full.StartsWith(($rootFull+'\'),[StringComparison]::OrdinalIgnoreCase)) { $matchedRoot=$rootFull; break }
    }
    if (-not $matchedRoot) { throw ('VM evidence input escapes its allowed roots: '+$full) }
    $current=$full
    while ($current.StartsWith(($matchedRoot+'\'),[StringComparison]::OrdinalIgnoreCase) -or
        $current.Equals($matchedRoot,[StringComparison]::OrdinalIgnoreCase)) {
        $item=Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('VM evidence input contains a reparse point: '+$current) }
        if ($current.Equals($matchedRoot,[StringComparison]::OrdinalIgnoreCase)) { break }
        $current=Split-Path -Parent $current
    }
    return $full
}
function Assert-PhaseOutputPath([string]$Path,[string]$RuntimeRoot,[string]$WorktreeRoot) {
    $full=[IO.Path]::GetFullPath($Path).TrimEnd('\')
    $runtimeFull=[IO.Path]::GetFullPath($RuntimeRoot).TrimEnd('\')
    if (-not $full.Equals($runtimeFull,[StringComparison]::OrdinalIgnoreCase) -and
        -not $full.StartsWith(($runtimeFull+'\'),[StringComparison]::OrdinalIgnoreCase)) {
        throw 'VM evidence output must remain under worktree spoof-handoff\reports\f6\runtime.'
    }
    $worktreeFull=[IO.Path]::GetFullPath($WorktreeRoot).TrimEnd('\')
    $current=$full
    while ($current.StartsWith(($worktreeFull+'\'),[StringComparison]::OrdinalIgnoreCase) -or
        $current.Equals($worktreeFull,[StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $current) {
            if (((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('VM evidence output contains a reparse point: '+$current) }
        }
        if ($current.Equals($worktreeFull,[StringComparison]::OrdinalIgnoreCase)) { break }
        $current=Split-Path -Parent $current
    }
    return $full
}
$metadataFull=Assert-PhaseInputPath $MetadataPath @($mainReportRoot,$worktreeReportRoot)
$outputFull=Assert-PhaseOutputPath $OutputDirectory $runtimeRoot $worktreeRoot
$metadata=Get-Content -LiteralPath $metadataFull -Raw | ConvertFrom-Json
$acceptedSchemas=@('combined-vm-reader-phase.v1','combined-vm-fixture-correction-action.v1')
$metadataSchema=''
$metadataStatus=''
if ($metadata -and $metadata.PSObject.Properties['schema']) { $metadataSchema=[string]$metadata.schema }
if ($metadata -and $metadata.PSObject.Properties['status']) { $metadataStatus=[string]$metadata.status }
if ($acceptedSchemas -cnotcontains $metadataSchema -or $metadataStatus -cne 'PASS') { throw 'Expected passing combined VM reader-phase or fixture-correction action metadata.' }
$bundle=$metadata.rawEvidenceBundle
if ($bundle.format -cne 'zip' -or [long]$bundle.bytes -le 0 -or [long]$bundle.bytes -gt 8MB -or [string]$bundle.sha256 -notmatch '\A[A-F0-9]{64}\z') { throw 'Raw evidence bundle metadata is invalid.' }
$bytes=[Convert]::FromBase64String([string]$bundle.base64)
$sha=[Security.Cryptography.SHA256]::Create()
try { $hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','') } finally { $sha.Dispose() }
if ($bytes.Length -ne [long]$bundle.bytes -or $hash -cne [string]$bundle.sha256) { throw 'Nested raw evidence archive size or SHA-256 differs.' }
$files=@($metadata.capturedFiles)
if ($files.Count -ne [int]$bundle.fileCount -or $files.Count -eq 0 -or $files.Count -gt 500) { throw 'Nested raw evidence file count is invalid.' }
$expected=@{}
foreach($file in $files) {
    $name=[string]$file.archiveLeaf
    if ($name -notmatch '\Af\.[0-9]{4}\.[A-Za-z0-9]{1,10}\z' -or $expected.ContainsKey($name) -or [string]$file.sha256 -notmatch '\A[A-F0-9]{64}\z' -or [long]$file.bytes -gt 8MB -or [long]$file.bytes -lt 0) { throw 'Raw evidence manifest name, hash or size is invalid.' }
    $expected[$name]=$file
}
if (Test-Path -LiteralPath $outputFull) { if (@(Get-ChildItem -LiteralPath $outputFull -Force).Count -gt 0) { throw 'Refusing to overwrite a populated evidence directory.' } }
$worktreeBoundary=[IO.Path]::GetFullPath($worktreeRoot).TrimEnd('\')
$current=$outputFull
while($current.StartsWith(($worktreeBoundary+'\'),[StringComparison]::OrdinalIgnoreCase) -or
    $current.Equals($worktreeBoundary,[StringComparison]::OrdinalIgnoreCase)) {
    if ((Test-Path -LiteralPath $current) -and ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Refusing a reparse point in evidence output.' }
    if ($current.Equals($worktreeBoundary,[StringComparison]::OrdinalIgnoreCase)) { break }
    $current=Split-Path -Parent $current
}
$stream=[IO.MemoryStream]::new($bytes,$false)
$extracted=[Collections.Generic.List[object]]::new()
try {
    $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Read,$false)
    try {
        if ($zip.Entries.Count -ne $expected.Count) { throw 'ZIP entry count differs from the pinned manifest.' }
        $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        [long]$total=0
        foreach($entry in $zip.Entries) {
            $name=[string]$entry.FullName
            if (-not $expected.ContainsKey($name) -or -not $seen.Add($name)) { throw 'ZIP contains an unexpected or duplicate entry.' }
            $record=$expected[$name]
            if ($entry.Length -ne [long]$record.bytes -or $entry.Length -gt 8MB) { throw 'ZIP entry size differs from the pinned evidence.' }
            $total+=$entry.Length
            if ($total -gt 32MB) { throw 'Expanded raw evidence exceeds the size limit.' }
        }
        [void][IO.Directory]::CreateDirectory($outputFull)
        foreach($entry in $zip.Entries) {
            $record=$expected[[string]$entry.FullName]
            $target=Join-Path $outputFull ([string]$entry.FullName)
            $input=$entry.Open();$output=[IO.File]::Open($target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            try {$input.CopyTo($output);$output.Flush($true)}finally{$input.Dispose();$output.Dispose()}
            $actual=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
            if ($actual -cne [string]$record.sha256) { throw ('Expanded raw evidence SHA-256 differs: '+$entry.FullName) }
            $extracted.Add([ordered]@{guestPath=[string]$record.guestPath;archiveLeaf=[string]$entry.FullName;path=$target;sha256=$actual;bytes=[long]$record.bytes})
        }
    }finally{$zip.Dispose()}
}finally{$stream.Dispose()}
$manifest=[ordered]@{schema='combined-vm-expanded-evidence.v1';status='PASS';metadata=[ordered]@{path=$metadataFull;sha256=(Get-FileHash -LiteralPath $metadataFull -Algorithm SHA256).Hash};nestedArchiveSha256=$hash;authenticationMustBeVerifiedByCaller=$true;files=@($extracted.ToArray())}
$manifestPath=Join-Path $outputFull 'extraction-manifest.json'
[IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
[pscustomobject]@{status='PASS';files=$extracted.Count;manifestPath=$manifestPath;sha256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash} | ConvertTo-Json
