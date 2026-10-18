[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GuestInventory,
    [Parameter(Mandatory = $true)][string]$SourceMetadataPath,
    [string]$BaselineMetadata = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\spoof-handoff\reports\combined-9field-vm-20261018\uefi-windows-probe-20261018-020825-1ad5e1e906\evidence\kernel-metadata.json',
    [string]$ReplacementMap = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\spoof-handoff\reports\client-originals.tsv'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$worktreeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $worktreeRoot '..\..\..'))
$combinedReportRoot = Join-Path $repositoryRoot 'spoof-handoff\reports\combined-9field-vm-20261018\storage-fixtures'
$deviceBuildScript = Join-Path $worktreeRoot 'spoof-handoff\tools\device_vm\build-storage.ps1'
$deviceReportRoot = Join-Path $repositoryRoot 'spoof-handoff\reports\device-vm-20261017\build'
$deviceArtifactRoot = Join-Path $repositoryRoot 'spoof-handoff\artifacts\device-vm-20261017\build'
$pinnedThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
$expectedReplacement = '1111_1111_1111_1111_A1B2_C3D4_5566_7788.'

function Assert-PathUnderRoot([string]$Path, [string]$Root, [string]$Label) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw ($Label + ' must remain under ' + $Root + ': ' + $fullPath)
    }
}

function Read-Json([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw ($Label + ' was not found: ' + $Path) }
    try { return (Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop) }
    catch { throw ($Label + ' is not valid JSON: ' + $_.Exception.Message) }
}

function Get-OneProperty($Object, [string]$Name, [string]$Label) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) { throw ($Label + ' is missing ' + $Name + '.') }
    return $property.Value
}

function Get-OptionalBoolean($Object, [string]$Name, [bool]$DefaultValue, [string]$Label) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) { return $DefaultValue }
    if ($property.Value -is [bool]) { return [bool]$property.Value }
    $text = [string]$property.Value
    if ($text -match '^(?i:true|false)$') { return ($text -ieq 'true') }
    throw ($Label + ' must be a Boolean value.')
}

function Get-NewBuildProfile([string[]]$PreviousRunIds, [string]$Mode, [string]$InventoryHash, [string]$Replacement) {
    $candidates = @()
    foreach ($directory in Get-ChildItem -LiteralPath $deviceReportRoot -Directory -ErrorAction Stop) {
        if ($directory.Name -in $PreviousRunIds) { continue }
        $profilePath = Join-Path $directory.FullName 'build-profile.json'
        if (-not (Test-Path -LiteralPath $profilePath -PathType Leaf)) { continue }
        try { $profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json -ErrorAction Stop }
        catch { continue }
        if ([string]$profile.guestInventorySha256 -ceq $InventoryHash -and
            [string]$profile.mode -ceq $Mode -and
            [int]$profile.startType -eq 0 -and
            [string]$profile.replacementSerial -ceq $Replacement -and
            [string]$profile.status -ceq 'signed VM-targeted storage package prepared') {
            $candidates += [pscustomobject]@{ path = $profilePath; profile = $profile }
        }
    }
    if ($candidates.Count -ne 1) {
        throw ('Expected exactly one new signed build profile for mode ' + $Mode + '; found ' + $candidates.Count + '.')
    }
    return $candidates[0]
}

function Assert-SignedPackage($BuildRecord, [string]$Mode, [string]$Replacement) {
    $profile = $BuildRecord.profile
    if ([string]$profile.status -cne 'signed VM-targeted storage package prepared' -or
        [string]$profile.mode -cne $Mode -or [int]$profile.startType -ne 0 -or
        [string]$profile.replacementSerial -cne $Replacement -or
        [string]$profile.signingCertificateThumbprint -cne $pinnedThumbprint -or
        [bool]$profile.driverInstalledOrLoaded -or [bool]$profile.bootConfigurationChanged -or
        [bool]$profile.hostTrustStoresChanged) {
        throw ('The ' + $Mode + ' build profile has an unexpected mode, signer, package, or side-effect marker.')
    }
    if ($null -eq $profile.packageFiles -or @($profile.packageFiles).Count -ne 3) {
        throw ('The ' + $Mode + ' package file manifest is incomplete.')
    }
    foreach ($file in @($profile.packageFiles)) {
        $path = Join-Path ([string]$profile.package) ([string]$file.name)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('Signed package file is missing: ' + $path) }
        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actualHash -cne [string]$file.sha256) { throw ('Signed package hash mismatch: ' + $path) }
    }
    if (-not [bool]$profile.embeddedDriverSignature.pkcs7SignatureValid -or
        -not [bool]$profile.embeddedDriverSignature.pinnedCertificateSignatureValid -or
        -not [bool]$profile.catalogSignature.pkcs7SignatureValid -or
        -not [bool]$profile.catalogSignature.pinnedCertificateSignatureValid) {
        throw ('The ' + $Mode + ' package signature evidence is incomplete.')
    }
}

$guestInventoryPath = [IO.Path]::GetFullPath($GuestInventory)
$authenticatedMetadataPath = [IO.Path]::GetFullPath($SourceMetadataPath)
$baselinePath = [IO.Path]::GetFullPath($BaselineMetadata)
$mapPath = [IO.Path]::GetFullPath($ReplacementMap)
$guestReportRoot = Join-Path $repositoryRoot 'spoof-handoff\reports\combined-9field-vm-20261018'
Assert-PathUnderRoot $guestInventoryPath $guestReportRoot 'Guest inventory'
Assert-PathUnderRoot $authenticatedMetadataPath $guestReportRoot 'Authenticated source metadata'
Assert-PathUnderRoot $baselinePath $guestReportRoot 'Authenticated baseline metadata'
Assert-PathUnderRoot $mapPath (Join-Path $repositoryRoot 'spoof-handoff\reports') 'Replacement map'

foreach ($path in @($deviceBuildScript, $guestInventoryPath, $authenticatedMetadataPath, $baselinePath, $mapPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('Required input is missing: ' + $path) }
}

$guest = Read-Json $guestInventoryPath 'Guest inventory'
$sourceMetadata = Read-Json $authenticatedMetadataPath 'Authenticated source metadata'
$baseline = Read-Json $baselinePath 'Authenticated baseline metadata'
if ([string]$guest.schema -cne 'vm-device-inventory.v1') {
    throw 'Guest inventory must use the vm-device-inventory.v1 schema.'
}
if ($null -ne $guest.PSObject.Properties['operation'] -and [string]$guest.operation -cne 'read-only-inventory') {
    throw 'Guest inventory operation must be read-only-inventory when present.'
}
if ((Get-OptionalBoolean $guest 'synthetic' $false 'Guest synthetic marker') -or
    (Get-OptionalBoolean $guest 'hostOrGuestSettingsChanged' $false 'Guest settings marker')) {
    throw 'Synthetic inventory or an inventory reporting settings changes cannot produce packages.'
}
if ([string]$baseline.schema -cne 'combined-vm-original-inventory.v1' -or
    [string]$baseline.machine.uuidWmi -cne [string]$baseline.machine.uuidSmbios) {
    throw 'Authenticated baseline does not contain one consistent guest UUID.'
}
if ([string]$sourceMetadata.schema -cne 'combined-vm-original-inventory.v1') {
    throw 'Authenticated source metadata must use combined-vm-original-inventory.v1.'
}
$guestInventoryHash = (Get-FileHash -LiteralPath $guestInventoryPath -Algorithm SHA256).Hash.ToUpperInvariant()
if ([string]$sourceMetadata.vmDeviceInventorySha256 -cne $guestInventoryHash) {
    throw 'The fresh authenticated metadata does not pin the supplied vm-device-inventory file hash.'
}
$inventoryRunId = [IO.Path]::GetFileNameWithoutExtension($guestInventoryPath)
if ($inventoryRunId -match '^vm-device-inventory\.(.+)$') { $inventoryRunId = $Matches[1] }
$metadataRunDirectory = Split-Path -Parent (Split-Path -Parent $authenticatedMetadataPath)
$expectedGuestInventoryPath = 'C:\ProgramData\UEFILoaderRunner\runs\' + $inventoryRunId + '\vm-device-inventory.json'
if ((Split-Path -Leaf $metadataRunDirectory) -cne $inventoryRunId -or
    -not [string]::Equals([string]$sourceMetadata.vmDeviceInventoryPath, $expectedGuestInventoryPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The supplied inventory and authenticated metadata do not belong to the same guest capture run.'
}
foreach ($changeField in @('testSigningChange', 'secureBootChange', 'networkChange', 'driverChange')) {
    if (Get-OptionalBoolean $sourceMetadata $changeField $false ('Authenticated ' + $changeField)) {
        throw ('Authenticated guest capture reports an unexpected state change: ' + $changeField)
    }
}
if ([string]$guest.machine.uuid -cne [string]$baseline.machine.uuidWmi) {
    throw ('Guest inventory UUID does not match the fresh authenticated baseline: expected ' +
        [string]$baseline.machine.uuidWmi + ', actual ' + [string]$guest.machine.uuid + '.')
}
if ([string]$guest.machine.manufacturer -notmatch '^(?i:VMware(?:,\s*Inc\.)?)$' -or
    [string]$guest.machine.model -cne [string]$baseline.machine.model -or
    [string]$guest.machine.model -cne 'VMware20,1') {
    throw 'Guest inventory does not match the authenticated VMware20,1 identity.'
}
if ([string]$sourceMetadata.machine.uuidWmi -cne [string]$guest.machine.uuid -or
    [string]$sourceMetadata.machine.uuidSmbios -cne [string]$guest.machine.uuid -or
    [string]$sourceMetadata.machine.manufacturer -cne [string]$guest.machine.manufacturer -or
    [string]$sourceMetadata.machine.model -cne [string]$guest.machine.model) {
    throw 'Guest inventory identity differs from the authenticated capture metadata.'
}
$disk = Get-OneProperty $guest 'disk' 'Guest inventory'
$baselineDisk = Get-OneProperty $baseline 'disk' 'Authenticated baseline'
if ([int](Get-OneProperty $disk 'number' 'Guest disk') -ne 0 -or
    -not (Get-OptionalBoolean $disk 'isBoot' $false 'Guest disk boot flag') -or
    -not (Get-OptionalBoolean $disk 'isSystem' $false 'Guest disk system flag')) {
    throw 'The guest inventory does not identify PHYSICALDRIVE0 as both boot and system disk.'
}
if ([string]$disk.devicePath -cne '\\.\PHYSICALDRIVE0' -or
    [string]$disk.pnpDeviceId -cne [string]$baselineDisk.pnpDeviceId -or
    [string]$disk.serial -cne [string]$baselineDisk.nativeSerial -or
    [string]$disk.wmiSerial -cne [string]$baselineDisk.wmiSerial -or
    [string]$disk.serial -cne [string]$disk.wmiSerial) {
    throw 'Guest disk path, PnP ID, native serial, or WMI serial does not match the authenticated current-VM baseline.'
}
foreach ($field in @('number', 'isBoot', 'isSystem', 'devicePath', 'pnpDeviceId', 'hardwareIds')) {
    $metadataField = if ($field -ceq 'devicePath') { [string]$sourceMetadata.disk.path }
        elseif ($field -ceq 'hardwareIds') { @($sourceMetadata.disk.hardwareIds) }
        else { $sourceMetadata.disk.PSObject.Properties[$field].Value }
    $guestField = $disk.PSObject.Properties[$field].Value
    if ($field -ceq 'hardwareIds') {
        if ((@($guestField) -join "`n") -cne (@($metadataField) -join "`n")) { throw 'Guest HardwareIds differ from authenticated capture metadata.' }
    } elseif ([string]$guestField -cne [string]$metadataField) {
        throw ('Guest disk ' + $field + ' differs from authenticated capture metadata.')
    }
}
if ([string]$disk.serial -cne [string]$sourceMetadata.disk.nativeSerial -or
    [string]$disk.wmiSerial -cne [string]$sourceMetadata.disk.wmiSerial) {
    throw 'Guest disk native or WMI serial differs from authenticated capture metadata.'
}
$guestWindowsBuild = [int](Get-OneProperty $guest.windows 'build' 'Guest Windows metadata')
$guestWindowsUbr = [int](Get-OneProperty $guest.windows 'ubr' 'Guest Windows metadata')
if ($guestWindowsBuild -lt 1 -or $guestWindowsUbr -lt 0) { throw 'Guest Windows build metadata is invalid.' }
if (@(Get-OneProperty $disk 'hardwareIds' 'Guest disk').Count -lt 1) { throw 'Guest disk HardwareIds is empty.' }

$replacementRows = @(Import-Csv -LiteralPath $mapPath -Delimiter "`t" | Where-Object { $_.member -ceq 'FakeDiskSerial' })
if ($replacementRows.Count -ne 1 -or [string]$replacementRows[0].replacement -cne $expectedReplacement) {
    throw 'The authoritative map must have exactly one FakeDiskSerial row with the locked replacement value.'
}
$replacementSerial = [string]$replacementRows[0].replacement
if ($replacementSerial.Length -ne ([string]$disk.serial).Length -or $replacementSerial -ceq [string]$disk.serial) {
    throw 'The locked disk replacement must differ from the fresh guest original and match its exact character length.'
}
if ([string]$disk.serial -notmatch '^[A-Z0-9_.-]+\.$' -or $replacementSerial -notmatch '^[A-Z0-9_.-]+\.$') {
    throw 'Guest and replacement serials do not meet the locked VMware disk serial format.'
}

$inventoryHash = (Get-FileHash -LiteralPath $guestInventoryPath -Algorithm SHA256).Hash.ToUpperInvariant()
$authenticatedMetadataHash = (Get-FileHash -LiteralPath $authenticatedMetadataPath -Algorithm SHA256).Hash.ToUpperInvariant()
$mapHash = (Get-FileHash -LiteralPath $mapPath -Algorithm SHA256).Hash.ToUpperInvariant()
$baselineHash = (Get-FileHash -LiteralPath $baselinePath -Algorithm SHA256).Hash.ToUpperInvariant()
$previousRuns = @(Get-ChildItem -LiteralPath $deviceReportRoot -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)

$powerShell = Join-Path $PSHOME 'powershell.exe'
$builds = @{}
foreach ($mode in @('transparent', 'enabled')) {
    $arguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $deviceBuildScript,
        '-GuestInventory', $guestInventoryPath,
        '-ReplacementSerial', $replacementSerial,
        '-Mode', $mode,
        '-StartType', '0'
    )
    $buildOutput = @(& $powerShell @arguments 2>&1 | ForEach-Object { [string]$_ })
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        throw ('Existing storage builder failed for ' + $mode + ' package (exit ' + $buildExitCode + '): ' + ($buildOutput -join [Environment]::NewLine))
    }
    $record = Get-NewBuildProfile $previousRuns $mode $inventoryHash $replacementSerial
    Assert-SignedPackage $record $mode $replacementSerial
    $builds[$mode] = $record
    $previousRuns += [string]$record.profile.runId
}

[void][IO.Directory]::CreateDirectory($combinedReportRoot)
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
$manifestPath = Join-Path $combinedReportRoot ('storage-fixture.' + $runId + '.json')
$manifest = [ordered]@{
    schema = 'combined-vm-storage-fixture.v1'
    status = 'SIGNED_FIXTURES_READY'
    recordedUtc = [DateTime]::UtcNow.ToString('o')
    runId = $runId
    guestInventoryPath = $guestInventoryPath
    guestInventorySha256 = $inventoryHash
    authenticatedMetadataPath = $authenticatedMetadataPath
    authenticatedMetadataSha256 = $authenticatedMetadataHash
    authenticatedBaselinePath = $baselinePath
    authenticatedBaselineSha256 = $baselineHash
    replacementMapPath = $mapPath
    replacementMapSha256 = $mapHash
    machine = [ordered]@{
        manufacturer = [string]$guest.machine.manufacturer
        model = [string]$guest.machine.model
        uuid = [string]$guest.machine.uuid
    }
    disk = [ordered]@{
        number = 0
        devicePath = [string]$disk.devicePath
        pnpDeviceId = [string]$disk.pnpDeviceId
        originalSerial = [string]$disk.serial
        wmiOriginalSerial = [string]$disk.wmiSerial
        replacementSerial = $replacementSerial
        windowsBuild = $guestWindowsBuild
        windowsUbr = $guestWindowsUbr
    }
    signerThumbprint = $pinnedThumbprint
    packages = [ordered]@{}
    driverInstalledOrLoaded = $false
    vmStartedOrChanged = $false
    hostTrustStoresChanged = $false
    bootConfigurationChanged = $false
}
foreach ($mode in @('transparent', 'enabled')) {
    $record = $builds[$mode]
    $profile = $record.profile
    $packageFiles = @($profile.packageFiles | ForEach-Object {
        [pscustomobject]@{ name = [string]$_.name; path = Join-Path ([string]$profile.package) ([string]$_.name); sha256 = [string]$_.sha256 }
    })
    $manifest.packages[$mode] = [ordered]@{
        buildProfilePath = [IO.Path]::GetFullPath($record.path)
        buildProfileSha256 = (Get-FileHash -LiteralPath $record.path -Algorithm SHA256).Hash.ToUpperInvariant()
        artifactDirectory = [string]$profile.artifactDirectory
        packageDirectory = [string]$profile.package
        mode = $mode
        startType = [int]$profile.startType
        originalSerial = [string]$profile.originalSerial
        replacementSerial = [string]$profile.replacementSerial
        signerThumbprint = [string]$profile.signingCertificateThumbprint
        files = $packageFiles
        embeddedSignatureValid = [bool]$profile.embeddedDriverSignature.pkcs7SignatureValid
        embeddedPinnedSignerValid = [bool]$profile.embeddedDriverSignature.pinnedCertificateSignatureValid
        catalogSignatureValid = [bool]$profile.catalogSignature.pkcs7SignatureValid
        catalogPinnedSignerValid = [bool]$profile.catalogSignature.pinnedCertificateSignatureValid
        kernelAdmissionProven = [bool]$profile.embeddedDriverSignature.windowsKernelAdmissionProven
        driverInstalledOrLoaded = [bool]$profile.driverInstalledOrLoaded
        bootConfigurationChanged = [bool]$profile.bootConfigurationChanged
    }
}
$json = $manifest | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText($manifestPath, $json, [Text.UTF8Encoding]::new($false))
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
Write-Output ('STORAGE FIXTURES READY: ' + $manifestPath)
Write-Output ('MANIFEST SHA256: ' + $manifestHash)
Write-Output ('GUEST ORIGINAL -> LOCKED REPLACEMENT: ' + [string]$disk.serial + ' -> ' + $replacementSerial)
Write-Output ('TRANSPARENT PACKAGE: ' + [string]$builds['transparent'].profile.package)
Write-Output ('REPLACEMENT PACKAGE: ' + [string]$builds['enabled'].profile.package)
