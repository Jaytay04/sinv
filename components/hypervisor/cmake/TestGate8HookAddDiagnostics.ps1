param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not $x){throw $m}}
function O($t,$tokens,$name){$at=0;foreach($token in $tokens){$next=$t.IndexOf($token,$at,[StringComparison]::Ordinal);A($next-ge0)"$name $token";$at=$next+$token.Length}}
function V($s,$t){
 $a=$s.IndexOf('static void TraceAddFailure(')
 $b=$s.IndexOf('static VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS EnsureOwner(',$a)
 A($a-ge0-and$b-gt$a)'trace bounds'
 $trace=$s.Substring($a,$b-$a)
 O $trace @(
  'session->CoverageRecords[index].RequiredGeneration != 0',
  'VmxEptHookLocate2Mb(',
  'VmxEptHookOwnerValidate(&context->EptHooks)',
  'DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL',
  'gate8 hook-add failure',
  'device=%d ensure=%d',
  'owner_validate=%d location=%d',
  'count=%u owned=%u used=%u',
  'mutation=%I64u pending=%I64u invalidated=%I64u',
  'stage_before=%u stage_after=%u invalidation=%u/%u') 'trace evidence'
 A($trace-notmatch '__vmx|__invept|VmxEptHookLifecycle(Add|Remove)|VmxEptHookOwnerPrepare')'trace is read-only'
 $addStart=$s.IndexOf('VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS VmxEptPersistentDeviceHookAdd(')
 $addEnd=$s.IndexOf('VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS VmxEptPersistentDeviceHookRemove(',$addStart)
 A($addStart-ge0-and$addEnd-gt$addStart)'add bounds'
 $add=$s.Substring($addStart,$addEnd-$addStart)
 O $add @(
  'status = EnsureOwner(context, &ensure_status);',
  'if (status != VMX_EPT_PERSISTENT_DEVICE_HOOK_OK) {',
  'TraceAddFailure(context, guest_physical_page, status,',
  'return status;',
  'VmxEptHookLifecycleAdd(',
  'status = MapLifecycle(lifecycle_status);',
  'if (status != VMX_EPT_PERSISTENT_DEVICE_HOOK_OK) {',
  'TraceAddFailure(context, guest_physical_page, status,',
  'return status;') 'failure-only trace order'
 A(([regex]::Matches($add,[regex]::Escape('TraceAddFailure(context, guest_physical_page, status,'))).Count-eq2)'two failure trace calls'
 A(([regex]::Matches($t,'test_gate8_hook_add_failure_diagnostics')).Count-eq1)'test once'
}
$s=Get-Content -Raw $SourcePath
$t=Get-Content -Raw $TestsCMake
V $s $t
$mut=@(
 $s.Replace('VmxEptHookLocate2Mb(','VmxEptHookOwnerQuery('),
 $s.Replace('DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL','DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL'),
 $s.Replace('session->CoverageRecords[index].RequiredGeneration != 0','0'),
 ([regex]::Replace($s,'(?s)        TraceAddFailure\(context, guest_physical_page, status,\r?\n            ensure_status, VMX_EPT_HOOK_LIFECYCLE_INVALID\);','',1)),
 ([regex]::Replace($s,'(?s)        TraceAddFailure\(context, guest_physical_page, status,\r?\n            ensure_status, lifecycle_status\);','',1))
)
foreach($m in $mut){
 A($m-ne$s)'vacuous source mutation'
 $bad=$false
 try{V $m $t}catch{$bad=$true}
 A($bad)'accepted source mutation'
}
Write-Host 'PASS: Gate 8 hook-add failures emit read-only owner/location/coverage/generation evidence on both initialization and lifecycle failure paths.'
