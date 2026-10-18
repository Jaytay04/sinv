[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$MetadataPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
Add-Type -AssemblyName System.IO.Compression
$root='C:\Users\jayli\OneDrive\Documents\Projects\hsinv\spoof-handoff\reports\'
$metadataFull=[IO.Path]::GetFullPath($MetadataPath)
$outputFull=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
foreach($path in @($metadataFull,$outputFull)) { if (-not $path.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)) { throw 'VM evidence paths must remain under the repository reports directory.' } }
$metadata=Get-Content -LiteralPath $metadataFull -Raw | ConvertFrom-Json
if ($metadata.schema -cne 'combined-vm-reader-phase.v1' -or $metadata.status -cne 'PASS') { throw 'Expected a passing combined VM reader phase.' }
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
$current=$outputFull
while($current.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)) {
    if ((Test-Path -LiteralPath $current) -and ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Refusing a reparse point in evidence output.' }
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
