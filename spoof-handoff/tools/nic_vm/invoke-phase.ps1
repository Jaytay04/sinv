[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Inventory','PreparePolicy','Install','Start','AddressSnapshot','AddressApply','AddressRestore','RemoveForAddressReadback','Readback','Remove','RestorePolicy','Reboot','Shutdown')][string]$Phase,
    [string]$PackageDirectory,
    [string]$NativeExePath,
    [ValidateSet('Original','Replacement')][string]$ExpectedMode='Original',
    [ValidateSet('Any','Present','Absent','AbsentConfigured')][string]$FilterState='Any'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..'))
$evidence=Join-Path $repo 'spoof-handoff\reports\nic-vm-20261017'
$profile=Join-Path $evidence 'guest-profile.json'
$data=[IO.File]::ReadAllText($profile)|ConvertFrom-Json
$vmx=Join-Path $repo 'spoof-handoff\artifacts\nic-vm-20261017\vm-plain\DeviceFilter-Win11.vmx'
$native=if($NativeExePath){[IO.Path]::GetFullPath($NativeExePath)}else{Join-Path $repo 'spoof-handoff\artifacts\nic-vm-20261017\native-build\Release\nic_native_read.exe'}
$action=Join-Path $evidence ('guest-actions\'+$Phase+'.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N')+'.ps1')
$render=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'render-guest.ps1'),'-Phase',$Phase,'-ProfileJson',$profile,'-NativeExePath',$native,'-ExpectedVmUuid',[string]$data.machine.uuid,'-ExpectedMode',$ExpectedMode,'-FilterState',$FilterState,'-OutputPath',$action)
if($PackageDirectory){$render+=@('-PackageDirectory',$PackageDirectory)}
& (Join-Path $PSHOME 'powershell.exe') @render
if($LASTEXITCODE -ne 0){throw 'VM guest rendering failed; no request was sent.'}
$mailbox=Join-Path $PSScriptRoot '..\device_vm\invoke-mailbox.ps1'
& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $mailbox -TestProfile Nic -VmxPath $vmx -GuestScriptPath $action -CompanionExePath (Join-Path $repo '.codex-build\efi_shim_loader\ib-tcg-dump.exe') -OutputDirectory $evidence -ExpectedProbeVerdict NOT_MET
if($LASTEXITCODE -ne 0){throw ('NIC VM phase failed: '+$Phase+'. Preserve the exact evidence under '+$evidence+'; recover the disposable VM before another install.')}
