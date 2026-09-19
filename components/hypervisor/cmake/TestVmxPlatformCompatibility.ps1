param(
 [Parameter(Mandatory=$true)][string]$PolicyPath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$AdapterPath,
 [Parameter(Mandatory=$true)][string]$AdapterHeaderPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$InterfacePath,
 [Parameter(Mandatory=$true)][string]$CoreCMakePath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$LoaderCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$UnitTestPath,
 [Parameter(Mandatory=$true)][string]$GatePath,
 [Parameter(Mandatory=$true)][string]$RoadmapPath,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$CapabilityAbiPath)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Has([string]$Text,[string[]]$Tokens,[string]$Name){
 foreach($Token in $Tokens){A ($Text.Contains($Token)) "$Name missing: $Token"}
}
function Ordered([string]$Text,[string[]]$Tokens,[string]$Name){
 $Position=-1
 foreach($Token in $Tokens){
  $Next=$Text.IndexOf($Token,$Position+1)
  A ($Next-gt$Position) "$Name order: $Token"
  $Position=$Next
 }
}
function Verify([string[]]$Text){
 $Policy=$Text[0];$Header=$Text[1];$Adapter=$Text[2]
 $AdapterHeader=$Text[3];$Ioctl=$Text[4];$Loader=$Text[5]
 $Interface=$Text[6];$CoreCMake=$Text[7];$DriverCMake=$Text[8]
 $LoaderCMake=$Text[9];$TestsCMake=$Text[10];$Unit=$Text[11]
 $Gate=$Text[12];$Roadmap=$Text[13];$Doc=$Text[14];$CapabilityAbi=$Text[15]
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput(
  $Gate,[ref]$Tokens,[ref]$Errors)
 A ($Errors.Count-eq0) 'Gate 60 does not parse'
 Has $Header @('VMX_PLATFORM_CPUID_IDENTITY',
  'VMX_PLATFORM_COMPATIBILITY_NATIVE',
  'VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED',
  'VMX_PLATFORM_COMPATIBILITY_MICROSOFT_HYPERV',
  'VMX_PLATFORM_COMPATIBILITY_UNSUPPORTED',
  'VmxPlatformCompatibilityClassify',
  'VmxPlatformCompatibilityAllowsVmx') 'policy header'
 Has $Policy @('VENDOR_VMWARE_EBX','VENDOR_VMWARE_ECX',
  'VENDOR_VMWARE_EDX','VENDOR_HYPERV_EBX','VENDOR_HYPERV_ECX',
  'VENDOR_HYPERV_EDX','Identity->Reserved != 0',
  'Identity->HypervisorPresent > VMX_PLATFORM_HYPERVISOR_PRESENT',
  'Identity->MaximumHypervisorLeaf < VMX_PLATFORM_HYPERVISOR_LEAF_MIN',
  'Classification == VMX_PLATFORM_COMPATIBILITY_NATIVE ||',
  'Classification == VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED',
  'return VMX_PLATFORM_COMPATIBILITY_UNSUPPORTED;') 'policy'
 A ($Policy-notmatch'(?i)ntddk|wdf|__cpuid|__vmx|vmxon|vmwrite|DeviceIoControl') `
  'pure policy gained platform or VMX dependencies'
 Has $AdapterHeader @('VmxPlatformCompatibilityQuery',
  'VmxPlatformCompatibilityQueryTopology') 'adapter header'
 Has $Adapter @('__cpuid(Registers, 1)',
  'Identity.HypervisorPresent',
  '__cpuid(Registers, (int)VMX_PLATFORM_HYPERVISOR_LEAF_MIN)',
  'VmxPlatformCompatibilityClassify(&Identity)') 'kernel adapter'
 $LoadStart=$Ioctl.IndexOf('IoctlDispatchLoad(')
 $LoadEnd=$Ioctl.IndexOf('IoctlDispatchUnload(',$LoadStart)
 A ($LoadStart-ge0-and$LoadEnd-gt$LoadStart) 'kernel LOAD section missing'
 $Load=$Ioctl.Substring($LoadStart,$LoadEnd-$LoadStart)
 Ordered $Load @('VmxPersistentDriverOwnerQueryPhase',
  'phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED',
  'VmxPlatformCompatibilityQueryTopology(&platform_result)',
  'platform_status != VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_ADMITTED',
  'ERR_PERSISTENT_PLATFORM_CONFLICT',
  'VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic(') 'kernel boundary'
 Has $Load @('DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,',
  'hypersinv: VMX substrate consensus denied status=%lu cpu=%lu',
  'hypersinv: VMX substrate consensus admitted class=%lu processors=%lu') `
  'kernel debugger evidence'
 $LoaderStart=$Loader.IndexOf('static int CmdLoadInternal(')
 $LoaderEnd=$Loader.IndexOf('static int CmdLoad(',$LoaderStart)
 A ($LoaderStart-ge0-and$LoaderEnd-gt$LoaderStart) 'loader LOAD section missing'
 $LoaderLoad=$Loader.Substring($LoaderStart,$LoaderEnd-$LoaderStart)
 Ordered $LoaderLoad @('VerifyPlatformCompatibilityBeforeScm()',
  'OpenScm()','CreateDriverService()','StartDriverService()','OpenDevice()') `
  'loader boundary'
 Has $Loader @('VMX substrate: %s',
  'SUCCESS: VMX substrate admitted before SCM mutation.',
  'Microsoft Hyper-V/VBS owns the virtualization substrate',
  'only native Intel VMX and VMware nested VMX are admitted',
  'Kernel VMX substrate consensus rejected status') 'loader diagnostics'
 Has $Interface @('#define ERR_PERSISTENT_PLATFORM_CONFLICT 0x8000004E',
  '#include "control_capabilities_abi.h"') 'public contract'
 Has $CapabilityAbi @(
  '#define HYPERVISOR_CONTROL_CAPABILITIES_ALL UINT64_C(0x00000000000007FF)',
  '#define HYPERVISOR_CONTROL_OPERATIONAL_IOCTL_COUNT UINT32_C(30)') `
  'capability manifest'
 $Ioctls=@([regex]::Matches($Interface,
  '(?m)^#define\s+IOCTL_HYPERVISOR_[A-Z0-9_]+\s+.*CTL_CODE\([^\r\n]+\r?$'))
 A ($Ioctls.Count-eq31) "public IOCTL inventory changed: $($Ioctls.Count)"
 Has $CoreCMake @('vmx_platform_compatibility.c') 'core build'
 Has $DriverCMake @('vmx_platform_compatibility_adapter.c') 'driver build'
 Has $LoaderCMake @('vmx_platform_compatibility.c') 'loader build'
 Has $TestsCMake @('test_vmx_platform_compatibility',
  'test_vmx_platform_compatibility.c') 'test registration'
 Has $Unit @('TestNativeAdmission','TestVmwareAdmission',
  'TestHyperVRejected','TestUnknownRejected','TestMalformedRejected') `
  'unit tests'
 Has $Gate @('gate60-vmx-substrate-admission',
  'VMX substrate: VMWARE_NESTED',
  'SUCCESS: VMX substrate admitted before SCM mutation.',
  'Probe=VMX_SUBSTRATE_ADMISSION','KernelAdmission=BEFORE_RUNTIME_CONFIG',
  "Invoke-Loader 'load' 'load'", "Invoke-Loader 'unload' 'unload'",
  'ServiceAfterUnload=ABSENT','tar.exe -a -c -f') 'Gate 60'
 A ($Gate-notmatch'(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
  'Gate 60 mutates boot, signing, service cleanup, or reboot state'
 Has $Roadmap @('Runtime V22 - Fail-Closed VMX Substrate Admission',
  'Microsoft Hyper-V','Gate 60','A true live failure') 'roadmap'
 Has $Doc @('Gate 60: fail-closed VMX substrate admission',
  'gate60-vmx-substrate-admission.ps1') 'test documentation'
}
$Paths=@($PolicyPath,$HeaderPath,$AdapterPath,$AdapterHeaderPath,$IoctlPath,
 $LoaderPath,$InterfacePath,$CoreCMakePath,$DriverCMakePath,$LoaderCMakePath,
 $TestsCMakePath,$UnitTestPath,$GatePath,$RoadmapPath,$DocPath,
 $CapabilityAbiPath)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace(
   'Classification == VMX_PLATFORM_COMPATIBILITY_NATIVE ||',
   'Classification == VMX_PLATFORM_COMPATIBILITY_MICROSOFT_HYPERV ||')},
 @{I=0;T=$Text[0].Replace(
   'return VMX_PLATFORM_COMPATIBILITY_UNSUPPORTED;',
   'return VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED;')},
 @{I=4;T=$Text[4].Replace(
   'platform_status != VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_ADMITTED','false')},
 @{I=4;T=$Text[4].Replace(
   'VmxPlatformCompatibilityQueryTopology(&platform_result)',
   'platform_status = VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_ADMITTED')},
 @{I=4;T=$Text[4].Replace('DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,',
   'KdPrint((')},
 @{I=5;T=$Text[5].Replace('if (!VerifyPlatformCompatibilityBeforeScm()) return 1;',
   'if (!OpenScm()) return 1;')},
 @{I=12;T=$Text[12].Replace('VMX substrate: VMWARE_NESTED',
   'VMX substrate: NATIVE')})
$MutationNumber=0
foreach($Mutation in $Mutations){
 $MutationNumber++
 A ($Mutation.T-ne$Text[$Mutation.I]) 'substrate mutation was vacuous'
 $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
 try{Verify $Copy}catch{$Rejected=$true}
 A $Rejected "substrate contract accepted mutation $MutationNumber"
}
Write-Host 'PASS: fail-closed VMX substrate admission contracts verified.'
