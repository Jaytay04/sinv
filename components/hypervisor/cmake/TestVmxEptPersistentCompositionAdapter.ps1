param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a,[StringComparison]::Ordinal);A($i-ge 0)"section $a";if([string]::IsNullOrEmpty($b)){return $t.Substring($i)};$j=$t.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);A($j-gt$i)"section end $b";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1,[StringComparison]::Ordinal);A($q-gt$p)"$n order $x";$p=$q}}
function V([string]$s,[string]$h,[string]$d,[string]$t){
 foreach($x in @('PASSIVE-only synchronous adapter','borrowed binding','never retained','Runtime gate','visitor status','SESSION_ONLY','must be destroyed before Persistent','externally serialized and unwired','VmxEptPersistentCompositionInitialize')){A($h.Contains($x))"header $x"}
 foreach($x in @('int32_t RuntimeStatus;','int32_t VisitorStatus;','int32_t CompositionStatus;','uint32_t VisitorCalls;','uint32_t BindingCount;','VMX_EPT_COMPOSITION_OWNER_REPORT Composition;')){A($h.Contains($x))"report $x"}
 $visitor=B $s 'static int32_t InitializeComposition(' 'VmxEptPersistentCompositionInitialize('
 O $visitor @('binding_count == 0','VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT','binding_count > UINT32_MAX','context->Calls != 0','!Zero(context->Composition','!Zero(&context->Report','context->Calls = 1','context->BindingCount = (uint32_t)binding_count','VmxEptCompositionOwnerInitialize(bindings','context->Composition, &context->Report','return context->Status') 'visitor'
 $entry=B $s 'VmxEptPersistentCompositionInitialize(' ''
 O $entry @('KeGetCurrentIrql() != PASSIVE_LEVEL','!Zero(composition, sizeof(*composition))','!Zero(out_report, sizeof(*out_report))','Separate(runtime_owner, sizeof(*runtime_owner), composition','Separate(runtime_owner, sizeof(*runtime_owner), out_report','Separate(composition, sizeof(*composition), out_report','RtlZeroMemory(&context','context.Composition = composition','context.Status = VMX_EPT_COMPOSITION_OWNER_INVALID','VmxPersistentDriverOwnerVisitVmcsBindings(runtime_owner','InitializeComposition, &context, sizeof(context), &visitor_status','staged.RuntimeStatus = runtime_status','staged.VisitorStatus = visitor_status','staged.CompositionStatus = context.Status','staged.VisitorCalls = context.Calls','staged.BindingCount = context.BindingCount','staged.Composition = context.Report','runtime_status != VMX_PERSISTENT_STATUS_OK','context.Calls == 0 && Zero(composition','context.Calls != 1 || visitor_status != context.Status','context.Status == VMX_EPT_COMPOSITION_OWNER_OK','VmxEptCompositionOwnerValidate(composition)','VMX_EPT_COMPOSITION_OWNER_READY','composition->Count == context.BindingCount','context.Report.StageAfter == VMX_EPT_COMPOSITION_OWNER_READY','Zero(composition, sizeof(*composition))','VMX_EPT_PERSISTENT_COMPOSITION_INITIALIZE_FAILED','VmxEptCompositionOwnerValidate(composition)','VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY','context.Report.StageAfter == VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY','RtlSecureZeroMemory(&context','*out_report = staged','return result') 'entry'
 A(([regex]::Matches($s,'VmxPersistentDriverOwnerVisitVmcsBindings\(')).Count-eq 1)'owner visitor exactly once'
 A(([regex]::Matches($s,'VmxEptCompositionOwnerInitialize\(')).Count-eq 1)'composition initialize exactly once'
 A($s-notmatch 'VmxEptCompositionOwnerDestroy|VmxPersistentDriverOwnerDetach|VmxPersistentRuntime|AllocatePool|FreePool|Serializer|__vmx|VMXON|VMXOFF|INVEPT|IOCTL|Hook')'forbidden adapter operation'
 A((([regex]::Matches($d,'(?m)^\s{8}vmx_ept_persistent_composition_adapter\.c\r?$')).Count-eq 1))'driver source once'
 A((([regex]::Matches($t,'test_vmx_ept_persistent_composition_adapter_contracts')).Count-eq 1))'test once'
}
$s=[IO.File]::ReadAllText((Resolve-Path $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath));$d=[IO.File]::ReadAllText((Resolve-Path $DriverCMakePath));$t=[IO.File]::ReadAllText((Resolve-Path $TestsCMakePath))
V $s $h $d $t
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptPersistentCompositionInitialize|vmx_ept_persistent_composition_adapter')"unwired $p"}
$mut=@(
 $s.Replace('!Zero(context->Composition, sizeof(*context->Composition))','false'),
 $s.Replace('context->Calls = 1;','context->Calls = 0;'),
 $s.Replace('VmxEptCompositionOwnerInitialize(bindings,','VmxEptCompositionOwnerValidate('),
 $s.Replace('!Zero(composition, sizeof(*composition))','false'),
 $s.Replace('!Zero(out_report, sizeof(*out_report))','false'),
 $s.Replace('!Separate(composition, sizeof(*composition), out_report,','!Separate(runtime_owner, sizeof(*runtime_owner), out_report,'),
 $s.Replace('VmxPersistentDriverOwnerVisitVmcsBindings(runtime_owner,','VmxPersistentRuntimeVisitVmcsBindings(&runtime_owner->Runtime,'),
 $s.Replace('staged.RuntimeStatus = runtime_status;',''),
 $s.Replace('context.Calls == 0 && Zero(composition, sizeof(*composition))','true'),
 $s.Replace('context.Calls != 1 || visitor_status != context.Status','context.Calls != 1'),
 $s.Replace('composition->Stage == VMX_EPT_COMPOSITION_OWNER_READY','composition->Stage == VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY'),
 $s.Replace('context.Report.StageAfter == VMX_EPT_COMPOSITION_OWNER_READY','true'),
 $s.Replace('context.Report.StageAfter == VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY','true'),
 $s.Replace('RtlSecureZeroMemory(&context, sizeof(context));',''),
 $s.Replace('*out_report = staged;','')
)
$i=0;foreach($m in $mut){$i++;A($m-ne$s)"vacuous mutation $i";$bad=$false;try{V $m $h $d $t}catch{$bad=$true};A $bad "accepted mutation $i"}
Write-Output 'Verified Persistent binding visit to E5b5 composition initialization, status separation, READY/SESSION_ONLY outcomes, scrub, and private unwired scope.'
