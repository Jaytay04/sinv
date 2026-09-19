param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$HeaderPath)
$ErrorActionPreference='Stop'
function A([bool]$o,[string]$w){if(-not $o){throw $w}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A ($i-ge 0-and $j-gt $i) "section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$x,[string]$n){$p=-1;foreach($v in $x){$q=$t.IndexOf($v,$p+1);A ($q-gt $p) "$n order $v";$p=$q}}
function Verify([string]$s,[string]$h){
 foreach($x in @('WDK-only','INITIALIZING','READY','RESOURCE_GONE','OWNER_GONE','STORAGE_GONE','OperationBusy','internally serialized','Resource lifecycle','no further calls after Destroy','active or error-state','preserves','current stage for retry','without repeating completed teardown','Start and Stop','BUSY','VmxPersistentRuntimeStart','VmxPersistentRuntimeStartDiagnosed','VmxPersistentRuntimeStop','VmxPersistentRuntimeCpuidProbe','VmxPersistentRuntimeMsrReadProbe')){A ($h.Contains($x)) "header $x"}
 A ($h-match 'volatile LONG +OperationBusy;' -and $h-match 'VMX_PERSISTENT_RUNTIME_RESOURCE_GONE += +3' -and $h-match 'VMX_PERSISTENT_RUNTIME_OWNER_GONE += +4' -and $h-match 'VMX_PERSISTENT_RUNTIME_STORAGE_GONE += +5') 'typed stages'
 A ($s-notmatch '(?i)DriverEntry|DriverEvtUnload|IRP_MJ|IOCTL|VMX_V2|CPU0|g_vmx|ExAllocate|ExFree') 'forbidden wiring/global/allocation'
 A ($s-notmatch 'VMX_PERSISTENT_RUNTIME (staged|local|copy)') 'staged runtime copy'
 foreach($x in @('PlatformValid(&c->Kernel)','AllocatorValid(&c->Allocator)','!c->LaunchStorageSize','!c->Cookie','!c->Dispatcher','!Rip(c->GuestRip)','!Rip(c->HostRip)','VmxParseBasicCapabilities(c->VmxBasic')){A ($s.Contains($x)) "config $x"}
 $reject=B $s 'static bool GuardReject' 'static void* GuardPool'
 foreach($x in @('Overlap(a,b)','if(ConfigSpanReleased(r))return false;','return !ConfigSpanActive(r,&c)||Overlap(a,c);')){A ($reject.Contains($x)) "allocator overlap gate $x"}
 $guard=B $s 'static bool GuardCoreValid' 'static bool ConfigValid'
 foreach($x in @('static bool ConfigSpanReleased','static bool ConfigSpanActive','ConfigBegin!=r->ConfigBeginContract','ConfigEnd!=r->ConfigEndContract')){A ($s.Contains($x)) "config span integrity $x"}
 foreach($x in @('ConfigSpanReleased(r)||(ConfigSpanActive(r,&b)','AllocatorEqual(&r->SourceAllocator,&r->SourceAllocatorContract)','GuardedAllocator.Context==(void*)r','VMX_PERSISTENT_GUARDED_PLATFORM_MAGIC','PlatformEqual(&g->Source,&g->SourceContract)','g->ForbiddenBegin==(uintptr_t)r','g->ForbiddenEnd==(uintptr_t)r+sizeof(*r)','PlatformEqual(&g->Platform,&g->PlatformContract)','VmxPersistentGuardedPlatformInitialize(&g->Source,&e,sizeof(e),&e)','PlatformEqual(&g->Platform,&e.Platform)','GuardCoreValid(r)&&PlatformEqual(&r->GuardedPlatform.Platform','&r->Storage.Frozen.Base)')){A ($guard.Contains($x)) "guard integrity $x"}
 $bound=B $s 'static bool StorageBound' 'static bool ProviderEqual'
 foreach($x in @('VMX_PERSISTENT_RUNTIME_OWNER_BOUND','SnapshotContract','SnapshotFree','BaseQueryActiveCount','BaseRestore','AllocationContracts','AllocationSizes','VmxPersistentBasicOpsInitialize','VmxPersistentCompositionBind','VmxPersistentOperationOwnerBindingFactory','static bool StorageReady','VMX_PERSISTENT_RUNTIME_STORAGE_READY','Zero(&s->Owner','Zero(p[i],s->AllocationSizes[i])')){A ($bound.Contains($x)) "storage state $x"}
 A ($bound-notmatch 'CurrentVmcs') 'unsynchronized mutable Basic scan'
 $valid=B $s 'static bool ResourceValid' 'static bool RuntimeAcquire'
 foreach($x in @('VMX_PERSISTENT_RESOURCE_MAGIC','VmxPersistentSerializerFactory','VmxPersistentOperationOwnerBindingFactory','BindingEqual','Router.Records','Coordinator.Bridge.OperationContext','static bool SerializerReady','static bool RuntimeStageValid','static bool ResourceGoneValid','VMX_PERSISTENT_RUNTIME_RESOURCE_GONE','Zero(&r->Resource','GuardValid(r)&&StorageBound','static bool OwnerGoneValid','VMX_PERSISTENT_RUNTIME_OWNER_GONE','GuardValid(r)&&StorageReady','static bool StorageGoneValid','VMX_PERSISTENT_RUNTIME_STORAGE_GONE','Zero(&r->Storage','static bool TeardownStage')){A ($valid.Contains($x)) "stage validity $x"}
 foreach($x in @('Records[i]','Contexts[i]','Lifecycle.Busy','Lifecycle.State','CpuState','OwnedResources')){A (-not $valid.Contains($x)) "resource mutable scan $x"}
 $busy=B $s 'static bool RuntimeAcquire' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeInitialize'
 A ($busy.Contains('InterlockedCompareExchange(&r->OperationBusy,1,0)==0')) 'busy CAS'
 A ($busy.Contains('InterlockedExchange(&r->OperationBusy,0)')) 'busy release'
 $init=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeInitialize' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeStart'
 O $init @('Zero(out,sizeof(*out))','ConfigValid(c,out)','KeGetCurrentIrql()!=PASSIVE_LEVEL','VMX_PERSISTENT_RUNTIME_INITIALIZING','out->SourceAllocator=out->SourceAllocatorContract=c->Allocator','out->ConfigBegin=out->ConfigBeginContract=(uintptr_t)c','out->ConfigEnd=out->ConfigEndContract=','out->GuardedAllocator=','VmxPersistentGuardedPlatformInitialize','VmxPersistentRuntimeStorageInitialize','VmxPersistentRuntimeOpsBind','VmxPersistentSerializerInitialize','VmxPersistentResourceInitialize','out->ConfigBegin=out->ConfigEnd=out->ConfigBeginContract=out->ConfigEndContract=0','if(!GuardValid(out))','out->Magic=VMX_PERSISTENT_RUNTIME_MAGIC','VMX_PERSISTENT_RUNTIME_READY','InterlockedExchange(&out->Initialized,1)') 'initialize'
 $route=B $s 'static bool RouteExternalSeparate' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeBindEptRoute'
 foreach($x in @('Span(r,sizeof(*r),&runtime)','Span(context,context_bytes,&external)','!Overlap(runtime,external)','ConfigSpanReleased(r)||(ConfigSpanActive(r,&config)','!Overlap(config,external)','RootLeaseExternalSeparate(r,context,context_bytes)')){A ($route.Contains($x)) "route separation $x"}
 $start=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeStart' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeStartDiagnosed'
 $diagnosed=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeStartDiagnosed' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeStop'
 $stop=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeStop' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeDiagnosticWrite'
$diagnostic=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeDiagnosticWrite' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeDiagnosticEptWrite'
$eptDiagnostic=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeDiagnosticEptWrite' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeCpuidProbe'
 $cpuid=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeCpuidProbe' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeMsrReadProbe'
 $msr=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeMsrReadProbe' 'static VMX_PERSISTENT_STATUS RootProofLocked'
 foreach($op in @(@($start,'VmxPersistentResourceStart'),@($stop,'VmxPersistentResourceStop'))){$body=$op[0];$call=$op[1];O $body @('if(!r)','KeGetCurrentIrql()!=PASSIVE_LEVEL','RuntimeAcquire(r)','TeardownStage(r->Stage)','VMX_PERSISTENT_STATUS_BUSY','else status=RuntimeValid(r)',$call,'RuntimeRelease(r)','return status') $call;A (([regex]::Matches($body,[regex]::Escape($call)+'\(')).Count-eq 1) "$call once";A ($body-notmatch 'Stage,|Serializer|Coordinator|\.Enter|\.Leave') "$call authority"}
 O $diagnosed @('StartTraceArgs(r,out)','RUNTIME_VALIDATION','KeGetCurrentIrql()!=PASSIVE_LEVEL','RUNTIME_PASSIVE','RuntimeAcquire(r)','RUNTIME_BUSY','TeardownStage(r->Stage)','RUNTIME_TEARDOWN','RuntimeValid(r)','VmxPersistentResourceStartDiagnosed(&r->Resource,out)','RuntimeRelease(r)','return status') 'diagnosed Start'
 A ($diagnosed-notmatch 'Serializer|Coordinator|\.Enter|\.Leave') 'diagnosed resource authority'
 O $diagnostic @('if(!r||!target)','KeGetCurrentIrql()!=PASSIVE_LEVEL','RuntimeAcquire(r)','RuntimeValid(r)','Lifecycle.Busy','Lifecycle.State!=VMX_PERSISTENT_LOADED','system_index>=r->Storage.Count','system_index>=r->Resource.Count','VmxProcessorIdentityEqual(','GuardReject(r,target,1)','RootLeaseExternalSeparate(r,target,1)','VmxPersistentDiagnosticWriteContext(','RuntimeRelease(r)','return status') 'diagnostic write'
 A (([regex]::Matches($diagnostic,'RuntimeAcquire')).Count-eq 1-and
    ([regex]::Matches($diagnostic,'RuntimeRelease')).Count-eq 1-and
    ([regex]::Matches($diagnostic,'VmxPersistentDiagnosticWriteContext')).Count-eq 1) 'diagnostic exact calls'
A ($diagnostic-notmatch 'Serializer|\.Enter|\.Leave|Allocate|Free|VMXON|VMLAUNCH|VMXOFF') 'diagnostic boundary'
 O $eptDiagnostic @('if(!r||!target)','KeGetCurrentIrql()!=PASSIVE_LEVEL','RuntimeAcquire(r)','RuntimeValid(r)','Lifecycle.Busy','Lifecycle.State!=VMX_PERSISTENT_LOADED','system_index>=r->Storage.Count','system_index>=r->Resource.Count','VmxProcessorIdentityEqual(','GuardReject(r,target,1)','RootLeaseExternalSeparate(r,target,1)','VmxPersistentDiagnosticEptWriteContext(','RuntimeRelease(r)','return status') 'EPT direct diagnostic write'
 A (([regex]::Matches($eptDiagnostic,'RuntimeAcquire')).Count-eq 1-and
    ([regex]::Matches($eptDiagnostic,'RuntimeRelease')).Count-eq 1-and
    ([regex]::Matches($eptDiagnostic,'VmxPersistentDiagnosticEptWriteContext')).Count-eq 1) 'EPT diagnostic exact calls'
 A ($eptDiagnostic-notmatch 'Serializer|\.Enter|\.Leave|Allocate|Free|VMXON|VMLAUNCH|VMXOFF|VmxPersistentDiagnosticWriteContext') 'EPT diagnostic boundary'
 O $cpuid @('if(!r||!target)','KeGetCurrentIrql()!=PASSIVE_LEVEL','RuntimeAcquire(r)','RuntimeValid(r)','Lifecycle.Busy','Lifecycle.State!=VMX_PERSISTENT_LOADED','system_index>=r->Storage.Count','system_index>=r->Resource.Count','VmxProcessorIdentityEqual(','GuardReject(r,target,sizeof(*target))','RootLeaseExternalSeparate(r,target,sizeof(*target))','VmxPersistentDiagnosticCpuidContext(','RuntimeRelease(r)','return status') 'CPUID diagnostic'
 A (([regex]::Matches($cpuid,'RuntimeAcquire')).Count-eq 1-and
    ([regex]::Matches($cpuid,'RuntimeRelease')).Count-eq 1-and
    ([regex]::Matches($cpuid,'VmxPersistentDiagnosticCpuidContext')).Count-eq 1) 'CPUID diagnostic exact calls'
 A ($cpuid-notmatch 'Serializer|\.Enter|\.Leave|Allocate|Free|VMXON|VMLAUNCH|VMXOFF') 'CPUID diagnostic boundary'
 O $msr @('if(!r||!target)','KeGetCurrentIrql()!=PASSIVE_LEVEL','RuntimeAcquire(r)','RuntimeValid(r)','Lifecycle.Busy','Lifecycle.State!=VMX_PERSISTENT_LOADED','system_index>=r->Storage.Count','system_index>=r->Resource.Count','VmxProcessorIdentityEqual(','GuardReject(r,target,sizeof(*target))','RootLeaseExternalSeparate(r,target,sizeof(*target))','VmxPersistentDiagnosticMsrReadContext(','RuntimeRelease(r)','return status') 'MSR read diagnostic'
 A (([regex]::Matches($msr,'RuntimeAcquire')).Count-eq 1-and
    ([regex]::Matches($msr,'RuntimeRelease')).Count-eq 1-and
    ([regex]::Matches($msr,'VmxPersistentDiagnosticMsrReadContext')).Count-eq 1) 'MSR read diagnostic exact calls'
 A ($msr-notmatch 'Serializer|\.Enter|\.Leave|Allocate|Free|VMXON|VMLAUNCH|VMXOFF') 'MSR read diagnostic boundary'
 $destroy=$s.Substring($s.IndexOf('VMX_PERSISTENT_STATUS VmxPersistentRuntimeDestroy'))
 O $destroy @('RuntimeAcquire(r)','stage=r->Stage','stage==VMX_PERSISTENT_RUNTIME_READY','RuntimeValid(r)','VmxPersistentResourceStop','VmxPersistentResourceDestroy','InterlockedExchange(&r->Stage','VMX_PERSISTENT_RUNTIME_RESOURCE_GONE','stage==VMX_PERSISTENT_RUNTIME_RESOURCE_GONE','ResourceGoneValid(r)','VmxPersistentRuntimeOpsUnbind','InterlockedExchange(&r->Stage','VMX_PERSISTENT_RUNTIME_OWNER_GONE','stage==VMX_PERSISTENT_RUNTIME_OWNER_GONE','OwnerGoneValid(r)','VmxPersistentRuntimeStorageDestroy','InterlockedExchange(&r->Stage','VMX_PERSISTENT_RUNTIME_STORAGE_GONE','StorageGoneValid(r)','VmxPersistentSerializerShutdown','RtlZeroMemory(r','fail:','RuntimeRelease(r)','return status') 'staged destroy'
 foreach($call in @('VmxPersistentResourceStop','VmxPersistentResourceDestroy','VmxPersistentRuntimeOpsUnbind','VmxPersistentRuntimeStorageDestroy','VmxPersistentSerializerShutdown')){A (([regex]::Matches($destroy,$call)).Count-eq 1) "destroy once $call"}
 A (([regex]::Matches($destroy,'goto fail')).Count-eq 10) 'all destroy failures release'
 foreach($stage in @('RESOURCE_GONE','OWNER_GONE','STORAGE_GONE')){A ($destroy-match ('InterlockedExchange\(&r->Stage,\s+VMX_PERSISTENT_RUNTIME_'+$stage+'\)')) "publish $stage"}
 A ($destroy-notmatch 'Coordinator|\.Enter|\.Leave|SerializerFactory|Lifecycle\.') 'destroy no direct serializer/coordinator'
}
$s=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath));Verify $s $h
$m=@(
 $s.Replace('if(ConfigSpanReleased(r))return false;','if(ConfigSpanReleased(r))return true;'),
 $s.Replace('return !ConfigSpanActive(r,&c)||Overlap(a,c);','return false;'),
 $s.Replace('config_valid=ConfigSpanReleased(r)||(ConfigSpanActive(r,&b)&&','config_valid=ConfigSpanActive(r,&b)&&'),
 $s.Replace('out->ConfigBegin=out->ConfigEnd=out->ConfigBeginContract=out->ConfigEndContract=0;',''),
 $s.Replace('(ConfigSpanReleased(r)||(ConfigSpanActive(r,&config)&&','(false||(ConfigSpanActive(r,&config)&&'),
 $s.Replace('out->SourceAllocator=out->SourceAllocatorContract=c->Allocator','out->SourceAllocator=c->Allocator'),
 $s.Replace('out->ConfigEnd=out->ConfigEndContract=(uintptr_t)c+sizeof(*c);','out->ConfigEnd=(uintptr_t)c+sizeof(*c);'),
 $s.Replace('PlatformEqual(&g->Platform,&e.Platform)','true'),
 $s.Replace('GuardCoreValid(r)&&PlatformEqual','true&&PlatformEqual'),
 $s.Replace('InterlockedCompareExchange(&r->OperationBusy,1,0)','InterlockedExchange(&r->OperationBusy,1)'),
 $s.Replace('if(TeardownStage(r->Stage))status=VMX_PERSISTENT_STATUS_BUSY;','if(false)status=VMX_PERSISTENT_STATUS_BUSY;'),
 $s.Replace('else status=RuntimeValid(r)?VmxPersistentResourceStart','status=RuntimeValid(r)?VmxPersistentResourceStart'),
 $s.Replace('VmxPersistentResourceStartDiagnosed(&r->Resource,out)','VmxPersistentResourceStart(&r->Resource)'),
 $s.Replace('else status=RuntimeValid(r)?VmxPersistentResourceStop','status=RuntimeValid(r)?VmxPersistentResourceStop'),
 $s.Replace('r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED','false'),
 $s.Replace('system_index>=r->Resource.Count','false'),
 $s.Replace('GuardReject(r,target,1)','false'),
 $s.Replace('!RootLeaseExternalSeparate(r,target,1)','false'),
 $s.Replace('VmxPersistentDiagnosticWriteContext(','VmxPersistentStopContext('),
 $s.Replace('VmxPersistentDiagnosticEptWriteContext(','VmxPersistentDiagnosticWriteContext('),
 $s.Replace('VmxPersistentDiagnosticCpuidContext(','VmxPersistentStopContext('),
 $s.Replace('VmxPersistentDiagnosticMsrReadContext(','VmxPersistentStopContext('),
 $s.Replace('Zero(&r->Resource,sizeof(r->Resource))','true'),
 $s.Replace('Zero(&r->Storage,sizeof(r->Storage))','true'),
 $s.Replace('GuardValid(r)&&StorageReady(&r->Storage)','GuardValid(r)&&StorageBound(&r->Storage)'),
 $s.Replace('status=VmxPersistentResourceStop(&r->Resource);if(status!=0)goto fail;','status=VMX_PERSISTENT_STATUS_OK;'),
 $s.Replace('status=VmxPersistentResourceDestroy(&r->Resource);if(status!=0)goto fail;','status=VmxPersistentResourceDestroy(&r->Resource);'),
 $s.Replace('VMX_PERSISTENT_RUNTIME_RESOURCE_GONE);','VMX_PERSISTENT_RUNTIME_OWNER_GONE);'),
 $s.Replace('status=VmxPersistentRuntimeOpsUnbind(&r->Storage);if(status!=0)goto fail;','status=VmxPersistentRuntimeOpsUnbind(&r->Storage);'),
 $s.Replace('VMX_PERSISTENT_RUNTIME_OWNER_GONE);','VMX_PERSISTENT_RUNTIME_STORAGE_GONE);'),
 $s.Replace('status=VmxPersistentRuntimeStorageDestroy(&r->Storage);if(status!=0)goto fail;','status=VmxPersistentRuntimeStorageDestroy(&r->Storage);'),
 $s.Replace('status=VmxPersistentSerializerShutdown(&r->Serializer);if(status!=0)goto fail;','status=VmxPersistentSerializerShutdown(&r->Serializer);'),
 $s.Replace('status=VmxPersistentResourceDestroy(&r->Resource);if(status!=0)goto fail;','status=VmxPersistentResourceDestroy(&r->Resource);if(status!=0)return status;'),
 $s.Replace('status=VmxPersistentResourceStop(&r->Resource);','(void)VmxPersistentSerializerFactory(&r->Serializer);status=VmxPersistentResourceStop(&r->Resource);'),
 ($s+[Environment]::NewLine+'void DriverEvtUnload(void){}')
)
$k=0;foreach($x in $m){$k++;A ($x-ne $s) 'vacuous mutation';$bad=$false;try{Verify $x $h}catch{$bad=$true};A $bad "accepted source mutation $k"}
$hm=@(
 $h.Replace('VMX_PERSISTENT_RUNTIME_RESOURCE_GONE = 3','VMX_PERSISTENT_RUNTIME_RESOURCE_GONE = 2'),
 $h.Replace('without repeating completed teardown','with repeated teardown'),
 $h.Replace('volatile LONG OperationBusy;','void* OperationBusy;')
)
$k=0;foreach($x in $hm){$k++;A ($x-ne $h) 'vacuous header mutation';$bad=$false;try{Verify $s $x}catch{$bad=$true};A $bad "accepted header mutation $k"}
Write-Output 'Verified lifecycle: atomic operations, ownership-only staged retry, zero/consistency gates, exact single teardown calls, failure preservation, and forbidden mutation rejection.'
