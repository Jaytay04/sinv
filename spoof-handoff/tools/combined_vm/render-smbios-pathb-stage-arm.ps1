[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$LifecycleId = 'smbios-failure-phase-r5-20261018'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$worktreeRoot = Join-Path $mainRoot '.codex-build\worktrees\smbios-boot-serial-probe'
$templatePath = Join-Path $worktreeRoot 'spoof-handoff\tools\combined_vm\guest-smbios-pathb-stage-arm.template.ps1'
$expectedOutput = Join-Path $mainRoot 'spoof-handoff\reports\combined-9field-vm-20261018\runtime'
$expectedVmx = Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018\DeviceFilter-Win11-Combined\DeviceFilter-Win11-Combined.vmx'
$fixtureManifestPath = Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018\smbios-failure-diagnostic-r5-20261018T042000Z\fixture-build.json'
$expectedFixtureManifestSha256 = '6D23EABF4C9711DDF4F8B234292AE06CDE070D2DBC662BE10F7F08FE009F4F09'
$expectedCollectorSha256 = 'BD86CA73EFACE5B2195DDF0AEEA4BE9BBB2BCD2DF778D77F22DF2D9B82AB60F9'
$expectedReaderSha256 = 'C0EF07AC4698644758F2384BC8524CB87FB9946FE04972F47C3F6BD30A80A74C'
$expectedLifecycleId = 'smbios-failure-phase-r5-20261018'
$expectedStateRoot = 'C:\ProgramData\CombinedVmLifecycle\f0114d56cf8a8a362772aae2fa2464d1\smbios-failure-phase-r5-20261018'

function Assert-ExactPath([string]$Actual, [string]$Expected, [string]$Label) {
    $actualFull = [IO.Path]::GetFullPath($Actual)
    $expectedFull = [IO.Path]::GetFullPath($Expected)
    if (-not $actualFull.Equals($expectedFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be the exact approved path: $expectedFull"
    }
}

if ($LifecycleId -cne $expectedLifecycleId) { throw 'Only the current r5 diagnostic lifecycle may be used for this one-shot action.' }
Assert-ExactPath $OutputDirectory $expectedOutput 'OutputDirectory'
foreach ($path in @($templatePath, $expectedVmx, $fixtureManifestPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required input is missing: $path" }
}
$fixtureManifestHash = (Get-FileHash -LiteralPath $fixtureManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
if ($fixtureManifestHash -cne $expectedFixtureManifestSha256) { throw "r5 fixture manifest hash mismatch: expected=$expectedFixtureManifestSha256 actual=$fixtureManifestHash" }
$fixtureManifest = Get-Content -LiteralPath $fixtureManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ([string]$fixtureManifest.result -cne 'PASS' -or [string]$fixtureManifest.buildIdentity.value -cne '9FEEC3B5542174E53384F98BA530B369') {
    throw 'The pinned r5 fixture manifest result/build identity is unexpected.'
}
$collectorPath = [string]$fixtureManifest.collector
$readerPath = [string]$fixtureManifest.windowsReadback
if ((Get-FileHash -LiteralPath $collectorPath -Algorithm SHA256).Hash.ToUpperInvariant() -cne $expectedCollectorSha256) { throw 'The pinned r5 collector hash does not match its build manifest.' }
if ((Get-FileHash -LiteralPath $readerPath -Algorithm SHA256).Hash.ToUpperInvariant() -cne $expectedReaderSha256) { throw 'The pinned r5 readback hash does not match its build manifest.' }

$template = [IO.File]::ReadAllText($templatePath)
$replacements = [ordered]@{
    '__LIFECYCLE_ID__' = $expectedLifecycleId
    '__LIFECYCLE_STATE_ROOT__' = $expectedStateRoot
    '__FIXTURE_MANIFEST_SHA256__' = $expectedFixtureManifestSha256
    '__FIXTURE_BUILD_ID__' = [string]$fixtureManifest.buildIdentity.value
}
foreach ($entry in $replacements.GetEnumerator()) {
    if (([regex]::Matches($template, [regex]::Escape($entry.Key))).Count -ne 1) {
        throw "Template placeholder must occur exactly once: $($entry.Key)"
    }
    $template = $template.Replace($entry.Key, [string]$entry.Value)
}

[void][IO.Directory]::CreateDirectory($OutputDirectory)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$scriptPath = Join-Path $OutputDirectory ('guest-smbios-pathb-stage-arm.' + $stamp + '.' + [Guid]::NewGuid().ToString('N') + '.ps1')
[IO.File]::WriteAllText($scriptPath, $template, [Text.UTF8Encoding]::new($false))
$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count -gt 0) {
    Remove-Item -LiteralPath $scriptPath -Force
    throw ('Rendered guest Path-B action has syntax errors: ' + ($errors.Message -join '; '))
}
$scriptHash = (Get-FileHash -LiteralPath $scriptPath -Algorithm SHA256).Hash.ToUpperInvariant()
$receiptPath = [IO.Path]::ChangeExtension($scriptPath, '.render.json')
[ordered]@{
    schema = 'combined-vm-smbios-pathb-stage-arm-render.v1'
    templatePath = $templatePath
    templateSha256 = (Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash.ToUpperInvariant()
    renderedScriptPath = $scriptPath
    renderedScriptSha256 = $scriptHash
    vmxPath = $expectedVmx
    vmxSha256 = (Get-FileHash -LiteralPath $expectedVmx -Algorithm SHA256).Hash.ToUpperInvariant()
    lifecycleId = $expectedLifecycleId
    stateRoot = $expectedStateRoot
    fixtureManifestPath = $fixtureManifestPath
    fixtureManifestSha256 = $expectedFixtureManifestSha256
    fixtureBuildId = [string]$fixtureManifest.buildIdentity.value
    collectorSha256 = $expectedCollectorSha256
    readerSha256 = $expectedReaderSha256
    renderedUtc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding utf8
Write-Output "RENDERED EFI-ONLY PATH-B STAGE/ARM: $scriptPath"
Write-Output "SCRIPT SHA256: $scriptHash"
Write-Output "RENDER RECEIPT: $receiptPath"
