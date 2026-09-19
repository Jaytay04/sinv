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
 foreach($term in @('WDK-only','one-time binder','caller-owned','nonpaged','stable',
  'No allocation','Invalid/BUSY calls preserve','rebinding')){
  Assert-C ($header -match "(?i)$term") "Missing header contract $term"
 }
 Assert-C ($source -notmatch '(?i)VMX_V2|vmx_transient|FAST_MUTEX|g_vmxOldIrql|CPU0|Affinity|DriverEntry|IRP_MJ|IOCTL|ExAllocate|MmAllocate|malloc|calloc|realloc|__vmx_vmwrite') 'Forbidden pattern'
 Assert-C ($source -notmatch '(?m)^\s*static\s+(?:VMX_PERSISTENT|VMX_PROCESSOR|VMX_CONTROL).+\w+\s*(?:=|;)') 'Hidden global storage'
 Assert-C ($source -notmatch '->CurrentVmcs(?:Pa|Valid)\s*=(?!=)') 'State binder must not mutate Basic current-VMCS ownership'

 $context=Slice $source 'static bool StateContextValid' 'static bool StateOutputValid'
 $gdt=Slice $source 'static bool StateGdtSpanValid' 'static bool StateCapture'
 $print=Slice $source 'static void StateCaptureFailurePrint' 'static bool StateCapture'
 $capture=Slice $source 'static bool StateCapture' 'static bool StateBuildControls'
 $controls=Slice $source 'static bool StateBuildControls' 'static bool StateRequiredBasicSlots'
 $required=Slice $source 'static bool StateRequiredBasicSlots' 'VMX_PERSISTENT_STATUS VmxPersistentStateOpsBind'
 $bind=$source.Substring($source.IndexOf('VMX_PERSISTENT_STATUS VmxPersistentStateOpsBind'))

 Order $context @('RtlCopyMemory(', 'VMX_PERSISTENT_BASIC_OPS_MAGIC',
  'VMX_PERSISTENT_BASIC_OPS_VERSION', 'copy.Reserved == 0',
  'copy.CurrentVmcsValid <= 1', 'copy.CurrentVmcsValid == 0',
  'copy.CurrentVmcsValid == 1', 'STATE_PAGE_MASK') 'Basic context'
 Order $gdt @('raw->GdtrBase == 0', 'VmxIsAddressCanonical(raw->GdtrBase)',
  'raw->GdtrBase > UINT64_MAX - raw->GdtrLimit',
  'last = raw->GdtrBase + raw->GdtrLimit', 'VmxIsAddressCanonical(last)') 'GDT span'
 Order $print @('DbgPrintEx(', 'persistent state capture failure',
  'phase, raw->Es', 'raw->Cs', 'raw->Ss', 'raw->Tr',
  'raw->GdtrBase', 'raw->IdtrBase', 'msrs->FsBase', 'msrs->GsBase',
  'state->CsDesc.present', 'state->SsDesc.present', 'state->TrDesc.present') 'Failure print'
 Order $capture @('StateOutputValid(', 'RtlZeroMemory(&raw', 'RtlZeroMemory(&msrs',
  'RtlZeroMemory(&staged', '__readcr0()', '__readcr3()', '__readcr4()', '__readdr(7)') 'Capture setup'
 foreach($msr in @('IA32_DEBUGCTL','IA32_PAT','IA32_EFER','IA32_SYSENTER_CS',
  'IA32_SYSENTER_ESP','IA32_SYSENTER_EIP','IA32_FS_BASE','IA32_GS_BASE')){
  Assert-C ((Count $capture "__readmsr($msr)") -eq 1) "Capture MSR changed $msr"
 }
 Order $capture @('__readmsr(IA32_GS_BASE)', 'VmxAsmCaptureSelectorsAndTables(&raw)',
  'StateGdtSpanValid(&raw)', 'StateCaptureFailurePrint(1u',
  'VmxPopulateProcessorState(', 'StateCaptureFailurePrint(2u',
  'VmxValidateProcessorState(&staged)', 'StateCaptureFailurePrint(3u',
  '*out_state = staged') 'Capture validate/publish'

 Assert-C ((Count $controls 'VmxSelectTrueControl(') -eq 4) 'Must select four true/legacy controls'
 Order $controls @('__readmsr(IA32_VMX_BASIC)',
  'IA32_VMX_TRUE_PINBASED_CTLS, IA32_VMX_PINBASED_CTLS',
  'IA32_VMX_TRUE_PROCBASED_CTLS, IA32_VMX_PROCBASED_CTLS',
  'IA32_VMX_TRUE_EXIT_CTLS, IA32_VMX_EXIT_CTLS',
  'IA32_VMX_TRUE_ENTRY_CTLS, IA32_VMX_ENTRY_CTLS',
  'VmxSplitControlMsr(pin_raw', 'VmxSplitControlMsr(primary_raw',
  'VmxSplitControlMsr(exit_raw', 'VmxSplitControlMsr(entry_raw') 'Control reads/splits'
 Order $controls @('VmxAdjustControls(', 'primary.is_valid',
  'primary.value & CPU_BASED_ACTIVATE_SECONDARY_CTLS',
  '__readmsr(IA32_VMX_PROCBASED_CTLS2)', 'VmxSplitControlMsr(secondary_raw',
  'VmxPlanVmControls(', '*out_plan = staged') 'Secondary gate/plan/publish'
 foreach($x in @('persistent control build failure phase=1',
  'persistent control build failure phase=2',
  'persistent control build failure phase=3')){
  Assert-C ($controls.Contains($x)) "Missing control diagnostic $x"
 }
 Assert-C ((Count $controls 'IA32_VMX_PROCBASED_CTLS2') -eq 1) 'Secondary MSR read changed'

 foreach($slot in @('EnterInstructionLevel','LeaveInstructionLevel','ReadCr0','ReadCr4',
  'WriteCr0','WriteCr4','PrepareControlRegisters','Vmxon','Vmclear','Vmptrld','Vmxoff')){
  Assert-C ($required -match "operations->$slot != NULL") "Missing basic slot $slot"
 }
 Order $bind @('RtlCopyMemory(&staged', 'staged.CaptureState != NULL',
  'staged.BuildControls != NULL', 'VMX_PERSISTENT_STATUS_BUSY',
  'StateContextValid(staged.Context)', 'StateRequiredBasicSlots(&staged)',
  'staged.CaptureState = StateCapture', 'staged.BuildControls = StateBuildControls',
  'KeMemoryBarrier()', 'RtlCopyMemory(operations, &staged') 'Preserved staged bind'
}
$source=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath))
$header=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath))
Verify $source $header
$mutations=@(
 $source.Replace('copy.Reserved == 0','true'),
 $source.Replace('RtlCopyMemory(&copy, opaque, sizeof(copy));','RtlCopyMemory(&copy, opaque, sizeof(copy)); ((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque)->CurrentVmcsValid = 0;'),
 $source.Replace('raw->GdtrBase > UINT64_MAX - raw->GdtrLimit','false'),
 $source.Replace('StateGdtSpanValid(&raw)','true'),
 $source.Replace('StateCaptureFailurePrint(2u, &raw, &msrs, &staged);','/* removed capture diagnostic */'),
 $source.Replace('IA32_SYSENTER_EIP','IA32_SYSENTER_ESP'),
 $source.Replace('RtlZeroMemory(&staged, sizeof(staged));','/* removed staged zero */'),
 $source.Replace('VmxValidateProcessorState(&staged)','true'),
 $source.Replace('IA32_VMX_TRUE_EXIT_CTLS, IA32_VMX_EXIT_CTLS','IA32_VMX_EXIT_CTLS, IA32_VMX_EXIT_CTLS'),
 $source.Replace('primary.is_valid &&','true &&'),
 $source.Replace('primary.value & CPU_BASED_ACTIVATE_SECONDARY_CTLS','CPU_BASED_ACTIVATE_SECONDARY_CTLS'),
 $source.Replace('VmxPlanVmControls(basic','RemovedPlan(basic'),
 $source.Replace('operations->Vmxoff != NULL','true'),
 $source.Replace('staged.CaptureState != NULL || staged.BuildControls != NULL','false'),
 ($source+[Environment]::NewLine+'FAST_MUTEX forbiddenMutation;')
)
foreach($mutation in $mutations){
 Assert-C ($mutation -ne $source) 'Mutation did not apply'
 $rejected=$false
 try{Verify $mutation $header}catch{$rejected=$true}
 Assert-C $rejected 'Verifier accepted a required-contract mutation'
}
Write-Output 'Verified A3b2 staged binder, exact state capture/GDT validation, true-control selection, gated secondary MSR, pure planning, preservation, forbidden patterns, and mutation rejection.'
