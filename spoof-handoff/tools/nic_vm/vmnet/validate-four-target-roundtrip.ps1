[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MetadataPath,
    [Parameter(Mandatory = $true)][string]$TsvPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:RequiredReaders = @(
    'OID_802_3_CURRENT_ADDRESS',
    'GetAdaptersAddresses',
    'GetAdaptersInfo',
    'GetIfTable2',
    'Win32_NetworkAdapter.MACAddress',
    'MSFT_NetAdapter.NetworkAddresses',
    'Get-NetAdapter.MacAddress'
)

function Get-FunctionText([string]$Source, [string]$Name) {
    $tokens = $null
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseInput($Source, [ref]$tokens, [ref]$parseErrors)
    if (@($parseErrors).Count -gt 0) { throw ('Mapper source parse failed: ' + (($parseErrors | ForEach-Object Message) -join '; ')) }
    $functionAsts = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $Name }, $true))
    if ($functionAsts.Count -ne 1) { throw ('Expected exactly one mapper function ' + $Name + '; actual=' + $functionAsts.Count) }
    return $functionAsts[0].Extent.Text
}

function Convert-PhaseForMapper([object]$Phase) {
    return [ordered]@{
        nativeCapture = $Phase.nativeCapture
        windowsReads = [ordered]@{
            win32NetworkAdapter = [ordered]@{ rows = @($Phase.windowsReads.win32NetworkAdapter); error = $null }
            msftNetAdapter = [ordered]@{ rows = @($Phase.windowsReads.msftNetAdapter); error = $null }
            getNetAdapter = [ordered]@{ rows = @($Phase.windowsReads.getNetAdapter); error = $null }
        }
    }
}

function Copy-JsonObject([object]$Value) {
    return (ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $Value -Depth 50))
}

function Assert-MapperRejects([string]$Name, [object]$Data, [object[]]$Bindings, [string]$ExpectedText) {
    $rejected = $false
    $message = $null
    try {
        [void](ConvertFrom-HostVmRawCaptureData $Data $Name 'Original' $Bindings $script:RequiredReaders)
    }
    catch {
        $rejected = $true
        $message = $_.Exception.Message
    }
    if (-not $rejected) { throw ('Negative case unexpectedly passed: ' + $Name) }
    if ($ExpectedText -and $message -notlike ('*' + $ExpectedText + '*')) { throw ('Negative case failed for an unexpected reason: ' + $Name + '; expected text=' + $ExpectedText + '; actual=' + $message) }
    return [ordered]@{ name = $Name; rejected = $rejected; error = $message }
}

$mapperPath = Join-Path (Get-Location) '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\nic_vm\host-address-roundtrip.ps1'
$mapperSource = [IO.File]::ReadAllText($mapperPath)
foreach ($mapperName in @(
    'Get-HostProperty',
    'ConvertTo-HostMacKey',
    'ConvertTo-HostVmGuidKey',
    'Get-HostVmRows',
    'Get-HostVmUniqueGuidRow',
    'Get-HostVmUniquePnpRow',
    'Get-HostVmRowAddress',
    'ConvertFrom-HostVmRawCaptureData'
)) {
    . ([ScriptBlock]::Create((Get-FunctionText $mapperSource $mapperName)))
}
$mapperHash = (Get-FileHash -LiteralPath $mapperPath -Algorithm SHA256).Hash.ToUpperInvariant()
$tsvHash = (Get-FileHash -LiteralPath $TsvPath -Algorithm SHA256).Hash.ToUpperInvariant()
if ($tsvHash -cne 'F628CB25D18F549A877C1529A82986B524BFD3D8B9801A776405F969FC4F42C3') { throw ('Locked values TSV hash mismatch: ' + $tsvHash) }
$tsvRows = @(Import-Csv -LiteralPath $TsvPath -Delimiter "`t" | Where-Object { $_.member -ceq 'FakeMacAddress' })
if ($tsvRows.Count -ne 4) { throw ('Expected four locked NIC rows; actual=' + $tsvRows.Count) }
$bindings = @($tsvRows | ForEach-Object {
    [ordered]@{
        label = [string]$_.device
        original = [string]$_.original
        replacement = [string]$_.replacement
    }
})

$metadataHash = (Get-FileHash -LiteralPath $MetadataPath -Algorithm SHA256).Hash.ToUpperInvariant()
$metadata = Get-Content -LiteralPath $MetadataPath -Raw | ConvertFrom-Json
if ([string]$metadata.schema -cne 'vmnet-four-target-address-roundtrip.v1') { throw ('Unexpected round-trip schema: ' + [string]$metadata.schema) }
if ([string]$metadata.vmIdentity.uuid -cne 'D7C14D56-24D1-9894-8906-C0CE95A07790') { throw ('Unexpected VM UUID: ' + [string]$metadata.vmIdentity.uuid) }
if ([string]$metadata.nativeExeSha256 -cne 'BF553F39E4E7024C75272C24215D995858627AEDD5E946A4B1289A8033DA1642') { throw 'Metadata native reader SHA is not pinned.' }
if ([string]$metadata.productionHelperBundleSha256 -cne 'E7B1B841372B890C0D1638E6CCB11D41B5F257A64FBF491B3FEF811B29A212D8') { throw 'Metadata production helper bundle SHA is not pinned.' }
if (-not [bool]$metadata.restored -or @($metadata.failures).Count -ne 0 -or @($metadata.mutationRecords).Count -ne 4) { throw 'Round-trip metadata does not prove four mutations, clean restoration, and zero failures.' }
$targets = @($metadata.targets)
if ($targets.Count -ne 4) { throw ('Expected four target bindings in metadata; actual=' + $targets.Count) }
$targetPairs = @($targets | ForEach-Object { ([string]$_.original).ToUpperInvariant() + '|' + ([string]$_.replacement).ToUpperInvariant() } | Sort-Object -Unique)
$tsvPairs = @($bindings | ForEach-Object { ([string]$_.original).ToUpperInvariant() + '|' + ([string]$_.replacement).ToUpperInvariant() } | Sort-Object -Unique)
if (($targetPairs -join "`n") -cne ($tsvPairs -join "`n")) { throw 'Guest target pairs do not exactly match client-originals.tsv.' }

$phaseModes = [ordered]@{ original = 'Original'; filteredActive = 'Replacement'; restored = 'Original' }
$rawByPhase = [ordered]@{}
$phaseProofs = [Collections.Generic.List[object]]::new()
foreach ($phaseName in @('original', 'filteredActive', 'restored')) {
    $phase = $metadata.phases.PSObject.Properties[$phaseName].Value
    if ($null -eq $phase) { throw ('Missing round-trip phase: ' + $phaseName) }
    if (-not [bool]$phase.nativeCapture.data.readOnly -or [bool]$phase.nativeCapture.data.networkConfigurationChanged) { throw ('Native capture does not attest read-only unchanged network configuration for ' + $phaseName) }
    $mapperData = Convert-PhaseForMapper $phase
    $raw = ConvertFrom-HostVmRawCaptureData $mapperData $phaseName $phaseModes[$phaseName] $bindings $script:RequiredReaders
    if ($raw.Count -ne 4) { throw ('Independent raw mapper expected 4 identities for ' + $phaseName + '; actual=' + $raw.Count) }
    $rawByPhase[$phaseName] = $raw
    foreach ($target in $targets) {
        $pnpKey = ([string]$target.pnpDeviceId).ToUpperInvariant()
        if (-not $raw.Contains($pnpKey)) { throw ('Independent raw mapper omitted guest PnP identity: ' + [string]$target.pnpDeviceId + ' phase=' + $phaseName) }
        $actual = $raw[$pnpKey]
        if ((ConvertTo-HostMacKey $actual.original) -cne (ConvertTo-HostMacKey $target.original) -or
            (ConvertTo-HostMacKey $actual.replacement) -cne (ConvertTo-HostMacKey $target.replacement) -or
            $actual.interfaceGuid -cne ([string]$target.interfaceGuid).ToUpperInvariant()) {
            throw ('Independent raw identity/value mapping differs from guest target metadata: phase=' + $phaseName + ' pnp=' + [string]$target.pnpDeviceId)
        }
        foreach ($reader in $script:RequiredReaders) {
            $expected = if ($phaseModes[$phaseName] -ceq 'Original') { [string]$target.original } else { [string]$target.replacement }
            $recorded = @($metadata.checks | Where-Object { $_.phase -ceq $phaseName -and $_.reader -ceq $reader -and [string]::Equals([string]$_.pnpDeviceId, [string]$target.pnpDeviceId, [StringComparison]::OrdinalIgnoreCase) })
            if ($recorded.Count -ne 1) { throw ('Recorded check row must resolve once: phase=' + $phaseName + ' pnp=' + [string]$target.pnpDeviceId + ' reader=' + $reader + ' count=' + $recorded.Count) }
            $actualRaw = $actual.readers[$reader]
            if ((ConvertTo-HostMacKey $actualRaw) -cne (ConvertTo-HostMacKey $expected) -or -not [bool]$recorded[0].pass -or
                (ConvertTo-HostMacKey $recorded[0].expected) -cne (ConvertTo-HostMacKey $expected) -or
                (ConvertTo-HostMacKey $recorded[0].actual) -cne (ConvertTo-HostMacKey $expected)) {
                throw ('Expected=' + $expected + ' actualRaw=' + [string]$actualRaw + ' recorded=' + (ConvertTo-Json -InputObject $recorded[0] -Compress -Depth 6))
            }
            $phaseProofs.Add([ordered]@{ phase = $phaseName; target = [string]$target.label; pnpDeviceId = [string]$target.pnpDeviceId; interfaceGuid = [string]$target.interfaceGuid; reader = $reader; expected = $expected; independentlyDerived = $actualRaw; recorded = $recorded[0].actual; pass = $true })
        }
    }
}
if ($phaseProofs.Count -ne 84) { throw ('Independent raw validation expected exactly 84 checks; actual=' + $phaseProofs.Count) }
if (@($metadata.checks).Count -ne 84 -or @($metadata.checks | Where-Object { -not [bool]$_.pass }).Count -ne 0) { throw 'Recorded guest check matrix is incomplete or contains failed checks.' }

$negativeRows = [Collections.Generic.List[object]]::new()
$originalData = Convert-PhaseForMapper $metadata.phases.original
$missingData = Copy-JsonObject $originalData
$missingGuid = [string]$missingData.nativeCapture.data.setupDiNdisLanClass.interfaces[0].netCfgInstanceId
$missingData.nativeCapture.data.getAdaptersInfo.entries = @($missingData.nativeCapture.data.getAdaptersInfo.entries | Where-Object { (ConvertTo-HostVmGuidKey $_.adapterName 'GetAdaptersInfo adapterName') -cne (ConvertTo-HostVmGuidKey $missingGuid 'SetupDi GUID') })
$negativeRows.Add((Assert-MapperRejects 'missing-GetAdaptersInfo-row' $missingData $bindings 'identity must resolve to exactly one row'))

$duplicateData = Copy-JsonObject $originalData
$duplicateGuid = [string]$duplicateData.nativeCapture.data.setupDiNdisLanClass.interfaces[0].netCfgInstanceId
$duplicateAdapterRow = @($duplicateData.windowsReads.getNetAdapter.rows | Where-Object { (ConvertTo-HostVmGuidKey $_.interfaceGuid 'Get-NetAdapter InterfaceGuid') -ceq (ConvertTo-HostVmGuidKey $duplicateGuid 'SetupDi GUID') })
if ($duplicateAdapterRow.Count -ne 1) { throw ('Cannot construct duplicate Get-NetAdapter negative; source count=' + $duplicateAdapterRow.Count) }
$duplicateData.windowsReads.getNetAdapter.rows = @($duplicateData.windowsReads.getNetAdapter.rows) + @($duplicateAdapterRow[0])
$negativeRows.Add((Assert-MapperRejects 'duplicate-Get-NetAdapter-GUID-row' $duplicateData $bindings 'identity must resolve to exactly one row'))

$tamperData = Copy-JsonObject $originalData
$tamperGuid = [string]$tamperData.nativeCapture.data.setupDiNdisLanClass.interfaces[0].netCfgInstanceId
$tamperRow = @($tamperData.nativeCapture.data.getAdaptersAddresses.entries | Where-Object { (ConvertTo-HostVmGuidKey $_.adapterName 'GetAdaptersAddresses adapterName') -ceq (ConvertTo-HostVmGuidKey $tamperGuid 'SetupDi GUID') })
if ($tamperRow.Count -ne 1) { throw ('Cannot construct GetAdaptersAddresses tamper negative; source count=' + $tamperRow.Count) }
$tamperRow[0].physicalAddress = '02:AA:BB:CC:DD:EE'
$negativeRows.Add((Assert-MapperRejects 'tampered-GetAdaptersAddresses-MAC' $tamperData $bindings 'did not match the locked mode'))

$result = [ordered]@{
    schema = 'nic-vmnet-four-target-raw-validation.v1'
    status = 'PASS'
    recordedUtc = [DateTime]::UtcNow.ToString('o')
    metadataPath = [IO.Path]::GetFullPath($MetadataPath)
    metadataSha256 = $metadataHash
    mapperPath = [IO.Path]::GetFullPath($mapperPath)
    mapperSha256 = $mapperHash
    lockedTsvSha256 = $tsvHash
    nativeExeSha256 = [string]$metadata.nativeExeSha256
    productionHelperBundleSha256 = [string]$metadata.productionHelperBundleSha256
    vmUuid = [string]$metadata.vmIdentity.uuid
    targetCount = 4
    readerCount = 7
    phaseCount = 3
    independentlyRecomputedChecks = $phaseProofs.Count
    recordedChecks = @($metadata.checks).Count
    negativeCases = $negativeRows.ToArray()
    phaseProofs = $phaseProofs.ToArray()
    hostOrGuestSettingsChanged = $false
}
$outputParent = Split-Path -Parent ([IO.Path]::GetFullPath($OutputPath))
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) { [void][IO.Directory]::CreateDirectory($outputParent) }
[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputPath), (ConvertTo-Json -InputObject $result -Depth 30), [Text.UTF8Encoding]::new($false))
$result | ConvertTo-Json -Depth 20
