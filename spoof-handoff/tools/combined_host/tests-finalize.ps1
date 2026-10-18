[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$testWorktree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$testRepository = [IO.Path]::GetFullPath((Join-Path $testWorktree '..\..\..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $testWorktree ('spoof-handoff\reports\cf-tests\' + [Guid]::NewGuid().ToString('N')) }
$testOutput = [IO.Path]::GetFullPath($OutputDirectory)
$testBoundary = (Join-Path $testWorktree 'spoof-handoff\reports').TrimEnd('\') + '\'
if (-not $testOutput.StartsWith($testBoundary,[StringComparison]::OrdinalIgnoreCase)) { throw 'Test output must remain in worktree reports.' }
[void][IO.Directory]::CreateDirectory($testOutput)
. (Join-Path $PSScriptRoot 'finalize.ps1') -RepositoryRoot $testRepository -WorktreeRoot $testWorktree -LibraryOnly
$checks = [Collections.Generic.List[object]]::new()
function Test-Check {
    param([string]$Name,[scriptblock]$Body,[switch]$Reject)
    $caught = $null
    try { & $Body | Out-Null } catch { $caught = $_.Exception.Message }
    $pass = if ($Reject) { $null -ne $caught } else { $null -eq $caught }
    $checks.Add([pscustomobject]@{name=$Name;pass=$pass;expectedRejection=[bool]$Reject;error=$caught})
}
function Copy-TestValue { param($Value) return ($Value | ConvertTo-Json -Depth 20 | ConvertFrom-Json) }

# Resolve the actual reader command graph in a clean process without device reads.
$storageReadPath = Join-Path $PSScriptRoot '..\storage_host\read.ps1'
. $storageReadPath
$dependencyTokens=$null; $dependencyErrors=$null
$readerAst=[Management.Automation.Language.Parser]::ParseFile($storageReadPath,[ref]$dependencyTokens,[ref]$dependencyErrors)
Test-Check 'Storage reader parses before inventory capture' { if ($dependencyErrors.Count) { throw 'Storage reader parse errors' } }
$readerCommands=@($readerAst.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst]},$true) | ForEach-Object { $_.GetCommandName() } | Where-Object { $_ } | Sort-Object -Unique)
foreach ($readerCommand in $readerCommands) {
    Test-Check ("Storage inventory dependency resolves: $readerCommand") { Get-Command -Name $readerCommand -ErrorAction Stop }
}
$stackFixture=@('Stack:','  partmgr','  disk','  EhStorClass','  stornvme')
Test-Check 'Real stack parser recognizes a restored storage stack' { if ((@(Get-StorageHostStackLines -PnpUtilOutput $stackFixture) -join '|') -cne 'partmgr|disk|EhStorClass|stornvme') { throw 'Unexpected parsed stack' } }
Test-Check 'Real filter detector distinguishes the restored stack' { if (Test-StorageHostStackContainsFilter -PnpUtilOutput $stackFixture) { throw 'Unexpected filter' } }
Test-Check 'Real BCD parser recognizes an absent test-signing element' { $element=Get-StorageHostBcdElement -Lines @('Windows Boot Loader','identifier {current}'); if ($element.exists -ne $false) { throw 'Unexpected test-signing element' } }
$pending = [pscustomobject]@{phase='PolicyRestorePendingReboot';operationInProgress=$false;pathBStageRestore=@{restored=$true};nicTransaction=@{restorationComplete=$true}}
Test-Check 'Completed storage can resume the final pending combined phase' { Assert-FinalizationPending $pending }
foreach ($badPhase in @('Prepared','ReplacementVerified','Complete')) {
    $candidate = Copy-TestValue $pending
    $candidate.phase = $badPhase
    Test-Check "Reject phase $badPhase" { Assert-FinalizationPending $candidate } -Reject
}
$candidate = Copy-TestValue $pending; $candidate.operationInProgress=$true
Test-Check 'Reject an operation still in progress' { Assert-FinalizationPending $candidate } -Reject
$candidate = Copy-TestValue $pending; $candidate.pathBStageRestore.restored=$false
Test-Check 'Reject missing stage restoration' { Assert-FinalizationPending $candidate } -Reject
$candidate = Copy-TestValue $pending; $candidate.nicTransaction.restorationComplete=$false
Test-Check 'Reject incomplete NIC restoration' { Assert-FinalizationPending $candidate } -Reject
Test-Check 'Real worktree gets a short native BCD destination' { $layout=Get-FinalizationLayout $testWorktree; if ($layout.exportPathLength -gt 240 -or -not $layout.directory.StartsWith($testBoundary)) { throw 'Invalid layout' } }
Test-Check 'Overlong destination is rejected before a BCD call' { Get-FinalizationLayout ('C:\' + ('x' * 240)) } -Reject
$sample = Join-Path $testOutput 'sample.txt'
[IO.File]::WriteAllText($sample,'evidence')
$sampleHash = Get-CombinedHostSha256 $sample
Test-Check 'Evidence reference matches its real bytes' { Assert-FinalizationReference $sample $sampleHash $testOutput }
Test-Check 'Reject hash mismatch' { Assert-FinalizationReference $sample ('0'*64) $testOutput } -Reject
Test-Check 'Reject evidence outside its report boundary' { Assert-FinalizationReference $sample $sampleHash (Join-Path $testOutput 'other') } -Reject
$bcdBase = [pscustomobject]@{}
foreach ($part in @('current','all','firmware','exportedAll')) { $bcdBase | Add-Member -NotePropertyName $part -NotePropertyValue ([pscustomobject]@{exitCode=0;stdout="captured $part";stderr=''}) }
Test-Check 'All four BCD views must match' { Assert-FinalizationBcd $bcdBase (Copy-TestValue $bcdBase) }
foreach ($part in @('current','all','firmware','exportedAll')) {
    $candidate = Copy-TestValue $bcdBase; $candidate.$part.stdout='different'
    Test-Check "Reject drift in $part" { Assert-FinalizationBcd $bcdBase $candidate } -Reject
}
$candidate = Copy-TestValue $bcdBase; $candidate.all.exitCode=1
Test-Check 'Reject failed BCD enumeration' { Assert-FinalizationBcd $bcdBase $candidate } -Reject
$candidate = Copy-TestValue $bcdBase; $candidate.firmware.stdout=''
Test-Check 'Reject empty firmware enumeration' { Assert-FinalizationBcd $bcdBase $candidate } -Reject
$inventory = [pscustomobject]@{
    disk=[pscustomobject]@{serial='original';wmiSerial='original';pnpDeviceId='device';number=0;isBoot=$true;isSystem=$true}
    storageFilter=[pscustomobject]@{present=$false;registry=$null}
    pnpStack=[pscustomobject]@{filterPresent=$false;exitCode=0}
    fileIo=[pscustomobject]@{match=$true;expectedSha256='match';actualSha256='match'}
    policy=[pscustomobject]@{secureBoot=$true;bcdCurrentExitCode=0;codeIntegrity=[pscustomobject]@{success=$true;options=1};testSigning=[pscustomobject]@{exists=$false;value=$null}}
}
$baseline = [pscustomobject]@{inventory=(Copy-TestValue $inventory);trustStores=[pscustomobject]@{Root=$false;TrustedPublisher=$false}}
$trust = [pscustomobject]@{Root=$false;TrustedPublisher=$false}
Test-Check 'Accept clean fresh storage and policy state' { Assert-FinalizationFreshStorage $inventory $baseline $trust }
foreach ($field in @('serial','wmiSerial','pnpDeviceId','number','isBoot','isSystem')) {
    $candidate = Copy-TestValue $inventory; $candidate.disk.$field='different'
    Test-Check "Reject disk $field mismatch" { Assert-FinalizationFreshStorage $candidate $baseline $trust } -Reject
}
foreach ($change in @(
    @{name='storage service';set={param($v) $v.storageFilter.present=$true}},
    @{name='service registry';set={param($v) $v.storageFilter.registry=@{start=0}}},
    @{name='stack filter';set={param($v) $v.pnpStack.filterPresent=$true}},
    @{name='stack query error';set={param($v) $v.pnpStack.exitCode=1}},
    @{name='file I/O';set={param($v) $v.fileIo.match=$false}},
    @{name='Secure Boot OFF';set={param($v) $v.policy.secureBoot=$false}},
    @{name='BCD read error';set={param($v) $v.policy.bcdCurrentExitCode=1}},
    @{name='Code Integrity query error';set={param($v) $v.policy.codeIntegrity.success=$false}},
    @{name='effective test-signing';set={param($v) $v.policy.codeIntegrity.options=3}},
    @{name='test-signing element drift';set={param($v) $v.policy.testSigning.exists=$true}}
)) {
    $candidate = Copy-TestValue $inventory; & $change.set $candidate
    Test-Check ('Reject '+$change.name) { Assert-FinalizationFreshStorage $candidate $baseline $trust } -Reject
}
foreach ($store in @('Root','TrustedPublisher')) {
    $changedTrust = Copy-TestValue $trust; $changedTrust.$store=$true
    Test-Check "Reject remaining $store certificate trust" { Assert-FinalizationFreshStorage $inventory $baseline $changedTrust } -Reject
}
$nicBaseline = @(0..3 | ForEach-Object { [pscustomobject]@{pnpDeviceId=('device'+$_);interfaceGuid=('guid'+$_);original=('00:50:56:C0:00:0'+$_);adminState=@{enabled=$true};mediaConnectState='1';linkConnected=$true;networkAddress=@{present=$false}} })
$nicCurrent = @($nicBaseline | ForEach-Object { [pscustomobject]@{pnpDeviceId=$_.pnpDeviceId;interfaceGuid=$_.interfaceGuid;mac=$_.original;adminEnabled=$true;mediaConnectState='1';linkConnected=$true;networkAddressPresent=$false} })
Test-Check 'Accept four restored NIC states' { Assert-FinalizationNicState $nicBaseline $nicCurrent }
$candidate = Copy-TestValue $nicCurrent
$candidate[0].pnpDeviceId = $candidate[0].pnpDeviceId.ToUpperInvariant()
Test-Check 'Restored NIC identity comparison accepts case-only PnP differences' { Assert-FinalizationNicState $nicBaseline $candidate }
$identitySnapshot=[pscustomobject]@{pnpDeviceId='PCI\VEN_10EC\4&3FA6681&0&00E2';interfaceGuid='1291F611-0C4F-4ACF-99A6-E6A22D2F9026'}
$identityPlain=[pscustomobject]@{PnPDeviceID='pci\ven_10ec\4&3fa6681&0&00e2';InterfaceGuid='{1291f611-0c4f-4acf-99a6-e6a22d2f9026}'}
$identityCim=[pscustomobject]@{PnPDeviceID='PCI\VEN_10EC\4&3fa6681&0&00E2';InterfaceGuid='{1291F611-0C4F-4ACF-99A6-E6A22D2F9026}'}
Test-Check 'Realtek-style case variants resolve through GUID and PnP cross-checks' { Resolve-FinalizationNicIdentity $identitySnapshot @($identityPlain) @($identityCim) }
Test-Check 'Duplicate CIM identities are rejected even with case variants' { $duplicate=Copy-TestValue $identityCim; $duplicate.PnPDeviceID=$duplicate.PnPDeviceID.ToUpperInvariant(); Resolve-FinalizationNicIdentity $identitySnapshot @($identityPlain) @($identityCim,$duplicate) } -Reject
Test-Check 'Duplicate plain adapter GUIDs are rejected' { Resolve-FinalizationNicIdentity $identitySnapshot @($identityPlain,$identityPlain) @($identityCim) } -Reject
Test-Check 'Different CIM PnP identity is rejected' { $different=Copy-TestValue $identityCim; $different.PnPDeviceID+='0'; Resolve-FinalizationNicIdentity $identitySnapshot @($identityPlain) @($different) } -Reject
Test-Check 'CIM GUID disagreement is rejected' { $different=Copy-TestValue $identityCim; $different.InterfaceGuid=[Guid]::Empty.ToString('D'); Resolve-FinalizationNicIdentity $identitySnapshot @($identityPlain) @($different) } -Reject
Test-Check 'Plain adapter PnP disagreement is rejected despite matching GUID' { $different=Copy-TestValue $identityPlain; $different.PnPDeviceID+='0'; Resolve-FinalizationNicIdentity $identitySnapshot @($different) @($identityCim) } -Reject
Test-Check 'Reject missing virtual adapter' { Assert-FinalizationNicState $nicBaseline @($nicCurrent | Select-Object -First 3) } -Reject
foreach ($field in @('pnpDeviceId','interfaceGuid','mac','linkConnected','adminEnabled','networkAddressPresent')) {
    $candidate = Copy-TestValue $nicCurrent
    if ($field -in @('linkConnected','adminEnabled')) { $candidate[0].$field=$false }
    elseif ($field -eq 'networkAddressPresent') { $candidate[0].$field=$true }
    else { $candidate[0].$field='different' }
    Test-Check "Reject restored NIC $field mismatch" { Assert-FinalizationNicState $nicBaseline $candidate } -Reject
}
Test-Check 'Unknown media with Disconnected status matches the existing host link rule' { if ((Get-FinalizationLinkState '0' 'Disconnected') -ne $false) { throw 'Expected disconnected' } }
Test-Check 'Connected sources agree' { if ((Get-FinalizationLinkState '1' 'Up') -ne $true) { throw 'Expected connected' } }
Test-Check 'Conflicting NIC link sources fail closed' { Get-FinalizationLinkState '1' 'Disconnected' } -Reject
Test-Check 'Unknown NIC link sources fail closed' { Get-FinalizationLinkState '0' 'Unknown' } -Reject

# Read existing evidence only; deliberately make repeated nested actions unusable.
function Invoke-CombinedStorageAction { throw 'A completed storage action must never be repeated by this continuation.' }
$currentRun = Join-Path $testWorktree 'spoof-handoff\reports\combined-host-20261018\20261018T205738834Z.bc2a10c95c164531a8a251d591d5bbbe'
$actualJournal = (Get-CombinedHostJournal $testRepository $testWorktree $currentRun).journal
$firmwareFixture=Get-Content -LiteralPath $actualJournal.firmwareSnapshot.path -Raw|ConvertFrom-Json
Test-Check 'Reproduce the frozen comparator null-versus-empty defect' { if (-not ($null -cne [string]$null)) { throw 'Expected the original comparison defect' } }
foreach ($firmwareRecord in @($firmwareFixture.records)) {
    Test-Check ('Unchanged firmware record: '+$firmwareRecord.name) { Assert-FinalizationFirmwareRecord $firmwareRecord (Copy-TestValue $firmwareRecord) }
}
$missing=@($firmwareFixture.records|Where-Object {$_.name -ceq 'BootNext'})[0]
$present=@($firmwareFixture.records|Where-Object {$_.name -ceq 'BootOrder'})[0]
$candidate=Copy-TestValue $missing; $candidate.base64=''; $candidate.sha256=''
Test-Check 'Missing-variable null and empty serialization both represent no bytes' { Assert-FinalizationFirmwareRecord $missing $candidate }
foreach ($alteration in @(
    @{name='absent read becomes access denied';set={param($v) $v.win32Error=5}},
    @{name='absent read has inconsistent attributes';set={param($v) $v.attributes=7}},
    @{name='absent record carries bytes';set={param($v) $v.base64='AA=='}},
    @{name='absent record carries a length';set={param($v) $v.size=1}},
    @{name='absent record carries a hash';set={param($v) $v.sha256=('0'*64)}},
    @{name='absent record claims presence';set={param($v) $v.present=$true}}
)) {
    $candidate=Copy-TestValue $missing; & $alteration.set $candidate
    Test-Check ('Reject '+$alteration.name) { Assert-FinalizationFirmwareRecord $missing $candidate } -Reject
}
$candidate=Copy-TestValue $present; $candidate.present=$false; $candidate.win32Error=203; $candidate.size=0; $candidate.attributes=0; $candidate.base64=$null; $candidate.sha256=$null
Test-Check 'A previously present variable becoming absent is rejected' { Assert-FinalizationFirmwareRecord $present $candidate } -Reject
$candidate=Copy-TestValue $missing; $candidate.present=$true; $candidate.win32Error=0; $candidate.size=1; $candidate.attributes=7; $candidate.base64='AA=='; $candidate.sha256=Get-CombinedByteArraySha256 ([byte[]]@(0))
Test-Check 'BootNext appearing after restoration is rejected' { Assert-FinalizationFirmwareRecord $missing $candidate } -Reject
foreach ($field in @('attributes','win32Error','size','sha256','base64','guid','name')) {
    $candidate=Copy-TestValue $present
    switch($field){
        'attributes' {$candidate.attributes=6}
        'win32Error' {$candidate.win32Error=5}
        'size' {$candidate.size++}
        'sha256' {$candidate.sha256=('0'*64)}
        'base64' {$candidate.base64='AA=='}
        'guid' {$candidate.guid=[Guid]::Empty.ToString('D')}
        'name' {$candidate.name='bootorder'}
    }
    Test-Check ("Reject changed present firmware $field") { Assert-FinalizationFirmwareRecord $present $candidate } -Reject
}
$candidate=Copy-TestValue $present; $changedBytes=[Convert]::FromBase64String($candidate.base64); $changedBytes[0]=$changedBytes[0] -bxor 1; $candidate.base64=[Convert]::ToBase64String($changedBytes); $candidate.sha256=Get-CombinedByteArraySha256 $changedBytes
Test-Check 'Changed bytes remain rejected with a self-consistent new hash' { Assert-FinalizationFirmwareRecord $present $candidate } -Reject
Test-Check 'All 21 baseline firmware records compare strictly' { Assert-FinalizationFirmwareRecords @($firmwareFixture.records) @($firmwareFixture.records) }
Test-Check 'Missing firmware record is rejected' { Assert-FinalizationFirmwareRecords @($firmwareFixture.records) @($firmwareFixture.records|Select-Object -Skip 1) } -Reject
$candidate=Copy-TestValue @($firmwareFixture.records); $candidate[1]=Copy-TestValue $candidate[0]
Test-Check 'Duplicate firmware identities are rejected' { Assert-FinalizationFirmwareRecords @($firmwareFixture.records) $candidate } -Reject
Test-Check 'Reuse actual completed storage proof without invoking its action runner' { $completed=Get-FinalizationCompletedStorage $actualJournal $testWorktree; if ($completed.evidence.result.status -cne 'FINAL_VERIFY_PASS' -or -not $completed.evidence.reusedCompletedStep) { throw 'Wrong completed proof' } }
Test-Check 'Current activation pins remain valid without changing sources or artifacts' { Assert-CombinedHostActivationJournal -Journal $actualJournal -Repository $testRepository -Worktree $testWorktree }
Test-Check 'Frozen runner bytes are preserved' { if ((Get-CombinedHostSha256 $frozenRunner) -cne $frozenRunnerSha256) { throw 'Changed frozen runner' } }
$result = [ordered]@{schema='combined-host-finalization-tests.v1';status=if(@($checks|Where-Object {-not $_.pass}).Count){'FAIL'}else{'PASS'};checks=$checks.Count;passed=@($checks|Where-Object pass).Count;results=@($checks.ToArray());hostMutationPerformed=$false}
Write-CombinedHostAtomicJson (Join-Path $testOutput 'tests.json') $result
[pscustomobject]@{status=$result.status;checks=$result.checks;passed=$result.passed;evidenceDirectory=$testOutput} | ConvertTo-Json -Compress
if ($result.status -cne 'PASS') { throw "Finalization tests failed; evidence=$testOutput" }
