param(
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$LaunchAsm,
    [Parameter(Mandatory=$true)][string]$CompositionSource,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake
)
$ErrorActionPreference='Stop'
function Assert-True([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Verify([string]$Source,[string]$Launch,[string]$Composition,[string]$Doc,[string]$CMake){
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput($Source,[ref]$Tokens,[ref]$Errors)
 Assert-True ($Errors.Count-eq0) 'Gate 30 script does not parse'
 foreach($Token in @(
  'gate30-tsc-native-pass-through.ps1','Gate30ProcessorGroups',
  'Invoke-Loader ''load'' ''load''','Invoke-Loader ''tsc-probe'' ''tsc-probe''',
  'ROOT_TSC_BEFORE=0x([0-9A-Fa-f]{16})','GUEST_TSC=0x([0-9A-Fa-f]{16})',
  'ROOT_TSC_AFTER=0x([0-9A-Fa-f]{16})',
  'PRIMARY_CONTROLS=0x([0-9A-Fa-f]{8})','SECONDARY_CONTROLS=0x([0-9A-Fa-f]{8})',
  'TSC_OFFSET=0x([0-9A-Fa-f]{16})','ROOT_WINDOW_CLASSIFICATION=([0-4])',
  '($PrimaryControlsValue -band 0x00001008)-ne0',
  '($SecondaryControlsValue -band 0x02000000)-ne0','$TscOffsetValue-ne0',
  'Probe=TSC_NATIVE_PASS_THROUGH','GuestRoundtrip=1','RdtscExit=0',
  'TscOffsetting=0','TscScaling=0','Invoke-Loader ''unload'' ''unload''',
  '$AfterService.ExitCode-ne1060','Stop-Transcript','tar.exe -a -c -f $ZipPath')){
  Assert-True ($Source.Contains($Token)) "Gate 30 missing token: $Token"
 }
 Assert-True ($Source-notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 30 mutates signing, boot, SCM, or reboot state'
 foreach($Token in @('V3dGuestTscDiagnostic:','rdtsc','VMX_V3D_TSC_PROBE_GUEST_OFFSET','VMX_V3D_TSC_PROBE_PRIMARY_CONTROLS_OFFSET','VMX_V3D_TSC_PROBE_SECONDARY_CONTROLS_OFFSET','VMX_V3D_TSC_PROBE_TSC_OFFSET_OFFSET','VMX_V3D_DIAGNOSTIC_STATE_TSC_COMPLETE','mov dword ptr [rsi + VMX_V3D_TSC_PROBE_COMPLETION_CONTRACT_OFFSET], 0ABACBCCEh')){
  Assert-True ($Launch.Contains($Token)) "Gate 30 launch path missing: $Token"
 }
 $Sequence='(?is)V3dGuestTscDiagnostic:.*?rdtsc\s+mov dword ptr \[rsi \+ VMX_V3D_TSC_PROBE_GUEST_OFFSET \+ 0\], eax\s+mov dword ptr \[rsi \+ VMX_V3D_TSC_PROBE_GUEST_OFFSET \+ 4\], edx'
 Assert-True ($Launch-match$Sequence) 'Gate 30 does not persist the native RDTSC result'
 foreach($Token in @('tsc_payload->RootTscBefore = __rdtsc()','tsc_payload->RootTscAfter = __rdtsc()','__vmx_vmread(VMCS_CTRL_CPU_BASED','__vmx_vmread(VMCS_CTRL_SECONDARY','__vmx_vmread(VMCS_CTRL_TSC_OFFSET','CPU_BASED_RDTSC_EXITING','SECONDARY_USE_TSC_SCALING')){
  Assert-True ($Composition.Contains($Token)) "Gate 30 live VMCS proof missing: $Token"
 }
 foreach($Token in @('Gate 30: supervised native RDTSC transparency','gate30-tsc-native-pass-through.ps1','Run Gate 30 only once')){
  Assert-True ($Doc.Contains($Token)) "Gate 30 documentation missing: $Token"
 }
 foreach($Token in @('test_gate30_tsc_native_pass_through_script_contracts','TestGate30TscNativePassThroughScript.ps1','gate30-tsc-native-pass-through.ps1')){
  Assert-True ($CMake.Contains($Token)) "Gate 30 CMake missing: $Token"
 }
}
$Source=[IO.File]::ReadAllText((Resolve-Path $ScriptPath));$Launch=[IO.File]::ReadAllText((Resolve-Path $LaunchAsm));$Composition=[IO.File]::ReadAllText((Resolve-Path $CompositionSource));$Doc=[IO.File]::ReadAllText((Resolve-Path $DocPath));$CMake=[IO.File]::ReadAllText((Resolve-Path $TestsCMake))
Verify $Source $Launch $Composition $Doc $CMake
foreach($Mutation in @(
 $Source.Replace('($PrimaryControlsValue -band 0x00001008)-ne0','$false'),
 $Source.Replace('($SecondaryControlsValue -band 0x02000000)-ne0','$false'),
 $Source.Replace('$TscOffsetValue-ne0','$false'),
 $Source.Replace('Probe=TSC_NATIVE_PASS_THROUGH','Probe=TSC_PASS'),
 $Source.Replace('$AfterService.ExitCode-ne1060','$false'),
 $Source.Replace('tar.exe -a -c -f $ZipPath','tar.exe -tf $ZipPath'))){
 Assert-True ($Mutation-ne$Source) 'Gate 30 vacuous mutation';$Rejected=$false
 try{Verify $Mutation $Launch $Composition $Doc $CMake}catch{$Rejected=$true}
 Assert-True $Rejected 'Gate 30 accepted a safety mutation'
}
$LaunchMutation=[regex]::Replace($Launch,'(?im)^\s*rdtsc\s*$','    nop',1)
Assert-True ($LaunchMutation-ne$Launch) 'Gate 30 RDTSC mutation was vacuous'
$Rejected=$false;try{Verify $Source $LaunchMutation $Composition $Doc $CMake}catch{$Rejected=$true}
Assert-True $Rejected 'Gate 30 accepted removal of RDTSC'
$LaunchMutation=$Launch.Replace('0ABACBCCEh','0ABACBCEh')
Assert-True ($LaunchMutation-ne$Launch) 'Gate 30 completion-contract mutation was vacuous'
$Rejected=$false;try{Verify $Source $LaunchMutation $Composition $Doc $CMake}catch{$Rejected=$true}
Assert-True $Rejected 'Gate 30 accepted an invalid completion complement'
Write-Host 'PASS: Gate 30 preserves native RDTSC, live VMCS transparency, diagnostic root-window, and clean lifecycle evidence.'
