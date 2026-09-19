param(
 [Parameter(Mandatory=$true)][string]$InterfacePath,
 [Parameter(Mandatory=$true)][string]$CapabilityPath,
 [Parameter(Mandatory=$true)][string]$AbiPath,
 [Parameter(Mandatory=$true)][string]$LaunchPath,
 [Parameter(Mandatory=$true)][string]$CompositionPath,
 [Parameter(Mandatory=$true)][string]$DispatchPath,
 [Parameter(Mandatory=$true)][string]$ActivatePath,
 [Parameter(Mandatory=$true)][string]$RuntimePath,
 [Parameter(Mandatory=$true)][string]$OwnerPath,
 [Parameter(Mandatory=$true)][string]$ValidatorPath,
 [Parameter(Mandatory=$true)][string]$RequestFramePath,
 [Parameter(Mandatory=$true)][string]$ResponseFramePath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$GatePath,
 [Parameter(Mandatory=$true)][string]$PackagerPath,
 [Parameter(Mandatory=$true)][string]$RoadmapPath,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ExitPath)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Has([string]$Text,[string[]]$Tokens,[string]$Name){
 foreach($Token in $Tokens){A ($Text.Contains($Token)) "$Name missing: $Token"}
}
function Parse([string]$Text,[string]$Name){
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput(
  $Text,[ref]$Tokens,[ref]$Errors)
 A ($Errors.Count-eq0) "$Name does not parse"
}
function Verify([string[]]$Text){
 $Interface,$Capability,$Abi,$Launch,$Composition,$Dispatch,$Activate,
  $Runtime,$Owner,$Validator,$RequestFrame,$ResponseFrame,$Ioctl,$Loader,
  $Gate,$Packager,$Roadmap,$Doc,$Tests,$Exit=$Text
 Parse $Gate 'Gate 64';Parse $Packager 'Gate 64 packager'
 Has $Interface @('IOCTL_HYPERVISOR_HLT_PROBE','0x81E, METHOD_BUFFERED, FILE_ANY_ACCESS',
  'HYPERVISOR_HLT_PROBE_REQUEST_SIZE UINT32_C(16)',
  'HYPERVISOR_HLT_PROBE_RESPONSE_SIZE UINT32_C(48)',
  'HYPERVISOR_HLT_PROBE_PRIMARY_HLT_EXITING UINT32_C(0x00000080)',
  'HYPERVISOR_HLT_PROBE_EXIT_REASON UINT32_C(12)',
  'sizeof(HYPERVISOR_HLT_PROBE_RESPONSE)==48') 'public HLT ABI'
 Has $Capability @('HYPERVISOR_CONTROL_CAPABILITY_HLT_EXIT_DIAGNOSTIC (UINT64_C(1) << 10)',
  'HYPERVISOR_CONTROL_CAPABILITIES_ALL UINT64_C(0x00000000000007FF)',
  'HYPERVISOR_CONTROL_OPERATIONAL_IOCTL_COUNT UINT32_C(30)') 'capability manifest'
 Has $Abi @('VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED 13u',
  'VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE 14u','VMX_V3D_HLT_PROBE_PAYLOAD',
  'VMX_V3D_HLT_PROBE_COMPLETION UINT32_C(0x484C5431)',
  'VMX_V3D_HLT_PROBE_PAYLOAD_SIZE UINT32_C(48)') 'internal HLT ABI'
 Has $Launch @('V3dGuestHltDiagnostic:','VMX_V3D_HLT_PROBE_PAYLOAD_MAGIC',
  'test dword ptr [rsi + VMX_V3D_HLT_PROBE_PRIMARY_BEFORE_OFFSET], 080h',
  'hlt','VMX_V3D_HLT_PROBE_EXIT_REASON_OFFSET], 0Ch','VMX_V3D_HLT_PROBE_COMPLETION',
  'VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE','vmcall') 'synthetic HLT guest'
 Has $Exit @('V3dEptResumeGate:',
  'cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_ARMED',
  'cmp eax, VMX_V3D_DIAGNOSTIC_LAST_ARMED',
  'test eax, VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK',
  'jz V3dExitFailClosed','V3dResumeDiagnosticStateValid:',
  'V3dDiagnosticReturnGate:',
  'cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_COMPLETE',
  'cmp eax, VMX_V3D_DIAGNOSTIC_LAST_COMPLETE',
  'jnz V3dExitFailClosed','V3dReturnDiagnosticStateValid:') `
  'bounded diagnostic state admission'
 Has $Composition @('VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED',
  '__vmx_vmread(VMCS_CTRL_CPU_BASED, &primary_controls)',
  '(primary_controls & CPU_BASED_HLT_EXITING) != 0',
  'armed_controls = primary_controls | CPU_BASED_HLT_EXITING',
  '__vmx_vmwrite(VMCS_CTRL_CPU_BASED, armed_controls)',
  'armed_controls !=','(primary_controls | CPU_BASED_HLT_EXITING)',
  '__vmx_vmwrite(VMCS_CTRL_CPU_BASED, original_controls)',
  'restored_controls != original_controls',
  'PrimaryControlsAfter = (uint32_t)restored_controls',
  'HLT_VMEXIT_OBSERVATION') 'transactional HLT composition'
 Has $Dispatch @('DispatchHlt(',
  'reason != VMX_V3D_DISPATCH_EXIT_REASON_HLT','qualification != 0',
  'length != 1','VMX_V3D_DISPATCH_VMCS_GUEST_RIP',
  'rip + 1','payload->ExitReason = (uint32_t)reason',
  'payload->ExitInstructionLength = (uint32_t)length') 'typed HLT dispatcher'
 Has $Activate @('VmxPersistentDiagnosticHltContext(',
  'VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED','VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE',
  'payload->PrimaryControlsAfter == payload->PrimaryControlsBefore',
  'payload->ExitReason == EXIT_REASON_HLT',
  'payload->ExitInstructionLength == 1') 'HLT activation'
 Has $Runtime @('VmxPersistentRuntimeHltProbe(') 'runtime lifecycle'
 Has $Owner @('VmxPersistentDriverOwnerHltProbe(') 'driver owner'
 Has $Validator @('IoctlValidateHltProbeRequest') 'request validator'
 Has $RequestFrame @('IOCTL_HYPERVISOR_HLT_PROBE',
  'sizeof(HYPERVISOR_HLT_PROBE_REQUEST)') 'request framing'
 Has $ResponseFrame @('IOCTL_HYPERVISOR_HLT_PROBE',
  'HYPERVISOR_HLT_PROBE_RESPONSE_SIZE') 'response framing'
 Has $Ioctl @('IoctlDispatchHltProbe(','VmxPersistentDriverOwnerHltProbe',
  'payload.PrimaryControlsAfter != payload.PrimaryControlsBefore',
  'payload.ExitReason != EXIT_REASON_HLT','payload.ExitInstructionLength != 1',
  'HLT_EXIT_PROBE boundary') 'driver dispatch'
 Has $Loader @('CmdHltProbe','hlt-probe','HLT_EXIT_REASON=%u',
  'HLT_EXIT_LENGTH=%u','HLT_COMPLETION=0x%08X',
  'SUCCESS: Persistent HLT VM-exit and exact primary-control restoration verified.') `
  'loader command'
 Has $Gate @('gate64-hlt-exit-roundtrip','Gate64ProcessorGroups',
  "Invoke-Loader 'hlt-probe' 'hlt-probe'",'HLT_EXIT_REASON=12',
  'HLT_EXIT_LENGTH=1','HLT_COMPLETION=0x484C5431',
  'PRIMARY_CONTROLS_BEFORE=0x([0-9A-Fa-f]{8})\r?$',
  'PRIMARY_CONTROLS_ARMED=0x([0-9A-Fa-f]{8})\r?$',
  'PRIMARY_CONTROLS_AFTER=0x([0-9A-Fa-f]{8})\r?$',
  '$Armed-ne($Before -bor 0x80)','$After-ne$Before',
  'Probe=HLT_EXIT_ROUNDTRIP','ControlRestored=TRUE',
  "Invoke-Loader 'unload' 'unload'",'tar.exe -a -c -f') 'Gate 64'
 A ($Gate-notmatch'(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
  'Gate 64 mutates signing, boot, SCM, or reboot state'
 Has $Packager @('status --porcelain --untracked-files=all',
  'Gate 64 source commit must be pushed before packaging.',
  '& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver',
  'gate64-hlt-exit-roundtrip.ps1','SOURCE_COMMIT.txt','SHA256SUMS.txt',
  "'(?i)^\.(pfx|p12|key|cer)$'",'tar.exe -a -c -f $ZipPath') 'Gate 64 packager'
 A ($Packager-notmatch'(?i)Copy-Item[^\r\n]+\$(Pfx|PfxPassword)') `
  'Gate 64 packager copies private signing material'
 Has $Roadmap @('Runtime V25 - Authenticated HLT VM-Exit Roundtrip',
  'Status: complete. Gate 64 is accepted',
  'One lifecycle is proportionate') 'Runtime V25 roadmap'
 Has $Doc @('Gate 64: authenticated HLT VM-exit roundtrip',
  'gate64-hlt-exit-roundtrip.ps1','Do not retry LOAD') 'test documentation'
 Has $Tests @('test_vmx_hlt_exit_roundtrip_contracts',
  'TestVmxHltExitRoundtrip.ps1') 'tests CMake'
}
$Paths=@($InterfacePath,$CapabilityPath,$AbiPath,$LaunchPath,$CompositionPath,
 $DispatchPath,$ActivatePath,$RuntimePath,$OwnerPath,$ValidatorPath,
 $RequestFramePath,$ResponseFramePath,$IoctlPath,$LoaderPath,$GatePath,
 $PackagerPath,$RoadmapPath,$DocPath,$TestsCMakePath,$ExitPath)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=3;T=$Text[3].Replace("`n    hlt`n","`n    nop`n")},
 @{I=4;T=$Text[4].Replace('restored_controls != original_controls','false')},
 @{I=5;T=$Text[5].Replace('qualification != 0','qualification == 0')},
 @{I=5;T=$Text[5].Replace('length != 1','length == 1')},
 @{I=12;T=$Text[12].Replace('payload.PrimaryControlsAfter != payload.PrimaryControlsBefore','false')},
 @{I=14;T=$Text[14].Replace('$Armed-ne($Before -bor 0x80)','$false')},
 @{I=14;T=$Text[14].Replace('\r?$','$')},
 @{I=14;T=$Text[14].Replace("Invoke-Loader 'unload' 'unload'","Invoke-Loader 'unload' 'status'")},
 @{I=15;T=$Text[15].Replace('& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver','Write-Host signing-skipped')})
$Mutations += @{I=19;T=$Text[19].Replace(
 'cmp eax, VMX_V3D_DIAGNOSTIC_LAST_ARMED',
 'cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_ARMED')}
$Mutations += @{I=19;T=$Text[19].Replace(
 'cmp eax, VMX_V3D_DIAGNOSTIC_LAST_COMPLETE',
 'cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_COMPLETE')}
$Number=0
foreach($Mutation in $Mutations){
 $Number++;A ($Mutation.T-ne$Text[$Mutation.I]) "Mutation $Number was vacuous"
 $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
 try{Verify $Copy}catch{$Rejected=$true}
 A $Rejected "Mutation $Number was accepted"
}
Write-Host 'PASS: authenticated transactional HLT VM-exit roundtrip contracts verified.'
