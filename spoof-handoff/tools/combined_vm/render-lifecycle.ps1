[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'CaptureOriginal', 'EnableTestMode',
        'InstallTransparent', 'VerifyTransparent', 'RemoveTransparent', 'VerifyTransparentRemoved',
        'InstallReplacement', 'VerifyReplacement', 'RemoveReplacement', 'VerifyReplacementRemoved',
        'StageSmbiosInventory', 'ArmSmbiosInventory', 'CleanupSmbiosInventory', 'RestorePolicy', 'FinalVerify'
    )]
    [string]$Action,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$GuestInventoryPath,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$StorageFixtureManifestPath,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$SmbiosFixtureManifestPath,

    [Parameter(Mandatory = $true)]
    [string]$SharedAssetsDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')]
    [string]$LifecycleId,

    [string]$ReferenceRepo = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Assert-PathUnder([string]$Root, [string]$Path, [string]$Label) {
    $rootPath = (Get-FullPath $Root) + '\'
    $fullPath = Get-FullPath $Path
    if (-not $fullPath.StartsWith($rootPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must remain under $Root"
    }
    return $fullPath
}

function Get-RequiredHash($Object, [string]$Property, [string]$Label) {
    $value = [string]$Object.$Property
    if ($value -cnotmatch '^[A-Fa-f0-9]{64}$') { throw "$Label does not contain a valid SHA-256 value." }
    return $value.ToUpperInvariant()
}

function Add-PinnedAsset([Collections.Generic.List[object]]$Assets, [string]$Name, [string]$SourcePath) {
    if ($Name -cnotmatch '\A[A-Za-z0-9_.-]+\z') { throw "Invalid VM asset name: $Name" }
    $path = [IO.Path]::GetFullPath($SourcePath)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required VM asset is missing: $path" }
    $item = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
    $assetDirectory = Join-Path $script:SharedAssetsFullPath $hash.ToLowerInvariant()
    $sharedPath = Join-Path $assetDirectory $Name
    if (-not (Test-Path -LiteralPath $sharedPath -PathType Leaf)) {
        throw "Hash-pinned read-only VM asset is not staged: $sharedPath"
    }
    $sharedItem = Get-Item -LiteralPath $sharedPath
    if ($sharedItem.Length -ne $item.Length -or
        (Get-FileHash -LiteralPath $sharedPath -Algorithm SHA256).Hash.ToUpperInvariant() -cne $hash) {
        throw "Shared VM asset differs from the source file: $Name"
    }
    $Assets.Add([ordered]@{
        name = $Name
        size = [long]$item.Length
        sha256 = $hash
        sourcePath = $path
        sharePath = $sharedPath
        uncPath = ('\\vmware-host\Shared Folders\CombinedFixture\' + $hash.ToLowerInvariant() + '\' + $Name)
    })
    return $hash
}

function Get-MapRows([string]$Path) {
    $rows = @(Import-Csv -LiteralPath $Path -Delimiter "`t")
    if ($rows.Count -lt 9) { throw 'The authoritative identifier map is incomplete.' }
    return ,$rows
}

function Get-UniqueReplacement($Rows, [string]$Member) {
    $matches = @($Rows | Where-Object { [string]$_.member -ceq $Member })
    if ($matches.Count -ne 1) { throw "Expected exactly one replacement-map row for $Member." }
    return [string]$matches[0].replacement
}

$repo = Get-FullPath $ReferenceRepo
$reportsRoot = Join-Path $repo 'spoof-handoff\reports'
$artifactsRoot = Join-Path $repo 'spoof-handoff\artifacts'
$guestInventoryPathFull = Assert-PathUnder $reportsRoot $GuestInventoryPath 'GuestInventoryPath'
$storageManifestPathFull = Assert-PathUnder $reportsRoot $StorageFixtureManifestPath 'StorageFixtureManifestPath'
$smbiosManifestPathFull = Assert-PathUnder $artifactsRoot $SmbiosFixtureManifestPath 'SmbiosFixtureManifestPath'
$script:SharedAssetsFullPath = Assert-PathUnder $artifactsRoot $SharedAssetsDirectory 'SharedAssetsDirectory'
$expectedOutput = Join-Path $reportsRoot 'combined-9field-vm-20261018\lifecycle'
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $output.Equals($expectedOutput, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputDirectory must be the combined VM lifecycle evidence directory: $expectedOutput"
}
if (-not (Test-Path -LiteralPath $script:SharedAssetsFullPath -PathType Container)) {
    throw "SharedAssetsDirectory does not exist: $script:SharedAssetsFullPath"
}

$inventoryText = Get-Content -LiteralPath $guestInventoryPathFull -Raw
$guestInventory = $inventoryText | ConvertFrom-Json
if ($guestInventory.schema -cne 'vm-device-inventory.v1' -or
    [string]$guestInventory.machine.manufacturer -cne 'VMware, Inc.' -or
    [string]$guestInventory.machine.model -cne 'VMware20,1' -or
    [string]$guestInventory.disk.pnpDeviceId -cnotmatch '^SCSI\\DISK&VEN_NVME&PROD_VMWARE_VIRTUAL_N\\' -or
    [string]$guestInventory.disk.serial -cne [string]$guestInventory.disk.wmiSerial -or
    [bool]$guestInventory.disk.isBoot -ne $true -or [bool]$guestInventory.disk.isSystem -ne $true) {
    throw 'Fresh VMware20,1 guest inventory with matching boot/system disk native and WMI values is required.'
}
$guestUuid = [Guid]::Empty
if (-not [Guid]::TryParse([string]$guestInventory.machine.uuid, [ref]$guestUuid)) {
    throw 'Fresh VM inventory UUID is invalid.'
}
$guestUuid = $guestUuid.ToString('D').ToUpperInvariant()

$storageManifestText = Get-Content -LiteralPath $storageManifestPathFull -Raw
$storageManifest = $storageManifestText | ConvertFrom-Json
if ($storageManifest.schema -cne 'combined-vm-storage-fixture.v1' -or
    $storageManifest.status -cne 'SIGNED_FIXTURES_READY' -or
    [string]$storageManifest.guestInventorySha256 -cne (Get-FileHash -LiteralPath $guestInventoryPathFull -Algorithm SHA256).Hash.ToUpperInvariant() -or
    [string]$storageManifest.machine.uuid -cne $guestUuid -or
    [string]$storageManifest.disk.pnpDeviceId -cne [string]$guestInventory.disk.pnpDeviceId -or
    [string]$storageManifest.disk.originalSerial -cne [string]$guestInventory.disk.serial -or
    [string]$storageManifest.disk.wmiOriginalSerial -cne [string]$guestInventory.disk.wmiSerial) {
    throw 'Storage fixture manifest does not match the fresh VMware guest inventory.'
}
foreach ($mode in @('transparent', 'enabled')) {
    $profile = $storageManifest.packages.$mode
    if ($null -eq $profile -or [int]$profile.startType -ne 0 -or
        [string]$profile.mode -cne $mode -or
        $profile.embeddedSignatureValid -ne $true -or $profile.embeddedPinnedSignerValid -ne $true -or
        $profile.catalogSignatureValid -ne $true -or $profile.catalogPinnedSignerValid -ne $true -or
        $profile.driverInstalledOrLoaded -ne $false -or $profile.bootConfigurationChanged -ne $false) {
        throw "Storage fixture $mode profile does not satisfy the boot-start signed package contract."
    }
}
$smbiosManifestText = Get-Content -LiteralPath $smbiosManifestPathFull -Raw
$smbiosManifest = $smbiosManifestText | ConvertFrom-Json
if ($smbiosManifest.schema -cne 'combined-vm-smbios-fixture.v1' -or
    $smbiosManifest.result -cne 'PASS' -or
    $smbiosManifest.mode -cne 'vm_only_smbios_fixture' -or
    [string]$smbiosManifest.guestCapture.sourceMetadata.sha256 -notmatch '^[A-Fa-f0-9]{64}$') {
    throw 'The VM-only SMBIOS fixture manifest is invalid or lacks source provenance.'
}
$capturePath = [IO.Path]::GetFullPath([string]$smbiosManifest.guestCapture.path)
if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf) -or
    (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash.ToUpperInvariant() -cne
        ([string]$smbiosManifest.guestCapture.sha256).ToUpperInvariant()) {
    throw 'The hash-pinned fresh VM SMBIOS capture is missing or changed.'
}
$capture = Get-Content -LiteralPath $capturePath -Raw | ConvertFrom-Json
if ([string]$capture.fields.system_uuid.observedNative -cne $guestUuid -or
    [string]$capture.fields.system_uuid.observedWmi -cne $guestUuid -or
    [string]$capture.fields.system_vendor.observedNative -cne 'VMware, Inc.' -or
    [string]$capture.fields.system_product.observedNative -cne 'VMware20,1') {
    throw 'Fresh SMBIOS fixture capture does not match the same VMware guest UUID and model.'
}

$mapPath = Join-Path $repo 'spoof-handoff\reports\client-originals.tsv'
$mapRows = Get-MapRows $mapPath
$smbiosMap = [ordered]@{
    system_serial = 'FakeSMBIOSSerial'
    system_vendor = 'FakeSMBIOSManufacturer'
    system_product = 'FakeSMBIOSProduct'
    board_serial = 'FakeMotherboardSerial'
    board_vendor = 'FakeMotherboardManufacturer'
    board_product = 'FakeMotherboardProduct'
    system_uuid = 'FakeSMBIOSUUID'
}
$smbiosOriginals = [ordered]@{}
$smbiosReplacements = [ordered]@{}
foreach ($field in $smbiosMap.Keys) {
    $entry = $capture.fields.PSObject.Properties[$field]
    if ($null -eq $entry -or $entry.Value.nativeMatchesWmi -ne $true -or
        [string]$entry.Value.observedNative -cne [string]$entry.Value.observedWmi) {
        throw "Fresh SMBIOS fixture has mismatched native/WMI values for $field."
    }
    $smbiosOriginals[$field] = [string]$entry.Value.observedNative
    $smbiosReplacements[$field] = Get-UniqueReplacement $mapRows $smbiosMap[$field]
}
if ($smbiosReplacements.system_vendor -cne 'NORTHLAB' -or
    $smbiosReplacements.system_product -cne 'NODE' -or
    $smbiosReplacements.system_uuid -cne 'A17F24D0-68E2-4A09-9C31-2B7D8450E6F9' -or
    [string]$storageManifest.disk.replacementSerial -cne (Get-UniqueReplacement $mapRows 'FakeDiskSerial')) {
    throw 'Fixture replacements drifted from the locked client-originals.tsv values.'
}

$sourcePaths = [ordered]@{}
foreach ($mode in @('transparent', 'enabled')) {
    foreach ($file in @($storageManifest.packages.$mode.files)) {
        if ([string]$file.name -notin @('client_storage_filter.inf', 'client_storage_filter.sys', 'client_storage_filter.cat')) {
            throw "Unexpected storage package file: $($file.name)"
        }
        if ([string]$file.path -notmatch '\\package\\[^\\]+$') { throw 'Storage package file path is outside its generated package directory.' }
        $sourcePaths[$mode + '-' + [string]$file.name] = [string]$file.path
    }
}
$sourcePaths['smbios_windows_readback.exe'] = [string]$smbiosManifest.windowsReadback
$sourcePaths['smbios-inventory.efi'] = [string]$smbiosManifest.collector
$loaderBuild = Join-Path $repo '.codex-build\efi_inventory_loader'
$inventoryBuild = Join-Path $repo '.codex-build\efi_inventory'
$sourcePaths['inventory_loader.efi'] = Join-Path $loaderBuild 'inventory_loader.efi'
$sourcePaths['ib-install.exe'] = Join-Path $loaderBuild 'ib-install.exe'
$sourcePaths['efi_inventory.exe'] = Join-Path $inventoryBuild 'efi_inventory.exe'
$sourcePaths['path-b-collector.ps1'] = Join-Path $repo 'tools\efi_inventory\path-b-collector.ps1'
$sourcePaths['storage-read.ps1'] = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\storage_host\read.ps1'))
$sourcePaths['storage-model.psm1'] = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\storage_host\model.psm1'))
$sourcePaths['hypersinv.cer'] = Join-Path $repo 'components\hypervisor\hypersinv.cer'
if ((Get-FileHash -LiteralPath $sourcePaths['hypersinv.cer'] -Algorithm SHA256).Hash.ToUpperInvariant() -cne
    'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE') {
    throw 'The public project certificate differs from its pinned SHA-256.'
}

$assets = [Collections.Generic.List[object]]::new()
foreach ($name in $sourcePaths.Keys) {
    [void](Add-PinnedAsset $assets $name ([string]$sourcePaths[$name]))
}
$assetLookup = [ordered]@{}
foreach ($asset in $assets) { $assetLookup[[string]$asset.name] = $asset }
$config = [ordered]@{
    schema = 'combined-vm-lifecycle-config.v1'
    action = $Action
    vmIdentity = [ordered]@{
        original = [ordered]@{ manufacturer = 'VMware, Inc.'; model = 'VMware20,1'; uuid = $guestUuid }
        patched = [ordered]@{ manufacturer = $smbiosReplacements.system_vendor; model = $smbiosReplacements.system_product; uuid = $smbiosReplacements.system_uuid }
    }
    disk = [ordered]@{
        pnpDeviceId = [string]$storageManifest.disk.pnpDeviceId
        hardwareIds = @($guestInventory.disk.hardwareIds | ForEach-Object { [string]$_ })
        number = [int]$storageManifest.disk.number
        originalNative = [string]$storageManifest.disk.originalSerial
        originalWmi = [string]$storageManifest.disk.wmiOriginalSerial
        replacement = [string]$storageManifest.disk.replacementSerial
        windowsBuild = [int]$storageManifest.disk.windowsBuild
        windowsUbr = [int]$storageManifest.disk.windowsUbr
    }
    smbios = [ordered]@{ originals = $smbiosOriginals; replacements = $smbiosReplacements }
    initialPolicy = [ordered]@{ secureBoot = $false; testSigning = 'no' }
    signer = [ordered]@{ thumbprint = [string]$storageManifest.signerThumbprint; certificateSha256 = [string]$assetLookup['hypersinv.cer'].sha256 }
    packages = [ordered]@{}
    assets = @($assets.ToArray())
    lifecycleId = $LifecycleId
    stateRoot = ('C:\ProgramData\CombinedVmLifecycle\' + $guestUuid.Replace('-', '').ToLowerInvariant() + '\' + $LifecycleId)
    pathBMountLetter = 'Z'
    shareRoot = '\\vmware-host\Shared Folders\CombinedFixture'
    expectedSmbiosSourceMetadataSha256 = ([string]$smbiosManifest.guestCapture.sourceMetadata.sha256).ToUpperInvariant()
    replacementMap = [ordered]@{ path = $mapPath; sha256 = (Get-FileHash -LiteralPath $mapPath -Algorithm SHA256).Hash.ToUpperInvariant() }
    fixturePins = [ordered]@{
        guestInventoryPath = $guestInventoryPathFull
        guestInventorySha256 = (Get-FileHash -LiteralPath $guestInventoryPathFull -Algorithm SHA256).Hash.ToUpperInvariant()
        storageFixtureManifestPath = $storageManifestPathFull
        storageFixtureManifestSha256 = (Get-FileHash -LiteralPath $storageManifestPathFull -Algorithm SHA256).Hash.ToUpperInvariant()
        smbiosFixtureManifestPath = $smbiosManifestPathFull
        smbiosFixtureManifestSha256 = (Get-FileHash -LiteralPath $smbiosManifestPathFull -Algorithm SHA256).Hash.ToUpperInvariant()
        smbiosCapturePath = $capturePath
        smbiosCaptureSha256 = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash.ToUpperInvariant()
    }
}
foreach ($mode in @('transparent', 'enabled')) {
    $config.packages[$mode] = [ordered]@{
        mode = [string]$storageManifest.packages.$mode.mode
        startType = [int]$storageManifest.packages.$mode.startType
        signerThumbprint = [string]$storageManifest.packages.$mode.signerThumbprint
        inf = [string]$assetLookup[$mode + '-client_storage_filter.inf'].uncPath
        sys = [string]$assetLookup[$mode + '-client_storage_filter.sys'].uncPath
        cat = [string]$assetLookup[$mode + '-client_storage_filter.cat'].uncPath
        files = @(
            foreach ($suffix in @('inf','sys','cat')) {
                $name = $mode + '-client_storage_filter.' + $suffix
                [ordered]@{ name = 'client_storage_filter.' + $suffix; uncPath = [string]$assetLookup[$name].uncPath; sha256 = [string]$assetLookup[$name].sha256; sourceName = $name }
            }
        )
    }
}
$config.assets = @($assets.ToArray())
$configBytes = [Text.Encoding]::UTF8.GetBytes(($config | ConvertTo-Json -Depth 20 -Compress))
$configBase64 = [Convert]::ToBase64String($configBytes)
$templatePath = Join-Path $PSScriptRoot 'guest-lifecycle.template.ps1'
if (-not (Test-Path -LiteralPath $templatePath -PathType Leaf)) { throw "Guest lifecycle template is missing: $templatePath" }
$template = [IO.File]::ReadAllText($templatePath)
$token = '__LIFECYCLE_CONFIG_BASE64__'
if (([regex]::Matches($template, [regex]::Escape($token))).Count -ne 1) { throw 'Lifecycle config placeholder is missing or duplicated.' }
$scriptText = $template.Replace($token, $configBase64)
if ([regex]::IsMatch($scriptText, '__[A-Z0-9_]+__')) { throw 'Rendered lifecycle script retains an unresolved placeholder.' }
$tokens = $null
$parseErrors = $null
[void][Management.Automation.Language.Parser]::ParseInput($scriptText, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -gt 0) { throw ('Rendered lifecycle script has PowerShell parse errors: ' + (($parseErrors | ForEach-Object { $_.Message }) -join '; ')) }
[void][IO.Directory]::CreateDirectory($output)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$scriptPath = Join-Path $output ('guest-lifecycle.' + $Action + '.' + $stamp + '.' + [Guid]::NewGuid().ToString('N') + '.ps1')
[IO.File]::WriteAllText($scriptPath, $scriptText, [Text.UTF8Encoding]::new($false))
$scriptHash = (Get-FileHash -LiteralPath $scriptPath -Algorithm SHA256).Hash.ToUpperInvariant()
$configPath = [IO.Path]::ChangeExtension($scriptPath, '.config.json')
$receiptPath = [IO.Path]::ChangeExtension($scriptPath, '.render.json')
[IO.File]::WriteAllText($configPath, ($config | ConvertTo-Json -Depth 20) + "`r`n", [Text.UTF8Encoding]::new($false))
$receipt = [ordered]@{
    schema = 'combined-vm-lifecycle-render.v1'
    action = $Action
    lifecycleId = $LifecycleId
    vmUuid = $guestUuid
    outputDirectory = $output
    templatePath = $templatePath
    templateSha256 = (Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash.ToUpperInvariant()
    renderedScriptPath = $scriptPath
    renderedScriptSha256 = $scriptHash
    renderedConfigPath = $configPath
    renderedConfigSha256 = (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToUpperInvariant()
    guestInventoryPath = $guestInventoryPathFull
    guestInventorySha256 = [string]$config.fixturePins.guestInventorySha256
    storageFixtureManifestPath = $storageManifestPathFull
    storageFixtureManifestSha256 = [string]$config.fixturePins.storageFixtureManifestSha256
    smbiosFixtureManifestPath = $smbiosManifestPathFull
    smbiosFixtureManifestSha256 = [string]$config.fixturePins.smbiosFixtureManifestSha256
    sourceMetadataSha256 = $config.expectedSmbiosSourceMetadataSha256
    replacementMapPath = $mapPath
    replacementMapSha256 = [string]$config.replacementMap.sha256
    assets = @($assets.ToArray())
    renderedUtc = [DateTime]::UtcNow.ToString('o')
}
[IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 20) + "`r`n", [Text.UTF8Encoding]::new($false))
Write-Output "RENDERED VM LIFECYCLE ACTION: $Action"
Write-Output "VM UUID: $guestUuid"
Write-Output "GUEST SCRIPT: $scriptPath"
Write-Output "SCRIPT SHA256: $scriptHash"
Write-Output "CONFIG: $configPath"
Write-Output "RENDER RECEIPT: $receiptPath"
