Set-StrictMode -Version Latest

function ConvertTo-EfiResidentNumber {
    param(
        [Parameter(Mandatory=$true)][string]$Text,
        [Parameter(Mandatory=$true)][bool]$HexDefault
    )

    $value = $Text.Trim()
    if ($value.StartsWith('0x', [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToUInt64($value.Substring(2), 16)
    }
    if ($value.EndsWith('h', [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToUInt64($value.Substring(0, $value.Length - 1), 16)
    }
    if ($HexDefault) { return [Convert]::ToUInt64($value, 16) }
    return [Convert]::ToUInt64($value, 10)
}

function Get-EfiResidentPeSection {
    param(
        [Parameter(Mandatory=$true)][byte[]]$ImageBytes,
        [Parameter(Mandatory=$true)][string]$SectionName
    )

    if ($ImageBytes.Length -lt 0x40) { throw 'EFI image is shorter than a DOS header.' }
    if ($ImageBytes[0] -ne 0x4d -or $ImageBytes[1] -ne 0x5a) {
        throw 'EFI image does not have an MZ signature.'
    }
    $peOffset = [uint64][BitConverter]::ToUInt32($ImageBytes, 0x3c)
    if ($peOffset + 24 -gt [uint64]$ImageBytes.LongLength) {
        throw 'EFI image PE header is outside the file.'
    }
    $pe = [int]$peOffset
    if ($ImageBytes[$pe] -ne 0x50 -or $ImageBytes[$pe + 1] -ne 0x45 -or
        $ImageBytes[$pe + 2] -ne 0 -or $ImageBytes[$pe + 3] -ne 0) {
        throw 'EFI image does not have a PE signature.'
    }

    $sectionCount = [BitConverter]::ToUInt16($ImageBytes, $pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($ImageBytes, $pe + 20)
    $optional = [uint64]$peOffset + 24
    if ($optional + $optionalSize -gt [uint64]$ImageBytes.LongLength -or
        $optionalSize -lt 32) {
        throw 'EFI image optional header is truncated.'
    }
    $optionalOffset = [int]$optional
    if ([BitConverter]::ToUInt16($ImageBytes, $optionalOffset) -ne 0x20b) {
        throw 'EFI image is not PE32+.'
    }
    $imageBase = [BitConverter]::ToUInt64($ImageBytes, $optionalOffset + 24)
    if ($imageBase -eq 0) { throw 'EFI image has a zero image base.' }

    $sectionTable = $optional + $optionalSize
    if ($sectionTable + ([uint64]$sectionCount * 40) -gt
        [uint64]$ImageBytes.LongLength) {
        throw 'EFI image section table is outside the file.'
    }
    for ($index = 0; $index -lt $sectionCount; ++$index) {
        $header = [int]($sectionTable + ([uint64]$index * 40))
        $nameLength = 0
        while ($nameLength -lt 8 -and $ImageBytes[$header + $nameLength] -ne 0) {
            ++$nameLength
        }
        $name = [System.Text.Encoding]::ASCII.GetString(
            $ImageBytes, $header, $nameLength)
        if ($name -ne $SectionName) { continue }

        $virtualSize = [uint64][BitConverter]::ToUInt32($ImageBytes, $header + 8)
        $sectionRva = [uint64][BitConverter]::ToUInt32($ImageBytes, $header + 12)
        $rawSize = [uint64][BitConverter]::ToUInt32($ImageBytes, $header + 16)
        $rawOffset = [uint64][BitConverter]::ToUInt32($ImageBytes, $header + 20)
        if ($virtualSize -eq 0 -or $rawSize -eq 0 -or
            $rawOffset + $rawSize -gt [uint64]$ImageBytes.LongLength -or
            $imageBase + $sectionRva -lt $imageBase -or
            $sectionRva + $virtualSize -lt $sectionRva) {
            throw "PE section $SectionName has invalid bounds."
        }
        return [pscustomobject]@{
            Name = $name
            ImageBase = [uint64]$imageBase
            VirtualAddress = [uint64]$sectionRva
            VirtualSize = [uint64]$virtualSize
            RawOffset = [uint64]$rawOffset
            RawSize = [uint64]$rawSize
        }
    }
    throw "EFI image does not contain section $SectionName."
}

function Get-EfiResidentInstructions {
    param([Parameter(Mandatory=$true)][string]$DisassemblyText)

    foreach ($line in ($DisassemblyText -split "`r?`n")) {
        $match = [regex]::Match(
            $line, '^\s*(?<address>[0-9a-f]{16}):\s*(?<text>\S.*)\s*$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $match.Success) { continue }
        $address = [Convert]::ToUInt64($match.Groups['address'].Value, 16)
        $text = $match.Groups['text'].Value.Trim()
        if ([string]::IsNullOrWhiteSpace($text)) { continue }
        [pscustomobject]@{ Address = [uint64]$address; Text = $text }
    }
}

function Get-EfiResidentRegister64 {
    param([Parameter(Mandatory=$true)][string]$Register32)
    $register = $Register32.ToLowerInvariant()
    $aliases = @{
        eax = 'rax'; ebx = 'rbx'; ecx = 'rcx'; edx = 'rdx'
        esi = 'rsi'; edi = 'rdi'; esp = 'rsp'; ebp = 'rbp'
    }
    if ($aliases.ContainsKey($register)) { return $aliases[$register] }
    if ($register -match '^r(8|9|1[0-5])d$') { return 'r' + $Matches[1] }
    return $null
}

function Get-EfiResidentRvaTableRanges {
    param(
        [Parameter(Mandatory=$true)][string]$DisassemblyText,
        [Parameter(Mandatory=$true)][byte[]]$ImageBytes,
        [Parameter(Mandatory=$true)][uint64]$ImageBase,
        [Parameter(Mandatory=$true)][uint64]$SectionRva,
        [Parameter(Mandatory=$true)][uint64]$SectionVirtualSize,
        [Parameter(Mandatory=$true)][uint64]$SectionRawOffset,
        [Parameter(Mandatory=$true)][uint64]$SectionRawSize
    )

    if ($SectionRawOffset + $SectionRawSize -gt [uint64]$ImageBytes.LongLength -or
        $SectionVirtualSize -eq 0 -or $SectionRawSize -eq 0) {
        throw 'Resident section raw or virtual bounds are invalid.'
    }
    $sectionEnd = $SectionRva + $SectionVirtualSize
    if ($sectionEnd -lt $SectionRva) { throw 'Resident section RVA overflows.' }
    $instructions = @(Get-EfiResidentInstructions $DisassemblyText)
    $starts = [System.Collections.Generic.HashSet[UInt64]]::new()
    foreach ($instruction in $instructions) {
        if ($instruction.Address -ge $ImageBase + $SectionRva -and
            $instruction.Address -lt $ImageBase + $sectionEnd) {
            [void]$starts.Add([uint64]$instruction.Address)
        }
    }

    $ranges = [System.Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $instructions.Count; ++$index) {
        $compare = [regex]::Match($instructions[$index].Text,
            '^cmp\s+(?<index>e(?:ax|bx|cx|dx|si|di|sp|bp)|r(?:8|9|1[0-5])d)\s*,\s*(?<bound>(?:0x)?[0-9a-f]+h?)$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $compare.Success -or $index + 5 -ge $instructions.Count) { continue }
        $branch = [regex]::Match($instructions[$index + 1].Text,
            '^ja\s+(?<target>(?:0x)?[0-9a-f]{8,16}h?)$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $branch.Success) { continue }

        $sequence = $index + 2
        $hasCdqe = $instructions[$sequence].Text -match '^cdqe$'
        if ($hasCdqe) { ++$sequence }
        if ($sequence + 3 -ge $instructions.Count) { continue }

        $lea = [regex]::Match($instructions[$sequence].Text,
            '^lea\s+(?<base>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))\s*,\s*\[(?<image>(?:0x)?[0-9a-f]+h?)\]$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        $load = [regex]::Match($instructions[$sequence + 1].Text,
            '^mov\s+(?<dst>e(?:ax|bx|cx|dx|si|di|sp|bp)|r(?:8|9|1[0-5])d)\s*,\s*dword ptr\s*\[\s*(?<base>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))\s*\+\s*(?<index>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))\s*\*\s*4\s*\+\s*(?<table>(?:0x)?[0-9a-f]+h?)\s*\]$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $lea.Success -or -not $load.Success) { continue }
        $add = [regex]::Match($instructions[$sequence + 2].Text,
            '^add\s+(?<dst>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))\s*,\s*(?<base>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        $jump = [regex]::Match($instructions[$sequence + 3].Text,
            '^jmp\s+(?<dst>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $add.Success -or -not $jump.Success) { continue }

        $index64 = Get-EfiResidentRegister64 $compare.Groups['index'].Value
        $dst64 = Get-EfiResidentRegister64 $load.Groups['dst'].Value
        $baseRegister = $lea.Groups['base'].Value.ToLowerInvariant()
        if ($hasCdqe -and $compare.Groups['index'].Value -ine 'eax') { continue }
        if ($null -eq $index64 -or $null -eq $dst64 -or
            $load.Groups['base'].Value -ine $baseRegister -or
            $load.Groups['index'].Value -ine $index64 -or
            $add.Groups['dst'].Value -ine $dst64 -or
            $add.Groups['base'].Value -ine $baseRegister -or
            $jump.Groups['dst'].Value -ine $dst64 -or
            $baseRegister -eq $index64 -or $baseRegister -eq $dst64) {
            continue
        }

        # The full bounded indirect-jump shape is now recognized. From this
        # point, any bad section/raw bounds or target is a hard audit failure;
        # it must never silently lose the range exemption and hide a jump.
        $bound = ConvertTo-EfiResidentNumber $compare.Groups['bound'].Value $false
        if ($bound -gt 255) { throw 'Resident RVA jump table bound exceeds 255.' }
        $entryCount = [uint64]$bound + 1
        $branchTarget = ConvertTo-EfiResidentNumber $branch.Groups['target'].Value $true
        if ($branchTarget -lt $ImageBase + $SectionRva -or
            $branchTarget -ge $ImageBase + $sectionEnd -or
            -not $starts.Contains([uint64]$branchTarget)) {
            throw 'Resident RVA jump table default branch is not resident code.'
        }
        $baseValue = ConvertTo-EfiResidentNumber $lea.Groups['image'].Value $true
        $tableRva = ConvertTo-EfiResidentNumber $load.Groups['table'].Value $true
        if ($baseValue -ne $ImageBase -or ($tableRva -band 3) -ne 0 -or
            $tableRva -lt $SectionRva -or $tableRva -ge $sectionEnd) {
            throw 'Resident RVA jump table base or alignment is invalid.'
        }
        $tableBytes = $entryCount * 4
        $tableOffsetInSection = $tableRva - $SectionRva
        if ($tableOffsetInSection + $tableBytes -gt $SectionRawSize -or
            $tableOffsetInSection + $tableBytes -gt $SectionVirtualSize) {
            throw 'Resident RVA jump table lies outside .hvscan raw or virtual bounds.'
        }
        $fileOffset = $SectionRawOffset + $tableOffsetInSection
        if ($fileOffset + $tableBytes -gt [uint64]$ImageBytes.LongLength) {
            throw 'Resident RVA jump table lies outside the image file.'
        }
        $tableAddress = $ImageBase + $tableRva
        if ($tableAddress -lt $ImageBase -or
            $tableAddress + $tableBytes -lt $tableAddress -or
            $tableAddress -le $instructions[$sequence + 3].Address) {
            throw 'Resident RVA jump table must follow its dispatch code without overflow.'
        }
        for ($entry = [uint64]0; $entry -lt $entryCount; ++$entry) {
            $entryOffset = [int]($fileOffset + ($entry * 4))
            $targetRva = [uint64][BitConverter]::ToUInt32($ImageBytes, $entryOffset)
            $targetAddress = $ImageBase + $targetRva
            if ($targetAddress -lt $ImageBase -or
                $targetRva -lt $SectionRva -or $targetRva -ge $tableRva -or
                $targetAddress -ge $tableAddress -or
                -not $starts.Contains([uint64]$targetAddress)) {
                throw 'Resident RVA jump table entry does not target a prior resident instruction.'
            }
        }

        $range = [pscustomobject]@{
            StartAddress = [uint64]$tableAddress
            EndAddress = [uint64]($tableAddress + $tableBytes)
            StartRva = [uint64]$tableRva
            ByteLength = [uint64]$tableBytes
            EntryCount = [uint32]$entryCount
        }
        foreach ($existing in $ranges) {
            if ($range.StartAddress -lt $existing.EndAddress -and
                $existing.StartAddress -lt $range.EndAddress) {
                throw 'Resident RVA jump tables overlap.'
            }
        }
        $ranges.Add($range)
        $index = $sequence + 3
    }
    return $ranges.ToArray()
}

function Get-EfiResidentExternalReferences {
    param(
        [Parameter(Mandatory=$true)][string]$DisassemblyText,
        [Parameter(Mandatory=$true)][AllowEmptyCollection()][object[]]$DataRanges,
        [Parameter(Mandatory=$true)][uint64]$ImageBase,
        [Parameter(Mandatory=$true)][uint64]$SectionRva,
        [Parameter(Mandatory=$true)][uint64]$SectionVirtualSize
    )

    $sectionStart = $ImageBase + $SectionRva
    $sectionEnd = $sectionStart + $SectionVirtualSize
    foreach ($instruction in (Get-EfiResidentInstructions $DisassemblyText)) {
        $isData = $false
        foreach ($range in $DataRanges) {
            if ($instruction.Address -ge $range.StartAddress -and
                $instruction.Address -lt $range.EndAddress) {
                $isData = $true
                break
            }
        }
        if ($isData) { continue }

        $call = [regex]::Match($instruction.Text,
            '^call\s+(?<target>[0-9a-f]{16})\b',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($call.Success) {
            $target = [Convert]::ToUInt64($call.Groups['target'].Value, 16)
            if ($target -lt $sectionStart -or $target -ge $sectionEnd) {
                [pscustomobject]@{
                    Kind = 'direct call'; SourceAddress = $instruction.Address
                    TargetAddress = $target; Text = $instruction.Text
                }
            }
        }
        foreach ($reference in [regex]::Matches($instruction.Text,
                '\[(?<target>[0-9a-f]{16})h?\]',
                [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
            $target = [Convert]::ToUInt64($reference.Groups['target'].Value, 16)
            $upper = $target -shr 48
            $sign = ($target -shr 47) -band 1
            $canonical = ($upper -eq 0 -and $sign -eq 0) -or
                ($upper -eq 0xffff -and $sign -eq 1)
            if ($canonical -and $target -ne $ImageBase -and
                ($target -lt $sectionStart -or $target -ge $sectionEnd)) {
                [pscustomobject]@{
                    Kind = 'absolute static reference';
                    SourceAddress = $instruction.Address
                    TargetAddress = $target; Text = $instruction.Text
                }
            }
        }
    }
}
