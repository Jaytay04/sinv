param([Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$OwnerSourcePath,
 [Parameter(Mandatory=$true)][string]$OwnerHeaderPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A ($i-ge0-and$j-gt$i) "section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n order $x";$p=$q}}
function V([string]$s,[string]$o,[string]$h){
 foreach($x in @('owner-lock-protected occupancy query','TEARDOWN_PENDING are occupied','Out is preserved on failure','Authenticated phase query','callers must not infer embedded Runtime state directly','VmxPersistentDriverOwnerQueryOccupied','VmxPersistentDriverOwnerQueryPhase')){A ($h.Contains($x)) "owner header $x"}
 $occupied=B $o 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryOccupied' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryPhase'
 $phase=B $o 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryPhase' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryExitTelemetry'
 foreach($q in @($occupied,$phase)){O $q @('Separate(o,sizeof(*o),out,sizeof(*out))','OwnerEnter(o)','if(status!=0)return status','OwnerValid(o)','*out=','OwnerLeave(o)','return status') 'owner query';A (([regex]::Matches($q,'OwnerEnter\(o\)')).Count-eq1-and([regex]::Matches($q,'OwnerLeave\(o\)')).Count-eq1) 'query balanced once';A ($q-notmatch 'Runtime(Start|Stop|Destroy)|PublishPhase|Advance\(|Serializer') 'query ownership only'}
 A ($occupied.Contains('o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_DETACHED')) 'occupied exact phase'
 A ($phase.Contains('(VMX_PERSISTENT_DRIVER_OWNER_PHASE)o->Phase')) 'phase exact copy'
 foreach($x in @('#include "vmx_persistent_runtime_config.h"','#include "vmx_ept_persistent_device_start.h"')){A ($s.Contains($x)) "include $x"}
 A ($s-notmatch 'VmxInitAllCores|VmxTeardownAllCores') 'legacy backend removed'
 $helper=B $s 'IoctlPersistentContext(' '/*==========================================================================='
 O $helper @('WdfIoQueueGetDevice(','WdfRequestGetIoQueue(Request)','VmxPersistentGetDeviceContext(device)','IoctlPersistentOwner(','IoctlPersistentContext(Request)->Owner') 'typed context'
 $load=B $s 'IoctlDispatchLoad(' 'IoctlDispatchUnload('
 O $load @('IoctlPersistentContext(Request)','owner = &device_context->Owner','VmxPersistentDriverOwnerQueryPhase(owner, &phase)','phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED','ERR_DRIVER_ALREADY_LOADED','VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic(','ERR_PERSISTENT_CONFIG_FAILED','VmxEptPersistentDeviceStart(device_context, &config)','EptStartReport.StartDiagnostic','EptStartReport.RollbackStatus','VMX_EPT_PERSISTENT_DEVICE_START_ATTACH_FAILED','ERR_PERSISTENT_ATTACH_FAILED','VMX_EPT_PERSISTENT_DEVICE_START_RUNTIME_FAILED','ERR_PERSISTENT_START_FAILED','VMX_EPT_PERSISTENT_DEVICE_START_EPT_FAILED','ERR_PERSISTENT_LIFECYCLE_FAILED','VMX_EPT_PERSISTENT_DEVICE_START_ROLLBACK_FAILED','ERR_PERSISTENT_ROLLBACK_FAILED','response.status = ERR_SUCCESS','KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS)','RtlSecureZeroMemory(&config','IoctlCompleteBufferedResponse') 'load'
 A (([regex]::Matches($load,'VmxEptPersistentDeviceStart\(')).Count-eq1) 'one device start'
 foreach($x in @('ERR_PERSISTENT_CONFIG_FAILED','ERR_PERSISTENT_ATTACH_FAILED','ERR_PERSISTENT_START_FAILED','ERR_PERSISTENT_LIFECYCLE_FAILED','ERR_PERSISTENT_ROLLBACK_FAILED','IoctlMapPersistentStartDiagnostic','response.active_cores = diagnostic.SystemIndex')){A ($load.Contains($x)) "load evidence $x"}
 A ($load-notmatch 'VmxPersistentDriverOwner(Attach|Start|StartDiagnosed|Stop|Detach)|VmxEptPersistentDeviceLifecycle(Initialize|Destroy)|VmxPersistentRuntime(Start|Stop|Destroy)|->Runtime|VmxGetDriverState|Serializer') 'load coordinator only'
 $unload=B $s 'IoctlDispatchUnload(' 'IoctlDispatchHook('
 O $unload @('IoctlPersistentContext(Request)','owner = &device_context->Owner','VmxPersistentDriverOwnerQueryPhase(owner, &phase)','phase == VMX_PERSISTENT_DRIVER_OWNER_DETACHED','ERR_DRIVER_NOT_LOADED','VmxEptPersistentDeviceTeardown(device_context)','teardown_status != VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK','ERR_DRIVER_UNLOAD_FAILED','response.status = ERR_SUCCESS','IoctlCompleteBufferedResponse') 'unload'
 A (([regex]::Matches($unload,'VmxEptPersistentDeviceTeardown\(')).Count-eq1) 'one device teardown'
 A ($unload-notmatch 'VmxPersistentDriverOwner(Start|Stop|Detach)|VmxEptPersistentDeviceLifecycleDestroy|VmxPersistentRuntime|->Runtime|VmxGetDriverState|Serializer') 'unload coordinator only'
 $status=B $s 'IoctlDispatchStatus(' 'IoctlDispatchProbe('
 O $status @('IoctlPersistentContext(Request)','VmxPersistentDriverOwnerAcquireStatusSnapshot(','snapshot.Buffer == NULL','snapshot.Size < sizeof(header)','VmxPersistentDriverOwnerReleaseStatusSnapshot(&snapshot)','RtlCopyMemory(&header, snapshot.Buffer, sizeof(header))','header.ContextCount == 0','DRIVER_STATE_UNLOADED','DRIVER_STATE_LOADED','response.active_cores = header.ContextCount','VmxEptHookOwnerValidate(&device_context->EptHooks)','response.hook_count = device_context->EptHooks.ActiveCount','VmxPersistentDriverOwnerReleaseStatusSnapshot(','RtlZeroMemory(&response, sizeof(response))','ERR_PERSISTENT_LIFECYCLE_FAILED','IoctlCompleteBufferedResponse') 'legacy status projection'
 A (([regex]::Matches($status,'VmxPersistentDriverOwnerReleaseStatusSnapshot\(')).Count-eq2) 'status snapshot released on malformed and normal paths'
 A ($status-notmatch 'VmxGetDriverState|HYPERVISOR_MAX_CORES|\[64\]') 'legacy status has no duplicate fixed-core state'
 $probe=B $s 'IoctlDispatchProbe(' 'IoctlDispatchLaunchProbe('
 O $probe @('VmxPersistentDriverOwnerQueryOccupied(owner, &occupied)','persistent_status != VMX_PERSISTENT_STATUS_OK','|| occupied','goto write_response','VmxRuntimeProbeProcessorZero(&probe_result)') 'probe gate'
 A ($probe-notmatch 'VmxGetDriverState|g_driver_state') 'probe authenticated owner gate only'
 A (([regex]::Matches($probe,'VmxRuntimeProbeProcessorZero')).Count-eq1) 'probe hardware once'
 $launch=B $s 'IoctlDispatchLaunchProbe(' 'IoctlMapPersistentStatusQueryStatus('
 O $launch @('WdfRequestRetrieveOutputBuffer(','VmxPersistentDriverOwnerQueryOccupied(','persistent_status != VMX_PERSISTENT_STATUS_OK','|| occupied','VMX_DOMAIN_REJECTED','IoctlCompleteBufferedResponse(','IoctlRunLaunchProbeLifecycle(&operations, &response)') 'launch gate'
 A ($launch-notmatch 'VmxGetDriverState|g_driver_state') 'launch authenticated owner gate only'
 A (([regex]::Matches($launch,'IoctlRunLaunchProbeLifecycle')).Count-eq1) 'launch hardware once'
 foreach($x in @('VmxPreflightCreateContext','VmxTransientExecute','VmxPreflightDestroyContext','VMX_LAUNCH_PROBE_RESPONSE_V2')){A ($launch.Contains($x)) "0x806 preserved $x"}
}
$s=[IO.File]::ReadAllText((Resolve-Path $IoctlPath));$o=[IO.File]::ReadAllText((Resolve-Path $OwnerSourcePath));$h=[IO.File]::ReadAllText((Resolve-Path $OwnerHeaderPath));V $s $o $h
$sm=@(
 $s.Replace('VmxPersistentDriverOwnerQueryPhase(owner, &phase)','VmxPersistentDriverOwnerQueryOccupied(owner, (bool*)&phase)'),
 $s.Replace('VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic(','VmxPersistentDriverOwnerStart('),
 $s.Replace('VmxEptPersistentDeviceStart(device_context, &config)','VmxPersistentDriverOwnerStart(owner)'),
 $s.Replace('VMX_EPT_PERSISTENT_DEVICE_START_EPT_FAILED','VMX_EPT_PERSISTENT_DEVICE_START_RUNTIME_FAILED'),
 $s.Replace('ERR_PERSISTENT_LIFECYCLE_FAILED','ERR_PERSISTENT_START_FAILED'),
 $s.Replace('rollback_status = device_context->EptStartReport.RollbackStatus;','rollback_status = 0;'),
 $s.Replace('VmxEptPersistentDeviceTeardown(device_context)','VmxPersistentDriverOwnerDetach(owner)'),
 $s.Replace('teardown_status != VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK','teardown_status == VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK'),
 $s.Replace('VmxPersistentDriverOwnerAcquireStatusSnapshot(','VmxPersistentDriverOwnerQueryPhase('),
 $s.Replace('response.active_cores = header.ContextCount;','response.active_cores = 1;'),
 $s.Replace('response.hook_count = device_context->EptHooks.ActiveCount;','response.hook_count = 0;'),
 $s.Replace('persistent_status = VmxPersistentDriverOwnerReleaseStatusSnapshot(','persistent_status = VMX_PERSISTENT_STATUS_OK; /*'),
 $s.Replace('VmxPersistentDriverOwnerQueryOccupied(owner, &occupied);','persistent_status = VMX_PERSISTENT_STATUS_OK;'),
 $s.Replace('|| occupied','&& occupied'),
 $s.Replace('VmxRuntimeProbeProcessorZero(&probe_result)','VmxGetDriverState()'),
 $s.Replace('IoctlRunLaunchProbeLifecycle(&operations, &response)','true'),
 ($s+[Environment]::NewLine+'void VmxInitAllCores(void){}')
)
$i=0;foreach($m in $sm){$i++;A ($m-ne$s) 'vacuous ioctl mutation';$bad=$false;try{V $m $o $h}catch{$bad=$true};A $bad "accepted ioctl mutation $i"}
$om=@($o.Replace('occupied=o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_DETACHED','occupied=false'),$o.Replace('phase=(VMX_PERSISTENT_DRIVER_OWNER_PHASE)o->Phase','phase=VMX_PERSISTENT_DRIVER_OWNER_DETACHED'),$o.Replace('if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;','if(false)status=VMX_PERSISTENT_STATUS_INVALID;'),$o.Replace('*out=occupied;status=VMX_PERSISTENT_STATUS_OK;','status=VMX_PERSISTENT_STATUS_OK;*out=occupied;OwnerLeave(o);'))
$i=0;foreach($m in $om){$i++;A ($m-ne$o) 'vacuous owner mutation';$bad=$false;try{V $s $m $h}catch{$bad=$true};A $bad "accepted owner mutation $i"}
$hm=@($h.Replace('TEARDOWN_PENDING are occupied','TEARDOWN_PENDING are vacant'),$h.Replace('VmxPersistentDriverOwnerQueryPhase','VmxPersistentDriverOwnerMissing'))
$i=0;foreach($m in $hm){$i++;A ($m-ne$h) 'vacuous header mutation';$bad=$false;try{V $s $o $m}catch{$bad=$true};A $bad "accepted header mutation $i"}
Write-Output 'Verified EPT-aware LOAD/UNLOAD coordinators, authenticated probe occupancy gates, preserved diagnostics, and authenticated dynamic legacy status projection.'
