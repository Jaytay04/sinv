param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$RoadmapPath,
 [Parameter(Mandatory=$true)][string]$GatePath,
 [Parameter(Mandatory=$true)][string]$PackagerPath,
 [Parameter(Mandatory=$true)][string]$DocPath)
$ErrorActionPreference='Stop'
function A([bool]$Ok,[string]$Why){if(-not$Ok){throw $Why}}
function C([string]$Text,[string]$Token){([regex]::Matches($Text,[regex]::Escape($Token))).Count}
function S([string]$Text,[string]$First,[string]$Next){
 $I=$Text.IndexOf($First,[StringComparison]::Ordinal)
 $J=$Text.IndexOf($Next,$I+$First.Length,[StringComparison]::Ordinal)
 A ($I-ge0-and$J-gt$I) "Missing section $First";$Text.Substring($I,$J-$I)
}
function O([string]$Text,[string[]]$Tokens,[string]$Name){
 $Last=-1;foreach($Token in $Tokens){
  $At=$Text.IndexOf($Token,$Last+1,[StringComparison]::Ordinal)
  A ($At-gt$Last) "$Name order changed at $Token";$Last=$At
 }
}
function VerifySource([string]$Source,[string]$Header){
 foreach($Token in @('DISPATCH_LEVEL','cannot migrate','between the hardware check','dependent VMX instruction')){A ($Header-match"(?i)$Token") "Header missing $Token"}
 A ((C $Source 'KeGetCurrentIrql()')-eq1) 'Composition must contain exactly one native IRQL observation'
 A ((C $Source 'VMCS_EXECUTION_CONTEXT_MISMATCH')-eq1) 'Execution-context mismatch marker drifted'
 A ((C $Source '__vmx_vmptrst(')-eq1) 'Composition VMPTRST count drifted'
 $Current=S $Source 'static bool ComposeCurrentVmcs' 'static bool ComposeContext'
 O $Current @('observed = UINT64_MAX','KeGetCurrentIrql() != DISPATCH_LEVEL','VMCS_EXECUTION_CONTEXT_MISMATCH','return false','__vmx_vmptrst(&observed)','observed != expected','VMCS_BINDING_MISMATCH','return false','return true') 'Execution-context/current-VMCS predicate'
 A ($Current-notmatch'(?i)VMCS_(?:EXECUTION_CONTEXT|BINDING)_MISMATCH[^\r\n]*(?:%p|0x%|%I64)') 'Mismatch markers expose a VMCS address'
 $Context=S $Source 'static bool ComposeContext' 'static VMX_V3D_PERSISTENT_IO_RESULT ComposeIoInvalid'
 A ($Context.Contains('ComposeCurrentVmcs(copy->CurrentVmcsPa)')) 'Shared composition does not require the hardened predicate'
 foreach($Boundary in @(
  @('static VMX_V3D_PERSISTENT_IO_RESULT ComposeVmread','static VMX_V3D_PERSISTENT_IO_RESULT ComposeVmwrite'),
  @('static VMX_V3D_PERSISTENT_IO_RESULT ComposeVmwrite','static bool ComposeLaunchReady'),
  @('static bool ComposeLaunchReady','static uint32_t ComposeLaunch'),
  @('static bool ComposeShutdownReady','typedef enum _COMPOSE_RESUME_CR3_REFRESH'))){
  $Part=S $Source $Boundary[0] $Boundary[1]
  A ($Part.Contains('ComposeContext(opaque, true')) "Valid-current boundary bypasses shared predicate: $($Boundary[0])"
 }
}
function VerifyArtifacts([string]$Roadmap,[string]$Gate,[string]$Packager,[string]$Doc){
 foreach($Token in @('Runtime V30','non-migratable','DISPATCH_LEVEL','Gate 69','no repetition gate')){A ($Roadmap-match"(?i)$Token") "Roadmap missing $Token"}
 foreach($Token in @('gate69-vmcs-execution-context','Probe=VMCS_EXECUTION_CONTEXT','VmcsBinding=EXACT_CURRENT','VmcsExecutionAttestations=15','VmcsExecutionLevel=DISPATCH_LEVEL','KernelVmcsContextEvidence=EXTERNAL_KD','OperationsVerified=7','ServiceAfterUnload=ABSENT')){A ($Gate.Contains($Token)) "Gate 69 missing $Token"}
 foreach($Token in @('Gate 69','gate69-vmcs-execution-context','feature/hypervisor-core','SOURCE_COMMIT.txt','SHA256SUMS.txt')){A ($Packager.Contains($Token)) "Gate 69 packager missing $Token"}
 foreach($Token in @('Gate 69','VMCS_EXECUTION_CONTEXT_MISMATCH','VMCS_BINDING_MISMATCH','DISPATCH_LEVEL','fifteen')){A ($Doc-match"(?i)$Token") "Test documentation missing $Token"}
}
$Source=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath))
$Header=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath))
$Roadmap=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $RoadmapPath))
$Gate=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $GatePath))
$Packager=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $PackagerPath))
$Doc=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $DocPath))
VerifySource $Source $Header;VerifyArtifacts $Roadmap $Gate $Packager $Doc
$IrqlBlock=@'
    if (KeGetCurrentIrql() != DISPATCH_LEVEL) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: VMCS_EXECUTION_CONTEXT_MISMATCH\n");
        return false;
    }
    __vmx_vmptrst(&observed);
'@
$LateBlock=@'
    __vmx_vmptrst(&observed);
    if (KeGetCurrentIrql() != DISPATCH_LEVEL) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: VMCS_EXECUTION_CONTEXT_MISMATCH\n");
        return false;
    }
'@
$SourceMutations=@(
 $Source.Replace('KeGetCurrentIrql() != DISPATCH_LEVEL','false'),
 $Source.Replace('KeGetCurrentIrql() != DISPATCH_LEVEL','KeGetCurrentIrql() == DISPATCH_LEVEL'),
 $Source.Replace('KeGetCurrentIrql() != DISPATCH_LEVEL','KeGetCurrentIrql() != PASSIVE_LEVEL'),
 $Source.Replace('VMCS_EXECUTION_CONTEXT_MISMATCH','VMCS_EXECUTION_CONTEXT_WARNING'),
 $Source.Replace($IrqlBlock,$LateBlock),
 $Source.Replace('ComposeCurrentVmcs(copy->CurrentVmcsPa)','true')
)
for($I=0;$I-lt$SourceMutations.Count;$I++){
 A ($SourceMutations[$I]-ne$Source) "Source mutation $I did not apply"
 $Rejected=$false;try{VerifySource $SourceMutations[$I] $Header}catch{$Rejected=$true}
 A $Rejected "Verifier accepted source mutation $I"
}
$ArtifactMutations=@(
 @($Source,$Header.Replace('cannot migrate','may migrate'),$Roadmap,$Gate,$Packager,$Doc),
 @($Source,$Header,$Roadmap.Replace('no repetition gate','a repetition gate'),$Gate,$Packager,$Doc),
 @($Source,$Header,$Roadmap,$Gate.Replace('VmcsExecutionLevel=DISPATCH_LEVEL','VmcsExecutionLevel=PASSIVE_LEVEL'),$Packager,$Doc),
 @($Source,$Header,$Roadmap,$Gate,$Packager.Replace('Gate 69','Gate 68'),$Doc),
 @($Source,$Header,$Roadmap,$Gate,$Packager,$Doc.Replace('VMCS_EXECUTION_CONTEXT_MISMATCH','VMCS_EXECUTION_CONTEXT_WARNING'))
)
for($I=0;$I-lt$ArtifactMutations.Count;$I++){
 $M=$ArtifactMutations[$I];$Rejected=$false
 try{VerifySource $M[0] $M[1];VerifyArtifacts $M[2] $M[3] $M[4] $M[5]}catch{$Rejected=$true}
 A $Rejected "Verifier accepted artifact mutation $I"
}
Write-Output 'Verified Runtime V30 non-migratable DISPATCH_LEVEL current-VMCS execution context, Gate 69 artifacts, and mutations.'
