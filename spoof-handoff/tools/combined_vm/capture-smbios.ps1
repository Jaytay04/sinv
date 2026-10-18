[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$ReadbackExePath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$exe = [IO.Path]::GetFullPath($ReadbackExePath)
$output = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $output) {
    throw "Capture output must be a new file: $output"
}
$parent = Split-Path -Parent $output
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    throw "Capture output directory does not exist: $parent"
}

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $runOutput = @(& $exe --capture --output $output 2>&1)
    $exitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousErrorActionPreference
}
if ($exitCode -ne 0) {
    $diagnostic = @($runOutput | ForEach-Object { [string]$_ }) -join ' '
    throw "SMBIOS capture failed with exit code ${exitCode}: $diagnostic Preserve the capture report if one was written: $output"
}
if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
    throw 'SMBIOS capture returned success without creating its report.'
}

$capture = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
if ($capture.schema -cne 'smbios-windows-capture.v1' -or
    $capture.mode -cne 'capture' -or $capture.result -cne 'PASS') {
    throw "SMBIOS capture report is incomplete or failed: $output"
}
$required = @(
    'system_serial', 'system_vendor', 'system_product', 'board_serial',
    'board_vendor', 'board_product', 'system_uuid'
)
foreach ($field in $required) {
    $item = $capture.fields.$field
    if ($null -eq $item -or $item.nativeMatchesWmi -ne $true -or
        [string]::IsNullOrEmpty([string]$item.observedNative) -or
        [string]$item.observedNative -cne [string]$item.observedWmi) {
        throw "SMBIOS capture has a missing or mismatched native/WMI field: $field"
    }
}
Write-Output "PASS: fresh RSMB and WMI values captured for all seven SMBIOS fields: $output"
