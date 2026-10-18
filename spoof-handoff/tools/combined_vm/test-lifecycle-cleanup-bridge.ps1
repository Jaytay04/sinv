[CmdletBinding()]
param(
    [string]$ReportDirectory,
    [string]$ActualCaptureRunDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$testScriptDirectory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($PSCommandPath))
$testWorktreeRoot = [IO.Path]::GetFullPath((Join-Path $testScriptDirectory '..\..\..'))
$testRepositoryRoot = [IO.Path]::GetFullPath((Join-Path $testWorktreeRoot '..\..\..'))
if ([string]::IsNullOrWhiteSpace($ReportDirectory)) {
    $ReportDirectory = Join-Path $PSScriptRoot '..\..\reports\f12-release\runtime\lifecycle-tests'
}
if ([string]::IsNullOrWhiteSpace($ActualCaptureRunDirectory)) {
    $ActualCaptureRunDirectory = Join-Path $testWorktreeRoot 'spoof-handoff\reports\f6\runtime\mailbox\uefi-windows-probe-20261018-091609-11b2c6e014'
}
$ActualCaptureRunDirectory = [IO.Path]::GetFullPath($ActualCaptureRunDirectory)

function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Copy-JsonObject($Value) {
    return ($Value | ConvertTo-Json -Depth 32 -Compress | ConvertFrom-Json -ErrorAction Stop)
}

function Get-TestBootId([string]$Uuid, [string]$BootUtc) {
    $material = [Text.Encoding]::UTF8.GetBytes($Uuid + '|' + $BootUtc)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($material))).Replace('-', '') }
    finally { $sha.Dispose(); [Array]::Clear($material, 0, $material.Length) }
}

$templatePath = Join-Path $PSScriptRoot 'guest-lifecycle.template.ps1'
$rendererPath = Join-Path $PSScriptRoot 'render-lifecycle.ps1'
$verifierPath = Join-Path $PSScriptRoot 'assemble-gate.ps1'
$null = . $verifierPath -LibraryOnly
if ($null -eq (Get-Command Assert-CombinedAssemblyResponseHmac -CommandType Function -ErrorAction SilentlyContinue)) {
    throw 'The shared mailbox cryptographic verifier did not load in LibraryOnly mode.'
}
$testMailboxKey = [byte[]]::new(32)
for ($index = 0; $index -lt $testMailboxKey.Length; $index++) { $testMailboxKey[$index] = [byte]($index + 1) }
Set-CombinedAssemblyUnitTestMailboxKey -Key $testMailboxKey
$rendererTokens = $null
$rendererErrors = $null
$rendererAst = [Management.Automation.Language.Parser]::ParseFile($rendererPath, [ref]$rendererTokens, [ref]$rendererErrors)
if (@($rendererErrors).Count -gt 0) { throw ('Lifecycle renderer parse failed: ' + (($rendererErrors | ForEach-Object { $_.Message }) -join '; ')) }
$authNodes = @($rendererAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Assert-CombinedFixtureMailboxAuthentication' }, $true))
if ($authNodes.Count -ne 1) { throw "Expected one renderer authentication function; found $($authNodes.Count)." }
. ([scriptblock]::Create($authNodes[0].Extent.Text))

$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($templatePath, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -gt 0) { throw ('Lifecycle template parse failed: ' + (($parseErrors | ForEach-Object { $_.Message }) -join '; ')) }
$names = @('Get-BaselineJournal','Get-CleanupActionName','Assert-CorrectedCollectorCaptureFacts','Assert-CorrectedFixtureJournal')
$source = [Collections.Generic.List[string]]::new()
foreach ($name in $names) {
    $nodes = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name }, $true))
    if ($nodes.Count -ne 1) { throw "Expected one lifecycle function '$name'; found $($nodes.Count)." }
    $source.Add($nodes[0].Extent.Text)
}
. ([scriptblock]::Create(($source -join [Environment]::NewLine)))

$originalUuid = 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1'
$patchedUuid = 'A17F24D0-68E2-4A09-9C31-2B7D8450E6F9'
$bootUtc = '2026-10-18T06:50:00.0000000Z'
$buildId = 'B83C403CF85F4B14825A6A680AE152B8'
$replacements = [ordered]@{
    system_serial = '4091-37'
    system_vendor = 'NORTHLAB'
    system_product = 'NODE'
    board_serial = '08E4821_MD2F297027'
    board_vendor = 'Example Devices Corporation'
    board_product = 'Client Reference Board (CB-4200)'
    system_uuid = $patchedUuid
}
$script:Config = [pscustomobject]@{
    lifecycleId = 'combined-f12-lifecycle-test'
    vmIdentity = [pscustomobject]@{
        original = [pscustomobject]@{ uuid = $originalUuid }
        patched = [pscustomobject]@{ uuid = $patchedUuid }
    }
    fixturePins = [pscustomobject]@{ storageFixtureManifestSha256 = ('A' * 64); smbiosFixtureManifestSha256 = ('B' * 64); smbiosBuildIdentity = $buildId }
    smbios = [pscustomobject]@{ replacements = [pscustomobject]$replacements }
}

$patchValues = [pscustomobject]@{}
$readbackFields = [pscustomobject]@{}
foreach ($field in $replacements.Keys) {
    $patchValues | Add-Member -MemberType NoteProperty -Name ('SMBIOS:' + $field) -Value ([string]$replacements[$field])
    $readbackFields | Add-Member -MemberType NoteProperty -Name $field -Value ([pscustomobject]@{ native = [string]$replacements[$field]; wmi = [string]$replacements[$field]; nativeMatchesWmi = $true })
}
$capture = [pscustomobject]@{
    schema = 'combined-vm-fixture-correction-action.v1'
    internalAction = 'CaptureActive'
    mode = 'corrected-collector'
    status = 'PASS'
    pathBAction = 'Collect'
    cleanup = [pscustomobject]@{ attempted = $true; result = 'OWNED_ENTRIES_STAGE_AND_TRACKED_ESP_RESTORED'; pathBStageRemoved = $true; trackedEspBaselineRestored = $true; errors = @() }
    vm = [pscustomobject]@{ uuid = $patchedUuid }
    journal = [pscustomobject]@{ path = 'C:\ProgramData\CombinedVmFixture\f0114d56cf8a8a362772aae2fa2464d1\smbios2-ep-correction-f12-r2-release-20261018\corrected-collector.journal.json'; sha256 = ('C' * 64); state = 'CAPTURED_AND_CLEANED' }
    bootIdentity = [pscustomobject]@{ bootId = (Get-TestBootId $originalUuid $bootUtc); lastBootUpTimeUtc = $bootUtc }
    firmware = [pscustomobject]@{
        secureBoot = $false
        secureBootVariable = [pscustomobject]@{ win32Error = 0; size = 1; bytesHex = '00' }
        bootOrderBefore = [pscustomobject]@{ bytesBase64 = 'AQID'; attributes = 7 }
        bootOrderAfter = [pscustomobject]@{ bytesBase64 = 'AQID'; attributes = 7 }
        bootNextAfter = [pscustomobject]@{ supported = $false; win32Error = 203 }
    }
    efi = [pscustomobject]@{ phase = 'ready_to_chainload'; status = '0x0000000000000000'; statusSource = 'prelude.report.child_load_status'; statusPhase = 'LoadImage(loader.efi)'; startStatusObserved = $false }
    collectorReport = [pscustomobject]@{ status = 'PASS'; result = 'applied'; mask = 127; buildId = $buildId; efiStatus = '0x0000000000000000'; efiStatusSource = 'prelude.report.child_load_status'; efiStatusPhase = 'LoadImage(loader.efi)'; startStatusObserved = $false; values = $patchValues }
    windowsReadback = [pscustomobject]@{ expectedMode = 'replacement'; fields = $readbackFields }
    esp = [pscustomobject]@{ afterCapture = [pscustomobject]@{ bootManagerSha256 = ('D' * 64) } }
}

$checks = [Collections.Generic.List[object]]::new()

$signedResponse = [pscustomobject][ordered]@{
    SchemaVersion = 1
    RunId = 'uefi-windows-probe-20261018-070000-0123456789'
    Status = 'PASS'
    MessageBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('capture complete'))
    CompletedUtc = '2026-10-18T07:00:00.0000000Z'
    EvidenceSha256 = '0' * 64
    EvidenceSize = 0
    EvidenceChunkSize = 0
    EvidenceChunkCount = 0
    ResponseHmacSha256 = ''
}
$signedResponse.ResponseHmacSha256 = Get-CombinedAssemblyHmacSha256Hex -Key $testMailboxKey -Text (Get-CombinedAssemblyCanonicalMailboxResponse $signedResponse)
$signedReceipt = [pscustomobject]@{
    RunId = $signedResponse.RunId
    Status = $signedResponse.Status
    ResponseHmacSha256 = $signedResponse.ResponseHmacSha256
    EvidenceSha256 = $signedResponse.EvidenceSha256
}
$verifiedMailbox = Assert-CombinedFixtureMailboxAuthentication -Response $signedResponse -ResponseMetadata $signedReceipt -HmacKey $testMailboxKey
Assert-Test ($verifiedMailbox.verified -eq $true -and $verifiedMailbox.sessionId -ceq $signedResponse.RunId) 'Valid synthetic mailbox HMAC was not cryptographically verified.'
$checks.Add([ordered]@{ check = 'mailbox-hmac-recomputed-positive'; passed = $true; synthetic = $true })

$forgedResponse = Copy-JsonObject $signedResponse
$forgedReceipt = Copy-JsonObject $signedReceipt
$forgedResponse.ResponseHmacSha256 = 'A' * 64
$forgedReceipt.ResponseHmacSha256 = $forgedResponse.ResponseHmacSha256
$forgedRejected = $false
try { [void](Assert-CombinedFixtureMailboxAuthentication -Response $forgedResponse -ResponseMetadata $forgedReceipt -HmacKey $testMailboxKey) } catch { $forgedRejected = $true }
Assert-Test $forgedRejected 'A self-consistent forged response/receipt HMAC was accepted.'
$checks.Add([ordered]@{ check = 'matching-forged-mailbox-hmac-rejected'; passed = $true; synthetic = $true })

$mismatchedReceipt = Copy-JsonObject $signedReceipt
$mismatchedReceipt.RunId = 'uefi-windows-probe-20261018-070001-0123456789'
$receiptRejected = $false
try { [void](Assert-CombinedFixtureMailboxAuthentication -Response $signedResponse -ResponseMetadata $mismatchedReceipt -HmacKey $testMailboxKey) } catch { $receiptRejected = $true }
Assert-Test $receiptRejected 'Mailbox response was accepted with a receipt from a different session.'
$checks.Add([ordered]@{ check = 'mailbox-session-receipt-mismatch-rejected'; passed = $true; synthetic = $true })

Assert-Test (Assert-CorrectedCollectorCaptureFacts $capture) 'Valid CaptureActive facts were rejected.'
$checks.Add([ordered]@{ check = 'corrected-capture-positive'; passed = $true; fields = 7; synthetic = $true })

$captureMutations = @(
    [pscustomobject]@{ name='wrong-action'; apply={ param($x) $x.internalAction='CaptureCorrectionOnly' } },
    [pscustomobject]@{ name='wrong-mode'; apply={ param($x) $x.mode='correction-only' } },
    [pscustomobject]@{ name='failed-status'; apply={ param($x) $x.status='FAIL' } },
    [pscustomobject]@{ name='wrong-vm-identity'; apply={ param($x) $x.vm.uuid='00000000-0000-0000-0000-000000000000' } },
    [pscustomobject]@{ name='wrong-boot-token'; apply={ param($x) $x.bootIdentity.bootId=('0' * 64) } },
    [pscustomobject]@{ name='secureboot-enabled'; apply={ param($x) $x.firmware.secureBoot=$true } },
    [pscustomobject]@{ name='bootorder-drift'; apply={ param($x) $x.firmware.bootOrderAfter.bytesBase64='BAUG' } },
    [pscustomobject]@{ name='bootnext-remains'; apply={ param($x) $x.firmware.bootNextAfter.supported=$true } },
    [pscustomobject]@{ name='efi-load-failed'; apply={ param($x) $x.efi.status='0x8000000000000001' } },
    [pscustomobject]@{ name='unobserved-start-overstated'; apply={ param($x) $x.efi.startStatusObserved=$true } },
    [pscustomobject]@{ name='collector-mask-incomplete'; apply={ param($x) $x.collectorReport.mask=126 } },
    [pscustomobject]@{ name='collector-build-mismatch'; apply={ param($x) $x.collectorReport.buildId=('E' * 32) } },
    [pscustomobject]@{ name='replacement-value-mismatch'; apply={ param($x) $x.collectorReport.values.'SMBIOS:system_serial'='wrong' } },
    [pscustomobject]@{ name='native-readback-mismatch'; apply={ param($x) $x.windowsReadback.fields.system_serial.native='wrong' } },
    [pscustomobject]@{ name='wmi-readback-mismatch'; apply={ param($x) $x.windowsReadback.fields.system_serial.wmi='wrong' } },
    [pscustomobject]@{ name='cleanup-legacy-status-only'; apply={ param($x) $x.cleanup.result='OWNED_ENTRIES_REMOVED_BOOTORDER_UNCHANGED' } },
    [pscustomobject]@{ name='cleanup-pathb-stage-remains'; apply={ param($x) $x.cleanup.pathBStageRemoved=$false } },
    [pscustomobject]@{ name='cleanup-esp-baseline-not-restored'; apply={ param($x) $x.cleanup.trackedEspBaselineRestored=$false } },
    [pscustomobject]@{ name='cleanup-reports-errors'; apply={ param($x) $x.cleanup.errors=@('stage cleanup failed') } },
    [pscustomobject]@{ name='cleanup-not-completed'; apply={ param($x) $x.cleanup.result='PARTIAL' } }
)
foreach ($mutation in $captureMutations) {
    $candidate = Copy-JsonObject $capture
    & $mutation.apply $candidate
    $rejected = $false
    try { [void](Assert-CorrectedCollectorCaptureFacts $candidate) } catch { $rejected = $true }
    Assert-Test $rejected "CaptureActive validator accepted invalid case '$($mutation.name)'."
    $checks.Add([ordered]@{ check = 'corrected-capture-rejection'; case = $mutation.name; passed = $true; synthetic = $true })
}

$fixtureJournal = [pscustomobject]@{
    schema = 'combined-vm-fixture-correction-journal.v2'
    lifecycleId = 'smbios2-ep-correction-f12-r2-release-20261018'
    vmUuid = $originalUuid
    mode = 'corrected-collector'
    state = 'CAPTURED_AND_CLEANED'
    pendingMutation = $null
    pathBReceiptSha256 = ('E' * 64)
    completedMutations = @('capture_pathb_collect','capture_remove_custom_boot_entry')
    bootManagerSha256 = ('D' * 64)
    bootOrderBefore = 'AQID'
    bootOrderAttributes = 7
    modeFlag = 'corrected-collector'
    captureStartedUtc = '2026-10-18T06:51:00.0000000Z'
    cleanupCompletedUtc = '2026-10-18T06:52:00.0000000Z'
}
Assert-Test (Assert-CorrectedFixtureJournal -FixtureJournal $fixtureJournal -Capture $capture -ExpectedLifecycleId $fixtureJournal.lifecycleId -ExpectedUuid $originalUuid) 'Valid terminal fixture journal was rejected.'
$checks.Add([ordered]@{ check = 'fixture-journal-positive'; passed = $true; synthetic = $true })

$journalMutations = @(
    [pscustomobject]@{ name='wrong-lifecycle'; apply={ param($x) $x.lifecycleId='other' } },
    [pscustomobject]@{ name='wrong-vm-uuid'; apply={ param($x) $x.vmUuid='00000000-0000-0000-0000-000000000000' } },
    [pscustomobject]@{ name='wrong-mode'; apply={ param($x) $x.mode='correction-only' } },
    [pscustomobject]@{ name='not-cleaned'; apply={ param($x) $x.state='ARMED' } },
    [pscustomobject]@{ name='mutation-pending'; apply={ param($x) $x.pendingMutation='remove_boot_entry' } },
    [pscustomobject]@{ name='pathb-not-collected'; apply={ param($x) $x.completedMutations=@('capture_remove_custom_boot_entry') } },
    [pscustomobject]@{ name='owned-entry-not-removed'; apply={ param($x) $x.completedMutations=@('capture_pathb_collect') } },
    [pscustomobject]@{ name='bad-receipt-hash'; apply={ param($x) $x.pathBReceiptSha256='bad' } },
    [pscustomobject]@{ name='bootmanager-mismatch'; apply={ param($x) $x.bootManagerSha256=('F' * 64) } },
    [pscustomobject]@{ name='bootorder-mismatch'; apply={ param($x) $x.bootOrderBefore='BAUG' } },
    [pscustomobject]@{ name='capture-after-cleanup-time'; apply={ param($x) $x.captureStartedUtc='2026-10-18T06:53:00.0000000Z' } }
)
foreach ($mutation in $journalMutations) {
    $candidate = Copy-JsonObject $fixtureJournal
    & $mutation.apply $candidate
    $rejected = $false
    try { [void](Assert-CorrectedFixtureJournal -FixtureJournal $candidate -Capture $capture -ExpectedLifecycleId 'smbios2-ep-correction-f12-r2-release-20261018' -ExpectedUuid $originalUuid) } catch { $rejected = $true }
    Assert-Test $rejected "Fixture-journal validator accepted invalid case '$($mutation.name)'."
    $checks.Add([ordered]@{ check = 'fixture-journal-rejection'; case = $mutation.name; passed = $true; synthetic = $true })
}

# Validate the archived CaptureActive metadata with the production guest-side
# predicate before rendering a lifecycle action. This uses real captured facts,
# not the synthetic capture object above.
if ([string]::IsNullOrWhiteSpace($testWorktreeRoot)) { throw "Test harness worktree root is empty (script='$PSCommandPath', scriptDirectory='$testScriptDirectory')." }
$fullGateInputsPath = Join-Path $testWorktreeRoot 'spoof-handoff\reports\f7\parent-control\full-gate-inputs.json'
if (-not (Test-Path -LiteralPath $fullGateInputsPath -PathType Leaf)) { throw "Actual-capture test inputs are missing: $fullGateInputsPath" }
$fullGateInputs = Get-Content -LiteralPath $fullGateInputsPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
if ([string]$fullGateInputs.schema -cne 'combined-vm-existing-input-references.v1') { throw 'Actual-capture reference manifest schema is unexpected.' }
$references = $fullGateInputs.references
foreach ($name in @('fixtureMap','deviceInventory','storageFixture','smbiosFixture')) {
    $reference = $references.PSObject.Properties[$name].Value
    if ($null -eq $reference -or -not (Test-Path -LiteralPath ([string]$reference.path) -PathType Leaf)) { throw "Actual-capture reference '$name' is missing." }
    $referenceFile = Get-Item -LiteralPath ([string]$reference.path)
    if ($referenceFile.Length -ne [long]$reference.bytes -or (Get-FileHash -LiteralPath $referenceFile.FullName -Algorithm SHA256).Hash -cne ([string]$reference.sha256).ToUpperInvariant()) {
        throw "Actual-capture reference '$name' differs from its pinned path, size, or SHA-256."
    }
}
$fixtureRows = @(Import-Csv -LiteralPath ([string]$references.fixtureMap.path) -Delimiter "`t")
$fieldMembers = [ordered]@{
    system_serial = 'FakeSMBIOSSerial'
    system_vendor = 'FakeSMBIOSManufacturer'
    system_product = 'FakeSMBIOSProduct'
    board_serial = 'FakeMotherboardSerial'
    board_vendor = 'FakeMotherboardManufacturer'
    board_product = 'FakeMotherboardProduct'
    system_uuid = 'FakeSMBIOSUUID'
}
$requiredFieldMembers = @($fieldMembers.Values)
$fixtureByMember = @{}
foreach ($row in $fixtureRows) {
    if ($requiredFieldMembers -cnotcontains [string]$row.member) { continue }
    if ($fixtureByMember.ContainsKey([string]$row.member)) { throw "Actual fixture map has a duplicate member: $($row.member)" }
    $fixtureByMember[[string]$row.member] = $row
}
$actualSmbiosReplacements = [ordered]@{}
foreach ($field in $fieldMembers.Keys) {
    $member = [string]$fieldMembers[$field]
    if (-not $fixtureByMember.ContainsKey($member)) { throw "Actual fixture map lacks SMBIOS member '$member'." }
    $actualSmbiosReplacements[$field] = [string]$fixtureByMember[$member].replacement
}
$actualOriginalUuid = [string]$fixtureByMember.FakeSMBIOSUUID.original
$actualPatchedUuid = [string]$fixtureByMember.FakeSMBIOSUUID.replacement
$actualSmbiosManifest = Get-Content -LiteralPath ([string]$references.smbiosFixture.path) -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
$actualBuildId = [string]$actualSmbiosManifest.buildIdentity.value
if ($actualBuildId -cnotmatch '\A[A-F0-9]{32}\z') { throw 'Pinned SMBIOS manifest build identity is malformed.' }
$script:Config.vmIdentity.original.uuid = $actualOriginalUuid
$script:Config.vmIdentity.patched.uuid = $actualPatchedUuid
$script:Config.smbios.replacements = [pscustomobject]$actualSmbiosReplacements
$script:Config.fixturePins.smbiosBuildIdentity = $actualBuildId
$script:Config.fixturePins.storageFixtureManifestSha256 = [string]$references.storageFixture.sha256
$script:Config.fixturePins.smbiosFixtureManifestSha256 = [string]$references.smbiosFixture.sha256
$actualMetadataPath = Join-Path $ActualCaptureRunDirectory 'evidence\kernel-metadata.json'
if (-not (Test-Path -LiteralPath $actualMetadataPath -PathType Leaf)) { throw "Archived CaptureActive metadata is missing: $actualMetadataPath" }
$actualCapture = Get-Content -LiteralPath $actualMetadataPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
Assert-Test (Assert-CorrectedCollectorCaptureFacts $actualCapture) 'Production corrected-capture predicate rejected the actual archived CaptureActive payload.'
$actualCaptureMetadataHash = (Get-FileHash -LiteralPath $actualMetadataPath -Algorithm SHA256).Hash.ToUpperInvariant()
$checks.Add([ordered]@{ check = 'actual-capture-facts-positive'; passed = $true; synthetic = $false; metadataPath = $actualMetadataPath; metadataSha256 = $actualCaptureMetadataHash; runId = (Split-Path $ActualCaptureRunDirectory -Leaf); fields = 7; cleanupResult = [string]$actualCapture.cleanup.result })

# Exercise the renderer's lifecycle pin against the real archived CaptureActive
# payload and require the exact authenticated F12-R2 lifecycle identity.
$lifecycleOutputDirectory = Join-Path $testWorktreeRoot 'spoof-handoff\reports\f6\runtime\lifecycle'
$sharedAssetsDirectory = Join-Path $testWorktreeRoot 'spoof-handoff\artifacts\f6\shared-readonly'
$actualJournalPath = [string]$actualCapture.journal.path
$actualFixtureLifecycleId = $null
if ($actualJournalPath -match '\A(?i)C:\\ProgramData\\CombinedVmFixture\\[0-9a-f]{32}\\(smbios2-ep-correction-f[0-9]+(?:-r[0-9]+)?-release-20261018)\\corrected-collector\.journal\.json\z') {
    $actualFixtureLifecycleId = [string]$Matches[1]
}
if ([string]::IsNullOrWhiteSpace($actualFixtureLifecycleId)) { throw 'Actual CaptureActive journal path does not identify a recognized fixture lifecycle.' }
if ($actualFixtureLifecycleId -ceq 'smbios2-ep-correction-f12-r2-release-20261018') {
    $renderOutput = @(& $rendererPath `
        -Action VerifyCorrectedCollectorCleanup `
        -GuestInventoryPath ([string]$references.deviceInventory.path) `
        -StorageFixtureManifestPath ([string]$references.storageFixture.path) `
        -SmbiosFixtureManifestPath ([string]$references.smbiosFixture.path) `
        -SharedAssetsDirectory $sharedAssetsDirectory `
        -OutputDirectory $lifecycleOutputDirectory `
        -LifecycleId 'combined-f12-20261018' `
        -FixtureCaptureRunDirectory $ActualCaptureRunDirectory `
        -ReferenceRepo $testRepositoryRoot)
    $receiptLines = @($renderOutput | ForEach-Object { [string]$_ } | Where-Object { $_.StartsWith('RENDER RECEIPT: ', [StringComparison]::Ordinal) })
    Assert-Test ($receiptLines.Count -eq 1) 'Actual F12 CaptureActive render did not return exactly one render receipt path.'
    $renderReceiptPath = $receiptLines[0].Substring('RENDER RECEIPT: '.Length)
    if (-not (Test-Path -LiteralPath $renderReceiptPath -PathType Leaf)) { throw "Actual CaptureActive render receipt is missing: $renderReceiptPath" }
    $renderReceipt = Get-Content -LiteralPath $renderReceiptPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $renderReceiptHash = (Get-FileHash -LiteralPath $renderReceiptPath -Algorithm SHA256).Hash.ToUpperInvariant()
    Assert-Test ($renderReceipt.schema -ceq 'combined-vm-lifecycle-render.v1' -and $renderReceipt.action -ceq 'VerifyCorrectedCollectorCleanup' -and $renderReceipt.lifecycleId -ceq 'combined-f12-20261018' -and $renderReceipt.fixtureCapture.authentication.verified -eq $true -and $renderReceipt.fixtureCapture.runId -ceq (Split-Path $ActualCaptureRunDirectory -Leaf)) 'Actual F12 CaptureActive mailbox render receipt lacks authenticated session binding or the expected action identity.'
    $checks.Add([ordered]@{ check = 'actual-f12-capture-render-positive'; passed = $true; synthetic = $false; renderReceiptPath = $renderReceiptPath; renderReceiptSha256 = $renderReceiptHash; renderedScriptSha256 = [string]$renderReceipt.renderedScriptSha256; renderedConfigSha256 = [string]$renderReceipt.renderedConfigSha256; mailboxHmacVerified = $true; runId = [string]$renderReceipt.fixtureCapture.runId })
} else {
    $staleCaptureError = $null
    try {
        [void](& $rendererPath `
            -Action VerifyCorrectedCollectorCleanup `
            -GuestInventoryPath ([string]$references.deviceInventory.path) `
            -StorageFixtureManifestPath ([string]$references.storageFixture.path) `
            -SmbiosFixtureManifestPath ([string]$references.smbiosFixture.path) `
            -SharedAssetsDirectory $sharedAssetsDirectory `
            -OutputDirectory $lifecycleOutputDirectory `
            -LifecycleId 'combined-f12-20261018' `
            -FixtureCaptureRunDirectory $ActualCaptureRunDirectory `
            -ReferenceRepo $testRepositoryRoot)
    } catch { $staleCaptureError = [string]$_.Exception.Message }
    Assert-Test ($actualFixtureLifecycleId -ceq 'smbios2-ep-correction-f10-release-20261018' -and
        $staleCaptureError -match 'pinned fixture lifecycle') ('A non-F12-R2 archived CaptureActive run was not rejected as stale by the F12-R2 renderer; lifecycle=' + [string]$actualFixtureLifecycleId + '; error=' + [string]$staleCaptureError)
    $checks.Add([ordered]@{ check = 'actual-stale-capture-rejected'; passed = $true; synthetic = $false; actualFixtureLifecycleId = $actualFixtureLifecycleId; error = $staleCaptureError; runId = (Split-Path $ActualCaptureRunDirectory -Leaf) })
}

$script:Config.lifecycleId = 'combined-f12-lifecycle-test'
$script:Config.vmIdentity.original.uuid = $originalUuid
$script:Config.fixturePins.storageFixtureManifestSha256 = ('A' * 64)
$script:Config.fixturePins.smbiosFixtureManifestSha256 = ('B' * 64)
foreach ($case in @(
    [pscustomobject]@{ name='legacy-cleanup-branch'; actions=@([pscustomobject]@{ name='CleanupSmbiosInventory'; status='PASS' }); expected='CleanupSmbiosInventory' },
    [pscustomobject]@{ name='bridge-cleanup-branch'; actions=@([pscustomobject]@{ name='VerifyCorrectedCollectorCleanup'; status='PASS' }); expected='VerifyCorrectedCollectorCleanup' }
)) {
    $script:Journal = [pscustomobject]@{ schema='combined-vm-lifecycle-journal.v1'; lifecycleId=$script:Config.lifecycleId; vmUuid=$originalUuid; fixturePins=$script:Config.fixturePins; actions=$case.actions }
    Assert-Test ((Get-CleanupActionName) -ceq $case.expected) "Cleanup branch '$($case.name)' did not remain compatible."
    $checks.Add([ordered]@{ check = $case.name; passed = $true; synthetic = $true })
}
foreach ($actions in @(
    @([pscustomobject]@{ name='CleanupSmbiosInventory'; status='PASS' },[pscustomobject]@{ name='VerifyCorrectedCollectorCleanup'; status='PASS' }),
    @([pscustomobject]@{ name='VerifyCorrectedCollectorCleanup'; status='FAIL' })
)) {
    $script:Journal.actions = $actions
    $rejected = $false
    try { [void](Get-CleanupActionName) } catch { $rejected = $true }
    Assert-Test $rejected 'Ambiguous or failed cleanup branch was accepted.'
}
$checks.Add([ordered]@{ check = 'cleanup-branch-ambiguity-and-failure-rejected'; passed = $true; synthetic = $true })

$reportRoot = [IO.Path]::GetFullPath($ReportDirectory)
$worktreeSpoof = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $reportRoot.StartsWith(($worktreeSpoof + '\'), [StringComparison]::OrdinalIgnoreCase)) { throw 'Lifecycle test report directory must remain under the worktree spoof-handoff directory.' }
[void][IO.Directory]::CreateDirectory($reportRoot)
$report = [ordered]@{
    schema = 'combined-vm-lifecycle-cleanup-bridge-tests.v1'
    result = 'PASS'
    checks = @($checks.ToArray())
    templatePath = $templatePath
    templateSha256 = (Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash.ToUpperInvariant()
    testPath = $PSCommandPath
    testSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToUpperInvariant()
    runUtc = [DateTime]::UtcNow.ToString('o')
}
$reportPath = Join-Path $reportRoot ('cleanup-bridge.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N') + '.json')
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 16) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output "PASS: corrected-collector cleanup bridge validation and F12 lifecycle pin; checks=$($checks.Count)."
Write-Output "REPORT: $reportPath"
Write-Output "REPORT SHA256: $((Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToUpperInvariant())"
