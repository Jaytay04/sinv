[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SmbiosReaderPath,
    [Parameter(Mandatory = $true)][string]$NicNativeReaderPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$smbiosRoot = Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm\capture-tool-20261018T033000Z\build'
$nicRoot = Join-Path $mainRoot 'spoof-handoff\artifacts\nic-reader-gap-20261017\native-build-vs18\Release'
$expectedOutput = Join-Path $mainRoot 'spoof-handoff\reports\combined-9field-vm-20261018\runtime'
$storageReaderPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\storage_host\read.ps1'))
$templatePath = Join-Path $PSScriptRoot 'guest-inventory.template.ps1'

function Assert-ExactPath([string]$Actual, [string]$Expected, [string]$Label) {
    $actualFull = [IO.Path]::GetFullPath($Actual)
    $expectedFull = [IO.Path]::GetFullPath($Expected)
    if (-not $actualFull.Equals($expectedFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be the exact approved path: $expectedFull"
    }
}

Assert-ExactPath $SmbiosReaderPath (Join-Path $smbiosRoot 'smbios_windows_readback.exe') 'SmbiosReaderPath'
Assert-ExactPath $NicNativeReaderPath (Join-Path $nicRoot 'nic_native_read.exe') 'NicNativeReaderPath'
Assert-ExactPath $OutputDirectory $expectedOutput 'OutputDirectory'
foreach ($path in @($SmbiosReaderPath, $NicNativeReaderPath, $storageReaderPath, $templatePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required input is missing: $path" }
}

$inputMap = [ordered]@{
    smbiosReader = [IO.Path]::GetFullPath($SmbiosReaderPath)
    nicReader = [IO.Path]::GetFullPath($NicNativeReaderPath)
    storageReader = $storageReaderPath
}
$encoded = [ordered]@{}
$hashes = [ordered]@{}
foreach ($name in $inputMap.Keys) {
    $path = [string]$inputMap[$name]
    $bytes = [IO.File]::ReadAllBytes($path)
    try { $encoded[$name] = [Convert]::ToBase64String($bytes) }
    finally { [Array]::Clear($bytes, 0, $bytes.Length) }
    $hashes[$name] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
}

$template = [IO.File]::ReadAllText($templatePath)
$placeholderMap = [ordered]@{
    '__SMBIOS_READER_BASE64__' = $encoded.smbiosReader
    '__NIC_READER_BASE64__' = $encoded.nicReader
    '__STORAGE_READER_BASE64__' = $encoded.storageReader
    '__SMBIOS_READER_SHA256__' = $hashes.smbiosReader
    '__NIC_READER_SHA256__' = $hashes.nicReader
    '__STORAGE_READER_SHA256__' = $hashes.storageReader
}
foreach ($placeholder in $placeholderMap.Keys) {
    if (([regex]::Matches($template, [regex]::Escape($placeholder))).Count -ne 1) {
        throw "Inventory template placeholder is missing or duplicated: $placeholder"
    }
    $template = $template.Replace($placeholder, [string]$placeholderMap[$placeholder])
}

[void][IO.Directory]::CreateDirectory($OutputDirectory)
$scriptPath = Join-Path $OutputDirectory ('guest-inventory.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N') + '.ps1')
[IO.File]::WriteAllText($scriptPath, $template, [Text.UTF8Encoding]::new($false))
$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count -gt 0) {
    Remove-Item -LiteralPath $scriptPath -Force
    throw ('Rendered guest inventory has PowerShell parse errors: ' + ($errors.Message -join '; '))
}
$scriptHash = (Get-FileHash -LiteralPath $scriptPath -Algorithm SHA256).Hash.ToUpperInvariant()
$metadataPath = [IO.Path]::ChangeExtension($scriptPath, '.render.json')
[ordered]@{
    schema = 'combined-vm-guest-inventory-render.v1'
    templatePath = $templatePath
    templateSha256 = (Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash.ToUpperInvariant()
    inputs = @(
        foreach ($name in $inputMap.Keys) {
            [ordered]@{ name=$name; path=$inputMap[$name]; sha256=$hashes[$name] }
        }
    )
    renderedScriptPath = $scriptPath
    renderedScriptSha256 = $scriptHash
    outputDirectory = $OutputDirectory
    renderedUtc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $metadataPath -Encoding utf8
Write-Output "RENDERED GUEST INVENTORY: $scriptPath"
Write-Output "SHA256: $scriptHash"
Write-Output "RENDER RECEIPT: $metadataPath"
