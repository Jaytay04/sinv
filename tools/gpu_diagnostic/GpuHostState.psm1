Set-StrictMode -Version 2.0

function Get-GpuCanonicalPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'Path must be a non-empty path.'
    }

    try {
        return [System.IO.Path]::GetFullPath($Path)
    }
    catch {
        throw "Path is invalid: $Path"
    }
}

function Get-GpuPathComparisonValue {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    $pathRoot = [System.IO.Path]::GetPathRoot($Path)
    if ($Path.Length -gt $pathRoot.Length) {
        return $Path.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    }
    return $Path
}

function Test-GpuReparsePoint {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    try {
        $attributes = [System.IO.File]::GetAttributes($Path)
    }
    catch [System.IO.FileNotFoundException] {
        return $false
    }
    catch [System.IO.DirectoryNotFoundException] {
        return $false
    }

    return (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
}

function Assert-GpuNoReparsePath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][bool]$IncludeLeaf
    )

    $canonicalPath = Get-GpuCanonicalPath -Path $Path
    $canonicalRoot = Get-GpuCanonicalPath -Path $Root
    $rootItem = Get-Item -LiteralPath $canonicalRoot -Force -ErrorAction Stop
    if (-not $rootItem.PSIsContainer) {
        throw "Root is not a directory: $canonicalRoot"
    }
    if (Test-GpuReparsePoint -Path $canonicalRoot) {
        throw "Reparse point is not permitted in the root path: $canonicalRoot"
    }

    $rootPrefix = $canonicalRoot
    if (-not $rootPrefix.EndsWith([System.IO.Path]::DirectorySeparatorChar.ToString(), [System.StringComparison]::Ordinal)) {
        $rootPrefix += [System.IO.Path]::DirectorySeparatorChar
    }

    if ([string]::Equals($canonicalPath, $canonicalRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        if ($IncludeLeaf -and (Test-GpuReparsePoint -Path $canonicalPath)) {
            throw "Reparse point is not permitted: $canonicalPath"
        }
        return
    }

    $relativePath = $null
    if ($canonicalPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        $relativePath = $canonicalPath.Substring($rootPrefix.Length)
    }
    else {
        throw "Path is outside the permitted root: $canonicalPath"
    }

    $segments = $relativePath.Split([char[]]@([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar), [System.StringSplitOptions]::RemoveEmptyEntries)
    $cursor = $canonicalRoot
    for ($index = 0; $index -lt $segments.Length; $index++) {
        $cursor = [System.IO.Path]::Combine($cursor, $segments[$index])
        $isFinal = ($index -eq ($segments.Length - 1))
        if ($isFinal -and -not $IncludeLeaf) {
            break
        }

        if (Test-GpuReparsePoint -Path $cursor) {
            throw "Reparse point is not permitted in the path: $cursor"
        }
    }
}

function Assert-GpuPathWithin {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [switch]$AllowRoot,
        [switch]$MustExist
    )

    $canonicalPath = Get-GpuCanonicalPath -Path $Path
    $canonicalRoot = Get-GpuCanonicalPath -Path $Root
    $pathComparison = Get-GpuPathComparisonValue -Path $canonicalPath
    $rootComparison = Get-GpuPathComparisonValue -Path $canonicalRoot

    if ([string]::Equals($pathComparison, $rootComparison, [System.StringComparison]::OrdinalIgnoreCase)) {
        if (-not $AllowRoot) {
            throw "The root itself is not an allowed target: $canonicalRoot"
        }
    }
    else {
        $rootPrefix = $rootComparison
        if (-not $rootPrefix.EndsWith([System.IO.Path]::DirectorySeparatorChar.ToString(), [System.StringComparison]::Ordinal)) {
            $rootPrefix += [System.IO.Path]::DirectorySeparatorChar
        }
        if (-not $canonicalPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Path is outside the permitted root: $canonicalPath"
        }
    }

    Assert-GpuNoReparsePath -Path $canonicalPath -Root $canonicalRoot -IncludeLeaf $true

    if ($MustExist -and -not (Test-Path -LiteralPath $canonicalPath)) {
        throw "Path does not exist: $canonicalPath"
    }

    return $canonicalPath
}

function Assert-GpuSafeFilePath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][bool]$MustExist
    )

    $canonicalPath = Get-GpuCanonicalPath -Path $Path
    $parentPath = [System.IO.Path]::GetDirectoryName($canonicalPath)
    if ([string]::IsNullOrEmpty($parentPath)) {
        throw "A parent directory is required: $canonicalPath"
    }

    $parent = Get-Item -LiteralPath $parentPath -Force -ErrorAction Stop
    if (-not $parent.PSIsContainer) {
        throw "Parent path is not a directory: $parentPath"
    }

    # Check the complete existing parent chain, including its filesystem root.
    $chain = New-Object System.Collections.Generic.List[string]
    $cursor = $parentPath
    while (-not [string]::IsNullOrEmpty($cursor)) {
        $chain.Add($cursor)
        $next = [System.IO.Directory]::GetParent($cursor)
        if ($null -eq $next) { break }
        $cursor = $next.FullName
    }
    for ($index = $chain.Count - 1; $index -ge 0; $index--) {
        if (Test-GpuReparsePoint -Path $chain[$index]) {
            throw "Reparse point is not permitted in the path: $($chain[$index])"
        }
    }

    $exists = Test-Path -LiteralPath $canonicalPath
    if ($MustExist -and -not $exists) {
        throw "File does not exist: $canonicalPath"
    }
    if ($exists) {
        $attributes = [System.IO.File]::GetAttributes($canonicalPath)
        if (($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
            throw "Path is not a regular file: $canonicalPath"
        }
        if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reparse point is not permitted: $canonicalPath"
        }
    }

    return $canonicalPath
}

function Write-GpuJson {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowNull()][object]$Value,
        [switch]$NoOverwrite
    )

    $canonicalPath = Assert-GpuSafeFilePath -Path $Path -MustExist $false
    $parentPath = [System.IO.Path]::GetDirectoryName($canonicalPath)
    $parent = Get-Item -LiteralPath $parentPath -Force -ErrorAction Stop
    if (-not $parent.PSIsContainer) {
        throw "Parent path is not a directory: $parentPath"
    }

    $targetExists = [System.IO.File]::Exists($canonicalPath)
    if ($NoOverwrite -and $targetExists) {
        throw "File already exists and NoOverwrite was specified: $canonicalPath"
    }

    $json = ConvertTo-Json -InputObject $Value -Depth 100 -Compress -ErrorAction Stop
    if ([string]::IsNullOrWhiteSpace($json)) {
        throw 'JSON serialization produced an empty document.'
    }

    $operationId = [guid]::NewGuid().ToString('N')
    $temporaryPath = [System.IO.Path]::Combine(
        $parentPath,
        ([System.IO.Path]::GetFileName($canonicalPath) + '.' + $operationId + '.tmp')
    )
    $backupPath = [System.IO.Path]::Combine(
        $parentPath,
        ([System.IO.Path]::GetFileName($canonicalPath) + '.' + $operationId + '.backup.tmp')
    )
    $encoding = New-Object System.Text.UTF8Encoding($false)
    $stream = $null
    try {
        $stream = New-Object System.IO.FileStream(
            $temporaryPath,
            [System.IO.FileMode]::CreateNew,
            [System.IO.FileAccess]::Write,
            [System.IO.FileShare]::None
        )
        $writer = New-Object System.IO.StreamWriter($stream, $encoding)
        try {
            $writer.Write($json)
            $writer.Flush()
            $stream.Flush($true)
        }
        finally {
            $writer.Dispose()
            $stream = $null
        }

        # Recheck the destination immediately before the atomic rename/replace.
        [void](Assert-GpuSafeFilePath -Path $canonicalPath -MustExist $false)
        if ($NoOverwrite) {
            [System.IO.File]::Move($temporaryPath, $canonicalPath)
        }
        elseif ([System.IO.File]::Exists($canonicalPath)) {
            [System.IO.File]::Replace($temporaryPath, $canonicalPath, $backupPath, $true)
        }
        else {
            [System.IO.File]::Move($temporaryPath, $canonicalPath)
        }
    }
    finally {
        if ($null -ne $stream) {
            $stream.Dispose()
        }
        if ([System.IO.File]::Exists($temporaryPath)) {
            [System.IO.File]::Delete($temporaryPath)
        }
        if ([System.IO.File]::Exists($backupPath)) {
            [System.IO.File]::Delete($backupPath)
        }
    }
}

function Read-GpuJson {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    $canonicalPath = Assert-GpuSafeFilePath -Path $Path -MustExist $true
    $text = [System.IO.File]::ReadAllText($canonicalPath, (New-Object System.Text.UTF8Encoding($false, $true)))
    if ([string]::IsNullOrWhiteSpace($text)) {
        throw "JSON file is empty: $canonicalPath"
    }

    try {
        return ConvertFrom-Json -InputObject $text -ErrorAction Stop
    }
    catch {
        throw "JSON file is malformed: $canonicalPath"
    }
}

function Get-GpuFilePin {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    $canonicalPath = Assert-GpuSafeFilePath -Path $Path -MustExist $true
    $stream = $null
    $sha = $null
    try {
        $stream = New-Object System.IO.FileStream(
            $canonicalPath,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::Read
        )
        $bytes = [long]$stream.Length
        $algorithm = [System.Security.Cryptography.SHA256]::Create()
        try {
            $digest = $algorithm.ComputeHash($stream)
            $sha = ([System.BitConverter]::ToString($digest)).Replace('-', '')
        }
        finally {
            $algorithm.Dispose()
        }
    }
    finally {
        if ($null -ne $stream) { $stream.Dispose() }
    }

    return [pscustomobject]@{
        Path   = $canonicalPath
        Sha256 = $sha
        Bytes  = $bytes
    }
}

function Assert-GpuFilePin {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Pin
    )

    if ($null -eq $Pin -or $null -eq $Pin.PSObject.Properties['Path'] -or
        $null -eq $Pin.PSObject.Properties['Sha256'] -or $null -eq $Pin.PSObject.Properties['Bytes']) {
        throw 'Pin must contain Path, Sha256, and Bytes properties.'
    }

    $pinPath = [string]$Pin.Path
    $pinHash = [string]$Pin.Sha256
    if ([string]::IsNullOrWhiteSpace($pinPath) -or $pinHash -notmatch '^[0-9A-Fa-f]{64}$') {
        throw 'Pin contains an invalid path or SHA-256 value.'
    }

    $pinBytes = 0L
    $bytesText = [string]$Pin.Bytes
    if (-not [long]::TryParse(
        $bytesText,
        [System.Globalization.NumberStyles]::None,
        [System.Globalization.CultureInfo]::InvariantCulture,
        [ref]$pinBytes
    )) {
        throw 'Pin byte count must be a non-negative integer.'
    }
    if ($pinBytes -lt 0) {
        throw 'Pin byte count must be a non-negative integer.'
    }

    $canonicalPinPath = Get-GpuCanonicalPath -Path $pinPath
    if (-not [string]::Equals($canonicalPinPath, $pinPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Pin path must already be canonical.'
    }

    $actual = Get-GpuFilePin -Path $canonicalPinPath
    if (-not [string]::Equals($actual.Sha256, $pinHash, [System.StringComparison]::OrdinalIgnoreCase) -or
        $actual.Bytes -ne $pinBytes) {
        throw "Pinned file has drifted: $canonicalPinPath"
    }
}

function Assert-GpuEspIdentity {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$Actual
    )

    $volumePattern = '^\\\\\?\\Volume\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}\\\z'
    if ($Expected -notmatch $volumePattern -or $Actual -notmatch $volumePattern) {
        throw 'ESP identity must use the exact Windows volume GUID path form.'
    }
    if (-not [string]::Equals($Expected, $Actual, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Expected and actual ESP identities do not match.'
    }
}

function Get-GpuHealthVerdict {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $false)][AllowNull()][string[]]$Lines,
        [Parameter(Mandatory = $true)][int]$ExpectedCpus,
        [Parameter(Mandatory = $false)][ValidateSet('PASS', 'ABSENT')][string]$ExpectedResult = 'PASS'
    )

    $reasons = New-Object System.Collections.Generic.List[string]
    $cpuRecords = New-Object 'System.Collections.Generic.Dictionary[string,string]' ([System.StringComparer]::Ordinal)
    $cpuPattern = '^UEFI_HYPERVISOR_WINDOWS_CPU:([0-9]+):([0-9]+):(PASS|FAIL|ABSENT)\z'
    $aggregatePattern = '^UEFI_HYPERVISOR_WINDOWS_CPUID:(PASS|FAIL|ABSENT)\z'
    $aggregateCount = 0
    $aggregate = $null
    $cpuPassCount = 0

    if ($ExpectedCpus -le 0) {
        [void]$reasons.Add('ExpectedCpus must be greater than zero.')
    }
    if ([string]::IsNullOrWhiteSpace($ExpectedResult)) {
        $ExpectedResult = 'PASS'
    }

    foreach ($line in @($Lines)) {
        if ($null -eq $line) { continue }
        if ($line.StartsWith('UEFI_HYPERVISOR_WINDOWS_CPUID', [System.StringComparison]::Ordinal)) {
            $match = [System.Text.RegularExpressions.Regex]::Match($line, $aggregatePattern, [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)
            if (-not $match.Success) {
                [void]$reasons.Add('Malformed aggregate health record.')
                continue
            }
            $aggregateCount++
            if ($aggregateCount -gt 1) {
                [void]$reasons.Add('Duplicate aggregate health record.')
                continue
            }
            $aggregate = $match.Groups[1].Value
            if ($aggregate -ne $ExpectedResult) {
                [void]$reasons.Add("Aggregate result differs from expected $ExpectedResult.")
            }
            continue
        }

        if ($line.StartsWith('UEFI_HYPERVISOR_WINDOWS_CPU', [System.StringComparison]::Ordinal)) {
            $match = [System.Text.RegularExpressions.Regex]::Match($line, $cpuPattern, [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)
            if (-not $match.Success) {
                [void]$reasons.Add('Malformed per-CPU health record.')
                continue
            }

            $groupNumber = 0
            $cpuNumber = 0
            if (-not [int]::TryParse($match.Groups[1].Value, [System.Globalization.NumberStyles]::None, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$groupNumber) -or
                -not [int]::TryParse($match.Groups[2].Value, [System.Globalization.NumberStyles]::None, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$cpuNumber) -or
                $groupNumber -gt 65535 -or $cpuNumber -ge 64) {
                [void]$reasons.Add('Per-CPU health record has an out-of-range group or CPU index.')
                continue
            }

            $key = $groupNumber.ToString([System.Globalization.CultureInfo]::InvariantCulture) + ':' + $cpuNumber.ToString([System.Globalization.CultureInfo]::InvariantCulture)
            if ($cpuRecords.ContainsKey($key)) {
                [void]$reasons.Add("Duplicate per-CPU health record: $key.")
                continue
            }
            $result = $match.Groups[3].Value
            $cpuRecords.Add($key, $result)
            if ($result -eq 'PASS') { $cpuPassCount++ }
            if ($result -ne $ExpectedResult) {
                [void]$reasons.Add("Per-CPU result differs from expected ${ExpectedResult}: $key=$result.")
            }
        }
    }

    if ($cpuRecords.Count -ne $ExpectedCpus) {
        [void]$reasons.Add("Expected $ExpectedCpus per-CPU records, found $($cpuRecords.Count).")
    }
    if ($aggregateCount -eq 0) {
        [void]$reasons.Add('Aggregate health record is missing.')
    }

    $passed = ($ExpectedCpus -gt 0 -and $reasons.Count -eq 0 -and $aggregateCount -eq 1 -and $cpuRecords.Count -eq $ExpectedCpus)
    $reason = 'PASS'
    if (-not $passed) {
        $uniqueReasons = New-Object System.Collections.Generic.List[string]
        foreach ($entry in $reasons) {
            if (-not $uniqueReasons.Contains($entry)) { [void]$uniqueReasons.Add($entry) }
        }
        $reason = [string]::Join(' ', $uniqueReasons.ToArray())
    }

    return [pscustomobject]@{
        Passed         = [bool]$passed
        ExpectedResult = $ExpectedResult
        ExpectedCpus   = $ExpectedCpus
        CpuCount       = $cpuRecords.Count
        CpuPassCount   = $cpuPassCount
        Aggregate      = $aggregate
        Reason         = $reason
    }
}

function Assert-GpuActionAllowed {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Action,
        [Parameter(Mandatory = $true)][string]$Phase,
        [Parameter(Mandatory = $false)][AllowNull()][object]$InProgress
    )

    $phaseNames = @('Preparing', 'Prepared', 'Built', 'Signed', 'BackedUp', 'Staged', 'Armed', 'Collected', 'Disarmed', 'Complete')
    if ($phaseNames -notcontains $Phase) {
        throw "Unknown GPU action phase: $Phase"
    }

    $forwardPhases = @{
        Build       = 'Prepared'
        Sign        = 'Built'
        Backup      = 'Signed'
        Stage       = 'BackedUp'
        Arm         = 'Staged'
        Collect     = 'Armed'
        FinalVerify = 'Disarmed'
    }
    $recoveryActions = @('Status', 'Disarm', 'Rollback')
    $isRecovery = ($recoveryActions -contains $Action)
    $isForward = $forwardPhases.ContainsKey($Action)
    if (-not $isRecovery -and -not $isForward) {
        throw "Unknown GPU action: $Action"
    }

    if ($isRecovery) {
        return
    }

    $hasIntent = ($null -ne $InProgress) -and -not (($InProgress -is [string]) -and ($InProgress -ceq ''))
    if ($hasIntent) {
        throw "Action $Action is blocked while another action is in progress."
    }
    if ($Phase -ne $forwardPhases[$Action]) {
        throw "Action $Action is not allowed during phase $Phase; expected $($forwardPhases[$Action])."
    }
}

Export-ModuleMember -Function @(
    'Assert-GpuPathWithin',
    'Write-GpuJson',
    'Read-GpuJson',
    'Get-GpuFilePin',
    'Assert-GpuFilePin',
    'Assert-GpuEspIdentity',
    'Get-GpuHealthVerdict',
    'Assert-GpuActionAllowed'
)
