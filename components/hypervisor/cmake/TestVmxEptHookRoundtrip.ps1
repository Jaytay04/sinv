param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$ContextHeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverPath,
 [Parameter(Mandatory=$true)][string]$TeardownPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not $x){throw $m}}
function O($x,$tokens,$name){$at=0;foreach($v in $tokens){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);A($n-ge 0)"$name $v";$at=$n+$v.Length}}
function V($s,$h,$c,$d,$z,$dc,$tc){
 foreach($x in @('PASSIVE-only','externally serialized','initialized before Add','never dereferenced between','retains the page and hook state','Release is legal only after no hook remains')){A($h.Contains($x))"header $x"}
 O $h @('VMX_EPT_HOOK_ROUNDTRIP_OWNER','void* Page;','const void* PageContract;','uint64_t GuestPhysicalPage,GuestPhysicalPageComplement;','VmxEptHookRoundtripRun(','VmxEptHookRoundtripRelease(') 'header owner/API'
 $route=$s.Substring($s.IndexOf('static int RouteZero'),$s.IndexOf('static void RouteClear')-$s.IndexOf('static int RouteZero'))
 O $route @('FIELD_OFFSET(VMX_EPT_HOOK_ROUNDTRIP_OWNER,ByteBefore)','FIELD_OFFSET(VMX_EPT_HOOK_ROUNDTRIP_OWNER,RouteState)','Zero(first,bytes)','owner->ByteAfter==0','owner->Reserved16==0','owner->Reserved32==0') 'pre-arm route zero'
 $valid=$s.Substring($s.IndexOf('static int OwnerCoreValid'),$s.IndexOf('static int NoHook')-$s.IndexOf('static int OwnerCoreValid'))
 O $valid @('VmxKernelPersistentAllocator()','owner->Magic==VMX_EPT_HOOK_ROUNDTRIP_MAGIC','owner->Version==VMX_EPT_HOOK_ROUNDTRIP_VERSION','owner->BaseReserved==0','owner->Page==owner->PageContract','owner->GuestPhysicalPageComplement==~owner->GuestPhysicalPage','GetPhysicalAddress(','owner->GuestPhysicalPage','RouteZero(owner)') 'authenticated owner'
 $release=$s.Substring($s.IndexOf('static VMX_EPT_HOOK_ROUNDTRIP_STATUS ReleasePage'),$s.IndexOf('VMX_EPT_HOOK_ROUNDTRIP_STATUS VmxEptHookRoundtripRun')-$s.IndexOf('static VMX_EPT_HOOK_ROUNDTRIP_STATUS ReleasePage'))
 O $release @('Zero(owner,sizeof(*owner))','OwnerCoreValid(owner)','NoHook(context,owner->GuestPhysicalPage)','RtlSecureZeroMemory(page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES)','allocator->FreeContiguous(','RtlSecureZeroMemory(owner,sizeof(*owner))') 'release after no hook'
 $run=$s.Substring($s.IndexOf('VMX_EPT_HOOK_ROUNDTRIP_STATUS VmxEptHookRoundtripRun'),$s.IndexOf('VMX_EPT_HOOK_ROUNDTRIP_STATUS VmxEptHookRoundtripRelease')-$s.IndexOf('VMX_EPT_HOOK_ROUNDTRIP_STATUS VmxEptHookRoundtripRun'))
 O $run @('KeGetCurrentIrql()!=PASSIVE_LEVEL','Zero(out,sizeof(*out))','Separate(context,sizeof(*context),out,sizeof(*out))','VmxEptPersistentLifecycleOwnerValidate(','VMX_EPT_PERSISTENT_LIFECYCLE_READY','PhysicalAddressBits','AllocateContiguous(','Separate(context,sizeof(*context),page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES)','Separate(out,sizeof(*out),page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES)','RtlFillMemory(page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES,0xA5)','GetPhysicalAddress(','owner->Page=page','KeMemoryBarrier()','VmxEptPersistentDeviceHookAdd(','Capture(&response,&context->EptHookReport,status,1)','HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_HOOK_RETAINED','VmxEptPersistentDeviceHookRemove(','Capture(&response,&context->EptHookReport,status,0)','HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_PAGE_RETAINED','ReleasePage(context)','HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_COMPLETE','*out=response') 'safe roundtrip order'
 $a=$run.IndexOf('VmxEptPersistentDeviceHookAdd(');$r=$run.IndexOf('VmxEptPersistentDeviceHookRemove(');A($a-ge 0-and$r-gt$a)'add/remove bounds'
 $between=$run.Substring($a,$r-$a)
 A($between-notmatch 'Rtl(Fill|SecureZero)Memory\((page|owner->Page)')'diagnostic page accessed while hooked'
 foreach($x in @('#include "vmx_ept_hook_roundtrip.h"','VMX_EPT_HOOK_ROUNDTRIP_OWNER EptHookRoundtrip;')){A($c.Contains($x))"context $x"}
 A($d.Contains('RtlZeroMemory(&persistent_context->EptHookRoundtrip,'))'owner zero before publication'
 O $z @('VmxEptPersistentDeviceHookDrain(context)','VmxEptHookRoundtripRelease(context)','VmxPersistentDriverOwnerStop(&context->Owner)') 'drain release stop'
 A(([regex]::Matches($dc,[regex]::Escape('vmx_ept_hook_roundtrip.c'))).Count-eq 1)'source once'
 A(([regex]::Matches($tc,'test_vmx_ept_hook_roundtrip_contracts')).Count-eq 1)'test once'
}
$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath;$c=Get-Content -Raw $ContextHeaderPath
$d=Get-Content -Raw $DriverPath;$z=Get-Content -Raw $TeardownPath
$dc=Get-Content -Raw $DriverCMakePath;$tc=Get-Content -Raw $TestsCMakePath
V $s $h $c $d $z $dc $tc
$sm=@(
 $s.Replace('owner->Magic==VMX_EPT_HOOK_ROUNDTRIP_MAGIC&&','1&&'),
 $s.Replace('FIELD_OFFSET(VMX_EPT_HOOK_ROUNDTRIP_OWNER,ByteBefore)','sizeof(*owner)'),
 $s.Replace('return OwnerCoreValid(owner)&&RouteZero(owner);','return OwnerCoreValid(owner);'),
 $s.Replace('NoHook(context,owner->GuestPhysicalPage)','1'),
 $s.Replace('RtlFillMemory(page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES,0xA5);',''),
 $s.Replace('status=VmxEptPersistentDeviceHookAdd(context,owner->GuestPhysicalPage,HOOK_PERM_WRITE);','status=VMX_EPT_PERSISTENT_DEVICE_HOOK_OK;'),
 $s.Replace('status=VmxEptPersistentDeviceHookRemove(context,owner->GuestPhysicalPage);','status=VMX_EPT_PERSISTENT_DEVICE_HOOK_OK;'),
 $s.Replace('if(ReleasePage(context)!=VMX_EPT_HOOK_ROUNDTRIP_OK){','if(VMX_EPT_HOOK_ROUNDTRIP_OK!=VMX_EPT_HOOK_ROUNDTRIP_OK){')
)
foreach($m in $sm){A($m-ne$s)'vacuous source mutation';$bad=$false;try{V $m $h $c $d $z $dc $tc}catch{$bad=$true};A($bad)'accepted source mutation'}
$zm=$z.Replace('VmxEptHookRoundtripRelease(context)','VMX_EPT_HOOK_ROUNDTRIP_OK')
A($zm-ne$z)'vacuous teardown mutation';$bad=$false;try{V $s $h $c $d $zm $dc $tc}catch{$bad=$true};A($bad)'accepted teardown mutation'
Write-Output 'PASS: private no-access hook roundtrip retains authenticated state, removes before page access/free, and drains before Runtime Stop.'
