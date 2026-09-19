param(
 [Parameter(Mandatory=$true)][string]$PolicyPath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$AdapterPath,
 [Parameter(Mandatory=$true)][string]$AdapterHeaderPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$ConfigPath,
 [Parameter(Mandatory=$true)][string]$ConfigHeaderPath,
 [Parameter(Mandatory=$true)][string]$UnitTestPath,
 [Parameter(Mandatory=$true)][string]$GatePath,
 [Parameter(Mandatory=$true)][string]$RoadmapPath,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$InterfacePath,
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
function Section([string]$Text,[string]$Begin,[string]$End){
 $Start=$Text.IndexOf($Begin);$Finish=$Text.IndexOf($End,$Start+$Begin.Length)
 A ($Start-ge0-and$Finish-gt$Start) "section missing: $Begin"
 $Text.Substring($Start,$Finish-$Start)
}
function Verify([string[]]$Text){
 $Policy=$Text[0];$Header=$Text[1];$Adapter=$Text[2]
 $AdapterHeader=$Text[3];$Ioctl=$Text[4];$Config=$Text[5]
 $ConfigHeader=$Text[6];$Unit=$Text[7];$Gate=$Text[8]
 $Roadmap=$Text[9];$Doc=$Text[10];$Interface=$Text[11]
 $CapabilityAbi=$Text[12];$Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput(
  $Gate,[ref]$Tokens,[ref]$Errors)
 A ($Errors.Count-eq0) 'Gate 62 does not parse'
 Has $Header @('VMX_PLATFORM_READINESS_CONSENSUS_VERSION',
  'VMX_PLATFORM_READINESS_CONSENSUS_VERSION UINT32_C(3)',
  'VMX_PLATFORM_READINESS_SAMPLE','VMX_PLATFORM_READINESS_CONSENSUS',
  'VMX_PLATFORM_READINESS_REASON_CPUID_VMX',
  'VMX_PLATFORM_READINESS_REASON_CET_ACTIVE',
  'VMX_PLATFORM_READINESS_REASON_FEATURE_CONTROL',
  'VMX_PLATFORM_READINESS_REASON_VMX_BASIC',
  'VMX_PLATFORM_READINESS_REASON_VMX_BASIC_MISMATCH',
  'VMX_PLATFORM_READINESS_REASON_TIMER_CAPABILITY_MISMATCH',
  'VMX_PLATFORM_READINESS_REASON_TIMER_RATE_MISMATCH',
  'uint64_t VmxMisc','uint64_t PinControls',
  'uint32_t PreemptionTimerRate','uint32_t PreemptionTimerAvailable',
  'VmxPlatformReadinessSampleValidate',
  'VmxPlatformReadinessConsensusBegin',
  'VmxPlatformReadinessConsensusObserve',
  'VmxPlatformReadinessConsensusComplete') 'readiness header'
 Ordered $Policy @('Sample->Reserved != 0','Sample->CpuidVmx != 1',
  '(Sample->Cr4 & CR4_CET) != 0','VmxPersistentPreentryValidate(',
  'VmxParseBasicCapabilities(Sample->VmxBasic',
  'SystemIndex != Consensus->ObservedProcessors',
  'VmxPlatformReadinessSampleValidate(Sample, &Reason)',
  'VmxSplitControlMsr(Sample->PinControls',
  'PIN_BASED_ACTIVATE_PREEMPTION_TIMER',
  'Staged.VmxBasic != Sample->VmxBasic',
  'VMX_PLATFORM_READINESS_REASON_VMX_BASIC_MISMATCH',
  'Staged.PreemptionTimerAvailable != TimerAvailable',
  'VMX_PLATFORM_READINESS_REASON_TIMER_CAPABILITY_MISMATCH',
  'Staged.PreemptionTimerRate !=',
  'VMX_PLATFORM_READINESS_REASON_TIMER_RATE_MISMATCH',
  'Consensus->ObservedProcessors != Consensus->ExpectedProcessors',
  'VMX_PLATFORM_READINESS_CONSENSUS_ADMITTED') 'readiness policy'
 A ($Policy-notmatch'(?i)ntddk|wdf|__cpuid|__readmsr|__readcr|__vmx|KeSetSystem') `
  'pure readiness policy gained platform or VMX dependencies'
 Has $AdapterHeader @('VMX_PLATFORM_READINESS_TOPOLOGY_STATUS',
  'VMX_PLATFORM_READINESS_TOPOLOGY_RESULT',
  'VmxPlatformReadinessQueryTopology') 'readiness adapter header'
 $Topology=$Adapter.Substring($Adapter.IndexOf(
  'VmxPlatformReadinessQueryTopology('))
 Ordered $Topology @('KeGetCurrentIrql() != PASSIVE_LEVEL',
  'KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS)',
  'VmxPlatformReadinessConsensusBegin(',
  'for (SystemIndex = 0; SystemIndex < ActiveProcessors; ++SystemIndex)',
  'KeGetProcessorNumberFromIndex(SystemIndex, &Target)',
  'KeSetSystemGroupAffinityThread(&Affinity, &Previous)',
  'KeGetCurrentProcessorNumberEx(&Current)',
  'CurrentIndex != SystemIndex','__cpuid(Registers, 1)',
  'Sample.Cr4 = __readcr4()','Sample.FeatureControl = __readmsr(',
  'Sample.VmxBasic = __readmsr(IA32_VMX_BASIC)',
  'Sample.VmxMisc = __readmsr(IA32_VMX_MISC)',
  'Sample.PinControls = __readmsr(',
  'KeRevertToUserGroupAffinityThread(&Previous)',
  'VmxPlatformReadinessConsensusObserve(&Staged.Consensus',
  'VmxPlatformReadinessConsensusComplete(&Staged.Consensus)',
  'VMX_PLATFORM_READINESS_TOPOLOGY_ADMITTED') 'readiness adapter'
 A ([regex]::Matches($Topology,
  [regex]::Escape('KeRevertToUserGroupAffinityThread(&Previous);')).Count-eq2) `
  'readiness adapter must restore mismatch and normal samples'
 A ($Topology-notmatch'(?i)ExAllocate|WdfMemoryCreate|malloc|__vmx|vmxon|vmwrite|__writecr') `
  'readiness adapter gained allocation or mutation'
 $Load=Section $Ioctl 'IoctlDispatchLoad(' 'IoctlDispatchUnload('
 Ordered $Load @('VmxPlatformCompatibilityQueryTopology(&platform_result)',
  'platform_status != VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_ADMITTED',
  'VmxPlatformReadinessQueryTopology(&readiness_result)',
  'readiness_status != VMX_PLATFORM_READINESS_TOPOLOGY_ADMITTED',
  'ERR_PERSISTENT_CONFIG_FAILED',
  'VMX preentry consensus admitted revision=%lu',
  'processors=%lu vmx_basic=0x%llx preemption-timer=%lu',
  'preemption-rate=%lu',
  'before runtime configuration',
  'VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic(',
  'readiness_result.Consensus.VmxBasic, &config)',
  'VmxEptPersistentDeviceStart(device_context, &config)') 'LOAD readiness boundary'
 Has $ConfigHeader @('VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic',
  'does not resample a caller CPU','Per-processor preentry checks remain active') `
  'config header'
 $Approved=Section $Config 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuild('
 Has $Approved @('BuildApprovedConfig(vmx_basic',
  'KernelPreflightPlatform.GenerateRandom') 'approved config handoff'
 A ($Approved-notmatch'ReadCr4|CheckCpuidVmx|ReadMsr|IA32_VMX_BASIC') `
  'approved config resamples a caller CPU'
 Has $Unit @('TestValidSample','TestInvalidSamples','TestUniformConsensus',
  'TestUniformTimerAbsenceAdmitted','TestMismatchRejected',
  'TestConsensusFraming','TestTimerCapabilityMismatchRejected',
  'TestTimerRateMismatchRejected') 'readiness unit tests'
 Has $Gate @('gate62-vmx-preentry-readiness',
  'Probe=VMX_PREENTRY_READINESS','ReadinessProcessors=4',
  'KernelReadinessEvidence=EXTERNAL_KD',"Invoke-Loader 'load' 'load'",
  "Invoke-Loader 'unload' 'unload'",'ServiceAfterUnload=ABSENT',
  'tar.exe -a -c -f') 'Gate 62'
 A ($Gate-notmatch'(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
  'Gate 62 mutates boot, signing, cleanup, or reboot state'
 Has $Roadmap @('Runtime V24 - Topology-Wide VMX Preentry Readiness',
  'Gate 62','two-group','A true failure') 'roadmap'
 Has $Doc @('Gate 62: topology-wide VMX preentry readiness',
  'gate62-vmx-preentry-readiness.ps1') 'test documentation'
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
 $ConfigPath,$ConfigHeaderPath,$UnitTestPath,$GatePath,$RoadmapPath,$DocPath,
 $InterfacePath,$CapabilityAbiPath)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace('Staged.VmxBasic != Sample->VmxBasic','false')},
 @{I=0;T=$Text[0].Replace(
   'Staged.PreemptionTimerAvailable != TimerAvailable','false')},
 @{I=0;T=$Text[0].Replace('Staged.PreemptionTimerRate !=','false &&')},
 @{I=0;T=$Text[0].Replace(
   'VmxPlatformReadinessSampleValidate(Sample, &Reason)','true')},
 @{I=2;T=$Text[2].Replace(
   'KeRevertToUserGroupAffinityThread(&Previous);','')},
 @{I=2;T=$Text[2].Replace(
   'Sample.VmxMisc = __readmsr(IA32_VMX_MISC);','Sample.VmxMisc = 0;')},
 @{I=4;T=$Text[4].Replace(
   'readiness_status != VMX_PLATFORM_READINESS_TOPOLOGY_ADMITTED','false')},
 @{I=4;T=$Text[4].Replace(
   'VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic(',
   'VmxPersistentRuntimeConfigBuild(')},
 @{I=4;T=$Text[4].Replace('admitted revision=%lu','admitted')},
 @{I=4;T=$Text[4].Replace('preemption-timer=%lu','preemption-timer=unknown')},
 @{I=8;T=$Text[8].Replace("'ReadinessProcessors=4'",
   "'ReadinessProcessors=1'")})
$Number=0
foreach($Mutation in $Mutations){
 $Number++
 A ($Mutation.T-ne$Text[$Mutation.I]) 'readiness mutation was vacuous'
 $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
 try{Verify $Copy}catch{$Rejected=$true}
 A $Rejected "readiness contract accepted mutation $Number"
}
Write-Host 'PASS: topology-wide VMX preentry readiness contracts verified.'
