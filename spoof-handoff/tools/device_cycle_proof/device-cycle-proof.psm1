Set-StrictMode -Version 2.0

$script:DeviceCycleExpectedGuidByPnp = @{
    'ROOT\VMWARE\0001' = '{E89EFD0E-E452-43D4-8E93-45DA2AF75831}'
    'ROOT\VMWARE\0000' = '{219E20CD-E7F3-409C-9AB4-99FF1E29203E}'
    'PCI\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\3&11583659&1&A3' = '{1D78C3CF-2BF6-453B-ABAF-2320AF9137F1}'
    'PCI\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\4&3FA6681&0&00E2' = '{1291F611-0C4F-4ACF-99A6-E6A22D2F9026}'
}

$script:DeviceCycleNativeIndexByPnp = @{
    'ROOT\VMWARE\0001' = 0
    'ROOT\VMWARE\0000' = 1
    'PCI\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\3&11583659&1&A3' = 2
    'PCI\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\4&3FA6681&0&00E2' = 3
}

$script:DeviceCycleTsvIndexByPnp = @{
    'ROOT\VMWARE\0001' = 0
    'ROOT\VMWARE\0000' = 3
    'PCI\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\3&11583659&1&A3' = 4
    'PCI\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\4&3FA6681&0&00E2' = 5
}

function DeviceCycle-GetProperty {
    param([Parameter(Mandatory=$true)]$InputObject, [Parameter(Mandatory=$true)][string]$Name)
    if ($null -eq $InputObject) { return $null }
    foreach ($property in $InputObject.PSObject.Properties) {
        if ($property.Name -ieq $Name) { return $property.Value }
    }
    return $null
}

function DeviceCycle-NewCheck {
    param(
        [Parameter(Mandatory=$true)][string]$Field,
        [Parameter(Mandatory=$true)][string]$Expected,
        [AllowNull()][object]$Actual,
        [Parameter(Mandatory=$true)][bool]$Pass
    )
    $actualText = if ($null -eq $Actual) { '<missing>' } else { [string]$Actual }
    [pscustomobject]@{
        field = $Field
        expected = $Expected
        actual = $actualText
        pass = $Pass
        line = ('{0} expected=[{1}] actual=[{2}]' -f $Field, $Expected, $actualText)
    }
}

function DeviceCycle-ReadLockedMap {
    param([Parameter(Mandatory=$true)][string]$LockedTsvPath)
    $fullPath = [IO.Path]::GetFullPath($LockedTsvPath)
    if (-not [IO.File]::Exists($fullPath)) { throw "Locked value map is missing: $fullPath" }
    $text = [IO.File]::ReadAllText($fullPath)
    $lines = $text -split "`r?`n"
    while ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -eq '') { $lines = $lines[0..($lines.Count - 2)] }
    if ($lines.Count -lt 2 -or $lines[0] -cne "member`	device`	original`	replacement") {
        throw 'Locked value map header is missing or has changed.'
    }

    $diskRows = @()
    $adapterRows = @()
    for ($lineIndex = 1; $lineIndex -lt $lines.Count; $lineIndex++) {
        $parts = $lines[$lineIndex].Split([char]9)
        if ($parts.Count -ne 4) { throw "Locked value map row $($lineIndex + 1) has $($parts.Count) columns; expected 4." }
        if ($parts[0] -ceq 'FakeDiskSerial') {
            if ($parts[1] -cne 'disk:\\.\PHYSICALDRIVE0') { throw "Locked disk identity changed: expected=[disk:\\.\PHYSICALDRIVE0] actual=[$($parts[1])]" }
            $diskRows += [pscustomobject]@{ member=$parts[0]; device=$parts[1]; original=$parts[2]; replacement=$parts[3]; pnpDeviceId='SCSI\DISK&VEN_NVME&PROD_INTEL_SSDPEKNU01\5&8432851&0&000000'; devicePath='\\.\PHYSICALDRIVE0' }
        }
        elseif ($parts[0] -ceq 'FakeMacAddress') {
            if ($parts[1] -notmatch '^adapter:(\d+)\|(.+)$') { throw "Locked adapter identity is malformed on row $($lineIndex + 1): $($parts[1])" }
            $adapterRows += [pscustomobject]@{ member=$parts[0]; device=$parts[1]; original=$parts[2]; replacement=$parts[3]; sourceIndex=[int]$Matches[1]; pnpDeviceId=$Matches[2] }
        }
    }

    if ($diskRows.Count -ne 1) { throw "Locked value map has $($diskRows.Count) selected disk rows; expected exactly 1." }
    if ($adapterRows.Count -ne 4) { throw "Locked value map has $($adapterRows.Count) selected adapter rows; expected exactly 4." }
    if (@($adapterRows.pnpDeviceId | Select-Object -Unique).Count -ne 4) { throw 'Locked value map contains duplicate selected adapter PnP identities.' }
    foreach ($row in $adapterRows) {
        if (-not $script:DeviceCycleExpectedGuidByPnp.ContainsKey($row.pnpDeviceId)) { throw "Locked adapter identity is not in the pinned native-reader set: $($row.pnpDeviceId)" }
        if (-not $script:DeviceCycleTsvIndexByPnp.ContainsKey($row.pnpDeviceId) -or $row.sourceIndex -ne $script:DeviceCycleTsvIndexByPnp[$row.pnpDeviceId]) {
            throw "Locked adapter source identity changed for $($row.pnpDeviceId): TSV index=$($row.sourceIndex), expected index=$($script:DeviceCycleTsvIndexByPnp[$row.pnpDeviceId])."
        }
        if ($row.original -notmatch '^[0-9A-F]{2}(:[0-9A-F]{2}){5}$' -or $row.replacement -notmatch '^[0-9A-F]{2}(:[0-9A-F]{2}){5}$') {
            throw "Locked MAC format is invalid for $($row.pnpDeviceId)."
        }
    }
    if ($adapterRows.original -contains $null -or $adapterRows.replacement -contains $null) { throw 'Locked value map contains an empty adapter value.' }
    if ($diskRows[0].original.Length -eq 0 -or $diskRows[0].replacement.Length -eq 0) { throw 'Locked disk value map contains an empty serial.' }
    [pscustomobject]@{ path=$fullPath; sha256=(Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash; disk=$diskRows[0]; adapters=@($adapterRows) }
}

function DeviceCycle-ReadCapture {
    param([Parameter(Mandatory=$true)][string]$CapturePath)
    $fullPath = [IO.Path]::GetFullPath($CapturePath)
    if (-not [IO.File]::Exists($fullPath)) { throw "Evidence capture is missing: $fullPath" }
    $item = Get-Item -LiteralPath $fullPath -Force
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Evidence path is not a regular file: $fullPath" }
    $raw = [IO.File]::ReadAllText($fullPath)
    $parsed = ConvertFrom-Json -InputObject $raw -ErrorAction Stop
    if ($null -eq $parsed -or $parsed -is [array]) { throw "Evidence is not a single JSON object: $fullPath" }
    $capturedUtc = [string](DeviceCycle-GetProperty $parsed 'capturedUtc')
    $parsedTime = [DateTimeOffset]::MinValue
    if ([string]::IsNullOrWhiteSpace($capturedUtc) -or -not [DateTimeOffset]::TryParse($capturedUtc, [ref]$parsedTime) -or $parsedTime.Offset -ne [TimeSpan]::Zero) {
        throw "Evidence capturedUtc is missing or is not an explicit UTC timestamp: $fullPath"
    }
    [pscustomobject]@{
        path=$fullPath
        sha256=(Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash
        length=$item.Length
        capturedUtc=$parsedTime.ToUniversalTime().ToString('o')
        capturedUtcTicks=$parsedTime.UtcDateTime.Ticks
        document=$parsed
    }
}

function DeviceCycle-TestNativeDocument {
    param($Capture, $LockedMap, [ValidateSet('Original','Replacement')][string]$Mode)
    $checks = New-Object 'System.Collections.Generic.List[object]'
    $doc = $Capture.document
    $schema = [string](DeviceCycle-GetProperty $doc 'schema')
    $checks.Add((DeviceCycle-NewCheck 'native.schema' 'independent-native-device-io.v1' $schema ($schema -ceq 'independent-native-device-io.v1')))
    foreach ($name in @('readOnly','successRequiresEverySelectedQuery','captureComplete')) {
        $actual = DeviceCycle-GetProperty $doc $name
        $checks.Add((DeviceCycle-NewCheck ("native.{0}" -f $name) 'true' $actual ($actual -is [bool] -and $actual)))
    }
    $replacementEnabled = DeviceCycle-GetProperty $doc 'replacementEnabled'
    $checks.Add((DeviceCycle-NewCheck 'native.readerReplacementEnabled' 'false (reader is read-only)' $replacementEnabled ($replacementEnabled -is [bool] -and -not $replacementEnabled)))
    $adapterRead = [string](DeviceCycle-GetProperty $doc 'adapterRead')
    $checks.Add((DeviceCycle-NewCheck 'native.adapterReadPath' 'direct IOCTL_NDIS_QUERY_GLOBAL_STATS + OID_802_3_CURRENT_ADDRESS' $adapterRead ($adapterRead.Contains('IOCTL_NDIS_QUERY_GLOBAL_STATS') -and $adapterRead.Contains('OID_802_3_CURRENT_ADDRESS'))))
    $diskRead = [string](DeviceCycle-GetProperty $doc 'diskRead')
    $checks.Add((DeviceCycle-NewCheck 'native.diskReadPath' 'direct IOCTL_STORAGE_QUERY_PROPERTY + StorageDeviceProperty' $diskRead ($diskRead.Contains('IOCTL_STORAGE_QUERY_PROPERTY') -and $diskRead.Contains('StorageDeviceProperty'))))

    $adapters = DeviceCycle-GetProperty $doc 'adapters'
    if ($null -eq $adapters -or $adapters -isnot [array] -or $adapters.Count -ne 4) {
        $checks.Add((DeviceCycle-NewCheck 'native.adapterRowCount' '4 selected rows' $(if ($null -eq $adapters) {'<missing>'} else { [string]@($adapters).Count }) $false))
        $adapters = @()
    }
    else {
        $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($row in $adapters) {
            $pnp = [string](DeviceCycle-GetProperty $row 'pnpDeviceId')
            if (-not $seen.Add($pnp)) { $checks.Add((DeviceCycle-NewCheck "native.adapter[$pnp].uniquePnp" 'one row' 'duplicate row' $false)) }
        }
    }

    $expectedValueByPnp = @{}
    foreach ($row in $LockedMap.adapters) { $expectedValueByPnp[$row.pnpDeviceId] = if ($Mode -ceq 'Original') { $row.original } else { $row.replacement } }
    $matchedPnp = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $adapters) {
        $pnp = [string](DeviceCycle-GetProperty $row 'pnpDeviceId')
        if (-not $expectedValueByPnp.ContainsKey($pnp)) {
            $checks.Add((DeviceCycle-NewCheck "native.adapter[$pnp].selectedIdentity" 'one of the four TSV-pinned PnP identities' $pnp $false))
            continue
        }
        [void]$matchedPnp.Add($pnp)
        $expectedMac = [string]$expectedValueByPnp[$pnp]
        $expectedGuid = [string]$script:DeviceCycleExpectedGuidByPnp[$pnp]
        $expectedIndex = [int]$script:DeviceCycleNativeIndexByPnp[$pnp]
        foreach ($pair in @(
            @('actualPnpDeviceId',$pnp),
            @('expectedInterfaceGuid',$expectedGuid),
            @('actualInterfaceGuid',$expectedGuid),
            @('oid','OID_802_3_CURRENT_ADDRESS'),
            @('oidCode','0x01010102'),
            @('ioctl','IOCTL_NDIS_QUERY_GLOBAL_STATS'),
            @('lockedOriginal',([string]($LockedMap.adapters | Where-Object pnpDeviceId -CEQ $pnp | Select-Object -First 1).original)),
            @('lockedReplacement',([string]($LockedMap.adapters | Where-Object pnpDeviceId -CEQ $pnp | Select-Object -First 1).replacement)),
            @('stage','complete'),
            @('win32Error','0'),
            @('bytesReturned','6'),
            @('index',[string]$expectedIndex),
            @('mac',$expectedMac)
        )) {
            $key = [string]$pair[0]
            $expectedText = [string]$pair[1]
            $actual = DeviceCycle-GetProperty $row $key
            $actualText = if ($null -eq $actual) { $null } else { [string]$actual }
            $isPass = if ($key -in @('actualPnpDeviceId','expectedInterfaceGuid','actualInterfaceGuid')) { [string]::Equals($actualText,$expectedText,[StringComparison]::OrdinalIgnoreCase) } else { [string]::Equals($actualText,$expectedText,[StringComparison]::Ordinal) }
            $checks.Add((DeviceCycle-NewCheck ("native.adapter[$pnp].{0}" -f $key) $expectedText $actualText $isPass))
        }
        $rawMac = ([string](DeviceCycle-GetProperty $row 'rawMacBytesHex')).ToUpperInvariant()
        $expectedHex = ($expectedMac -replace ':','')
        $checks.Add((DeviceCycle-NewCheck "native.adapter[$pnp].rawMacBytesHex" $expectedHex $rawMac ([string]::Equals($rawMac,$expectedHex,[StringComparison]::Ordinal))))
        $expectedOriginal = [string]($LockedMap.adapters | Where-Object pnpDeviceId -CEQ $pnp | Select-Object -First 1).original
        $expectedReplacement = [string]($LockedMap.adapters | Where-Object pnpDeviceId -CEQ $pnp | Select-Object -First 1).replacement
        if ($expectedOriginal -ceq $expectedReplacement) { $checks.Add((DeviceCycle-NewCheck "native.adapter[$pnp].lockedValuesDistinct" 'original differs from replacement' $expectedMac $false)) }
    }
    foreach ($row in $LockedMap.adapters) {
        if (-not $matchedPnp.Contains($row.pnpDeviceId)) { $checks.Add((DeviceCycle-NewCheck "native.adapter[$($row.pnpDeviceId)].present" 'exactly one selected row' '<missing>' $false)) }
    }

    $disk = DeviceCycle-GetProperty $doc 'disk'
    $expectedDisk = if ($Mode -ceq 'Original') { $LockedMap.disk.original } else { $LockedMap.disk.replacement }
    if ($null -eq $disk) {
        $checks.Add((DeviceCycle-NewCheck 'native.disk.present' 'one selected disk result' '<missing>' $false))
    }
    else {
        foreach ($pair in @(
            @('devicePath',$LockedMap.disk.devicePath),
            @('pnpDeviceId',$LockedMap.disk.pnpDeviceId),
            @('ioctl','IOCTL_STORAGE_QUERY_PROPERTY'),
            @('property','StorageDeviceProperty'),
            @('serialOffsetMeaning','STORAGE_DEVICE_DESCRIPTOR.SerialNumberOffset'),
            @('lockedOriginal',$LockedMap.disk.original),
            @('lockedReplacement',$LockedMap.disk.replacement),
            @('stage','complete'),
            @('win32Error','0'),
            @('serial',$expectedDisk)
        )) {
            $key = [string]$pair[0]
            $expectedText = [string]$pair[1]
            $actual = DeviceCycle-GetProperty $disk $key
            $actualText = if ($null -eq $actual) { $null } else { [string]$actual }
            $isPass = [string]::Equals($actualText,$expectedText,[StringComparison]::Ordinal)
            $checks.Add((DeviceCycle-NewCheck ("native.disk.{0}" -f $key) $expectedText $actualText $isPass))
        }
        $expectedSerialHex = [BitConverter]::ToString([Text.Encoding]::ASCII.GetBytes([string]$expectedDisk)).Replace('-','')
        $actualSerialHex = ([string](DeviceCycle-GetProperty $disk 'serialBytesHex')).ToUpperInvariant()
        $checks.Add((DeviceCycle-NewCheck 'native.disk.serialBytesHex' $expectedSerialHex $actualSerialHex ([string]::Equals($actualSerialHex,$expectedSerialHex,[StringComparison]::Ordinal))))
        $descriptorBytes = 0
        $descriptorRaw = DeviceCycle-GetProperty $disk 'descriptorBytes'
        $descriptorValid = ($null -ne $descriptorRaw -and [int]::TryParse([string]$descriptorRaw,[ref]$descriptorBytes) -and $descriptorBytes -gt 0)
        $checks.Add((DeviceCycle-NewCheck 'native.disk.descriptorBytes' 'positive byte count' $descriptorRaw $descriptorValid))
        $expectedDiskRow = $LockedMap.disk
        if ($expectedDiskRow.original -ceq $expectedDiskRow.replacement) { $checks.Add((DeviceCycle-NewCheck 'native.disk.lockedValuesDistinct' 'original differs from replacement' $expectedDisk $false)) }
    }

    [pscustomobject]@{ pass=(@($checks | Where-Object { -not $_.pass }).Count -eq 0); mode=$Mode; checks=@($checks.ToArray()) }
}

function Test-DeviceCycleNativeCapture {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$CapturePath,
        [Parameter(Mandatory=$true)][string]$LockedTsvPath,
        [Parameter(Mandatory=$true)][ValidateSet('Original','Replacement')][string]$Mode,
        [string]$NativeReaderPath
    )
    $capture = $null
    $locked = $null
    $failure = $null
    try { $capture = DeviceCycle-ReadCapture $CapturePath; $locked = DeviceCycle-ReadLockedMap $LockedTsvPath }
    catch { $failure = $_.Exception.Message }
    if ($null -ne $failure) {
        return [pscustomobject]@{ pass=$false; mode=$Mode; capturePath=[IO.Path]::GetFullPath($CapturePath); captureSha256=$null; capturedUtc=$null; readerPath=$null; readerSha256=$null; lockedMapPath=[IO.Path]::GetFullPath($LockedTsvPath); lockedMapSha256=$null; checks=@((DeviceCycle-NewCheck 'native.capture.validation' 'valid timestamped native evidence and locked map' $failure $false)) }
    }
    $result = DeviceCycle-TestNativeDocument $capture $locked $Mode
    $readerFullPath = $null
    $readerHash = $null
    $readerCheck = @()
    if (-not [string]::IsNullOrWhiteSpace($NativeReaderPath)) {
        $readerFullPath = [IO.Path]::GetFullPath($NativeReaderPath)
        if (-not [IO.File]::Exists($readerFullPath)) {
            $readerCheck += DeviceCycle-NewCheck 'native.readerBinary' 'existing native reader executable' '<missing>' $false
        } else {
            $isExe = [IO.Path]::GetExtension($readerFullPath) -ieq '.exe'
            $stream = [IO.File]::OpenRead($readerFullPath)
            try { $signature = New-Object byte[] 2; $readCount = $stream.Read($signature,0,2) } finally { $stream.Dispose() }
            $isPeHeader = ($isExe -and $readCount -eq 2 -and $signature[0] -eq 0x4D -and $signature[1] -eq 0x5A)
            $readerCheck += DeviceCycle-NewCheck 'native.readerBinaryPath' 'path to a PE .exe reader' $readerFullPath $isPeHeader
            if ($isPeHeader) {
                $readerHash = (Get-FileHash -LiteralPath $readerFullPath -Algorithm SHA256).Hash
                $readerCheck += DeviceCycle-NewCheck 'native.readerBinarySha256' 'SHA256 of supplied PE executable' $readerHash ($readerHash -match '^[A-Fa-f0-9]{64}$')
            }
        }
    }
    $checks = @($result.checks) + @($readerCheck)
    [pscustomobject]@{
        pass=(@($checks | Where-Object { -not $_.pass }).Count -eq 0)
        mode=$Mode
        capturePath=$capture.path
        captureSha256=$capture.sha256
        captureLength=$capture.length
        capturedUtc=$capture.capturedUtc
        readerPath=$readerFullPath
        readerSha256=$readerHash
        readerSource=$(if ($readerHash) { 'PE MZ header validated; SHA-256 calculated from the exact executable path supplied by the caller.' } else { $null })
        lockedMapPath=$locked.path
        lockedMapSha256=$locked.sha256
        checks=$checks
    }
}

function Test-DeviceCycleNativeRoundTrip {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$OriginalBeforePath,
        [Parameter(Mandatory=$true)][string]$ReplacementPath,
        [Parameter(Mandatory=$true)][string]$OriginalAfterPath,
        [Parameter(Mandatory=$true)][string]$LockedTsvPath,
        [string]$NativeReaderPath
    )
    $phaseResults = @(
        (Test-DeviceCycleNativeCapture -CapturePath $OriginalBeforePath -LockedTsvPath $LockedTsvPath -Mode Original -NativeReaderPath $NativeReaderPath),
        (Test-DeviceCycleNativeCapture -CapturePath $ReplacementPath -LockedTsvPath $LockedTsvPath -Mode Replacement -NativeReaderPath $NativeReaderPath),
        (Test-DeviceCycleNativeCapture -CapturePath $OriginalAfterPath -LockedTsvPath $LockedTsvPath -Mode Original -NativeReaderPath $NativeReaderPath)
    )
    $checks = New-Object 'System.Collections.Generic.List[object]'
    foreach ($phase in $phaseResults) {
        foreach ($check in $phase.checks) {
            if (-not $check.pass) { $checks.Add((DeviceCycle-NewCheck ("$($phase.mode).$($check.field)") $check.expected $check.actual $false)) }
        }
    }
    $paths = @($phaseResults | ForEach-Object { $_.capturePath })
    $hashes = @($phaseResults | ForEach-Object { $_.captureSha256 })
    $times = @()
    foreach ($phase in $phaseResults) {
        $timeValue = [DateTimeOffset]::MinValue
        if ([string]::IsNullOrWhiteSpace([string]$phase.capturedUtc) -or -not [DateTimeOffset]::TryParse([string]$phase.capturedUtc,[ref]$timeValue) -or $timeValue.Offset -ne [TimeSpan]::Zero) {
            $checks.Add((DeviceCycle-NewCheck ("roundTrip.$($phase.mode).capturedUtc") 'explicit UTC capture timestamp' $phase.capturedUtc $false))
            $times += [long]0
        } else { $times += $timeValue.UtcDateTime.Ticks }
    }
    if (@($paths | Select-Object -Unique).Count -ne 3) { $checks.Add((DeviceCycle-NewCheck 'roundTrip.distinctCapturePaths' '3 distinct paths' ($paths -join ' | ') $false)) }
    if (@($hashes | Select-Object -Unique).Count -ne 3) { $checks.Add((DeviceCycle-NewCheck 'roundTrip.distinctCaptureHashes' '3 distinct SHA-256 values' ($hashes -join ' | ') $false)) }
    $lockedHashes = @($phaseResults | ForEach-Object { $_.lockedMapSha256 })
    if (@($lockedHashes | Select-Object -Unique).Count -ne 1) { $checks.Add((DeviceCycle-NewCheck 'roundTrip.lockedMapHashConsistent' 'one locked TSV SHA-256 across all phases' ($lockedHashes -join ' | ') $false)) }
    $readerHashes = @($phaseResults | ForEach-Object { $_.readerSha256 } | Where-Object { $null -ne $_ })
    if ($readerHashes.Count -gt 0 -and @($readerHashes | Select-Object -Unique).Count -ne 1) { $checks.Add((DeviceCycle-NewCheck 'roundTrip.readerHashConsistent' 'one native reader SHA-256 across all phases' ($readerHashes -join ' | ') $false)) }
    $orderedTimes = ($times.Count -eq 3 -and $times[0] -lt $times[1] -and $times[1] -lt $times[2])
    $checks.Add((DeviceCycle-NewCheck 'roundTrip.strictCaptureTimeOrder' 'OriginalBefore < Replacement < OriginalAfter (UTC)' ($phaseResults.capturedUtc -join ' | ') $orderedTimes))
    [pscustomobject]@{
        pass=(@($checks | Where-Object { -not $_.pass }).Count -eq 0)
        mode='Original->Replacement->Original'
        evidence=@($phaseResults | ForEach-Object { [pscustomobject]@{mode=$_.mode;path=$_.capturePath;sha256=$_.captureSha256;capturedUtc=$_.capturedUtc;readerPath=$_.readerPath;readerSha256=$_.readerSha256;lockedMapPath=$_.lockedMapPath;lockedMapSha256=$_.lockedMapSha256;pass=$_.pass;checks=$_.checks} })
        checks=@($checks.ToArray())
    }
}

function Test-DeviceCycleWmiOriginals {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$CapturePath,
        [Parameter(Mandatory=$true)][string]$LockedTsvPath,
        [Parameter(Mandatory=$true)][string]$NotBeforeUtc
    )
    $checks = New-Object 'System.Collections.Generic.List[object]'
    try {
        $capture = DeviceCycle-ReadCapture $CapturePath
        $locked = DeviceCycle-ReadLockedMap $LockedTsvPath
        $threshold = [DateTimeOffset]::MinValue
        if (-not [DateTimeOffset]::TryParse($NotBeforeUtc,[ref]$threshold) -or $threshold.Offset -ne [TimeSpan]::Zero) { throw 'NotBeforeUtc must be an explicit UTC timestamp.' }
        $checks.Add((DeviceCycle-NewCheck 'wmi.freshness' ("capturedUtc later than {0}" -f $threshold.ToUniversalTime().ToString('o')) $capture.capturedUtc ($capture.capturedUtcTicks -gt $threshold.UtcDateTime.Ticks)))
        $doc = $capture.document
        $schema = [string](DeviceCycle-GetProperty $doc 'schema')
        if ($schema -cne 'device-cycle-wmi-readback.v1') { throw "Unsupported WMI evidence schema: $schema; expected device-cycle-wmi-readback.v1." }
        $complete = DeviceCycle-GetProperty $doc 'captureComplete'
        if ($null -eq $complete) { $complete = DeviceCycle-GetProperty $doc 'nativeCaptureComplete' }
        $checks.Add((DeviceCycle-NewCheck 'wmi.captureComplete' 'true' $complete ($complete -is [bool] -and $complete)))
        $sourceValue = DeviceCycle-GetProperty $doc 'wmiSource'
        if ($null -eq $sourceValue -or [string]::IsNullOrWhiteSpace([string]$sourceValue)) { $sourceValue = DeviceCycle-GetProperty $doc 'wmiSources' }
        if ($sourceValue -is [array]) { $source = $sourceValue -join '; ' } else { $source = [string]$sourceValue }
        if ([string]::IsNullOrWhiteSpace($source)) { $source = [string](DeviceCycle-GetProperty $doc 'source') }
        $checks.Add((DeviceCycle-NewCheck 'wmi.source' 'fresh Win32_DiskDrive + Win32_NetworkAdapter query' $source ($source -match 'Win32_DiskDrive' -and $source -match 'Win32_NetworkAdapter')))

        $diskRows = DeviceCycle-GetProperty $doc 'disks'
        if ($null -eq $diskRows) { $diskRows = DeviceCycle-GetProperty $doc 'allDisks' }
        $adapterRows = DeviceCycle-GetProperty $doc 'adapters'
        if ($null -eq $adapterRows) { $adapterRows = DeviceCycle-GetProperty $doc 'allWmiAdapters' }
        if ($null -eq $diskRows -or $null -eq $adapterRows) { throw 'WMI evidence is missing disks[] or adapters[] rows.' }

        $selectedDisks = @($diskRows | Where-Object { [string](DeviceCycle-GetProperty $_ 'PNPDeviceID') -ieq $locked.disk.pnpDeviceId })
        $diskGuidOrPathMatches = @($diskRows | Where-Object { [string](DeviceCycle-GetProperty $_ 'DeviceID') -ieq $locked.disk.devicePath })
        if ($selectedDisks.Count -ne 1) { $checks.Add((DeviceCycle-NewCheck 'wmi.disk.selectedCount' '1 row for locked disk PnP identity' $selectedDisks.Count $false)) }
        if ($diskGuidOrPathMatches.Count -ne 1) { $checks.Add((DeviceCycle-NewCheck 'wmi.disk.devicePathCount' '1 row for \\.\PHYSICALDRIVE0' $diskGuidOrPathMatches.Count $false)) }
        if ($selectedDisks.Count -eq 1) {
            $disk = $selectedDisks[0]
            $checks.Add((DeviceCycle-NewCheck 'wmi.disk.pnpDeviceId' $locked.disk.pnpDeviceId (DeviceCycle-GetProperty $disk 'PNPDeviceID') ([string]::Equals([string](DeviceCycle-GetProperty $disk 'PNPDeviceID'),$locked.disk.pnpDeviceId,[StringComparison]::OrdinalIgnoreCase))))
            $checks.Add((DeviceCycle-NewCheck 'wmi.disk.deviceId' $locked.disk.devicePath (DeviceCycle-GetProperty $disk 'DeviceID') ([string]::Equals([string](DeviceCycle-GetProperty $disk 'DeviceID'),$locked.disk.devicePath,[StringComparison]::OrdinalIgnoreCase))))
            $actualSerial = DeviceCycle-GetProperty $disk 'SerialNumber'
            $checks.Add((DeviceCycle-NewCheck 'wmi.disk.SerialNumber' $locked.disk.original $actualSerial ([string]::Equals([string]$actualSerial,$locked.disk.original,[StringComparison]::Ordinal))))
        }

        $mapByPnp = @{}
        foreach ($row in $locked.adapters) { $mapByPnp[$row.pnpDeviceId] = $row }
        $selectedAdapterRows = @()
        foreach ($row in $adapterRows) {
            $pnp = [string](DeviceCycle-GetProperty $row 'PNPDeviceID')
            $guid = [string](DeviceCycle-GetProperty $row 'GUID')
            $selectedByPnp = $mapByPnp.ContainsKey($pnp)
            $selectedByGuid = $script:DeviceCycleExpectedGuidByPnp.Values -contains $guid
            if ($selectedByGuid -and -not $selectedByPnp) { $checks.Add((DeviceCycle-NewCheck "wmi.adapter[$pnp].unexpectedSelectedIdentity" 'PnP ID corresponding to locked GUID' $guid $false)) }
            if ($selectedByPnp) { $selectedAdapterRows += $row }
        }
        $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($row in $selectedAdapterRows) {
            $pnp = [string](DeviceCycle-GetProperty $row 'PNPDeviceID')
            if (-not $seen.Add($pnp)) { $checks.Add((DeviceCycle-NewCheck "wmi.adapter[$pnp].uniquePnp" 'one row' 'duplicate row' $false)) }
            $lockedRow = $mapByPnp[$pnp]
            $expectedGuid = [string]$script:DeviceCycleExpectedGuidByPnp[$pnp]
            $actualGuid = [string](DeviceCycle-GetProperty $row 'GUID')
            $actualMac = [string](DeviceCycle-GetProperty $row 'MACAddress')
            $checks.Add((DeviceCycle-NewCheck "wmi.adapter[$pnp].PNPDeviceID" $lockedRow.pnpDeviceId $pnp ([string]::Equals($pnp,$lockedRow.pnpDeviceId,[StringComparison]::OrdinalIgnoreCase))))
            $checks.Add((DeviceCycle-NewCheck "wmi.adapter[$pnp].GUID" $expectedGuid $actualGuid ([string]::Equals($actualGuid,$expectedGuid,[StringComparison]::OrdinalIgnoreCase))))
            $checks.Add((DeviceCycle-NewCheck "wmi.adapter[$pnp].MACAddress" $lockedRow.original $actualMac ([string]::Equals($actualMac,$lockedRow.original,[StringComparison]::Ordinal))))
        }
        foreach ($row in $locked.adapters) {
            if (-not $seen.Contains($row.pnpDeviceId)) { $checks.Add((DeviceCycle-NewCheck "wmi.adapter[$($row.pnpDeviceId)].present" 'exactly one row with matching GUID' '<missing>' $false)) }
        }
    }
    catch { $checks.Add((DeviceCycle-NewCheck 'wmi.capture.validation' 'fresh complete WMI evidence with locked original values' $_.Exception.Message $false)); $capture=$null; $locked=$null }
    [pscustomobject]@{
        pass=(@($checks | Where-Object { -not $_.pass }).Count -eq 0)
        mode='FinalOriginalsOnly'
        capturePath=$(if ($capture) { $capture.path } else { [IO.Path]::GetFullPath($CapturePath) })
        captureSha256=$(if ($capture) { $capture.sha256 } else { $null })
        capturedUtc=$(if ($capture) { $capture.capturedUtc } else { $null })
        lockedMapPath=$(if ($locked) { $locked.path } else { [IO.Path]::GetFullPath($LockedTsvPath) })
        lockedMapSha256=$(if ($locked) { $locked.sha256 } else { $null })
        checks=@($checks.ToArray())
        claim='Compares final WMI observations to locked originals. It does not claim that WMI showed replacements while filters were active.'
    }
}

function Assert-DeviceCycleProof {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)]$Result)
    if (-not $Result.pass) {
        $lines = @($Result.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.line })
        throw ('Device cycle proof failed:' + [Environment]::NewLine + ($lines -join [Environment]::NewLine))
    }
    return $Result
}

Export-ModuleMember -Function Test-DeviceCycleNativeCapture,Test-DeviceCycleNativeRoundTrip,Test-DeviceCycleWmiOriginals,Assert-DeviceCycleProof
