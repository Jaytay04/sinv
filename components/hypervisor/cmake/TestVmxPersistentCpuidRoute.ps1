param(
 [Parameter(Mandatory=$true)][string]$PlanHeader,
 [Parameter(Mandatory=$true)][string]$PlanSource,
 [Parameter(Mandatory=$true)][string]$DispatchHeader,
 [Parameter(Mandatory=$true)][string]$DispatchSource,
 [Parameter(Mandatory=$true)][string]$CompositionSource,
 [Parameter(Mandatory=$true)][string]$LoaderClassifier,
 [Parameter(Mandatory=$true)][string]$DispatchTest,
 [Parameter(Mandatory=$true)][string]$PlanTest,
 [Parameter(Mandatory=$true)][string]$HypervisorCMake,
 [Parameter(Mandatory=$true)][string]$TestsCMake
)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function O([string]$Source,[string[]]$Tokens,[string]$Name){
 $Last=-1
 foreach($Token in $Tokens){
  $At=$Source.IndexOf($Token,$Last+1,[StringComparison]::Ordinal)
  A ($At-gt$Last) "$Name order changed at: $Token";$Last=$At
 }
}
function Verify(
 [string]$PH,[string]$PS,[string]$DH,[string]$DS,[string]$CS,
 [string]$LC,[string]$DT,[string]$PT,[string]$HC,[string]$TC
){
 foreach($Token in @(
  'VMX_CPUID_INSTRUCTION_LENGTH','VMX_CPUID_ROUTE_INVALID_INPUT',
  'VMX_CPUID_ROUTE_INVALID_RIP','VMX_CPUID_ROUTE_INVALID_LENGTH',
  'VMX_CPUID_ROUTE_OK','VMX_CPUID_REGISTERS','VMX_CPUID_ROUTE_PLAN',
  'VmxCpuidRoutePlanBuild'
 )){A ($PH.Contains($Token)) "CPUID plan header missing: $Token"}
 foreach($Token in @(
  'CanonicalRip','memset(OutPlan, 0','Host == NULL',
  'InstructionLength != VMX_CPUID_INSTRUCTION_LENGTH',
  'GuestRip > UINT64_MAX - InstructionLength',
  'cpuid_transparent_apply_stealth','staged.AdvancedRip',
  '*OutPlan = staged'
 )){A ($PS.Contains($Token)) "CPUID plan source missing: $Token"}
 foreach($Token in @(
  'VMX_V3D_DISPATCH_EXIT_REASON_CPUID','VMX_V3D_DISPATCH_FAIL_CPUID_ROUTE',
  'VMX_V3D_PERSISTENT_CPUID_FN','VMX_V3D_PERSISTENT_CPUID_FN Cpuid'
 )){A ($DH.Contains($Token)) "CPUID dispatch header missing: $Token"}
 foreach($Token in @(
  'DispatchCpuid','io->Cpuid == NULL',
  'length != VMX_CPUID_INSTRUCTION_LENGTH',
  'io->Cpuid(io->Context, leaf, subleaf, &host)',
  'VmxCpuidRoutePlanBuild','io->Vmwrite(',
  'guest_gprs->Rax = plan.Guest.Eax',
  'guest_gprs->Rbx = plan.Guest.Ebx',
  'guest_gprs->Rcx = plan.Guest.Ecx',
  'guest_gprs->Rdx = plan.Guest.Edx',
  'VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST'
 )){A ($DS.Contains($Token)) "CPUID dispatcher missing: $Token"}
 O $DS @(
  'io->Cpuid(io->Context, leaf, subleaf, &host)',
  'VmxCpuidRoutePlanBuild',
  'io->Vmwrite(',
  'guest_gprs->Rax = plan.Guest.Eax'
 ) 'CPUID transactional dispatch'
 foreach($Token in @(
  'ComposeCpuid','RtlZeroMemory(out_registers',
  'ComposeContext(opaque, true, &context)','__cpuidex',
  'staged_io.Cpuid != NULL','staged_io.Cpuid = ComposeCpuid'
 )){A ($CS.Contains($Token)) "CPUID composition missing: $Token"}
 foreach($Token in @(
  'LOADER_CPUID_PROBE_HYPERVISOR_BIT_VISIBLE',
  'LOADER_CPUID_PROBE_VENDOR_LEAF_VISIBLE',
  'LOADER_CPUID_PROBE_CET_VISIBLE','LoaderCpuidProbeClassify',
  'LOADER_CPUID_LEAF1_HYPERVISOR','LOADER_CPUID_LEAF7_CET_SHSTK',
  'LOADER_CPUID_LEAF7_CET_IBT'
 )){A ($LC.Contains($Token)) "Loader CPUID classifier missing: $Token"}
 foreach($Token in @(
  'TestCpuidRoute','CPUID route resumes','CPUID callback failure',
  'CPUID VMWRITE failure','CPUID write failure preserves GPRs',
  'CPUID length no callback','unbound CPUID rejected'
 )){A ($DT.Contains($Token)) "CPUID dispatcher test missing: $Token"}
 foreach($Token in @(
  'TestLeafOneMasksHypervisorBit','TestHypervisorRangeIsZero',
  'TestLeafSevenMasksCet','TestPassThroughAndFailures',
  'null host zero output','bad length zero output'
 )){A ($PT.Contains($Token)) "CPUID plan test missing: $Token"}
 A ($HC.Contains('vmx_cpuid_route_plan.c')) 'Hypervisor CMake omits CPUID route plan'
 foreach($Token in @('test_vmx_cpuid_route_plan','test_vmx_persistent_dispatch','test_loader_cpuid_probe')){
  A ($TC.Contains($Token)) "Tests CMake missing: $Token"
 }
}
$Values=@($PlanHeader,$PlanSource,$DispatchHeader,$DispatchSource,$CompositionSource,
 $LoaderClassifier,$DispatchTest,$PlanTest,$HypervisorCMake,$TestsCMake)|
 ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))}
Verify @Values
$Mutations=@(
 @($Values[0],$Values[1].Replace('Host == NULL','false'),$Values[2],$Values[3],$Values[4],$Values[5],$Values[6],$Values[7],$Values[8],$Values[9]),
 @($Values[0],$Values[1].Replace('InstructionLength != VMX_CPUID_INSTRUCTION_LENGTH','false'),$Values[2],$Values[3],$Values[4],$Values[5],$Values[6],$Values[7],$Values[8],$Values[9]),
 @($Values[0],$Values[1],$Values[2],$Values[3].Replace('io->Cpuid == NULL','false'),$Values[4],$Values[5],$Values[6],$Values[7],$Values[8],$Values[9]),
 @($Values[0],$Values[1],$Values[2],$Values[3].Replace('guest_gprs->Rax = plan.Guest.Eax','guest_gprs->Rax = 0'),$Values[4],$Values[5],$Values[6],$Values[7],$Values[8],$Values[9]),
 @($Values[0],$Values[1],$Values[2],$Values[3],$Values[4].Replace('staged_io.Cpuid = ComposeCpuid','staged_io.Cpuid = NULL'),$Values[5],$Values[6],$Values[7],$Values[8],$Values[9]),
 @($Values[0],$Values[1],$Values[2],$Values[3],$Values[4],$Values[5].Replace('LOADER_CPUID_LEAF1_HYPERVISOR','UINT32_C(0)'),$Values[6],$Values[7],$Values[8],$Values[9])
)
foreach($Mutation in $Mutations){
 $Rejected=$false
 try{Verify @Mutation}catch{$Rejected=$true}
 A $Rejected 'Persistent CPUID route accepted a contract mutation'
}
Write-Host 'PASS: persistent CPUID route is planned, kernel-adapted, transactionally dispatched, classified, and mutation-guarded.'
