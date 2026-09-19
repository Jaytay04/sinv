[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourcePath,
    [Parameter(Mandatory=$true)][string]$HeaderPath,
    [Parameter(Mandatory=$true)][string]$TestPath
)

$ErrorActionPreference = 'Stop'
function Assert-Contract([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Ordered([string]$Text, [string[]]$Needles) {
    $Offset = 0
    foreach ($Needle in $Needles) {
        $Found = $Text.IndexOf($Needle, $Offset, [StringComparison]::Ordinal)
        if ($Found -lt 0) { return $false }
        $Offset = $Found + $Needle.Length
    }
    return $true
}
function Verify([string]$Source) {
    return Ordered $Source @(
        'static int SpanValid',
        'first <= UINTPTR_MAX - (size - 1u)',
        'static int Separate',
        'l > r + right_size - 1u || r > l + left_size - 1u',
        'static int BuildSpansValid',
        '!SpanValid(context, sizeof(*context))',
        '!SpanValid(launch, sizeof(*launch))',
        '!SpanValid(controls, sizeof(*controls))',
        '!SpanValid(out_plan, sizeof(*out_plan))',
        'Separate(out_plan, sizeof(*out_plan), context, sizeof(*context))',
        'Separate(out_plan, sizeof(*out_plan), launch, sizeof(*launch))',
        'Separate(out_plan, sizeof(*out_plan), controls, sizeof(*controls))',
        'static int InputValid',
        'return BuildSpansValid(context, launch, controls, out_plan)',
        'VMX_PERSISTENT_VMCS_PLAN_STATUS VmxPersistentBuildVmcsPlan',
        'if (!InputValid(context, launch, controls, guest_rip, host_rip, out_plan))',
        '(void)memset(&plan, 0, sizeof(plan)); plan.Count = 1',
        '*out_plan = plan')
}

$Source = Get-Content -Raw -LiteralPath $SourcePath
$Header = Get-Content -Raw -LiteralPath $HeaderPath
$Tests = Get-Content -Raw -LiteralPath $TestPath

Assert-Contract (Verify $Source) `
    'Persistent VMCS-plan span contract is incomplete or misordered.'
foreach ($Token in @('must not overlap the context', 'launch context',
    'controls')) {
    Assert-Contract ($Header.Contains($Token)) "Header missing: $Token"
}
foreach ($Token in @('test_output_overlap_preserves_every_input',
    'overlapping output rejected', 'overlap preserves fixture',
    'overlap preserves launch context', '_Alignof(VMX_VMCS_PLAN)',
    'test_wrapping_spans_are_rejected_before_use',
    'wrapping context rejected', 'wrapping launch rejected',
    'wrapping controls rejected', 'wrapping output rejected')) {
    Assert-Contract ($Tests.Contains($Token)) "Test missing: $Token"
}

$Mutations = @(
    $Source.Replace('first <= UINTPTR_MAX - (size - 1u)', 'value != NULL'),
    $Source.Replace(
        'Separate(out_plan, sizeof(*out_plan), context, sizeof(*context))',
        '1'),
    $Source.Replace(
        'Separate(out_plan, sizeof(*out_plan), launch, sizeof(*launch))',
        '1'),
    $Source.Replace(
        'Separate(out_plan, sizeof(*out_plan), controls, sizeof(*controls))',
        '1'),
    $Source.Replace(
        'return BuildSpansValid(context, launch, controls, out_plan) &&',
        'return context != NULL && launch != NULL && controls != NULL && out_plan != NULL &&')
)
foreach ($Mutation in $Mutations) {
    Assert-Contract ($Mutation -ne $Source) `
        'A persistent VMCS-plan span mutation did not apply.'
    Assert-Contract (-not (Verify $Mutation)) `
        'A persistent VMCS-plan span mutation survived verification.'
}

Write-Host 'PASS: persistent VMCS-plan publication rejects exact and partial output overlap before construction.'
