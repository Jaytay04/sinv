[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GuestInventoryPath,
    [string]$OutputDirectory = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\spoof-handoff\reports\combined-9field-vm-20261018\runtime'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$fixtureRoot = Join-Path $mainRoot 'spoof-handoff\reports\nic-vmnet-miniport-20261018\vmnet-miniport-20261018T004517Z.3b91e91f'
$templatePath = Join-Path $fixtureRoot 'install-probe.template.ps1'
$assetRoot = Join-Path $fixtureRoot 'inputs'
$expectedOutput = Join-Path $mainRoot 'spoof-handoff\reports\combined-9field-vm-20261018\runtime'

function Assert-ExactPath([string]$Actual, [string]$Expected, [string]$Label) {
    $actualFull = [IO.Path]::GetFullPath($Actual)
    $expectedFull = [IO.Path]::GetFullPath($Expected)
    if (-not $actualFull.Equals($expectedFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be the exact approved path: $expectedFull"
    }
}

Assert-ExactPath $OutputDirectory $expectedOutput 'OutputDirectory'
$reportsRoot = [IO.Path]::GetFullPath((Join-Path $mainRoot 'spoof-handoff\reports')).TrimEnd('\') + '\'
$inventoryFullPath = [IO.Path]::GetFullPath($GuestInventoryPath)
if (-not $inventoryFullPath.StartsWith($reportsRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'GuestInventoryPath must remain under spoof-handoff/reports.'
}
foreach ($path in @($inventoryFullPath, $templatePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required input is missing: $path" }
}

$inventory = Get-Content -LiteralPath $inventoryFullPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
if ([string]$inventory.schema -cne 'combined-vm-original-inventory.v1') {
    throw 'Guest inventory schema is not the expected combined VM original schema.'
}
if ([string]$inventory.machine.manufacturer -notmatch '^(?i:VMware(?:,\s*Inc\.)?)$') {
    throw 'Guest inventory does not establish VMware as the machine manufacturer.'
}
$guestUuid = [Guid]::Empty
$smbiosUuid = [Guid]::Empty
if (-not [Guid]::TryParse([string]$inventory.machine.uuidWmi, [ref]$guestUuid) -or
    -not [Guid]::TryParse([string]$inventory.machine.uuidSmbios, [ref]$smbiosUuid) -or
    $guestUuid -ne $smbiosUuid) {
    throw 'Guest inventory WMI and SMBIOS UUID values are invalid or differ.'
}
$boundUuid = $guestUuid.ToString('D').ToUpperInvariant()

$assetPins = @(
    [ordered]@{ name='devcon.exe'; path=(Join-Path $assetRoot 'devcon.exe'); size=121328; sha256='BB3FB8661A4DC1586FDCE5E4C177A8728A200A77B554DE1CDDB0251D12F322FF' },
    [ordered]@{ name='netadapter.inf'; path=(Join-Path $assetRoot 'vmware-package\netadapter.inf'); size=24186; sha256='9EAAB742E85B31E311D2B50ED3C8F2DE252D0EA700A3C4D7AB4F0C33E32378DB' },
    [ordered]@{ name='vmnet.sys'; path=(Join-Path $assetRoot 'vmware-package\vmnet.sys'); size=31824; sha256='A1F6CAECB25BDD17963F9EF836030263C0ECC69E9D4C3EB8BFED971882824941' },
    [ordered]@{ name='vmnetadapter.cat'; path=(Join-Path $assetRoot 'vmware-package\vmnetadapter.cat'); size=13311; sha256='612701059A78733CC93D2CBADBD378179C011ADB2B454E78B1D10789F5E0D39C' },
    [ordered]@{ name='vmnetadapter.sys'; path=(Join-Path $assetRoot 'vmware-package\vmnetadapter.sys'); size=31312; sha256='900DAB67810607208D37C7EF441D607D11D294650183475AF50158FAECEC7CCE' }
)
$assets = [Collections.Generic.List[object]]::new()
foreach ($pin in $assetPins) {
    if (-not (Test-Path -LiteralPath $pin.path -PathType Leaf)) { throw "Pinned VMnet input is missing: $($pin.path)" }
    $bytes = [IO.File]::ReadAllBytes($pin.path)
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $actualHash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
        finally { $sha.Dispose() }
        if ($bytes.Length -ne [int]$pin.size -or $actualHash -cne [string]$pin.sha256) {
            throw "VMnet input hash/size mismatch: $($pin.name); size=$($bytes.Length); sha256=$actualHash"
        }
        $assets.Add([ordered]@{ name=[string]$pin.name; size=$bytes.Length; sha256=$actualHash; base64=[Convert]::ToBase64String($bytes) })
    }
    finally { [Array]::Clear($bytes, 0, $bytes.Length) }
}

$template = [IO.File]::ReadAllText($templatePath)
$assetToken = '__ASSETS_JSON_BASE64__'
if (([regex]::Matches($template, [regex]::Escape($assetToken))).Count -ne 1) {
    throw 'VMnet asset placeholder must occur exactly once in the pinned template.'
}
$pinnedUuid = 'D7C14D56-24D1-9894-8906-C0CE95A07790'
if (([regex]::Matches($template, [regex]::Escape($pinnedUuid))).Count -ne 1) {
    throw 'VMnet template identity guard drifted; expected one exact prior UUID literal.'
}
$template = $template.Replace($pinnedUuid, $boundUuid)
$assetsJson = ConvertTo-Json -InputObject @($assets.ToArray()) -Depth 8 -Compress
$assetBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($assetsJson))
$guestScript = $template.Replace($assetToken, $assetBase64)
if ([regex]::IsMatch($guestScript, '__[A-Z0-9_]+__')) { throw 'Rendered VMnet install script retains an unresolved placeholder.' }
$tokens = $null
$parseErrors = $null
[void][Management.Automation.Language.Parser]::ParseInput($guestScript, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -gt 0) {
    throw ('Rendered VMnet install script has PowerShell parse errors: ' + (($parseErrors | ForEach-Object { $_.Message }) -join '; '))
}

[void][IO.Directory]::CreateDirectory($OutputDirectory)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$payloadPath = Join-Path $OutputDirectory "guest-vmnet-install.$stamp.$([Guid]::NewGuid().ToString('N')).ps1"
[IO.File]::WriteAllText($payloadPath, $guestScript, [Text.UTF8Encoding]::new($false))
$payloadHash = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash.ToUpperInvariant()
$manifestPath = [IO.Path]::ChangeExtension($payloadPath, '.render.json')
$manifest = [ordered]@{
    schema = 'combined-vm-vmnet-install-render.v1'
    renderedUtc = [DateTime]::UtcNow.ToString('o')
    templatePath = $templatePath
    templateSha256 = (Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash.ToUpperInvariant()
    guestInventoryPath = $inventoryFullPath
    guestInventorySha256 = (Get-FileHash -LiteralPath $inventoryFullPath -Algorithm SHA256).Hash.ToUpperInvariant()
    vmUuidSource = 'Fresh Win32_ComputerSystemProduct.UUID and SMBIOS Type 1 UUID match in captured guest inventory.'
    boundVmUuid = $boundUuid
    assetInputs = @(
        foreach ($pin in $assetPins) {
            [ordered]@{ name=[string]$pin.name; path=[string]$pin.path; size=[int]$pin.size; sha256=[string]$pin.sha256 }
        }
    )
    renderedScriptPath = $payloadPath
    renderedScriptSha256 = $payloadHash
}
[IO.File]::WriteAllText($manifestPath, (ConvertTo-Json -InputObject $manifest -Depth 12), [Text.UTF8Encoding]::new($false))
Write-Output "RENDERED VMNET INSTALL: $payloadPath"
Write-Output "BOUND GUEST UUID: $boundUuid"
Write-Output "SHA256: $payloadHash"
Write-Output "RENDER RECEIPT: $manifestPath"
