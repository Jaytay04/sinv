param(
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$ValidatorPath,
 [Parameter(Mandatory=$true)][string]$DriverPath,
 [Parameter(Mandatory=$true)][string]$ModulePath,
 [Parameter(Mandatory=$true)][string]$ModuleHeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$AbiTestPath,
 [Parameter(Mandatory=$true)][string]$LaunchAsmPath,
 [Parameter(Mandatory=$true)][string]$ScriptPath,
 [Parameter(Mandatory=$true)][string]$DocPath)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Ordered([string]$Text,[string[]]$Tokens,[string]$Name){
 $last=-1
 foreach($token in $Tokens){
  $at=$Text.IndexOf($token,$last+1,[StringComparison]::Ordinal)
  A ($at-gt$last) "$Name order changed at: $token"
  $last=$at
 }
}
function Verify-ModuleCore([string]$Text){
 foreach($token in @(
  'VmxPersistentDriverOwnerDiagnosticWrite(',
  'ExInitializeWorkItem(',
  'ExQueueWorkItem(',
  'KeWaitForSingleObject(',
  'KeSetSystemGroupAffinityThread(',
  'KeRevertToUserGroupAffinityThread(',
  'PsGetCurrentProcess() == Work->ExpectedProcess',
  'CallerIsTarget = PsGetCurrentProcess() == TargetProcess',
  'SystemIsTarget = TargetProcess == PsInitialSystemProcess',
  'if (CallerIsTarget == SystemIsTarget)',
  'Work.ExpectedProcess = PsInitialSystemProcess',
  'Header.TargetCount == 1',
  'Record.MatchedIndex == HYPERVISOR_CR3_STATUS_NO_INDEX',
  'Target.Transitions <= Before.Transitions',
  'Target.Resident != (SystemIsTarget ? 1u : 0u)',
  'After.Transitions <= Target.Transitions',
  'After.Resident != (CallerIsTarget ? 1u : 0u)',
  'HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_COMPLETE')){
  A ($Text.Contains($token)) "module proof missing: $token"
 }
 Ordered $Text @(
  '&DiagnosticByte, &Before);',
  'HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_BASELINE_WRITE_RETURNED',
  'ExQueueWorkItem(',
  'Target = Work.TargetSample',
  'Target.Resident != (SystemIsTarget ? 1u : 0u)',
  '&DiagnosticByte, &After);',
  'After.Resident != (CallerIsTarget ? 1u : 0u)',
  'HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_COMPLETE') 'residency roundtrip'
 $observed=$Text.IndexOf('static VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS ObservedWritePinned')
 $worker=$Text.IndexOf('static VOID TargetWorkItem')
 A ($observed-ge0-and$worker-gt$observed) 'observed boundary or target worker missing'
 Ordered $Text.Substring($observed,$worker-$observed) @(
  'KeSetSystemGroupAffinityThread(',
  'VmxPersistentDriverOwnerDiagnosticWrite(',
  'CaptureSample(Owner, OutSample)',
  'KeRevertToUserGroupAffinityThread(') 'outer-affinity observation'
 A ($Text.IndexOf('ObservedWritePinned(',$worker) -lt
    $Text.IndexOf('KeSetEvent(',$worker)) 'target state must be captured before the System worker signals completion'
 A (-not$Text.Contains('KeStackAttachProcess')-and
    -not$Text.Contains('KeUnstackDetachProcess')) 'probe must not run a complex boundary inside attach window'
 A (-not$Text.Contains('VmxPersistentProcessVisitAttached')) 'probe must not attach to a third-party process'
 A ($Text-notmatch'(?i)(DbgPrint|KdPrint|printf)[^\r\n]*(?:cr3|root|0x%|%p|%I64)') 'module must not print CR3 or pointers'
}
function Verify-LaunchLoopCore([string]$Text){
 Ordered $Text @(
  'V3dGuestDiagnosticGate:',
  'VMX_V3D_DIAGNOSTIC_STATE_ARMED',
  'mov rax, cr3',
  'mov cr3, rax',
  'VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE',
  'vmcall',
  'jmp V3dGuestDiagnosticGate',
  'V3dGuestShutdown:') 'reusable guest diagnostic continuation'
}
function Verify-ScriptCore([string]$Text){
 foreach($token in @(
  'CR3 Status: class=INACTIVE .* targets=1 .* residents=0 transitions=0',
  'SUCCESS: Exact redacted CR3 status snapshot verified (INACTIVE).',
  'Pre-probe CR3 status did not prove an exact inactive target baseline')){
  A ($Text.Contains($token)) "script baseline proof missing: $token"
 }
 A (-not$Text.Contains("Contains('CR3 Status: class=OBSERVED')")) 'script must not require prior CR3 transitions before the positive probe'
}
$Header=[IO.File]::ReadAllText((Resolve-Path $HeaderPath))
$Validator=[IO.File]::ReadAllText((Resolve-Path $ValidatorPath))
$Driver=[IO.File]::ReadAllText((Resolve-Path $DriverPath))
$Module=[IO.File]::ReadAllText((Resolve-Path $ModulePath))
$ModuleHeader=[IO.File]::ReadAllText((Resolve-Path $ModuleHeaderPath))
$DriverCMake=[IO.File]::ReadAllText((Resolve-Path $DriverCMakePath))
$Loader=[IO.File]::ReadAllText((Resolve-Path $LoaderPath))
$AbiTest=[IO.File]::ReadAllText((Resolve-Path $AbiTestPath))
$LaunchAsm=[IO.File]::ReadAllText((Resolve-Path $LaunchAsmPath))
$Script=[IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Doc=[IO.File]::ReadAllText((Resolve-Path $DocPath))

A ($Header.Contains('0x80D, METHOD_BUFFERED')-and
   $Header.Contains('HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE_SIZE UINT32_C(96)')) 'fixed residency ABI missing'
foreach($token in @(
 'IoctlValidateCr3ResidencyProbeRequest(',
 'request->Version != HYPERVISOR_CR3_RESIDENCY_PROBE_VERSION',
 'request->Size != HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST_SIZE',
 'request->Flags != HYPERVISOR_CR3_RESIDENCY_PROBE_FLAGS_NONE',
 'request->Reserved != 0')){A ($Validator.Contains($token)) "validator token missing: $token"}
foreach($token in @(
 'IoctlDispatchCr3ResidencyProbe(',
 'IoControlCode == IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE',
 'VmxCr3BoundaryRoundtripExecute(',
 'CR3_RESIDENCY stage=%lu status=%ld',
 'response.BeforeResident = result.BeforeResident',
 'response.TargetResident = result.TargetResident',
 'response.AfterResident = result.AfterResident')){A ($Driver.Contains($token)) "driver bridge missing: $token"}
foreach($token in @(
 'VMX_CR3_BOUNDARY_ROUNDTRIP_RESULT',
 'Partial redacted stage evidence is retained',
 'raw CR3 values and pointers are never copied')){A ($ModuleHeader.Contains($token)) "module contract missing: $token"}
Verify-ModuleCore $Module
Verify-LaunchLoopCore $LaunchAsm
A ($DriverCMake.Contains('vmx_cr3_boundary_roundtrip.c')) 'driver module not built'
foreach($token in @(
 'hypervisor_loader.exe cr3-residency-probe',
 'CmdCr3ResidencyProbe(',
 'RunCr3ResidencyProbeOnOpenDevice(',
 'IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE',
 'response.BeforeTransitions >= response.TargetTransitions',
 'response.TargetTransitions >= response.AfterTransitions',
 'response.TargetResident != (CallerIsTarget ? 0u : 1u)',
 'response.AfterResident != (CallerIsTarget ? 1u : 0u)',
 'SUCCESS: Exact redacted self-target LEAVE/RETURN roundtrip ',
 'SUCCESS: Exact redacted CR3 target ENTER/LEAVE roundtrip ')){A ($Loader.Contains($token)) "loader proof missing: $token"}
A ($Loader-notmatch'(?i)(root|guest_cr3|system_cr3)=') 'loader must not expose roots'
A ($AbiTest.Contains('0x80002034')-and$AbiTest.Contains('sizeof(response) == 96')) 'ABI pins missing'
$errors=$null
[Management.Automation.Language.Parser]::ParseFile(
 (Resolve-Path $ScriptPath),[ref]$null,[ref]$errors)|Out-Null
A ($errors.Count-eq0) ('script parse errors: '+($errors.Message-join'; '))
Verify-ScriptCore $Script
foreach($token in @(
 "'cr3-before' 'cr3-status'",
 "'cr3-residency-probe' 'cr3-residency-probe'",
 "'cr3-after' 'cr3-status'",
 "'unload' 'unload'",
 'Exact redacted CR3 target ENTER/LEAVE roundtrip verified.',
 'tar.exe -a -c -f')){A ($Script.Contains($token)) "script token missing: $token"}
Ordered $Script @(
 "'cr3-before' 'cr3-status'",
 "'cr3-residency-probe' 'cr3-residency-probe'",
 "'cr3-after' 'cr3-status'",
 "'unload' 'unload'") 'operator run'
foreach($token in @(
 'Gate 17: positive CR3 target-residency roundtrip',
 'COMPLETE',
 '0/1/0',
 '4294967295/0/4294967295',
 'gate17-cr3-residency-roundtrip.ps1')){
 A ($Doc.Contains($token)) "Gate 17 documentation missing: $token"
}
$Mutations=@(
 $Module.Replace('Header.TargetCount == 1','Header.TargetCount != 0'),
 $Module.Replace('PsGetCurrentProcess() == Work->ExpectedProcess','Work->ExpectedProcess != NULL'),
 $Module.Replace('if (CallerIsTarget == SystemIsTarget)','if (FALSE)'),
 $Module.Replace('Status = CaptureSample(Owner, OutSample);','Status = VMX_CR3_BOUNDARY_ROUNDTRIP_OK;'),
 $Module.Replace('Target.Transitions <= Before.Transitions','false'),
 $Module.Replace('Target.Resident != (SystemIsTarget ? 1u : 0u)','false'),
 $Module.Replace('After.Transitions <= Target.Transitions','false'),
 $Module.Replace('After.Resident != (CallerIsTarget ? 1u : 0u)','false'),
 $Module.Replace('HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_COMPLETE','HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_CALLER_CAPTURED'))
foreach($Mutation in $Mutations){
 A ($Mutation-ne$Module) 'vacuous residency mutation'
 $Rejected=$false
 try{Verify-ModuleCore $Mutation}catch{$Rejected=$true}
 A $Rejected 'Gate 17 accepted a residency safety mutation'
}
$ScriptMutations=@(
 $Script.Replace('class=INACTIVE','class=OBSERVED'),
 $Script.Replace('residents=0 transitions=0','residents=0 transitions=(\d+)'),
 $Script.Replace('snapshot verified (INACTIVE).','snapshot verified (OBSERVED).'))
foreach($Mutation in $ScriptMutations){
 A ($Mutation-ne$Script) 'vacuous Gate 17 script mutation'
 $Rejected=$false
 try{Verify-ScriptCore $Mutation}catch{$Rejected=$true}
 A $Rejected 'Gate 17 accepted a pre-probe baseline mutation'
}
$LaunchMutation=$LaunchAsm.Replace('jmp V3dGuestDiagnosticGate','jmp V3dGuestShutdown')
A ($LaunchMutation-ne$LaunchAsm) 'vacuous guest diagnostic continuation mutation'
$LaunchRejected=$false
try{Verify-LaunchLoopCore $LaunchMutation}catch{$LaunchRejected=$true}
A $LaunchRejected 'Gate 17 accepted a one-shot guest diagnostic continuation'
Write-Output 'Verified fixed Gate 17 ABI, isolated System-worker ENTER/LEAVE proof, reusable guest continuation, loader, fail-closed VM script, and mutation rejection.'
