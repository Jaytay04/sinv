param([Parameter(Mandatory=$true)][string]$SourcePath,
      [Parameter(Mandatory=$true)][string]$HeaderPath,
      [Parameter(Mandatory=$true)][string]$AbiPath,
      [Parameter(Mandatory=$true)][string]$PublicSource,
      [Parameter(Mandatory=$true)][string]$TestPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n $x";$p=$q}}
function V([string]$s,[string]$h,[string]$abi,[string]$p,[string]$t){
 foreach($x in @('LOADER_CR3_WIRE_OBSERVED','LOADER_CR3_WIRE_INACTIVE','LOADER_CR3_WIRE_MALFORMED','LOADER_CR3_WIRE_SHORT','LOADER_CR3_WIRE_OVERFLOW','LoaderClassifyCr3Status')){A ($h.Contains($x)) "header $x"}
 foreach($x in @('HYPERVISOR_CR3_STATUS_MAX_TARGETS UINT32_C(2)','HYPERVISOR_CR3_STATUS_MAX_PCID UINT32_C(0xfff)','HYPERVISOR_CR3_STATUS_MIN_PHYSICAL_BITS UINT32_C(36)','HYPERVISOR_CR3_STATUS_MAX_PHYSICAL_BITS UINT32_C(52)')){A ($abi.Contains($x)) "ABI $x"}
 A ($p.Contains('HYPERVISOR_CR3_STATUS_MAX_TARGETS==VMX_CR3_TARGET_MAX_ROOTS')-and$p.Contains('HYPERVISOR_CR3_STATUS_MAX_PCID==VMX_CR3_TARGET_PCID_MASK')) 'producer drift pins'
 $a=$s.IndexOf('LOADER_CR3_WIRE_CLASS LoaderClassifyCr3Status(');$b=$s.IndexOf('LOADER_PERSISTENT_STATUS_PROOF LoaderValidateDetachedStatus(',$a);A ($a-ge0-and$b-gt$a) 'classifier section';$c=$s.Substring($a,$b-$a)
 O $c @('status_bytes == NULL','status_size < sizeof(header)','memcpy(&header','header.ContextCount >','return LOADER_CR3_WIRE_OVERFLOW','status_size < total','status_size != total','header.Version != HYPERVISOR_CR3_STATUS_VERSION','header.HeaderSize != HYPERVISOR_CR3_STATUS_HEADER_SIZE','header.RecordSize != HYPERVISOR_CR3_STATUS_RECORD_SIZE','header.TotalSize != total','header.OwnerGeneration == 0u','header.TargetGeneration == 0u','header.ContextCount == 0u','header.TargetCount > HYPERVISOR_CR3_STATUS_MAX_TARGETS','header.PhysicalAddressBits < HYPERVISOR_CR3_STATUS_MIN_PHYSICAL_BITS','header.PhysicalAddressBits > HYPERVISOR_CR3_STATUS_MAX_PHYSICAL_BITS','HYPERVISOR_CR3_STATUS_FLAG_AUTHENTICATED','header.Reserved32 != 0u','memcpy(&record','record.SystemIndex != index','record.Number >= 64u','record.Resident > 1u','record.MatchedIndex != HYPERVISOR_CR3_STATUS_NO_INDEX','record.MatchedIndex >= header.TargetCount','previous.Group == record.Group','previous.Number == record.Number','UINT64_MAX - transitions','transitions != header.TotalTransitions','LOADER_CR3_WIRE_OBSERVED','LOADER_CR3_WIRE_INACTIVE') 'classifier order'
 A ($c-notmatch 'DeviceIoControl|CreateThread|Wdf|SC_HANDLE|OpenService|Vmx') 'classifier pure'
 A ($c.Contains('return transitions != 0u ? LOADER_CR3_WIRE_OBSERVED')) 'observed transition gate'
 foreach($x in @('TEST_RUN(TestCr3StatusClassifier);','TEST_RUN(TestCr3StatusMultiGroup);','TEST_RUN(TestCr3StatusMalformed);','canonical inactive CR3 status','canonical observed CR3 status','unaligned CR3 wire','65 CPU multi-group CR3 status','duplicate CR3 identity rejected','CR3 wire exceeds uint32','wire.Header.TargetCount = 3u','wire.Records[0].MatchedIndex = 0u','wire.Header.TotalTransitions = 1u')){A ($t.Contains($x)) "test $x"}
}
$s=[IO.File]::ReadAllText((Resolve-Path $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath));$abi=[IO.File]::ReadAllText((Resolve-Path $AbiPath));$p=[IO.File]::ReadAllText((Resolve-Path $PublicSource));$t=[IO.File]::ReadAllText((Resolve-Path $TestPath));V $s $h $abi $p $t
$sm=@($s.Replace('status_size != total','status_size < total'),$s.Replace('header.TargetCount > HYPERVISOR_CR3_STATUS_MAX_TARGETS','false'),$s.Replace('record.SystemIndex != index','false'),$s.Replace('record.MatchedIndex >= header.TargetCount','false'),$s.Replace('previous.Number == record.Number','false'),$s.Replace('transitions != header.TotalTransitions','false'),$s.Replace('return transitions != 0u ? LOADER_CR3_WIRE_OBSERVED','return false ? LOADER_CR3_WIRE_OBSERVED'))
foreach($m in $sm){A ($m-ne$s) 'vacuous source mutation';$bad=$false;try{V $m $h $abi $p $t}catch{$bad=$true};A $bad 'accepted source mutation'}
$tm=@($t.Replace('TEST_RUN(TestCr3StatusClassifier);',''),$t.Replace('TEST_RUN(TestCr3StatusMultiGroup);',''),$t.Replace('TEST_RUN(TestCr3StatusMalformed);',''))
foreach($m in $tm){A ($m-ne$t) 'vacuous test mutation';$bad=$false;try{V $s $h $abi $p $m}catch{$bad=$true};A $bad 'accepted test mutation'}
'Verified pure redacted CR3 classifier: exact sizing, overflow, N=65 identities, state/PCID/aggregate invariants, drift pins, and mutations.'