[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EfiResidentRvaTables.ps1')

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "ASSERTION FAILED: $Message" }
}

function Assert-Throws {
    param([scriptblock]$Action, [string]$Message)
    $threw = $false
    try { & $Action | Out-Null } catch { $threw = $true }
    Assert-True $threw $Message
}

function Set-UInt16 {
    param([byte[]]$Bytes, [int]$Offset, [uint16]$Value)
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 2)
}

function Set-UInt32 {
    param([byte[]]$Bytes, [int]$Offset, [uint32]$Value)
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}

function Set-UInt64 {
    param([byte[]]$Bytes, [int]$Offset, [uint64]$Value)
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 8)
}

function New-SyntheticEfiImage {
    $image = [byte[]]::new(0x1c200)
    $image[0] = 0x4d
    $image[1] = 0x5a
    Set-UInt32 $image 0x3c 0x80
    $image[0x80] = 0x50
    $image[0x81] = 0x45
    Set-UInt16 $image 0x86 1
    Set-UInt16 $image 0x94 0xf0
    $optional = 0x98
    Set-UInt16 $image $optional 0x20b
    Set-UInt64 $image ($optional + 24) 0x140000000
    $section = $optional + 0xf0
    $sectionName = [System.Text.Encoding]::ASCII.GetBytes('.hvscan')
    [Array]::Copy($sectionName, 0, $image, $section, $sectionName.Length)
    Set-UInt32 $image ($section + 8) 0x1c000
    Set-UInt32 $image ($section + 12) 0x1000
    Set-UInt32 $image ($section + 16) 0x1c000
    Set-UInt32 $image ($section + 20) 0x200

    # A two-entry absolute-RVA switch table. Entry zero begins with bytes E8 C3
    # 01 00, and entry one's low byte is 0F; linear disassembly therefore sees
    # plausible instructions even though these bytes are data.
    $tableFileOffset = 0x200 + (0x1c494 - 0x1000)
    Set-UInt32 $image $tableFileOffset 0x1c3e8
    Set-UInt32 $image ($tableFileOffset + 4) 0x1c40f
    return $image
}

$imageBase = [uint64]0x140000000
$sectionRva = [uint64]0x1000
$sectionVirtualSize = [uint64]0x1c000
$sectionRawOffset = [uint64]0x200
$sectionRawSize = [uint64]0x1c000
$image = New-SyntheticEfiImage
$section = Get-EfiResidentPeSection -ImageBytes $image -SectionName '.hvscan'
Assert-True ($section.ImageBase -eq $imageBase) 'PE image base was parsed.'
Assert-True ($section.RawOffset -eq $sectionRawOffset) 'PE raw offset was parsed.'
Assert-True ($section.RawSize -eq $sectionRawSize) 'PE raw size was parsed.'

$boundedDisassembly = @'
0000000140001100: call 000000018F01C66C
0000000140001200: cmp eax, 1
0000000140001203: ja 0000000140001300
0000000140001209: cdqe
000000014000120B: lea rdx, [140000000h]
0000000140001212: mov ecx, dword ptr [rdx+rax*4+1C494h]
000000014000121A: add rcx, rdx
000000014000121D: jmp rcx
0000000140001300: ret
000000014001C3E8: ret
000000014001C40F: ret
000000014001C494: call 000000018F01C66C
'@
$ranges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $boundedDisassembly `
    -ImageBytes $image `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize `
    -SectionRawOffset $sectionRawOffset `
    -SectionRawSize $sectionRawSize)
Assert-True ($ranges.Count -eq 1) 'The exact bounded RVA table was recognized.'
Assert-True ($ranges[0].StartRva -eq 0x1c494 -and
    $ranges[0].ByteLength -eq 8 -and $ranges[0].EntryCount -eq 2) `
    'The exempt range is exactly the two table entries.'
$references = @(Get-EfiResidentExternalReferences `
    -DisassemblyText $boundedDisassembly `
    -DataRanges $ranges `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize)
Assert-True ($references.Count -eq 1 -and
    $references[0].SourceAddress -eq ($imageBase + 0x1100)) `
    'Only the genuine external call outside the table remains checked.'

$absentBound = $boundedDisassembly.Replace(
    '0000000140001200: cmp eax, 1',
    '0000000140001200: test eax, eax')
$absentRanges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $absentBound `
    -ImageBytes $image `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize `
    -SectionRawOffset $sectionRawOffset `
    -SectionRawSize $sectionRawSize)
Assert-True ($absentRanges.Count -eq 0) 'A table without a bound is not exempted.'
$absentReferences = @(Get-EfiResidentExternalReferences `
    -DisassemblyText $absentBound `
    -DataRanges $absentRanges `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize)
Assert-True (@($absentReferences | Where-Object {
    $_.SourceAddress -eq ($imageBase + 0x1c494)
}).Count -eq 1) 'The misleading call stays audited without a proven bound.'

$unreferencedTable = @'
000000014001C494: call 000000018F01C66C
'@
$unreferencedRanges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $unreferencedTable `
    -ImageBytes $image `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize `
    -SectionRawOffset $sectionRawOffset `
    -SectionRawSize $sectionRawSize)
Assert-True ($unreferencedRanges.Count -eq 0) 'Unreferenced data bytes do not create an exemption.'
$unreferencedReferences = @(Get-EfiResidentExternalReferences `
    -DisassemblyText $unreferencedTable `
    -DataRanges $unreferencedRanges `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize)
Assert-True ($unreferencedReferences.Count -eq 1) 'An unreferenced fake call remains audited.'

$chainedImage = [byte[]]$image.Clone()
$firstTableOffset = 0x200 + (0x1c494 - 0x1000)
Set-UInt32 $chainedImage $firstTableOffset 0x1300
Set-UInt32 $chainedImage ($firstTableOffset + 4) 0x1380
$secondTableOffset = 0x200 + (0x1c49c - 0x1000)
Set-UInt32 $chainedImage $secondTableOffset 0x1300
Set-UInt32 $chainedImage ($secondTableOffset + 4) 0x1380
Set-UInt32 $chainedImage ($secondTableOffset + 8) 0x1318
$chainedDisassembly = @'
0000000140001100: call 000000018F01C66C
0000000140001104: mov rax,qword ptr [000000018F01C66Ch]
0000000140001200: cmp eax, 1
0000000140001203: ja 0000000140001280
0000000140001209: cdqe
000000014000120B: lea rdx, [140000000h]
0000000140001212: mov ecx, dword ptr [rdx+rax*4+1C494h]
000000014000121A: add rcx, rdx
000000014000121D: jmp rcx
0000000140001280: ret
0000000140001300: mov dword ptr [rbx+48h],0
0000000140001307: lea rcx,[rbx+10h]
000000014000130E: mov eax,2
0000000140001313: sub rax,rdi
0000000140001316: jne 0000000140001313
0000000140001318: mov eax,1
000000014000131D: dec eax
0000000140001320: cmp eax, 2
0000000140001323: ja 0000000140001380
0000000140001329: mov ecx, dword ptr [rdx+rax*4+1C49Ch]
0000000140001330: add rcx, rdx
0000000140001333: jmp rcx
0000000140001380: ret
000000014001C494: call 000000018F01C66C
000000014001C49C: mov rax,qword ptr [000000018F01C66Ch]
'@
$chainedRanges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $chainedDisassembly `
    -ImageBytes $chainedImage `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize `
    -SectionRawOffset $sectionRawOffset `
    -SectionRawSize $sectionRawSize)
Assert-True ($chainedRanges.Count -eq 2) `
    'A reused image-base dispatch is recognized only after the validated first table.'
Assert-True ($chainedRanges[0].StartRva -eq 0x1c494 -and
    $chainedRanges[0].ByteLength -eq 8 -and
    $chainedRanges[1].StartRva -eq 0x1c49c -and
    $chainedRanges[1].ByteLength -eq 12 -and
    $chainedRanges[1].EntryCount -eq 3) `
    'Fresh and reused tables exempt exactly their bounded entry bytes.'
$chainedReferences = @(Get-EfiResidentExternalReferences `
    -DisassemblyText $chainedDisassembly `
    -DataRanges $chainedRanges `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize)
Assert-True ($chainedReferences.Count -eq 2 -and
    @($chainedReferences | Where-Object { $_.Kind -eq 'direct call' }).Count -eq 1 -and
    @($chainedReferences | Where-Object { $_.Kind -eq 'absolute static reference' }).Count -eq 1) `
    'Genuine external calls and static references outside both tables remain flagged.'

$baseChangedDispatch = $chainedDisassembly.Replace(
    '000000014000120B: lea rdx, [140000000h]',
    '000000014000120B: lea r8, [140000000h]')
$baseChangedDispatch = $baseChangedDispatch.Replace(
    '0000000140001212: mov ecx, dword ptr [rdx+rax*4+1C494h]',
    '0000000140001212: mov ecx, dword ptr [r8+rax*4+1C494h]')
$baseChangedDispatch = $baseChangedDispatch.Replace(
    '000000014000121A: add rcx, rdx',
    '000000014000121A: add rcx, r8')
$changedBaseRanges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $baseChangedDispatch `
    -ImageBytes $chainedImage `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize `
    -SectionRawOffset $sectionRawOffset `
    -SectionRawSize $sectionRawSize)
Assert-True ($changedBaseRanges.Count -eq 1) `
    'A reused base without a matching trusted dispatch fact is not exempted.'

$conflictingImage = [byte[]]$chainedImage.Clone()
$conflictingTableOffset = 0x200 + (0x1c4a4 - 0x1000)
Set-UInt32 $conflictingImage $conflictingTableOffset 0x1300
Set-UInt32 $conflictingImage ($conflictingTableOffset + 4) 0x1380
Set-UInt32 $conflictingImage ($conflictingTableOffset + 8) 0x1318
$conflictingDisassembly = $chainedDisassembly.Replace(
    '0000000140001280: ret', @'
0000000140001280: ret
0000000140001290: cmp eax, 1
0000000140001293: ja 00000001400012F0
0000000140001299: lea r8, [140000000h]
00000001400012A0: mov ecx, dword ptr [r8+rax*4+1C49Ch]
00000001400012A8: add rcx, r8
00000001400012AB: jmp rcx
00000001400012F0: ret
'@)
$conflictingDisassembly = $conflictingDisassembly.Replace(
    '0000000140001329: mov ecx, dword ptr [rdx+rax*4+1C49Ch]',
    '0000000140001329: mov ecx, dword ptr [rdx+rax*4+1C4A4h]')
$conflictingRanges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $conflictingDisassembly `
    -ImageBytes $conflictingImage `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize `
    -SectionRawOffset $sectionRawOffset `
    -SectionRawSize $sectionRawSize)
Assert-True ($conflictingRanges.Count -eq 2) `
    'Conflicting image-base facts for one entry prevent a reused-table exemption.'

foreach ($clobber in @('mov edx,0', 'xor dl,dl')) {
    $clobberedDisassembly = $chainedDisassembly.Replace(
        '0000000140001300: mov dword ptr [rbx+48h],0',
        "0000000140001300: $clobber")
    $clobberedRanges = @(Get-EfiResidentRvaTableRanges `
        -DisassemblyText $clobberedDisassembly `
        -ImageBytes $chainedImage `
        -ImageBase $imageBase `
        -SectionRva $sectionRva `
        -SectionVirtualSize $sectionVirtualSize `
        -SectionRawOffset $sectionRawOffset `
        -SectionRawSize $sectionRawSize)
    Assert-True ($clobberedRanges.Count -eq 1) `
        "A base clobber through $clobber prevents reused-table exemption."
}

$unsafeBlock = $chainedDisassembly.Replace(
    '0000000140001307: lea rcx,[rbx+10h]',
    '0000000140001307: call 000000018F01C66C')
$unsafeRanges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $unsafeBlock `
    -ImageBytes $chainedImage `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize `
    -SectionRawOffset $sectionRawOffset `
    -SectionRawSize $sectionRawSize)
Assert-True ($unsafeRanges.Count -eq 1) 'A call in the proof block prevents reused-table exemption.'

$escapingBlock = $chainedDisassembly.Replace(
    '0000000140001316: jne 0000000140001313',
    '0000000140001316: jne 0000000140001400')
$escapingRanges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $escapingBlock `
    -ImageBytes $chainedImage `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize `
    -SectionRawOffset $sectionRawOffset `
    -SectionRawSize $sectionRawSize)
Assert-True ($escapingRanges.Count -eq 1) `
    'An escaping or non-instruction branch target prevents reused-table exemption.'

$incomingEdgeDisassembly = $chainedDisassembly.Replace(
    '0000000140001280: ret',
    "0000000140001270: jne 0000000140001300`r`n0000000140001280: ret")
$incomingEdgeRanges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $incomingEdgeDisassembly `
    -ImageBytes $chainedImage `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize `
    -SectionRawOffset $sectionRawOffset `
    -SectionRawSize $sectionRawSize)
Assert-True ($incomingEdgeRanges.Count -eq 1) `
    'An external direct branch into the proof block prevents reused-table exemption.'

$dispatchEdgeDisassembly = $chainedDisassembly.Replace(
    '0000000140001280: ret',
    "0000000140001270: jne 0000000140001320`r`n0000000140001280: ret")
$dispatchEdgeRanges = @(Get-EfiResidentRvaTableRanges `
    -DisassemblyText $dispatchEdgeDisassembly `
    -ImageBytes $chainedImage `
    -ImageBase $imageBase `
    -SectionRva $sectionRva `
    -SectionVirtualSize $sectionVirtualSize `
    -SectionRawOffset $sectionRawOffset `
    -SectionRawSize $sectionRawSize)
Assert-True ($dispatchEdgeRanges.Count -eq 1) `
    'An external direct branch to the candidate compare prevents reused-table exemption.'

$tamperedSecondImage = [byte[]]$chainedImage.Clone()
Set-UInt32 $tamperedSecondImage $secondTableOffset 0x900000
Assert-Throws {
    Get-EfiResidentRvaTableRanges `
        -DisassemblyText $chainedDisassembly `
        -ImageBytes $tamperedSecondImage `
        -ImageBase $imageBase `
        -SectionRva $sectionRva `
        -SectionVirtualSize $sectionVirtualSize `
        -SectionRawOffset $sectionRawOffset `
        -SectionRawSize $sectionRawSize
} 'A proven reused-base table with an invalid RVA target fails closed.'

$tableDataTargetImage = [byte[]]$chainedImage.Clone()
Set-UInt32 $tableDataTargetImage $secondTableOffset 0x1c494
Assert-Throws {
    Get-EfiResidentRvaTableRanges `
        -DisassemblyText $chainedDisassembly `
        -ImageBytes $tableDataTargetImage `
        -ImageBase $imageBase `
        -SectionRva $sectionRva `
        -SectionVirtualSize $sectionVirtualSize `
        -SectionRawOffset $sectionRawOffset `
        -SectionRawSize $sectionRawSize
} 'A table entry targeting previously validated table data fails closed.'

$tamperedImage = [byte[]]$image.Clone()
$tableFileOffset = 0x200 + (0x1c494 - 0x1000)
Set-UInt32 $tamperedImage $tableFileOffset 0x900000
Assert-Throws {
    Get-EfiResidentRvaTableRanges `
        -DisassemblyText $boundedDisassembly `
        -ImageBytes $tamperedImage `
        -ImageBase $imageBase `
        -SectionRva $sectionRva `
        -SectionVirtualSize $sectionVirtualSize `
        -SectionRawOffset $sectionRawOffset `
        -SectionRawSize $sectionRawSize
} 'A recognized table with an external/tampered target fails closed.'

Write-Output 'PASS: resident RVA table parsing, exact exemptions, and external-reference checks.'
