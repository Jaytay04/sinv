[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('PreparePolicy','Install','Readback','Remove','Reboot','Shutdown')][string]$Operation,
    [string]$PackageDirectory,
    [ValidateSet('Original','Replacement')][string]$ExpectedMode='Original',
    [ValidateSet('Any','Present','Absent')][string]$FilterState='Any',
    [string]$ReplacementSerial='1111_1111_1111_1111_A1B2_C3D4_5566_7788.'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..'))
$evidence=Join-Path $repo 'spoof-handoff\reports\device-vm-20261017'
$baseline=Join-Path $evidence 'uefi-windows-probe-20261017-053613-3871a92bfa\evidence\kernel-metadata.json'
$vmx=Join-Path $repo 'spoof-handoff\artifacts\device-vm-20261017\vm-plain\DeviceFilter-Win11.vmx'
$action=Join-Path $evidence ('guest-actions\'+$Operation+'.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N')+'.ps1')
$render=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'render-guest-action.ps1'),'-GuestInventory',$baseline,'-Operation',$Operation,'-OutputPath',$action,'-ExpectedMode',$ExpectedMode,'-FilterState',$FilterState,'-ReplacementSerial',$ReplacementSerial)
if($PackageDirectory){$render+=@('-PackageDirectory',$PackageDirectory)}
& (Join-Path $PSHOME 'powershell.exe') @render
if($LASTEXITCODE){throw 'Guest stage rendering failed; no new guest request was sent.'}
& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'invoke-mailbox.ps1') -VmxPath $vmx -GuestScriptPath $action -CompanionExePath (Join-Path $repo '.codex-build\efi_shim_loader\ib-tcg-dump.exe') -OutputDirectory $evidence -ExpectedProbeVerdict NOT_MET
if($LASTEXITCODE){throw ('VM stage failed: '+$Operation+'; exact guest evidence is under '+$evidence)}
