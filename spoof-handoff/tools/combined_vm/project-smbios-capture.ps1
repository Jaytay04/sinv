[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$SourceMetadataPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [Parameter(Mandatory = $true)]
    [string]$PinPath,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$ReferenceRepo
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Assert-UnderRoot([string]$Root, [string]$Path, [string]$Label) {
    $rootFull = (Get-FullPath $Root) + '\'
    $pathFull = Get-FullPath $Path
    if (-not $pathFull.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must remain under $Root"
    }
}

$repo = Get-FullPath $ReferenceRepo
$reportsRoot = Join-Path $repo 'spoof-handoff\reports'
$sourcePath = [IO.Path]::GetFullPath($SourceMetadataPath)
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
$pinFullPath = [IO.Path]::GetFullPath($PinPath)
Assert-UnderRoot $reportsRoot $outputFullPath 'OutputPath'
Assert-UnderRoot $reportsRoot $pinFullPath 'PinPath'
if (Test-Path -LiteralPath $outputFullPath) { throw "OutputPath must be new: $outputFullPath" }
if (Test-Path -LiteralPath $pinFullPath) { throw "PinPath must be new: $pinFullPath" }

$sourceText = Get-Content -LiteralPath $sourcePath -Raw
$metadata = $sourceText | ConvertFrom-Json
$capture = $metadata.smbiosCapture
if ($null -eq $capture -or $capture.schema -cne 'smbios-windows-capture.v1' -or
    $capture.mode -cne 'capture' -or $capture.result -cne 'PASS') {
    throw 'Source metadata does not contain a successful SMBIOS Windows capture.'
}
$requiredFields = @(
    'system_serial', 'system_vendor', 'system_product', 'board_serial',
    'board_vendor', 'board_product', 'system_uuid'
)
$actualFields = @($capture.fields.PSObject.Properties.Name)
if ($actualFields.Count -ne $requiredFields.Count) {
    throw "Source SMBIOS capture has $($actualFields.Count) fields; expected exactly seven."
}
foreach ($field in $requiredFields) {
    if (@($actualFields | Where-Object { $_ -ceq $field }).Count -ne 1) {
        throw "Source SMBIOS capture is missing or duplicates $field."
    }
    $entry = $capture.fields.$field
    if ($entry.nativeMatchesWmi -ne $true -or
        [string]::IsNullOrEmpty([string]$entry.observedNative) -or
        [string]$entry.observedNative -cne [string]$entry.observedWmi) {
        throw "Source SMBIOS native/WMI reads do not agree for $field."
    }
}
if ([string]$capture.machineIdentity.systemVendorNative -cne 'VMware, Inc.' -or
    [string]$capture.machineIdentity.systemVendorWmi -cne 'VMware, Inc.' -or
    [string]$capture.machineIdentity.systemProductNative -cne 'VMware20,1' -or
    [string]$capture.machineIdentity.systemProductWmi -cne 'VMware20,1') {
    throw 'Source capture is not from the expected VMware20,1 guest.'
}

$outputDirectory = Split-Path -Parent $outputFullPath
$pinDirectory = Split-Path -Parent $pinFullPath
foreach ($directory in @($outputDirectory, $pinDirectory)) {
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw "Output directory must already exist: $directory"
    }
}
$captureJson = ConvertTo-Json -InputObject $capture -Depth 50
[IO.File]::WriteAllText($outputFullPath, $captureJson + "`r`n", [Text.UTF8Encoding]::new($false))
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToUpperInvariant()
$projectionHash = (Get-FileHash -LiteralPath $outputFullPath -Algorithm SHA256).Hash.ToUpperInvariant()
$sha256 = [Security.Cryptography.SHA256]::Create()
try {
    $captureBytes = [Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $capture -Depth 50 -Compress))
    $sourceCaptureHash = ([BitConverter]::ToString($sha256.ComputeHash($captureBytes))).Replace('-', '')
}
finally {
    $sha256.Dispose()
}
$pin = [ordered]@{
    schema = 'combined-vm-smbios-capture-projection.v1'
    result = 'PASS'
    sourceMetadataPath = $sourcePath
    sourceMetadataSha256 = $sourceHash
    sourceSmbiosCaptureSha256 = $sourceCaptureHash
    projectionPath = $outputFullPath
    projectionSha256 = $projectionHash
    guestIdentity = [ordered]@{
        systemVendor = [string]$capture.machineIdentity.systemVendorNative
        systemProduct = [string]$capture.machineIdentity.systemProductNative
        systemUuid = [string]$capture.fields.system_uuid.observedNative
    }
}
[IO.File]::WriteAllText($pinFullPath, ($pin | ConvertTo-Json -Depth 8) + "`r`n", [Text.UTF8Encoding]::new($false))
Write-Output "PASS: projected seven-field VMware SMBIOS capture: $outputFullPath"
Write-Output "SOURCE SHA256: $sourceHash"
Write-Output "PROJECTION SHA256: $projectionHash"
Write-Output "SOURCE PIN: $pinFullPath"
