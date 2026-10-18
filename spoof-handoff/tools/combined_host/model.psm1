Set-StrictMode -Version 2.0

$script:CombinedSmbiosMembers = [ordered]@{
    FakeSMBIOSSerial = 'system_serial'
    FakeSMBIOSManufacturer = 'system_vendor'
    FakeSMBIOSProduct = 'system_product'
    FakeMotherboardSerial = 'board_serial'
    FakeMotherboardManufacturer = 'board_vendor'
    FakeMotherboardProduct = 'board_product'
    FakeSMBIOSUUID = 'system_uuid'
}

function Get-CombinedValue {
    param([AllowNull()][object]$InputObject, [Parameter(Mandatory=$true)][string]$Name)
    if ($null -eq $InputObject) { return $null }
    if ($InputObject -is [System.Collections.IDictionary]) {
        if ($InputObject.Contains($Name)) { return $InputObject[$Name] }
        return $null
    }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Get-CombinedMapRows {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Locked map does not exist: $Path" }
    $rows = @(Import-Csv -LiteralPath $Path -Delimiter "`t" -ErrorAction Stop)
    if ($rows.Count -eq 0) { throw 'Locked map has no rows.' }
    foreach ($column in @('member','device','original','replacement')) {
        if ($null -eq $rows[0].PSObject.Properties[$column]) { throw "Locked map is missing the required '$column' column." }
    }
    return ,$rows
}

function New-CombinedBindingMap {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][AllowEmptyCollection()][object[]]$MapRows)

    $rows = @($MapRows)
    $bindings = [System.Collections.Generic.List[object]]::new()
    $excludedDisks = [System.Collections.Generic.List[object]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $mappedSmbiosRows = @($rows | Where-Object { [string]$_.member -match '^Fake(SMBIOS|Motherboard)' })
    $unexpectedSmbiosRows = @($mappedSmbiosRows | Where-Object { $_.member -notin $script:CombinedSmbiosMembers.Keys })
    if ($mappedSmbiosRows.Count -ne 7 -or $unexpectedSmbiosRows.Count -gt 0) { throw "Locked map must contain exactly the seven confirmed SMBIOS fields; actual rows=$($mappedSmbiosRows.Count), unexpected=$($unexpectedSmbiosRows.Count)." }

    foreach ($member in $script:CombinedSmbiosMembers.Keys) {
        $matches = @($rows | Where-Object { [string]$_.member -ceq $member })
        if ($matches.Count -ne 1) { throw "Expected exactly one locked map row for $member; actual count=$($matches.Count)." }
        $row = $matches[0]
        $field = [string]$script:CombinedSmbiosMembers[$member]
        $expectedType = if ($field -in @('system_serial','system_vendor','system_product','system_uuid')) { 'SMBIOS Type 1' } else { 'SMBIOS Type 2' }
        if ([string]$row.device -cne $expectedType) { throw "Locked map row $member has device '$($row.device)'; expected '$expectedType'." }
        if ([string]::IsNullOrWhiteSpace([string]$row.original) -or [string]::IsNullOrWhiteSpace([string]$row.replacement)) {
            throw "Locked map row $member has an empty original or replacement value."
        }
        $id = 'SMBIOS:' + $field
        if (-not $seen.Add($id)) { throw "Duplicate combined binding id: $id" }
        $bindings.Add([pscustomobject][ordered]@{
            id = $id; category = 'SMBIOS'; member = $member; field = $field; device = [string]$row.device
            original = [string]$row.original; replacement = [string]$row.replacement
            readers = @('rsmb','wmi')
        })
    }

    $diskRows = @($rows | Where-Object { [string]$_.member -ceq 'FakeDiskSerial' })
    $diskTargets = @($diskRows | Where-Object { [string]$_.device -match '(?i)PHYSICALDRIVE0\s*$' })
    foreach ($row in @($diskRows | Where-Object { [string]$_.device -notmatch '(?i)PHYSICALDRIVE0\s*$' })) {
        $excludedDisks.Add([pscustomobject]@{ device = [string]$row.device; reason = 'Outside requested PHYSICALDRIVE0 scope; excluded by the approved disk scope.' })
    }
    if ($diskTargets.Count -ne 1) { throw "Expected exactly one locked PHYSICALDRIVE0 serial row; actual count=$($diskTargets.Count)." }
    $disk = $diskTargets[0]
    if ([string]::IsNullOrWhiteSpace([string]$disk.original) -or [string]::IsNullOrWhiteSpace([string]$disk.replacement)) { throw 'PHYSICALDRIVE0 map row has an empty original or replacement value.' }
    $diskId = 'DISK:PHYSICALDRIVE0'
    if (-not $seen.Add($diskId)) { throw "Duplicate combined binding id: $diskId" }
    $bindings.Add([pscustomobject][ordered]@{
        id = $diskId; category = 'DISK'; member = 'FakeDiskSerial'; field = 'serial'; device = [string]$disk.device
        original = [string]$disk.original; replacement = [string]$disk.replacement
        readers = @('storageIoctl','freshWmi')
    })

    $macRows = @($rows | Where-Object { [string]$_.member -ceq 'FakeMacAddress' })
    if ($macRows.Count -ne 4) { throw "Expected exactly four locked NIC MAC rows; actual count=$($macRows.Count)." }
    $seenPnp = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $macRows) {
        $device = [string]$row.device
        $separator = $device.IndexOf('|')
        if ($separator -lt 0 -or $separator -eq $device.Length - 1) { throw "Locked NIC row has no PnP identity: $device" }
        $pnp = $device.Substring($separator + 1)
        if (-not $seenPnp.Add($pnp)) { throw "Duplicate locked NIC PnP identity: $pnp" }
        if ([string]::IsNullOrWhiteSpace([string]$row.original) -or [string]::IsNullOrWhiteSpace([string]$row.replacement)) { throw "Locked NIC row has an empty original or replacement value: $device" }
        if ($null -eq (ConvertTo-CombinedComparableValue -Category 'NIC' -Field 'mac' -Value $row.original)) { throw "Locked NIC original is not a six-byte MAC address: $($row.original)" }
        if ($null -eq (ConvertTo-CombinedComparableValue -Category 'NIC' -Field 'mac' -Value $row.replacement)) { throw "Locked NIC replacement is not a six-byte MAC address: $($row.replacement)" }
        $id = 'NIC:' + $device
        if (-not $seen.Add($id)) { throw "Duplicate combined binding id: $id" }
        $bindings.Add([pscustomobject][ordered]@{
            id = $id; category = 'NIC'; member = 'FakeMacAddress'; field = 'mac'; device = $device; pnpDeviceId = $pnp
            original = [string]$row.original; replacement = [string]$row.replacement
            readers = @('ndisOid8023CurrentAddress','getAdaptersAddresses','getAdaptersInfoAddress','getIfTable2','win32NetworkAdapter','msftNetAdapterNetworkAddresses','getNetAdapter')
        })
    }
    if ($bindings.Count -ne 12) { throw "Combined binding count must be 12 (seven SMBIOS, one disk, four NIC); actual count=$($bindings.Count)." }
    return [pscustomobject][ordered]@{
        schema = 'combined-locked-map.v1'
        bindings = @($bindings.ToArray())
        excludedDiskTargets = @($excludedDisks.ToArray())
        scope = 'Seven SMBIOS fields, PHYSICALDRIVE0, and the four locked NICs. Other disks, including USB devices, are excluded.'
    }
}

function ConvertTo-CombinedComparableValue {
    param([Parameter(Mandatory=$true)][string]$Category,[Parameter(Mandatory=$true)][string]$Field,[AllowNull()][object]$Value)
    if ($null -eq $Value) { return $null }
    $text = [string]$Value
    if ([string]::IsNullOrWhiteSpace($text)) { return $null }
    if ($Category -ceq 'NIC') {
        $hex = $text -replace '[^0-9A-Fa-f]',''
        if ($hex -notmatch '\A[0-9A-Fa-f]{12}\z') { return $null }
        return $hex.ToUpperInvariant()
    }
    $text = $text.Trim()
    if ($Category -ceq 'SMBIOS' -and $Field -ceq 'system_uuid') {
        $guid = [Guid]::Empty
        if (-not [Guid]::TryParse($text, [ref]$guid)) { return $null }
        return $guid.ToString('D').ToUpperInvariant()
    }
    return $text
}

function Get-CombinedFieldObservation {
    param([object]$Report,[string]$Field)
    $fields = Get-CombinedValue $Report 'fields'
    return Get-CombinedValue $fields $Field
}

function Get-CombinedNicObservation {
    param([object]$Evaluation,[string]$Reader)
    $observations = Get-CombinedValue $Evaluation 'observations'
    $record = Get-CombinedValue $observations $Reader
    if ($null -eq $record) { return [pscustomobject]@{ actual = $null; applicable = $false; cardinality = 0 } }
    if ($Reader -ceq 'msftNetAdapterNetworkAddresses') {
        $items = @($record)
        if ($items.Count -ne 1) {
            return [pscustomobject]@{ actual = @($items | ForEach-Object { Get-CombinedValue $_ 'value' }); cardinality = $items.Count }
        }
        return [pscustomobject]@{ actual = Get-CombinedValue $items[0] 'value'; cardinality = 1 }
    }
    if ($Reader -ceq 'ndisOidDot11CurrentAddress') {
        if ((Get-CombinedValue $record 'applicable') -ne $true) { return [pscustomobject]@{ actual = $null; applicable = $false; cardinality = 0 } }
        return [pscustomobject]@{ actual = Get-CombinedValue $record 'actual'; applicable = $true; cardinality = 1 }
    }
    return [pscustomobject]@{ actual = Get-CombinedValue $record 'value'; cardinality = if ($null -eq $record) { 0 } else { 1 } }
}

function New-CombinedReadbackModel {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][object]$BindingMap,
        [Parameter(Mandatory=$true)][AllowEmptyCollection()][object[]]$Polls,
        [Parameter(Mandatory=$true)][ValidateSet('Original','Replacement')][string]$ExpectedMode,
        [Parameter(Mandatory=$true)][object]$FileIntegrityProbe,
        [Parameter(Mandatory=$true)][string]$LockedMapPath,
        [Parameter(Mandatory=$true)][string]$LockedMapSha256,
        [Parameter(Mandatory=$true)][string]$CaptureStartedUtc,
        [Parameter(Mandatory=$true)][string]$CaptureEndedUtc,
        [Parameter(Mandatory=$true)][AllowEmptyString()][string]$FinalBootTimeUtc,
        [AllowNull()][string]$FinalBootIdentityError,
        [object[]]$SourcePins = @()
    )

    $errors = [System.Collections.Generic.List[object]]::new()
    $rawBindings = @(Get-CombinedValue $BindingMap 'bindings')
    if ($rawBindings.Count -ne 12) { $errors.Add([pscustomobject]@{ reason='binding_count'; expected=12; actual=$rawBindings.Count; message='Expected 12 unique bindings.' }) }
    $bindingIds = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($binding in $rawBindings) {
        $id = [string](Get-CombinedValue $binding 'id')
        if ([string]::IsNullOrWhiteSpace($id) -or -not $bindingIds.Add($id)) { $errors.Add([pscustomobject]@{ reason='duplicate_binding'; expected='12 non-empty unique binding ids'; actual=$id; message='Combined bindings must have unique non-empty ids.' }) }
    }
    # Rebuild the derived fields instead of trusting caller-supplied categories
    # or reader lists. A malformed map must never produce an empty passing matrix.
    try {
        $rebuiltRows = @(foreach ($binding in $rawBindings) {
            [pscustomobject]@{ member=(Get-CombinedValue $binding 'member'); device=(Get-CombinedValue $binding 'device'); original=(Get-CombinedValue $binding 'original'); replacement=(Get-CombinedValue $binding 'replacement') }
        })
        $rebuiltMap = New-CombinedBindingMap -MapRows $rebuiltRows
        foreach ($binding in $rawBindings) {
            $id = [string](Get-CombinedValue $binding 'id')
            $canonical = @($rebuiltMap.bindings | Where-Object { $_.id -ceq $id })
            if ($canonical.Count -ne 1) { throw "Unknown binding id: $id" }
            foreach ($property in @('category','member','field','device','pnpDeviceId')) {
                if ([string](Get-CombinedValue $binding $property) -cne [string](Get-CombinedValue $canonical[0] $property)) { throw "Derived binding property differs: $id/$property" }
            }
            $readers = @(Get-CombinedValue $binding 'readers')
            if (($readers -join '|') -cne (@($canonical[0].readers) -join '|')) { throw "Required reader list differs: $id" }
        }
        if ([string](Get-CombinedValue $BindingMap 'schema') -cne 'combined-locked-map.v1') { throw 'Unsupported binding map schema.' }
    }
    catch { $errors.Add([pscustomobject]@{ reason='binding_map_shape'; expected='seven SMBIOS, one PHYSICALDRIVE0 and four NIC bindings with their complete reader lists'; actual=$_.Exception.Message; message='Combined binding map is malformed.' }) }
    if (@($Polls).Count -ne 2) { $errors.Add([pscustomobject]@{ reason='poll_count'; expected=2; actual=@($Polls).Count; message='Exactly two complete polls are required.' }) }
    $expectedProbeHash = [string](Get-CombinedValue $FileIntegrityProbe 'expectedSha256')
    $actualProbeHash = [string](Get-CombinedValue $FileIntegrityProbe 'actualSha256')
    if ((Get-CombinedValue $FileIntegrityProbe 'match') -ne $true -or
        (Get-CombinedValue $FileIntegrityProbe 'removedAfterRead') -ne $true -or
        (Get-CombinedValue $FileIntegrityProbe 'ownedFileCreated') -ne $true -or
        (Get-CombinedValue $FileIntegrityProbe 'lengthMatches') -ne $true -or
        (Get-CombinedValue $FileIntegrityProbe 'writeThrough') -ne $true -or
        (Get-CombinedValue $FileIntegrityProbe 'identifierSource') -ne $false -or
        (Get-CombinedValue $FileIntegrityProbe 'bytes') -ne 4096 -or
        -not [string]::IsNullOrWhiteSpace([string](Get-CombinedValue $FileIntegrityProbe 'error')) -or
        $expectedProbeHash -notmatch '\A[0-9A-Fa-f]{64}\z' -or
        $actualProbeHash -notmatch '\A[0-9A-Fa-f]{64}\z' -or
        $expectedProbeHash.ToUpperInvariant() -cne $actualProbeHash.ToUpperInvariant()) {
        $errors.Add([pscustomobject]@{ reason='file_integrity'; expected=(Get-CombinedValue $FileIntegrityProbe 'expectedSha256'); actual=(Get-CombinedValue $FileIntegrityProbe 'actualSha256'); message='Repo-local file write/read SHA-256 integrity probe failed.' })
    }

    $pollResults = [System.Collections.Generic.List[object]]::new()
    $bootTimes = [System.Collections.Generic.List[string]]::new()
    $pollIndex = 0
    foreach ($poll in @($Polls)) {
        $pollIndex++
        $pollErrors = [System.Collections.Generic.List[object]]::new()
        foreach ($acquisitionError in @(Get-CombinedValue $poll 'acquisitionErrors')) {
            $pollErrors.Add([pscustomobject]@{ reason='reader_acquisition'; expected='successful fresh reader capture'; actual=$acquisitionError; message="Reader acquisition failed in poll $pollIndex." })
        }
        $bootTimeStart = [string](Get-CombinedValue $poll 'windowsLastBootUpTimeUtcStart')
        $bootTimeEnd = [string](Get-CombinedValue $poll 'windowsLastBootUpTimeUtcEnd')
        foreach ($checkpoint in @($bootTimeStart,$bootTimeEnd)) {
            if ([string]::IsNullOrWhiteSpace($checkpoint)) {
                $pollErrors.Add([pscustomobject]@{ reason='missing_boot_time'; expected='boot time at start and end of each poll'; actual=$checkpoint; message="Poll $pollIndex is missing a Windows boot identity checkpoint." })
            } else { $bootTimes.Add($checkpoint) }
        }
        if (-not [string]::IsNullOrWhiteSpace($bootTimeStart) -and -not [string]::IsNullOrWhiteSpace($bootTimeEnd) -and $bootTimeStart -cne $bootTimeEnd) {
            $pollErrors.Add([pscustomobject]@{ reason='boot_identity_changed_during_poll'; expected=$bootTimeStart; actual=$bootTimeEnd; message="Windows boot identity changed during poll $pollIndex." })
        }

        $smbios = Get-CombinedValue $poll 'smbios'
        $storage = Get-CombinedValue $poll 'storage'
        $nic = Get-CombinedValue $poll 'nic'
        $smbiosSchema = [string](Get-CombinedValue $smbios 'schema')
        if ($smbiosSchema -cne 'smbios-windows-capture.v1') { $pollErrors.Add([pscustomobject]@{ reason='smbios_schema'; expected='smbios-windows-capture.v1'; actual=$smbiosSchema; message="SMBIOS report schema differs in poll $pollIndex." }) }
        $smbiosErrors = Get-CombinedValue $smbios 'errors'
        if ($null -ne $smbiosErrors) {
            foreach ($failure in @($smbiosErrors)) { $pollErrors.Add([pscustomobject]@{ reason='smbios_reader_failure'; expected='no SMBIOS reader errors'; actual=$failure; message="SMBIOS reader reported a failure in poll $pollIndex." }) }
        }
        $nicSchema = [string](Get-CombinedValue $nic 'schema')
        if ($nicSchema -cne 'nic-native-wmi-readback.v1') { $pollErrors.Add([pscustomobject]@{ reason='nic_schema'; expected='nic-native-wmi-readback.v1'; actual=$nicSchema; message="NIC report schema differs in poll $pollIndex." }) }
        $nicMode = [string](Get-CombinedValue $nic 'expectedMode')
        if ($nicMode -cne $ExpectedMode) {
            $pollErrors.Add([pscustomobject]@{ reason='nic_mode'; expected=$ExpectedMode; actual=$nicMode; message="NIC readback mode differs in poll $pollIndex." })
        }
        $nicFailures = @(Get-CombinedValue $nic 'failures')
        if ($nicFailures.Count -gt 0) {
            foreach ($failure in $nicFailures) { $pollErrors.Add([pscustomobject]@{ reason='nic_reader_failure'; expected='no NIC reader failures'; actual=$failure; message="NIC reader reported a failure in poll $pollIndex." }) }
        }
        if ((Get-CombinedValue $nic 'allLockedExpectedValuesMatchReadOnlySources') -ne $true) { $pollErrors.Add([pscustomobject]@{ reason='nic_source_summary'; expected=$true; actual=(Get-CombinedValue $nic 'allLockedExpectedValuesMatchReadOnlySources'); message="NIC helper did not confirm all locked reader comparisons in poll $pollIndex." }) }

        $nicEvaluations = @(Get-CombinedValue $nic 'lockedOriginalEvaluations')
        $bindingResults = [System.Collections.Generic.List[object]]::new()
        foreach ($binding in $rawBindings) {
            $expected = if ($ExpectedMode -ceq 'Replacement') { [string](Get-CombinedValue $binding 'replacement') } else { [string](Get-CombinedValue $binding 'original') }
            $readers = [System.Collections.Generic.List[object]]::new()
            if ((Get-CombinedValue $binding 'category') -ceq 'SMBIOS') {
                $fieldRecord = Get-CombinedFieldObservation $smbios ([string](Get-CombinedValue $binding 'field'))
                foreach ($reader in @('rsmb','wmi')) {
                    $property = if ($reader -ceq 'rsmb') { 'observedNative' } else { 'observedWmi' }
                    $actual = Get-CombinedValue $fieldRecord $property
                    $readers.Add((New-CombinedReaderResult $binding $reader $expected $actual $null))
                }
            }
            elseif ((Get-CombinedValue $binding 'category') -ceq 'DISK') {
                $readers.Add((New-CombinedReaderResult $binding 'storageIoctl' $expected (Get-CombinedValue $storage 'nativeSerial') $null))
                $readers.Add((New-CombinedReaderResult $binding 'freshWmi' $expected (Get-CombinedValue $storage 'wmiSerial') $null))
            }
            elseif ((Get-CombinedValue $binding 'category') -ceq 'NIC') {
                $pnp = [string](Get-CombinedValue $binding 'pnpDeviceId')
                $targetRows = @($nicEvaluations | Where-Object { [string]::Equals([string](Get-CombinedValue $_ 'pnpDeviceId'), $pnp, [StringComparison]::OrdinalIgnoreCase) })
                if ($targetRows.Count -ne 1) {
                    $pollErrors.Add([pscustomobject]@{ reason='nic_join_cardinality'; bindingId=(Get-CombinedValue $binding 'id'); expected=1; actual=$targetRows.Count; message="NIC PnP join must have exactly one row in poll $pollIndex." })
                }
                $target = if ($targetRows.Count -eq 1) { $targetRows[0] } else { $null }
                if ($null -ne $target -and [string](Get-CombinedValue $target 'expectedMode') -cne $ExpectedMode) {
                    $pollErrors.Add([pscustomobject]@{ reason='nic_target_mode'; bindingId=(Get-CombinedValue $binding 'id'); expected=$ExpectedMode; actual=(Get-CombinedValue $target 'expectedMode'); message="NIC target mode differs in poll $pollIndex." })
                }
                $nicReaderNames = @('ndisOid8023CurrentAddress','getAdaptersAddresses','getAdaptersInfoAddress','getIfTable2','win32NetworkAdapter','msftNetAdapterNetworkAddresses','getNetAdapter')
                foreach ($reader in $nicReaderNames) {
                    $record = Get-CombinedNicObservation $target $reader
                    $readers.Add((New-CombinedReaderResult $binding $reader $expected (Get-CombinedValue $record 'actual') (Get-CombinedValue $record 'cardinality')))
                }
                $dot11 = Get-CombinedNicObservation $target 'ndisOidDot11CurrentAddress'
                if ((Get-CombinedValue $dot11 'applicable') -eq $true) {
                    $readers.Add((New-CombinedReaderResult $binding 'ndisOidDot11CurrentAddress' $expected (Get-CombinedValue $dot11 'actual') 1))
                }
            }
            if ($readers.Count -eq 0) { $pollErrors.Add([pscustomobject]@{ reason='missing_readers'; bindingId=(Get-CombinedValue $binding 'id'); expected='nonempty native and WMI reader coverage'; actual=0; message="Binding has no reader observations in poll $pollIndex." }) }
            $failedReaders = @($readers | Where-Object { $_.match -ne $true })
            foreach ($failed in $failedReaders) {
                $pollErrors.Add([pscustomobject]@{
                    reason = if ($failed.cardinality -ne 1 -and $null -ne $failed.cardinality) { 'reader_cardinality' } else { 'value_mismatch' }
                    bindingId = [string](Get-CombinedValue $binding 'id')
                    reader = $failed.reader
                    expected = $failed.expected
                    actual = $failed.actual
                    message = "Expected '$($failed.expected)' but $($failed.reader) returned '$($failed.actual)' in poll $pollIndex."
                })
            }
            $bindingResults.Add([pscustomobject][ordered]@{
                id = [string](Get-CombinedValue $binding 'id')
                category = [string](Get-CombinedValue $binding 'category')
                member = [string](Get-CombinedValue $binding 'member')
                device = [string](Get-CombinedValue $binding 'device')
                expected = $expected
                readers = @($readers.ToArray())
                pass = ($readers.Count -gt 0 -and $failedReaders.Count -eq 0)
            })
        }
        $pollResults.Add([pscustomobject][ordered]@{
            index = $pollIndex
            windowsLastBootUpTimeUtcStart = $bootTimeStart
            windowsLastBootUpTimeUtcEnd = $bootTimeEnd
            evidence = Get-CombinedValue $poll 'evidence'
            bindings = @($bindingResults.ToArray())
            errors = @($pollErrors.ToArray())
            pass = ($pollErrors.Count -eq 0 -and @($bindingResults | Where-Object { $_.pass -ne $true }).Count -eq 0)
        })
        foreach ($errorRecord in $pollErrors) { $errors.Add($errorRecord) }
    }

    if ([string]::IsNullOrWhiteSpace($FinalBootTimeUtc)) {
        $errors.Add([pscustomobject]@{ reason='missing_final_boot_time'; expected='final Windows boot identity after poll 2'; actual=$FinalBootIdentityError; message='Final Windows boot identity could not be read.' })
    } else { $bootTimes.Add($FinalBootTimeUtc) }
    $sameBoot = $bootTimes.Count -eq 5
    if ($sameBoot) { foreach ($checkpoint in $bootTimes) { if ($checkpoint -cne $bootTimes[0]) { $sameBoot = $false; break } } }
    if (-not $sameBoot) { $errors.Add([pscustomobject]@{ reason='boot_identity_drift'; expected=if ($bootTimes.Count -gt 0) { $bootTimes[0] } else { $null }; actual=@($bootTimes.ToArray()); message='Both polls, their start/end checkpoints, and the final checkpoint must share one Windows boot identity.' }) }
    foreach ($pin in @($SourcePins)) {
        $path = [string](Get-CombinedValue $pin 'path')
        $before = [string](Get-CombinedValue $pin 'beforePollSha256')
        $between = [string](Get-CombinedValue $pin 'betweenPollsSha256')
        $after = [string](Get-CombinedValue $pin 'afterPollSha256')
        $pinMatches = $before -match '\A[A-Fa-f0-9]{64}\z' -and $between -match '\A[A-Fa-f0-9]{64}\z' -and $after -match '\A[A-Fa-f0-9]{64}\z' -and $before -ceq $between -and $before -ceq $after
        if (-not $pinMatches) {
            $errors.Add([pscustomobject]@{ reason='source_pin_drift'; expected=$before; actual=[pscustomobject]@{ betweenPolls=$between; afterPoll=$after }; path=$path; message='Readback source changed or could not be hash-pinned across the two polls.' })
        }
    }
    $stable = $false
    if ($pollResults.Count -eq 2) {
        $stable = $true
        foreach ($binding in $rawBindings) {
            $first = @($pollResults[0].bindings | Where-Object { $_.id -ceq [string](Get-CombinedValue $binding 'id') })
            $second = @($pollResults[1].bindings | Where-Object { $_.id -ceq [string](Get-CombinedValue $binding 'id') })
            if ($first.Count -ne 1 -or $second.Count -ne 1) { $stable = $false; continue }
            $firstReaders = @($first[0].readers)
            $secondReaders = @($second[0].readers)
            foreach ($one in $firstReaders) {
                $two = @($secondReaders | Where-Object { $_.reader -ceq $one.reader })
                if ($two.Count -ne 1 -or $one.normalizedActual -cne $two[0].normalizedActual) {
                    $stable = $false
                    $errors.Add([pscustomobject]@{ reason='poll_value_drift'; bindingId=$first[0].id; reader=$one.reader; expected=$one.normalizedActual; actual=if ($two.Count -eq 1) { $two[0].normalizedActual } else { $null }; message='Readback changed between the two polls.' })
                }
            }
        }
    }
    if (-not $stable) { $errors.Add([pscustomobject]@{ reason='unstable_polls'; expected=2; actual=$pollResults.Count; message='Two complete stable polls are required.' }) }

    $allPollsPass = $pollResults.Count -eq 2 -and @($pollResults | Where-Object { $_.pass -ne $true }).Count -eq 0
    $pass = $errors.Count -eq 0 -and $sameBoot -and $stable -and $allPollsPass -and (Get-CombinedValue $FileIntegrityProbe 'match') -eq $true
    return [pscustomobject][ordered]@{
        schema = 'combined-host-readback.v1'
        status = if ($pass) { 'PASS' } else { 'FAIL' }
        readOnly = $true
        deviceConfigurationChanged = $false
        expectedMode = $ExpectedMode
        captureStartedUtc = $CaptureStartedUtc
        captureEndedUtc = $CaptureEndedUtc
        lockedMap = [pscustomobject]@{ path=$LockedMapPath; sha256=$LockedMapSha256; bindingCount=$rawBindings.Count }
        scope = [string](Get-CombinedValue $BindingMap 'scope')
        excludedDiskTargets = @(Get-CombinedValue $BindingMap 'excludedDiskTargets')
        bootIdentity = [pscustomobject]@{ windowsLastBootUpTimeUtc = if ($bootTimes.Count -gt 0) { $bootTimes[0] } else { $null }; checkpoints = @($bootTimes.ToArray()); finalError = $FinalBootIdentityError; sameBoot = $sameBoot }
        sourcePins = @($SourcePins)
        fileIntegrityProbe = $FileIntegrityProbe
        polls = @($pollResults.ToArray())
        allBindingsMatchedInEachPoll = $allPollsPass
        stableAcrossPolls = $stable
        simultaneousReadbackPass = ($pass -and $sameBoot -and $allPollsPass)
        failures = @($errors.ToArray())
    }
}

function New-CombinedReaderResult {
    param([object]$Binding,[string]$Reader,[string]$Expected,[AllowNull()][object]$Actual,[AllowNull()][object]$Cardinality)
    $category = [string](Get-CombinedValue $Binding 'category')
    $field = [string](Get-CombinedValue $Binding 'field')
    $normalizedExpected = ConvertTo-CombinedComparableValue $category $field $Expected
    $normalizedActual = ConvertTo-CombinedComparableValue $category $field $Actual
    $match = $null -ne $normalizedExpected -and $null -ne $normalizedActual -and $normalizedExpected -ceq $normalizedActual
    if ($null -ne $Cardinality -and [int]$Cardinality -ne 1) { $match = $false }
    return [pscustomobject][ordered]@{
        reader = $Reader
        expected = $Expected
        actual = $Actual
        normalizedExpected = $normalizedExpected
        normalizedActual = $normalizedActual
        cardinality = $Cardinality
        match = [bool]$match
    }
}

Export-ModuleMember -Function Get-CombinedValue,Get-CombinedMapRows,New-CombinedBindingMap,ConvertTo-CombinedComparableValue,New-CombinedReadbackModel
