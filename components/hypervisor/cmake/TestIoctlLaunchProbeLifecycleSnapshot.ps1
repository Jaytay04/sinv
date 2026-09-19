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
function Ordered([string]$Text, [string[]]$Tokens) {
    $Offset = 0
    foreach ($Token in $Tokens) {
        $Found = $Text.IndexOf($Token, $Offset, [StringComparison]::Ordinal)
        if ($Found -lt 0) { return $false }
        $Offset = $Found + $Token.Length
    }
    return $true
}
function Verify([string]$Source) {
    $Start = $Source.IndexOf('bool IoctlRunLaunchProbeLifecycle',
        [StringComparison]::Ordinal)
    if ($Start -lt 0) { return $false }
    $Body = $Source.Substring($Start)
    $Ordered = Ordered $Body @(
        'VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS lifecycle;',
        'if (!LifecycleInputsValid(operations, response))',
        '(void)memset(response, 0, sizeof(*response))',
        'if (operations == NULL)',
        'lifecycle = *operations;',
        'if (!IoctlLaunchProbeOperationsValid(&lifecycle))',
        'preflight_rc = lifecycle.CreateContext(',
        'transient_stage = lifecycle.ExecuteTransient(',
        'lifecycle.DestroyContext(',
        'return lifecycle.MapResponse(&map_input, response)'
    )
    $Spans = $Source.Contains('first <= UINTPTR_MAX - (size - 1u)') -and
        $Source.Contains(
            'LifecycleSpansSeparate(operations, sizeof(*operations),')
    return $Spans -and $Ordered -and -not $Body.Contains('operations->')
}

$Source = Get-Content -Raw -LiteralPath $SourcePath
$Header = Get-Content -Raw -LiteralPath $HeaderPath
$Tests = Get-Content -Raw -LiteralPath $TestPath

foreach ($Token in @('static bool LifecycleSpanValid',
    'first <= UINTPTR_MAX - (size - 1u)',
    'static bool LifecycleSpansSeparate',
    'static bool LifecycleInputsValid',
    'LifecycleSpansSeparate(operations, sizeof(*operations),',
    'response, sizeof(*response))')) {
    Assert-Contract ($Source.Contains($Token)) "Source missing: $Token"
}
Assert-Contract (Verify $Source) `
    'Launch-probe lifecycle snapshot is incomplete, bypassed, or misordered.'
foreach ($Token in @('non-wrapping and separate', 'local snapshot',
    'zeroed response')) {
    Assert-Contract ($Header.Contains($Token)) "Header missing: $Token"
}
foreach ($Token in @('test_launch_probe_lifecycle_input_spans',
    'exact lifecycle overlap preserves', 'partial lifecycle overlap preserves',
    'wrapping operations rejected', 'wrapping response rejected',
    'test_launch_probe_lifecycle_snapshot',
    'caller lifecycle table was mutated',
    'lifecycle snapshot blocks poison')) {
    Assert-Contract ($Tests.Contains($Token)) "Test missing: $Token"
}

$Mutations = @(
    $Source.Replace('first <= UINTPTR_MAX - (size - 1u)', 'value != NULL'),
    $Source.Replace(
        'LifecycleSpansSeparate(operations, sizeof(*operations),',
        'LifecycleSpansSeparate(response, sizeof(*response),'),
    $Source.Replace('lifecycle = *operations;',
        'lifecycle = (VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS){0};'),
    $Source.Replace('if (!IoctlLaunchProbeOperationsValid(&lifecycle))',
        'if (!IoctlLaunchProbeOperationsValid(operations))'),
    $Source.Replace('transient_stage = lifecycle.ExecuteTransient(',
        'transient_stage = operations->ExecuteTransient('),
    $Source.Replace('return lifecycle.MapResponse(&map_input, response);',
        'return operations->MapResponse(&map_input, response);')
)
foreach ($Mutation in $Mutations) {
    Assert-Contract ($Mutation -ne $Source) `
        'A launch-probe lifecycle mutation did not apply.'
    Assert-Contract (-not (Verify $Mutation)) `
        'A launch-probe lifecycle mutation survived verification.'
}

Write-Host 'PASS: launch-probe lifecycle preflights disjoint inputs and uses one frozen operations snapshot.'
