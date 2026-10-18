[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$CaptureReaderPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$expectedReader = Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018\smbios-failure-diagnostic-r5-20261018T042000Z\build\smbios_windows_readback.exe'
$expectedReaderSha256 = 'C0EF07AC4698644758F2384BC8524CB87FB9946FE04972F47C3F6BD30A80A74C'
$expectedOutputRoot = Join-Path $mainRoot 'spoof-handoff\reports\combined-9field-vm-20261018\runtime'
$templatePath = Join-Path $PSScriptRoot 'guest-smbios-diagnostic.template.ps1'
$lifecycleId = 'smbios-failure-phase-r5-20261018'
$stateRoot = 'C:\ProgramData\CombinedVmLifecycle\f0114d56cf8a8a362772aae2fa2464d1\smbios-failure-phase-r5-20261018'

function Assert-ExactPath([string]$Actual, [string]$Expected, [string]$Label) {
    $actualFull = [IO.Path]::GetFullPath($Actual)
    $expectedFull = [IO.Path]::GetFullPath($Expected)
    if (-not $actualFull.Equals($expectedFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be the exact approved path: $expectedFull"
    }
}

Assert-ExactPath $CaptureReaderPath $expectedReader 'CaptureReaderPath'
$outputRoot = [IO.Path]::GetFullPath($expectedOutputRoot).TrimEnd('\') + '\'
$outputFull = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if (-not $outputFull.Equals($outputRoot.TrimEnd('\\'), [StringComparison]::OrdinalIgnoreCase) -and
    -not $outputFull.StartsWith($outputRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputDirectory must remain under $expectedOutputRoot"
}
foreach ($path in @($CaptureReaderPath, $templatePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required input is missing: $path" }
}

$readerHash = (Get-FileHash -LiteralPath $CaptureReaderPath -Algorithm SHA256).Hash.ToUpperInvariant()
if ($readerHash -cne $expectedReaderSha256) { throw "Pinned r5 reader hash mismatch: expected=$expectedReaderSha256 actual=$readerHash" }
$sourceText = [IO.File]::ReadAllText($templatePath)
$replacements = [ordered]@{
    '__CAPTURE_READER_SHA256__' = $readerHash
    '__LIFECYCLE_ID__' = $lifecycleId
    '__LIFECYCLE_STATE_ROOT__' = $stateRoot
}
foreach ($entry in $replacements.GetEnumerator()) {
    if (([regex]::Matches($sourceText, [regex]::Escape($entry.Key))).Count -ne 1) {
        throw "Guest diagnostic placeholder is missing or duplicated: $($entry.Key)"
    }
    $sourceText = $sourceText.Replace($entry.Key, [string]$entry.Value)
}
$rendered = $sourceText
[void][IO.Directory]::CreateDirectory($outputFull)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$scriptPath = Join-Path $outputFull ('guest-smbios-diagnostic.' + $stamp + '.' + [Guid]::NewGuid().ToString('N') + '.ps1')
[IO.File]::WriteAllText($scriptPath, $rendered, [Text.UTF8Encoding]::new($false))
$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count -gt 0) {
    Remove-Item -LiteralPath $scriptPath -Force
    throw ('Rendered guest diagnostic has PowerShell parse errors: ' + ($errors.Message -join '; '))
}
$scriptHash = (Get-FileHash -LiteralPath $scriptPath -Algorithm SHA256).Hash.ToUpperInvariant()
$receiptPath = [IO.Path]::ChangeExtension($scriptPath, '.render.json')
[ordered]@{
    schema = 'combined-vm-smbios-diagnostic-render.v1'
    templatePath = $templatePath
    templateSha256 = (Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash.ToUpperInvariant()
    captureReaderPath = $CaptureReaderPath
    captureReaderSha256 = $readerHash
    lifecycleId = $lifecycleId
    stateRoot = $stateRoot
    renderedScriptPath = $scriptPath
    renderedScriptSha256 = $scriptHash
    vmxPath = (Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018\DeviceFilter-Win11-Combined\DeviceFilter-Win11-Combined.vmx')
    vmxSha256 = (Get-FileHash -LiteralPath (Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018\DeviceFilter-Win11-Combined\DeviceFilter-Win11-Combined.vmx') -Algorithm SHA256).Hash.ToUpperInvariant()
    writtenUtc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding utf8
Write-Output "RENDERED READ-ONLY SMBIOS DIAGNOSTIC: $scriptPath"
Write-Output "SCRIPT SHA256: $scriptHash"
Write-Output "RENDER RECEIPT: $receiptPath"
