param(
 [Parameter(Mandatory=$true)][string]$IoctlSource,
 [Parameter(Mandatory=$true)][string]$ValidatorSource,
 [Parameter(Mandatory=$true)][string]$LoaderSource,
 [Parameter(Mandatory=$true)][string]$TestSource)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function S([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,[Math]::Max(0,$i+1));A($i-ge0-and$j-gt$i)"section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=0;foreach($x in $v){$q=$t.IndexOf($x,$p,[StringComparison]::Ordinal);A($q-ge0)"$n $x";$p=$q+$x.Length}}
function V([string]$i,[string]$v,[string]$l,[string]$t){
 A($i.Contains('#include "vmx_ept_persistent_device_hooks.h"'))'typed hook include'
 $h=S $i 'IoctlDispatchHook(' 'IoctlDispatchUnhook('
 O $h @('WdfRequestRetrieveInputBuffer(','RtlCopyMemory(&request, input, sizeof(request));','IoctlValidateHookRequest(&request, InputBufferLength);','context = IoctlPersistentContext(Request);','VmxEptPersistentDeviceHookAdd(','request.gpa, request.permissions','VMX_EPT_PERSISTENT_DEVICE_HOOK_OK','response.active_cores = context->EptLifecycle.Composition.Count','response.reserved[0] = context->EptHookReport.HookId','VMX_EPT_PERSISTENT_DEVICE_HOOK_BUSY','ERR_HOOK_TABLE_FULL','VMX_EPT_PERSISTENT_DEVICE_HOOK_INVALIDATION_FAILED','ERR_HOOK_SPLIT_FAILED','IoctlCompleteBufferedResponse(')'hook dispatch'
 $u=S $i 'IoctlDispatchUnhook(' 'IoctlDispatchStatus('
 O $u @('WdfRequestRetrieveInputBuffer(','RtlCopyMemory(&request, input, sizeof(request));','IoctlValidateUnhookRequest(&request, InputBufferLength);','context = IoctlPersistentContext(Request);','VmxEptPersistentDeviceHookRemove(context, request.gpa)','VMX_EPT_PERSISTENT_DEVICE_HOOK_OK','VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_FOUND','ERR_HOOK_GPA_NOT_FOUND','VMX_EPT_PERSISTENT_DEVICE_HOOK_INVALIDATION_FAILED','ERR_HOOK_SPLIT_FAILED','IoctlCompleteBufferedResponse(')'unhook dispatch'
 A($h-notmatch 'DriverHook|VmxGetDriverState|HookTable|__vmx|Invept')'hook forbidden bypass'
 A($u-notmatch 'DriverHook|VmxGetDriverState|HookTable|__vmx|Invept')'unhook forbidden bypass'
 $vh=S $v 'IoctlValidateHookRequest(' 'IoctlValidateUnhookRequest('
 O $vh @('buffer_length != sizeof(hypervisor_hook_request_t)','request->gpa & 0xFFF','request->permissions == HOOK_PERM_NONE','HOOK_PERM_READ | HOOK_PERM_WRITE | HOOK_PERM_EXECUTE','sizeof(request->reserved)','request->reserved[index] != 0','IOCTL_VALID_OK')'hook validator'
 $vu=$v.Substring($v.IndexOf('IoctlValidateUnhookRequest('))
 O $vu @('buffer_length != sizeof(hypervisor_unhook_request_t)','request->gpa & 0xFFF','sizeof(request->reserved)','request->reserved[index] != 0','IOCTL_VALID_OK')'unhook validator'
 $lh=S $l 'static int CmdHook(' 'static int CmdUnhook('
 O $lh @('argc != 4','argv[2]','argv[3]','IOCTL_HYPERVISOR_HOOK','ERR_HOOK_SPLIT_FAILED','Transactional EPT hook mutation or invalidation failed')'loader hook'
 $lu=S $l 'static int CmdUnhook(' 'static int CmdStatus('
 O $lu @('argc != 3','argv[2]','IOCTL_HYPERVISOR_UNHOOK','ERR_HOOK_GPA_NOT_FOUND','ERR_HOOK_SPLIT_FAILED','Transactional EPT unhook mutation or invalidation failed')'loader unhook'
 foreach($x in @('test_validate_hook_permissions_and_reserved','test_validate_unhook_alignment_and_reserved','unknown permissions','validate hook: overlong buffer','validate hook: reserved','validate unhook: overlong','validate unhook: unaligned','validate unhook: reserved')){A($t.Contains($x))"test $x"}
}
$i=Get-Content -Raw $IoctlSource;$v=Get-Content -Raw $ValidatorSource;$l=Get-Content -Raw $LoaderSource;$t=Get-Content -Raw $TestSource
V $i $v $l $t
$im=@(
 $i.Replace('RtlCopyMemory(&request, input, sizeof(request));',''),
 $i.Replace('VmxEptPersistentDeviceHookAdd(','VmxEptPersistentDeviceHookRemove('),
 $i.Replace('response.reserved[0] = context->EptHookReport.HookId;',''),
 $i.Replace('VmxEptPersistentDeviceHookRemove(context, request.gpa)','VmxEptPersistentDeviceHookAdd(context, request.gpa, 1)'),
 $i.Replace('response.status = ERR_HOOK_GPA_NOT_FOUND;','response.status = ERR_SUCCESS;')
)
foreach($m in $im){A($m-ne$i)'vacuous ioctl mutation';$bad=$false;try{V $m $v $l $t}catch{$bad=$true};A($bad)'accepted ioctl mutation'}
$vm=@(
 $v.Replace('buffer_length != sizeof(hypervisor_hook_request_t)','buffer_length < sizeof(hypervisor_hook_request_t)'),
 $v.Replace('buffer_length != sizeof(hypervisor_unhook_request_t)','buffer_length < sizeof(hypervisor_unhook_request_t)'),
 $v.Replace('request->permissions == HOOK_PERM_NONE','0'),
 $v.Replace('request->reserved[index] != 0','0'),
 $v.Replace('(request->gpa & 0xFFF) != 0','0'))
foreach($m in $vm){A($m-ne$v)'vacuous validator mutation';$bad=$false;try{V $i $m $l $t}catch{$bad=$true};A($bad)'accepted validator mutation'}
$lm=@($l.Replace('argc != 4','argc < 5'),$l.Replace('request.gpa = wcstoull(argv[2], NULL, 16);','request.gpa = wcstoull(argv[3], NULL, 16);'),$l.Replace('argc != 3','argc < 4'))
foreach($m in $lm){A($m-ne$l)'vacuous loader mutation';$bad=$false;try{V $i $v $m $t}catch{$bad=$true};A($bad)'accepted loader mutation'}
Write-Host 'PASS: transactional HOOK/UNHOOK dispatch, strict ABI validation, exact status mapping, and loader argument contracts'
