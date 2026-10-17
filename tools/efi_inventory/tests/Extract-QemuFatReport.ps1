param(
    [Parameter(Mandatory = $true)][string]$ImagePath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'

function Read-At([IO.FileStream]$Stream, [long]$Offset, [int]$Length) {
    if ($Offset -lt 0 -or $Length -lt 0 -or $Offset + $Length -gt $Stream.Length) {
        throw 'FAT image read is outside the disk image.'
    }
    $buffer = [byte[]]::new($Length)
    $Stream.Position = $Offset
    $read = 0
    while ($read -lt $Length) {
        $n = $Stream.Read($buffer, $read, $Length - $read)
        if ($n -eq 0) { throw 'Unexpected end of FAT image.' }
        $read += $n
    }
    return ,$buffer
}

$image = [IO.File]::OpenRead((Resolve-Path -LiteralPath $ImagePath).Path)
try {
    $mbr = Read-At $image 0 512
    if ($mbr[510] -ne 0x55 -or $mbr[511] -ne 0xAA) {
        throw 'Missing MBR signature.'
    }
    $partition = $null
    for ($i = 0; $i -lt 4; $i++) {
        $offset = 446 + $i * 16
        if ($mbr[$offset + 4] -eq 0x04 -or $mbr[$offset + 4] -eq 0x06 -or
            $mbr[$offset + 4] -eq 0x0E) {
            $partition = [BitConverter]::ToUInt32($mbr, $offset + 8)
            break
        }
    }
    if ($null -eq $partition) { throw 'No MBR FAT16 partition found.' }
    $boot = Read-At $image ([long]$partition * 512) 512
    $bps = [BitConverter]::ToUInt16($boot, 11)
    $spc = [int]$boot[13]
    $reserved = [BitConverter]::ToUInt16($boot, 14)
    $fatCount = [int]$boot[16]
    $rootEntries = [BitConverter]::ToUInt16($boot, 17)
    $sectorsPerFat = [BitConverter]::ToUInt16($boot, 22)
    if ($bps -ne 512 -or $spc -lt 1 -or $fatCount -lt 1 -or
        $rootEntries -lt 1 -or $sectorsPerFat -lt 1) {
        throw 'Unsupported FAT16 geometry.'
    }
    $base = [long]$partition * 512
    $fatOffset = $base + [long]$reserved * $bps
    $rootOffset = $fatOffset + [long]$fatCount * $sectorsPerFat * $bps
    $rootSectors = [math]::Ceiling($rootEntries * 32 / $bps)
    $dataOffset = $rootOffset + [long]$rootSectors * $bps
    $root = Read-At $image $rootOffset ($rootEntries * 32)
    $found = $false
    for ($i = 0; $i -lt $rootEntries; $i++) {
        $entry = $i * 32
        if ($root[$entry] -eq 0) { break }
        $attr = $root[$entry + 11]
        if ($root[$entry] -eq 0xE5 -or $attr -eq 0x0F -or
            ($attr -band 0x18) -ne 0) { continue }
        $size = [BitConverter]::ToUInt32($root, $entry + 28)
        $cluster = [int][BitConverter]::ToUInt16($root, $entry + 26)
        if ($size -lt 9 -or $size -gt 16MB -or $cluster -lt 2) { continue }
        $bytes = [byte[]]::new($size)
        $copied = 0
        $seen = [Collections.Generic.HashSet[int]]::new()
        while ($copied -lt $size) {
            if ($cluster -lt 2 -or $cluster -ge 0xFFF8 -or -not $seen.Add($cluster)) {
                throw 'Invalid or cyclic FAT16 cluster chain.'
            }
            $chunk = [math]::Min($spc * $bps, $size - $copied)
            $part = Read-At $image ($dataOffset + [long]($cluster - 2) * $spc * $bps) $chunk
            [Array]::Copy($part, 0, $bytes, $copied, $chunk)
            $copied += $chunk
            if ($copied -lt $size) {
                $fatEntry = Read-At $image ($fatOffset + [long]$cluster * 2) 2
                $cluster = [int][BitConverter]::ToUInt16($fatEntry, 0)
            }
        }
        if ([Text.Encoding]::ASCII.GetString($bytes, 0, 8) -ne 'HVEFI001') {
            continue
        }
        $output = [IO.File]::Open($OutputPath, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $output.Write($bytes, 0, $bytes.Length) }
        finally { $output.Dispose() }
        Write-Output "Extracted EFI report ($size bytes) to $OutputPath"
        $found = $true
        break
    }
    if (-not $found) { throw 'No HVEFI001 report found in the FAT16 root directory.' }
}
finally { $image.Dispose() }
