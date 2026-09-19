param([string]$SourcePath,[string]$HeaderPath,[string]$TestPath)
$ErrorActionPreference='Stop';$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath;$t=Get-Content -Raw $TestPath
function A($x,$m){if(-not $x){throw $m}}
function V($x){$tokens=@('VmxEptDomainValidate(d)!=VMX_EPT_DOMAIN_OK','!OutputSeparate(d,out,sizeof(*out))','Identity.LeafKind!=VMX_EPT_ENTRY_LEAF_2MB','gpa>=d->Identity.MappedBytes','pd_first=1u+d->Plan.PdptPages','pd_page=(uint32_t)(gpa>>30)','s.ParentPageOrdinal=pd_first+pd_page','s.ParentEntryIndex=(uint32_t)((gpa>>21)&UINT64_C(0x1ff))','s.ObservedParentEntry=EntryAt','s.MutationGeneration=d->MutationGeneration','!(s.ObservedParentEntry&LARGE)','*out=s');$at=0;foreach($v in $tokens){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};return $true}
A (V $s) 'locator ordering';A ($h.Contains('No internal pointer escapes')) 'pointer-free contract';foreach($v in @('PD ordinal','generation stale','outside preserves','not large')){A ($t.Contains($v)) "test $v"}
$m=@($s.Replace('Identity.LeafKind!=VMX_EPT_ENTRY_LEAF_2MB','0'),$s.Replace('gpa>=d->Identity.MappedBytes','0'),$s.Replace('pd_page=(uint32_t)(gpa>>30)','pd_page=0'),$s.Replace('s.MutationGeneration=d->MutationGeneration','s.MutationGeneration=0'),$s.Replace('!(s.ObservedParentEntry&LARGE)','0'),$s.Replace('*out=s','memset(out,0,sizeof(*out))'))
foreach($x in $m){A ($x-ne$s) 'mutation missing';A (-not (V $x)) 'accepted mutation'}
Write-Host 'PASS: EPT hook location contracts'
