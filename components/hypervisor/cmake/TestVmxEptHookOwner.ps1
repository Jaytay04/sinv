param([string]$SourcePath,[string]$HeaderPath,[string]$TestPath)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath;$t=Get-Content -Raw $TestPath
function A($x,$m){if(-not $x){throw $m}}
function O($x,$tokens){$at=0;foreach($v in $tokens){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};return $true}
function V($x){
 return (O $x @('VmxEptHookOwnerValidateExternalSpan(','OwnerSeparate(o,v,n)')) -and
  (O $x @('VmxEptHookOwnerQuery(','OwnerSeparate(o,out_id','OwnerSeparate(o,out_stage','Separate(out_id','GuestPhysicalPage==gpa','*out_id=i','*out_stage=o->Slots[i].Record.Stage')) -and
  (O $x @('VmxEptHookOwnerQueryFirst(','OwnerSeparate(o,out_gpa','*out_gpa=o->Slots[i].Record.Plan.GuestPhysicalPage')) -and
  (O $x @('InitializeSeparate(s,a,o)','canonical_allocator=a==&s->Allocator','VmxEptSessionOwnerValidate(s)','allocator=canonical_allocator?s->Allocator:*a','AllocatorValid(&allocator)','!canonical_allocator&&!SessionSeparate(s,a,sizeof(*a))','SessionSeparate(s,o,sizeof(*o))','Zero(o,sizeof(*o))','x.Allocator=allocator','x.AllocatorContract=allocator','x.Slots=allocator.AllocatePool','SessionSeparate(s,x.Slots,bytes)','allocator.FreePool','memset(x.Slots,0,bytes)','x.Magic=VMX_EPT_HOOK_OWNER_MAGIC','*o=x')) -and
  (O $x @('VmxEptHookLocate2Mb','AllocateContiguous','OwnerSeparate(o,pt,VMX_EPT_TABLE_PAGE_SIZE)','VmxEptHookPlan2Mb','VmxEptHookPrepare(&p,o->Session->Storage->Domain.MutationGeneration+1','VmxEptSessionBeginMutation','MutationGeneration!=staged.Record.Generation','o->Slots[free_id]=staged','*ParentEntry(o,&o->Slots[free_id])=p.ReplacementParentEntry','o->ActiveCount++','*out_id=free_id')) -and
  (O $x @('PendingGeneration||o->Session->Storage->Domain.InvalidatedGeneration!=s->Record.Generation','VmxEptHookCommit')) -and
  (O $x @('staged_record=s->Record','VmxEptHookBeginRollback(&staged_record','VmxEptSessionBeginMutation','s->Record=staged_record','OriginalTargetEntry','OriginalParentEntry','*out_id=i')) -and
  (O $x @('VmxEptHookCompleteRollback','FreeContiguous','*s=zero','o->ActiveCount--')) -and
  $x.Contains('if(o->ActiveCount)return VMX_EPT_HOOK_OWNER_BUSY')
}
A (V $s) 'owner ordering'
foreach($x in @('allocation-owned','pairwise-disjoint','owned graph and storage','freezes','allocator for allocation','Commit','CompleteUnhook','Destroy','VmxEptHookOwnerQuery','VmxEptHookOwnerQueryFirst','VmxEptHookOwnerValidateExternalSpan')){A($h.Contains($x))"header $x"}
foreach($x in @('query missing','query prepared','query first','query empty','commit waits','active blocks destroy','duplicate','parent restored','canonical embedded allocator admitted','allocator mutation unwind','frozen allocator cleanup','poison cleanup rejected','session graph output overlap rejected','session graph overlap preserves storage','allocator and owner overlap rejected','overlap preserves caller storage')){A($t.Contains($x))"test $x"}
$m=@(
 $s.Replace('InitializeSeparate(s,a,o)','Separate(s,sizeof(*s),o,sizeof(*o))'),
 $s.Replace('allocator=canonical_allocator?s->Allocator:*a','memset(&allocator,0,sizeof(allocator))'),
 $s.Replace('SessionSeparate(s,o,sizeof(*o))','Separate(s,sizeof(*s),o,sizeof(*o))'),
 $s.Replace('x.Slots=allocator.AllocatePool','x.Slots=a->AllocatePool'),
 $s.Replace('allocator.FreePool(allocator.Context,x.Slots)','a->FreePool(a->Context,x.Slots)'),
 $s.Replace('OwnerSeparate(o,out_id,sizeof(*out_id))','1'),
 $s.Replace('*out_stage=o->Slots[i].Record.Stage',''),
 $s.Replace('OwnerSeparate(o,v,n)','1'),
 $s.Replace('SessionSeparate(s,x.Slots,bytes)','1'),
 $s.Replace('OwnerSeparate(o,pt,VMX_EPT_TABLE_PAGE_SIZE)','1'),
 $s.Replace('VmxEptHookPrepare(&p,o->Session->Storage->Domain.MutationGeneration+1','VmxEptHookPrepare(&p,1'),
 $s.Replace('VmxEptSessionBeginMutation(o->Session)','VMX_EPT_SESSION_OPERATION_OK'),
 $s.Replace('*ParentEntry(o,&o->Slots[free_id])=p.ReplacementParentEntry',''),
 $s.Replace('PendingGeneration||o->Session->Storage->Domain.InvalidatedGeneration!=s->Record.Generation','0'),
 $s.Replace('s->Record=staged_record',''),
 $s.Replace('*ParentEntry(o,s)=s->Record.Plan.OriginalParentEntry',''),
 $s.Replace('if(o->ActiveCount)return VMX_EPT_HOOK_OWNER_BUSY','if(0)return VMX_EPT_HOOK_OWNER_BUSY')
)
foreach($x in $m){A($x-ne$s)'mutation missing';A(-not(V $x))'accepted mutation'}
Write-Host 'PASS: owned EPT hook transaction and query contracts'
