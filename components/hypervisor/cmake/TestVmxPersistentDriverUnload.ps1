param(
 [Parameter(Mandatory=$true)][string]$DriverPath,
 [Parameter(Mandatory=$true)][string]$PolicySourcePath,
 [Parameter(Mandatory=$true)][string]$PolicyHeaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n $x";$p=$q}}
function S([string]$t,[string]$a,[string]$b){$x=$t.IndexOf($a);$y=$t.IndexOf($b,$x+1);A ($x-ge0-and$y-gt$x) "section $a";$t.Substring($x,$y-$x)}
function V([string]$d,[string]$s,[string]$h,[string]$t){
 foreach($x in @('VMX_PERSISTENT_UNLOAD_MAX_ATTEMPTS UINT32_C(4)','QueryPhase','Stop','Detach','Delay','FailStop')){A ($h.Contains($x)) "header $x"}
 O $s @('PlatformValid','max_attempts == 0','max_attempts > VMX_PERSISTENT_UNLOAD_MAX_ATTEMPTS','for (attempt = 1','QueryPhase','VMX_PERSISTENT_UNLOAD_PHASE_DETACHED','p->Stop','p->Detach','VMX_PERSISTENT_UNLOAD_PHASE_TEARDOWN_PENDING','p->Detach','QueryPhase','p->Delay','p->FailStop','return status != 0') 'policy'
 foreach($x in @('#include "vmx_ept_persistent_device_status.h"','#include "vmx_ept_persistent_device_teardown.h"')){A ($d.Contains($x)) "driver include $x"}
 $q=S $d 'static int32_t PersistentUnloadQuery(' 'static int32_t PersistentUnloadStop('
 O $q @('VMX_EPT_PERSISTENT_STATUS_RECORD status_record = {0}','VmxEptPersistentDeviceStatusProject(','VMX_EPT_PERSISTENT_STATUS_DETACHED','VMX_PERSISTENT_UNLOAD_PHASE_DETACHED','VMX_EPT_PERSISTENT_STATUS_READY','VMX_PERSISTENT_UNLOAD_PHASE_ATTACHED','VMX_EPT_PERSISTENT_STATUS_TEARDOWN_PENDING','VMX_PERSISTENT_UNLOAD_PHASE_TEARDOWN_PENDING','VMX_EPT_PERSISTENT_STATUS_BLOCKED','VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_BLOCKED','VMX_EPT_PERSISTENT_STATUS_MALFORMED','VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT') 'combined query'
 $stop=S $d 'static int32_t PersistentUnloadStop(' 'static int32_t PersistentUnloadDetach('
 O $stop @('UNREFERENCED_PARAMETER(context)','return 0;') 'merged stop'
 A ($stop-notmatch 'DriverOwner|DeviceTeardown|Runtime|EptLifecycle') 'stop has no split teardown'
 $detach=S $d 'static int32_t PersistentUnloadDetach(' 'static bool PersistentUnloadDelay('
 O $detach @('VmxEptPersistentDeviceTeardown(','(VMX_PERSISTENT_DEVICE_CONTEXT*)context') 'combined teardown'
 A ($detach-notmatch 'DriverOwnerStop|DriverOwnerDetach|DeviceLifecycleDestroy') 'no split teardown'
 $name=$d.IndexOf('DriverEvtUnload(');A ($name-ge0) 'driver unload name';$start=$d.LastIndexOf('VOID',$name);$end=$d.IndexOf('EvtDeviceFileCreate',$name);A ($start-ge0-and$end-gt$name) 'driver unload section';$u=$d.Substring($start,$end-$start)
 O $u @('KeGetCurrentIrql() != PASSIVE_LEVEL','g_device == NULL','VmxPersistentGetDeviceContext(g_device)','platform.Context = device_context','platform.QueryPhase = PersistentUnloadQuery','platform.Stop = PersistentUnloadStop','platform.Detach = PersistentUnloadDetach','platform.Delay = PersistentUnloadDelay','platform.FailStop = PersistentUnloadFailStop','VmxPersistentUnloadPolicyRun','if (status != 0)','PersistentUnloadFailStop','g_device = NULL') 'driver unload'
 A ($u-notmatch 'g_driver_state|DRIVER_STATE_|VmxGetDriverState|VmxTeardownAllCores|VmxInitAllCores|core_mgmt') 'unload has no duplicate legacy state'
 foreach($x in @('VmxEptPersistentDeviceStatusProject','VmxEptPersistentDeviceTeardown','KeDelayExecutionThread','-100000LL','KeBugCheckEx(MANUALLY_INITIATED_CRASH')){A ($d.Contains($x)) "driver adapter $x"}
 foreach($x in @('detached has no side effects','pre/post proof','detached proof overrides prior stop failure','stop not replayed pending','bounded detach attempts','exact fail-stop status','delay failure stops retry','query retry','unknown phase fail-stop','missing fail-stop','zero attempts','attempt overflow')){A ($t.Contains($x)) "test $x"}
}
$d=[IO.File]::ReadAllText((Resolve-Path $DriverPath));$s=[IO.File]::ReadAllText((Resolve-Path $PolicySourcePath));$h=[IO.File]::ReadAllText((Resolve-Path $PolicyHeaderPath));$t=[IO.File]::ReadAllText((Resolve-Path $TestPath));V $d $s $h $t
$sm=@($s.Replace('int32_t stop = p->Stop(p->Context);','int32_t stop = 0;'),$s.Replace('int32_t detach = p->Detach(p->Context);','int32_t detach = 0;'),$s.Replace('status = p->Detach(p->Context);','status = 0;'),$s.Replace('!p->Delay(p->Context, attempt)','false'),$s.Replace('p->FailStop(p->Context, status, phase, completed);',''))
foreach($m in $sm){A ($m-ne$s) 'vacuous policy mutation';$bad=$false;try{V $d $m $h $t}catch{$bad=$true};A $bad 'accepted policy mutation'}
$dm=@(
 $d.Replace('VmxEptPersistentDeviceStatusProject(','VMX_EPT_PERSISTENT_DEVICE_STATUS_OK /*'),
 $d.Replace('VMX_EPT_PERSISTENT_STATUS_TEARDOWN_PENDING','VMX_EPT_PERSISTENT_STATUS_DETACHED'),
 $d.Replace('static int32_t PersistentUnloadStop','static int32_t PersistentUnloadMissing'),
 $d.Replace('VmxEptPersistentDeviceTeardown(','VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK /*'),
 $d.Replace('platform.Context = device_context;','platform.Context = &device_context->Owner;'),
 $d.Replace('KeBugCheckEx(MANUALLY_INITIATED_CRASH,','return; /*'),
 $d.Replace('status = VmxPersistentUnloadPolicyRun(','status = 0; /*'),
 $d.Replace('g_device = NULL;','')
)
foreach($m in $dm){A ($m-ne$d) 'vacuous driver mutation';$bad=$false;try{V $m $s $h $t}catch{$bad=$true};A $bad 'accepted driver mutation'}
'Verified bounded DriverEvtUnload combined Runtime+EPT teardown, authenticated detached proof, and deliberate fail-stop.'
