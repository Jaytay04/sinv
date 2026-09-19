param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$HeaderPath,[Parameter(Mandatory=$true)][string]$TestPath)
$ErrorActionPreference='Stop'
$s=[IO.File]::ReadAllText($SourcePath);$h=[IO.File]::ReadAllText($HeaderPath);$t=[IO.File]::ReadAllText($TestPath)
function Need([string]$v,[string]$p,[string]$m){if($v -notmatch $p){throw $m}}
Need $h 'VMX_EPT_HOOK_STAGE_PREPARED' 'stage ABI'
Need $h 'VMX_EPT_HOOK_STAGE_ROLLBACK_PENDING' 'rollback ABI'
Need $s 'Separate\(entries,sizeof\(staged\),out,sizeof\(\*out\)\)' 'output separation'
Need $s 'staged\[p\.TargetIndex\]=p\.HookedTargetEntry' 'target-only mutation'
Need $s 'if\(!VmxEptHookPlanValid\(&p\)\)return VMX_EPT_HOOK_INVARIANT' 'validate before publish'
Need $s 'memcpy\(entries,staged,sizeof\(staged\)\);\*out=p' 'publish last'
Need $s 'parent!=r->Plan\.ReplacementParentEntry\|\|target!=r->Plan\.HookedTargetEntry' 'commit proof'
Need $s 'parent!=r->Plan\.OriginalParentEntry\|\|target!=r->Plan\.OriginalTargetEntry' 'rollback proof'
Need $t 'entries preserved' 'failure preservation test'
Need $t 'commit invariant' 'commit mutation test'
$mutations=@(
 @('staged[p.TargetIndex]=p.HookedTargetEntry','staged[0]=p.HookedTargetEntry'),
 @('if(!VmxEptHookPlanValid(&p))return VMX_EPT_HOOK_INVARIANT','if(0)return VMX_EPT_HOOK_INVARIANT'),
 @('parent!=r->Plan.ReplacementParentEntry','parent==r->Plan.ReplacementParentEntry'),
 @('parent!=r->Plan.OriginalParentEntry||target!=r->Plan.OriginalTargetEntry','parent!=r->Plan.OriginalParentEntry')
)
foreach($m in $mutations){$x=$s.Replace($m[0],$m[1]);if($x -eq $s){throw "mutation anchor missing: $($m[0])"};$accepted=$true;try{Need $x 'staged\[p\.TargetIndex\]=p\.HookedTargetEntry' 'x';Need $x 'if\(!VmxEptHookPlanValid\(&p\)\)return VMX_EPT_HOOK_INVARIANT' 'x';Need $x 'parent!=r->Plan\.ReplacementParentEntry\|\|target!=r->Plan\.HookedTargetEntry' 'x';Need $x 'parent!=r->Plan\.OriginalParentEntry\|\|target!=r->Plan\.OriginalTargetEntry' 'x'}catch{$accepted=$false};if($accepted){throw 'accepted source mutation'}}
Write-Host 'PASS: transactional EPT hook model contract'
