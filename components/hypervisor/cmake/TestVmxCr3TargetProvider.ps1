param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A($i-ge0-and$j-gt$i)"section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A($q-gt$p)"$n order $x";$p=$q}}
function V($s,$h,$c){
 foreach($x in @('WDK-only capture of a referenced process','Process must remain referenced','Generation is caller-owned','No process pointer is','VmxPersistentProcessCr3TargetCapture','synchronous attach seam','Visitor runs exactly once','VmxPersistentProcessVisitAttached','System-process kernel root','PsInitialSystemProcess is system-lifetime storage','VmxPersistentSystemCr3TargetCapture')){A($h.Contains($x))"header $x"}
 $visit=B $s 'VMX_CR3_TARGET_STATUS VmxPersistentProcessVisitAttached' 'VMX_CR3_TARGET_STATUS VmxPersistentProcessCr3TargetCapture'
 O $visit @('Process == NULL || Context == NULL || Visitor == NULL','KeGetCurrentIrql() != PASSIVE_LEVEL','PsGetCurrentProcess() != Process','KeStackAttachProcess','Visitor(Context)','KeUnstackDetachProcess','VMX_CR3_TARGET_OK') 'visit'
 A(([regex]::Matches($visit,'Visitor\(Context\)')).Count-eq1)'visitor exactly once'
 $capture=B $s 'VMX_CR3_TARGET_STATUS VmxPersistentProcessCr3TargetCapture' 'VMX_CR3_TARGET_STATUS VmxPersistentSystemCr3TargetCapture'
 O $capture @('ProcessObject == NULL || Generation == 0','!Zero(OutTargetSet','KeGetCurrentIrql() != PASSIVE_LEVEL','PhysicalAddressBits(&Bits)','__readcr4() & CR4_PCIDE','VmxPersistentProcessVisitAttached(','Roots[0] = Root & VMX_CR3_TARGET_PAGE_MASK','VmxCr3TargetSetBuild(Bits, Pcide, Roots, UINT32_C(1)','Generation, &Staged','RtlCopyMemory(OutTargetSet','RtlSecureZeroMemory(Roots','VMX_CR3_TARGET_OK') 'capture'
 A(([regex]::Matches($capture,'CaptureCr3')).Count-eq1)'capture visitor once'
 A(([regex]::Matches($capture,'RtlCopyMemory\(OutTargetSet')).Count-eq1)'publish once'
 $system=$s.Substring($s.IndexOf('VMX_CR3_TARGET_STATUS VmxPersistentSystemCr3TargetCapture'))
 O $system @('OutTargetSet == NULL','!Zero(OutTargetSet','PsInitialSystemProcess == NULL','KeQueryPerformanceCounter(NULL)','Counter.QuadPart <= 0','VmxPersistentProcessCr3TargetCapture','PsInitialSystemProcess','Counter.QuadPart','OutTargetSet') 'system wrapper'
 A(([regex]::Matches($c,'(?m)^\s*vmx_cr3_target_provider\.c\s*$')).Count-eq1)'source once'
 A($s-notmatch '(?i)ExAllocate|MmAllocate|IOCTL|Wdf|DriverEntry|OwnerAttach|OwnerStart|RuntimeStart|__vmx|vmread|vmwrite|vmlaunch|vmresume|vmxoff|__writecr')'forbidden side effects'
}
$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath;$c=Get-Content -Raw $DriverCMakePath;V $s $h $c
$m=@(
 $s.Replace('Visitor(Context);','/* visitor removed */'),
 $s.Replace('KeUnstackDetachProcess(&ApcState);','Attached = false;'),
 $s.Replace('Generation == 0','false'),
 $s.Replace('!Zero(OutTargetSet, sizeof(*OutTargetSet))','false'),
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','false'),
 $s.Replace('!PhysicalAddressBits(&Bits)','false'),
 $s.Replace('VmxPersistentProcessVisitAttached(','VmxPersistentProcessVisitAttached_REMOVED('),
 $s.Replace('Root & VMX_CR3_TARGET_PAGE_MASK','Root'),
 $s.Replace('Generation, &Staged','UINT64_C(1), &Staged'),
 $s.Replace('RtlCopyMemory(OutTargetSet, &Staged, sizeof(Staged));','RtlZeroMemory(OutTargetSet, sizeof(*OutTargetSet));'),
 $s.Replace('PsInitialSystemProcess, (uint64_t)Counter.QuadPart','NULL, (uint64_t)Counter.QuadPart'))
$i=0;foreach($x in $m){$i++;A($x-ne$s)"vacuous $i";$bad=$false;try{V $x $h $c}catch{$bad=$true};A($bad)"accepted $i"}
Write-Host 'PASS: the generic PASSIVE-only provider captures one normalized CR3 from a caller-referenced process, while the System wrapper preserves the system-lifetime default without allocation or lifecycle side effects.'
