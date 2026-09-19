param(
    [Parameter(Mandatory = $true)][string]$SourcePath,
    [Parameter(Mandatory = $true)][string]$HeaderPath,
    [Parameter(Mandatory = $true)][string]$ContextHeaderPath,
    [Parameter(Mandatory = $true)][string]$AsmPath,
    [Parameter(Mandatory = $true)][string]$DriverCMakePath,
    [Parameter(Mandatory = $true)][string]$DriverMainPath,
    [Parameter(Mandatory = $true)][string]$IoctlPath,
    [Parameter(Mandatory = $true)][string]$RuntimePath,
    [Parameter(Mandatory = $true)][string]$HookPath
)
$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Count-Token([string]$Text, [string]$Token) {
    return ([regex]::Matches($Text, [regex]::Escape($Token))).Count
}
function Section([string]$Text, [string]$Begin, [string]$End) {
    $start = $Text.IndexOf($Begin, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "missing section: $Begin" }
    if ([string]::IsNullOrEmpty($End)) { return $Text.Substring($start) }
    $finish = $Text.IndexOf($End, $start + $Begin.Length,
        [StringComparison]::Ordinal)
    if ($finish -lt 0) { throw "missing section boundary: $End" }
    return $Text.Substring($start, $finish - $start)
}
function Assert-Order([string]$Text, [string[]]$Tokens, [string]$Name) {
    $position = -1
    foreach ($token in $Tokens) {
        $next = $Text.IndexOf($token, $position + 1,
            [StringComparison]::Ordinal)
        if ($next -lt 0) { throw "$Name missing or misordered token: $token" }
        $position = $next
    }
}
function Check-Source([string]$Text) {
    Assert-True (-not [regex]::IsMatch($Text, '__vmx|__invept|<intrin\.h>')) 'C adapter must not embed VMX intrinsics'
    Assert-True ((Count-Token $Text 'KeGetCurrentIrql() != PASSIVE_LEVEL') -eq 1) 'initializer must have one PASSIVE gate'
    Assert-True ((Count-Token $Text 'KeGetCurrentIrql() != DISPATCH_LEVEL') -eq 3) 'identity, validation, and transition must have exact DISPATCH gates'
    Assert-True ((Count-Token $Text 'KeGetCurrentProcessorNumberEx(&number)') -eq 1) 'current processor identity must be queried exactly once'
    Assert-True ((Count-Token $Text 'InterlockedCompareExchange(') -eq 1) 'root ownership must use one compare-exchange'

    $execute = Section $Text 'static uint32_t NativeExecute(' 'VMX_EPT_NATIVE_STATUS VmxEptNativePlatformInitialize('
    Assert-Order $execute @(
        'type != VMX_EPT_INVALIDATION_SINGLE_CONTEXT',
        'type != VMX_EPT_INVALIDATION_ALL_CONTEXTS',
        'NativeSpanValid(descriptor',
        'NativeSnapshot(',
        'NativeRangesSeparate(',
        'NativeCurrentProcessorMatches(',
        'snapshot.RootOwned != 1',
        'return VMX_EPT_INSTRUCTION_NOT_ATTEMPTED;',
        'VmxEptInveptAsm('
    ) 'native execute'

    $root = Section $Text 'static int32_t NativeIsVmxRootOwned(' 'static uint32_t NativeExecute('
    Assert-Order $root @(
        'NativeSnapshot(',
        'NativeCurrentProcessorMatches(',
        'return -1;',
        'snapshot.RootOwned == 1 ? 1 : 0'
    ) 'root proof'

    $initialize = Section $Text 'VMX_EPT_NATIVE_STATUS VmxEptNativePlatformInitialize(' 'VMX_EPT_NATIVE_STATUS VmxEptNativePlatformValidate('
    Assert-Order $initialize @(
        'KeGetCurrentIrql() != PASSIVE_LEVEL',
        'NativeRangesSeparate(',
        'RtlCompareMemory(',
        'staged_context.Magic = VMX_EPT_NATIVE_CONTEXT_MAGIC;',
        'VmxEptNativeContextValidate(&staged_context)',
        'staged_context.Magic = 0;',
        'staged_platform.GetCurrentIrql = NativeGetCurrentIrql;',
        'staged_platform.IsVmxRootOwned = NativeIsVmxRootOwned;',
        'staged_platform.Execute = NativeExecute;',
        'RtlCopyMemory(context, &staged_context',
        '*platform = staged_platform;',
        'KeMemoryBarrier();',
        'InterlockedExchange('
    ) 'initializer publication'

    $validate = Section $Text 'VMX_EPT_NATIVE_STATUS VmxEptNativePlatformValidate(' 'VMX_EPT_NATIVE_STATUS VmxEptNativeRootTransition('
    Assert-Order $validate @(
        'KeGetCurrentIrql() != DISPATCH_LEVEL',
        'NativeSpanValid(context',
        'NativeSpanValid(identity',
        'NativeSpanValid(platform',
        'NativeRangesSeparate(context',
        'NativeRangesSeparate(identity',
        'RtlCopyMemory(&context_snapshot',
        'RtlCopyMemory(&identity_snapshot',
        'RtlCopyMemory(&platform_snapshot',
        'VmxEptNativeContextValidate(&context_snapshot)',
        'identity_snapshot.Reserved != 0',
        'identity_snapshot.Group != context_snapshot.Group',
        'identity_snapshot.Number != context_snapshot.Number',
        'identity_snapshot.SystemIndex != context_snapshot.SystemIndex',
        'platform_snapshot.Context != context',
        'platform_snapshot.GetCurrentIrql != NativeGetCurrentIrql',
        'platform_snapshot.IsVmxRootOwned != NativeIsVmxRootOwned',
        'platform_snapshot.Execute != NativeExecute',
        'platform_snapshot.Reserved != 0',
        'NativeCurrentProcessorMatches(&context_snapshot)',
        'VMX_EPT_NATIVE_WRONG_PROCESSOR'
    ) 'native platform validation'

    $transition = Section $Text 'VMX_EPT_NATIVE_STATUS VmxEptNativeRootTransition(' ''
    Assert-Order $transition @(
        'expected_owned > 1',
        'expected_owned == desired_owned',
        'KeGetCurrentIrql() != DISPATCH_LEVEL',
        'NativeSnapshot(',
        'NativeCurrentProcessorMatches(',
        'InterlockedCompareExchange(',
        'VMX_EPT_NATIVE_STATE_MISMATCH'
    ) 'root transition'
}

function Check-Asm([string]$Text) {
    Assert-True ((Count-Token $Text 'invept rcx, oword ptr [rdx]') -eq 1) 'assembly must contain exactly one INVEPT'
    Assert-Order $Text @(
        'invept rcx, oword ptr [rdx]',
        'pushfq',
        'pop rax',
        'test eax, 1',
        'jnz invept_fail_invalid',
        'test eax, 40h',
        'jnz invept_fail_valid',
        'mov eax, 1',
        'invept_fail_valid:',
        'mov eax, 2',
        'invept_fail_invalid:',
        'mov eax, 3'
    ) 'INVEPT flags'
}

$source = Get-Content -Raw -LiteralPath $SourcePath
$header = Get-Content -Raw -LiteralPath $HeaderPath
$contextHeader = Get-Content -Raw -LiteralPath $ContextHeaderPath
$asm = Get-Content -Raw -LiteralPath $AsmPath
$cmake = Get-Content -Raw -LiteralPath $DriverCMakePath

Check-Source $source
Check-Asm $asm
Assert-True ($header.Contains('VmxEptNativePlatformInitialize(') -and $header.Contains('VmxEptNativePlatformValidate(') -and $header.Contains('VmxEptNativeRootTransition(')) 'native APIs missing'
Assert-True ($header.Contains('caller-owned nonpaged storage') -and $header.Contains('pairwise separate') -and $header.Contains('no root transition') -and $header.Contains('no instruction')) 'native validation lifetime contract missing'
Assert-True ($contextHeader.Contains('typedef struct VMX_EPT_NATIVE_CONTEXT') -and $contextHeader.Contains('volatile int32_t RootOwned;') -and $contextHeader.Contains('sizeof(VMX_EPT_NATIVE_CONTEXT) == 24')) 'fixed native context ABI missing'
Assert-True ((Count-Token $cmake 'vmx_ept_native_adapter.c') -eq 1) 'adapter source must be registered once'
Assert-True (([regex]::Matches($cmake, '(?m)^\s{8}vmx_ept_invept\.asm\r?$')).Count -eq 1) 'INVEPT assembly source must be registered once'

foreach ($path in @($DriverMainPath, $IoctlPath, $RuntimePath, $HookPath)) {
    $text = Get-Content -Raw -LiteralPath $path
    Assert-True (-not [regex]::IsMatch($text, 'VmxEptNative|VmxEptInvept|vmx_ept_native|vmx_ept_invept')) "native EPT wiring is forbidden in $path"
}

$sourceMutations = @(
    $source.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL', 'KeGetCurrentIrql() == PASSIVE_LEVEL'),
    $source.Replace('KeGetCurrentIrql() != DISPATCH_LEVEL', 'KeGetCurrentIrql() != PASSIVE_LEVEL'),
    $source.Replace('KeGetCurrentProcessorNumberEx(&number)', 'KeGetCurrentProcessorNumberEx(NULL)'),
    $source.Replace('InterlockedCompareExchange(', 'InterlockedExchange('),
    $source.Replace('snapshot.RootOwned != 1', 'snapshot.RootOwned == 1'),
    $source.Replace('VmxEptInveptAsm((uint64_t)type, descriptor)', 'VMX_EPT_INSTRUCTION_SUCCESS'),
    $source.Replace('staged_platform.Execute = NativeExecute;', 'staged_platform.Execute = 0;'),
    $source.Replace('staged_context.Magic = 0;', 'staged_context.Magic = VMX_EPT_NATIVE_CONTEXT_MAGIC;'),
    $source.Replace('identity_snapshot.SystemIndex != context_snapshot.SystemIndex', '0'),
    $source.Replace('platform_snapshot.Execute != NativeExecute', '0'),
    $source.Replace('NativeCurrentProcessorMatches(&context_snapshot) ?', '1 ?')
)
foreach ($mutation in $sourceMutations) {
    Assert-True ($mutation -ne $source) 'source mutation was not applied'
    $rejected = $false
    try { Check-Source $mutation } catch { $rejected = $true }
    Assert-True $rejected 'accepted native adapter mutation'
}

$asmMutations = @(
    $asm.Replace('pushfq', 'nop'),
    $asm.Replace('test eax, 1', 'test eax, 40h'),
    $asm.Replace('mov eax, 2', 'mov eax, 3')
)
foreach ($mutation in $asmMutations) {
    Assert-True ($mutation -ne $asm) 'assembly mutation was not applied'
    $rejected = $false
    try { Check-Asm $mutation } catch { $rejected = $true }
    Assert-True $rejected 'accepted INVEPT assembly mutation'
}

Write-Host 'PASS: native EPT adapter contracts'
