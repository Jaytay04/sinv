[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..'))
$evidence=Join-Path $repo 'spoof-handoff\reports\nic-vm-20261017'
$temporary=Join-Path $evidence 'temporary'
[void][IO.Directory]::CreateDirectory($temporary)
$env:TEMP=$temporary;$env:TMP=$temporary
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NicHostIntegrity {
    [StructLayout(LayoutKind.Sequential)] struct CI { public uint Length; public uint Flags; }
    [DllImport("ntdll.dll")] static extern uint NtQuerySystemInformation(int kind, ref CI data, int size, out uint returned);
    public static uint[] Read() { CI data=new CI(); data.Length=8; uint returned; uint status=NtQuerySystemInformation(103,ref data,8,out returned); return new uint[]{status,data.Flags,returned}; }
}
'@
$ci=[NicHostIntegrity]::Read()
if($ci[0] -ne 0){throw ('Read-only kernel policy query failed: 0x{0:X8}' -f $ci[0])}
$thumb='59DBD6780CFE60C020B3F4511211FEC22C397B85'
$trust=[ordered]@{}
foreach($name in @('Root','TrustedPublisher')){
    $store=[Security.Cryptography.X509Certificates.X509Store]::new($name,'LocalMachine')
    try{$store.Open('ReadOnly');$trust[$name]=@($store.Certificates|Where-Object Thumbprint -ceq $thumb).Count -gt 0}finally{$store.Close()}
}
$clone=[IO.File]::ReadAllText((Join-Path $evidence 'clone.json'))|ConvertFrom-Json
$sourceHash=(Get-FileHash -LiteralPath $clone.sourceVmx -Algorithm SHA256).Hash
$vmrun='C:\Program Files\VMware\VMware Workstation\vmrun.exe'
$running=@(& $vmrun list 2>&1|ForEach-Object {$_.ToString()})
if($LASTEXITCODE -ne 0){throw 'VM inventory failed; cannot confirm released VM RAM.'}
$index=(Get-FileHash -LiteralPath (Join-Path $repo '.git\index')).Hash
$head=(& git -C $repo rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0){throw 'Cannot read main checkout HEAD.'}
$windows=Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$record=[ordered]@{
    utc=[DateTime]::UtcNow.ToString('o');readOnlyHostQueries=$true;hostActivationExecuted=$false
    hostWindowsBuild=([string]$windows.CurrentBuildNumber+'.'+[string]$windows.UBR)
    hostSecureBootCachedFlag=[int](Get-ItemPropertyValue 'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State' 'UEFISecureBootEnabled')
    secureBootEvidenceScope='Windows boot-state registry flag; user previously supplied elevated Confirm-SecureBootUEFI=True. No host firmware or boot-policy write was performed.'
    hostKernelIntegrityNtStatus=('0x{0:X8}' -f $ci[0]);hostKernelIntegrityOptions=('0x{0:X8}' -f $ci[1]);hostKernelTestSigning=[bool]($ci[1] -band 2)
    hostTrustMembership=$trust
    hostNicServicePresent=(Test-Path 'HKLM:\SYSTEM\CurrentControlSet\Services\HypersinvNicAddressFilter')
    hostStorageServicePresent=(Test-Path 'HKLM:\SYSTEM\CurrentControlSet\Services\client_storage_filter')
    sourceVmx=$clone.sourceVmx;sourceVmxSha256=$sourceHash;sourceVmxUnchanged=($sourceHash -ceq $clone.sourceSha256Before)
    mainCheckoutHead=$head;mainCheckoutHeadUnchanged=($head -ceq '9b82c1dc5409b36829842bae6ac841ef60481c72')
    mainCheckoutIndexSha256=$index;mainCheckoutIndexUnchanged=($index -ceq 'C15E40DC497E9C24BE73197E96ACCC5B43D3C62F14AB0199C3286BE65198D2F9')
    virtualMachinesRunning=@($running|Where-Object {$_ -match '\.vmx$'}).Count;vmrunList=$running
    hostBcdEspFirmwareScope='No host deployment or changes; no fresh byte-for-byte BCD/ESP/firmware restore claim is made.'
}
$path=Join-Path $evidence 'final-state.json'
if(Test-Path -LiteralPath $path){throw 'Final-state evidence exists; refusing overwrite.'}
[IO.File]::WriteAllText($path,($record|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
$record|ConvertTo-Json -Depth 6
