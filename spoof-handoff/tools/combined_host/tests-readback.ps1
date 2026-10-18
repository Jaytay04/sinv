$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot 'model.psm1') -Force
. (Join-Path $PSScriptRoot 'readback.ps1')

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$mapPath = Join-Path $repositoryRoot 'spoof-handoff\reports\client-originals.tsv'
$rows = Get-CombinedMapRows $mapPath
$bindingMap = New-CombinedBindingMap $rows
$testsPassed = 0
$unitArtifactBase = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'spoof-handoff\artifacts\combined-readback-unit')).TrimEnd('\') + '\'
$unitArtifactRoot = Join-Path $unitArtifactBase ([Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($unitArtifactRoot)

function Assert-CombinedTest {
    param([bool]$Condition,[string]$Message)
    if (-not $Condition) { throw "FAIL: $Message" }
    $script:testsPassed++
}

function Assert-CombinedThrows {
    param([scriptblock]$Action,[string]$Message)
    $threw = $false
    try { & $Action } catch { $threw = $true }
    Assert-CombinedTest $threw $Message
}

function Get-TestValue {
    param([AllowNull()][object]$Object,[string]$Name)
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function New-TestPoll {
    param([object]$Bindings,[string]$Mode,[string]$BootTime)
    $smbiosFields = [ordered]@{}
    foreach ($binding in @($Bindings.bindings | Where-Object category -ceq 'SMBIOS')) {
        $value = if ($Mode -ceq 'Replacement') { $binding.replacement } else { $binding.original }
        $smbiosFields[$binding.field] = [pscustomobject]@{ observedNative=$value; observedWmi=$value }
    }
    $disk = @($Bindings.bindings | Where-Object category -ceq 'DISK')[0]
    $diskValue = if ($Mode -ceq 'Replacement') { $disk.replacement } else { $disk.original }
    $nicRows = [System.Collections.Generic.List[object]]::new()
    foreach ($binding in @($Bindings.bindings | Where-Object category -ceq 'NIC')) {
        $value = if ($Mode -ceq 'Replacement') { $binding.replacement } else { $binding.original }
        $observations = [ordered]@{}
        foreach ($reader in @('ndisOid8023CurrentAddress','getAdaptersAddresses','getAdaptersInfoAddress','getIfTable2','win32NetworkAdapter','getNetAdapter')) {
            $observations[$reader] = [pscustomobject]@{ value=$value }
        }
        $observations.msftNetAdapterNetworkAddresses = @([pscustomobject]@{ value=$value })
        $nicRows.Add([pscustomobject]@{ pnpDeviceId=$binding.pnpDeviceId; expectedMode=$Mode; observations=[pscustomobject]$observations })
    }
    return [pscustomobject]@{
        windowsLastBootUpTimeUtcStart = $BootTime
        windowsLastBootUpTimeUtcEnd = $BootTime
        smbios = [pscustomobject]@{ schema='smbios-windows-capture.v1'; fields=[pscustomobject]$smbiosFields }
        storage = [pscustomobject]@{ nativeSerial=$diskValue; wmiSerial=$diskValue }
        nic = [pscustomobject]@{ schema='nic-native-wmi-readback.v1'; expectedMode=$Mode; failures=@(); allLockedExpectedValuesMatchReadOnlySources=$true; lockedOriginalEvaluations=@($nicRows.ToArray()) }
        evidence = [pscustomobject]@{ poll=$BootTime }
        acquisitionErrors = @()
    }
}

$smbiosBindings = @($bindingMap.bindings | Where-Object category -ceq 'SMBIOS')
$diskBindings = @($bindingMap.bindings | Where-Object category -ceq 'DISK')
$nicBindings = @($bindingMap.bindings | Where-Object category -ceq 'NIC')
Assert-CombinedTest ($bindingMap.bindings.Count -eq 12) 'map resolves to exactly 12 confirmed bindings'
Assert-CombinedTest ($smbiosBindings.Count -eq 7) 'map contains seven SMBIOS bindings'
Assert-CombinedTest ($diskBindings.Count -eq 1 -and $diskBindings[0].device -match 'PHYSICALDRIVE0$') 'map contains only the locked PHYSICALDRIVE0 disk binding'
Assert-CombinedTest ($nicBindings.Count -eq 4) 'map contains all four locked NIC bindings'
$lockedReplacements = @{
    'SMBIOS:system_serial' = '4091-37'
    'SMBIOS:system_vendor' = 'NORTHLAB'
    'SMBIOS:system_product' = 'NODE'
    'SMBIOS:board_serial' = '08E4821_MD2F297027'
    'SMBIOS:board_vendor' = 'Example Devices Corporation'
    'SMBIOS:board_product' = 'Client Reference Board (CB-4200)'
    'SMBIOS:system_uuid' = 'A17F24D0-68E2-4A09-9C31-2B7D8450E6F9'
    'DISK:PHYSICALDRIVE0' = '1111_1111_1111_1111_A1B2_C3D4_5566_7788.'
    'NIC:adapter:0|ROOT\VMWARE\0001' = '02:90:10:20:30:40'
    'NIC:adapter:3|ROOT\VMWARE\0000' = '02:90:10:20:30:41'
    'NIC:adapter:4|PCI\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\3&11583659&1&A3' = '02:90:10:20:30:42'
    'NIC:adapter:5|PCI\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\4&3FA6681&0&00E2' = '02:90:10:20:30:43'
}
foreach ($binding in $bindingMap.bindings) {
    Assert-CombinedTest ($binding.replacement -ceq $lockedReplacements[$binding.id]) ("locked replacement retained for " + $binding.id)
}
Assert-CombinedTest ($bindingMap.excludedDiskTargets.Count -eq 0 -or @($bindingMap.excludedDiskTargets | Where-Object { $_.reason -match 'Outside requested PHYSICALDRIVE0 scope' }).Count -eq $bindingMap.excludedDiskTargets.Count) 'other disk targets are explicitly outside scope'
# A synthetic extra disk row exercises exclusion without adding a device target.
$selectedDiskRow = @($rows | Where-Object { $_.member -ceq 'FakeDiskSerial' -and $_.device -match 'PHYSICALDRIVE0\s*$' })[0]
$usbScopeMap = New-CombinedBindingMap (@($rows) + @([pscustomobject]@{
    member='FakeDiskSerial'; device='unit USB disk|\\.\PHYSICALDRIVE1'
    original=$selectedDiskRow.original; replacement=$selectedDiskRow.replacement
}))
Assert-CombinedTest ($usbScopeMap.bindings.Count -eq 12 -and @($usbScopeMap.bindings | Where-Object category -ceq 'DISK').Count -eq 1 -and $usbScopeMap.bindings[7].id -ceq 'DISK:PHYSICALDRIVE0') 'an extra USB disk row cannot expand the selected disk scope'
Assert-CombinedTest (@($usbScopeMap.excludedDiskTargets | Where-Object device -ceq 'unit USB disk|\\.\PHYSICALDRIVE1').Count -eq 1) 'an extra USB disk is recorded as excluded'

Assert-CombinedThrows { New-CombinedBindingMap @($rows | Where-Object { $_.member -cne 'FakeSMBIOSUUID' }) } 'missing required SMBIOS field is rejected'
Assert-CombinedThrows { New-CombinedBindingMap @($rows + $rows[0]) } 'duplicate binding row is rejected'
$duplicateNic = @($rows) + @($rows | Where-Object member -ceq 'FakeMacAddress' | Select-Object -First 1)
Assert-CombinedThrows { New-CombinedBindingMap $duplicateNic } 'duplicate NIC target row is rejected'
$unknownSmbios = @($rows) + @([pscustomobject]@{ member='FakeSMBIOSCpuSerial'; device='SMBIOS Type 4'; original=''; replacement='' })
Assert-CombinedThrows { New-CombinedBindingMap $unknownSmbios } 'an unconfirmed SMBIOS binding is rejected'

$bootTime = '2026-10-18T01:00:00.0000000Z'
$first = New-TestPoll $bindingMap 'Replacement' $bootTime
$second = New-TestPoll $bindingMap 'Replacement' $bootTime
$probe = [pscustomobject]@{ match=$true; removedAfterRead=$true; ownedFileCreated=$true; lengthMatches=$true; writeThrough=$true; identifierSource=$false; bytes=4096; error=$null; expectedSha256=('A' * 64); actualSha256=('A' * 64) }
$model = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($first,$second) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
if ($model.status -cne 'PASS') { $model.failures | ConvertTo-Json -Depth 8 | Write-Output }
Assert-CombinedTest ($model.status -ceq 'PASS') 'two replacement polls pass when every actual observation matches'
Assert-CombinedTest ($model.simultaneousReadbackPass -eq $true -and $model.bootIdentity.sameBoot -eq $true) 'all active bindings are proven in one Windows boot'
Assert-CombinedTest ($model.bootIdentity.windowsLastBootUpTimeUtc -ceq $bootTime) 'canonical boot identity is exposed for gate integration'
Assert-CombinedTest ($model.stableAcrossPolls -eq $true) 'file probe is bracketed by stable full reader polls'
Assert-CombinedTest (@($model.polls[0].bindings).Count -eq 12 -and @($model.polls[1].bindings).Count -eq 12) 'both polls independently cover all 12 bindings'
Assert-CombinedTest (@($model.polls[0].bindings | Where-Object { -not $_.pass }).Count -eq 0) 'every field passes its readers in the first poll'

$unknownMap = $bindingMap | ConvertTo-Json -Depth 12 | ConvertFrom-Json
foreach ($binding in $unknownMap.bindings) { $binding.category='UNKNOWN'; $binding.readers=@() }
$unknownModel = New-CombinedReadbackModel -BindingMap $unknownMap -Polls @($first,$second) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
Assert-CombinedTest ($unknownModel.status -ceq 'FAIL' -and @($unknownModel.failures | Where-Object reason -ceq 'binding_map_shape').Count -eq 1) 'unknown binding categories cannot pass an empty matrix'
Assert-CombinedTest (@($unknownModel.polls[0].bindings | Where-Object pass -eq $true).Count -eq 0) 'bindings without observations do not claim individual passes'
$missingReaderMap = $bindingMap | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$missingReaderMap.bindings[0].readers=@('rsmb')
$missingReaderModel = New-CombinedReadbackModel -BindingMap $missingReaderMap -Polls @($first,$second) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
Assert-CombinedTest ($missingReaderModel.status -ceq 'FAIL') 'a caller cannot reduce the required reader list'
$misclassifiedMap = $bindingMap | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$misclassifiedMap.bindings[0].field='board_serial'
$misclassifiedModel = New-CombinedReadbackModel -BindingMap $misclassifiedMap -Polls @($first,$second) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
Assert-CombinedTest ($misclassifiedModel.status -ceq 'FAIL') 'derived field substitutions are rejected independently of summary flags'
foreach ($defect in @('actualSha256','bytes','lengthMatches','ownedFileCreated','error','identifierSource')) {
    $forgedProbe = $probe | ConvertTo-Json | ConvertFrom-Json
    switch ($defect) {
        'actualSha256' { $forgedProbe.actualSha256=('B' * 64) }
        'bytes' { $forgedProbe.bytes=0 }
        'lengthMatches' { $forgedProbe.lengthMatches=$false }
        'ownedFileCreated' { $forgedProbe.ownedFileCreated=$false }
        'error' { $forgedProbe.error='probe failure retained despite forged match flag' }
        'identifierSource' { $forgedProbe.identifierSource=$true }
    }
    $forgedModel = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($first,$second) -ExpectedMode Replacement -FileIntegrityProbe $forgedProbe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
    Assert-CombinedTest ($forgedModel.status -ceq 'FAIL' -and @($forgedModel.failures | Where-Object reason -ceq 'file_integrity').Count -eq 1) ("forged file probe match cannot conceal invalid " + $defect)
}

$mismatch = New-TestPoll $bindingMap 'Replacement' $bootTime
$mismatch.storage.nativeSerial = 'WRONG_DISK_SERIAL'
$mismatchModel = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($mismatch,$second) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
Assert-CombinedTest ($mismatchModel.status -ceq 'FAIL') 'an actual storage IOCTL mismatch fails the combined model'
Assert-CombinedTest (@($mismatchModel.failures | Where-Object { (Get-TestValue $_ 'reader') -ceq 'storageIoctl' -and (Get-TestValue $_ 'expected') -ceq $diskBindings[0].replacement -and (Get-TestValue $_ 'actual') -ceq 'WRONG_DISK_SERIAL' }).Count -eq 1) 'mismatch records exact expected and actual values'

$missing = New-TestPoll $bindingMap 'Replacement' $bootTime
$missing.smbios.fields.system_serial = [pscustomobject]@{ observedNative=$null; observedWmi=$null }
$missingModel = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($missing,$second) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
Assert-CombinedTest ($missingModel.status -ceq 'FAIL') 'a missing reader value fails closed'
Assert-CombinedTest (@($missingModel.failures | Where-Object { (Get-TestValue $_ 'bindingId') -ceq 'SMBIOS:system_serial' -and (Get-TestValue $_ 'reader') -ceq 'rsmb' -and $null -eq (Get-TestValue $_ 'actual') }).Count -eq 1) 'missing readback is recorded with its field and reader'

$duplicateEval = New-TestPoll $bindingMap 'Replacement' $bootTime
$duplicateEval.nic.lockedOriginalEvaluations += $duplicateEval.nic.lockedOriginalEvaluations[0]
$duplicateModel = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($duplicateEval,$second) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
Assert-CombinedTest ($duplicateModel.status -ceq 'FAIL') 'duplicate NIC observations are rejected'
Assert-CombinedTest (@($duplicateModel.failures | Where-Object { (Get-TestValue $_ 'reason') -ceq 'nic_join_cardinality' }).Count -gt 0) 'duplicate NIC join cardinality is preserved as evidence'

$differentBoot = New-TestPoll $bindingMap 'Replacement' '2026-10-18T02:00:00.0000000Z'
$bootModel = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($first,$differentBoot) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc '2026-10-18T02:00:00.0000000Z' -FinalBootIdentityError $null
Assert-CombinedTest ($bootModel.status -ceq 'FAIL' -and $bootModel.bootIdentity.sameBoot -eq $false) 'polls from different Windows boots are rejected'
$intraPollBoot = New-TestPoll $bindingMap 'Replacement' $bootTime
$intraPollBoot.windowsLastBootUpTimeUtcEnd = '2026-10-18T02:00:00.0000000Z'
$intraPollModel = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($intraPollBoot,$first) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
Assert-CombinedTest ($intraPollModel.status -ceq 'FAIL' -and @($intraPollModel.failures | Where-Object { (Get-TestValue $_ 'reason') -ceq 'boot_identity_changed_during_poll' }).Count -eq 1) 'boot changes within a poll are rejected'
$finalBootModel = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($first,$second) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc '2026-10-18T02:00:00.0000000Z' -FinalBootIdentityError $null
Assert-CombinedTest ($finalBootModel.status -ceq 'FAIL') 'boot changes after the second poll are rejected by the final checkpoint'

$sourceDrift = [pscustomobject]@{ name='nicReaderScript'; path='nic/read-host.ps1'; beforePollSha256=('A' * 64); betweenPollsSha256=('B' * 64); afterPollSha256=('A' * 64) }
$sourceDriftModel = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($first,$second) -ExpectedMode Replacement -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null -SourcePins @($sourceDrift)
Assert-CombinedTest ($sourceDriftModel.status -ceq 'FAIL' -and @($sourceDriftModel.failures | Where-Object { (Get-TestValue $_ 'reason') -ceq 'source_pin_drift' }).Count -eq 1) 'readback source drift between polls fails closed'

$badProbe = [pscustomobject]@{ match=$false; removedAfterRead=$true; expectedSha256=('C' * 64); actualSha256=('D' * 64) }
$probeModel = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($first,$second) -ExpectedMode Replacement -FileIntegrityProbe $badProbe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
Assert-CombinedTest ($probeModel.status -ceq 'FAIL') 'file integrity probe failure blocks a pass'

$originalFirst = New-TestPoll $bindingMap 'Original' $bootTime
$originalSecond = New-TestPoll $bindingMap 'Original' $bootTime
$originalModel = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($originalFirst,$originalSecond) -ExpectedMode Original -FileIntegrityProbe $probe -LockedMapPath $mapPath -LockedMapSha256 ('B' * 64) -CaptureStartedUtc $bootTime -CaptureEndedUtc $bootTime -FinalBootTimeUtc $bootTime -FinalBootIdentityError $null
Assert-CombinedTest ($originalModel.status -ceq 'PASS') 'the same reader model verifies fresh original values'

$sentinelPath = Join-Path $unitArtifactRoot 'preexisting.bin'
$sentinelBytes = [Text.Encoding]::UTF8.GetBytes('preserve-this-preexisting-probe-target')
[IO.File]::WriteAllBytes($sentinelPath,$sentinelBytes)
try {
    $collisionProbe = Invoke-CombinedFileIntegrityProbe $sentinelPath
    $afterCollision = [IO.File]::ReadAllBytes($sentinelPath)
    Assert-CombinedTest ($collisionProbe.match -eq $false -and $collisionProbe.ownedFileCreated -eq $false) 'probe refuses a preexisting target without claiming it'
    Assert-CombinedTest ($collisionProbe.preexistingPathPreserved -eq $true -and $collisionProbe.removedAfterRead -eq $false) 'probe does not remove a preexisting path'
    Assert-CombinedTest ([Convert]::ToBase64String($afterCollision) -ceq [Convert]::ToBase64String($sentinelBytes)) 'preexisting target bytes remain unchanged'
}
finally { Remove-Item -LiteralPath $sentinelPath -Force -ErrorAction SilentlyContinue }
$ownedProbePath = Join-Path $unitArtifactRoot 'owned.bin'
$ownedProbe = Invoke-CombinedFileIntegrityProbe $ownedProbePath
Assert-CombinedTest ($ownedProbe.match -eq $true -and $ownedProbe.ownedFileCreated -eq $true) 'successful integrity probe verifies only its own new file'
Assert-CombinedTest ($ownedProbe.removedAfterRead -eq $true -and -not (Test-Path -LiteralPath $ownedProbePath)) 'successful integrity probe removes its owned file'

$shortRunDirectory = Join-Path ([IO.Path]::GetTempPath()) ('c.' + [Guid]::NewGuid().ToString('N'))
$shortPreflight = Get-CombinedNativePathPreflight $shortRunDirectory
Assert-CombinedTest ($shortPreflight.withinLimit -eq $true -and $shortPreflight.maximumPathLength -le $shortPreflight.limit) 'short output roots fit the unchanged native report writer'
$longOutputLeaf = 'long-' + [Guid]::NewGuid().ToString('N') + '-' + ('x' * 120)
$longRelativeOutput = 'spoof-handoff\reports\combined-host-20261018\' + $longOutputLeaf
$longOutputFullPath = [IO.Path]::GetFullPath((Join-Path $repositoryRoot $longRelativeOutput))
$fakeReaderRoot = Join-Path $unitArtifactRoot 'preflight'
[void][IO.Directory]::CreateDirectory($fakeReaderRoot)
$fakeReaderPath = Join-Path $fakeReaderRoot 'reader.ps1'
$fakeNativePath = Join-Path $fakeReaderRoot 'reader.exe'
$readerSentinelPath = Join-Path $fakeReaderRoot 'reader-invoked.txt'
[IO.File]::WriteAllText($fakeReaderPath, "[IO.File]::WriteAllText('$readerSentinelPath','invoked')", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllBytes($fakeNativePath,[byte[]]@())
$preflightFailure = $null
try {
    Invoke-CombinedReadback -RepositoryRoot $repositoryRoot -OutputDirectory $longRelativeOutput -ExpectedMode Original -SmbiosReaderPath $fakeReaderPath -NicReaderPath $fakeReaderPath -NicNativeExePath $fakeNativePath -StorageReaderPath $fakeReaderPath -LockedMapPath $mapPath | Out-Null
} catch { $preflightFailure = $_.Exception.Message }
Assert-CombinedTest ($preflightFailure -match 'output root is too long.*before invoking any reader') 'overlong report roots fail during preflight'
Assert-CombinedTest (-not (Test-Path -LiteralPath $readerSentinelPath) -and -not (Test-Path -LiteralPath $longOutputFullPath)) 'overlong-root rejection happens before any reader or output directory is created'
$resolvedUnitArtifactRoot = [IO.Path]::GetFullPath($unitArtifactRoot)
if (-not $resolvedUnitArtifactRoot.StartsWith($unitArtifactBase,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unit fixture cleanup escaped its artifact directory.' }
Remove-Item -LiteralPath $resolvedUnitArtifactRoot -Force -Recurse -ErrorAction Stop

Write-Output ('PASS: ' + $testsPassed + ' combined readback model checks; no device, driver, firmware, or VM operations were performed.')
