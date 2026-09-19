param(
 [Parameter(Mandatory=$true)][string]$LoaderSource,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not$x){throw $m}}
function O($t,$v,$n){$p=0;foreach($x in $v){$q=$t.IndexOf($x,$p,[StringComparison]::Ordinal);A($q-ge0)"$n $x";$p=$q+$x.Length}}
function V($l,$t){
 foreach($x in @('hypervisor_loader.exe hook-roundtrip','Runs a private kernel-owned page hook transaction without accessing the page while hooked.')){A($l.Contains($x))"usage $x"}
 $a=$l.IndexOf('static int CmdHookRoundtrip(');$b=$l.IndexOf('static int CmdStatus(');A($a-ge0-and$b-gt$a)'command bounds';$x=$l.Substring($a,$b-$a)
 O $x @('HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST request = { 0 }','HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE response = { 0 }','request.Version = HYPERVISOR_EPT_HOOK_ROUNDTRIP_VERSION','request.Size = HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST_SIZE','request.Flags = HYPERVISOR_EPT_HOOK_ROUNDTRIP_FLAGS_NONE','OpenDevice()','IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP','&request, sizeof(request)','&response, sizeof(response)','response.Version !=','response.Size !=','response.Status != ERR_SUCCESS','response.Stage != HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_COMPLETE','response.GuestPhysicalPage == 0','response.GuestPhysicalPage & 0xFFFu','response.HookId == UINT32_MAX','response.ProcessorCount == 0','response.AddStatus != 0','response.RemoveStatus != 0','response.AddInvalidationCalls != response.ProcessorCount','response.AddInvalidationSuccesses != response.AddInvalidationCalls','response.RemoveInvalidationCalls != response.ProcessorCount','response.RemoveInvalidationSuccesses != response.RemoveInvalidationCalls','response.Generation == 0','SUCCESS: Kernel-owned no-access EPT hook roundtrip verified.','CloseHandle(g_device_handle)','g_device_handle = INVALID_HANDLE_VALUE') 'loader diagnostic'
 A($x-notmatch 'hook <gpa|wcstoull|Rtl|VirtualAlloc|ReadProcessMemory|WriteProcessMemory')'no caller GPA/page access'
 $m=$l.Substring($l.IndexOf('int wmain('))
 O $m @('L"hook-roundtrip"','return CmdHookRoundtrip();','L"status"') 'main routing'
 A(([regex]::Matches($m,'L"hook-roundtrip"')).Count-eq1)'one command'
 A(([regex]::Matches($t,'test_vmx_ept_hook_roundtrip_loader_contracts')).Count-eq1)'test once'
}
$l=Get-Content -Raw $LoaderSource;$t=Get-Content -Raw $TestsCMake;V $l $t
$mut=@(
 $l.Replace('request.Version = HYPERVISOR_EPT_HOOK_ROUNDTRIP_VERSION;',''),
 $l.Replace('IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP','IOCTL_HYPERVISOR_EPT_STATUS_QUERY'),
 $l.Replace('response.Stage != HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_COMPLETE','0'),
 $l.Replace('response.AddInvalidationCalls != response.ProcessorCount','0'),
 $l.Replace('response.RemoveInvalidationSuccesses != response.RemoveInvalidationCalls','0'),
 $l.Replace('response.Generation == 0','0'),
 $l.Replace('CloseHandle(g_device_handle);','')
)
foreach($q in $mut){A($q-ne$l)'vacuous mutation';$bad=$false;try{V $q $t}catch{$bad=$true};A($bad)'accepted mutation'}
Write-Host 'PASS: loader validates the exact complete 0x809 no-access hook roundtrip and closes the device on every command path.'
