param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath,
 [Parameter(Mandatory=$true)][string]$HypervisorCMake,
 [Parameter(Mandatory=$true)][string]$TestsCMake,
 [Parameter(Mandatory=$true)][string]$ControlsPath,
 [Parameter(Mandatory=$true)][string]$RoadmapPath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Verify([string]$Source,[string]$Header,[string]$Test,[string]$HvCMake,
 [string]$TestCMake,[string]$Controls,[string]$Roadmap,[string]$DriverMain,
 [string]$Ioctl){
 foreach($Token in @(
  'VPID_CAP_INVVPID_BIT','UINT64_C(1) << 32',
  'VPID_CAP_INDIVIDUAL_ADDRESS_BIT','UINT64_C(1) << 40',
  'VPID_CAP_SINGLE_CONTEXT_BIT','UINT64_C(1) << 41',
  'VPID_CAP_ALL_CONTEXTS_BIT','UINT64_C(1) << 42',
  'VPID_CAP_SINGLE_CONTEXT_RETAINING_GLOBALS_BIT','UINT64_C(1) << 43',
  'Capabilities->Flags != DecodeFlags(Capabilities->Raw)',
  'VMX_VPID_CAP_RUNTIME_REQUIRED',
  'VMX_VPID_CAP_INVVPID | VMX_VPID_CAP_SINGLE_CONTEXT |',
  'VMX_VPID_CAP_ALL_CONTEXTS)','VMX_VPID_CAPABILITY_UNSUPPORTED')){
  A ($Source.Contains($Token)-or$Header.Contains($Token)) "VPID model missing: $Token"
 }
 foreach($Token in @('VPID_SECONDARY_ENABLE_ALLOWED1_BIT',
  'UINT64_C(1) << (32 + 5)','VmxVpidValidateSecondaryControls(')){
  A ($Source.Contains($Token)-or$Header.Contains($Token)) "VPID secondary-control model missing: $Token"
 }
 A ($Source-notmatch'(?i)ntddk|wdm\.h|wdf\.h|__vmx|__invvpid|vmread|vmwrite|IOCTL') 'VPID model is not pure'
 foreach($Token in @('required decode','optional flags','missing required rejected',
  'raw flag mismatch','reserved rejected','null decode output',
  'VPID secondary allowed-1 accepted','missing VPID secondary allowed-1 rejected',
  'allowed-0 half cannot authorize VPID')){
  A $Test.Contains($Token) "VPID test missing: $Token"
 }
 A (([regex]::Matches($HvCMake,'(?m)^\s*vmx_vpid_capability\.c\s*$')).Count-eq1) 'VPID source not wired exactly once'
 foreach($Token in @('test_vmx_vpid_capability','test_vmx_vpid_capability_contracts',
  'TestVmxVpidCapabilityModel.ps1')){A $TestCMake.Contains($Token) "VPID CMake missing: $Token"}
 A $Controls.Contains('No secondary feature has backing state in this runtime.') 'accepted no-VPID control boundary missing'
 A ($Controls-notmatch'SECONDARY_ENABLE_VPID|VMCS_CTRL_VPID|VmxVpid') 'V4A enabled VPID controls'
 A ($DriverMain-notmatch'VmxVpid|vmx_vpid_capability') 'driver entry bypasses the composed VPID lifecycle'
 A ($Ioctl.Contains('IOCTL_HYPERVISOR_VPID_STATUS_QUERY')-and
    $Ioctl.Contains('VmxVpidPersistentOwnerQuery(')) 'later V4 truthful-status bridge missing'
 foreach($Token in @('Runtime V4 Roadmap - VPID and Translation-Context Lifecycle',
  'V4A Capability model','VPID remains disabled','Do not add an intrinsic')){
  A $Roadmap.Contains($Token) "VPID roadmap missing: $Token"
 }
}
$Text=@($SourcePath,$HeaderPath,$TestPath,$HypervisorCMake,$TestsCMake,
 $ControlsPath,$RoadmapPath,$DriverMainPath,$IoctlPath|ForEach-Object{
  [IO.File]::ReadAllText((Resolve-Path $_))})
Verify @Text
$Mutations=@(
 @{Index=0;Text=$Text[0].Replace('UINT64_C(1) << 41','UINT64_C(1) << 39')},
 @{Index=0;Text=$Text[0].Replace('Capabilities->Flags != DecodeFlags(Capabilities->Raw)','0')},
 @{Index=1;Text=$Text[1].Replace(
  'VMX_VPID_CAP_INVVPID | VMX_VPID_CAP_SINGLE_CONTEXT |',
  'VMX_VPID_CAP_INVVPID |')},
 @{Index=5;Text=$Text[5] + "`n#define SECONDARY_ENABLE_VPID 1"})
foreach($Mutation in $Mutations){
 A ($Mutation.Text-ne$Text[$Mutation.Index]) 'VPID mutation was vacuous'
 $Copy=$Text.Clone();$Copy[$Mutation.Index]=$Mutation.Text;$Rejected=$false
 try{Verify @Copy}catch{$Rejected=$true}
 A $Rejected "VPID contract accepted mutation index $($Mutation.Index)"
}
Write-Host 'PASS: pure VPID capability decoding is exact, fail-closed, mutation-backed, and isolated from the later composed lifecycle.'
