[CmdletBinding()]
param(
    [string]$RepoRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptWorktreeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$worktreeRoot = if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $scriptWorktreeRoot } else { [IO.Path]::GetFullPath($RepoRoot) }
$worktreeRoot = $worktreeRoot.TrimEnd('\')
if (-not $worktreeRoot.Equals($scriptWorktreeRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Renderer tests must use their containing worktree as RepoRoot: $scriptWorktreeRoot"
}
$defaultOutput = Join-Path $worktreeRoot 'spoof-handoff\reports\f12-release\runtime\fixture-correction'
$renderer = Join-Path $PSScriptRoot 'render-fixture-correction.ps1'
$templatePath = Join-Path $PSScriptRoot 'guest-fixture-correction.template.ps1'
$templateSource = [IO.File]::ReadAllText($templatePath)
$vmMap = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\spoof-handoff\reports\combined-9field-vm-20261018\vmnet-baseline\vm-fixture-map-fresh.20261018T024623Z.tsv'
$lockedMap = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\spoof-handoff\reports\client-originals.tsv'
$checks = [Collections.Generic.List[object]]::new()
$runId = [Guid]::NewGuid().ToString('N')
$testOutput = Join-Path $defaultOutput ('dry-tests.' + $runId)

function Assert-True([bool]$Condition, [string]$Name, $Actual = $null) {
    if (-not $Condition) { throw "$Name failed: actual=$Actual" }
    $checks.Add([ordered]@{ name = $Name; pass = $true; actual = $Actual })
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Get-RenderedConfig([string]$ScriptPath) {
    $source = [IO.File]::ReadAllText($ScriptPath)
    $tokens = $null
    $parseErrors = $null
    [void][Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$parseErrors)
    if (@($parseErrors).Count -ne 0) { throw "Rendered guest script parse failed: $ScriptPath" }
    Assert-True (-not $source.Contains('__CONFIG_BASE64__')) 'rendered script has no config placeholder' $ScriptPath
    $match = [regex]::Match($source, "\`$configBytes\s*=\s*\[Convert\]::FromBase64String\('(?<data>[A-Za-z0-9+/=]+)'\)")
    if (-not $match.Success) { throw "Rendered guest script has no embedded config: $ScriptPath" }
    $bytes = [Convert]::FromBase64String($match.Groups['data'].Value)
    try { return [Text.Encoding]::UTF8.GetString($bytes) | ConvertFrom-Json -ErrorAction Stop }
    finally { [Array]::Clear($bytes, 0, $bytes.Length) }
}

[void][IO.Directory]::CreateDirectory($testOutput)
$wrongRootOutput = Join-Path $testOutput 'wrong-root-output'
$priorErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $wrongRootLines = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $renderer -RepoRoot (Join-Path $worktreeRoot 'not-the-worktree') -Action CaptureCorrectionOnly -OutputDirectory $wrongRootOutput 2>&1 | ForEach-Object { [string]$_ })
    $wrongRootExitCode = [int]$LASTEXITCODE
} finally { $ErrorActionPreference = $priorErrorActionPreference }
Assert-True ($wrongRootExitCode -ne 0 -and -not (Test-Path -LiteralPath $wrongRootOutput)) 'renderer rejects a RepoRoot that differs from its pinned worktree' ($wrongRootLines -join ' | ')

$actions = @(
    [pscustomobject]@{ name = 'StageArmCorrectionOnly'; mode = 'correction-only' },
    [pscustomobject]@{ name = 'CaptureCorrectionOnly'; mode = 'correction-only' },
    [pscustomobject]@{ name = 'StageArmActive'; mode = 'corrected-collector' },
    [pscustomobject]@{ name = 'CaptureActive'; mode = 'corrected-collector' },
    [pscustomobject]@{ name = 'RecoverPending'; mode = 'recovery' }
)
$receipts = [Collections.Generic.List[object]]::new()
$sharedSetIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)

foreach ($action in $actions) {
    $outputLines = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $renderer -RepoRoot $worktreeRoot -Action $action.name -OutputDirectory $testOutput 2>&1 | ForEach-Object { [string]$_ })
    $exitCode = [int]$LASTEXITCODE
    if ($exitCode -ne 0) { throw "Renderer failed for $($action.name), exit=$exitCode; output=$($outputLines -join ' | ')" }
    $result = ($outputLines -join [Environment]::NewLine) | ConvertFrom-Json -ErrorAction Stop
    Assert-True ([string]$result.status -ceq 'PASS') "$($action.name) render status" $result.status
    Assert-True ([string]$result.mode -ceq $action.mode) "$($action.name) mode" $result.mode
    Assert-True (Test-Path -LiteralPath ([string]$result.scriptPath) -PathType Leaf) "$($action.name) output script exists" $result.scriptPath
    Assert-True (Test-Path -LiteralPath ([string]$result.receiptPath) -PathType Leaf) "$($action.name) receipt exists" $result.receiptPath
    Assert-True ([string](Get-Sha256 ([string]$result.scriptPath)) -ceq [string]$result.scriptSha256) "$($action.name) script hash" $result.scriptSha256
    Assert-True ([string](Get-Sha256 ([string]$result.receiptPath)) -ceq [string]$result.receiptSha256) "$($action.name) receipt hash" $result.receiptSha256
    Assert-True ([IO.Path]::GetFullPath([string]$result.scriptPath).StartsWith(([IO.Path]::GetFullPath($worktreeRoot).TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase)) "$($action.name) output confinement" $result.scriptPath

    $receipt = Get-Content -LiteralPath ([string]$result.receiptPath) -Raw | ConvertFrom-Json -ErrorAction Stop
    Assert-True ([string]$receipt.schema -ceq 'combined-vm-fixture-correction-render.v1') "$($action.name) receipt schema" $receipt.schema
    Assert-True ([string]$receipt.script.sha256 -ceq [string]$result.scriptSha256) "$($action.name) receipt script pin" $receipt.script.sha256
    Assert-True ([string]$receipt.sharedAssets.guestWriteAccessPermitted -ceq 'False') "$($action.name) shared asset write access denied" $receipt.sharedAssets.guestWriteAccessPermitted
    Assert-True (@($receipt.sharedAssets.files).Count -eq 5) "$($action.name) asset count" @($receipt.sharedAssets.files).Count
    [void]$sharedSetIds.Add([string]$receipt.sharedAssets.setId)
    foreach ($pin in @($receipt.sourcePins)) {
        Assert-True ((Test-Path -LiteralPath ([string]$pin.path) -PathType Leaf) -and (Get-Sha256 ([string]$pin.path) -ceq [string]$pin.sha256)) "$($action.name) source pin $($pin.name)" $pin.path
    }
    foreach ($asset in @($receipt.sharedAssets.files)) {
        $assetPath = Join-Path ([string]$receipt.sharedAssets.hostRoot) (Join-Path ([string]$receipt.sharedAssets.setId) ([string]$asset.localName))
        Assert-True ((Test-Path -LiteralPath $assetPath -PathType Leaf) -and (Get-Sha256 $assetPath -ceq [string]$asset.sha256)) "$($action.name) shared asset $($asset.localName)" $assetPath
    }

    $config = Get-RenderedConfig ([string]$result.scriptPath)
    Assert-True ([string]$config.internalAction -ceq $action.name) "$($action.name) embedded action" $config.internalAction
    Assert-True ([string]$config.mode -ceq $action.mode) "$($action.name) embedded mode" $config.mode
    Assert-True ([string]$config.lifecycleId -ceq 'smbios2-ep-correction-f12-r2-release-20261018') "$($action.name) uses the isolated F12-R2 lifecycle ID" $config.lifecycleId
    Assert-True ([string]$receipt.lifecycleId -ceq 'smbios2-ep-correction-f12-r2-release-20261018') "$($action.name) receipt uses the isolated F12-R2 lifecycle ID" $receipt.lifecycleId
    Assert-True ([string]$config.collectorManifestSha256 -ceq '51D2A19C2A62BCDF2D9D62000789704B6F1190357BEE8E8505C5C271988C79EE') "$($action.name) final2 manifest pin" $config.collectorManifestSha256
    Assert-True ([string]$config.assets.collectorSha256 -ceq 'BCB5B25FD38857A80044EE697249F784A04662E35036F8CC067689074D42A014') "$($action.name) final2 collector pin" $config.assets.collectorSha256
    Assert-True ([string]$config.preludeSha256 -ceq '55436571C6D2CC23E120F0470B26C6611DCE94F88B2D04A8ECA0AEF3BC3E0676') "$($action.name) F12-R2 prelude pin" $config.preludeSha256
    Assert-True ([string]$receipt.fixtureBuild.path -like '*spoof-handoff\reports\f12-release\r2\fixture-build.json') "$($action.name) F12-R2 fixture manifest path" $receipt.fixtureBuild.path
    Assert-True ([string]$receipt.fixtureBuild.sha256 -ceq '947AB2ABFBF6A92C9E20F303F1911FBEFCDD9872CEE9CE483624DE28C3D6EA24') "$($action.name) F12-R2 fixture manifest pin" $receipt.fixtureBuild.sha256
    Assert-True ([string]$receipt.fixtureBuild.buildId -ceq '1C74082DBD2749E99203377719AC0A5E') "$($action.name) F12-R2 fixture build ID" $receipt.fixtureBuild.buildId
    Assert-True ([string]$receipt.fixtureBuild.sourceSetSha256 -ceq 'F1D159D23C7D37622EA715E5D8367B35A69A8B64F25FA712DBD5F84BF7D62939') "$($action.name) F12-R2 fixture source set" $receipt.fixtureBuild.sourceSetSha256
    Assert-True ([string]$receipt.fixtureBuild.preludeSha256 -ceq '55436571C6D2CC23E120F0470B26C6611DCE94F88B2D04A8ECA0AEF3BC3E0676') "$($action.name) F12-R2 manifest prelude pin" $receipt.fixtureBuild.preludeSha256
    Assert-True ([string]$config.stageBaselineCollectorSha256 -ceq 'BD86CA73EFACE5B2195DDF0AEEA4BE9BBB2BCD2DF778D77F22DF2D9B82AB60F9') "$($action.name) historical stage collector pin" $config.stageBaselineCollectorSha256
    Assert-True ([string]$config.stageBaselineScope -like 'historical-*') "$($action.name) historical stage evidence scope" $config.stageBaselineScope
    Assert-True ([string]$config.stageBaselineCollectorSha256 -cne [string]$config.assets.collectorSha256) "$($action.name) distinguishes final2 collector from historical stage collector" $config.assets.collectorSha256
    $prelude = [Convert]::FromBase64String([string]$config.preludeBase64)
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $actualPrelude = ([BitConverter]::ToString($sha.ComputeHash($prelude))).Replace('-', '') }
        finally { $sha.Dispose() }
    } finally { [Array]::Clear($prelude, 0, $prelude.Length) }
    Assert-True ($actualPrelude -ceq [string]$config.preludeSha256) "$($action.name) embedded prelude bytes" $actualPrelude

    $lockedRows = @(Import-Csv -LiteralPath $lockedMap -Delimiter "`t")
    $vmRows = @(Import-Csv -LiteralPath $vmMap -Delimiter "`t")
    foreach ($mapMember in @('FakeSMBIOSSerial','FakeSMBIOSManufacturer','FakeSMBIOSProduct','FakeMotherboardSerial','FakeMotherboardManufacturer','FakeMotherboardProduct','FakeSMBIOSUUID')) {
        $locked = @($lockedRows | Where-Object { [string]$_.member -ceq $mapMember })
        $vm = @($vmRows | Where-Object { [string]$_.member -ceq $mapMember })
        if ($locked.Count -ne 1 -or $vm.Count -ne 1) { throw "Expected exactly one locked and VM map row for $mapMember." }
        $field = switch ($mapMember) {
            'FakeSMBIOSSerial' { 'system_serial' }
            'FakeSMBIOSManufacturer' { 'system_vendor' }
            'FakeSMBIOSProduct' { 'system_product' }
            'FakeMotherboardSerial' { 'board_serial' }
            'FakeMotherboardManufacturer' { 'board_vendor' }
            'FakeMotherboardProduct' { 'board_product' }
            'FakeSMBIOSUUID' { 'system_uuid' }
        }
        Assert-True ([string]$config.smbios.$field.original -ceq [string]$vm[0].original) "$($action.name) original $field" $config.smbios.$field.original
        Assert-True ([string]$config.smbios.$field.replacement -ceq [string]$locked[0].replacement -and [string]$config.smbios.$field.replacement -ceq [string]$vm[0].replacement) "$($action.name) exact locked replacement $field" $config.smbios.$field.replacement
    }
    $receipts.Add([ordered]@{ action = $action.name; script = [string]$result.scriptPath; scriptSha256 = [string]$result.scriptSha256; receipt = [string]$result.receiptPath; receiptSha256 = [string]$result.receiptSha256 })
}

Assert-True ($sharedSetIds.Count -eq 1) 'all phases use one immutable shared asset set' (@($sharedSetIds) -join ',')
Assert-True ($templateSource.Contains("`$metadata.collectorReport.efiStatus = [string]`$metadata.efi.status")) 'collector EFI status is sourced from the prelude report' 'metadata.efi.status'
Assert-True ($templateSource.Contains("if (`$mode -ceq 'corrected-collector') { 'LoadImage(loader.efi)' } else { 'LoadImage(bootmgfw.efi)' }")) 'EFI phase names match the actual selected child image' 'loader.efi or bootmgfw.efi'
Assert-True ($templateSource.Contains('$script:fixtureJournalOwnedThisInvocation -and -not $script:recoveryAttempted')) 'failed fixture actions attempt journal-owned recovery' 'automatic recovery guard'
Assert-True ($templateSource.Contains('combined-vm-fixture-correction-journal.v2')) 'durable journal schema is versioned' 'v2'
Assert-True ($templateSource.Contains('function Get-PendingFixtureJournal')) 'RecoverPending discovers only exact lifecycle journals' 'Get-PendingFixtureJournal'

$tokens = $null
$parseErrors = $null
$templateAst = [Management.Automation.Language.Parser]::ParseInput($templateSource, [ref]$tokens, [ref]$parseErrors)
$stageFunction = @($templateAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Stage-And-Arm' }, $false))
$allocatorFunction = @($templateAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-FreeBootNumber' }, $false))
$assetBootstrapFunction = @($templateAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Install-VerifiedReadOnlyAsset' }, $false))
$volumeGuidFunction = @($templateAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Normalize-MountvolVolumeGuid' }, $false))
$partitionFactsFunction = @($templateAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-PartitionFacts' }, $false))
if ($stageFunction.Count -ne 1 -or $allocatorFunction.Count -ne 1 -or $assetBootstrapFunction.Count -ne 1 -or $volumeGuidFunction.Count -ne 1 -or $partitionFactsFunction.Count -ne 1) { throw 'Could not locate the production boot allocation, asset bootstrap, and ESP lookup functions.' }
$stageSource = $stageFunction[0].Extent.Text
Assert-True ($stageSource.IndexOf("Invoke-PathB 'Arm'", [StringComparison]::Ordinal) -lt $stageSource.IndexOf('Get-FreeBootNumber -ExcludeNumbers', [StringComparison]::Ordinal)) 'custom Boot#### allocation occurs after standard Path-B allocation' 'Path-B Arm precedes custom allocation'
Assert-True ($stageSource.Contains('if ($customNumber -eq $standardNumber) { throw')) 'custom allocation collision fails closed' 'explicit standard/custom inequality'
Invoke-Expression $allocatorFunction[0].Extent.Text
function ConvertTo-EfiBootVariableName([int]$Number) { return ('Boot{0:X4}' -f $Number) }
function Read-VariableOrAbsent([string]$Name) { return [pscustomobject]@{ supported = $false; win32Error = 203 } }
$collisionCandidate = Get-FreeBootNumber -ExcludeNumbers @(65535)
Assert-True ($collisionCandidate -ne 65535 -and $collisionCandidate -eq 65534) 'allocator skips Path-B selected Boot####' $collisionCandidate

# Execute the production asset bootstrap against ordinary local files. This catches
# path construction that turns a file path into a directory path before Test-Path.
Invoke-Expression $assetBootstrapFunction[0].Extent.Text
$assetBootstrapRoot = Join-Path $testOutput 'asset-bootstrap'
[void][IO.Directory]::CreateDirectory($assetBootstrapRoot)
$assetSource = Join-Path $assetBootstrapRoot 'inventory_loader.efi'
$assetCache = Join-Path $assetBootstrapRoot 'cache\inventory_loader.efi'
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $assetCache))
$assetBytes = [Text.Encoding]::ASCII.GetBytes('local fixture asset bootstrap regression')
[IO.File]::WriteAllBytes($assetSource, $assetBytes)
$assetExpectedHash = Get-Sha256 $assetSource
Install-VerifiedReadOnlyAsset -SourcePath $assetSource -DestinationPath $assetCache -ExpectedSha256 $assetExpectedHash
Assert-True ((Test-Path -LiteralPath $assetCache -PathType Leaf) -and ((Get-Sha256 $assetCache) -ceq $assetExpectedHash)) 'production bootstrap copies and verifies a local file path' $assetCache
Install-VerifiedReadOnlyAsset -SourcePath $assetSource -DestinationPath $assetCache -ExpectedSha256 $assetExpectedHash
Assert-True ((Get-Sha256 $assetCache) -ceq $assetExpectedHash) 'production bootstrap accepts an already verified cache' $assetCache
$legacyAssetPath = Join-Path $assetSource ''
Assert-True ($legacyAssetPath -cne $assetSource -and -not (Test-Path -LiteralPath $legacyAssetPath -PathType Leaf)) 'empty-child Join-Path regression reproduces file-to-directory path corruption' $legacyAssetPath
$legacyRejected = $false
try { Install-VerifiedReadOnlyAsset -SourcePath $legacyAssetPath -DestinationPath (Join-Path $assetBootstrapRoot 'legacy-cache.efi') -ExpectedSha256 $assetExpectedHash }
catch { $legacyRejected = $_.Exception.Message -like '*trailing separator*' }
Assert-True $legacyRejected 'production bootstrap rejects the legacy trailing-separator file path' $legacyAssetPath
$wrongHashRejected = $false
try { Install-VerifiedReadOnlyAsset -SourcePath $assetSource -DestinationPath (Join-Path $assetBootstrapRoot 'wrong-hash.efi') -ExpectedSha256 ('0' * 64) }
catch { $wrongHashRejected = $_.Exception.Message -like '*hash mismatch*' }
Assert-True $wrongHashRejected 'production bootstrap rejects source bytes that do not match the pinned hash' 'expected hash mismatch'

# Exercise production mountvol normalization and partition selection without
# mounting a disk. The mock partition deliberately has no DriveLetter; its exact
# mountvol volume GUID is present in AccessPaths, matching Windows ESP behavior.
Invoke-Expression $volumeGuidFunction[0].Extent.Text
Invoke-Expression $partitionFactsFunction[0].Extent.Text
$volumeGuid = '8be4df61-93ca-11d2-aa0d-00e098032b8c'
$volumeGuidPath = '\\?\Volume{' + $volumeGuid + '}\'
$normalizedVolumeGuid = Normalize-MountvolVolumeGuid -Lines @('', 'informational prefix', ('  ' + $volumeGuidPath + '  '))
Assert-True ($normalizedVolumeGuid -ceq $volumeGuidPath) 'mountvol output ignores blank/prefix text and normalizes the single GUID path' $normalizedVolumeGuid
$badMountvolOutputRejected = $false
try { [void](Normalize-MountvolVolumeGuid -Lines @('The system cannot find the file specified.')) }
catch { $badMountvolOutputRejected = $_.Exception.Message -like '*exactly one volume GUID*' }
Assert-True $badMountvolOutputRejected 'mountvol output without a volume GUID is rejected' 'zero valid GUID paths'
$ambiguousMountvolOutputRejected = $false
try { [void](Normalize-MountvolVolumeGuid -Lines @($volumeGuidPath, ('\\?\Volume{11111111-2222-3333-4444-555555555555}\'))) }
catch { $ambiguousMountvolOutputRejected = $_.Exception.Message -like '*exactly one volume GUID*' }
Assert-True $ambiguousMountvolOutputRejected 'mountvol output with multiple volume GUID paths is rejected' 'two valid GUID paths'
$malformedMountvolOutputRejected = $false
try { [void](Normalize-MountvolVolumeGuid -Lines @('\\?\Volume{8be4df61-93ca-11d2-aa0d-00e098032b8c}extra\')) }
catch { $malformedMountvolOutputRejected = $_.Exception.Message -like '*exactly one volume GUID*' }
Assert-True $malformedMountvolOutputRejected 'malformed mountvol device path is rejected' 'non-terminal path text'

$script:systemEspGuid = '{C12A7328-F81F-11D2-BA4B-00A0C93EC93B}'
$script:mockEspPartitions = @()
function Get-Partition {
    [CmdletBinding()]
    param()
    return @($script:mockEspPartitions)
}
function Get-CimInstance {
    [CmdletBinding()]
    param([string]$ClassName, [string]$Filter)
    if ($ClassName -cne 'Win32_DiskDrive' -or $Filter -cne 'Index=0') { throw "Unexpected CIM request: class=$ClassName filter=$Filter" }
    return @([pscustomobject]@{ BytesPerSector = 512 })
}
$validEspPartition = [pscustomobject]@{
    DriveLetter = $null
    AccessPaths = @($volumeGuidPath, 'Z:\')
    GptType = $script:systemEspGuid
    DiskNumber = 0
    PartitionNumber = 1
    Guid = '{33333333-4444-5555-6666-777777777777}'
    Offset = [uint64]1048576
    Size = [uint64]2097152
}
$script:mockEspPartitions = @($validEspPartition)
$partitionFacts = Get-PartitionFacts -EspRoot 'Z:\' -VolumeGuid $normalizedVolumeGuid
Assert-True ($null -eq $validEspPartition.DriveLetter -and $partitionFacts.diskNumber -eq 0 -and $partitionFacts.partitionNumber -eq 1 -and $partitionFacts.startLba -eq 2048 -and $partitionFacts.sizeLba -eq 4096) 'ESP lookup accepts a null DriveLetter and matches the mountvol GUID in AccessPaths' ($partitionFacts | ConvertTo-Json -Compress)

function Assert-PartitionLookupFailure($Rows, [string]$ExpectedText, [string]$Name) {
    $script:mockEspPartitions = @($Rows)
    $failedAsExpected = $false
    try { [void](Get-PartitionFacts -EspRoot 'Z:\' -VolumeGuid $normalizedVolumeGuid) }
    catch { $failedAsExpected = $_.Exception.Message -like $ExpectedText }
    Assert-True $failedAsExpected $Name $ExpectedText
}
Assert-PartitionLookupFailure @() '*matches=0*' 'ESP lookup rejects a missing mounted volume partition'
Assert-PartitionLookupFailure @($validEspPartition, $validEspPartition) '*matches=2*' 'ESP lookup rejects ambiguous mounted volume partitions'
$nonEspPartition = $validEspPartition.PSObject.Copy()
$nonEspPartition.GptType = '{EBD0A0A2-B9E5-4433-87C0-68B6B72699C7}'
Assert-PartitionLookupFailure @($nonEspPartition) '*partition type mismatch*' 'ESP lookup rejects a matching non-ESP GPT partition'

# Mirror the production mounted-preflight try/finally with mocked mount state to
# verify a lookup exception cannot leave the temporary ESP mount owned.
$script:mockEspMounted = $false
function Dismount-Esp { $script:mockEspMounted = $false }
$script:mockEspPartitions = @()
$lookupFailureObserved = $false
$script:mockEspMounted = $true
try {
    try { [void](Get-PartitionFacts -EspRoot 'Z:\' -VolumeGuid $normalizedVolumeGuid) }
    finally { Dismount-Esp }
} catch { $lookupFailureObserved = $true }
Assert-True ($lookupFailureObserved -and -not $script:mockEspMounted) 'mounted ESP lookup failure runs the dismount finally' 'lookup failed and mock mount cleared'
$stageTryWithLookup = @($stageFunction[0].FindAll({ param($node) $node -is [Management.Automation.Language.TryStatementAst] -and $node.Body.Extent.Text.Contains('Get-PartitionFacts') }, $true))
Assert-True (@($stageTryWithLookup | Where-Object { $_.Finally.Extent.Text.Contains('Dismount-Esp') }).Count -gt 0) 'production ESP preflight lookup is protected by dismount finally' 'Stage-And-Arm'

$outsideOutput = Join-Path $worktreeRoot 'spoof-handoff\artifacts\f6\rejected-render-output'
$priorErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $rejected = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $renderer -Action CaptureCorrectionOnly -OutputDirectory $outsideOutput 2>&1 | ForEach-Object { [string]$_ })
    $rejectedExitCode = [int]$LASTEXITCODE
} finally { $ErrorActionPreference = $priorErrorActionPreference }
Assert-True ($rejectedExitCode -ne 0 -and -not (Test-Path -LiteralPath $outsideOutput)) 'renderer rejects output outside runtime report root' ($rejected -join ' | ')

$reportRoot = Join-Path $worktreeRoot 'spoof-handoff\reports\f12-release\runtime'
[void][IO.Directory]::CreateDirectory($reportRoot)
$reportPath = Join-Path $reportRoot ('fixture-correction-render-tests.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + $runId + '.json')
$report = [ordered]@{
    schema = 'combined-vm-fixture-correction-render-tests.v1'
    status = 'PASS'
    testCount = $checks.Count
    vmBooted = $false
    firmwareChanged = $false
    changedHostState = $false
    sharedAssetSetId = @($sharedSetIds)[0]
    outputDirectory = $testOutput
    renderedActions = @($receipts.ToArray())
    checks = @($checks.ToArray())
}
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 20) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
[Array]::Clear($assetBytes, 0, $assetBytes.Length)
Remove-Item -LiteralPath $assetBootstrapRoot -Recurse -Force
[pscustomobject]@{ status = $report.status; testCount = $report.testCount; reportPath = $reportPath; sharedAssetSetId = $report.sharedAssetSetId; vmBooted = $false; firmwareChanged = $false; changedHostState = $false } | ConvertTo-Json -Depth 5
