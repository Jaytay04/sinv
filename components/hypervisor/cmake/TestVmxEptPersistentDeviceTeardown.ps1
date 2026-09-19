param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not $x){throw $m}}
function O($x,$tokens,$name){$at=0;foreach($v in $tokens){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);A($n-ge 0)"$name $v";$at=$n+$v.Length}}
function V($s,$h,$d,$t){
 foreach($x in @('Private PASSIVE-only','externally serialized','drains every transactional hook generation','COMPOSITION_GONE skips','BLOCKED state','never reaches Persistent Detach','zero hook/EPT ownership','DETACHED Runtime phase','pinned INVEPT')){A($h.Contains($x))"header $x"}
 $finish=$s.Substring($s.IndexOf('FinishDetached('),$s.IndexOf('VmxEptPersistentDeviceTeardown(')-$s.IndexOf('FinishDetached('))
 O $finish @('VmxPersistentDriverOwnerQueryPhase(','VMX_PERSISTENT_DRIVER_OWNER_DETACHED','!StorageZero(&context->EptLifecycle','!StorageZero(&context->EptHooks','!StorageZero(&context->EptHookRoundtrip','VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT','VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK') 'finish'
 $body=$s.Substring($s.IndexOf('VmxEptPersistentDeviceTeardown('))
 O $body @('KeGetCurrentIrql() != PASSIVE_LEVEL','context == NULL','VmxPersistentDriverOwnerQueryPhase(','StorageZero(&context->EptLifecycle','!StorageZero(&context->EptHooks','!StorageZero(&context->EptHookRoundtrip','VmxEptPersistentLifecycleOwnerValidate(','VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED','VMX_EPT_PERSISTENT_LIFECYCLE_READY','VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED','VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASE_PENDING','VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED','VmxEptPersistentDeviceHookDrain(context)','VmxEptHookRoundtripRelease(context)','VmxPersistentDriverOwnerStop(&context->Owner)','VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE','VmxEptPersistentDeviceLifecycleDestroy(context)','FinishDetached(context)') 'teardown'
 $ready=$body.Substring($body.IndexOf('if (stage == VMX_EPT_PERSISTENT_LIFECYCLE_READY'),$body.IndexOf('} else if (stage == VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE')-$body.IndexOf('if (stage == VMX_EPT_PERSISTENT_LIFECYCLE_READY'))
 O $ready @('VmxEptPersistentDeviceHookDrain(context)','VmxEptHookRoundtripRelease(context)','VmxPersistentDriverOwnerStop(&context->Owner)') 'drain before stop'
 A(([regex]::Matches($s,[regex]::Escape('VmxEptPersistentDeviceHookDrain('))).Count-eq 1)'one drain'
 A(([regex]::Matches($s,[regex]::Escape('VmxEptPersistentDeviceLifecycleDestroy('))).Count-eq 1)'one EPT destroy'
 A(([regex]::Matches($d,[regex]::Escape('vmx_ept_persistent_device_teardown.c'))).Count-eq 1)'driver source once'
 A(([regex]::Matches($t,'test_vmx_ept_persistent_device_teardown_contracts')).Count-eq 1)'test once'
}
$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath;$d=Get-Content -Raw $DriverCMakePath;$t=Get-Content -Raw $TestsCMakePath
V $s $h $d $t
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw $p)-notmatch 'VmxEptPersistentDeviceTeardown|vmx_ept_persistent_device_teardown')"unwired $p"}
$m=@(
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','KeGetCurrentIrql() == PASSIVE_LEVEL'),
 $s.Replace('VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED ||','VMX_EPT_PERSISTENT_LIFECYCLE_READY ||'),
 $s.Replace('VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASE_PENDING ||','VMX_EPT_PERSISTENT_LIFECYCLE_READY ||'),
 $s.Replace('VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED) {','VMX_EPT_PERSISTENT_LIFECYCLE_READY) {'),
 $s.Replace('VmxEptPersistentDeviceHookDrain(context)','VMX_EPT_PERSISTENT_DEVICE_HOOK_OK'),
 $s.Replace('VmxEptHookRoundtripRelease(context)','VMX_EPT_HOOK_ROUNDTRIP_OK'),
 $s.Replace('!StorageZero(&context->EptHookRoundtrip','StorageZero(&context->EptHookRoundtrip'),
 $s.Replace('VmxPersistentDriverOwnerStop(&context->Owner)','VMX_PERSISTENT_STATUS_OK'),
 $s.Replace('VmxEptPersistentDeviceLifecycleDestroy(context)','VMX_EPT_PERSISTENT_LIFECYCLE_OK')
)
foreach($q in $m){A($q-ne$s)'vacuous mutation';$bad=$false;try{V $q $h $d $t}catch{$bad=$true};A($bad)'accepted mutation'}
Write-Output 'PASS: teardown drains READY hooks, rejects hooks in partial stages, stops Runtime, releases EPT roots, and proves detached zero state.'
