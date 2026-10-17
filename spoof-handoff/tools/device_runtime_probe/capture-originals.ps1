param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$probeWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$probeRepo=[IO.Path]::GetFullPath((Join-Path $probeWorktree '..\..\..'))
$probePackage=Join-Path $probeRepo 'spoof-handoff'
$probeBuild=Join-Path $probePackage '.build\device-runtime-probe'
$probeOutput=[IO.Path]::GetFullPath($OutputDirectory)
foreach($target in @($probeBuild,$probeOutput)){
    if(-not $target.StartsWith($probePackage.TrimEnd([char]92)+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must remain inside the handoff package.'}
    $ancestor=$target
    while($ancestor -and $ancestor.Length -gt $probePackage.Length){
        if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Output contains a reparse point.'}
        $ancestor=Split-Path -Parent $ancestor
    }
}
New-Item -ItemType Directory -Path $probeBuild -Force|Out-Null
New-Item -ItemType Directory -Path $probeOutput -Force|Out-Null
$probeTemp=Join-Path $probeBuild 'temporary'
New-Item -ItemType Directory -Path $probeTemp -Force|Out-Null
$env:TEMP=$probeTemp;$env:TMP=$probeTemp
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installations=@(& $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
$vcvars=$null
foreach($installation in $installations){$candidate=Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat';if(Test-Path -LiteralPath $candidate){$vcvars=$candidate;break}}
if(-not $vcvars){throw 'The existing x64 MSVC toolchain is unavailable.'}
$toolchain=& $env:ComSpec /d /s /c ('call "'+$vcvars+'" >nul && set')
if($LASTEXITCODE -ne 0){throw 'MSVC environment setup failed.'}
foreach($line in $toolchain){$equal=$line.IndexOf('=');if($equal -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$equal),$line.Substring($equal+1),'Process')}}
$nativeExecutable=Join-Path $probeBuild 'native_originals.exe'
Push-Location -LiteralPath $probeBuild
try{
    & cl.exe /nologo /W4 /WX /EHsc /std:c++17 /utf-8 /MT (Join-Path $PSScriptRoot 'native_originals.cpp') ('/Fe'+$nativeExecutable) /link iphlpapi.lib ole32.lib 2>&1|Tee-Object -FilePath (Join-Path $probeOutput ('build.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.log'))
    if($LASTEXITCODE -ne 0){throw 'Read-only probe compilation failed.'}
}finally{Pop-Location}
$disks=@(Get-CimInstance -ClassName Win32_DiskDrive -OperationTimeoutSec 20|Select-Object DeviceID,PNPDeviceID,Model,SerialNumber,Index)
$nics=@(Get-CimInstance -ClassName Win32_NetworkAdapter -OperationTimeoutSec 20|Select-Object DeviceID,Index,InterfaceIndex,GUID,PNPDeviceID,Name,MACAddress,PhysicalAdapter,NetEnabled)
$nativePath=Join-Path $probeOutput 'native-originals.json'
& $nativeExecutable --output $nativePath @($disks|ForEach-Object{$_.DeviceID})
$nativeExit=$LASTEXITCODE
$native=Get-Content -LiteralPath $nativePath -Raw|ConvertFrom-Json
$baseline=Import-Csv -LiteralPath (Join-Path $probePackage 'reports\client-originals.tsv') -Delimiter "`t"
$diskMatches=@(foreach($disk in $disks){
    $rows=@($native.disks|Where-Object path -CEQ $disk.DeviceID)
    $expected=@($baseline|Where-Object{$_.member -ceq 'FakeDiskSerial' -and $_.device -ceq ('disk:'+$disk.DeviceID)})
    [ordered]@{device=$disk.DeviceID;wmiSerial=$disk.SerialNumber;nativeSerial=$(if($rows.Count -eq 1){$rows[0].serial}else{$null});nativeWin32Error=$(if($rows.Count -eq 1){$rows[0].win32Error}else{-1});nativeWmiAgree=($rows.Count -eq 1 -and $rows[0].win32Error -eq 0 -and $rows[0].serial -ceq $disk.SerialNumber);lockedOriginal=$(if($expected.Count -eq 1){$expected[0].original}else{$null});lockedReplacement=$(if($expected.Count -eq 1){$expected[0].replacement}else{$null});replacementEnabled=$false}
})
$nicMatches=@(foreach($nic in $nics){
    $rows=@($native.interfaces|Where-Object{([string]$_.interfaceGuid).Trim('{}') -ieq ([string]$nic.GUID).Trim('{}')})
    $joinSource='InterfaceGuid'
    if($rows.Count -eq 0 -and [string]::IsNullOrEmpty($nic.GUID) -and $nic.InterfaceIndex -gt 0){
        $rows=@($native.interfaces|Where-Object interfaceIndex -EQ $nic.InterfaceIndex)
        $joinSource='InterfaceIndex within this capture only'
    }
    $expected=@($baseline|Where-Object{$_.member -ceq 'FakeMacAddress' -and $_.device.EndsWith('|'+$nic.PNPDeviceID,[StringComparison]::OrdinalIgnoreCase)})
    $hasMac=-not[string]::IsNullOrEmpty($nic.MACAddress)
    [ordered]@{pnpDeviceId=$nic.PNPDeviceID;wmiIndex=$nic.Index;interfaceIndex=$nic.InterfaceIndex;guid=$nic.GUID;name=$nic.Name;wmiCurrentMac=$nic.MACAddress;nativeCurrentMac=$(if($rows.Count -eq 1){$rows[0].currentMac}else{$null});nativePermanentMac=$(if($rows.Count -eq 1){$rows[0].permanentMac}else{$null});nativeRows=$rows.Count;joinSource=$joinSource;hasWmiCurrentMac=$hasMac;nativeWmiAgree=($hasMac -and $rows.Count -eq 1 -and $rows[0].currentMac -ieq $nic.MACAddress);virtualInstance=([string]$nic.PNPDeviceID -like 'ROOT\VMWARE\*');lockedOriginal=$(if($expected.Count -eq 1){$expected[0].original}else{$null});lockedReplacement=$(if($expected.Count -eq 1){$expected[0].replacement}else{$null});replacementEnabled=$false}
})
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$report=[ordered]@{schema='native-wmi-device-originals.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');elevated=([Security.Principal.WindowsPrincipal]::new($identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator));readOnly=$true;nativeExitCode=$nativeExit;nativeEvidence=$nativePath;wmiSources=@('ROOT\CIMV2:Win32_DiskDrive','ROOT\CIMV2:Win32_NetworkAdapter');nativeSources=@('IOCTL_STORAGE_QUERY_PROPERTY:StorageDeviceProperty','GetIfTable2:MIB_IF_ROW2.PhysicalAddress');disks=$diskMatches;adapters=$nicMatches;allWmiDiskRows=$disks;allWmiAdapterRows=$nics;allNativeInterfaces=$native.interfaces;nativeCaptureComplete=$native.captureComplete;replacementEnabled=$false;nativeReplacementProof=$false;note='Current MAC is the mapped field. Permanent MAC is recorded separately and is not relabeled. Missing native rows or source disagreements remain explicit. This tool never applies replacements.'}
$path=Join-Path $probeOutput 'native-wmi-originals.json'
$stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($report|ConvertTo-Json -Depth 10));$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
'READ-ONLY NATIVE/WMI ORIGINALS '+$path
if($nativeExit -ne 0){throw 'A native read was unavailable; the report records exact errors and makes no replacement claim.'}
