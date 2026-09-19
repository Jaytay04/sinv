param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$RuntimeSourcePath,[Parameter(Mandatory=$true)][string]$RuntimeHeaderPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A ($i-ge 0-and$j-gt$i) "section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n order $x";$p=$q}}
function V([string]$s,[string]$h,[string]$rs,[string]$rh){
 foreach($x in @('WDK-only','embedded persistent runtime','PASSIVE-only','non-reentrant','must not mutate','must not overlap Owner','trusted immutable vmx_kernel_adapters','Owner lock acquisition','Runtime OperationBusy','DETACHED','ATTACHING','ATTACHED','TEARDOWN_PENDING','unchanged generation','remains ATTACHING fail-closed','exact Runtime status for retry','VmxPersistentDriverOwnerInitialize','VmxPersistentDriverOwnerAttach','VmxPersistentDriverOwnerStart','VmxPersistentDriverOwnerStop','VmxPersistentDriverOwnerDetach')){A ($h.Contains($x)) "header $x"}
 foreach($x in @('EX_PUSH_LOCK Lock;','PVOID volatile LockOwner;','uint32_t Magic, MagicContract;','uint32_t Version, VersionContract;','volatile LONG Phase, PhaseContract;','uint64_t Generation, GenerationContract;','VMX_PERSISTENT_RUNTIME Runtime;')){A ($h.Contains($x)) "field $x"}
 A ($h-notmatch 'VmxPersistentDriverOwner(Get|Runtime)') 'no raw runtime API'
 A ($s-notmatch '(?i)DriverEntry|DriverEvtUnload|IRP_MJ|IOCTL|core_mgmt|VMX_V2|CPU0|g_vmx|ExAllocate|ExFree') 'forbidden'
 $trusted=B $s 'static bool PlatformEqual' 'static bool Separate'
 foreach($x in @('QueryActiveCount','MapSystemIndex','Allocate','Free','CheckPassive','Pin','QueryCurrent','Restore','AllocatePool','FreePool','AllocateContiguous','FreeContiguous','GetPhysicalAddress','VmxKernelPersistentAllocator()','KernelPersistentPlatform')){A ($trusted.Contains($x)) "trusted $x"}
 $pub=B $s 'static void PublishPhase' 'static bool Advance';O $pub @('PhaseContract=(LONG)~(uint32_t)phase','KeMemoryBarrier()','InterlockedExchange(&o->Phase,phase)') 'phase'
 $advance=B $s 'static bool Advance' 'static bool RuntimeReady';O $advance @('Generation==UINT64_MAX','n=o->Generation+1','GenerationContract=~n','KeMemoryBarrier()','o->Generation=n') 'generation'
 $valid=B $s 'static bool OwnerValid' 'static VMX_PERSISTENT_STATUS OwnerEnter'
 foreach($x in @('MagicContract!=~VMX_PERSISTENT_DRIVER_OWNER_MAGIC','VersionContract!=~VMX_PERSISTENT_DRIVER_OWNER_VERSION','LockOwner,NULL,NULL)!=PsGetCurrentThread()','GenerationContract!=~o->Generation','PhaseContract!=~(uint32_t)p','VMX_PERSISTENT_DRIVER_OWNER_DETACHED','Zero(&o->Runtime','VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','RuntimeReady(&o->Runtime)','VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING','RuntimePending(&o->Runtime)')){A ($valid.Contains($x)) "valid $x"}
 $enter=B $s 'static VMX_PERSISTENT_STATUS OwnerEnter' 'static void OwnerLeave'
 O $enter @('if(!o)','KeGetCurrentIrql()!=PASSIVE_LEVEL','Published(o)','PsGetCurrentThread()','LockOwner,NULL,NULL)==current','KeEnterCriticalRegion()','ExTryAcquirePushLockExclusive(&o->Lock)','KeLeaveCriticalRegion()','VMX_PERSISTENT_STATUS_BUSY','InterlockedCompareExchangePointer(&o->LockOwner,current,NULL)','ExReleasePushLockExclusive(&o->Lock)','KeLeaveCriticalRegion()') 'enter'
 A ($enter-notmatch 'ExAcquirePushLockExclusive\(&o->Lock\)') 'owner lock must never block'
 $leave=B $s 'static void OwnerLeave' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerInitialize';O $leave @('InterlockedExchangePointer(&o->LockOwner,NULL)','ExReleasePushLockExclusive(&o->Lock)','KeLeaveCriticalRegion()') 'leave'
 $init=B $s 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerInitialize' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAttach'
 O $init @('if(!o)','KeGetCurrentIrql()!=PASSIVE_LEVEL','magic=ReadMagic(o)','magic==VMX_PERSISTENT_DRIVER_OWNER_MAGIC','Zero(o,sizeof(*o))','ExInitializePushLock','MagicContract=~','Version=VMX_PERSISTENT_DRIVER_OWNER_VERSION','Generation=0','PublishPhase(o,VMX_PERSISTENT_DRIVER_OWNER_DETACHED)','KeMemoryBarrier()','o->Magic=VMX_PERSISTENT_DRIVER_OWNER_MAGIC') 'initialize'
 $attach=B $s 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAttach' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerStart'
 O $attach @('OwnerEnter(o)','OwnerValid(o)','Phase!=VMX_PERSISTENT_DRIVER_OWNER_DETACHED','TrustedConfig(c)','Separate(o,sizeof(*o),c,sizeof(*c))','Generation==UINT64_MAX','VMX_PERSISTENT_DRIVER_OWNER_ATTACHING','VmxPersistentRuntimeInitialize(c,&o->Runtime)','if(status!=0)','!Zero(&o->Runtime','VMX_PERSISTENT_DRIVER_OWNER_DETACHED','RuntimeReady(&o->Runtime)','Advance(o)','VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','done:OwnerLeave(o)','return status') 'attach'
 A (([regex]::Matches($attach,'VmxPersistentRuntimeInitialize')).Count-eq 1-and$attach-notmatch 'RuntimeDestroy|RuntimeStart|RuntimeStop|TEARDOWN_PENDING') 'attach authority/fail closed'
 $start=B $s 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerStart' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerStop'
 $stop=B $s 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerStop' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDiagnosticWrite'
 foreach($op in @(@($start,'VmxPersistentRuntimeStart'),@($stop,'VmxPersistentRuntimeStop'))){$body=$op[0];$call=$op[1]
  O $body @('OwnerEnter(o)','if(status!=0)return status','OwnerValid(o)','Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VMX_PERSISTENT_STATUS_BUSY','else status='+$call+'(&o->Runtime)','OwnerLeave(o)','return status') $call
  A (([regex]::Matches($body,$call)).Count-eq 1) "$call once"
  A ($body-notmatch 'PublishPhase|Advance\(|Generation|PhaseContract|InterlockedExchange|Serializer|Allocate|Free') "$call preserves owner"
  $after=$body.Substring($body.IndexOf('if(status!=0)return status')+'if(status!=0)return status'.Length);A ($after-notmatch 'return status;.*OwnerLeave') "$call release order"
 }
 $diagnostic=B $s 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDiagnosticWrite' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDiagnosticEptWrite'
 O $diagnostic @('if(!o||!target','Separate(o,sizeof(*o),target,1)','OwnerEnter(o)','OwnerValid(o)','Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VmxPersistentRuntimeDiagnosticWrite(','OwnerLeave(o)','return status') 'diagnostic write'
 A (([regex]::Matches($diagnostic,'VmxPersistentRuntimeDiagnosticWrite')).Count-eq 1-and
    $diagnostic-notmatch 'PublishPhase|Advance\(|Generation|Serializer|Allocate|Free') 'diagnostic exact delegation/preservation'
 $eptDiagnostic=B $s 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDiagnosticEptWrite' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerCpuidProbe'
 O $eptDiagnostic @('if(!o||!target','Separate(o,sizeof(*o),target,1)','OwnerEnter(o)','OwnerValid(o)','Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VmxPersistentRuntimeDiagnosticEptWrite(','OwnerLeave(o)','return status') 'EPT direct diagnostic write'
 A (([regex]::Matches($eptDiagnostic,'VmxPersistentRuntimeDiagnosticEptWrite')).Count-eq 1-and
    $eptDiagnostic-notmatch 'PublishPhase|Advance\(|Generation|Serializer|Allocate|Free|VmxPersistentRuntimeDiagnosticWrite\(') 'EPT diagnostic exact delegation/preservation'
 $detach=B $s 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDetach' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAcquireStatusSnapshot'
 O $detach @('OwnerEnter(o)','OwnerValid(o)','Phase==VMX_PERSISTENT_DRIVER_OWNER_DETACHED','status=0','Generation==UINT64_MAX','VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING','VmxPersistentRuntimeDestroy(&o->Runtime)','if(status!=0)goto done','!Zero(&o->Runtime','Advance(o)','VMX_PERSISTENT_DRIVER_OWNER_DETACHED','done:OwnerLeave(o)','return status') 'detach'
 A (([regex]::Matches($detach,'VmxPersistentRuntimeDestroy')).Count-eq 1-and$detach-notmatch 'RuntimeStart|RuntimeStop') 'detach authority'
 foreach($x in @('VMX_PERSISTENT_DRIVER_SNAPSHOT_MAGIC','OwnerGeneration, OwnerGenerationContract','Size, SizeContract','const void* Buffer; const void* BufferContract;','Allocator, AllocatorContract','pointer-free V3F1 wire image','canonical 48-byte V3F1 tombstones','UNLOADED, zero contexts','both diagnostic indices NO_INDEX','TEARDOWN_PENDING remains BUSY','handle and wire buffer immutable','works after Owner Detach','rejects double release','VmxPersistentDriverOwnerAcquireStatusSnapshot','VmxPersistentDriverOwnerReleaseStatusSnapshot')){A ($h.Contains($x)) "snapshot header $x"}
 $acquire=B $s 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAcquireStatusSnapshot' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerReleaseStatusSnapshot'
 O $acquire @('Separate(o,sizeof(*o),out,sizeof(*out))','Zero(out,sizeof(*out))','OwnerEnter(o)','OwnerValid(o)','Phase==VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING','VMX_PERSISTENT_STATUS_BUSY','VmxKernelPersistentAllocator()','Phase==VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VmxPersistentRuntimeEncodePublicStatus(&o->Runtime,NULL,0','VMX_PERSISTENT_PUBLIC_STATUS_BUFFER_TOO_SMALL','else required=sizeof(tombstone)','AllocatePool','Separate(o,sizeof(*o),buffer,required)','Separate(out,sizeof(*out),buffer,required)','Phase==VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VmxPersistentRuntimeEncodePublicStatus(','VMX_PERSISTENT_PUBLIC_STATUS_OK','actual!=required','tombstone.Version=VMX_PERSISTENT_PUBLIC_STATUS_VERSION','tombstone.HeaderSize=VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE','tombstone.RecordSize=VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE','tombstone.TotalSize=VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE','tombstone.LifecycleState=VMX_PERSISTENT_UNLOADED','tombstone.FirstOperationIndex=VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX','tombstone.FirstCleanupIndex=VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX','RtlZeroMemory(buffer,required)','RtlCopyMemory(buffer,&tombstone,sizeof(tombstone))','OwnerGeneration=o->Generation','Size=required','Buffer=staged.BufferContract=buffer','Allocator=staged.AllocatorContract=*a','staged.Magic=','KeMemoryBarrier()','RtlCopyMemory(out,&staged','fail:a->FreePool','RtlZeroMemory(out','done:OwnerLeave(o)') 'snapshot acquire'
 A ($acquire.Contains('VMX_PERSISTENT_PUBLIC_STATUS_HEADER tombstone={0}')-and
    ([regex]::Matches($acquire,'VmxPersistentRuntimeEncodePublicStatus')).Count-eq 2) 'canonical tombstone and attached query/encode exactly'
 $tomb=B $acquire ' }else{' ' staged.MagicContract'
 A ($tomb-notmatch 'VmxPersistentRuntimeEncodePublicStatus|tombstone\.(ContextCount|Flags|Reserved|FirstOperationStatus|FirstCleanupStatus)\s*=') 'tombstone zero fields and no runtime'
 $release=B $s 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerReleaseStatusSnapshot' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAcquireCr3StatusSnapshot'
 O $release @('KeGetCurrentIrql()!=PASSIVE_LEVEL','RtlCopyMemory(&h,s','VmxKernelPersistentAllocator()','MagicContract!=~','OwnerGenerationContract!=~','SizeContract!=~','Buffer!=h.BufferContract','Separate(s,sizeof(*s),h.Buffer,h.Size)','AllocatorEqual(&h.Allocator,&h.AllocatorContract)','AllocatorEqual(&h.Allocator,a)','RtlZeroMemory(s','FreePool(h.Allocator.Context,(void*)h.Buffer)') 'snapshot release'
 A ($release-notmatch 'OwnerEnter|Runtime|ExAcquire|Serializer') 'release owner independent'
 foreach($x in @('synchronous encoder','OperationBusy','never the serializer','gates preserve output, required_size, and encode_result','only an OK gate','VmxPersistentRuntimeEncodePublicStatus')){A ($rh.Contains($x)) "runtime header $x"}
 $encode=B $rs 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeEncodePublicStatus' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeEncodeCr3Status'
 O $encode @('StatusArgs(r,out,n,required,encode_result)','KeGetCurrentIrql()!=PASSIVE_LEVEL','RuntimeAcquire(r)','RuntimeValid(r)','Resource.Coordinator.Lifecycle','Resource.Contexts,r->Resource.Count','Storage.OperationResults,r->Storage.Count','VmxPersistentPublicStatusEncode','RuntimeRelease(r)','*encode_result=encoded') 'runtime encode'
 A (([regex]::Matches($encode,'RuntimeAcquire')).Count-eq 1-and([regex]::Matches($encode,'RuntimeRelease')).Count-eq 1-and([regex]::Matches($encode,'VmxPersistentPublicStatusEncode')).Count-eq 1) 'runtime exact calls'
 $afterAcquire=$encode.Substring($encode.IndexOf('if(!RuntimeAcquire(r))')+'if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;'.Length)
 A ($afterAcquire.Substring(0,$afterAcquire.IndexOf('done:RuntimeRelease(r)'))-notmatch 'return') 'runtime acquired paths release'
 A ($encode-notmatch 'Serializer|Allocate|Free|VMXON|VMLAUNCH|VMXOFF|\.Enter|\.Leave') 'runtime encode forbidden'
}
$s=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath))
$rs=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $RuntimeSourcePath));$rh=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $RuntimeHeaderPath));V $s $h $rs $rh
$m=@(
 $s.Replace('if(!Published(o))return VMX_PERSISTENT_STATUS_INVALID;','if(false)return VMX_PERSISTENT_STATUS_INVALID;'),
 $s.Replace('if(!ExTryAcquirePushLockExclusive(&o->Lock)){','if(false){'),
 $s.Replace('LockOwner,NULL,NULL)==current','LockOwner,NULL,NULL)==NULL'),
 $s.Replace('PhaseContract=(LONG)~(uint32_t)phase','PhaseContract=phase'),
 $s.Replace('o->GenerationContract=~n','o->GenerationContract=n'),
 $s.Replace('magic==VMX_PERSISTENT_DRIVER_OWNER_MAGIC','magic==0'),
 $s.Replace('!TrustedConfig(c)||!Separate','!Separate'),
 $s.Replace('a->QueryCurrent==b->QueryCurrent&&a->Restore==b->Restore','a->QueryCurrent==b->QueryCurrent'),
 $s.Replace('a->GetPhysicalAddress==b->GetPhysicalAddress','true'),
 $s.Replace('PublishPhase(o,VMX_PERSISTENT_DRIVER_OWNER_ATTACHING);','PublishPhase(o,VMX_PERSISTENT_DRIVER_OWNER_ATTACHED);'),
 $s.Replace('VmxPersistentRuntimeInitialize(c,&o->Runtime)','VmxPersistentRuntimeInitialize(c,(VMX_PERSISTENT_RUNTIME*)c)'),
 $s.Replace('if(!Zero(&o->Runtime,sizeof(o->Runtime)))','if(false)'),
 $s.Replace('if(status!=0)goto done;','if(status!=0)status=0;'),
 $s.Replace('if(!Zero(&o->Runtime,sizeof(o->Runtime))||!Advance(o))','if(!Advance(o))'),
 $s.Replace('else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)','else if(o->Phase==VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)'),
 $s.Replace('else status=VmxPersistentRuntimeStart(&o->Runtime);','else status=VMX_PERSISTENT_STATUS_OK;'),
 $s.Replace('else status=VmxPersistentRuntimeStop(&o->Runtime);','else status=VMX_PERSISTENT_STATUS_OK;'),
 $s.Replace('!o||!target||!Separate(o,sizeof(*o),target,1)','!o||!target'),
 $s.Replace('else status=VmxPersistentRuntimeDiagnosticWrite(','else status=VmxPersistentRuntimeStop('),
 $s.Replace('else status=VmxPersistentRuntimeDiagnosticEptWrite(','else status=VmxPersistentRuntimeDiagnosticWrite('),
 $s.Replace('else status=VmxPersistentRuntimeStart(&o->Runtime);','PublishPhase(o,VMX_PERSISTENT_DRIVER_OWNER_ATTACHED);else status=VmxPersistentRuntimeStart(&o->Runtime);'),
 $s.Replace('OwnerLeave(o);return status;','return status;OwnerLeave(o);'),
 $s.Replace('VmxPersistentRuntimeEncodePublicStatus(&o->Runtime,NULL,0','VmxPersistentRuntimeEncodePublicStatus(&o->Runtime,out,sizeof(*out)'),
 $s.Replace('buffer=a->AllocatePool(a->Context,required);','buffer=NULL;'),
 $s.Replace('staged.OwnerGeneration=o->Generation','staged.OwnerGeneration=0'),
 $s.Replace('staged.Buffer=staged.BufferContract=buffer','staged.Buffer=buffer'),
 $s.Replace('RtlCopyMemory(out,&staged,sizeof(staged))','RtlZeroMemory(out,sizeof(*out))'),
 $s.Replace('Phase==VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING','Phase==VMX_PERSISTENT_DRIVER_OWNER_DETACHED'),
 $s.Replace('else required=sizeof(tombstone)','else required=0'),
 $s.Replace('tombstone.LifecycleState=VMX_PERSISTENT_UNLOADED','tombstone.LifecycleState=VMX_PERSISTENT_ERROR'),
 $s.Replace('tombstone.Version=VMX_PERSISTENT_PUBLIC_STATUS_VERSION;','tombstone.Version=VMX_PERSISTENT_PUBLIC_STATUS_VERSION;tombstone.ContextCount=1;'),
 $s.Replace('tombstone.HeaderSize=VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE;','tombstone.HeaderSize=VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE;tombstone.Flags=1;'),
 $s.Replace('tombstone.FirstOperationIndex=VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX','tombstone.FirstOperationIndex=0'),
 $s.Replace('tombstone.FirstCleanupIndex=VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX','tombstone.FirstCleanupIndex=0'),
 $s.Replace('RtlZeroMemory(buffer,required);RtlCopyMemory(buffer,&tombstone,sizeof(tombstone));','RtlCopyMemory(buffer,&tombstone,sizeof(tombstone));'),
 $s.Replace('RtlZeroMemory(s,sizeof(*s));h.Allocator.FreePool','h.Allocator.FreePool'),
 ($s+[Environment]::NewLine+'void DriverEvtUnload(void){}')
)
$i=0;foreach($x in $m){$i++;A ($x-ne$s) 'vacuous mutation';$bad=$false;try{V $x $h $rs $rh}catch{$bad=$true};A $bad "accepted source mutation $i"}
$rm=@(
 $rs.Replace('if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;','if(false)return VMX_PERSISTENT_STATUS_BUSY;'),
 $rs.Replace('if(!RuntimeValid(r))','if(false)'),
 $rs.Replace('status=VMX_PERSISTENT_STATUS_INVALID;goto done;','status=VMX_PERSISTENT_STATUS_INVALID;return status;'),
 $rs.Replace('&r->Resource.Coordinator.Lifecycle','NULL'),
 $rs.Replace('r->Storage.OperationResults,r->Storage.Count','r->Storage.OperationResults,r->Resource.Count'),
 $rs.Replace('done:RuntimeRelease(r);','done:'),
 $rs.Replace('encoded=VmxPersistentPublicStatusEncode(&view,out,n,required);','encoded=VMX_PERSISTENT_PUBLIC_STATUS_OK;')
)
$i=0;foreach($x in $rm){$i++;A ($x-ne$rs) 'vacuous runtime mutation';$bad=$false;try{V $s $h $x $rh}catch{$bad=$true};A $bad "accepted runtime mutation $i"}
$hm=@(
 $h.Replace('volatile LONG Phase, PhaseContract;','void* Phase;'),
 $h.Replace('uint64_t Generation, GenerationContract;','uint32_t Generation;'),
 $h.Replace('must not mutate','may mutate'),
 $h.Replace('exact Runtime status for retry','mapped status for retry'),
 $h.Replace('remains ATTACHING fail-closed','returns DETACHED'),
 $h.Replace('const void* Buffer; const void* BufferContract;','void* Buffer;'),
 $h.Replace('VmxPersistentDriverOwnerStart','VmxPersistentDriverOwnerMissing')
)
$i=0;foreach($x in $hm){$i++;A ($x-ne$h) 'vacuous header mutation';$bad=$false;try{V $s $x $rs $rh}catch{$bad=$true};A $bad "accepted header mutation $i"}
Write-Output 'Verified driver owner: trusted adapters, PASSIVE/reentry gates, owner-to-runtime lock order, authenticated phases/generation, exact attach/detach delegation, retry preservation, and forbidden mutation rejection.'
