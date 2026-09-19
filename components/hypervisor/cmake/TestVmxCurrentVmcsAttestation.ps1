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
 foreach($Token in @('exact 32-byte Basic context','VMPTRST','hardware','exact tracked per-processor VMCS')){
  A ($Header-match"(?i)$Token") "Header contract missing $Token"
 }
 A ((C $Source '__vmx_vmptrst(')-eq1) 'Composition must contain exactly one VMPTRST site'
 A ((C $Source 'VMCS_BINDING_MISMATCH')-eq1) 'Composition must contain exactly one mismatch marker site'
 A ((C $Source 'VMCS_BINDING_ATTESTED phase=launch state=0')-eq1) 'Launch attestation marker drifted'
 A ((C $Source 'VMCS_BINDING_ATTESTED phase=resume state=%lu')-eq1) 'Resume attestation marker drifted'
 $Current=S $Source 'static bool ComposeCurrentVmcs' 'static bool ComposeContext'
 $Context=S $Source 'static bool ComposeContext' 'static VMX_V3D_PERSISTENT_IO_RESULT ComposeIoInvalid'
 $Launch=S $Source 'static uint32_t ComposeLaunch' 'static bool ComposeShutdownReady'
 $Shutdown=S $Source 'static uint32_t ComposeShutdown' 'static bool ComposeRequired'
 O $Current @('observed = UINT64_MAX','__vmx_vmptrst(&observed)','observed != expected','VMCS_BINDING_MISMATCH','return false','return true') 'VMPTRST predicate'
 A ($Current-notmatch'(?i)VMCS_BINDING_MISMATCH[^\r\n]*(?:%p|0x%|%I64)') 'Mismatch marker must not expose a VMCS address'
 O $Context @('RtlCopyMemory(','copy->CurrentVmcsValid == 0','!require_current && copy->CurrentVmcsPa == 0','copy->CurrentVmcsPa != 0','COMPOSE_PAGE_MASK','ComposeCurrentVmcs(copy->CurrentVmcsPa)') 'Shared composition gate'
 O $Launch @('ComposeLaunchReady(','VMCS_BINDING_ATTESTED phase=launch state=0','VmxV3dPersistentLaunch(') 'Launch attestation'
 O $Shutdown @('ComposeShutdownReady(','VMCS_BINDING_ATTESTED phase=resume state=%lu','ComposeRefreshResumeCr3()','VmxV3dPersistentLaunch(') 'Resume attestation'
}
function VerifyArtifacts([string]$Roadmap,[string]$Gate,[string]$Packager,[string]$Doc){
 foreach($Token in @('Runtime V29','VMPTRST','fifteen attestations total','no repetition gate')){A ($Roadmap-match"(?i)$Token") "Roadmap missing $Token"}
 foreach($Token in @('gate68-vmcs-binding-attestation','Probe=VMCS_BINDING_ATTESTATION','VmcsBinding=EXACT_CURRENT','VmcsBindingAttestations=15','KernelVmcsEvidence=EXTERNAL_KD','OperationsVerified=7','ServiceAfterUnload=ABSENT')){A ($Gate.Contains($Token)) "Gate 68 missing $Token"}
 foreach($Command in @("'ept-violation-roundtrip'","'cpuid-probe'","'msr-transparency-probe'","'tsc-probe'","'invlpg-probe'","'cet-vmcs-probe'","'hlt-probe'")){A ((C $Gate $Command)-ge1) "Gate 68 missing command $Command"}
 foreach($Token in @('Gate 68','gate68-vmcs-binding-attestation','feature/hypervisor-core','SOURCE_COMMIT.txt','SHA256SUMS.txt')){A ($Packager.Contains($Token)) "Gate 68 packager missing $Token"}
 foreach($Token in @('Gate 68','VMCS_BINDING_ATTESTED phase=launch state=0','VMCS_BINDING_ATTESTED phase=resume state=<decimal>','VMCS_BINDING_MISMATCH','fifteen')){A ($Doc-match"(?i)$Token") "Test documentation missing $Token"}
}
$Source=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath))
$Header=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath))
$Roadmap=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $RoadmapPath))
$Gate=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $GatePath))
$Packager=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $PackagerPath))
$Doc=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $DocPath))
VerifySource $Source $Header;VerifyArtifacts $Roadmap $Gate $Packager $Doc
$SourceMutations=@(
 $Source.Replace('__vmx_vmptrst(&observed);','observed = expected;'),
 $Source.Replace('observed != expected','false'),
 $Source.Replace('ComposeCurrentVmcs(copy->CurrentVmcsPa)','true'),
 $Source.Replace('observed = UINT64_MAX','observed = expected'),
 $Source.Replace('VMCS_BINDING_ATTESTED phase=launch state=0','VMCS_BINDING_ATTESTED phase=launch'),
 $Source.Replace('VMCS_BINDING_ATTESTED phase=resume state=%lu','VMCS_BINDING_ATTESTED phase=resume'),
 $Source.Replace('hypersinv: VMCS_BINDING_MISMATCH\n','hypersinv: VMCS_BINDING_MISMATCH expected=0x%I64X\n')
)
for($I=0;$I-lt$SourceMutations.Count;$I++){
 A ($SourceMutations[$I]-ne$Source) "Source mutation $I did not apply"
 $Rejected=$false;try{VerifySource $SourceMutations[$I] $Header}catch{$Rejected=$true}
 A $Rejected "Verifier accepted source mutation $I"
}
$ArtifactMutations=@(
 @($Source,$Header.Replace('exact 32-byte Basic context','exact 24-byte Basic context'),$Roadmap,$Gate,$Packager,$Doc),
 @($Source,$Header,$Roadmap.Replace('fifteen attestations total','fourteen attestations total'),$Gate,$Packager,$Doc),
 @($Source,$Header,$Roadmap,$Gate.Replace('VmcsBindingAttestations=15','VmcsBindingAttestations=14'),$Packager,$Doc),
 @($Source,$Header,$Roadmap,$Gate,$Packager.Replace('Gate 68','Gate 67'),$Doc),
 @($Source,$Header,$Roadmap,$Gate,$Packager,$Doc.Replace('VMCS_BINDING_MISMATCH','VMCS_BINDING_WARNING'))
)
for($I=0;$I-lt$ArtifactMutations.Count;$I++){
 $M=$ArtifactMutations[$I];$Rejected=$false
 try{VerifySource $M[0] $M[1];VerifyArtifacts $M[2] $M[3] $M[4] $M[5]}catch{$Rejected=$true}
 A $Rejected "Verifier accepted artifact mutation $I"
}
Write-Output 'Verified Runtime V29 exact current-VMCS VMPTRST attestation, boundary evidence, Gate 68 packaging/docs, and mutations.'
