param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$ContextHeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a);A($i-ge0)"section $a";if(!$b){return $x.Substring($i)};$j=$x.IndexOf($b,$i+$a.Length);A($j-gt$i)"end $b";$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$n,[string]$m){$at=-1;foreach($v in $n){$i=$x.IndexOf($v,$at+1);A($i-gt$at)"$m $v";$at=$i}}
function V([string]$s,[string]$h,[string]$c,[string]$d,[string]$t){
 O $h @('#include <ntddk.h>','#include "vmx_persistent_device_context.h"') 'WDK include order'
 foreach($x in @('Private PASSIVE-only','externally serialized','clears the embedded EptDestroyReport','exactly one E5b8 call','rejects zero/invalid EptLifecycle','preserves prior report evidence','No caller-selected','report address','direct Persistent lifecycle call','IOCTL','unload','hook','public EPT','live VMX/INVEPT')){A($h.Contains($x))"header $x"}
 foreach($x in @('VmxEptPersistentDeviceLifecycleInitialize','VmxEptPersistentDeviceLifecycleDestroy')){A($h.Contains($x))"API $x"}
 O $c @('VMX_PERSISTENT_DRIVER_OWNER Owner;','VMX_EPT_PERSISTENT_LIFECYCLE_OWNER EptLifecycle;','VMX_EPT_PERSISTENT_LIFECYCLE_REPORT EptDestroyReport;') 'context layout'
 $z=B $s 'static int StorageZero(' 'VmxEptPersistentDeviceLifecycleInitialize('
 O $z @('storage == NULL','index < size','bytes[index] != 0','return 1') 'zero helper'
 $i=B $s 'VmxEptPersistentDeviceLifecycleInitialize(' 'VmxEptPersistentDeviceLifecycleDestroy('
 O $i @('KeGetCurrentIrql() != PASSIVE_LEVEL','context == NULL','StorageZero(&context->EptLifecycle','RtlSecureZeroMemory(&context->EptDestroyReport','sizeof(context->EptDestroyReport)','VmxEptPersistentLifecycleOwnerInitialize(','&context->Owner','&context->EptLifecycle','&context->EptDestroyReport') 'initialize'
 $x=B $s 'VmxEptPersistentDeviceLifecycleDestroy(' ''
 O $x @('KeGetCurrentIrql() != PASSIVE_LEVEL','context == NULL','VmxEptPersistentLifecycleOwnerValidate(&context->EptLifecycle)','VMX_EPT_PERSISTENT_LIFECYCLE_OK','RtlSecureZeroMemory(&context->EptDestroyReport','sizeof(context->EptDestroyReport)','VmxEptPersistentLifecycleOwnerDestroy(','&context->EptLifecycle','&context->EptDestroyReport') 'destroy'
 A(([regex]::Matches($s,'RtlSecureZeroMemory\(&context->EptDestroyReport')).Count-eq2)'two exact report clears'
 A(([regex]::Matches($s,'VmxEptPersistentLifecycleOwnerInitialize\(')).Count-eq1)'one E5b8 initialize'
 A(([regex]::Matches($s,'VmxEptPersistentLifecycleOwnerValidate\(')).Count-eq1)'one E5b8 validate'
 A(([regex]::Matches($s,'VmxEptPersistentLifecycleOwnerDestroy\(')).Count-eq1)'one E5b8 destroy'
 A($s-notmatch 'VmxPersistentDriverOwner(Attach|Start|Stop|Detach|AcquireStatusSnapshot|QueryPhase)|VmxPersistentRuntime|AllocatePool|FreePool|Wdf|IOCTL|Hook|__vmx|VMXON|VMXOFF|INVEPT')'forbidden execution'
 A((([regex]::Matches($d,'(?m)^\s{8}vmx_ept_persistent_device_lifecycle\.c\r?$')).Count-eq1))'driver source once'
 A((([regex]::Matches($t,'test_vmx_ept_persistent_device_lifecycle_contracts')).Count-eq1))'test once'
}
$s=[IO.File]::ReadAllText((Resolve-Path $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath));$c=[IO.File]::ReadAllText((Resolve-Path $ContextHeaderPath));$d=[IO.File]::ReadAllText((Resolve-Path $DriverCMakePath));$t=[IO.File]::ReadAllText((Resolve-Path $TestsCMakePath));V $s $h $c $d $t
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw $p)-notmatch 'VmxEptPersistentDeviceLifecycle|vmx_ept_persistent_device_lifecycle')"unwired $p"}
$m=@(
 $s.Replace('if (storage == NULL)','if (0)'),
 $s.Replace('!StorageZero(&context->EptLifecycle, sizeof(context->EptLifecycle))','0'),
 $s.Replace('RtlSecureZeroMemory(&context->EptDestroyReport,','RtlSecureZeroMemory(&context->EptLifecycle,'),
 $s.Replace('VmxEptPersistentLifecycleOwnerInitialize(','VMX_EPT_PERSISTENT_LIFECYCLE_OK; /*'),
 $s.Replace('VmxEptPersistentLifecycleOwnerValidate(&context->EptLifecycle)','VMX_EPT_PERSISTENT_LIFECYCLE_OK'),
 $s.Replace('VmxEptPersistentLifecycleOwnerDestroy(','VMX_EPT_PERSISTENT_LIFECYCLE_OK; /*'),
 $s.Replace('&context->Owner, &context->EptLifecycle,','&context->Owner, NULL,'),
 $s.Replace('&context->EptLifecycle, &context->EptDestroyReport','&context->EptLifecycle, NULL'),
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','KeGetCurrentIrql() == PASSIVE_LEVEL')
)
$i=0;foreach($q in $m){$i++;A($q-ne$s)"vacuous source mutation $i";$bad=$false;try{V $q $h $c $d $t}catch{$bad=$true};A($bad)"accepted source mutation $i"}
$hm=@($h.Replace('#include <ntddk.h>',''),$h.Replace('preserves prior report evidence','discards prior report evidence'),$h.Replace('VmxEptPersistentDeviceLifecycleDestroy','VmxEptPersistentDeviceLifecycleReset'))
$i=0;foreach($q in $hm){$i++;A($q-ne$h)"vacuous header mutation $i";$bad=$false;try{V $s $q $c $d $t}catch{$bad=$true};A($bad)"accepted header mutation $i"}
Write-Output 'Verified E5b9c embedded-storage adapter: PASSIVE gates, exact report clear/address, E5b8-only delegation, preflight evidence preservation, and private unwired scope.'
