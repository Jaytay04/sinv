Set-StrictMode -Version 2.0

$script:GpuFirmwareComparisonKnownWindowsBootManagerId = '9dea862c-5cdd-4e70-acc1-f32b344d4795'
$script:GpuFirmwareComparisonGenericTypes = [ordered]@{
    'UEFI:CD/DVD Drive' = 'CD/DVD'
    'UEFI:Removable Device' = 'Removable'
    'UEFI:Network Device' = 'Network'
}

function Get-GpuFirmwareComparisonSha256 {
    param([Parameter(Mandatory = $true)][string]$Text)

    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Text)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
    }
}

function ConvertTo-GpuFirmwareComparisonGuid {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Context
    )

    if ($Value -cnotmatch '^\{([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})\}$') {
        throw "$Context must contain exactly one braced GUID."
    }
    return $Matches[1].ToLowerInvariant()
}

function ConvertFrom-GpuFirmwareText {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text)

    if ([string]::IsNullOrEmpty($Text)) { throw 'Firmware enumeration text is empty.' }

    # Normalize only line endings and terminal blank lines. In particular, do not
    # trim leading whitespace or alter the contents of stable BCD blocks.
    $normalized = $Text.Replace("`r`n", "`n")
    if ($normalized.Contains("`r")) { throw 'Firmware enumeration contains a lone carriage return.' }
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($line in $normalized.Split([char]10)) { $lines.Add([string]$line) }
    while ($lines.Count -gt 0 -and $lines[$lines.Count - 1].Length -eq 0) { $lines.RemoveAt($lines.Count - 1) }
    if ($lines.Count -eq 0) { throw 'Firmware enumeration contains no sections.' }

    $sections = [Collections.Generic.List[object]]::new()
    $index = 0
    $preamble = [Collections.Generic.List[string]]::new()
    while ($index -lt $lines.Count -and $lines[$index].Length -eq 0) {
        $preamble.Add('')
        $index++
    }
    if ($preamble.Count -gt 1) { throw 'Firmware enumeration has ambiguous leading blank lines.' }

    while ($index -lt $lines.Count) {
        if ($lines[$index].Length -eq 0) { throw 'Firmware enumeration has an unexpected blank line between sections.' }
        if ($index + 1 -ge $lines.Count -or $lines[$index + 1] -cnotmatch '^-+$' -or $lines[$index + 1].Length -ne $lines[$index].Length) {
            throw "Firmware enumeration has a malformed section heading at line $($index + 1)."
        }

        $start = $index
        $title = $lines[$index]
        $index += 2
        $body = [Collections.Generic.List[string]]::new()
        while ($index -lt $lines.Count -and $lines[$index].Length -gt 0) {
            if ($index + 1 -lt $lines.Count -and $lines[$index + 1] -cmatch '^-+$' -and $lines[$index + 1].Length -eq $lines[$index].Length) {
                throw "Firmware section '$title' is missing its blank separator."
            }
            $body.Add($lines[$index])
            $index++
        }

        $rawLines = [Collections.Generic.List[string]]::new()
        $rawLines.Add($title)
        $rawLines.Add($lines[$start + 1])
        foreach ($bodyLine in $body) { $rawLines.Add($bodyLine) }
        $sections.Add([pscustomobject]@{
            Title = $title
            RawLines = $rawLines.ToArray()
            Fields = [Collections.Generic.List[object]]::new()
            Identifier = $null
            IdentifierKey = $null
            Description = $null
            DisplayOrder = @()
            CanonicalLines = $null
            IsGeneric = $false
            GenericType = $null
        })

        if ($index -lt $lines.Count) {
            # The block parser requires the native bcdedit blank-line separator.
            $index++
            if ($index -ge $lines.Count) { throw 'Firmware enumeration ends with an incomplete section separator.' }
        }
    }

    $identifiers = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    $firmwareManagers = [Collections.Generic.List[object]]::new()
    $windowsManagers = [Collections.Generic.List[object]]::new()
    $genericByType = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    $genericIdToType = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)

    foreach ($section in $sections) {
        $seenFields = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        $bodyIndex = 2
        while ($bodyIndex -lt $section.RawLines.Length) {
            $line = [string]$section.RawLines[$bodyIndex]
            if ($line -cmatch '^([A-Za-z][A-Za-z0-9]*)[ ]{2,}(.+)$') {
                $name = [string]$Matches[1]
                $value = [string]$Matches[2]
                if (-not $seenFields.Add($name)) { throw "Firmware section '$($section.Title)' repeats field '$name'." }
                $field = [pscustomobject]@{ Name = $name; Value = $value; LineIndex = $bodyIndex; Prefix = [string]$Matches[0].Substring(0, $Matches[0].Length - $value.Length) }
                $section.Fields.Add($field)
                if ([string]::Equals($name, 'identifier', [StringComparison]::Ordinal)) {
                    $section.Identifier = $value
                    $section.IdentifierKey = ConvertTo-GpuFirmwareComparisonGuid $value "Identifier in '$($section.Title)'"
                }
                elseif ([string]::Equals($name, 'description', [StringComparison]::Ordinal)) {
                    $section.Description = $value
                }
                elseif ([string]::Equals($name, 'displayorder', [StringComparison]::Ordinal)) {
                    if ([string]::Equals($section.Title, 'Firmware Boot Manager', [StringComparison]::Ordinal)) {
                        $order = [Collections.Generic.List[object]]::new()
                        $order.Add([pscustomobject]@{ Guid = (ConvertTo-GpuFirmwareComparisonGuid $value 'Firmware displayorder'); LineIndex = $bodyIndex })
                        $bodyIndex++
                        while ($bodyIndex -lt $section.RawLines.Length -and $section.RawLines[$bodyIndex] -cmatch '^ +(\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\})$') {
                            $order.Add([pscustomobject]@{ Guid = (ConvertTo-GpuFirmwareComparisonGuid ([string]$Matches[1]) 'Firmware displayorder continuation'); LineIndex = $bodyIndex })
                            $bodyIndex++
                        }
                        $section.DisplayOrder = $order.ToArray()
                        continue
                    }
                }
                $bodyIndex++
                continue
            }

            if ([string]::Equals($section.Title, 'Firmware Boot Manager', [StringComparison]::Ordinal)) {
                throw "Firmware Boot Manager has an unparsed or ambiguous line at line $($bodyIndex + 1)."
            }
            if ($line -match '^\s*(identifier|description|displayorder)\b') {
                throw "Firmware section '$($section.Title)' has a malformed recognized field at line $($bodyIndex + 1)."
            }
            $bodyIndex++
        }

        if ($null -eq $section.IdentifierKey) { throw "Firmware section '$($section.Title)' has no valid identifier field." }
        if ($identifiers.ContainsKey($section.IdentifierKey)) { throw "Firmware enumeration repeats identifier {$($section.IdentifierKey)}." }
        $identifiers.Add($section.IdentifierKey, $section)
        if ([string]::Equals($section.Title, 'Firmware Boot Manager', [StringComparison]::Ordinal)) { $firmwareManagers.Add($section) }
        if ([string]::Equals($section.Title, 'Windows Boot Manager', [StringComparison]::Ordinal)) { $windowsManagers.Add($section) }

        $matchedGenericType = $null
        foreach ($description in $script:GpuFirmwareComparisonGenericTypes.Keys) {
            if ([string]::Equals([string]$section.Description, [string]$description, [StringComparison]::Ordinal)) {
                $matchedGenericType = [string]$script:GpuFirmwareComparisonGenericTypes[$description]
                break
            }
        }
        if ($null -ne $section.Description -and $null -eq $matchedGenericType) {
            foreach ($description in $script:GpuFirmwareComparisonGenericTypes.Keys) {
                if ($section.Description -match ('^' + [regex]::Escape([string]$description) + '(?:$|[ ,;:/])')) {
                    throw "Firmware section '$($section.Title)' has a suspicious generic placeholder description."
                }
            }
        }
        if ($null -ne $matchedGenericType) {
            if (-not [string]::Equals($section.Title, 'Firmware Application (101fffff)', [StringComparison]::Ordinal)) {
                throw "Generic firmware placeholder '$($section.Description)' has an unexpected section heading."
            }
            if ($section.Fields.Count -ne 2 -or
                -not [string]::Equals([string]$section.Fields[0].Name, 'identifier', [StringComparison]::Ordinal) -or
                -not [string]::Equals([string]$section.Fields[1].Name, 'description', [StringComparison]::Ordinal) -or
                $section.RawLines.Length -ne 4) {
                throw "Generic firmware placeholder '$($section.Description)' contains fields or content that must not be normalized."
            }
            if ($genericByType.ContainsKey($matchedGenericType)) { throw "Firmware enumeration repeats generic placeholder type '$matchedGenericType'." }
            $section.IsGeneric = $true
            $section.GenericType = $matchedGenericType
            $genericByType.Add($matchedGenericType, $section)
            $genericIdToType.Add($section.IdentifierKey, $matchedGenericType)
        }
    }

    if ($firmwareManagers.Count -ne 1) { throw 'Firmware enumeration must contain exactly one Firmware Boot Manager section.' }
    if ($windowsManagers.Count -ne 1 -or
        -not [string]::Equals([string]$windowsManagers[0].IdentifierKey, $script:GpuFirmwareComparisonKnownWindowsBootManagerId, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Firmware enumeration must contain exactly one Windows Boot Manager with the known firmware identifier.'
    }

    $firmwareManager = $firmwareManagers[0]
    $displayOrderFields = @($firmwareManager.Fields | Where-Object { [string]::Equals([string]$_.Name, 'displayorder', [StringComparison]::Ordinal) })
    if ($displayOrderFields.Count -ne 1 -or @($firmwareManager.DisplayOrder).Count -eq 0) {
        throw 'Firmware Boot Manager must contain exactly one nonempty displayorder field.'
    }

    $seenReferences = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $stableReferences = [Collections.Generic.List[string]]::new()
    $genericReferenceTypes = [Collections.Generic.List[string]]::new()
    $sawGenericReference = $false
    $genericOrder = @('CD/DVD', 'Removable', 'Network')
    $lastGenericRank = -1
    foreach ($reference in $firmwareManager.DisplayOrder) {
        $guid = [string]$reference.Guid
        if (-not $seenReferences.Add($guid)) { throw "Firmware displayorder repeats reference {$guid}." }
        if (-not $identifiers.ContainsKey($guid)) { throw "Firmware displayorder reference {$guid} does not resolve to an enumerated entry." }
        if ($genericIdToType.ContainsKey($guid)) {
            $sawGenericReference = $true
            $type = $genericIdToType[$guid]
            $rank = [Array]::IndexOf($genericOrder, $type)
            if ($rank -le $lastGenericRank) { throw 'Generic firmware references are not in DVD, Removable, Network order.' }
            $lastGenericRank = $rank
            $genericReferenceTypes.Add($type)
        }
        else {
            if ($sawGenericReference) { throw 'Generic firmware references must be a suffix after all stable firmware references.' }
            $stableReferences.Add($guid)
        }
    }
    foreach ($type in @($genericByType.Keys)) {
        $genericEntry = $genericByType[$type]
        if (-not $seenReferences.Contains([string]$genericEntry.IdentifierKey)) {
            throw "Generic firmware placeholder '$type' is not referenced exactly once by Firmware Boot Manager displayorder."
        }
    }
    if ($stableReferences.Count -eq 0 -or
        -not [string]::Equals($stableReferences[0], $script:GpuFirmwareComparisonKnownWindowsBootManagerId, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Windows Boot Manager must be first in the retained firmware displayorder.'
    }

    $sectionsByStart = [Collections.Generic.List[object]]::new()
    foreach ($section in $sections) {
        if ($section.IsGeneric) { continue }
        if ([object]::ReferenceEquals($section, $firmwareManager)) {
            $genericReferenceSet = [Collections.Generic.HashSet[int]]::new()
            foreach ($reference in $firmwareManager.DisplayOrder) {
                if ($genericIdToType.ContainsKey([string]$reference.Guid)) { $genericReferenceSet.Add([int]$reference.LineIndex) | Out-Null }
            }
            $canonical = [Collections.Generic.List[string]]::new()
            for ($lineIndex = 0; $lineIndex -lt $section.RawLines.Length; $lineIndex++) {
                if (-not $genericReferenceSet.Contains($lineIndex)) { $canonical.Add([string]$section.RawLines[$lineIndex]) }
            }
            $section.CanonicalLines = $canonical.ToArray()
        }
        else {
            $section.CanonicalLines = [string[]]$section.RawLines
        }
        $sectionsByStart.Add($section)
    }

    $canonicalLines = [Collections.Generic.List[string]]::new()
    foreach ($line in $preamble) { $canonicalLines.Add([string]$line) }
    for ($sectionIndex = 0; $sectionIndex -lt $sectionsByStart.Count; $sectionIndex++) {
        if ($sectionIndex -gt 0) { $canonicalLines.Add('') }
        foreach ($line in $sectionsByStart[$sectionIndex].CanonicalLines) { $canonicalLines.Add([string]$line) }
    }
    $canonicalText = [string]::Join("`n", $canonicalLines.ToArray())
    $genericEntries = [Collections.Generic.List[object]]::new()
    foreach ($type in @('CD/DVD', 'Removable', 'Network')) {
        if ($genericByType.ContainsKey($type)) {
            $entry = $genericByType[$type]
            $genericEntries.Add([pscustomobject]@{
                Type = $type
                Identifier = [string]$entry.Identifier
                Description = [string]$entry.Description
                Referenced = [bool]($genericReferenceTypes -contains $type)
            })
        }
    }

    return [pscustomobject]@{
        SchemaVersion = 1
        CanonicalText = $canonicalText
        StableSha256 = Get-GpuFirmwareComparisonSha256 $canonicalText
        GenericEntries = $genericEntries.ToArray()
        StableEntryCount = $sectionsByStart.Count
    }
}

function Compare-GpuFirmwareText {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$BaselineText,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$CurrentText
    )

    $textEqual = [string]::Equals($BaselineText, $CurrentText, [StringComparison]::Ordinal)
    $baseline = ConvertFrom-GpuFirmwareText -Text $BaselineText
    $current = ConvertFrom-GpuFirmwareText -Text $CurrentText
    $equivalent = [string]::Equals([string]$baseline.CanonicalText, [string]$current.CanonicalText, [StringComparison]::Ordinal)

    $baselineByType = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($entry in $baseline.GenericEntries) { $baselineByType.Add([string]$entry.Type, $entry) }
    $currentByType = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($entry in $current.GenericEntries) { $currentByType.Add([string]$entry.Type, $entry) }
    $changes = [Collections.Generic.List[object]]::new()
    foreach ($type in @('CD/DVD', 'Removable', 'Network')) {
        $hasBaseline = $baselineByType.ContainsKey($type)
        $hasCurrent = $currentByType.ContainsKey($type)
        $baselineEntry = if ($hasBaseline) { $baselineByType[$type] } else { $null }
        $currentEntry = if ($hasCurrent) { $currentByType[$type] } else { $null }
        $changeParts = [Collections.Generic.List[string]]::new()
        if (-not $hasBaseline -and $hasCurrent) { $changeParts.Add('Added') }
        elseif ($hasBaseline -and -not $hasCurrent) { $changeParts.Add('Removed') }
        elseif ($hasBaseline -and $hasCurrent) {
            if (-not [string]::Equals([string]$baselineEntry.Identifier, [string]$currentEntry.Identifier, [StringComparison]::OrdinalIgnoreCase)) { $changeParts.Add('IdentifierReplaced') }
            if ([bool]$baselineEntry.Referenced -ne [bool]$currentEntry.Referenced) { $changeParts.Add('ReferenceChanged') }
        }
        if ($changeParts.Count -eq 0) { $changeParts.Add('Unchanged') }
        $changes.Add([pscustomobject]@{
            Type = $type
            BaselineIdentifier = if ($hasBaseline) { [string]$baselineEntry.Identifier } else { $null }
            CurrentIdentifier = if ($hasCurrent) { [string]$currentEntry.Identifier } else { $null }
            BaselinePresent = $hasBaseline
            CurrentPresent = $hasCurrent
            BaselineReferenced = if ($hasBaseline) { [bool]$baselineEntry.Referenced } else { $false }
            CurrentReferenced = if ($hasCurrent) { [bool]$currentEntry.Referenced } else { $false }
            Change = [string]::Join(';', $changeParts.ToArray())
        })
    }

    $reason = if ($equivalent) {
        if ($textEqual) { 'Original firmware enumeration strings are ordinally equal.' }
        else { 'Stable firmware enumeration content is equivalent after approved generic-placeholder and line-ending/final-blank normalization.' }
    }
    else { 'Stable firmware enumeration content differs.' }

    return [pscustomobject]@{
        SchemaVersion = 1
        Equivalent = $equivalent
        TextEqual = $textEqual
        Reason = $reason
        BaselineStableSha256 = [string]$baseline.StableSha256
        CurrentStableSha256 = [string]$current.StableSha256
        BaselineGenericEntries = @($baseline.GenericEntries)
        CurrentGenericEntries = @($current.GenericEntries)
        GenericChanges = $changes.ToArray()
    }
}

Export-ModuleMember -Function @('ConvertFrom-GpuFirmwareText', 'Compare-GpuFirmwareText')
