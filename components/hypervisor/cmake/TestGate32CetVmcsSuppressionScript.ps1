param(
 [Parameter(Mandatory=$true)][string]$ScriptPath,
 [Parameter(Mandatory=$true)][string]$AbiHeader,
 [Parameter(Mandatory=$true)][string]$LaunchAsm,
 [Parameter(Mandatory=$true)][string]$ExitAsm,
 [Parameter(Mandatory=$true)][string]$CompositionSource,
 [Parameter(Mandatory=$true)][string]$DispatchSource,
 [Parameter(Mandatory=$true)][string]$InterfacePath,
 [Parameter(Mandatory=$true)][string]$ValidatorPath,
 [Parameter(Mandatory=$true)][string]$DriverPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$RuntimeSource,
 [Parameter(Mandatory=$true)][string]$OwnerSource,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function Assert-T([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Verify([string]$Source,[string]$Abi,[string]$Launch,[string]$Exit,
 [string]$Composition,[string]$Dispatch,[string]$Interface,[string]$Validator,
 [string]$Driver,[string]$Loader,[string]$Runtime,[string]$Owner,
 [string]$Doc,[string]$CMake){
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput($Source,[ref]$Tokens,[ref]$Errors)
 Assert-T ($Errors.Count-eq0) 'Gate 32 script does not parse'
 foreach($Token in @(
  'gate32-cet-vmcs-suppression.ps1','Gate32ProcessorGroups',
  'Invoke-Loader ''load'' ''load''','Invoke-Loader ''cet-vmcs-probe'' ''cet-vmcs-probe''',
  'CET VMCS Probe: status=0x00000000 stage=1',
  'PRIMARY_CONTROLS=0x([0-9A-Fa-f]{8})','EXIT_CONTROLS=0x([0-9A-Fa-f]{8})',
  'ENTRY_CONTROLS=0x([0-9A-Fa-f]{8})','GUEST_CR4=0x([0-9A-Fa-f]{16})',
  'HOST_CR4=0x([0-9A-Fa-f]{16})','ROOT_CR4=0x([0-9A-Fa-f]{16})',
  'CET_VMCS_COMPLETION=0x([0-9A-Fa-f]{8})',
  '($PrimaryControlsValue -band 0x00000080)-ne0',
  '($ExitControlsValue -band 0x10000000)-ne0',
  '($EntryControlsValue -band 0x00100000)-ne0',
  '($GuestCr4Value -band 0x0000000000800000)-ne0',
  '($HostCr4Value -band 0x0000000000800000)-ne0',
  '($RootCr4Value -band 0x0000000000800000)-ne0',
  '$CetVmcsCompletionValue-ne0x43455431','Probe=CET_VMCS_SUPPRESSION',
  'GuestRoundtrip=1','Invoke-Loader ''unload'' ''unload''',
  '$AfterService.ExitCode-ne1060','Stop-Transcript','tar.exe -a -c -f $ZipPath')){
  Assert-T ($Source.Contains($Token)) "Gate 32 missing token: $Token"
 }
 Assert-T ($Source-notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 32 mutates signing, boot, SCM, or reboot state'
 foreach($Token in @('VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_ARMED','VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_COMPLETE','VMX_V3D_CET_VMCS_PROBE_PAYLOAD','VMX_V3D_CET_VMCS_PROBE_COMPLETION')){
  Assert-T ($Abi.Contains($Token)) "Gate 32 ABI missing: $Token"
 }
 foreach($Token in @('V3dGuestCetVmcsDiagnostic:','VMX_V3D_CET_VMCS_PROBE_PAYLOAD_MAGIC','0BCBAABCEh','0FFFFFFF3h')){
  Assert-T ($Launch.Contains($Token)) "Gate 32 guest completion missing: $Token"
 }
 $CetGuest=[regex]::Match($Launch,
  '(?ms)^V3dGuestCetVmcsDiagnostic:.*?(?=^V3dGuestHltDiagnostic:)').Value
 Assert-T ($CetGuest.Length-ne0) 'Gate 32 CET guest block is missing'
 Assert-T ($CetGuest-notmatch '(?im)^\s*(hlt|rdssp|wrss|rstorssp|saveprevssp|setssbsy|clrssbsy)\b') 'Gate 32 guest path must not execute HLT or CET instructions'
 foreach($Token in @('VMX_V3D_DIAGNOSTIC_FIRST_ARMED','VMX_V3D_DIAGNOSTIC_LAST_ARMED',
  'VMX_V3D_DIAGNOSTIC_FIRST_COMPLETE','VMX_V3D_DIAGNOSTIC_LAST_COMPLETE',
  'VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK')){
  Assert-T ($Exit.Contains($Token)) "Gate 32 exit gate missing: $Token"
 }
 foreach($Token in @('__vmx_vmread(VMCS_CTRL_VM_EXIT_CONTROLS','__vmx_vmread(VMCS_CTRL_VM_ENTRY_CONTROLS','__vmx_vmread(VMCS_GUEST_CR4','__vmx_vmread(VMCS_HOST_CR4','__readcr4()','CPU_BASED_HLT_EXITING','VM_EXIT_LOAD_CET_STATE','VM_ENTRY_LOAD_CET_STATE','CR4_CET','CET_VMCS_OBSERVATION')){
  Assert-T ($Composition.Contains($Token)) "Gate 32 live observation missing: $Token"
 }
 foreach($Token in @('const int diagnostic_complete =',
  'VmxV3dDiagnosticStateIsComplete(Context->DiagnosticState)',
  'VmxV3dDiagnosticStateContractValid(','!diagnostic_complete')){
  Assert-T ($Dispatch.Contains($Token)) "Gate 32 dispatch completion missing: $Token"
 }
 foreach($Token in @('IOCTL_HYPERVISOR_CET_VMCS_PROBE','HYPERVISOR_CET_VMCS_PROBE_RESPONSE_SIZE','HYPERVISOR_CET_VMCS_PROBE_RESPONSE')){
  Assert-T ($Interface.Contains($Token)) "Gate 32 public ABI missing: $Token"
 }
 Assert-T ($Validator.Contains('IoctlValidateCetVmcsProbeRequest')) 'Gate 32 request validator missing'
 foreach($Token in @('IoctlDispatchCetVmcsProbe','VmxPersistentDriverOwnerCetVmcsProbe','CET_SUPPRESSION_PROBE boundary')){
  Assert-T ($Driver.Contains($Token)) "Gate 32 driver dispatch missing: $Token"
 }
 foreach($Token in @('CmdCetVmcsProbe','cet-vmcs-probe','LoaderCetVmcsStateSuppressed','CET_VMCS_COMPLETION')){
  Assert-T ($Loader.Contains($Token)) "Gate 32 loader path missing: $Token"
 }
 Assert-T ($Runtime.Contains('VmxPersistentRuntimeCetVmcsProbe')) 'Gate 32 runtime path missing'
 Assert-T ($Owner.Contains('VmxPersistentDriverOwnerCetVmcsProbe')) 'Gate 32 owner path missing'
 foreach($Token in @('Gate 32: supervised live CET/HLT VMCS suppression','gate32-cet-vmcs-suppression.ps1','Run Gate 32 once')){
  Assert-T ($Doc.Contains($Token)) "Gate 32 documentation missing: $Token"
 }
 foreach($Token in @('test_gate32_cet_vmcs_suppression_script_contracts','TestGate32CetVmcsSuppressionScript.ps1','gate32-cet-vmcs-suppression.ps1')){
  Assert-T ($CMake.Contains($Token)) "Gate 32 CMake missing: $Token"
 }
}
$Paths=@($ScriptPath,$AbiHeader,$LaunchAsm,$ExitAsm,$CompositionSource,$DispatchSource,
 $InterfacePath,$ValidatorPath,$DriverPath,$LoaderPath,$RuntimeSource,$OwnerSource,
 $DocPath,$TestsCMake)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify @Text
foreach($Mutation in @(
 $Text[0].Replace('($PrimaryControlsValue -band 0x00000080)-ne0','$false'),
 $Text[0].Replace('($ExitControlsValue -band 0x10000000)-ne0','$false'),
 $Text[0].Replace('($EntryControlsValue -band 0x00100000)-ne0','$false'),
 $Text[0].Replace('($GuestCr4Value -band 0x0000000000800000)-ne0','$false'),
 $Text[0].Replace('$CetVmcsCompletionValue-ne0x43455431','$false'),
 $Text[0].Replace('$AfterService.ExitCode-ne1060','$false'),
 $Text[0].Replace('tar.exe -a -c -f $ZipPath','tar.exe -tf $ZipPath'))){
 Assert-T ($Mutation-ne$Text[0]) 'Gate 32 script mutation was vacuous'
 $Copy=$Text.Clone();$Copy[0]=$Mutation;$Rejected=$false
 try{Verify @Copy}catch{$Rejected=$true};Assert-T $Rejected 'Gate 32 accepted a script safety mutation'
}
foreach($IndexToken in @(
 @(2,'0BCBAABCEh','0BCBAABCFh'),
 @(4,'__vmx_vmread(VMCS_CTRL_VM_EXIT_CONTROLS','false /* removed exit VMREAD */ ('),
 @(4,'VM_EXIT_LOAD_CET_STATE) != 0','0) != 0'),
 @(5,'VmxV3dDiagnosticStateIsComplete(Context->DiagnosticState)','VmxV3dDiagnosticStateIsArmed(Context->DiagnosticState)'))){
 $Index=[int]$IndexToken[0];$Mutation=$Text[$Index].Replace($IndexToken[1],$IndexToken[2])
 Assert-T ($Mutation-ne$Text[$Index]) 'Gate 32 implementation mutation was vacuous'
 $Copy=$Text.Clone();$Copy[$Index]=$Mutation;$Rejected=$false
 try{Verify @Copy}catch{$Rejected=$true};Assert-T $Rejected 'Gate 32 accepted an implementation safety mutation'
}
Write-Host 'PASS: Gate 32 preserves authenticated live CET/HLT VMCS suppression and clean lifecycle evidence.'
