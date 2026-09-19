param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$NativeSourcePath,
 [Parameter(Mandatory=$true)][string]$NativeHeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$RuntimePath,
 [Parameter(Mandatory=$true)][string]$HookPath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath
$h=Get-Content -Raw -LiteralPath $HeaderPath
$n=Get-Content -Raw -LiteralPath $NativeSourcePath
$nh=Get-Content -Raw -LiteralPath $NativeHeaderPath
$c=Get-Content -Raw -LiteralPath $DriverCMakePath
function A([bool]$x,[string]$m){if (-not $x) { throw $m }}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);if($i-lt 0){ return '' };if([string]::IsNullOrEmpty($b)){return $x.Substring($i)};$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($j-lt 0){ return '' };$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$at=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($at-lt 0){return $false};$at+=$v.Length}; $true}
function V([string]$x){
 $spans=B $x 'static int ContextSeparate(' 'static VMX_EPT_KERNEL_INVALIDATION_STATUS MapOperation('
 $operation=B $x 'static VMX_EPT_KERNEL_INVALIDATION_STATUS MapOperation(' 'static VMX_EPT_KERNEL_INVALIDATION_STATUS MapNative('
 $native=B $x 'static VMX_EPT_KERNEL_INVALIDATION_STATUS MapNative(' 'static VMX_EPT_KERNEL_INVALIDATION_STATUS MapCoordinator('
 $coord=B $x 'static VMX_EPT_KERNEL_INVALIDATION_STATUS MapCoordinator(' 'VMX_EPT_KERNEL_INVALIDATION_STATUS VmxEptKernelCoordinateInvalidation('
 $call=B $x 'VMX_EPT_KERNEL_INVALIDATION_STATUS VmxEptKernelCoordinateInvalidation(' ''
 return (O $spans @('owner == NULL','identity == NULL','context == NULL','platform == NULL','report == NULL','VmxEptSessionOwnerValidate(owner)','owner->GraphBytes > SIZE_MAX','storage->PageCount > SIZE_MAX','Separate(owner, sizeof(*owner), context','Separate(owner->Graph','Separate(storage->Tables','Separate(storage->PagesByOrdinal','Separate(storage->PhysicalIndex','Separate(identity, sizeof(*identity), context','Separate(context, sizeof(*context), platform','Separate(context, sizeof(*context), report'))-and
  (O $operation @('VMX_EPT_SESSION_OPERATION_OK','KERNEL_INVALIDATION_OK','IDENTITY_MISMATCH','UNSUPPORTED','NOT_READY','BUSY','DIRTY','INSTRUCTION_FAILED','OVERFLOW','KERNEL_INVALIDATION_INVALID'))-and
  (O $native @('VMX_EPT_NATIVE_OK','KERNEL_INVALIDATION_OK','VMX_EPT_NATIVE_WRONG_IRQL','KERNEL_INVALIDATION_WRONG_IRQL','VMX_EPT_NATIVE_WRONG_PROCESSOR','KERNEL_INVALIDATION_WRONG_PROCESSOR','VMX_EPT_NATIVE_STATE_MISMATCH','KERNEL_INVALIDATION_PLATFORM_INVALID'))-and
  (O $coord @('COORDINATOR_OK','KERNEL_INVALIDATION_OK','COORDINATOR_IDENTITY_MISMATCH','COORDINATOR_UNSUPPORTED','COORDINATOR_NOT_READY','COORDINATOR_BUSY','COORDINATOR_DIRTY','COORDINATOR_INSTRUCTION_FAILED','COORDINATOR_OVERFLOW','COORDINATOR_WRONG_IRQL','COORDINATOR_NOT_VMX_ROOT','COORDINATOR_PLATFORM_INVALID','KERNEL_INVALIDATION_INVALID'))-and
  (O $call @('KeGetCurrentIrql() != DISPATCH_LEVEL','ContextSeparate(owner, identity, context, platform, report)','VmxEptSessionValidateInvalidationExecution(','owner, identity, platform, report','operation_status != VMX_EPT_SESSION_OPERATION_OK','MapOperation(operation_status)','VmxEptNativePlatformValidate(','context, identity, platform','native_status != VMX_EPT_NATIVE_OK','MapNative(native_status)','MapCoordinator(','VmxEptCoordinateInvalidation(','owner, identity, platform, report'))
}
function N([string]$x){
 $v=B $x 'VMX_EPT_NATIVE_STATUS VmxEptNativePlatformValidate(' 'VMX_EPT_NATIVE_STATUS VmxEptNativeRootTransition('
 return (O $v @('KeGetCurrentIrql() != DISPATCH_LEVEL','NativeRangesSeparate(context','NativeRangesSeparate(identity','RtlCopyMemory(&context_snapshot','RtlCopyMemory(&identity_snapshot','RtlCopyMemory(&platform_snapshot','VmxEptNativeContextValidate(&context_snapshot)','identity_snapshot.Group != context_snapshot.Group','identity_snapshot.Number != context_snapshot.Number','identity_snapshot.SystemIndex != context_snapshot.SystemIndex','platform_snapshot.Context != context','platform_snapshot.GetCurrentIrql != NativeGetCurrentIrql','platform_snapshot.IsVmxRootOwned != NativeIsVmxRootOwned','platform_snapshot.Execute != NativeExecute','platform_snapshot.Reserved != 0','NativeCurrentProcessorMatches(&context_snapshot)','VMX_EPT_NATIVE_WRONG_PROCESSOR'))
}
A(V $s)'trusted invalidation adapter ordering'
A(N $n)'native platform authentication ordering'
foreach($v in @('Trusted WDK binding','externally','nonpaged','pinned to identity','DISPATCH_LEVEL','immutable','Pre-instruction failures preserve report','allocates nothing','no pin','root transition','table','VMCS','runtime','hook','IOCTL','direct INVEPT')){A($h.Contains($v))"header $v"}
A($nh.Contains('VmxEptNativePlatformValidate('))'native validation API missing'
A((([regex]::Matches($c,'(?m)^\s{8}vmx_ept_invalidation_adapter\.c\r?$')).Count-eq 1))'adapter source registration'
A($s-notmatch '__vmx|__invept|VmxEptInveptAsm|VmxEptExecuteInvalidation|VmxEptNativeRootTransition|KeSetSystemGroupAffinityThread|KeRevertToUserGroupAffinityThread')'forbidden direct native operation'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptKernelCoordinateInvalidation|vmx_ept_invalidation_adapter')"E2b remains unwired: $p"}
$sm=@(
 $s.Replace('KeGetCurrentIrql() != DISPATCH_LEVEL','KeGetCurrentIrql() != PASSIVE_LEVEL'),
 $s.Replace('VmxEptSessionOwnerValidate(owner) != VMX_EPT_SESSION_OK','0'),
 $s.Replace('Separate(owner->Graph, (SIZE_T)owner->GraphBytes,','Separate(owner, sizeof(*owner),'),
 $s.Replace('Separate(storage->Tables, (SIZE_T)storage->TableBytes,','Separate(owner, sizeof(*owner),'),
 $s.Replace('Separate(storage->PhysicalIndex,','Separate(storage->Tables,'),
 $s.Replace('Separate(context, sizeof(*context), report, sizeof(*report))','1'),
 $s.Replace('VmxEptSessionValidateInvalidationExecution(','VmxEptSessionAcceptInvalidation('),
 $s.Replace('VmxEptNativePlatformValidate(','VmxEptNativeRootTransition('),
 $s.Replace('VmxEptCoordinateInvalidation(','VmxEptExecuteInvalidation(')
)
foreach($m in $sm){A($m-ne$s)'source mutation missing';A(-not(V $m))'accepted E2b source mutation'}
$nm=@(
 $n.Replace('identity_snapshot.SystemIndex != context_snapshot.SystemIndex','0'),
 $n.Replace('platform_snapshot.Context != context','0'),
 $n.Replace('platform_snapshot.Execute != NativeExecute','0'),
 $n.Replace('NativeCurrentProcessorMatches(&context_snapshot) ?','1 ?')
)
foreach($m in $nm){A($m-ne$n)'native mutation missing';A(-not(N $m))'accepted E2b native mutation'}
Write-Host 'PASS: EPT trusted native invalidation binding contracts'
