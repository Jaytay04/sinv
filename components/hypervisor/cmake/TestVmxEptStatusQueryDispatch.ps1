param([Parameter(Mandatory=$true)][string]$SourcePath,
      [Parameter(Mandatory=$true)][string]$DriverPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n order $x";$p=$q}}
function V([string]$s,[string]$d){
 A ($s.Contains('#include "vmx_ept_persistent_device_status.h"')-and$s.Contains('#include "vmx_ept_status_abi.h"')) 'EPT status dependencies'
 $a=$s.IndexOf('IoctlMapEptStatusQueryStatus(');$b=$s.IndexOf('/*===========================================================================',[Math]::Max(0,$a+1));A ($a-ge0-and$b-gt$a) 'EPT query section';$q=$s.Substring($a,$b-$a)
 foreach($x in @('VMX_EPT_PERSISTENT_DEVICE_STATUS_INVALID','STATUS_INVALID_PARAMETER','VMX_EPT_PERSISTENT_DEVICE_STATUS_WRONG_IRQL','STATUS_INVALID_DEVICE_STATE','VMX_EPT_PERSISTENT_DEVICE_STATUS_MODEL_FAILED','STATUS_INTERNAL_ERROR')){A ($q.Contains($x)) "map $x"}
 O $q @('WdfRequestRetrieveInputBuffer(','RtlCopyMemory(&query, input_buffer, sizeof(query));','WdfRequestRetrieveOutputBuffer(','RtlZeroMemory(output_buffer, output_length);','IoctlValidateEptStatusQueryRequest(&query, InputBufferLength);','output_length < HYPERVISOR_EPT_STATUS_RESPONSE_SIZE','information = HYPERVISOR_EPT_STATUS_RESPONSE_SIZE;','VmxPersistentGetDeviceContext(','WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request))','VmxEptPersistentDeviceStatusProject(','RtlZeroMemory(output_buffer, output_length);','WdfRequestCompleteWithInformation(Request, status, information);') 'EPT query'
 A (([regex]::Matches($q,'WdfRequestCompleteWithInformation\(').Count)-eq1) 'complete once'
 A ($q-notmatch 'WdfRequestComplete\(') 'no alternate completion'
 A ($q-notmatch '(?i)Allocate|Free|Serializer|RuntimeStart|RuntimeStop|VmxOn|VmxOff|Invept') 'read-only bridge'
 A ($q.Contains('STATUS_BUFFER_TOO_SMALL;')-and$q.Contains('HYPERVISOR_EPT_STATUS_RESPONSE_SIZE;')) 'fixed exact size'
 $m=$s.Substring($b)
 O $m @('if (IoControlCode == IOCTL_HYPERVISOR_EPT_STATUS_QUERY)','IoctlDispatchEptStatusQuery(','if (IoControlCode == IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY)','IoctlDispatchPersistentStatusQuery(','action = IoctlGetAction(IoControlCode);','case IOCTL_ACTION_LOAD:','case IOCTL_ACTION_UNLOAD:','case IOCTL_ACTION_STATUS:','case IOCTL_ACTION_LAUNCH_PROBE:','case IOCTL_ACTION_UNKNOWN:') 'main'
 foreach($x in @('IOCTL_ACTION_LOAD','IOCTL_ACTION_UNLOAD','IOCTL_ACTION_HOOK','IOCTL_ACTION_UNHOOK','IOCTL_ACTION_STATUS','IOCTL_ACTION_PROBE','IOCTL_ACTION_LAUNCH_PROBE')){A (([regex]::Matches($m,"case ${x}:").Count)-eq1) "legacy case $x"}
 O $d @('WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(','WdfIoQueueDispatchSequential','attributes.ExecutionLevel = WdfExecutionLevelPassive;','WdfIoQueueCreate(') 'queue serialization'
}
$s=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath));$d=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $DriverPath));V $s $d
$mutations=@(
 $s.Replace('RtlCopyMemory(&query, input_buffer, sizeof(query));',''),
 $s.Replace('IoctlValidateEptStatusQueryRequest(&query, InputBufferLength);','IoctlValidateEptStatusQueryRequest(&query, sizeof(query));'),
 $s.Replace('output_length < HYPERVISOR_EPT_STATUS_RESPONSE_SIZE','output_length == HYPERVISOR_EPT_STATUS_RESPONSE_SIZE'),
 $s.Replace('VmxEptPersistentDeviceStatusProject(','VmxEptPersistentDeviceStart('),
 $s.Replace('information = HYPERVISOR_EPT_STATUS_RESPONSE_SIZE;','information = 0;'),
 $s.Replace('if (IoControlCode == IOCTL_HYPERVISOR_EPT_STATUS_QUERY)','if (IoControlCode == IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY)'),
 $s.Replace('RtlZeroMemory(output_buffer, output_length);','')
)
$i=0;foreach($x in $mutations){$i++;A ($x-ne$s) 'vacuous source mutation';$bad=$false;try{V $x $d}catch{$bad=$true};A $bad "accepted source mutation $i"}
$dm=@($d.Replace('WdfIoQueueDispatchSequential','WdfIoQueueDispatchParallel'),$d.Replace('WdfExecutionLevelPassive','WdfExecutionLevelDispatch'))
$i=0;foreach($x in $dm){$i++;A ($x-ne$d) 'vacuous driver mutation';$bad=$false;try{V $s $x}catch{$bad=$true};A $bad "accepted driver mutation $i"}
Write-Output 'Verified private 0x808 bridge: alias-safe validation, zero/no-partial fixed output, authenticated projection, sequential PASSIVE queue, one completion, and legacy isolation.'
