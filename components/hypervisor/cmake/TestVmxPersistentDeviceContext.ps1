param(
 [Parameter(Mandatory=$true)][string]$DriverPath,
 [Parameter(Mandatory=$true)][string]$ContextHeaderPath)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not $x){throw $m}}
function O($x,$tokens,$name){$at=0;foreach($v in $tokens){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);A($n-ge 0)"$name $v";$at=$n+$v.Length}}
function V($s,$h){
 foreach($x in @('KMDF device-owned','nonpaged','stable for the device lifetime','Runtime/EPT allocation graph','vmx_persistent_driver_owner.h','vmx_ept_persistent_lifecycle_owner.h','vmx_ept_hook_lifecycle.h','vmx_ept_hook_roundtrip.h')){A($h.Contains($x))"header $x"}
 O $h @('typedef struct _VMX_PERSISTENT_DEVICE_CONTEXT','VMX_PERSISTENT_DRIVER_OWNER Owner;','VMX_EPT_PERSISTENT_LIFECYCLE_OWNER EptLifecycle;','VMX_EPT_HOOK_OWNER EptHooks;','VMX_EPT_HOOK_LIFECYCLE_REPORT EptHookReport;','VMX_EPT_HOOK_ROUNDTRIP_OWNER EptHookRoundtrip;','VMX_EPT_PERSISTENT_LIFECYCLE_REPORT EptDestroyReport;','VMX_EPT_PERSISTENT_DEVICE_START_REPORT EptStartReport;','} VMX_PERSISTENT_DEVICE_CONTEXT;') 'exact seven-member context'
 A(([regex]::Matches($h,[regex]::Escape('WDF_DECLARE_CONTEXT_TYPE_WITH_NAME('))).Count-eq 1)'one context declaration'
 O $h @('WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(','VMX_PERSISTENT_DEVICE_CONTEXT,','VmxPersistentGetDeviceContext);') 'context declaration'
 A($h-notmatch '(?m)^s*(static|extern)s+.*(OWNER|DEVICE_CONTEXT)')'no singleton'
 $an=$s.IndexOf('DriverEntry(');$a=$s.LastIndexOf('NTSTATUS',$an);$bn=$s.IndexOf('DriverEvtUnload(');$b=$s.LastIndexOf('VOID',$bn)
 A($a-ge 0-and$b-gt$a)'entry boundaries';$e=$s.Substring($a,$b-$a)
 O $e @('WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(','status = WdfDeviceCreate(','persistent_context = VmxPersistentGetDeviceContext(g_device);',
  'RtlZeroMemory(&persistent_context->EptLifecycle,','RtlZeroMemory(&persistent_context->EptHooks,','RtlZeroMemory(&persistent_context->EptHookReport,','RtlZeroMemory(&persistent_context->EptHookRoundtrip,',
  'RtlZeroMemory(&persistent_context->EptDestroyReport,','RtlZeroMemory(&persistent_context->EptStartReport,',
  'VmxPersistentDriverOwnerInitialize(','WdfDeviceCreateSymbolicLink(','WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(','WdfIoQueueDispatchSequential','WdfExecutionLevelPassive','WdfControlFinishInitializing(g_device)') 'publication'
 foreach($x in @('EptLifecycle','EptHooks','EptHookReport','EptHookRoundtrip','EptDestroyReport','EptStartReport')){A(([regex]::Matches($e,[regex]::Escape("RtlZeroMemory(&persistent_context->$x,"))).Count-eq 1)"zero $x"}
 A($e-notmatch 'VmxPersistentDriverOwner(Attach|Start|Stop|AcquireStatusSnapshot|Detach)')'no lifecycle before publish'
 $ue=$s.IndexOf('EvtDeviceFileCreate',$bn);A($ue-gt$bn)'unload end';$u=$s.Substring($b,$ue-$b)
 A($u.Contains('VmxPersistentGetDeviceContext(g_device)'))'unload typed context'
}
$s=Get-Content -Raw $DriverPath;$h=Get-Content -Raw $ContextHeaderPath
V $s $h
$sm=@(
 $s.Replace('WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(','WDF_OBJECT_ATTRIBUTES_INIT('),
 $s.Replace('VmxPersistentGetDeviceContext(g_device)','VmxPersistentGetDeviceContext(NULL)'),
 $s.Replace('RtlZeroMemory(&persistent_context->EptHooks,','RtlZeroMemory(&persistent_context->Owner,'),
 $s.Replace('RtlZeroMemory(&persistent_context->EptHookReport,','RtlZeroMemory(&persistent_context->Owner,'),
 $s.Replace('RtlZeroMemory(&persistent_context->EptHookRoundtrip,','RtlZeroMemory(&persistent_context->Owner,'),
 $s.Replace('WdfIoQueueDispatchSequential','WdfIoQueueDispatchParallel'),
 $s.Replace('attributes.ExecutionLevel = WdfExecutionLevelPassive','attributes.ExecutionLevel = WdfExecutionLevelDispatch')
)
foreach($m in $sm){A($m-ne$s)'vacuous source mutation';$bad=$false;try{V $m $h}catch{$bad=$true};A($bad)'accepted source mutation'}
$hm=@(
 $h.Replace('VMX_EPT_HOOK_OWNER EptHooks;','void* EptHooks;'),
 $h.Replace('VMX_EPT_HOOK_LIFECYCLE_REPORT EptHookReport;','void* EptHookReport;'),
 $h.Replace('VMX_EPT_HOOK_ROUNDTRIP_OWNER EptHookRoundtrip;','void* EptHookRoundtrip;'),
 $h.Replace('    VMX_EPT_HOOK_OWNER EptHooks;',''),
 $h.Replace('VmxPersistentGetDeviceContext','VmxPersistentGetWrongContext'),
 $h.Replace('nonpaged','paged')
)
foreach($m in $hm){A($m-ne$h)'vacuous header mutation';$bad=$false;try{V $s $m}catch{$bad=$true};A($bad)'accepted header mutation'}
Write-Output 'Verified KMDF context: stable Runtime/EPT/hook/report storage, sequential PASSIVE publication, explicit zeroing, and no lifecycle before publication.'
