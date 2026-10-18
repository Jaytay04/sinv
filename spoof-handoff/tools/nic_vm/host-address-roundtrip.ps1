[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Run', 'Rollback')]
    [string]$Action,

    [Parameter(Mandatory = $false)]
    [string]$VmGatePath,

    [Parameter(Mandatory = $false)]
    [string]$NativeExe,

    [Parameter(Mandatory = $false)]
    [string]$RunDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$script:HostRunSchema = 'nic-host-address-roundtrip.v1'
$script:ExpectedHostUuid = 'C88B4631-C88C-871B-A6D7-D8BBC1D85720'
$script:ExpectedBaseboardManufacturer = 'Micro-Star International Co., Ltd.'
$script:ExpectedSecureBootVariableGuid = '8be4df61-93ca-11d2-aa0d-00e098032b8c'
$script:ExpectedNativeExeSha256 = 'BF553F39E4E7024C75272C24215D995858627AEDD5E946A4B1289A8033DA1642'
$script:ExpectedRegistryVmCaptureRelativePath = 'spoof-handoff\reports\nic-vm-20261017\uefi-windows-probe-20261018-011305-e1ffeb9e48\evidence\kernel-metadata.json'
$script:ExpectedRegistryVmCaptureSha256 = '067674BBA247E1AD31FCA99826D4F3A0A67655DF989E1D129B8493117C518695'
$script:ExpectedVmnetAdapterDriverSha256 = '900DAB67810607208D37C7EF441D607D11D294650183475AF50158FAECEC7CCE'
$script:ExpectedLockedTsvSha256 = 'F628CB25D18F549A877C1529A82986B524BFD3D8B9801A776405F969FC4F42C3'
$script:ExpectedCertificateThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
$script:ExpectedVmGateSha256 = '118E71B245CF1FF163E1E0F6DB1AB5C11575CE6B59A08FA67975135FD15CDE83'
$script:ExpectedHostBindings = @(
    [ordered]@{ pnpDeviceId = 'ROOT\VMWARE\0001'; original = '00:50:56:C0:00:08'; replacement = '02:90:10:20:30:40' },
    [ordered]@{ pnpDeviceId = 'ROOT\VMWARE\0000'; original = '00:50:56:C0:00:01'; replacement = '02:90:10:20:30:41' },
    [ordered]@{ pnpDeviceId = 'PCI\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\3&11583659&1&A3'; original = 'C4:BD:E5:32:06:D1'; replacement = '02:90:10:20:30:42' },
    [ordered]@{ pnpDeviceId = 'PCI\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\4&3FA6681&0&00E2'; original = 'D8:BB:C1:D8:57:20'; replacement = '02:90:10:20:30:43' }
)
$script:RequiredReaders = @(
    'OID_802_3_CURRENT_ADDRESS',
    'GetAdaptersAddresses',
    'GetAdaptersInfo',
    'GetIfTable2',
    'Win32_NetworkAdapter.MACAddress',
    'MSFT_NetAdapter.NetworkAddresses',
    'Get-NetAdapter.MacAddress'
)

. (Join-Path $PSScriptRoot 'adapter-address.ps1')

function Get-HostProperty {
    param([AllowNull()][object]$Object, [Parameter(Mandatory = $true)][string]$Name)
    if ($null -eq $Object) { return $null }
    if ($Object -is [Collections.IDictionary]) {
        foreach ($key in $Object.Keys) {
            if ([string]::Equals([string]$key, $Name, [StringComparison]::OrdinalIgnoreCase)) { return $Object[$key] }
        }
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function ConvertTo-HostMacKey {
    param([AllowNull()][object]$Value)
    if ($null -eq $Value) { return $null }
    $text = ([string]$Value) -replace '[^0-9A-Fa-f]', ''
    if ($text -notmatch '\A[0-9A-Fa-f]{12}\z') { return $null }
    return $text.ToUpperInvariant()
}

function ConvertTo-HostCanonicalMac {
    param([Parameter(Mandatory = $true)][object]$Value, [Parameter(Mandatory = $true)][string]$Label)
    $key = ConvertTo-HostMacKey $Value
    if ($null -eq $key) { throw ($Label + ' is not a six-byte MAC address: ' + [string]$Value) }
    return (($key -split '(..)' | Where-Object { $_ }) -join ':')
}

function Get-HostSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Write-HostJsonAtomic {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][object]$Value)
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { [void][IO.Directory]::CreateDirectory($parent) }
    $tempPath = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 20))
    $stream = [IO.File]::Open($tempPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($tempPath, $Path, [NullString]::Value) }
    else { [IO.File]::Move($tempPath, $Path) }
}

function ConvertTo-HostMutableData {
    param([AllowNull()][object]$Value)
    if ($null -eq $Value -or $Value -is [string] -or $Value -is [ValueType]) { return $Value }
    if ($Value -is [Collections.IDictionary]) {
        $copy = [ordered]@{}
        foreach ($key in $Value.Keys) { $copy[[string]$key] = ConvertTo-HostMutableData $Value[$key] }
        return $copy
    }
    if ($Value -is [Array]) {
        $items = @()
        foreach ($item in $Value) { $items += ,(ConvertTo-HostMutableData $item) }
        return ,$items
    }
    if ($Value -is [Management.Automation.PSCustomObject]) {
        $copy = [ordered]@{}
        foreach ($property in $Value.PSObject.Properties) { $copy[$property.Name] = ConvertTo-HostMutableData $property.Value }
        return $copy
    }
    return $Value
}

function Assert-HostAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this action from an elevated PowerShell session.'
    }
}

function Assert-PathWithinRoot {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Root, [Parameter(Mandatory = $true)][string]$Label)
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $resolvedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (-not $resolvedPath.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw ($Label + ' must remain beneath ' + $resolvedRoot + ': ' + $resolvedPath)
    }
    return $resolvedPath
}

function Get-HostRepositoryRoots {
    $worktreeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
    $mainRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..'))
    if (-not (Test-Path -LiteralPath (Join-Path $mainRoot '.git'))) { throw ('Cannot resolve the main repository root from the worktree: ' + $mainRoot) }
    return [ordered]@{ worktree = $worktreeRoot; main = $mainRoot }
}

function Get-HostLockedBindings {
    param([Parameter(Mandatory = $true)][string]$TsvPath)
    if (-not (Test-Path -LiteralPath $TsvPath -PathType Leaf)) { throw ('Locked identifier map is missing: ' + $TsvPath) }
    $rows = @(Import-Csv -LiteralPath $TsvPath -Delimiter "`t" -ErrorAction Stop | Where-Object { $_.member -ceq 'FakeMacAddress' })
    if ($rows.Count -ne 4) { throw ('Expected four locked NIC rows in client-originals.tsv; actual=' + $rows.Count) }
    $byPnp = @{}
    foreach ($row in $rows) {
        $separator = ([string]$row.device).IndexOf('|')
        if ($separator -lt 0) { throw ('Locked NIC row has no PnP identity separator: ' + [string]$row.device) }
        $pnp = ([string]$row.device).Substring($separator + 1)
        if ([string]::IsNullOrWhiteSpace($pnp) -or $byPnp.ContainsKey($pnp.ToUpperInvariant())) { throw ('Locked NIC PnP identity is missing or duplicated: ' + $pnp) }
        $original = ConvertTo-HostCanonicalMac $row.original ('Locked original for ' + $pnp)
        $replacement = ConvertTo-HostCanonicalMac $row.replacement ('Locked replacement for ' + $pnp)
        $byPnp[$pnp.ToUpperInvariant()] = [ordered]@{ pnpDeviceId = $pnp; original = $original; replacement = $replacement }
    }
    foreach ($expected in $script:ExpectedHostBindings) {
        $key = ([string]$expected.pnpDeviceId).ToUpperInvariant()
        if (-not $byPnp.ContainsKey($key)) { throw ('The locked TSV is missing the pinned host adapter: ' + $expected.pnpDeviceId) }
        $actual = $byPnp[$key]
        if ((ConvertTo-HostMacKey $actual.original) -cne (ConvertTo-HostMacKey $expected.original) -or
            (ConvertTo-HostMacKey $actual.replacement) -cne (ConvertTo-HostMacKey $expected.replacement)) {
            throw ('The locked TSV values differ from the pinned approved mapping for ' + $expected.pnpDeviceId + '; expected=' + $expected.original + ' -> ' + $expected.replacement + '; actual=' + $actual.original + ' -> ' + $actual.replacement)
        }
    }
    return @($script:ExpectedHostBindings | ForEach-Object { $byPnp[([string]$_.pnpDeviceId).ToUpperInvariant()] })
}

function Get-HostGateCapture {
    param([Parameter(Mandatory = $true)][object]$Capture, [Parameter(Mandatory = $true)][string]$RepositoryRoot, [Parameter(Mandatory = $true)][string]$Label)
    $relative = [string](Get-HostProperty $Capture 'path')
    if ([string]::IsNullOrWhiteSpace($relative) -or [IO.Path]::IsPathRooted($relative) -or $relative -match '(^|[/\\])\.\.([/\\]|$)') {
        throw ('VM gate ' + $Label + ' capture has an unsafe repository-relative path: ' + $relative)
    }
    $full = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $relative.Replace('/', '\')))
    [void](Assert-PathWithinRoot $full (Join-Path $RepositoryRoot 'spoof-handoff\reports') ('VM gate ' + $Label + ' capture'))
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw ('VM gate ' + $Label + ' capture is missing: ' + $full) }
    $item = Get-Item -LiteralPath $full -Force
    $expectedBytes = [long](Get-HostProperty $Capture 'bytes')
    $expectedHash = [string](Get-HostProperty $Capture 'sha256')
    $actualHash = Get-HostSha256 $full
    if ($item.Length -ne $expectedBytes -or $actualHash -cne $expectedHash.ToUpperInvariant()) {
        throw ('VM gate ' + $Label + ' capture hash/size mismatch: ' + $relative + ' expectedBytes=' + $expectedBytes + ' actualBytes=' + $item.Length + ' expectedSha256=' + $expectedHash + ' actualSha256=' + $actualHash)
    }
    return [ordered]@{ path = $relative.Replace('\', '/'); bytes = [long]$item.Length; sha256 = $actualHash }
}

function ConvertTo-HostVmGuidKey {
    param([AllowNull()][object]$Value, [Parameter(Mandatory = $true)][string]$Label)
    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) { throw ($Label + ' is missing.') }
    try { return ([Guid]::Parse([string]$Value)).ToString('D').ToUpperInvariant() }
    catch { throw ($Label + ' is not a valid GUID: ' + [string]$Value) }
}

function Get-HostVmRows {
    param([Parameter(Mandatory = $true)][object]$Object, [Parameter(Mandatory = $true)][string]$Property, [Parameter(Mandatory = $true)][string]$Label)
    $value = Get-HostProperty $Object $Property
    if ($null -eq $value) { throw ($Label + ' collection is missing.') }
    return ,@($value)
}

function Get-HostVmUniqueGuidRow {
    param([Parameter(Mandatory = $true)][object[]]$Rows, [Parameter(Mandatory = $true)][string[]]$GuidProperties, [Parameter(Mandatory = $true)][string]$Guid, [Parameter(Mandatory = $true)][string]$Label)
    $found = @()
    foreach ($row in $Rows) {
        $rowGuids = @()
        foreach ($property in $GuidProperties) {
            $raw = Get-HostProperty $row $property
            if ($null -ne $raw -and -not [string]::IsNullOrWhiteSpace([string]$raw)) {
                $rowGuids += ConvertTo-HostVmGuidKey $raw ($Label + '.' + $property)
            }
        }
        if ($rowGuids.Count -gt 0) {
            $uniqueRowGuids = @($rowGuids | Sort-Object -Unique)
            if ($uniqueRowGuids.Count -ne 1) { throw ($Label + ' row has conflicting GUID fields: ' + (ConvertTo-Json -InputObject $row -Compress -Depth 5)) }
            if ($uniqueRowGuids[0] -ceq $Guid) { $found += ,$row }
        }
    }
    if ($found.Count -ne 1) { throw ($Label + ' identity must resolve to exactly one row; GUID=' + $Guid + ' matches=' + $found.Count) }
    return $found[0]
}

function Get-HostVmUniquePnpRow {
    param([Parameter(Mandatory = $true)][object[]]$Rows, [Parameter(Mandatory = $true)][string]$PnpDeviceId, [Parameter(Mandatory = $true)][string]$Label)
    $found = @()
    foreach ($row in $Rows) {
        $pnp = [string](Get-HostProperty $row 'PNPDeviceID')
        if (-not [string]::IsNullOrWhiteSpace($pnp) -and [string]::Equals($pnp, $PnpDeviceId, [StringComparison]::OrdinalIgnoreCase)) { $found += ,$row }
    }
    if ($found.Count -ne 1) { throw ($Label + ' identity must resolve to exactly one row; PnP=' + $PnpDeviceId + ' matches=' + $found.Count) }
    return $found[0]
}

function Get-HostVmRowAddress {
    param([Parameter(Mandatory = $true)][object]$Row, [Parameter(Mandatory = $true)][string]$Property, [Parameter(Mandatory = $true)][string]$Label)
    $raw = Get-HostProperty $Row $Property
    $items = @()
    if ($null -ne $raw) {
        if ($raw -is [Array] -or $raw -is [Collections.IList]) { $items = @($raw) }
        else { $items = @($raw) }
    }
    $addresses = @()
    foreach ($item in $items) {
        if ($null -eq $item -or [string]::IsNullOrWhiteSpace([string]$item)) { continue }
        $key = ConvertTo-HostMacKey $item
        if ($null -eq $key) { throw ($Label + ' contains a malformed MAC address: ' + [string]$item) }
        $addresses += $key
    }
    $distinct = @($addresses | Sort-Object -Unique)
    if ($distinct.Count -ne 1 -or $addresses.Count -ne 1) { throw ($Label + ' must contain exactly one MAC address; actual=' + (ConvertTo-Json -InputObject $raw -Compress -Depth 4)) }
    return $distinct[0]
}

function ConvertFrom-HostVmRawCaptureData {
    param(
        [Parameter(Mandatory = $true)][object]$Data,
        [Parameter(Mandatory = $true)][string]$PhaseName,
        [Parameter(Mandatory = $true)][string]$ExpectedMode,
        [Parameter(Mandatory = $true)][object[]]$LockedBindings,
        [Parameter(Mandatory = $true)][string[]]$Readers
    )
    if ($ExpectedMode -cnotin @('Original', 'Replacement')) { throw ('Unexpected VM expected mode: ' + $ExpectedMode) }
    if ($Readers.Count -ne 7 -or @($Readers | Sort-Object -Unique).Count -ne 7 -or $Readers -ccontains 'Unknown') { throw 'VM raw mapper requires the exact seven-reader list.' }

    $native = Get-HostProperty $Data 'nativeCapture'
    if ($null -eq $native -or [int](Get-HostProperty $native 'exitCode') -ne 0) { throw ('VM native capture did not exit successfully: phase=' + $PhaseName) }
    $nativeData = Get-HostProperty $native 'data'
    if ($null -eq $nativeData -or -not [bool](Get-HostProperty $nativeData 'captureComplete') -or
        -not [bool](Get-HostProperty $nativeData 'readOnly') -or [bool](Get-HostProperty $nativeData 'networkConfigurationChanged')) {
        throw ('VM native capture data is incomplete or not read-only: phase=' + $PhaseName)
    }
    $windows = Get-HostProperty $Data 'windowsReads'
    if ($null -eq $windows) { throw ('VM Windows API read collection is missing: phase=' + $PhaseName) }
    foreach ($name in @('win32NetworkAdapter', 'msftNetAdapter', 'getNetAdapter')) {
        $collection = Get-HostProperty $windows $name
        if ($null -eq $collection -or $null -ne (Get-HostProperty $collection 'error')) { throw ('VM Windows reader failed or is missing: phase=' + $PhaseName + ' reader=' + $name + ' error=' + [string](Get-HostProperty $collection 'error')) }
    }

    $bindingsByAddress = @{}
    foreach ($binding in $LockedBindings) {
        $property = if ($ExpectedMode -ceq 'Original') { 'original' } else { 'replacement' }
        $address = ConvertTo-HostMacKey (Get-HostProperty $binding $property)
        if ($null -eq $address -or $bindingsByAddress.ContainsKey($address)) { throw ('Locked NIC addresses are missing, malformed, or duplicated for mode ' + $ExpectedMode) }
        $bindingsByAddress[$address] = $binding
    }
    if ($bindingsByAddress.Count -ne 4) { throw ('VM raw capture requires four distinct locked adapter values; actual=' + $bindingsByAddress.Count) }

    $setup = Get-HostProperty $nativeData 'setupDiNdisLanClass'
    if ($null -eq $setup -or -not [bool](Get-HostProperty $setup 'complete') -or [string](Get-HostProperty $setup 'enumerationErrorHex') -cne '0x00000000') { throw ('VM SetupDi NDIS LAN enumeration is incomplete or failed: phase=' + $PhaseName + ' error=' + [string](Get-HostProperty $setup 'enumerationErrorHex')) }
    $interfaces = Get-HostVmRows $setup 'interfaces' 'SetupDi NDIS LAN'
    if ($interfaces.Count -ne 4 -or [int](Get-HostProperty $setup 'enumeratedInterfaces') -ne 4) { throw ('VM SetupDi NDIS LAN must contain exactly four target interfaces; phase=' + $PhaseName + ' rows=' + $interfaces.Count) }

    $nativeReaders = [ordered]@{
        'GetAdaptersAddresses' = @{ collection = Get-HostProperty $nativeData 'getAdaptersAddresses'; rowsProperty = 'entries'; guidProperties = @('adapterName'); valueProperty = 'physicalAddress'; label = 'GetAdaptersAddresses' }
        'GetAdaptersInfo' = @{ collection = Get-HostProperty $nativeData 'getAdaptersInfo'; rowsProperty = 'entries'; guidProperties = @('adapterName'); valueProperty = 'address'; label = 'GetAdaptersInfo' }
        'GetIfTable2' = @{ collection = Get-HostProperty $nativeData 'getIfTable2'; rowsProperty = 'entries'; guidProperties = @('interfaceGuid'); valueProperty = 'physicalAddress'; label = 'GetIfTable2' }
    }
    foreach ($nativeReader in $nativeReaders.GetEnumerator()) {
        if ($null -eq $nativeReader.Value.collection -or [string](Get-HostProperty $nativeReader.Value.collection 'statusHex') -cne '0x00000000') {
            throw ('VM native reader failed or is missing: phase=' + $PhaseName + ' reader=' + $nativeReader.Key + ' status=' + [string](Get-HostProperty $nativeReader.Value.collection 'statusHex'))
        }
        $nativeReader.Value.rows = Get-HostVmRows $nativeReader.Value.collection $nativeReader.Value.rowsProperty $nativeReader.Key
    }

    $win32Rows = Get-HostVmRows (Get-HostProperty $windows 'win32NetworkAdapter') 'rows' 'Win32_NetworkAdapter'
    $msftRows = Get-HostVmRows (Get-HostProperty $windows 'msftNetAdapter') 'rows' 'MSFT_NetAdapter'
    $getNetRows = Get-HostVmRows (Get-HostProperty $windows 'getNetAdapter') 'rows' 'Get-NetAdapter'

    $rawValues = [ordered]@{}
    $seenPnp = @{}
    $seenGuids = @{}
    $seenLocked = @{}
    foreach ($interface in $interfaces) {
        $pnp = [string](Get-HostProperty $interface 'pnpDeviceId')
        $pnpKey = $pnp.ToUpperInvariant()
        if ([string]::IsNullOrWhiteSpace($pnp) -or $seenPnp.ContainsKey($pnpKey)) { throw ('VM raw SetupDi capture has empty or duplicate PnP identity: phase=' + $PhaseName + ' pnp=' + $pnp) }
        $seenPnp[$pnpKey] = $true
        $guid = ConvertTo-HostVmGuidKey (Get-HostProperty $interface 'netCfgInstanceId') ('SetupDi NetCfgInstanceId for ' + $pnp)
        if ($seenGuids.ContainsKey($guid)) { throw ('VM raw SetupDi capture has duplicate NetCfgInstanceId: phase=' + $PhaseName + ' guid=' + $guid) }
        $seenGuids[$guid] = $true

        $oid = Get-HostProperty $interface 'ethernetAddress'
        if ($null -eq $oid -or [string](Get-HostProperty $oid 'name') -cne 'OID_802_3_CURRENT_ADDRESS' -or
            -not [bool](Get-HostProperty $oid 'ioctlSucceeded') -or [int](Get-HostProperty $oid 'bytesReturned') -ne 6 -or [int](Get-HostProperty $oid 'bytesCaptured') -ne 6) {
            throw ('VM raw NDIS OID read is missing or unsuccessful: phase=' + $PhaseName + ' pnp=' + $pnp)
        }
        $oidMac = Get-HostVmRowAddress $oid 'mac' ('OID_802_3_CURRENT_ADDRESS for ' + $pnp)
        if (-not $bindingsByAddress.ContainsKey($oidMac)) { throw ('VM raw SetupDi identity maps to an unknown locked value: phase=' + $PhaseName + ' pnp=' + $pnp + ' mac=' + $oidMac) }
        $binding = $bindingsByAddress[$oidMac]
        $bindingKey = ConvertTo-HostMacKey (Get-HostProperty $binding original)
        if ($seenLocked.ContainsKey($bindingKey)) { throw ('VM raw capture duplicates a locked adapter mapping: phase=' + $PhaseName + ' pnp=' + $pnp + ' original=' + $binding.original) }
        $seenLocked[$bindingKey] = $true
        $expectedMac = if ($ExpectedMode -ceq 'Original') { ConvertTo-HostMacKey $binding.original } else { ConvertTo-HostMacKey $binding.replacement }
        if ($oidMac -cne $expectedMac) { throw ('VM raw OID value does not match its locked mode: phase=' + $PhaseName + ' pnp=' + $pnp + ' expected=' + $expectedMac + ' actual=' + $oidMac) }

        $byReader = [ordered]@{}
        $byReader['OID_802_3_CURRENT_ADDRESS'] = $oidMac
        foreach ($readerName in @('GetAdaptersAddresses', 'GetAdaptersInfo', 'GetIfTable2')) {
            $readerInfo = $nativeReaders[$readerName]
            $row = Get-HostVmUniqueGuidRow $readerInfo.rows $readerInfo.guidProperties $guid ('Raw ' + $readerName + ' for ' + $pnp)
            $byReader[$readerName] = Get-HostVmRowAddress $row $readerInfo.valueProperty ('Raw ' + $readerName + ' for ' + $pnp)
        }

        $win32 = Get-HostVmUniquePnpRow $win32Rows $pnp ('Raw Win32_NetworkAdapter for ' + $pnp)
        $win32Guid = ConvertTo-HostVmGuidKey (Get-HostProperty $win32 'GUID') ('Win32_NetworkAdapter.GUID for ' + $pnp)
        if ($win32Guid -cne $guid) { throw ('Raw Win32 adapter GUID does not match SetupDi identity: pnp=' + $pnp + ' expected=' + $guid + ' actual=' + $win32Guid) }
        $byReader['Win32_NetworkAdapter.MACAddress'] = Get-HostVmRowAddress $win32 'MACAddress' ('Raw Win32_NetworkAdapter.MACAddress for ' + $pnp)

        $msft = Get-HostVmUniquePnpRow $msftRows $pnp ('Raw MSFT_NetAdapter for ' + $pnp)
        $msftGuid = ConvertTo-HostVmGuidKey (Get-HostProperty $msft 'InterfaceGuid') ('MSFT_NetAdapter.InterfaceGuid for ' + $pnp)
        if ($msftGuid -cne $guid) { throw ('Raw MSFT adapter GUID does not match SetupDi identity: pnp=' + $pnp + ' expected=' + $guid + ' actual=' + $msftGuid) }
        $byReader['MSFT_NetAdapter.NetworkAddresses'] = Get-HostVmRowAddress $msft 'NetworkAddresses' ('Raw MSFT_NetAdapter.NetworkAddresses for ' + $pnp)

        $netAdapter = Get-HostVmUniqueGuidRow $getNetRows @('InterfaceGuid') $guid ('Raw Get-NetAdapter for ' + $pnp)
        $byReader['Get-NetAdapter.MacAddress'] = Get-HostVmRowAddress $netAdapter 'MacAddress' ('Raw Get-NetAdapter.MacAddress for ' + $pnp)

        if ($byReader.Count -ne 7) { throw ('VM raw mapping produced an unexpected reader count: phase=' + $PhaseName + ' pnp=' + $pnp + ' count=' + $byReader.Count) }
        foreach ($reader in $Readers) {
            if (-not $byReader.Contains($reader)) { throw ('VM raw capture is missing an expected reader: phase=' + $PhaseName + ' pnp=' + $pnp + ' reader=' + $reader) }
            if ((ConvertTo-HostMacKey $byReader[$reader]) -cne $expectedMac) { throw ('VM raw reader did not match the locked mode: phase=' + $PhaseName + ' pnp=' + $pnp + ' reader=' + $reader + ' expected=' + $expectedMac + ' actual=' + [string]$byReader[$reader]) }
        }
        $rawValues[$pnpKey] = [ordered]@{ pnpDeviceId = $pnp; interfaceGuid = $guid; original = [string]$binding.original; replacement = [string]$binding.replacement; readers = $byReader }
    }
    if ($rawValues.Count -ne 4 -or $seenLocked.Count -ne 4) { throw ('VM raw capture did not cover exactly four locked adapters: phase=' + $PhaseName + ' adapters=' + $rawValues.Count) }
    return $rawValues
}

function Get-HostVmRawCaptureValues {
    param(
        [Parameter(Mandatory = $true)][object]$Capture,
        [Parameter(Mandatory = $true)][string]$PhaseName,
        [Parameter(Mandatory = $true)][string]$ExpectedMode,
        [Parameter(Mandatory = $true)][object[]]$LockedBindings,
        [Parameter(Mandatory = $true)][string[]]$Readers,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot
    )
    $path = Join-Path $RepositoryRoot ([string](Get-HostProperty $Capture 'path')).Replace('/', '\')
    $data = Get-Content -LiteralPath $path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $nativeExeHash = [string](Get-HostProperty (Get-HostProperty $data 'nativeCapture') 'executableSha256')
    if ($nativeExeHash.ToUpperInvariant() -cne $script:ExpectedNativeExeSha256) { throw ('VM raw native capture does not name the pinned reader executable: phase=' + $PhaseName + ' expected=' + $script:ExpectedNativeExeSha256 + ' actual=' + $nativeExeHash) }
    if ((Get-HostProperty $data 'phase') -cne 'Readback' -or (Get-HostProperty $data 'requiredReaderSet') -cne 'CurrentSeven' -or
        -not [bool](Get-HostProperty $data 'readOnly') -or -not [bool](Get-HostProperty $data 'allTargetsMatch') -or
        [bool](Get-HostProperty $data 'hostOrGuestSettingsChanged') -or (Get-HostProperty $data 'expectedMode') -cne $ExpectedMode) {
        throw ('VM raw capture did not satisfy its phase/read-only/all-target gate: phase=' + $PhaseName + ' path=' + $path)
    }
    if (@(Get-HostProperty $data 'failures').Count -ne 0) { throw ('VM raw capture contains failure records: phase=' + $PhaseName + ' path=' + $path) }
    $expectedFilter = switch ($PhaseName) {
        'original' { 'Absent' }
        'filteredActive' { 'Present' }
        'unfilteredActive' { 'AbsentConfigured' }
        'restored' { 'Absent' }
        default { throw ('Unexpected VM raw capture phase name: ' + $PhaseName) }
    }
    if ((Get-HostProperty $data 'requestedFilterState') -cne $expectedFilter) {
        throw ('VM raw capture filter state mismatch: phase=' + $PhaseName + ' expected=' + $expectedFilter + ' actual=' + [string](Get-HostProperty $data 'requestedFilterState'))
    }
    $services = @(Get-HostProperty $data 'adapterServices')
    if ($PhaseName -ceq 'filteredActive') {
        if ($services.Count -ne 1 -or [string](Get-HostProperty $services[0] 'Name') -cne 'HypersinvNicAddressFilter' -or [string](Get-HostProperty $services[0] 'State') -cne 'Running') {
            throw ('Filtered VM raw phase did not record the expected running NIC filter service: ' + (ConvertTo-Json -InputObject $services -Compress -Depth 6))
        }
    }
    elseif ($services.Count -ne 0) { throw ('VM raw phase expected no NIC filter service: phase=' + $PhaseName + ' actual=' + (ConvertTo-Json -InputObject $services -Compress -Depth 6)) }
    if ($PhaseName -ceq 'restored') {
        $integrity = Get-HostProperty $data 'codeIntegrity'
        if ([bool](Get-HostProperty $integrity 'testSigning')) { throw 'Restored VM capture has Windows test-signing enabled.' }
        $trust = Get-HostProperty $data 'trustMembership'
        if ([bool](Get-HostProperty $trust 'Root') -or [bool](Get-HostProperty $trust 'TrustedPublisher')) { throw 'Restored VM capture still trusts the test signer.' }
    }

    $rawValues = ConvertFrom-HostVmRawCaptureData $data $PhaseName $ExpectedMode $LockedBindings $Readers
    $evaluations = @(Get-HostProperty $data 'evaluations')
    if ($evaluations.Count -ne 4) { throw ('VM raw capture must contain four adapter evaluations: phase=' + $PhaseName + ' actual=' + $evaluations.Count) }
    $seenEvaluations = @{}
    foreach ($evaluation in $evaluations) {
        $pnp = [string](Get-HostProperty $evaluation 'pnpDeviceId')
        $pnpKey = $pnp.ToUpperInvariant()
        if ([string]::IsNullOrWhiteSpace($pnp) -or $seenEvaluations.ContainsKey($pnpKey) -or -not $rawValues.Contains($pnpKey)) { throw ('VM evaluation identity is empty, duplicate, or absent from independently derived raw values: phase=' + $PhaseName + ' pnp=' + $pnp) }
        $seenEvaluations[$pnpKey] = $true
        $raw = $rawValues[$pnpKey]
        $guid = ConvertTo-HostVmGuidKey (Get-HostProperty $evaluation 'profileInterfaceGuid') ('Evaluation profile GUID for ' + $pnp)
        $netCfgGuid = ConvertTo-HostVmGuidKey (Get-HostProperty $evaluation 'netCfgInstanceGuid') ('Evaluation NetCfg GUID for ' + $pnp)
        if ($guid -cne $raw.interfaceGuid -or $netCfgGuid -cne $raw.interfaceGuid -or (Get-HostProperty $evaluation 'expectedMode') -cne $ExpectedMode) {
            throw ('VM evaluation identity/mode differs from independent raw identity: phase=' + $PhaseName + ' pnp=' + $pnp + ' rawGuid=' + $raw.interfaceGuid + ' profileGuid=' + $guid + ' netCfgGuid=' + $netCfgGuid)
        }
        $expectedMac = if ($ExpectedMode -ceq 'Original') { ConvertTo-HostMacKey $raw.original } else { ConvertTo-HostMacKey $raw.replacement }
        if ((ConvertTo-HostMacKey (Get-HostProperty $evaluation 'expected')) -cne $expectedMac) { throw ('VM evaluation expected value differs from independent raw values: phase=' + $PhaseName + ' pnp=' + $pnp + ' expected=' + $expectedMac + ' actual=' + [string](Get-HostProperty $evaluation 'expected')) }

        $identityChecks = @(Get-HostProperty $evaluation 'identityChecks')
        if ($identityChecks.Count -ne 3) { throw ('VM identity check count is not 3: pnp=' + $pnp + ' count=' + $identityChecks.Count) }
        $identitySources = @('profile interface GUID', 'Win32_NetworkAdapter GUID', 'MSFT_NetAdapter GUID')
        $seenIdentitySources = @{}
        foreach ($identityCheck in $identityChecks) {
            $identitySource = [string](Get-HostProperty $identityCheck 'source')
            if ($identitySources -cnotcontains $identitySource -or $seenIdentitySources.ContainsKey($identitySource)) { throw ('VM evaluation identity source is unknown or duplicated: phase=' + $PhaseName + ' pnp=' + $pnp + ' source=' + $identitySource) }
            $seenIdentitySources[$identitySource] = $true
            $identityExpected = ConvertTo-HostVmGuidKey (Get-HostProperty $identityCheck 'expected') ('Evaluation identity expected for ' + $pnp)
            $identityActual = ConvertTo-HostVmGuidKey (Get-HostProperty $identityCheck 'actual') ('Evaluation identity actual for ' + $pnp)
            if (-not [bool](Get-HostProperty $identityCheck 'matchesExpected') -or $identityExpected -cne $raw.interfaceGuid -or $identityActual -cne $raw.interfaceGuid) {
                throw ('VM evaluation identity readback differs from independently derived GUID: phase=' + $PhaseName + ' pnp=' + $pnp + ' source=' + $identitySource + ' expected=' + $raw.interfaceGuid + ' actual=' + $identityActual)
            }
        }
        foreach ($identitySource in $identitySources) { if (-not $seenIdentitySources.ContainsKey($identitySource)) { throw ('VM evaluation identity source is missing: phase=' + $PhaseName + ' pnp=' + $pnp + ' source=' + $identitySource) } }
        $checks = @(Get-HostProperty $evaluation 'checks')
        if ($checks.Count -ne $Readers.Count) { throw ('VM evaluation reader count mismatch: pnp=' + $pnp + ' phase=' + $PhaseName + ' actual=' + $checks.Count) }
        $seenReaders = @{}
        foreach ($check in $checks) {
            $reader = [string](Get-HostProperty $check 'source')
            if ($Readers -cnotcontains $reader -or $seenReaders.ContainsKey($reader) -or -not $raw.readers.Contains($reader)) { throw ('VM evaluation reader is unknown, duplicate, or absent from raw data: pnp=' + $pnp + ' reader=' + $reader) }
            $seenReaders[$reader] = $true
            $actual = ConvertTo-HostMacKey (Get-HostProperty $check 'actual')
            $checkExpected = ConvertTo-HostMacKey (Get-HostProperty $check 'expected')
            $rawActual = ConvertTo-HostMacKey $raw.readers[$reader]
            if ($actual -cne $rawActual -or $checkExpected -cne $expectedMac -or [int](Get-HostProperty $check 'matchCount') -ne 1 -or -not [bool](Get-HostProperty $check 'matchesExpected')) {
                throw ('VM evaluation reader differs from independent raw observation: phase=' + $PhaseName + ' pnp=' + $pnp + ' reader=' + $reader + ' expected=' + $expectedMac + ' raw=' + $rawActual + ' evaluation=' + [string](Get-HostProperty $check 'actual'))
            }
        }
        foreach ($reader in $Readers) { if (-not $seenReaders.ContainsKey($reader)) { throw ('VM evaluation is missing a reader: phase=' + $PhaseName + ' pnp=' + $pnp + ' reader=' + $reader) } }
        if (-not [bool](Get-HostProperty $evaluation 'allRequiredSourcesMatch')) { throw ('VM evaluation summary is false: phase=' + $PhaseName + ' pnp=' + $pnp) }
    }
    if ($seenEvaluations.Count -ne $rawValues.Count) { throw ('VM raw and evaluation identities differ: phase=' + $PhaseName + ' raw=' + $rawValues.Count + ' evaluations=' + $seenEvaluations.Count) }
    return [ordered]@{ phase = $PhaseName; path = $path; values = $rawValues }
}

function Assert-HostRegistryVmCapture {
    param(
        [Parameter(Mandatory = $true)][string]$CapturePath,
        [Parameter(Mandatory = $true)][string]$ExpectedCaptureSha256,
        [Parameter(Mandatory = $true)][object[]]$LockedBindings,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot
    )

    if ($ExpectedCaptureSha256 -notmatch '\A[0-9A-Fa-f]{64}\z') { throw 'Expected VM capture SHA-256 is malformed.' }
    $reportsRoot = Join-Path ([IO.Path]::GetFullPath($RepositoryRoot)) 'spoof-handoff\reports'
    $resolvedCapturePath = Assert-PathWithinRoot $CapturePath $reportsRoot 'VM registry capture'
    if (-not (Test-Path -LiteralPath $resolvedCapturePath -PathType Leaf)) { throw ('VM registry capture is missing: ' + $resolvedCapturePath) }
    $captureHash = Get-HostSha256 $resolvedCapturePath
    if ($captureHash -cne $ExpectedCaptureSha256.ToUpperInvariant()) {
        throw ('VM registry capture SHA-256 mismatch: expected=' + $ExpectedCaptureSha256.ToUpperInvariant() + ' actual=' + $captureHash)
    }
    $capture = Get-Content -LiteralPath $resolvedCapturePath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ((Get-HostProperty $capture 'schema') -cne 'vmnet-four-target-address-roundtrip.v1') { throw ('VM registry capture schema is invalid: actual=' + [string](Get-HostProperty $capture 'schema')) }
    $vmIdentity = Get-HostProperty $capture 'vmIdentity'
    $manufacturer = [string](Get-HostProperty $vmIdentity 'manufacturer')
    if ($manufacturer -cne 'VMware, Inc.') { throw ('VM registry capture manufacturer mismatch: expected=VMware, Inc. actual=' + $manufacturer) }
    $identityUuid = ConvertTo-HostVmGuidKey (Get-HostProperty $vmIdentity 'uuid') 'VM registry capture VM identity UUID'
    if ($identityUuid -cne 'D7C14D56-24D1-9894-8906-C0CE95A07790') { throw ('VM registry capture identity UUID mismatch: expected=D7C14D56-24D1-9894-8906-C0CE95A07790 actual=' + $identityUuid) }
    $journal = Get-HostProperty $capture 'stateJournal'
    $vmUuid = ConvertTo-HostVmGuidKey (Get-HostProperty $journal 'vmUuid') 'VM registry capture state journal UUID'
    if ($vmUuid -cne 'D7C14D56-24D1-9894-8906-C0CE95A07790') { throw ('VM registry capture UUID mismatch: expected=D7C14D56-24D1-9894-8906-C0CE95A07790 actual=' + $vmUuid) }
    if ($identityUuid -cne $vmUuid) { throw ('VM registry capture identity and state journal UUID values differ: identity=' + $identityUuid + ' journal=' + $vmUuid) }
    $topLevelUuid = Get-HostProperty $capture 'vmUuid'
    if ($null -ne $topLevelUuid -and -not [string]::IsNullOrWhiteSpace([string]$topLevelUuid) -and
        (ConvertTo-HostVmGuidKey $topLevelUuid 'VM registry capture top-level UUID') -cne $vmUuid) {
        throw ('VM registry capture top-level and journal UUID values differ: journal=' + $vmUuid + ' topLevel=' + [string]$topLevelUuid)
    }
    if (-not [bool](Get-HostProperty $capture 'restored')) { throw 'VM registry capture does not record a completed restore.' }
    $failures = @(Get-HostProperty $capture 'failures')
    if ($failures.Count -ne 0) { throw ('VM registry capture contains failure records: count=' + $failures.Count) }

    if ($LockedBindings.Count -ne 4) { throw ('Exactly four locked NIC bindings are required; actual=' + $LockedBindings.Count) }
    $lockedPairs = @{}
    foreach ($binding in $LockedBindings) {
        $original = ConvertTo-HostMacKey (Get-HostProperty $binding 'original')
        $replacement = ConvertTo-HostMacKey (Get-HostProperty $binding 'replacement')
        $pair = $original + '|' + $replacement
        if ($null -eq $original -or $null -eq $replacement -or $lockedPairs.ContainsKey($pair)) { throw 'Locked NIC map has malformed or duplicate original/replacement pairs.' }
        $lockedPairs[$pair] = $true
    }
    if ($lockedPairs.Count -ne 4) { throw ('Locked NIC map does not contain four distinct value pairs; actual=' + $lockedPairs.Count) }

    $targets = @(Get-HostProperty $capture 'targets')
    if ($targets.Count -ne 4) { throw ('VM registry capture must contain exactly four targets; actual=' + $targets.Count) }
    $targetByPnp = @{}
    $targetByGuid = @{}
    $targetPairs = @{}
    foreach ($target in $targets) {
        $pnp = [string](Get-HostProperty $target 'pnpDeviceId')
        if ([string]::IsNullOrWhiteSpace($pnp) -or $pnp.Length -gt 260 -or $pnp -notmatch '\A[\x20-\x7e]+\z' -or $pnp.Contains('"') -or $pnp.Contains('%')) {
            throw ('VM registry capture target has an invalid PnP identity: ' + $pnp)
        }
        $pnpKey = $pnp.ToUpperInvariant()
        $guid = ConvertTo-HostVmGuidKey (Get-HostProperty $target 'interfaceGuid') ('VM target InterfaceGuid for ' + $pnp)
        $original = ConvertTo-HostMacKey (Get-HostProperty $target 'original')
        $replacement = ConvertTo-HostMacKey (Get-HostProperty $target 'replacement')
        $pair = $original + '|' + $replacement
        if ($null -eq $original -or $null -eq $replacement -or -not $lockedPairs.ContainsKey($pair)) {
            throw ('VM registry capture target does not use an exact locked original/replacement pair: pnp=' + $pnp + ' original=' + [string](Get-HostProperty $target 'original') + ' replacement=' + [string](Get-HostProperty $target 'replacement'))
        }
        if ($targetByPnp.ContainsKey($pnpKey) -or $targetByGuid.ContainsKey($guid) -or $targetPairs.ContainsKey($pair)) {
            throw ('VM registry capture target identity or locked value pair is duplicated: pnp=' + $pnp + ' guid=' + $guid + ' pair=' + $pair)
        }
        $targetByPnp[$pnpKey] = [ordered]@{ pnpDeviceId = $pnp; interfaceGuid = $guid; original = $original; replacement = $replacement; label = [string](Get-HostProperty $target 'label'); hardwareId = [string](Get-HostProperty $target 'hardwareId') }
        $targetByGuid[$guid] = $pnpKey
        $targetPairs[$pair] = $true
    }
    if ($targetPairs.Count -ne 4 -or $lockedPairs.Count -ne $targetPairs.Count) { throw 'VM target set does not cover each of the four locked value pairs exactly once.' }

    $journalDevices = @(Get-HostProperty $journal 'devices')
    if ($journalDevices.Count -ne 2) { throw ('VM state journal must contain exactly two owned VMnet devices; actual=' + $journalDevices.Count) }
    $requiredVmnetDevices = [ordered]@{ '*VMnetAdapter1' = $true; '*VMnetAdapter8' = $true }
    $seenVmnetDevices = @{}
    $seenVmnetPnp = @{}
    foreach ($device in $journalDevices) {
        $hardwareId = [string](Get-HostProperty $device 'hardwareId')
        $instanceId = [string](Get-HostProperty $device 'instanceId')
        if (-not $requiredVmnetDevices.Contains($hardwareId) -or $seenVmnetDevices.ContainsKey($hardwareId)) {
            throw ('VM state journal contains an unexpected or duplicate owned virtual adapter: hardwareId=' + $hardwareId)
        }
        $instanceKey = $instanceId.ToUpperInvariant()
        if ([string]::IsNullOrWhiteSpace($instanceId) -or $seenVmnetPnp.ContainsKey($instanceKey) -or -not $targetByPnp.ContainsKey($instanceKey)) {
            throw ('VM state journal virtual adapter instance ID is missing, duplicated, or absent from the exact target set: hardwareId=' + $hardwareId + ' instanceId=' + $instanceId)
        }
        if ($targetByPnp[$instanceKey].hardwareId -cne $hardwareId) {
            throw ('VM state journal hardware ID differs from the exact target profile: instanceId=' + $instanceId + ' expected=' + $targetByPnp[$instanceKey].hardwareId + ' actual=' + $hardwareId)
        }
        $seenVmnetDevices[$hardwareId] = $true
        $seenVmnetPnp[$instanceKey] = $true
    }
    if ($seenVmnetDevices.Count -ne 2 -or -not $seenVmnetDevices.ContainsKey('*VMnetAdapter1') -or -not $seenVmnetDevices.ContainsKey('*VMnetAdapter8')) {
        throw 'VM state journal does not contain exactly VMnet1 and VMnet8 owned adapter records.'
    }

    $expectedHelperBundleSha256 = 'E7B1B841372B890C0D1638E6CCB11D41B5F257A64FBF491B3FEF811B29A212D8'
    $helperBundleHash = [string](Get-HostProperty $capture 'productionHelperBundleSha256')
    if ($helperBundleHash.ToUpperInvariant() -cne $expectedHelperBundleSha256) {
        throw ('VM production helper bundle SHA-256 mismatch: expected=' + $expectedHelperBundleSha256 + ' actual=' + $helperBundleHash)
    }
    $nativeExeHash = [string](Get-HostProperty $capture 'nativeExeSha256')
    if ($nativeExeHash.ToUpperInvariant() -cne $script:ExpectedNativeExeSha256) {
        throw ('VM native reader SHA-256 mismatch: expected=' + $script:ExpectedNativeExeSha256 + ' actual=' + $nativeExeHash)
    }

    $phaseModes = [ordered]@{ original = 'Original'; filteredActive = 'Replacement'; restored = 'Original' }
    $rawByPhase = [ordered]@{}
    foreach ($phaseName in @('original', 'filteredActive', 'restored')) {
        $phase = Get-HostProperty (Get-HostProperty $capture 'phases') $phaseName
        if ($null -eq $phase) { throw ('VM registry capture is missing required phase: ' + $phaseName) }
        if ([string](Get-HostProperty $phase 'mode') -cne $phaseModes[$phaseName]) {
            throw ('VM registry capture phase mode mismatch: phase=' + $phaseName + ' expected=' + $phaseModes[$phaseName] + ' actual=' + [string](Get-HostProperty $phase 'mode'))
        }
        $nativeCapture = Get-HostProperty $phase 'nativeCapture'
        if ($null -eq $nativeCapture -or [int](Get-HostProperty $nativeCapture 'exitCode') -ne 0) { throw ('VM registry native capture failed: phase=' + $phaseName + ' exitCode=' + [string](Get-HostProperty $nativeCapture 'exitCode')) }
        $nativeData = Get-HostProperty $nativeCapture 'data'
        $setup = Get-HostProperty $nativeData 'setupDiNdisLanClass'
        if ($null -eq $setup -or -not [bool](Get-HostProperty $setup 'complete') -or [string](Get-HostProperty $setup 'enumerationErrorHex') -cne '0x00000000') {
            throw ('VM registry SetupDi capture is incomplete or failed: phase=' + $phaseName + ' error=' + [string](Get-HostProperty $setup 'enumerationErrorHex'))
        }
        $allInterfaces = Get-HostVmRows $setup 'interfaces' ('VM SetupDi ' + $phaseName)
        $interfacesByPnp = @{}
        foreach ($interface in $allInterfaces) {
            $interfacePnp = [string](Get-HostProperty $interface 'pnpDeviceId')
            $interfaceKey = $interfacePnp.ToUpperInvariant()
            if ($targetByPnp.ContainsKey($interfaceKey)) {
                if ($interfacesByPnp.ContainsKey($interfaceKey)) { throw ('VM SetupDi target PnP identity is duplicated: phase=' + $phaseName + ' pnp=' + $interfacePnp) }
                $interfacesByPnp[$interfaceKey] = $interface
            }
        }
        if ($interfacesByPnp.Count -ne 4) { throw ('VM SetupDi capture does not contain every exact target once: phase=' + $phaseName + ' targets=' + $interfacesByPnp.Count) }
        foreach ($pnpKey in $targetByPnp.Keys) {
            $interface = $interfacesByPnp[$pnpKey]
            $interfaceGuid = ConvertTo-HostVmGuidKey (Get-HostProperty $interface 'netCfgInstanceId') ('SetupDi NetCfgInstanceId for ' + [string]$targetByPnp[$pnpKey].pnpDeviceId)
            if ($interfaceGuid -cne [string]$targetByPnp[$pnpKey].interfaceGuid) {
                throw ('VM SetupDi GUID differs from target profile: phase=' + $phaseName + ' pnp=' + [string]$targetByPnp[$pnpKey].pnpDeviceId + ' expected=' + [string]$targetByPnp[$pnpKey].interfaceGuid + ' actual=' + $interfaceGuid)
            }
        }

        $windowsSource = Get-HostProperty $phase 'windowsReads'
        if ($null -eq $windowsSource) { throw ('VM registry Windows reads are missing: phase=' + $phaseName) }
        $windowsRows = [ordered]@{}
        foreach ($entryName in @('win32NetworkAdapter', 'msftNetAdapter', 'getNetAdapter')) {
            $entries = Get-HostProperty $windowsSource $entryName
            if ($null -eq $entries) { throw ('VM registry Windows reader array is missing: phase=' + $phaseName + ' reader=' + $entryName) }
            $windowsRows[$entryName] = [ordered]@{ rows = @($entries); error = $null }
        }
        $mapperData = [ordered]@{
            nativeCapture = [ordered]@{ exitCode = [int](Get-HostProperty $nativeCapture 'exitCode'); data = $nativeData }
            windowsReads = $windowsRows
        }
        $rawByPhase[$phaseName] = ConvertFrom-HostVmRawCaptureData $mapperData $phaseName $phaseModes[$phaseName] $LockedBindings $script:RequiredReaders
        if ($rawByPhase[$phaseName].Count -ne 4) { throw ('VM raw mapper did not derive exactly four target rows: phase=' + $phaseName + ' count=' + $rawByPhase[$phaseName].Count) }
    }

    $actualValues = [Collections.Generic.List[object]]::new()
    foreach ($phaseName in @('original', 'filteredActive', 'restored')) {
        $phase = Get-HostProperty (Get-HostProperty $capture 'phases') $phaseName
        $observations = @(Get-HostProperty $phase 'targetObservations')
        if ($observations.Count -ne 4) { throw ('VM phase does not contain four target observations: phase=' + $phaseName + ' count=' + $observations.Count) }
        $seenPhasePnp = @{}
        foreach ($observation in $observations) {
            $target = Get-HostProperty $observation 'target'
            $pnp = [string](Get-HostProperty $target 'pnpDeviceId')
            $pnpKey = $pnp.ToUpperInvariant()
            if (-not $targetByPnp.ContainsKey($pnpKey) -or $seenPhasePnp.ContainsKey($pnpKey)) { throw ('VM phase target identity is unknown or duplicated: phase=' + $phaseName + ' pnp=' + $pnp) }
            $seenPhasePnp[$pnpKey] = $true
            $profile = $targetByPnp[$pnpKey]
            $guid = ConvertTo-HostVmGuidKey (Get-HostProperty $target 'interfaceGuid') ('VM phase target GUID for ' + $pnp)
            $original = ConvertTo-HostMacKey (Get-HostProperty $target 'original')
            $replacement = ConvertTo-HostMacKey (Get-HostProperty $target 'replacement')
            if ($guid -cne $profile.interfaceGuid -or $original -cne $profile.original -or $replacement -cne $profile.replacement) {
                throw ('VM phase target mapping drifted from the stable target set: phase=' + $phaseName + ' pnp=' + $pnp + ' expectedGuid=' + $profile.interfaceGuid + ' actualGuid=' + $guid + ' expectedPair=' + $profile.original + '|' + $profile.replacement + ' actualPair=' + $original + '|' + $replacement)
            }
            $raw = $rawByPhase[$phaseName][$pnpKey]
            if ($null -eq $raw -or $raw.interfaceGuid -cne $profile.interfaceGuid) { throw ('VM raw mapping is missing the exact phase target: phase=' + $phaseName + ' pnp=' + $pnp) }
            $observedValues = Get-HostProperty $observation 'observations'
            foreach ($reader in $script:RequiredReaders) {
                $observed = ConvertTo-HostMacKey (Get-HostProperty $observedValues $reader)
                $derived = ConvertTo-HostMacKey $raw.readers[$reader]
                if ($observed -cne $derived) { throw ('VM target observation differs from independent native/Windows raw reads: phase=' + $phaseName + ' pnp=' + $pnp + ' reader=' + $reader + ' derived=' + $derived + ' recorded=' + [string](Get-HostProperty $observedValues $reader)) }
                $expected = if ($phaseModes[$phaseName] -ceq 'Original') { $profile.original } else { $profile.replacement }
                $actualValues.Add([ordered]@{ phase = $phaseName; pnpDeviceId = $pnp; interfaceGuid = $profile.interfaceGuid; original = $profile.original; replacement = $profile.replacement; reader = $reader; expected = $expected; actual = $derived })
            }
        }
        if ($seenPhasePnp.Count -ne 4) { throw ('VM phase target identities do not cover four unique targets: phase=' + $phaseName) }
    }
    if ($actualValues.Count -ne 84) { throw ('VM raw observation count must be 84; actual=' + $actualValues.Count) }

    $checks = @(Get-HostProperty $capture 'checks')
    if ($checks.Count -ne 84) { throw ('VM capture must record exactly 84 reader checks; actual=' + $checks.Count) }
    $expectedValuesByCell = @{}
    foreach ($value in $actualValues) { $expectedValuesByCell[$value.phase + '|' + $value.pnpDeviceId.ToUpperInvariant() + '|' + $value.reader] = $value }
    $seenChecks = @{}
    foreach ($check in $checks) {
        $phaseName = [string](Get-HostProperty $check 'phase')
        $pnp = [string](Get-HostProperty $check 'pnpDeviceId')
        $reader = [string](Get-HostProperty $check 'reader')
        $cell = $phaseName + '|' + $pnp.ToUpperInvariant() + '|' + $reader
        if (-not $expectedValuesByCell.ContainsKey($cell) -or $seenChecks.ContainsKey($cell)) { throw ('VM recorded check is unknown or duplicated: ' + $cell) }
        $seenChecks[$cell] = $true
        $expected = $expectedValuesByCell[$cell]
        $checkGuid = ConvertTo-HostVmGuidKey (Get-HostProperty $check 'interfaceGuid') ('Recorded check GUID for ' + $pnp)
        $checkExpected = ConvertTo-HostMacKey (Get-HostProperty $check 'expected')
        $checkActual = ConvertTo-HostMacKey (Get-HostProperty $check 'actual')
        if ($checkGuid -cne $expected.interfaceGuid -or $checkExpected -cne (ConvertTo-HostMacKey $expected.expected) -or
            $checkActual -cne (ConvertTo-HostMacKey $expected.actual) -or [int](Get-HostProperty $check 'readerMatchCount') -ne 1 -or
            [int](Get-HostProperty $check 'readerSourceRowCount') -ne 1 -or -not [bool](Get-HostProperty $check 'pass')) {
            throw ('VM recorded check differs from independently derived observation: phase=' + $phaseName + ' pnp=' + $pnp + ' reader=' + $reader + ' expected=' + $expected.expected + ' actual=' + $expected.actual + ' recordedExpected=' + [string](Get-HostProperty $check 'expected') + ' recordedActual=' + [string](Get-HostProperty $check 'actual') + ' pass=' + [string](Get-HostProperty $check 'pass'))
        }
    }
    if ($seenChecks.Count -ne 84) { throw ('VM capture check matrix is incomplete: expected=84 actual=' + $seenChecks.Count) }

    $hostJournal = Get-HostProperty $capture 'hostAdapterJournal'
    if ($null -eq $hostJournal -or -not [bool](Get-HostProperty $hostJournal 'mutationStarted')) { throw 'VM production adapter journal does not record the address mutation cycle.' }
    $journalSnapshots = @(Get-HostProperty $hostJournal 'adapterSnapshots')
    $progressRows = @(Get-HostProperty $hostJournal 'adapterProgress')
    $restoredSnapshots = @(Get-HostProperty $capture 'restoredAdapterSnapshots')
    $linkChecks = @(Get-HostProperty $hostJournal 'linkRestoreChecks')
    foreach ($collection in @(@{ name = 'adapterSnapshots'; rows = $journalSnapshots }, @{ name = 'adapterProgress'; rows = $progressRows }, @{ name = 'restoredAdapterSnapshots'; rows = $restoredSnapshots }, @{ name = 'linkRestoreChecks'; rows = $linkChecks })) {
        if (@($collection.rows).Count -ne 4) { throw ('VM restore evidence requires four rows: collection=' + $collection.name + ' actual=' + @($collection.rows).Count) }
    }
    $snapshotByPnp = @{}
    foreach ($snapshot in $journalSnapshots) {
        $pnpKey = ([string](Get-HostProperty $snapshot 'pnpDeviceId')).ToUpperInvariant()
        if (-not $targetByPnp.ContainsKey($pnpKey) -or $snapshotByPnp.ContainsKey($pnpKey)) { throw ('VM journal snapshot identity is unknown or duplicated: pnp=' + $pnpKey) }
        $target = $targetByPnp[$pnpKey]
        $guid = ConvertTo-HostVmGuidKey (Get-HostProperty $snapshot 'interfaceGuid') ('VM journal snapshot GUID for ' + $pnpKey)
        if ($guid -cne $target.interfaceGuid -or (ConvertTo-HostMacKey (Get-HostProperty $snapshot 'original')) -cne $target.original -or
            (ConvertTo-HostMacKey (Get-HostProperty $snapshot 'replacement')) -cne $target.replacement) {
            throw ('VM adapter journal snapshot differs from the target map: pnp=' + $pnpKey)
        }
        $snapshotByPnp[$pnpKey] = $snapshot
    }
    $progressByPnp = @{}
    foreach ($progress in $progressRows) {
        $pnpKey = ([string](Get-HostProperty $progress 'pnpDeviceId')).ToUpperInvariant()
        if (-not $snapshotByPnp.ContainsKey($pnpKey) -or $progressByPnp.ContainsKey($pnpKey) -or
            [string](Get-HostProperty $progress 'restoreState') -cne 'verified') {
            throw ('VM adapter journal restore progress is missing, duplicated, or not verified: pnp=' + $pnpKey + ' state=' + [string](Get-HostProperty $progress 'restoreState'))
        }
        $snapshot = $snapshotByPnp[$pnpKey]
        $expectedRegistry = Get-HostProperty $snapshot 'networkAddress'
        $actualRegistry = Get-HostProperty $progress 'restoreRegistry'
        if (-not (Test-NicAddressRegistryValueSnapshotEqual $expectedRegistry $actualRegistry)) {
            throw ('VM typed NetworkAddress restore mismatch: pnp=' + $pnpKey + ' expected=' + (ConvertTo-Json -InputObject $expectedRegistry -Compress -Depth 8) + ' actual=' + (ConvertTo-Json -InputObject $actualRegistry -Compress -Depth 8))
        }
        $target = $targetByPnp[$pnpKey]
        if ((ConvertTo-HostMacKey (Get-HostProperty $progress 'restoreActualMac')) -cne $target.original) {
            throw ('VM restore journal MAC mismatch: pnp=' + $pnpKey + ' expected=' + $target.original + ' actual=' + [string](Get-HostProperty $progress 'restoreActualMac'))
        }
        $snapshotAdminStatus = [string](Get-HostProperty (Get-HostProperty $snapshot 'adminState') 'value')
        $restoreAdminStatus = [string](Get-HostProperty $progress 'restoreAdminStatus')
        if ([string]::IsNullOrWhiteSpace($restoreAdminStatus) -or $restoreAdminStatus -cne $snapshotAdminStatus) {
            throw ('VM restore journal admin status mismatch: pnp=' + $pnpKey + ' expected=' + $snapshotAdminStatus + ' actual=' + $restoreAdminStatus)
        }
        $progressByPnp[$pnpKey] = $progress
    }
    $restoredByPnp = @{}
    foreach ($snapshot in $restoredSnapshots) {
        $pnpKey = ([string](Get-HostProperty $snapshot 'pnpDeviceId')).ToUpperInvariant()
        if (-not $snapshotByPnp.ContainsKey($pnpKey) -or $restoredByPnp.ContainsKey($pnpKey)) { throw ('VM restored adapter snapshot identity is unknown or duplicated: pnp=' + $pnpKey) }
        $baseline = $snapshotByPnp[$pnpKey]
        $guid = ConvertTo-HostVmGuidKey (Get-HostProperty $snapshot 'interfaceGuid') ('VM restored snapshot GUID for ' + $pnpKey)
        if ($guid -cne [string]$baseline.interfaceGuid -or
            -not (Test-NicAddressRegistryValueSnapshotEqual (Get-HostProperty $baseline 'networkAddress') (Get-HostProperty $snapshot 'networkAddress'))) {
            throw ('VM restored adapter snapshot differs from its typed baseline: pnp=' + $pnpKey)
        }
        $target = $targetByPnp[$pnpKey]
        $observedOriginal = ConvertTo-HostMacKey (Get-HostProperty $snapshot 'observedOriginalMac')
        $baselineObservedOriginal = ConvertTo-HostMacKey (Get-HostProperty $baseline 'observedOriginalMac')
        $snapshotEnabled = Get-HostProperty (Get-HostProperty $snapshot 'adminState') 'enabled'
        $baselineEnabled = Get-HostProperty (Get-HostProperty $baseline 'adminState') 'enabled'
        $snapshotClassKey = [string](Get-HostProperty $snapshot 'registrySubKey')
        $baselineClassKey = [string](Get-HostProperty $baseline 'registrySubKey')
        if ($observedOriginal -cne $target.original -or $baselineObservedOriginal -cne $target.original -or
            $snapshotEnabled -ne $baselineEnabled -or $snapshotClassKey -cne $baselineClassKey) {
            throw ('VM restored adapter identity/state differs from target baseline: pnp=' + $pnpKey + ' expectedMac=' + $target.original + ' snapshotMac=' + [string](Get-HostProperty $snapshot 'observedOriginalMac') + ' baselineMac=' + [string](Get-HostProperty $baseline 'observedOriginalMac') + ' expectedAdminEnabled=' + [string]$baselineEnabled + ' actualAdminEnabled=' + [string]$snapshotEnabled + ' expectedRegistrySubKey=' + $baselineClassKey + ' actualRegistrySubKey=' + $snapshotClassKey)
        }
        $restoredByPnp[$pnpKey] = $snapshot
    }
    $linksByPnp = @{}
    foreach ($link in $linkChecks) {
        $pnpKey = ([string](Get-HostProperty $link 'pnpDeviceId')).ToUpperInvariant()
        if (-not $snapshotByPnp.ContainsKey($pnpKey) -or $linksByPnp.ContainsKey($pnpKey) -or
            -not [bool](Get-HostProperty $link 'matchesBaseline') -or [bool](Get-HostProperty $link 'timedOut') -or
            $null -ne (Get-HostProperty $link 'error')) {
            throw ('VM adapter link restore check failed: pnp=' + $pnpKey + ' matchesBaseline=' + [string](Get-HostProperty $link 'matchesBaseline') + ' timedOut=' + [string](Get-HostProperty $link 'timedOut') + ' error=' + [string](Get-HostProperty $link 'error'))
        }
        $baseline = $snapshotByPnp[$pnpKey]
        $guid = ConvertTo-HostVmGuidKey (Get-HostProperty $link 'interfaceGuid') ('VM link restore GUID for ' + $pnpKey)
        if ($guid -cne [string]$baseline.interfaceGuid -or [bool](Get-HostProperty $link 'expectedConnected') -ne [bool](Get-HostProperty $baseline 'linkConnected') -or
            [bool](Get-HostProperty $link 'actualConnected') -ne [bool](Get-HostProperty $baseline 'linkConnected')) {
            throw ('VM adapter link state differs from baseline: pnp=' + $pnpKey + ' expectedConnected=' + [string](Get-HostProperty $baseline 'linkConnected') + ' actualConnected=' + [string](Get-HostProperty $link 'actualConnected'))
        }
        $linksByPnp[$pnpKey] = $link
    }
    if ($snapshotByPnp.Count -ne 4 -or $progressByPnp.Count -ne 4 -or $restoredByPnp.Count -ne 4 -or $linksByPnp.Count -ne 4) {
        throw 'VM typed-registry, adapter restore, or link-state evidence is incomplete.'
    }

    return [ordered]@{
        schema = 'nic-vm-registry-preflight.v1'
        status = 'VM_FOUR_TARGET_REGISTRY_CAPTURE_PASS'
        capturePath = $resolvedCapturePath
        captureSha256 = $captureHash
        vmUuid = $vmUuid
        targetCount = $targetByPnp.Count
        phaseCount = $phaseModes.Count
        readerCount = $script:RequiredReaders.Count
        actualValueCount = $actualValues.Count
        actualValues = $actualValues.ToArray()
        recordedChecksMatched = $seenChecks.Count
        typedRegistryRestoreCount = $progressByPnp.Count
        restoredAdapterSnapshotCount = $restoredByPnp.Count
        restoredLinkCheckCount = $linksByPnp.Count
        productionHelperBundleSha256 = $helperBundleHash.ToUpperInvariant()
        nativeExeSha256 = $nativeExeHash.ToUpperInvariant()
    }
}

function Assert-HostVmnetAdapterDriverPin {
    $systemRoot = [string]$env:SystemRoot
    if ([string]::IsNullOrWhiteSpace($systemRoot)) { throw 'SystemRoot is unavailable; cannot verify the pinned VMware adapter driver.' }
    $driverPath = Join-Path $systemRoot 'System32\drivers\vmnetadapter.sys'
    if (-not (Test-Path -LiteralPath $driverPath -PathType Leaf)) { throw ('Pinned VMware adapter driver is missing: ' + $driverPath) }
    $driverHash = Get-HostSha256 $driverPath
    if ($driverHash -cne $script:ExpectedVmnetAdapterDriverSha256) {
        throw ('VMware adapter driver SHA-256 mismatch: expected=' + $script:ExpectedVmnetAdapterDriverSha256 + ' actual=' + $driverHash + ' path=' + $driverPath)
    }
    return [ordered]@{ path = $driverPath; sha256 = $driverHash; expectedSha256 = $script:ExpectedVmnetAdapterDriverSha256; verified = $true }
}

function Assert-HostVmGate {
    param([Parameter(Mandatory = $true)][string]$GatePath, [Parameter(Mandatory = $true)][string]$RepositoryRoot, [Parameter(Mandatory = $true)][string]$TsvPath, [Parameter(Mandatory = $true)][string]$NativeExePath)
    $expectedGate = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'spoof-handoff\reports\nic-reader-gap-20261017\vm-seven-reader-gate.json'))
    $resolvedGate = [IO.Path]::GetFullPath($GatePath)
    if (-not [string]::Equals($resolvedGate, $expectedGate, [StringComparison]::OrdinalIgnoreCase)) { throw ('VM gate must be the sealed repository gate at ' + $expectedGate) }
    if (-not (Test-Path -LiteralPath $resolvedGate -PathType Leaf)) { throw ('Required VM validation gate is not present: ' + $resolvedGate) }
    $actualGateHash = Get-HostSha256 $resolvedGate
    if ($actualGateHash -cne $script:ExpectedVmGateSha256) { throw ('VM gate SHA-256 differs from the reviewed gate: expected=' + $script:ExpectedVmGateSha256 + ' actual=' + $actualGateHash) }
    $gate = Get-Content -LiteralPath $resolvedGate -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ((Get-HostProperty $gate 'schema') -cne 'nic-vm-host-gate.v1' -or (Get-HostProperty $gate 'status') -cne 'VM_SEVEN_READER_ROUND_TRIP_PASS') { throw 'VM gate schema or success status is invalid.' }
    if ([int](Get-HostProperty $gate 'adapters') -ne 4) { throw ('VM gate adapter count must be 4; actual=' + [string](Get-HostProperty $gate 'adapters')) }
    $gateReaders = @((Get-HostProperty $gate 'readers') | ForEach-Object { [string]$_ } | Sort-Object -Unique)
    $requiredReaders = @($script:RequiredReaders | Sort-Object -Unique)
    if ($gateReaders.Count -ne $requiredReaders.Count -or @($gateReaders | Where-Object { $requiredReaders -cnotcontains $_ }).Count -ne 0) { throw ('VM gate reader set is incomplete or unexpected: ' + ($gateReaders -join ', ')) }
    if (-not [bool](Get-HostProperty $gate 'vmRecoveredAndPoweredOff')) { throw 'VM gate does not prove that the disposable VM was recovered and powered off.' }
    $tsvHash = Get-HostSha256 $TsvPath
    if ($tsvHash -cne $script:ExpectedLockedTsvSha256 -or ([string](Get-HostProperty $gate 'lockedTsvSha256')).ToUpperInvariant() -cne $tsvHash) { throw ('VM gate locked TSV hash mismatch: pinned=' + $script:ExpectedLockedTsvSha256 + ' gate=' + [string](Get-HostProperty $gate 'lockedTsvSha256') + ' actual=' + $tsvHash) }
    $nativeHash = Get-HostSha256 $NativeExePath
    if ($nativeHash -cne $script:ExpectedNativeExeSha256 -or ([string](Get-HostProperty $gate 'nativeExeSha256')).ToUpperInvariant() -cne $nativeHash) {
        throw ('Native reader hash mismatch: pinned=' + $script:ExpectedNativeExeSha256 + ' actual=' + $nativeHash + ' gate=' + [string](Get-HostProperty $gate 'nativeExeSha256'))
    }
    $locked = @(Get-HostLockedBindings $TsvPath)
    $captureMap = [ordered]@{}
    $rawCaptureMap = [ordered]@{}
    $captures = Get-HostProperty $gate 'captures'
    $phaseModes = [ordered]@{ original = 'Original'; filteredActive = 'Replacement'; unfilteredActive = 'Replacement'; restored = 'Original' }
    foreach ($name in @('original', 'filteredActive', 'unfilteredActive', 'restored')) {
        $captureMap[$name] = Get-HostGateCapture (Get-HostProperty $captures $name) $RepositoryRoot $name
        $rawCaptureMap[$name] = Get-HostVmRawCaptureValues (Get-HostProperty $captures $name) $name $phaseModes[$name] $locked $requiredReaders $RepositoryRoot
    }
    $recovery = Get-HostGateCapture (Get-HostProperty $gate 'recoveryEvidence') $RepositoryRoot 'recovery'
    $recoveryData = Get-Content -LiteralPath (Join-Path $RepositoryRoot $recovery.path.Replace('/', '\')) -Raw | ConvertFrom-Json
    if ((Get-HostProperty $recoveryData 'status') -cne 'RECOVERED_AND_POWERED_OFF') { throw 'VM recovery evidence does not state RECOVERED_AND_POWERED_OFF.' }

    $lockedPairs = @{}
    foreach ($binding in $locked) { $lockedPairs[(ConvertTo-HostMacKey $binding.original)] = (ConvertTo-HostMacKey $binding.replacement) }
    $proofs = @(Get-HostProperty $gate 'proofs')
    if ($proofs.Count -ne 28) { throw ('VM gate must contain 28 reader proofs; actual=' + $proofs.Count) }
    $seen = @{}
    $pnpMappings = @{}
    foreach ($proof in $proofs) {
        $pnp = [string](Get-HostProperty $proof 'pnpDeviceId')
        $reader = [string](Get-HostProperty $proof 'reader')
        $key = $pnp.ToUpperInvariant() + '|' + $reader
        if ($seen.ContainsKey($key)) { throw ('Duplicate VM proof row: ' + $key) }
        $seen[$key] = $true
        if ($requiredReaders -cnotcontains $reader) { throw ('Unexpected VM proof reader: ' + $reader) }
        $original = ConvertTo-HostMacKey (Get-HostProperty $proof 'original')
        $replacement = ConvertTo-HostMacKey (Get-HostProperty $proof 'expectedReplacement')
        if ($null -eq $original -or -not $lockedPairs.ContainsKey($original) -or $lockedPairs[$original] -cne $replacement) {
            throw ('VM proof does not map to an exact approved replacement: pnp=' + $pnp + ' reader=' + $reader + ' original=' + [string](Get-HostProperty $proof 'original') + ' expectedReplacement=' + [string](Get-HostProperty $proof 'expectedReplacement'))
        }
        $pnpKey = $pnp.ToUpperInvariant()
        if ($pnpMappings.ContainsKey($pnpKey)) {
            if ($pnpMappings[$pnpKey].original -cne $original -or $pnpMappings[$pnpKey].replacement -cne $replacement) {
                throw ('VM adapter proof rows disagree about the locked address pair: pnp=' + $pnp + ' reader=' + $reader)
            }
        }
        else { $pnpMappings[$pnpKey] = [ordered]@{ original = $original; replacement = $replacement } }
        $observed = @(
            (Get-HostProperty $proof 'activeObserved'),
            (Get-HostProperty $proof 'restored'),
            (Get-HostProperty $proof 'unfilteredObserved')
        )
        $expected = @($replacement, $original, $replacement)
        for ($index = 0; $index -lt 3; $index++) {
        $values = @($observed[$index])
            if ($values.Count -eq 0 -or @($values | Where-Object { (ConvertTo-HostMacKey $_) -cne $expected[$index] }).Count -gt 0) {
                throw ('VM proof raw-value mismatch: pnp=' + $pnp + ' reader=' + $reader + ' phase=' + $index + ' expected=' + $expected[$index] + ' actual=' + (ConvertTo-Json -InputObject $observed[$index] -Compress -Depth 6))
            }
        }
        $rawOriginal = $rawCaptureMap.original.values[$pnp.ToUpperInvariant()]
        $rawFiltered = $rawCaptureMap.filteredActive.values[$pnp.ToUpperInvariant()]
        $rawUnfiltered = $rawCaptureMap.unfilteredActive.values[$pnp.ToUpperInvariant()]
        $rawRestored = $rawCaptureMap.restored.values[$pnp.ToUpperInvariant()]
        if ($null -eq $rawOriginal -or $null -eq $rawFiltered -or $null -eq $rawUnfiltered -or $null -eq $rawRestored) { throw ('VM proof PnP identity is absent from a raw phase capture: ' + $pnp) }
        if ((ConvertTo-HostMacKey $rawOriginal.original) -cne $original -or (ConvertTo-HostMacKey $rawOriginal.replacement) -cne $replacement -or
            (ConvertTo-HostMacKey $rawFiltered.readers[$reader]) -cne $replacement -or
            (ConvertTo-HostMacKey $rawRestored.readers[$reader]) -cne $original -or
            (ConvertTo-HostMacKey $rawUnfiltered.readers[$reader]) -cne $replacement) {
            throw ('VM proof does not match recomputed raw capture observations: pnp=' + $pnp + ' reader=' + $reader)
        }
        foreach ($flag in @('unfilteredMatches', 'originalMatches', 'replacementMatches', 'restoredMatches', 'roundTripPass')) {
            if (-not [bool](Get-HostProperty $proof $flag)) { throw ('VM proof did not pass its recorded predicate: pnp=' + $pnp + ' reader=' + $reader + ' flag=' + $flag) }
        }
    }
    $gatePnpIds = @($proofs | ForEach-Object { ([string](Get-HostProperty $_ 'pnpDeviceId')).ToUpperInvariant() } | Sort-Object -Unique)
    if ($gatePnpIds.Count -ne 4) { throw ('VM proof matrix must contain four distinct guest adapter identities; actual=' + $gatePnpIds.Count) }
    $hostPnpIds = @($script:ExpectedHostBindings | ForEach-Object { ([string]$_.pnpDeviceId).ToUpperInvariant() })
    foreach ($guestPnp in $gatePnpIds) {
        if ($hostPnpIds -contains $guestPnp) { throw ('VM gate contains a production-host adapter identity: ' + $guestPnp) }
    }
    $gatePairKeys = @($pnpMappings.Values | ForEach-Object { [string]$_.original + '|' + [string]$_.replacement } | Sort-Object -Unique)
    $lockedPairKeys = @($locked | ForEach-Object { (ConvertTo-HostMacKey $_.original) + '|' + (ConvertTo-HostMacKey $_.replacement) } | Sort-Object -Unique)
    if (($gatePairKeys -join "`n") -cne ($lockedPairKeys -join "`n")) { throw ('VM gate adapter-to-value mapping differs from the four locked host pairs: actual=' + ($gatePairKeys -join ', ')) }
    foreach ($pnpId in $gatePnpIds) {
        foreach ($reader in $requiredReaders) {
            $key = $pnpId + '|' + $reader
            if (-not $seen.ContainsKey($key)) { throw ('VM gate is missing a matrix cell: ' + $key) }
        }
    }
    return [ordered]@{
        path = $resolvedGate
        sha256 = $actualGateHash
        tsvSha256 = $tsvHash
        nativeExeSha256 = $nativeHash
        captures = $captureMap
        recoveryEvidence = $recovery
        proofCount = $proofs.Count
        adapters = 4
        readers = $requiredReaders
    }
}

function Get-HostProcessCapture {
    param([Parameter(Mandatory = $true)][string]$Program, [Parameter(Mandatory = $true)][string[]]$Arguments)
    $quoted = @($Arguments | ForEach-Object { '"' + ([string]$_).Replace('"', '\"') + '"' }) -join ' '
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.Arguments = $quoted
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $timedOut = $false
    $killError = $null
    $stdout = ''
    $stderr = ''
    $stdoutComplete = $false
    $stderrComplete = $false
    try {
        if (-not $process.Start()) { throw ('Could not start ' + $Program) }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(120000)) {
            $timedOut = $true
            try { $process.Kill() }
            catch { $killError = $_.Exception.ToString() }
            [void]$process.WaitForExit(15000)
        }
        try { $stdoutComplete = [bool]$stdoutTask.Wait(5000); if ($stdoutComplete) { $stdout = [string]$stdoutTask.Result } }
        catch { $stdout = '<stdout capture failed: ' + $_.Exception.ToString() + '>' }
        try { $stderrComplete = [bool]$stderrTask.Wait(5000); if ($stderrComplete) { $stderr = [string]$stderrTask.Result } }
        catch { $stderr = '<stderr capture failed: ' + $_.Exception.ToString() + '>' }
        if (-not $stdoutComplete -and [string]::IsNullOrEmpty($stdout)) { $stdout = '<stdout was not fully available before the bounded capture deadline>' }
        if (-not $stderrComplete -and [string]::IsNullOrEmpty($stderr)) { $stderr = '<stderr was not fully available before the bounded capture deadline>' }
        $exitCode = if ($process.HasExited) { [int]$process.ExitCode } else { $null }
        return [ordered]@{
            exitCode = $exitCode
            stdout = $stdout
            stderr = $stderr
            program = $Program
            arguments = $Arguments
            timedOut = $timedOut
            timeoutMilliseconds = 120000
            killError = $killError
            processExited = [bool]$process.HasExited
            stdoutComplete = $stdoutComplete
            stderrComplete = $stderrComplete
        }
    }
    finally { $process.Dispose() }
}

function Test-HostManagedBcdFile {
    param([Parameter(Mandatory = $true)][string]$RelativePath, [bool]$IsSystemPartition)
    if (-not $IsSystemPartition) { return $false }
    return $RelativePath.Replace('\', '/') -imatch '\AEFI/Microsoft/Boot/BCD(?:\.LOG[12]?)?\z'
}

function Assert-HostBcdSnapshotUsable {
    param([AllowNull()][object]$Snapshot)
    if ($null -eq $Snapshot) { throw 'A supported BCD snapshot is required before classifying the live BCD hive.' }
    foreach ($name in @('current', 'all', 'firmware', 'exportedAll')) {
        $entry = Get-HostProperty $Snapshot $name
        if ($null -eq $entry -or [int](Get-HostProperty $entry 'exitCode') -ne 0 -or
            [bool](Get-HostProperty $entry 'timedOut') -or [string]::IsNullOrWhiteSpace([string](Get-HostProperty $entry 'stdout'))) {
            throw ('BCD snapshot requires a successful complete enumeration: ' + $name)
        }
    }
    $export = Get-HostProperty $Snapshot 'export'
    if ($null -eq $export -or [int](Get-HostProperty $export 'exitCode') -ne 0 -or
        [string]::IsNullOrWhiteSpace([string](Get-HostProperty $export 'path')) -or
        [string](Get-HostProperty $export 'sha256') -notmatch '\A[0-9A-Fa-f]{64}\z' -or [long](Get-HostProperty $export 'bytes') -le 0) {
        throw 'A successful hash-pinned BCD export is required for the live hive snapshot.'
    }
}

function Get-HostEspSnapshot {
    param([Parameter(Mandatory = $true)][string]$EvidenceDirectory, [AllowNull()][object]$BcdSnapshot)
    $espType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
    $partitions = @(Get-Partition -ErrorAction Stop | Where-Object { [string]$_.GptType -ieq $espType })
    if ($partitions.Count -eq 0) { throw 'No EFI System Partition was found by its GPT type.' }
    $results = [Collections.Generic.List[object]]::new()
    foreach ($partition in $partitions) {
        $accessPaths = @($partition.AccessPaths | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) })
        $rootPath = $null
        foreach ($candidate in $accessPaths) {
            if (Test-Path -LiteralPath ([string]$candidate) -PathType Container) { $rootPath = [string]$candidate; break }
        }
        if (-not $rootPath) { throw ('EFI partition is not reachable through an existing access path; refusing to mount or assign a drive letter. Disk=' + $partition.DiskNumber + ' Partition=' + $partition.PartitionNumber + ' AccessPaths=' + ($accessPaths -join ';')) }
        $fileRecords = [Collections.Generic.List[object]]::new()
        $pending = [Collections.Generic.Queue[string]]::new()
        $pending.Enqueue($rootPath)
        while ($pending.Count -gt 0) {
            $directory = $pending.Dequeue()
            foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)) {
                if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('Unexpected reparse point in the ESP snapshot: ' + $item.FullName) }
                if ($item.PSIsContainer) { $pending.Enqueue($item.FullName); continue }
                $relative = $item.FullName.Substring($rootPath.Length).TrimStart('\') -replace '\\', '/'
                $systemPartition = [bool](Get-HostProperty $partition 'IsSystem')
                if (Test-HostManagedBcdFile $relative $systemPartition) {
                    Assert-HostBcdSnapshotUsable $BcdSnapshot
                    $export = Get-HostProperty $BcdSnapshot 'export'
                    $fileRecords.Add([ordered]@{
                        path = $relative; bytes = [long]$item.Length; sha256 = $null
                        verification = 'bcdedit-system-store'; rawBytesCaptured = $false
                        exportPath = [string](Get-HostProperty $export 'path'); exportSha256 = [string](Get-HostProperty $export 'sha256')
                        note = 'Windows owns the live BCD hive and transaction logs. Their raw bytes are not read or claimed identical; live and exported-store enumerations are compared.'
                    })
                }
                elseif ([string]::Equals($relative, 'EFI/Microsoft/Boot/BOOTSTAT.DAT', [StringComparison]::OrdinalIgnoreCase)) {
                    $fileRecords.Add([ordered]@{ path = $relative; bytes = [long]$item.Length; sha256 = $null; verification = 'mutable-windows-boot-status'; rawBytesCaptured = $false })
                }
                else { $fileRecords.Add([ordered]@{ path = $relative; bytes = [long]$item.Length; sha256 = Get-HostSha256 $item.FullName; verification = 'file-sha256'; rawBytesCaptured = $true }) }
            }
        }
        $sorted = @($fileRecords.ToArray() | Sort-Object path)
        $results.Add([ordered]@{
            diskNumber = [int]$partition.DiskNumber
            partitionNumber = [int]$partition.PartitionNumber
            partitionGuid = [string]$partition.Guid
            offset = [long]$partition.Offset
            size = [long]$partition.Size
            isReadOnly = [bool]$partition.IsReadOnly
            isSystem = [bool](Get-HostProperty $partition 'IsSystem')
            accessPath = $rootPath
            files = $sorted
        })
    }
    return @($results.ToArray())
}

function Get-HostFirmwareVariableSnapshot {
    param([Parameter(Mandatory = $true)][string]$EvidenceDirectory)
    if (-not ('NicHostFirmwareReader' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class NicHostFirmwareReader {
    const uint TOKEN_QUERY = 0x0008, TOKEN_ADJUST_PRIVILEGES = 0x0020, SE_PRIVILEGE_ENABLED = 0x00000002;
    const int ERROR_NOT_ALL_ASSIGNED = 1300, ERROR_ENVVAR_NOT_FOUND = 203;
    [StructLayout(LayoutKind.Sequential)] struct Luid { public uint LowPart; public int HighPart; }
    [StructLayout(LayoutKind.Sequential)] struct LuidAndAttributes { public Luid Luid; public uint Attributes; }
    [StructLayout(LayoutKind.Sequential)] struct TokenPrivileges { public uint PrivilegeCount; public LuidAndAttributes Privileges; }
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool LookupPrivilegeValueW(string systemName, string name, out Luid luid);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TokenPrivileges requested, uint length, out TokenPrivileges previous, out uint returned);
    [DllImport("kernel32.dll", SetLastError=true, EntryPoint="SetLastError")] static extern void ClearLastError(uint error);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="GetFirmwareEnvironmentVariableExW")] static extern uint GetFirmwareEnvironmentVariableEx(string name, string guid, byte[] buffer, uint size, out uint attributes);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
    public static object Read(string name) {
        const string guid = "{8be4df61-93ca-11d2-aa0d-00e098032b8c}";
        IntPtr token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, out token)) {
            int error = Marshal.GetLastWin32Error();
            return new { state="error", error=error, errorText=new Win32Exception(error).Message, privilegeRestoreError=0, bytes=(byte[])null, attributes=(uint?)null };
        }
        try {
            Luid luid;
            if (!LookupPrivilegeValueW(null, "SeSystemEnvironmentPrivilege", out luid)) {
                int error = Marshal.GetLastWin32Error();
                return new { state="error", error=error, errorText=new Win32Exception(error).Message, privilegeRestoreError=0, bytes=(byte[])null, attributes=(uint?)null };
            }
            TokenPrivileges request = new TokenPrivileges(); request.PrivilegeCount=1; request.Privileges.Luid=luid; request.Privileges.Attributes=SE_PRIVILEGE_ENABLED;
            TokenPrivileges previous; uint previousSize;
            ClearLastError(0);
            if (!AdjustTokenPrivileges(token, false, ref request, (uint)Marshal.SizeOf(typeof(TokenPrivileges)), out previous, out previousSize)) {
                int error = Marshal.GetLastWin32Error();
                return new { state="error", error=error, errorText=new Win32Exception(error).Message, privilegeRestoreError=0, bytes=(byte[])null, attributes=(uint?)null };
            }
            int assignError = Marshal.GetLastWin32Error();
            if (assignError == ERROR_NOT_ALL_ASSIGNED) {
                TokenPrivileges ignored; uint ignoredSize; ClearLastError(0);
                AdjustTokenPrivileges(token, false, ref previous, (uint)Marshal.SizeOf(typeof(TokenPrivileges)), out ignored, out ignoredSize);
                int privilegeRestoreError = Marshal.GetLastWin32Error();
                return new { state="error", error=assignError, errorText=new Win32Exception(assignError).Message, privilegeRestoreError=privilegeRestoreError, bytes=(byte[])null, attributes=(uint?)null };
            }
            byte[] data = null; uint attributes = 0; int readError = 0, restoreError = 0;
            try {
                byte[] buffer = new byte[4096];
                ClearLastError(0);
                uint size = GetFirmwareEnvironmentVariableEx(name, guid, buffer, (uint)buffer.Length, out attributes);
                if (size == 0) {
                    readError = Marshal.GetLastWin32Error();
                    if (readError == ERROR_ENVVAR_NOT_FOUND) data = new byte[0];
                } else if (size > buffer.Length) {
                    readError = 122;
                } else {
                    data = new byte[(int)size]; Array.Copy(buffer, data, (int)size);
                }
            } finally {
                TokenPrivileges ignored; uint ignoredSize;
                ClearLastError(0);
                if (!AdjustTokenPrivileges(token, false, ref previous, (uint)Marshal.SizeOf(typeof(TokenPrivileges)), out ignored, out ignoredSize)) {
                    restoreError = Marshal.GetLastWin32Error();
                }
                else {
                    restoreError = Marshal.GetLastWin32Error();
                }
            }
            if (restoreError != 0) return new { state="error", error=readError, errorText=(readError == 0 ? null : new Win32Exception(readError).Message), privilegeRestoreError=restoreError, bytes=data, attributes=(uint?)attributes };
            if (readError == ERROR_ENVVAR_NOT_FOUND) return new { state="absent", error=readError, errorText=new Win32Exception(readError).Message, privilegeRestoreError=0, bytes=data, attributes=(uint?)attributes };
            if (readError != 0) return new { state="error", error=readError, errorText=new Win32Exception(readError).Message, privilegeRestoreError=0, bytes=data, attributes=(uint?)attributes };
            return new { state="present", error=0, errorText=(string)null, privilegeRestoreError=0, bytes=data, attributes=(uint?)attributes };
        } finally { CloseHandle(token); }
    }
}
'@
    }
    $names = @('SecureBoot', 'SetupMode', 'BootOrder', 'BootNext', 'BootCurrent')
    $records = [ordered]@{}
    $directory = Join-Path $EvidenceDirectory 'firmware-variables'
    [void][IO.Directory]::CreateDirectory($directory)
    foreach ($name in $names) {
        try {
            $variable = [NicHostFirmwareReader]::Read($name)
            if ([string]$variable.state -ceq 'error') { throw ('GetFirmwareEnvironmentVariableExW failed: Win32=' + $variable.error + ' ' + [string]$variable.errorText + ' privilegeRestoreError=' + $variable.privilegeRestoreError) }
            if ([int]$variable.privilegeRestoreError -ne 0) { throw ('SeSystemEnvironmentPrivilege restoration failed: Win32=' + $variable.privilegeRestoreError) }
            if ([string]$variable.state -ceq 'absent') {
                $records[$name] = [ordered]@{ state = 'absent'; guid = $script:ExpectedSecureBootVariableGuid; win32Error = [int]$variable.error; error = [string]$variable.errorText; privilegeRestored = $true }
                continue
            }
            $bytes = [byte[]]$variable.bytes
            $path = Join-Path $directory ($name + '.bin')
            [IO.File]::WriteAllBytes($path, $bytes)
            $records[$name] = [ordered]@{
                state = 'present'
                guid = $script:ExpectedSecureBootVariableGuid
                attributes = [uint32]$variable.attributes
                size = [long]$bytes.Length
                bytesPath = $path
                bytesSha256 = Get-HostSha256 $path
                bytesHex = [BitConverter]::ToString($bytes).Replace('-', '')
                win32Error = [int]$variable.error
                privilegeRestored = $true
            }
        }
        catch {
            $records[$name] = [ordered]@{ state = 'unavailable'; errorType = $_.Exception.GetType().FullName; error = $_.Exception.ToString() }
        }
    }
    if ($records.SecureBoot.state -cne 'present' -or $records.SecureBoot.size -ne 1 -or $records.SecureBoot.bytesHex -cne '01') {
        throw ('SecureBoot firmware variable must be one byte 01; actual=' + (ConvertTo-Json -InputObject $records.SecureBoot -Compress -Depth 5))
    }
    if ($records.SetupMode.state -cne 'present' -or $records.SetupMode.size -ne 1 -or $records.SetupMode.bytesHex -cne '00') {
        throw ('SetupMode firmware variable must be one byte 00 while Secure Boot is enabled; actual=' + (ConvertTo-Json -InputObject $records.SetupMode -Compress -Depth 5))
    }
    if ($records.BootOrder.state -cne 'present') { throw ('BootOrder could not be snapshotted: ' + (ConvertTo-Json -InputObject $records.BootOrder -Compress -Depth 5)) }
    foreach ($name in @('SetupMode', 'BootNext', 'BootCurrent')) {
        if ($records[$name].state -cne 'present' -and $records[$name].state -cne 'absent') { throw ($name + ' firmware variable could not be read: ' + (ConvertTo-Json -InputObject $records[$name] -Compress -Depth 5)) }
    }
    return $records
}

function Get-HostBcdSnapshot {
    param([Parameter(Mandatory = $true)][string]$EvidenceDirectory)
    [void][IO.Directory]::CreateDirectory($EvidenceDirectory)
    $bcdedit = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
    $records = [ordered]@{}
    foreach ($name in @('current', 'all', 'firmware')) {
        $args = if ($name -ceq 'current') { @('/enum', '{current}', '/v') } elseif ($name -ceq 'all') { @('/enum', 'all', '/v') } else { @('/enum', 'firmware', '/v') }
        $capture = Get-HostProcessCapture $bcdedit $args
        $records[$name] = $capture
        if ($capture.timedOut -or $capture.exitCode -ne 0) { throw ('bcdedit /enum ' + $name + ' failed exit=' + $capture.exitCode + ' timedOut=' + $capture.timedOut + ' killError=' + $capture.killError + ' stdout=' + $capture.stdout + ' stderr=' + $capture.stderr) }
    }
    $exportPath = Join-Path $EvidenceDirectory 'boot-store-export.bcd'
    $export = Get-HostProcessCapture $bcdedit @('/export', $exportPath)
    $records.export = [ordered]@{ exitCode = $export.exitCode; stdout = $export.stdout; stderr = $export.stderr; path = $exportPath }
    if ($export.timedOut -or $export.exitCode -ne 0 -or -not (Test-Path -LiteralPath $exportPath -PathType Leaf)) {
        throw ('bcdedit /export failed exit=' + $export.exitCode + ' timedOut=' + $export.timedOut + ' killError=' + $export.killError + ' stdout=' + $export.stdout + ' stderr=' + $export.stderr)
    }
    $records.export.sha256 = Get-HostSha256 $exportPath
    $records.export.bytes = [long](Get-Item -LiteralPath $exportPath).Length
    $records.exportedAll = Get-HostProcessCapture $bcdedit @('/store', $exportPath, '/enum', 'all', '/v')
    if ($records.exportedAll.timedOut -or $records.exportedAll.exitCode -ne 0) {
        Write-HostJsonAtomic (Join-Path $EvidenceDirectory 'snapshot.json') $records
        throw ('Exported BCD enumeration failed exit=' + $records.exportedAll.exitCode + ' stdout=' + $records.exportedAll.stdout + ' stderr=' + $records.exportedAll.stderr)
    }
    # The supported reader may update backup hive metadata; pin the final backup file.
    $records.export.sha256 = Get-HostSha256 $exportPath
    $records.export.bytes = [long](Get-Item -LiteralPath $exportPath).Length
    Assert-HostBcdSnapshotUsable $records
    Write-HostJsonAtomic (Join-Path $EvidenceDirectory 'snapshot.json') $records
    return $records
}

function Get-HostCodeIntegritySnapshot {
    if (-not ('NicHostRoundTripIntegrity' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NicHostRoundTripIntegrity {
    [StructLayout(LayoutKind.Sequential)] public struct Information { public uint Length; public uint Options; }
    [DllImport("ntdll.dll")] public static extern uint NtQuerySystemInformation(int kind, ref Information data, int size, out uint returned);
    public static uint[] Read() { Information value = new Information(); value.Length = 8; uint returned; uint status = NtQuerySystemInformation(103, ref value, 8, out returned); return new uint[] { status, value.Options, returned }; }
}
'@
    }
    $values = [NicHostRoundTripIntegrity]::Read()
    if ([uint32]$values[0] -ne 0) { throw ('NtQuerySystemInformation(SystemCodeIntegrityInformation) failed: 0x{0:X8}' -f [uint32]$values[0]) }
    return [ordered]@{ ntStatus = ('0x{0:X8}' -f [uint32]$values[0]); options = ('0x{0:X8}' -f [uint32]$values[1]); testSigning = [bool](([uint32]$values[1] -band 2) -ne 0) }
}

function Get-HostTrustSnapshot {
    param([Parameter(Mandatory = $true)][string]$Thumbprint)
    $result = [ordered]@{}
    foreach ($name in @('Root', 'TrustedPublisher')) {
        $store = [Security.Cryptography.X509Certificates.X509Store]::new($name, 'LocalMachine')
        try {
            $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
            $result[$name] = @($store.Certificates | Where-Object { $_.Thumbprint -ceq $Thumbprint }).Count -gt 0
        }
        finally { $store.Close() }
    }
    return $result
}

function Get-HostFilterSnapshot {
    $servicePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\HypersinvNicAddressFilter'
    $servicePresent = Test-Path -LiteralPath $servicePath
    $bindings = @()
    $bindingError = $null
    try { $bindings = @(Get-NetAdapterBinding -Name '*' -AllBindings -ErrorAction Stop | Where-Object { ([string]$_.ComponentID -match 'HypersinvNicAddressFilter') -or ([string]$_.DisplayName -match 'Hypersinv NIC') }) }
    catch { $bindingError = $_.Exception.ToString() }
    if ($null -ne $bindingError) { throw ('Cannot verify NIC filter binding absence: ' + $bindingError) }
    return [ordered]@{ servicePresent = $servicePresent; serviceRegistryPath = $servicePath; matchingBindings = @($bindings | ForEach-Object { [ordered]@{ Name = [string]$_.Name; ComponentID = [string]$_.ComponentID; Enabled = [bool]$_.Enabled; DisplayName = [string]$_.DisplayName } }); clear = (-not $servicePresent -and $bindings.Count -eq 0) }
}

function Get-HostPlatformSnapshot {
    param([Parameter(Mandatory = $true)][string]$Directory, [Parameter(Mandatory = $true)][string]$CertificateThumbprint)
    [void][IO.Directory]::CreateDirectory($Directory)
    $secureBoot = $null
    $secureBootError = $null
    try { $secureBoot = [bool](Confirm-SecureBootUEFI -ErrorAction Stop) }
    catch { $secureBootError = $_.Exception.ToString() }
    if ($null -eq $secureBoot) { throw ('Confirm-SecureBootUEFI could not establish the current state: ' + $secureBootError) }
    $firmware = Get-HostFirmwareVariableSnapshot (Join-Path $Directory 'firmware')
    $bcd = Get-HostBcdSnapshot (Join-Path $Directory 'bcd')
    $esp = Get-HostEspSnapshot (Join-Path $Directory 'esp') $bcd
    $codeIntegrity = Get-HostCodeIntegritySnapshot
    $trust = Get-HostTrustSnapshot $CertificateThumbprint
    $filter = Get-HostFilterSnapshot
    if (-not $secureBoot) { throw 'Secure Boot must remain ON for the NIC host address test.' }
    if ($codeIntegrity.testSigning) { throw 'Windows test-signing must be OFF for this host test.' }
    if ($trust.Root -or $trust.TrustedPublisher) { throw ('The test signer must be absent from both LocalMachine trust stores; actual=' + (ConvertTo-Json -InputObject $trust -Compress)) }
    if (-not $filter.clear) { throw ('NIC filter service or binding is present; refusing built-in adapter test: ' + (ConvertTo-Json -InputObject $filter -Compress -Depth 6)) }
    return [ordered]@{
        capturedUtc = [DateTime]::UtcNow.ToString('o')
        secureBoot = $secureBoot
        codeIntegrity = $codeIntegrity
        trustStores = $trust
        nicFilter = $filter
        firmware = $firmware
        bcd = $bcd
        esp = $esp
    }
}

function Get-HostStablePlatformData {
    param([Parameter(Mandatory = $true)][object]$Snapshot)
    $esp = @((Get-HostProperty $Snapshot 'esp') | ForEach-Object {
        $partition = $_
        $files = @((Get-HostProperty $partition 'files') | ForEach-Object {
            $path = [string](Get-HostProperty $_ 'path')
            if ([string]::Equals($path, 'EFI/Microsoft/Boot/BOOTSTAT.DAT', [StringComparison]::OrdinalIgnoreCase)) { return }
            if (Test-HostManagedBcdFile $path ([bool](Get-HostProperty $partition 'isSystem'))) {
                Assert-HostBcdSnapshotUsable (Get-HostProperty $Snapshot 'bcd')
                if ((Get-HostProperty $_ 'verification') -cne 'bcdedit-system-store') { throw ('Live BCD classification is missing supported verification: ' + $path) }
                [ordered]@{ path = $path; verification = 'bcdedit-system-store' }
            }
            else {
                $hash = [string](Get-HostProperty $_ 'sha256')
                if ($hash -notmatch '\A[0-9A-Fa-f]{64}\z') { throw ('ESP file is missing its required SHA-256: ' + $path) }
                [ordered]@{ path = $path; bytes = [long](Get-HostProperty $_ 'bytes'); sha256 = $hash }
            }
        })
        [ordered]@{
            diskNumber = [int](Get-HostProperty $partition 'diskNumber')
            partitionNumber = [int](Get-HostProperty $partition 'partitionNumber')
            partitionGuid = [string](Get-HostProperty $partition 'partitionGuid')
            isSystem = [bool](Get-HostProperty $partition 'isSystem')
            offset = [long](Get-HostProperty $partition 'offset')
            size = [long](Get-HostProperty $partition 'size')
            files = $files
        }
    })
    $firmware = [ordered]@{}
    foreach ($name in @('SecureBoot', 'SetupMode', 'BootOrder', 'BootNext', 'BootCurrent')) {
        $item = Get-HostProperty (Get-HostProperty $Snapshot 'firmware') $name
        $firmware[$name] = if ((Get-HostProperty $item 'state') -ceq 'present') { [ordered]@{ state = 'present'; guid = [string](Get-HostProperty $item 'guid'); attributes = [uint32](Get-HostProperty $item 'attributes'); size = [long](Get-HostProperty $item 'size'); bytesSha256 = [string](Get-HostProperty $item 'bytesSha256'); bytesHex = [string](Get-HostProperty $item 'bytesHex') } } else { [ordered]@{ state = [string](Get-HostProperty $item 'state'); win32Error = [int](Get-HostProperty $item 'win32Error'); errorType = [string](Get-HostProperty $item 'errorType'); error = [string](Get-HostProperty $item 'error') } }
    }
    $bcd = Get-HostProperty $Snapshot 'bcd'
    $bcdStable = [ordered]@{}
    foreach ($name in @('current', 'all', 'firmware', 'exportedAll')) {
        $entry = Get-HostProperty $bcd $name
        $bcdStable[$name] = [ordered]@{ exitCode = [int](Get-HostProperty $entry 'exitCode'); stdout = [string](Get-HostProperty $entry 'stdout'); stderr = [string](Get-HostProperty $entry 'stderr') }
    }
    $export = Get-HostProperty $bcd 'export'
    $bcdStable.export = [ordered]@{ exitCode = [int](Get-HostProperty $export 'exitCode') }
    return [ordered]@{
        secureBoot = [bool](Get-HostProperty $Snapshot 'secureBoot')
        codeIntegrity = Get-HostProperty $Snapshot 'codeIntegrity'
        trustStores = Get-HostProperty $Snapshot 'trustStores'
        nicFilter = [ordered]@{ servicePresent = [bool](Get-HostProperty (Get-HostProperty $Snapshot 'nicFilter') 'servicePresent'); matchingBindings = @(Get-HostProperty (Get-HostProperty $Snapshot 'nicFilter') 'matchingBindings') }
        firmware = $firmware
        bcd = $bcdStable
        esp = $esp
        espMutableExclusions = @(
            'EFI/Microsoft/Boot/BOOTSTAT.DAT is inventoried but excluded from byte equality because Windows owns boot status.'
            'Only the system ESP BCD hive and BCD.LOG/LOG1/LOG2 are inventoried through supported export and live/exported-store enumeration; raw hive/log byte equality is not claimed.'
        )
    }
}

function Assert-HostPlatformUnchanged {
    param([Parameter(Mandatory = $true)][object]$Before, [Parameter(Mandatory = $true)][object]$After)
    $beforeJson = ConvertTo-Json -InputObject (Get-HostStablePlatformData $Before) -Depth 24 -Compress
    $afterJson = ConvertTo-Json -InputObject (Get-HostStablePlatformData $After) -Depth 24 -Compress
    if (-not [string]::Equals($beforeJson, $afterJson, [StringComparison]::Ordinal)) {
        throw ('Host boot/security/ESP state changed during NIC test. BEFORE=' + $beforeJson + ' AFTER=' + $afterJson)
    }
}

function Invoke-HostReadback {
    param([Parameter(Mandatory = $true)][string]$Mode, [Parameter(Mandatory = $true)][string]$NativeExePath, [Parameter(Mandatory = $true)][string]$TsvPath, [Parameter(Mandatory = $true)][string]$Directory, [AllowNull()][object]$Dot11Baseline = $null)
    [void][IO.Directory]::CreateDirectory($Directory)
    $readScript = Join-Path $PSScriptRoot '..\nic_vm_probe\read-host.ps1'
    if (-not (Test-Path -LiteralPath $readScript -PathType Leaf)) { throw ('Read-only native/WMI collector script is missing: ' + $readScript) }
    $powershell = Join-Path $PSHOME 'powershell.exe'
    $capture = Get-HostProcessCapture $powershell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $readScript, '-NativeExe', $NativeExePath, '-OutputDirectory', $Directory, '-LockedMap', $TsvPath, '-ExpectedMode', $Mode)
    $stdoutPath = Join-Path $Directory 'read-host.stdout.txt'
    $stderrPath = Join-Path $Directory 'read-host.stderr.txt'
    [IO.File]::WriteAllText($stdoutPath, [string]$capture.stdout, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($stderrPath, [string]$capture.stderr, [Text.UTF8Encoding]::new($false))
    if ($capture.timedOut -or $capture.exitCode -ne 0) { throw ('read-host.ps1 ExpectedMode=' + $Mode + ' exited ' + $capture.exitCode + ' timedOut=' + $capture.timedOut + ' killError=' + $capture.killError + '; stdout=' + $capture.stdout + '; stderr=' + $capture.stderr) }
    $reports = @(Get-ChildItem -LiteralPath $Directory -Filter 'readback.*.json' -File -ErrorAction Stop)
    if ($reports.Count -ne 1) { throw ('Expected exactly one readback JSON in ' + $Directory + '; actual=' + $reports.Count) }
    $data = Get-Content -LiteralPath $reports[0].FullName -Raw | ConvertFrom-Json
    if ((Get-HostProperty $data 'schema') -cne 'nic-native-wmi-readback.v1' -or (Get-HostProperty $data 'expectedMode') -cne $Mode) { throw ('Unexpected readback schema/mode: schema=' + [string](Get-HostProperty $data 'schema') + ' mode=' + [string](Get-HostProperty $data 'expectedMode')) }
    if (-not [bool](Get-HostProperty $data 'allLockedExpectedValuesMatchReadOnlySources') -or [int](Get-HostProperty $data 'lockedMacOriginalCount') -ne 4 -or [int](Get-HostProperty $data 'lockedMacReplacementCount') -ne 4) {
        throw ('Readback did not prove all seven required sources for every adapter. ExpectedMode=' + $Mode + ' failures=' + (ConvertTo-Json -InputObject (Get-HostProperty $data 'failures') -Depth 10 -Compress))
    }
    $rows = @(Get-HostProperty $data 'lockedOriginalEvaluations')
    if ($rows.Count -ne 4) { throw ('Readback adapter count differs from 4: ' + $rows.Count) }
    $dot11ByPnp = [ordered]@{}
    foreach ($binding in $script:HostBindings) {
        $matching = @($rows | Where-Object { [string]::Equals([string](Get-HostProperty $_ 'pnpDeviceId'), [string]$binding.pnpDeviceId, [StringComparison]::OrdinalIgnoreCase) })
        if ($matching.Count -ne 1) { throw ('Readback target identity count mismatch: pnp=' + $binding.pnpDeviceId + ' actual=' + $matching.Count) }
        $expected = if ($Mode -ceq 'Replacement') { [string]$binding.replacement } else { [string]$binding.original }
        if ((ConvertTo-HostMacKey (Get-HostProperty $matching[0] 'expectedMac')) -cne (ConvertTo-HostMacKey $expected) -or -not [bool](Get-HostProperty $matching[0] 'allRequiredExpectedSourcesMatch')) {
            throw ('Readback expected/actual mismatch: pnp=' + $binding.pnpDeviceId + ' expected=' + $expected + ' actual=' + [string](Get-HostProperty $matching[0] 'expectedMac') + ' allRequiredExpectedSourcesMatch=' + [string](Get-HostProperty $matching[0] 'allRequiredExpectedSourcesMatch'))
        }
        $dot11 = Get-HostProperty $matching[0] 'dot11CurrentAddressCheck'
        if ($null -eq $dot11) { throw ('Readback lacks the conditional OID_DOT11_CURRENT_ADDRESS observation: pnp=' + $binding.pnpDeviceId) }
        $dot11Status = [string](Get-HostProperty $dot11 'status')
        if ($dot11Status -ceq 'unavailable') { throw ('OID_DOT11_CURRENT_ADDRESS was not captured: pnp=' + $binding.pnpDeviceId + ' diagnostic=' + (ConvertTo-Json -InputObject $dot11 -Compress -Depth 8)) }
        $dot11Applicable = [bool](Get-HostProperty $dot11 'applicable')
        $dot11Actual = ConvertTo-HostMacKey (Get-HostProperty $dot11 'actual')
        $dot11Matches = Get-HostProperty $dot11 'matchesExpected'
        if ($dot11Applicable -and ($null -eq $dot11Actual -or $dot11Matches -ne $true)) {
            throw ('OID_DOT11_CURRENT_ADDRESS mismatch: mode=' + $Mode + ' pnp=' + $binding.pnpDeviceId + ' expected=' + $expected + ' actual=' + [string](Get-HostProperty $dot11 'actual') + ' ioctlSucceeded=' + [string](Get-HostProperty $dot11 'ioctlSucceeded') + ' win32Error=' + [string](Get-HostProperty $dot11 'win32Error'))
        }
        $baselineEntry = Get-HostProperty $Dot11Baseline ([string]$binding.pnpDeviceId)
        $baselineApplicable = if ($null -ne $baselineEntry) { [bool](Get-HostProperty $baselineEntry 'applicable') } elseif ($Mode -ceq 'Original') { $dot11Applicable } else { $false }
        if ($baselineApplicable -and -not $dot11Applicable) {
            throw ('OID_DOT11_CURRENT_ADDRESS was supported in the original host baseline but is unavailable now: mode=' + $Mode + ' pnp=' + $binding.pnpDeviceId + ' status=' + $dot11Status + ' win32Error=' + [string](Get-HostProperty $dot11 'win32Error') + ' errorText=' + [string](Get-HostProperty $dot11 'win32ErrorText'))
        }
        $dot11ByPnp[[string]$binding.pnpDeviceId] = [ordered]@{
            applicable = $dot11Applicable
            status = $dot11Status
            ioctlSucceeded = Get-HostProperty $dot11 'ioctlSucceeded'
            actual = [string](Get-HostProperty $dot11 'actual')
            normalizedActual = $dot11Actual
            expected = $expected
            matchesExpected = $dot11Matches
            win32Error = Get-HostProperty $dot11 'win32Error'
            win32ErrorText = Get-HostProperty $dot11 'win32ErrorText'
        }
    }
    return [ordered]@{ path = $reports[0].FullName; sha256 = Get-HostSha256 $reports[0].FullName; expectedMode = $Mode; adapters = $rows.Count; allSevenReadersMatched = $true; dot11ByPnp = $dot11ByPnp }
}

function Get-HostAdapterSnapshots {
    param([Parameter(Mandatory = $true)][object[]]$Bindings)
    $plainAdapters = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop)
    $msftAdapters = @(Get-CimInstance -Namespace 'root\StandardCimv2' -ClassName MSFT_NetAdapter -ErrorAction Stop)
    $snapshots = [Collections.Generic.List[object]]::new()
    foreach ($binding in $Bindings) {
        $pnp = [string]$binding.pnpDeviceId
        $msftMatches = @($msftAdapters | Where-Object { [string]::Equals([string](Get-HostProperty $_ 'PnPDeviceID'), $pnp, [StringComparison]::OrdinalIgnoreCase) })
        if ($msftMatches.Count -ne 1) { throw ('Expected one host MSFT_NetAdapter by exact PnP identity; pnp=' + $pnp + ' actual=' + $msftMatches.Count) }
        $guid = ([Guid]::Parse([string](Get-HostProperty $msftMatches[0] 'InterfaceGuid'))).ToString('D').ToUpperInvariant()
        $plainMatches = @($plainAdapters | Where-Object { ([Guid]::Parse([string](Get-HostProperty $_ 'InterfaceGuid'))).ToString('D').ToUpperInvariant() -ceq $guid })
        if ($plainMatches.Count -ne 1) { throw ('Expected one Get-NetAdapter by exact interface GUID; pnp=' + $pnp + ' guid=' + $guid + ' actual=' + $plainMatches.Count) }
        $adapter = $plainMatches[0]
        $mac = ConvertTo-HostCanonicalMac (Get-HostProperty $adapter 'MacAddress') ('Fresh host Get-NetAdapter MAC for ' + $pnp)
        if ((ConvertTo-HostMacKey $mac) -cne (ConvertTo-HostMacKey $binding.original)) { throw ('Fresh host original mismatch: pnp=' + $pnp + ' expected=' + $binding.original + ' actual=' + $mac) }
        $classEntry = Get-NicAddressRegistryClassEntry $guid $pnp
        $admin = Get-NicAddressAdminState $adapter $msftMatches[0] $pnp
        $link = Get-HostLinkConnectedState $adapter $msftMatches[0] $pnp
        $snapshots.Add([ordered]@{
            pnpDeviceId = $pnp
            interfaceGuid = $guid
            original = [string]$binding.original
            replacement = [string]$binding.replacement
            observedOriginalMac = $mac
            adapterName = [string](Get-HostProperty $adapter 'Name')
            interfaceDescription = [string](Get-HostProperty $adapter 'InterfaceDescription')
            adapterStatus = [string](Get-HostProperty $adapter 'Status')
            adminState = $admin
            mediaConnectState = $link.mediaConnectState
            linkConnected = [bool]$link.connected
            linkStateSource = [string]$link.source
            registrySubKey = [string]$classEntry.registrySubKey
            registryKeyName = [string]$classEntry.registryKeyName
            registryPnpValueName = [string]$classEntry.registryPnpValueName
            registryPnpDeviceId = [string]$classEntry.registryPnpDeviceId
            networkAddress = $classEntry.networkAddress
        })
    }
    return @($snapshots.ToArray())
}

function Get-HostLinkConnectedState {
    param([AllowNull()][object]$Adapter, [AllowNull()][object]$MsftAdapter, [Parameter(Mandatory = $true)][string]$PnpDeviceId)
    $mediaValue = Get-HostProperty $MsftAdapter 'MediaConnectState'
    $statusValue = Get-HostProperty $Adapter 'Status'
    $mediaText = [string]$mediaValue
    $statusText = [string]$statusValue
    $mediaConnected = if ($mediaText -match '^(?i:Connected)$' -or $mediaText -ceq '1') { $true } elseif ($mediaText -match '^(?i:Disconnected)$' -or $mediaText -ceq '2') { $false } else { $null }
    $statusConnected = if ($statusText -match '^(?i:Up|Connected)$') { $true } elseif ($statusText -match '^(?i:Down|Disconnected|Disabled)$') { $false } else { $null }
    if ($null -eq $mediaConnected -and $null -eq $statusConnected) {
        throw ('Cannot establish link state from MediaConnectState/Status: pnp=' + $PnpDeviceId + ' mediaConnectState=' + $mediaText + ' status=' + $statusText)
    }
    if ($null -ne $mediaConnected -and $null -ne $statusConnected -and $mediaConnected -ne $statusConnected) {
        throw ('Adapter link-state sources conflict: pnp=' + $PnpDeviceId + ' mediaConnectState=' + $mediaText + ' status=' + $statusText)
    }
    $connected = if ($null -ne $mediaConnected) { [bool]$mediaConnected } else { [bool]$statusConnected }
    $source = if ($null -ne $mediaConnected -and $null -ne $statusConnected) { 'MSFT_NetAdapter.MediaConnectState+Get-NetAdapter.Status' } elseif ($null -ne $mediaConnected) { 'MSFT_NetAdapter.MediaConnectState' } else { 'Get-NetAdapter.Status' }
    return [ordered]@{ connected = $connected; mediaConnectState = $mediaText; adapterStatus = $statusText; source = $source }
}

function Get-HostExactAdapter {
    param([Parameter(Mandatory = $true)][object]$Snapshot)
    $guid = [string](Get-HostProperty $Snapshot 'interfaceGuid')
    $pnp = [string](Get-HostProperty $Snapshot 'pnpDeviceId')
    $msft = @(Get-CimInstance -Namespace 'root\StandardCimv2' -ClassName MSFT_NetAdapter -ErrorAction Stop | Where-Object {
        [string]::Equals([string](Get-HostProperty $_ 'PnPDeviceID'), $pnp, [StringComparison]::OrdinalIgnoreCase) -and
        ([Guid]::Parse([string](Get-HostProperty $_ 'InterfaceGuid'))).ToString('D').ToUpperInvariant() -ceq $guid
    })
    $plain = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop | Where-Object { ([Guid]::Parse([string](Get-HostProperty $_ 'InterfaceGuid'))).ToString('D').ToUpperInvariant() -ceq $guid })
    if ($msft.Count -ne 1 -or $plain.Count -ne 1) { throw ('Exact adapter disappeared or became ambiguous: pnp=' + $pnp + ' guid=' + $guid + ' MSFT=' + $msft.Count + ' plain=' + $plain.Count) }
    return [ordered]@{ adapter = $plain[0]; msft = $msft[0] }
}

function Invoke-HostAdapterRegistryApply {
    param(
        [Parameter(Mandatory = $true)][object]$Snapshot,
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][object]$Progress,
        [Parameter(Mandatory = $true)][ref]$MutationStarted
    )

    $pnp = [string](Get-HostProperty $Snapshot 'pnpDeviceId')
    $guid = ConvertTo-NicAddressGuid (Get-HostProperty $Snapshot 'interfaceGuid')
    if ([string]::IsNullOrWhiteSpace($pnp) -or $pnp.Length -gt 260 -or $pnp -notmatch '\A[\x20-\x7e]+\z' -or $pnp.Contains('"') -or $pnp.Contains('%') -or $null -eq $guid) {
        throw ('NIC registry apply snapshot has an invalid exact identity: pnp=' + $pnp + ' guid=' + [string](Get-HostProperty $Snapshot 'interfaceGuid'))
    }

    $registryKeyName = [string](Get-HostProperty $Snapshot 'registryKeyName')
    $registrySubKey = [string](Get-HostProperty $Snapshot 'registrySubKey')
    $classPrefix = 'SYSTEM\CurrentControlSet\Control\Class\{4d36e972-e325-11ce-bfc1-08002be10318}\'
    if ($registryKeyName -notmatch '\A[0-9]{4}\z' -or
        -not [string]::Equals($registrySubKey, ($classPrefix + $registryKeyName), [StringComparison]::OrdinalIgnoreCase)) {
        throw ('NIC registry apply snapshot has an invalid class-key identity: pnp=' + $pnp + ' guid=' + $guid + ' keyName=' + $registryKeyName + ' subKey=' + $registrySubKey)
    }

    $originalMac = ConvertTo-HostCanonicalMac (Get-HostProperty $Snapshot 'original') ('Captured original MAC for ' + $pnp)
    $replacementMac = ConvertTo-HostCanonicalMac (Get-HostProperty $Snapshot 'replacement') ('Locked replacement MAC for ' + $pnp)
    if (-not (Test-NicAddressUnicastMac $originalMac) -or -not (Test-NicAddressUnicastMac $replacementMac) -or
        (ConvertTo-NicAddressMacKey $originalMac) -ceq (ConvertTo-NicAddressMacKey $replacementMac)) {
        throw ('NIC registry apply snapshot has an invalid or non-distinct original/replacement pair: pnp=' + $pnp + ' original=' + $originalMac + ' replacement=' + $replacementMac)
    }

    $live = Get-HostExactAdapter $Snapshot
    $livePnp = [string](Get-HostProperty $live.msft 'PnPDeviceID')
    $liveGuid = ConvertTo-NicAddressGuid (Get-HostProperty $live.msft 'InterfaceGuid')
    $adapterGuid = ConvertTo-NicAddressGuid (Get-HostProperty $live.adapter 'InterfaceGuid')
    if (-not [string]::Equals($livePnp, $pnp, [StringComparison]::OrdinalIgnoreCase) -or
        $liveGuid -cne $guid -or $adapterGuid -cne $guid) {
        throw ('Exact NIC identity changed before registry apply: expectedPnp=' + $pnp + ' actualPnp=' + $livePnp + ' expectedGuid=' + $guid + ' actualMsftGuid=' + [string](Get-HostProperty $live.msft 'InterfaceGuid') + ' actualAdapterGuid=' + [string](Get-HostProperty $live.adapter 'InterfaceGuid'))
    }
    $liveMac = ConvertTo-HostCanonicalMac (Get-HostProperty $live.adapter 'MacAddress') ('Fresh Get-NetAdapter MAC for ' + $pnp)
    if ((ConvertTo-NicAddressMacKey $liveMac) -cne (ConvertTo-NicAddressMacKey $originalMac)) {
        throw ('NIC original MAC changed before registry apply: pnp=' + $pnp + ' expected=' + $originalMac + ' actual=' + $liveMac)
    }

    $classEntry = Get-NicAddressRegistryClassEntry $guid $pnp
    $classSubKey = [string](Get-NicAddressPropertyValue $classEntry 'registrySubKey')
    $className = [string](Get-NicAddressPropertyValue $classEntry 'registryKeyName')
    $snapshotPnp = [string](Get-HostProperty $Snapshot 'registryPnpDeviceId')
    $classPnp = [string](Get-NicAddressPropertyValue $classEntry 'registryPnpDeviceId')
    $snapshotPnpValueName = [string](Get-HostProperty $Snapshot 'registryPnpValueName')
    $classPnpValueName = [string](Get-NicAddressPropertyValue $classEntry 'registryPnpValueName')
    if (-not [string]::Equals($classSubKey, $registrySubKey, [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals($className, $registryKeyName, [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals($classPnp, $snapshotPnp, [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals($classPnpValueName, $snapshotPnpValueName, [StringComparison]::OrdinalIgnoreCase)) {
        throw ('NIC network-class identity changed before registry apply: pnp=' + $pnp + ' guid=' + $guid + ' expectedSubKey=' + $registrySubKey + ' actualSubKey=' + $classSubKey + ' expectedKeyName=' + $registryKeyName + ' actualKeyName=' + $className + ' expectedRegistryPnp=' + $snapshotPnp + ' actualRegistryPnp=' + $classPnp + ' expectedPnpValueName=' + $snapshotPnpValueName + ' actualPnpValueName=' + $classPnpValueName)
    }

    $baselineRegistry = Get-HostProperty $Snapshot 'networkAddress'
    $currentRegistry = Get-NicAddressRegistryValueSnapshot $registrySubKey
    if (-not (Test-NicAddressRegistryValueSnapshotEqual $baselineRegistry $currentRegistry)) {
        throw ('NetworkAddress registry value changed before apply: pnp=' + $pnp + ' expected=' + (ConvertTo-Json -InputObject $baselineRegistry -Compress -Depth 8) + ' actual=' + (ConvertTo-Json -InputObject $currentRegistry -Compress -Depth 8))
    }
    $restartCommand = Get-Command Restart-NetAdapter -ErrorAction Stop
    if (-not $restartCommand.Parameters.ContainsKey('InputObject')) { throw 'Restart-NetAdapter lost the exact InputObject parameter required for NIC address apply.' }

    $replacementHex = ConvertTo-NicAddressMacKey $replacementMac
    $intent = New-NicAddressIntent 'apply' $Snapshot $replacementMac
    $Progress.applyIntentUtc = [DateTime]::UtcNow.ToString('o')
    $Progress.targetMac = $replacementMac
    $Progress.state = 'apply-intent-recorded'
    $Progress.registryIntent = [ordered]@{
        keyName = $registryKeyName
        subKey = $registrySubKey
        valueName = 'NetworkAddress'
        valueKind = 'String'
        value = $replacementHex
        baseline = $baselineRegistry
    }
    $Progress.addressIntent = $intent
    Write-HostJournal $Journal

    $Journal.mutationStarted = $true
    $MutationStarted.Value = $true
    Write-HostJournal $Journal

    $replacementRegistry = New-NicAddressRegistryValueSnapshot $true 'String' $replacementHex
    Set-NicAddressRegistryValueSnapshot $registrySubKey $replacementRegistry
    $registryAfterWrite = Get-NicAddressRegistryValueSnapshot $registrySubKey
    if (-not (Test-NicAddressRegistryValueSnapshotEqual $replacementRegistry $registryAfterWrite)) {
        throw ('NetworkAddress registry apply readback mismatch: pnp=' + $pnp + ' expected=' + (ConvertTo-Json -InputObject $replacementRegistry -Compress -Depth 8) + ' actual=' + (ConvertTo-Json -InputObject $registryAfterWrite -Compress -Depth 8))
    }

    Restart-NetAdapter -InputObject $live.adapter -Confirm:$false -ErrorAction Stop | Out-Null

    $afterRestart = Get-NicAddressRegistryValueSnapshot $registrySubKey
    if (-not (Test-NicAddressRegistryValueSnapshotEqual $replacementRegistry $afterRestart)) {
        throw ('NetworkAddress registry changed during adapter restart: pnp=' + $pnp + ' expected=' + (ConvertTo-Json -InputObject $replacementRegistry -Compress -Depth 8) + ' actual=' + (ConvertTo-Json -InputObject $afterRestart -Compress -Depth 8))
    }
    $fresh = Get-HostExactAdapter $Snapshot
    $freshPnp = [string](Get-HostProperty $fresh.msft 'PnPDeviceID')
    $freshGuid = ConvertTo-NicAddressGuid (Get-HostProperty $fresh.msft 'InterfaceGuid')
    $freshAdapterGuid = ConvertTo-NicAddressGuid (Get-HostProperty $fresh.adapter 'InterfaceGuid')
    if (-not [string]::Equals($freshPnp, $pnp, [StringComparison]::OrdinalIgnoreCase) -or
        $freshGuid -cne $guid -or $freshAdapterGuid -cne $guid) {
        throw ('Exact NIC identity changed after registry apply/restart: expectedPnp=' + $pnp + ' actualPnp=' + $freshPnp + ' expectedGuid=' + $guid + ' actualMsftGuid=' + [string](Get-HostProperty $fresh.msft 'InterfaceGuid') + ' actualAdapterGuid=' + [string](Get-HostProperty $fresh.adapter 'InterfaceGuid'))
    }
    $actualMac = ConvertTo-HostCanonicalMac (Get-HostProperty $fresh.adapter 'MacAddress') ('Post-restart Get-NetAdapter MAC for ' + $pnp)
    if ((ConvertTo-NicAddressMacKey $actualMac) -cne $replacementHex) {
        throw ('NetworkAddress apply readback mismatch: pnp=' + $pnp + ' expected=' + $replacementMac + ' actual=' + $actualMac)
    }

    $Progress.state = 'replacement-active'
    $Progress.actualMac = $actualMac
    $Progress.registryActual = $afterRestart
    $Progress.appliedUtc = [DateTime]::UtcNow.ToString('o')
    Write-HostJournal $Journal
    return [ordered]@{
        action = 'apply-registry'
        pnpDeviceId = $pnp
        interfaceGuid = $guid
        registrySubKey = $registrySubKey
        registryValueKind = 'String'
        registryValue = $replacementHex
        original = $originalMac
        expected = $replacementMac
        actual = $actualMac
        matchesExpected = $true
        mutationStarted = [bool]$Journal.mutationStarted
    }
}

function Write-HostJournal {
    param([Parameter(Mandatory = $true)][object]$Journal)
    $Journal.updatedUtc = [DateTime]::UtcNow.ToString('o')
    Write-HostJsonAtomic ([string]$Journal.journalPath) $Journal
}

function Wait-HostAdapterLinksRestored {
    param([Parameter(Mandatory = $true)][object[]]$Snapshots, [ValidateRange(1, 30)][int]$TimeoutSeconds = 30)
    $checks = @{}
    foreach ($snapshot in $Snapshots) {
        $pnp = [string]$snapshot.pnpDeviceId
        if ($null -eq (Get-HostProperty $snapshot 'linkConnected')) { throw ('Adapter snapshot does not contain a baseline link-state value: pnp=' + $pnp) }
        $checks[$pnp.ToUpperInvariant()] = [ordered]@{
            pnpDeviceId = $pnp
            interfaceGuid = [string]$snapshot.interfaceGuid
            expectedConnected = [bool]$snapshot.linkConnected
            expectedMediaConnectState = [string]$snapshot.mediaConnectState
            source = [string]$snapshot.linkStateSource
            actualConnected = $null
            actualMediaConnectState = $null
            actualAdapterStatus = $null
            matchesBaseline = $false
            timedOut = $false
            error = $null
        }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $pending = $false
        foreach ($snapshot in $Snapshots) {
            $pnp = [string]$snapshot.pnpDeviceId
            $check = $checks[$pnp.ToUpperInvariant()]
            if ($check.matchesBaseline -or ($check.error -and -not $check.expectedConnected)) { continue }
            if (-not $check.expectedConnected -and $null -ne $check.actualConnected) { continue }
            try {
                $live = Get-HostExactAdapter $snapshot
                $state = Get-HostLinkConnectedState $live.adapter $live.msft $pnp
                $check.actualConnected = [bool]$state.connected
                $check.actualMediaConnectState = [string]$state.mediaConnectState
                $check.actualAdapterStatus = [string]$state.adapterStatus
                $check.error = $null
                $check.matchesBaseline = ([bool]$state.connected -eq [bool]$check.expectedConnected)
                if ($check.expectedConnected -and -not $check.matchesBaseline) { $pending = $true }
            }
            catch {
                $check.error = $_.Exception.ToString()
                $check.matchesBaseline = $false
                if ($check.expectedConnected) { $pending = $true }
            }
        }
        if (-not $pending) { break }
        if ([DateTime]::UtcNow -ge $deadline) { break }
        Start-Sleep -Milliseconds 1000
    } while ([DateTime]::UtcNow -lt $deadline)

    $results = @()
    foreach ($check in $checks.Values) {
        if ($check.expectedConnected -and -not $check.matchesBaseline) { $check.timedOut = $true }
        $results += $check
    }
    return $results
}

function Restore-HostAdapters {
    param([Parameter(Mandatory = $true)][object]$Journal)
    $errors = [Collections.Generic.List[object]]::new()
    $snapshots = @($Journal.adapterSnapshots)
    foreach ($snapshot in $snapshots) {
        $pnp = [string]$snapshot.pnpDeviceId
        $entry = $null
        try {
            $progressMatches = @($Journal.adapterProgress | Where-Object { [string]::Equals([string]$_.pnpDeviceId, $pnp, [StringComparison]::OrdinalIgnoreCase) })
            if ($progressMatches.Count -gt 1) { throw ('Journal progress row is ambiguous for ' + $pnp) }
            if ($progressMatches.Count -eq 1) { $entry = $progressMatches[0] }
            else {
                $entry = [ordered]@{ pnpDeviceId = $pnp; state = 'progress-reconstructed-for-recovery' }
                $Journal.adapterProgress = @($Journal.adapterProgress) + @($entry)
            }
            $entry.restoreIntentUtc = [DateTime]::UtcNow.ToString('o')
            $entry.restoreState = 'intent-recorded'
            try { Write-HostJournal $Journal }
            catch { $errors.Add([ordered]@{ pnpDeviceId = $pnp; stage = 'persist-restore-intent'; exception = $_.Exception.ToString() }) }
        }
        catch {
            $errors.Add([ordered]@{ pnpDeviceId = $pnp; stage = 'prepare-restore-journal'; exception = $_.Exception.ToString() })
        }

        try {
            $live = Get-HostExactAdapter $snapshot
            $registryIdentity = Get-NicAddressRegistryClassEntry ([string]$snapshot.interfaceGuid) $pnp
            if ([string]$registryIdentity.registrySubKey -cne [string]$snapshot.registrySubKey -or
                [string]$registryIdentity.registryKeyName -cne [string]$snapshot.registryKeyName -or
                [string]$registryIdentity.registryPnpDeviceId -cne [string]$snapshot.registryPnpDeviceId) {
                throw ('Network class registry identity changed: pnp=' + $pnp + ' expectedSubKey=' + [string]$snapshot.registrySubKey + ' actualSubKey=' + [string]$registryIdentity.registrySubKey + ' expectedKeyName=' + [string]$snapshot.registryKeyName + ' actualKeyName=' + [string]$registryIdentity.registryKeyName + ' expectedRegistryPnp=' + [string]$snapshot.registryPnpDeviceId + ' actualRegistryPnp=' + [string]$registryIdentity.registryPnpDeviceId)
            }
            Set-NicAddressRegistryValueSnapshot -RegistrySubKey ([string]$snapshot.registrySubKey) -ValueSnapshot $snapshot.networkAddress
            Restart-NetAdapter -InputObject $live.adapter -Confirm:$false -ErrorAction Stop | Out-Null

            $adminBefore = Get-HostProperty $snapshot 'adminState'
            $expectedEnabled = [bool](Get-HostProperty $adminBefore 'enabled')
            $afterRestart = Get-HostExactAdapter $snapshot
            $adminText = [string](Get-HostProperty $afterRestart.adapter 'AdminStatus')
            $actualEnabled = $adminText -match '^(?i:Up|Enabled|1|True)$'
            if ($expectedEnabled -and -not $actualEnabled) { Enable-NetAdapter -InputObject $afterRestart.adapter -Confirm:$false -ErrorAction Stop | Out-Null }
            elseif (-not $expectedEnabled -and $actualEnabled) { Disable-NetAdapter -InputObject $afterRestart.adapter -Confirm:$false -ErrorAction Stop | Out-Null }

            $registryActual = Get-NicAddressRegistryValueSnapshot ([string]$snapshot.registrySubKey)
            if (-not (Test-NicAddressRegistryValueSnapshotEqual $snapshot.networkAddress $registryActual)) {
                throw ('Registry restore mismatch: pnp=' + $pnp + ' expected=' + (ConvertTo-Json -InputObject $snapshot.networkAddress -Compress -Depth 8) + ' actual=' + (ConvertTo-Json -InputObject $registryActual -Compress -Depth 8))
            }
            $restored = Get-HostExactAdapter $snapshot
            $restoredMac = ConvertTo-HostCanonicalMac (Get-HostProperty $restored.adapter 'MacAddress') ('Restored adapter MAC for ' + $pnp)
            $expectedMac = [string]$snapshot.original
            if ((ConvertTo-HostMacKey $restoredMac) -cne (ConvertTo-HostMacKey $expectedMac)) { throw ('Restored adapter query mismatch: pnp=' + $pnp + ' expected=' + $expectedMac + ' actual=' + $restoredMac) }
            $adminActual = [string](Get-HostProperty $restored.adapter 'AdminStatus')
            $adminEnabled = $adminActual -match '^(?i:Up|Enabled|1|True)$'
            if ($adminEnabled -ne $expectedEnabled) { throw ('Adapter administrative state restore mismatch: pnp=' + $pnp + ' expectedEnabled=' + $expectedEnabled + ' actual=' + $adminActual) }
            if ($null -ne $entry) {
                $entry.restoreState = 'verified'
                $entry.restoreActualMac = $restoredMac
                $entry.restoreRegistry = $registryActual
                $entry.restoreAdminStatus = $adminActual
                $entry.restoreCompletedUtc = [DateTime]::UtcNow.ToString('o')
                try { Write-HostJournal $Journal }
                catch { $errors.Add([ordered]@{ pnpDeviceId = $pnp; stage = 'persist-restore-result'; exception = $_.Exception.ToString() }) }
            }
        }
        catch {
            $errors.Add([ordered]@{ pnpDeviceId = $pnp; exceptionType = $_.Exception.GetType().FullName; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId })
            if ($null -ne $entry) {
                $entry.restoreState = 'failed'
                $entry.restoreError = $_.Exception.ToString()
                try { Write-HostJournal $Journal }
                catch { $errors.Add([ordered]@{ pnpDeviceId = $pnp; stage = 'persist-restore-failure'; exception = $_.Exception.ToString() }) }
            }
        }
    }
    try {
        $Journal.linkRestoreChecks = @(Wait-HostAdapterLinksRestored $snapshots 30)
        foreach ($check in $Journal.linkRestoreChecks) {
            if (-not [bool]$check.matchesBaseline) {
                $errors.Add([ordered]@{
                    pnpDeviceId = [string]$check.pnpDeviceId
                    stage = 'verify-restored-link-state'
                    expectedConnected = [bool]$check.expectedConnected
                    actualConnected = $check.actualConnected
                    actualMediaConnectState = [string]$check.actualMediaConnectState
                    actualAdapterStatus = [string]$check.actualAdapterStatus
                    timedOut = [bool]$check.timedOut
                    error = [string]$check.error
                })
            }
        }
        try { Write-HostJournal $Journal }
        catch { $errors.Add([ordered]@{ stage = 'persist-link-restore-checks'; exception = $_.Exception.ToString() }) }
    }
    catch { $errors.Add([ordered]@{ stage = 'check-restored-link-state'; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId }) }
    return @($errors.ToArray())
}

function Assert-HostIdentity {
    $baseboard = Get-CimInstance -ClassName Win32_BaseBoard -ErrorAction Stop
    $product = Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop
    $manufacturer = [string](Get-HostProperty $baseboard 'Manufacturer')
    $uuid = ([string](Get-HostProperty $product 'UUID')).ToUpperInvariant()
    if ($manufacturer -cne $script:ExpectedBaseboardManufacturer -or $uuid -cne $script:ExpectedHostUuid) {
        throw ('Physical host identity mismatch; expected baseboardManufacturer=' + $script:ExpectedBaseboardManufacturer + ' UUID=' + $script:ExpectedHostUuid + '; actual baseboardManufacturer=' + $manufacturer + ' UUID=' + $uuid)
    }
}

function Assert-HostJournalSnapshot {
    param([Parameter(Mandatory = $true)][object]$Journal, [Parameter(Mandatory = $true)][string]$RepositoryRoot)
    $expectedRoot = [IO.Path]::GetFullPath($RepositoryRoot)
    $expectedTsv = [IO.Path]::GetFullPath((Join-Path $expectedRoot 'spoof-handoff\reports\client-originals.tsv'))
    $recordedRoot = [IO.Path]::GetFullPath([string](Get-HostProperty $Journal 'repositoryRoot'))
    $recordedTsv = [IO.Path]::GetFullPath([string](Get-HostProperty $Journal 'lockedMap'))
    if (-not [string]::Equals($recordedRoot, $expectedRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals($recordedTsv, $expectedTsv, [StringComparison]::OrdinalIgnoreCase)) {
        throw ('Rollback journal repository/locked-map paths are not the expected in-repository sources: repository=' + $recordedRoot + ' lockedMap=' + $recordedTsv)
    }
    if (-not (Test-Path -LiteralPath $recordedTsv -PathType Leaf)) { throw ('Rollback locked map is missing: ' + $recordedTsv) }
    $tsvHash = Get-HostSha256 $recordedTsv
    if ($tsvHash -cne $script:ExpectedLockedTsvSha256 -or ([string](Get-HostProperty $Journal 'lockedMapSha256')).ToUpperInvariant() -cne $tsvHash) {
        throw ('Rollback locked map hash mismatch: pinned=' + $script:ExpectedLockedTsvSha256 + ' journal=' + [string](Get-HostProperty $Journal 'lockedMapSha256') + ' actual=' + $tsvHash)
    }

    $nativeRoot = Join-Path $expectedRoot 'spoof-handoff\artifacts\nic-reader-gap-20261017'
    $nativePath = [IO.Path]::GetFullPath([string](Get-HostProperty $Journal 'nativeExe'))
    [void](Assert-PathWithinRoot $nativePath $nativeRoot 'Rollback native reader')
    if (-not (Test-Path -LiteralPath $nativePath -PathType Leaf)) { throw ('Rollback native reader is missing: ' + $nativePath) }
    $nativeHash = Get-HostSha256 $nativePath
    if ($nativeHash -cne $script:ExpectedNativeExeSha256 -or ([string](Get-HostProperty $Journal 'nativeExeSha256')).ToUpperInvariant() -cne $nativeHash) {
        throw ('Rollback native reader hash mismatch: pinned=' + $script:ExpectedNativeExeSha256 + ' journal=' + [string](Get-HostProperty $Journal 'nativeExeSha256') + ' actual=' + $nativeHash)
    }
    if (([string](Get-HostProperty $Journal 'certificateThumbprint')).ToUpperInvariant() -cne $script:ExpectedCertificateThumbprint) {
        throw ('Rollback signer pin mismatch: expected=' + $script:ExpectedCertificateThumbprint + ' actual=' + [string](Get-HostProperty $Journal 'certificateThumbprint'))
    }

    $mutationStarted = [bool](Get-HostProperty $Journal 'mutationStarted')
    $snapshots = @(Get-HostProperty $Journal 'adapterSnapshots')
    if (-not $mutationStarted) { return [ordered]@{ mutationStarted = $false; adapterSnapshots = $snapshots.Count; lockedTsvSha256 = $tsvHash; nativeExeSha256 = $nativeHash } }
    if ($snapshots.Count -ne 4) { throw ('Rollback requires exactly four durable adapter snapshots before any mutation; actual=' + $snapshots.Count) }

    $bindings = @(Get-HostLockedBindings $recordedTsv)
    $bindingByPnp = @{}
    foreach ($binding in $bindings) { $bindingByPnp[([string]$binding.pnpDeviceId).ToUpperInvariant()] = $binding }
    $seenPnp = @{}
    $seenGuid = @{}
    $classPrefix = 'SYSTEM\CurrentControlSet\Control\Class\{4d36e972-e325-11ce-bfc1-08002be10318}\'
    foreach ($snapshot in $snapshots) {
        $pnp = [string](Get-HostProperty $snapshot 'pnpDeviceId')
        $pnpKey = $pnp.ToUpperInvariant()
        if (-not $bindingByPnp.ContainsKey($pnpKey) -or $seenPnp.ContainsKey($pnpKey)) { throw ('Rollback snapshot has an unexpected or duplicate adapter identity: ' + $pnp) }
        $seenPnp[$pnpKey] = $true
        $binding = $bindingByPnp[$pnpKey]
        if ((ConvertTo-HostMacKey (Get-HostProperty $snapshot 'original')) -cne (ConvertTo-HostMacKey $binding.original) -or
            (ConvertTo-HostMacKey (Get-HostProperty $snapshot 'replacement')) -cne (ConvertTo-HostMacKey $binding.replacement)) {
            throw ('Rollback snapshot value pair differs from the locked TSV: pnp=' + $pnp + ' expected=' + $binding.original + ' -> ' + $binding.replacement + ' actual=' + [string](Get-HostProperty $snapshot 'original') + ' -> ' + [string](Get-HostProperty $snapshot 'replacement'))
        }
        $guid = ([Guid]::Parse([string](Get-HostProperty $snapshot 'interfaceGuid'))).ToString('D').ToUpperInvariant()
        if ($seenGuid.ContainsKey($guid)) { throw ('Rollback snapshot repeats interface GUID ' + $guid) }
        $seenGuid[$guid] = $true
        $subKey = [string](Get-HostProperty $snapshot 'registrySubKey')
        $keyName = [string](Get-HostProperty $snapshot 'registryKeyName')
        if (-not $subKey.StartsWith($classPrefix, [StringComparison]::OrdinalIgnoreCase) -or $subKey.Substring($classPrefix.Length) -cnotmatch '\A[0-9]{4}\z' -or $subKey.Substring($classPrefix.Length) -cne $keyName) {
            throw ('Rollback snapshot registry key is outside the exact NIC class-key scope: pnp=' + $pnp + ' subKey=' + $subKey + ' keyName=' + $keyName)
        }
        $registryPnp = [string](Get-HostProperty $snapshot 'registryPnpDeviceId')
        if (-not [string]::IsNullOrWhiteSpace($registryPnp) -and -not [string]::Equals($registryPnp, $pnp, [StringComparison]::OrdinalIgnoreCase)) {
            throw ('Rollback snapshot registry PnP identity mismatch: expected=' + $pnp + ' actual=' + $registryPnp)
        }
        $networkAddress = Get-HostProperty $snapshot 'networkAddress'
        [void](New-NicAddressRestorePlan ([ordered]@{ networkAddress = $networkAddress }))
        $adminState = Get-HostProperty $snapshot 'adminState'
        if ($null -eq (Get-HostProperty $adminState 'enabled') -or (Get-HostProperty $adminState 'source') -cnotin @('Get-NetAdapter.AdminStatus', 'MSFT_NetAdapter.AdminStatus')) {
            throw ('Rollback snapshot administrative-state evidence is invalid: pnp=' + $pnp)
        }
        if ($null -eq (Get-HostProperty $snapshot 'linkConnected') -or [string]::IsNullOrWhiteSpace([string](Get-HostProperty $snapshot 'linkStateSource'))) {
            throw ('Rollback snapshot link-state evidence is missing: pnp=' + $pnp)
        }
    }
    if ($seenPnp.Count -ne $bindingByPnp.Count) { throw ('Rollback snapshot is missing one or more locked adapters: expected=' + $bindingByPnp.Count + ' actual=' + $seenPnp.Count) }
    return [ordered]@{ mutationStarted = $true; adapterSnapshots = $snapshots.Count; lockedTsvSha256 = $tsvHash; nativeExeSha256 = $nativeHash }
}

function Invoke-HostRollback {
    param([Parameter(Mandatory = $true)][string]$ResolvedRunDirectory, [Parameter(Mandatory = $true)][string]$RepositoryRoot)
    $journalPath = Join-Path $ResolvedRunDirectory 'journal.json'
    if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf)) { throw ('Run journal is missing: ' + $journalPath) }
    $journal = ConvertTo-HostMutableData (Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json)
    if ((Get-HostProperty $journal 'schema') -cne $script:HostRunSchema -or [IO.Path]::GetFullPath([string](Get-HostProperty $journal 'runDirectory')) -cne $ResolvedRunDirectory) { throw 'Run journal schema/path does not match the requested rollback directory.' }
    Assert-HostAdministrator
    Assert-HostIdentity
    [void](Assert-HostJournalSnapshot $journal $RepositoryRoot)
    $errors = @()
    if ([bool](Get-HostProperty $journal 'mutationStarted')) { $errors = @(Restore-HostAdapters $journal) }
    $nativePath = [string](Get-HostProperty $journal 'nativeExe')
    $tsvPath = [string](Get-HostProperty $journal 'lockedMap')
    $rollbackReadback = $null
    $dot11Baseline = Get-HostProperty (Get-HostProperty $journal 'preflightOriginal') 'dot11ByPnp'
    try { $rollbackReadback = Invoke-HostReadback 'Original' $nativePath $tsvPath (Join-Path $ResolvedRunDirectory 'rollback-readback') $dot11Baseline }
    catch { $errors += [ordered]@{ stage = 'readback-original'; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId } }
    $platform = $null
    try {
        $platform = Get-HostPlatformSnapshot (Join-Path $ResolvedRunDirectory 'rollback-platform') ([string]$journal.certificateThumbprint)
        if ($null -ne $journal.platformBefore) { Assert-HostPlatformUnchanged $journal.platformBefore $platform }
        elseif ([bool](Get-HostProperty $journal 'mutationStarted')) { throw 'Mutation was recorded but no complete pre-mutation platform snapshot exists.' }
    }
    catch { $errors += [ordered]@{ stage = 'verify-platform'; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId } }
    $journal.rollbackErrors = @($errors)
    $journal.rollbackReadback = $rollbackReadback
    $journal.rollbackCompletedUtc = [DateTime]::UtcNow.ToString('o')
    $journal.state = if ($errors.Count -eq 0) { 'ROLLED_BACK_AND_VERIFIED' } else { 'ROLLBACK_INCOMPLETE' }
    Write-HostJournal $journal
    if ($errors.Count -gt 0) { throw ('Rollback incomplete. See exact per-step errors in ' + $journalPath + ': ' + (ConvertTo-Json -InputObject $errors -Compress -Depth 10)) }
    return [ordered]@{ status = $journal.state; runDirectory = $ResolvedRunDirectory; readback = $rollbackReadback; journal = $journalPath }
}

function Invoke-HostRun {
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot, [Parameter(Mandatory = $true)][string]$WorktreeRoot)
    Assert-HostAdministrator
    $reportsRoot = Join-Path $RepositoryRoot 'spoof-handoff\reports'
    $evidenceRoot = Join-Path $reportsRoot 'nic-reader-gap-20261017\host-runs'
    [void][IO.Directory]::CreateDirectory($evidenceRoot)
    $runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
    $run = Join-Path $evidenceRoot $runId
    [void][IO.Directory]::CreateDirectory($run)
    $temporaryDirectory = Join-Path $run 'temporary'
    [void][IO.Directory]::CreateDirectory($temporaryDirectory)
    $env:TEMP = $temporaryDirectory
    $env:TMP = $temporaryDirectory
    $script:ActiveHostTemporaryDirectory = $temporaryDirectory
    $tsv = Join-Path $reportsRoot 'client-originals.tsv'
    $bindings = @(Get-HostLockedBindings $tsv)
    $nativePath = if ([string]::IsNullOrWhiteSpace($NativeExe)) { Join-Path $RepositoryRoot 'spoof-handoff\artifacts\nic-reader-gap-20261017\native-build-vs18\Release\nic_native_read.exe' } else { [IO.Path]::GetFullPath($NativeExe) }
    if (-not (Test-Path -LiteralPath $nativePath -PathType Leaf)) { throw ('Pinned native read-only collector is missing: ' + $nativePath) }
    $thumbprint = $script:ExpectedCertificateThumbprint
    Assert-HostIdentity

    $gatePath = if ([string]::IsNullOrWhiteSpace($VmGatePath)) { Join-Path $reportsRoot 'nic-reader-gap-20261017\vm-seven-reader-gate.json' } else { [IO.Path]::GetFullPath($VmGatePath) }
    $gate = Assert-HostVmGate $gatePath $RepositoryRoot $tsv $nativePath
    $registryVmCapturePath = Join-Path $RepositoryRoot $script:ExpectedRegistryVmCaptureRelativePath
    $registryVmCapture = Assert-HostRegistryVmCapture -CapturePath $registryVmCapturePath -ExpectedCaptureSha256 $script:ExpectedRegistryVmCaptureSha256 -LockedBindings $bindings -RepositoryRoot $RepositoryRoot
    $vmnetDriverPin = Assert-HostVmnetAdapterDriverPin
    $journalPath = Join-Path $run 'journal.json'
    $script:HostBindings = $bindings
    $journal = [ordered]@{
        schema = $script:HostRunSchema
        runId = $runId
        runDirectory = $run
        temporaryDirectory = $temporaryDirectory
        repositoryRoot = $RepositoryRoot
        worktreeRoot = $WorktreeRoot
        state = 'PREFLIGHT'
        startedUtc = [DateTime]::UtcNow.ToString('o')
        nativeExe = $nativePath
        nativeExeSha256 = Get-HostSha256 $nativePath
        lockedMap = $tsv
        lockedMapSha256 = Get-HostSha256 $tsv
        certificateThumbprint = $thumbprint
        hostIdentity = [ordered]@{ baseboardManufacturer = $script:ExpectedBaseboardManufacturer; uuid = $script:ExpectedHostUuid }
        vmGate = $gate
        registryVmCapture = $registryVmCapture
        vmnetAdapterDriverPin = $vmnetDriverPin
        backend = 'RegistryNetworkAddress'
        adapterSnapshots = @()
        adapterProgress = @()
        linkRestoreChecks = @()
        readbacks = [ordered]@{ preflightOriginal = $null; replacement = $null; originalAfterRestore = $null }
        platformBefore = $null
        platformAfter = $null
        mutationStarted = $false
        journalPath = $journalPath
    }
    Write-HostJournal $journal
    $mutationStarted = $false
    $failure = $null
    try {
        $journal.preflightOriginal = Invoke-HostReadback 'Original' $nativePath $tsv (Join-Path $run 'readback-original')
        $journal.readbacks.preflightOriginal = $journal.preflightOriginal
        $journal.platformBefore = Get-HostPlatformSnapshot (Join-Path $run 'platform-before') $thumbprint
        $journal.state = 'PLATFORM_SNAPSHOTTED'
        Write-HostJournal $journal
        $journal.adapterSnapshots = @(Get-HostAdapterSnapshots $bindings)
        if ($journal.adapterSnapshots.Count -ne 4) { throw ('Adapter snapshot count must be 4; actual=' + $journal.adapterSnapshots.Count) }
        $journal.adapterProgress = @($journal.adapterSnapshots | ForEach-Object { [ordered]@{ pnpDeviceId = [string]$_.pnpDeviceId; state = 'snapshotted' } })
        $journal.state = 'BASELINE_SNAPSHOTTED'
        Write-HostJournal $journal

        foreach ($snapshot in $journal.adapterSnapshots) {
            $progress = @($journal.adapterProgress | Where-Object { [string]::Equals([string]$_.pnpDeviceId, [string]$snapshot.pnpDeviceId, [StringComparison]::OrdinalIgnoreCase) })[0]
            [void](Invoke-HostAdapterRegistryApply -Snapshot $snapshot -Journal $journal -Progress $progress -MutationStarted ([ref]$mutationStarted))
        }
        $journal.state = 'REPLACEMENT_CONFIGURED'
        Write-HostJournal $journal
        $journal.readbacks.replacement = Invoke-HostReadback 'Replacement' $nativePath $tsv (Join-Path $run 'readback-replacement') $journal.preflightOriginal.dot11ByPnp
        $journal.state = 'REPLACEMENT_READBACK_PASS'
        Write-HostJournal $journal
    }
    catch { $failure = $_ }

    if ($mutationStarted) {
        $journal.state = if ($null -eq $failure) { 'RESTORING_AFTER_PASS' } else { 'RESTORING_AFTER_FAILURE' }
        $restoreErrors = @()
        try { Write-HostJournal $journal }
        catch { $restoreErrors += [ordered]@{ stage = 'persist-before-restore'; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId } }
        $restoreErrors += @(Restore-HostAdapters $journal)
    }
    else { $restoreErrors = @() }

    try { $journal.readbacks.originalAfterRestore = Invoke-HostReadback 'Original' $nativePath $tsv (Join-Path $run 'readback-original-restored') $journal.preflightOriginal.dot11ByPnp }
    catch {
        $restoreErrors += [ordered]@{ stage = 'final-original-readback'; exceptionType = $_.Exception.GetType().FullName; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId }
    }
    try {
        $journal.platformAfter = Get-HostPlatformSnapshot (Join-Path $run 'platform-after') $thumbprint
        if ($null -ne $journal.platformBefore) { Assert-HostPlatformUnchanged $journal.platformBefore $journal.platformAfter }
    }
    catch {
        $restoreErrors += [ordered]@{ stage = 'platform-restore-verification'; exceptionType = $_.Exception.GetType().FullName; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId }
    }
    $journal.restoreErrors = @($restoreErrors)
    $journal.failure = if ($null -ne $failure) { [ordered]@{ exceptionType = $failure.Exception.GetType().FullName; exception = $failure.Exception.ToString(); fullyQualifiedErrorId = $failure.FullyQualifiedErrorId; category = [string]$failure.CategoryInfo.Category } } else { $null }
    $journal.completedUtc = [DateTime]::UtcNow.ToString('o')
    $journal.state = if (-not $mutationStarted -and $null -ne $failure) { 'PREFLIGHT_FAILED_NO_ADAPTER_CHANGES' } elseif ($restoreErrors.Count -eq 0 -and $null -eq $failure) { 'ROUND_TRIP_PASS_AND_RESTORED' } elseif ($restoreErrors.Count -eq 0) { 'FAILED_BUT_RESTORED' } else { 'RESTORE_OR_FINAL_VERIFICATION_FAILED' }
    Write-HostJournal $journal
    $summary = [ordered]@{ schema = 'nic-host-address-roundtrip-summary.v1'; status = [string]$journal.state; runDirectory = $run; journalPath = $journalPath; journalSha256 = Get-HostSha256 $journalPath; vmGateSha256 = [string]$gate.sha256; originalReadback = $journal.readbacks.originalAfterRestore; replacementReadback = $journal.readbacks.replacement; restoreErrors = @($restoreErrors); originalFailure = $journal.failure }
    Write-HostJsonAtomic (Join-Path $run 'summary.json') $summary
    if ($journal.state -cne 'ROUND_TRIP_PASS_AND_RESTORED') {
        if ($null -ne $failure) { throw ('NIC round-trip failed; state=' + $journal.state + '; original error=' + $failure.Exception.ToString() + '; evidence=' + $run) }
        throw ('NIC round-trip restoration/final verification failed; details are in ' + $run)
    }
    return $summary
}

$roots = Get-HostRepositoryRoots
$mainRoot = $roots.main
$worktreeRoot = $roots.worktree
$originalTemp = $env:TEMP
$originalTmp = $env:TMP
try {
    if ($Action -ceq 'Run') {
        if (-not [string]::IsNullOrWhiteSpace($RunDirectory)) { throw 'Run does not accept -RunDirectory; each run receives a unique evidence directory.' }
        $result = Invoke-HostRun $mainRoot $worktreeRoot
        $result | ConvertTo-Json -Depth 12
    }
    else {
        if ([string]::IsNullOrWhiteSpace($RunDirectory)) { throw 'Rollback requires -RunDirectory pointing to an existing host run directory.' }
        $resolvedRun = Assert-PathWithinRoot $RunDirectory (Join-Path $mainRoot 'spoof-handoff\reports\nic-reader-gap-20261017\host-runs') 'RunDirectory'
        $temporaryDirectory = Join-Path $resolvedRun 'temporary'
        [void][IO.Directory]::CreateDirectory($temporaryDirectory)
        $env:TEMP = $temporaryDirectory
        $env:TMP = $temporaryDirectory
        $script:ActiveHostTemporaryDirectory = $temporaryDirectory
        $result = Invoke-HostRollback $resolvedRun $mainRoot
        $result | ConvertTo-Json -Depth 12
    }
}
finally {
    if ($null -eq $originalTemp) { Remove-Item Env:TEMP -ErrorAction SilentlyContinue } else { $env:TEMP = $originalTemp }
    if ($null -eq $originalTmp) { Remove-Item Env:TMP -ErrorAction SilentlyContinue } else { $env:TMP = $originalTmp }
}
