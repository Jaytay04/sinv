param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n $x";$p=$q}}
function V([string]$s,[string]$h,[string]$t){
 foreach($x in @('LOADER_EPT_WIRE_DETACHED','LOADER_EPT_WIRE_READY','LOADER_EPT_WIRE_RESIDUAL','LOADER_EPT_WIRE_MALFORMED','LOADER_EPT_WIRE_SHORT','LoaderClassifyEptStatus')){A ($h.Contains($x)) "header $x"}
 $a=$s.IndexOf('LOADER_EPT_WIRE_CLASS LoaderClassifyEptStatus(');$b=$s.IndexOf('LOADER_PERSISTENT_STATUS_PROOF LoaderValidateDetachedStatus(',$a);A ($a-ge0-and$b-gt$a) 'classifier section';$c=$s.Substring($a,$b-$a)
 O $c @('status_bytes == NULL','status_size < sizeof(status)','status_size != sizeof(status)','memcpy(&status','status.Version != HYPERVISOR_EPT_STATUS_VERSION','status.Size != HYPERVISOR_EPT_STATUS_RESPONSE_SIZE','status.RuntimePhase > 4u','status.EptStage > 4u','~known_flags','EptEvidenceValid','status.PhaseQueryStatus','HYPERVISOR_EPT_STATUS_BLOCKED','status.EptValidateStatus','HYPERVISOR_EPT_STATUS_MALFORMED','status.EptStage == 4u','status.EptStage == 1u','HYPERVISOR_EPT_STATUS_READY','HYPERVISOR_EPT_STATUS_TEARDOWN_PENDING','HYPERVISOR_EPT_STATUS_FLAG_CLEANUP_REQUIRED','status.Flags != expected_flags','status.Classification != expected_class','LOADER_EPT_WIRE_DETACHED','LOADER_EPT_WIRE_READY','LOADER_EPT_WIRE_RESIDUAL') 'classifier order'
 A ($c-notmatch 'DeviceIoControl|CreateThread|Wdf|SC_HANDLE|VmxEptPersistent') 'classifier is pure'
 foreach($x in @('TEST_RUN(TestEptStatusCanonical);','TEST_RUN(TestEptStatusResidual);','TEST_RUN(TestEptStatusMalformed);','canonical EPT detached','canonical EPT ready','unaligned EPT status','bounded EPT evidence accepted','TestEptStatusResidual','EPT teardown pending','EPT blocked','authenticated contradiction is residual','TestEptStatusMalformed','null EPT status','short EPT status','oversize EPT status','EPT version','EPT size','EPT class mismatch','EPT unknown flag','EPT phase range','EPT stage range','hidden start evidence','start call range','diagnostic range','destroy call range')){A ($t.Contains($x)) "test $x"}
}
$s=[IO.File]::ReadAllText((Resolve-Path $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath));$t=[IO.File]::ReadAllText((Resolve-Path $TestPath));V $s $h $t
$sm=@(
 $s.Replace('status_size != sizeof(status)','status_size < sizeof(status)'),
 $s.Replace('status.Version != HYPERVISOR_EPT_STATUS_VERSION','status.Version == HYPERVISOR_EPT_STATUS_VERSION'),
 $s.Replace('status.RuntimePhase > 4u','status.RuntimePhase > 5u'),
 $s.Replace('!EptEvidenceValid(&status)','0'),
 $s.Replace('status.Flags != expected_flags','status.Flags == expected_flags'),
 $s.Replace('status.Classification != expected_class','status.Classification == expected_class'),
 $s.Replace('return LOADER_EPT_WIRE_RESIDUAL;','return LOADER_EPT_WIRE_READY;')
)
foreach($m in $sm){A ($m-ne$s) 'vacuous source mutation';$bad=$false;try{V $m $h $t}catch{$bad=$true};A $bad 'accepted source mutation'}
$tm=@(
 $t.Replace('TEST_RUN(TestEptStatusCanonical);',''),
 $t.Replace('TEST_RUN(TestEptStatusResidual);',''),
 $t.Replace('TEST_RUN(TestEptStatusMalformed);','')
)
foreach($m in $tm){A ($m-ne$t) 'vacuous test mutation';$bad=$false;try{V $s $h $m}catch{$bad=$true};A $bad 'accepted test mutation'}
'Verified pure fixed EPT wire classifier, canonical READY/DETACHED proof, residual rejection, unaligned reads, and mutation coverage.'