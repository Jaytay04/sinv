[CmdletBinding()]
param(
    [string]$RepositoryRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv',
    [string]$LockedTsvPath
)

$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'device-cycle-proof.psm1'
if ([string]::IsNullOrWhiteSpace($LockedTsvPath)) { $LockedTsvPath = Join-Path $RepositoryRoot 'spoof-handoff\reports\client-originals.tsv' }
[string]$resolvedHandoffRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'spoof-handoff')).TrimEnd([IO.Path]::DirectorySeparatorChar)
$proofDirectory = Join-Path $resolvedHandoffRoot 'reports\device-reboot-cycle-20261016\proof-tests'
[void](New-Item -ItemType Directory -Path $proofDirectory -Force)
$attemptGuid = [Guid]::NewGuid().ToString('N')
$attemptStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$attemptDirectory = Join-Path $proofDirectory ('run.' + $attemptStamp + '.' + $attemptGuid)
[void](New-Item -ItemType Directory -Path $attemptDirectory -Force)
$resultPath = Join-Path $attemptDirectory 'device-cycle-proof-tests.txt'
$manifestPath = Join-Path $attemptDirectory 'device-cycle-proof-test-manifest.json'
$expectedFixtureLeaf = 'fixtures.' + $attemptGuid
$fixtureRoot = Join-Path $attemptDirectory $expectedFixtureLeaf
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
Import-Module $modulePath -Force

$script:cases = New-Object 'System.Collections.Generic.List[object]'
function Add-TestCase {
    param([string]$Name,[scriptblock]$Action)
    try { & $Action; $script:cases.Add([pscustomobject]@{name=$Name;pass=$true;detail='PASS'}) }
    catch { $script:cases.Add([pscustomobject]@{name=$Name;pass=$false;detail=$_.Exception.Message}) }
}
function Assert-TestCondition {
    param([bool]$Condition,[string]$Message)
    if (-not $Condition) { throw $Message }
}
function Write-TestJson {
    param([string]$Path,$Object)
    $json = ConvertTo-Json -InputObject $Object -Depth 20
    [IO.File]::WriteAllText($Path,$json,[Text.UTF8Encoding]::new($false))
}
function New-NativeTestCapture {
    param([string]$Timestamp,[ValidateSet('Original','Replacement')][string]$Mode)
    $adapterData = @(
        @{pnp='ROOT\VMWARE\0001';guid='{E89EFD0E-E452-43D4-8E93-45DA2AF75831}';original='00:50:56:C0:00:08';replacement='02:90:10:20:30:40';index=0},
        @{pnp='ROOT\VMWARE\0000';guid='{219E20CD-E7F3-409C-9AB4-99FF1E29203E}';original='00:50:56:C0:00:01';replacement='02:90:10:20:30:41';index=1},
        @{pnp='PCI\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\3&11583659&1&A3';guid='{1D78C3CF-2BF6-453B-ABAF-2320AF9137F1}';original='C4:BD:E5:32:06:D1';replacement='02:90:10:20:30:42';index=2},
        @{pnp='PCI\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\4&3FA6681&0&00E2';guid='{1291F611-0C4F-4ACF-99A6-E6A22D2F9026}';original='D8:BB:C1:D8:57:20';replacement='02:90:10:20:30:43';index=3}
    )
    $adapters = @()
    foreach ($item in $adapterData) {
        $mac = if ($Mode -ceq 'Original') { $item.original } else { $item.replacement }
        $adapters += [ordered]@{
            index=$item.index;pnpDeviceId=$item.pnp;expectedInterfaceGuid=$item.guid;actualPnpDeviceId=$item.pnp;actualInterfaceGuid=$item.guid
            devicePath='\\?\native-test-interface';oid='OID_802_3_CURRENT_ADDRESS';oidCode='0x01010102';ioctl='IOCTL_NDIS_QUERY_GLOBAL_STATS'
            lockedOriginal=$item.original;lockedReplacement=$item.replacement;rawMacBytesHex=($mac -replace ':','');mac=$mac;bytesReturned=6;stage='complete';win32Error=0;win32ErrorText=$null
        }
    }
    $diskOriginal='0000_0000_0100_0000_E4D2_5CDF_3227_5501.'
    $diskReplacement='1111_1111_1111_1111_A1B2_C3D4_5566_7788.'
    $serial = if ($Mode -ceq 'Original') { $diskOriginal } else { $diskReplacement }
    [ordered]@{
        schema='independent-native-device-io.v1';capturedUtc=$Timestamp;readOnly=$true;replacementEnabled=$false;successRequiresEverySelectedQuery=$true
        adapterEnumeration='SetupDiEnumDeviceInterfaces(GUID_NDIS_LAN_CLASS); exact PnP and NetCfgInstanceId identity match'
        adapterRead='CreateFileW(interface path) + DeviceIoControl(IOCTL_NDIS_QUERY_GLOBAL_STATS, OID_802_3_CURRENT_ADDRESS)'
        diskRead='CreateFileW(physical drive) + DeviceIoControl(IOCTL_STORAGE_QUERY_PROPERTY, StorageDeviceProperty)'
        adapters=$adapters
        disk=[ordered]@{devicePath='\\.\PHYSICALDRIVE0';pnpDeviceId='SCSI\DISK&VEN_NVME&PROD_INTEL_SSDPEKNU01\5&8432851&0&000000';ioctl='IOCTL_STORAGE_QUERY_PROPERTY';property='StorageDeviceProperty';serialOffsetMeaning='STORAGE_DEVICE_DESCRIPTOR.SerialNumberOffset';lockedOriginal=$diskOriginal;lockedReplacement=$diskReplacement;serial=$serial;serialBytesHex=[BitConverter]::ToString([Text.Encoding]::ASCII.GetBytes($serial)).Replace('-','');busType=17;descriptorBytes=396;stage='complete';win32Error=0;win32ErrorText=$null}
        captureComplete=$true
    }
}
function New-WmiTestCapture {
    param([string]$Timestamp)
    [ordered]@{
        schema='device-cycle-wmi-readback.v1';capturedUtc=$Timestamp;captureComplete=$true
        wmiSource='Get-CimInstance Win32_DiskDrive; Get-CimInstance Win32_NetworkAdapter'
        disks=@([ordered]@{DeviceID='\\.\PHYSICALDRIVE0';PNPDeviceID='SCSI\DISK&VEN_NVME&PROD_INTEL_SSDPEKNU01\5&8432851&0&000000';SerialNumber='0000_0000_0100_0000_E4D2_5CDF_3227_5501.'})
        adapters=@(
            [ordered]@{PNPDeviceID='ROOT\VMWARE\0001';GUID='{E89EFD0E-E452-43D4-8E93-45DA2AF75831}';MACAddress='00:50:56:C0:00:08'},
            [ordered]@{PNPDeviceID='ROOT\VMWARE\0000';GUID='{219E20CD-E7F3-409C-9AB4-99FF1E29203E}';MACAddress='00:50:56:C0:00:01'},
            [ordered]@{PNPDeviceID='PCI\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\3&11583659&1&A3';GUID='{1D78C3CF-2BF6-453B-ABAF-2320AF9137F1}';MACAddress='C4:BD:E5:32:06:D1'},
            [ordered]@{PNPDeviceID='PCI\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\4&3FA6681&0&00E2';GUID='{1291F611-0C4F-4ACF-99A6-E6A22D2F9026}';MACAddress='D8:BB:C1:D8:57:20'}
        )
    }
}
function Get-TestCapturePath { param([string]$Name); Join-Path $fixtureRoot $Name }

$beforePath = Get-TestCapturePath 'native-before.json'
$replacementPath = Get-TestCapturePath 'native-replacement.json'
$afterPath = Get-TestCapturePath 'native-after.json'
Write-TestJson $beforePath (New-NativeTestCapture '2026-10-16T22:00:00.000Z' 'Original')
Write-TestJson $replacementPath (New-NativeTestCapture '2026-10-16T22:00:01.000Z' 'Replacement')
Write-TestJson $afterPath (New-NativeTestCapture '2026-10-16T22:00:02.000Z' 'Original')
$nativeValidPath = $beforePath

Add-TestCase 'valid five-field Original -> Replacement -> Original round trip with fresh hashes and times' {
    $result = Test-DeviceCycleNativeRoundTrip -OriginalBeforePath $beforePath -ReplacementPath $replacementPath -OriginalAfterPath $afterPath -LockedTsvPath $LockedTsvPath
    Assert-TestCondition $result.pass ('Valid round trip rejected: ' + (($result.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.line }) -join '; '))
    Assert-TestCondition ($result.evidence.Count -eq 3) 'Round-trip result did not retain all three evidence hashes.'
}
Add-TestCase 'missing native selected row rejected' {
    $doc = New-NativeTestCapture '2026-10-16T22:01:00Z' 'Original'; $doc.adapters = @($doc.adapters | Select-Object -First 3)
    $path = Get-TestCapturePath 'native-missing.json'; Write-TestJson $path $doc
    $result = Test-DeviceCycleNativeCapture -CapturePath $path -LockedTsvPath $LockedTsvPath -Mode Original
    Assert-TestCondition (-not $result.pass) 'Missing selected row unexpectedly passed.'
}
Add-TestCase 'duplicate native selected row rejected' {
    $doc = New-NativeTestCapture '2026-10-16T22:02:00Z' 'Original'; $doc.adapters = @($doc.adapters) + @($doc.adapters[0])
    $path = Get-TestCapturePath 'native-duplicate.json'; Write-TestJson $path $doc
    $result = Test-DeviceCycleNativeCapture -CapturePath $path -LockedTsvPath $LockedTsvPath -Mode Original
    Assert-TestCondition (-not $result.pass) 'Duplicate selected row unexpectedly passed.'
}
Add-TestCase 'partial native status rejected with exact expected and actual line' {
    $doc = New-NativeTestCapture '2026-10-16T22:03:00Z' 'Original'; $doc.adapters[2].win32Error = 5
    $path = Get-TestCapturePath 'native-status.json'; Write-TestJson $path $doc
    $result = Test-DeviceCycleNativeCapture -CapturePath $path -LockedTsvPath $LockedTsvPath -Mode Original
    $failed = @($result.checks | Where-Object { -not $_.pass })
    Assert-TestCondition ($failed.Count -gt 0) 'Nonzero native status unexpectedly passed.'
    Assert-TestCondition ($failed[0].line.Contains('expected=[0] actual=[5]')) ('Failure line lost exact status: ' + $failed[0].line)
}
Add-TestCase 'wrong native replacement bytes rejected' {
    $doc = New-NativeTestCapture '2026-10-16T22:04:00Z' 'Replacement'; $doc.adapters[1].rawMacBytesHex = '005056C00001'
    $path = Get-TestCapturePath 'native-wrong-bytes.json'; Write-TestJson $path $doc
    $result = Test-DeviceCycleNativeCapture -CapturePath $path -LockedTsvPath $LockedTsvPath -Mode Replacement
    Assert-TestCondition (-not $result.pass) 'Wrong replacement bytes unexpectedly passed.'
}
Add-TestCase 'stale or identical capture rejected for round trip' {
    $result = Test-DeviceCycleNativeRoundTrip -OriginalBeforePath $beforePath -ReplacementPath $beforePath -OriginalAfterPath $afterPath -LockedTsvPath $LockedTsvPath
    Assert-TestCondition (-not $result.pass) 'Reused capture unexpectedly passed freshness checks.'
}
Add-TestCase 'malformed native JSON rejected' {
    $path = Get-TestCapturePath 'native-malformed.json'; [IO.File]::WriteAllText($path,'{broken',[Text.UTF8Encoding]::new($false))
    $result = Test-DeviceCycleNativeCapture -CapturePath $path -LockedTsvPath $LockedTsvPath -Mode Original
    Assert-TestCondition (-not $result.pass) 'Malformed evidence unexpectedly passed.'
}

$wmiPath = Get-TestCapturePath 'wmi-final-original.json'
Write-TestJson $wmiPath (New-WmiTestCapture '2026-10-16T22:05:00.000Z')
Add-TestCase 'fresh WMI final-original proof for disk and all four adapters' {
    $result = Test-DeviceCycleWmiOriginals -CapturePath $wmiPath -LockedTsvPath $LockedTsvPath -NotBeforeUtc '2026-10-16T22:04:59Z'
    Assert-TestCondition $result.pass ('Valid WMI originals rejected: ' + (($result.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.line }) -join '; '))
    Assert-TestCondition ($result.claim -like '*does not claim*') 'WMI result does not limit its claim to original values.'
}
Add-TestCase 'stale WMI capture rejected' {
    $result = Test-DeviceCycleWmiOriginals -CapturePath $wmiPath -LockedTsvPath $LockedTsvPath -NotBeforeUtc '2026-10-16T22:05:00Z'
    Assert-TestCondition (-not $result.pass) 'WMI capture at/before cycle start unexpectedly passed.'
}
Add-TestCase 'untyped cached WMI envelope rejected' {
    $doc = New-WmiTestCapture '2026-10-16T22:05:30Z'; $doc.Remove('schema')
    $path = Get-TestCapturePath 'wmi-untyped.json'; Write-TestJson $path $doc
    $result = Test-DeviceCycleWmiOriginals -CapturePath $path -LockedTsvPath $LockedTsvPath -NotBeforeUtc '2026-10-16T22:05:29Z'
    Assert-TestCondition (-not $result.pass) 'WMI evidence without the cycle read-back schema unexpectedly passed.'
}
Add-TestCase 'duplicate WMI selected adapter rejected' {
    $doc = New-WmiTestCapture '2026-10-16T22:06:00Z'; $doc.adapters = @($doc.adapters) + @($doc.adapters[0])
    $path = Get-TestCapturePath 'wmi-duplicate.json'; Write-TestJson $path $doc
    $result = Test-DeviceCycleWmiOriginals -CapturePath $path -LockedTsvPath $LockedTsvPath -NotBeforeUtc '2026-10-16T22:05:59Z'
    Assert-TestCondition (-not $result.pass) 'Duplicate WMI selected row unexpectedly passed.'
}
Add-TestCase 'wrong WMI final original rejected with exact expected and actual line' {
    $doc = New-WmiTestCapture '2026-10-16T22:07:00Z'; $doc.adapters[3].MACAddress = '02:90:10:20:30:43'
    $path = Get-TestCapturePath 'wmi-wrong-value.json'; Write-TestJson $path $doc
    $result = Test-DeviceCycleWmiOriginals -CapturePath $path -LockedTsvPath $LockedTsvPath -NotBeforeUtc '2026-10-16T22:06:59Z'
    $failed = @($result.checks | Where-Object { -not $_.pass -and $_.field -like '*.MACAddress' })
    Assert-TestCondition ($failed.Count -eq 1) 'Wrong final original was not isolated to its selected WMI field.'
    Assert-TestCondition ($failed[0].line.Contains('expected=[D8:BB:C1:D8:57:20] actual=[02:90:10:20:30:43]')) ('WMI mismatch line lost exact expected/actual: ' + $failed[0].line)
}
Add-TestCase 'incomplete WMI report rejected' {
    $doc = New-WmiTestCapture '2026-10-16T22:08:00Z'; $doc.captureComplete = $false
    $path = Get-TestCapturePath 'wmi-incomplete.json'; Write-TestJson $path $doc
    $result = Test-DeviceCycleWmiOriginals -CapturePath $path -LockedTsvPath $LockedTsvPath -NotBeforeUtc '2026-10-16T22:07:59Z'
    Assert-TestCondition (-not $result.pass) 'Incomplete WMI capture unexpectedly passed.'
}

$caseLines = @($script:cases | ForEach-Object { '{0}: {1}{2}' -f $(if ($_.pass) {'PASS'} else {'FAIL'}),$_.name,$(if ($_.pass) {''} else { ' - ' + $_.detail }) })
$passed = @($script:cases | Where-Object pass).Count
$failedCount = $script:cases.Count - $passed
$moduleHash = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
$lockedHash = (Get-FileHash -LiteralPath $LockedTsvPath -Algorithm SHA256).Hash
$report = @(
    'Device cycle proof module host tests (synthetic fixtures; no device access or mutation)',
    ('moduleSha256=' + $moduleHash),
    ('lockedTsvSha256=' + $lockedHash),
    ('cases=' + $script:cases.Count + ' passed=' + $passed + ' failed=' + $failedCount)
)
$report += $caseLines
$report = $report -join [Environment]::NewLine
[IO.File]::WriteAllText($resultPath,$report + [Environment]::NewLine,[Text.UTF8Encoding]::new($false))
$manifest = [ordered]@{
    schema='device-cycle-proof-module-tests.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');resultPath=$resultPath
    resultSha256=(Get-FileHash -LiteralPath $resultPath -Algorithm SHA256).Hash;modulePath=[IO.Path]::GetFullPath($modulePath);moduleSha256=$moduleHash
    lockedTsvPath=[IO.Path]::GetFullPath($LockedTsvPath);lockedTsvSha256=$lockedHash;passed=$passed;failed=$failedCount;cases=@($script:cases.ToArray())
}
Write-TestJson $manifestPath $manifest
$resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
$handoffBoundary = $resolvedHandoffRoot + [IO.Path]::DirectorySeparatorChar
if (-not $resolvedFixtureRoot.StartsWith($handoffBoundary,[StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing fixture cleanup outside spoof-handoff: $resolvedFixtureRoot"
}
if ([IO.Path]::GetFileName($resolvedFixtureRoot) -cne $expectedFixtureLeaf -or $expectedFixtureLeaf -notmatch '^fixtures\.[0-9a-f]{32}$') {
    throw "Refusing fixture cleanup because the expected GUID test leaf did not match: $resolvedFixtureRoot"
}
Remove-Item -LiteralPath $resolvedFixtureRoot -Recurse -Force
Get-Content -LiteralPath $resultPath
if ($failedCount -ne 0) { exit 1 }
