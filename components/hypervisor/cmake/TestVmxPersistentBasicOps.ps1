param(
    [Parameter(Mandatory = $true)] [string]$SourcePath,
    [Parameter(Mandatory = $true)] [string]$HeaderPath,
    [Parameter(Mandatory = $true)] [string]$ContextHeaderPath
)
$ErrorActionPreference = 'Stop'
function Assert-C([bool]$ok, [string]$why) { if (-not $ok) { throw $why } }
function Slice([string]$text, [string]$first, [string]$next) {
    $a = $text.IndexOf($first, [StringComparison]::Ordinal)
    $b = $text.IndexOf($next, $a + $first.Length, [StringComparison]::Ordinal)
    Assert-C ($a -ge 0 -and $b -gt $a) "Missing section $first"
    $text.Substring($a, $b - $a)
}
function Order([string]$text, [string[]]$tokens, [string]$name) {
    $last = -1
    foreach ($token in $tokens) {
        $at = $text.IndexOf($token, $last + 1, [StringComparison]::Ordinal)
        Assert-C ($at -gt $last) "$name order changed at $token"
        $last = $at
    }
}
function Verify([string]$source, [string]$header, [string]$contextHeader) {
    foreach ($term in @('WDK-only','caller-owned per-CPU','zero-initialized','nonpaged',
        'stable through the final Vmxoff','no destructor','hidden allocation','intentionally partial',
        'rejects stale CR4','per-target','CET/FEATURE_CONTROL gate','preserving outputs')) {
        Assert-C ($header -match "(?i)$term") "Missing contract: $term"
    }
    Assert-C ($header -match '#include\s+"vmx_persistent_basic_context\.h"') 'Canonical context include missing'
    Assert-C ($header -notmatch 'typedef\s+struct\s+_VMX_PERSISTENT_BASIC_OPS_CONTEXT') 'Duplicate context definition'
    Assert-C ($contextHeader -match 'VMX_PERSISTENT_BASIC_OPS_MAGIC UINT32_C\(0x4F334256\)') 'Numeric Magic missing'
    Assert-C ($contextHeader -match 'VMX_PERSISTENT_BASIC_OPS_VERSION UINT32_C\(2\)') 'Numeric Version missing'
    Assert-C ($contextHeader -match 'uint64_t\s+CurrentVmcsPa' -and $contextHeader -match 'uint32_t\s+CurrentVmcsValid') 'Current VMCS state missing'
    Assert-C ($contextHeader -notmatch '(?i)ntddk|wdm\.h|windows\.h') 'Canonical context header is not WDK-free'
    foreach ($abi in @(
        'sizeof\(VMX_PERSISTENT_BASIC_OPS_CONTEXT\)\s*==\s*32',
        'ALIGNOF\(VMX_PERSISTENT_BASIC_OPS_CONTEXT\)\s*==\s*8',
        'offsetof\(VMX_PERSISTENT_BASIC_OPS_CONTEXT,\s*Magic\)\s*==\s*0',
        'offsetof\(VMX_PERSISTENT_BASIC_OPS_CONTEXT,\s*Version\)\s*==\s*4',
        'offsetof\(VMX_PERSISTENT_BASIC_OPS_CONTEXT,\s*CurrentVmcsPa\)\s*==\s*8',
        'offsetof\(VMX_PERSISTENT_BASIC_OPS_CONTEXT,\s*CurrentVmcsValid\)\s*==\s*16',
        'offsetof\(VMX_PERSISTENT_BASIC_OPS_CONTEXT,\s*Reserved\)\s*==\s*20',
        'offsetof\(VMX_PERSISTENT_BASIC_OPS_CONTEXT,\s*Cr3RouteBound\)\s*==\s*24',
        'offsetof\(VMX_PERSISTENT_BASIC_OPS_CONTEXT,\s*Cr3RouteBoundContract\)\s*==\s*28')) {
        Assert-C ($contextHeader -match $abi) "Missing ABI assertion: $abi"
    }
    Assert-C ($source -notmatch '(?i)FAST_MUTEX|g_vmxOldIrql|Affinity|CPU0|DriverEntry|IRP_MJ|IOCTL|VMX_V2|vmx_transient|RunOnce|InitOnce|ExAllocate|MmAllocate|malloc|calloc|realloc') 'Forbidden legacy/global/wiring pattern'
    Assert-C ($source -notmatch '(?m)^\s*static\s+VMX_PERSISTENT_BASIC_OPS_CONTEXT\s+\w+') 'Hidden context storage'
    Assert-C ($source -match '#include\s+"vmx_persistent_preentry\.h"') 'Pre-entry validator include missing'

    $enter = Slice $source 'static bool BasicEnter' 'static bool BasicLeave'
    $leave = Slice $source 'static bool BasicLeave' 'static uint64_t BasicReadCr0'
    $prepare = Slice $source 'static bool BasicPrepareControlRegisters' 'static VMX_PERSISTENT_INSTRUCTION_REPORT BasicReport'
    $report = Slice $source 'static VMX_PERSISTENT_INSTRUCTION_REPORT BasicReport' 'static bool BasicPaValid'
    $pa = Slice $source 'static bool BasicPaValid' 'static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmxon'
    $vmxon = Slice $source 'static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmxon' 'static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmclear'
    $clear = Slice $source 'static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmclear' 'static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmptrld'
    $load = Slice $source 'static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmptrld' 'static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmxoff'
    $off = Slice $source 'static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmxoff' 'VMX_PERSISTENT_STATUS VmxPersistentBasicOpsInitialize'
    $init = $source.Substring($source.IndexOf('VMX_PERSISTENT_STATUS VmxPersistentBasicOpsInitialize'))

    Order $enter @('KeGetCurrentIrql()', 'current != PASSIVE_LEVEL', '*previous = (uint8_t)current',
        'KeRaiseIrql(DISPATCH_LEVEL', 'KeGetCurrentIrql() == DISPATCH_LEVEL') 'Enter'
    Order $enter @('return true;', 'KeGetCurrentIrql() == DISPATCH_LEVEL',
        'KeLowerIrql(raised_from)', 'return false;') 'Enter failed-raise unwind'
    Order $leave @('KeGetCurrentIrql() != DISPATCH_LEVEL', 'previous != (uint8_t)PASSIVE_LEVEL',
        'KeLowerIrql((KIRQL)previous)', 'KeGetCurrentIrql() == (KIRQL)previous') 'Leave saved target'
    foreach ($msr in @('IA32_VMX_CR0_FIXED0','IA32_VMX_CR0_FIXED1',
        'IA32_VMX_CR4_FIXED0','IA32_VMX_CR4_FIXED1')) {
        Assert-C (([regex]::Matches($prepare, $msr)).Count -eq 1) "Fixed MSR changed: $msr"
    }
    Assert-C (([regex]::Matches($prepare, 'IA32_FEATURE_CONTROL')).Count -eq 1) 'FEATURE_CONTROL read count changed'
    Order $prepare @('__readcr4()', '__readmsr(IA32_FEATURE_CONTROL)',
        'current_cr4 != cr4', 'VmxPersistentPreentryValidate(current_cr4, feature_control)',
        '__readmsr(IA32_VMX_CR0_FIXED0)', '__readmsr(IA32_VMX_CR0_FIXED1)',
        '__readmsr(IA32_VMX_CR4_FIXED0)', '__readmsr(IA32_VMX_CR4_FIXED1)',
        '(cr0_fixed0 & ~cr0_fixed1) != 0', '(cr4_fixed0 & ~cr4_fixed1) != 0',
        'VmxComputeFixedCr0(', 'VmxComputeFixedCr4(cr4 | CR4_VMXE',
        '(prepared_cr4 & CR4_VMXE) == 0', '*out_cr0 =', '*out_cr4 =') 'CR prepare'
    Order $report @('raw == 0', 'VMX_V3D_INSTRUCTION_SUCCESS', 'raw == 1',
        'VMX_V3D_INSTRUCTION_FAIL_VALID', 'raw == 2', 'VMX_V3D_INSTRUCTION_FAIL_INVALID') 'Status map'
    Order $report @('allow_error &&', 'raw == 1 &&', 'BasicSnapshot(context', 'copy.CurrentVmcsValid == 1',
        '__vmx_vmread(VMCS_VM_INSTRUCTION_ERROR') 'VM-error gate'
    Order $pa @('pa != 0', '(pa & VMX_PAGE_MASK) == 0') 'PA nonzero/alignment'
    Order $vmxon @('BasicPaValid(pa)', '__vmx_on(&pa)', ', false)') 'VMXON validation/no-error-read'
    Assert-C ($vmxon -notmatch '__vmx_on\(&pa\), true') 'VMXON must suppress VM-error reads'
    Order $clear @('BasicPaValid(pa)', '__vmx_vmclear(&pa)', 'raw == 0',
        'copy.CurrentVmcsValid == 1', 'copy.CurrentVmcsPa == pa', 'CurrentVmcsPa = 0',
        'CurrentVmcsValid = 0') 'VMCLEAR tracking'
    Order $load @('BasicPaValid(pa)', '__vmx_vmptrld(&pa)', 'raw == 0',
        'CurrentVmcsPa = pa', 'CurrentVmcsValid = 1') 'VMPTRLD tracking'
    Order $off @('__vmx_off()', 'CurrentVmcsPa = 0', 'CurrentVmcsValid = 0',
        'BasicReport((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque, 0, false)') 'VMXOFF tracking'
    Order $init @('BasicOverlap(context', 'RtlCopyMemory(&magic', 'VMX_PERSISTENT_STATUS_BUSY',
        'RtlCompareMemory(', 'RtlZeroMemory(&staged_operations',
        '*out_operations = staged_operations', 'KeMemoryBarrier()', 'context->Version =',
        'InterlockedExchange((volatile LONG*)&context->Magic') 'Initialize/publish'
    foreach ($missing in @('CaptureState','BuildControls','ApplyPlan','Launch','Shutdown')) {
        Assert-C ($init -notmatch "staged_operations\.$missing\s*=") "Partial table populated $missing"
    }
}
$source = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath))
$header = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath))
$contextHeader = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $ContextHeaderPath))
Verify $source $header $contextHeader
$mutations = @(
    $source.Replace('current != PASSIVE_LEVEL', 'current == PASSIVE_LEVEL'),
    $source.Replace('*previous = (uint8_t)current;', '/* removed previous */'),
    $source.Replace('KeLowerIrql(raised_from);', '/* removed unwind */'),
    $source.Replace('KeLowerIrql((KIRQL)previous);', 'KeLowerIrql(DISPATCH_LEVEL);'),
    $source.Replace('current_cr4 = __readcr4();', 'current_cr4 = cr4;'),
    $source.Replace('feature_control = __readmsr(IA32_FEATURE_CONTROL);', 'feature_control = 5;'),
    $source.Replace('current_cr4 != cr4', 'false'),
    $source.Replace('!VmxPersistentPreentryValidate(current_cr4, feature_control)', 'false'),
    $source.Replace('IA32_VMX_CR4_FIXED1', 'IA32_VMX_CR4_FIXED0'),
    $source.Replace('(cr0_fixed0 & ~cr0_fixed1) != 0', 'false'),
    $source.Replace('(cr4_fixed0 & ~cr4_fixed1) != 0', 'false'),
    $source.Replace('(prepared_cr4 & CR4_VMXE) == 0', 'false'),
    $source.Replace('pa != 0 &&', 'true &&'),
    $source.Replace('(pa & VMX_PAGE_MASK) == 0', 'true'),
    $source.Replace('copy.CurrentVmcsValid == 1 &&', 'true &&'),
    $source.Replace('copy.CurrentVmcsPa == pa', 'copy.CurrentVmcsPa != pa'),
    $source.Replace('CurrentVmcsValid = 1;', 'CurrentVmcsValid = 0;'),
    $source.Replace('__vmx_off();', '/* removed vmxoff */'),
    $source.Replace('__vmx_on(&pa), false', '__vmx_on(&pa), true'),
    ($source + [Environment]::NewLine + 'FAST_MUTEX forbiddenMutation;')
)
foreach ($mutation in $mutations) {
    $rejected = $false
    try { Verify $mutation $header $contextHeader } catch { $rejected = $true }
    Assert-C $rejected 'Verifier accepted a required-contract mutation'
}
$headerMutations = @(
    $header.Replace('#include "vmx_persistent_basic_context.h"', '/* canonical include removed */'),
    ($header + [Environment]::NewLine + 'typedef struct _VMX_PERSISTENT_BASIC_OPS_CONTEXT { int Duplicate; } VMX_PERSISTENT_BASIC_OPS_CONTEXT;')
)
foreach ($mutation in $headerMutations) {
    Assert-C ($mutation -ne $header) 'Header mutation was vacuous'
    $rejected = $false
    try { Verify $source $mutation $contextHeader } catch { $rejected = $true }
    Assert-C $rejected 'Verifier accepted a required BasicOps-header mutation'
}
$contextMutations = @(
    $contextHeader.Replace('UINT32_C(0x4F334256)', 'UINT32_C(0x4F334257)'),
    $contextHeader.Replace('VMX_PERSISTENT_BASIC_OPS_VERSION UINT32_C(2)', 'VMX_PERSISTENT_BASIC_OPS_VERSION UINT32_C(3)'),
    $contextHeader.Replace('uint64_t CurrentVmcsPa', 'uint32_t CurrentVmcsPa'),
    $contextHeader.Replace('== 24', '== 16'),
    $contextHeader.Replace('CurrentVmcsValid) == 16', 'CurrentVmcsValid) == 12')
)
foreach ($mutation in $contextMutations) {
    Assert-C ($mutation -ne $contextHeader) 'Context ABI mutation was vacuous'
    $rejected = $false
    try { Verify $source $header $mutation } catch { $rejected = $true }
    Assert-C $rejected 'Verifier accepted a required context-ABI mutation'
}
Write-Output 'Verified partial basic VMX ops and canonical WDK-free context ABI: layout assertions, single definition, IRQL order, per-target CET/FEATURE_CONTROL gate, stale-CR4 rejection, fixed CR MSRs, result normalization/error gate, PA validation, current-VMCS tracking, explicit preserved init, and mutation rejection.'
