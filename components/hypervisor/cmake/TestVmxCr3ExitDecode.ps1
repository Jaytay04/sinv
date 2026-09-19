param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath,
 [Parameter(Mandatory=$true)][string]$HypervisorCMake,
 [Parameter(Mandatory=$true)][string]$TestsCMake,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not$x){throw$m}}
function O($t,$tokens,$name){$at=0;foreach($token in $tokens){$n=$t.IndexOf($token,$at,[StringComparison]::Ordinal);A($n-ge0)"$name $token";$at=$n+$token.Length}}
function V($s,$h,$t,$hc,$tc){
 foreach($x in @('VMX_CR3_EXIT_DECISION_VERSION UINT32_C(1)','VMX_CR3_EXIT_GPR_COUNT UINT32_C(16)','VMX_CR3_EXIT_QUAL_ALLOWED_MASK UINT64_C(0x0000000000000f3f)','VMX_CR3_EXIT_ACTION_WRITE_GUEST_CR3_ADVANCE','typedef struct VMX_CR3_EXIT_DECISION','VMX_CR3_TARGET_DECISION Target','VmxCr3ExitDecode(','performs no VMREAD, VMWRITE, RIP advance')){A($h.Contains($x))"header $x"}
 foreach($x in @('sizeof(VMX_CR3_EXIT_DECISION) == 72','offsetof(VMX_CR3_EXIT_DECISION, Qualification) == 16','offsetof(VMX_CR3_EXIT_DECISION, Target) == 24')){A($h.Contains($x))"ABI $x"}
 O $s @('GuestGprCount != VMX_CR3_EXIT_GPR_COUNT','!Zero(OutDecision, sizeof(*OutDecision))','Separate(TargetSet, sizeof(*TargetSet)','Separate(GuestGprs, GprBytes','(Qualification & ~VMX_CR3_EXIT_QUAL_ALLOWED_MASK) != 0','CrNumber != VMX_CR3_EXIT_CR3','Access != VMX_CR3_EXIT_MOV_TO_CR','GprIndex =','VmxCr3TargetSelect(TargetSet, GuestGprs[GprIndex], &Target)','Status != VMX_CR3_TARGET_OK','Staged.Action = VMX_CR3_EXIT_ACTION_WRITE_GUEST_CR3_ADVANCE','Staged.Target = Target','*OutDecision = Staged','return VMX_CR3_TARGET_OK') 'decode'
 A($s-notmatch '(?i)ntddk|wdm\.h|wdf\.h|__vmx|vmread|vmwrite|invept|Ke[A-Z]|ExAllocate|IOCTL|Hook|VMCS_|Dispatcher')'pure source'
 foreach($x in @('DecodeEveryGpr','Index < VMX_CR3_EXIT_GPR_COUNT','GPR/output alias','set/output alias','CR0 access','MOV-from CR3','reserved bit 6','reserved LMSW source','reserved high qualification','short GPR file','non-target unmatched','corrupt set preserves')){A($t.Contains($x))"test $x"}
 A(([regex]::Matches($hc,'(?m)^\s*vmx_cr3_exit_decode\.c\s*$')).Count-eq1)'source once'
 A(([regex]::Matches($tc,'add_test\(NAME test_vmx_cr3_exit_decode COMMAND')).Count-eq1)'unit once'
 A(([regex]::Matches($tc,'test_vmx_cr3_exit_decode_contracts')).Count-eq1)'contract once'
}
$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath;$t=Get-Content -Raw $TestPath;$hc=Get-Content -Raw $HypervisorCMake;$tc=Get-Content -Raw $TestsCMake
V $s $h $t $hc $tc
foreach($path in $ForbiddenPaths.Split(',')){A((Get-Content -Raw $path)-notmatch 'VmxCr3ExitDecode|vmx_cr3_exit_decode')"unwired $path"}
$mut=@($s.Replace('GuestGprCount != VMX_CR3_EXIT_GPR_COUNT','0'),$s.Replace('(Qualification & ~VMX_CR3_EXIT_QUAL_ALLOWED_MASK) != 0','0'),$s.Replace('CrNumber != VMX_CR3_EXIT_CR3','0'),$s.Replace('Access != VMX_CR3_EXIT_MOV_TO_CR','0'),$s.Replace('GuestGprs[GprIndex]','GuestGprs[0]'),$s.Replace('Status != VMX_CR3_TARGET_OK','0'),$s.Replace('VMX_CR3_EXIT_ACTION_WRITE_GUEST_CR3_ADVANCE','VMX_CR3_EXIT_ACTION_INVALID'),$s.Replace('*OutDecision = Staged','memset(OutDecision,0,sizeof(*OutDecision))'))
$i=0;foreach($m in $mut){$i++;A($m-ne$s)"vacuous $i";$bad=$false;try{V $m $h $t $hc $tc}catch{$bad=$true};A($bad)"accepted $i"}
Write-Host 'PASS: pure CR3 exit decode validates MOV-to-CR3 qualification, all 16 GPR operands, exact target selection, output preservation, and remains unwired.'