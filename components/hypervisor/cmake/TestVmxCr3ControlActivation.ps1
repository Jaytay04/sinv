param(
 [Parameter(Mandatory=$true)][string]$ContextHeader,
 [Parameter(Mandatory=$true)][string]$BasicSource,
 [Parameter(Mandatory=$true)][string]$ApplySource,
 [Parameter(Mandatory=$true)][string]$CompositionSource,
 [Parameter(Mandatory=$true)][string]$StateSource,
 [Parameter(Mandatory=$true)][string]$OpsSource,
 [Parameter(Mandatory=$true)][string]$LifecycleSource,
 [Parameter(Mandatory=$true)][string]$PlanSource,
 [Parameter(Mandatory=$true)][string]$ContextTest,
 [Parameter(Mandatory=$true)][string]$PlanTest,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A($i-ge0-and$j-gt$i)"section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A($q-gt$p)"$n order $x";$p=$q}}
function V($h,$b,$a,$c,$s,$o,$l,$p,$ht,$pt,$tc){
 foreach($x in @('VMX_PERSISTENT_BASIC_OPS_VERSION UINT32_C(2)','uint32_t Cr3RouteBound','uint32_t Cr3RouteBoundContract','sizeof(VMX_PERSISTENT_BASIC_OPS_CONTEXT) == 32','Cr3RouteBound) == 24','Cr3RouteBoundContract) == 28','VmxPersistentBasicContextCr3RouteValid')){A($h.Contains($x))"context $x"}
 O $h @('Cr3RouteBound == 0','Cr3RouteBoundContract == 0','Cr3RouteBound == 1','Cr3RouteBoundContract == ~UINT32_C(1)') 'flag auth'
 foreach($x in @($b,$a,$c,$s)){A($x.Contains('VmxPersistentBasicContextCr3RouteValid'))'consumer validates flag'}
 $state=B $s 'static bool StateBuildControls' 'static bool StateRequiredBasicSlots'
 O $state @('RtlCopyMemory(&context, opaque','RtlZeroMemory(&staged','RtlZeroMemory(&decorated','VmxPlanVmControls(','staged = decorated','context.Cr3RouteBound != 0','RtlZeroMemory(&decorated, sizeof(decorated));','VmxCr3ControlsEnable(&staged, primary1, &decorated)','staged = decorated','VmxEnablePreemptionTimerControl(','staged = decorated','*out_plan = staged') 'state controls'
 $bind=B $o 'static VMX_PERSISTENT_STATUS BindCr3Route' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBindCr3Route('
 O $bind @('VmxCr3RouteOwnerInitializeGuarded','VmxCr3RouteOwnerInitialize','Cr3Owner=&s->Cr3RouteOwner','ProcessorIndex=i','Cr3RouteBoundContract=~UINT32_C(1)','KeMemoryBarrier()','InterlockedExchange((volatile LONG*)&s->BasicContexts[i].Cr3RouteBound,1)','if(Bound(s))return VMX_PERSISTENT_STATUS_OK','Cr3RouteBound=0','Cr3RouteBoundContract=0','Cr3Owner=NULL','VmxCr3RouteOwnerReset') 'atomic bind'
 foreach($x in @('VmxPersistentBasicContextCr3RouteValid(&s->BasicContexts[i])','Cr3RouteBound!=0','Cr3RouteBound!=1')){A($o.Contains($x)-and$l.Contains($x))"storage auth $x"}
 $plan=B $p 'static int InputValid' 'static int PlanValid'
 A(-not$plan.Contains('CPU_BASED_CR3_STORE_EXITING'))'forced store accepted'
 $build=$p.Substring($p.IndexOf('VMX_PERSISTENT_VMCS_PLAN_STATUS VmxPersistentBuildVmcsPlan'))
 O $build @('VmxPlanControlFields(&plan, controls)','controls->PrimaryControls & CPU_BASED_CR3_LOAD_EXITING','VmxCr3ControlsAppendTargetCount(controls, &plan)','cr3_count = 1u','VmxPlanGuestSegments','control_count + bitmap_count + timer_count +','cr3_count + 74u','*out_plan = plan') 'target count plan'
 foreach($x in @('sizeof(context) == 32','context.Version == UINT32_C(2)','unbound_valid','bound_valid','invalid_contract_rejected','VmxPersistentBasicContextCr3RouteValid')){A($ht.Contains($x))"context test $x"}
 foreach($x in @('CR3 route on','plan.Count, 83','VMCS_CTRL_CR3_TARGET_COUNT','CR3 target count zero','forced CR3-store route on')){A($pt.Contains($x))"plan test $x"}
 foreach($x in @($b,$a,$c,$s,$o,$l,$p)){A($x-notmatch '(?i)DriverEntry|IRP_MJ|IOCTL|VMX_V2|process lookup|PsLookupProcess')'forbidden public/target wiring'}
 A(([regex]::Matches($tc,'test_vmx_cr3_control_activation_contracts')).Count-eq1)'CMake once'
}
$h=Get-Content -Raw $ContextHeader;$b=Get-Content -Raw $BasicSource;$a=Get-Content -Raw $ApplySource
$c=Get-Content -Raw $CompositionSource;$s=Get-Content -Raw $StateSource;$o=Get-Content -Raw $OpsSource
$l=Get-Content -Raw $LifecycleSource;$p=Get-Content -Raw $PlanSource;$ht=Get-Content -Raw $ContextTest
$pt=Get-Content -Raw $PlanTest;$tc=Get-Content -Raw $TestsCMake;V $h $b $a $c $s $o $l $p $ht $pt $tc
$m=@(
 @('h',$h.Replace('Cr3RouteBoundContract == ~UINT32_C(1)','Cr3RouteBoundContract == 0')),
 @('b',$b.Replace('VmxPersistentBasicContextCr3RouteValid(copy)','true')),
 @('a',$a.Replace('!VmxPersistentBasicContextCr3RouteValid(copy)','false')),
 @('c',$c.Replace('!VmxPersistentBasicContextCr3RouteValid(copy)','false')),
 @('s',$s.Replace('VmxCr3ControlsEnable(&staged, primary1, &decorated)','VMX_CR3_CONTROL_OK')),
 @('s',$s.Replace('if (VmxCr3ControlsEnable(&staged, primary1, &decorated) !=','if (VMX_CR3_CONTROL_OK !=')),
 @('o',$o.Replace('Cr3RouteBoundContract=~UINT32_C(1)','Cr3RouteBoundContract=0')),
 @('o',$o.Replace('InterlockedExchange((volatile LONG*)&s->BasicContexts[i].Cr3RouteBound,1)','InterlockedExchange((volatile LONG*)&s->BasicContexts[i].Cr3RouteBound,0')),
 @('l',$l.Replace('s->BasicContexts[i].Cr3RouteBound!=1','false')),
 @('p',$p.Replace('VmxCr3ControlsAppendTargetCount(controls, &plan)','VMX_CR3_CONTROL_OK')),
 @('p',$p.Replace('control_count + bitmap_count + timer_count +','control_count + bitmap_count +'))
)
foreach($x in $m){$mh=$h;$mb=$b;$ma=$a;$mc=$c;$ms=$s;$mo=$o;$ml=$l;$mp=$p;switch($x[0]){'h'{$mh=$x[1]}'b'{$mb=$x[1]}'a'{$ma=$x[1]}'c'{$mc=$x[1]}'s'{$ms=$x[1]}'o'{$mo=$x[1]}'l'{$ml=$x[1]}'p'{$mp=$x[1]}};A($x[1]-ne$(switch($x[0]){'h'{$h}'b'{$b}'a'{$a}'c'{$c}'s'{$s}'o'{$o}'l'{$l}'p'{$p}}))'vacuous mutation';$bad=$false;try{V $mh $mb $ma $mc $ms $mo $ml $mp $ht $pt $tc}catch{$bad=$true};A($bad)"accepted $($x[0]) mutation"}
Write-Host 'PASS: CR3-load controls and target-count zero activate only with the authenticated runtime route flag; hardware-forced store exits are preserved for transparent dispatch.'

