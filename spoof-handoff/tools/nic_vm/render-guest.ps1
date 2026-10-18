[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('Inventory', 'PreparePolicy', 'Install', 'Start', 'AddressSnapshot', 'AddressApply', 'AddressRestore', 'RemoveForAddressReadback', 'Readback', 'Remove', 'RestorePolicy', 'Reboot', 'Shutdown')][string]$Phase,
    [Parameter(Mandatory = $true)][string]$ProfileJson,
    [Parameter(Mandatory = $true)][string]$NativeExePath,
    [string]$ExpectedVmUuid,
    [ValidateSet('Original', 'Replacement')][string]$ExpectedMode = 'Original',
    [ValidateSet('Any', 'Present', 'Absent', 'AbsentConfigured')][string]$FilterState = 'Any',
    [string]$PackageDirectory,
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$worktreeRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot '..\..\..'))
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot '..\..\..\..\..\..'))
$reportRoot = Join-Path $repositoryRoot 'spoof-handoff\reports\nic-vm-20261017'
$outputRoot = Join-Path $reportRoot 'guest-actions'
$artifactRoot = Join-Path $repositoryRoot 'spoof-handoff\artifacts\nic-vm-20261017'
$lockedMapPath = Join-Path $repositoryRoot 'spoof-handoff\reports\client-originals.tsv'
$guestTemplatePath = Join-Path $scriptRoot 'guest.ps1'
$addressHelperPath = Join-Path $scriptRoot 'adapter-address.ps1'
$certificatePath = Join-Path $worktreeRoot 'components\hypervisor\hypersinv.cer'
$pinnedThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
$pinnedCertificateSha256 = 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'

function Assert-UnderDirectory([string]$Path, [string]$Root, [string]$Description) {
    $full = [IO.Path]::GetFullPath($Path)
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw ($Description + ' is outside its allowed directory: ' + $full) }
    return $full
}

function Get-Sha256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}

function ConvertTo-MacKey([string]$Value) {
    $normalized = $Value -replace '[^0-9A-Fa-f]', ''
    if ($normalized -notmatch '\A[0-9A-Fa-f]{12}\z') { throw ('Invalid six-byte MAC in profile: ' + $Value) }
    return $normalized.ToUpperInvariant()
}

function Get-Required([object]$Object, [string]$Name, [string]$Description) {
    if ($null -eq $Object) { throw ('Profile is missing ' + $Description + '.') }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) { throw ('Profile is missing ' + $Description + '.') }
    return $property.Value
}

foreach ($path in @($ProfileJson, $NativeExePath, $lockedMapPath, $guestTemplatePath, $certificatePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('Required guest package input is missing: ' + $path) }
}
$resolvedProfilePath = Assert-UnderDirectory $ProfileJson $reportRoot 'Profile JSON'
$resolvedNativePath = [IO.Path]::GetFullPath($NativeExePath)
if ([IO.Path]::GetExtension($resolvedNativePath) -cne '.exe') { throw 'Native reader must be a Windows executable.' }

$profileText = [IO.File]::ReadAllText($resolvedProfilePath)
try { $profile = $profileText | ConvertFrom-Json }
catch { throw ('VM profile JSON is invalid: ' + $_.Exception.Message) }
$syntheticProperty = $profile.PSObject.Properties['synthetic']
if ($null -ne $syntheticProperty -and [bool]$syntheticProperty.Value) { throw 'Synthetic profiles cannot be used to render guest actions.' }
$machine = Get-Required $profile 'machine' 'machine identity'
$manufacturer = [string](Get-Required $machine 'manufacturer' 'VMware manufacturer')
$profileUuid = [string](Get-Required $machine 'uuid' 'VM UUID')
$parsedUuid = [Guid]::Empty
if ($manufacturer -notmatch '^(?i:VMware(?:,\s*Inc\.)?)$' -or -not [Guid]::TryParse($profileUuid, [ref]$parsedUuid) -or $parsedUuid -eq [Guid]::Empty) {
    throw 'Profile must contain an exact VMware manufacturer and a nonempty UUID.'
}
if ([string]::IsNullOrWhiteSpace($ExpectedVmUuid)) {
    if ($Phase -ne 'Inventory') { throw 'Every mutation and read-back phase requires the exact expected VM UUID.' }
}
else {
    $expectedParsed = [Guid]::Empty
    if (-not [Guid]::TryParse($ExpectedVmUuid, [ref]$expectedParsed) -or $expectedParsed -eq [Guid]::Empty -or $expectedParsed.ToString('D') -cne $parsedUuid.ToString('D')) {
        throw 'ExpectedVmUuid must exactly match the canonical UUID in the fresh profile.'
    }
}
$bindings = @(Get-Required $profile 'bindings' 'four guest adapter bindings')
if ($bindings.Count -ne 4) { throw ('Expected exactly four VM adapters; found ' + $bindings.Count) }
$lockedRows = @(Import-Csv -LiteralPath $lockedMapPath -Delimiter "`t" | Where-Object { $_.member -ceq 'FakeMacAddress' })
if ($lockedRows.Count -ne 4) { throw 'Locked TSV must contain exactly four MAC records.' }
$lockedByOriginal = @{}
$excludedPnpIds = [Collections.Generic.List[string]]::new()
foreach ($row in $lockedRows) {
    $originalKey = ConvertTo-MacKey ([string]$row.original)
    $replacementKey = ConvertTo-MacKey ([string]$row.replacement)
    if ($lockedByOriginal.ContainsKey($originalKey)) { throw 'Locked TSV contains duplicate original MAC values.' }
    $lockedByOriginal[$originalKey] = $replacementKey
    $parts = ([string]$row.device) -split '[|]', 2
    if ($parts.Count -ne 2 -or [string]::IsNullOrWhiteSpace($parts[1])) { throw 'Locked TSV adapter row is missing its exact production PnP pin.' }
    $excludedPnpIds.Add([string]$parts[1])
}
$seenPnp = @{}
$seenOriginal = @{}
foreach ($binding in $bindings) {
    $pnp = [string](Get-Required $binding 'pnpDeviceId' 'guest PnP device ID')
    if ($pnp.Length -lt 1 -or $pnp.Length -gt 260 -or $pnp -notmatch '^[\x20-\x7e]+$' -or $pnp.Contains('"') -or $pnp.Contains('%')) { throw ('Guest PnP ID is not a bounded safe identity: ' + $pnp) }
    if (@($excludedPnpIds | Where-Object { [string]::Equals($_, $pnp, [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0) { throw ('Guest profile includes a production host PnP pin: ' + $pnp) }
    $pnpKey = $pnp.ToUpperInvariant()
    if ($seenPnp.ContainsKey($pnpKey)) { throw ('Duplicate guest PnP device ID: ' + $pnp) }
    $seenPnp[$pnpKey] = $true
    $interfaceGuid = [string](Get-Required $binding 'interfaceGuid' 'guest adapter interface GUID')
    $parsedInterfaceGuid = [Guid]::Empty
    if (-not [Guid]::TryParse($interfaceGuid, [ref]$parsedInterfaceGuid) -or $parsedInterfaceGuid -eq [Guid]::Empty) { throw ('Invalid guest adapter interface GUID: ' + $interfaceGuid) }
    $originalKey = ConvertTo-MacKey ([string](Get-Required $binding 'original' 'captured original MAC'))
    $replacementKey = ConvertTo-MacKey ([string](Get-Required $binding 'replacement' 'locked replacement MAC'))
    if ($seenOriginal.ContainsKey($originalKey)) { throw 'Profile contains a duplicate original MAC.' }
    $seenOriginal[$originalKey] = $true
    if (-not $lockedByOriginal.ContainsKey($originalKey) -or $lockedByOriginal[$originalKey] -cne $replacementKey) {
        throw ('Guest binding does not match an exact locked original/replacement TSV pair: ' + $pnp)
    }
}
if ($seenOriginal.Count -ne $lockedByOriginal.Count) { throw 'Profile does not include every exact locked MAC pair.' }

$nativeBytes = [IO.File]::ReadAllBytes($resolvedNativePath)
if ($nativeBytes.Length -lt 4096 -or $nativeBytes.Length -gt 32MB) { throw 'Native NIC reader size is outside the package bounds.' }
$nativeImageHash = Get-Sha256 $nativeBytes
$publicCertificateBytes = [IO.File]::ReadAllBytes($certificatePath)
if ((Get-Sha256 $publicCertificateBytes) -cne $pinnedCertificateSha256) { throw 'Checked-in public signing certificate SHA-256 differs from the existing pinned key.' }
$publicCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($publicCertificateBytes)
if ($publicCertificate.Thumbprint -cne $pinnedThumbprint) { throw 'Checked-in public certificate thumbprint differs from the existing pinned key.' }
$config = [ordered]@{
    schema = 'client-nic-vm-guest-config.v1'
    phase = $Phase
    expectedVmUuid = if ([string]::IsNullOrWhiteSpace($ExpectedVmUuid)) { $null } else { $parsedUuid.ToString('D').ToUpperInvariant() }
    expectedMode = $ExpectedMode
    filterState = $FilterState
    profile = $profile
    profileSha256 = Get-Sha256 ([Text.Encoding]::UTF8.GetBytes($profileText))
    excludedPnpIds = @($excludedPnpIds.ToArray())
    nativeExe = [ordered]@{ name = 'nic_native_read.exe'; sha256 = $nativeImageHash; size = $nativeBytes.Length; base64 = [Convert]::ToBase64String($nativeBytes) }
    publicCertificate = [ordered]@{ thumbprint = $pinnedThumbprint; sha256 = $pinnedCertificateSha256; base64 = [Convert]::ToBase64String($publicCertificateBytes) }
    package = $null
}

if ($Phase -eq 'Install') {
    if ([string]::IsNullOrWhiteSpace($PackageDirectory)) { throw 'Install phase requires PackageDirectory.' }
    $packageFullPath = Assert-UnderDirectory $PackageDirectory $artifactRoot 'NIC install package'
    $packageFiles = @(Get-ChildItem -LiteralPath $packageFullPath -File -Force)
    if ($packageFiles.Count -ne 3) { throw 'NIC package directory must contain exactly one SYS, INF, and CAT.' }
    $embeddedFiles = [Collections.Generic.List[object]]::new()
    foreach ($name in @('client_nic_filter.sys', 'client_nic_filter.inf', 'client_nic_filter.cat')) {
        $filePath = Join-Path $packageFullPath $name
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) { throw ('NIC package is missing ' + $name) }
        $bytes = [IO.File]::ReadAllBytes($filePath)
        if ($bytes.Length -gt 2MB) { throw ('NIC package member exceeds 2 MiB: ' + $name) }
        $embeddedFiles.Add([ordered]@{ name = $name; sha256 = Get-Sha256 $bytes; size = $bytes.Length; base64 = [Convert]::ToBase64String($bytes) })
    }
    $config.package = [ordered]@{ files = @($embeddedFiles.ToArray()); directoryName = Split-Path -Leaf $packageFullPath }
}

$configJson = ConvertTo-Json -InputObject $config -Depth 24 -Compress
$configBytes = [Text.Encoding]::UTF8.GetBytes($configJson)
if ($configBytes.Length -gt 3MB) { throw 'Embedded guest request exceeds the mailbox package budget.' }
$configBase64 = [Convert]::ToBase64String($configBytes)
$template = [IO.File]::ReadAllText($guestTemplatePath)
if (-not $template.Contains('__NIC_CONFIG_BASE64__')) { throw 'Guest script configuration marker is missing.' }
if (-not $template.Contains('__NIC_ADAPTER_ADDRESS_FUNCTIONS__')) { throw 'Guest adapter-address helper marker is missing.' }
$addressFunctions = [IO.File]::ReadAllText($addressHelperPath)
$rendered = $template.Replace('__NIC_CONFIG_BASE64__', $configBase64).Replace('__NIC_ADAPTER_ADDRESS_FUNCTIONS__', $addressFunctions)

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $OutputPath = Join-Path $outputRoot ($Phase.ToLowerInvariant() + '.' + $stamp + '.' + [Guid]::NewGuid().ToString('N') + '.ps1')
}
$resolvedOutput = Assert-UnderDirectory $OutputPath $outputRoot 'Generated guest script'
[void][IO.Directory]::CreateDirectory($outputRoot)
$stream = [IO.File]::Open($resolvedOutput, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    $renderedBytes = [Text.UTF8Encoding]::new($false).GetBytes($rendered)
    $stream.Write($renderedBytes, 0, $renderedBytes.Length)
    $stream.Flush($true)
}
finally { $stream.Dispose() }
$tokens = $null
$parseErrors = $null
[void][Management.Automation.Language.Parser]::ParseFile($resolvedOutput, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -gt 0) { throw ('Rendered guest script has parse errors: ' + (@($parseErrors | ForEach-Object { $_.Message }) -join '; ')) }
'GUEST ACTION RENDERED: phase=' + $Phase + '; output=' + $resolvedOutput + '; nativeSha256=' + $nativeImageHash + '; no guest command was executed.'
