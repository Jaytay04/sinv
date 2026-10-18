[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$templatePath = Join-Path $PSScriptRoot 'guest-fixture-correction.template.ps1'
$template = [IO.File]::ReadAllText($templatePath)
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseInput($template, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -ne 0) { throw 'Fixture journal template does not parse.' }

$functionAsts = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -in @('Get-FixtureJournalPath','Ensure-FixtureJournalProperties','Save-FixtureJournal','Read-FixtureJournal','Invoke-JournaledFixtureMutation') }, $false))
if (@($functionAsts | Where-Object { $_.Name -ceq 'Save-FixtureJournal' }).Count -ne 1 -or
    @($functionAsts | Where-Object { $_.Name -ceq 'Read-FixtureJournal' }).Count -ne 1 -or
    @($functionAsts | Where-Object { $_.Name -ceq 'Ensure-FixtureJournalProperties' }).Count -ne 1 -or
    @($functionAsts | Where-Object { $_.Name -ceq 'Invoke-JournaledFixtureMutation' }).Count -ne 1) {
    throw 'Could not isolate the production journal functions for pure failure-injection and transition tests.'
}
foreach ($functionAst in $functionAsts) { Invoke-Expression $functionAst.Extent.Text }
Import-Module (Join-Path $PSScriptRoot 'boot-option.psm1') -Force

$worktreeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$reportRoot = Join-Path $worktreeRoot 'spoof-handoff\reports\f6\runtime'
[void][IO.Directory]::CreateDirectory($reportRoot)
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $reportRoot ('journal-failure-tests.' + $testId)
[void][IO.Directory]::CreateDirectory($testRoot)
trap {
    if (Test-Path -LiteralPath $testRoot -PathType Container) { Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue }
    throw
}
$checks = [Collections.Generic.List[object]]::new()

function Assert-True([bool]$Condition, [string]$Name, $Actual = $null) {
    if (-not $Condition) { throw "$Name failed: actual=$Actual" }
    $checks.Add([ordered]@{ name = $Name; pass = $true; actual = $Actual })
}

function Set-TestJournal([string]$Directory, [string]$Operation) {
    $script:fixtureJournalPath = Join-Path $Directory 'fixture.journal.json'
    $script:fixtureJournal = [ordered]@{
        schema = 'combined-vm-fixture-correction-journal.v2'
        state = 'PREPARING'
        pendingMutation = $null
        completedMutations = @()
        operationUnderTest = $Operation
        updatedUtc = $null
    }
    Save-FixtureJournal
}

Assert-True ($template.Contains('captureStartedUtc = $null') -and $template.Contains('stageExitCode = $null') -and $template.Contains('armExitCode = $null')) 'new stage journal declares fields later written by capture' 'complete journal schema'
Assert-True (-not $template.Contains('$script:fixtureJournal.cleanedUtc')) 'capture uses the existing cleanupCompletedUtc field' 'single terminal timestamp field'

$requiredOperations = @(
    'pathb_stage',
    'pathb_arm',
    'create_correction_directory',
    'write_prelude',
    'write_mode_flag',
    'write_custom_boot_entry',
    'write_boot_next',
    'capture_remove_custom_boot_next',
    'capture_remove_custom_boot_entry',
    'recovery_remove_custom_boot_next',
    'recovery_remove_custom_boot_entry',
    'recovery_disarm_pathb',
    'recovery_remove_correction_directory',
    'recovery_remove_stage_directory'
)
$operationMatches = [regex]::Matches($template, "Invoke-JournaledFixtureMutation\s+'(?<name>[^']+)'\s+'[A-Z_]+'\s+\{")
$literalOperations = @($operationMatches | ForEach-Object { $_.Groups['name'].Value })
foreach ($operation in $requiredOperations) {
    Assert-True ($literalOperations -ccontains $operation) "production journals mutation $operation" $literalOperations
}
Assert-True ($template.Contains("'capture_pathb_disarm'") -and $template.Contains("'capture_pathb_collect'") -and $template.Contains('Invoke-JournaledFixtureMutation $operation')) 'capture journals its mode-specific Path-B action through the shared cleanup helper' 'capture_pathb_disarm/capture_pathb_collect'
Assert-True ($template.Contains('$operation = ''capture_remove_correction_'' + $item.Name') -and $template.Contains('Invoke-JournaledFixtureMutation $operation')) 'capture journals every exact correction-file deletion' 'dynamic file operation'
Assert-True ($template.Contains('$operation = ''recovery_remove_correction_'' + $item.Name') -and $template.Contains('Invoke-JournaledFixtureMutation $operation')) 'recovery journals every exact correction-file deletion' 'dynamic file operation'
Assert-True ($template.Contains('$operation = ''recovery_remove_stage_'' + $item.Name') -and $template.Contains('Invoke-JournaledFixtureMutation $operation')) 'recovery journals every exact Path-B stage-file deletion' 'dynamic file operation'
Assert-True ($template.Contains("Invoke-JournaledFixtureMutation ('recovery_remove_' + `$rootFile.name)")) 'recovery journals each exact collector report deletion' 'dynamic root-file operation'

$operationCases = [Collections.Generic.List[string]]::new()
foreach ($operation in $requiredOperations) { $operationCases.Add($operation) }
foreach ($name in @('smbios2-ep-prelude.efi','mode.txt','report.txt','eps2-before.bin','eps2-after.bin','eps3-before.bin','eps3-after.bin','table-before.bin','table-after.bin')) {
    $operationCases.Add('recovery_remove_correction_' + $name)
}
foreach ($name in @('smbios2-ep-prelude.efi','report.txt','eps2-before.bin','eps2-after.bin','eps3-before.bin','eps3-after.bin','table-before.bin','table-after.bin')) {
    $operationCases.Add('capture_remove_correction_' + $name)
}
foreach ($name in @('loader.efi','inventory.efi','manifest.sha256')) { $operationCases.Add('recovery_remove_stage_' + $name) }
foreach ($name in @('HypersinvEfiInventory.txt','HypersinvSmbiosPatch.txt')) { $operationCases.Add('recovery_remove_' + $name) }

$cases = [Collections.Generic.List[object]]::new()
foreach ($operation in $operationCases) { $cases.Add([pscustomobject]@{ operation = $operation; failurePoint = 'before-side-effect' }) }
foreach ($operation in $operationCases) { $cases.Add([pscustomobject]@{ operation = $operation; failurePoint = 'after-side-effect' }) }

$caseIndex = 0
foreach ($case in $cases) {
    $caseIndex++
    $caseRoot = Join-Path $testRoot ('case-' + $caseIndex.ToString('D3'))
    [void][IO.Directory]::CreateDirectory($caseRoot)
    Set-TestJournal $caseRoot $case.operation
    $markerPath = Join-Path $caseRoot 'side-effect.marker'
    $failureSeen = $false
    $failureMessage = ''
    try {
        Invoke-JournaledFixtureMutation $case.operation 'COMPLETE' {
            $before = Get-Content -LiteralPath $script:fixtureJournalPath -Raw | ConvertFrom-Json -ErrorAction Stop
            if ([string]$before.state -cne 'MUTATION_INTENT' -or [string]$before.pendingMutation -cne [string]$case.operation) {
                throw 'Mutation callback ran before its durable intent was visible.'
            }
            if ($case.failurePoint -ceq 'after-side-effect') { [IO.File]::WriteAllText($markerPath, 'side-effect-completed') }
            throw ('injected-' + $case.failurePoint)
        } | Out-Null
    } catch {
        $failureMessage = $_.Exception.Message
        $failureSeen = $failureMessage -match ('injected-' + [regex]::Escape([string]$case.failurePoint))
        if (-not $failureSeen -and $failureMessage -notmatch 'injected-') { throw }
    }
    $persisted = Get-Content -LiteralPath $script:fixtureJournalPath -Raw | ConvertFrom-Json -ErrorAction Stop
    Assert-True $failureSeen "$($case.operation) $($case.failurePoint) injected failure observed" $failureMessage
    Assert-True ([string]$persisted.state -ceq 'MUTATION_INTENT' -and [string]$persisted.pendingMutation -ceq [string]$case.operation) "$($case.operation) $($case.failurePoint) leaves durable recovery intent" $persisted
    Assert-True ((Test-Path -LiteralPath $markerPath -PathType Leaf) -eq ($case.failurePoint -ceq 'after-side-effect')) "$($case.operation) $($case.failurePoint) side-effect boundary" $markerPath
}

$successRoot = Join-Path $testRoot 'success'
[void][IO.Directory]::CreateDirectory($successRoot)
Set-TestJournal $successRoot 'successful-write'
$marker = Join-Path $successRoot 'side-effect.marker'
Invoke-JournaledFixtureMutation 'successful-write' 'COMPLETED' { [IO.File]::WriteAllText($marker, 'done') } | Out-Null
$successJournal = Get-Content -LiteralPath $script:fixtureJournalPath -Raw | ConvertFrom-Json -ErrorAction Stop
Assert-True ([string]$successJournal.state -ceq 'COMPLETED' -and $null -eq $successJournal.pendingMutation -and @($successJournal.completedMutations) -contains 'successful-write') 'successful mutation commits its completion checkpoint' $successJournal
Assert-True (@(Get-ChildItem -LiteralPath $successRoot -Force | Where-Object { $_.Name -match '\.(tmp|bak)$' }).Count -eq 0) 'journal update leaves no temporary or backup file' $successRoot

$transitionRoot = Join-Path $testRoot 'stage-capture-transition'
[void][IO.Directory]::CreateDirectory($transitionRoot)
$script:stateRoot = $transitionRoot
$script:preludeRelative = '\EFI\HypersinvInventory\fixture-correction\smbios2-ep-prelude.efi'
$script:config = [pscustomobject]@{
    lifecycleId = 'journal-transition-test'
    vmUuid = 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1'
    preludeSha256 = ('A' * 64)
}
$script:fixtureJournalPath = Get-FixtureJournalPath 'corrected-collector'
$script:fixtureJournal = [ordered]@{
    schema = 'combined-vm-fixture-correction-journal.v2'
    lifecycleId = [string]$script:config.lifecycleId
    vmUuid = [string]$script:config.vmUuid
    mode = 'corrected-collector'
    state = 'PREPARING'
    pendingMutation = $null
    completedMutations = @()
    startedUtc = [DateTime]::UtcNow.ToString('o')
    cleanupCompletedUtc = $null
    partition = @{}
    espVolumeGuid = 'volume-guid-test'
    bootManagerSha256 = ('B' * 64)
    espBefore = @{}
    bootOrderBefore = 'AQI='
    bootOrderAttributes = [uint32]7
    bootNextBefore = $null
    bootNextAttributes = $null
    standardBootNumber = $null
    standardBootName = $null
    standardBootBytes = $null
    standardBootAttributes = $null
    customBootNumber = $null
    customBootName = $null
    customBootBytes = $null
    customBootAttributes = $null
    customPreludeRelative = $script:preludeRelative
    preludeSha256 = [string]$script:config.preludeSha256
    modeFlag = 'corrected-collector'
    stageExpected = @{}
    pathBReceiptPath = 'unused-receipt.json'
    pathBReceiptSha256 = $null
    pathBArmIntent = $false
}
Save-FixtureJournal
$preparing = Read-FixtureJournal 'corrected-collector'
Assert-True ([string]$preparing.state -ceq 'PREPARING' -and [bool]$preparing.PSObject.Properties['captureStartedUtc'] -and $null -eq $preparing.captureStartedUtc) 'production stage journal saves and reloads before firmware writes' $preparing

$script:fixtureJournal = $preparing
$script:fixtureJournal.state = 'ARMED'
$script:fixtureJournal.standardBootNumber = 1
$script:fixtureJournal.standardBootName = 'Boot0001'
$script:fixtureJournal.standardBootBytes = 'AQID'
$script:fixtureJournal.standardBootAttributes = [uint32]7
$script:fixtureJournal.customBootNumber = 65535
$script:fixtureJournal.customBootName = 'BootFFFF'
$script:fixtureJournal.customBootBytes = 'BAUG'
$script:fixtureJournal.customBootAttributes = [uint32]7
$script:fixtureJournal.stageExitCode = 0
$script:fixtureJournal.armExitCode = 0
Save-FixtureJournal
$armed = Read-FixtureJournal 'corrected-collector'
Assert-True ([string]$armed.state -ceq 'ARMED' -and [int]$armed.customBootNumber -eq 65535) 'production stage journal saves and reloads the armed boot identity' $armed

# Recreate the R3 persisted shape: an armed v2 journal written before
# captureStartedUtc existed. Rehydration must add that field before StrictMode
# capture assignments run.
$legacyArmed = [ordered]@{}
foreach ($property in $armed.PSObject.Properties) {
    if ($property.Name -cne 'captureStartedUtc') { $legacyArmed[$property.Name] = $property.Value }
}
[IO.File]::WriteAllText($script:fixtureJournalPath, (($legacyArmed | ConvertTo-Json -Depth 24) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
$script:fixtureJournal = Read-FixtureJournal 'corrected-collector'
Assert-True ([bool]$script:fixtureJournal.PSObject.Properties['captureStartedUtc'] -and $null -eq $script:fixtureJournal.captureStartedUtc) 'R3 armed journal rehydrates the missing capture timestamp' $script:fixtureJournal
$script:fixtureJournal.state = 'CAPTURE_IN_PROGRESS'
$script:fixtureJournal.captureStartedUtc = [DateTime]::UtcNow.ToString('o')
Save-FixtureJournal
$capturing = Read-FixtureJournal 'corrected-collector'
Assert-True ([string]$capturing.state -ceq 'CAPTURE_IN_PROGRESS' -and -not [string]::IsNullOrWhiteSpace([string]$capturing.captureStartedUtc)) 'capture transition persists and reloads its start timestamp' $capturing
$script:fixtureJournal = $capturing
$script:fixtureJournal.pendingMutation = $null
$script:fixtureJournal.state = 'CAPTURED_AND_CLEANED'
$script:fixtureJournal.cleanupCompletedUtc = [DateTime]::UtcNow.ToString('o')
Save-FixtureJournal
$captured = Read-FixtureJournal 'corrected-collector'
Assert-True ([string]$captured.state -ceq 'CAPTURED_AND_CLEANED' -and -not [string]::IsNullOrWhiteSpace([string]$captured.cleanupCompletedUtc) -and $null -eq $captured.PSObject.Properties['cleanedUtc']) 'capture terminal transition uses the declared cleanup timestamp' $captured

$reportPath = Join-Path $reportRoot ('fixture-correction-journal-tests.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + $testId + '.json')
$report = [ordered]@{
    schema = 'combined-vm-fixture-correction-journal-tests.v1'
    status = 'PASS'
    checkCount = $checks.Count
    injectedBoundaries = $cases.Count
    vmBooted = $false
    firmwareChanged = $false
    hostStateChanged = $false
    checks = @($checks.ToArray())
}
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 12) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction Stop
[pscustomobject]@{ status = $report.status; checkCount = $report.checkCount; injectedBoundaries = $report.injectedBoundaries; reportPath = $reportPath; vmBooted = $false; firmwareChanged = $false; hostStateChanged = $false } | ConvertTo-Json -Depth 5
