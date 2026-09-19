param([string]$SourcePath,[string]$HeaderPath,[string]$AbiPath,[string]$TestPath,[string]$HypervisorCMake,[string]$TestsCMake,[string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not $x){throw $m}}
$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath;$a=Get-Content -Raw $AbiPath;$t=Get-Content -Raw $TestPath;$hc=Get-Content -Raw $HypervisorCMake;$tc=Get-Content -Raw $TestsCMake
function V($x){foreach($q in @('TargetValid','IdentityValid','StateValid','s->Busy','s->Reserved32','s->Reserved','TransitionSequence','UINT64_MAX-transitions','memcpy(out,&h','HYPERVISOR_CR3_STATUS_FLAG_AUTHENTICATED')){A($x.Contains($q)) "source $q"}}
V $s
foreach($q in @('pointer-free','CR3 roots are redacted','HYPERVISOR_CR3_STATUS_HEADER','HYPERVISOR_CR3_STATUS_RECORD','sizeof(HYPERVISOR_CR3_STATUS_HEADER)==64','sizeof(HYPERVISOR_CR3_STATUS_RECORD)==32')){A($a.Contains($q)) "ABI $q"}
foreach($q in @('Before source/output','No partial wire image','VmxCr3PublicStatusEncode')){A($h.Contains($q)) "header $q"}
foreach($q in @('#define N 65','root redacted','duplicate','busy','alias preserves')){A($t.Contains($q)) "test $q"}
A(($hc|Select-String 'vmx_cr3_public_status.c').Count-eq1) 'source once'
A($tc.Contains('add_executable(test_vmx_cr3_public_status')) 'test executable'
A($tc.Contains('add_test(NAME test_vmx_cr3_public_status_contracts')) 'test contract wiring'
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw $p)-notmatch 'VmxCr3PublicStatusEncode') "forbidden $p"}
foreach($m in @($s.Replace('s->Busy','0'),$s.Replace('UINT64_MAX-transitions','UINT64_MAX'),$s.Replace('memcpy(out,&h','memcpy((char*)out+1,&h'))){$ok=$false;try{V $m;$ok=$true}catch{};A(-not $ok) 'accepted mutation'}
'CR3 public status contract PASS'