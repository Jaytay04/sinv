param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath,
 [Parameter(Mandatory=$true)][string]$HypervisorCMake,
 [Parameter(Mandatory=$true)][string]$TestsCMake,
 [Parameter(Mandatory=$true)][string]$ControlsPath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Verify([string]$Source,[string]$Header,[string]$Test,[string]$Hv,
 [string]$Tests,[string]$Controls,[string]$DriverMain,[string]$Ioctl){
 foreach($Token in @('VMX_VPID_RECORD_RETIRE_PENDING','VMX_VPID_MAX UINT16_MAX',
  'Record->Vpid != (uint16_t)(Index + 1)','Identity.SystemIndex != (uint32_t)Index',
  'VmxVpidOwnerAcknowledgeInvalidation','Record->State = VMX_VPID_RECORD_FREE',
  'VmxVpidOwnerDestroy')){A ($Source.Contains($Token)-or$Header.Contains($Token)) "VPID owner missing: $Token"}
 foreach($Token in @('multi-group initialize','VPID exhaustion rejected',
  'duplicate ownership rejected','reuse before invalidation rejected',
  'successful invalidation acknowledged','aliased output rejected')){
  A $Test.Contains($Token) "VPID owner test missing: $Token"
 }
 A ($Source-notmatch'(?i)ntddk|wdm\.h|wdf\.h|__vmx|invvpid|vmread|vmwrite|IOCTL') 'VPID owner is not pure'
 A (([regex]::Matches($Hv,'(?m)^\s*vmx_vpid_owner\.c\s*$')).Count-eq1) 'VPID owner source not wired exactly once'
 foreach($Token in @('test_vmx_vpid_owner','test_vmx_vpid_owner_contracts','TestVmxVpidOwner.ps1')){A $Tests.Contains($Token) "VPID owner CMake missing: $Token"}
 A ($Controls-notmatch'SECONDARY_ENABLE_VPID|VMCS_CTRL_VPID|VmxVpid') 'V4B enabled VPID controls'
 A ($DriverMain-notmatch'VmxVpid|vmx_vpid_owner') 'driver entry bypasses the composed VPID lifecycle'
 A ($Ioctl.Contains('IOCTL_HYPERVISOR_VPID_STATUS_QUERY')-and
    $Ioctl.Contains('VmxVpidPersistentOwnerQuery(')) 'later V4 truthful-status bridge missing'
}
$Text=@($SourcePath,$HeaderPath,$TestPath,$HypervisorCMake,$TestsCMake,
 $ControlsPath,$DriverMainPath,$IoctlPath|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify @Text
$Mutations=@(
 @{Index=0;Text=$Text[0].Replace('Record->State = VMX_VPID_RECORD_FREE;','Record->State = VMX_VPID_RECORD_OWNED;')},
 @{Index=0;Text=$Text[0].Replace('Record->Vpid != (uint16_t)(Index + 1)','0')},
 @{Index=2;Text=$Text[2].Replace('reuse before invalidation rejected','reuse allowed')},
 @{Index=5;Text=$Text[5]+"`n#define SECONDARY_ENABLE_VPID 1"})
foreach($Mutation in $Mutations){
 A ($Mutation.Text-ne$Text[$Mutation.Index]) 'VPID owner mutation was vacuous'
 $Copy=$Text.Clone();$Copy[$Mutation.Index]=$Mutation.Text;$Rejected=$false
 try{Verify @Copy}catch{$Rejected=$true}
 A $Rejected "VPID owner contract accepted mutation index $($Mutation.Index)"
}
Write-Host 'PASS: group-safe VPID ownership is pure, nonzero, alias-free, and invalidation-gated.'
