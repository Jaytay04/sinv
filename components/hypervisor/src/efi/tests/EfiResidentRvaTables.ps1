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

function Get-EfiResidentRegisterAlias64 {
    param([Parameter(Mandatory=$true)][string]$Register)
    $register = $Register.ToLowerInvariant()
    if ($register -match '^r(8|9|1[0-5])(?:d|w|b)?$') {
        return 'r' + $Matches[1]
    }
    $aliases = @{
        rax='rax'; eax='rax'; ax='rax'; al='rax'; ah='rax'
        rbx='rbx'; ebx='rbx'; bx='rbx'; bl='rbx'; bh='rbx'
        rcx='rcx'; ecx='rcx'; cx='rcx'; cl='rcx'; ch='rcx'
        rdx='rdx'; edx='rdx'; dx='rdx'; dl='rdx'; dh='rdx'
        rsi='rsi'; esi='rsi'; si='rsi'; sil='rsi'
        rdi='rdi'; edi='rdi'; di='rdi'; dil='rdi'
        rsp='rsp'; esp='rsp'; sp='rsp'; spl='rsp'
        rbp='rbp'; ebp='rbp'; bp='rbp'; bpl='rbp'
    }
    if ($aliases.ContainsKey($register)) { return $aliases[$register] }
    return $null
}

function Test-EfiResidentScalarOperand {
    param([Parameter(Mandatory=$true)][string]$Operand)
    $value = $Operand.Trim()
    if ($value -match '^(?:(?:byte|word|dword|qword) ptr\s*)?\[[^\]]+\]$') {
        return $true
    }
    if ($null -ne (Get-EfiResidentRegisterAlias64 $value)) { return $true }
    return $value -match '^(?:[-+]?(?:0x)?[0-9a-f]+h?|[0-9]+)$'
}

function Test-EfiResidentBasePreservingInstruction {
    param(
        [Parameter(Mandatory=$true)][string]$Text,
        [Parameter(Mandatory=$true)][string]$BaseRegister
    )

    $instruction = $Text.Trim().ToLowerInvariant()
    $branch = [regex]::Match($instruction,
        '^j(?:a|ae|b|be|c|e|g|ge|l|le|na|nae|nb|nbe|nc|ne|ng|nge|nl|nle|no|np|ns|nz|o|p|pe|po|s|z)\s+(?<target>(?:0x)?[0-9a-f]{8,16}h?)$')
    if ($branch.Success) {
        return [pscustomobject]@{
            IsConditionalBranch = $true
            BranchTarget = [uint64](ConvertTo-EfiResidentNumber $branch.Groups['target'].Value $true)
            PreservesBase = $true
        }
    }
    if ($instruction -match '^(?:nop|endbr64)$') {
        return [pscustomobject]@{
            IsConditionalBranch = $false; BranchTarget = [uint64]0
            PreservesBase = $true
        }
    }

    $mnemonicMatch = [regex]::Match($instruction, '^(?<mnemonic>[a-z][a-z0-9]*)\s+(?<operands>.+)$')
    if (-not $mnemonicMatch.Success) { return $null }
    $mnemonic = $mnemonicMatch.Groups['mnemonic'].Value
    $operands = $mnemonicMatch.Groups['operands'].Value.Trim()
    $unary = $mnemonic -match '^(?:inc|dec|neg|not)$'
    $twoOperand = $mnemonic -match '^(?:mov|movzx|movsx|movsxd|lea|test|cmp|cmov[a-z]+|add|sub|and|or|xor|shl|shr|sar|sal)$'
    if ($unary) {
        if ($operands.Contains(',' ) -or -not (Test-EfiResidentScalarOperand $operands)) {
            return $null
        }
        $destination = $operands
    } elseif ($twoOperand) {
        $separator = $operands.IndexOf(',')
        if ($separator -lt 1 -or $operands.IndexOf(',', $separator + 1) -ge 0) {
            return $null
        }
        $destination = $operands.Substring(0, $separator).Trim()
        $source = $operands.Substring($separator + 1).Trim()
        if (-not (Test-EfiResidentScalarOperand $destination) -or
            -not (Test-EfiResidentScalarOperand $source)) {
            return $null
        }
        if ($mnemonic -match '^(?:lea|movzx|movsx|movsxd|cmov[a-z]+)$' -and
            $null -eq (Get-EfiResidentRegisterAlias64 $destination)) {
            return $null
        }
    } else {
        return $null
    }

    $destinationRegister = Get-EfiResidentRegisterAlias64 $destination
    $preservesBase = $null -eq $destinationRegister -or
        $destinationRegister -ine $BaseRegister.ToLowerInvariant()
    return [pscustomobject]@{
        IsConditionalBranch = $false; BranchTarget = [uint64]0
        PreservesBase = [bool]$preservesBase
    }
}

function Test-EfiResidentReusedImageBase {
    param(
        [Parameter(Mandatory=$true)][object[]]$Instructions,
        [Parameter(Mandatory=$true)][System.Collections.Generic.HashSet[UInt64]]$InstructionStarts,
        [Parameter(Mandatory=$true)][object[]]$ValidatedTables,
        [Parameter(Mandatory=$true)][uint64]$EntryAddress,
        [Parameter(Mandatory=$true)][uint64]$DispatchAddress,
        [Parameter(Mandatory=$true)][string]$BaseRegister,
        [Parameter(Mandatory=$true)][uint64]$SectionStart
    )

    # This is local provenance for a modeled dispatch sequence, not whole-program
    # control-flow integrity.
    $entryFacts = @($ValidatedTables | Where-Object {
        $_.Targets -contains [uint64]$EntryAddress
    })
    if ($entryFacts.Count -eq 0) { return $false }
    foreach ($fact in $entryFacts) {
        if ($fact.BaseRegister -ine $BaseRegister) { return $false }
    }

    $entryIndex = -1
    $dispatchIndex = -1
    for ($index = 0; $index -lt $Instructions.Count; ++$index) {
        if ($Instructions[$index].Address -eq $EntryAddress) { $entryIndex = $index }
        if ($Instructions[$index].Address -eq $DispatchAddress) { $dispatchIndex = $index }
    }
    if ($entryIndex -lt 0 -or $dispatchIndex -le $entryIndex) { return $false }
    $instructionCount = $dispatchIndex - $entryIndex
    if ($instructionCount -gt 64) { return $false }

    if ($entryIndex -eq 0) {
        if ($EntryAddress -ne $SectionStart) { return $false }
    } else {
        $previous = $Instructions[$entryIndex - 1].Text.Trim().ToLowerInvariant()
        if ($previous -notmatch '^(?:jmp\s+.+|ret(?:\s+.+)?)$') {
            return $false
        }
    }

    # An entry reached from another validated table carries the value established
    # by that table's dispatch. Conflicting aliases inside the candidate block
    # make the value ambiguous and therefore cannot support an exemption.
    foreach ($table in $ValidatedTables) {
        foreach ($target in $table.Targets) {
            if ($target -ge $EntryAddress -and $target -le $DispatchAddress -and
                $table.BaseRegister -ine $BaseRegister) {
                return $false
            }
        }
    }

    # Direct branches and calls from outside the proven block could enter it
    # without carrying the image-base value supplied by the validated table.
    foreach ($instruction in $Instructions) {
        if ($instruction.Address -ge $EntryAddress -and
            $instruction.Address -lt $DispatchAddress) { continue }
        $edge = [regex]::Match($instruction.Text.Trim(),
            '^(?:call|j[a-z]+)\s+(?:near ptr\s+)?(?<target>(?:0x)?[0-9a-f]{8,16}h?)\b',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $edge.Success) { continue }
        $targetAddress = ConvertTo-EfiResidentNumber $edge.Groups['target'].Value $true
        if ($targetAddress -ge $EntryAddress -and $targetAddress -le $DispatchAddress) {
            return $false
        }
    }

    for ($index = $entryIndex; $index -lt $dispatchIndex; ++$index) {
        $instruction = $Instructions[$index]
        $proof = Test-EfiResidentBasePreservingInstruction `
            -Text $instruction.Text -BaseRegister $BaseRegister
        if ($null -eq $proof -or -not $proof.PreservesBase) { return $false }
        if ($proof.IsConditionalBranch) {
            $target = [uint64]$proof.BranchTarget
            if ($target -lt $EntryAddress -or $target -gt $DispatchAddress -or
                -not $InstructionStarts.Contains($target)) {
                return $false
            }
        }
    }
    return $true
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
    $instructions = @(Get-EfiResidentInstructions $DisassemblyText | Sort-Object Address)
    $starts = [System.Collections.Generic.HashSet[UInt64]]::new()
    foreach ($instruction in $instructions) {
        if ($instruction.Address -ge $ImageBase + $SectionRva -and
            $instruction.Address -lt $ImageBase + $sectionEnd) {
            [void]$starts.Add([uint64]$instruction.Address)
        }
    }

    $ranges = [System.Collections.Generic.List[object]]::new()
    $validatedTables = [System.Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $instructions.Count; ++$index) {
        $compare = [regex]::Match($instructions[$index].Text,
            '^cmp\s+(?<index>e(?:ax|bx|cx|dx|si|di|sp|bp)|r(?:8|9|1[0-5])d)\s*,\s*(?<bound>(?:0x)?[0-9a-f]+h?)$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $compare.Success -or $index + 4 -ge $instructions.Count) { continue }
        $branch = [regex]::Match($instructions[$index + 1].Text,
            '^ja\s+(?<target>(?:0x)?[0-9a-f]{8,16}h?)$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $branch.Success) { continue }

        $sequence = $index + 2
        $hasCdqe = $instructions[$sequence].Text -match '^cdqe$'
        $hasMovsxd = $false
        $extendedIndex64 = $null
        if ($hasCdqe) {
            ++$sequence
        } else {
            $movsxd = [regex]::Match($instructions[$sequence].Text,
                '^movsxd\s+(?<dst>r(?:ax|bx|cx|dx|si|di|bp|8|9|1[0-5]))\s*,\s*(?<src>e(?:ax|bx|cx|dx|si|di|sp|bp)|r(?:8|9|1[0-5])d)$',
                [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($movsxd.Success) {
                # A signed extension is safe after the unsigned `ja` guard only
                # when it extends the exact value that was compared. The load
                # below must then use this same 64-bit destination as its index.
                if ($movsxd.Groups['src'].Value -ine $compare.Groups['index'].Value) {
                    continue
                }
                $hasMovsxd = $true
                $extendedIndex64 = Get-EfiResidentRegisterAlias64 `
                    $movsxd.Groups['dst'].Value
                ++$sequence
            }
        }
        if ($sequence + 2 -ge $instructions.Count) { continue }

        $lea = [regex]::Match($instructions[$sequence].Text,
            '^lea\s+(?<base>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))\s*,\s*\[(?<image>(?:0x)?[0-9a-f]+h?)\]$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        $reusesBase = -not $lea.Success
        $loadIndex = if ($reusesBase) { $sequence } else { $sequence + 1 }
        $addIndex = $loadIndex + 1
        $jumpIndex = $loadIndex + 2
        if ($jumpIndex -ge $instructions.Count) { continue }
        $load = [regex]::Match($instructions[$loadIndex].Text,
            '^mov\s+(?<dst>e(?:ax|bx|cx|dx|si|di|sp|bp)|r(?:8|9|1[0-5])d)\s*,\s*dword ptr\s*\[\s*(?<base>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))\s*\+\s*(?<index>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))\s*\*\s*4\s*\+\s*(?<table>(?:0x)?[0-9a-f]+h?)\s*\]$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $load.Success) { continue }
        $add = [regex]::Match($instructions[$addIndex].Text,
            '^add\s+(?<dst>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))\s*,\s*(?<base>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        $jump = [regex]::Match($instructions[$jumpIndex].Text,
            '^jmp\s+(?<dst>r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|1[0-5]))$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $add.Success -or -not $jump.Success) { continue }

        $index64 = if ($hasMovsxd) {
            $extendedIndex64
        } else {
            Get-EfiResidentRegister64 $compare.Groups['index'].Value
        }
        $dst64 = Get-EfiResidentRegister64 $load.Groups['dst'].Value
        $baseRegister = if ($lea.Success) {
            $lea.Groups['base'].Value.ToLowerInvariant()
        } else {
            $load.Groups['base'].Value.ToLowerInvariant()
        }
        if ($hasCdqe -and $compare.Groups['index'].Value -ine 'eax') { continue }
        if ($null -eq $index64 -or $null -eq $dst64 -or
            ($lea.Success -and $load.Groups['base'].Value -ine $baseRegister) -or
            $load.Groups['index'].Value -ine $index64 -or
            $add.Groups['dst'].Value -ine $dst64 -or
            $add.Groups['base'].Value -ine $baseRegister -or
            $jump.Groups['dst'].Value -ine $dst64 -or
            $baseRegister -eq $index64 -or $baseRegister -eq $dst64) {
            continue
        }

        if ($reusesBase) {
            $entryProven = $false
            foreach ($priorTable in $validatedTables) {
                if ($priorTable.BaseRegister -ine $baseRegister) { continue }
                foreach ($entryTarget in $priorTable.Targets) {
                    if (Test-EfiResidentReusedImageBase `
                        -Instructions $instructions `
                        -InstructionStarts $starts `
                        -ValidatedTables $validatedTables.ToArray() `
                        -EntryAddress $entryTarget `
                        -DispatchAddress $instructions[$index].Address `
                        -BaseRegister $baseRegister `
                        -SectionStart ([uint64]($ImageBase + $SectionRva))) {
                        $entryProven = $true
                        break
                    }
                }
                if ($entryProven) { break }
            }
            if (-not $entryProven) { continue }
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
        $baseValue = if ($lea.Success) {
            ConvertTo-EfiResidentNumber $lea.Groups['image'].Value $true
        } else {
            [uint64]$ImageBase
        }
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
            $tableAddress -le $instructions[$jumpIndex].Address) {
            throw 'Resident RVA jump table must follow its dispatch code without overflow.'
        }
        $targetAddresses = [System.Collections.Generic.List[UInt64]]::new()
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
            foreach ($dataRange in $ranges) {
                if ($targetAddress -ge $dataRange.StartAddress -and
                    $targetAddress -lt $dataRange.EndAddress) {
                    throw 'Resident RVA jump table entry targets validated table data.'
                }
            }
            $targetAddresses.Add([uint64]$targetAddress)
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
        $validatedTables.Add([pscustomobject]@{
            BaseRegister = $baseRegister
            Targets = [uint64[]]@($targetAddresses)
            Range = $range
        })
        $index = $jumpIndex
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
