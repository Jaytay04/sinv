[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [Parameter(Mandatory = $true)]
    [string]$AutounattendPath,

    [Parameter(Mandatory = $true)]
    [string]$ProvisionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Set-UInt16Le {
    param([byte[]]$Buffer, [int]$Offset, [uint16]$Value)
    $Buffer[$Offset] = [byte]($Value -band 0xff)
    $Buffer[$Offset + 1] = [byte](($Value -shr 8) -band 0xff)
}

function Set-UInt32Le {
    param([byte[]]$Buffer, [int]$Offset, [uint32]$Value)
    for ($Index = 0; $Index -lt 4; $Index++) {
        $Buffer[$Offset + $Index] = [byte](($Value -shr (8 * $Index)) -band 0xff)
    }
}

function Set-Ascii {
    param([byte[]]$Buffer, [int]$Offset, [string]$Text, [int]$Length)
    $Bytes = [Text.Encoding]::ASCII.GetBytes($Text.PadRight($Length).Substring(0, $Length))
    [Array]::Copy($Bytes, 0, $Buffer, $Offset, $Length)
}

function Set-Fat12Entry {
    param([byte[]]$Fat, [int]$Cluster, [int]$Value)
    $Offset = [int][Math]::Floor($Cluster * 1.5)
    if (($Cluster -band 1) -eq 0) {
        $Fat[$Offset] = [byte]($Value -band 0xff)
        $Fat[$Offset + 1] = [byte](($Fat[$Offset + 1] -band 0xf0) -bor (($Value -shr 8) -band 0x0f))
    }
    else {
        $Fat[$Offset] = [byte](($Fat[$Offset] -band 0x0f) -bor (($Value -shl 4) -band 0xf0))
        $Fat[$Offset + 1] = [byte](($Value -shr 4) -band 0xff)
    }
}

function Get-ShortNameChecksum {
    param([byte[]]$ShortName)
    [byte]$Checksum = 0
    foreach ($Value in $ShortName) {
        $Checksum = [byte](((($Checksum -band 1) -shl 7) + ($Checksum -shr 1) + $Value) -band 0xff)
    }
    return $Checksum
}

function New-DirectoryEntries {
    param(
        [string]$LongName,
        [string]$ShortBase,
        [string]$ShortExtension,
        [int]$FirstCluster,
        [int]$Length
    )
    $ShortText = $ShortBase.PadRight(8).Substring(0, 8) +
        $ShortExtension.PadRight(3).Substring(0, 3)
    $ShortName = [Text.Encoding]::ASCII.GetBytes($ShortText.ToUpperInvariant())
    $Checksum = Get-ShortNameChecksum $ShortName
    $Characters = [Collections.Generic.List[uint16]]::new()
    foreach ($Character in $LongName.ToCharArray()) {
        $Characters.Add([uint16]$Character)
    }
    $Characters.Add(0)
    while (($Characters.Count % 13) -ne 0) { $Characters.Add(0xffff) }
    $Count = [int]($Characters.Count / 13)
    $Entries = [Collections.Generic.List[byte[]]]::new()
    $NameOffsets = @(1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)
    for ($Ordinal = $Count; $Ordinal -ge 1; $Ordinal--) {
        $Entry = [byte[]]::new(32)
        $Entry[0] = [byte]($Ordinal -bor $(if ($Ordinal -eq $Count) { 0x40 } else { 0 }))
        $Entry[11] = 0x0f
        $Entry[13] = $Checksum
        for ($Index = 0; $Index -lt 13; $Index++) {
            Set-UInt16Le $Entry $NameOffsets[$Index] $Characters[(($Ordinal - 1) * 13) + $Index]
        }
        $Entries.Add($Entry)
    }
    $ShortEntry = [byte[]]::new(32)
    [Array]::Copy($ShortName, 0, $ShortEntry, 0, 11)
    $ShortEntry[11] = 0x20
    Set-UInt16Le $ShortEntry 26 ([uint16]$FirstCluster)
    Set-UInt32Le $ShortEntry 28 ([uint32]$Length)
    $Entries.Add($ShortEntry)
    return $Entries
}

foreach ($Path in @($AutounattendPath, $ProvisionPath)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required answer-disk input is missing: $Path"
    }
}

$SectorSize = 512
$SectorCount = 2880
$FatSectors = 9
$RootEntryCount = 224
$RootSectors = 14
$DataStartSector = 1 + (2 * $FatSectors) + $RootSectors
$Image = [byte[]]::new($SectorSize * $SectorCount)
$Boot = [byte[]]::new($SectorSize)
$Boot[0] = 0xeb; $Boot[1] = 0x3c; $Boot[2] = 0x90
Set-Ascii $Boot 3 'MSDOS5.0' 8
Set-UInt16Le $Boot 11 $SectorSize
$Boot[13] = 1
Set-UInt16Le $Boot 14 1
$Boot[16] = 2
Set-UInt16Le $Boot 17 $RootEntryCount
Set-UInt16Le $Boot 19 $SectorCount
$Boot[21] = 0xf0
Set-UInt16Le $Boot 22 $FatSectors
Set-UInt16Le $Boot 24 18
Set-UInt16Le $Boot 26 2
Set-UInt32Le $Boot 28 0
Set-UInt32Le $Boot 32 0
$Boot[36] = 0
$Boot[38] = 0x29
Set-UInt32Le $Boot 39 ([uint32](Get-Random -Minimum 1 -Maximum ([int64][uint32]::MaxValue)))
Set-Ascii $Boot 43 'UEFIANSWER' 11
Set-Ascii $Boot 54 'FAT12' 8
$Boot[510] = 0x55; $Boot[511] = 0xaa
[Array]::Copy($Boot, 0, $Image, 0, $SectorSize)

$Fat = [byte[]]::new($FatSectors * $SectorSize)
$Fat[0] = 0xf0; $Fat[1] = 0xff; $Fat[2] = 0xff
$Root = [byte[]]::new($RootSectors * $SectorSize)
$DirectoryOffset = 0
$NextCluster = 2
$Files = @(
    [pscustomobject]@{ Path = $AutounattendPath; LongName = 'Autounattend.xml'; ShortBase = 'AUTOUNAT'; Extension = 'XML' },
    [pscustomobject]@{ Path = $ProvisionPath; LongName = 'Provision.ps1'; ShortBase = 'PROVISIO'; Extension = 'PS1' })

foreach ($File in $Files) {
    $Content = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($File.Path))
    $ClusterCount = [Math]::Max(1, [int][Math]::Ceiling($Content.Length / $SectorSize))
    $FirstCluster = $NextCluster
    for ($Index = 0; $Index -lt $ClusterCount; $Index++) {
        $Cluster = $FirstCluster + $Index
        $Value = if ($Index -eq ($ClusterCount - 1)) { 0xfff } else { $Cluster + 1 }
        Set-Fat12Entry $Fat $Cluster $Value
    }
    $DataOffset = ($DataStartSector + ($FirstCluster - 2)) * $SectorSize
    [Array]::Copy($Content, 0, $Image, $DataOffset, $Content.Length)
    foreach ($Entry in (New-DirectoryEntries -LongName $File.LongName `
            -ShortBase $File.ShortBase -ShortExtension $File.Extension `
            -FirstCluster $FirstCluster -Length $Content.Length)) {
        if (($DirectoryOffset + 32) -gt $Root.Length) {
            throw 'The FAT12 root directory is full.'
        }
        [Array]::Copy($Entry, 0, $Root, $DirectoryOffset, 32)
        $DirectoryOffset += 32
    }
    $NextCluster += $ClusterCount
}

if (($DataStartSector + ($NextCluster - 2)) -gt $SectorCount) {
    throw 'The answer-disk files exceed the FAT12 image capacity.'
}

[Array]::Copy($Fat, 0, $Image, $SectorSize, $Fat.Length)
[Array]::Copy($Fat, 0, $Image, (1 + $FatSectors) * $SectorSize, $Fat.Length)
[Array]::Copy($Root, 0, $Image, (1 + (2 * $FatSectors)) * $SectorSize, $Root.Length)
$ResolvedOutput = [IO.Path]::GetFullPath($OutputPath)
$Parent = Split-Path -Parent $ResolvedOutput
if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
    New-Item -ItemType Directory -Path $Parent | Out-Null
}
[IO.File]::WriteAllBytes($ResolvedOutput, $Image)
[pscustomobject]@{
    Result = 'PASS'
    OutputPath = $ResolvedOutput
    Length = $Image.Length
    Files = @($Files.LongName)
}
