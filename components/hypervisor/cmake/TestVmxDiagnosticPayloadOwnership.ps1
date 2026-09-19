param(
 [Parameter(Mandatory=$true)][string]$AbiPath,
 [Parameter(Mandatory=$true)][string]$IncPath,
 [Parameter(Mandatory=$true)][string]$LaunchPath,
 [Parameter(Mandatory=$true)][string]$CompositionPath,
 [Parameter(Mandatory=$true)][string]$ActivatePath,
 [Parameter(Mandatory=$true)][string]$DispatchPath,
 [Parameter(Mandatory=$true)][string]$StopTestPath,
 [Parameter(Mandatory=$true)][string]$DispatchTestPath,
 [Parameter(Mandatory=$true)][string]$RoadmapPath,
 [Parameter(Mandatory=$true)][string]$GatePath,
 [Parameter(Mandatory=$true)][string]$PackagerPath,
 [Parameter(Mandatory=$true)][string]$DocPath)
$ErrorActionPreference='Stop'

function Assert-True([bool]$Condition,[string]$Message){
 if(-not $Condition){throw $Message}
}
function Require([string]$Text,[string[]]$Tokens,[string]$Name){
 foreach($Token in $Tokens){Assert-True $Text.Contains($Token) "$Name missing: $Token"}
}
function Ordered([string]$Text,[string[]]$Tokens,[string]$Name){
 $Position=-1
 foreach($Token in $Tokens){
  $Next=$Text.IndexOf($Token,$Position+1)
  Assert-True ($Next -gt $Position) "$Name order missing: $Token"
  $Position=$Next
 }
}
function Body([string]$Text,[string]$Start,[string]$End){
 $a=$Text.IndexOf($Start);$b=$Text.IndexOf($End,$a+1)
 Assert-True ($a -ge 0 -and $b -gt $a) "unable to isolate $Start"
 $Text.Substring($a,$b-$a)
}

function Verify([string[]]$Text,[string]$EptOverride=$null){
 $Abi,$Inc,$Launch,$Composition,$Activate,$Dispatch,$StopTest,
  $DispatchTest,$Roadmap,$Gate,$Packager,$Doc=$Text
 $SourceRoot=Split-Path (Split-Path (Resolve-Path $ActivatePath) -Parent) -Parent
 $Ept=if($null -eq $EptOverride){
  [IO.File]::ReadAllText((Join-Path (Join-Path $SourceRoot 'driver') 'vmx_ept_hook_roundtrip.c'))
 }else{$EptOverride}

 Require $Abi @(
  '#define VMX_V3D_PERSISTENT_LAUNCH_VERSION    4u',
  '#define VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE            0u',
  '#define VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD   1u',
  '#define VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE 2u',
  'uint32_t DiagnosticTargetMode, DiagnosticTargetModeContract;',
  '#define VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET 920u',
  '#define VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_CONTRACT_OFFSET 924u',
  '#define VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET 928u',
  '#define VMX_V3D_CONTEXT_SIZE 1008u',
  'VMX_V3D_ASSERT_CONTEXT(DiagnosticTargetMode,',
  'VMX_V3D_ASSERT_CONTEXT(DiagnosticTargetModeContract,',
  'VMX_V3D_CONTEXT_SIZE <= VMX_V3D_X64_PAGE_SIZE') 'private C ABI'
 Assert-True (-not $Abi.Contains('DispatchSnapshotReserved'))
  'old reserved field must be replaced by target mode fields'
 Ordered $Abi @('DispatchSnapshot;','DiagnosticTargetMode,',
  'DiagnosticPayloadStorage[') 'C ABI append order'
 Require $Inc @(
  'VMX_V3D_PERSISTENT_LAUNCH_VERSION EQU 4h',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE EQU 0h',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD EQU 1h',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE EQU 2h',
  'VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET EQU 398h',
  'VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_CONTRACT_OFFSET EQU 39Ch',
  'VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET EQU 3A0h',
  'VMX_V3D_CONTEXT_SIZE EQU 3F0h') 'private MASM ABI'

 Assert-True (([regex]::Matches($Launch,
  'lea rdx, \[r15 \+ VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET\]')).Count-eq7) `
  'generic and six typed guest routes must derive the owned-storage boundary'
 Assert-True (([regex]::Matches($Launch,'cmp rsi, rdx')).Count-ge6) `
  'six typed guest routes must compare the owned address'
 Assert-True ($Launch.Contains('cmp rax, rdx')) `
  'write guest route must compare the owned address'

 Require $Composition @(
  'ComposeDiagnosticPayloadZero',
  'ComposeDiagnosticPayloadZero(launch)',
  '(UINT_PTR)launch->DiagnosticPayloadStorage',
  'launch->DiagnosticTargetMode == VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE',
  'launch->DiagnosticTargetModeContract == 0') 'composition ownership gate'

 Require $Activate @(
  'VmxPersistentDiagnosticWriteContext(',
  'VmxPersistentDiagnosticEptWriteContext(',
  'DiagnosticPayloadStorageZero',
  'DiagnosticPayloadClear',
  'uint32_t TargetMode;',
  'diagnostic->TargetMode !=',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE',
  'launch->DiagnosticTargetMode = diagnostic->TargetMode;',
  'launch->DiagnosticTargetModeContract = ~diagnostic->TargetMode;',
  'DiagnosticTargetComplete(diagnostic,') 'activation mode contract'
 $Clear=Body $Activate 'static void DiagnosticPayloadClear(' 'static bool Prepared('
 Require $Clear @(
  'launch->DiagnosticTargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE',
  'launch->DiagnosticTargetModeContract = 0') 'activation scrub'
 $OnCpu=Body $Activate 'static int32_t DiagnosticOnCpu(' 'VMX_PERSISTENT_STATUS VmxPersistentDiagnosticWriteContext('
 Require $OnCpu @(
  'owned_payload = diagnostic->TargetMode ==',
  'if (owned_payload) {',
  'memcpy(launch->DiagnosticPayloadStorage,',
  'published_target = (uintptr_t)launch->DiagnosticPayloadStorage;',
  'published_target = diagnostic->CallerTarget;',
  'launch->DiagnosticTarget = published_target;',
  'DiagnosticProof(launch, action, diagnostic->CompleteState,',
  'memcpy(completed, launch->DiagnosticPayloadStorage,',
  'memcpy((void*)diagnostic->CallerTarget, completed,') `
  'activation owned/direct transaction'
 $Owned=Body $Activate 'VMX_PERSISTENT_STATUS VmxPersistentDiagnosticWriteContext(' 'VMX_PERSISTENT_STATUS VmxPersistentDiagnosticCpuidContext('
 $Direct=Body $Activate 'VMX_PERSISTENT_STATUS VmxPersistentDiagnosticEptWriteContext(' 'VMX_PERSISTENT_STATUS VmxPersistentDiagnosticMsrReadContext('
 Require $Owned @(
  'call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;') `
  'owned copy-in/out mode'
 Require $Direct @(
  'call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE;') `
  'direct EPT mode'
 Assert-True (-not $Direct.Contains('memcpy(launch->DiagnosticPayloadStorage,')) `
  'direct EPT path must not copy into owned payload storage'
 Assert-True (-not $Direct.Contains('memcpy((void*)diagnostic->CallerTarget,')) `
  'direct EPT path must not copy completion back to caller'

 $OwnedGate=Body $Dispatch 'static int DiagnosticTargetOwned(' 'static int DiagnosticTargetDirectEpt('
 Require $OwnedGate @(
  'context->DiagnosticTargetMode ==',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD',
  'context->DiagnosticTargetModeContract ==',
  '~VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD',
  '(uintptr_t)context->DiagnosticPayloadStorage') 'typed ownership gate'
 $DirectGate=Body $Dispatch 'static int DiagnosticTargetDirectEpt(' 'static void ResetDiagnostics('
 Require $DirectGate @(
  'context->DiagnosticTargetMode ==',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE',
  'context->DiagnosticTargetModeContract ==',
  '~VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE',
  'context->DiagnosticTarget !=',
  '(uintptr_t)context->DiagnosticPayloadStorage') 'direct EPT ownership gate'
 $EptGate=Body $Dispatch 'static uint32_t DispatchEptViolation(' 'typedef struct VMX_V3D_CR3_WRITE_CONTEXT'
 Require $EptGate @(
  'DiagnosticTargetDirectEpt(context)',
  'io->EptViolation(io->EptContext, &event)') 'EPT direct-target gate'

 Require $Ept @(
  'VmxPersistentDriverOwnerDiagnosticEptWrite(',
  '&context->Owner,0,owner->Page',
  'owner->ByteAfter=*((volatile UCHAR*)owner->Page)') 'EPT direct owner path'
 Assert-True (-not $Ept.Contains('VmxPersistentDriverOwnerDiagnosticWrite(')) `
  'EPT must not use the generic copy-in/out owner call'
 Require $StopTest @(
  'VmxPersistentDiagnosticEptWriteContext',
  'DirectDiagnosticObserved',
  'DirectPayloadStorageUntouched',
  'direct EPT target receives exact guest byte',
  'diagnostic target mode scrubbed') 'activation unit coverage'
 Require $DispatchTest @(
  'ArmOwnedTarget', 'ArmEptTarget',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE',
  'corrupt EPT target mode rejected') 'dispatcher unit coverage'

 Require $Roadmap @('Runtime V27','Gate 66','One composite run',
  'DRIVER_LIFECYCLE_PAGE') 'Runtime V27 roadmap'
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput(
  $Gate,[ref]$Tokens,[ref]$Errors)
 Assert-True ($Errors.Count-eq0) 'Gate 66 does not parse'
 Require $Gate @(
  'gate66-diagnostic-payload-ownership',
  'Probe=DIAGNOSTIC_PAYLOAD_OWNERSHIP',
  'StructuredPayloadOwner=PERSISTENT_LAUNCH_CONTEXT',
  'EptTargetOwner=DRIVER_LIFECYCLE_PAGE',
  'PayloadCapacity=80','OperationsVerified=7',
  "Invoke-Loader 'ept-violation-roundtrip' 'ept-violation-roundtrip'",
  "Invoke-Loader 'hlt-probe' 'hlt-probe'",
  "Invoke-Loader 'unload' 'unload'",'ServiceAfterUnload=ABSENT') 'Gate 66'
 Assert-True ($Gate-notmatch'(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
  'Gate 66 mutates signing, boot, SCM teardown, or reboot state'
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput(
  $Packager,[ref]$Tokens,[ref]$Errors)
 Assert-True ($Errors.Count-eq0) 'Gate 66 packager does not parse'
 Require $Packager @(
  'Gate 66 source commit must be pushed before packaging.',
  'gate66-diagnostic-payload-ownership.ps1','SOURCE_COMMIT.txt',
  'SHA256SUMS.txt',
  '& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver',
  "'(?i)^\.(pfx|p12|key|cer)$'") 'Gate 66 packager'
 Require $Doc @('Gate 66: diagnostic target ownership boundary',
  'gate66-diagnostic-payload-ownership.ps1') 'test documentation'
}

$Paths=@($AbiPath,$IncPath,$LaunchPath,$CompositionPath,$ActivatePath,
 $DispatchPath,$StopTestPath,$DispatchTestPath,$RoadmapPath,$GatePath,
 $PackagerPath,$DocPath)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
$SourceRoot=Split-Path (Split-Path (Resolve-Path $ActivatePath) -Parent) -Parent
$EptPath=Join-Path (Join-Path $SourceRoot 'driver') 'vmx_ept_hook_roundtrip.c'
$EptText=[IO.File]::ReadAllText((Resolve-Path $EptPath))
Verify $Text $EptText

$Mutations=@(
 @{I=0;T=$Text[0].Replace(
  '#define VMX_V3D_PERSISTENT_LAUNCH_VERSION    4u',
  '#define VMX_V3D_PERSISTENT_LAUNCH_VERSION    3u')},
 @{I=0;T=$Text[0] -replace
  '(?m)(#define\s+VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE\s+)2u',
  '${1}3u'},
 @{I=1;T=$Text[1].Replace(
  'VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET EQU 398h',
  'VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET EQU 390h')},
 @{I=3;T=$Text[3].Replace(
  'launch->DiagnosticTargetMode == VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE',
  'true')},
 @{I=4;T=$Text[4].Replace(
  'call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;',
  'call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE;')},
 @{I=4;T=$Text[4].Replace(
  'call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE;',
  'call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;')},
 @{I=5;T=$Text[5].Replace(
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD &&',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE &&')},
 @{I=5;T=$Text[5].Replace(
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE &&',
  'VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD &&')},
 @{I=9;T=$Text[9].Replace('OperationsVerified=7','OperationsVerified=6')},
 @{I=10;T=$Text[10].Replace(
  '& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver',
  'Write-Host signing-skipped')}
)
$Number=0
foreach($Mutation in $Mutations){
 $Number++;Assert-True ($Mutation.T-ne$Text[$Mutation.I]) "Mutation $Number was vacuous"
 $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
 try{Verify $Copy $EptText}catch{$Rejected=$true}
 Assert-True $Rejected "Mutation $Number was accepted"
}
$EptMutation=$EptText.Replace(
 'VmxPersistentDriverOwnerDiagnosticEptWrite(',
 'VmxPersistentDriverOwnerDiagnosticWrite(')
Assert-True ($EptMutation-ne$EptText) 'EPT mutation was vacuous'
$Rejected=$false
try{Verify $Text $EptMutation}catch{$Rejected=$true}
Assert-True $Rejected 'EPT generic owner mutation was accepted'
Write-Host 'PASS: ABI v4 separates owned diagnostic payloads from the direct EPT target and fails closed on contract weakening.'
