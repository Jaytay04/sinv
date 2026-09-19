param(
 [Parameter(Mandatory=$true)][string]$MainPath,
 [Parameter(Mandatory=$true)][string]$ServicePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n $x";$p=$q}}
function S([string]$t,[string]$a,[string]$b){$x=$t.IndexOf($a);$y=$t.IndexOf($b,$x+1);A ($x-ge0-and$y-gt$x) "section $a";$t.Substring($x,$y-$x)}
function V([string]$m,[string]$s,[string]$h,[string]$t){
 foreach($x in @('LoaderEptStatusProvesReady','LoaderEptStatusProvesDetached')){A ($h.Contains($x)) "header $x"}
 foreach($x in @('query_valid && classification == LOADER_EPT_WIRE_READY','query_valid && classification == LOADER_EPT_WIRE_DETACHED')){A ($s.Contains($x)) "policy $x"}
 foreach($x in @('TestEptStatusProofPolicy','only EPT READY proves load','only EPT DETACHED proves cleanup','failed EPT query cannot prove load','failed EPT query cannot prove cleanup','TEST_RUN(TestEptStatusProofPolicy);')){A ($t.Contains($x)) "test $x"}
 $q=S $m 'typedef struct _LOADER_EPT_STATUS_QUERY_CONTEXT' 'static BOOL CleanupDriverServiceAfterDetachedProof'
 foreach($x in @('HYPERVISOR_EPT_STATUS_QUERY_REQUEST Request;','HANDLE DeviceHandle;','HYPERVISOR_EPT_STATUS_RESPONSE Status;','DWORD FetchBytes;','BOOL FetchSucceeded;')){A ($q.Contains($x)) "query model $x"}
 $thread=S $q 'static DWORD WINAPI EptStatusQueryThread' 'static BOOL QueryEptStatus'
 O $thread @('context->DeviceHandle','IOCTL_HYPERVISOR_EPT_STATUS_QUERY','&context->Request, sizeof(context->Request)','&context->Status, sizeof(context->Status)','&context->FetchBytes') 'EPT thread'
 A (([regex]::Matches($thread,'DeviceIoControl\(')).Count-eq1) 'one EPT DeviceIoControl'
 $query=$q.Substring($q.IndexOf('static BOOL QueryEptStatus'))
 O $query @('HYPERVISOR_EPT_STATUS_QUERY_VERSION','HYPERVISOR_EPT_STATUS_QUERY_REQUEST_SIZE','HYPERVISOR_EPT_STATUS_QUERY_FLAGS_NONE','DuplicateHandle(GetCurrentProcess(), g_device_handle','CreateThread(','WaitForSingleObject(thread, timeout_ms)','CancelSynchronousIo(thread)','WaitForSingleObject(thread, 5000u)','!context->FetchSucceeded','context->FetchBytes != HYPERVISOR_EPT_STATUS_RESPONSE_SIZE','LoaderClassifyEptStatus(','FreeEptStatusQueryContext(context)','return TRUE;') 'EPT query'
 A ($q.Contains('CloseHandle(context->DeviceHandle)')) 'duplicate handle closed';A ($query.Contains('A non-cancellable request retains its context and duplicate handle')) 'non-cancellable ownership'
 $load=S $m 'static int CmdLoadInternal(' 'static int CmdLoad(void)'
 O $load @('IOCTL_HYPERVISOR_LOAD','QueryPersistentStatus(&classification, &proof)','ept_query_valid = QueryEptStatus(&ept_classification)','LoaderDecideLoadVerification(','LoaderEptStatusProvesReady(','INFO: Exact EPT READY status is verified.','LoaderEptStatusProvesDetached(','IOCTL_HYPERVISOR_UNLOAD','detached_proof = QueryPersistentDetachedStatus()','detached_ept_query_valid = QueryEptStatus(','LoaderEptStatusProvesDetached(','CleanupDriverServiceAfterDetachedProof()') 'dual load proof'
 $unload=S $m 'static int CmdUnload(void)' 'static int CmdHook'
 O $unload @('IOCTL_HYPERVISOR_UNLOAD','LOADER_UNLOAD_IOCTL_SUCCESS','LOADER_UNLOAD_IOCTL_NOT_LOADED','status_proof = QueryPersistentDetachedStatus()','ept_query_valid = QueryEptStatus(&ept_classification)','LoaderDecideScmCleanup','LOADER_SCM_CLEANUP_FORBIDDEN','LoaderEptStatusProvesDetached(','CleanupDriverServiceAfterDetachedProof()') 'dual unload proof'
 A ($m-notmatch 'IOCTL_HYPERVISOR_LAUNCH_PROBE.{0,120}EPT_STATUS_QUERY') '0x806 path unchanged'
}
$m=[IO.File]::ReadAllText((Resolve-Path $MainPath));$s=[IO.File]::ReadAllText((Resolve-Path $ServicePath));$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath));$t=[IO.File]::ReadAllText((Resolve-Path $TestPath));V $m $s $h $t
$mm=@(
 $m.Replace('context->DeviceHandle, IOCTL_HYPERVISOR_EPT_STATUS_QUERY','g_device_handle, IOCTL_HYPERVISOR_EPT_STATUS_QUERY'),
 $m.Replace('context->FetchBytes != HYPERVISOR_EPT_STATUS_RESPONSE_SIZE','FALSE'),
 $m.Replace('*classification = LoaderClassifyEptStatus(','*classification = LOADER_EPT_WIRE_READY; /*'),
 $m.Replace('ept_query_valid = QueryEptStatus(&ept_classification);','ept_query_valid = TRUE;'),
 $m.Replace('LoaderEptStatusProvesReady(','LoaderEptStatusProvesDetached('),
 $m.Replace('LoaderEptStatusProvesDetached(','LoaderEptStatusProvesReady('),
 $m.Replace('detached_ept_query_valid = QueryEptStatus(','detached_ept_query_valid = TRUE; /*')
)
$i=0;foreach($x in $mm){$i++;A ($x-ne$m) "vacuous main mutation $i";$bad=$false;try{V $x $s $h $t}catch{$bad=$true};A $bad "accepted main mutation $i"}
$sm=@($s.Replace('classification == LOADER_EPT_WIRE_READY','classification == LOADER_EPT_WIRE_RESIDUAL'),$s.Replace('classification == LOADER_EPT_WIRE_DETACHED','classification == LOADER_EPT_WIRE_RESIDUAL'))
foreach($x in $sm){A ($x-ne$s) 'vacuous policy mutation';$bad=$false;try{V $m $x $h $t}catch{$bad=$true};A $bad 'accepted policy mutation'}
'Verified bounded 0x808 loader query, duplicate-handle lifetime, dual Runtime/EPT load proof, rollback proof, and unload cleanup gate.'
