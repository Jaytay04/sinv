[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ImagePath,
    [Parameter(Mandatory=$true)][string]$DumpbinPath,
    [Parameter(Mandatory=$false)][uint32]$MaximumPages = 16
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Headers = (& $DumpbinPath /headers $ImagePath | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw 'dumpbin could not inspect the EFI image headers.'
}
$ImageMatch = [regex]::Match(
    $Headers, '(?im)^\s*([0-9a-f]+) image base')
$SectionMatch = [regex]::Match(
    $Headers,
    '(?ims)SECTION HEADER #\d+\s+\.hvscan name\s+([0-9a-f]+) virtual size\s+([0-9a-f]+) virtual address')
if (-not $ImageMatch.Success -or -not $SectionMatch.Success) {
    throw 'The EFI image does not expose the expected .hvscan section.'
}

$ImageBase = [Convert]::ToUInt64($ImageMatch.Groups[1].Value, 16)
$VirtualSize = [Convert]::ToUInt64($SectionMatch.Groups[1].Value, 16)
$SectionRva = [Convert]::ToUInt64($SectionMatch.Groups[2].Value, 16)
$SectionStart = $ImageBase + $SectionRva
$SectionEnd = $SectionStart + $VirtualSize
if ($VirtualSize -eq 0 -or $VirtualSize -gt ([uint64]$MaximumPages * 4096)) {
    throw ".hvscan size $VirtualSize exceeds the copied runtime allocation."
}

$Disassembly = (& $DumpbinPath /section:.hvscan /disasm $ImagePath | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw 'dumpbin could not disassemble the EFI scanner section.'
}

foreach ($Match in [regex]::Matches(
        $Disassembly, '(?im)\bcall\s+([0-9a-f]{16})\b')) {
    $Target = [Convert]::ToUInt64($Match.Groups[1].Value, 16)
    if ($Target -lt $SectionStart -or $Target -ge $SectionEnd) {
        throw ('Copied scanner has an external direct call to 0x{0:X}.' -f $Target)
    }
}
foreach ($Match in [regex]::Matches(
        $Disassembly, '(?im)\[([0-9a-f]{16})h\]')) {
    $Target = [Convert]::ToUInt64($Match.Groups[1].Value, 16)
    $Upper = $Target -shr 48
    $Canonical = ($Upper -eq 0 -and (($Target -shr 47) -band 1) -eq 0) -or
        ($Upper -eq 0xffff -and (($Target -shr 47) -band 1) -eq 1)
    # dumpbin linearly decodes inline jump-table bytes as instructions.  An
    # invented non-canonical absolute operand is data, not an addressable
    # dependency.  Real x64 image references must be canonical.
    if ($Canonical -and
        ($Target -lt $SectionStart -or $Target -ge $SectionEnd) -and
        $Target -ne $ImageBase) {
        throw ('Copied scanner references external static data at 0x{0:X}.' -f $Target)
    }
}

Write-Output ('PASS: .hvscan is self-contained ({0} bytes).' -f $VirtualSize)
