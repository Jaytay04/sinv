[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedSourceSha256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$localProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
if ($localProjectRoot -match '[\\/]\.codex-build[\\/]worktrees[\\/][^\\/]+$') {
    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $localProjectRoot '..\..\..'))
}
else { $repositoryRoot = $localProjectRoot }
$sourcePath = Join-Path $repositoryRoot '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\nic_vm\host-address-roundtrip.ps1'
$gatePath = Join-Path $repositoryRoot 'spoof-handoff\reports\nic-reader-gap-20261017\vm-seven-reader-gate.json'
$lockedTsvPath = Join-Path $repositoryRoot 'spoof-handoff\reports\client-originals.tsv'
$outputRoot = Join-Path $repositoryRoot 'spoof-handoff\reports\nic-reader-gap-20261017\raw-gate-validation'
$runName = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N')
$runDirectory = Join-Path $outputRoot $runName
$fixtureDirectory = Join-Path $runDirectory 'fixtures'
$reportPath = Join-Path $runDirectory 'raw-input-gate-tests.json'

$testRows = New-Object 'System.Collections.Generic.List[object]'
$comparisons = New-Object 'System.Collections.Generic.List[object]'
$evidence = [ordered]@{}
$failure = $null

function Add-TestResult {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Status,
        [Parameter(Mandatory = $true)][string]$Detail,
        [AllowNull()][string]$FixturePath,
        [AllowNull()][string]$FixtureSha256
    )
    $testRows.Add([ordered]@{
        name = $Name
        status = $Status
        detail = $Detail
        fixturePath = $FixturePath
        fixtureSha256 = $FixtureSha256
    })
}

function Assert-TestCondition {
    param([Parameter(Mandatory = $true)][bool]$Condition, [Parameter(Mandatory = $true)][string]$Message)
    if (-not $Condition) { throw $Message }
}

function Get-TestSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Get-TestProperty {
    param([AllowNull()][object]$Object, [Parameter(Mandatory = $true)][string]$Name)
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Assert-EvaluationSummariesRemainGreen {
    param([Parameter(Mandatory = $true)][object]$CaptureData, [Parameter(Mandatory = $true)][string]$Label)
    $evaluations = @($CaptureData.evaluations)
    if ($evaluations.Count -ne 4) { throw ($Label + ': expected four unchanged evaluation summaries; actual=' + $evaluations.Count) }
    foreach ($evaluation in $evaluations) {
        if (-not [bool]$evaluation.allRequiredSourcesMatch) { throw ($Label + ': test fixture unexpectedly changed an evaluation summary for ' + [string]$evaluation.pnpDeviceId) }
        if (@($evaluation.checks).Count -ne 7) { throw ($Label + ': expected seven unchanged reader summaries for ' + [string]$evaluation.pnpDeviceId) }
        foreach ($check in @($evaluation.checks)) {
            if (-not [bool]$check.matchesExpected) { throw ($Label + ': test fixture unexpectedly changed evaluation result for ' + [string]$evaluation.pnpDeviceId + '/' + [string]$check.source) }
        }
    }
}

function Copy-TestJsonObject {
    param([Parameter(Mandatory = $true)][object]$InputObject)
    $json = ConvertTo-Json -InputObject $InputObject -Depth 100
    return (ConvertFrom-Json -InputObject $json -ErrorAction Stop)
}

function Get-TestTargetInterface {
    param([Parameter(Mandatory = $true)][object]$CaptureData)
    $interfaces = @($CaptureData.nativeCapture.data.setupDiNdisLanClass.interfaces)
    if ($interfaces.Count -ne 4) { throw ('Expected four SetupDi interfaces; actual=' + $interfaces.Count) }
    return $interfaces[0]
}

function Get-TestGuidKey {
    param([Parameter(Mandatory = $true)][object]$Value)
    return ([Guid]::Parse([string]$Value)).ToString('D').ToUpperInvariant()
}

function Find-TestRowIndexByGuid {
    param([Parameter(Mandatory = $true)][object[]]$Rows, [Parameter(Mandatory = $true)][string]$PropertyName, [Parameter(Mandatory = $true)][string]$Guid)
    $matches = New-Object 'System.Collections.Generic.List[int]'
    for ($index = 0; $index -lt $Rows.Count; $index++) {
        $value = Get-TestProperty $Rows[$index] $PropertyName
        if ($null -ne $value -and (Get-TestGuidKey $value) -ceq $Guid) { $matches.Add($index) }
    }
    if ($matches.Count -ne 1) { throw ('Expected exactly one synthetic target row for ' + $PropertyName + '/' + $Guid + '; actual=' + $matches.Count) }
    return $matches[0]
}

function Write-TestFixture {
    param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][object]$CaptureData)
    $path = Join-Path $fixtureDirectory ($Name + '.json')
    $json = ConvertTo-Json -InputObject $CaptureData -Depth 100
    [IO.File]::WriteAllText($path, $json, [Text.UTF8Encoding]::new($false))
    return [ordered]@{ path = $path; sha256 = Get-TestSha256 $path }
}

function Assert-RawFixtureRejected {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][object]$CaptureData,
        [Parameter(Mandatory = $true)][string]$ExpectedErrorText,
        [Parameter(Mandatory = $true)][object[]]$LockedBindings,
        [Parameter(Mandatory = $true)][string[]]$Readers
    )
    Assert-EvaluationSummariesRemainGreen $CaptureData $Name
    $fixture = Write-TestFixture $Name $CaptureData
    $captureRef = [ordered]@{ path = ('spoof-handoff/reports/nic-reader-gap-20261017/raw-gate-validation/' + $runName + '/fixtures/' + $Name + '.json') }
    $actualError = $null
    try {
        $null = Get-HostVmRawCaptureValues $captureRef 'filteredActive' 'Replacement' $LockedBindings $Readers $repositoryRoot
    }
    catch { $actualError = $_.Exception.Message }
    if ([string]::IsNullOrWhiteSpace($actualError)) { throw ('Negative raw-input test unexpectedly passed: ' + $Name) }
    if ($actualError.IndexOf($ExpectedErrorText, [StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw ('Negative raw-input test failed for an unexpected reason: ' + $Name + ' expectedErrorContains=' + $ExpectedErrorText + ' actual=' + $actualError)
    }
    Add-TestResult $Name 'PASS' ('Rejected malformed raw input while all evaluation booleans remained true; ' + $actualError) $fixture.path $fixture.sha256
}

try {
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw ('Host source is missing: ' + $sourcePath) }
    if (-not (Test-Path -LiteralPath $gatePath -PathType Leaf)) { throw ('Sealed VM gate is missing: ' + $gatePath) }
    if (-not (Test-Path -LiteralPath $lockedTsvPath -PathType Leaf)) { throw ('Locked value map is missing: ' + $lockedTsvPath) }
    $sourceSha256 = Get-TestSha256 $sourcePath
    if ($sourceSha256 -cne $ExpectedSourceSha256.ToUpperInvariant()) {
        throw ('Host source changed after freeze: expected=' + $ExpectedSourceSha256.ToUpperInvariant() + ' actual=' + $sourceSha256)
    }
    if (Test-Path -LiteralPath $runDirectory) { throw ('Unique raw-gate report directory already exists: ' + $runDirectory) }
    New-Item -ItemType Directory -Path $fixtureDirectory -Force:$false -ErrorAction Stop | Out-Null

    $sourceTokens = $null
    $parseErrors = $null
    $sourceAst = [Management.Automation.Language.Parser]::ParseFile($sourcePath, [ref]$sourceTokens, [ref]$parseErrors)
    if (@($parseErrors).Count -gt 0) { throw ('Frozen host source has PowerShell parse errors: ' + (@($parseErrors | ForEach-Object { $_.Message }) -join '; ')) }

    $nativeHashAssignments = @($sourceAst.FindAll({
        param($node)
        ($node -is [Management.Automation.Language.AssignmentStatementAst]) -and
        ($node.Left -is [Management.Automation.Language.VariableExpressionAst]) -and
        ($node.Left.VariablePath.UserPath -ceq 'script:ExpectedNativeExeSha256')
    }, $true))
    if ($nativeHashAssignments.Count -ne 1 -or
        $nativeHashAssignments[0].Right -isnot [Management.Automation.Language.CommandExpressionAst] -or
        $nativeHashAssignments[0].Right.Expression -isnot [Management.Automation.Language.StringConstantExpressionAst]) {
        throw 'The pinned native reader hash is not one literal source assignment.'
    }
    $script:ExpectedNativeExeSha256 = [string]$nativeHashAssignments[0].Right.Expression.SafeGetValue()

    $pureFunctionNames = @(
        'Get-HostProperty',
        'ConvertTo-HostMacKey',
        'ConvertTo-HostVmGuidKey',
        'Get-HostVmRows',
        'Get-HostVmUniqueGuidRow',
        'Get-HostVmUniquePnpRow',
        'Get-HostVmRowAddress',
        'ConvertFrom-HostVmRawCaptureData',
        'Get-HostVmRawCaptureValues'
    )
    $functionAsts = @{}
    foreach ($name in $pureFunctionNames) {
        $found = @($sourceAst.FindAll({ param($node) ($node -is [Management.Automation.Language.FunctionDefinitionAst]) -and $node.Name -ceq $name }, $true))
        if ($found.Count -ne 1) { throw ('Expected one AST definition for pure host helper ' + $name + '; actual=' + $found.Count) }
        $functionAsts[$name] = $found[0]
    }
    $allowedCommands = @('Get-Content', 'ConvertFrom-Json', 'ConvertTo-Json', 'Join-Path', 'Sort-Object')
    foreach ($name in $pureFunctionNames) {
        $commands = @($functionAsts[$name].FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] }, $true))
        foreach ($command in $commands) {
            $commandName = $command.GetCommandName()
            if ($null -eq $commandName) { throw ('A dynamic command exists in AST-extracted pure helper ' + $name + ': ' + $command.Extent.Text) }
            if ($commandName -notlike 'Get-Host*' -and $commandName -notlike 'ConvertTo-Host*' -and $commandName -notlike 'ConvertFrom-Host*' -and $allowedCommands -cnotcontains $commandName) {
                throw ('Unexpected command in AST-extracted pure helper ' + $name + ': ' + $commandName)
            }
        }
    }
    foreach ($name in $pureFunctionNames) { . ([scriptblock]::Create($functionAsts[$name].Extent.Text)) }

    $gateSha256 = Get-TestSha256 $gatePath
    $gate = Get-Content -LiteralPath $gatePath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $lockedRows = @(Import-Csv -LiteralPath $lockedTsvPath -Delimiter "`t" -ErrorAction Stop | Where-Object { $_.member -ceq 'FakeMacAddress' })
    if ($lockedRows.Count -ne 4) { throw ('Locked TSV must contain exactly four MAC rows; actual=' + $lockedRows.Count) }
    $lockedBindings = @($lockedRows | ForEach-Object {
        $separator = ([string]$_.device).IndexOf('|')
        if ($separator -lt 0) { throw ('Locked TSV row lacks a device identity separator: ' + [string]$_.device) }
        [ordered]@{ pnpDeviceId = ([string]$_.device).Substring($separator + 1); original = [string]$_.original; replacement = [string]$_.replacement }
    })
    $readers = @($gate.readers | ForEach-Object { [string]$_ })
    if ($readers.Count -ne 7 -or @($readers | Sort-Object -Unique).Count -ne 7) { throw ('Sealed gate must name seven unique readers; actual=' + $readers.Count) }
    $evidence.sourcePath = $sourcePath
    $evidence.sourceSha256 = $sourceSha256
    $evidence.gatePath = $gatePath
    $evidence.gateSha256 = $gateSha256
    $evidence.lockedTsvPath = $lockedTsvPath
    $evidence.lockedTsvSha256 = Get-TestSha256 $lockedTsvPath
    $evidence.captures = [ordered]@{}
    $evidence.inputComparisons = $comparisons

    $proofIndex = @{}
    foreach ($proof in @($gate.proofs)) {
        $key = ([string]$proof.pnpDeviceId).ToUpperInvariant() + '|' + [string]$proof.reader
        if ($proofIndex.ContainsKey($key)) { throw ('Duplicate sealed gate proof identity: ' + $key) }
        $proofIndex[$key] = $proof
    }
    $phaseModes = [ordered]@{ original = 'Original'; filteredActive = 'Replacement'; unfilteredActive = 'Replacement'; restored = 'Original' }
    foreach ($phase in @('original', 'filteredActive', 'unfilteredActive', 'restored')) {
        $captureMeta = $gate.captures.PSObject.Properties[$phase].Value
        $capturePath = Join-Path $repositoryRoot ([string]$captureMeta.path).Replace('/', '\')
        $captureHash = Get-TestSha256 $capturePath
        if ($captureHash -cne ([string]$captureMeta.sha256).ToUpperInvariant()) { throw ('Sealed capture hash mismatch: phase=' + $phase + ' expected=' + [string]$captureMeta.sha256 + ' actual=' + $captureHash) }
        $data = Get-Content -LiteralPath $capturePath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        Assert-EvaluationSummariesRemainGreen $data ($phase + ' source capture')
        if ([string]$data.expectedMode -cne $phaseModes[$phase]) { throw ('Capture expectedMode mismatch: phase=' + $phase + ' expected=' + $phaseModes[$phase] + ' actual=' + [string]$data.expectedMode) }
        $raw = ConvertFrom-HostVmRawCaptureData $data $phase $phaseModes[$phase] $lockedBindings $readers
        if ($raw.Count -ne 4) { throw ('Raw input-derived adapter count mismatch: phase=' + $phase + ' actual=' + $raw.Count) }
        $evidence.captures[$phase] = [ordered]@{ path = $capturePath; sha256 = $captureHash }

        foreach ($rawKey in @($raw.Keys)) {
            $entry = $raw[$rawKey]
            $gatePnp = ([string]$entry.pnpDeviceId).ToUpperInvariant()
            foreach ($reader in $readers) {
                $proofKey = $gatePnp + '|' + $reader
                if (-not $proofIndex.ContainsKey($proofKey)) { throw ('Sealed proof matrix is missing raw-derived cell: phase=' + $phase + ' key=' + $proofKey) }
                $proof = $proofIndex[$proofKey]
                $gateValue = switch ($phase) {
                    'original' { [string]$proof.original }
                    'filteredActive' { [string]$proof.activeObserved }
                    'unfilteredActive' { [string]$proof.unfilteredObserved }
                    'restored' { [string]$proof.restored }
                }
                $rawValue = [string]$entry.readers[$reader]
                $binding = @($lockedBindings | Where-Object { (ConvertTo-HostMacKey $_.original) -ceq (ConvertTo-HostMacKey $entry.original) })
                if ($binding.Count -ne 1) { throw ('Raw guest adapter did not map uniquely to locked TSV values: phase=' + $phase + ' pnp=' + $entry.pnpDeviceId) }
                $lockedValue = if ($phaseModes[$phase] -ceq 'Original') { [string]$binding[0].original } else { [string]$binding[0].replacement }
                $gateKey = ConvertTo-HostMacKey $gateValue
                $rawCanonical = ConvertTo-HostMacKey $rawValue
                $lockedKey = ConvertTo-HostMacKey $lockedValue
                $matched = ($rawCanonical -ceq $gateKey) -and ($rawCanonical -ceq $lockedKey)
                $comparisons.Add([ordered]@{
                    phase = $phase
                    pnpDeviceId = [string]$entry.pnpDeviceId
                    reader = $reader
                    expectedMode = [string]$phaseModes[$phase]
                    rawInputValue = $rawValue
                    sealedGateValue = $gateValue
                    lockedTsvValue = $lockedValue
                    matched = [bool]$matched
                })
                if (-not $matched) { throw ('Raw-input comparison failed: phase=' + $phase + ' pnp=' + $entry.pnpDeviceId + ' reader=' + $reader + ' locked=' + $lockedValue + ' raw=' + $rawValue + ' gate=' + $gateValue) }
            }
        }
    }
    if ($comparisons.Count -ne 112) { throw ('Expected 112 input-derived read comparisons; actual=' + $comparisons.Count) }
    Add-TestResult 'positive-raw-input-recompute' 'PASS' 'Recomputed all four adapters x seven readers across original, filtered-active, unfiltered-active, and restored captures; all 112 raw observations matched both locked TSV values and the sealed gate.' $null $null

    $filteredMeta = $gate.captures.filteredActive
    $filteredPath = Join-Path $repositoryRoot ([string]$filteredMeta.path).Replace('/', '\')
    $filteredBase = Get-Content -LiteralPath $filteredPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $targetInterface = Get-TestTargetInterface $filteredBase
    $targetGuid = Get-TestGuidKey $targetInterface.netCfgInstanceId

    $tampered = Copy-TestJsonObject $filteredBase
    $gaaRows = @($tampered.nativeCapture.data.getAdaptersAddresses.entries)
    $gaaIndex = Find-TestRowIndexByGuid $gaaRows 'adapterName' $targetGuid
    $tampered.nativeCapture.data.getAdaptersAddresses.entries[$gaaIndex].physicalAddress = '02:AA:BB:CC:DD:EE'
    Assert-RawFixtureRejected 'tampered-GetAdaptersAddresses' $tampered 'GetAdaptersAddresses' $lockedBindings $readers

    $missing = Copy-TestJsonObject $filteredBase
    $infoRows = @($missing.nativeCapture.data.getAdaptersInfo.entries)
    $infoIndex = Find-TestRowIndexByGuid $infoRows 'adapterName' $targetGuid
    $missing.nativeCapture.data.getAdaptersInfo.entries = @($infoRows | Where-Object { (Get-TestGuidKey $_.adapterName) -cne $targetGuid })
    Assert-RawFixtureRejected 'missing-GetAdaptersInfo-row' $missing 'matches=0' $lockedBindings $readers

    $duplicated = Copy-TestJsonObject $filteredBase
    $ifRows = @($duplicated.nativeCapture.data.getIfTable2.entries)
    $ifIndex = Find-TestRowIndexByGuid $ifRows 'interfaceGuid' $targetGuid
    $duplicated.nativeCapture.data.getIfTable2.entries = @($ifRows) + @((Copy-TestJsonObject $ifRows[$ifIndex]))
    Assert-RawFixtureRejected 'duplicate-GetIfTable2-row' $duplicated 'matches=2' $lockedBindings $readers

    $report = [ordered]@{
        schema = 'nic-vm-raw-input-host-gate-tests.v1'
        status = 'PASS'
        source = [ordered]@{ path = $sourcePath; sha256 = $sourceSha256; extraction = 'PowerShell AST function extents only; selected pure reader-verification helpers; main Run/Rollback entry points not invoked.' }
        testSource = [ordered]@{ path = $PSCommandPath; sha256 = Get-TestSha256 $PSCommandPath }
        gate = [ordered]@{ path = $gatePath; sha256 = $gateSha256 }
        lockedTsv = [ordered]@{ path = $lockedTsvPath; sha256 = $evidence.lockedTsvSha256 }
        runDirectory = $runDirectory
        summary = [ordered]@{ rawInputComparisons = $comparisons.Count; negativeCases = 3; passed = $testRows.Count; failed = 0; deviceOrHostActions = 0 }
        tests = $testRows.ToArray()
        captures = $evidence.captures
        inputComparisons = $comparisons.ToArray()
    }
}
catch {
    $failure = $_.Exception.ToString()
    Add-TestResult 'test-run' 'FAIL' $failure $null $null
    $report = [ordered]@{
        schema = 'nic-vm-raw-input-host-gate-tests.v1'
        status = 'FAIL'
        source = [ordered]@{ path = $sourcePath; sha256 = if (Test-Path -LiteralPath $sourcePath) { Get-TestSha256 $sourcePath } else { $null } }
        testSource = [ordered]@{ path = $PSCommandPath; sha256 = if (Test-Path -LiteralPath $PSCommandPath) { Get-TestSha256 $PSCommandPath } else { $null } }
        runDirectory = $runDirectory
        summary = [ordered]@{ rawInputComparisons = $comparisons.Count; negativeCases = 0; passed = 0; failed = 1; deviceOrHostActions = 0 }
        tests = $testRows.ToArray()
        failure = $failure
    }
}

if (Test-Path -LiteralPath $runDirectory -PathType Container) {
    $reportJson = ConvertTo-Json -InputObject $report -Depth 100
    [IO.File]::WriteAllText($reportPath, $reportJson, [Text.UTF8Encoding]::new($false))
    Write-Output $reportJson
    if ($report.status -cne 'PASS') { exit 1 }
}
else {
    Write-Error ('No report directory was created; failure=' + [string]$failure)
    exit 1
}
