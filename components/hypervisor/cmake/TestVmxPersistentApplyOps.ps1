param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath
)
$ErrorActionPreference='Stop'
function Assert-C([bool]$ok,[string]$why){if(-not $ok){throw $why}}
function Slice([string]$text,[string]$first,[string]$next){
 $a=$text.IndexOf($first,[StringComparison]::Ordinal)
 $b=$text.IndexOf($next,$a+$first.Length,[StringComparison]::Ordinal)
 Assert-C ($a -ge 0 -and $b -gt $a) "Missing section $first"
 $text.Substring($a,$b-$a)
}
function Order([string]$text,[string[]]$tokens,[string]$name){
 $last=-1
 foreach($token in $tokens){
  $at=$text.IndexOf($token,$last+1,[StringComparison]::Ordinal)
  Assert-C ($at -gt $last) "$name order changed at $token"
  $last=$at
 }
}
function Count([string]$text,[string]$token){
 ([regex]::Matches($text,[regex]::Escape($token))).Count
}
function Verify([string]$source,[string]$header){
 foreach($term in @('WDK-only','one-time binder','caller-owned',
  'nonpaged','stable','no allocation','Adds only ApplyPlan','preserve')){
  Assert-C ($header -match "(?i)$term") "Missing contract $term"
 }
 Assert-C ($source -notmatch '(?i)VMX_V2|vmx_transient|FAST_MUTEX|g_vmxOldIrql|CPU0|Affinity|DriverEntry|IRP_MJ|IOCTL|ExAllocate|MmAllocate|malloc|calloc|realloc|__vmx_(?:on|off|vmclear|vmptrld|vmlaunch|vmresume)') 'Forbidden pattern'
 Assert-C ($source -notmatch '(?m)^\s*static\s+(?:VMX_PERSISTENT|VMX_VMWRITE).+\w+\s*(?:=|;)') 'Hidden global storage'
 Assert-C ($source -notmatch '->CurrentVmcs(?:Pa|Valid)\s*=(?!=)') 'Apply adapter must not mutate Basic VMCS ownership'
 Assert-C ($source -notmatch 'launch->VmcsPlan(?:Outcome|ErrorValid|Error|WriteCount)\s*=') 'Activate-owned launch diagnostics duplicated'
 Assert-C ((Count $source 'launch->FailedVmcsField =') -eq 1) 'Apply owns exactly one launch field'

 $context=Slice $source 'static bool ApplyContextSnapshot' 'static int ApplyVmwrite'
 $writer=Slice $source 'static int ApplyVmwrite' 'static bool ApplyReadInstructionError'
 $reader=Slice $source 'static bool ApplyReadInstructionError' 'static VMX_PERSISTENT_INSTRUCTION_REPORT ApplyInvalidReport'
 $map=Slice $source 'static VMX_PERSISTENT_INSTRUCTION_REPORT ApplyMapReport' 'static bool ApplyInputsValid'
 $inputs=Slice $source 'static bool ApplyInputsValid' 'static VMX_PERSISTENT_INSTRUCTION_REPORT ApplyPlan'
 $apply=Slice $source 'static VMX_PERSISTENT_INSTRUCTION_REPORT ApplyPlan' 'static bool ApplyRequiredSlots'
 $required=Slice $source 'static bool ApplyRequiredSlots' 'VMX_PERSISTENT_STATUS VmxPersistentApplyOpsBind'
 $bind=$source.Substring($source.IndexOf('VMX_PERSISTENT_STATUS VmxPersistentApplyOpsBind'))

 Order $context @('RtlCopyMemory(', 'VMX_PERSISTENT_BASIC_OPS_MAGIC',
  'VMX_PERSISTENT_BASIC_OPS_VERSION', 'copy->Reserved != 0',
  'copy->CurrentVmcsValid > 1', 'copy->CurrentVmcsValid == 0',
  '!require_current && copy->CurrentVmcsPa == 0', 'copy->CurrentVmcsPa != 0',
  'APPLY_PAGE_MASK') 'Context state'
 Order $writer @('__vmx_vmwrite(', 'case 0:', 'VMX_VMWRITE_SUCCESS',
  'case 1:', 'VMX_VMWRITE_FAIL_VALID', 'case 2:', 'VMX_VMWRITE_FAIL_INVALID',
  'default: return VMX_VMWRITE_FAIL_INVALID') 'Writer normalization'
 Order $reader @('__vmx_vmread(VMCS_VM_INSTRUCTION_ERROR', '*out_error = (uint64_t)error') 'Error reader'
 Order $map @('VMX_VMWRITE_SUCCESS', 'VMX_V3D_INSTRUCTION_SUCCESS',
  'VMX_VMWRITE_FAIL_VALID', 'VMX_V3D_INSTRUCTION_FAIL_VALID',
  'VMX_VMWRITE_FAIL_INVALID', 'VMX_V3D_INSTRUCTION_FAIL_INVALID',
  'result->VmInstructionErrorValid', 'result->VmInstructionError <= UINT32_MAX',
  'report.ErrorValid = 1', 'report.Error = (uint32_t)result->VmInstructionError') 'Report mapping/range'

 Order $inputs @('ApplyContextSnapshot(opaque, true', 'ApplySpanValid(plan',
  'ApplySpanValid(launch', 'ApplyOverlap(opaque', 'ApplyOverlap(plan',
  'plan->Count != 0', 'plan->Count <= VMX_VMCS_PLAN_CAPACITY',
  'launch->Header.Magic', 'launch->Header.Size', 'launch->Header.Version') 'Pre-side-effect validation'
 Order $apply @('ApplyInputsValid(', 'VmxApplyVmcsPlan(', 'ApplyVmwrite',
  'ApplyReadInstructionError', 'true)', 'ApplyMapReport(&result)',
  'launch->FailedVmcsField =', '(uint64_t)result.FailedField') 'Pure apply/first failure'
 Assert-C ((Count $apply 'VmxApplyVmcsPlan(') -eq 1) 'Pure apply call changed'

 foreach($slot in @('EnterInstructionLevel','LeaveInstructionLevel','ReadCr0','ReadCr4',
  'WriteCr0','WriteCr4','PrepareControlRegisters','Vmxon','Vmclear','Vmptrld',
  'Vmxoff','CaptureState','BuildControls')){
  Assert-C ($required -match "operations->$slot != NULL") "Missing required slot $slot"
 }
 Order $bind @('RtlCopyMemory(&staged', 'staged.ApplyPlan != NULL',
  'VMX_PERSISTENT_STATUS_BUSY', 'ApplyContextSnapshot(staged.Context, false',
  'ApplyRequiredSlots(&staged)', 'staged.ApplyPlan = ApplyPlan',
  'KeMemoryBarrier()', 'RtlCopyMemory(operations, &staged') 'Staged bind'
}
$source=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath))
$header=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath))
Verify $source $header
$mutations=@(
 $source.Replace('ApplyContextSnapshot(opaque, true','ApplyContextSnapshot(opaque, false'),
 $source.Replace('plan->Count <= VMX_VMCS_PLAN_CAPACITY','true'),
 $source.Replace('launch->Header.Magic == VMX_V3D_PERSISTENT_LAUNCH_MAGIC','true'),
 $source.Replace('VmxApplyVmcsPlan(','RemovedPureApply('),
 $source.Replace('ApplyReadInstructionError, true','NULL, true'),
 $source.Replace('default: return VMX_VMWRITE_FAIL_INVALID','default: return VMX_VMWRITE_SUCCESS'),
 $source.Replace('result->VmInstructionError <= UINT32_MAX','true'),
 $source.Replace('(uint64_t)result.FailedField','0'),
 $source.Replace('copy->CurrentVmcsValid > 1','((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque)->CurrentVmcsValid = 1'),
 $source.Replace('launch->FailedVmcsField =','launch->VmcsPlanOutcome = 1; launch->FailedVmcsField ='),
 $source.Replace('staged.ApplyPlan != NULL','false'),
 $source.Replace('operations->BuildControls != NULL','true'),
 ($source+[Environment]::NewLine+'FAST_MUTEX forbiddenMutation;')
)
foreach($mutation in $mutations){
 Assert-C ($mutation -ne $source) 'Mutation did not apply'
 $rejected=$false
 try{Verify $mutation $header}catch{$rejected=$true}
 Assert-C $rejected 'Verifier accepted required-contract mutation'
}
Write-Output 'Verified A3b3 current-VMCS gating, bounded inputs, normalized contextless writer/error reader, pure first-failure apply, exact representable mapping, staged binding, ownership separation, and mutations.'