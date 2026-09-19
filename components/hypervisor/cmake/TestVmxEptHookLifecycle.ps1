param(
 [string]$LifecycleSource,[string]$LifecycleHeader,
 [string]$DeviceSource,[string]$DeviceHeader,
 [string]$ContextHeader,[string]$TeardownSource,
 [string]$DriverCMake,[string]$TestsCMake)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not $x){throw $m}}
function O($x,$tokens,$name){$at=0;foreach($v in $tokens){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);A($n-ge 0)"$name $v";$at=$n+$v.Length}}
function V($l,$h,$d,$dh,$c,$z,$dc,$tc){
 foreach($x in @('PASSIVE-only','externally serialized','Every mutation is followed by exact','generation coverage','pin -> DISPATCH -> E2 invalidation','Failures retain PREPARED or','ROLLBACK_PENDING')){A($h.Contains($x))"lifecycle header $x"}
 $pin=$l.Substring($l.IndexOf('static int32_t InvalidatePinned'),$l.IndexOf('static VMX_EPT_HOOK_LIFECYCLE_STATUS DriveInvalidation')-$l.IndexOf('static int32_t InvalidatePinned'))
 O $pin @('KeGetCurrentIrql() != PASSIVE_LEVEL','KeRaiseIrql(DISPATCH_LEVEL','VmxEptInstallationOwnerCoordinateInvalidation(','KeLowerIrql(previous_irql)','return context->InvalidationStatus') 'pinned'
 $drive=$l.Substring($l.IndexOf('static VMX_EPT_HOOK_LIFECYCLE_STATUS DriveInvalidation'),$l.IndexOf('static int ArgumentsValid')-$l.IndexOf('static VMX_EPT_HOOK_LIFECYCLE_STATUS DriveInvalidation'))
 O $drive @('coverage->RequiredGeneration == 0','coverage->AcknowledgedGeneration ==','MutationGeneration','VmxPersistentKernelExecutePinned(','&KernelPersistentPlatform','&installation->Records[index].Identity','InvalidatePinned','status != VMX_PERSISTENT_KERNEL_OK','context.InvalidationStatus != VMX_EPT_KERNEL_INVALIDATION_OK','FirstFailureIndex = index','InvalidationSuccesses','PendingGeneration != 0','InvalidatedGeneration !=','MutationGeneration') 'drive'
 $add=$l.Substring($l.IndexOf('VmxEptHookLifecycleAdd('),$l.IndexOf('VmxEptHookLifecycleRemove(')-$l.IndexOf('VmxEptHookLifecycleAdd('))
 O $add @('KeGetCurrentIrql() != PASSIVE_LEVEL','ArgumentsValid(','trap_permissions == 0','~VMX_EPT_HOOK_PERMISSION_MASK','VmxEptHookOwnerQuery(','VMX_EPT_HOOK_OWNER_NOT_FOUND','VmxEptHookOwnerPrepare(','VMX_EPT_HOOK_STAGE_PREPARED','FinishPrepared(','*out_report = report') 'add'
 $remove=$l.Substring($l.IndexOf('VmxEptHookLifecycleRemove('),$l.IndexOf('VmxEptHookLifecycleDrain(')-$l.IndexOf('VmxEptHookLifecycleRemove('))
 O $remove @('VmxEptHookOwnerQuery(','VMX_EPT_HOOK_STAGE_PREPARED','FinishPrepared(','VMX_EPT_HOOK_STAGE_COMMITTED','VmxEptHookOwnerBeginUnhook(','VMX_EPT_HOOK_STAGE_ROLLBACK_PENDING','DriveInvalidation(','VmxEptHookOwnerCompleteUnhook(','*out_report = report') 'remove'
 $drain=$l.Substring($l.IndexOf('VmxEptHookLifecycleDrain('))
 O $drain @('VmxEptHookOwnerQueryFirst(','VMX_EPT_HOOK_OWNER_NOT_FOUND','VmxEptHookLifecycleRemove(','VmxEptHookOwnerDestroy(','*out_report = report') 'drain'
 foreach($x in @('sequential KMDF queue','initialized lazily','Drain completes every','before Runtime Stop')){A($dh.Contains($x))"device header $x"}
 O $d @('static int Ready(','VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VMX_EPT_PERSISTENT_LIFECYCLE_READY','VMX_EPT_COMPOSITION_OWNER_READY',
  'static VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS EnsureOwner','VMX_EPT_PERSISTENT_HOOK_CAPACITY','&session->Allocator','VmxEptHookLifecycleAdd(','VmxEptHookLifecycleRemove(','VmxEptHookLifecycleDrain(') 'device facade'
 foreach($x in @('VMX_EPT_HOOK_OWNER EptHooks;','VMX_EPT_HOOK_LIFECYCLE_REPORT EptHookReport;')){A($c.Contains($x))"context $x"}
 O $z @('VMX_EPT_PERSISTENT_LIFECYCLE_READY','VmxEptPersistentDeviceHookDrain(context)','VMX_EPT_PERSISTENT_DEVICE_HOOK_OK','VmxPersistentDriverOwnerStop(&context->Owner)') 'teardown drain-before-stop'
 A($z.Contains('!StorageZero(&context->EptHooks'))'teardown zero gate'
 A(([regex]::Matches($dc,[regex]::Escape('vmx_ept_hook_lifecycle.c'))).Count-eq 1)'lifecycle source once'
 A(([regex]::Matches($dc,[regex]::Escape('vmx_ept_persistent_device_hooks.c'))).Count-eq 1)'device source once'
 A(([regex]::Matches($tc,'test_vmx_ept_hook_lifecycle_contracts')).Count-eq 1)'test once'
}
$l=Get-Content -Raw $LifecycleSource;$h=Get-Content -Raw $LifecycleHeader
$d=Get-Content -Raw $DeviceSource;$dh=Get-Content -Raw $DeviceHeader
$c=Get-Content -Raw $ContextHeader;$z=Get-Content -Raw $TeardownSource
$dc=Get-Content -Raw $DriverCMake;$tc=Get-Content -Raw $TestsCMake
V $l $h $d $dh $c $z $dc $tc
$lm=@(
 $l.Replace('KeRaiseIrql(DISPATCH_LEVEL, &previous_irql);',''),
 $l.Replace('VmxEptInstallationOwnerCoordinateInvalidation(','VMX_EPT_KERNEL_INVALIDATION_OK; /*'),
 $l.Replace('coverage->AcknowledgedGeneration ==','0 =='),
 $l.Replace('VmxPersistentKernelExecutePinned(','VMX_PERSISTENT_KERNEL_OK; /*'),
 $l.Replace('VmxEptHookOwnerPrepare(','VMX_EPT_HOOK_OWNER_OK; /*'),
 $l.Replace('FinishPrepared(hooks, installation, hook_id, &report)','VMX_EPT_HOOK_LIFECYCLE_OK'),
 $l.Replace('VmxEptHookOwnerBeginUnhook(','VMX_EPT_HOOK_OWNER_OK; /*'),
 $l.Replace('VmxEptHookOwnerCompleteUnhook(','VMX_EPT_HOOK_OWNER_OK; /*'),
 $l.Replace('VmxEptHookOwnerDestroy(hooks)','VMX_EPT_HOOK_OWNER_OK')
)
foreach($m in $lm){A($m-ne$l)'vacuous lifecycle mutation';$bad=$false;try{V $m $h $d $dh $c $z $dc $tc}catch{$bad=$true};A($bad)'accepted lifecycle mutation'}
$dm=@(
 $d.Replace('VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VMX_PERSISTENT_DRIVER_OWNER_DETACHED'),
 $d.Replace('&session->Allocator','NULL'),
 $d.Replace('VmxEptHookLifecycleAdd(','VmxEptHookLifecycleRemove('),
 $d.Replace('VmxEptHookLifecycleDrain(','VmxEptHookLifecycleRemove(')
)
foreach($m in $dm){A($m-ne$d)'vacuous device mutation';$bad=$false;try{V $l $h $m $dh $c $z $dc $tc}catch{$bad=$true};A($bad)'accepted device mutation'}
$zm=$z.Replace('VmxEptPersistentDeviceHookDrain(context)','VMX_EPT_PERSISTENT_DEVICE_HOOK_OK')
A($zm-ne$z)'vacuous teardown mutation';$bad=$false;try{V $l $h $d $dh $c $zm $dc $tc}catch{$bad=$true};A($bad)'accepted teardown mutation'
Write-Host 'PASS: transactional hook lifecycle, pinned all-CPU invalidation, device ownership, and drain-before-stop contracts'
