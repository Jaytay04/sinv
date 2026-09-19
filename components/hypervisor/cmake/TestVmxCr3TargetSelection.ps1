param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath,
 [Parameter(Mandatory=$true)][string]$HypervisorCMake,
 [Parameter(Mandatory=$true)][string]$TestsCMake,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not $x){throw $m}}
function O($t,$tokens,$name){$at=0;foreach($token in $tokens){$n=$t.IndexOf($token,$at,[StringComparison]::Ordinal);A($n-ge0)"$name $token";$at=$n+$token.Length}}
function V($s,$h,$t,$hc,$tc){
 foreach($x in @('VMX_CR3_TARGET_SET_VERSION UINT32_C(1)','VMX_CR3_TARGET_DECISION_VERSION UINT32_C(1)','VMX_CR3_TARGET_MAX_ROOTS UINT32_C(2)','VMX_CR3_TARGET_NO_FLUSH UINT64_C(0x8000000000000000)','typedef struct VMX_CR3_TARGET_SET','uint64_t Roots[VMX_CR3_TARGET_MAX_ROOTS]','typedef struct VMX_CR3_TARGET_DECISION','VmxCr3TargetSetBuild(','VmxCr3TargetSelect(','well-formed non-target operand returns OK','side-effect free','consumed only through the','performs no process lookup')){A($h.Contains($x))"header $x"}
 foreach($x in @('sizeof(VMX_CR3_TARGET_SET) == 48','offsetof(VMX_CR3_TARGET_SET, Roots) == 16','sizeof(VMX_CR3_TARGET_DECISION) == 48','offsetof(VMX_CR3_TARGET_DECISION, Generation) == 32')){A($h.Contains($x))"ABI $x"}
 O $s @('static int PhysicalBitsValid','bits >= 36 && bits <= 52','static int RootValid','root != 0','root & VMX_CR3_TARGET_PCID_MASK','static int SetValid','set->Count > VMX_CR3_TARGET_MAX_ROOTS','set->PcideEnabled > 1','set->Generation == 0','set->Roots[0] != set->Roots[1]') 'validation'
 $ba=$s.IndexOf('VMX_CR3_TARGET_STATUS VmxCr3TargetSetBuild(');$sa=$s.IndexOf('VMX_CR3_TARGET_STATUS VmxCr3TargetSelect(');A($ba-ge0-and$sa-gt$ba)'function bounds';$build=$s.Substring($ba,$sa-$ba);$select=$s.Substring($sa)
 O $build @('!Zero(out_set, sizeof(*out_set))','root_bytes = (size_t)count * sizeof(*roots)','Separate(roots, root_bytes, out_set, sizeof(*out_set))','PhysicalMask(physical_address_bits)','RootValid(roots[index], physical_mask)','roots[0] == roots[1]','staged.Version = VMX_CR3_TARGET_SET_VERSION','staged.Roots[index] = roots[index]','staged.Generation = generation','SetValid(&staged)','*out_set = staged','return VMX_CR3_TARGET_OK') 'build'
 O $select @('SetValid(set)','Separate(set, sizeof(*set), out_decision, sizeof(*out_decision))','Zero(out_decision, sizeof(*out_decision))','allowed_mask = physical_mask','allowed_mask |= VMX_CR3_TARGET_NO_FLUSH','(operand & ~allowed_mask) != 0','!set->PcideEnabled && (operand & VMX_CR3_TARGET_PCID_MASK) != 0','root = operand & physical_mask & VMX_CR3_TARGET_PAGE_MASK','staged.MatchedIndex = UINT32_MAX','staged.Pcid = set->PcideEnabled','staged.NoFlush = set->PcideEnabled','if (root == set->Roots[index])','staged.Matched = 1','staged.MatchedIndex = index','*out_decision = staged','return VMX_CR3_TARGET_OK') 'select'
 A($s-notmatch '(?i)ntddk|wdm\.h|wdf\.h|__vmx|vmread|vmwrite|invept|Ke[A-Z]|ExAllocate|IOCTL|Hook|PsLookup|EPROCESS|DirectoryTableBase')'pure source'
 foreach($x in @('KVA pair build','PCID decoded','no-flush decoded','non-target unmatched','physical bits low','physical bits high','duplicate roots','alias roots/output','PCID while disabled','no-flush while disabled','reserved operand high bit','preserves')){A($t.Contains($x))"test $x"}
 A(([regex]::Matches($hc,'(?m)^\s*vmx_cr3_target_selection\.c\s*$')).Count-eq1)'source once'
 A(([regex]::Matches($tc,'add_test\(NAME test_vmx_cr3_target_selection COMMAND')).Count-eq1)'unit test once'
 A(([regex]::Matches($tc,'test_vmx_cr3_target_selection_contracts')).Count-eq1)'contract test once'
}
$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath;$t=Get-Content -Raw $TestPath;$hc=Get-Content -Raw $HypervisorCMake;$tc=Get-Content -Raw $TestsCMake
V $s $h $t $hc $tc
foreach($path in $ForbiddenPaths.Split(',')){A((Get-Content -Raw $path)-notmatch 'VmxCr3Target|vmx_cr3_target_selection')"unwired $path"}
$mut=@(
 $s.Replace('bits >= 36 && bits <= 52','bits >= 12 && bits <= 63'),
 $s.Replace('!Zero(out_set, sizeof(*out_set))','false'),
 $s.Replace('Separate(roots, root_bytes, out_set, sizeof(*out_set))','true'),
 $s.Replace('count == 2 && roots[0] == roots[1]','false'),
 $s.Replace('(operand & ~allowed_mask) != 0','false'),
 $s.Replace('!set->PcideEnabled && (operand & VMX_CR3_TARGET_PCID_MASK) != 0','false'),
 $s.Replace('root = operand & physical_mask & VMX_CR3_TARGET_PAGE_MASK','root = operand'),
 $s.Replace('if (root == set->Roots[index])','if (true)'),
 $s.Replace('staged.MatchedIndex = UINT32_MAX','staged.MatchedIndex = 0'),
 $s.Replace('*out_decision = staged','memset(out_decision,0,sizeof(*out_decision))')
)
$i=0;foreach($m in $mut){$i++;A($m-ne$s)"vacuous mutation $i";$bad=$false;try{V $m $h $t $hc $tc}catch{$bad=$true};A($bad)"accepted mutation $i"}
Write-Host 'PASS: pure CR3 target selection validates trusted one/two-root sets, PCID/no-flush normalization, exact matching, output preservation, and is consumed only through the authenticated route owner.'