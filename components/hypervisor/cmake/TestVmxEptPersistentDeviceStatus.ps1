param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function O([string]$x,[string[]]$n,[string]$m){$at=-1;foreach($v in $n){$i=$x.IndexOf($v,$at+1);A($i-gt$at)"$m $v";$at=$i}}
function V([string]$s,[string]$h,[string]$d,[string]$t){
 foreach($x in @('Private PASSIVE-only','externally serialized','authenticates','pointer-free evidence',
  'classification to E5b9e1','Output must be zero','disjoint from the complete device context',
  'No allocation','lifecycle mutation','IOCTL','unload','hook','public EPT','INVEPT wiring')){A($h.Contains($x))"header $x"}
 foreach($x in @('VMX_EPT_PERSISTENT_DEVICE_STATUS_OK','VMX_EPT_PERSISTENT_DEVICE_STATUS_INVALID',
  'VMX_EPT_PERSISTENT_DEVICE_STATUS_WRONG_IRQL','VMX_EPT_PERSISTENT_DEVICE_STATUS_MODEL_FAILED',
  'VmxEptPersistentDeviceStatusProject')){A($h.Contains($x))"API $x"}
 foreach($x in @('VMX_EPT_PERSISTENT_STATUS_PHASE_DETACHED','VMX_PERSISTENT_DRIVER_OWNER_DETACHED',
  'VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED','VMX_PERSISTENT_DRIVER_OWNER_ATTACHED',
  'VMX_EPT_PERSISTENT_STATUS_PHASE_TEARDOWN_PENDING','VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING',
  'VMX_EPT_PERSISTENT_STATUS_STAGE_READY','VMX_EPT_PERSISTENT_LIFECYCLE_READY',
  'VMX_EPT_PERSISTENT_STATUS_STAGE_SESSION_ONLY','VMX_EPT_PERSISTENT_LIFECYCLE_SESSION_ONLY',
  'VMX_EPT_PERSISTENT_STATUS_STAGE_COMPOSITION_GONE','VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE',
  'VMX_EPT_PERSISTENT_STATUS_STAGE_BLOCKED','VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED')){A($s.Contains($x))"pin $x"}
 $body=$s.Substring($s.IndexOf('VmxEptPersistentDeviceStatusProject('))
 O $body @('KeGetCurrentIrql() != PASSIVE_LEVEL','!context || !output','!Separate(context, sizeof(*context), output, sizeof(*output))',
  'view.Version = VMX_EPT_PERSISTENT_STATUS_MODEL_VERSION','VmxPersistentDriverOwnerQueryPhase(',
  'view.EptZero = StorageZero(&context->EptLifecycle','if (!view.EptZero)',
  'VmxEptPersistentLifecycleOwnerValidate(','view.EptStage = context->EptLifecycle.Stage',
  'view.StartEvidenceValid','context->EptStartReport.Reserved','CopyStart(',
  'view.DestroyEvidenceValid','CopyDestroy(','VmxEptPersistentStatusModelProject(&view, output)',
  'VMX_EPT_PERSISTENT_DEVICE_STATUS_OK') 'projection'
 A(([regex]::Matches($s,'VmxPersistentDriverOwnerQueryPhase\(')).Count-eq 1)'one phase query'
 A(([regex]::Matches($s,'VmxEptPersistentLifecycleOwnerValidate\(')).Count-eq 1)'one EPT validate'
 A(([regex]::Matches($s,'VmxEptPersistentStatusModelProject\(')).Count-eq 1)'one model call'
 A($s-notmatch 'AllocatePool|FreePool|VmxEptPersistent(DeviceStart|DeviceTeardown|LifecycleOwnerInitialize|LifecycleOwnerDestroy)|VmxPersistentDriverOwner(Attach|Start|Stop|Detach)|Wdf|IOCTL|Hook|__vmx|VMXON|VMXOFF|INVEPT')'forbidden execution'
 A((([regex]::Matches($d,'(?m)^\s{8}vmx_ept_persistent_device_status\.c\r?$')).Count-eq 1))'driver source once'
 A((([regex]::Matches($t,'test_vmx_ept_persistent_device_status_contracts')).Count-eq 1))'test once'
}
$s=[IO.File]::ReadAllText((Resolve-Path $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath))
$d=[IO.File]::ReadAllText((Resolve-Path $DriverCMakePath));$t=[IO.File]::ReadAllText((Resolve-Path $TestsCMakePath));V $s $h $d $t
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw $p)-notmatch 'VmxEptPersistentDeviceStatusProject|vmx_ept_persistent_device_status')"unwired $p"}
$m=@(
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','KeGetCurrentIrql() == PASSIVE_LEVEL'),
 $s.Replace('!Separate(context, sizeof(*context), output, sizeof(*output))','0'),
 $s.Replace('VmxPersistentDriverOwnerQueryPhase(','VmxPersistentDriverOwnerQueryOccupied('),
 $s.Replace('VmxEptPersistentLifecycleOwnerValidate(','VMX_EPT_PERSISTENT_LIFECYCLE_OK; /*'),
 $s.Replace('CopyStart(&context->EptStartReport, &view.Start);',''),
 $s.Replace('CopyDestroy(&context->EptDestroyReport, &view.Destroy);',''),
 $s.Replace('VmxEptPersistentStatusModelProject(&view, output)','1'),
 $s.Replace('VMX_EPT_PERSISTENT_STATUS_STAGE_BLOCKED ==','VMX_EPT_PERSISTENT_STATUS_STAGE_READY ==')
)
$i=0;foreach($q in $m){$i++;A($q-ne$s)"vacuous mutation $i";$bad=$false;try{V $q $h $d $t}catch{$bad=$true};A($bad)"accepted mutation $i"}
Write-Output 'Verified E5b9e2 device status projection: PASSIVE/disjoint preflight, phase/EPT authentication, pointer-free evidence copies, E5b9e1 classification, numeric pins, and private scope; the authorized 0x808 bridge is verified separately.'
