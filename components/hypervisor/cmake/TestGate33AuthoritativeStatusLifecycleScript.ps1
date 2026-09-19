param(
 [Parameter(Mandatory=$true)][string]$ScriptPath,
 [Parameter(Mandatory=$true)][string]$LoaderMain,
 [Parameter(Mandatory=$true)][string]$LoaderService,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function Assert-T([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Verify([string]$Script,[string]$Main,[string]$Service,[string]$Doc,[string]$CMake){
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput($Script,[ref]$Tokens,[ref]$Errors)
 Assert-T ($Errors.Count-eq0) 'Gate 33 script does not parse'
 foreach($Token in @(
  'gate33-authoritative-status-lifecycle.ps1','Gate33ProcessorGroups',
  'Invoke-Loader ''load'' ''load''','Invoke-Loader ''authoritative-status'' ''status''',
  '=== AUTHORITATIVE STATUS ===','Persistent Runtime: LOADED',
  'EPT Runtime:        READY','Combined Readiness: READY',
  'CR3 Observation:    INACTIVE',
  'Live Gate Evidence: EXTERNAL (not encoded by runtime status)',
  'Live Gate Evidence: VERIFIED','VMX root active',
  'Probe=AUTHORITATIVE_OPERATOR_STATUS','PersistentRuntime=LOADED',
  'EptRuntime=READY','CombinedReadiness=READY','Cr3Observation=INACTIVE',
  'LiveGateEvidence=EXTERNAL','StatusExit=0',
  'Invoke-Loader ''unload'' ''unload''','$AfterService.ExitCode-ne1060',
  'Stop-Transcript','tar.exe -a -c -f $ZipPath')){
  Assert-T ($Script.Contains($Token)) "Gate 33 missing token: $Token"
 }
 Assert-T ($Script-notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 33 mutates signing, boot, SCM, or reboot state'
 foreach($Token in @('static int CmdStatus(void)','QueryPersistentStatus(','QueryEptStatus(','QueryCr3Status(','Live Gate Evidence: EXTERNAL (not encoded by runtime status)')){
  Assert-T ($Main.Contains($Token)) "Authoritative status command missing: $Token"
 }
 Assert-T (-not$Main.Contains('IOCTL_HYPERVISOR_STATUS')) 'Loader main still exposes the legacy status IOCTL'
 foreach($Token in @('LOADER_OPERATOR_RUNTIME_READY','LOADER_PERSISTENT_WIRE_LOADED','LOADER_EPT_WIRE_READY','return LOADER_OPERATOR_RUNTIME_RESIDUAL;')){
  Assert-T ($Service.Contains($Token)) "Status classifier missing: $Token"
 }
 foreach($Token in @('Gate 33: authoritative operator-status lifecycle','gate33-authoritative-status-lifecycle.ps1','Run Gate 33 once')){
  Assert-T ($Doc.Contains($Token)) "Gate 33 documentation missing: $Token"
 }
 foreach($Token in @('test_gate33_authoritative_status_lifecycle_script_contracts','TestGate33AuthoritativeStatusLifecycleScript.ps1','gate33-authoritative-status-lifecycle.ps1')){
  Assert-T ($CMake.Contains($Token)) "Gate 33 CMake missing: $Token"
 }
}
$Text=@($ScriptPath,$LoaderMain,$LoaderService,$DocPath,$TestsCMake|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify @Text
foreach($Mutation in @(
 $Text[0].Replace('Persistent Runtime: LOADED','Persistent Runtime: DETACHED'),
 $Text[0].Replace('EPT Runtime:        READY','EPT Runtime:        DETACHED'),
 $Text[0].Replace('Combined Readiness: READY','Combined Readiness: DETACHED'),
 $Text[0].Replace('CR3 Observation:    INACTIVE','CR3 Observation:    OBSERVED'),
 $Text[0].Replace('Live Gate Evidence: EXTERNAL (not encoded by runtime status)','Live Gate Evidence: VERIFIED'),
 $Text[0].Replace('$AfterService.ExitCode-ne1060','$false'),
 $Text[0].Replace('tar.exe -a -c -f $ZipPath','tar.exe -tf $ZipPath'))){
 Assert-T ($Mutation-ne$Text[0]) 'Gate 33 script mutation was vacuous'
 $Copy=$Text.Clone();$Copy[0]=$Mutation;$Rejected=$false
 try{Verify @Copy}catch{$Rejected=$true};Assert-T $Rejected 'Gate 33 accepted a script safety mutation'
}
Write-Host 'PASS: Gate 33 preserves exact authoritative READY status, external gate disclosure, and clean lifecycle evidence.'
