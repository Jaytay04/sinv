param([Parameter(Mandatory=$true)][string]$HeaderPath,
      [Parameter(Mandatory=$true)][string]$ValidatorPath,
      [Parameter(Mandatory=$true)][string]$TestPath,
      [Parameter(Mandatory=$true)][string]$DriverPath,
      [Parameter(Mandatory=$true)][string]$LoaderPath)
$ErrorActionPreference='Stop'
function A([bool]$c,[string]$m){if(-not$c){throw $m}}
function O([string]$s,[string[]]$tokens,[string]$m){$p=-1;foreach($t in $tokens){$n=$s.IndexOf($t,$p+1);A ($n-gt$p) "$m missing/order: $t";$p=$n}}
function V([string]$h,[string]$s,[string]$t){
 A ($h.Contains('#define IOCTL_HYPERVISOR_CR3_STATUS_QUERY ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x80B, METHOD_BUFFERED, FILE_ANY_ACCESS))')) '0x80B IOCTL'
 foreach($x in @('QUERY_VERSION UINT32_C(1)','QUERY_REQUEST_SIZE UINT32_C(16)','QUERY_FLAGS_NONE UINT32_C(0)')){A ($h.Contains('HYPERVISOR_CR3_STATUS_'+$x)) "header $x"}
 $layout='(?s)typedef struct _HYPERVISOR_CR3_STATUS_QUERY_REQUEST \{\s*uint32_t Version;\s*uint32_t Size;\s*uint32_t Flags;\s*uint32_t Reserved;\s*\} HYPERVISOR_CR3_STATUS_QUERY_REQUEST;'
 A ([regex]::IsMatch($h,$layout)) 'exact pointer-free request'
 foreach($x in @('REQUEST)==16','REQUEST)==4','REQUEST,Version)==0','REQUEST,Size)==4','REQUEST,Flags)==8','REQUEST,Reserved)==12')){A ($h.Contains($x)) "ABI $x"}
 A ($t.Contains('0x8000202C')-and$t.Contains('0x80002028')) 'disjoint values pinned'
 $a=$s.IndexOf('IoctlValidateCr3StatusQueryRequest(');A ($a-ge0) 'validator missing';$v=$s.Substring($a)
 O $v @('if (request == NULL)','return HYPERVISOR_CR3_STATUS_QUERY_NULL_INPUT;','input_length != sizeof(*request)','return HYPERVISOR_CR3_STATUS_QUERY_INVALID_LENGTH;','request->Version != HYPERVISOR_CR3_STATUS_QUERY_VERSION','return HYPERVISOR_CR3_STATUS_QUERY_INVALID_VERSION;','request->Size != HYPERVISOR_CR3_STATUS_QUERY_REQUEST_SIZE','return HYPERVISOR_CR3_STATUS_QUERY_INVALID_SIZE;','request->Flags != HYPERVISOR_CR3_STATUS_QUERY_FLAGS_NONE','return HYPERVISOR_CR3_STATUS_QUERY_INVALID_FLAGS;','request->Reserved != 0','return HYPERVISOR_CR3_STATUS_QUERY_NONZERO_RESERVED;','return HYPERVISOR_CR3_STATUS_QUERY_VALID;') 'validator'
 A (-not$v.Contains('input_length <')-and-not$v.Contains('input_length >')) 'exact length only'
 A ($v-notmatch'(?i)Wdf|DriverEvt|Owner|Runtime|VMXON|VMLAUNCH|VMXOFF|Allocate|Free') 'pure validator'
}
$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath));$s=[IO.File]::ReadAllText((Resolve-Path $ValidatorPath));$t=[IO.File]::ReadAllText((Resolve-Path $TestPath));V $h $s $t
$driver=[IO.File]::ReadAllText((Resolve-Path $DriverPath));$loader=[IO.File]::ReadAllText((Resolve-Path $LoaderPath))
$hm=@($h.Replace('0x80B, METHOD_BUFFERED','0x80A, METHOD_BUFFERED'),$h.Replace('HYPERVISOR_CR3_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)','HYPERVISOR_CR3_STATUS_QUERY_REQUEST_SIZE UINT32_C(20)'),$h.Replace('} HYPERVISOR_CR3_STATUS_QUERY_REQUEST;','    void* Pointer;`n} HYPERVISOR_CR3_STATUS_QUERY_REQUEST;'))
$sm=@($s.Replace('input_length != sizeof(*request)','input_length < sizeof(*request)'),$s.Replace('request->Version != HYPERVISOR_CR3_STATUS_QUERY_VERSION','false'),$s.Replace('request->Size != HYPERVISOR_CR3_STATUS_QUERY_REQUEST_SIZE','false'),$s.Replace('request->Flags != HYPERVISOR_CR3_STATUS_QUERY_FLAGS_NONE','false'),$s.Replace('request->Reserved != 0','false'))
$i=0;foreach($m in $hm){$i++;A ($m-ne$h) 'vacuous header mutation';$bad=$false;try{V $m $s $t}catch{$bad=$true};A $bad "accepted header mutation $i"}
$i=0;foreach($m in $sm){$i++;A ($m-ne$s) 'vacuous source mutation';$bad=$false;try{V $h $m $t}catch{$bad=$true};A $bad "accepted source mutation $i"}
Write-Output 'Verified CR3 status query ABI: fixed 0x80B, exact pointer-free request, pure validator, and deferred wiring.'