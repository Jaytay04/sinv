[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GuestCapturePath,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [string]$CanonicalMapPath = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\spoof-handoff\reports\client-originals.tsv'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$authorizedReports = [IO.Path]::GetFullPath((Join-Path $mainRoot 'spoof-handoff\reports\combined-9field-vm-20261018'))
$guestCaptureFull = [IO.Path]::GetFullPath($GuestCapturePath)
$outputFull = [IO.Path]::GetFullPath($OutputPath)
$canonicalFull = [IO.Path]::GetFullPath($CanonicalMapPath)
$outputPrefix = $authorizedReports.TrimEnd('\') + '\'
if (-not $guestCaptureFull.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not (Split-Path -Leaf $guestCaptureFull).Equals('kernel-metadata.json', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'GuestCapturePath must identify an authenticated kernel-metadata.json under the combined VM reports tree.'
}
if (-not $outputFull.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not $outputFull.EndsWith('.tsv', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputPath must be a TSV under the combined VM reports tree.'
}
if (-not $canonicalFull.Equals((Join-Path $mainRoot 'spoof-handoff\reports\client-originals.tsv'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'CanonicalMapPath must be the pinned client-originals.tsv.'
}
foreach ($path in @($guestCaptureFull, $canonicalFull)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required input is missing: $path" }
}

function Normalize-Mac([string]$Value) {
    $hex = $Value -replace '[^0-9A-Fa-f]', ''
    if ($hex -notmatch '\A[0-9A-Fa-f]{12}\z') { throw "Invalid six-byte MAC address: $Value" }
    return $hex.ToUpperInvariant()
}

function Get-One([object[]]$Rows, [scriptblock]$Predicate, [string]$Label) {
    $found = @($Rows | Where-Object $Predicate)
    if ($found.Count -ne 1) { throw "Expected exactly one $Label; found $($found.Count)." }
    return $found[0]
}

$capture = Get-Content -LiteralPath $guestCaptureFull -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
if ($capture.schema -cne 'combined-vm-original-inventory.v1' -or
    $capture.machine.manufacturer -cne 'VMware, Inc.' -or
    $capture.machine.model -cne 'VMware20,1' -or
    $capture.smbiosCapture.result -cne 'PASS' -or
    $capture.nic.nativeCapture.captureComplete -ne $true -or
    $capture.nic.nativeCapture.readOnly -ne $true -or
    $capture.nic.nativeCapture.replacementEnabled -ne $false -or
    $capture.nic.nativeCapture.networkConfigurationChanged -ne $false) {
    throw 'Guest capture is not a complete read-only inventory from the approved VMware model.'
}
if ([string]$capture.machine.uuidWmi -cne [string]$capture.machine.uuidSmbios) {
    throw 'Fresh guest SMBIOS and WMI UUIDs differ.'
}
if ([string]$capture.disk.path -cne '\\.\PHYSICALDRIVE0' -or
    [int]$capture.disk.number -ne 0 -or $capture.disk.isBoot -ne $true -or
    $capture.disk.isSystem -ne $true -or
    [string]$capture.disk.nativeSerial -cne [string]$capture.disk.wmiSerial) {
    throw 'Fresh guest PHYSICALDRIVE0 identity, boot/system status, or native/WMI equality check failed.'
}

$canonical = @(Import-Csv -LiteralPath $canonicalFull -Delimiter "`t" -ErrorAction Stop)
$expectedMembers = @(
    'FakeSMBIOSSerial', 'FakeSMBIOSManufacturer', 'FakeSMBIOSProduct',
    'FakeMotherboardSerial', 'FakeMotherboardManufacturer', 'FakeMotherboardProduct',
    'FakeSMBIOSUUID'
)
$smbiosFields = [ordered]@{
    FakeSMBIOSSerial = 'system_serial'
    FakeSMBIOSManufacturer = 'system_vendor'
    FakeSMBIOSProduct = 'system_product'
    FakeMotherboardSerial = 'board_serial'
    FakeMotherboardManufacturer = 'board_vendor'
    FakeMotherboardProduct = 'board_product'
    FakeSMBIOSUUID = 'system_uuid'
}
$rows = [Collections.Generic.List[object]]::new()
foreach ($member in $expectedMembers) {
    $canonicalRow = Get-One $canonical { [string]$_.member -ceq $member } "canonical $member row"
    $field = [string]$smbiosFields[$member]
    $observed = $capture.smbiosCapture.fields.$field
    if ($null -eq $observed -or $observed.nativeMatchesWmi -ne $true -or
        [string]$observed.observedNative -cne [string]$observed.observedWmi) {
        throw "Fresh guest SMBIOS RSMB/WMI values did not match for $field."
    }
    $rows.Add([pscustomobject][ordered]@{
        member = $member
        device = [string]$canonicalRow.device
        original = [string]$observed.observedNative
        replacement = [string]$canonicalRow.replacement
    })
}

$canonicalDisk = Get-One $canonical { [string]$_.member -ceq 'FakeDiskSerial' -and [string]$_.device -match '(?i)PHYSICALDRIVE0\s*$' } 'canonical PHYSICALDRIVE0 row'
$rows.Add([pscustomobject][ordered]@{
    member = 'FakeDiskSerial'
    device = [string]$canonicalDisk.device
    original = [string]$capture.disk.nativeSerial
    replacement = [string]$canonicalDisk.replacement
})

$native = $capture.nic.nativeCapture
$nativeInterfaces = @($native.setupDiNdisLanClass.interfaces)
$targetPnpPattern = '^(?i:ROOT\\NET\\000[01]|PCI\\VEN_15AD&DEV_07B0)'
$targetNativeInterfaces = @($nativeInterfaces | Where-Object { [string]$_.pnpDeviceId -match $targetPnpPattern })
if ($native.setupDiNdisLanClass.complete -ne $true -or $native.setupDiNdisLanClass.enumerationError -ne 0 -or
    $targetNativeInterfaces.Count -ne 4) {
    throw "Native SetupDi/NDIS enumeration did not return exactly four requested VM adapters; count=$($targetNativeInterfaces.Count)."
}
if (@($nativeInterfaces | Where-Object { [string]$_.pnpDeviceId -match '^(?i:PCI\\VEN_8086&DEV_10D3)' }).Count -ne 0) {
    throw 'Unexpected e1000 adapter is present in the target VM topology.'
}

$wmiRows = @($capture.nic.wmiAdapters)
$netRows = @($capture.nic.getNetAdapter)
$allTargetPnp = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($nativeInterface in $targetNativeInterfaces) {
    $pnp = [string]$nativeInterface.pnpDeviceId
    if (-not $allTargetPnp.Add($pnp)) { throw "Duplicate native PnP identity: $pnp" }
    $oid = $nativeInterface.ethernetAddress
    if ($oid.ioctlSucceeded -ne $true -or [int]$oid.bytesReturned -ne 6 -or [int]$oid.bytesCaptured -ne 6) {
        throw "NDIS OID_802_3_CURRENT_ADDRESS failed for $pnp (Win32=$($oid.win32Error))."
    }
    $oidMac = [string]$oid.mac
    $oidNormalized = Normalize-Mac $oidMac
    $wmi = Get-One $wmiRows { [string]$_.PNPDeviceID -ieq $pnp } "fresh Win32_NetworkAdapter row for $pnp"
    $net = Get-One $netRows { [string]$_.PnPDeviceID -ieq $pnp } "fresh Get-NetAdapter row for $pnp"
    if ((Normalize-Mac ([string]$wmi.MACAddress)) -cne $oidNormalized -or
        (Normalize-Mac ([string]$net.MacAddress)) -cne $oidNormalized) {
        throw "Fresh OID/WMI/Get-NetAdapter MAC mismatch for $pnp."
    }
    $guid = [string]$net.InterfaceGuid
    if ([string]::IsNullOrWhiteSpace($guid)) { throw "Get-NetAdapter did not provide InterfaceGuid for $pnp." }
    $guid = $guid.Trim('{}')
    $address = Get-One @($native.getAdaptersAddresses.entries) { ([string]$_.adapterName).Trim('{}') -ieq $guid } "GetAdaptersAddresses row for $pnp"
    $info = Get-One @($native.getAdaptersInfo.entries) { ([string]$_.adapterName).Trim('{}') -ieq $guid } "GetAdaptersInfo row for $pnp"
    $ifTable = Get-One @($native.getIfTable2.entries) { ([string]$_.interfaceGuid).Trim('{}') -ieq $guid } "GetIfTable2 row for $pnp"
    if ((Normalize-Mac ([string]$address.physicalAddress)) -cne $oidNormalized -or
        (Normalize-Mac ([string]$info.addressBytesHex)) -cne $oidNormalized -or
        (Normalize-Mac ([string]$ifTable.physicalAddress)) -cne $oidNormalized) {
        throw "Fresh NDIS OID and ordinary adapter API MACs differ for $pnp."
    }
    $canonicalMac = Get-One $canonical {
        [string]$_.member -ceq 'FakeMacAddress' -and
        (Normalize-Mac ([string]$_.original)) -ceq $oidNormalized
    } "canonical replacement MAC for $oidMac"
    $interfaceIndex = [int]$net.ifIndex
    if ($interfaceIndex -lt 1) { throw "Get-NetAdapter returned an invalid interface index for $pnp." }
    $rows.Add([pscustomobject][ordered]@{
        member = 'FakeMacAddress'
        device = 'adapter:' + $interfaceIndex + '|' + $pnp
        original = $oidMac
        replacement = [string]$canonicalMac.replacement
    })
}
if ($allTargetPnp.Count -ne 4) { throw 'The target NIC PnP identity set is incomplete.' }
$diskRows = @($canonical | Where-Object { [string]$_.member -ceq 'FakeDiskSerial' })
$macRows = @($canonical | Where-Object { [string]$_.member -ceq 'FakeMacAddress' })
if ($diskRows.Count -ne 1 -or $macRows.Count -ne 4 -or $rows.Count -ne 12) {
    throw "Canonical binding cardinality mismatch: disk=$($diskRows.Count), NIC=$($macRows.Count), generated=$($rows.Count)."
}

$header = 'member' + "`t" + 'device' + "`t" + 'original' + "`t" + 'replacement'
$lines = [Collections.Generic.List[string]]::new()
$lines.Add($header)
foreach ($row in $rows) {
    $cells = @([string]$row.member, [string]$row.device, [string]$row.original, [string]$row.replacement)
    foreach ($cell in $cells) {
        if ($cell -match '[\t\r\n]') { throw "Map cell contains a tab or newline: $($row.member)" }
    }
    $lines.Add(($cells -join "`t"))
}
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $outputFull))
[IO.File]::WriteAllLines($outputFull, $lines.ToArray(), [Text.UTF8Encoding]::new($false))
$mapHash = (Get-FileHash -LiteralPath $outputFull -Algorithm SHA256).Hash.ToUpperInvariant()
$canonicalHash = (Get-FileHash -LiteralPath $canonicalFull -Algorithm SHA256).Hash.ToUpperInvariant()
$captureHash = (Get-FileHash -LiteralPath $guestCaptureFull -Algorithm SHA256).Hash.ToUpperInvariant()
$manifestPath = [IO.Path]::ChangeExtension($outputFull, '.manifest.json')
[ordered]@{
    schema = 'combined-vm-fixture-map-provenance.v1'
    mapPath = $outputFull
    mapSha256 = $mapHash
    mapRows = $rows.Count
    vmOriginalsUsed = $true
    canonicalReplacementSource = $canonicalFull
    canonicalSourceSha256 = $canonicalHash
    authenticatedGuestCapture = $guestCaptureFull
    authenticatedGuestCaptureSha256 = $captureHash
    guestUuid = [string]$capture.machine.uuidWmi
    guestDiskPnp = [string]$capture.disk.pnpDeviceId
    targetAdapters = @($rows | Where-Object { $_.member -ceq 'FakeMacAddress' } | ForEach-Object {
        [ordered]@{ device=$_.device; original=$_.original; replacement=$_.replacement }
    })
    usbIncluded = $false
    generatedUtc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "VM FIXTURE MAP WRITTEN: $outputFull"
Write-Output "MAP SHA256: $mapHash"
Write-Output "PROVENANCE: $manifestPath"
