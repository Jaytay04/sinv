param(
 [Parameter(Mandatory=$true)][string]$IoctlSource,
 [Parameter(Mandatory=$true)][string]$RoundtripHeader,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not$x){throw $m}}
function O($t,$v,$n){$p=0;foreach($x in $v){$q=$t.IndexOf($x,$p,[StringComparison]::Ordinal);A($q-ge0)"$n $x";$p=$q+$x.Length}}
function V($i,$h,$t){
 A($i.Contains('#include "vmx_ept_hook_roundtrip.h"'))'roundtrip include'
 $a=$i.IndexOf('IoctlDispatchEptHookRoundtrip(');$b=$i.IndexOf('IoctlDispatchEptViolationRoundtrip(');A($a-ge0-and$b-gt$a)'handler bounds';$x=$i.Substring($a,$b-$a)
 O $x @('WdfRequestRetrieveInputBuffer(','RtlCopyMemory(&query, input_buffer, sizeof(query));','WdfRequestRetrieveOutputBuffer(','RtlZeroMemory(output_buffer, output_length);','IoctlValidateEptHookRoundtripRequest(','HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALID','STATUS_INVALID_PARAMETER','HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE_SIZE','STATUS_BUFFER_TOO_SMALL','VmxEptHookRoundtripRun(','IoctlPersistentContext(Request)','IoctlMapEptHookRoundtripError(','RtlCopyMemory(output_buffer, &response, sizeof(response));','information = sizeof(response);','status = STATUS_SUCCESS','WdfRequestCompleteWithInformation(Request, status, information)') 'alias-safe complete response'
 A(([regex]::Matches($x,[regex]::Escape('WdfRequestCompleteWithInformation('))).Count-eq1)'one completion'
 A($x-notmatch 'VmxEptHookOwner|VmxEptHookLifecycle|__vmx|Invept|RtlFillMemory')'no bypass/page access'
 $map=$i.Substring($i.IndexOf('IoctlMapEptHookRoundtripError('),$a-$i.IndexOf('IoctlMapEptHookRoundtripError('))
 foreach($v in @('VMX_EPT_HOOK_ROUNDTRIP_OK','ERR_SUCCESS','VMX_EPT_HOOK_ROUNDTRIP_NOT_READY','ERR_DRIVER_NOT_LOADED','VMX_EPT_HOOK_ROUNDTRIP_ALLOCATION_FAILED','ERR_VMX_REGION_FAILED','VMX_EPT_HOOK_ROUNDTRIP_REMOVE_FAILED','ERR_HOOK_SPLIT_FAILED')){A($map.Contains($v))"map $v"}
 $mn=$i.IndexOf('IoctlMainDispatch(');$mb=$i.LastIndexOf('VOID',$mn);A($mb-ge0)'main bounds';$m=$i.Substring($mb)
 O $m @('IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP','IoctlDispatchEptViolationRoundtrip(','IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP','IoctlDispatchEptHookRoundtrip(','IOCTL_HYPERVISOR_EPT_STATUS_QUERY','IoctlDispatchEptStatusQuery(','IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY') 'raw dispatch order'
 A(([regex]::Matches($m,'IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP')).Count-eq1)'one raw case'
 foreach($v in @('PASSIVE-only','externally serialized','never dereferenced between')){A($h.Contains($v))"owner contract $v"}
 A(([regex]::Matches($t,'test_vmx_ept_hook_roundtrip_dispatch_contracts')).Count-eq1)'test once'
}
$i=Get-Content -Raw $IoctlSource;$h=Get-Content -Raw $RoundtripHeader;$t=Get-Content -Raw $TestsCMake
V $i $h $t
$mut=@(
 $i.Replace('RtlCopyMemory(&query, input_buffer, sizeof(query));',''),
 $i.Replace('IoctlValidateEptHookRoundtripRequest(','HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALID; /*'),
 $i.Replace('VmxEptHookRoundtripRun(','VMX_EPT_HOOK_ROUNDTRIP_OK; /*'),
 $i.Replace('RtlCopyMemory(output_buffer, &response, sizeof(response));',''),
 $i.Replace('information = sizeof(response);','information = 0;'),
 $i.Replace('IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP','IOCTL_HYPERVISOR_EPT_STATUS_QUERY')
)
foreach($q in $mut){A($q-ne$i)'vacuous mutation';$bad=$false;try{V $q $h $t}catch{$bad=$true};A($bad)'accepted mutation'}
Write-Host 'PASS: 0x809 uses alias-safe exact validation, typed no-access roundtrip delegation, complete diagnostics, and one completion.'
