[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$WorktreeRoot,
    [string]$RunDirectory,
    [switch]$LibraryOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$finalizerLibraryOnly = [bool]$LibraryOnly
$resumeRunDirectory = $RunDirectory
$repository = [IO.Path]::GetFullPath($RepositoryRoot)
$worktree = [IO.Path]::GetFullPath($WorktreeRoot)
$frozenRunner = Join-Path $worktree 'spoof-handoff\tools\combined_host\run.ps1'
$frozenRunnerSha256 = '5E20BFE3E4BAC1AA8C95AAD5BC5A2B53DEC0D1E4301021A76A9F03D54C09666D'
if ((Get-FileHash -LiteralPath $frozenRunner -Algorithm SHA256).Hash -cne $frozenRunnerSha256) { throw 'This continuation requires the unchanged R16 combined runner.' }
. $frozenRunner -Action Status -RepositoryRoot $repository -WorktreeRoot $worktree -LibraryOnly
Import-Module (Join-Path $PSScriptRoot 'step-support.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot '..\storage_host\model.psm1') -Force -DisableNameChecking -ErrorAction Stop

function Assert-FinalizationReference {
    param([string]$Path,[string]$Sha256,[string]$Boundary)
    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetFullPath($Boundary).TrimEnd('\') + '\'
    if (-not $full.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -or $Sha256 -notmatch '\A[A-F0-9]{64}\z') { throw "Invalid finalization evidence reference: $Path" }
    $actual = Get-CombinedHostSha256 $full
    if ($actual -cne $Sha256) { throw "Evidence SHA-256 mismatch: $Path expected=$Sha256 actual=$actual" }
    return [pscustomobject]@{path=$full;sha256=$actual}
}

function Assert-FinalizationPending {
    param([object]$Journal)
    if ([string]$Journal.phase -cne 'PolicyRestorePendingReboot' -or (Get-CombinedValue $Journal 'operationInProgress') -ne $false) { throw 'Continuation requires an idle PolicyRestorePendingReboot combined journal.' }
    if ((Get-CombinedValue (Get-CombinedValue $Journal 'pathBStageRestore') 'restored') -ne $true -or (Get-CombinedValue (Get-CombinedValue $Journal 'nicTransaction') 'restorationComplete') -ne $true) { throw 'The recorded ESP stage and NIC transaction must already be restored.' }
}

function Get-FinalizationLayout {
    param([string]$Worktree)
    $directory = Join-Path $Worktree ('spoof-handoff\reports\cf\' + [Guid]::NewGuid().ToString('N'))
    $export = Join-Path $directory 'bcd\boot-store-export.bcd'
    if ($export.Length -gt 240) { throw "Final BCD export destination exceeds the conservative native path limit: length=$($export.Length) limit=240 path=$export" }
    return [pscustomobject]@{directory=$directory;exportPath=$export;exportPathLength=$export.Length}
}

function Get-FinalizationCompletedStorage {
    param([object]$Journal,[string]$Worktree)
    $storageRoot = [string]$Journal.storageSnapshot.runDirectory
    $boundary = [IO.Path]::GetFullPath((Join-Path $Worktree 'spoof-handoff\reports\storage-host-runs')).TrimEnd('\') + '\'
    if (-not [IO.Path]::GetFullPath($storageRoot).StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)) { throw 'Storage state is outside the worktree report boundary.' }
    $statePath = Join-Path $storageRoot 'state.json'
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ($state.schema -cne 'storage-host-run.v1' -or $state.phase -cne 'Complete' -or $state.requiresRecovery -ne $false -or $null -ne $state.pendingOperation -or $null -ne $state.ownedPackages.transparent -or $null -ne $state.ownedPackages.replacement) { throw 'Storage must already be Complete with no pending recovery or owned packages; no storage action will be repeated.' }
    $baselinePath = Join-Path $storageRoot 'snapshot\baseline.json'
    $baselineReference = Assert-FinalizationReference $baselinePath $state.snapshotBaselineSha256 $storageRoot
    $baseline = Get-Content -LiteralPath $baselineReference.path -Raw | ConvertFrom-Json
    $manifestReference = Assert-FinalizationReference (Join-Path $storageRoot 'recovery-copy-manifest.json') $Journal.storageSnapshot.recoveryManifestSha256 $storageRoot
    $manifest = Get-Content -LiteralPath $manifestReference.path -Raw | ConvertFrom-Json
    $baselineRecords = @($manifest.files | Where-Object { $_.relativePath -ceq 'snapshot\baseline.json' })
    if ($baselineRecords.Count -ne 1 -or [string]$baselineRecords[0].sha256 -cne [string]$baselineReference.sha256) { throw 'Storage baseline does not match its independently pinned recovery manifest entry.' }
    $null = Assert-FinalizationReference $baselinePath $baselineRecords[0].sha256 $storageRoot
    $certRecords = @($manifest.files | Where-Object { $_.relativePath -ceq 'snapshot\hypersinv.cer' })
    if ($certRecords.Count -ne 1) { throw 'Pinned recovery manifest must contain exactly one signing certificate.' }
    $certificateReference = Assert-FinalizationReference (Join-Path $storageRoot 'snapshot\hypersinv.cer') $certRecords[0].sha256 $storageRoot
    $reference = Assert-FinalizationReference $state.finalVerificationPath $state.finalVerificationSha256 $storageRoot
    $report = Get-Content -LiteralPath $reference.path -Raw | ConvertFrom-Json
    if ($report.schema -cne 'storage-host-final-verification.v1' -or $report.result -cne 'PASS' -or $report.testOutcome -cne 'BOTH_PACKAGES_VERIFIED_AND_REMOVED' -or @($report.mismatches).Count -ne 0) { throw 'Completed storage final report does not prove a successful restored round trip.' }
    foreach ($proofName in @('verifiedTransparentProof','verifiedReplacementProof')) {
        $proof = $report.$proofName
        $null = Assert-FinalizationReference $proof.path $proof.sha256 $storageRoot
    }
    $stdoutPath = Join-Path $Journal.runDirectory 'actions\storage-finalverify.stdout.txt'
    $summary = Get-Content -LiteralPath $stdoutPath -Raw | ConvertFrom-Json
    if ($summary.status -cne 'FINAL_VERIFY_PASS' -or $summary.testOutcome -cne $report.testOutcome -or [IO.Path]::GetFullPath([string]$summary.finalReport) -cne $reference.path -or [IO.Path]::GetFullPath([string]$summary.runDirectory) -cne [IO.Path]::GetFullPath($storageRoot) -or $summary.secureBoot -ne $true -or $summary.filterPresent -ne $false) { throw 'Original storage FinalVerify stdout does not match its persisted report.' }
    return [pscustomobject]@{
        state=$state; baseline=$baseline; report=$report;certificate=$certificateReference
        evidence=[pscustomobject]@{result=$summary;reusedCompletedStep=$true;report=$reference;statePath=$statePath;stateSha256=(Get-CombinedHostSha256 $statePath);baseline=$baselineReference;stdoutPath=$stdoutPath;stdoutSha256=(Get-CombinedHostSha256 $stdoutPath);capturedUtc=$report.capturedUtc}
    }
}

function Assert-FinalizationFreshStorage {
    param([object]$Inventory,[object]$Baseline,[object]$Trust)
    foreach ($field in @('serial','wmiSerial','pnpDeviceId','number','isBoot','isSystem')) {
        if ([string]$Inventory.disk.$field -cne [string]$Baseline.inventory.disk.$field) { throw "Restored disk $field mismatch: expected=$($Baseline.inventory.disk.$field) actual=$($Inventory.disk.$field)" }
    }
    if ($Inventory.storageFilter.present -ne $false -or $null -ne $Inventory.storageFilter.registry -or $Inventory.pnpStack.filterPresent -ne $false -or $Inventory.pnpStack.exitCode -ne 0) { throw 'Fresh disk service/stack checks do not prove filter removal.' }
    if ($Inventory.fileIo.match -ne $true) { throw "File I/O mismatch: expected=$($Inventory.fileIo.expectedSha256) actual=$($Inventory.fileIo.actualSha256)" }
    if ($Inventory.policy.secureBoot -ne $true -or $Inventory.policy.bcdCurrentExitCode -ne 0 -or $Inventory.policy.codeIntegrity.success -ne $true -or (([long]$Inventory.policy.codeIntegrity.options -band 2) -ne 0)) { throw 'Fresh policy check requires Secure Boot ON, readable BCD, and effective test-signing OFF.' }
    $expected = $Baseline.inventory.policy.testSigning
    $actual = $Inventory.policy.testSigning
    if ($actual.exists -ne $expected.exists -or [string]$actual.value -cne [string]$expected.value) { throw "Test-signing element mismatch: expected=$($expected | ConvertTo-Json -Compress) actual=$($actual | ConvertTo-Json -Compress)" }
    foreach ($store in @('Root','TrustedPublisher')) {
        if ($Trust.$store -ne $false -or $Trust.$store -ne $Baseline.trustStores.$store) { throw "Certificate trust mismatch: store=$store expected=$($Baseline.trustStores.$store) actual=$($Trust.$store)" }
    }
}

function Assert-FinalizationBcd {
    param([object]$Baseline,[object]$Current)
    foreach ($name in @('current','all','firmware','exportedAll')) {
        $capture = $Current.$name
        if ($capture.exitCode -ne 0 -or [string]::IsNullOrWhiteSpace([string]$capture.stdout)) { throw "Final BCD capture failed: $name exit=$($capture.exitCode) stdout=$($capture.stdout) stderr=$($capture.stderr)" }
        if ([string]$capture.stdout -cne [string]$Baseline.$name.stdout) { throw "Supported BCD enumeration mismatch: $name expected=$($Baseline.$name.stdout) actual=$($capture.stdout)" }
    }
}

function Assert-FinalizationNicState {
    param([object[]]$Baseline,[object[]]$Current)
    if ($Baseline.Count -ne 4 -or $Current.Count -ne 4) { throw 'Final NIC state must contain the four captured adapters.' }
    foreach ($expected in $Baseline) {
        $matches = @($Current | Where-Object { [string]::Equals([string]$_.pnpDeviceId,[string]$expected.pnpDeviceId,[StringComparison]::OrdinalIgnoreCase) })
        if ($matches.Count -ne 1) { throw "NIC identity cardinality mismatch: $($expected.pnpDeviceId) expected=1 actual=$($matches.Count)" }
        $actual = $matches[0]
        foreach ($field in @('interfaceGuid','linkConnected')) {
            if ([string]$actual.$field -cne [string]$expected.$field) { throw "NIC $field mismatch: $($expected.pnpDeviceId) expected=$($expected.$field) actual=$($actual.$field)" }
        }
        if (($actual.mac -replace '[:-]','').ToUpperInvariant() -cne ($expected.original -replace '[:-]','').ToUpperInvariant()) { throw "NIC MAC mismatch: $($expected.pnpDeviceId) expected=$($expected.original) actual=$($actual.mac)" }
        if ($actual.adminEnabled -ne $expected.adminState.enabled) { throw "NIC administrative state mismatch: $($expected.pnpDeviceId) expected=$($expected.adminState.enabled) actual=$($actual.adminEnabled)" }
        if ($expected.networkAddress.present -ne $false -or $actual.networkAddressPresent -ne $false) { throw "NIC NetworkAddress restoration mismatch: $($expected.pnpDeviceId) expected absent actualPresent=$($actual.networkAddressPresent). This continuation requires the captured absent-value baseline." }
    }
}

function Get-FinalizationLinkState {
    param([string]$Media,[string]$Status)
    $mediaConnected = if ($Media -match '^(?i:Connected)$' -or $Media -ceq '1') { $true } elseif ($Media -match '^(?i:Disconnected)$' -or $Media -ceq '2') { $false } else { $null }
    $statusConnected = if ($Status -match '^(?i:Up|Connected)$') { $true } elseif ($Status -match '^(?i:Down|Disconnected|Disabled)$') { $false } else { $null }
    if ($null -eq $mediaConnected -and $null -eq $statusConnected) { throw "Cannot establish NIC link state: media=$Media status=$Status" }
    if ($null -ne $mediaConnected -and $null -ne $statusConnected -and $mediaConnected -ne $statusConnected) { throw "Conflicting NIC link state: media=$Media status=$Status" }
    if ($null -ne $mediaConnected) { return [bool]$mediaConnected }
    return [bool]$statusConnected
}

function Resolve-FinalizationNicIdentity {
    param([object]$Snapshot,[object[]]$PlainAdapters,[object[]]$CimAdapters)
    $guid = ([Guid]$Snapshot.interfaceGuid).ToString('D').ToUpperInvariant()
    $adapters = @($PlainAdapters | Where-Object { ([Guid]$_.InterfaceGuid).ToString('D').ToUpperInvariant() -ceq $guid })
    $devices = @($CimAdapters | Where-Object { [string]::Equals([string]$_.PnPDeviceID,[string]$Snapshot.pnpDeviceId,[StringComparison]::OrdinalIgnoreCase) })
    if ($adapters.Count -ne 1 -or $devices.Count -ne 1) { throw "Fresh NIC identity lookup failed: $($Snapshot.pnpDeviceId) adapterCount=$($adapters.Count) cimCount=$($devices.Count)" }
    if (([Guid]$devices[0].InterfaceGuid).ToString('D').ToUpperInvariant() -cne $guid -or -not [string]::Equals([string]$adapters[0].PnPDeviceID,[string]$Snapshot.pnpDeviceId,[StringComparison]::OrdinalIgnoreCase)) { throw "Fresh NIC GUID/PnP cross-check failed: expectedGuid=$guid expectedPnp=$($Snapshot.pnpDeviceId) plainPnp=$($adapters[0].PnPDeviceID) cimGuid=$($devices[0].InterfaceGuid)" }
    return [pscustomobject]@{adapter=$adapters[0];device=$devices[0]}
}

function Read-FinalizationNicState {
    param([object[]]$Snapshots)
    $plain = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop)
    $cim = @(Get-CimInstance -Namespace root\StandardCimv2 -ClassName MSFT_NetAdapter -ErrorAction Stop)
    $records = [Collections.Generic.List[object]]::new()
    foreach ($snapshot in $Snapshots) {
        $identity = Resolve-FinalizationNicIdentity -Snapshot $snapshot -PlainAdapters $plain -CimAdapters $cim
        $adapter = $identity.adapter
        $device = $identity.device
        $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey([string]$snapshot.registrySubKey,$false)
        if ($null -eq $key) { throw "NIC registry key is missing: $($snapshot.registrySubKey)" }
        try { $addressPresent = @($key.GetValueNames() | Where-Object { $_ -ieq 'NetworkAddress' }).Count -ne 0 } finally { $key.Dispose() }
        $link = Get-FinalizationLinkState ([string]$device.MediaConnectState) ([string]$adapter.Status)
        $records.Add([pscustomobject]@{pnpDeviceId=[string]$device.PnPDeviceID;interfaceGuid=([Guid]$device.InterfaceGuid).ToString('D').ToUpperInvariant();mac=[string]$adapter.MacAddress;adminEnabled=([string]$adapter.AdminStatus -ceq 'Up');mediaConnectState=[string]$device.MediaConnectState;adapterStatus=[string]$adapter.Status;linkConnected=$link;networkAddressPresent=$addressPresent})
    }
    return @($records.ToArray())
}

function Assert-FinalizationFirmwareRecord {
    param([object]$Expected,[object]$Actual)
    foreach ($record in @($Expected,$Actual)) {
        if ($record.present -isnot [bool]) { throw "Firmware presence is not boolean: $($record.name)" }
        if (-not $record.present) {
            if ([int]$record.win32Error -ne 203 -or [uint32]$record.attributes -ne 0 -or [long]$record.size -ne 0 -or -not [string]::IsNullOrEmpty([string]$record.base64) -or -not [string]::IsNullOrEmpty([string]$record.sha256)) { throw "Invalid missing firmware record: $($record.name) error=$($record.win32Error) attributes=$($record.attributes) size=$($record.size) bytes=$($record.base64) hash=$($record.sha256)" }
        } else {
            if ([int]$record.win32Error -ne 0 -or [long]$record.size -le 0 -or [string]$record.sha256 -notmatch '\A[A-F0-9]{64}\z') { throw "Invalid present firmware record: $($record.name) error=$($record.win32Error) size=$($record.size)" }
            $decoded = [Convert]::FromBase64String([string]$record.base64)
            if ($decoded.Length -ne [long]$record.size -or (Get-CombinedByteArraySha256 $decoded) -cne [string]$record.sha256) { throw "Firmware record size/hash does not match its bytes: $($record.name)" }
        }
    }
    if ([string]$Expected.name -cne [string]$Actual.name -or ([Guid]$Expected.guid) -ne ([Guid]$Actual.guid)) { throw 'Firmware record identity mismatch.' }
    foreach ($field in @('present','win32Error','attributes','size','sha256','base64')) {
        if ([string]$Expected.$field -cne [string]$Actual.$field) { throw "Firmware record changed: $($Expected.name) field=$field expected=$($Expected.$field) actual=$($Actual.$field)" }
    }
}

function Assert-FinalizationFirmwareRecords {
    param([object[]]$Expected,[object[]]$Actual)
    if ($Expected.Count -eq 0 -or $Expected.Count -ne $Actual.Count) { throw "Firmware record count mismatch: expected=$($Expected.Count) actual=$($Actual.Count)" }
    foreach ($record in $Expected) {
        $matches = @($Actual | Where-Object { [string]$_.name -ceq [string]$record.name -and ([Guid]$_.guid) -eq ([Guid]$record.guid) })
        if ($matches.Count -ne 1) { throw "Firmware record identity missing or duplicated: $($record.name) actualCount=$($matches.Count)" }
        Assert-FinalizationFirmwareRecord $record $matches[0]
    }
}

function Read-FinalizationFirmwareState {
    param([object]$Snapshot,[string]$Repository,[string]$Worktree,[string]$EvidenceDirectory)
    $reference = Assert-FinalizationReference $Snapshot.path $Snapshot.sha256 (Join-Path $Worktree 'spoof-handoff\reports')
    $baseline = Get-Content -LiteralPath $reference.path -Raw | ConvertFrom-Json
    if ($baseline.schema -cne 'combined-host-firmware-snapshot.v1' -or $baseline.secureBootMode -ne $true -or $Snapshot.secureBootMode -ne $baseline.secureBootMode) { throw 'Pinned firmware snapshot schema or Secure Boot mode is invalid.' }
    Assert-FinalizationFirmwareRecords @($baseline.records) @($Snapshot.records)
    . (Join-Path $Worktree 'spoof-handoff\tools\smbios_boot\snapshot-reference.ps1') -RepoRoot $Repository -OutputBoundary (Join-Path $Worktree 'spoof-handoff') -LibraryOnly
    Initialize-ReadyFirmwareReader
    [ReadyToArmFirmwareRead]::Enable()
    $records = [Collections.Generic.List[object]]::new()
    foreach ($expected in @($baseline.records)) {
        [uint32]$attributes=0; [int]$readError=0
        $bytes=[ReadyToArmFirmwareRead]::Read([string]$expected.name,[string]$expected.guid,[ref]$attributes,[ref]$readError)
        $present=$null -ne $bytes
        $records.Add([pscustomobject]@{name=[string]$expected.name;guid=[string]$expected.guid;present=$present;win32Error=$readError;attributes=$attributes;size=if($present){$bytes.Length}else{0};base64=if($present){[Convert]::ToBase64String($bytes)}else{$null};sha256=if($present){Get-CombinedByteArraySha256 $bytes}else{$null}})
    }
    $mode=[bool](Confirm-SecureBootUEFI -ErrorAction Stop)
    $path=Join-Path $EvidenceDirectory 'firmware-current.json'
    Write-CombinedHostAtomicJson $path ([ordered]@{schema='combined-host-final-firmware-read.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');baseline=$reference;records=@($records.ToArray());secureBootMode=$mode;writeAttempted=$false})
    Assert-FinalizationFirmwareRecords @($baseline.records) @($records.ToArray())
    if ($mode -ne $baseline.secureBootMode) { throw "Secure Boot mode changed: expected=$($baseline.secureBootMode) actual=$mode" }
    return [pscustomobject]@{match=$true;path=$path;sha256=(Get-CombinedHostSha256 $path);recordCount=$records.Count;secureBootMode=$mode;writeAttempted=$false}
}

function Invoke-CombinedFinalization {
    param([string]$Repository,[string]$Worktree,[string]$RequestedRun)
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Open elevated Windows PowerShell for the read-only final verification.' }
    $defaults = Get-CombinedStepDefaults -RepositoryRoot $Repository -WorktreeRoot $Worktree
    $lock = Enter-CombinedStepLock -Path $defaults.LockPath
    $evidenceDirectory = $null
    try {
        if (-not $RequestedRun) { $RequestedRun = (Read-CombinedStepPrepared -Path $defaults.PreparedPath -RunRoot $defaults.RunRoot).runDirectory }
        $loaded = Get-CombinedHostJournal -Repository $Repository -Worktree $Worktree -RunDirectory $RequestedRun
        $journal = $loaded.journal
        Assert-FinalizationPending $journal
        $layout = Get-FinalizationLayout $Worktree
        $evidenceDirectory = $layout.directory
        [void][IO.Directory]::CreateDirectory($evidenceDirectory)
        Write-Host "Combined final continuation started; evidence: $evidenceDirectory"
        $before = [IO.File]::ReadAllBytes($loaded.path)
        [IO.File]::WriteAllBytes((Join-Path $evidenceDirectory 'journal-before.json'),$before)
        $null = Assert-CombinedHostActivationJournal -Journal $journal -Repository $Repository -Worktree $Worktree
        $map = New-CombinedBindingMap (Get-CombinedMapRows ([string]$journal.lockedMap.path))
        foreach ($pair in @(@('originals','original'),@('nicReadback','active'))) {
            $record = $journal[$pair[0]]
            $recordPath = [string](Get-CombinedValue $record 'reportPath')
            if (-not $recordPath) { $recordPath = [string](Get-CombinedValue $record 'path') }
            $null = Assert-FinalizationReference $recordPath $record.sha256 (Join-Path $Worktree 'spoof-handoff\reports')
            $null = Assert-CombinedReadbackPhase -BindingMap $map -Readback (Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json) -Phase $pair[1]
        }
        $nicReference = Assert-FinalizationReference $journal.nicTransaction.path $journal.nicTransaction.sha256 $journal.runDirectory
        $nic = Get-Content -LiteralPath $nicReference.path -Raw | ConvertFrom-Json
        if ($nic.restorationComplete -ne $true -or @($nic.restoreErrors).Count -ne 0 -or @($nic.adapterSnapshots).Count -ne 4) { throw 'NIC restoration journal is incomplete.' }
        $storage = Get-FinalizationCompletedStorage $journal $Worktree
        $secureBoot = Get-CombinedSecureBootVariableSnapshot -Worktree $Worktree -ExpectedEnabled $true
        $null = Assert-CombinedTestSigning $false
        Write-Host 'Capturing BCD with the shorter destination; the live store is not modified.'
        $bcd = Get-CombinedBcdSnapshot -Repository $Repository -EvidenceDirectory $evidenceDirectory
        Assert-FinalizationBcd $journal.bcdSnapshot $bcd
        Write-CombinedHostAtomicJson (Join-Path $evidenceDirectory 'bcd-snapshot.json') $bcd
        Write-Host 'Checking fresh storage, policy, trust, and package-removal state.'
        . (Join-Path $Worktree 'spoof-handoff\tools\storage_host\read.ps1')
        $fresh = Write-StorageHostInventory -EvidenceDirectory $evidenceDirectory -Name 'storage-current.json'
        $cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new($storage.certificate.path)
        try { $trust = Get-StorageHostCertificateMembership -Thumbprint $cert.Thumbprint } finally { $cert.Dispose() }
        Assert-FinalizationFreshStorage $fresh.inventory $storage.baseline $trust
        $infHashes = @([string]$storage.report.packageRemoval.transparentInfSha256,[string]$storage.report.packageRemoval.replacementInfSha256)
        foreach ($candidate in @(Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' -File -ErrorAction Stop)) {
            if ((Get-CombinedHostSha256 $candidate.FullName) -cin $infHashes) { throw "Owned storage INF remains installed: $($candidate.FullName)" }
        }
        $nicCurrent = @(Read-FinalizationNicState @($nic.adapterSnapshots))
        Assert-FinalizationNicState @($nic.adapterSnapshots) $nicCurrent
        $nicDrivers = @(Get-CimInstance Win32_SystemDriver -Filter "Name='client_nic_filter'" -ErrorAction Stop)
        if ($nicDrivers.Count -ne 0 -or (Test-Path -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\client_nic_filter' -ErrorAction Stop)) { throw 'NIC filter service remains present; final verification is incomplete.' }
        Write-CombinedHostAtomicJson (Join-Path $evidenceDirectory 'nic-current.json') ([ordered]@{adapters=$nicCurrent;nicFilterServicePresent=$false;registryChanged=$false})
        Write-Host 'Reading all twelve bindings in original mode through the full reader matrix.'
        $original = Invoke-CombinedReadback -RepositoryRoot $Worktree -OutputDirectory 'spoof-handoff\reports\ch\r' -ExpectedMode Original -SmbiosReaderPath $journal.smbiosReader.path -NicReaderPath $journal.nicReader.path -NicNativeExePath $journal.nicNativeReader.path -LockedMapPath $journal.lockedMap.path
        $originalReport = Get-Content -LiteralPath $original.reportPath -Raw | ConvertFrom-Json
        $null = Assert-CombinedReadbackPhase -BindingMap $map -Readback $originalReport -Phase restored
        Write-Host 'Comparing ESP and firmware with the original snapshots.'
        $esp = Get-CombinedEspSnapshot -EvidenceDirectory $evidenceDirectory -BcdSnapshot $bcd
        $espCheck = Assert-CombinedEspMatchesBaseline -Baseline $journal.espSnapshot -Current $esp
        $firmwareReadback = Read-FinalizationFirmwareState -Snapshot $journal.firmwareSnapshot -Repository $Repository -Worktree $Worktree -EvidenceDirectory $evidenceDirectory
        $firmware = [bool]$firmwareReadback.match
        if ((Get-CombinedHostSha256 $loaded.path) -cne $loaded.sha256) { throw 'Combined journal changed during finalization; no journal update was applied.' }
        if ((Get-CombinedHostSha256 $storage.evidence.statePath) -cne $storage.evidence.stateSha256) { throw 'Completed storage state changed during finalization; no journal update was applied.' }
        $result = [ordered]@{schema='combined-host-final-continuation.v1';status='FINAL_VERIFY_PASS';simultaneousRun=$true;bindingCount=12;runDirectory=$journal.runDirectory;originalReadback=$original;activeReadback=$journal.nicReadback;storage=$storage.evidence;freshStorage=$fresh;freshNicState=$nicCurrent;secureBoot=$secureBoot;testSigning=$false;trustStores=$trust;bcd=$bcd;esp=$espCheck;firmwareRecordsUnchanged=$firmware;firmwareReadback=$firmwareReadback;originalJournalSha256=$loaded.sha256;runnerSha256=$frozenRunnerSha256;continuationSha256=(Get-CombinedHostSha256 $PSCommandPath);machineSettingsChanged=$false;rawLiveBcdHashClaimed=$false}
        $proofPath = Join-Path $evidenceDirectory 'final-verification.json'
        Write-CombinedHostAtomicJson $proofPath $result
        $journal.phase = 'Complete'
        $journal.operationInProgress = $false
        Set-CombinedHostField $journal 'finalFirmwareReadback' $firmwareReadback
        foreach ($entry in @(@('finalStorageVerification',$storage.evidence),@('finalReadback',$original),@('finalBcdSnapshot',$bcd),@('finalEspSnapshot',$esp),@('finalFirmwareUnchanged',$firmware),@('finalEspComparison',$espCheck),@('finalContinuation',([ordered]@{path=$proofPath;sha256=(Get-CombinedHostSha256 $proofPath);journalBeforePath=(Join-Path $evidenceDirectory 'journal-before.json');journalBeforeSha256=$loaded.sha256})))) { Set-CombinedHostField $journal $entry[0] $entry[1] }
        $journal.lastAction = 'FinalVerify'
        $journal.lastActionUtc = [DateTime]::UtcNow.ToString('o')
        $journalHash = Save-CombinedHostJournal $journal
        $summary = [ordered]@{status='FINAL_VERIFY_PASS';simultaneousRun=$true;bindingCount=12;secureBoot=$true;testSigning=$false;originalReadback=$original;proofPath=$proofPath;proofSha256=(Get-CombinedHostSha256 $proofPath);journalPath=$loaded.path;journalSha256=$journalHash;evidenceDirectory=$evidenceDirectory;machineSettingsChanged=$false}
        Write-CombinedHostAtomicJson (Join-Path $evidenceDirectory 'summary.json') $summary
        return [pscustomobject]$summary
    } catch {
        if ($evidenceDirectory) { Write-CombinedHostAtomicJson (Join-Path $evidenceDirectory 'failure.json') ([ordered]@{status='FINAL_VERIFY_INCOMPLETE';capturedUtc=[DateTime]::UtcNow.ToString('o');error=$_.Exception.ToString();scriptStackTrace=$_.ScriptStackTrace;machineSettingsChanged=$false}) }
        throw
    } finally { $lock.Dispose() }
}

if (-not $finalizerLibraryOnly) {
    try { Invoke-CombinedFinalization -Repository $repository -Worktree $worktree -RequestedRun $resumeRunDirectory | ConvertTo-Json -Depth 12 }
    catch { [Console]::Error.WriteLine($_.Exception.ToString()); exit 1 }
}
