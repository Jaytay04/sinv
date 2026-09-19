param([string]$Header,[string]$Source,[string]$Test)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not$x){throw $m}}
function O($t,$v,$n){$p=0;foreach($x in $v){$q=$t.IndexOf($x,$p,[StringComparison]::Ordinal);A($q-ge0)"$n $x";$p=$q+$x.Length}}
function V($h,$s,$t){
 A($h.Contains('0x809, METHOD_BUFFERED, FILE_ANY_ACCESS'))'0x809'
 foreach($x in @('ROUNDTRIP_VERSION UINT32_C(1)','ROUNDTRIP_REQUEST_SIZE UINT32_C(16)','ROUNDTRIP_RESPONSE_SIZE UINT32_C(64)','ROUNDTRIP_FLAGS_NONE UINT32_C(0)','ROUNDTRIP_NOT_ATTEMPTED INT32_MIN')){A($h.Contains('HYPERVISOR_EPT_HOOK_'+$x))"header $x"}
 A([regex]::IsMatch($h,'(?s)HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST \{\s*uint32_t Version;\s*uint32_t Size;\s*uint32_t Flags;\s*uint32_t Reserved;'))'request layout'
 foreach($x in @('uint64_t GuestPhysicalPage;','uint32_t HookId;','uint32_t ProcessorCount;','int32_t AddStatus;','int32_t RemoveStatus;','uint32_t AddInvalidationCalls;','uint32_t RemoveInvalidationCalls;','uint64_t Generation;')){A($h.Contains($x))"response $x"}
 foreach($x in @('RESPONSE)==64','RESPONSE,GuestPhysicalPage)==16','RESPONSE,Generation)==56')){A($h.Contains($x))"assert $x"}
 $a=$s.IndexOf('IoctlValidateEptHookRoundtripRequest(');A($a-ge0)'validator';$v=$s.Substring($a)
 O $v @('request == NULL','NULL_INPUT','input_length != sizeof(*request)','INVALID_LENGTH','request->Version !=','INVALID_VERSION','request->Size !=','INVALID_SIZE','request->Flags !=','INVALID_FLAGS','request->Reserved != 0','NONZERO_RESERVED','ROUNDTRIP_VALID')'validator'
 A($v-notmatch'(?i)Wdf|DriverEvt|Owner|Runtime|Vmx|Allocate|Free|DeviceIoControl')'pure validator'
 foreach($x in @('0x80002024','sizeof(request)==16','NOT_ATTEMPTED == INT32_MIN','RESPONSE)==64','GuestPhysicalPage)==16','Generation)==56','INVALID_LENGTH','INVALID_VERSION','INVALID_SIZE','INVALID_FLAGS','NONZERO_RESERVED')){A($t.Contains($x))"test $x"}
}
$h=Get-Content -Raw $Header;$s=Get-Content -Raw $Source;$t=Get-Content -Raw $Test;V $h $s $t
$hm=@($h.Replace('0x809, METHOD_BUFFERED','0x808, METHOD_BUFFERED'),$h.Replace('RESPONSE_SIZE UINT32_C(64)','RESPONSE_SIZE UINT32_C(60)'),$h.Replace('uint64_t Generation;','void* Generation;'))
$sm=@($s.Replace('input_length != sizeof(*request)','input_length < sizeof(*request)'),$s.Replace('request->Version !=','0 !='),$s.Replace('request->Flags !=','0 !='),$s.Replace('request->Reserved != 0','0'))
foreach($m in $hm){A($m-ne$h)'vacuous header mutation';$bad=$false;try{V $m $s $t}catch{$bad=$true};A($bad)'accepted header mutation'}
foreach($m in $sm){A($m-ne$s)'vacuous source mutation';$bad=$false;try{V $h $m $t}catch{$bad=$true};A($bad)'accepted source mutation'}
Write-Host 'PASS: fixed pointer-free 0x809 EPT hook round-trip ABI and exact pure validator'