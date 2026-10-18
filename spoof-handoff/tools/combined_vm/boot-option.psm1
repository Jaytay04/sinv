Set-StrictMode -Version Latest

function ConvertTo-EfiBootVariableName {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][ValidateRange(0, 65535)][int]$Number)
    return 'Boot{0:X4}' -f $Number
}

function ConvertFrom-EfiBootNextBytes {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)
    if ($Bytes.Length -ne 2) { throw "BootNext must contain exactly two bytes; actual=$($Bytes.Length)." }
    return [int][BitConverter]::ToUInt16($Bytes, 0)
}

function ConvertFrom-EfiBootOrderBytes {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)
    if (($Bytes.Length % 2) -ne 0) { throw "BootOrder has an odd byte count: $($Bytes.Length)." }
    $numbers = [Collections.Generic.List[int]]::new()
    for ($offset = 0; $offset -lt $Bytes.Length; $offset += 2) {
        $numbers.Add([int][BitConverter]::ToUInt16($Bytes, $offset))
    }
    $result = $numbers.ToArray()
    $unique = @($result | Select-Object -Unique)
    if ($unique.Count -ne $result.Length) { throw 'BootOrder contains a duplicate boot option number.' }
    return ,$result
}

function Assert-EfiBootNextPointsTo {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][ValidateRange(0, 65535)][int]$ExpectedNumber
    )
    $actualNumber = ConvertFrom-EfiBootNextBytes -Bytes $Bytes
    if ($actualNumber -ne $ExpectedNumber) {
        throw ('BootNext target mismatch: expected=0x{0:X4} actual=0x{1:X4}.' -f $ExpectedNumber, $actualNumber)
    }
    return $actualNumber
}

function Test-EfiVariableRecordEquals {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)][byte[]]$ExpectedBytes,
        [Parameter(Mandatory = $true)][ValidateRange(0, 4294967295)][uint32]$ExpectedAttributes
    )
    if ($null -eq $Actual -or $Actual.supported -ne $true -or [uint32]$Actual.attributes -ne $ExpectedAttributes) { return $false }
    $actualBase64 = [string]$Actual.bytesBase64
    if ([string]::IsNullOrWhiteSpace($actualBase64)) { return $false }
    try { $actualBytes = [Convert]::FromBase64String($actualBase64) } catch { return $false }
    return [Convert]::ToBase64String($actualBytes) -ceq [Convert]::ToBase64String($ExpectedBytes)
}

function New-EfiBootLoadOptionBytes {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Description,
        [ValidateRange(0, 4294967295)][uint32]$Attributes = 1,
        [Parameter(Mandatory = $true)][ValidateRange(0, 4294967295)][uint32]$PartitionNumber,
        [Parameter(Mandatory = $true)][ValidateRange(0, 18446744073709551615)][uint64]$PartitionStartLba,
        [Parameter(Mandatory = $true)][ValidateRange(1, 18446744073709551615)][uint64]$PartitionSizeLba,
        [Parameter(Mandatory = $true)][Guid]$PartitionGuid,
        [Parameter(Mandatory = $true)][string]$FilePath
    )

    if ([string]::IsNullOrWhiteSpace($Description) -or $Description.Contains([char]0)) {
        throw 'Boot option description must be non-empty and contain no NUL character.'
    }
    $normalizedPath = $FilePath.Replace('/', '\')
    if (-not $normalizedPath.StartsWith('\', [StringComparison]::Ordinal) -or $normalizedPath.Contains([char]0)) {
        throw 'EFI file path must be an absolute UEFI path beginning with a backslash.'
    }

    $strictUnicode = [Text.UnicodeEncoding]::new($false, $false, $true)
    try {
        $descriptionBytes = $strictUnicode.GetBytes($Description + [char]0)
        $pathBytes = $strictUnicode.GetBytes($normalizedPath + [char]0)
    } catch { throw 'Boot option text contains an invalid UTF-16 surrogate sequence.' }
    $filePathNodeLength = 4 + $pathBytes.Length
    $filePathListLength = 42 + $filePathNodeLength + 4
    if ($filePathNodeLength -gt 65535 -or $filePathListLength -gt 65535) {
        throw 'EFI device path exceeds the EFI_LOAD_OPTION 16-bit path-length limit.'
    }

    $bytes = [Collections.Generic.List[byte]]::new()
    $bytes.AddRange([BitConverter]::GetBytes($Attributes))
    $bytes.AddRange([BitConverter]::GetBytes([uint16]$filePathListLength))
    $bytes.AddRange($descriptionBytes)

    # EFI_HARD_DRIVE_DEVICE_PATH (Media / HardDrive), 42 bytes.
    $bytes.Add([byte]4)
    $bytes.Add([byte]1)
    $bytes.AddRange([BitConverter]::GetBytes([uint16]42))
    $bytes.AddRange([BitConverter]::GetBytes($PartitionNumber))
    $bytes.AddRange([BitConverter]::GetBytes($PartitionStartLba))
    $bytes.AddRange([BitConverter]::GetBytes($PartitionSizeLba))
    $bytes.AddRange($PartitionGuid.ToByteArray())
    $bytes.Add([byte]2) # GPT
    $bytes.Add([byte]2) # GUID signature

    # EFI_FILE_PATH_DEVICE_PATH (Media / FilePath).
    $bytes.Add([byte]4)
    $bytes.Add([byte]4)
    $bytes.AddRange([BitConverter]::GetBytes([uint16]$filePathNodeLength))
    $bytes.AddRange($pathBytes)

    # End entire device path.
    $bytes.Add([byte]0x7f)
    $bytes.Add([byte]0xff)
    $bytes.Add([byte]4)
    $bytes.Add([byte]0)
    return ,$bytes.ToArray()
}

function Read-EfiBootLoadOptionBytes {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    if ($Bytes.Length -lt 6) { throw "EFI_LOAD_OPTION is truncated: $($Bytes.Length) bytes." }
    $attributes = [BitConverter]::ToUInt32($Bytes, 0)
    $pathLength = [int][BitConverter]::ToUInt16($Bytes, 4)
    $cursor = 6
    $descriptionEnd = -1
    while (($cursor + 1) -lt $Bytes.Length) {
        if ($Bytes[$cursor] -eq 0 -and $Bytes[$cursor + 1] -eq 0) { $descriptionEnd = $cursor; break }
        $cursor += 2
    }
    if ($descriptionEnd -lt 6) { throw 'EFI_LOAD_OPTION description is not NUL-terminated UTF-16.' }
    $strictUnicode = [Text.UnicodeEncoding]::new($false, $false, $true)
    try { $description = $strictUnicode.GetString($Bytes, 6, $descriptionEnd - 6) }
    catch { throw 'EFI_LOAD_OPTION description contains invalid UTF-16.' }
    $devicePathOffset = $descriptionEnd + 2
    if ($pathLength -lt 46 -or ($devicePathOffset + $pathLength) -ne $Bytes.Length) {
        throw "EFI_LOAD_OPTION device path length is invalid: declared=$pathLength remaining=$($Bytes.Length - $devicePathOffset)."
    }

    $cursor = $devicePathOffset
    $devicePathEnd = $devicePathOffset + $pathLength
    $hardDrive = $null
    $filePath = $null
    $endNodeSeen = $false
    while ($cursor -lt $devicePathEnd) {
        if ($endNodeSeen) { throw 'EFI device path contains data after the end node.' }
        if (($cursor + 4) -gt $devicePathEnd) { throw 'EFI device path node header is truncated.' }
        $type = [int]$Bytes[$cursor]
        $subType = [int]$Bytes[$cursor + 1]
        $nodeLength = [int][BitConverter]::ToUInt16($Bytes, $cursor + 2)
        if ($nodeLength -lt 4 -or ($cursor + $nodeLength) -gt $devicePathEnd) { throw 'EFI device path node length is invalid.' }
        if ($type -eq 4 -and $subType -eq 1) {
            if ($null -ne $hardDrive -or $nodeLength -ne 42) { throw 'EFI hard-drive node is duplicated or has an invalid size.' }
            $guidBytes = [byte[]]::new(16)
            [Array]::Copy($Bytes, $cursor + 24, $guidBytes, 0, 16)
            $hardDrive = [ordered]@{
                partitionNumber = [BitConverter]::ToUInt32($Bytes, $cursor + 4)
                partitionStartLba = [BitConverter]::ToUInt64($Bytes, $cursor + 8)
                partitionSizeLba = [BitConverter]::ToUInt64($Bytes, $cursor + 16)
                partitionGuid = ([Guid]::new($guidBytes)).ToString('D')
                mbrType = [int]$Bytes[$cursor + 40]
                signatureType = [int]$Bytes[$cursor + 41]
            }
        } elseif ($type -eq 4 -and $subType -eq 4) {
            if ($null -ne $filePath -or $nodeLength -lt 6 -or (($nodeLength - 4) % 2) -ne 0) { throw 'EFI file-path node is duplicated or has an invalid size.' }
            try { $pathText = $strictUnicode.GetString($Bytes, $cursor + 4, $nodeLength - 4) }
            catch { throw 'EFI file-path node contains invalid UTF-16.' }
            if (-not $pathText.EndsWith([string][char]0) -or $pathText.TrimEnd([char]0).Contains([char]0)) { throw 'EFI file-path node is not exactly NUL-terminated.' }
            $filePath = $pathText.TrimEnd([char]0)
            if (-not $filePath.StartsWith('\', [StringComparison]::Ordinal) -or $filePath.Contains('/') -or
                @($filePath.Split('\') | Where-Object { $_ -ceq '.' -or $_ -ceq '..' }).Count -gt 0) {
                throw 'EFI file-path node is not a safe absolute UEFI path.'
            }
        } elseif ($type -eq 0x7f -and $subType -eq 0xff) {
            if ($nodeLength -ne 4 -or ($cursor + $nodeLength) -ne $devicePathEnd) { throw 'EFI device path end node is malformed or not final.' }
            $endNodeSeen = $true
        } else {
            throw ('Unsupported EFI device path node type/subtype: ' + $type + '/' + $subType)
        }
        $cursor += $nodeLength
    }
    if (-not $endNodeSeen -or $null -eq $hardDrive -or $null -eq $filePath) { throw 'EFI device path must contain one hard-drive node, one file-path node, and a terminal end node.' }
    if ([int]$hardDrive.mbrType -ne 2 -or [int]$hardDrive.signatureType -ne 2) { throw 'EFI hard-drive node must identify a GPT partition by GUID.' }
    return [ordered]@{ attributes = $attributes; active = (($attributes -band 1) -ne 0); description = $description; filePathListLength = $pathLength; hardDrive = $hardDrive; filePath = $filePath; totalBytes = $Bytes.Length }
}

Export-ModuleMember -Function ConvertTo-EfiBootVariableName, ConvertFrom-EfiBootNextBytes, ConvertFrom-EfiBootOrderBytes, Assert-EfiBootNextPointsTo, Test-EfiVariableRecordEquals, New-EfiBootLoadOptionBytes, Read-EfiBootLoadOptionBytes
