[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('Original','Replacement')][string]$ExpectedState,[Parameter(Mandatory=$true)][string]$ReportDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$readWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$readRepository=[IO.Path]::GetFullPath((Join-Path $readWorktree '..\..\..'))
$readPackage=Join-Path $readRepository 'spoof-handoff'
$readOutput=[IO.Path]::GetFullPath($ReportDirectory)
if(-not $readOutput.StartsWith($readPackage+'\reports\',[StringComparison]::OrdinalIgnoreCase)){
    throw 'Read-back evidence must remain inside spoof-handoff/reports.'
}
if(Test-Path -LiteralPath $readOutput){throw 'Read-back destination already exists.'}
New-Item -ItemType Directory -Path $readOutput | Out-Null
$readLocked=@(Import-Csv -LiteralPath (Join-Path $readPackage 'reports\client-originals.tsv') -Delimiter "`t" |
    Where-Object member -In @('FakeDiskSerial','FakeMacAddress'))
$readDisks=@(Get-CimInstance Win32_DiskDrive -OperationTimeoutSec 20 | Select-Object DeviceID,PNPDeviceID,SerialNumber)
$readAdapters=@(Get-CimInstance Win32_NetworkAdapter -OperationTimeoutSec 20 | Select-Object PNPDeviceID,GUID,MACAddress)
$readExecutable=Join-Path $readPackage '.build\device-runtime-probe\native_originals.exe'
$readNativePath=Join-Path $readOutput 'native.json'
& $readExecutable --output $readNativePath @($readDisks | ForEach-Object {$_.DeviceID})
$readNativeExit=$LASTEXITCODE
$readNative=Get-Content -LiteralPath $readNativePath -Raw | ConvertFrom-Json
$readRows=@();$readMismatches=@()
foreach($readBinding in $readLocked){
    $readExpected=if($ExpectedState -ceq 'Original'){$readBinding.original}else{$readBinding.replacement}
    if($readBinding.member -ceq 'FakeDiskSerial'){
        $readDevice=$readBinding.device.Substring(5)
        $readWmi=@($readDisks | Where-Object DeviceID -CEQ $readDevice)
        $readNativeRows=@($readNative.disks | Where-Object path -CEQ $readDevice)
        $readWmiValue=if($readWmi.Count -eq 1){$readWmi[0].SerialNumber}else{$null}
        $readNativeValue=if($readNativeRows.Count -eq 1 -and $readNativeRows[0].win32Error -eq 0){$readNativeRows[0].serial}else{$null}
        $readNativeSource='IOCTL_STORAGE_QUERY_PROPERTY:StorageDeviceProperty'
        $readWmiSource='Win32_DiskDrive.SerialNumber'
    }else{
        $readDevice=$readBinding.device.Substring($readBinding.device.IndexOf('|')+1)
        $readWmi=@($readAdapters | Where-Object PNPDeviceID -IEQ $readDevice)
        $readNativeRows=@()
        if($readWmi.Count -eq 1 -and $readWmi[0].GUID){
            $readNativeRows=@($readNative.interfaces | Where-Object {([string]$_.interfaceGuid).Trim('{}') -ieq ([string]$readWmi[0].GUID).Trim('{}')})
        }
        $readWmiValue=if($readWmi.Count -eq 1){$readWmi[0].MACAddress}else{$null}
        $readNativeValue=if($readNativeRows.Count -eq 1){$readNativeRows[0].currentMac}else{$null}
        $readNativeSource='GetIfTable2:MIB_IF_ROW2.PhysicalAddress'
        $readWmiSource='Win32_NetworkAdapter.MACAddress'
    }
    $readRows+=[pscustomobject]@{member=$readBinding.member;device=$readDevice;expected=$readExpected;actualNative=$readNativeValue;actualWmi=$readWmiValue;nativeSource=$readNativeSource;wmiSource=$readWmiSource}
    foreach($readObservation in @(@{source=$readNativeSource;value=$readNativeValue},@{source=$readWmiSource;value=$readWmiValue})){
        $readMatches=if($readBinding.member -ceq 'FakeMacAddress'){$readObservation.value -ieq $readExpected}else{$readObservation.value -ceq $readExpected}
        if(-not $readMatches){$readMismatches+=('MISMATCH '+$readBinding.member+' '+$readDevice+' source='+$readObservation.source+' expected="'+$readExpected+'" actual="'+$readObservation.value+'"')}
    }
}
$readPass=($readRows.Count -eq 5 -and $readMismatches.Count -eq 0 -and $readNativeExit -eq 0 -and $readNative.captureComplete)
[IO.File]::WriteAllText((Join-Path $readOutput 'read-back.json'),([ordered]@{
    capturedUtc=[DateTime]::UtcNow.ToString('o');result=$(if($readPass){'PASS'}else{'FAIL'});expectedState=$ExpectedState
    sources='Unmodified native Windows reader and fresh WMI queries';rows=$readRows;mismatches=$readMismatches
    nativeExit=$readNativeExit;nativeCaptureComplete=$readNative.captureComplete;nativeReaderSha256=(Get-FileHash -LiteralPath $readExecutable).Hash
    driverStateNotInferred=$true;bootOrDriverStateChanged=$false
} | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllLines((Join-Path $readOutput 'mismatches.txt'),[string[]]$readMismatches,[Text.UTF8Encoding]::new($false))
if(-not $readPass){$readMismatches | Write-Output;throw 'Native/WMI read-back failed. Exact comparisons are preserved.'}
'PASS: all five locked field/device rows match through native and WMI reads. '+$readOutput
