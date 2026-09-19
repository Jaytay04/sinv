param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$ProofHeaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath)
$ErrorActionPreference='Stop'
function A([bool]$c,[string]$m){if(-not $c){throw $m}}
function O([string]$t,[string[]]$tokens){
 $at=0;foreach($token in $tokens){$n=$t.IndexOf($token,$at);A ($n-ge 0) "missing/order $token";$at=$n+$token.Length}}
function V([string]$s,[string]$h,[string]$p,[string]$t){
 foreach($x in @('VMX_PERSISTENT_ROOT_PROOF_VERSION','typedef struct _VMX_PERSISTENT_ROOT_PROOF',
  'Reserved[2]')){A ($p.Contains($x)) "proof $x"}
 A ($p-notmatch 'ntddk|wdm\.h|wdf\.h') 'proof header imports WDK'
 foreach($x in @('VMX_EPT_RUNTIME_ROOT_PLAN_VERSION','AdoptExistingRoot','NativeEnterRequired',
  'snapshot, not a freshness lease','never','second VMXON','preserves out_plan',
  'VmxEptRuntimeRootPlanBuild')){A ($h.Contains($x)) "header $x"}
 A ($s.Contains('VMX_PERSISTENT_OWNER_CONTEXT')) 'owner context'
 A ($s.Contains('VMX_PERSISTENT_OWNER_RUNTIME')) 'owner runtime'
 O $s @('Separate(proof, sizeof(*proof), out_plan, sizeof(*out_plan))',
  'memcpy(&snapshot, proof, sizeof(snapshot))',
  'snapshot.Version != VMX_PERSISTENT_ROOT_PROOF_VERSION',
  'snapshot.ContextOwnedResources != ROOT_CONTEXT_RESOURCES',
  'snapshot.CleanupRequired != 0',
  'snapshot.VmxActive != 1','snapshot.VmxCurrent != 1',
  'snapshot.RecordStage != VMX_PERSISTENT_CPU_ACTIVE',
  'snapshot.CurrentVmcsPhysicalAddress != snapshot.VmcsPhysicalAddress',
  'staged.AdoptExistingRoot = 1','staged.NativeEnterRequired = 0',
  '*out_plan = staged','return VMX_EPT_RUNTIME_ROOT_PLAN_OK')
 A ($s-notmatch '(?i)__vmx|VmxOn\s*\(|VmxEptInstallation|KeSet|Pin\(|IOCTL|Hook|ExAllocate') 'side effect'
 foreach($x in @('SystemIndex=65','valid active proof','no second VMXON',
  'VMX_EPT_RUNTIME_ROOT_PLAN_DIRTY','VMX_EPT_RUNTIME_ROOT_PLAN_NOT_ACTIVE',
  'preserves')){A ($t.Contains($x)) "test $x"}
}
$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath
$p=Get-Content -Raw $ProofHeaderPath;$t=Get-Content -Raw $TestPath
V $s $h $p $t
$m=@(
 $s.Replace('snapshot.CleanupRequired != 0','false'),
 $s.Replace('snapshot.VmxActive != 1','false'),
 $s.Replace('snapshot.VmxCurrent != 1','false'),
 $s.Replace('snapshot.CurrentVmcsPhysicalAddress != snapshot.VmcsPhysicalAddress','false'),
 $s.Replace('staged.AdoptExistingRoot = 1','staged.AdoptExistingRoot = 0'),
 $s.Replace('staged.NativeEnterRequired = 0','staged.NativeEnterRequired = 1'),
 $s.Replace('*out_plan = staged','memset(out_plan,0,sizeof(*out_plan))')
)
$i=0;foreach($x in $m){$i++;A ($x-ne $s) 'vacuous mutation';$bad=$false
 try{V $x $h $p $t}catch{$bad=$true};A $bad "accepted mutation $i"}
Write-Host 'PASS: runtime-root adoption plan contracts verified'
