param(
 [Parameter(Mandatory=$true)][string]$ControlsHeader,
 [Parameter(Mandatory=$true)][string]$ControlsSource,
 [Parameter(Mandatory=$true)][string]$StateSource,
 [Parameter(Mandatory=$true)][string]$ReadinessHeader,
 [Parameter(Mandatory=$true)][string]$ReadinessSource,
 [Parameter(Mandatory=$true)][string]$ReadinessAdapter,
 [Parameter(Mandatory=$true)][string]$VmcsSource,
 [Parameter(Mandatory=$true)][string]$CompositionSource,
 [Parameter(Mandatory=$true)][string]$DispatchHeader,
 [Parameter(Mandatory=$true)][string]$DispatchSource,
 [Parameter(Mandatory=$true)][string]$ControlsTest,
 [Parameter(Mandatory=$true)][string]$ReadinessTest,
 [Parameter(Mandatory=$true)][string]$VmcsTest,
 [Parameter(Mandatory=$true)][string]$DispatchTest,
 [Parameter(Mandatory=$true)][string]$GatePath,
 [Parameter(Mandatory=$true)][string]$PackagerPath,
 [Parameter(Mandatory=$true)][string]$RoadmapPath,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
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
 $ControlsH=$Text[0];$ControlsC=$Text[1];$State=$Text[2]
 $ReadyH=$Text[3];$ReadyC=$Text[4];$ReadyA=$Text[5]
 $Vmcs=$Text[6];$Compose=$Text[7];$DispatchH=$Text[8]
 $DispatchC=$Text[9];$ControlsT=$Text[10];$ReadyT=$Text[11]
 $VmcsT=$Text[12];$DispatchT=$Text[13];$Gate=$Text[14]
 $Packager=$Text[15];$Roadmap=$Text[16];$Doc=$Text[17];$CMake=$Text[18]
 $ParserTokens=$null;$ParserErrors=$null
 [void][Management.Automation.Language.Parser]::ParseInput(
  $Gate,[ref]$ParserTokens,[ref]$ParserErrors)
 A ($ParserErrors.Count-eq0) 'Gate 67 does not parse'

 Has $ControlsH @('PreemptionTimerValue','PreemptionTimerRate',
  'VMX_PREEMPTION_TIMER_RATE_MASK','VMX_PREEMPTION_TIMER_INITIAL_VALUE UINT32_MAX',
  'VmxEnablePreemptionTimerControl(') 'control header'
 $Decorator=$ControlsC.Substring($ControlsC.IndexOf('bool VmxEnablePreemptionTimerControl('))
 Ordered $Decorator @('base->IsValid','base->PreemptionTimerValue != 0',
  '(base->PinControls & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) != 0',
  '(base->ExitControls & VM_EXIT_SAVE_VMX_PREEMPTION_TIMER) != 0',
  '(pin_allowed_1 & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) == 0',
  'staged.PinControls |= PIN_BASED_ACTIVATE_PREEMPTION_TIMER',
  'staged.PreemptionTimerValue = VMX_PREEMPTION_TIMER_INITIAL_VALUE',
  'staged.PreemptionTimerRate =','*out_plan = staged') `
  'persistent timer decorator'
 Has $ControlsT @('test_enables_preemption_timer_watchdog',
  'test_rejects_unavailable_preemption_timer_watchdog',
  'Pin timer enabled','Exit timer save remains disabled','Maximum timer budget',
  'Timer rate decoded','Missing pin support rejected',
  'test_rejects_forced_preemption_timer_save') `
  'control tests'

 $StateBuild=Section $State 'static bool StateBuildControls' 'static bool StateRequiredBasicSlots'
 Ordered $StateBuild @('misc = __readmsr(IA32_VMX_MISC)',
  'VmxPlanVmControls(','VmxEnableMsrBitmapControl(',
  '(pin1 & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) != 0',
  'VmxEnablePreemptionTimerControl(','staged = decorated',
  'VMX_PREEMPTION_TIMER_READY rate=%lu',
  'VMX_PREEMPTION_TIMER_UNAVAILABLE','*out_plan = staged') `
  'state timer composition'

 Has $ReadyH @('VMX_PLATFORM_READINESS_CONSENSUS_VERSION UINT32_C(3)',
  'VMX_PLATFORM_READINESS_REASON_TIMER_CAPABILITY_MISMATCH',
  'VMX_PLATFORM_READINESS_REASON_TIMER_RATE_MISMATCH','uint64_t VmxMisc',
  'uint64_t PinControls','uint32_t PreemptionTimerRate',
  'uint32_t PreemptionTimerAvailable') `
  'readiness header'
 Ordered $ReadyC @('VmxPlatformReadinessSampleValidate(Sample, &Reason)',
  'VmxSplitControlMsr(Sample->PinControls',
  'PIN_BASED_ACTIVATE_PREEMPTION_TIMER','Staged.PreemptionTimerAvailable =',
  'Staged.PreemptionTimerRate =',
  'Staged.PreemptionTimerAvailable != TimerAvailable',
  'VMX_PLATFORM_READINESS_REASON_TIMER_CAPABILITY_MISMATCH',
  'Staged.PreemptionTimerRate !=',
  'VMX_PLATFORM_READINESS_REASON_TIMER_RATE_MISMATCH') 'readiness policy'
 Ordered $ReadyA @('Sample.VmxBasic = __readmsr(IA32_VMX_BASIC)',
  'Sample.VmxMisc = __readmsr(IA32_VMX_MISC)','Sample.PinControls = __readmsr(',
  'KeRevertToUserGroupAffinityThread(&Previous)',
  'VmxPlatformReadinessConsensusObserve(') 'topology timer sampling'
 Has $ReadyT @('TestUniformTimerAbsenceAdmitted',
  'timer-absent admitted status','TestTimerCapabilityMismatchRejected',
  'TestTimerRateMismatchRejected') 'readiness tests'

 Ordered $Vmcs @('PreemptionTimerControlsValid(controls)',
  'VMCS_CTRL_MSR_BITMAP_ADDR',
  '(controls->PinControls & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) != 0',
  'VMCS_GUEST_VMX_PREEMPTION_TIMER','controls->PreemptionTimerValue',
  'timer_count = 1u','VmxPlanGuestSegments',
  'control_count + bitmap_count + timer_count +') 'persistent VMCS timer'
 Has $VmcsT @('VMCS_GUEST_VMX_PREEMPTION_TIMER','timer boundary',
  'timer value','PreemptionTimerValue = 0',
  'test_success_without_optional_preemption_timer','timer field omitted') `
  'VMCS tests'

 $Arm=Section $Compose 'static bool ComposeArmPreemptionTimer' 'static uint32_t ComposeShutdown'
 Ordered $Arm @('__vmx_vmread(VMCS_CTRL_PIN_BASED, &pin_controls)',
  '(pin_controls & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) == 0',
  'VMX_PREEMPTION_TIMER_BYPASSED state=%lu',
  '__vmx_vmwrite(VMCS_GUEST_VMX_PREEMPTION_TIMER',
  'VMX_PREEMPTION_TIMER_INITIAL_VALUE','__vmx_vmread(VMCS_GUEST_VMX_PREEMPTION_TIMER',
  'observed != (size_t)VMX_PREEMPTION_TIMER_INITIAL_VALUE',
  'VMX_PREEMPTION_TIMER_ARMED state=%lu value=0x%08lx') 'timer arm/readback'
 $Shutdown=$Compose.Substring($Compose.IndexOf('static uint32_t ComposeShutdown'))
 Ordered $Shutdown @('ComposeRefreshResumeCr3()','ComposeArmPreemptionTimer(launch->DiagnosticState)',
  'VmxV3dPersistentLaunch(') 'transaction rearm'

 Has $DispatchH @('VMX_V3D_DISPATCH_EXIT_REASON_PREEMPTION_TIMER UINT64_C(52)',
  'VMX_V3D_DISPATCH_FAIL_PREEMPTION_TIMER UINT32_C(0x10A)') 'dispatch header'
 $Route=Section $DispatchC 'static uint32_t DispatchBody(' 'uint32_t VmxV3dPersistentDispatch('
 Ordered $Route @('VMX_V3D_DISPATCH_EXIT_REASON_PREEMPTION_TIMER',
  'VMX_V3D_DISPATCH_FAIL_PREEMPTION_TIMER',
  'VMX_V3D_DISPATCH_VMCS_EXIT_LENGTH') `
  'timer fail-closed dispatch'
 Has $DispatchT @('TestPreemptionTimerFailsClosed',
  'preemption timer expiry fails closed','preemption timer failure code',
  'timer expiry bounded reads','timer expiry performs no writes') 'dispatch tests'

 Has $Gate @('gate67-preemption-watchdog','Probe=VMX_PREEMPTION_WATCHDOG',
  'PreemptionTimer=CAPABILITY_QUALIFIED','PreemptionTimerValue=EXTERNAL_KD',
  'KernelTimerEvidence=EXTERNAL_KD','OperationsVerified=7',
  'ServiceAfterUnload=ABSENT') 'Gate 67'
 A ($Gate-notmatch'(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
  'Gate 67 mutates boot, signing, cleanup, or reboot state'
 Has $Packager @('Gate 67','gate67-preemption-watchdog',
  "'hypervisor_driver.sys'",'Working tree must be clean',
  'source commit must be pushed','CN=hypersinv','SHA256SUMS.txt') 'Gate 67 packager'
 Has $Roadmap @('Runtime V28 - VMX Preemption Watchdog','Gate 67',
  'no hardware deadline','infinite guest: expiry behavior',
  'save-on-exit disabled','No 10/100-cycle') `
  'Runtime V28 roadmap'
 Has $Doc @('Gate 67: VMX preemption watchdog','gate67-preemption-watchdog.ps1',
  'require four UNAVAILABLE markers and eleven BYPASSED markers',
  'fail=0x10a') 'test documentation'
 A (([regex]::Matches($CMake,'test_vmx_preemption_timer_watchdog_contracts')).Count-eq1) `
  'timer watchdog contract registration changed'
}
$Paths=@($ControlsHeader,$ControlsSource,$StateSource,$ReadinessHeader,
 $ReadinessSource,$ReadinessAdapter,$VmcsSource,$CompositionSource,
 $DispatchHeader,$DispatchSource,$ControlsTest,$ReadinessTest,$VmcsTest,
 $DispatchTest,$GatePath,$PackagerPath,$RoadmapPath,$DocPath,$TestsCMake)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=1;T=$Text[1].Replace('staged.PinControls |= PIN_BASED_ACTIVATE_PREEMPTION_TIMER','staged.PinControls &= ~PIN_BASED_ACTIVATE_PREEMPTION_TIMER')},
 @{I=1;T=$Text[1].Replace('(base->ExitControls & VM_EXIT_SAVE_VMX_PREEMPTION_TIMER) != 0','false')},
 @{I=2;T=$Text[2].Replace('misc = __readmsr(IA32_VMX_MISC)','misc = 0')},
 @{I=2;T=$Text[2].Replace('(pin1 & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) != 0','true')},
 @{I=4;T=$Text[4].Replace('Staged.PreemptionTimerAvailable != TimerAvailable','false')},
 @{I=4;T=$Text[4].Replace('Staged.PreemptionTimerRate !=','false &&')},
 @{I=5;T=$Text[5].Replace('Sample.VmxMisc = __readmsr(IA32_VMX_MISC)','Sample.VmxMisc = 0')},
 @{I=6;T=$Text[6].Replace('PreemptionTimerControlsValid(controls)','true')},
 @{I=6;T=$Text[6].Replace('VMCS_GUEST_VMX_PREEMPTION_TIMER','VMCS_GUEST_ES_SELECTOR')},
 @{I=7;T=$Text[7].Replace('__vmx_vmread(VMCS_CTRL_PIN_BASED, &pin_controls) != 0','false')},
 @{I=7;T=$Text[7].Replace('__vmx_vmread(VMCS_GUEST_VMX_PREEMPTION_TIMER, &observed) != 0','false')},
 @{I=7;T=$Text[7].Replace('!ComposeArmPreemptionTimer(launch->DiagnosticState)','false')},
 @{I=9;T=$Text[9].Replace('VMX_V3D_DISPATCH_EXIT_REASON_PREEMPTION_TIMER) {','VMX_V3D_DISPATCH_EXIT_REASON_HLT) {')},
 @{I=14;T=$Text[14].Replace("'PreemptionTimer=CAPABILITY_QUALIFIED'","'PreemptionTimer=UNKNOWN'")},
 @{I=16;T=$Text[16].Replace('infinite guest: expiry behavior','infinite guest without bounded expiry behavior')})
$Number=0
foreach($Mutation in $Mutations){
 $Number++
 A ($Mutation.T-ne$Text[$Mutation.I]) "timer mutation $Number was vacuous"
 $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
 try{Verify $Copy}catch{$Rejected=$true}
 A $Rejected "timer watchdog contract accepted mutation $Number"
}
Write-Host 'PASS: Runtime V28 VMX-preemption capability, topology consensus, VMCS residency, per-transaction rearm/readback, fail-closed dispatch, Gate 67, and mutations verified.'
