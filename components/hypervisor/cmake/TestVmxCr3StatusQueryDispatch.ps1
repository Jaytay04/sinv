param([Parameter(Mandatory=$true)][string]$SourcePath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n order $x";$p=$q}}
function V([string]$s){
 A ($s.Contains('#include "vmx_persistent_device_context.h"')) 'typed device context'
 $a=$s.IndexOf('IoctlDispatchCr3StatusQuery(');$b=$s.IndexOf('/*===========================================================================',[Math]::Max(0,$a+1));A ($a-ge0-and$b-gt$a) 'CR3 query section';$q=$s.Substring($a,$b-$a)
 foreach($x in @('CR3_DIAG ioctl_query enter','CR3_DIAG ioctl_query snapshot begin','CR3_DIAG ioctl_query snapshot end','CR3_DIAG ioctl_query release begin','CR3_DIAG ioctl_query release end','CR3_DIAG ioctl_query complete')){A ($q.Contains($x)) "query diagnostic $x"}
 A ($q-notmatch 'CR3_DIAG[^\r\n]*(root|cr3=|address)') 'query diagnostics remain redacted'
 O $q @('WdfRequestRetrieveInputBuffer(','RtlCopyMemory(&query, input_buffer, sizeof(query));','WdfRequestRetrieveOutputBuffer(','RtlZeroMemory(output_buffer, output_length);','IoctlValidateCr3StatusQueryRequest(','&query, InputBufferLength);','VmxPersistentGetDeviceContext(','WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request))','VmxPersistentDriverOwnerAcquireCr3StatusSnapshot(','output_length < snapshot.Size','OutputBufferLength < snapshot.Size','information = snapshot.Size;','RtlCopyMemory(output_buffer, snapshot.Buffer, snapshot.Size);','VmxPersistentDriverOwnerReleaseCr3StatusSnapshot(','WdfRequestCompleteWithInformation(Request, status, information);') 'CR3 query'
 A ($q.Contains('IoctlMapPersistentStatusQueryStatus(persistent_status)')) 'status mapping'
 A ($q.Contains('snapshot_acquired = TRUE;')-and$q.Contains('snapshot_acquired = FALSE;')) 'snapshot ownership'
 A (([regex]::Matches($q,'VmxPersistentDriverOwnerReleaseCr3StatusSnapshot\(').Count)-eq2) 'release success and cleanup paths'
 A (([regex]::Matches($q,'WdfRequestCompleteWithInformation\(').Count)-eq1) 'complete once'
 A ($q-notmatch 'WdfRequestComplete\(') 'no alternate completion'
 A ($q-notmatch '(?i)Serializer|RuntimeEncode|RuntimeStart|RuntimeStop|OwnerStart|OwnerStop|BindCr3|VmxOn|VmxOff') 'read-only owner snapshot bridge'
 $m=$s.Substring($b)
 O $m @('if (IoControlCode == IOCTL_HYPERVISOR_CR3_STATUS_QUERY)','IoctlDispatchCr3StatusQuery(','if (IoControlCode == IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP)','if (IoControlCode == IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP)','if (IoControlCode == IOCTL_HYPERVISOR_EPT_STATUS_QUERY)','if (IoControlCode == IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY)','action = IoctlGetAction(IoControlCode);') 'raw dispatch'
 foreach($x in @('IOCTL_ACTION_LOAD','IOCTL_ACTION_UNLOAD','IOCTL_ACTION_HOOK','IOCTL_ACTION_UNHOOK','IOCTL_ACTION_STATUS','IOCTL_ACTION_PROBE','IOCTL_ACTION_LAUNCH_PROBE')){A (([regex]::Matches($m,"case ${x}:").Count)-eq1) "legacy case $x"}
}
$s=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath));V $s
$mutations=@(
 $s.Replace('RtlCopyMemory(&query, input_buffer, sizeof(query));',''),
 $s.Replace('IoctlValidateCr3StatusQueryRequest(','IoctlValidateEptStatusQueryRequest('),
 $s.Replace('&query, InputBufferLength);','&query, sizeof(query));'),
 $s.Replace('VmxPersistentDriverOwnerAcquireCr3StatusSnapshot(','VmxPersistentDriverOwnerStart('),
 $s.Replace('output_length < snapshot.Size','output_length == snapshot.Size'),
 $s.Replace('RtlCopyMemory(output_buffer, snapshot.Buffer, snapshot.Size);',''),
 $s.Replace('information = snapshot.Size;','information = 0;'),
 $s.Replace('VmxPersistentDriverOwnerReleaseCr3StatusSnapshot(','VmxPersistentDriverOwnerReleaseStatusSnapshot('),
 $s.Replace('if (IoControlCode == IOCTL_HYPERVISOR_CR3_STATUS_QUERY)','if (IoControlCode == IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP)'),
 $s.Replace('RtlZeroMemory(output_buffer, output_length);','')
)
$i=0;foreach($x in $mutations){$i++;A ($x-ne$s) 'vacuous mutation';$bad=$false;try{V $x}catch{$bad=$true};A $bad "accepted mutation $i"}
Write-Output 'Verified private 0x80B bridge: alias-safe validation, redacted owner snapshot, exact no-partial output, complete release, and legacy isolation.'