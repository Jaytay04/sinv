[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$CaptureReaderPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$expectedReaderRoot = Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm\capture-tool-20261018T033000Z\build'
$expectedOutputRoot = Join-Path $mainRoot 'spoof-handoff\reports\combined-9field-vm-20261018\runtime'
$templatePath = Join-Path $PSScriptRoot 'guest-capture.template.ps1'

function Assert-ExactPath([string]$Actual, [string]$Expected, [string]$Label) {
    $actualFull = [IO.Path]::GetFullPath($Actual)
    $expectedFull = [IO.Path]::GetFullPath($Expected)
    if (-not $actualFull.Equals($expectedFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be the exact approved path: $expectedFull"
    }
}

Assert-ExactPath $CaptureReaderPath (Join-Path $expectedReaderRoot 'smbios_windows_readback.exe') 'CaptureReaderPath'
Assert-ExactPath $OutputDirectory $expectedOutputRoot 'OutputDirectory'
foreach ($path in @($CaptureReaderPath, $templatePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required input is missing: $path" }
}

$readerHash = (Get-FileHash -LiteralPath $CaptureReaderPath -Algorithm SHA256).Hash.ToUpperInvariant()
$readerBase64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($CaptureReaderPath))
$source = [IO.File]::ReadAllText($templatePath)
$readerPlaceholder = '__CAPTURE_READER_BASE64__'
$hashPlaceholder = '__CAPTURE_READER_SHA256__'
if (([regex]::Matches($source, [regex]::Escape($readerPlaceholder))).Count -ne 1 -or
    ([regex]::Matches($source, [regex]::Escape($hashPlaceholder))).Count -ne 1) {
    throw 'Guest capture template placeholders are missing or duplicated.'
}
$rendered = $source.Replace($readerPlaceholder, $readerBase64).Replace($hashPlaceholder, $readerHash)
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$scriptPath = Join-Path $OutputDirectory ('guest-capture.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N') + '.ps1')
[IO.File]::WriteAllText($scriptPath, $rendered, [Text.UTF8Encoding]::new($false))
$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count -gt 0) {
    Remove-Item -LiteralPath $scriptPath -Force
    throw ('Rendered guest capture has PowerShell parse errors: ' + ($errors.Message -join '; '))
}
$scriptHash = (Get-FileHash -LiteralPath $scriptPath -Algorithm SHA256).Hash.ToUpperInvariant()
$metadataPath = [IO.Path]::ChangeExtension($scriptPath, '.render.json')
[ordered]@{
    schema = 'combined-vm-guest-capture-render.v1'
    templatePath = $templatePath
    templateSha256 = (Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash.ToUpperInvariant()
    captureReaderPath = $CaptureReaderPath
    captureReaderSha256 = $readerHash
    renderedScriptPath = $scriptPath
    renderedScriptSha256 = $scriptHash
    outputDirectory = $OutputDirectory
    renderedUtc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $metadataPath -Encoding utf8
Write-Output "RENDERED GUEST CAPTURE: $scriptPath"
Write-Output "SHA256: $scriptHash"
Write-Output "RENDER RECEIPT: $metadataPath"
