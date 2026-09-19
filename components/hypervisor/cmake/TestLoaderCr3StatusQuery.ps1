param([Parameter(Mandatory=$true)][string]$MainPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n $x";$p=$q}}
function S([string]$t,[string]$a,[string]$b){$x=$t.IndexOf($a);$y=$t.IndexOf($b,$x+1);A ($x-ge0-and$y-gt$x) "section $a";$t.Substring($x,$y-$x)}
function V([string]$m){
 A ($m.Contains('hypervisor_loader.exe cr3-status')) 'usage'
 $q=S $m '#define LOADER_CR3_STATUS_MAX_CONTEXTS' 'typedef struct _LOADER_EPT_STATUS_QUERY_CONTEXT'
 foreach($x in @('LOADER_CR3_STATUS_MAX_CONTEXTS 4096u','HYPERVISOR_CR3_STATUS_HEADER_SIZE','HYPERVISOR_CR3_STATUS_RECORD_SIZE','HYPERVISOR_CR3_STATUS_QUERY_REQUEST Request;','HANDLE DeviceHandle;','DWORD FetchBytes, FetchError;','BOOL FetchSucceeded;')){A ($q.Contains($x)) "model $x"}
 $thread=S $q 'static DWORD WINAPI Cr3StatusQueryThread' 'static BOOL QueryCr3Status'
 O $thread @('context->DeviceHandle, IOCTL_HYPERVISOR_CR3_STATUS_QUERY','context->Status, context->StatusSize','&context->FetchBytes','context->FetchError') 'thread'
 A (([regex]::Matches($thread,'DeviceIoControl\(').Count)-eq1) 'single bounded fetch'
 $query=$q.Substring($q.IndexOf('static BOOL QueryCr3Status'))
 O $query @('*status_bytes = NULL','HYPERVISOR_CR3_STATUS_QUERY_VERSION','HYPERVISOR_CR3_STATUS_QUERY_REQUEST_SIZE','HYPERVISOR_CR3_STATUS_QUERY_FLAGS_NONE','context->StatusSize = LOADER_CR3_STATUS_MAX_BYTES','HeapAlloc(','DuplicateHandle(GetCurrentProcess(), g_device_handle','CreateThread(','WaitForSingleObject(thread, timeout_ms)','CancelSynchronousIo(thread)','WaitForSingleObject(thread, 5000u)','!context->FetchSucceeded','CR3 status DeviceIoControl failed','context->FetchBytes < HYPERVISOR_CR3_STATUS_HEADER_SIZE','context->FetchBytes > context->StatusSize','LoaderClassifyCr3Status(','context->Status, context->FetchBytes','LOADER_CR3_WIRE_OBSERVED','LOADER_CR3_WIRE_INACTIVE','*status_bytes = context->Status','*status_size = context->FetchBytes','context->Status = NULL','FreeCr3StatusQueryContext(context)') 'query'
 A ($q.Contains('CloseHandle(context->DeviceHandle)')) 'duplicate closed'
 A ($query.Contains('A non-cancellable request retains context, buffer, and duplicate handle')) 'timeout lifetime'
 $cmd=S $m 'static int CmdCr3Status(void)' 'static int CmdStatus(void)'
 O $cmd @('OpenDevice()','QueryCr3Status(&status_bytes, &status_size, &classification)','memcpy(&header','CR3 Status: class=%s','header.ContextCount','header.TotalTransitions','memcpy(&record','CR3 CPU: index=%u group=%u number=%u resident=%u matched=%u','SUCCESS: Exact redacted CR3 status snapshot verified','HeapFree(GetProcessHeap(), 0, status_bytes)','CloseHandle(g_device_handle)') 'command'
 A ($q-notmatch 'Root|NormalizedRoot|GuestCr3|SystemCr3') 'query redacted'
 A ($cmd-notmatch 'Root|NormalizedRoot|GuestCr3|SystemCr3|OpenScm|StopDriverService|DeleteDriverService|IOCTL_HYPERVISOR_LOAD|IOCTL_HYPERVISOR_UNLOAD') 'command read-only and redacted'
 O $m @('L"ept-violation-roundtrip"','L"cr3-status"','return CmdCr3Status();','L"status"') 'command dispatch'
}
$m=[IO.File]::ReadAllText((Resolve-Path $MainPath));V $m
$mutations=@(
 $m.Replace('context->DeviceHandle, IOCTL_HYPERVISOR_CR3_STATUS_QUERY','g_device_handle, IOCTL_HYPERVISOR_CR3_STATUS_QUERY'),
 $m.Replace('context->StatusSize = LOADER_CR3_STATUS_MAX_BYTES','context->StatusSize = 0u'),
 $m.Replace('context->FetchBytes < HYPERVISOR_CR3_STATUS_HEADER_SIZE','false'),
 $m.Replace('context->FetchBytes > context->StatusSize','false'),
 $m.Replace('*classification = LoaderClassifyCr3Status(','*classification = LOADER_CR3_WIRE_OBSERVED; /*'),
 $m.Replace('*classification != LOADER_CR3_WIRE_OBSERVED &&','false &&'),
 $m.Replace('(void)CancelSynchronousIo(thread);',''),
 $m.Replace('context->Status = NULL;',''),
 $m.Replace('if (_wcsicmp(argv[1], L"cr3-status") == 0)','if (_wcsicmp(argv[1], L"status") == 0)')
)
$i=0;foreach($x in $mutations){$i++;A ($x-ne$m) 'vacuous mutation';$bad=$false;try{V $x}catch{$bad=$true};A $bad "accepted mutation $i"}
Write-Output 'Verified read-only loader CR3 status: bounded one-shot fetch, exact returned-size classifier gate, duplicate-handle timeout lifetime, redacted output, and no SCM mutation.'