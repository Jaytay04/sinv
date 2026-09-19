param([Parameter(Mandatory=$true)][string]$SourcePath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n order $x";$p=$q}}
function V([string]$s){
 A ($s.Contains('#include "vmx_persistent_device_context.h"')) 'typed device context'
 $a=$s.IndexOf('IoctlMapPersistentStatusQueryStatus(');$b=$s.IndexOf('/*===========================================================================',[Math]::Max(0,$a+1));A ($a-ge0-and$b-gt$a) 'query section';$q=$s.Substring($a,$b-$a)
 foreach($x in @('VMX_PERSISTENT_STATUS_INVALID','STATUS_INVALID_PARAMETER','VMX_PERSISTENT_STATUS_BUSY','STATUS_DEVICE_BUSY','VMX_PERSISTENT_STATUS_NOT_READY','VMX_PERSISTENT_STATUS_NOT_ACTIVE','STATUS_DEVICE_NOT_READY','VMX_PERSISTENT_STATUS_INVARIANT','STATUS_INTERNAL_ERROR')){A ($q.Contains($x)) "map $x"}
 O $q @('WdfRequestRetrieveInputBuffer(','RtlCopyMemory(&query, input_buffer, sizeof(query));','WdfRequestRetrieveOutputBuffer(','RtlZeroMemory(output_buffer, output_length);','IoctlValidatePersistentStatusQueryRequest(','&query, InputBufferLength);','VmxPersistentGetDeviceContext(','WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request))','VmxPersistentDriverOwnerAcquireStatusSnapshot(','output_length < snapshot.Size','OutputBufferLength < snapshot.Size','information = snapshot.Size;','RtlCopyMemory(output_buffer, snapshot.Buffer, snapshot.Size);','VmxPersistentDriverOwnerReleaseStatusSnapshot(&snapshot);','WdfRequestCompleteWithInformation(Request, status, information);') 'query'
 A (([regex]::Matches($q,'WdfRequestCompleteWithInformation\(').Count)-eq1) 'complete once'
 A ($q-notmatch 'WdfRequestComplete\(') 'no alternate completion'
 A ($q-notmatch '(?i)Serializer|RuntimeEncode|VmxPersistentRuntime|VmxInitAllCores|VmxTeardownAllCores') 'owner snapshot only'
 A ($q.Contains('STATUS_BUFFER_TOO_SMALL;')-and$q.Contains('information = snapshot.Size;')) 'exact size query'
 A ($q.Contains('snapshot_acquired = TRUE;')-and$q.Contains('snapshot_acquired = FALSE;')) 'snapshot ownership'
 $m=$s.Substring($b)
 O $m @('if (IoControlCode == IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY)','IoctlDispatchPersistentStatusQuery(','action = IoctlGetAction(IoControlCode);','case IOCTL_ACTION_LOAD:','case IOCTL_ACTION_UNLOAD:','case IOCTL_ACTION_STATUS:','case IOCTL_ACTION_LAUNCH_PROBE:','case IOCTL_ACTION_UNKNOWN:') 'main'
 foreach($x in @('IOCTL_ACTION_LOAD','IOCTL_ACTION_UNLOAD','IOCTL_ACTION_HOOK','IOCTL_ACTION_UNHOOK','IOCTL_ACTION_STATUS','IOCTL_ACTION_PROBE','IOCTL_ACTION_LAUNCH_PROBE')){A (([regex]::Matches($m,"case ${x}:").Count)-eq1) "legacy case $x"}
}
$s=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath));V $s
$mutations=@(
 $s.Replace('RtlCopyMemory(&query, input_buffer, sizeof(query));',''),
 $s.Replace('RtlZeroMemory(output_buffer, output_length);',''),
 $s.Replace('IoctlValidatePersistentStatusQueryRequest(','IoctlValidateUnhookRequest('),
 $s.Replace('&query, InputBufferLength);','&query, sizeof(query));'),
 $s.Replace('VmxPersistentDriverOwnerAcquireStatusSnapshot(','VmxPersistentDriverOwnerStart('),
 $s.Replace('output_length < snapshot.Size','output_length == snapshot.Size'),
 $s.Replace('information = snapshot.Size;','information = 0;'),
 $s.Replace('VmxPersistentDriverOwnerReleaseStatusSnapshot(&snapshot);',''),
 $s.Replace('WdfRequestCompleteWithInformation(Request, status, information);','WdfRequestComplete(Request,status);'),
 $s.Replace('if (IoControlCode == IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY)','if (IoControlCode == IOCTL_HYPERVISOR_LAUNCH_PROBE)'),
 $s.Replace('case IOCTL_ACTION_LAUNCH_PROBE:','case IOCTL_ACTION_STATUS:')
)
$i=0;foreach($m in $mutations){$i++;A ($m-ne$s) 'vacuous mutation';$bad=$false;try{V $m}catch{$bad=$true};A $bad "accepted mutation $i"}
Write-Output 'Verified private 0x807 bridge: alias-safe request copy, zero/no-partial output, exact size, owner snapshot lifetime, status mapping, one completion, and legacy isolation.'
