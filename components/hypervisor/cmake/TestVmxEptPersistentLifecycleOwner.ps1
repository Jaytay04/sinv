param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$CompositionSourcePath,
 [Parameter(Mandatory=$true)][string]$CompositionHeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths
)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);A($i-ge 0)"section $a";if([string]::IsNullOrEmpty($b)){return $x.Substring($i)};$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);A($j-gt$i)"end $b";$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$tokens,[string]$name){$at=0;foreach($token in $tokens){$i=$x.IndexOf($token,$at,[StringComparison]::Ordinal);A($i-ge 0)"$name $token";$at=$i+$token.Length}}
function V([string]$s,[string]$h,[string]$cs,[string]$ch,[string]$d,[string]$t){
 foreach($x in @('PASSIVE-only','externally serialized lifetime owner',
  'adopts every authenticated active Persistent root',
  'activates and verifies EPTP plus both execution-control bits',
  'marks exact invalidation coverage','initial all-processor','before READY',
  'drains conservative invalidation coverage before startup rollback','Stop-then-cleanup','release every root only after','release stages','BLOCKED',
  'forbids Detach')){A($h.Contains($x))"header $x"}
 foreach($x in @('VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED',
  'VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASE_PENDING',
  'VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED',
  'VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED_STATUS',
  'VMX_EPT_PERSISTENT_LIFECYCLE_ROOT_CLEANUP_FAILED',
  'AdoptionCalls','CoverageCalls','ActivationCalls','ActivationSuccesses',
  'ActivationFailureIndex','VMX_EPT_INSTALLATION_REPORT Activation',
  'InitialInvalidationCalls','RootRollbackCalls','RootReleaseCalls',
  'VMX_EPT_RUNTIME_ADAPTER_REPORT Adoption')){A($h.Contains($x))"ABI $x"}
 A($ch.Contains('VmxEptCompositionOwnerValidateExternalSpan('))'external span declaration'
 $span=B $cs 'VmxEptCompositionOwnerValidateExternalSpan(' ''
 O $span @('OwnerValid(owner)','Separate(owner, sizeof(*owner), external, external_size)',
  'OwnedGraphsSeparate(owner, external, external_size)',
  'VMX_EPT_COMPOSITION_OWNER_OK','VMX_EPT_COMPOSITION_OWNER_INVALID') 'external span'
 $active=B $s 'static int RootsActive(' 'static int RootsReleased('
 O $active @('VMX_EPT_ROOT_TRANSITION_NONE','root->OwnedCount != composition->Count',
  'VMX_EPT_DOMAIN_STAGE_ROOT_OWNED','domain->PendingGeneration != 0',
  'domain->MutationGeneration != domain->InvalidatedGeneration','RootOwned != 1',
  'NativeContext.RootOwned != 1','coverage->RequiredGeneration != domain->MutationGeneration',
  'coverage->AcknowledgedGeneration !=','domain->InvalidatedGeneration') 'roots active'
 $released=B $s 'static int RootsReleased(' 'static int OwnerValid('
 O $released @('VMX_EPT_ROOT_TRANSITION_NONE','root->OwnedCount != 0',
  'VMX_EPT_DOMAIN_STAGE_READY','domain->MutationGeneration != domain->InvalidatedGeneration',
  'RootOwned != 0','NativeContext.RootOwned != 0','RequiredGeneration != 0',
  'AcknowledgedGeneration != 0') 'roots released'
 $valid=B $s 'static int OwnerValid(' 'static int32_t ValidateReport('
 O $valid @('MagicContract','VersionContract','StageContract','DestroyReportContract',
  'VmxEptCompositionOwnerValidate(&owner->Composition)',
  'VMX_EPT_PERSISTENT_LIFECYCLE_READY','RootsActive(&owner->Composition)',
  'VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED',
  'VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASE_PENDING',
  'VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED','RootsReleased(&owner->Composition)',
  'VMX_EPT_PERSISTENT_LIFECYCLE_SESSION_ONLY') 'owner valid'
 $activate=B $s 'static int32_t ActivatePinned(' 'static VMX_EPT_PERSISTENT_LIFECYCLE_STATUS AdoptAll('
 O $activate @('KeGetCurrentIrql() != PASSIVE_LEVEL',
  'RtlZeroMemory(&context->Report','KeRaiseIrql(DISPATCH_LEVEL',
  'VmxEptInstallationOwnerCoordinate(','context->Installation, identity, &context->Report',
  'KeLowerIrql(previous_irql)') 'activate pinned'
 $adopt=B $s 'static VMX_EPT_PERSISTENT_LIFECYCLE_STATUS AdoptAll(' 'static int32_t RunRootOperation('
 O $adopt @('composition->Installation.Records[index].Identity',
  'VmxEptAdoptPersistentRuntimeRoot(','report->AdoptionStatus',
  'VMX_EPT_RUNTIME_ADAPTER_OK','report->AdoptionSuccesses',
  'activation.Installation = &composition->Installation','ActivationCalls',
  'VmxPersistentKernelExecutePinned(','ActivatePinned','report->ActivationKernelStatus',
  'report->ActivationStatus','report->Activation = activation.Report',
  'activation.Report.CapabilityChecked != 0',
  'report->CoverageStatus = VMX_EPT_SESSION_OPERATION_OK','report->CoverageSuccesses',
  'activation.Status != VMX_EPT_KERNEL_INSTALLATION_OK',
  'report->ActivationFailureIndex = index','report->AdoptionFailureIndex = index',
  '(void)DriveInitialInvalidation(composition, report, 0)',
  'report->ActivationSuccesses','DriveInitialInvalidation(composition, report, 1)') 'adopt activate'
 $invalidate=B $s 'static VMX_EPT_PERSISTENT_LIFECYCLE_STATUS DriveInitialInvalidation(' 'static int32_t ActivatePinned('
 O $invalidate @('coverage->RequiredGeneration ==','coverage->AcknowledgedGeneration',
  'VmxPersistentKernelExecutePinned(','InvalidatePinned','report->InitialKernelStatus',
  'report->InitialInvalidationStatus','InitialInvalidationFailureIndex',
  'InitialInvalidationSuccesses','require_active != 0','!RootsActive(composition)',
  'VMX_EPT_PERSISTENT_LIFECYCLE_OK') 'initial invalidation'
 $cleanup=B $s 'static VMX_EPT_PERSISTENT_LIFECYCLE_STATUS CleanupRoots(' 'VMX_EPT_PERSISTENT_LIFECYCLE_STATUS VmxEptPersistentLifecycleOwnerValidate('
 O $cleanup @('VMX_EPT_ROOT_TRANSITION_ENTER','RootRollbackCalls',
  'ROOT_OPERATION_ROLLBACK','VMX_EPT_ROOT_TRANSITION_RELEASE','RootReleaseCalls',
  'ROOT_OPERATION_RESUME_RELEASE','RootReleaseSuccesses',
  'session_owned == 0 && native_owned == 0','ROOT_OPERATION_RELEASE',
  'RootCleanupFailureIndex','RootsReleased(composition)') 'cleanup'
 $init=B $s 'VmxEptPersistentLifecycleOwnerInitialize(' 'VmxEptPersistentLifecycleOwnerDestroy('
 O $init @('KeGetCurrentIrql() != PASSIVE_LEVEL','!Zero(owner, sizeof(*owner))',
  'report.ActivationFailureIndex = VMX_EPT_PERSISTENT_LIFECYCLE_NO_INDEX',
  'VmxPersistentDriverOwnerVisitVmcsBindings(','VmxEptPersistentCompositionInitialize(',
  'AdoptAll(runtime_owner, &owner->Composition, &report)',
  'VMX_EPT_PERSISTENT_LIFECYCLE_READY','VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED',
  'owner->DestroyReport = out_report','Publish(owner, runtime_owner, stage)',
  'OwnerValid(owner)','*out_report = report') 'initialize'
 $destroy=B $s 'VmxEptPersistentLifecycleOwnerDestroy(' ''
 O $destroy @('OwnerValid(owner)','owner->DestroyReport != out_report',
  'VmxEptCompositionOwnerValidateExternalSpan(','VmxPersistentDriverOwnerQueryPhase(',
  'VMX_EPT_PERSISTENT_LIFECYCLE_READY','VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED',
  'SetStage(owner, VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASE_PENDING)',
  'CleanupRoots(&owner->Composition, &report)',
  'SetStage(owner, VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED)',
  'VmxEptCompositionOwnerDestroy(','VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE',
  'VmxPersistentDriverOwnerDetach(','VMX_PERSISTENT_DRIVER_OWNER_DETACHED',
  'RtlSecureZeroMemory(owner, sizeof(*owner))','*out_report = report') 'destroy'
 A(([regex]::Matches($s,'VmxEptAdoptPersistentRuntimeRoot\(')).Count-eq 1)'one adoption call'
 A(([regex]::Matches($s,'VmxEptInstallationOwnerCoordinate\(')).Count-eq 1)'one activation call'
 A(([regex]::Matches($s,'VmxEptSessionMarkUse\(')).Count-eq 0)'no duplicate mark-use call'
 A(([regex]::Matches($s,'VmxPersistentKernelExecutePinned\(')).Count-eq 3)'three pinned paths'
 A(([regex]::Matches($s,'\(void\)DriveInitialInvalidation\(composition, report, 0\)')).Count-eq 2)'two failure coverage drains'
 A(([regex]::Matches($s,'return DriveInitialInvalidation\(composition, report, 1\);')).Count-eq 1)'one success coverage drain'
 A(([regex]::Matches($s,'VmxEptCompositionOwnerDestroy\(')).Count-eq 1)'one composition destroy'
 A(([regex]::Matches($s,'VmxPersistentDriverOwnerDetach\(')).Count-eq 1)'one detach'
 A($s-notmatch '__vmx|VmxOn\s*\(|VmxOff\s*\(|VMXON|VMXOFF|IOCTL|Wdf|DriverEntry')'forbidden direct execution'
 A((([regex]::Matches($d,'(?m)^\s{8}vmx_ept_persistent_lifecycle_owner\.c\r?$')).Count-eq 1))'driver source once'
 A((([regex]::Matches($t,'test_vmx_ept_persistent_lifecycle_owner_contracts')).Count-eq 1))'test once'
}
$s=[IO.File]::ReadAllText((Resolve-Path $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath))
$cs=[IO.File]::ReadAllText((Resolve-Path $CompositionSourcePath));$ch=[IO.File]::ReadAllText((Resolve-Path $CompositionHeaderPath))
$d=[IO.File]::ReadAllText((Resolve-Path $DriverCMakePath));$t=[IO.File]::ReadAllText((Resolve-Path $TestsCMakePath))
V $s $h $cs $ch $d $t
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw $p)-notmatch 'VmxEptPersistentLifecycleOwner|vmx_ept_persistent_lifecycle_owner')"unwired $p"}
$m=@(
 $s.Replace('RootsActive(&owner->Composition)','1'),
 $s.Replace('VmxEptAdoptPersistentRuntimeRoot(','VmxEptPersistentCompositionInitialize('),
 $s.Replace('VmxEptInstallationOwnerCoordinate(','VmxEptInstallationOwnerCoordinateInvalidation('),
 $s.Replace('ActivatePinned, &kernel','InvalidatePinned, &kernel'),
 $s.Replace('activation.Report.CapabilityChecked != 0','0'),
 $s.Replace('activation.Status != VMX_EPT_KERNEL_INSTALLATION_OK','0'),
 $s.Replace('(void)DriveInitialInvalidation(composition, report, 0)',''),
 $s.Replace('DriveInitialInvalidation(composition, report, 1)','VMX_EPT_PERSISTENT_LIFECYCLE_OK'),
 $s.Replace('CleanupRoots(&owner->Composition, &report)','VMX_EPT_PERSISTENT_LIFECYCLE_OK'),
 $s.Replace('VmxEptCompositionOwnerDestroy(','VmxEptCompositionOwnerValidate('),
 $s.Replace('VmxPersistentDriverOwnerDetach(owner->RuntimeOwner)','VMX_PERSISTENT_STATUS_OK'),
 $cs.Replace('OwnedGraphsSeparate(owner, external, external_size)','1')
)
for($i=0;$i-lt$m.Count;$i++){
 $base=if($i-eq$m.Count-1){$cs}else{$s};A($m[$i]-ne$base)"vacuous $i"
 $bad=$false;try{if($i-eq$m.Count-1){V $s $h $m[$i] $ch $d $t}else{V $m[$i] $h $cs $ch $d $t}}catch{$bad=$true}
 A($bad)"accepted $i"
}
Write-Output 'PASS: Persistent EPT lifecycle activates every adopted VMCS before READY and releases roots before detach.'
