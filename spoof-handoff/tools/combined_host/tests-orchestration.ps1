$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$worktreeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $worktreeRoot '..\..\..'))
$implementation = Join-Path $PSScriptRoot 'run.ps1'
$mapPath = Join-Path $repositoryRoot 'spoof-handoff\reports\client-originals.tsv'
$script:canonicalMapPath = $mapPath
$reportRoot = Join-Path $worktreeRoot 'spoof-handoff\reports'
$fixtureRoot = Join-Path (Join-Path $reportRoot 'combined-host-20261018') ('.orchestration-tests\' + [Guid]::NewGuid().ToString('N'))
. $implementation -Action Status -RepositoryRoot $repositoryRoot -WorktreeRoot $worktreeRoot -LibraryOnly
. (Join-Path $PSScriptRoot 'nic-transaction.ps1')

$script:passed = 0
function Assert-Test {
    param([bool]$Condition,[string]$Name)
    if (-not $Condition) { throw "FAIL: $Name" }
    $script:passed++
}
function Assert-Throws {
    param([scriptblock]$Action,[string]$Name)
    $threw = $false
    try { & $Action } catch { $threw = $true }
    Assert-Test $threw $Name
}
function New-TestZipBytes {
    param([Parameter(Mandatory=$true)][System.Collections.IDictionary]$Entries)
    Add-Type -AssemblyName System.IO.Compression -ErrorAction Stop
    $memory = [IO.MemoryStream]::new()
    try {
        $zip = [IO.Compression.ZipArchive]::new($memory,[IO.Compression.ZipArchiveMode]::Create,$true)
        try {
            foreach ($name in $Entries.Keys) {
                $entry = $zip.CreateEntry([string]$name,[IO.Compression.CompressionLevel]::Optimal)
                $stream = $entry.Open()
                try { $bytes = [byte[]]$Entries[$name]; $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
            }
        } finally { $zip.Dispose() }
        return ,([byte[]]$memory.ToArray())
    } finally { $memory.Dispose() }
}
function New-TestFixtureMailboxArchive {
    param([Parameter(Mandatory=$true)][string]$Directory,[Parameter(Mandatory=$true)][ValidateSet('correction-only','corrected-collector')][string]$Mode,[ValidateSet('PASS','FAIL')][string]$MetadataStatus='PASS',[switch]$TamperNestedContent,[switch]$EmptySmbios3,[ValidateSet('None','ModeCase','MissingField','NativeMismatch','WmiMismatch','NativeAgreement','NativeError','WmiError')][string]$WindowsReadbackTamper='None')
    [void][IO.Directory]::CreateDirectory($Directory)
    $names = @('report.txt','eps2-before.bin','eps2-after.bin','eps3-before.bin','eps3-after.bin','table-before.bin','table-after.bin','smbios2-ep-prelude.efi')
    if ($Mode -ceq 'corrected-collector') { $names += 'HypersinvSmbiosPatch.txt' }
    $captureDirectory = Join-Path $Directory 'captured'
    [void][IO.Directory]::CreateDirectory($captureDirectory)
    $captured = [Collections.Generic.List[object]]::new()
    $nestedEntries = [ordered]@{}
    $references = @{}
    for ($index=0; $index -lt $names.Count; $index++) {
        $name = [string]$names[$index]
        if ($EmptySmbios3 -and $name -in @('eps3-before.bin','eps3-after.bin')) { $content = [byte[]]::new(0) } else { $content = [Text.UTF8Encoding]::new($false).GetBytes('fixture-content|' + $Mode + '|' + $name) }
        $leafExtension = if ([IO.Path]::GetExtension($name) -ceq '.efi') { 'efi' } elseif ([IO.Path]::GetExtension($name) -ceq '.txt') { 'txt' } else { 'bin' }
        $leaf = 'f.' + ($index + 1).ToString('D4') + '.' + $leafExtension
        $guestPath = if ($name -ceq 'HypersinvSmbiosPatch.txt') { 'Z:\HypersinvSmbiosPatch.txt' } elseif ($name -ceq 'smbios2-ep-prelude.efi') { 'Z:\EFI\HypersinvInventory\fixture-correction\smbios2-ep-prelude.efi' } else { 'Z:\EFI\HypersinvInventory\fixture-correction\' + $name }
        $extractedPath = Join-Path $captureDirectory $name
        [IO.File]::WriteAllBytes($extractedPath,$content)
        $sha = Get-CombinedByteArraySha256 -Bytes $content
        $captured.Add([ordered]@{name=$name;guestPath=$guestPath;archiveLeaf=$leaf;path=$guestPath;bytes=[long]$content.Length;sha256=$sha})
        $nestedEntries[$leaf] = $content
        $references[$name] = [pscustomobject][ordered]@{path=$extractedPath;bytes=[long]$content.Length;sha256=$sha;archiveEntry=$leaf}
    }
    if ($TamperNestedContent) {
        $tampered = [byte[]]$nestedEntries['f.0001.txt'].Clone()
        $tampered[0] = [byte]($tampered[0] -bxor 1)
        $nestedEntries['f.0001.txt'] = $tampered
    }
    $nestedBytes = New-TestZipBytes -Entries $nestedEntries
    $bootIdentity = [ordered]@{bootId=('B' * 64);windowsLastBootUpTimeUtc='2026-10-18T03:21:00.0000000Z';lastBootUpTimeUtc='2026-10-18T03:21:00.0000000Z'}
    $childTarget = if ($Mode -ceq 'correction-only') { 'EFI\Microsoft\Boot\bootmgfw.efi' } else { 'EFI\HypersinvInventory\loader.efi' }
    $loadImagePhase = if ($Mode -ceq 'correction-only') { 'LoadImage(bootmgfw.efi)' } else { 'LoadImage(loader.efi)' }
    $windowsReadbackMode = if ($Mode -ceq 'correction-only') { 'original' } else { 'replacement' }
    $windowsValues = if ($Mode -ceq 'correction-only') {
        [ordered]@{system_serial='VMware-56 4d 11 f0 8a cf 36 8a-27 72 aa e2 fa 24 64 d1';system_vendor='VMware, Inc.';system_product='VMware20,1';board_serial='None';board_vendor='Intel Corporation';board_product='440BX Desktop Reference Platform';system_uuid='F0114D56-CF8A-8A36-2772-AAE2FA2464D1'}
    } else {
        [ordered]@{system_serial='4091-37';system_vendor='NORTHLAB';system_product='NODE';board_serial='08E4821_MD2F297027';board_vendor='Example Devices Corporation';board_product='Client Reference Board (CB-4200)';system_uuid='A17F24D0-68E2-4A09-9C31-2B7D8450E6F9'}
    }
    $windowsFields=[ordered]@{}
    foreach($fieldName in $windowsValues.Keys){$windowsFields[$fieldName]=[ordered]@{expected=$windowsValues[$fieldName];native=$windowsValues[$fieldName];wmi=$windowsValues[$fieldName];nativeMatchesWmi=$true}}
    switch -CaseSensitive ($WindowsReadbackTamper) {
        'ModeCase' { $windowsReadbackMode=$windowsReadbackMode.ToUpperInvariant() }
        'MissingField' { [void]$windowsFields.Remove('board_product') }
        'NativeMismatch' { $windowsFields.system_serial.native='MISMATCHED-NATIVE' }
        'WmiMismatch' { $windowsFields.system_uuid.wmi='MISMATCHED-WMI' }
        'NativeAgreement' { $windowsFields.board_vendor.nativeMatchesWmi=$false }
    }
    $windowsNativeError=if($WindowsReadbackTamper -ceq 'NativeError'){'synthetic native read error'}else{$null}
    $windowsWmiError=if($WindowsReadbackTamper -ceq 'WmiError'){'synthetic WMI read error'}else{$null}
    $windowsReadback=[ordered]@{schema='smbios-windows-capture.v1';path='C:\ProgramData\fixture\smbios-capture.json';sha256=('1'*64);bytes=1024;expectedMode=$windowsReadbackMode;identityPath='C:\ProgramData\fixture\machine-identity.json';identitySha256=('2'*64);fields=$windowsFields;native=[ordered]@{source='GetSystemFirmwareTable(RSMB)';major=2;minor=7;tableBytes=12842;structureCount=165;error=$windowsNativeError};wmi=[ordered]@{source='ROOT\CIMV2';systemProductClass='Win32_ComputerSystemProduct';systemProductInstancesObserved=1;systemProductInstanceCountExact=$true;systemProductError=$windowsWmiError;baseboardClass='Win32_BaseBoard';baseboardInstancesObserved=1;baseboardInstanceCountExact=$true;baseboardError=$windowsWmiError}}
    $collectorRecord = if ($Mode -ceq 'corrected-collector') { $capture = @($captured | Where-Object { $_.name -ceq 'HypersinvSmbiosPatch.txt' })[0]; [ordered]@{schema='combined-vm-smbios-patch-report.v1';path=$capture.path;sha256=$capture.sha256;bytes=$capture.bytes;status='PASS';result='applied';mask=127;buildId='TEST-BUILD';values=[ordered]@{system_serial='4091-37';system_vendor='NORTHLAB';system_product='NODE';board_serial='08E4821_MD2F297027';board_vendor='Example Devices Corporation';board_product='Client Reference Board (CB-4200)';system_uuid='A17F24D0-68E2-4A09-9C31-2B7D8450E6F9'};efiStatusSource='prelude.report.child_load_status';efiStatusPhase='LoadImage(loader.efi)';efiStatus='0x0000000000000000';startStatusObserved=$false;bootIdentity=$bootIdentity;bootIdentityProvenance=[ordered]@{source='Win32_OperatingSystem.LastBootUpTime via Capture-AfterBoot/Get-BootIdentity';association='Measured by this same authenticated CaptureActive invocation after the owned prelude boot; not emitted by the EFI collector.';emittedByEfiCollector=$false}} } else { $null }
    $metadata = [ordered]@{
        schema='combined-vm-fixture-correction-action.v1'
        internalAction=if($Mode -ceq 'correction-only'){'CaptureCorrectionOnly'}else{'CaptureActive'}
        mode=$Mode
        status=$MetadataStatus
        errors=@()
        bootIdentity=$bootIdentity
        windowsReadback=$windowsReadback
        cleanup=[ordered]@{attempted=$true;errors=@()}
        efi=[ordered]@{schema='combined-vm-smbios2-ep-correction.v1';mode=$Mode;result='prepared';childTarget=$childTarget;childLoadObserved=$true;childLoadStatus='0x0000000000000000';childStartStatusObserved=$false;status='0x0000000000000000';startStatusObserved=$false;statusSource='prelude.report.child_load_status';statusPhase=$loadImagePhase}
        collectorReport=$collectorRecord
        capturedFiles=@($captured.ToArray())
        rawEvidenceBundle=[ordered]@{format='zip';bytes=[long]$nestedBytes.Length;sha256=(Get-CombinedByteArraySha256 -Bytes $nestedBytes);fileCount=$names.Count;base64=[Convert]::ToBase64String($nestedBytes)}
    }
    $metadataBytes = [Text.UTF8Encoding]::new($false).GetBytes(($metadata | ConvertTo-Json -Depth 24 -Compress))
    $outerEntries=[ordered]@{'stdout.txt'=[Text.UTF8Encoding]::new($false).GetBytes("capture complete`n");'stderr.txt'=[byte[]]::new(0);'kernel-metadata.json'=$metadataBytes}
    $outerBytes = New-TestZipBytes -Entries $outerEntries
    $outerPath = Join-Path $Directory 'guest-evidence.zip'
    [IO.File]::WriteAllBytes($outerPath,$outerBytes)
    return [pscustomobject][ordered]@{archivePath=$outerPath;bootIdentity=[pscustomobject]$bootIdentity;references=$references;metadata=$metadata;directory=$Directory}
}
function New-TestReadback {
    param([object]$BindingMap,[ValidateSet('Original','Replacement')][string]$Mode,[string]$MapPath,[string]$MapSha)
    if ([string]::IsNullOrWhiteSpace($MapPath)) { $MapPath = $script:canonicalMapPath }
    if ([string]::IsNullOrWhiteSpace($MapSha)) { $MapSha = Get-CombinedHostSha256 $MapPath }
    $polls = [Collections.Generic.List[object]]::new()
    for ($pollNumber=1; $pollNumber -le 2; $pollNumber++) {
        $pollBindings = [Collections.Generic.List[object]]::new()
        foreach ($binding in $BindingMap.bindings) {
            $expected = if ($Mode -ceq 'Replacement') { [string]$binding.replacement } else { [string]$binding.original }
            $readers = [Collections.Generic.List[object]]::new()
            foreach ($readerName in $binding.readers) {
                $readers.Add([pscustomobject]@{reader=[string]$readerName;actual=$expected;expected=$expected;match=$true;cardinality=1})
            }
            $pollBindings.Add([pscustomobject]@{id=[string]$binding.id;readers=@($readers.ToArray())})
        }
        $polls.Add([pscustomobject]@{bindings=@($pollBindings.ToArray())})
    }
    return [pscustomobject]@{
        schema='combined-host-readback.v1'
        expectedMode=$Mode
        status='PASS'
        simultaneousReadbackPass=$true
        stableAcrossPolls=$true
        lockedMap=[pscustomobject]@{path=$MapPath;sha256=$MapSha;bindingCount=$BindingMap.bindings.Count}
        polls=@($polls.ToArray())
    }
}
function New-TestVmFixtureRows {
    param([object[]]$CanonicalRows)
    $nicIndexByOriginal = @{
        '005056C00008'='7'
        '005056C00001'='6'
        'C4BDE53206D1'='2'
        'D8BBC1D85720'='5'
    }
    foreach ($row in $CanonicalRows) {
        $copy = [pscustomobject][ordered]@{member=[string]$row.member;device=[string]$row.device;original=[string]$row.original;replacement=[string]$row.replacement}
        switch -CaseSensitive ($copy.member) {
            'FakeSMBIOSSerial' { $copy.original = 'VM-SERIAL-0001' }
            'FakeSMBIOSManufacturer' { $copy.original = 'VMware, Inc.' }
            'FakeSMBIOSProduct' { $copy.original = 'VMware7,1' }
            'FakeMotherboardSerial' { $copy.original = 'VM-BOARD-0001' }
            'FakeMotherboardManufacturer' { $copy.original = 'VMware, Inc.' }
            'FakeMotherboardProduct' { $copy.original = 'Virtual Platform' }
            'FakeSMBIOSUUID' { $copy.original = '564D0000-0000-4000-8000-000000000001' }
            'FakeDiskSerial' { $copy.device = 'disk:\\.\PHYSICALDRIVE0'; $copy.original = 'VMWARE-VIRTUAL-DISK-0001' }
            'FakeMacAddress' {
                $key = ConvertTo-CombinedComparableValue -Category 'NIC' -Field 'mac' -Value $copy.original
                $ifIndex = [string]$nicIndexByOriginal[[string]$key]
                $pnp = switch ([string]$key) {
                    '005056C00008' { 'ROOT\NET\0001' }
                    '005056C00001' { 'ROOT\NET\0000' }
                    'C4BDE53206D1' { 'PCI\VEN_15AD&DEV_07B0\C4BDE5FFFF3206D100' }
                    'D8BBC1D85720' { 'PCI\VEN_15AD&DEV_07B0\D8BBC1FFFFD8572000' }
                    default { throw "Unexpected canonical NIC original in test fixture: $($copy.original)" }
                }
                $copy.device = 'adapter:' + $ifIndex + '|' + $pnp
            }
        }
        $copy
    }
}
function Write-TestVmMap {
    param([object[]]$Rows,[string]$Path)
    $csv = @($Rows | ConvertTo-Csv -Delimiter "`t" -NoTypeInformation)
    [IO.File]::WriteAllText($Path,($csv -join "`r`n") + "`r`n",[Text.UTF8Encoding]::new($false))
}
function Write-TestMailboxReceiptVariant {
    param([Parameter(Mandatory=$true)][System.Collections.IDictionary]$Base,[Parameter(Mandatory=$true)][string]$Path,[System.Collections.IDictionary]$Overrides=@{})
    $copy=[ordered]@{}
    foreach($name in $Base.Keys){$copy[$name]=$Base[$name]}
    foreach($name in $Overrides.Keys){$copy[$name]=$Overrides[$name]}
    [IO.File]::WriteAllText($Path,($copy | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{path=$Path;sha256=(Get-CombinedHostSha256 $Path)}
}
function Write-TestEfiApplication([string]$Path) {
    $bytes = [byte[]]::new(512)
    $bytes[0] = 0x4D; $bytes[1] = 0x5A
    [Array]::Copy([BitConverter]::GetBytes([uint32]0x80),0,$bytes,0x3C,4)
    $bytes[0x80] = 0x50; $bytes[0x81] = 0x45
    [Array]::Copy([BitConverter]::GetBytes([uint16]0x8664),0,$bytes,0x84,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]0xF0),0,$bytes,0x94,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]0x20B),0,$bytes,0x98,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]10),0,$bytes,0xDC,2)
    [IO.File]::WriteAllBytes($Path,$bytes)
    return ,$bytes
}
function Get-TestByteSum([byte[]]$Bytes,[int]$Offset,[int]$Length) {
    $sum=0
    for($index=$Offset;$index -lt ($Offset+$Length);$index++){$sum=($sum+[int]$Bytes[$index])-band 0xFF}
    return $sum
}
function Set-TestSmbios2EpsChecksums([byte[]]$Bytes) {
    $Bytes[21]=0
    $Bytes[21]=[byte]((256-(Get-TestByteSum $Bytes 16 15))-band 0xFF)
    $Bytes[4]=0
    $Bytes[4]=[byte]((256-(Get-TestByteSum $Bytes 0 31))-band 0xFF)
}
function New-TestSmbios2Eps([int]$Maximum,[int]$TableLength,[uint32]$TableAddress,[uint16]$StructureCount) {
    $bytes=[byte[]]::new(31)
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes('_SM_'),0,$bytes,0,4)
    $bytes[5]=31;$bytes[6]=2;$bytes[7]=7
    [Array]::Copy([BitConverter]::GetBytes([uint16]$Maximum),0,$bytes,8,2)
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes('_DMI_'),0,$bytes,16,5)
    [Array]::Copy([BitConverter]::GetBytes([uint16]$TableLength),0,$bytes,22,2)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$TableAddress),0,$bytes,24,4)
    [Array]::Copy([BitConverter]::GetBytes([uint16]$StructureCount),0,$bytes,28,2)
    $bytes[30]=0x27
    Set-TestSmbios2EpsChecksums $bytes
    return ,$bytes
}
function New-TestSmbios3Eps {
    $bytes=[byte[]]::new(24)
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes('_SM3_'),0,$bytes,0,5)
    $bytes[6]=24;$bytes[7]=3;$bytes[8]=5;$bytes[9]=0;$bytes[10]=1
    [Array]::Copy([BitConverter]::GetBytes([uint32]0x00100000),0,$bytes,12,4)
    [Array]::Copy([BitConverter]::GetBytes([uint64]0x0000000012345678),0,$bytes,16,8)
    $bytes[5]=[byte]((256-(Get-TestByteSum $bytes 0 24))-band 0xFF)
    return ,$bytes
}
function New-TestSmbiosTable([int]$LongStructureSize=112) {
    if($LongStructureSize -lt 6 -or $LongStructureSize -gt 4096){throw 'Invalid synthetic SMBIOS structure length.'}
    $table=[Collections.Generic.List[byte]]::new()
    foreach($value in [byte[]]@(0,4,0,0)){ $table.Add($value) }
    foreach($value in [Text.Encoding]::ASCII.GetBytes(('A'*($LongStructureSize-6)))){$table.Add($value)}
    $table.Add([byte]0);$table.Add([byte]0)
    foreach($value in [byte[]]@(127,4,0,0,0,0)){$table.Add($value)}
    return ,([byte[]]$table.ToArray())
}
function Write-TestSmbios2CorrectionReport([string]$Path,[ValidateSet('correction-only','corrected-collector')][string]$Mode,[byte[]]$EpsBefore,[byte[]]$EpsAfter,[int]$MeasuredMaximum=112,[string]$ChangedOffsets='4,8',[int]$Smbios3Count=0,[string]$ChildTarget) {
    if ([string]::IsNullOrWhiteSpace($ChildTarget)) { $ChildTarget = Get-CombinedFixtureChildTarget -Mode $Mode }
    $fields=[ordered]@{
        schema='combined-vm-smbios2-ep-correction.v1';mode=$Mode;prelude_path='EFI\HypersinvInventory\fixture-correction\smbios2-ep-prelude.efi';child_target=$ChildTarget;mode_consumption='strict_one_shot_delete_before_configuration_change'
        secureboot_read_status='0x0000000000000000';secureboot_size='1';secureboot_value='0x00';secureboot_attributes='0x00000006'
        smbios2_guid_count='1';smbios3_guid_count=[string]$Smbios3Count;smbios2_major='2';smbios2_minor='7';smbios2_table_address=('0x'+[BitConverter]::ToUInt32($EpsBefore,24).ToString('X8'));smbios2_table_bytes=[string][BitConverter]::ToUInt16($EpsBefore,22)
        smbios2_structure_count_before='2';smbios2_structure_count_after='2';smbios2_max_structure_before=[string][BitConverter]::ToUInt16($EpsBefore,8);smbios2_max_structure_measured=[string]$MeasuredMaximum;smbios2_max_structure_after=[string][BitConverter]::ToUInt16($EpsAfter,8)
        eps2_primary_checksum_before=('0x'+$EpsBefore[4].ToString('X2'));eps2_primary_checksum_after=('0x'+$EpsAfter[4].ToString('X2'));eps2_intermediate_checksum_before=('0x'+$EpsBefore[21].ToString('X2'));eps2_intermediate_checksum_after=('0x'+$EpsAfter[21].ToString('X2'))
        eps2_primary_checksum_sum_before=[string](Get-TestByteSum $EpsBefore 0 31);eps2_primary_checksum_sum_after=[string](Get-TestByteSum $EpsAfter 0 31);eps2_intermediate_checksum_sum_before=[string](Get-TestByteSum $EpsBefore 16 15);eps2_intermediate_checksum_sum_after=[string](Get-TestByteSum $EpsAfter 16 15)
        eps2_changed_offsets=$ChangedOffsets;table_address_length_count_unchanged='yes';table_bytes_identical_before_after='yes';eps3_raw_unchanged='yes';smbios2_model_status='ok';smbios3_validation=$(if($Smbios3Count -eq 1){'ok'}else{'not_present'});result='prepared';phase='ready_to_chainload'
        mode_delete_status='0x0000000000000000';configuration_publish_observed='true';configuration_publish_status='0x0000000000000000';configuration_rollback_observed='false';configuration_rollback_status='0x8000000000000013'
        allocated_page_free_observed='false';allocated_page_free_status='0x8000000000000013';child_load_observed='true';child_load_status='0x0000000000000000';child_start_status_observed='false';child_unload_observed='false';child_unload_status='0x8000000000000013';table_baseline_free_observed='false';table_baseline_free_status='0x8000000000000013'
    }
    $lines=@(foreach($key in $fields.Keys){$key+'='+[string]$fields[$key]})
    [IO.File]::WriteAllLines($Path,$lines,[Text.Encoding]::ASCII)
}

$rows = Get-CombinedMapRows $mapPath
$bindingMap = New-CombinedBindingMap $rows
Assert-Test ($bindingMap.bindings.Count -eq 12) 'locked scope resolves to seven SMBIOS, one disk, and four NIC bindings'
Assert-Test (@($bindingMap.bindings | Where-Object { $_.category -ceq 'NIC' }).Count -eq 4) 'all four locked NIC bindings are present'
$diskBindings = @($bindingMap.bindings | Where-Object { $_.category -ceq 'DISK' })
Assert-Test ($diskBindings.Count -eq 1 -and $diskBindings[0].device -match 'PHYSICALDRIVE0$') 'only the locked system disk is in scope; other disks including USB are excluded'

$validTransitions = @(
    @('EnableTestMode','Prepared'),@('InstallTransparent','PolicyPendingReboot'),@('VerifyTransparent','TransparentInstallPendingReboot'),
    @('Remove','TransparentVerified'),@('VerifyOriginal','TransparentRemovalPendingReboot'),@('InstallReplacement','TransparentRemoved'),
    @('VerifyReplacement','ReplacementInstallPendingReboot'),@('ArmCombined','ReplacementVerified'),@('CollectCombined','CombinedArmPendingReboot'),
    @('Remove','CombinedReadbackComplete'),@('VerifyOriginal','ReplacementRemovalPendingReboot'),@('RestorePolicy','ReplacementRemoved'),@('FinalVerify','PolicyRestorePendingReboot')
)
foreach ($transition in $validTransitions) { Assert-Test (Assert-CombinedLifecyclePhase -Action $transition[0] -Phase $transition[1]) ("lifecycle allows " + $transition[0] + ' from ' + $transition[1]) }
Assert-Throws { Assert-CombinedLifecyclePhase -Action 'ArmCombined' -Phase 'Prepared' } 'lifecycle rejects arming before the replacement storage phase is verified'
Assert-Throws { Assert-CombinedLifecyclePhase -Action 'FinalVerify' -Phase 'CombinedReadbackComplete' } 'lifecycle rejects final verification before package and policy restoration'

$original = New-TestReadback $bindingMap 'Original'
$active = New-TestReadback $bindingMap 'Replacement'
Assert-Test (Assert-CombinedReadbackPhase -BindingMap $bindingMap -Readback $original -Phase original) 'original phase validates all raw reader values'
Assert-Test (Assert-CombinedReadbackPhase -BindingMap $bindingMap -Readback $active -Phase active) 'active phase validates all raw reader values'
Assert-Test (Assert-CombinedReadbackPhase -BindingMap $bindingMap -Readback $original -Phase restored) 'restored phase validates all raw reader values'

$forged = New-TestReadback $bindingMap 'Replacement'
$forged.polls[0].bindings[0].readers[0].actual = [string]$bindingMap.bindings[0].original
$forged.polls[0].bindings[0].readers[0].match = $true
Assert-Throws { Assert-CombinedReadbackPhase -BindingMap $bindingMap -Readback $forged -Phase active } 'actual value mismatch is rejected even when status and match booleans claim success'

$sourcePin = Resolve-CombinedPinnedSource -Repository $repositoryRoot -Worktree $worktreeRoot -Path 'tools/combined_host/model.psm1'
Assert-Test ($sourcePin.relative -ceq 'tools/combined_host/model.psm1' -and (Test-Path -LiteralPath $sourcePin.path -PathType Leaf)) 'worktree source pins resolve using the gate path convention'
$pathBScriptPath = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'tools\efi_inventory\path-b-collector.ps1'))
$pathBManifestPath = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'spoof-handoff\reports\combined-9field-vm-20261018\pathb-source-tree-manifest.json'))
$pathBSourcePin = Resolve-CombinedPinnedSource -Repository $repositoryRoot -Worktree $worktreeRoot -Path $pathBScriptPath
Assert-Test ($pathBSourcePin.root -ceq 'repository' -and $pathBSourcePin.relative -ceq 'tools/efi_inventory/path-b-collector.ps1' -and [IO.Path]::GetFullPath($pathBSourcePin.path) -ceq $pathBScriptPath) 'absolute Path-B source pins resolve to the mutating child in the main repository'
Assert-Test ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..\tools\efi_inventory\path-b-collector.ps1')) -ceq $pathBScriptPath) 'Path-B mutating child is resolved from the main repository rather than the worktree'
Assert-Test (Test-Path -LiteralPath $pathBManifestPath -PathType Leaf) 'hash-pinned Path-B native source manifest exists under reports'
$completeRequiredPins = @(foreach ($relative in $script:CombinedRequiredSourcePins) { $sourcePath = if ($relative -ceq 'tools/efi_inventory/path-b-collector.ps1') { Join-Path $repositoryRoot 'tools\efi_inventory\path-b-collector.ps1' } else { $relative }; $resolved = Resolve-CombinedPinnedSource -Repository $repositoryRoot -Worktree $worktreeRoot -Path $sourcePath; if (-not (Test-Path -LiteralPath $resolved.path -PathType Leaf)) { throw "Required VM source fixture is missing: $relative" }; [pscustomobject]@{relative=$resolved.relative;path=$resolved.path} })
Assert-Test (Assert-CombinedRequiredVmSourcePins -NormalizedPins $completeRequiredPins) 'required VM gate pins include the SMBIOS collector headers, table source, CMake configuration, and fixture builder'
$missingSmbiosHeaderPins = @($completeRequiredPins | Where-Object { $_.relative -cne 'tools/smbios_boot/boot_report.h' })
Assert-Throws { Assert-CombinedRequiredVmSourcePins -NormalizedPins $missingSmbiosHeaderPins } 'VM gate with the boot report compile header omitted is rejected'
$executionPins = @(
    [pscustomobject]@{name='combined-host-runner';path=(Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\run.ps1');sha256=(Get-CombinedHostSha256 (Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\run.ps1'))},
    [pscustomobject]@{name='combined-nic-transaction';path=(Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\nic-transaction.ps1');sha256=(Get-CombinedHostSha256 (Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\nic-transaction.ps1'))},
    [pscustomobject]@{name='combined-host-payload-builder';path=(Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\build-host-payload.ps1');sha256=(Get-CombinedHostSha256 (Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\build-host-payload.ps1'))},
    [pscustomobject]@{name='path-b-child';path=$pathBScriptPath;sha256=(Get-CombinedHostSha256 $pathBScriptPath)},
    [pscustomobject]@{name='path-b-source-manifest';path=$pathBManifestPath;sha256=(Get-CombinedHostSha256 $pathBManifestPath)}
)
$executionPinJournal = [pscustomobject]@{executionSourcePins=$executionPins;activationAvailable=$false;vmGate=[pscustomobject]@{pathBSourcePin=[pscustomobject]@{path=$pathBScriptPath;sha256=$executionPins[3].sha256};pathBSourceManifest=[pscustomobject]@{path=$pathBManifestPath;sha256=$executionPins[4].sha256}}}
Assert-Test (Assert-CombinedExecutionSourcePins -Journal $executionPinJournal -Repository $repositoryRoot -Worktree $worktreeRoot) 'prepared execution source pins validate runner, NIC transaction, host payload builder, Path-B child, and source manifest hashes'
$staleExecutionPins = @($executionPins | ForEach-Object { [pscustomobject]@{name=$_.name;path=$_.path;sha256=$_.sha256} })
$staleExecutionPins[3].sha256 = '0' * 64
$staleExecutionPinJournal = [pscustomobject]@{executionSourcePins=$staleExecutionPins;activationAvailable=$false;vmGate=[pscustomobject]@{pathBSourcePin=[pscustomobject]@{path=$pathBScriptPath;sha256=$staleExecutionPins[3].sha256};pathBSourceManifest=[pscustomobject]@{path=$pathBManifestPath;sha256=$staleExecutionPins[4].sha256}}}
Assert-Throws { Assert-CombinedExecutionSourcePins -Journal $staleExecutionPinJournal -Repository $repositoryRoot -Worktree $worktreeRoot } 'Path-B source drift after Prepare is rejected before execution'
$staleManifestPins = @($executionPins | ForEach-Object { [pscustomobject]@{name=$_.name;path=$_.path;sha256=$_.sha256} })
$staleManifestPins[4].sha256 = '0' * 64
$staleManifestJournal = [pscustomobject]@{executionSourcePins=$staleManifestPins;activationAvailable=$false;vmGate=[pscustomobject]@{pathBSourcePin=[pscustomobject]@{path=$pathBScriptPath;sha256=$staleManifestPins[3].sha256};pathBSourceManifest=[pscustomobject]@{path=$pathBManifestPath;sha256=$staleManifestPins[4].sha256}}}
Assert-Throws { Assert-CombinedExecutionSourcePins -Journal $staleManifestJournal -Repository $repositoryRoot -Worktree $worktreeRoot } 'Path-B native source manifest file drift is rejected before execution'
    $hostPayloadFixtureRoot = Join-Path $worktreeRoot ('spoof-handoff\artifacts\chp\.orchestration-test.' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($hostPayloadFixtureRoot)
    $hostPayloadImagePath = Join-Path $hostPayloadFixtureRoot 'inventory.efi'
    $hostPayloadImageBytes = Write-TestEfiApplication $hostPayloadImagePath
    $hostPayloadCtestPath = Join-Path $hostPayloadFixtureRoot 'ctest.log'
    [IO.File]::WriteAllText($hostPayloadCtestPath,"100% tests passed, 0 tests failed out of 2`r`n",[Text.UTF8Encoding]::new($false))
    $hostPayloadPins = @(Get-CombinedHostPayloadSourcePins -Repository $repositoryRoot -Worktree $worktreeRoot)
    $hostPayloadSourceSetSha = Get-CombinedHostPayloadSourceSetHash -Pins $hostPayloadPins
    $hostPayloadMapSha = Get-CombinedHostSha256 $mapPath
    $hostPayloadManifestSha = Get-CombinedHostSha256 $pathBManifestPath
    $hostPayloadImageSha = Get-CombinedHostSha256 $hostPayloadImagePath
    $hostPayloadCtestSha = Get-CombinedHostSha256 $hostPayloadCtestPath
    $hostPayloadBuildId = '0123456789ABCDEF0123456789ABCDEF'
    $hostPayloadVerdictPath = Join-Path $hostPayloadFixtureRoot 'build-verdict.json'
    $hostPayloadVerdict = [ordered]@{
        schema='combined-host-smbios-payload.v1';result='PASS';hostValuesProfile='checked-in-host-values';vmOnlyProfileEnabled=$false;activeBootMask=127;buildId=$hostPayloadBuildId
        payload=[ordered]@{path=$hostPayloadImagePath;bytes=[long]$hostPayloadImageBytes.Length;sha256=$hostPayloadImageSha;efiMachine='x64';efiSubsystem='EFI_APPLICATION'}
        lockedMap=[ordered]@{path=$mapPath;sha256=$hostPayloadMapSha;bindingCount=12;activeSmbiosFields=7;usbDiskTargetsExcluded=$true}
        sourceManifest=[ordered]@{path=$pathBManifestPath;sha256=$hostPayloadManifestSha}
        sourceSet=[ordered]@{sha256=$hostPayloadSourceSetSha;fileCount=$hostPayloadPins.Count;pinnedFiles=$hostPayloadPins}
        configuration=[ordered]@{vmOnlyProfile='OFF';vmValuesHeader='';buildType='Release';buildTesting=$true;activeBootMask=127;diskSerialEnabled=$false;adapterMacEnabled=$false;cpuSerialEnabled=$false;gpuSerialEnabled=$false}
        ctest=[ordered]@{count=2;passed=2;failed=0;exitCode=0;logPath=$hostPayloadCtestPath;logSha256=$hostPayloadCtestSha}
        liveBootProven=$false;signingPerformed=$false;stageOrArmPerformed=$false
    }
    [IO.File]::WriteAllText($hostPayloadVerdictPath,($hostPayloadVerdict | ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
    $hostPayloadReference = [pscustomobject][ordered]@{verdictPath=$hostPayloadVerdictPath;verdictSha256=(Get-CombinedHostSha256 $hostPayloadVerdictPath);payloadPath=$hostPayloadImagePath;payloadSha256=$hostPayloadImageSha;buildId=$hostPayloadBuildId;sourceSetSha256=$hostPayloadSourceSetSha}
    Assert-Test ((Assert-CombinedHostPayload -Reference $hostPayloadReference -Repository $repositoryRoot -Worktree $worktreeRoot).activeBootMask -eq 127) 'fresh host payload validator accepts the locked host profile, two passing tests, current source set, and x64 EFI application'
    $staleHostPayloadReference = [pscustomobject][ordered]@{verdictPath=$hostPayloadVerdictPath;verdictSha256=$hostPayloadReference.verdictSha256;payloadPath=$hostPayloadImagePath;payloadSha256=('0' * 64);buildId=$hostPayloadBuildId;sourceSetSha256=$hostPayloadSourceSetSha}
    Assert-Throws { Assert-CombinedHostPayload -Reference $staleHostPayloadReference -Repository $repositoryRoot -Worktree $worktreeRoot } 'host payload with a mismatched prepared binary SHA is rejected before stage or arm'
    $hostPayloadVerdict.configuration.vmOnlyProfile='ON'
    [IO.File]::WriteAllText($hostPayloadVerdictPath,($hostPayloadVerdict | ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
    $vmProfileOnReference = [pscustomobject][ordered]@{verdictPath=$hostPayloadVerdictPath;verdictSha256=(Get-CombinedHostSha256 $hostPayloadVerdictPath);payloadPath=$hostPayloadImagePath;payloadSha256=$hostPayloadImageSha;buildId=$hostPayloadBuildId;sourceSetSha256=$hostPayloadSourceSetSha}
    Assert-Throws { Assert-CombinedHostPayload -Reference $vmProfileOnReference -Repository $repositoryRoot -Worktree $worktreeRoot } 'VM-only SMBIOS profile is rejected for the host-specific payload'
Assert-Throws { Resolve-CombinedPinnedSource -Repository $repositoryRoot -Worktree $worktreeRoot -Path '../outside.ps1' } 'source pin path traversal is rejected'
$nicBundleEvidence = Import-CombinedNicHelperBundle -RepositoryRoot $repositoryRoot
$missingNicHelpers = @($script:CombinedNicHelperNames | Where-Object { $null -eq (Get-Command $_ -ErrorAction SilentlyContinue) })
Assert-Test ($nicBundleEvidence.functionCount -eq 11 -and $missingNicHelpers.Count -eq 0) 'tested NIC helper bundle is source-parity pinned and its functions remain callable after module import returns'

[void][IO.Directory]::CreateDirectory($fixtureRoot)
$vehicleBuildRoot = $null
$hostPayloadFixtureRoot = $null
try {
    $espBoot=[pscustomobject]@{path='EFI/Microsoft/Boot/bootmgfw.efi';directory=$false;verification='file-sha256';sha256=('A'*64)}
    $espStatus=[pscustomobject]@{path='EFI/Microsoft/Boot/BOOTSTAT.DAT';directory=$false;verification='file-sha256';sha256=('B'*64)}
    $espBaseline=[pscustomobject]@{partitions=@([pscustomobject]@{diskNumber=0;partitionNumber=1;files=@($espBoot,$espStatus)})}
    $espCurrent=$espBaseline|ConvertTo-Json -Depth 8|ConvertFrom-Json
    $espCurrent.partitions[0].files[1].sha256='C'*64
    $espResult=Assert-CombinedEspMatchesBaseline -Baseline $espBaseline -Current $espCurrent
    Assert-Test ($espResult.match -and $espResult.mutableBootStatusEvidence[0].before[0].sha256 -ceq ('B'*64) -and $espResult.mutableBootStatusEvidence[0].after[0].sha256 -ceq ('C'*64)) 'only the exact mutable BOOTSTAT content is excluded and both observed hashes remain in evidence'
    $espWithoutStatus=[pscustomobject]@{partitions=@([pscustomobject]@{diskNumber=0;partitionNumber=1;files=@($espBoot)})}
    Assert-Test ([bool](Assert-CombinedEspMatchesBaseline -Baseline $espWithoutStatus -Current $espCurrent).match) 'creation of the exact mutable BOOTSTAT path does not fail recovery'
    Assert-Test ([bool](Assert-CombinedEspMatchesBaseline -Baseline $espBaseline -Current $espWithoutStatus).match) 'removal of the exact mutable BOOTSTAT path does not fail recovery'
    $espChangedBoot=$espBaseline|ConvertTo-Json -Depth 8|ConvertFrom-Json
    $espChangedBoot.partitions[0].files[0].sha256='D'*64
    Assert-Throws { Assert-CombinedEspMatchesBaseline -Baseline $espBaseline -Current $espChangedBoot } 'Windows Boot Manager bytes remain strictly compared despite mutable boot-status classification'
    $espOtherStatus=[pscustomobject]@{partitions=@([pscustomobject]@{diskNumber=0;partitionNumber=1;files=@($espBoot,[pscustomobject]@{path='EFI/Other/BOOTSTAT.DAT';directory=$false;verification='file-sha256';sha256=('B'*64)})})}
    $espChangedOther=$espOtherStatus|ConvertTo-Json -Depth 8|ConvertFrom-Json
    $espChangedOther.partitions[0].files[1].sha256='E'*64
    Assert-Throws { Assert-CombinedEspMatchesBaseline -Baseline $espOtherStatus -Current $espChangedOther } 'a different BOOTSTAT path is not classified as mutable'
    $espUnexpected=$espBaseline|ConvertTo-Json -Depth 8|ConvertFrom-Json
    $espUnexpected.partitions[0].files+=@([pscustomobject]@{path='EFI/Microsoft/Boot/BOOTSTAT.DAT.extra';directory=$false;verification='file-sha256';sha256=('F'*64)})
    Assert-Throws { Assert-CombinedEspMatchesBaseline -Baseline $espBaseline -Current $espUnexpected } 'a path sharing the mutable filename prefix remains unexpected'
    $hostPayloadBuilderPath = Join-Path $PSScriptRoot 'build-host-payload.ps1'
    $boundaryWorktree = Join-Path $fixtureRoot 'host-payload-boundary-worktree'
    [void][IO.Directory]::CreateDirectory($boundaryWorktree)
    $allowedWorktreeOutput = Join-Path $boundaryWorktree 'spoof-handoff\artifacts\chp\allowed-output'
    $allowedOutputFailure = $null
    try {
        & $hostPayloadBuilderPath -RepositoryRoot $repositoryRoot -WorktreeRoot $boundaryWorktree -OutputDirectory $allowedWorktreeOutput -PathBSourceManifestPath $pathBManifestPath | Out-Null
    } catch { $allowedOutputFailure = $_.Exception.Message }
    Assert-Test ($allowedOutputFailure -match 'Required host source, locked map, or Path-B source manifest is missing' -and -not (Test-Path -LiteralPath $allowedWorktreeOutput)) 'host payload builder accepts the assigned worktree artifact root and stops before creating output when its isolated source tree is absent'

    $mainCheckoutOutput = Join-Path $repositoryRoot ('spoof-handoff\artifacts\chp\.orchestration-boundary-rejected.' + [Guid]::NewGuid().ToString('N'))
    $mainOutputFailure = $null
    try {
        & $hostPayloadBuilderPath -RepositoryRoot $repositoryRoot -WorktreeRoot $boundaryWorktree -OutputDirectory $mainCheckoutOutput -PathBSourceManifestPath $pathBManifestPath | Out-Null
    } catch { $mainOutputFailure = $_.Exception.Message }
    Assert-Test ($mainOutputFailure -match 'Host payload output must remain under the worktree spoof-handoff\\artifacts\\chp' -and -not (Test-Path -LiteralPath $mainCheckoutOutput)) 'host payload builder rejects main-checkout artifact output before any directory is created'

    $correctionMailbox = New-TestFixtureMailboxArchive -Directory (Join-Path $fixtureRoot 'nested-mailbox-correction') -Mode 'correction-only'
    $correctionMailboxContext=Get-CombinedVmFixtureArchiveContext -Worktree $worktreeRoot -ArchivePath $correctionMailbox.archivePath -ExpectedMode 'correction-only' -ExpectedBootIdentity $correctionMailbox.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $correctionMailbox.archivePath)
    Assert-Test ($correctionMailboxContext.windowsReadback.mode -ceq 'original' -and $correctionMailboxContext.windowsReadback.fieldCount -eq 7 -and $correctionMailboxContext.windowsReadback.native.source -ceq 'GetSystemFirmwareTable(RSMB)' -and $correctionMailboxContext.windowsReadback.wmi.source -ceq 'ROOT\CIMV2') 'correction-only nested metadata requires lowercase original mode and seven consistent native/WMI SMBIOS fields'
    $correctionNestedMember = Assert-CombinedVmFixtureArchiveMember -Repository $repositoryRoot -Worktree $worktreeRoot -ArchivePath $correctionMailbox.archivePath -Reference $correctionMailbox.references['report.txt'] -Label 'synthetic authenticated correction report' -ExpectedName 'report.txt' -ExpectedMode 'correction-only' -ExpectedBootIdentity $correctionMailbox.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $correctionMailbox.archivePath)
    Assert-Test ($correctionNestedMember.archiveEntry -ceq 'f.0001.txt' -and $correctionNestedMember.containerArchiveEntry -ceq 'kernel-metadata.json' -and $correctionNestedMember.containerArchivePath -ceq $correctionMailbox.archivePath -and $correctionNestedMember.containerMetadataSha256 -match '\A[A-F0-9]{64}\z' -and [Text.Encoding]::UTF8.GetString($correctionNestedMember.data) -ceq 'fixture-content|correction-only|report.txt') 'authenticated outer guest archive resolves raw correction report through kernel-metadata nested ZIP leaf f.0001.txt'
    $emptyEpsMailbox = New-TestFixtureMailboxArchive -Directory (Join-Path $fixtureRoot 'nested-mailbox-empty-eps3') -Mode 'correction-only' -EmptySmbios3
    $emptyEpsMember = Assert-CombinedVmFixtureArchiveMember -Repository $repositoryRoot -Worktree $worktreeRoot -ArchivePath $emptyEpsMailbox.archivePath -Reference $emptyEpsMailbox.references['eps3-before.bin'] -Label 'zero-length SMBIOS3 EPS evidence' -ExpectedName 'eps3-before.bin' -ExpectedMode 'correction-only' -ExpectedBootIdentity $emptyEpsMailbox.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $emptyEpsMailbox.archivePath)
    Assert-Test ($emptyEpsMember.bytes -eq 0 -and $emptyEpsMember.data -is [byte[]] -and $emptyEpsMember.data.Length -eq 0 -and (Get-CombinedByteArraySha256 -Bytes $emptyEpsMember.data) -ceq 'E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855') 'zero-length nested SMBIOS3 evidence preserves a non-null typed byte array and the standard empty-content SHA-256'
    $activeMailbox = New-TestFixtureMailboxArchive -Directory (Join-Path $fixtureRoot 'nested-mailbox-active') -Mode 'corrected-collector'
    $activeMailboxContext=Get-CombinedVmFixtureArchiveContext -Worktree $worktreeRoot -ArchivePath $activeMailbox.archivePath -ExpectedMode 'corrected-collector' -ExpectedBootIdentity $activeMailbox.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $activeMailbox.archivePath)
    $activeExpectedSmbios=[ordered]@{system_serial='4091-37';system_vendor='NORTHLAB';system_product='NODE';board_serial='08E4821_MD2F297027';board_vendor='Example Devices Corporation';board_product='Client Reference Board (CB-4200)';system_uuid='A17F24D0-68E2-4A09-9C31-2B7D8450E6F9'}
    $activeSmbiosMatches=$activeMailboxContext.windowsReadback.mode -ceq 'replacement' -and $activeMailboxContext.windowsReadback.fieldCount -eq 7
    foreach($fieldName in $activeExpectedSmbios.Keys){$field=Get-CombinedValue $activeMailboxContext.windowsReadback.fields $fieldName;if([string]$field.expected -cne [string]$activeExpectedSmbios[$fieldName] -or [string]$field.native -cne [string]$activeExpectedSmbios[$fieldName] -or [string]$field.wmi -cne [string]$activeExpectedSmbios[$fieldName] -or $field.nativeMatchesWmi -ne $true){$activeSmbiosMatches=$false}}
    Assert-Test $activeSmbiosMatches 'corrected-collector nested metadata records all seven exact replacements through native RSMB and WMI with lowercase replacement mode'
    $r15InputPath=Join-Path $worktreeRoot 'spoof-handoff\reports\f11\inputs\assembly-input.r15.json'
    $r15Input=Get-Content -LiteralPath $r15InputPath -Raw -ErrorAction Stop|ConvertFrom-Json -ErrorAction Stop
    $r15MapReference=[pscustomobject]@{path=[string]$r15Input.vmFixtureMap.path;sha256=[string]$r15Input.vmFixtureMap.sha256}
    $r15VmBindings=(Get-CombinedVmFixtureBindingMap -Repository $repositoryRoot -Worktree $worktreeRoot -Reference $r15MapReference -HostBindingMap $bindingMap).bindingMap
    $r15CorrectionRoot=Join-Path $worktreeRoot 'spoof-handoff\reports\f11\gate-r15\fixtureCorrection'
    foreach($actualPhase in @(@{name='correctionOnly';mode='correction-only';readbackMode='original';phase='original'},@{name='active';mode='corrected-collector';readbackMode='replacement';phase='active'})){
        $actualDirectory=Join-Path $r15CorrectionRoot $actualPhase.name
        $actualArchive=Join-Path $actualDirectory 'guest-evidence.zip'
        $actualMetadata=Get-Content -LiteralPath (Join-Path $actualDirectory 'kernel-metadata.json') -Raw -ErrorAction Stop|ConvertFrom-Json -ErrorAction Stop
        $actualContext=Get-CombinedVmFixtureArchiveContext -Worktree $worktreeRoot -ArchivePath $actualArchive -ExpectedMode $actualPhase.mode -ExpectedBootIdentity $actualMetadata.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $actualArchive)
        $actualFieldsMatch=$actualContext.windowsReadback.mode -ceq $actualPhase.readbackMode -and $actualContext.windowsReadback.fieldCount -eq 7
        foreach($binding in @($r15VmBindings.bindings|Where-Object{$_.category -ceq 'SMBIOS'})){
            $field=Get-CombinedValue $actualContext.windowsReadback.fields $binding.field
            $expectedValue=if($actualPhase.phase -ceq 'active'){[string]$binding.replacement}else{[string]$binding.original}
            if([string]$field.expected -cne $expectedValue -or [string]$field.native -cne $expectedValue -or [string]$field.wmi -cne $expectedValue -or $field.nativeMatchesWmi -ne $true){$actualFieldsMatch=$false}
        }
        Assert-Test $actualFieldsMatch ("actual r15 " + $actualPhase.name + " authenticated archive contains all seven VM-map SMBIOS values through native RSMB and WMI")
    }
    $activeCollectorMember = Assert-CombinedVmFixtureArchiveMember -Repository $repositoryRoot -Worktree $worktreeRoot -ArchivePath $activeMailbox.archivePath -Reference $activeMailbox.references['HypersinvSmbiosPatch.txt'] -Label 'synthetic authenticated active collector report' -ExpectedName 'HypersinvSmbiosPatch.txt' -ExpectedMode 'corrected-collector' -ExpectedBootIdentity $activeMailbox.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $activeMailbox.archivePath)
    Assert-Test ($activeCollectorMember.archiveEntry -ceq 'f.0009.txt' -and $activeCollectorMember.containerArchiveEntry -ceq 'kernel-metadata.json' -and [string]$activeCollectorMember.metadata.internalAction -ceq 'CaptureActive' -and [string]$activeCollectorMember.metadata.mode -ceq 'corrected-collector' -and [string]$activeCollectorMember.metadata.bootIdentity.bootId -ceq $activeMailbox.bootIdentity.bootId) 'active payload report binds through nested f.0009.txt with authenticated CaptureActive mode and matching boot identity'
    $collectorIdentityProof = Assert-CombinedCollectorBootIdentity -StandaloneCollectorReport $activeMailbox.metadata.collectorReport -AuthenticatedCollectorMetadata $activeCollectorMember.metadata.collectorReport -CaptureBootIdentity $activeMailbox.bootIdentity -Label 'synthetic active collector'
    Assert-Test ($collectorIdentityProof.bootIdentity.bootId -ceq $activeMailbox.bootIdentity.bootId -and $collectorIdentityProof.provenance.emittedByEfiCollector -eq $false) 'collector boot identity is associated with the authenticated CaptureActive guest measurement, not attributed to EFI'
    foreach($tamperName in @('ModeCase','MissingField','NativeMismatch','WmiMismatch','NativeAgreement','NativeError','WmiError')){
        $tamperedCapture=New-TestFixtureMailboxArchive -Directory (Join-Path $fixtureRoot ('nested-mailbox-readback-' + $tamperName)) -Mode 'correction-only' -WindowsReadbackTamper $tamperName
        Assert-Throws { Get-CombinedVmFixtureArchiveContext -Worktree $worktreeRoot -ArchivePath $tamperedCapture.archivePath -ExpectedMode 'correction-only' -ExpectedBootIdentity $tamperedCapture.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $tamperedCapture.archivePath) } "nested metadata rejects invalid Windows SMBIOS readback: $tamperName"
    }
    $wrongStandaloneCollector = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $activeMailbox.metadata.collectorReport -Depth 12)
    $wrongStandaloneCollector.bootIdentity.bootId = 'C' * 64
    Assert-Throws { Assert-CombinedCollectorBootIdentity -StandaloneCollectorReport $wrongStandaloneCollector -AuthenticatedCollectorMetadata $activeCollectorMember.metadata.collectorReport -CaptureBootIdentity $activeMailbox.bootIdentity -Label 'wrong standalone collector identity' } 'standalone collector boot identity cannot alias a different active capture boot'
    $wrongAuthenticatedCollector = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $activeCollectorMember.metadata.collectorReport -Depth 12)
    $wrongAuthenticatedCollector.bootIdentity.lastBootUpTimeUtc = '2026-10-18T03:22:00.0000000Z'
    Assert-Throws { Assert-CombinedCollectorBootIdentity -StandaloneCollectorReport $activeMailbox.metadata.collectorReport -AuthenticatedCollectorMetadata $wrongAuthenticatedCollector -CaptureBootIdentity $activeMailbox.bootIdentity -Label 'wrong authenticated collector identity' } 'authenticated collector metadata must carry the active capture Windows boot timestamp'
    $wrongCollectorProvenance = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $activeMailbox.metadata.collectorReport -Depth 12)
    $wrongCollectorProvenance.bootIdentityProvenance.source = 'EFI collector timestamp'
    Assert-Throws { Assert-CombinedCollectorBootIdentity -StandaloneCollectorReport $wrongCollectorProvenance -AuthenticatedCollectorMetadata $activeCollectorMember.metadata.collectorReport -CaptureBootIdentity $activeMailbox.bootIdentity -Label 'wrong collector provenance source' } 'collector boot identity rejects an unsupported or EFI-derived provenance source'
    $collectorClaimsFirmwareTimestamp = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $activeMailbox.metadata.collectorReport -Depth 12)
    $collectorClaimsFirmwareTimestamp.bootIdentityProvenance.emittedByEfiCollector = $true
    Assert-Throws { Assert-CombinedCollectorBootIdentity -StandaloneCollectorReport $collectorClaimsFirmwareTimestamp -AuthenticatedCollectorMetadata $activeCollectorMember.metadata.collectorReport -CaptureBootIdentity $activeMailbox.bootIdentity -Label 'false EFI provenance claim' } 'collector boot identity cannot claim it was emitted by the EFI collector'
    $missingCollectorProvenance = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $activeMailbox.metadata.collectorReport -Depth 12)
    $missingCollectorProvenance.bootIdentityProvenance = $null
    Assert-Throws { Assert-CombinedCollectorBootIdentity -StandaloneCollectorReport $missingCollectorProvenance -AuthenticatedCollectorMetadata $activeCollectorMember.metadata.collectorReport -CaptureBootIdentity $activeMailbox.bootIdentity -Label 'missing collector provenance' } 'collector boot identity requires explicit CaptureActive provenance'
    Assert-Throws { Assert-CombinedVmFixtureArchiveMember -Repository $repositoryRoot -Worktree $worktreeRoot -ArchivePath $activeMailbox.archivePath -Reference $activeMailbox.references['report.txt'] -Label 'wrong capture phase' -ExpectedName 'report.txt' -ExpectedMode 'correction-only' -ExpectedBootIdentity $activeMailbox.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $activeMailbox.archivePath) } 'nested evidence rejects an authenticated active capture when correction-only mode is expected'
    $wrongBootIdentity = [pscustomobject]@{bootId=('C' * 64);windowsLastBootUpTimeUtc=$correctionMailbox.bootIdentity.windowsLastBootUpTimeUtc;lastBootUpTimeUtc=$correctionMailbox.bootIdentity.lastBootUpTimeUtc}
    Assert-Throws { Assert-CombinedVmFixtureArchiveMember -Repository $repositoryRoot -Worktree $worktreeRoot -ArchivePath $correctionMailbox.archivePath -Reference $correctionMailbox.references['report.txt'] -Label 'wrong boot provenance' -ExpectedName 'report.txt' -ExpectedMode 'correction-only' -ExpectedBootIdentity $wrongBootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $correctionMailbox.archivePath) } 'fixture nested evidence rejects boot identity that differs from the authenticated guest metadata'
    $failedMetadataMailbox = New-TestFixtureMailboxArchive -Directory (Join-Path $fixtureRoot 'nested-mailbox-failed-metadata') -Mode 'correction-only' -MetadataStatus 'FAIL'
    Assert-Throws { Assert-CombinedVmFixtureArchiveMember -Repository $repositoryRoot -Worktree $worktreeRoot -ArchivePath $failedMetadataMailbox.archivePath -Reference $failedMetadataMailbox.references['report.txt'] -Label 'failed outer metadata' -ExpectedName 'report.txt' -ExpectedMode 'correction-only' -ExpectedBootIdentity $failedMetadataMailbox.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $failedMetadataMailbox.archivePath) } 'nested evidence rejects authenticated kernel metadata whose status is FAIL'
    $tamperedNestedMailbox = New-TestFixtureMailboxArchive -Directory (Join-Path $fixtureRoot 'nested-mailbox-tampered-inner') -Mode 'correction-only' -TamperNestedContent
    Assert-Throws { Assert-CombinedVmFixtureArchiveMember -Repository $repositoryRoot -Worktree $worktreeRoot -ArchivePath $tamperedNestedMailbox.archivePath -Reference $tamperedNestedMailbox.references['report.txt'] -Label 'tampered nested member' -ExpectedName 'report.txt' -ExpectedMode 'correction-only' -ExpectedBootIdentity $tamperedNestedMailbox.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $tamperedNestedMailbox.archivePath) } 'nested evidence rejects a raw ZIP entry that differs from its authenticated capturedFiles SHA-256'
    $wrongNestedName = [pscustomobject][ordered]@{path=$correctionMailbox.references['report.txt'].path;bytes=$correctionMailbox.references['report.txt'].bytes;sha256=$correctionMailbox.references['report.txt'].sha256;archiveEntry='f.0002.bin'}
    Assert-Throws { Assert-CombinedVmFixtureArchiveMember -Repository $repositoryRoot -Worktree $worktreeRoot -ArchivePath $correctionMailbox.archivePath -Reference $wrongNestedName -Label 'wrong nested archive leaf' -ExpectedName 'report.txt' -ExpectedMode 'correction-only' -ExpectedBootIdentity $correctionMailbox.bootIdentity -ExpectedArchiveSha256 (Get-CombinedHostSha256 $correctionMailbox.archivePath) } 'nested evidence rejects a reference that maps report bytes to another raw file leaf'
    $fixtureTable=New-TestSmbiosTable
    $fixtureEpsBefore=New-TestSmbios2Eps -Maximum 111 -TableLength $fixtureTable.Length -TableAddress 0x12345678 -StructureCount 2
    $fixtureEpsAfter=New-TestSmbios2Eps -Maximum 112 -TableLength $fixtureTable.Length -TableAddress 0x12345678 -StructureCount 2
    $fixtureReportPath=Join-Path $fixtureRoot 'smbios2-correction-report.txt'
    Write-TestSmbios2CorrectionReport -Path $fixtureReportPath -Mode 'correction-only' -EpsBefore $fixtureEpsBefore -EpsAfter $fixtureEpsAfter
    $fixtureCorrection=Assert-CombinedSmbios2FixtureCorrection -ReportPath $fixtureReportPath -Eps2Before $fixtureEpsBefore -Eps2After $fixtureEpsAfter -Eps3Before ([byte[]]::new(0)) -Eps3After ([byte[]]::new(0)) -TableBefore $fixtureTable -TableAfter $fixtureTable -ExpectedMode 'correction-only'
    Assert-Test ($fixtureCorrection.measuredMaximumStructureSize -eq 112 -and ($fixtureCorrection.actualChangedOffsets -join ',') -ceq '4,8' -and $fixtureCorrection.structureCount -eq 2 -and $fixtureCorrection.tableSha256 -ceq (Get-CombinedByteArraySha256 $fixtureTable)) 'independent EPS2 parser proves dynamic 111-to-112 max correction with unchanged table and actual offsets 4,8'
    Assert-Test ($fixtureCorrection.childTarget -ceq 'EFI\Microsoft\Boot\bootmgfw.efi' -and $fixtureCorrection.loadImagePhase -ceq 'LoadImage(bootmgfw.efi)') 'correction-only EFI report binds the exact Windows Boot Manager LoadImage target and phase'
    Assert-Test (Assert-CombinedFixtureLoadImageProvenance -Mode 'correction-only' -Status '0x0000000000000000' -StatusSource 'prelude.report.child_load_status' -StatusPhase 'LoadImage(bootmgfw.efi)' -StartStatusObserved $false -Label 'test correction-only') 'correction-only metadata accepts only its observed bootmgfw.efi LoadImage result'
    Assert-Throws { Assert-CombinedFixtureLoadImageProvenance -Mode 'correction-only' -Status '0x0000000000000000' -StatusSource 'prelude.report.child_load_status' -StatusPhase 'LoadImage(loader.efi)' -StartStatusObserved $false -Label 'test swapped correction-only' } 'correction-only metadata rejects the active loader phase'
    Assert-Throws { Assert-CombinedFixtureLoadImageProvenance -Mode 'correction-only' -Status '0x0000000000000000' -StatusSource 'prelude.report.child_load_status' -StatusPhase 'LoadImage(\\EFI\\Microsoft\\Boot\\bootmgfw.efi)' -StartStatusObserved $false -Label 'test aliased correction-only' } 'correction-only metadata rejects a path alias instead of the exact observed phase label'
    Assert-Throws { Assert-CombinedFixtureLoadImageProvenance -Mode 'correction-only' -Status 'EFI_SUCCESS' -StatusSource 'prelude.report.child_load_status' -StatusPhase 'LoadImage(bootmgfw.efi)' -StartStatusObserved $false -Label 'test status alias' } 'fixture metadata rejects symbolic EFI_SUCCESS in place of the raw EFI status'
    Assert-Throws { Assert-CombinedFixtureLoadImageProvenance -Mode 'correction-only' -Status '0x0000000000000000' -StatusSource 'child_load_status' -StatusPhase 'LoadImage(bootmgfw.efi)' -StartStatusObserved $false -Label 'test source alias' } 'fixture metadata rejects an abbreviated status provenance source'
    Assert-Throws { Assert-CombinedFixtureLoadImageProvenance -Mode 'correction-only' -Status '0x0000000000000000' -StatusSource 'prelude.report.child_load_status' -StatusPhase 'LoadImage(bootmgfw.efi)' -StartStatusObserved $true -Label 'test observed StartImage' } 'fixture metadata does not claim an observed StartImage result'
    $activeFixtureReportPath=Join-Path $fixtureRoot 'smbios2-active-correction-report.txt'
    Write-TestSmbios2CorrectionReport -Path $activeFixtureReportPath -Mode 'corrected-collector' -EpsBefore $fixtureEpsBefore -EpsAfter $fixtureEpsAfter
    $activeFixtureCorrection=Assert-CombinedSmbios2FixtureCorrection -ReportPath $activeFixtureReportPath -Eps2Before $fixtureEpsBefore -Eps2After $fixtureEpsAfter -Eps3Before ([byte[]]::new(0)) -Eps3After ([byte[]]::new(0)) -TableBefore $fixtureTable -TableAfter $fixtureTable -ExpectedMode 'corrected-collector'
    Assert-Test ($activeFixtureCorrection.childTarget -ceq 'EFI\HypersinvInventory\loader.efi' -and $activeFixtureCorrection.loadImagePhase -ceq 'LoadImage(loader.efi)') 'active correction report records the dedicated inventory loader target'

    $mailboxFixtureRoot=Join-Path $fixtureRoot 'mailbox-receipt-contract'
    [void][IO.Directory]::CreateDirectory($mailboxFixtureRoot)
    $mailboxArchivePath=Join-Path $mailboxFixtureRoot 'guest-evidence.zip'
    $mailboxArchiveBytes=New-TestZipBytes -Entries ([ordered]@{'kernel-metadata.json'=[Text.UTF8Encoding]::new($false).GetBytes('{}')})
    [IO.File]::WriteAllBytes($mailboxArchivePath,$mailboxArchiveBytes)
    $mailboxArchiveSha=Get-CombinedHostSha256 $mailboxArchivePath
    $mailboxMessage='Authenticated fixture capture completed.'
    $mailboxMessageBase64=[Convert]::ToBase64String([Text.UTF8Encoding]::new($false).GetBytes($mailboxMessage))
    $mailboxRunId='uefi-test-fixture-correction-001'
    $mailboxHmac='A'*64
    $mailboxResponse=[ordered]@{
        SchemaVersion=1;RunId=$mailboxRunId;Status='PASS';MessageBase64=$mailboxMessageBase64
        CompletedUtc='2026-10-18T08:07:11.3392287Z';EvidenceSha256=$mailboxArchiveSha
        EvidenceSize=[long]$mailboxArchiveBytes.Length;EvidenceChunkSize=9000;EvidenceChunkCount=1
        ResponseHmacSha256=$mailboxHmac
    }
    $mailboxReceipt=[ordered]@{
        SchemaVersion=1;RunId=$mailboxRunId;Status='PASS';Message=$mailboxMessage
        CompletedUtc=$mailboxResponse.CompletedUtc;EvidenceSha256=$mailboxArchiveSha
        EvidenceSize=[long]$mailboxArchiveBytes.Length;EvidenceChunkSize=9000;EvidenceChunkCount=1
        ResponseHmacSha256=$mailboxHmac;EvidenceArchive='guest-evidence.zip'
        ResponseManifestVariable='uefiloader.response.manifest'
    }
    $mailboxResponseObject=[pscustomobject]$mailboxResponse
    $mailboxResponsePath=Join-Path $mailboxFixtureRoot 'response.json'
    [IO.File]::WriteAllText($mailboxResponsePath,($mailboxResponse | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    $mailboxReceiptPath=Join-Path $mailboxFixtureRoot 'response-metadata.json'
    [IO.File]::WriteAllText($mailboxReceiptPath,($mailboxReceipt | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    $mailboxFixture=[pscustomobject]@{mailboxReceipt=[pscustomobject]@{path=$mailboxReceiptPath;sha256=(Get-CombinedHostSha256 $mailboxReceiptPath)}}
    $mailboxProof=Assert-CombinedVmFixtureMailboxReceipt -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $mailboxFixture -Response $mailboxResponseObject -RunId $mailboxRunId -Hmac $mailboxHmac -EvidenceSha $mailboxArchiveSha -ArchivePath $mailboxArchivePath -ArchiveSha256 $mailboxArchiveSha -ArchiveBytes ([long]$mailboxArchiveBytes.Length) -Label 'test fixture correction mailbox'
    Assert-Test ($mailboxProof.status -ceq 'PASS' -and $mailboxProof.archiveName -ceq 'guest-evidence.zip' -and $mailboxProof.responseMetadataSha256 -ceq (Get-CombinedHostSha256 $mailboxReceiptPath) -and @($mailboxResponseObject.PSObject.Properties.Name) -notcontains 'EvidenceArchive') 'raw authenticated mailbox response omits EvidenceArchive while its hash-pinned metadata receipt binds the archive leaf'
    $badArchiveReference=Write-TestMailboxReceiptVariant -Base $mailboxReceipt -Path $mailboxReceiptPath -Overrides ([ordered]@{EvidenceArchive='different-evidence.zip'})
    $badArchiveFixture=[pscustomobject]@{mailboxReceipt=$badArchiveReference}
    Assert-Throws { Assert-CombinedVmFixtureMailboxReceipt -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $badArchiveFixture -Response $mailboxResponseObject -RunId $mailboxRunId -Hmac $mailboxHmac -EvidenceSha $mailboxArchiveSha -ArchivePath $mailboxArchivePath -ArchiveSha256 $mailboxArchiveSha -ArchiveBytes ([long]$mailboxArchiveBytes.Length) -Label 'test archive-name mismatch' } 'hash-pinned response metadata with an archive leaf mismatch is rejected'
    $badHmacReference=Write-TestMailboxReceiptVariant -Base $mailboxReceipt -Path $mailboxReceiptPath -Overrides ([ordered]@{ResponseHmacSha256=('B'*64)})
    $badHmacFixture=[pscustomobject]@{mailboxReceipt=$badHmacReference}
    Assert-Throws { Assert-CombinedVmFixtureMailboxReceipt -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $badHmacFixture -Response $mailboxResponseObject -RunId $mailboxRunId -Hmac $mailboxHmac -EvidenceSha $mailboxArchiveSha -ArchivePath $mailboxArchivePath -ArchiveSha256 $mailboxArchiveSha -ArchiveBytes ([long]$mailboxArchiveBytes.Length) -Label 'test HMAC mismatch' } 'response metadata with a different HMAC is rejected'
    $badSizeReference=Write-TestMailboxReceiptVariant -Base $mailboxReceipt -Path $mailboxReceiptPath -Overrides ([ordered]@{EvidenceSize=([long]$mailboxArchiveBytes.Length+1)})
    $badSizeFixture=[pscustomobject]@{mailboxReceipt=$badSizeReference}
    Assert-Throws { Assert-CombinedVmFixtureMailboxReceipt -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $badSizeFixture -Response $mailboxResponseObject -RunId $mailboxRunId -Hmac $mailboxHmac -EvidenceSha $mailboxArchiveSha -ArchivePath $mailboxArchivePath -ArchiveSha256 $mailboxArchiveSha -ArchiveBytes ([long]$mailboxArchiveBytes.Length) -Label 'test size mismatch' } 'response metadata with a different archive size is rejected'
    $badRunReference=Write-TestMailboxReceiptVariant -Base $mailboxReceipt -Path $mailboxReceiptPath -Overrides ([ordered]@{RunId='uefi-test-other-run'})
    $badRunFixture=[pscustomobject]@{mailboxReceipt=$badRunReference}
    Assert-Throws { Assert-CombinedVmFixtureMailboxReceipt -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $badRunFixture -Response $mailboxResponseObject -RunId $mailboxRunId -Hmac $mailboxHmac -EvidenceSha $mailboxArchiveSha -ArchivePath $mailboxArchivePath -ArchiveSha256 $mailboxArchiveSha -ArchiveBytes ([long]$mailboxArchiveBytes.Length) -Label 'test RunId mismatch' } 'response metadata with a different RunId is rejected'
    $badEvidenceReference=Write-TestMailboxReceiptVariant -Base $mailboxReceipt -Path $mailboxReceiptPath -Overrides ([ordered]@{EvidenceSha256=('0'*64)})
    $badEvidenceFixture=[pscustomobject]@{mailboxReceipt=$badEvidenceReference}
    Assert-Throws { Assert-CombinedVmFixtureMailboxReceipt -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $badEvidenceFixture -Response $mailboxResponseObject -RunId $mailboxRunId -Hmac $mailboxHmac -EvidenceSha $mailboxArchiveSha -ArchivePath $mailboxArchivePath -ArchiveSha256 $mailboxArchiveSha -ArchiveBytes ([long]$mailboxArchiveBytes.Length) -Label 'test digest mismatch' } 'response metadata with a different evidence digest is rejected'
    $staleReceiptReference=[pscustomobject]@{path=$mailboxReceiptPath;sha256=('0'*64)}
    Assert-Throws { Assert-CombinedVmFixtureMailboxReceipt -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture ([pscustomobject]@{mailboxReceipt=$staleReceiptReference}) -Response $mailboxResponseObject -RunId $mailboxRunId -Hmac $mailboxHmac -EvidenceSha $mailboxArchiveSha -ArchivePath $mailboxArchivePath -ArchiveSha256 $mailboxArchiveSha -ArchiveBytes ([long]$mailboxArchiveBytes.Length) -Label 'test stale metadata hash' } 'response metadata reference with a stale SHA-256 is rejected'
    Assert-Throws { Assert-CombinedVmFixtureMailboxReceipt -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture ([pscustomobject]@{}) -Response $mailboxResponseObject -RunId $mailboxRunId -Hmac $mailboxHmac -EvidenceSha $mailboxArchiveSha -ArchivePath $mailboxArchivePath -ArchiveSha256 $mailboxArchiveSha -ArchiveBytes ([long]$mailboxArchiveBytes.Length) -Label 'test missing receipt' } 'fixture correction without a response-metadata reference is rejected'
    $rawWithArchive=[ordered]@{}
    foreach($propertyName in $mailboxResponse.Keys){$rawWithArchive[$propertyName]=$mailboxResponse[$propertyName]}
    $rawWithArchive.EvidenceArchive='guest-evidence.zip'
    Assert-Throws { Assert-CombinedVmFixtureMailboxReceipt -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $mailboxFixture -Response ([pscustomobject]$rawWithArchive) -RunId $mailboxRunId -Hmac $mailboxHmac -EvidenceSha $mailboxArchiveSha -ArchivePath $mailboxArchivePath -ArchiveSha256 $mailboxArchiveSha -ArchiveBytes ([long]$mailboxArchiveBytes.Length) -Label 'test unexpected raw archive field' } 'raw mailbox response with a non-protocol EvidenceArchive field is rejected'

    Assert-Test (Assert-CombinedFixtureLoadImageProvenance -Mode 'corrected-collector' -Status '0x0000000000000000' -StatusSource 'prelude.report.child_load_status' -StatusPhase 'LoadImage(loader.efi)' -StartStatusObserved $false -Label 'test corrected-collector') 'corrected-collector metadata accepts its observed loader.efi LoadImage result'
    Assert-Throws { Assert-CombinedFixtureLoadImageProvenance -Mode 'corrected-collector' -Status '0x0000000000000000' -StatusSource 'prelude.report.child_load_status' -StatusPhase 'LoadImage(bootmgfw.efi)' -StartStatusObserved $false -Label 'test swapped active' } 'corrected-collector metadata rejects the correction-only Boot Manager phase'
    $swappedCorrectionPath=Join-Path $fixtureRoot 'smbios2-swapped-correction-target.txt'
    Write-TestSmbios2CorrectionReport -Path $swappedCorrectionPath -Mode 'correction-only' -EpsBefore $fixtureEpsBefore -EpsAfter $fixtureEpsAfter -ChildTarget 'EFI\HypersinvInventory\loader.efi'
    Assert-Throws { Assert-CombinedSmbios2FixtureCorrection -ReportPath $swappedCorrectionPath -Eps2Before $fixtureEpsBefore -Eps2After $fixtureEpsAfter -Eps3Before ([byte[]]::new(0)) -Eps3After ([byte[]]::new(0)) -TableBefore $fixtureTable -TableAfter $fixtureTable -ExpectedMode 'correction-only' } 'correction-only EFI report rejects the active loader target'
    $swappedActivePath=Join-Path $fixtureRoot 'smbios2-swapped-active-target.txt'
    Write-TestSmbios2CorrectionReport -Path $swappedActivePath -Mode 'corrected-collector' -EpsBefore $fixtureEpsBefore -EpsAfter $fixtureEpsAfter -ChildTarget 'EFI\Microsoft\Boot\bootmgfw.efi'
    Assert-Throws { Assert-CombinedSmbios2FixtureCorrection -ReportPath $swappedActivePath -Eps2Before $fixtureEpsBefore -Eps2After $fixtureEpsAfter -Eps3Before ([byte[]]::new(0)) -Eps3After ([byte[]]::new(0)) -TableBefore $fixtureTable -TableAfter $fixtureTable -ExpectedMode 'corrected-collector' } 'corrected-collector EFI report rejects the correction-only Boot Manager target'
    $dynamicTable=New-TestSmbiosTable -LongStructureSize 113
    $dynamicBefore=New-TestSmbios2Eps -Maximum 111 -TableLength $dynamicTable.Length -TableAddress 0x12345678 -StructureCount 2
    $dynamicAfter=New-TestSmbios2Eps -Maximum 113 -TableLength $dynamicTable.Length -TableAddress 0x12345678 -StructureCount 2
    $dynamicReportPath=Join-Path $fixtureRoot 'smbios2-dynamic-max-report.txt'
    Write-TestSmbios2CorrectionReport -Path $dynamicReportPath -Mode 'correction-only' -EpsBefore $dynamicBefore -EpsAfter $dynamicAfter -MeasuredMaximum 113
    $dynamicProof=Assert-CombinedSmbios2FixtureCorrection -ReportPath $dynamicReportPath -Eps2Before $dynamicBefore -Eps2After $dynamicAfter -Eps3Before ([byte[]]::new(0)) -Eps3After ([byte[]]::new(0)) -TableBefore $dynamicTable -TableAfter $dynamicTable -ExpectedMode 'correction-only'
    Assert-Test ($dynamicProof.measuredMaximumStructureSize -eq 113 -and $dynamicProof.declaredMaximumAfter -eq 113 -and $dynamicProof.tableBytes -eq $dynamicTable.Length) 'EPS2 parser derives corrected maximum from the raw table rather than a hard-coded 112-byte fixture'
    $smbios3Before=New-TestSmbios3Eps
    $smbios3ReportPath=Join-Path $fixtureRoot 'smbios2-smbios3-preservation-report.txt'
    Write-TestSmbios2CorrectionReport -Path $smbios3ReportPath -Mode 'correction-only' -EpsBefore $fixtureEpsBefore -EpsAfter $fixtureEpsAfter -Smbios3Count 1
    $smbios3Proof=Assert-CombinedSmbios2FixtureCorrection -ReportPath $smbios3ReportPath -Eps2Before $fixtureEpsBefore -Eps2After $fixtureEpsAfter -Eps3Before $smbios3Before -Eps3After $smbios3Before -TableBefore $fixtureTable -TableAfter $fixtureTable -ExpectedMode 'correction-only'
    Assert-Test ($smbios3Proof.smbios3GuidCount -eq 1) 'unique valid SMBIOS3 raw entry is preserved byte-for-byte while SMBIOS2 is corrected'
    $smbios3Drift=[byte[]]$smbios3Before.Clone();$smbios3Drift[12]=[byte]($smbios3Drift[12]+1);$smbios3Drift[5]=0;$smbios3Drift[5]=[byte]((256-(Get-TestByteSum $smbios3Drift 0 24))-band 0xFF)
    Assert-Throws { Assert-CombinedSmbios2FixtureCorrection -ReportPath $smbios3ReportPath -Eps2Before $fixtureEpsBefore -Eps2After $fixtureEpsAfter -Eps3Before $smbios3Before -Eps3After $smbios3Drift -TableBefore $fixtureTable -TableAfter $fixtureTable -ExpectedMode 'correction-only' } 'a changed SMBIOS3 EPS is rejected even when its checksum is recomputed'
    $badOffsetEps=[byte[]]$fixtureEpsAfter.Clone();$badOffsetEps[12]=1;Set-TestSmbios2EpsChecksums $badOffsetEps
    $badOffsetReportPath=Join-Path $fixtureRoot 'smbios2-correction-offset-drift.txt'
    Write-TestSmbios2CorrectionReport -Path $badOffsetReportPath -Mode 'correction-only' -EpsBefore $fixtureEpsBefore -EpsAfter $badOffsetEps -ChangedOffsets '4,8,12'
    Assert-Throws { Assert-CombinedSmbios2FixtureCorrection -ReportPath $badOffsetReportPath -Eps2Before $fixtureEpsBefore -Eps2After $badOffsetEps -Eps3Before ([byte[]]::new(0)) -Eps3After ([byte[]]::new(0)) -TableBefore $fixtureTable -TableAfter $fixtureTable -ExpectedMode 'correction-only' } 'EPS2 correction changing an unapproved byte is rejected even when checksums are recalculated'
    $driftTable=[byte[]]$fixtureTable.Clone();$driftTable[4]=[byte][char]'B'
    $driftReportPath=Join-Path $fixtureRoot 'smbios2-table-drift.txt'
    Write-TestSmbios2CorrectionReport -Path $driftReportPath -Mode 'correction-only' -EpsBefore $fixtureEpsBefore -EpsAfter $fixtureEpsAfter
    Assert-Throws { Assert-CombinedSmbios2FixtureCorrection -ReportPath $driftReportPath -Eps2Before $fixtureEpsBefore -Eps2After $fixtureEpsAfter -Eps3Before ([byte[]]::new(0)) -Eps3After ([byte[]]::new(0)) -TableBefore $fixtureTable -TableAfter $driftTable -ExpectedMode 'correction-only' } 'SMBIOS payload table byte drift is rejected independently of matching table length and pointer'

    $f6ManifestPath=Join-Path $worktreeRoot 'spoof-handoff\reports\f6\fixture-build.json'
    $f6Manifest=Get-Content -LiteralPath $f6ManifestPath -Raw | ConvertFrom-Json
    $f6SourcePins=@(foreach($source in $f6Manifest.build.sources){
        $sourcePath=if($source.path.StartsWith('source/',[StringComparison]::Ordinal)){Join-Path $worktreeRoot ('spoof-handoff\tools\combined_vm\smbios_fixture\'+$source.path.Substring(7).Replace('/','\'))}
        elseif($source.path.StartsWith('uefi-support/',[StringComparison]::Ordinal)){Join-Path $worktreeRoot ('components\uefi-loader\src\vendor\'+$source.path.Substring(13))}
        elseif($source.path -ceq 'production-guard/efi_smbios.c'){$f6Manifest.productionGuard.path}
        elseif($source.path -ceq 'builder/build-firmware-fixture.ps1'){Join-Path $worktreeRoot 'spoof-handoff\tools\combined_vm\build-firmware-fixture.ps1'}
        else{throw "Unsupported f6 fixture source in test: $($source.path)"}
        [pscustomobject]@{path=$sourcePath;sha256=[string]$source.sha256}
    })
    $f6Fixture=[pscustomobject]@{prelude=[pscustomobject]@{path=[string]$f6Manifest.prelude.path;sha256=[string]$f6Manifest.prelude.sha256};buildManifest=[pscustomobject]@{path=$f6ManifestPath;sha256=(Get-CombinedHostSha256 $f6ManifestPath)};sourcePins=$f6SourcePins}
    $currentManifestPath=Join-Path $worktreeRoot 'spoof-handoff\reports\f12-release\r2\fixture-build.json'
    $currentManifest=Get-Content -LiteralPath $currentManifestPath -Raw | ConvertFrom-Json
    $fixtureSourcePins=@(foreach($source in $currentManifest.build.sources){
        $sourcePath=if($source.path.StartsWith('source/',[StringComparison]::Ordinal)){Join-Path $worktreeRoot ('spoof-handoff\tools\combined_vm\smbios_fixture\'+$source.path.Substring(7).Replace('/','\'))}
        elseif($source.path.StartsWith('uefi-support/',[StringComparison]::Ordinal)){Join-Path $worktreeRoot ('components\uefi-loader\src\vendor\'+$source.path.Substring(13))}
        elseif($source.path -ceq 'production-guard/efi_smbios.c'){$currentManifest.productionGuard.path}
        elseif($source.path -ceq 'builder/build-firmware-fixture.ps1'){Join-Path $worktreeRoot 'spoof-handoff\tools\combined_vm\build-firmware-fixture.ps1'}
        else{throw "Unsupported current fixture source in test: $($source.path)"}
        [pscustomobject]@{path=$sourcePath;sha256=[string]$source.sha256}
    })
    $pathBLoaderPath=[IO.Path]::GetFullPath([string]$currentManifest.pathB.loader.path)
    $pathBLoaderSha=[string]$currentManifest.pathB.loader.sha256
    $pathBCollectorPath=[IO.Path]::GetFullPath([string]$currentManifest.pathB.collector.path)
    $gateFixturePins=@($fixtureSourcePins)+@(
        [pscustomobject]@{path=$pathBLoaderPath;sha256=$pathBLoaderSha},
        [pscustomobject]@{path=$pathBCollectorPath;sha256=[string]$currentManifest.pathB.collector.sha256}
    )
    Assert-Throws { Assert-CombinedVmFixtureBuildManifest -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $f6Fixture -NormalizedPins $gateFixturePins -GuardSourcePath ([string]$f6Manifest.productionGuard.path) } 'superseded f6 fixture bundle is rejected after the builder source changed'
    $currentFixture=[pscustomobject]@{
        prelude=[pscustomobject]@{path=[string]$currentManifest.prelude.path;sha256=[string]$currentManifest.prelude.sha256}
        buildManifest=[pscustomobject]@{path=$currentManifestPath;sha256=(Get-CombinedHostSha256 $currentManifestPath)}
        sourcePins=$fixtureSourcePins
    }
    $staleManifestPath=Join-Path $worktreeRoot 'spoof-handoff\reports\f10-release\fixture-build.json'
    $staleManifest=Get-Content -LiteralPath $staleManifestPath -Raw | ConvertFrom-Json
    $staleSourcePins=@(foreach($source in $staleManifest.build.sources){
        $sourcePath=if($source.path.StartsWith('source/',[StringComparison]::Ordinal)){Join-Path $worktreeRoot ('spoof-handoff\tools\combined_vm\smbios_fixture\'+$source.path.Substring(7).Replace('/','\'))}
        elseif($source.path.StartsWith('uefi-support/',[StringComparison]::Ordinal)){Join-Path $worktreeRoot ('components\uefi-loader\src\vendor\'+$source.path.Substring(13))}
        elseif($source.path -ceq 'production-guard/efi_smbios.c'){$staleManifest.productionGuard.path}
        elseif($source.path -ceq 'builder/build-firmware-fixture.ps1'){Join-Path $worktreeRoot 'spoof-handoff\tools\combined_vm\build-firmware-fixture.ps1'}
        else{throw "Unsupported stale fixture source in test: $($source.path)"}
        [pscustomobject]@{path=$sourcePath;sha256=[string]$source.sha256}
    })
    $staleFixture=[pscustomobject]@{prelude=[pscustomobject]@{path=[string]$staleManifest.prelude.path;sha256=[string]$staleManifest.prelude.sha256};buildManifest=[pscustomobject]@{path=$staleManifestPath;sha256=(Get-CombinedHostSha256 $staleManifestPath)};sourcePins=$staleSourcePins}
    Assert-Throws { Assert-CombinedVmFixtureBuildManifest -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $staleFixture -NormalizedPins $gateFixturePins -GuardSourcePath ([string]$staleManifest.productionGuard.path) } 'superseded F10 fixture is rejected when its pinned source differs from the current fixture'
    $manifestProof=Assert-CombinedVmFixtureBuildManifest -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $currentFixture -NormalizedPins $gateFixturePins -GuardSourcePath ([string]$currentManifest.productionGuard.path)
    $collectorManifest=Get-Content -LiteralPath ([string]$currentManifest.pathB.collectorManifest.path) -Raw | ConvertFrom-Json
    Assert-Test ($manifestProof.buildId -ceq [string]$currentManifest.buildId -and $manifestProof.sourceCount -eq 9 -and $manifestProof.preludeSha256 -ceq [string]$currentManifest.prelude.sha256 -and $manifestProof.collectorBuildId -ceq [string]$collectorManifest.buildIdentity.value -and $manifestProof.collectorManifestSha256 -ceq [string]$currentManifest.pathB.collectorManifest.sha256) 'current F12-R2 build manifest pins the exact staged inventory_loader.efi, collector, boot guard, source set, and prelude'
    $driftedSourcePins=@($fixtureSourcePins | ForEach-Object {[pscustomobject]@{path=$_.path;sha256=$_.sha256}})
    $driftedSourcePins[0].sha256='0'*64
    $driftedFixture=[pscustomobject]@{prelude=$currentFixture.prelude;buildManifest=$currentFixture.buildManifest;sourcePins=$driftedSourcePins}
    Assert-Throws { Assert-CombinedVmFixtureBuildManifest -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $driftedFixture -NormalizedPins $gateFixturePins -GuardSourcePath ([string]$currentManifest.productionGuard.path) } 'fixture source receipt cannot substitute a stale or mismatched current build source pin'
    $guardDriftManifest=Get-Content -LiteralPath $currentManifestPath -Raw | ConvertFrom-Json
    $guardDriftManifest.productionGuard.sha256='0'*64
    $guardDriftManifestPath=Join-Path $fixtureRoot 'guard-drift-build-manifest.json'
    [IO.File]::WriteAllText($guardDriftManifestPath,($guardDriftManifest | ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
    $guardDriftFixture=[pscustomobject]@{prelude=$currentFixture.prelude;buildManifest=[pscustomobject]@{path=$guardDriftManifestPath;sha256=(Get-CombinedHostSha256 $guardDriftManifestPath)};sourcePins=$fixtureSourcePins}
    Assert-Throws { Assert-CombinedVmFixtureBuildManifest -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $guardDriftFixture -NormalizedPins $gateFixturePins -GuardSourcePath ([string]$currentManifest.productionGuard.path) } 'build manifest pinning a changed production SMBIOS guard is rejected'
    $loaderReceiptDriftManifest=Get-Content -LiteralPath $currentManifestPath -Raw | ConvertFrom-Json
    $loaderReceiptDriftManifest.pathB.loaderInputValidation.sourceReceiptSha256='0'*64
    $loaderReceiptDriftManifestPath=Join-Path $fixtureRoot 'loader-receipt-drift-build-manifest.json'
    [IO.File]::WriteAllText($loaderReceiptDriftManifestPath,($loaderReceiptDriftManifest | ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
    $loaderReceiptDriftFixture=[pscustomobject]@{prelude=$currentFixture.prelude;buildManifest=[pscustomobject]@{path=$loaderReceiptDriftManifestPath;sha256=(Get-CombinedHostSha256 $loaderReceiptDriftManifestPath)};sourcePins=$fixtureSourcePins}
    Assert-Throws { Assert-CombinedVmFixtureBuildManifest -Repository $repositoryRoot -Worktree $worktreeRoot -Fixture $loaderReceiptDriftFixture -NormalizedPins $gateFixturePins -GuardSourcePath ([string]$currentManifest.productionGuard.path) } 'fixture loader input receipt hash drift is rejected'

    $sourceFixture = Join-Path $fixtureRoot 'source-manifest-inputs'
    $sourceRootOne = Join-Path $sourceFixture 'components\uefi-loader'
    $sourceRootTwo = Join-Path $sourceFixture 'tools\efi_inventory'
    foreach ($directory in @($sourceRootOne,$sourceRootTwo,(Join-Path $sourceRootOne 'build'),(Join-Path $sourceRootTwo 'private'))) { [void][IO.Directory]::CreateDirectory($directory) }
    [IO.File]::WriteAllText((Join-Path $sourceRootOne 'boot.c'),'int boot(void) { return 0; }',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $sourceRootOne 'CMakeLists.txt'),'add_library(boot boot.c)',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $sourceRootTwo 'reader.cpp'),'int reader(void) { return 1; }',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $sourceRootOne 'build\generated.c'),'generated',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $sourceRootTwo 'private\signing-key.c'),'secret',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $sourceRootTwo 'notes.md'),'not a native build input',[Text.UTF8Encoding]::new($false))
    $fixtureRootRelative = (ConvertTo-CombinedRelativePath $sourceFixture.Substring(([IO.Path]::GetFullPath($worktreeRoot).TrimEnd('\') + '\').Length))
    $sourceRoots = @($fixtureRootRelative)
    $sourceManifestPath = Join-Path $fixtureRoot 'source-tree-manifest.json'
    $sourceManifest = Write-CombinedPathBSourceManifest -Repository $worktreeRoot -OutputPath $sourceManifestPath -SourceRoots $sourceRoots
    Assert-Test ($sourceManifest.fileCount -eq 3) 'Path-B source manifest includes native source and CMake inputs but excludes generated, private, and unrelated files'
    $sourceManifestCheck = Assert-CombinedPathBSourceManifest -Repository $worktreeRoot -Path $sourceManifestPath -ExpectedFileSha256 $sourceManifest.sha256 -SourceRoots $sourceRoots
    Assert-Test ($sourceManifestCheck.filesSha256 -ceq $sourceManifest.filesSha256) 'Path-B source manifest recomputes the exact sorted source set and member hashes'
    [IO.File]::WriteAllText((Join-Path $sourceRootTwo 'added.hpp'),'struct Added {};',[Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-CombinedPathBSourceManifest -Repository $worktreeRoot -Path $sourceManifestPath -ExpectedFileSha256 $sourceManifest.sha256 -SourceRoots $sourceRoots } 'Path-B source additions after manifest creation are rejected'

    $vehicleRunId='orchestration-test.'+[Guid]::NewGuid().ToString('N')
    $vehicleRunDirectory=Join-Path $fixtureRoot $vehicleRunId
    [void][IO.Directory]::CreateDirectory($vehicleRunDirectory)
    $vehicleLayout=Get-CombinedPathBBuildLayout -Worktree $worktreeRoot -RunDirectory $vehicleRunDirectory
    $vehicleBuildRoot=$vehicleLayout.buildRoot
    $vehicleLoaderBuild=$vehicleLayout.loaderBuild
    $vehicleSourceRoot=Join-Path $vehicleBuildRoot 'vm-pinned-source'
    foreach ($directory in @($vehicleLoaderBuild,$vehicleSourceRoot)) { [void][IO.Directory]::CreateDirectory($directory) }
    $vehicleRecords=[Collections.Generic.List[object]]::new()
    foreach ($name in @('inventory_loader.efi','ib-install.exe')) {
        $source=Join-Path $vehicleSourceRoot $name
        $destination=Join-Path $vehicleLoaderBuild $name
        [IO.File]::WriteAllText($source,('vm-tested-'+$name),[Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($destination,('native-rebuild-'+$name),[Text.UTF8Encoding]::new($false))
        $vehicleRecords.Add([pscustomobject]@{name=$name;path=$source;sha256=(Get-CombinedHostSha256 $source)})
    }
    $vehicleJournal=[pscustomobject]@{runDirectory=$vehicleRunDirectory;vmGate=[pscustomobject]@{pathBArtifacts=@($vehicleRecords.ToArray())}}
    $vehicleInstall=Install-CombinedVmPinnedPathBArtifacts -Journal $vehicleJournal -Repository $repositoryRoot -Worktree $worktreeRoot -BuildRoot $vehicleBuildRoot
    $vehicleInstallPassed=$vehicleInstall.Count -eq 2
    foreach ($record in $vehicleInstall) {
        $sourceRecord=@($vehicleRecords.ToArray() | Where-Object { $_.name -ceq $record.name })
        if ($sourceRecord.Count -ne 1 -or $record.installedSha256 -cne $sourceRecord[0].sha256 -or (Get-CombinedHostSha256 $record.destination) -cne $sourceRecord[0].sha256 -or -not $record.nativeBuildBackup -or (Get-CombinedHostSha256 $record.nativeBuildBackup) -cne $record.nativeBuildSha256) { $vehicleInstallPassed=$false }
    }
    Assert-Test $vehicleInstallPassed 'host build selects hash-pinned VM-tested loader and installer bytes while preserving the native rebuild outputs'
    $tamperedVehicleJournal=[pscustomobject]@{runDirectory=$vehicleJournal.runDirectory;vmGate=[pscustomobject]@{pathBArtifacts=@([pscustomobject]@{name='inventory_loader.efi';path=$vehicleRecords[0].path;sha256=('0'*64)},$vehicleRecords[1])}}
    Assert-Throws { Install-CombinedVmPinnedPathBArtifacts -Journal $tamperedVehicleJournal -Repository $repositoryRoot -Worktree $worktreeRoot -BuildRoot $vehicleBuildRoot } 'VM-tested vehicle artifact drift is rejected before copying'

    $journalFixtureDirectory = Join-Path $fixtureRoot 'journal-roundtrip'
    [void][IO.Directory]::CreateDirectory($journalFixtureDirectory)
    $journalFixturePath = Join-Path $journalFixtureDirectory 'journal.json'
    $journalFixture = [ordered]@{
        schema='combined-host-preparation.v1'
        runDirectory=$journalFixtureDirectory
        journalPath=$journalFixturePath
        updatedUtc=[DateTime]::UtcNow.ToString('o')
        phase='Prepared'
        operationInProgress=$false
        hostMutationPerformed=$false
        activationAvailable=$false
        nicTransaction=$null
    }
    Write-CombinedHostAtomicJson -Path $journalFixturePath -Value $journalFixture
    $loadedJournal = Get-CombinedHostJournal -Repository $repositoryRoot -Worktree $worktreeRoot -RunDirectory $journalFixtureDirectory
    Assert-Test ($loadedJournal.journal -is [Collections.IDictionary] -and [IO.Path]::GetFullPath($loadedJournal.journal.journalPath) -ceq [IO.Path]::GetFullPath($journalFixturePath)) 'prepared JSON journal reloads as a mutable dictionary with its authoritative path'
    $persistedLifecycleProperties = @('lastStorageAction','secureBootOffObservation','policyMutationBootIdentity','replacementVerifiedBootIdentity','armBootIdentity','pathBPreparedBuild','pathBBuild','pathBBackupPath','pathBStageEvidence','pathBArmEvidence','armedUtc','collectBootIdentity','pathBSmbiosReport','smbiosBootProof','bootIdentityAfterArm','storageReplacementVerification','pathBCollectEvidence','pathBStageRestore','nicReadback','finalStorageVerification','finalReadback','finalBcdSnapshot','finalEspSnapshot','finalFirmwareUnchanged','finalEspComparison','storagePhase','lastRecoveryError','recoveryUpdatedUtc','pinnedVehicleArtifacts')
    foreach ($propertyName in $persistedLifecycleProperties) { $loadedJournal.journal.$propertyName = ('persisted:' + $propertyName) }
    $loadedJournal.journal.nicTransaction = [ordered]@{path=(Join-Path $journalFixtureDirectory 'nic-transaction.json');mutationIntentRecorded=$true;restorationComplete=$false}
    $loadedJournal.journal.nicTransaction.restorationComplete = $true
    $loadedJournal.journal.nicTransaction.sha256 = ('A' * 64)
    $loadedJournal.journal.phase = 'NicMutationIntent'
    $loadedJournal.journal.operationInProgress = $true
    $loadedJournal.journal.hostMutationPerformed = $true
    $null = Save-CombinedHostJournal $loadedJournal.journal
    $reloadedJournal = Get-CombinedHostJournal -Repository $repositoryRoot -Worktree $worktreeRoot -RunDirectory $journalFixtureDirectory
    $lifecyclePropertiesPersisted = $true
    foreach ($propertyName in $persistedLifecycleProperties) {
        if ([string](Get-CombinedValue $reloadedJournal.journal $propertyName) -cne ('persisted:' + $propertyName)) { $lifecyclePropertiesPersisted=$false; break }
    }
    Assert-Test ($lifecyclePropertiesPersisted -and [bool]$reloadedJournal.journal.operationInProgress -and [string]$reloadedJournal.journal.phase -ceq 'NicMutationIntent' -and [bool]$reloadedJournal.journal.nicTransaction.restorationComplete -and [string]$reloadedJournal.journal.nicTransaction.sha256 -ceq ('A' * 64)) 'dynamic lifecycle and nested NIC recovery writes persist across JSON reload before mutation resumes'
    $mockMutation = { param([bool]$IntentPersisted) [pscustomobject]@{called=$true;intentPersisted=$IntentPersisted} }
    $mockMutationResult = & $mockMutation ([bool]$reloadedJournal.journal.operationInProgress -and [string]$reloadedJournal.journal.phase -ceq 'NicMutationIntent')
    Assert-Test ($mockMutationResult.called -and $mockMutationResult.intentPersisted) 'mock lifecycle mutation dispatch occurs only after the persisted intent is reloaded'

    $vmMapPath = Join-Path $fixtureRoot 'vm-fixture-map.tsv'
    $vmRows = @(New-TestVmFixtureRows $rows)
    Write-TestVmMap -Rows $vmRows -Path $vmMapPath
    $vmMapReference = [pscustomobject]@{path=$vmMapPath;sha256=(Get-CombinedHostSha256 $vmMapPath)}
    $vmFixture = Get-CombinedVmFixtureBindingMap -Repository $repositoryRoot -Worktree $worktreeRoot -Reference $vmMapReference -HostBindingMap $bindingMap
    Assert-Test ($vmFixture.bindingMap.bindings.Count -eq 12) 'VM fixture map with distinct originals and PnP identities resolves to 12 bindings'
    $vmSerial = @($vmFixture.bindingMap.bindings | Where-Object { $_.id -ceq 'SMBIOS:system_serial' })[0]
    $hostSerial = @($bindingMap.bindings | Where-Object { $_.id -ceq 'SMBIOS:system_serial' })[0]
    $vmDisk = @($vmFixture.bindingMap.bindings | Where-Object { $_.category -ceq 'DISK' })[0]
    $hostDisk = @($bindingMap.bindings | Where-Object { $_.category -ceq 'DISK' })[0]
    $hostPnps = @($bindingMap.bindings | Where-Object { $_.category -ceq 'NIC' } | ForEach-Object { $_.pnpDeviceId } | Sort-Object)
    $vmPnps = @($vmFixture.bindingMap.bindings | Where-Object { $_.category -ceq 'NIC' } | ForEach-Object { $_.pnpDeviceId } | Sort-Object)
    Assert-Test ($vmSerial.original -cne $hostSerial.original -and $vmDisk.original -cne $hostDisk.original -and ($vmPnps -join ',') -cne ($hostPnps -join ',')) 'VM originals and PnP identities are distinct from the physical-host map'
    $vmOriginalReadback = New-TestReadback $vmFixture.bindingMap 'Original' $vmFixture.path $vmFixture.sha256
    $vmActiveReadback = New-TestReadback $vmFixture.bindingMap 'Replacement' $vmFixture.path $vmFixture.sha256
    Assert-Test (Assert-CombinedReadbackMapReference -Readback $vmOriginalReadback -MapReference $vmFixture) 'VM-phase readback pins the exact VM fixture map hash'
    Assert-Test (Assert-CombinedReadbackPhase -BindingMap $vmFixture.bindingMap -Readback $vmOriginalReadback -Phase original) 'gate original phase accepts VM-captured originals and PnP identifiers'
    Assert-Test (Assert-CombinedReadbackPhase -BindingMap $vmFixture.bindingMap -Readback $vmActiveReadback -Phase active) 'gate active phase verifies exact locked replacements against VM NIC identities'
    Assert-Throws { Assert-CombinedReadbackMapReference -Readback $vmOriginalReadback -MapReference ([pscustomobject]@{path=$mapPath;sha256=(Get-CombinedHostSha256 $mapPath)}) } 'VM phase is rejected when its readback names a different map hash'
    $alteredVmRows = @(New-TestVmFixtureRows $rows)
    ($alteredVmRows | Where-Object { $_.member -ceq 'FakeSMBIOSSerial' })[0].replacement = 'UNLOCKED-REPLACEMENT'
    $alteredVmMapPath = Join-Path $fixtureRoot 'vm-fixture-map-altered.tsv'
    Write-TestVmMap -Rows $alteredVmRows -Path $alteredVmMapPath
    $alteredVmReference = [pscustomobject]@{path=$alteredVmMapPath;sha256=(Get-CombinedHostSha256 $alteredVmMapPath)}
    Assert-Throws { Get-CombinedVmFixtureBindingMap -Repository $repositoryRoot -Worktree $worktreeRoot -Reference $alteredVmReference -HostBindingMap $bindingMap } 'VM fixture map with an altered locked replacement is rejected'

    $swappedVmRows = @(New-TestVmFixtureRows $rows)
    $firstVirtual = @($swappedVmRows | Where-Object { $_.member -ceq 'FakeMacAddress' -and (ConvertTo-CombinedComparableValue -Category 'NIC' -Field 'mac' -Value $_.original) -ceq '005056C00008' })
    $secondVirtual = @($swappedVmRows | Where-Object { $_.member -ceq 'FakeMacAddress' -and (ConvertTo-CombinedComparableValue -Category 'NIC' -Field 'mac' -Value $_.original) -ceq '005056C00001' })
    if ($firstVirtual.Count -ne 1 -or $secondVirtual.Count -ne 1) { throw 'Could not locate the two canonical VMnet adapter rows in the unit fixture.' }
    $temporaryReplacement = [string]$firstVirtual[0].replacement
    $firstVirtual[0].replacement = [string]$secondVirtual[0].replacement
    $secondVirtual[0].replacement = $temporaryReplacement
    $swappedVmMapPath = Join-Path $fixtureRoot 'vm-fixture-map-swapped-vmnet-values.tsv'
    Write-TestVmMap -Rows $swappedVmRows -Path $swappedVmMapPath
    $swappedVmReference = [pscustomobject]@{path=$swappedVmMapPath;sha256=(Get-CombinedHostSha256 $swappedVmMapPath)}
    Assert-Throws { Get-CombinedVmFixtureBindingMap -Repository $repositoryRoot -Worktree $worktreeRoot -Reference $swappedVmReference -HostBindingMap $bindingMap } 'VM fixture map with the two virtual adapter replacement values swapped is rejected'

    $gatePath = Join-Path $fixtureRoot 'pass-boolean-only.json'
    [IO.File]::WriteAllText($gatePath,('{"schema":"combined-vm-gate.v1","status":"PASS"}'),[Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-CombinedVmGateEvidence -Repository $repositoryRoot -Worktree $worktreeRoot -GatePath $gatePath } 'bare PASS status without pinned source/evidence is rejected before any host preparation'

    $preparedRun = Join-Path $fixtureRoot 'prepared-run'
    [void][IO.Directory]::CreateDirectory($preparedRun)
    $preparedJournal = [ordered]@{schema='combined-host-preparation.v1';runDirectory=$preparedRun;status='PREPARED_READONLY_VM_GATE_REQUIRED';phase='Prepared';hostMutationPerformed=$false;activationAvailable=$false;blockedBoundary='VM gate required';operationInProgress=$false}
    [IO.File]::WriteAllText((Join-Path $preparedRun 'journal.json'),($preparedJournal | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
    # Test the production root-resolution statements in isolation. Full wrapper
    # process, locking, and failure behavior is covered by tests-step-support.ps1.
    # This check must not import a real helper bundle or require elevation.
    $simulatedMain = Join-Path $fixtureRoot 'wrapper-root'
    $simulatedWorktree = Join-Path $simulatedMain '.codex-build\worktrees\smbios-boot-serial-probe'
    foreach($wrapperName in @('combined-host-test.ps1','combined-host-rollback.ps1')){
        $wrapperPath=Join-Path $PSScriptRoot ('..\..\commands\'+$wrapperName)
        $wrapperTokens=$null;$wrapperErrors=$null
        $wrapperAst=[Management.Automation.Language.Parser]::ParseFile($wrapperPath,[ref]$wrapperTokens,[ref]$wrapperErrors)
        if($wrapperErrors.Count){throw ('Wrapper does not parse: '+$wrapperPath)}
        $rootAssignment=@($wrapperAst.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left -is [Management.Automation.Language.VariableExpressionAst] -and $_.Left.VariablePath.UserPath -ceq 'locationRoot'})
        $implementationAssignment=@($wrapperAst.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left -is [Management.Automation.Language.VariableExpressionAst] -and $_.Left.VariablePath.UserPath -ceq 'implementation'})
        if($rootAssignment.Count -ne 1 -or $implementationAssignment.Count -ne 1){throw 'Wrapper root-resolution statement boundaries changed; review the fixture.'}
        $wrapperSource=[IO.File]::ReadAllText($wrapperPath)
        $resolutionSource=$wrapperSource.Substring($rootAssignment[0].Extent.StartOffset,($implementationAssignment[0].Extent.StartOffset-$rootAssignment[0].Extent.StartOffset)).Replace('$PSScriptRoot','$CommandDirectory')
        $resolver=[scriptblock]::Create('param([string]$CommandDirectory)'+[Environment]::NewLine+$resolutionSource+[Environment]::NewLine+'[pscustomobject]@{RepositoryRoot=$repositoryRoot;WorktreeRoot=$worktreeRoot}')
        $mainResolution=& $resolver -CommandDirectory (Join-Path $simulatedMain 'spoof-handoff\commands')
        $worktreeResolution=& $resolver -CommandDirectory (Join-Path $simulatedWorktree 'spoof-handoff\commands')
        Assert-Test ([IO.Path]::GetFullPath($mainResolution.RepositoryRoot) -ceq [IO.Path]::GetFullPath($simulatedMain) -and [IO.Path]::GetFullPath($mainResolution.WorktreeRoot) -ceq [IO.Path]::GetFullPath($simulatedWorktree)) ($wrapperName+' resolves roots when invoked from the main checkout')
        Assert-Test ([IO.Path]::GetFullPath($worktreeResolution.RepositoryRoot) -ceq [IO.Path]::GetFullPath($simulatedMain) -and [IO.Path]::GetFullPath($worktreeResolution.WorktreeRoot) -ceq [IO.Path]::GetFullPath($simulatedWorktree)) ($wrapperName+' resolves roots when invoked from the feature worktree')
    }
    $recoveryScript = Join-Path $PSScriptRoot 'recovery.ps1'
    $recoveryOutput = @(& $recoveryScript -RepositoryRoot $repositoryRoot -WorktreeRoot $worktreeRoot -RunDirectory $preparedRun)
    $recoveryResult = ($recoveryOutput -join "`n") | ConvertFrom-Json -ErrorAction Stop
    Assert-Test ($recoveryResult.status -ceq 'NO_MUTATION_TO_ROLL_BACK') 'rollback explicitly confirms a read-only preparation has no system changes to restore'

    $mutatedRun = Join-Path $fixtureRoot 'mutated-run'
    [void][IO.Directory]::CreateDirectory($mutatedRun)
    $mutatedJournal = [ordered]@{schema='combined-host-preparation.v1';runDirectory=$mutatedRun;journalPath=(Join-Path $mutatedRun 'journal.json');phase='NicMutationIntent';operationInProgress=$true;hostMutationPerformed=$true;activationAvailable=$true;storageSnapshot=$null}
    [IO.File]::WriteAllText((Join-Path $mutatedRun 'journal.json'),($mutatedJournal | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
    $mutationFailure = $null
    try { & $recoveryScript -RepositoryRoot $repositoryRoot -WorktreeRoot $worktreeRoot -RunDirectory $mutatedRun | Out-Null } catch { $mutationFailure = $_.Exception.Message }
    Assert-Test ($mutationFailure -match 'cannot roll back an armed or mutated combined run without its verified external storage recovery snapshot') 'rollback fails closed for any journal that reports host mutation without its recovery snapshot'
}
finally {
    if (Test-Path -LiteralPath $fixtureRoot -PathType Container) { Remove-Item -LiteralPath $fixtureRoot -Recurse -Force }
    if ($vehicleBuildRoot -and (Test-Path -LiteralPath $vehicleBuildRoot -PathType Container)) { Remove-Item -LiteralPath $vehicleBuildRoot -Recurse -Force }
    if ($hostPayloadFixtureRoot -and (Test-Path -LiteralPath $hostPayloadFixtureRoot -PathType Container)) { Remove-Item -LiteralPath $hostPayloadFixtureRoot -Recurse -Force }
}

[pscustomobject]@{schema='combined-host-orchestration-tests.v1';status='PASS';assertions=$script:passed;readbackBindings=$bindingMap.bindings.Count;vmGateAccepted=$false;hostMutationPerformed=$false;fixtureRootRemoved=(-not (Test-Path -LiteralPath $fixtureRoot))} | ConvertTo-Json -Depth 8
