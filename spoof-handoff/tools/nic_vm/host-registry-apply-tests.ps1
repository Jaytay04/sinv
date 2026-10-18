[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
if ($PSVersionTable.PSVersion.Major -ne 5) {
    throw ('This pure NIC registry apply regression requires Windows PowerShell 5.x; actual=' + $PSVersionTable.PSVersion)
}

$hostSourcePath = Join-Path $PSScriptRoot 'host-address-roundtrip.ps1'
$adapterSourcePath = Join-Path $PSScriptRoot 'adapter-address.ps1'
$hostTokens = $null
$hostParseErrors = $null
$hostAst = [Management.Automation.Language.Parser]::ParseFile($hostSourcePath, [ref]$hostTokens, [ref]$hostParseErrors)
if (@($hostParseErrors).Count -gt 0) { throw ('Host NIC source parse failed: ' + (($hostParseErrors | ForEach-Object { $_.Message }) -join '; ')) }
$adapterTokens = $null
$adapterParseErrors = $null
$adapterAst = [Management.Automation.Language.Parser]::ParseFile($adapterSourcePath, [ref]$adapterTokens, [ref]$adapterParseErrors)
if (@($adapterParseErrors).Count -gt 0) { throw ('Adapter source parse failed: ' + (($adapterParseErrors | ForEach-Object { $_.Message }) -join '; ')) }

function Get-FunctionAst {
    param([Parameter(Mandatory = $true)][object]$Ast, [Parameter(Mandatory = $true)][string]$Name)
    $functions = @($Ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $Name }, $true))
    if ($functions.Count -ne 1) { throw ('Expected exactly one function AST for ' + $Name + '; actual=' + $functions.Count) }
    return $functions[0]
}

$hostFunctions = @(
    'Get-HostProperty', 'ConvertTo-HostMacKey', 'ConvertTo-HostCanonicalMac', 'Get-HostSha256',
    'Assert-PathWithinRoot', 'ConvertTo-HostMutableData', 'ConvertTo-HostVmGuidKey',
    'Get-HostVmRows', 'Get-HostVmUniqueGuidRow', 'Get-HostVmUniquePnpRow', 'Get-HostVmRowAddress',
    'ConvertFrom-HostVmRawCaptureData', 'Invoke-HostAdapterRegistryApply', 'Assert-HostRegistryVmCapture',
    'Assert-HostVmnetAdapterDriverPin'
)
foreach ($name in $hostFunctions) { . ([scriptblock]::Create((Get-FunctionAst $hostAst $name).Extent.Text)) }
$adapterFunctions = @(
    'Get-NicAddressPropertyValue', 'ConvertTo-NicAddressGuid', 'ConvertTo-NicAddressMacKey',
    'ConvertTo-NicAddressCanonicalMac', 'Test-NicAddressUnicastMac',
    'New-NicAddressRegistryValueSnapshot', 'Test-NicAddressRegistryValueSnapshotEqual',
    'New-NicAddressIntent'
)
foreach ($name in $adapterFunctions) { . ([scriptblock]::Create((Get-FunctionAst $adapterAst $name).Extent.Text)) }
$script:ExpectedNativeExeSha256 = 'BF553F39E4E7024C75272C24215D995858627AEDD5E946A4B1289A8033DA1642'
$script:RequiredReaders = @(
    'OID_802_3_CURRENT_ADDRESS', 'GetAdaptersAddresses', 'GetAdaptersInfo', 'GetIfTable2',
    'Win32_NetworkAdapter.MACAddress', 'MSFT_NetAdapter.NetworkAddresses', 'Get-NetAdapter.MacAddress'
)

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..'))
$reportRoot = Join-Path $repoRoot 'spoof-handoff\reports\nic-vmnet-compatibility-20261018'
[void][IO.Directory]::CreateDirectory($reportRoot)
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
$reportPath = Join-Path $reportRoot ('host-registry-apply-tests.' + $runId + '.json')
$lockedMapPath = Join-Path $repoRoot 'spoof-handoff\reports\client-originals.tsv'
if (-not (Test-Path -LiteralPath $lockedMapPath -PathType Leaf)) { throw ('Locked NIC map is missing: ' + $lockedMapPath) }
$lockedRows = @(Import-Csv -LiteralPath $lockedMapPath -Delimiter "`t" -ErrorAction Stop | Where-Object { $_.member -ceq 'FakeMacAddress' })
if ($lockedRows.Count -ne 4) { throw ('Expected four locked NIC mapping rows; actual=' + $lockedRows.Count) }

$script:TestResults = New-Object 'System.Collections.Generic.List[object]'
$script:MockMode = 'success'
$script:MockRegistry = $null
$script:MockClassEntry = $null
$script:MockLive = $null
$script:MockEvents = New-Object 'System.Collections.Generic.List[string]'
$script:MockRegistryWrites = 0
$script:MockRestartCalls = 0
$script:MockJournalCalls = 0
$script:ExpectedRestartAdapter = $null
$script:ReplacementHex = $null
$script:FailureJournalCall = 0

function Get-HostExactAdapter {
    param([Parameter(Mandatory = $true)][object]$Snapshot)
    $script:MockEvents.Add('live-query')
    return $script:MockLive
}

function Get-NicAddressRegistryClassEntry {
    param([Parameter(Mandatory = $true)][string]$InterfaceGuid, [Parameter(Mandatory = $true)][string]$PnpDeviceId)
    $script:MockEvents.Add('class-query')
    return $script:MockClassEntry
}

function Get-NicAddressRegistryValueSnapshot {
    param([Parameter(Mandatory = $true)][string]$RegistrySubKey)
    $script:MockEvents.Add('registry-read')
    return $script:MockRegistry
}

function Set-NicAddressRegistryValueSnapshot {
    param([Parameter(Mandatory = $true)][string]$RegistrySubKey, [Parameter(Mandatory = $true)][object]$ValueSnapshot)
    $script:MockRegistryWrites++
    $script:MockEvents.Add('registry-write')
    if ([string](Get-NicAddressPropertyValue $ValueSnapshot 'kind') -cne 'String' -or
        [string](Get-NicAddressPropertyValue $ValueSnapshot 'rawValue') -cnotmatch '\A[0-9A-F]{12}\z') {
        throw 'Synthetic registry setter received an invalid REG_SZ MAC payload.'
    }
    if ($script:MockMode -ceq 'write-throws') { throw 'synthetic registry write failure' }
    if ($script:MockMode -cne 'silent-noop') { $script:MockRegistry = $ValueSnapshot }
}

function Get-Command {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Name)
    if ($Name -cne 'Restart-NetAdapter') { throw ('Unexpected command lookup in pure test: ' + $Name) }
    if ($script:MockMode -ceq 'restart-command-missing') { return [pscustomobject]@{ Name = $Name; Parameters = @{ Name = $true } } }
    return [pscustomobject]@{ Name = $Name; Parameters = @{ InputObject = $true } }
}

function Restart-NetAdapter {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param([Parameter(Mandatory = $true)][object]$InputObject)
    $script:MockRestartCalls++
    $script:MockEvents.Add('adapter-restart')
    if (-not [object]::ReferenceEquals($InputObject, $script:ExpectedRestartAdapter)) { throw 'Restart-NetAdapter did not receive the exact enumerated adapter object.' }
    if ($script:MockMode -ceq 'restart-throws') { throw 'synthetic adapter restart failure' }
    $address = if ($script:MockMode -ceq 'wrong-mac') { '02:AA:BB:CC:DD:EE' } else { $script:ReplacementHex }
    $script:MockLive.adapter.MacAddress = (($address -split '(..)' | Where-Object { $_ }) -join ':')
}

function Write-HostJournal {
    param([Parameter(Mandatory = $true)][object]$Journal)
    $script:MockJournalCalls++
    $script:MockEvents.Add(('journal-' + [string]$Journal.mutationStarted + '-' + [string]$Journal.progress.state))
    if ($script:FailureJournalCall -gt 0 -and $script:MockJournalCalls -eq $script:FailureJournalCall) {
        throw 'synthetic durable journal write failure'
    }
}

function New-ApplyFixture {
    param([Parameter(Mandatory = $true)][int]$Index)
    $row = $lockedRows[$Index]
    $device = [string]$row.device
    $separator = $device.IndexOf('|')
    if ($separator -lt 0) { throw ('Locked NIC row lacks PnP identity: ' + $device) }
    $pnp = $device.Substring($separator + 1)
    $guid = ('00000000-0000-4000-8000-{0:D12}' -f ($Index + 10)).ToUpperInvariant()
    $registryKeyName = '{0:D4}' -f ($Index + 1)
    $classPrefix = 'SYSTEM\CurrentControlSet\Control\Class\{4d36e972-e325-11ce-bfc1-08002be10318}\'
    $registrySubKey = $classPrefix + $registryKeyName
    $original = ConvertTo-HostCanonicalMac $row.original 'fixture original'
    $replacement = ConvertTo-HostCanonicalMac $row.replacement 'fixture replacement'
    $baseline = New-NicAddressRegistryValueSnapshot $true 'String' (ConvertTo-NicAddressMacKey $original)
    $adapter = [pscustomobject]@{
        InterfaceGuid = $guid
        MacAddress = $original
        Status = 'Up'
    }
    $msft = [pscustomobject]@{
        InterfaceGuid = $guid
        PnPDeviceID = $pnp
        MediaConnectState = 1
    }
    $script:MockLive = [ordered]@{ adapter = $adapter; msft = $msft }
    $script:ExpectedRestartAdapter = $adapter
    $script:ReplacementHex = ConvertTo-NicAddressMacKey $replacement
    $script:MockRegistry = $baseline
    $script:MockClassEntry = [ordered]@{
        registrySubKey = $registrySubKey
        registryKeyName = $registryKeyName
        registryPnpDeviceId = $pnp
        registryPnpValueName = 'PnpInstanceID'
        networkAddress = $baseline
    }
    $script:MockEvents = New-Object 'System.Collections.Generic.List[string]'
    $script:MockRegistryWrites = 0
    $script:MockRestartCalls = 0
    $script:MockJournalCalls = 0
    $script:FailureJournalCall = 0
    $script:MockMode = 'success'
    $snapshot = [ordered]@{
        pnpDeviceId = $pnp
        interfaceGuid = $guid
        original = $original
        replacement = $replacement
        registrySubKey = $registrySubKey
        registryKeyName = $registryKeyName
        registryPnpDeviceId = $pnp
        registryPnpValueName = 'PnpInstanceID'
        networkAddress = $baseline
    }
    $journal = [ordered]@{ state = 'APPLYING'; mutationStarted = $false; progress = $null }
    $progress = [ordered]@{ state = 'snapshotted' }
    $journal.progress = $progress
    return [ordered]@{ snapshot = $snapshot; journal = $journal; progress = $progress; original = $original; replacement = $replacement; pnp = $pnp; guid = $guid; baseline = $baseline }
}

function Add-TestResult {
    param([string]$Name, [bool]$Passed, [string]$Details)
    $script:TestResults.Add([ordered]@{ name = $Name; status = if ($Passed) { 'PASS' } else { 'FAIL' }; details = $Details })
    if (-not $Passed) { throw ('Assertion failed: ' + $Name + '; ' + $Details) }
}

function Assert-ThrowsExactContains {
    param([Parameter(Mandatory = $true)][scriptblock]$Body, [Parameter(Mandatory = $true)][string[]]$Fragments)
    try { & $Body }
    catch {
        foreach ($fragment in $Fragments) {
            if (-not $_.Exception.Message.Contains($fragment)) { throw ('Expected error fragment=' + $fragment + '; actual=' + $_.Exception.Message) }
        }
        return $_.Exception.Message
    }
    throw 'Expected the operation to fail, but it succeeded.'
}

function Invoke-ApplyFixture($Fixture) {
    $started = $false
    $result = Invoke-HostAdapterRegistryApply -Snapshot $Fixture.snapshot -Journal $Fixture.journal -Progress $Fixture.progress -MutationStarted ([ref]$started)
    return [ordered]@{ result = $result; started = $started }
}

$result = [ordered]@{
    schema = 'nic-host-registry-apply-tests.v1'
    runId = $runId
    status = 'FAIL'
    syntheticOnly = $true
    liveRegistryMutationExecuted = $false
    vmOrHostMutationExecuted = $false
    lockedValuesSource = 'spoof-handoff/reports/client-originals.tsv'
    tests = @()
}

try {
    for ($index = 0; $index -lt 4; $index++) {
        $fixture = New-ApplyFixture $index
        $invocation = Invoke-ApplyFixture $fixture
        $expectedHex = ConvertTo-NicAddressMacKey $fixture.replacement
        $registryWriteIndex = $script:MockEvents.IndexOf('registry-write')
        $journalMutationIndex = -1
        for ($eventIndex = 0; $eventIndex -lt $script:MockEvents.Count; $eventIndex++) {
            if ($script:MockEvents[$eventIndex] -match '^journal-True-apply-intent-recorded$') { $journalMutationIndex = $eventIndex; break }
        }
        Add-TestResult ('locked connected adapter apply ' + $index) (
            $invocation.result.matchesExpected -and $invocation.started -and $fixture.journal.mutationStarted -and
            $fixture.progress.registryIntent.value -ceq $expectedHex -and
            $script:MockRegistry.kind -ceq 'String' -and $script:MockRegistry.rawValue -ceq $expectedHex -and
            $script:MockLive.adapter.MacAddress -ceq $fixture.replacement -and
            $registryWriteIndex -gt $journalMutationIndex -and $script:MockRestartCalls -eq 1
        ) ('pnp=' + $fixture.pnp + '; guid=' + $fixture.guid + '; original=' + $fixture.original + '; expected=' + $fixture.replacement + '; actual=' + $invocation.result.actual + '; journalBeforeRegistry=' + ($registryWriteIndex -gt $journalMutationIndex))
    }

    $snapshotSource = (Get-FunctionAst $hostAst 'Get-HostAdapterSnapshots').Extent.Text
    $capturesRegistryPnpValueName = $snapshotSource -match '(?m)^\s*registryPnpValueName\s*=\s*\[string\]\$classEntry\.registryPnpValueName\s*$'
    Add-TestResult 'host snapshot retains exact registry PnP value-name identity' $capturesRegistryPnpValueName 'The source snapshot must preserve registryPnpValueName for the later apply identity comparison.'

    $fixture = New-ApplyFixture 0
    $fixture.snapshot.networkAddress = New-NicAddressRegistryValueSnapshot $true 'ExpandString' '001122334455'
    $beforeError = Assert-ThrowsExactContains { Invoke-ApplyFixture $fixture | Out-Null } @('NetworkAddress registry value changed before apply:', 'expected=', 'actual=')
    Add-TestResult 'typed registry baseline drift rejected before mutation' ($script:MockRegistryWrites -eq 0 -and $script:MockJournalCalls -eq 0 -and -not $fixture.journal.mutationStarted) $beforeError

    $fixture = New-ApplyFixture 0
    $script:MockLive.msft.PnPDeviceID = 'ROOT\VMWARE\UNEXPECTED'
    $beforeError = Assert-ThrowsExactContains { Invoke-ApplyFixture $fixture | Out-Null } @('Exact NIC identity changed before registry apply:', 'expectedPnp=', 'actualPnp=')
    Add-TestResult 'wrong live PnP identity rejected before mutation' ($script:MockRegistryWrites -eq 0 -and $script:MockJournalCalls -eq 0 -and -not $fixture.journal.mutationStarted) $beforeError

    $fixture = New-ApplyFixture 0
    $script:MockClassEntry.registrySubKey = $fixture.snapshot.registrySubKey.Substring(0, $fixture.snapshot.registrySubKey.Length - 4) + '9999'
    $beforeError = Assert-ThrowsExactContains { Invoke-ApplyFixture $fixture | Out-Null } @('NIC network-class identity changed before registry apply:', 'expectedSubKey=', 'actualSubKey=')
    Add-TestResult 'wrong network class key rejected before mutation' ($script:MockRegistryWrites -eq 0 -and $script:MockJournalCalls -eq 0 -and -not $fixture.journal.mutationStarted) $beforeError

    $fixture = New-ApplyFixture 0
    $script:MockLive.adapter.MacAddress = '02:AA:BB:CC:DD:EE'
    $beforeError = Assert-ThrowsExactContains { Invoke-ApplyFixture $fixture | Out-Null } @('NIC original MAC changed before registry apply:', 'expected=', 'actual=')
    Add-TestResult 'snapshot MAC drift rejected before mutation' ($script:MockRegistryWrites -eq 0 -and $script:MockJournalCalls -eq 0 -and -not $fixture.journal.mutationStarted) $beforeError

    $fixture = New-ApplyFixture 0
    $script:MockMode = 'write-throws'
    $writeError = Assert-ThrowsExactContains { Invoke-ApplyFixture $fixture | Out-Null } @('synthetic registry write failure')
    Add-TestResult 'write failure is journaled before attempted write' ($script:MockRegistryWrites -eq 1 -and $script:MockRestartCalls -eq 0 -and $fixture.journal.mutationStarted -and $fixture.progress.state -ceq 'apply-intent-recorded') $writeError

    $fixture = New-ApplyFixture 0
    $script:MockMode = 'restart-command-missing'
    $commandError = Assert-ThrowsExactContains { Invoke-ApplyFixture $fixture | Out-Null } @('Restart-NetAdapter lost the exact InputObject parameter')
    Add-TestResult 'restart capability checked before mutation' ($script:MockRegistryWrites -eq 0 -and $script:MockJournalCalls -eq 0 -and -not $fixture.journal.mutationStarted) $commandError

    $fixture = New-ApplyFixture 0
    $script:MockMode = 'silent-noop'
    $noopError = Assert-ThrowsExactContains { Invoke-ApplyFixture $fixture | Out-Null } @('NetworkAddress registry apply readback mismatch:', 'expected=', 'actual=')
    Add-TestResult 'silent registry no-op is detected with expected and actual' ($script:MockRegistryWrites -eq 1 -and $script:MockRestartCalls -eq 0 -and $fixture.journal.mutationStarted) $noopError

    $fixture = New-ApplyFixture 0
    $script:MockMode = 'restart-throws'
    $restartError = Assert-ThrowsExactContains { Invoke-ApplyFixture $fixture | Out-Null } @('synthetic adapter restart failure')
    Add-TestResult 'restart failure is surfaced after durable mutation marker' ($script:MockRegistryWrites -eq 1 -and $script:MockRestartCalls -eq 1 -and $fixture.journal.mutationStarted) $restartError

    $fixture = New-ApplyFixture 0
    $script:MockMode = 'wrong-mac'
    $wrongMacError = Assert-ThrowsExactContains { Invoke-ApplyFixture $fixture | Out-Null } @('NetworkAddress apply readback mismatch:', ('expected=' + $fixture.replacement), 'actual=02:AA:BB:CC:DD:EE')
    Add-TestResult 'wrong post-restart MAC reports exact expected and actual' ($script:MockRegistryWrites -eq 1 -and $script:MockRestartCalls -eq 1 -and $fixture.journal.mutationStarted) $wrongMacError

    $fixture = New-ApplyFixture 0
    $script:FailureJournalCall = 2
    $journalError = Assert-ThrowsExactContains { Invoke-ApplyFixture $fixture | Out-Null } @('synthetic durable journal write failure')
    Add-TestResult 'durable mutation journal failure prevents first registry write' ($script:MockRegistryWrites -eq 0 -and $script:MockRestartCalls -eq 0 -and $fixture.journal.mutationStarted) $journalError

    $twoTargetCapturePath = Join-Path $repoRoot 'spoof-handoff\reports\nic-vm-20261017\uefi-windows-probe-20261018-010423-f83cb4c682\evidence\kernel-metadata.json'
    if (-not (Test-Path -LiteralPath $twoTargetCapturePath -PathType Leaf)) { throw ('Sealed actual two-target VM capture is missing: ' + $twoTargetCapturePath) }
    $twoTargetHash = '48A767E7DBE7F0A16F4C6E3CA011ED9D4CF0DC74932595D66BB4C2C6FAE18599'
    $lockedBindings = @($lockedRows | ForEach-Object {
        $separator = ([string]$_.device).IndexOf('|')
        if ($separator -lt 0) { throw ('Locked map row lacks PnP identity: ' + [string]$_.device) }
        [ordered]@{
            pnpDeviceId = ([string]$_.device).Substring($separator + 1)
            original = [string]$_.original
            replacement = [string]$_.replacement
        }
    })
    $twoTargetError = Assert-ThrowsExactContains {
        Assert-HostRegistryVmCapture -CapturePath $twoTargetCapturePath -ExpectedCaptureSha256 $twoTargetHash -LockedBindings $lockedBindings -RepositoryRoot $repoRoot | Out-Null
    } @('VM registry capture schema is invalid: actual=vmnet-miniport-address-roundtrip.v1')
    Add-TestResult 'sealed prior-schema two-target VM evidence rejected by the four-target gate' $twoTargetError.Contains('vmnet-miniport-address-roundtrip.v1') ('captureSha256=' + $twoTargetHash + '; ' + $twoTargetError)

    $fourTargetBaselinePath = Join-Path $repoRoot 'spoof-handoff\reports\nic-vm-20261017\uefi-windows-probe-20261018-011108-3f0f96b372\evidence\kernel-metadata.json'
    if (-not (Test-Path -LiteralPath $fourTargetBaselinePath -PathType Leaf)) { throw ('Four-target read-only baseline capture is missing: ' + $fourTargetBaselinePath) }
    $fourTargetBaselineHash = '7BBC320D4F65281AD8F2541613A0D3C7B42BCF14EABDCEFA079DDF690D569D06'
    $fourTargetBaselineError = Assert-ThrowsExactContains {
        Assert-HostRegistryVmCapture -CapturePath $fourTargetBaselinePath -ExpectedCaptureSha256 $fourTargetBaselineHash -LockedBindings $lockedBindings -RepositoryRoot $repoRoot | Out-Null
    } @('VM registry capture schema is invalid: actual=vmnet-four-target-readonly-baseline.v1')
    Add-TestResult 'sealed four-target read-only inventory rejected as a round-trip gate' $fourTargetBaselineError.Contains('vmnet-four-target-readonly-baseline.v1') ('captureSha256=' + $fourTargetBaselineHash + '; ' + $fourTargetBaselineError)

    $fourTargetRoundTripPath = Join-Path $repoRoot 'spoof-handoff\reports\nic-vm-20261017\uefi-windows-probe-20261018-011305-e1ffeb9e48\evidence\kernel-metadata.json'
    if (-not (Test-Path -LiteralPath $fourTargetRoundTripPath -PathType Leaf)) { throw ('Sealed four-target VM round-trip capture is missing: ' + $fourTargetRoundTripPath) }
    $fourTargetRoundTripHash = '067674BBA247E1AD31FCA99826D4F3A0A67655DF989E1D129B8493117C518695'
    $roundTripSummary = Assert-HostRegistryVmCapture -CapturePath $fourTargetRoundTripPath -ExpectedCaptureSha256 $fourTargetRoundTripHash -LockedBindings $lockedBindings -RepositoryRoot $repoRoot
    Add-TestResult 'sealed four-target VM round-trip independently validates all 84 reads and restore rows' ($roundTripSummary.targetCount -eq 4 -and $roundTripSummary.phaseCount -eq 3 -and $roundTripSummary.actualValueCount -eq 84 -and $roundTripSummary.recordedChecksMatched -eq 84 -and $roundTripSummary.typedRegistryRestoreCount -eq 4 -and $roundTripSummary.restoredAdapterSnapshotCount -eq 4 -and $roundTripSummary.restoredLinkCheckCount -eq 4) ('captureSha256=' + $fourTargetRoundTripHash + '; actualValues=' + $roundTripSummary.actualValueCount + '; readers=' + $roundTripSummary.readerCount + '; restoreRows=' + $roundTripSummary.typedRegistryRestoreCount)

    $roundTripFixtureRoot = Join-Path $reportRoot ('four-target-roundtrip-tamper-fixtures.' + $runId)
    [void][IO.Directory]::CreateDirectory($roundTripFixtureRoot)
    try {
        $roundTripJson = Get-Content -LiteralPath $fourTargetRoundTripPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        $roundTripTamperCases = @(
            [ordered]@{ name = 'missing-check'; expected = 'VM capture must record exactly 84 reader checks; actual=83' },
            [ordered]@{ name = 'duplicate-check'; expected = 'VM recorded check is unknown or duplicated:' },
            [ordered]@{ name = 'wrong-reader-value'; expected = 'VM recorded check differs from independently derived observation:' },
            [ordered]@{ name = 'missing-owned-vmnet-device'; expected = 'VM state journal must contain exactly two owned VMnet devices; actual=1' },
            [ordered]@{ name = 'duplicate-owned-vmnet-instance-id'; expected = 'VM state journal virtual adapter instance ID is missing, duplicated, or absent from the exact target set:' },
            [ordered]@{ name = 'restored-original-mac-drift'; expected = 'VM restored adapter identity/state differs from target baseline:' },
            [ordered]@{ name = 'restored-admin-enabled-drift'; expected = 'VM restored adapter identity/state differs from target baseline:' },
            [ordered]@{ name = 'restored-network-class-drift'; expected = 'VM restored adapter identity/state differs from target baseline:' },
            [ordered]@{ name = 'restored-admin-status-drift'; expected = 'VM restore journal admin status mismatch:' }
        )
        foreach ($case in $roundTripTamperCases) {
            $tampered = ConvertTo-HostMutableData $roundTripJson
            switch ([string]$case.name) {
                'missing-check' { $tampered.checks = @($tampered.checks | Select-Object -Skip 1) }
                'duplicate-check' { $tampered.checks[1] = ConvertTo-HostMutableData $tampered.checks[0] }
                'wrong-reader-value' { $tampered.checks[0].actual = '02:FF:FF:FF:FF:FE' }
                'missing-owned-vmnet-device' { $tampered.stateJournal.devices = @($tampered.stateJournal.devices | Select-Object -Skip 1) }
                'duplicate-owned-vmnet-instance-id' { $tampered.stateJournal.devices[1].instanceId = [string]$tampered.stateJournal.devices[0].instanceId }
                'restored-original-mac-drift' { $tampered.restoredAdapterSnapshots[0].observedOriginalMac = [string]$tampered.restoredAdapterSnapshots[0].replacement }
                'restored-admin-enabled-drift' { $tampered.restoredAdapterSnapshots[0].adminState.enabled = -not [bool]$tampered.restoredAdapterSnapshots[0].adminState.enabled }
                'restored-network-class-drift' { $tampered.restoredAdapterSnapshots[0].registrySubKey = 'SYSTEM\CurrentControlSet\Control\Class\{4d36e972-e325-11ce-bfc1-08002be10318}\9999' }
                'restored-admin-status-drift' { $tampered.hostAdapterJournal.adapterProgress[0].restoreAdminStatus = 'Down' }
                default { throw ('Unknown four-target round-trip tamper test: ' + [string]$case.name) }
            }
            $tamperedPath = Join-Path $roundTripFixtureRoot ([string]$case.name + '.json')
            [IO.File]::WriteAllText($tamperedPath, (ConvertTo-Json -InputObject $tampered -Depth 64), [Text.UTF8Encoding]::new($false))
            $tamperedHash = (Get-FileHash -LiteralPath $tamperedPath -Algorithm SHA256).Hash.ToUpperInvariant()
            $tamperError = Assert-ThrowsExactContains {
                Assert-HostRegistryVmCapture -CapturePath $tamperedPath -ExpectedCaptureSha256 $tamperedHash -LockedBindings $lockedBindings -RepositoryRoot $repoRoot | Out-Null
            } @([string]$case.expected)
            Add-TestResult ('rehash-pinned four-target tamper rejected: ' + [string]$case.name) $tamperError.Contains([string]$case.expected) ('recomputedSha256=' + $tamperedHash + '; ' + $tamperError)
        }
    }
    finally {
        $fixtureResolved = [IO.Path]::GetFullPath($roundTripFixtureRoot)
        $reportResolved = [IO.Path]::GetFullPath($reportRoot).TrimEnd('\') + '\'
        if (-not $fixtureResolved.StartsWith($reportResolved, [StringComparison]::OrdinalIgnoreCase)) { throw 'Refusing to remove four-target VM tamper fixtures outside the compatibility report directory.' }
        if (Test-Path -LiteralPath $fixtureResolved -PathType Container) { Remove-Item -LiteralPath $fixtureResolved -Recurse -Force }
    }

    $badHashError = Assert-ThrowsExactContains {
        Assert-HostRegistryVmCapture -CapturePath $twoTargetCapturePath -ExpectedCaptureSha256 ('0' * 64) -LockedBindings $lockedBindings -RepositoryRoot $repoRoot | Out-Null
    } @('VM registry capture SHA-256 mismatch:', 'expected=', 'actual=')
    Add-TestResult 'capture hash pin rejects changed expected digest' $badHashError.Contains('SHA-256 mismatch') $badHashError

    $fixtureRoot = Join-Path $reportRoot ('vm-capture-tamper-fixtures.' + $runId)
    [void][IO.Directory]::CreateDirectory($fixtureRoot)
    try {
        $originalJson = Get-Content -LiteralPath $twoTargetCapturePath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        foreach ($case in @(
            [ordered]@{ name = 'schema'; property = 'schema'; value = 'tampered.v1'; expected = 'VM registry capture schema is invalid:' },
            [ordered]@{ name = 'restore'; property = 'restored'; value = $false; expected = 'VM registry capture does not record a completed restore.' }
        )) {
            $tampered = ConvertTo-HostMutableData $originalJson
            $tampered.schema = 'vmnet-four-target-address-roundtrip.v1'
            $tampered.vmIdentity = [ordered]@{ manufacturer = 'VMware, Inc.'; uuid = 'D7C14D56-24D1-9894-8906-C0CE95A07790' }
            $tampered[$case.property] = $case.value
            $tamperedPath = Join-Path $fixtureRoot ($case.name + '.json')
            [IO.File]::WriteAllText($tamperedPath, (ConvertTo-Json -InputObject $tampered -Depth 64), [Text.UTF8Encoding]::new($false))
            $tamperedHash = (Get-FileHash -LiteralPath $tamperedPath -Algorithm SHA256).Hash.ToUpperInvariant()
            $tamperError = Assert-ThrowsExactContains {
                Assert-HostRegistryVmCapture -CapturePath $tamperedPath -ExpectedCaptureSha256 $tamperedHash -LockedBindings $lockedBindings -RepositoryRoot $repoRoot | Out-Null
            } @([string]$case.expected)
            Add-TestResult ('rehash-pinned content tamper rejected: ' + $case.name) $tamperError.Contains([string]$case.expected) ('recomputedSha256=' + $tamperedHash + '; ' + $tamperError)
        }
    }
    finally {
        $fixtureResolved = [IO.Path]::GetFullPath($fixtureRoot)
        $reportResolved = [IO.Path]::GetFullPath($reportRoot).TrimEnd('\') + '\'
        if (-not $fixtureResolved.StartsWith($reportResolved, [StringComparison]::OrdinalIgnoreCase)) { throw 'Refusing to remove VM tamper fixtures outside the compatibility report directory.' }
        if (Test-Path -LiteralPath $fixtureResolved -PathType Container) { Remove-Item -LiteralPath $fixtureResolved -Recurse -Force }
    }

    $runSource = (Get-FunctionAst $hostAst 'Invoke-HostRun').Extent.Text
    $registryGateIndex = $runSource.IndexOf('Assert-HostRegistryVmCapture', [StringComparison]::Ordinal)
    $driverPinIndex = $runSource.IndexOf('Assert-HostVmnetAdapterDriverPin', [StringComparison]::Ordinal)
    $registryApplyIndex = $runSource.IndexOf('Invoke-HostAdapterRegistryApply', [StringComparison]::Ordinal)
    $registryBackendWired = $registryGateIndex -ge 0 -and $driverPinIndex -gt $registryGateIndex -and $registryApplyIndex -gt $driverPinIndex -and
        $runSource.Contains("backend = 'RegistryNetworkAddress'") -and
        $runSource.Contains('-MutationStarted ([ref]$mutationStarted)') -and
        -not $runSource.Contains('Set-NetAdapter')
    $driverPinSource = (Get-FunctionAst $hostAst 'Assert-HostVmnetAdapterDriverPin').Extent.Text
    $driverPinImplementationPinned = $driverPinSource.Contains('System32\drivers\vmnetadapter.sys') -and $driverPinSource.Contains('Get-HostSha256') -and $driverPinSource.Contains('$script:ExpectedVmnetAdapterDriverSha256')
    Add-TestResult 'host run pins VM evidence and the read-only vendor driver before registry backend mutation' ($registryBackendWired -and $driverPinImplementationPinned) ('registryGateIndex=' + $registryGateIndex + '; driverPinIndex=' + $driverPinIndex + '; registryApplyIndex=' + $registryApplyIndex)

    $rollbackSource = (Get-FunctionAst $hostAst 'Invoke-HostRollback').Extent.Text
    $snapshotGuardSource = (Get-FunctionAst $hostAst 'Assert-HostJournalSnapshot').Extent.Text
    $rollbackUnaffected = $rollbackSource.Contains('Assert-HostJournalSnapshot') -and
        -not $rollbackSource.Contains('Assert-HostRegistryVmCapture') -and
        -not $snapshotGuardSource.Contains('registryVmCapture') -and
        -not $snapshotGuardSource.Contains('Assert-HostRegistryVmCapture')
    Add-TestResult 'rollback validates durable host state without requiring the new VM capture' $rollbackUnaffected 'The rollback and journal validators do not depend on the additional run-time VM evidence field.'

    $result.status = 'PASS'
    $result.tests = $script:TestResults.ToArray()
    $result.testCount = $script:TestResults.Count
    $result.lockedAdapterCount = 4
}
catch {
    $result.status = 'FAIL'
    $result.tests = $script:TestResults.ToArray()
    $result.failure = $_.Exception.ToString()
    $result.fullyQualifiedErrorId = $_.FullyQualifiedErrorId
}

[IO.File]::WriteAllText($reportPath, (ConvertTo-Json -InputObject $result -Depth 16), [Text.UTF8Encoding]::new($false))
$result | ConvertTo-Json -Depth 16
if ($result.status -cne 'PASS') { throw ('NIC host registry apply pure regression failed. See ' + $reportPath) }
Write-Output ('EVIDENCE ' + $reportPath)
