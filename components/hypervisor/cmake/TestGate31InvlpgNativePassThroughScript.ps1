param(
 [Parameter(Mandatory=$true)][string]$ScriptPath,
 [Parameter(Mandatory=$true)][string]$LaunchAsm,
 [Parameter(Mandatory=$true)][string]$ExitAsm,
 [Parameter(Mandatory=$true)][string]$CompositionSource,
 [Parameter(Mandatory=$true)][string]$DispatchSource,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function Assert-T([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Get-Block([string]$Text,[string]$Start,[string]$End){
 $StartIndex=$Text.IndexOf($Start,[StringComparison]::Ordinal)
 Assert-T ($StartIndex-ge0) "Gate 31 block start missing: $Start"
 $EndIndex=$Text.IndexOf($End,$StartIndex+$Start.Length,[StringComparison]::Ordinal)
 Assert-T ($EndIndex-gt$StartIndex) "Gate 31 block end missing: $End"
 $Text.Substring($StartIndex,$EndIndex-$StartIndex)
}
function Verify([string]$Source,[string]$Launch,[string]$Exit,[string]$Composition,[string]$Dispatch,[string]$Doc,[string]$CMake){
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput($Source,[ref]$Tokens,[ref]$Errors)
 Assert-T ($Errors.Count-eq0) 'Gate 31 script does not parse'
 foreach($Token in @(
  'gate31-invlpg-native-pass-through.ps1','Gate31ProcessorGroups',
  'Invoke-Loader ''load'' ''load''','Invoke-Loader ''invlpg-probe'' ''invlpg-probe''',
  'INVLPG Probe: status=0x00000000 stage=1',
  'PRIMARY_CONTROLS=0x([0-9A-Fa-f]{8})',
  'INVLPG_COMPLETION=0x([0-9A-Fa-f]{8})',
  '($PrimaryControlsValue -band 0x00000200)-ne0',
  '$InvlpgCompletionValue-ne0x494E564C','Probe=INVLPG_NATIVE_PASS_THROUGH',
  'InvlpgExit=0','GuestRoundtrip=1','Invoke-Loader ''unload'' ''unload''',
  '$AfterService.ExitCode-ne1060','Stop-Transcript','tar.exe -a -c -f $ZipPath')){
  Assert-T ($Source.Contains($Token)) "Gate 31 missing token: $Token"
 }
 Assert-T ($Source-notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 31 mutates signing, boot, SCM, or reboot state'
 foreach($Token in @('V3dGuestInvlpgDiagnostic:','invlpg byte ptr [rsi]',
  'VMX_V3D_INVLPG_PROBE_COMPLETION_OFFSET','VMX_V3D_DIAGNOSTIC_STATE_INVLPG_COMPLETE',
  '0B6B1A9B3h')){
  Assert-T ($Launch.Contains($Token)) "Gate 31 launch path missing: $Token"
 }
 Assert-T (([regex]::Matches($Launch,'(?im)^\s*invlpg\s')).Count-eq1) 'Gate 31 requires exactly one INVLPG instruction'
 $ResumeGate=Get-Block $Exit 'V3dEptResumeGate:' 'V3dResumeDiagnosticStateValid:'
 foreach($Token in @('VMX_V3D_DIAGNOSTIC_FIRST_ARMED','VMX_V3D_DIAGNOSTIC_LAST_ARMED',
  'VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK','jb V3dExitFailClosed','ja V3dExitFailClosed','jz V3dExitFailClosed')){
  Assert-T ($ResumeGate.Contains($Token)) "Gate 31 resume gate missing: $Token"
 }
 $ReturnGate=Get-Block $Exit 'V3dDiagnosticReturnGate:' 'V3dReturnDiagnosticStateValid:'
 foreach($Token in @('VMX_V3D_DIAGNOSTIC_FIRST_COMPLETE','VMX_V3D_DIAGNOSTIC_LAST_COMPLETE',
  'VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK','jb V3dExitFailClosed','ja V3dExitFailClosed','jnz V3dExitFailClosed')){
  Assert-T ($ReturnGate.Contains($Token)) "Gate 31 return gate missing: $Token"
 }
 foreach($Token in @('__vmx_vmread(VMCS_CTRL_CPU_BASED, &primary_controls) != 0','CPU_BASED_INVLPG_EXITING',
  'INVLPG_VMCS_OBSERVATION')){
  Assert-T ($Composition.Contains($Token)) "Gate 31 live VMCS proof missing: $Token"
 }
 foreach($Token in @('const int diagnostic_complete =',
  'VmxV3dDiagnosticStateIsComplete(Context->DiagnosticState)',
  'VmxV3dDiagnosticStateContractValid(','!diagnostic_complete')){
  Assert-T ($Dispatch.Contains($Token)) "Gate 31 dispatch completion missing: $Token"
 }
 foreach($Token in @('Gate 31: supervised native INVLPG transparency',
  'gate31-invlpg-native-pass-through.ps1','Run Gate 31 only once')){
  Assert-T ($Doc.Contains($Token)) "Gate 31 documentation missing: $Token"
 }
 foreach($Token in @('test_gate31_invlpg_native_pass_through_script_contracts',
  'TestGate31InvlpgNativePassThroughScript.ps1','gate31-invlpg-native-pass-through.ps1')){
  Assert-T ($CMake.Contains($Token)) "Gate 31 CMake missing: $Token"
 }
}
$Source=[IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Launch=[IO.File]::ReadAllText((Resolve-Path $LaunchAsm))
$Exit=[IO.File]::ReadAllText((Resolve-Path $ExitAsm))
$Composition=[IO.File]::ReadAllText((Resolve-Path $CompositionSource))
$Dispatch=[IO.File]::ReadAllText((Resolve-Path $DispatchSource))
$Doc=[IO.File]::ReadAllText((Resolve-Path $DocPath))
$CMake=[IO.File]::ReadAllText((Resolve-Path $TestsCMake))
Verify $Source $Launch $Exit $Composition $Dispatch $Doc $CMake
foreach($Mutation in @(
 $Source.Replace('($PrimaryControlsValue -band 0x00000200)-ne0','$false'),
 $Source.Replace('$InvlpgCompletionValue-ne0x494E564C','$false'),
 $Source.Replace('Probe=INVLPG_NATIVE_PASS_THROUGH','Probe=INVLPG_PASS'),
 $Source.Replace('$AfterService.ExitCode-ne1060','$false'),
 $Source.Replace('tar.exe -a -c -f $ZipPath','tar.exe -tf $ZipPath'))){
 Assert-T ($Mutation-ne$Source) 'Gate 31 vacuous mutation';$Rejected=$false
 try{Verify $Mutation $Launch $Exit $Composition $Dispatch $Doc $CMake}catch{$Rejected=$true}
 Assert-T $Rejected 'Gate 31 accepted a safety mutation'
}
$LaunchMutation=[regex]::Replace($Launch,'(?im)^\s*invlpg\s+byte ptr \[rsi\]\s*$','    nop',1)
Assert-T ($LaunchMutation-ne$Launch) 'Gate 31 INVLPG mutation was vacuous'
$Rejected=$false;try{Verify $Source $LaunchMutation $Exit $Composition $Dispatch $Doc $CMake}catch{$Rejected=$true}
Assert-T $Rejected 'Gate 31 accepted removal of INVLPG'
$LaunchMutation=$Launch.Replace('0B6B1A9B3h','0B6B1A9B2h')
Assert-T ($LaunchMutation-ne$Launch) 'Gate 31 completion mutation was vacuous'
$Rejected=$false;try{Verify $Source $LaunchMutation $Exit $Composition $Dispatch $Doc $CMake}catch{$Rejected=$true}
Assert-T $Rejected 'Gate 31 accepted an invalid completion complement'
foreach($CompositionMutation in @(
 $Composition.Replace('__vmx_vmread(VMCS_CTRL_CPU_BASED, &primary_controls) != 0','false'),
 $Composition.Replace('CPU_BASED_INVLPG_EXITING) != 0','0) != 0'))){
 Assert-T ($CompositionMutation-ne$Composition) 'Gate 31 composition mutation was vacuous'
 $Rejected=$false
 try{Verify $Source $Launch $Exit $CompositionMutation $Dispatch $Doc $CMake}catch{$Rejected=$true}
 Assert-T $Rejected 'Gate 31 accepted removal of live primary-control enforcement'
}
foreach($ExitMutation in @(
 $Exit.Replace('cmp eax, VMX_V3D_DIAGNOSTIC_LAST_ARMED','cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_ARMED'),
 $Exit.Replace('cmp eax, VMX_V3D_DIAGNOSTIC_LAST_COMPLETE','cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_COMPLETE'))){
 Assert-T ($ExitMutation-ne$Exit) 'Gate 31 exit-gate mutation was vacuous'
 $Rejected=$false
 try{Verify $Source $Launch $ExitMutation $Composition $Dispatch $Doc $CMake}catch{$Rejected=$true}
 Assert-T $Rejected 'Gate 31 accepted a weakened bounded exit-state gate'
}
$DispatchMutation=$Dispatch.Replace(
 'VmxV3dDiagnosticStateIsComplete(Context->DiagnosticState)',
 'VmxV3dDiagnosticStateIsArmed(Context->DiagnosticState)')
Assert-T ($DispatchMutation-ne$Dispatch) 'Gate 31 dispatch mutation was vacuous'
$Rejected=$false
try{Verify $Source $Launch $Exit $Composition $DispatchMutation $Doc $CMake}catch{$Rejected=$true}
Assert-T $Rejected 'Gate 31 accepted weakened diagnostic completion admission'
Write-Host 'PASS: Gate 31 preserves native INVLPG, live VMCS transparency, and clean lifecycle evidence.'
