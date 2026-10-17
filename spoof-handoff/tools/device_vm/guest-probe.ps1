[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunRoot,[string]$Action='Probe',[string]$ExpectedProbeVerdict='NOT_MET')

$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
$result=[ordered]@{SchemaVersion=1;RunId=(Split-Path -Leaf $RunRoot);Status='FAIL';Message='Inventory did not finish'}
try{
    $machine=Get-CimInstance Win32_ComputerSystem
    if($machine.Manufacturer -notmatch '^VMware'){
        throw ('Read-only inventory requires a VMware guest; manufacturer='+$machine.Manufacturer+' model='+$machine.Model)
    }
    if(-not [IO.Path]::GetFullPath($RunRoot).StartsWith('C:\ProgramData\UEFILoaderRunner\runs\',[StringComparison]::OrdinalIgnoreCase)){
        throw 'Guest evidence path is outside the existing mailbox run root.'
    }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public static class VmStorageNativeProbe {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr CreateFileW(string name,uint access,uint share,IntPtr security,uint disposition,uint flags,IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(IntPtr file,uint code,byte[] input,uint inputLength,byte[] output,uint outputLength,out uint returned,IntPtr overlapped);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr file);
    public static string ReadSerial(string path) {
        IntPtr file=CreateFileW(path,0,3,IntPtr.Zero,3,0,IntPtr.Zero);
        if(file==new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            byte[] query=new byte[12]; byte[] descriptor=new byte[8192]; uint returned;
            if(!DeviceIoControl(file,0x002D1400,query,(uint)query.Length,descriptor,(uint)descriptor.Length,out returned,IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            if(returned<36) throw new InvalidOperationException("Storage descriptor is shorter than its header");
            uint size=BitConverter.ToUInt32(descriptor,4),offset=BitConverter.ToUInt32(descriptor,24);
            if(size<36 || size>returned || offset<36 || offset>=size) throw new InvalidOperationException("Storage serial offset or descriptor size is invalid");
            uint end=offset; while(end<size && descriptor[end]!=0) end++;
            if(end==size || end==offset) throw new InvalidOperationException("Storage serial is absent or unterminated");
            return Encoding.ASCII.GetString(descriptor,(int)offset,(int)(end-offset));
        } finally {CloseHandle(file);}
    }
}
'@
    $os=Get-CimInstance Win32_OperatingSystem
    $version=Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $disk=Get-Disk -Number 0
    $wmiDisks=@(Get-CimInstance Win32_DiskDrive)
    $wmiDisk=@($wmiDisks|Where-Object {$_.DeviceID -ceq '\\.\PHYSICALDRIVE0'})
    if($wmiDisk.Count -ne 1){throw 'Exactly one guest physical-drive-0 WMI record is required.'}
    $nativeSerial=[VmStorageNativeProbe]::ReadSerial('\\.\PHYSICALDRIVE0')
    $pnpId=[string]$wmiDisk[0].PNPDeviceID
    $hardwareIds=@((Get-PnpDeviceProperty -InstanceId $pnpId -KeyName 'DEVPKEY_Device_HardwareIds').Data)
    $secureBoot=$null;$secureBootError=$null
    try{$secureBoot=Confirm-SecureBootUEFI}catch{$secureBootError=$_.Exception.Message}
    $bcd=@(& "$env:SystemRoot\System32\bcdedit.exe" /enum '{current}' 2>&1|ForEach-Object{[string]$_})
    $bcdExit=$LASTEXITCODE
    $record=[ordered]@{
        schema='vm-device-inventory.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');operation='read-only-inventory'
        machine=[ordered]@{manufacturer=[string]$machine.Manufacturer;model=[string]$machine.Model;uuid=[string](Get-CimInstance Win32_ComputerSystemProduct).UUID}
        windows=[ordered]@{build=[string]$version.CurrentBuildNumber;ubr=[int]$version.UBR;displayVersion=[string]$version.DisplayVersion;lastBootUpTime=$os.LastBootUpTime.ToUniversalTime().ToString('o')}
        memory=[ordered]@{totalMiB=[math]::Round($os.TotalVisibleMemorySize/1024);freeMiB=[math]::Round($os.FreePhysicalMemory/1024)}
        disk=[ordered]@{number=0;isBoot=[bool]$disk.IsBoot;isSystem=[bool]$disk.IsSystem;pnpDeviceId=$pnpId;hardwareIds=$hardwareIds;serial=$nativeSerial;wmiSerial=([string]$wmiDisk[0].SerialNumber).Trim();devicePath='\\.\PHYSICALDRIVE0';nativeSource='CreateFileW + IOCTL_STORAGE_QUERY_PROPERTY / StorageDeviceProperty';wmiSource='Fresh Win32_DiskDrive query'}
        adapters=@(Get-CimInstance Win32_NetworkAdapter|Select-Object DeviceID,PNPDeviceID,GUID,Name,MACAddress,PhysicalAdapter)
        policy=[ordered]@{secureBoot=$secureBoot;secureBootReadError=$secureBootError;bcdCurrentExitCode=$bcdExit;bcdCurrentLines=$bcd}
        storageFilterServices=@(Get-CimInstance Win32_SystemDriver -Filter "Name='client_storage_filter'"|Select-Object Name,State,StartMode,PathName)
        hostOrGuestSettingsChanged=$false
    }
    [IO.File]::WriteAllText((Join-Path $RunRoot 'kernel-metadata.json'),(ConvertTo-Json -InputObject $record -Depth 10),[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $RunRoot 'stdout.txt'),'VM inventory collected through native storage IOCTL and fresh WMI; no settings changed.'+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $RunRoot 'stderr.txt'),'', [Text.UTF8Encoding]::new($false))
    $result.Status='PASS';$result.Message='VM read-only baseline collected'
}catch{
    $result.Message=$_.Exception.Message
    [IO.File]::WriteAllText((Join-Path $RunRoot 'stderr.txt'),($_|Out-String),[Text.UTF8Encoding]::new($false))
}finally{
    [IO.File]::WriteAllText((Join-Path $RunRoot 'result.json'),(ConvertTo-Json -InputObject $result -Depth 5),[Text.UTF8Encoding]::new($false))
}
if($result.Status -cne 'PASS'){exit 1}
