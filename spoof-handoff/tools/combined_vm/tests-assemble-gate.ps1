[CmdletBinding()]
param([string]$WorktreeRoot,[string]$RepositoryRoot)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue

if ([string]::IsNullOrWhiteSpace($WorktreeRoot)) { $WorktreeRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) }
if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) { $RepositoryRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $WorktreeRoot)) }
$WorktreeRoot = [IO.Path]::GetFullPath($WorktreeRoot)
$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$reportsRoot = Join-Path $WorktreeRoot 'spoof-handoff\reports'
$testRoot = Join-Path $reportsRoot ('gate-assembler-tests.' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($testRoot)
$testWorktreeRoot = $WorktreeRoot
$testRepositoryRoot = $RepositoryRoot
. (Join-Path $PSScriptRoot 'assemble-gate.ps1') -LibraryOnly
$WorktreeRoot = $testWorktreeRoot
$RepositoryRoot = $testRepositoryRoot
$unitMailboxKey = [byte[]](0..31)
Set-CombinedAssemblyUnitTestMailboxKey -Key $unitMailboxKey

function Assert-Test([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Get-TestSha([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','') } finally { $sha.Dispose() }
}
function New-TestColdRestoreFixture([string]$Directory) {
    [void][IO.Directory]::CreateDirectory($Directory)
    $snapshotPath = Join-Path $Directory 'cold-snapshot.vmsd'
    [IO.File]::WriteAllBytes($snapshotPath,[byte[]](1,2,3,4,5))
    $id = 'Combined-Clean4-20261018'
    $restoredBoot = [ordered]@{bootId=('D' * 64);lastBootUpTimeUtc='2026-10-18T10:07:00.0000000Z'}
    $baselineSecureBoot = [ordered]@{enabled=$false;variable=[ordered]@{supported=$true;win32Error=0;size=1;attributes=6;bytesHex='00';bytesBase64='AA=='}}
    $baselineCodeIntegrity = [ordered]@{success=$true;options=5;ntStatus='0x00000000';returnLength=8}
    $baselineTestSigning = [ordered]@{exists=$false;value=$null}
    $baselineBcdExportHash = 'A' * 64
    $finalBcdExportHash = 'B' * 64
    $details = [ordered]@{
        status='FINAL_VERIFY_PASS';secureBoot=$baselineSecureBoot;effectiveCodeIntegrity=$baselineCodeIntegrity;bcdTestSigning=$baselineTestSigning
        bcdNonTestSigningPropertiesIdentical=$true;bcdExportByteIdentical=$false;bcdExportByteIdentityIsEvidenceOnly=$true
        espDelta=@();filterServices=@();trust=[ordered]@{Root=$false;TrustedPublisher=$false};bootIdentity=[ordered]@{lastBootUpTimeUtc='2026-10-18T09:55:00.0000000Z'}
    }
    $lifecycleRecord = [ordered]@{schema='combined-vm-lifecycle-result.v1';action='FinalVerify';status='FINAL_VERIFY_PASS';result=$details;failure=$null}
    $journal = [ordered]@{
        schema='combined-vm-lifecycle-journal.v1';status='FINAL_VERIFY_PASS';secureBoot=$baselineSecureBoot;initialTestSigning=$baselineTestSigning
        initialCodeIntegrity=$baselineCodeIntegrity;bcdExportSha256=$baselineBcdExportHash
        finalSnapshot=[ordered]@{bcdExportSha256=$finalBcdExportHash;bcdExportByteIdentityIsEvidenceOnly=$true}
    }
    $lifecycleMetadata = [ordered]@{schema='combined-vm-lifecycle-metadata.v1';action='FinalVerify';outcomeStatus='FINAL_VERIFY_PASS';result=$lifecycleRecord;journal=$journal;completedUtc='2026-10-18T10:02:00.0000000Z'}
    $cold = [ordered]@{
        schema='combined-vm-cold-snapshot-restore.v1';status='PASS'
        snapshot=[ordered]@{id=$id;path=$snapshotPath;sha256=(Get-CombinedAssemblySha256 $snapshotPath)}
        snapshotInventory=[ordered]@{command='C:\\VMware\\vmrun.exe -T ws listSnapshots C:\\VM\\Combined.vmx';exitCode=0;stdout="Total snapshots: 3`r`n$id`r`n";stderr='';startedUtc='2026-10-18T10:02:10.0000000Z';completedUtc='2026-10-18T10:02:11.0000000Z'}
        restore=[ordered]@{command="C:\\VMware\\vmrun.exe -T ws revertToSnapshot C:\\VM\\Combined.vmx $id";exitCode=0;stdout='Reverted to snapshot.';stderr='';startedUtc='2026-10-18T10:03:00.0000000Z';completedUtc='2026-10-18T10:04:00.0000000Z'}
        start=[ordered]@{command='C:\\VMware\\vmrun.exe -T ws start C:\\VM\\Combined.vmx nogui';exitCode=0;stdout='Started';stderr='';startedUtc='2026-10-18T10:05:00.0000000Z';completedUtc='2026-10-18T10:06:00.0000000Z'}
        bootIdentity=$restoredBoot
    }
    $phaseMetadata = [ordered]@{status='PASS';secureBoot=$false;codeIntegrity=$baselineCodeIntegrity;completedUtc='2026-10-18T10:08:00.0000000Z'}
    $originalPhase = [ordered]@{phase=[ordered]@{bootIdentity=[ordered]@{bootId=('C' * 64);lastBootUpTimeUtc='2026-10-18T09:50:00.0000000Z'}};metadata=$phaseMetadata}
    $restoredPhase = [ordered]@{phase=[ordered]@{bootIdentity=[ordered]@{bootId=$restoredBoot.bootId;lastBootUpTimeUtc=$restoredBoot.lastBootUpTimeUtc}};metadata=$phaseMetadata}
    return [pscustomobject]@{Mailbox=[pscustomobject]@{metadata=$lifecycleMetadata};Cold=$cold;Original=$originalPhase;Restored=$restoredPhase;SnapshotPath=$snapshotPath;Directory=$Directory}
}
function Assert-ColdLifecycleRejected([string]$Name,[scriptblock]$Mutator,[string]$ExpectedError) {
    $fixture = New-TestColdRestoreFixture -Directory (Join-Path $testRoot ('cold-reject-' + $Name))
    $metadata = $fixture.Mailbox.metadata | ConvertTo-Json -Depth 30 | ConvertFrom-Json
    $original = $fixture.Original | ConvertTo-Json -Depth 30 | ConvertFrom-Json
    $restored = $fixture.Restored | ConvertTo-Json -Depth 30 | ConvertFrom-Json
    $null = & $Mutator $metadata $original $restored
    $rejected = $false
    try {
        $null = Assert-CombinedAssemblyFinalLifecycle -Mailbox ([pscustomobject]@{metadata=$metadata}) -ColdRestore $fixture.Cold -OriginalPhase $original -RestoredPhase $restored -ReportRoots @($testRoot)
    } catch { $rejected = $_.Exception.Message -match $ExpectedError }
    Assert-Test $rejected ("Final lifecycle accepted or unexpectedly rejected invalid $Name state.")
    $checks.Add([ordered]@{name=('cold-final-' + $Name);status='PASS';note='A mismatched final policy/state or incomplete BCD classification was rejected against the authenticated lifecycle baseline.'})
}
function New-TestActiveNicRestoreFixture([string]$Directory) {
    [void][IO.Directory]::CreateDirectory($Directory)
    $mapPath = Join-Path $Directory 'vm-fixture-map.tsv'
    $mapRows = @(
        [ordered]@{member='FakeMacAddress';device='adapter:2|ROOT\NET\0000';original='00:50:56:C0:00:01';replacement='02:90:10:20:30:40'},
        [ordered]@{member='FakeMacAddress';device='adapter:3|ROOT\NET\0001';original='00:50:56:C0:00:02';replacement='02:90:10:20:30:41'},
        [ordered]@{member='FakeMacAddress';device='adapter:5|PCI\VEN_15AD&DEV_07B0\0000';original='00:50:56:C0:00:03';replacement='02:90:10:20:30:42'},
        [ordered]@{member='FakeMacAddress';device='adapter:7|PCI\VEN_15AD&DEV_07B0\0001';original='00:50:56:C0:00:04';replacement='02:90:10:20:30:43'}
    )
    $mapText = "member`tdevice`toriginal`treplacement`r`n" + (($mapRows | ForEach-Object { $_.member + "`t" + $_.device + "`t" + $_.original + "`t" + $_.replacement }) -join "`r`n") + "`r`n"
    [IO.File]::WriteAllText($mapPath,$mapText,[Text.UTF8Encoding]::new($false))
    $readers = @('ndisOid8023CurrentAddress','getAdaptersAddresses','getAdaptersInfoAddress','getIfTable2','win32NetworkAdapter','msftNetAdapterNetworkAddresses','getNetAdapter')
    $evaluations = [Collections.Generic.List[object]]::new()
    $counters = [ordered]@{setupDiNdisInterfaceMatchCount=1;getAdaptersAddressesMatchCount=1;getAdaptersInfoMatchCount=1;getIfTable2MatchCount=1;win32NetworkAdapterMatchCount=1;msftNetAdapterMatchCount=1;getNetAdapterMatchCount=1}
    foreach ($mapRow in $mapRows) {
        $pnp = ([string]$mapRow.device -split '\|',2)[1]
        $observations = [ordered]@{}
        foreach ($reader in $readers) { $observations[$reader] = @([ordered]@{value=$mapRow.original}) }
        $evaluations.Add([ordered]@{pnpDeviceId=$pnp;expectedMode='Original';observations=$observations;ndisOidStatus=[ordered]@{ioctlSucceeded=$true;bytesReturned=6;win32Error=0};dot11CurrentAddressCheck=[ordered]@{applicable=$false};setupDiNdisInterfaceMatchCount=$counters.setupDiNdisInterfaceMatchCount;getAdaptersAddressesMatchCount=$counters.getAdaptersAddressesMatchCount;getAdaptersInfoMatchCount=$counters.getAdaptersInfoMatchCount;getIfTable2MatchCount=$counters.getIfTable2MatchCount;win32NetworkAdapterMatchCount=$counters.win32NetworkAdapterMatchCount;msftNetAdapterMatchCount=$counters.msftNetAdapterMatchCount;getNetAdapterMatchCount=$counters.getNetAdapterMatchCount})
    }
    $report = [ordered]@{schema='nic-native-wmi-readback.v1';expectedMode='Original';allLockedExpectedValuesMatchReadOnlySources=$true;allLockedOriginalsMatchReadOnlySources=$true;failures=@();lockedOriginalEvaluations=@($evaluations.ToArray())}
    $reportPath = Join-Path $Directory 'nic-original-readback.json'
    [IO.File]::WriteAllText($reportPath,($report | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
    $sha = Get-CombinedAssemblySha256 $reportPath
    $metadata = [ordered]@{nicMutationStarted=$true;nicRestoreErrors=@();nicOriginalReadback=[ordered]@{path=$reportPath;sha256=$sha;content=(Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json)}}
    $members = @([pscustomobject][ordered]@{guestPath=$reportPath;sha256=$sha;path=$reportPath})
    return [pscustomobject]@{Metadata=$metadata;Members=$members;MapPath=$mapPath;ReportPath=$reportPath;Directory=$Directory}
}
function New-TestZipBytes([string]$Name,[byte[]]$Bytes) {
    $memory = [IO.MemoryStream]::new()
    $zip = [IO.Compression.ZipArchive]::new($memory,[IO.Compression.ZipArchiveMode]::Create,$true)
    try {
        $entry = $zip.CreateEntry($Name)
        $stream = $entry.Open()
        try { $stream.Write($Bytes,0,$Bytes.Length) } finally { $stream.Dispose() }
    } finally { $zip.Dispose() }
    return ,([byte[]]$memory.ToArray())
}
function Write-TestFixtureMap([string]$Path,[object[]]$Rows) {
    $header = 'member' + "`t" + 'device' + "`t" + 'original' + "`t" + 'replacement'
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add($header)
    foreach ($row in $Rows) { $lines.Add(([string]$row.member + "`t" + [string]$row.device + "`t" + [string]$row.original + "`t" + [string]$row.replacement)) }
    [IO.File]::WriteAllText($Path,(($lines.ToArray()) -join "`r`n") + "`r`n",[Text.UTF8Encoding]::new($false))
}
function New-TestMailboxFiles([string]$Directory,[object]$Metadata) {
    [void][IO.Directory]::CreateDirectory($Directory)
    $memberBytes = [Text.Encoding]::UTF8.GetBytes('{"schema":"test-only","status":"PASS"}')
    $memberHash = Get-TestSha $memberBytes
    $innerBytes = New-TestZipBytes -Name 'f.0001.json' -Bytes $memberBytes
    $innerHash = Get-TestSha $innerBytes
    $innerBase64 = [Convert]::ToBase64String($innerBytes)
    $Metadata.capturedFiles = @([ordered]@{name='test-only.json';guestPath='C:\ProgramData\CI\evidence\test-only.json';archiveLeaf='f.0001.json';bytes=$memberBytes.Length;sha256=$memberHash})
    $Metadata.rawEvidenceBundle = [ordered]@{format='zip';bytes=$innerBytes.Length;sha256=$innerHash;fileCount=1;base64=$innerBase64}
    $metadataBytes = [Text.Encoding]::UTF8.GetBytes(($Metadata | ConvertTo-Json -Depth 20 -Compress))
    $outerBytes = New-TestZipBytes -Name 'kernel-metadata.json' -Bytes $metadataBytes
    $archivePath = Join-Path $Directory 'guest-evidence.zip'
    [IO.File]::WriteAllBytes($archivePath,$outerBytes)
    $archiveHash = Get-CombinedAssemblySha256 $archivePath
    $runId = 'uefi-windows-probe-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,10)
    $completedUtc=[DateTime]::UtcNow.ToString('o')
    $message = 'synthetic mailbox response'
    $messageBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($message))
    $chunkSize = 9000
    $chunkCount = [int][Math]::Ceiling($outerBytes.Length / [double]$chunkSize)
    $response = [ordered]@{SchemaVersion=1;RunId=$runId;Status='PASS';MessageBase64=$messageBase64;CompletedUtc=$completedUtc;EvidenceSha256=$archiveHash;EvidenceSize=$outerBytes.Length;EvidenceChunkSize=$chunkSize;EvidenceChunkCount=$chunkCount;ResponseHmacSha256=''}
    $response.ResponseHmacSha256 = Get-CombinedAssemblyHmacSha256Hex -Key $script:CombinedAssemblyUnitTestMailboxKey -Text (Get-CombinedAssemblyCanonicalMailboxResponse $response)
    $responsePath = Join-Path $Directory 'response.json'
    [IO.File]::WriteAllText($responsePath,($response | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    $receipt = [ordered]@{SchemaVersion=1;RunId=$runId;Status='PASS';Message=$message;CompletedUtc=$completedUtc;ResponseHmacSha256=$response.ResponseHmacSha256;EvidenceSha256=$archiveHash;EvidenceArchive='guest-evidence.zip';EvidenceSize=$outerBytes.Length;EvidenceChunkSize=$chunkSize;EvidenceChunkCount=$chunkCount;ResponseManifestVariable='uefiloader.response.manifest'}
    $receiptPath = Join-Path $Directory 'response-metadata.json'
    [IO.File]::WriteAllText($receiptPath,($receipt | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    $transportPath = Join-Path $Directory 'transport-metadata.json'
    $transport = [ordered]@{RunId=$runId;Action='Probe';VmxPath='C:\\VM\\Combined.vmx';GuestScriptSha256=('C' * 64);CompanionExeSha256=('D' * 64);PackageSha256=('E' * 64);PackageSize=256;ExpectedProbeVerdict='NOT_MET';ResultStatus='PASS'}
    [IO.File]::WriteAllText($transportPath,($transport | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{
        Capture=[ordered]@{
            archive=[ordered]@{path=$archivePath;sha256=$archiveHash}
            response=[ordered]@{path=$responsePath;sha256=(Get-CombinedAssemblySha256 $responsePath)}
            responseMetadata=[ordered]@{path=$receiptPath;sha256=(Get-CombinedAssemblySha256 $receiptPath)}
            transportMetadata=[ordered]@{path=$transportPath;sha256=(Get-CombinedAssemblySha256 $transportPath)}
            expectedSchema='combined-vm-reader-phase.v1';expectedPhase='Original'
        }
        InnerBytes=$innerBytes;InnerRecord=$Metadata.capturedFiles[0]
    }
}

function New-TestAuthenticationSourcePins {
    return @(
        [pscustomobject][ordered]@{path=(Join-Path $WorktreeRoot 'spoof-handoff\tools\device_vm\invoke-mailbox.ps1');sha256=(Get-CombinedAssemblySha256 (Join-Path $WorktreeRoot 'spoof-handoff\tools\device_vm\invoke-mailbox.ps1'))},
        [pscustomobject][ordered]@{path=(Join-Path $RepositoryRoot 'tools\host-runner\HostRunner.Core.psm1');sha256=(Get-CombinedAssemblySha256 (Join-Path $RepositoryRoot 'tools\host-runner\HostRunner.Core.psm1'))}
    )
}

$checks = [Collections.Generic.List[object]]::new()
try {
    $vectorResponse = [ordered]@{
        SchemaVersion=1
        RunId='uefi-windows-probe-20261018-064910-0123456789'
        Status='PASS'
        MessageBase64='c3ludGhldGljIG1haWxib3ggcmVzcG9uc2U='
        CompletedUtc='2026-10-18T06:49:10.0000000Z'
        EvidenceSha256=('A' * 64)
        EvidenceSize=123
        EvidenceChunkSize=9000
        EvidenceChunkCount=1
    }
    $expectedCanonical = @('SchemaVersion=1','RunId=uefi-windows-probe-20261018-064910-0123456789','Status=PASS','MessageBase64=c3ludGhldGljIG1haWxib3ggcmVzcG9uc2U=','CompletedUtc=2026-10-18T06:49:10.0000000Z',('EvidenceSha256=' + ('A' * 64)),'EvidenceSize=123','EvidenceChunkSize=9000','EvidenceChunkCount=1') -join "`n"
    Assert-Test ((Get-CombinedAssemblyCanonicalMailboxResponse $vectorResponse) -ceq $expectedCanonical) 'Canonical response field order or newline encoding differs from invoke-mailbox.ps1.'
    $vectorHmac = Get-CombinedAssemblyHmacSha256Hex -Key $script:CombinedAssemblyUnitTestMailboxKey -Text $expectedCanonical
    Assert-Test ($vectorHmac -ceq '9D03869804D36DD1DD19F5D15B34F2FBF7456855FA7D0A343A3CF3F44534AE35') 'Canonical response HMAC differs from the independent fixed vector.'
    $checks.Add([ordered]@{name='canonical-response-protocol-vector';status='PASS';note='Field order, UTF-8/newline canonical text, and HMAC match a fixed independent unit vector.'})

    $canonicalNull = Get-CombinedAssemblyCanonicalJson -Value $null
    $canonicalEmptyArray = Get-CombinedAssemblyCanonicalJson -Value ([object[]]@())
    $canonicalNullableObject = Get-CombinedAssemblyCanonicalJson -Value ([ordered]@{empty=@();items=@('value',$null);nil=$null})
    Assert-Test ($canonicalNull -ceq 'null') 'A null JSON value was not preserved by canonicalization.'
    Assert-Test ($canonicalEmptyArray -ceq '[]') 'An empty JSON array was not preserved by canonicalization.'
    Assert-Test ($canonicalNullableObject -ceq '{"empty":[],"items":["value",null],"nil":null}') 'Null values or empty arrays inside an object were not preserved by canonicalization.'
    $checks.Add([ordered]@{name='canonical-null-and-empty-collections';status='PASS';note='Null, empty-array, and mixed null/array values canonicalize to valid JSON.'})

    $actualOriginalPath = Join-Path $WorktreeRoot 'spoof-handoff\reports\f6\runtime\mailbox\uefi-windows-probe-20261018-080727-61dba672ee\evidence\kernel-metadata.json'
    if (Test-Path -LiteralPath $actualOriginalPath -PathType Leaf) {
        $actualOriginalHash = Get-CombinedAssemblySha256 $actualOriginalPath
        Assert-Test ($actualOriginalHash -ceq '860846355285F55F4B3A053655135C8DE76144EC3307E879F9427011596F6A5C') 'Authenticated Original metadata fixture hash differs from the reviewed evidence.'
        $actualOriginalMetadata = Get-Content -LiteralPath $actualOriginalPath -Raw | ConvertFrom-Json -ErrorAction Stop
        Assert-Test ($null -ne $actualOriginalMetadata.PSObject.Properties['failure'] -and $null -eq $actualOriginalMetadata.failure) 'Authenticated Original metadata fixture no longer contains its expected null failure field.'
        Assert-Test ($actualOriginalMetadata.nicRestoreErrors -is [System.Array] -and $actualOriginalMetadata.capturedFiles -is [System.Array] -and $actualOriginalMetadata.inputsVerified -is [System.Array]) 'Authenticated Original metadata fixture arrays were not parsed as arrays.'
        $actualOriginalCanonical = Get-CombinedAssemblyCanonicalJson -Value $actualOriginalMetadata
        $actualOriginalRoundTrip = ConvertFrom-Json -InputObject $actualOriginalCanonical -ErrorAction Stop
        Assert-Test ((Get-CombinedAssemblyCanonicalJson -Value $actualOriginalRoundTrip) -ceq $actualOriginalCanonical) 'Actual authenticated Original metadata did not survive canonical JSON round-trip.'
        $checks.Add([ordered]@{name='canonical-actual-original-metadata';status='PASS';note='Hash-pinned Original mailbox metadata containing null fields and arrays canonicalized and round-tripped without loss.'})
    } else {
        $checks.Add([ordered]@{name='canonical-actual-original-metadata';status='SKIP';note='The local hash-pinned Original mailbox capture is absent; synthetic null/empty-array cases still ran.'})
    }

    $libraryGateRejected = $false
    try { $null = Invoke-CombinedVmGateAssembly -InputManifestPath 'unused.json' -RepositoryRoot $RepositoryRoot -WorktreeRoot $WorktreeRoot -OutputDirectory (Join-Path $testRoot 'library-gate-attempt') } catch { $libraryGateRejected = $_.Exception.Message -match 'disabled in LibraryOnly mode' }
    Assert-Test $libraryGateRejected 'LibraryOnly unit key could be used to assemble runtime gate evidence.'
    $checks.Add([ordered]@{name='library-key-cannot-assemble-gate';status='PASS';note='LibraryOnly mode rejects gate assembly before touching input or emitting any gate evidence.'})

    $authPins = @(Assert-CombinedAssemblyAuthenticationSourcePins -SourcePins (New-TestAuthenticationSourcePins) -RepositoryRoot $RepositoryRoot -WorktreeRoot $WorktreeRoot)
    Assert-Test ($authPins.Count -eq 2 -and $authPins[0].sha256 -match '^[A-F0-9]{64}$' -and $authPins[1].sha256 -match '^[A-F0-9]{64}$') 'Pinned mailbox protocol and DPAPI resolver sources were not validated.'
    $badAuthPins = @(New-TestAuthenticationSourcePins)
    $badAuthPins[0].sha256 = '0' * 64
    $authPinDriftRejected = $false
    try { $null = Assert-CombinedAssemblyAuthenticationSourcePins -SourcePins $badAuthPins -RepositoryRoot $RepositoryRoot -WorktreeRoot $WorktreeRoot } catch { $authPinDriftRejected = $_.Exception.Message -match 'source changed' }
    Assert-Test $authPinDriftRejected 'Mailbox protocol source hash drift was not rejected.'
    $checks.Add([ordered]@{name='authentication-source-pin-and-drift';status='PASS';note='The exact mailbox verifier and credential resolver source hashes are required; a stale pin is rejected.'})

    # A fully shaped but explicitly test-only input exercises the metadata and outer-authentication linkage parser.
    $metadata = [ordered]@{schema='combined-vm-reader-phase.v1';phase='Original';status='PASS';hostExecutionPermitted=$false;storageLifecycleOwnedByOuterRunner=$true;nicRestoreErrors=@();combinedReadbackContent=@{schema='synthetic-test-only'};syntheticFixtureOnly=$true}
    $mailboxInput = New-TestMailboxFiles -Directory (Join-Path $testRoot 'mailbox-valid') -Metadata $metadata
    $mailbox = Get-CombinedAssemblyMailbox -Capture $mailboxInput.Capture -Label 'unit-valid' -OutputDirectory $testRoot -ReportRoots @($reportsRoot) -ArtifactRoots @((Join-Path $WorktreeRoot 'spoof-handoff\artifacts')) -RepositoryRoot $RepositoryRoot
    Assert-Test ($mailbox.runId -match '^uefi-windows-probe-') 'Valid test mailbox RunId was not preserved.'
    Assert-Test ($mailbox.metadata.syntheticFixtureOnly -eq $true -and $mailbox.authentication.verified -eq $true -and $mailbox.authentication.verificationSource -match 'cryptographic recomputation') 'Test-only marker or cryptographic verification evidence was lost.'
    Assert-Test ($null -eq $mailbox.PSObject.Properties['status']) 'Mailbox parser must not produce a gate PASS status.'
    $checks.Add([ordered]@{name='mailbox-hmac-recomputed';status='PASS';note='Synthetic test-only response HMAC recomputed from the known LibraryOnly unit key before authenticated status was emitted.'})

    $lifecycleMetadata = [ordered]@{schema='combined-vm-lifecycle-metadata.v1';action='FinalVerify';outcomeStatus='FINAL_VERIFY_PASS';completedUtc=[DateTime]::UtcNow.ToString('o')}
    $lifecycleInput = New-TestMailboxFiles -Directory (Join-Path $testRoot 'mailbox-lifecycle') -Metadata $lifecycleMetadata
    $lifecycleInput.Capture.expectedSchema='combined-vm-lifecycle-metadata.v1'
    $lifecycleMailbox = Get-CombinedAssemblyMailbox -Capture $lifecycleInput.Capture -Label 'unit-lifecycle' -OutputDirectory $testRoot -ReportRoots @($reportsRoot) -ArtifactRoots @((Join-Path $WorktreeRoot 'spoof-handoff\artifacts')) -RepositoryRoot $RepositoryRoot
    Assert-Test ($lifecycleMailbox.metadata.action -ceq 'FinalVerify' -and $lifecycleMailbox.metadata.outcomeStatus -ceq 'FINAL_VERIFY_PASS' -and $null -eq (Get-CombinedAssemblyValue $lifecycleMailbox.metadata 'status')) 'Lifecycle metadata must use outcomeStatus without requiring a reader-phase status field.'
    $checks.Add([ordered]@{name='lifecycle-outcome-status';status='PASS';note='Lifecycle metadata with FinalVerify outcomeStatus and no root status field was accepted.'})

    $nestedDirectory = Join-Path $testRoot 'nested-valid'
    $nested = @(Get-CombinedAssemblyNestedBundle -Metadata $mailbox.metadata -Label 'unit-nested-valid' -OutputDirectory $nestedDirectory)
    Assert-Test ($nested.Count -eq 1 -and $nested[0].sha256 -ceq $mailboxInput.InnerRecord.sha256) 'Nested raw bundle leaf did not preserve its pinned digest.'
    $checks.Add([ordered]@{name='nested-archive-roundtrip';status='PASS';note='One test-only nested member matched capturedFiles size and SHA.'})

    $badMetadata = $mailbox.metadata | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $badMetadata.rawEvidenceBundle.sha256 = ('0' * 64)
    $nestedRejected = $false
    try { $null = Get-CombinedAssemblyNestedBundle -Metadata $badMetadata -Label 'unit-nested-tamper' -OutputDirectory (Join-Path $testRoot 'nested-tampered') } catch { $nestedRejected = $_.Exception.Message -match 'SHA-256 mismatch' }
    Assert-Test $nestedRejected 'Tampered nested bundle SHA was not rejected.'
    $checks.Add([ordered]@{name='nested-digest-tamper';status='PASS';note='Tampered nested archive digest was rejected.'})

    $badCapture = $mailboxInput.Capture | ConvertTo-Json -Depth 10 | ConvertFrom-Json
    $badCapture.responseMetadata.sha256 = ('0' * 64)
    $receiptRejected = $false
    try { $null = Get-CombinedAssemblyMailbox -Capture $badCapture -Label 'unit-receipt-tamper' -OutputDirectory (Join-Path $testRoot 'bad-receipt') -ReportRoots @($reportsRoot) -ArtifactRoots @((Join-Path $WorktreeRoot 'spoof-handoff\artifacts')) -RepositoryRoot $RepositoryRoot } catch { $receiptRejected = $_.Exception.Message -match 'SHA-256 mismatch' }
    Assert-Test $receiptRejected 'Response metadata digest tampering was not rejected.'
    $checks.Add([ordered]@{name='response-metadata-tamper';status='PASS';note='Tampered response metadata reference hash was rejected.'})

    $badHmacCapture = $mailboxInput.Capture | ConvertTo-Json -Depth 10 | ConvertFrom-Json
    $badHmacCapture.response = [ordered]@{path=$mailboxInput.Capture.response.path;sha256=$mailboxInput.Capture.response.sha256}
    $responseObject = Get-Content -LiteralPath $mailboxInput.Capture.response.path -Raw | ConvertFrom-Json
    $responseObject.ResponseHmacSha256 = 'B2' * 32
    $badResponsePath = Join-Path $testRoot 'response-hmac-mismatch.json'
    [IO.File]::WriteAllText($badResponsePath,($responseObject | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    $badHmacCapture.response.path = $badResponsePath
    $badHmacCapture.response.sha256 = Get-CombinedAssemblySha256 $badResponsePath
    $hmacRejected = $false
    try { $null = Get-CombinedAssemblyMailbox -Capture $badHmacCapture -Label 'unit-hmac-tamper' -OutputDirectory (Join-Path $testRoot 'bad-hmac') -ReportRoots @($reportsRoot) -ArtifactRoots @((Join-Path $WorktreeRoot 'spoof-handoff\artifacts')) -RepositoryRoot $RepositoryRoot } catch { $hmacRejected = $_.Exception.Message -match 'cryptographic HMAC verification failed' }
    Assert-Test $hmacRejected 'Response HMAC mismatch was not cryptographically rejected.'
    $checks.Add([ordered]@{name='response-hmac-mismatch';status='PASS';note='A response with a modified HMAC was rejected by recomputation with the known unit key.'})

    $forgedCapture = $mailboxInput.Capture | ConvertTo-Json -Depth 10 | ConvertFrom-Json
    $forgedResponse = Get-Content -LiteralPath $mailboxInput.Capture.response.path -Raw | ConvertFrom-Json
    $forgedResponse.ResponseHmacSha256 = 'A1' * 32
    $forgedResponsePath = Join-Path $testRoot 'response-forged-matching-hmac.json'
    [IO.File]::WriteAllText($forgedResponsePath,($forgedResponse | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    $forgedCapture.response.path = $forgedResponsePath
    $forgedCapture.response.sha256 = Get-CombinedAssemblySha256 $forgedResponsePath
    $forgedReceipt = Get-Content -LiteralPath $mailboxInput.Capture.responseMetadata.path -Raw | ConvertFrom-Json
    $forgedReceipt.ResponseHmacSha256 = $forgedResponse.ResponseHmacSha256
    $forgedReceiptPath = Join-Path $testRoot 'receipt-forged-matching-hmac.json'
    [IO.File]::WriteAllText($forgedReceiptPath,($forgedReceipt | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    $forgedCapture.responseMetadata.path = $forgedReceiptPath
    $forgedCapture.responseMetadata.sha256 = Get-CombinedAssemblySha256 $forgedReceiptPath
    $matchingForgeryRejected = $false
    try { $null = Get-CombinedAssemblyMailbox -Capture $forgedCapture -Label 'unit-forged-matching-hmac' -OutputDirectory (Join-Path $testRoot 'bad-forged-hmac') -ReportRoots @($reportsRoot) -ArtifactRoots @((Join-Path $WorktreeRoot 'spoof-handoff\artifacts')) -RepositoryRoot $RepositoryRoot } catch { $matchingForgeryRejected = $_.Exception.Message -match 'cryptographic HMAC verification failed' }
    Assert-Test $matchingForgeryRejected 'Forged response and receipt with a matching arbitrary HMAC were accepted.'
    $checks.Add([ordered]@{name='matching-forged-hmac-rejected';status='PASS';note='A forged response plus matching forged receipt HMAC was rejected by cryptographic recomputation.'})

    $badTransportCapture = $mailboxInput.Capture | ConvertTo-Json -Depth 10 | ConvertFrom-Json
    $transportObject = Get-Content -LiteralPath $mailboxInput.Capture.transportMetadata.path -Raw | ConvertFrom-Json
    $transportObject.ResultStatus = 'FAIL'
    $badTransportPath = Join-Path $testRoot 'transport-status-fail.json'
    [IO.File]::WriteAllText($badTransportPath,($transportObject | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    $badTransportCapture.transportMetadata.path = $badTransportPath
    $badTransportCapture.transportMetadata.sha256 = Get-CombinedAssemblySha256 $badTransportPath
    $transportRejected = $false
    try { $null = Get-CombinedAssemblyMailbox -Capture $badTransportCapture -Label 'unit-transport-fail' -OutputDirectory (Join-Path $testRoot 'bad-transport') -ReportRoots @($reportsRoot) -ArtifactRoots @((Join-Path $WorktreeRoot 'spoof-handoff\artifacts')) -RepositoryRoot $RepositoryRoot } catch { $transportRejected = $_.Exception.Message -match 'transport metadata does not bind' }
    Assert-Test $transportRejected 'Outer transport metadata with failed result status must be rejected.'
    $checks.Add([ordered]@{name='transport-status-failure';status='PASS';note='Outer transport metadata that reports FAIL was rejected.'})

    $outsideRejected = $false
    try { $null = Assert-CombinedAssemblyNoReparsePath -Root $reportsRoot -Path $WorktreeRoot -Label 'unit boundary' } catch { $outsideRejected = $_.Exception.Message -match 'must remain below' }
    Assert-Test $outsideRejected 'Evidence root escape was not rejected.'
    $checks.Add([ordered]@{name='output-root-boundary';status='PASS';note='Path outside the assigned reports root was rejected.'})

    $nicFixture = New-TestActiveNicRestoreFixture -Directory (Join-Path $testRoot 'active-nic-restore')
    $nicRestore = Assert-CombinedAssemblyActiveNicRestore -Metadata $nicFixture.Metadata -Members $nicFixture.Members -FixtureMapPath $nicFixture.MapPath
    Assert-Test ($nicRestore.status -ceq 'PASS' -and $nicRestore.adapterCount -eq 4 -and $nicRestore.readerCount -eq 7) 'Active phase must prove all four original MACs through the seven ordinary readers and OID.'
    $checks.Add([ordered]@{name='active-nic-restore-positive';status='PASS';note='Synthetic test fixture only: all four adapter originals matched seven ordinary reader paths and direct OID.'})

    $activePhaseResultShape = [pscustomobject][ordered]@{phase=[ordered]@{nicRestoreEvidence=[ordered]@{status='PASS';adapterCount=4}}}
    $activePhaseNicEvidence = Get-CombinedAssemblyActiveNicRestoreEvidence -ActivePhaseResult $activePhaseResultShape
    Assert-Test ($activePhaseNicEvidence.status -ceq 'PASS' -and $activePhaseNicEvidence.adapterCount -eq 4) 'Validated active NIC evidence was not read from the production phase result shape.'
    $legacyRootEvidenceShapeRejected = $false
    try { $null = Get-CombinedAssemblyActiveNicRestoreEvidence -ActivePhaseResult ([pscustomobject]@{nicRestoreEvidence=[pscustomobject]@{status='PASS'}}) } catch { $legacyRootEvidenceShapeRejected = $_.Exception.Message -match 'active NIC restore evidence did not pass' }
    Assert-Test $legacyRootEvidenceShapeRejected 'A NIC evidence property at the wrong phase-result level was accepted.'
    $checks.Add([ordered]@{name='active-nic-phase-result-shape';status='PASS';note='The consumer reads validated NIC evidence from Get-CombinedAssemblyPhase.phase and rejects a top-level fallback.'})

    $validNicMapRows = @(Import-Csv -LiteralPath $nicFixture.MapPath -Delimiter "`t")
    $nicMapNegativeCases = @(
        [pscustomobject]@{name='wrong-member';rows=@($validNicMapRows | ForEach-Object { $_.PSObject.Copy() });pattern='member=FakeMacAddress'},
        [pscustomobject]@{name='wrong-device-prefix';rows=@($validNicMapRows | ForEach-Object { $_.PSObject.Copy() });pattern='device=adapter:'},
        [pscustomobject]@{name='missing-adapter';rows=@($validNicMapRows[0..2]);pattern='exactly four VM adapters'},
        [pscustomobject]@{name='extra-adapter';rows=@($validNicMapRows) + @([pscustomobject]@{member='FakeMacAddress';device='adapter:9|ROOT\NET\0002';original='00:50:56:C0:00:09';replacement='02:90:10:20:30:49'});pattern='exactly four VM adapters'},
        [pscustomobject]@{name='duplicate-pnp';rows=@($validNicMapRows | ForEach-Object { $_.PSObject.Copy() });pattern='duplicate adapter interface index or PnP identity'}
    )
    $nicMapNegativeCases[0].rows[0].member = 'adapter:2'
    $nicMapNegativeCases[1].rows[0].device = 'virtual:2|ROOT\NET\0000'
    $nicMapNegativeCases[4].rows[3].device = 'adapter:7|ROOT\NET\0000'
    foreach ($mapCase in $nicMapNegativeCases) {
        $caseDirectory = Join-Path $testRoot ('active-nic-map-' + $mapCase.name)
        [void][IO.Directory]::CreateDirectory($caseDirectory)
        $caseMapPath = Join-Path $caseDirectory 'vm-fixture-map.tsv'
        Write-TestFixtureMap -Path $caseMapPath -Rows $mapCase.rows
        $mapRejected = $false
        try { $null = Assert-CombinedAssemblyActiveNicRestore -Metadata $nicFixture.Metadata -Members $nicFixture.Members -FixtureMapPath $caseMapPath }
        catch { $mapRejected = $_.Exception.Message -match [string]$mapCase.pattern }
        Assert-Test $mapRejected ("Malformed NIC fixture map was accepted or rejected for an unexpected reason: " + [string]$mapCase.name)
        $checks.Add([ordered]@{name=('active-nic-map-' + [string]$mapCase.name);status='PASS';note='Malformed production-schema adapter rows were rejected before they could weaken the four-adapter originals proof.'})
    }

    $badNicReport = Get-Content -LiteralPath $nicFixture.ReportPath -Raw | ConvertFrom-Json
    $badNicReport.lockedOriginalEvaluations[0].observations.getAdaptersAddresses[0].value='02:90:10:20:30:40'
    $badNicPath = Join-Path $nicFixture.Directory 'nic-original-readback-tampered.json'
    [IO.File]::WriteAllText($badNicPath,($badNicReport | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
    $badNicSha=Get-CombinedAssemblySha256 $badNicPath
    $badNicMetadata=$nicFixture.Metadata | ConvertTo-Json -Depth 30 | ConvertFrom-Json
    $badNicMetadata.nicOriginalReadback.path=$badNicPath
    $badNicMetadata.nicOriginalReadback.sha256=$badNicSha
    $badNicMetadata.nicOriginalReadback.content=$badNicReport
    $badNicMembers=@([pscustomobject][ordered]@{guestPath=$badNicPath;sha256=$badNicSha;path=$badNicPath})
    $badNicRejected=$false
    try { $null=Assert-CombinedAssemblyActiveNicRestore -Metadata $badNicMetadata -Members $badNicMembers -FixtureMapPath $nicFixture.MapPath } catch { $badNicRejected=$_.Exception.Message -match 'Active NIC original mismatch' }
    Assert-Test $badNicRejected 'An active NIC reader that returns a replacement after restore must be rejected.'
    $checks.Add([ordered]@{name='active-nic-restore-mismatch';status='PASS';note='A single ordinary reader retaining a replacement after restore was rejected.'})

    $coldFixture = New-TestColdRestoreFixture -Directory (Join-Path $testRoot 'cold-receipt-valid')
    [void][IO.Directory]::CreateDirectory($coldFixture.Directory)
    $coldPass = Assert-CombinedAssemblyFinalLifecycle -Mailbox $coldFixture.Mailbox -ColdRestore $coldFixture.Cold -OriginalPhase $coldFixture.Original -RestoredPhase $coldFixture.Restored -ReportRoots @($testRoot)
    Assert-Test ($coldPass.status -ceq 'PASS' -and $coldPass.snapshotId -ceq 'Combined-Clean4-20261018') 'Valid FinalVerify plus ordered cold snapshot restore should pass the local receipt validator.'
    Assert-Test ($coldPass.bcdExportEvidence.byteIdentical -eq $false -and $coldPass.bcdExportEvidence.baselineSha256 -cne $coldPass.bcdExportEvidence.finalSha256 -and $coldPass.bcdExportEvidence.classification -match 'evidence-only') 'Different raw BCD export hashes must be reported as evidence without failing matching BCD semantics.'
    $checks.Add([ordered]@{name='cold-receipt-positive';status='PASS';note='Synthetic schema fixture only: exact snapshot/list/restore/start ordering and distinct final/cold boot identities validated; no runtime evidence was emitted.'})

    Assert-ColdLifecycleRejected -Name 'secureboot-baseline-enabled-drift' -ExpectedError 'Secure Boot state does not match' -Mutator { param($m,$o,$r) $m.result.result.secureBoot.enabled=$true }
    Assert-ColdLifecycleRejected -Name 'secureboot-variable-bytes-drift' -ExpectedError 'SecureBoot variable bytesHex differs' -Mutator { param($m,$o,$r) $m.result.result.secureBoot.variable.bytesHex='01'; $m.result.result.secureBoot.variable.bytesBase64='AQ==' }
    Assert-ColdLifecycleRejected -Name 'secureboot-variable-attributes-drift' -ExpectedError 'SecureBoot variable attributes differs' -Mutator { param($m,$o,$r) $m.result.result.secureBoot.variable.attributes=7 }
    Assert-ColdLifecycleRejected -Name 'original-phase-policy-drift' -ExpectedError 'original phase Secure Boot state differs' -Mutator { param($m,$o,$r) $o.metadata.secureBoot=$true }
    Assert-ColdLifecycleRejected -Name 'cold-restored-policy-drift' -ExpectedError 'cold-restored phase Code Integrity state differs' -Mutator { param($m,$o,$r) $r.metadata.codeIntegrity.options=7 }
    Assert-ColdLifecycleRejected -Name 'test-signing-still-active' -ExpectedError 'BCD test-signing element differs' -Mutator { param($m,$o,$r) $m.result.result.bcdTestSigning.exists=$true; $m.result.result.bcdTestSigning.value='Yes' }
    Assert-ColdLifecycleRejected -Name 'effective-test-policy-still-active' -ExpectedError 'still has test-signing enabled' -Mutator { param($m,$o,$r) $m.result.result.effectiveCodeIntegrity.options=7 }
    Assert-ColdLifecycleRejected -Name 'bcd-semantics-drift' -ExpectedError 'BCD semantic restoration' -Mutator { param($m,$o,$r) $m.result.result.bcdNonTestSigningPropertiesIdentical=$false }
    Assert-ColdLifecycleRejected -Name 'bcd-evidence-only-classification-missing' -ExpectedError 'BCD semantic restoration' -Mutator { param($m,$o,$r) $m.result.result.bcdExportByteIdentityIsEvidenceOnly=$false }
    Assert-ColdLifecycleRejected -Name 'bcd-export-hash-claim-inconsistent' -ExpectedError 'BCD semantic restoration' -Mutator { param($m,$o,$r) $m.journal.finalSnapshot.bcdExportSha256=$m.journal.bcdExportSha256 }

    $lateFinal = $coldFixture.Mailbox.metadata | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $lateFinal.completedUtc='2026-10-18T10:03:30.0000000Z'
    $lateFinalMailbox=[pscustomobject]@{metadata=$lateFinal}
    $lateRejected=$false
    try { $null=Assert-CombinedAssemblyFinalLifecycle -Mailbox $lateFinalMailbox -ColdRestore $coldFixture.Cold -OriginalPhase $coldFixture.Original -RestoredPhase $coldFixture.Restored -ReportRoots @($testRoot) } catch { $lateRejected=$_.Exception.Message -match 'timestamps are not ordered' }
    Assert-Test $lateRejected 'FinalVerify completion after snapshot restore start must be rejected.'
    $checks.Add([ordered]@{name='cold-receipt-finalverify-order';status='PASS';note='FinalVerify after revertToSnapshot start was rejected.'})

    $wrongSnapshot=$coldFixture.Cold | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $wrongSnapshot.restore.command=$wrongSnapshot.restore.command.Replace('Combined-Clean4-20261018','Clean4')
    $wrongSnapshotRejected=$false
    try { $null=Assert-CombinedAssemblyFinalLifecycle -Mailbox $coldFixture.Mailbox -ColdRestore $wrongSnapshot -OriginalPhase $coldFixture.Original -RestoredPhase $coldFixture.Restored -ReportRoots @($testRoot) } catch { $wrongSnapshotRejected=$_.Exception.Message -match 'exact recorded snapshot id' }
    Assert-Test $wrongSnapshotRejected 'Restore command targeting a different snapshot must be rejected.'
    $checks.Add([ordered]@{name='cold-receipt-snapshot-target';status='PASS';note='Restore command with a different snapshot id was rejected.'})

    $bootMismatch=$coldFixture.Cold | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $bootMismatch.bootIdentity.lastBootUpTimeUtc='2026-10-18T10:08:00.0000000Z'
    $bootMismatchRejected=$false
    try { $null=Assert-CombinedAssemblyFinalLifecycle -Mailbox $coldFixture.Mailbox -ColdRestore $bootMismatch -OriginalPhase $coldFixture.Original -RestoredPhase $coldFixture.Restored -ReportRoots @($testRoot) } catch { $bootMismatchRejected=$_.Exception.Message -match 'boot identity' }
    Assert-Test $bootMismatchRejected 'Cold restore boot identity differing from the authenticated restored matrix must be rejected.'
    $checks.Add([ordered]@{name='cold-receipt-boot-binding';status='PASS';note='Snapshot receipt boot identity not matching the authenticated restored phase was rejected.'})

    $report = [ordered]@{schema='combined-vm-gate-assembler-tests.v1';status='PASS';syntheticRuntimeProofProduced=$false;testCount=$checks.Count;checks=@($checks.ToArray());completedUtc=[DateTime]::UtcNow.ToString('o')}
    $reportPath = Join-Path $reportsRoot ('combined-vm-gate-assembler-tests.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N') + '.json')
    [IO.File]::WriteAllText($reportPath,($report | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    $reportSha = Get-CombinedAssemblySha256 $reportPath
    [pscustomobject]@{status='PASS';tests=$checks.Count;syntheticRuntimeProofProduced=$false;reportPath=$reportPath;reportSha256=$reportSha} | ConvertTo-Json -Depth 5
} finally {
    if ($null -ne $unitMailboxKey) { [Array]::Clear($unitMailboxKey,0,$unitMailboxKey.Length); $unitMailboxKey=$null }
    if ($null -ne $script:CombinedAssemblyUnitTestMailboxKey) { [Array]::Clear($script:CombinedAssemblyUnitTestMailboxKey,0,$script:CombinedAssemblyUnitTestMailboxKey.Length); $script:CombinedAssemblyUnitTestMailboxKey=$null }
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue }
}
