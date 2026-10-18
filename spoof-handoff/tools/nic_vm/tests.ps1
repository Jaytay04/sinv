[CmdletBinding()]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion.Major -ne 5) { throw ('This regression test requires Windows PowerShell 5.x; actual=' + $PSVersionTable.PSVersion) }

$scriptRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$worktreeRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot '..\..\..'))
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot '..\..\..\..\..\..'))
$reportRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'spoof-handoff\reports\nic-vm-20261017\tests'))
$guestTemplatePath = Join-Path $scriptRoot 'guest.ps1'
$rendererPath = Join-Path $scriptRoot 'render-guest.ps1'
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
$fixtureRoot = Join-Path (Join-Path $reportRoot 'fixtures') ('journal.' + $runId)
$consolePath = Join-Path $reportRoot ('console.' + $runId + '.txt')
$resultPath = Join-Path $reportRoot ('result.' + $runId + '.json')

function Add-TestRecord([string]$Name, [string]$Status, [string]$Details) {
    $script:TestRecords.Add([ordered]@{ name = $Name; status = $Status; details = $Details })
    $script:ConsoleLines.Add(($Status + ': ' + $Name + ' - ' + $Details))
}

function Assert-True([bool]$Condition, [string]$Name, [string]$Details) {
    if (-not $Condition) {
        Add-TestRecord $Name 'FAIL' $Details
        throw ('Assertion failed: ' + $Name + '; ' + $Details)
    }
    Add-TestRecord $Name 'PASS' $Details
}

function Assert-Equal([string]$Expected, [string]$Actual, [string]$Name) {
    if ($Expected -cne $Actual) {
        $details = 'expected=[' + $Expected + '] actual=[' + $Actual + ']'
        Add-TestRecord $Name 'FAIL' $details
        throw ('Assertion failed: ' + $Name + '; ' + $details)
    }
    Add-TestRecord $Name 'PASS' ('value=[' + $Actual + ']')
}

function Get-ExceptionMessage([scriptblock]$Action) {
    try { & $Action; return $null }
    catch { return [string]$_.Exception.Message }
}

function Get-FunctionText([object]$Ast, [string]$Name) {
    $matches = @($Ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $Name }, $true))
    if ($matches.Count -ne 1) { throw ('Expected exactly one source function named ' + $Name + '; found=' + $matches.Count) }
    return [string]$matches[0].Extent.Text
}

$script:TestRecords = [Collections.Generic.List[object]]::new()
$script:ConsoleLines = [Collections.Generic.List[string]]::new()
$result = [ordered]@{
    schema = 'client-nic-vm-journal-regression.v1'
    runId = $runId
    status = 'FAIL'
    startedUtc = [DateTime]::UtcNow.ToString('o')
    powershellVersion = $PSVersionTable.PSVersion.ToString()
    hostOrVmPolicyChanged = $false
    guestScriptExecuted = $false
    fixtureRoot = $fixtureRoot
    tests = @()
    failure = $null
}

try {
    if (-not [IO.Directory]::Exists($reportRoot)) { [void][IO.Directory]::CreateDirectory($reportRoot) }
    $reportPrefix = $reportRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $fixtureRoot.StartsWith($reportPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Journal fixture resolved outside the repository report directory.' }
    if ([IO.Directory]::Exists($fixtureRoot)) { throw ('Unique journal fixture unexpectedly exists: ' + $fixtureRoot) }
    [void][IO.Directory]::CreateDirectory($fixtureRoot)

    $tokens = $null
    $parseErrors = $null
    $guestAst = [Management.Automation.Language.Parser]::ParseFile($guestTemplatePath, [ref]$tokens, [ref]$parseErrors)
    if (@($parseErrors).Count -gt 0) { throw ('Guest source AST parse failed: ' + (($parseErrors | ForEach-Object { $_.Message }) -join '; ')) }

    $functionNames = @('Write-Utf8', 'Write-Json', 'Write-Journal', 'Read-Journal', 'Get-NormalizedGuid', 'ConvertTo-MacKey', 'Get-PropertyValue', 'Get-Evaluations')
    $functionTexts = @{}
    foreach ($name in $functionNames) { $functionTexts[$name] = Get-FunctionText $guestAst $name }

    $sourceRootLiteral = "'C:\ProgramData\ClientNicVmTest'"
    $fixtureRootLiteral = "'" + $fixtureRoot.Replace("'", "''") + "'"
    foreach ($name in @('Write-Journal', 'Read-Journal')) {
        $sourceText = [string]$functionTexts[$name]
        if (-not $sourceText.Contains($sourceRootLiteral)) { throw ('Source function is missing its expected owned-root literal: ' + $name) }
        $functionTexts[$name] = $sourceText.Replace($sourceRootLiteral, $fixtureRootLiteral)
        if ($functionTexts[$name].Contains($sourceRootLiteral)) { throw ('Source function retained the live VM journal root: ' + $name) }
    }
    foreach ($name in $functionNames) { Invoke-Expression ([string]$functionTexts[$name]) }
    Add-TestRecord 'AST extraction and test-root isolation' 'PASS' 'Loaded the actual journal and readback evaluator function ASTs; only the journal owned-root strings were redirected to this unique fixture.'

    $vmUuid = 'D7C14D56-24D1-9894-8906-C0CE95A07790'
    $thumbprint = '0123456789ABCDEF0123456789ABCDEF01234567'
    $certificateHash = '0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF'
    $script:Record = [ordered]@{ host = [ordered]@{ uuid = $vmUuid } }
    $script:Config = [ordered]@{ publicCertificate = [ordered]@{ thumbprint = $thumbprint; sha256 = $certificateHash } }

    $journalPath = Join-Path $fixtureRoot 'journal.json'
    $seedJournal = [ordered]@{
        schema = 'client-nic-vm-journal.v1'
        vmUuid = $vmUuid
        certificateThumbprint = $thumbprint
        certificateSha256 = $certificateHash
        baselineDirectoryExisted = $false
        trustMembership = [ordered]@{ Root = $false; TrustedPublisher = $true }
        testSigning = [ordered]@{ exists = $true; value = 'no' }
        installState = 'not-started'
    }
    [IO.File]::WriteAllText($journalPath, (ConvertTo-Json -InputObject $seedJournal -Depth 8), [Text.UTF8Encoding]::new($false))

    $journal = Read-Journal
    Assert-True ($journal -is [System.Collections.IDictionary]) 'journal deserializes to mutable dictionary' ('actualType=' + $journal.GetType().FullName)
    Assert-True ($journal.trustMembership -is [Management.Automation.PSCustomObject]) 'nested trust baseline remains intact' ('Root=' + $journal.trustMembership.PSObject.Properties['Root'].Value + '; TrustedPublisher=' + $journal.trustMembership.PSObject.Properties['TrustedPublisher'].Value)

    $journal.installState = 'install-pending'
    $journal.installIntentUtc = '2026-10-17T22:30:00.0000000Z'
    $journal.package = [ordered]@{ infSha256 = 'A1'; sysSha256 = 'B2'; catSha256 = 'C3' }
    Write-Journal $journal
    $journal = Read-Journal
    $journal.installState = 'installed'
    $journal.installedUtc = '2026-10-17T22:31:00.0000000Z'
    Write-Journal $journal
    $journal = Read-Journal
    $journal.installState = 'remove-pending'
    $journal.removeIntentUtc = '2026-10-17T22:32:00.0000000Z'
    $journal.removalRebootRequired = $true
    $journal.infRemovalRebootRequired = $false
    Write-Journal $journal
    $journal = Read-Journal
    $journal.installState = 'removed'
    $journal.removedUtc = '2026-10-17T22:33:00.0000000Z'
    Write-Journal $journal
    $roundTrip = Read-Journal

    Assert-Equal '2026-10-17T22:30:00.0000000Z' ([string]$roundTrip.installIntentUtc) 'install intent timestamp survives journal replacement'
    Assert-Equal '2026-10-17T22:31:00.0000000Z' ([string]$roundTrip.installedUtc) 'installed timestamp survives journal replacement'
    Assert-Equal '2026-10-17T22:32:00.0000000Z' ([string]$roundTrip.removeIntentUtc) 'remove intent timestamp survives journal replacement'
    Assert-True ([bool]$roundTrip.removalRebootRequired) 'NetCfg reboot flag survives journal replacement' 'expected=true actual=true'
    Assert-True (-not [bool]$roundTrip.infRemovalRebootRequired) 'INF removal reboot flag survives journal replacement' 'expected=false actual=false'
    Assert-Equal '2026-10-17T22:33:00.0000000Z' ([string]$roundTrip.removedUtc) 'removed timestamp survives journal replacement'
    Assert-Equal 'removed' ([string]$roundTrip.installState) 'journal state survives repeated atomic replacement'
    Assert-Equal 'A1' ([string]$roundTrip.package.infSha256) 'nested package fields survive journal replacement'
    Assert-True ([bool]$roundTrip.trustMembership.PSObject.Properties['TrustedPublisher'].Value) 'nested trust membership survives journal replacement' 'expected=true actual=true'
    Assert-True (-not (Test-Path -LiteralPath ($journalPath + '.new'))) 'atomic replacement leaves no temporary journal' 'journal.json.new is absent'

    $savedBytes = [IO.File]::ReadAllBytes($journalPath)
    $missingPath = $journalPath + '.saved'
    [IO.File]::Move($journalPath, $missingPath)
    try {
        $missingMessage = Get-ExceptionMessage { Read-Journal }
        Assert-Equal 'Owned NIC VM journal is absent; restore cannot infer prior policy.' $missingMessage 'missing journal fails closed'
    }
    finally { [IO.File]::Move($missingPath, $journalPath) }

    [IO.File]::WriteAllText($journalPath, '{"truncated":', [Text.UTF8Encoding]::new($false))
    try {
        $corruptMessage = Get-ExceptionMessage { Read-Journal }
        Assert-True (-not [string]::IsNullOrWhiteSpace($corruptMessage)) 'corrupt journal fails closed' ('exactError=' + $corruptMessage)
    }
    finally { [IO.File]::WriteAllBytes($journalPath, $savedBytes) }

    $script:Record.host.uuid = 'A7C14D56-24D1-9894-8906-C0CE95A07791'
    try {
        $wrongUuidMessage = Get-ExceptionMessage { Read-Journal }
        Assert-Equal 'NIC VM journal schema or VM identity mismatch.' $wrongUuidMessage 'wrong VM UUID fails closed'
    }
    finally { $script:Record.host.uuid = $vmUuid }

    $originalThumbprint = [string]$script:Config.publicCertificate.thumbprint
    $script:Config.publicCertificate.thumbprint = 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF'
    try {
        $wrongPinMessage = Get-ExceptionMessage { Read-Journal }
        Assert-Equal 'NIC VM journal trust pin or owned-root baseline is invalid.' $wrongPinMessage 'wrong certificate pin fails closed'
    }
    finally { $script:Config.publicCertificate.thumbprint = $originalThumbprint }

    $capturePath = Join-Path (Join-Path $repositoryRoot 'spoof-handoff\reports\nic-vm-20261017\uefi-windows-probe-20261017-222319-1fbeae2c5e') 'evidence\kernel-metadata.json'
    $profileEvidencePath = Join-Path $repositoryRoot 'spoof-handoff\reports\nic-vm-20261017\guest-profile.json'
    if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf)) { throw ('Captured evaluator input is missing: ' + $capturePath) }
    if (-not (Test-Path -LiteralPath $profileEvidencePath -PathType Leaf)) { throw ('Captured adapter profile is missing: ' + $profileEvidencePath) }
    $captureJson = [IO.File]::ReadAllText($capturePath)
    $networkAddressesJsonArrays = [regex]::Matches($captureJson, '"NetworkAddresses"\s*:\s*\[').Count
    Assert-True ($networkAddressesJsonArrays -ge 4) 'captured metadata preserves MSFT NetworkAddresses JSON arrays' ('raw JSON array properties=' + $networkAddressesJsonArrays + '; at least four are expected for the captured adapters')
    $capture = $captureJson | ConvertFrom-Json
    $profileEvidence = [IO.File]::ReadAllText($profileEvidencePath) | ConvertFrom-Json
    if ($capture.phase -cne 'Readback' -or $null -eq $capture.nativeCapture.data -or $null -eq $capture.windowsReads) { throw 'Captured evaluator input does not contain the expected Readback native and Windows data.' }
    if ($profileEvidence.bindings.Count -ne 4) { throw ('Captured VM profile does not contain four adapter bindings; actual=' + $profileEvidence.bindings.Count) }

    $originalEvaluations = @(Get-Evaluations $capture.nativeCapture.data $capture.windowsReads $profileEvidence.bindings 'Original' 'LegacySix')
    Assert-Equal '4' ([string]$originalEvaluations.Count) 'captured original replay covers all four adapters'
    $replaySummaries = [Collections.Generic.List[object]]::new()
    foreach ($evaluation in $originalEvaluations) {
        $pnp = [string]$evaluation.pnpDeviceId
        Assert-True ([bool]$evaluation.allRequiredSourcesMatch) ('captured Original evaluator result ' + $pnp) ('all required sources matched expected=' + [string]$evaluation.expected)
        Assert-Equal '6' ([string]@($evaluation.checks).Count) ('captured Original source count ' + $pnp)
        foreach ($identityCheck in @($evaluation.identityChecks)) {
            Assert-True ([bool]$identityCheck.matchesExpected) ('captured Original identity ' + $pnp + ' / ' + [string]$identityCheck.source) ('expected=' + [string]$identityCheck.expected + '; actual=' + [string]$identityCheck.actual)
        }
        $sourceSummaries = [Collections.Generic.List[object]]::new()
        foreach ($check in @($evaluation.checks)) {
            $source = [string]$check['source']
            Assert-True ([bool]$check['matchesExpected']) ('captured Original source ' + $pnp + ' / ' + $source) ('expected=' + [string]$check['expected'] + '; actual=' + (ConvertTo-Json -InputObject $check['actual'] -Compress -Depth 4))
            Assert-Equal '1' ([string]$check['matchCount']) ('captured Original source value count ' + $pnp + ' / ' + $source)
            if ($source -ceq 'MSFT_NetAdapter.NetworkAddresses') {
                Assert-True ($null -ne $check['actual']) ('captured MSFT NetworkAddresses value ' + $pnp) ('PS5 replay projection actualType=' + $check['actual'].GetType().FullName + '; original JSON property was an array')
            }
            else {
                Assert-True ($check['actual'] -is [string]) ('captured scalar MAC source ' + $pnp + ' / ' + $source) ('actualType=' + $check['actual'].GetType().FullName)
            }
            $sourceSummaries.Add([ordered]@{ source = $source; expectedOriginal = $check['expected']; actual = $check['actual']; matchCount = $check['matchCount']; matchesExpected = $check['matchesExpected'] })
        }
        $replaySummaries.Add([ordered]@{ pnpDeviceId = $pnp; expectedOriginal = $evaluation.expected; originalAllRequiredSourcesMatch = $evaluation.allRequiredSourcesMatch; originalSources = @($sourceSummaries.ToArray()) })
    }

    $replacementEvaluations = @(Get-Evaluations $capture.nativeCapture.data $capture.windowsReads $profileEvidence.bindings 'Replacement' 'LegacySix')
    Assert-Equal '4' ([string]$replacementEvaluations.Count) 'captured replacement replay covers all four adapters'
    for ($index = 0; $index -lt $replacementEvaluations.Count; $index++) {
        $evaluation = $replacementEvaluations[$index]
        $pnp = [string]$evaluation.pnpDeviceId
        Assert-True (-not [bool]$evaluation.allRequiredSourcesMatch) ('captured Replacement is not reported present ' + $pnp) ('replacement expected=' + [string]$evaluation.expected)
        Assert-Equal '6' ([string]@($evaluation.checks).Count) ('captured Replacement source count ' + $pnp)
        for ($checkIndex = 0; $checkIndex -lt @($evaluation.checks).Count; $checkIndex++) {
            $check = $evaluation.checks[$checkIndex]
            Assert-True (-not [bool]$check['matchesExpected']) ('captured Replacement mismatch ' + $pnp + ' / ' + [string]$check['source']) ('replacementExpected=' + [string]$check['expected'] + '; capturedActual=' + (ConvertTo-Json -InputObject $check['actual'] -Compress -Depth 4))
            Assert-Equal '1' ([string]$check['matchCount']) ('captured Replacement observed value count ' + $pnp + ' / ' + [string]$check['source'])
            $replaySummaries[$index].replacementExpected = $evaluation.expected
            $replaySummaries[$index].replacementMismatches = $true
        }
    }

    $syntheticWindows = $capture.windowsReads
    $syntheticBinding = $profileEvidence.bindings[0]
    $syntheticMsftRows = @($syntheticWindows.msftNetAdapter.rows | Where-Object { [string]::Equals([string]$_.PNPDeviceID, [string]$syntheticBinding.pnpDeviceId, [StringComparison]::OrdinalIgnoreCase) })
    Assert-Equal '1' ([string]$syntheticMsftRows.Count) 'synthetic array case uses one captured adapter identity'
    $syntheticMsftRows[0].NetworkAddresses = [string[]]@([string]$syntheticBinding.original, [string]$syntheticBinding.original)
    $syntheticOriginal = @(Get-Evaluations $capture.nativeCapture.data $syntheticWindows @($syntheticBinding) 'Original' 'LegacySix')
    Assert-Equal '1' ([string]$syntheticOriginal.Count) 'synthetic MSFT array case produces one evaluator row'
    $syntheticMsftCheck = @($syntheticOriginal[0].checks | Where-Object { $_['source'] -ceq 'MSFT_NetAdapter.NetworkAddresses' })[0]
    Assert-True ($syntheticMsftCheck['actual'] -is [array]) 'synthetic MSFT NetworkAddresses reaches evaluator as array' ('actualType=' + $syntheticMsftCheck['actual'].GetType().FullName)
    Assert-Equal '2' ([string]$syntheticMsftCheck['matchCount']) 'synthetic MSFT array preserves both values in match count'
    Assert-True ([bool]$syntheticMsftCheck['matchesExpected']) 'synthetic MSFT array matches captured original value' 'pure evaluator input only; no live read asserted'
    $syntheticReplacement = @(Get-Evaluations $capture.nativeCapture.data $syntheticWindows @($syntheticBinding) 'Replacement' 'LegacySix')
    $syntheticMsftReplacementCheck = @($syntheticReplacement[0].checks | Where-Object { $_['source'] -ceq 'MSFT_NetAdapter.NetworkAddresses' })[0]
    Assert-True (-not [bool]$syntheticMsftReplacementCheck['matchesExpected']) 'synthetic MSFT array does not match replacement' 'replacement is distinct from every synthetic original entry'
    Assert-Equal '2' ([string]$syntheticMsftReplacementCheck['matchCount']) 'synthetic replacement retains observed array count'
    $syntheticMsftRows[0].NetworkAddresses = [string[]]@([string]$syntheticBinding.original, [string]$syntheticBinding.replacement)
    $mixedAddresses = @(Get-Evaluations $capture.nativeCapture.data $syntheticWindows @($syntheticBinding) 'Replacement' 'LegacySix')
    $mixedCheck = @($mixedAddresses[0].checks | Where-Object { $_['source'] -ceq 'MSFT_NetAdapter.NetworkAddresses' })[0]
    Assert-True (-not [bool]$mixedCheck['matchesExpected']) 'mixed original and replacement array fails closed' 'Returning the replacement alongside an original is not full replacement coverage.'
    $syntheticMsftRows[0].NetworkAddresses = [string[]]@([string]$syntheticBinding.original)

    # Historical captures contain six readers. Only this offline replay opts into
    # that historical set; live VM phases always require the current seven.
    $missingNewReader = @(Get-Evaluations $capture.nativeCapture.data $capture.windowsReads $profileEvidence.bindings 'Original')
    foreach ($evaluation in $missingNewReader) {
        Assert-True (-not $evaluation.allRequiredSourcesMatch) ('new reader missing fails closed ' + $evaluation.pnpDeviceId) 'Historical data cannot establish a GetAdaptersInfo observation.'
        Assert-Equal '7' ([string]@($evaluation.checks).Count) ('current reader count ' + $evaluation.pnpDeviceId)
    }
    $syntheticNative = ($capture.nativeCapture.data | ConvertTo-Json -Depth 30 | ConvertFrom-Json)
    $syntheticGai = [ordered]@{ status = 0; complete = $true; entries = @($profileEvidence.bindings | ForEach-Object {
        [ordered]@{ adapterName = $_.interfaceGuid; address = $_.original }
    }) }
    $syntheticGai = ($syntheticGai | ConvertTo-Json -Depth 8 | ConvertFrom-Json)
    $syntheticNative | Add-Member -NotePropertyName 'getAdaptersInfo' -NotePropertyValue $syntheticGai
    $sevenReaderUnits = @(Get-Evaluations $syntheticNative $capture.windowsReads $profileEvidence.bindings 'Original')
    foreach ($evaluation in $sevenReaderUnits) {
        Assert-True ([bool]$evaluation.allRequiredSourcesMatch) ('synthetic seventh reader accepted ' + $evaluation.pnpDeviceId) 'Synthetic evaluator input only; this is not live evidence.'
    }
    $syntheticGai.entries[0].address = $profileEvidence.bindings[0].replacement
    $wrongNewReader = @(Get-Evaluations $syntheticNative $capture.windowsReads $profileEvidence.bindings 'Original')
    Assert-True (-not $wrongNewReader[0].allRequiredSourcesMatch) 'seventh reader mismatch fails closed' 'Six matching readers cannot hide one mismatched independent reader.'

    $captureHash = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash
    $profileHash = (Get-FileHash -LiteralPath $profileEvidencePath -Algorithm SHA256).Hash
    $phaseFailure = @($capture.failures | Where-Object { $_.source -ceq 'phase' } | ForEach-Object { [string]$_.exception })
    $result.evaluatorReplay = [ordered]@{
        kind = 'offline replay of captured native and Windows records through the current source evaluator; not a live spoof or driver proof'
        liveProof = $false
        driverLoadedByTest = $false
        sourceMetadataPath = $capturePath
        sourceMetadataSha256 = $captureHash
        sourceProfilePath = $profileEvidencePath
        sourceProfileSha256 = $profileHash
        sourcePhase = $capture.phase
        sourceExpectedMode = $capture.expectedMode
        sourceReadOnly = $capture.readOnly
        sourceHostOrGuestSettingsChanged = $capture.hostOrGuestSettingsChanged
        sourcePhaseFailure = $phaseFailure
        originalAdapterResults = @($replaySummaries.ToArray())
        syntheticArrayUnit = [ordered]@{ kind = 'pure evaluator unit input; duplicated captured original value to exercise a multi-entry array; not live evidence'; expectedOriginal = $syntheticBinding.original; values = @($syntheticMsftCheck['actual']); matchCount = $syntheticMsftCheck['matchCount']; originalMatches = $syntheticMsftCheck['matchesExpected']; replacementMatches = $syntheticMsftReplacementCheck['matchesExpected'] }
    }

    $profilePath = Join-Path $fixtureRoot 'wrong-uuid-profile.json'
    $profile = [ordered]@{ synthetic = $false; machine = [ordered]@{ manufacturer = 'VMware, Inc.'; uuid = 'D7C14D56-24D1-9894-8906-C0CE95A07790' } }
    [IO.File]::WriteAllText($profilePath, (ConvertTo-Json -InputObject $profile -Depth 4), [Text.UTF8Encoding]::new($false))
    $nativePlaceholder = Join-Path $fixtureRoot 'reader-not-executed.exe'
    [IO.File]::WriteAllBytes($nativePlaceholder, [byte[]]::new(4096))
    $negativeOutput = Join-Path (Join-Path $repositoryRoot 'spoof-handoff\reports\nic-vm-20261017\guest-actions') ('test-wrong-uuid.' + $runId + '.ps1')
    $wrongUuid = 'A7C14D56-24D1-9894-8906-C0CE95A07791'
    $rendererError = Get-ExceptionMessage { & $rendererPath -Phase PreparePolicy -ProfileJson $profilePath -NativeExePath $nativePlaceholder -ExpectedVmUuid $wrongUuid -OutputPath $negativeOutput }
    Assert-Equal 'ExpectedVmUuid must exactly match the canonical UUID in the fresh profile.' $rendererError 'renderer rejects a mismatched VM UUID before generating guest action'
    Assert-True (-not (Test-Path -LiteralPath $negativeOutput)) 'negative renderer test writes no guest action' 'wrong UUID rejected before output creation; no guest process launched'

    $result.status = 'PASS'
    $result.failure = $null
}
catch {
    $result.status = 'FAIL'
    $result.failure = [ordered]@{ message = $_.Exception.Message; errorRecord = $_.ToString() }
    Add-TestRecord 'test runner' 'FAIL' $_.Exception.Message
}
finally {
    $result.completedUtc = [DateTime]::UtcNow.ToString('o')
    $result.tests = @($script:TestRecords.ToArray())
    $script:ConsoleLines.Insert(0, ('NIC VM journal regression: ' + $result.status + '; runId=' + $runId))
    $script:ConsoleLines.Add('Result JSON: ' + $resultPath)
    [IO.File]::WriteAllText($consolePath, ($script:ConsoleLines -join [Environment]::NewLine) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($resultPath, (ConvertTo-Json -InputObject $result -Depth 16), [Text.UTF8Encoding]::new($false))
}

Get-Content -LiteralPath $consolePath
if ($result.status -cne 'PASS') { exit 1 }
