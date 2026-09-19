param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$ContextHeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$x,[string]$a,[string]$b){
 $i=$x.IndexOf($a);A($i-ge 0)"section $a"
 if(!$b){return $x.Substring($i)}
 $j=$x.IndexOf($b,$i+$a.Length);A($j-gt$i)"end $b"
 return $x.Substring($i,$j-$i)
}
function O([string]$x,[string[]]$n,[string]$m){
 $at=-1;foreach($v in $n){$i=$x.IndexOf($v,$at+1);A($i-gt$at)"$m $v";$at=$i}
}
function V([string]$s,[string]$h,[string]$c,[string]$d,[string]$t){
 foreach($x in @('Private PASSIVE-only','externally serialized','Config disjoint from device storage','authenticated',
  'captures one stable System-process CR3 target','authenticated EPT-route Bind -> authenticated CR3-route Bind -> diagnosed','exactly one E5b9c Initialize','exactly one E5b9d1 rollback',
  'embedded','EptStartReport','distinct Attach/Start/EPT/rollback evidence',
  'copy of E5b8 initialization evidence','ATTACHED Runtime plus READY EPT storage',
  'No direct Stop/Detach','allocation','caller-selected report','IOCTL','unload',
  'hook','public EPT','INVEPT wiring')){A($h.Contains($x))"header $x"}
 foreach($x in @('VMX_EPT_PERSISTENT_DEVICE_START_OK',
  'VMX_EPT_PERSISTENT_DEVICE_START_INVALID','VMX_EPT_PERSISTENT_DEVICE_START_WRONG_IRQL',
  'VMX_EPT_PERSISTENT_DEVICE_START_PHASE_FAILED','VMX_EPT_PERSISTENT_DEVICE_START_ATTACH_FAILED',
  'VMX_EPT_PERSISTENT_DEVICE_START_RUNTIME_FAILED','VMX_EPT_PERSISTENT_DEVICE_START_EPT_FAILED',
  'VMX_EPT_PERSISTENT_DEVICE_START_ROLLBACK_FAILED','VMX_EPT_PERSISTENT_DEVICE_START_BLOCKED',
  'VMX_EPT_PERSISTENT_DEVICE_START_INVARIANT','VMX_EPT_PERSISTENT_DEVICE_START_BIND_FAILED','VMX_EPT_PERSISTENT_DEVICE_START_CR3_CAPTURE_FAILED','VMX_EPT_PERSISTENT_DEVICE_START_CR3_BIND_FAILED','VmxEptPersistentDeviceStart')){A($h.Contains($x))"API $x"}
 foreach($x in @('PhaseBeforeStatus, AttachStatus, BindStatus, StartStatus',
  'EptInitializeStatus, RollbackStatus, PhaseAfterStatus',
  'PhaseBeforeCalls, AttachCalls, BindCalls, StartCalls',
  'EptInitializeCalls, RollbackCalls, PhaseAfterCalls',
  'RuntimePhaseBefore, RuntimePhaseAfter, EptStageAfter, Reserved',
  'VMX_PERSISTENT_START_DIAGNOSTIC StartDiagnostic',
  'VMX_EPT_PERSISTENT_LIFECYCLE_REPORT EptInitialize',
  'int32_t Cr3CaptureStatus, Cr3BindStatus','uint32_t Cr3CaptureCalls, Cr3BindCalls',
  'VMX_CR3_TARGET_SET Cr3Target',
  'VMX_EPT_PERSISTENT_DEVICE_START_REPORT EptStartReport')){A($c.Contains($x))"context evidence $x"}
 $rollback=B $s 'Rollback(' 'VmxEptPersistentDeviceStart('
 O $rollback @('++report->RollbackCalls','VmxEptPersistentDeviceTeardown(context)',
  'report->RollbackStatus','QueryAfter(context)','VMX_PERSISTENT_DRIVER_OWNER_DETACHED',
  '!StorageZero(&context->EptLifecycle','VMX_EPT_PERSISTENT_DEVICE_START_ROLLBACK_FAILED',
  'return original_status') 'rollback'
 $body=B $s 'VmxEptPersistentDeviceStart(' ''
 O $body @('KeGetCurrentIrql() != PASSIVE_LEVEL','context == NULL || config == NULL','!Separate(context, sizeof(*context), config, sizeof(*config))',
  'VmxPersistentDriverOwnerQueryPhase(','VMX_PERSISTENT_DRIVER_OWNER_DETACHED',
  '!StorageZero(&context->EptLifecycle','RtlSecureZeroMemory(report, sizeof(*report))',
  'report->PhaseBeforeCalls = 1','report->Cr3CaptureCalls = 1',
  'VmxPersistentSystemCr3TargetCapture(&report->Cr3Target)','report->Cr3CaptureStatus',
  'VMX_EPT_PERSISTENT_DEVICE_START_CR3_CAPTURE_FAILED','report->AttachCalls = 1',
  'VmxPersistentDriverOwnerAttach(','report->AttachStatus',
  'QueryAfter(context)','VMX_EPT_PERSISTENT_DEVICE_START_BLOCKED',
  'VMX_EPT_PERSISTENT_DEVICE_START_ATTACH_FAILED','report->BindCalls = 1','VmxPersistentDriverOwnerBindEptRoute(','VmxEptHookRoundtripRoute','report->BindStatus','VMX_EPT_PERSISTENT_DEVICE_START_BIND_FAILED','report->Cr3BindCalls = 1',
  'VmxPersistentDriverOwnerBindCr3Route(','&report->Cr3Target','report->Cr3BindStatus',
  'VMX_EPT_PERSISTENT_DEVICE_START_CR3_BIND_FAILED','report->StartCalls = 1',
  'VmxPersistentDriverOwnerStartDiagnosed(','report->StartStatus',
  'Rollback(context,','VMX_EPT_PERSISTENT_DEVICE_START_RUNTIME_FAILED',
  'report->EptInitializeCalls = 1','VmxEptPersistentDeviceLifecycleInitialize(context)',
  'report->EptInitializeStatus','RtlCopyMemory(&report->EptInitialize',
  '&context->EptDestroyReport','VmxEptPersistentLifecycleOwnerValidate(',
  'report->EptStageAfter','VMX_EPT_PERSISTENT_DEVICE_START_EPT_FAILED',
  'VMX_EPT_PERSISTENT_LIFECYCLE_READY','QueryAfter(context)',
  'VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VMX_EPT_PERSISTENT_DEVICE_START_OK') 'start'
 A(([regex]::Matches($s,'VmxPersistentDriverOwnerAttach\(')).Count-eq 1)'one Attach'
 A(([regex]::Matches($s,'VmxPersistentDriverOwnerBindEptRoute\(')).Count-eq 1)'one EPT route bind'
 A(([regex]::Matches($s,'VmxPersistentSystemCr3TargetCapture\(')).Count-eq 1)'one CR3 capture'
 A(([regex]::Matches($s,'VmxPersistentDriverOwnerBindCr3Route\(')).Count-eq 1)'one CR3 route bind'
 A(([regex]::Matches($s,'VmxPersistentDriverOwnerStartDiagnosed\(')).Count-eq 1)'one diagnosed Start'
 A(([regex]::Matches($s,'VmxEptPersistentDeviceLifecycleInitialize\(')).Count-eq 1)'one E5b9c Initialize'
 A(([regex]::Matches($s,'VmxEptPersistentDeviceTeardown\(')).Count-eq 1)'one E5b9d1 rollback site'
 A(([regex]::Matches($s,'VmxEptPersistentLifecycleOwnerValidate\(')).Count-eq 1)'one E5b8 validate'
 A($s-notmatch 'VmxPersistentDriverOwner(Stop|Detach)\s*\(|AllocatePool|FreePool|Wdf|IOCTL|__vmx|VMXON|VMXOFF|INVEPT')'forbidden execution'
 A((([regex]::Matches($d,'(?m)^\s{8}vmx_ept_persistent_device_start\.c\r?$')).Count-eq 1))'driver source once'
 A((([regex]::Matches($t,'test_vmx_ept_persistent_device_start_contracts')).Count-eq 1))'test once'
}
$s=[IO.File]::ReadAllText((Resolve-Path $SourcePath))
$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath))
$c=[IO.File]::ReadAllText((Resolve-Path $ContextHeaderPath))
$d=[IO.File]::ReadAllText((Resolve-Path $DriverCMakePath))
$t=[IO.File]::ReadAllText((Resolve-Path $TestsCMakePath))
V $s $h $c $d $t
foreach($p in $ForbiddenPaths.Split(',')){
 A((Get-Content -Raw $p)-notmatch 'VmxEptPersistentDeviceStart|vmx_ept_persistent_device_start')"unwired $p"
}
$m=@(
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','KeGetCurrentIrql() == PASSIVE_LEVEL'),
 $s.Replace('!Separate(context, sizeof(*context), config, sizeof(*config))','0'),
 $s.Replace('VmxPersistentDriverOwnerQueryPhase(','VmxPersistentDriverOwnerQueryOccupied('),
 $s.Replace('VmxPersistentDriverOwnerAttach(','VmxPersistentDriverOwnerStart('),
 $s.Replace('VmxPersistentDriverOwnerBindEptRoute(','VmxPersistentDriverOwnerStart('),
 $s.Replace('VmxPersistentSystemCr3TargetCapture(&report->Cr3Target)','VMX_CR3_TARGET_OK'),
 $s.Replace('VmxPersistentDriverOwnerBindCr3Route(','VmxPersistentDriverOwnerStart('),
 $s.Replace('VmxPersistentDriverOwnerStartDiagnosed(','VmxPersistentDriverOwnerStart('),
 $s.Replace('VmxEptPersistentDeviceLifecycleInitialize(context)','VMX_EPT_PERSISTENT_LIFECYCLE_OK'),
 $s.Replace('VmxEptPersistentDeviceTeardown(context)','VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK'),
 $s.Replace('RtlCopyMemory(&report->EptInitialize,','RtlZeroMemory(&report->EptInitialize,'),
 $s.Replace('VmxEptPersistentLifecycleOwnerValidate(','VMX_EPT_PERSISTENT_LIFECYCLE_OK; /*'),
 $s.Replace('!StorageZero(&context->EptLifecycle,','StorageZero(&context->EptLifecycle,'),
 $s.Replace('RtlSecureZeroMemory(report, sizeof(*report));',''),
 $s.Replace('VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VMX_PERSISTENT_DRIVER_OWNER_DETACHED')
)
$i=0;foreach($q in $m){$i++;A($q-ne$s)"vacuous source mutation $i";$bad=$false;try{V $q $h $c $d $t}catch{$bad=$true};A($bad)"accepted source mutation $i"}
$hm=@(
 $h.Replace('exactly one E5b9d1 rollback','optional rollback'),
 $h.Replace('VmxEptPersistentDeviceStart','VmxEptPersistentDeviceBegin')
)
$i=0;foreach($q in $hm){$i++;A($q-ne$h)"vacuous header mutation $i";$bad=$false;try{V $s $q $c $d $t}catch{$bad=$true};A($bad)"accepted header mutation $i"}
$cm=@(
 $c.Replace('VMX_PERSISTENT_START_DIAGNOSTIC StartDiagnostic;','void* StartDiagnostic;'),
 $c.Replace('VMX_EPT_PERSISTENT_LIFECYCLE_REPORT EptInitialize;','void* EptInitialize;'),
 $c.Replace('VMX_CR3_TARGET_SET Cr3Target;','void* Cr3Target;'),
 $c.Replace('    VMX_EPT_PERSISTENT_DEVICE_START_REPORT EptStartReport;','')
)
$i=0;foreach($q in $cm){$i++;A($q-ne$c)"vacuous context mutation $i";$bad=$false;try{V $s $h $q $d $t}catch{$bad=$true};A($bad)"accepted context mutation $i"}
Write-Output 'Verified startup routing: PASSIVE detached/zero preflight, stable System CR3 capture, Attach, EPT and CR3 bind, diagnosed Start, EPT initialization, copied evidence, rollback, and final READY/ATTACHED proof.'
