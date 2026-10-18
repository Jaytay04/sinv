function Get-NicAddressPropertyValue {
    param([AllowNull()][object]$InputObject, [Parameter(Mandatory = $true)][string]$Name)

    if ($null -eq $InputObject) { return $null }
    if ($InputObject -is [Collections.IDictionary]) {
        foreach ($key in $InputObject.Keys) {
            if ([string]::Equals([string]$key, $Name, [StringComparison]::OrdinalIgnoreCase)) {
                return $InputObject[$key]
            }
        }
        return $null
    }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function ConvertTo-NicAddressGuid {
    param([AllowNull()][object]$Value)

    if ($null -eq $Value) { return $null }
    try { return ([Guid]::Parse([string]$Value)).ToString('D').ToUpperInvariant() }
    catch { return $null }
}

function ConvertTo-NicAddressMacKey {
    param([AllowNull()][object]$Value)

    if ($null -eq $Value) { return $null }
    $text = ([string]$Value) -replace '[^0-9A-Fa-f]', ''
    if ($text -notmatch '\A[0-9A-Fa-f]{12}\z') { return $null }
    return $text.ToUpperInvariant()
}

function ConvertTo-NicAddressCanonicalMac {
    param([Parameter(Mandatory = $true)][object]$Value, [Parameter(Mandatory = $true)][string]$Label)

    $key = ConvertTo-NicAddressMacKey $Value
    if ($null -eq $key) { throw ($Label + ' is not a six-byte MAC address: ' + [string]$Value) }
    return (($key -split '(..)' | Where-Object { $_ }) -join ':')
}

function Test-NicAddressUnicastMac {
    param([AllowNull()][object]$Value)

    $key = ConvertTo-NicAddressMacKey $Value
    if ($null -eq $key) { return $false }
    $bytes = @()
    for ($index = 0; $index -lt 12; $index += 2) {
        $bytes += [Convert]::ToByte($key.Substring($index, 2), 16)
    }
    if (($bytes[0] -band 1) -ne 0) { return $false }
    if (@($bytes | Where-Object { $_ -ne 0 }).Count -eq 0) { return $false }
    if (@($bytes | Where-Object { $_ -ne 255 }).Count -eq 0) { return $false }
    return $true
}

function Assert-NicAddressProfileBindings {
    param([Parameter(Mandatory = $true)][object[]]$ProfileBindings)

    if ($ProfileBindings.Count -ne 4) {
        throw ('Expected exactly four profile adapter bindings; actual=' + $ProfileBindings.Count)
    }

    $seenPnp = @{}
    $seenGuid = @{}
    $seenReplacement = @{}
    $validated = [Collections.Generic.List[object]]::new()
    foreach ($binding in $ProfileBindings) {
        $pnp = [string](Get-NicAddressPropertyValue $binding 'pnpDeviceId')
        $guid = ConvertTo-NicAddressGuid (Get-NicAddressPropertyValue $binding 'interfaceGuid')
        $original = ConvertTo-NicAddressCanonicalMac (Get-NicAddressPropertyValue $binding 'original') 'Profile original MAC'
        $replacement = ConvertTo-NicAddressCanonicalMac (Get-NicAddressPropertyValue $binding 'replacement') 'Profile replacement MAC'
        if ([string]::IsNullOrWhiteSpace($pnp) -or $pnp.Length -gt 260 -or $pnp -notmatch '\A[\x20-\x7e]+\z' -or $pnp.Contains('"') -or $pnp.Contains('%')) {
            throw ('Profile PnP identity is not a bounded safe string: ' + $pnp)
        }
        if ($null -eq $guid) { throw ('Profile interface GUID is missing or invalid: ' + $pnp) }
        if (-not (Test-NicAddressUnicastMac $original)) { throw ('Profile original MAC is not a valid unicast address: ' + $pnp + ' actual=' + $original) }
        if (-not (Test-NicAddressUnicastMac $replacement)) { throw ('Profile replacement MAC is not a valid unicast address: ' + $pnp + ' actual=' + $replacement) }
        if ((ConvertTo-NicAddressMacKey $original) -ceq (ConvertTo-NicAddressMacKey $replacement)) {
            throw ('Profile original and replacement MAC are identical: ' + $pnp + ' value=' + $original)
        }

        $pnpKey = $pnp.ToUpperInvariant()
        $replacementKey = ConvertTo-NicAddressMacKey $replacement
        if ($seenPnp.ContainsKey($pnpKey)) { throw ('Duplicate profile PnP identity: ' + $pnp) }
        if ($seenGuid.ContainsKey($guid)) { throw ('Duplicate profile interface GUID: ' + $guid) }
        if ($seenReplacement.ContainsKey($replacementKey)) { throw ('Duplicate profile replacement MAC: ' + $replacement) }
        $seenPnp[$pnpKey] = $true
        $seenGuid[$guid] = $true
        $seenReplacement[$replacementKey] = $true
        $validated.Add([ordered]@{
            pnpDeviceId = $pnp
            interfaceGuid = $guid
            original = $original
            replacement = $replacement
        })
    }
    return $validated.ToArray()
}

function ConvertTo-NicAddressHex {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    return ([BitConverter]::ToString($Bytes)).Replace('-', '')
}

function ConvertFrom-NicAddressHex {
    param([Parameter(Mandatory = $true)][string]$Hex, [Parameter(Mandatory = $true)][int]$ByteCount)

    if ($Hex -notmatch ('\A[0-9A-Fa-f]{' + ($ByteCount * 2) + '}\z')) {
        throw ('Registry value hex has an invalid length or character: expectedBytes=' + $ByteCount + ' actual=' + $Hex)
    }
    $bytes = New-Object byte[] $ByteCount
    for ($index = 0; $index -lt $ByteCount; $index++) {
        $bytes[$index] = [Convert]::ToByte($Hex.Substring($index * 2, 2), 16)
    }
    return ,$bytes
}

function New-NicAddressRegistryValueSnapshot {
    param(
        [Parameter(Mandatory = $true)][bool]$Present,
        [AllowNull()][string]$Kind,
        [AllowNull()][object]$Value
    )

    if (-not $Present) {
        if (-not [string]::IsNullOrEmpty($Kind) -or $null -ne $Value) { throw 'Absent NetworkAddress registry values must not carry a kind or raw value.' }
        return [ordered]@{ present = $false; kind = $null; encoding = 'none'; rawValue = $null }
    }
    if ([string]::IsNullOrWhiteSpace($Kind)) { throw 'Present NetworkAddress registry values require a registry kind.' }

    switch -CaseSensitive ($Kind) {
        'String' { $encoding = 'string'; $rawValue = [string]$Value }
        'ExpandString' { $encoding = 'expand-string'; $rawValue = [string]$Value }
        'MultiString' { $encoding = 'multi-string'; $rawValue = [string[]]@($Value | ForEach-Object { [string]$_ }) }
        'DWord' {
            $encoding = 'dword-le-hex'
            $bytes = [BitConverter]::GetBytes([int]$Value)
            $rawValue = ConvertTo-NicAddressHex $bytes
        }
        'QWord' {
            $encoding = 'qword-le-hex'
            $bytes = [BitConverter]::GetBytes([long]$Value)
            $rawValue = ConvertTo-NicAddressHex $bytes
        }
        'Binary' {
            $encoding = 'base64'
            $rawValue = [Convert]::ToBase64String([byte[]]$Value)
        }
        'None' {
            $encoding = 'base64'
            $rawValue = [Convert]::ToBase64String([byte[]]$Value)
        }
        default { throw ('Unsupported NetworkAddress registry kind; refusing a lossy snapshot: ' + $Kind) }
    }
    return [ordered]@{ present = $true; kind = $Kind; encoding = $encoding; rawValue = $rawValue }
}

function ConvertFrom-NicAddressRegistryValueSnapshot {
    param([Parameter(Mandatory = $true)][object]$ValueSnapshot)

    $present = [bool](Get-NicAddressPropertyValue $ValueSnapshot 'present')
    if (-not $present) { return $null }
    $kind = [string](Get-NicAddressPropertyValue $ValueSnapshot 'kind')
    $encoding = [string](Get-NicAddressPropertyValue $ValueSnapshot 'encoding')
    $rawValue = Get-NicAddressPropertyValue $ValueSnapshot 'rawValue'
    switch -CaseSensitive ($encoding) {
        'string' { if ($kind -cne 'String') { throw ('Registry snapshot encoding/kind mismatch: ' + $encoding + '/' + $kind) }; return [string]$rawValue }
        'expand-string' { if ($kind -cne 'ExpandString') { throw ('Registry snapshot encoding/kind mismatch: ' + $encoding + '/' + $kind) }; return [string]$rawValue }
        'multi-string' { if ($kind -cne 'MultiString') { throw ('Registry snapshot encoding/kind mismatch: ' + $encoding + '/' + $kind) }; return ,([string[]]@($rawValue | ForEach-Object { [string]$_ })) }
        'dword-le-hex' { if ($kind -cne 'DWord') { throw ('Registry snapshot encoding/kind mismatch: ' + $encoding + '/' + $kind) }; return [BitConverter]::ToInt32((ConvertFrom-NicAddressHex ([string]$rawValue) 4), 0) }
        'qword-le-hex' { if ($kind -cne 'QWord') { throw ('Registry snapshot encoding/kind mismatch: ' + $encoding + '/' + $kind) }; return [BitConverter]::ToInt64((ConvertFrom-NicAddressHex ([string]$rawValue) 8), 0) }
        'base64' {
            if ($kind -cne 'Binary' -and $kind -cne 'None') { throw ('Registry snapshot encoding/kind mismatch: ' + $encoding + '/' + $kind) }
            return ,([Convert]::FromBase64String([string]$rawValue))
        }
        default { throw ('Unsupported NetworkAddress registry snapshot encoding: ' + $encoding) }
    }
}

function Test-NicAddressRegistryValueSnapshotEqual {
    param([Parameter(Mandatory = $true)][object]$Expected, [Parameter(Mandatory = $true)][object]$Actual)

    $expectedPresent = [bool](Get-NicAddressPropertyValue $Expected 'present')
    $actualPresent = [bool](Get-NicAddressPropertyValue $Actual 'present')
    if ($expectedPresent -ne $actualPresent) { return $false }
    if (-not $expectedPresent) { return $true }
    foreach ($name in @('kind', 'encoding')) {
        if ([string](Get-NicAddressPropertyValue $Expected $name) -cne [string](Get-NicAddressPropertyValue $Actual $name)) { return $false }
    }
    $expectedRaw = Get-NicAddressPropertyValue $Expected 'rawValue'
    $actualRaw = Get-NicAddressPropertyValue $Actual 'rawValue'
    $expectedJson = ConvertTo-Json -InputObject $expectedRaw -Compress -Depth 8
    $actualJson = ConvertTo-Json -InputObject $actualRaw -Compress -Depth 8
    return [string]::Equals($expectedJson, $actualJson, [StringComparison]::Ordinal)
}

function New-NicAddressRestorePlan {
    param([Parameter(Mandatory = $true)][object]$Snapshot)

    $registry = Get-NicAddressPropertyValue $Snapshot 'networkAddress'
    if ($null -eq $registry) { throw 'NIC adapter snapshot is missing its NetworkAddress registry baseline.' }
    $present = [bool](Get-NicAddressPropertyValue $registry 'present')
    if (-not $present) {
        return [ordered]@{ action = 'delete'; valueName = 'NetworkAddress'; kind = $null; encoding = 'none'; rawValue = $null }
    }
    $kind = [string](Get-NicAddressPropertyValue $registry 'kind')
    $encoding = [string](Get-NicAddressPropertyValue $registry 'encoding')
    $rawValue = Get-NicAddressPropertyValue $registry 'rawValue'
    if ($null -eq (ConvertFrom-NicAddressRegistryValueSnapshot $registry) -and $encoding -notin @('string', 'expand-string')) {
        throw 'Present NetworkAddress registry snapshot decoded to null.'
    }
    return [ordered]@{ action = 'set'; valueName = 'NetworkAddress'; kind = $kind; encoding = $encoding; rawValue = $rawValue }
}

function New-NicAddressApplyPlan {
    param([Parameter(Mandatory = $true)][object]$Snapshot)

    $original = ConvertTo-NicAddressCanonicalMac (Get-NicAddressPropertyValue $Snapshot 'original') 'Snapshot original MAC'
    $replacement = ConvertTo-NicAddressCanonicalMac (Get-NicAddressPropertyValue $Snapshot 'replacement') 'Snapshot replacement MAC'
    if ((ConvertTo-NicAddressMacKey $original) -ceq (ConvertTo-NicAddressMacKey $replacement)) {
        throw ('Apply plan original and replacement are identical: ' + $original)
    }
    if (-not (Test-NicAddressUnicastMac $replacement)) { throw ('Apply plan replacement MAC is not unicast: ' + $replacement) }
    return [ordered]@{
        action = 'set-net-adapter-mac'
        pnpDeviceId = [string](Get-NicAddressPropertyValue $Snapshot 'pnpDeviceId')
        interfaceGuid = [string](Get-NicAddressPropertyValue $Snapshot 'interfaceGuid')
        expectedOriginal = $original
        replacement = $replacement
        networkAddressBefore = Get-NicAddressPropertyValue $Snapshot 'networkAddress'
    }
}

function Get-NicAddressVmIdentity {
    $computer = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
    $product = Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop
    return [ordered]@{
        manufacturer = [string]$computer.Manufacturer
        model = [string]$computer.Model
        uuid = [string]$product.UUID
    }
}

function Assert-NicAddressVmIdentity {
    param([Parameter(Mandatory = $true)][string]$ExpectedVmUuid)

    $expected = ConvertTo-NicAddressGuid $ExpectedVmUuid
    if ($null -eq $expected) { throw ('Expected VM UUID is missing or invalid: ' + $ExpectedVmUuid) }
    $identity = Get-NicAddressVmIdentity
    if ([string]$identity.manufacturer -notmatch '^(?i:VMware(?:,\s*Inc\.)?)$') {
        throw ('NIC address mutation requires a VMware guest; actual manufacturer=' + [string]$identity.manufacturer)
    }
    $actualUuid = ConvertTo-NicAddressGuid $identity.uuid
    if ($actualUuid -cne $expected) {
        throw ('VM UUID mismatch: expected=' + $expected + ' actual=' + [string]$identity.uuid)
    }
    $identity.uuid = $actualUuid
    return $identity
}

function Get-NicAddressRegistryClassEntry {
    param([Parameter(Mandatory = $true)][string]$InterfaceGuid, [Parameter(Mandatory = $true)][string]$PnpDeviceId)

    $classRelative = 'SYSTEM\CurrentControlSet\Control\Class\{4d36e972-e325-11ce-bfc1-08002be10318}'
    $base = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($classRelative, $false)
    if ($null -eq $base) { throw ('Network adapter class registry key is absent: HKLM\' + $classRelative) }
    $classEntries = [Collections.Generic.List[object]]::new()
    try {
        foreach ($childName in $base.GetSubKeyNames()) {
            if ($childName -notmatch '\A[0-9]{4}\z') { continue }
            $child = $base.OpenSubKey($childName, $false)
            if ($null -eq $child) { continue }
            try {
                $netCfg = [string]$child.GetValue('NetCfgInstanceId', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
                if ((ConvertTo-NicAddressGuid $netCfg) -cne $InterfaceGuid) { continue }
                $pnpValueName = @('PnpInstanceID', 'PnPInstanceID', 'PnpDeviceId', 'PnPDeviceID') | Where-Object { $child.GetValueNames() -contains $_ } | Select-Object -First 1
                $registeredPnp = if ($pnpValueName) { [string]$child.GetValue([string]$pnpValueName, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) } else { $null }
                if ($registeredPnp -and -not [string]::Equals($registeredPnp, $PnpDeviceId, [StringComparison]::OrdinalIgnoreCase)) {
                    throw ('Network class PnP identity mismatch: expected=' + $PnpDeviceId + ' actual=' + $registeredPnp + ' subkey=' + $childName)
                }
                $names = @($child.GetValueNames())
                $present = @($names | Where-Object { [string]::Equals([string]$_, 'NetworkAddress', [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0
                $networkValue = $null
                $networkKind = $null
                if ($present) {
                    $networkKind = $child.GetValueKind('NetworkAddress').ToString()
                    $networkValue = $child.GetValue('NetworkAddress', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
                }
                $classEntries.Add([ordered]@{
                    registrySubKey = ($classRelative + '\' + $childName)
                    registryKeyName = $childName
                    registryPnpValueName = [string]$pnpValueName
                    registryPnpDeviceId = $registeredPnp
                    networkAddress = New-NicAddressRegistryValueSnapshot $present $networkKind $networkValue
                })
            }
            finally { $child.Close() }
        }
    }
    finally { $base.Close() }

    if ($classEntries.Count -ne 1) {
        throw ('Expected exactly one network class key for NetCfgInstanceId=' + $InterfaceGuid + ' and PnP=' + $PnpDeviceId + '; actualMatches=' + $classEntries.Count)
    }
    return $classEntries[0]
}

function Get-NicAddressAdvancedPropertyCapability {
    param([Parameter(Mandatory = $true)][object]$Adapter)

    $setCommand = Get-Command Set-NetAdapter -ErrorAction SilentlyContinue
    $restartCommand = Get-Command Restart-NetAdapter -ErrorAction SilentlyContinue
    $enableCommand = Get-Command Enable-NetAdapter -ErrorAction SilentlyContinue
    $disableCommand = Get-Command Disable-NetAdapter -ErrorAction SilentlyContinue
    $hasInput = $null -ne $setCommand -and $setCommand.Parameters.ContainsKey('InputObject')
    $hasMacAddress = $null -ne $setCommand -and $setCommand.Parameters.ContainsKey('MacAddress')
    $restartInput = $null -ne $restartCommand -and $restartCommand.Parameters.ContainsKey('InputObject')
    $enableInput = $null -ne $enableCommand -and $enableCommand.Parameters.ContainsKey('InputObject')
    $disableInput = $null -ne $disableCommand -and $disableCommand.Parameters.ContainsKey('InputObject')
    $errorText = $null
    $matches = @()
    try {
        $properties = @(Get-NetAdapterAdvancedProperty -Name ([string]$Adapter.Name) -AllProperties -ErrorAction Stop)
        $matches = @($properties | Where-Object { [string]$_.RegistryKeyword -ceq 'NetworkAddress' })
    }
    catch { $errorText = $_.Exception.ToString() }
    return [ordered]@{
        setNetAdapterCommandFound = ($null -ne $setCommand)
        inputObjectParameter = [bool]$hasInput
        macAddressParameter = [bool]$hasMacAddress
        restartInputObjectParameter = [bool]$restartInput
        enableInputObjectParameter = [bool]$enableInput
        disableInputObjectParameter = [bool]$disableInput
        advancedPropertyQuerySucceeded = ($null -eq $errorText)
        networkAddressPropertyCount = $matches.Count
        networkAddressProperties = @($matches | ForEach-Object {
            [ordered]@{
                name = [string]$_.Name
                interfaceDescription = [string]$_.InterfaceDescription
                registryKeyword = [string]$_.RegistryKeyword
                registryValue = [string]$_.RegistryValue
                registryDataType = [string]$_.RegistryDataType
                displayName = [string]$_.DisplayName
                displayValue = [string]$_.DisplayValue
            }
        })
        error = $errorText
        supported = ([bool]$hasInput -and [bool]$hasMacAddress -and [bool]$restartInput -and [bool]$enableInput -and [bool]$disableInput -and $matches.Count -le 1)
    }
}

function Test-NicAddressDisconnected {
    param([AllowNull()][object]$MediaConnectState, [AllowNull()][object]$AdapterStatus)

    $mediaText = [string]$MediaConnectState
    $statusText = [string]$AdapterStatus
    $mediaDisconnected = ($mediaText -match '^(?i:Disconnected)$' -or $mediaText -ceq '2')
    $statusDisconnected = $statusText -match '^(?i:Disconnected|Disabled)$'
    $mediaConnected = ($mediaText -match '^(?i:Connected)$' -or $mediaText -ceq '1')
    $statusConnected = $statusText -match '^(?i:Up|Connected)$'
    if (($mediaDisconnected -and $statusConnected) -or ($statusDisconnected -and $mediaConnected)) { return $false }
    return ($mediaDisconnected -or $statusDisconnected)
}

function Get-NicAddressAdminState {
    param([AllowNull()][object]$Adapter, [AllowNull()][object]$MsftAdapter, [Parameter(Mandatory = $true)][string]$PnpDeviceId)

    $admin = Get-NicAddressPropertyValue $Adapter 'AdminStatus'
    $source = 'Get-NetAdapter.AdminStatus'
    if ($null -eq $admin) {
        $admin = Get-NicAddressPropertyValue $MsftAdapter 'AdminStatus'
        $source = 'MSFT_NetAdapter.AdminStatus'
    }
    $text = [string]$admin
    if ($text -match '^(?i:Up|Enabled|1|True)$') { $enabled = $true }
    elseif ($text -match '^(?i:Down|Disabled|2|False)$') { $enabled = $false }
    else { throw ('Cannot establish adapter administrative state: pnp=' + $PnpDeviceId + ' source=' + $source + ' actual=' + $text) }
    return [ordered]@{ value = $text; enabled = $enabled; source = $source }
}

function Get-NicAddressSnapshot {
    param([Parameter(Mandatory = $true)][object[]]$ProfileBindings, [Parameter(Mandatory = $true)][string]$ExpectedVmUuid)

    $identity = Assert-NicAddressVmIdentity $ExpectedVmUuid
    $bindings = @(Assert-NicAddressProfileBindings $ProfileBindings)
    $netAdapters = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop)
    $msftAdapters = @(Get-CimInstance -Namespace 'root\StandardCimv2' -ClassName MSFT_NetAdapter -ErrorAction Stop)
    $snapshots = [Collections.Generic.List[object]]::new()

    foreach ($binding in $bindings) {
        $pnp = [string]$binding.pnpDeviceId
        $guid = [string]$binding.interfaceGuid
        $msftMatches = @($msftAdapters | Where-Object {
            [string]::Equals([string](Get-NicAddressPropertyValue $_ 'PNPDeviceID'), $pnp, [StringComparison]::OrdinalIgnoreCase) -and
            (ConvertTo-NicAddressGuid (Get-NicAddressPropertyValue $_ 'InterfaceGuid')) -ceq $guid
        })
        if ($msftMatches.Count -ne 1) {
            throw ('Expected exactly one MSFT_NetAdapter match by PnP+InterfaceGuid; pnp=' + $pnp + ' guid=' + $guid + ' actual=' + $msftMatches.Count)
        }
        $plainMatches = @($netAdapters | Where-Object { (ConvertTo-NicAddressGuid (Get-NicAddressPropertyValue $_ 'InterfaceGuid')) -ceq $guid })
        if ($plainMatches.Count -ne 1) {
            throw ('Expected exactly one plain Get-NetAdapter match by InterfaceGuid; pnp=' + $pnp + ' guid=' + $guid + ' actual=' + $plainMatches.Count)
        }
        $msft = $msftMatches[0]
        $adapter = $plainMatches[0]
        $status = [string](Get-NicAddressPropertyValue $adapter 'Status')
        $mediaConnectState = Get-NicAddressPropertyValue $msft 'MediaConnectState'
        if (-not (Test-NicAddressDisconnected $mediaConnectState $status)) {
            throw ('NIC mutation target is not disconnected/disabled: pnp=' + $pnp + ' MediaConnectState=' + [string]$mediaConnectState + ' Status=' + $status)
        }
        $actualMac = ConvertTo-NicAddressCanonicalMac (Get-NicAddressPropertyValue $adapter 'MacAddress') ('Get-NetAdapter MAC for ' + $pnp)
        if ((ConvertTo-NicAddressMacKey $actualMac) -cne (ConvertTo-NicAddressMacKey $binding.original)) {
            throw ('Fresh adapter original mismatch: pnp=' + $pnp + ' expected=' + $binding.original + ' actual=' + $actualMac)
        }
        $adminState = Get-NicAddressAdminState $adapter $msft $pnp
        $registry = Get-NicAddressRegistryClassEntry $guid $pnp
        $capability = Get-NicAddressAdvancedPropertyCapability $adapter
        $snapshots.Add([ordered]@{
            pnpDeviceId = $pnp
            interfaceGuid = $guid
            original = [string]$binding.original
            replacement = [string]$binding.replacement
            observedOriginalMac = $actualMac
            adapterName = [string](Get-NicAddressPropertyValue $adapter 'Name')
            interfaceDescription = [string](Get-NicAddressPropertyValue $adapter 'InterfaceDescription')
            adapterStatus = $status
            mediaConnectState = [string]$mediaConnectState
            adminState = $adminState
            registrySubKey = [string]$registry.registrySubKey
            registryKeyName = [string]$registry.registryKeyName
            registryPnpValueName = [string]$registry.registryPnpValueName
            registryPnpDeviceId = [string]$registry.registryPnpDeviceId
            networkAddress = $registry.networkAddress
            setCapability = $capability
        })
    }

    return [ordered]@{ vmIdentity = $identity; adapters = $snapshots.ToArray() }
}

function New-NicAddressIntent {
    param([Parameter(Mandatory = $true)][string]$Action, [Parameter(Mandatory = $true)][object]$Snapshot, [AllowNull()][string]$TargetMac)

    return [ordered]@{
        schema = 'nic-adapter-address-intent.v1'
        action = $Action
        createdUtc = [DateTime]::UtcNow.ToString('o')
        pnpDeviceId = [string](Get-NicAddressPropertyValue $Snapshot 'pnpDeviceId')
        interfaceGuid = [string](Get-NicAddressPropertyValue $Snapshot 'interfaceGuid')
        originalMac = [string](Get-NicAddressPropertyValue $Snapshot 'original')
        expectedMac = [string](Get-NicAddressPropertyValue $Snapshot 'replacement')
        targetMac = $TargetMac
        registrySubKey = [string](Get-NicAddressPropertyValue $Snapshot 'registrySubKey')
        networkAddressBefore = Get-NicAddressPropertyValue $Snapshot 'networkAddress'
        adminStateBefore = Get-NicAddressPropertyValue $Snapshot 'adminState'
    }
}

function Get-NicAddressLiveAdapter {
    param([Parameter(Mandatory = $true)][object]$Snapshot, [Parameter(Mandatory = $true)][string]$ExpectedVmUuid)

    [void](Assert-NicAddressVmIdentity $ExpectedVmUuid)
    $pnp = [string](Get-NicAddressPropertyValue $Snapshot 'pnpDeviceId')
    $guid = ConvertTo-NicAddressGuid (Get-NicAddressPropertyValue $Snapshot 'interfaceGuid')
    if ($null -eq $guid -or [string]::IsNullOrWhiteSpace($pnp)) { throw 'NIC adapter snapshot has an invalid PnP identity or InterfaceGuid.' }
    $msft = @(Get-CimInstance -Namespace 'root\StandardCimv2' -ClassName MSFT_NetAdapter -ErrorAction Stop | Where-Object {
        [string]::Equals([string](Get-NicAddressPropertyValue $_ 'PNPDeviceID'), $pnp, [StringComparison]::OrdinalIgnoreCase) -and
        (ConvertTo-NicAddressGuid (Get-NicAddressPropertyValue $_ 'InterfaceGuid')) -ceq $guid
    })
    $plain = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop | Where-Object { (ConvertTo-NicAddressGuid (Get-NicAddressPropertyValue $_ 'InterfaceGuid')) -ceq $guid })
    if ($msft.Count -ne 1 -or $plain.Count -ne 1) {
        throw ('NIC identity changed or is ambiguous: pnp=' + $pnp + ' guid=' + $guid + ' MSFTmatches=' + $msft.Count + ' plainMatches=' + $plain.Count)
    }
    $status = [string](Get-NicAddressPropertyValue $plain[0] 'Status')
    $media = Get-NicAddressPropertyValue $msft[0] 'MediaConnectState'
    if (-not (Test-NicAddressDisconnected $media $status)) {
        throw ('NIC mutation target is no longer disconnected/disabled: pnp=' + $pnp + ' MediaConnectState=' + [string]$media + ' Status=' + $status)
    }
    $registry = Get-NicAddressRegistryClassEntry $guid $pnp
    if ([string]$registry.registrySubKey -cne [string](Get-NicAddressPropertyValue $Snapshot 'registrySubKey')) {
        throw ('Network class key changed for NIC target: pnp=' + $pnp + ' expected=' + [string](Get-NicAddressPropertyValue $Snapshot 'registrySubKey') + ' actual=' + [string]$registry.registrySubKey)
    }
    return [ordered]@{ adapter = $plain[0]; msftAdapter = $msft[0]; status = $status; mediaConnectState = [string]$media; registry = $registry }
}

function Get-NicAddressRegistryValueSnapshot {
    param([Parameter(Mandatory = $true)][string]$RegistrySubKey)

    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($RegistrySubKey, $false)
    if ($null -eq $key) { throw ('Network class registry key disappeared: HKLM\' + $RegistrySubKey) }
    try {
        $present = @($key.GetValueNames() | Where-Object { [string]::Equals([string]$_, 'NetworkAddress', [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0
        if (-not $present) { return New-NicAddressRegistryValueSnapshot $false $null $null }
        $kind = $key.GetValueKind('NetworkAddress').ToString()
        $value = $key.GetValue('NetworkAddress', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        return New-NicAddressRegistryValueSnapshot $true $kind $value
    }
    finally { $key.Close() }
}

function Set-NicAddressRegistryValueSnapshot {
    param([Parameter(Mandatory = $true)][string]$RegistrySubKey, [Parameter(Mandatory = $true)][object]$ValueSnapshot)

    $classPrefix = 'SYSTEM\CurrentControlSet\Control\Class\{4d36e972-e325-11ce-bfc1-08002be10318}\'
    if (-not $RegistrySubKey.StartsWith($classPrefix, [StringComparison]::OrdinalIgnoreCase) -or $RegistrySubKey.Substring($classPrefix.Length) -notmatch '\A[0-9]{4}\z') {
        throw ('Refusing registry write outside a single network adapter class key: ' + $RegistrySubKey)
    }
    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($RegistrySubKey, $true)
    if ($null -eq $key) { throw ('Network class registry key cannot be opened for write: HKLM\' + $RegistrySubKey) }
    try {
        $present = [bool](Get-NicAddressPropertyValue $ValueSnapshot 'present')
        if (-not $present) {
            $key.DeleteValue('NetworkAddress', $false)
            return
        }
        $kindName = [string](Get-NicAddressPropertyValue $ValueSnapshot 'kind')
        $kind = [Microsoft.Win32.RegistryValueKind][Enum]::Parse([Microsoft.Win32.RegistryValueKind], $kindName)
        $value = ConvertFrom-NicAddressRegistryValueSnapshot $ValueSnapshot
        $key.SetValue('NetworkAddress', $value, $kind)
    }
    finally { $key.Close() }
}

function Assert-NicAddressSnapshotCapability {
    param([Parameter(Mandatory = $true)][object]$Snapshot)

    $capability = Get-NicAddressPropertyValue $Snapshot 'setCapability'
    if ($null -eq $capability -or -not [bool](Get-NicAddressPropertyValue $capability 'supported')) {
        $details = if ($null -eq $capability) { 'capability metadata is missing' } else { ConvertTo-Json -InputObject $capability -Compress -Depth 6 }
        throw ('Set-NetAdapter MAC override is not proven supported for this adapter; refusing mutation. ' + $details)
    }
    $replacement = ConvertTo-NicAddressCanonicalMac (Get-NicAddressPropertyValue $Snapshot 'replacement') 'Snapshot replacement MAC'
    if (-not (Test-NicAddressUnicastMac $replacement)) { throw ('Snapshot replacement MAC is not valid unicast: ' + $replacement) }
}

function Invoke-NicAddressApply {
    param([Parameter(Mandatory = $true)][object]$Snapshot, [Parameter(Mandatory = $true)][string]$ExpectedVmUuid, [Parameter(Mandatory = $true)][scriptblock]$PersistIntent)

    Assert-NicAddressSnapshotCapability $Snapshot
    $live = Get-NicAddressLiveAdapter $Snapshot $ExpectedVmUuid
    $liveMac = ConvertTo-NicAddressCanonicalMac (Get-NicAddressPropertyValue $live.adapter 'MacAddress') 'Current Get-NetAdapter MAC'
    $expectedOriginal = ConvertTo-NicAddressCanonicalMac (Get-NicAddressPropertyValue $Snapshot 'original') 'Snapshot original MAC'
    if ((ConvertTo-NicAddressMacKey $liveMac) -cne (ConvertTo-NicAddressMacKey $expectedOriginal)) {
        throw ('NIC address changed before apply: pnp=' + [string](Get-NicAddressPropertyValue $Snapshot 'pnpDeviceId') + ' expectedOriginal=' + $expectedOriginal + ' actual=' + $liveMac)
    }
    $currentRegistry = Get-NicAddressRegistryValueSnapshot ([string](Get-NicAddressPropertyValue $Snapshot 'registrySubKey'))
    $expectedRegistry = Get-NicAddressPropertyValue $Snapshot 'networkAddress'
    if (-not (Test-NicAddressRegistryValueSnapshotEqual $expectedRegistry $currentRegistry)) {
        throw ('NetworkAddress registry value changed before apply: pnp=' + [string](Get-NicAddressPropertyValue $Snapshot 'pnpDeviceId') + ' expected=' + (ConvertTo-Json -InputObject $expectedRegistry -Compress -Depth 8) + ' actual=' + (ConvertTo-Json -InputObject $currentRegistry -Compress -Depth 8))
    }
    $plan = New-NicAddressApplyPlan $Snapshot
    $replacement = [string]$plan.replacement
    $intent = New-NicAddressIntent 'apply' $Snapshot $replacement
    & $PersistIntent $intent | Out-Null

    $setCommand = Get-Command Set-NetAdapter -ErrorAction Stop
    if (-not $setCommand.Parameters.ContainsKey('InputObject') -or -not $setCommand.Parameters.ContainsKey('MacAddress')) {
        throw 'Set-NetAdapter lost the required InputObject or MacAddress parameter after preflight.'
    }
    Set-NetAdapter -InputObject $live.adapter -MacAddress $replacement -Confirm:$false -ErrorAction Stop | Out-Null
    $after = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop | Where-Object { (ConvertTo-NicAddressGuid (Get-NicAddressPropertyValue $_ 'InterfaceGuid')) -ceq [string](Get-NicAddressPropertyValue $Snapshot 'interfaceGuid') })
    if ($after.Count -ne 1) { throw ('Set-NetAdapter completed but exact adapter readback is ambiguous: expectedMatches=1 actual=' + $after.Count) }
    $actualMac = ConvertTo-NicAddressCanonicalMac (Get-NicAddressPropertyValue $after[0] 'MacAddress') 'Post-Set-NetAdapter MAC'
    if ((ConvertTo-NicAddressMacKey $actualMac) -cne (ConvertTo-NicAddressMacKey $replacement)) {
        throw ('Set-NetAdapter readback mismatch: expected=' + $replacement + ' actual=' + $actualMac + ' pnp=' + [string](Get-NicAddressPropertyValue $Snapshot 'pnpDeviceId'))
    }
    return [ordered]@{ action = 'apply'; pnpDeviceId = [string](Get-NicAddressPropertyValue $Snapshot 'pnpDeviceId'); interfaceGuid = [string](Get-NicAddressPropertyValue $Snapshot 'interfaceGuid'); original = $expectedOriginal; expected = $replacement; actual = $actualMac; matchesExpected = $true }
}

function Restore-NicAddressSnapshot {
    param([Parameter(Mandatory = $true)][object]$Snapshot, [Parameter(Mandatory = $true)][string]$ExpectedVmUuid, [Parameter(Mandatory = $true)][scriptblock]$PersistIntent)

    $live = Get-NicAddressLiveAdapter $Snapshot $ExpectedVmUuid
    $intent = New-NicAddressIntent 'restore' $Snapshot $null
    & $PersistIntent $intent | Out-Null
    $registrySubKey = [string](Get-NicAddressPropertyValue $Snapshot 'registrySubKey')
    $baseline = Get-NicAddressPropertyValue $Snapshot 'networkAddress'
    Set-NicAddressRegistryValueSnapshot $registrySubKey $baseline
    $restartCommand = Get-Command Restart-NetAdapter -ErrorAction Stop
    if (-not $restartCommand.Parameters.ContainsKey('InputObject')) { throw 'Restart-NetAdapter lost the exact InputObject parameter required for restoration.' }
    Restart-NetAdapter -InputObject $live.adapter -Confirm:$false -ErrorAction Stop | Out-Null

    $adminState = Get-NicAddressPropertyValue $Snapshot 'adminState'
    $originalEnabled = [bool](Get-NicAddressPropertyValue $adminState 'enabled')
    $current = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop | Where-Object { (ConvertTo-NicAddressGuid (Get-NicAddressPropertyValue $_ 'InterfaceGuid')) -ceq [string](Get-NicAddressPropertyValue $Snapshot 'interfaceGuid') })
    if ($current.Count -ne 1) { throw ('Restored adapter administrative state cannot be checked uniquely: expectedMatches=1 actual=' + $current.Count) }
    $currentAdmin = [string](Get-NicAddressPropertyValue $current[0] 'AdminStatus')
    $currentEnabled = $currentAdmin -match '^(?i:Up|Enabled|1|True)$'
    if ($originalEnabled -and -not $currentEnabled) {
        $enableCommand = Get-Command Enable-NetAdapter -ErrorAction Stop
        if (-not $enableCommand.Parameters.ContainsKey('InputObject')) { throw 'Enable-NetAdapter lost the exact InputObject parameter required for restoration.' }
        Enable-NetAdapter -InputObject $current[0] -Confirm:$false -ErrorAction Stop | Out-Null
    }
    elseif (-not $originalEnabled -and $currentEnabled) {
        $disableCommand = Get-Command Disable-NetAdapter -ErrorAction Stop
        if (-not $disableCommand.Parameters.ContainsKey('InputObject')) { throw 'Disable-NetAdapter lost the exact InputObject parameter required for restoration.' }
        Disable-NetAdapter -InputObject $current[0] -Confirm:$false -ErrorAction Stop | Out-Null
    }

    $actualRegistry = Get-NicAddressRegistryValueSnapshot $registrySubKey
    if (-not (Test-NicAddressRegistryValueSnapshotEqual $baseline $actualRegistry)) {
        throw ('NetworkAddress registry restore mismatch: expected=' + (ConvertTo-Json -InputObject $baseline -Compress -Depth 8) + ' actual=' + (ConvertTo-Json -InputObject $actualRegistry -Compress -Depth 8))
    }
    $restored = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop | Where-Object { (ConvertTo-NicAddressGuid (Get-NicAddressPropertyValue $_ 'InterfaceGuid')) -ceq [string](Get-NicAddressPropertyValue $Snapshot 'interfaceGuid') })
    if ($restored.Count -ne 1) { throw ('Restored adapter readback is ambiguous: expectedMatches=1 actual=' + $restored.Count) }
    $restoredAdmin = [string](Get-NicAddressPropertyValue $restored[0] 'AdminStatus')
    $restoredEnabled = $restoredAdmin -match '^(?i:Up|Enabled|1|True)$'
    if ($restoredEnabled -ne $originalEnabled) {
        throw ('Adapter administrative state restore mismatch: expectedEnabled=' + $originalEnabled + ' actual=' + $restoredAdmin)
    }
    return [ordered]@{
        action = 'restore'
        pnpDeviceId = [string](Get-NicAddressPropertyValue $Snapshot 'pnpDeviceId')
        interfaceGuid = [string](Get-NicAddressPropertyValue $Snapshot 'interfaceGuid')
        registryExpected = $baseline
        registryActual = $actualRegistry
        registryMatches = $true
        adminExpectedEnabled = $originalEnabled
        adminActual = $restoredAdmin
        adminMatches = $true
        originalMacForReadback = [string](Get-NicAddressPropertyValue $Snapshot 'original')
    }
}
