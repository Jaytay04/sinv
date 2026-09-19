param(
 [Parameter(Mandatory=$true)][string]$AbiPath,
 [Parameter(Mandatory=$true)][string]$IncPath,
 [Parameter(Mandatory=$true)][string]$ExitPath,
 [Parameter(Mandatory=$true)][string]$CompositionPath,
 [Parameter(Mandatory=$true)][string]$ActivatePath,
 [Parameter(Mandatory=$true)][string]$DispatchPath,
 [Parameter(Mandatory=$true)][string]$UnitPath,
 [Parameter(Mandatory=$true)][string]$RoadmapPath,
 [Parameter(Mandatory=$true)][string]$GatePath,
 [Parameter(Mandatory=$true)][string]$PackagerPath,
 [Parameter(Mandatory=$true)][string]$DocPath)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Has([string]$Text,[string[]]$Tokens,[string]$Name){
 foreach($Token in $Tokens){A ($Text.Contains($Token)) "$Name missing: $Token"}
}
function Verify([string[]]$Text){
 $Abi,$Inc,$Exit,$Composition,$Activate,$Dispatch,$Unit,$Roadmap,$Gate,$Packager,$Doc=$Text
 Has $Abi @(
  'VMX_V3D_DIAGNOSTIC_FIRST_ARMED VMX_V3D_DIAGNOSTIC_STATE_ARMED',
  'VMX_V3D_DIAGNOSTIC_LAST_ARMED VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED',
  'VMX_V3D_DIAGNOSTIC_FIRST_COMPLETE VMX_V3D_DIAGNOSTIC_STATE_COMPLETE',
  'VMX_V3D_DIAGNOSTIC_LAST_COMPLETE VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE',
  'VmxV3dDiagnosticStateIsArmed','VmxV3dDiagnosticStateIsComplete',
  'state & VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK',
  'VmxV3dDiagnosticStateContractValid','contract == ~state',
  'VmxV3dDiagnosticStatePairValid','complete_state == armed_state + 1u') `
  'C diagnostic state contract'
 A ($Abi-match 'state <= VMX_V3D_DIAGNOSTIC_LAST_ARMED &&\s*\(state & VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK\) != 0') `
  'armed upper-bound/parity contract changed'
 A ($Abi-match 'state <= VMX_V3D_DIAGNOSTIC_LAST_COMPLETE &&\s*\(state & VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK\) == 0') `
  'complete upper-bound/parity contract changed'
 A ($Abi-match 'VmxV3dDiagnosticStateIsComplete\(complete_state\) &&\s*complete_state == armed_state \+ 1u') `
  'adjacent state-pair contract changed'
 Has $Inc @(
  'VMX_V3D_DIAGNOSTIC_FIRST_ARMED EQU VMX_V3D_DIAGNOSTIC_STATE_ARMED',
  'VMX_V3D_DIAGNOSTIC_LAST_ARMED EQU VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED',
  'VMX_V3D_DIAGNOSTIC_FIRST_COMPLETE EQU VMX_V3D_DIAGNOSTIC_STATE_COMPLETE',
  'VMX_V3D_DIAGNOSTIC_LAST_COMPLETE EQU VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE',
  'VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK EQU 1h') 'MASM state contract'
 Has $Exit @(
  'cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_ARMED','jb V3dExitFailClosed',
  'cmp eax, VMX_V3D_DIAGNOSTIC_LAST_ARMED','ja V3dExitFailClosed',
  'test eax, VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK','jz V3dExitFailClosed',
  'cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_COMPLETE',
  'cmp eax, VMX_V3D_DIAGNOSTIC_LAST_COMPLETE','jnz V3dExitFailClosed') `
  'MASM bounded parity admission'
 A ($Exit-notmatch 'cmp eax, VMX_V3D_DIAGNOSTIC_STATE_(?:CPUID|MSR_READ|TSC|INVLPG|CET_VMCS|HLT)_(?:ARMED|COMPLETE)') `
  'MASM still enumerates individual diagnostic states'
 Has $Composition @(
  'VmxV3dDiagnosticStateIsArmed(launch->DiagnosticState)',
  'VmxV3dDiagnosticStateContractValid(',
  'diagnostic = VmxV3dDiagnosticStateIsArmed(launch->DiagnosticState);') `
  'composition admission'
 Has $Activate @(
  '!VmxV3dDiagnosticStateIsComplete(diagnostic->CompleteState)',
  '!VmxV3dDiagnosticStatePairValid(',
  'diagnostic->ArmedState, diagnostic->CompleteState') 'activation admission'
 Has $Dispatch @(
  'const int diagnostic_complete =',
  'VmxV3dDiagnosticStateIsComplete(Context->DiagnosticState)',
  'VmxV3dDiagnosticStateContractValid(',
  'Context->DiagnosticState, Context->DiagnosticStateContract)',
  'if (!diagnostic_complete ||') 'dispatcher completion admission'
 A ($Dispatch-notmatch 'Context->DiagnosticState\s*==\s*VMX_V3D_DIAGNOSTIC_STATE_[A-Z_]+_COMPLETE') `
  'dispatcher still enumerates individual diagnostic completion states'
 Has $Unit @('TestDiagnosticStateClasses','TestDiagnosticStatePairs',
  'state above range is not armed','non-adjacent pair rejected') 'unit tests'
 Has $Roadmap @('Runtime V26 - Authenticated Diagnostic State Admission',
  'Gate 65','one composite lifecycle') 'roadmap'
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput(
  $Gate,[ref]$Tokens,[ref]$Errors)
 A ($Errors.Count-eq0) 'Gate 65 does not parse'
 Has $Gate @('gate65-diagnostic-admission-matrix',
  "Invoke-Loader 'ept-violation-roundtrip' 'ept-violation-roundtrip'",
  "Invoke-Loader 'cpuid-probe' 'cpuid-probe'",
  "Invoke-Loader 'msr-transparency-probe' 'msr-transparency-probe'",
  "Invoke-Loader 'tsc-probe' 'tsc-probe'",
  "Invoke-Loader 'invlpg-probe' 'invlpg-probe'",
  "Invoke-Loader 'cet-vmcs-probe' 'cet-vmcs-probe'",
  "Invoke-Loader 'hlt-probe' 'hlt-probe'",
  "Invoke-Loader 'authoritative-status' 'status'",
  "Invoke-Loader 'unload' 'unload'",'StatePairs=7',
  '  CR3 Observation:    OBSERVED','Cr3Observation=OBSERVED',
  'ServiceAfterUnload=ABSENT','tar.exe -a -c -f') 'Gate 65'
 A ($Gate-notmatch'(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
  'Gate 65 mutates signing, boot, SCM teardown, or reboot state'
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput(
  $Packager,[ref]$Tokens,[ref]$Errors)
 A ($Errors.Count-eq0) 'Gate 65 packager does not parse'
 Has $Packager @('Gate 65 source commit must be pushed before packaging.',
  'gate65-diagnostic-admission-matrix.ps1','SOURCE_COMMIT.txt',
  'SHA256SUMS.txt','& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver',
  "'(?i)^\.(pfx|p12|key|cer)$'") 'Gate 65 packager'
 Has $Doc @('Gate 65: diagnostic admission matrix',
  'gate65-diagnostic-admission-matrix.ps1') 'test documentation'
}
$Paths=@($AbiPath,$IncPath,$ExitPath,$CompositionPath,$ActivatePath,$DispatchPath,$UnitPath,
 $RoadmapPath,$GatePath,$PackagerPath,$DocPath)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace('state <= VMX_V3D_DIAGNOSTIC_LAST_ARMED','state >= VMX_V3D_DIAGNOSTIC_LAST_ARMED')},
 @{I=0;T=$Text[0].Replace('contract == ~state','contract == state')},
 @{I=0;T=$Text[0].Replace('complete_state == armed_state + 1u','complete_state >= armed_state + 1u')},
 @{I=2;T=$Text[2].Replace('cmp eax, VMX_V3D_DIAGNOSTIC_LAST_ARMED','cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_ARMED')},
 @{I=2;T=$Text[2].Replace('jnz V3dExitFailClosed','jz V3dExitFailClosed')},
 @{I=3;T=$Text[3].Replace('VmxV3dDiagnosticStateContractValid(','VmxV3dDiagnosticStateIsComplete(')},
 @{I=4;T=$Text[4].Replace('!VmxV3dDiagnosticStatePairValid(','VmxV3dDiagnosticStatePairValid(')},
 @{I=5;T=$Text[5].Replace('VmxV3dDiagnosticStateIsComplete(Context->DiagnosticState)','VmxV3dDiagnosticStateIsArmed(Context->DiagnosticState)')},
 @{I=8;T=$Text[8].Replace("Invoke-Loader 'hlt-probe' 'hlt-probe'","Invoke-Loader 'hlt-probe' 'status'")},
 @{I=8;T=$Text[8].Replace('  CR3 Observation:    OBSERVED','  CR3 Observation:    INACTIVE')},
 @{I=9;T=$Text[9].Replace('& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver','Write-Host signing-skipped')}
)
$Number=0
foreach($Mutation in $Mutations){
 $Number++;A ($Mutation.T-ne$Text[$Mutation.I]) "Mutation $Number was vacuous"
 $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
 try{Verify $Copy}catch{$Rejected=$true}
 A $Rejected "Mutation $Number was accepted"
}
Write-Host 'PASS: authenticated diagnostic state admission parity contracts verified.'
