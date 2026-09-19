param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths
)
$ErrorActionPreference='Stop';$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath;$d=Get-Content -Raw $DriverCMakePath;$t=Get-Content -Raw $TestsCMakePath
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);if($i-lt 0){return ''};if([string]::IsNullOrEmpty($b)){return $x.Substring($i)};$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$n){$at=0;foreach($v in $n){$i=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($i-lt 0){return $false};$at=$i+$v.Length};$true}
function N([string]$x,[string]$start,[string]$a,[string]$b){$at=$x.IndexOf($start,[StringComparison]::Ordinal);if($at-lt 0){return $x};$i=$x.IndexOf($a,$at,[StringComparison]::Ordinal);if($i-lt 0){return $x};$x.Substring(0,$i)+$b+$x.Substring($i+$a.Length)}
function V([string]$x){
 $args=B $x 'static int ArgumentsValid(' 'static int32_t AcquireLease(';$acq=B $x 'static int32_t AcquireLease(' 'static int32_t AdoptExisting(';$adopt=B $x 'static int32_t AdoptExisting(' 'static int32_t RollbackBookkeeping(';$rollback=B $x 'static int32_t RollbackBookkeeping(' 'static int32_t ReleaseLease(';$release=B $x 'static int32_t ReleaseLease(' 'static int32_t CoordinatePinned(';$pin=B $x 'static int32_t CoordinatePinned(' 'static int32_t VisitRoot(';$visit=B $x 'static int32_t VisitRoot(' 'VMX_EPT_RUNTIME_ADAPTER_STATUS VmxEptAdoptPersistentRuntimeRoot(';$entry=B $x 'VMX_EPT_RUNTIME_ADAPTER_STATUS VmxEptAdoptPersistentRuntimeRoot(' ''
 return (O $args @('Zero(report, sizeof(*report))','Separate(runtime, sizeof(*runtime), owner','Separate(runtime, sizeof(*runtime), identity','Separate(runtime, sizeof(*runtime), report','VmxEptInstallationOwnerValidate(owner)','owner->GraphBytes > SIZE_MAX','Separate(owner->Graph, graph_bytes, runtime','Separate(owner->Graph, graph_bytes, identity','Separate(owner->Graph, graph_bytes, report','identity->SystemIndex >= owner->Count','VmxProcessorIdentityEqual(identity,')) -and
 (O $acq @('context->LogicalLease != 0','Zero(out_proof, sizeof(*out_proof))','VmxProcessorIdentityEqual(identity, &context->Proof->Identity)','*out_proof = *context->Proof','context->LogicalLease = 1')) -and
 (O $adopt @('context->LogicalLease != 1','*out_published != 0','PlanMatchesProof(plan, context->Proof)','VmxEptInstallationAdoptExisting(')) -and
 (O $rollback @('context->LogicalLease != 1','PlanMatchesProof(plan, context->Proof)','VmxEptInstallationRollbackAdoption(')) -and
 (O $release @('context->LogicalLease != 1','VmxProcessorIdentityEqual(identity, &context->Proof->Identity)','context->LogicalLease = 0')) -and
 (O $pin @('RtlZeroMemory(&platform','RtlZeroMemory(&report','platform.Context = context','platform.ContextBytes = sizeof(*context)','platform.AcquireLease = AcquireLease','platform.AdoptExisting = AdoptExisting','platform.RollbackBookkeeping = RollbackBookkeeping','platform.ReleaseLease = ReleaseLease','KeRaiseIrql(DISPATCH_LEVEL, &previous_irql)','KeGetCurrentIrql() != DISPATCH_LEVEL','KeLowerIrql(previous_irql)','VmxEptCoordinateRuntimeRootAdoption(','identity, &platform, &report','KeLowerIrql(previous_irql)','context->Adoption = report','context->LogicalLease != 0')) -and
 (O $visit @('VmxProcessorIdentityEqual(identity, &proof->Identity)','context->Proof = proof','context->VisitorInvoked = 1','VmxPersistentKernelExecutePinned(','&context->Runtime->GuardedPlatform.Platform','CoordinatePinned, &context->Kernel','context->Proof = NULL')) -and
 (O $entry @('KeGetCurrentIrql() != PASSIVE_LEVEL','ArgumentsValid(runtime, owner, identity, out_report)','RtlZeroMemory(&context','RtlZeroMemory(&staged','context.Runtime = runtime','context.Owner = owner','VmxPersistentRuntimeVisitRootProofLease(','VisitRoot, &context, sizeof(context)','staged.RuntimeStatus = runtime_status','staged.Kernel = context.Kernel','staged.Adoption = context.Adoption','VMX_EPT_RUNTIME_ADAPTER_RUNTIME_FAILED','VMX_EPT_RUNTIME_ADAPTER_ADOPTION_FAILED','VMX_EPT_RUNTIME_ADAPTER_KERNEL_FAILED','VMX_EPT_RUNTIME_ADAPTER_INVARIANT','RtlSecureZeroMemory(&context','*out_report = staged'))
}
A(V $s)'adapter ordering'
foreach($n in @('PASSIVE-only','allocation-free trusted binding','runtime gate remains held','exact processor is pinned','IRQL is raised','restored before affinity','Logical E5b3 acquire/release never releases','No VMXON','VMXOFF','INVEPT','hook','IOCTL','public dispatch','serializer','VmxEptAdoptPersistentRuntimeRoot')){A($h.Contains($n))"header $n"}
A((([regex]::Matches($d,'(?m)^\s{8}vmx_ept_runtime_adoption_adapter\.c\r?$')).Count-eq 1))'driver source once';A((([regex]::Matches($t,'test_vmx_ept_runtime_adoption_adapter_contracts')).Count-eq 1))'test once'
A($s-notmatch '__vmx|VmxOn\s*\(|VmxOff\s*\(|VmxEptInvept|VmxEptInstallationExecute|VmxEptInvalidation|Serializer|IOCTL|Hook')'forbidden execution'
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw $p)-notmatch 'VmxEptAdoptPersistentRuntimeRoot|vmx_ept_runtime_adoption_adapter')"unwired $p"}
$m=@(
 (N $s 'static int ArgumentsValid(' 'Zero(report, sizeof(*report))' '1'),
 (N $s 'static int ArgumentsValid(' 'VmxEptInstallationOwnerValidate(owner)' 'VMX_EPT_INSTALLATION_OWNER_OK'),
 (N $s 'static int32_t AcquireLease(' 'context->LogicalLease != 0' 'context->LogicalLease == 0'),
 (N $s 'static int32_t AcquireLease(' 'context->LogicalLease = 1' 'context->LogicalLease = 0'),
 (N $s 'static int32_t AdoptExisting(' 'VmxEptInstallationAdoptExisting(' 'VmxEptInstallationPrepareEnter('),
 (N $s 'static int32_t RollbackBookkeeping(' 'VmxEptInstallationRollbackAdoption(' 'VmxEptInstallationRecoverEnterAfterVmxoff('),
 (N $s 'static int32_t ReleaseLease(' 'context->LogicalLease = 0' 'context->LogicalLease = 1'),
 (N $s 'static int32_t CoordinatePinned(' 'KeRaiseIrql(DISPATCH_LEVEL, &previous_irql)' 'previous_irql = DISPATCH_LEVEL'),
 (N $s 'static int32_t CoordinatePinned(' 'KeLowerIrql(previous_irql);' ''),
 (N $s 'static int32_t CoordinatePinned(' 'identity, &platform, &report' 'identity, &platform, &context->Adoption'),
 (N $s 'static int32_t VisitRoot(' '&context->Runtime->GuardedPlatform.Platform' '&context->Runtime->SourceAllocator'),
 (N $s 'static int32_t VisitRoot(' 'context->Proof = NULL;' ''),
 (N $s 'VmxEptAdoptPersistentRuntimeRoot(' 'VmxPersistentRuntimeVisitRootProofLease(' 'VmxPersistentRuntimeQueryRootProof('),
 (N $s 'VmxEptAdoptPersistentRuntimeRoot(' 'RtlSecureZeroMemory(&context, sizeof(context));' '')
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i]-ne$s)("mutation $i missing");A(-not(V $m[$i]))("accepted adapter mutation $i")}
Write-Host 'PASS: EPT persistent runtime adoption adapter contracts'