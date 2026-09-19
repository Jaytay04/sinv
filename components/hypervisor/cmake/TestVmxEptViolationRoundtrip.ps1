param(
 [Parameter(Mandatory=$true)][string]$Header,
 [Parameter(Mandatory=$true)][string]$Validator,
 [Parameter(Mandatory=$true)][string]$Dispatch,
 [Parameter(Mandatory=$true)][string]$DispatchTest,
 [Parameter(Mandatory=$true)][string]$RuntimeStorage,
 [Parameter(Mandatory=$true)][string]$RuntimeOps,
 [Parameter(Mandatory=$true)][string]$RuntimeLifecycle,
 [Parameter(Mandatory=$true)][string]$Owner,
 [Parameter(Mandatory=$true)][string]$Start,
 [Parameter(Mandatory=$true)][string]$Roundtrip,
 [Parameter(Mandatory=$true)][string]$Ioctl,
 [Parameter(Mandatory=$true)][string]$Loader,
 [Parameter(Mandatory=$true)][string]$AbiTest,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not $x){throw $m}}
function O($t,$v,$n){$p=0;foreach($x in $v){$q=$t.IndexOf($x,$p,[StringComparison]::Ordinal);A($q-ge0)"$n $x";$p=$q+$x.Length}}
function V($h,$v,$d,$dt,$rs,$ro,$rl,$o,$st,$r,$i,$l,$at,$tc){
 A($h.Contains('0x80A, METHOD_BUFFERED, FILE_ANY_ACCESS'))'0x80A ABI'
 foreach($x in @('ROUNDTRIP_REQUEST_SIZE UINT32_C(16)','ROUNDTRIP_RESPONSE_SIZE UINT32_C(128)')){A($h.Contains('HYPERVISOR_EPT_VIOLATION_'+$x))"ABI $x"}
 foreach($x in @('RESPONSE)==128','RESPONSE,GuestPhysicalPage)==16','RESPONSE,RouteState)==64','RESPONSE,DispatcherFailure)==108')){A($h.Contains($x))"ABI assert $x"}
 $va=$v.IndexOf('IoctlValidateEptViolationRoundtripRequest(');A($va-ge0)'validator';$vx=$v.Substring($va)
 O $vx @('request == NULL','NULL_INPUT','input_length != sizeof(*request)','INVALID_LENGTH','request->Version !=','INVALID_VERSION','request->Size !=','INVALID_SIZE','request->Flags !=','INVALID_FLAGS','request->Reserved != 0','NONZERO_RESERVED','ROUNDTRIP_VALID') 'validator order'
 A($d.Contains('VMX_V3D_DISPATCH_EXIT_REASON_EPT_VIOLATION'))'dispatcher reason 48'
 A($d.Contains('return DispatchEptViolation(Context, io, qualification, rip);'))'dispatcher branch delegation'
 O $d @('static uint32_t DispatchEptViolation(','VMX_V3D_DISPATCH_VMCS_GUEST_PHYSICAL_ADDRESS','VMX_V3D_DISPATCH_VMCS_EPT_POINTER','io->EptViolation(io->EptContext, &event)','VMX_V3D_EPT_ROUTE_RESUME','VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST') 'dispatcher route'
 $ea=$d.IndexOf('static uint32_t DispatchEptViolation(');$eb=$d.IndexOf('typedef struct VMX_V3D_CR3_WRITE_CONTEXT');A($ea-ge0-and$eb-gt$ea)'dispatcher bounds';$ex=$d.Substring($ea,$eb-$ea)
 A($ex-notmatch 'Vmwrite|DISPATCH_VMCS_EXIT_LENGTH')'EPT route does not advance RIP'
 foreach($x in @('g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_EPT_VIOLATION','TEST_ASSERT_UINT_EQ(g_read_count, 5','TEST_ASSERT_UINT_EQ(g_write_count, 0','TEST_ASSERT_UINT_EQ(g_ept_count, 1','VMX_V3D_DISPATCH_VMCS_GUEST_PHYSICAL_ADDRESS','VMX_V3D_DISPATCH_VMCS_EPT_POINTER','EPT GPR frame preserved')){A($dt.Contains($x))"dispatcher test $x"}
 foreach($x in @('EptRouteContext','EptRouteContextContract','EptRoute','EptRouteContract','EptRouteBound','EptRouteBoundContract')){A($rs.Contains($x))"storage $x"}
 O $ro @('VmxPersistentRuntimeOpsBindEptRoute(','if(!Bound(s)||!context||!route||s->EptRouteBound!=0)','s->PersistentIo[i].EptContext=context;','s->PersistentIo[i].EptViolation=route;','s->EptRouteBoundContract=~UINT32_C(1)','InterlockedExchange') 'ops bind'
 O $rl @('VmxPersistentRuntimeBindEptRoute(','KeGetCurrentIrql()!=PASSIVE_LEVEL','RuntimeAcquire(r)','RuntimeValid(r)','Lifecycle.State!=VMX_PERSISTENT_UNLOADED','RouteExternalSeparate(','VmxPersistentRuntimeOpsBindEptRoute(','RuntimeRelease(r)') 'runtime bind'
 O $o @('VmxPersistentDriverOwnerBindEptRoute(','Separate(o,sizeof(*o),context,context_bytes)','OwnerEnter(o)','OwnerValid(o)','VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VmxPersistentRuntimeBindEptRoute(','OwnerLeave(o)') 'owner bind'
 O $st @('VmxPersistentDriverOwnerAttach(','report->BindCalls = 1','VmxPersistentDriverOwnerBindEptRoute(','VmxEptHookRoundtripRoute','report->BindStatus','VmxPersistentDriverOwnerStartDiagnosed(') 'attach-bind-start'
 A($r.Contains('if(!OwnerValid(owner)||owner->ByteBefore!=0xA5||'))'arm authenticates retained byte evidence'
 A([regex]::IsMatch($r,'InterlockedCompareExchange64\(\(volatile LONG64\*\)owner->TargetEntry,\s*\(LONG64\)owner->OriginalTargetEntry,\(LONG64\)owner->HookedTargetEntry\)'))'exact target restore compare/exchange'
 O $r @('VmxEptHookRoundtripRoute(','VMX_EPT_VIOLATION_ROUTE_ARMED','InterlockedCompareExchange(','VMX_EPT_VIOLATION_ROUTE_HANDLING','event->GuestPhysicalAddress','event->EptPointer','VMX_EPT_VIOLATION_ROUTE_WRITE','InterlockedCompareExchange64(','owner->OriginalTargetEntry','owner->HookedTargetEntry','VmxEptInveptAsm(VMX_EPT_VIOLATION_ROUTE_INVEPT_SINGLE','VMX_EPT_VIOLATION_ROUTE_OBSERVED','VMX_V3D_EPT_ROUTE_RESUME') 'callback restore'
 $ra=$r.IndexOf('VmxEptViolationRoundtripRun(');A($ra-ge0)'diagnostic run';$rx=$r.Substring($ra)
 O $rx @('owner->ByteBefore=*((volatile UCHAR*)owner->Page)','VmxEptPersistentDeviceHookAdd(','ArmViolationRoute(context)','VmxPersistentDriverOwnerDiagnosticEptWrite(','&context->Owner,0,owner->Page','owner->ByteAfter=*((volatile UCHAR*)owner->Page)','VMX_EPT_VIOLATION_ROUTE_OBSERVED','VmxEptPersistentDeviceHookRemove(','RouteClear(owner)','ReleasePage(context)','HYPERVISOR_EPT_VIOLATION_STAGE_COMPLETE') 'one-shot order'
 A($rx-notmatch 'VmxPersistentDriverOwnerDiagnosticWrite\(')'generic copied diagnostic path forbidden for EPT trigger'
 A($rx-notmatch '\*\(\(volatile UCHAR\*\)owner->Page\)=0x5A')'root-mode write forbidden'
 $ia=$i.IndexOf('IoctlDispatchEptViolationRoundtrip(');$ib=$i.IndexOf('IoctlDispatchEptStatusQuery(');A($ia-ge0-and$ib-gt$ia)'IOCTL bounds';$ix=$i.Substring($ia,$ib-$ia)
 O $ix @('WdfRequestRetrieveInputBuffer(','RtlCopyMemory(&query,input_buffer,sizeof(query))','RtlZeroMemory(output_buffer,output_length)','IoctlValidateEptViolationRoundtripRequest(','STATUS_BUFFER_TOO_SMALL','VmxEptViolationRoundtripRun(','RtlCopyMemory(output_buffer,&response,sizeof(response))','information=sizeof(response)','WdfRequestCompleteWithInformation(') 'IOCTL exact response'
 A(([regex]::Matches($ix,'WdfRequestCompleteWithInformation\(')).Count-eq1)'one completion'
 O $l @('CmdEptViolationRoundtrip(','IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP','response.RouteState != HYPERVISOR_EPT_VIOLATION_ROUTE_OBSERVED','response.InveptOutcome != 1','response.ByteBefore != 0xA5','response.ByteAfter != 0x5A','response.AddInvalidationCalls != response.ProcessorCount','response.RemoveInvalidationCalls != response.ProcessorCount','SUCCESS: One-shot EPT violation VM-exit roundtrip verified.') 'loader verification'
 foreach($x in @('0x80002028','sizeof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE) == 128','INVALID_LENGTH','NONZERO_RESERVED')){A($at.Contains($x))"ABI test $x"}
 A(([regex]::Matches($tc,'test_vmx_ept_violation_roundtrip_abi COMMAND')).Count-eq1)'ABI test once'
 A(([regex]::Matches($tc,'test_vmx_ept_violation_roundtrip_contracts')).Count-eq1)'contract test once'
}
$h=Get-Content -Raw $Header;$v=Get-Content -Raw $Validator;$d=Get-Content -Raw $Dispatch;$dt=Get-Content -Raw $DispatchTest
$rs=Get-Content -Raw $RuntimeStorage;$ro=Get-Content -Raw $RuntimeOps;$rl=Get-Content -Raw $RuntimeLifecycle;$o=Get-Content -Raw $Owner
$st=Get-Content -Raw $Start;$r=Get-Content -Raw $Roundtrip;$i=Get-Content -Raw $Ioctl;$l=Get-Content -Raw $Loader;$at=Get-Content -Raw $AbiTest;$tc=Get-Content -Raw $TestsCMake
V $h $v $d $dt $rs $ro $rl $o $st $r $i $l $at $tc
$mut=@(
 @('d',$d.Replace('VMX_V3D_DISPATCH_EXIT_REASON_EPT_VIOLATION','UINT64_C(49)')),
 @('d',$d.Replace('return DispatchEptViolation(Context, io, qualification, rip);','return VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;')),
 @('ro',$ro.Replace('s->PersistentIo[i].EptViolation=route;','')),
 @('rl',$rl.Replace('RuntimeAcquire(r)','true')),
 @('o',$o.Replace('OwnerEnter(o)','VMX_PERSISTENT_STATUS_OK')),
 @('st',$st.Replace('VmxPersistentDriverOwnerBindEptRoute(','VmxPersistentDriverOwnerStart(')),
 @('r',$r.Replace('owner->ByteBefore!=0xA5','false')),
 @('r',$r.Replace('VmxEptInveptAsm(VMX_EPT_VIOLATION_ROUTE_INVEPT_SINGLE,&descriptor)','1')),
 @('r',$r.Replace('VmxPersistentDriverOwnerDiagnosticEptWrite(','VmxPersistentDriverOwnerStop(')),
 @('r',$r.Replace('VmxPersistentDriverOwnerDiagnosticEptWrite(','VmxPersistentDriverOwnerDiagnosticWrite(')),
 @('r',$r.Replace('&context->Owner,0,owner->Page','&context->Owner,0,&response')),
 @('r',$r.Replace('(LONG64)owner->OriginalTargetEntry,(LONG64)owner->HookedTargetEntry)','(LONG64)owner->HookedTargetEntry,(LONG64)owner->HookedTargetEntry)')),
 @('i',$i.Replace('VmxEptViolationRoundtripRun(','VMX_EPT_HOOK_ROUNDTRIP_OK; /*')),
 @('l',$l.Replace('response.InveptOutcome != 1','0'))
)
foreach($m in $mut){$bad=$false;$md=$d;$mro=$ro;$mrl=$rl;$mo=$o;$mst=$st;$mr=$r;$mi=$i;$ml=$l;switch($m[0]){'d'{$md=$m[1]}'ro'{$mro=$m[1]}'rl'{$mrl=$m[1]}'o'{$mo=$m[1]}'st'{$mst=$m[1]}'r'{$mr=$m[1]}'i'{$mi=$m[1]}'l'{$ml=$m[1]}};A($m[1]-ne($(switch($m[0]){'d'{$d}'ro'{$ro}'rl'{$rl}'o'{$o}'st'{$st}'r'{$r}'i'{$i}'l'{$l}})))"vacuous mutation $($m[0])";try{V $h $v $md $dt $rs $mro $mrl $mo $mst $mr $mi $ml $at $tc}catch{$bad=$true};A($bad)"accepted mutation $($m[0])"}
Write-Host 'PASS: 0x80A binds an authenticated resident EPT route, restores one trapped PTE with single-context INVEPT, resumes without RIP advance, and returns exact pointer-free evidence.'
