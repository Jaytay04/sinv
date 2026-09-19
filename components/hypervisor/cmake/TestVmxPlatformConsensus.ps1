param(
 [Parameter(Mandatory=$true)][string]$PolicyPath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$AdapterPath,
 [Parameter(Mandatory=$true)][string]$AdapterHeaderPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$InterfacePath,
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
 $Interface=$Text[6];$Unit=$Text[7];$Gate=$Text[8]
 $Roadmap=$Text[9];$Doc=$Text[10];$CapabilityAbi=$Text[11]
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput(
  $Gate,[ref]$Tokens,[ref]$Errors)
 A ($Errors.Count-eq0) 'Gate 61 does not parse'
 Has $Header @('VMX_PLATFORM_COMPATIBILITY_CONSENSUS_VERSION',
  'VMX_PLATFORM_COMPATIBILITY_NO_PROCESSOR',
  'VMX_PLATFORM_COMPATIBILITY_CONSENSUS_COLLECTING',
  'VMX_PLATFORM_COMPATIBILITY_CONSENSUS_ADMITTED',
  'VMX_PLATFORM_COMPATIBILITY_CONSENSUS_DENIED',
  'VMX_PLATFORM_COMPATIBILITY_CONSENSUS',
  'VmxPlatformCompatibilityConsensusBegin',
  'VmxPlatformCompatibilityConsensusObserve',
  'VmxPlatformCompatibilityConsensusComplete') 'consensus header'
 Has $Policy @('ConsensusCollectingValid',
  'ExpectedProcessors == 0','SystemIndex != Consensus->ObservedProcessors',
  '!VmxPlatformCompatibilityAllowsVmx(Classification)',
  'Staged.ConsensusClassification != (uint32_t)Classification',
  'Staged.Status = VMX_PLATFORM_COMPATIBILITY_CONSENSUS_DENIED',
  'Consensus->ObservedProcessors != Consensus->ExpectedProcessors',
  'Staged.Status = VMX_PLATFORM_COMPATIBILITY_CONSENSUS_ADMITTED') `
  'consensus policy'
 A ($Policy-notmatch'(?i)ntddk|wdf|__cpuid|__vmx|vmxon|vmwrite|KeSetSystemGroupAffinityThread') `
  'pure consensus policy gained platform or VMX dependencies'
 Has $AdapterHeader @('VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_STATUS',
  'VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_RESULT',
  'VmxPlatformCompatibilityQueryTopology') 'topology adapter header'
 $TopologyStart=$Adapter.IndexOf('VmxPlatformCompatibilityQueryTopology(')
 A ($TopologyStart-ge0) 'topology adapter missing'
 $Topology=$Adapter.Substring($TopologyStart)
 Ordered $Topology @('KeGetCurrentIrql() != PASSIVE_LEVEL',
  'KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS)',
  'VmxPlatformCompatibilityConsensusBegin(',
  'for (SystemIndex = 0; SystemIndex < ActiveProcessors; ++SystemIndex)',
  'KeGetProcessorNumberFromIndex(SystemIndex, &Target)',
  'KeSetSystemGroupAffinityThread(&Affinity, &Previous)',
  'KeGetCurrentProcessorNumberEx(&Current)',
  'CurrentIndex != SystemIndex',
  'Classification = VmxPlatformCompatibilityQuery()',
  'KeRevertToUserGroupAffinityThread(&Previous)',
  'VmxPlatformCompatibilityConsensusObserve(&Staged.Consensus',
  'VmxPlatformCompatibilityConsensusComplete(&Staged.Consensus)',
  'VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_ADMITTED') 'topology adapter'
 A ([regex]::Matches($Topology,
  [regex]::Escape('KeRevertToUserGroupAffinityThread(&Previous);')).Count-eq2) `
  'topology adapter must restore once on mismatch and once on normal sampling'
 A ($Topology-notmatch'(?i)ExAllocate|WdfMemoryCreate|malloc|__vmx|vmxon|vmwrite|__writecr[0348]') `
  'topology adapter gained allocation or VMX/control-register mutation'
 $LoadStart=$Ioctl.IndexOf('IoctlDispatchLoad(')
 $LoadEnd=$Ioctl.IndexOf('IoctlDispatchUnload(',$LoadStart)
 A ($LoadStart-ge0-and$LoadEnd-gt$LoadStart) 'kernel LOAD section missing'
 $Load=$Ioctl.Substring($LoadStart,$LoadEnd-$LoadStart)
 Ordered $Load @('VmxPersistentDriverOwnerQueryPhase',
  'phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED',
  'VmxPlatformCompatibilityQueryTopology(&platform_result)',
  'platform_status != VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_ADMITTED',
  'ERR_PERSISTENT_PLATFORM_CONFLICT',
  'VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic(') 'kernel consensus boundary'
 Has $Load @(
  'hypersinv: VMX substrate consensus denied status=%lu cpu=%lu',
  'hypersinv: VMX substrate consensus admitted class=%lu processors=%lu',
  'platform_result.Consensus.ConsensusClassification',
  'platform_result.Consensus.ObservedProcessors') 'kernel consensus evidence'
 $LoaderStart=$Loader.IndexOf('static int CmdLoadInternal(')
 $LoaderEnd=$Loader.IndexOf('static int CmdLoad(',$LoaderStart)
 A ($LoaderStart-ge0-and$LoaderEnd-gt$LoaderStart) 'loader LOAD section missing'
 Ordered $Loader.Substring($LoaderStart,$LoaderEnd-$LoaderStart) @(
  'VerifyPlatformCompatibilityBeforeScm()','OpenScm()','CreateDriverService()') `
  'loader early admission'
 Has $Loader @('Kernel VMX substrate consensus rejected status',
  'at system CPU %lu before runtime configuration.') 'loader diagnostics'
 Has $Unit @('TestNativeConsensus','TestVmwareConsensus',
  'TestMixedConsensusRejected','TestDisallowedConsensusRejected',
  'TestConsensusFramingRejected') 'consensus unit tests'
 Has $Gate @('gate61-vmx-substrate-consensus',
  'Probe=VMX_SUBSTRATE_CONSENSUS','ConsensusProcessors=4',
  'KernelConsensusEvidence=EXTERNAL_KD','VMX substrate: VMWARE_NESTED',
  "Invoke-Loader 'load' 'load'", "Invoke-Loader 'unload' 'unload'",
  'ServiceAfterUnload=ABSENT','tar.exe -a -c -f') 'Gate 61'
 A ($Gate-notmatch'(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
  'Gate 61 mutates boot, signing, service cleanup, or reboot state'
 Has $Roadmap @('Runtime V23 - Topology-Wide VMX Substrate Consensus',
  'Gate 61','two-group','A true failure') 'roadmap'
 Has $Doc @('Gate 61: topology-wide VMX substrate consensus',
  'gate61-vmx-substrate-consensus.ps1') 'test documentation'
 Has $Interface @('#define ERR_PERSISTENT_PLATFORM_CONFLICT 0x8000004E') `
  'existing error contract'
 Has $CapabilityAbi @(
  '#define HYPERVISOR_CONTROL_CAPABILITIES_ALL UINT64_C(0x00000000000007FF)',
  '#define HYPERVISOR_CONTROL_OPERATIONAL_IOCTL_COUNT UINT32_C(30)') `
  'capability manifest'
 $Ioctls=@([regex]::Matches($Interface,
  '(?m)^#define\s+IOCTL_HYPERVISOR_[A-Z0-9_]+\s+.*CTL_CODE\([^\r\n]+\r?$'))
 A ($Ioctls.Count-eq31) "public IOCTL inventory changed: $($Ioctls.Count)"
}
$Paths=@($PolicyPath,$HeaderPath,$AdapterPath,$AdapterHeaderPath,$IoctlPath,
 $LoaderPath,$InterfacePath,$UnitTestPath,$GatePath,$RoadmapPath,$DocPath,
 $CapabilityAbiPath)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace(
   'Staged.ConsensusClassification != (uint32_t)Classification','false')},
 @{I=2;T=$Text[2].Replace('CurrentIndex != SystemIndex','false')},
 @{I=2;T=$Text[2].Replace(
   'KeRevertToUserGroupAffinityThread(&Previous);','')},
 @{I=4;T=$Text[4].Replace(
   'platform_status != VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_ADMITTED','false')},
 @{I=4;T=$Text[4].Replace('class=%lu processors=%lu','class=%lu')},
 @{I=8;T=$Text[8].Replace("'ConsensusProcessors=4'",
   "'ConsensusProcessors=1'")})
$MutationNumber=0
foreach($Mutation in $Mutations){
 $MutationNumber++
 A ($Mutation.T-ne$Text[$Mutation.I]) 'consensus mutation was vacuous'
 $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
 try{Verify $Copy}catch{$Rejected=$true}
 A $Rejected "consensus contract accepted mutation $MutationNumber"
}
Write-Host 'PASS: topology-wide VMX substrate consensus contracts verified.'
