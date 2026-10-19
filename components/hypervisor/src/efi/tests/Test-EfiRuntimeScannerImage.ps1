[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ImagePath,
    [Parameter(Mandatory=$true)][string]$DumpbinPath,
    [Parameter(Mandatory=$false)][uint32]$MaximumPages = 16
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EfiResidentRvaTables.ps1')

$Headers = (& $DumpbinPath /headers $ImagePath | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw 'dumpbin could not inspect the EFI image headers.'
}
$ImageBytes = [System.IO.File]::ReadAllBytes($ImagePath)
$Section = Get-EfiResidentPeSection -ImageBytes $ImageBytes -SectionName '.hvscan'
$ImageBase = [uint64]$Section.ImageBase
$VirtualSize = [uint64]$Section.VirtualSize
$SectionRva = [uint64]$Section.VirtualAddress
$SectionRawOffset = [uint64]$Section.RawOffset
$SectionRawSize = [uint64]$Section.RawSize
$SectionStart = $ImageBase + $SectionRva
$SectionEnd = $SectionStart + $VirtualSize
if ($VirtualSize -eq 0 -or $VirtualSize -gt ([uint64]$MaximumPages * 4096)) {
    throw ".hvscan size $VirtualSize exceeds the copied runtime allocation."
}
if ($Headers -notmatch '(?im)^\s*\.hvscan name\s*$') {
    throw 'dumpbin does not report the expected .hvscan section.'
}

$Disassembly = (& $DumpbinPath /section:.hvscan /disasm:nobytes $ImagePath | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw 'dumpbin could not disassemble the EFI scanner section.'
}

$DataRanges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $Disassembly `
    -ImageBytes $ImageBytes `
    -ImageBase $ImageBase `
    -SectionRva $SectionRva `
    -SectionVirtualSize $VirtualSize `
    -SectionRawOffset $SectionRawOffset `
    -SectionRawSize $SectionRawSize)
foreach ($Reference in (Get-EfiResidentExternalReferences `
        -DisassemblyText $Disassembly `
        -DataRanges $DataRanges `
        -ImageBase $ImageBase `
        -SectionRva $SectionRva `
        -SectionVirtualSize $VirtualSize)) {
    if ($Reference.Kind -eq 'direct call') {
        throw ('Copied scanner has an external direct call to 0x{0:X}.' -f
            $Reference.TargetAddress)
    }
    throw ('Copied scanner references external static data at 0x{0:X}.' -f
        $Reference.TargetAddress)
}

Write-Output ('PASS: .hvscan is self-contained ({0} bytes).' -f $VirtualSize)
