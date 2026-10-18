[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('StageArmCorrectionOnly','CaptureCorrectionOnly','StageArmActive','CaptureActive','RecoverPending')]
    [string]$Action,

    [string]$OutputDirectory,

    [string]$RepoRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptWorktreeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$worktreeRoot = if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $scriptWorktreeRoot } else { [IO.Path]::GetFullPath($RepoRoot) }
$worktreeRoot = $worktreeRoot.TrimEnd('\')
$mainRoot = [IO.Path]::GetFullPath((Join-Path $worktreeRoot '..\..\..'))
$expectedWorktree = Join-Path $mainRoot '.codex-build\worktrees\smbios-boot-serial-probe'
if (-not $worktreeRoot.Equals($expectedWorktree, [StringComparison]::OrdinalIgnoreCase) -or
    -not $worktreeRoot.Equals($scriptWorktreeRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Fixture correction renderer must run from the approved worktree: $expectedWorktree"
}

$fixtureId = 'smbios2-ep-correction-f12-r2-release-20261018'
$vmUuid = 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1'
$expectedDiskPnp = 'SCSI\DISK&VEN_NVME&PROD_VMWARE_VIRTUAL_N\5&25A13950&0&000000'
$expectedLoaderSha256 = '24B321ECAABB33EC3DAD346F2E8941F0E035D87056712128410F75598FFFFE6A'
$expectedPathBScriptSha256 = 'F8C356E7099F4BBE06FB52F98BC795CA6628E638CB283DDEB2BA3E1E7E48C639'
$expectedF12ManifestSha256 = '947AB2ABFBF6A92C9E20F303F1911FBEFCDD9872CEE9CE483624DE28C3D6EA24'
$expectedF12BuildId = '1C74082DBD2749E99203377719AC0A5E'
$expectedF12SourceSetSha256 = 'F1D159D23C7D37622EA715E5D8367B35A69A8B64F25FA712DBD5F84BF7D62939'
$expectedF12PreludeSha256 = '55436571C6D2CC23E120F0470B26C6611DCE94F88B2D04A8ECA0AEF3BC3E0676'
$expectedFinal2ManifestSha256 = '51D2A19C2A62BCDF2D9D62000789704B6F1190357BEE8E8505C5C271988C79EE'
$expectedStageReceiptSha256 = '435951FE616A645360BD979FF12989B66FBC72FB55ADB6EF454A630A7E2BB263'
$expectedHistoricalStageFixtureManifestSha256 = '6D23EABF4C9711DDF4F8B234292AE06CDE070D2DBC662BE10F7F08FE009F4F09'
$expectedHistoricalStageBuildId = '9FEEC3B5542174E53384F98BA530B369'
$expectedVmMapSha256 = '4BBF070D9E28ECA831F6094CC4BAC4F1947669FA68AE0147E54590A5B90899BC'
$expectedOriginalsSha256 = 'F628CB25D18F549A877C1529A82986B524BFD3D8B9801A776405F969FC4F42C3'
$expectedBootManagerSha256 = '25D8869E3C79083AE62C698A9DB9030748E3267BD53593357447F3F72761D1F2'
$expectedBootOrderHex = '050000000100020003000400'
$expectedHistoricalDiagnosticCollectorSha256 = 'BD86CA73EFACE5B2195DDF0AEEA4BE9BBB2BCD2DF778D77F22DF2D9B82AB60F9'

$templatePath = Join-Path $PSScriptRoot 'guest-fixture-correction.template.ps1'
$firmwareModulePath = Join-Path $PSScriptRoot 'firmware-variable.psm1'
$bootOptionModulePath = Join-Path $PSScriptRoot 'boot-option.psm1'
$fixtureManifestPath = Join-Path $worktreeRoot 'spoof-handoff\reports\f12-release\r2\fixture-build.json'
$pathBScriptPath = Join-Path $mainRoot 'tools\efi_inventory\path-b-collector.ps1'
$pathBStageEvidencePath = Join-Path $mainRoot 'spoof-handoff\reports\combined-9field-vm-20261018\uefi-windows-probe-20261018-041850-9958a7a427\evidence\kernel-metadata.json'
$vmMapPath = Join-Path $mainRoot 'spoof-handoff\reports\combined-9field-vm-20261018\vmnet-baseline\vm-fixture-map-fresh.20261018T024623Z.tsv'
$originalsPath = Join-Path $mainRoot 'spoof-handoff\reports\client-originals.tsv'
$vmxPath = Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018\DeviceFilter-Win11-Combined\DeviceFilter-Win11-Combined.vmx'
$assetRoot = Join-Path $worktreeRoot 'spoof-handoff\artifacts\f6\shared-readonly'
$defaultOutput = Join-Path $worktreeRoot 'spoof-handoff\reports\f12-release\runtime\fixture-correction'
if (-not $OutputDirectory) { $OutputDirectory = $defaultOutput }
$outputFull = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$outputRoot = [IO.Path]::GetFullPath($defaultOutput).TrimEnd('\') + '\'
if (-not $outputFull.Equals($defaultOutput, [StringComparison]::OrdinalIgnoreCase) -and
    -not $outputFull.StartsWith($outputRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputDirectory must remain under the approved worktree runtime root: $defaultOutput"
}

function Get-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Get-HashFromBytes([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}

function Assert-Hash([string]$Path, [string]$Expected, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required pinned input is missing: $Label ($Path)" }
    $actual = Get-Hash $Path
    if ($actual -cne $Expected) { throw "$Label SHA-256 mismatch: expected=$Expected actual=$actual path=$Path" }
    return [ordered]@{ name = $Label; path = [IO.Path]::GetFullPath($Path); bytes = [long](Get-Item -LiteralPath $Path).Length; sha256 = $actual }
}

function Assert-NoReparsePath([string]$Path, [string]$StopAt) {
    $current = [IO.Path]::GetFullPath($Path)
    $stop = [IO.Path]::GetFullPath($StopAt).TrimEnd('\')
    while ($current.Length -ge $stop.Length) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point is not allowed in the fixture asset path: $current" }
        }
        if ($current.Equals($stop, [StringComparison]::OrdinalIgnoreCase)) { return }
        $parent = Split-Path -Parent $current
        if (-not $parent -or $parent -ceq $current) { break }
        $current = $parent
    }
    throw "Path is outside the approved root: $Path"
}

function Resolve-FixtureSource([string]$ManifestPath, $SourceRow) {
    $relative = ([string]$SourceRow.path).Replace('/', '\')
    if ($relative.StartsWith('uefi-support\', [StringComparison]::OrdinalIgnoreCase)) {
        return Join-Path (Join-Path $mainRoot 'components\uefi-loader\src\vendor') ($relative.Substring('uefi-support\'.Length))
    }
    if ($relative.StartsWith('source\', [StringComparison]::OrdinalIgnoreCase)) {
        return Join-Path (Join-Path $PSScriptRoot 'smbios_fixture') ($relative.Substring('source\'.Length))
    }
    if ($relative -ceq 'builder\build-firmware-fixture.ps1') {
        return Join-Path $PSScriptRoot 'build-firmware-fixture.ps1'
    }
    if ($relative -ceq 'production-guard\efi_smbios.c') {
        return Join-Path $worktreeRoot 'spoof-handoff\tools\smbios_boot\efi_smbios.c'
    }
    throw "Unsupported fixture build source path in manifest: $relative"
}

function Import-Map([string]$Path, [string]$ExpectedHash, [string]$Label) {
    [void](Assert-Hash $Path $ExpectedHash $Label)
    $rows = @(Import-Csv -LiteralPath $Path -Delimiter "`t" -ErrorAction Stop)
    if ($rows.Count -lt 7) { throw "$Label does not contain the seven confirmed SMBIOS rows." }
    return $rows
}

function Get-FieldRows($Rows) {
    $names = [ordered]@{
        FakeSMBIOSSerial = 'system_serial'
        FakeSMBIOSManufacturer = 'system_vendor'
        FakeSMBIOSProduct = 'system_product'
        FakeMotherboardSerial = 'board_serial'
        FakeMotherboardManufacturer = 'board_vendor'
        FakeMotherboardProduct = 'board_product'
        FakeSMBIOSUUID = 'system_uuid'
    }
    $result = [ordered]@{}
    foreach ($entry in $names.GetEnumerator()) {
        $matches = @($Rows | Where-Object { [string]$_.member -ceq [string]$entry.Key })
        if ($matches.Count -ne 1) { throw "Expected exactly one map row for $($entry.Key); actual=$($matches.Count)" }
        $row = $matches[0]
        if ([string]::IsNullOrEmpty([string]$row.original) -or [string]::IsNullOrEmpty([string]$row.replacement)) { throw "Map row $($entry.Key) has an empty value." }
        $result[[string]$entry.Value] = [ordered]@{ original = [string]$row.original; replacement = [string]$row.replacement }
    }
    return $result
}

function Write-SharedAssets($Assets) {
    [void][IO.Directory]::CreateDirectory($assetRoot)
    Assert-NoReparsePath $assetRoot (Join-Path $worktreeRoot 'spoof-handoff\artifacts')
    $canonical = @($Assets | Sort-Object -Property localName -CaseSensitive | ForEach-Object { [string]$_.localName + ':' + ([string]$_.sha256).ToUpperInvariant() }) -join "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $setId = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical)))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $setRoot = Join-Path $assetRoot $setId
    $expectedNames = @($Assets | ForEach-Object { [string]$_.localName } | Sort-Object -CaseSensitive)
    if (Test-Path -LiteralPath $setRoot -PathType Container) {
        Assert-NoReparsePath $setRoot $assetRoot
        $existing = @(Get-ChildItem -LiteralPath $setRoot -Force)
        if ($existing.Count -ne $expectedNames.Count -or @($existing | Where-Object { $_.PSIsContainer -or ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $expectedNames -cnotcontains $_.Name }).Count -ne 0) {
            throw "Existing content-addressed fixture asset set is not exact: $setRoot"
        }
    } else {
        $temporary = Join-Path $assetRoot ('.stage-' + [Guid]::NewGuid().ToString('N'))
        [void][IO.Directory]::CreateDirectory($temporary)
        try {
            foreach ($asset in $Assets) {
                Copy-Item -LiteralPath ([string]$asset.sourcePath) -Destination (Join-Path $temporary ([string]$asset.localName)) -ErrorAction Stop
                if ((Get-Hash (Join-Path $temporary ([string]$asset.localName))) -cne [string]$asset.sha256) { throw "Fixture asset copy hash mismatch: $($asset.localName)" }
            }
            Move-Item -LiteralPath $temporary -Destination $setRoot -ErrorAction Stop
        } catch {
            if (Test-Path -LiteralPath $temporary -PathType Container) {
                $temporaryFull = [IO.Path]::GetFullPath($temporary)
                if (-not $temporaryFull.StartsWith(([IO.Path]::GetFullPath($assetRoot).TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase)) { throw 'Temporary asset cleanup path escaped the approved root.' }
                Remove-Item -LiteralPath $temporaryFull -Recurse -Force -ErrorAction SilentlyContinue
            }
            throw
        }
    }
    $assetFiles = [Collections.Generic.List[object]]::new()
    foreach ($asset in $Assets) {
        $path = Join-Path $setRoot ([string]$asset.localName)
        [void](Assert-Hash $path ([string]$asset.sha256) ("shared fixture asset " + [string]$asset.localName))
        $assetFiles.Add([ordered]@{
            localName = [string]$asset.localName
            sha256 = [string]$asset.sha256
            uncPath = '\\vmware-host\Shared Folders\CombinedFixture\' + $setId + '\' + [string]$asset.localName
        })
    }
    return [pscustomobject]@{ setId = $setId; directory = $setRoot; assetFiles = @($assetFiles.ToArray()) }
}

foreach ($path in @($templatePath,$firmwareModulePath,$bootOptionModulePath,$fixtureManifestPath,$pathBScriptPath,$pathBStageEvidencePath,$vmMapPath,$originalsPath,$vmxPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required fixture correction input is missing: $path" }
}

$fixtureManifestPin = Assert-Hash $fixtureManifestPath $expectedF12ManifestSha256 'f12 fixture manifest'
$fixtureManifest = Get-Content -LiteralPath $fixtureManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ([string]$fixtureManifest.schema -cne 'vm-smbios2-ep-prelude-build.v1' -or [string]$fixtureManifest.result -cne 'PASS' -or
    [string]$fixtureManifest.prelude.fixedEspPath -cne 'EFI\HypersinvInventory\fixture-correction\smbios2-ep-prelude.efi' -or
    [string]$fixtureManifest.buildId -cne $expectedF12BuildId -or
    [string]$fixtureManifest.build.sourceSetSha256 -cne $expectedF12SourceSetSha256 -or
    [string]$fixtureManifest.pathB.loader.sha256 -cne $expectedLoaderSha256 -or $fixtureManifest.pathB.payloadsUnmodifiedByPreludeBuild -ne $true) {
    throw 'f12 build manifest does not describe the reviewed prelude and unchanged Path-B loader/payload.'
}
$preludePath = [string]$fixtureManifest.prelude.path
$preludePin = Assert-Hash $preludePath ([string]$fixtureManifest.prelude.sha256) 'f12 SMBIOS2 prelude'
if ([string]$fixtureManifest.prelude.sha256 -cne $expectedF12PreludeSha256) { throw 'f12 prelude hash is not the reviewed artifact.' }
$fixtureSourcePins = [Collections.Generic.List[object]]::new()
$fixtureSourcePins.Add($fixtureManifestPin)
$fixtureSourcePins.Add($preludePin)
foreach ($source in @($fixtureManifest.build.sources)) {
    $sourcePath = Resolve-FixtureSource $fixtureManifestPath $source
    $fixtureSourcePins.Add((Assert-Hash $sourcePath ([string]$source.sha256) ('f12 source ' + [string]$source.path)))
}

$final2ManifestPath = [string]$fixtureManifest.pathB.collectorManifest.path
$final2ManifestPin = Assert-Hash $final2ManifestPath $expectedFinal2ManifestSha256 'final2 collector manifest'
$final2Manifest = Get-Content -LiteralPath $final2ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ([string]$final2Manifest.schema -cne 'combined-vm-smbios-fixture.v1' -or [string]$final2Manifest.result -cne 'PASS' -or
    [string]$final2Manifest.mode -cne 'vm_only_smbios_fixture' -or [IO.Path]::GetFullPath([string]$final2Manifest.collector) -cne [IO.Path]::GetFullPath([string]$fixtureManifest.pathB.collector.path)) {
    throw 'Final2 collector manifest is not the exact successful VM-only SMBIOS collector build.'
}
if ([string]$final2Manifest.replacementMap.sha256 -cne $expectedOriginalsSha256) { throw 'Final2 collector manifest replacement-map pin changed.' }
$collectorPath = [string]$final2Manifest.collector
$collectorPin = Assert-Hash $collectorPath ([string]$fixtureManifest.pathB.collector.sha256) 'final2 Path-B collector EFI'
$smbiosReaderPath = [string]$final2Manifest.windowsReadback
$smbiosReaderPin = Assert-Hash $smbiosReaderPath '1728F100551B52E07A95A5F8459AA221C6544DA58A355F075D016BEEF63D5CDE' 'final2 SMBIOS readback executable'
$loaderPath = [string]$fixtureManifest.pathB.loader.path
$loaderPin = Assert-Hash $loaderPath $expectedLoaderSha256 'dedicated Path-B inventory_loader.efi'
$installerPath = Join-Path $mainRoot '.codex-build\efi_inventory_loader\ib-install.exe'
$installerPin = Assert-Hash $installerPath 'DC52141BEB419FB8754A03640D2D3507A3D7521290728EAAF84A3DCE2F671949' 'Path-B ib-install.exe'
$scannerPath = Join-Path $mainRoot '.codex-build\efi_inventory\efi_inventory.exe'
$scannerPin = Assert-Hash $scannerPath '618EC349BD69F6C8E558BB3977B1F512520176217A9B193F46ACB8258A118BBF' 'Path-B efi_inventory.exe'
$pathBScriptPin = Assert-Hash $pathBScriptPath $expectedPathBScriptSha256 'Path-B source script'
$fixtureSourcePins.Add($final2ManifestPin)
$fixtureSourcePins.Add($collectorPin)
$fixtureSourcePins.Add($smbiosReaderPin)
$fixtureSourcePins.Add($loaderPin)
$fixtureSourcePins.Add($installerPin)
$fixtureSourcePins.Add($scannerPin)
$fixtureSourcePins.Add($pathBScriptPin)

$stageReceiptPin = Assert-Hash $pathBStageEvidencePath $expectedStageReceiptSha256 'authenticated Path-B stage receipt'
$stageReceipt = Get-Content -LiteralPath $pathBStageEvidencePath -Raw | ConvertFrom-Json -ErrorAction Stop
if ([string]$stageReceipt.schema -cne 'combined-vm-pathb-diagnostic-stage-arm.v1' -or [string]$stageReceipt.status -cne 'PASS' -or
    [string]$stageReceipt.fixture.manifestSha256 -cne $expectedHistoricalStageFixtureManifestSha256 -or
    [string]$stageReceipt.fixture.buildId -cne $expectedHistoricalStageBuildId -or
    [string]$stageReceipt.vm.observedUuid -cne $vmUuid -or [string]$stageReceipt.vm.diskPnp -cne $expectedDiskPnp -or
    [string]$stageReceipt.firmware.bootOrderBefore.bytesHex -cne $expectedBootOrderHex -or
    [string]$stageReceipt.firmware.bootOrderAfterArm.bytesHex -cne $expectedBootOrderHex -or
    [string]$stageReceipt.esp.before.bootManagerSha256 -cne $expectedBootManagerSha256 -or
    [string]$stageReceipt.esp.afterArm.bootManagerSha256 -cne $expectedBootManagerSha256 -or
    [string]$stageReceipt.pathB.receipt.loaderSha256 -cne $expectedLoaderSha256 -or
    [string]$stageReceipt.pathB.receipt.inventorySha256 -cne $expectedHistoricalDiagnosticCollectorSha256 -or
    [string]$stageReceipt.fixture.collectorSha256 -cne $expectedHistoricalDiagnosticCollectorSha256) {
    throw 'The historical Path-B stage receipt does not match the pinned VM, BootOrder, Boot Manager, dedicated loader, and r5 diagnostic collector.'
}
$fixtureSourcePins.Add($stageReceiptPin)

$vmMapRows = Import-Map $vmMapPath $expectedVmMapSha256 'VM fixture map'
$hostMapRows = Import-Map $originalsPath $expectedOriginalsSha256 'locked client originals map'
$fixtureSourcePins.Add((Assert-Hash $vmMapPath $expectedVmMapSha256 'VM fixture map'))
$fixtureSourcePins.Add((Assert-Hash $originalsPath $expectedOriginalsSha256 'locked client originals map'))
$templatePin = Assert-Hash $templatePath (Get-Hash $templatePath) 'guest fixture template'
$rendererPin = Assert-Hash $PSCommandPath (Get-Hash $PSCommandPath) 'fixture correction renderer'
$firmwareModulePin = Assert-Hash $firmwareModulePath (Get-Hash $firmwareModulePath) 'firmware variable module'
$bootOptionModulePin = Assert-Hash $bootOptionModulePath (Get-Hash $bootOptionModulePath) 'EFI boot option module'
$fixtureSourcePins.Add($templatePin)
$fixtureSourcePins.Add($rendererPin)
$fixtureSourcePins.Add($firmwareModulePin)
$fixtureSourcePins.Add($bootOptionModulePin)
$vmSmbios = Get-FieldRows $vmMapRows
$hostSmbios = Get-FieldRows $hostMapRows
foreach ($field in @($vmSmbios.Keys)) {
    if ([string]$vmSmbios[$field].replacement -cne [string]$hostSmbios[$field].replacement) { throw "VM fixture replacement differs from the locked client map: field=$field" }
}
if ([string]$vmSmbios.system_uuid.original -cne $vmUuid) { throw 'VM fixture map UUID does not match the authorized clone UUID.' }
$diskRow = @($vmMapRows | Where-Object { [string]$_.member -ceq 'FakeDiskSerial' })
if ($diskRow.Count -ne 1 -or [string]$diskRow[0].device -cne 'disk:\\.\PHYSICALDRIVE0' -or
    [string]$diskRow[0].original -cne [string]$stageReceipt.vm.diskSerial) { throw 'VM fixture map Disk0 value does not match the authenticated guest stage baseline.' }

$vmxText = [IO.File]::ReadAllText($vmxPath)
$vmxUuidMatch = [regex]::Match($vmxText, '(?im)^uuid\.bios\s*=\s*"([^"]+)"\s*$')
if (-not $vmxUuidMatch.Success) { throw 'Authorized VMX does not declare uuid.bios.' }
$vmxUuidHex = ($vmxUuidMatch.Groups[1].Value -replace '[^0-9A-Fa-f]', '').ToUpperInvariant()
$expectedVmUuidBytes = ([BitConverter]::ToString(([Guid]$vmUuid).ToByteArray())).Replace('-', '')
if ($vmxUuidHex -cne $expectedVmUuidBytes) { throw "Authorized VMX uuid.bios byte identity mismatch: expected=$expectedVmUuidBytes actual=$vmxUuidHex" }
$vmxPin = [ordered]@{ name = 'authorized disposable VMX'; path = [IO.Path]::GetFullPath($vmxPath); bytes = [long](Get-Item -LiteralPath $vmxPath).Length; sha256 = Get-Hash $vmxPath }

$assets = @(
    [pscustomobject]@{ localName = 'inventory_loader.efi'; sourcePath = $loaderPath; sha256 = $loaderPin.sha256 },
    [pscustomobject]@{ localName = 'ib-install.exe'; sourcePath = $installerPath; sha256 = $installerPin.sha256 },
    [pscustomobject]@{ localName = 'inventory.efi'; sourcePath = $collectorPath; sha256 = $collectorPin.sha256 },
    [pscustomobject]@{ localName = 'efi_inventory.exe'; sourcePath = $scannerPath; sha256 = $scannerPin.sha256 },
    [pscustomobject]@{ localName = 'smbios_windows_readback.exe'; sourcePath = $smbiosReaderPath; sha256 = $smbiosReaderPin.sha256 }
)
$shared = Write-SharedAssets $assets

$internalMode = switch ($Action) {
    'StageArmCorrectionOnly' { 'correction-only' }
    'CaptureCorrectionOnly' { 'correction-only' }
    'StageArmActive' { 'corrected-collector' }
    'CaptureActive' { 'corrected-collector' }
    'RecoverPending' { 'recovery' }
}
$moduleBytes = [IO.File]::ReadAllBytes($firmwareModulePath)
try { $firmwareModuleBase64 = [Convert]::ToBase64String($moduleBytes) } finally { [Array]::Clear($moduleBytes,0,$moduleBytes.Length) }
$bootBytes = [IO.File]::ReadAllBytes($bootOptionModulePath)
try { $bootOptionModuleBase64 = [Convert]::ToBase64String($bootBytes) } finally { [Array]::Clear($bootBytes,0,$bootBytes.Length) }
$pathBBytes = [IO.File]::ReadAllBytes($pathBScriptPath)
try { $pathBScriptBase64 = [Convert]::ToBase64String($pathBBytes) } finally { [Array]::Clear($pathBBytes,0,$pathBBytes.Length) }
$preludeBytes = [IO.File]::ReadAllBytes($preludePath)
try { $preludeBase64 = [Convert]::ToBase64String($preludeBytes) } finally { [Array]::Clear($preludeBytes,0,$preludeBytes.Length) }

$config = [ordered]@{
    schema = 'combined-vm-fixture-correction-config.v1'
    lifecycleId = $fixtureId
    internalAction = $Action
    mode = $internalMode
    stateRoot = ('C:\ProgramData\CombinedVmFixture\' + $vmUuid.Replace('-','').ToLowerInvariant() + '\' + $fixtureId)
    vmUuid = $vmUuid
    diskPnp = $expectedDiskPnp
    expectedBootManagerSha256 = $expectedBootManagerSha256
    expectedBootOrderHex = $expectedBootOrderHex
    assets = [ordered]@{
        inventoryLoaderSha256 = $loaderPin.sha256
        collectorSha256 = $collectorPin.sha256
        ibInstallSha256 = $installerPin.sha256
        efiInventorySha256 = $scannerPin.sha256
        smbiosReaderSha256 = $smbiosReaderPin.sha256
    }
    assetFiles = @($shared.assetFiles)
    preludeSha256 = [string]$fixtureManifest.prelude.sha256
    preludeBase64 = $preludeBase64
    pathBScriptSha256 = $pathBScriptPin.sha256
    pathBScriptBase64 = $pathBScriptBase64
    firmwareModuleBase64 = $firmwareModuleBase64
    bootOptionModuleBase64 = $bootOptionModuleBase64
    collectorBuildId = [string]$final2Manifest.buildIdentity.value
    collectorManifestPath = [IO.Path]::GetFullPath($final2ManifestPath)
    collectorManifestSha256 = $final2ManifestPin.sha256
    smbios = $vmSmbios
    stageBaselineScope = 'historical-loader-and-VM-baseline-only; the captured r5 diagnostic collector is not the final2 collector staged by this action'
    stageBaselineCollectorSha256 = $expectedHistoricalDiagnosticCollectorSha256
    sourcePins = @($fixtureSourcePins.ToArray())
}
$configJson = $config | ConvertTo-Json -Depth 20 -Compress
$configBytes = [Text.Encoding]::UTF8.GetBytes($configJson)
$configBase64 = [Convert]::ToBase64String($configBytes)
$templateText = [IO.File]::ReadAllText($templatePath)
if ([regex]::Matches($templateText,[regex]::Escape('__CONFIG_BASE64__')).Count -ne 1) { throw 'Guest fixture template must contain exactly one config placeholder.' }
$rendered = $templateText.Replace('__CONFIG_BASE64__',$configBase64)
if ($rendered.Contains('__CONFIG_BASE64__')) { throw 'Guest fixture template retained an unresolved placeholder.' }
[void][IO.Directory]::CreateDirectory($outputFull)
Assert-NoReparsePath $outputFull (Join-Path $worktreeRoot 'spoof-handoff\reports')
$parseTokens = $null
$parseErrors = $null
[void][Management.Automation.Language.Parser]::ParseInput($rendered, [ref]$parseTokens, [ref]$parseErrors)
if (@($parseErrors).Count -ne 0) {
    $messages = @($parseErrors | ForEach-Object { $_.Message + ' at ' + $_.Extent.StartLineNumber + ':' + $_.Extent.StartColumnNumber }) -join '; '
    throw "Rendered guest script has PowerShell parse errors: $messages"
}
if ($rendered -match '__[A-Z0-9_]+__') { throw 'Rendered guest script contains an unresolved template token.' }

$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$nonce = [Guid]::NewGuid().ToString('N')
$scriptPath = Join-Path $outputFull ('guest-fixture-correction.' + $Action + '.' + $stamp + '.' + $nonce + '.ps1')
$receiptPath = $scriptPath + '.render.json'
$temporaryScript = $scriptPath + '.tmp'
if ((Test-Path -LiteralPath $scriptPath) -or (Test-Path -LiteralPath $receiptPath) -or (Test-Path -LiteralPath $temporaryScript)) { throw 'Unique render output path unexpectedly already exists.' }
$encoding = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($temporaryScript, $rendered, $encoding)
try {
    $temporaryPin = Assert-Hash $temporaryScript (Get-Hash $temporaryScript) 'temporary rendered guest script'
    Move-Item -LiteralPath $temporaryScript -Destination $scriptPath -ErrorAction Stop
} catch {
    if (Test-Path -LiteralPath $temporaryScript -PathType Leaf) { Remove-Item -LiteralPath $temporaryScript -Force -ErrorAction SilentlyContinue }
    throw
}
$renderedPin = Assert-Hash $scriptPath $temporaryPin.sha256 'rendered guest script'
$configHash = Get-HashFromBytes $configBytes
$receipt = [ordered]@{
    schema = 'combined-vm-fixture-correction-render.v1'
    status = 'PASS'
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    action = $Action
    mode = $internalMode
    lifecycleId = $fixtureId
    script = $renderedPin
    template = $templatePin
    renderer = $rendererPin
    config = [ordered]@{ bytes = $configBytes.Length; sha256 = $configHash }
    fixtureBuild = [ordered]@{
        path = [IO.Path]::GetFullPath($fixtureManifestPath)
        sha256 = $fixtureManifestPin.sha256
        buildId = [string]$fixtureManifest.buildId
        sourceSetSha256 = [string]$fixtureManifest.build.sourceSetSha256
        preludeSha256 = [string]$fixtureManifest.prelude.sha256
    }
    collectorBuild = [ordered]@{ manifestPath = [IO.Path]::GetFullPath($final2ManifestPath); manifestSha256 = $final2ManifestPin.sha256; buildId = [string]$final2Manifest.buildIdentity.value; collectorPath = [IO.Path]::GetFullPath($collectorPath); collectorSha256 = $collectorPin.sha256 }
    historicalStageBaseline = [ordered]@{ path = [IO.Path]::GetFullPath($pathBStageEvidencePath); sha256 = $stageReceiptPin.sha256; scope = 'historical VM/BootOrder/Boot Manager/dedicated-loader baseline only'; capturedCollectorSha256 = $expectedHistoricalDiagnosticCollectorSha256 }
    vmFixtureMap = [ordered]@{ path = [IO.Path]::GetFullPath($vmMapPath); sha256 = $expectedVmMapSha256 }
    lockedClientMap = [ordered]@{ path = [IO.Path]::GetFullPath($originalsPath); sha256 = $expectedOriginalsSha256 }
    authorizedVm = [ordered]@{ vmxPath = [string]$vmxPin.path; vmxBytes = $vmxPin.bytes; vmxSha256 = $vmxPin.sha256; uuid = $vmUuid; diskPnp = $expectedDiskPnp }
    sharedAssets = [ordered]@{ enabled = $true; hostRoot = [IO.Path]::GetFullPath($assetRoot); setId = $shared.setId; guestRoot = ('\\vmware-host\Shared Folders\CombinedFixture\' + $shared.setId); requiredShareName = 'CombinedFixture'; guestWriteAccessPermitted = $false; files = @($shared.assetFiles) }
    sourcePins = @($fixtureSourcePins.ToArray()) + @($vmxPin)
}
$receiptJson = $receipt | ConvertTo-Json -Depth 24
$temporaryReceipt = $receiptPath + '.tmp'
[IO.File]::WriteAllText($temporaryReceipt, $receiptJson + [Environment]::NewLine, $encoding)
Move-Item -LiteralPath $temporaryReceipt -Destination $receiptPath -ErrorAction Stop
$receiptPin = Assert-Hash $receiptPath (Get-Hash $receiptPath) 'fixture correction render receipt'
[pscustomobject]@{
    status = 'PASS'
    action = $Action
    mode = $internalMode
    scriptPath = [string]$renderedPin.path
    scriptSha256 = [string]$renderedPin.sha256
    receiptPath = [string]$receiptPin.path
    receiptSha256 = [string]$receiptPin.sha256
    sharedAssetSetId = [string]$shared.setId
    sharedAssetRoot = [string]$shared.directory
} | ConvertTo-Json -Depth 6

