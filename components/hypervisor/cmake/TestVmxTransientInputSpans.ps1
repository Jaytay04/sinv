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
        'static bool SpanValid',
        'first <= UINTPTR_MAX - (size - 1u)',
        'static bool Separate',
        'l > r + right_size - 1u || r > l + left_size - 1u',
        'static bool InputsValid',
        '!SpanValid(plat, sizeof(*plat))',
        '!SpanValid(ctx, sizeof(*ctx))',
        '!Separate(plat, sizeof(*plat), ctx, sizeof(*ctx))',
        '!SpanValid(reports, sizeof(*reports))',
        '!Separate(reports, sizeof(*reports), plat, sizeof(*plat))',
        '!Separate(reports, sizeof(*reports), ctx, sizeof(*ctx))',
        'VMX_LIFECYCLE_STAGE VmxTransientExecute',
        'if (!InputsValid(plat, ctx, reports)) return VMX_STAGE_NOT_STARTED',
        'platform = *plat',
        'if (!ValidatePlatform(&platform)) return VMX_STAGE_NOT_STARTED',
        'if (reports != NULL) (void)memset(reports, 0, sizeof(*reports))',
        'saved_cr0 = platform.ReadCr0()'
    )
}

$Source = Get-Content -Raw -LiteralPath $SourcePath
$Header = Get-Content -Raw -LiteralPath $HeaderPath
$Tests = Get-Content -Raw -LiteralPath $TestPath

Assert-Contract (Verify $Source) `
    'Transient input span contract is incomplete or misordered.'
foreach ($Token in @('pairwise separate', 'before any input mutation',
    'preserves every caller-owned byte')) {
    Assert-Contract ($Header.Contains($Token)) "Header missing: $Token"
}
foreach ($Token in @('t_input_spans_preflight',
    'exact platform context overlap preserves',
    'partial platform context overlap preserves',
    'exact platform report overlap preserves',
    'partial platform report overlap preserves',
    'exact context report overlap preserves',
    'partial context report overlap preserves',
    'wrapping platform rejected', 'wrapping context rejected',
    'wrapping report rejected', 'invalid platform preserves report')) {
    Assert-Contract ($Tests.Contains($Token)) "Test missing: $Token"
}

$Mutations = @(
    $Source.Replace('first <= UINTPTR_MAX - (size - 1u)', 'value != NULL'),
    $Source.Replace(
        '!Separate(plat, sizeof(*plat), ctx, sizeof(*ctx))', 'false'),
    $Source.Replace(
        '!Separate(reports, sizeof(*reports), plat, sizeof(*plat))', 'false'),
    $Source.Replace(
        '!Separate(reports, sizeof(*reports), ctx, sizeof(*ctx))', 'false'),
    $Source.Replace(
        'if (!InputsValid(plat, ctx, reports)) return VMX_STAGE_NOT_STARTED;',
        'if (plat == NULL || ctx == NULL) return VMX_STAGE_NOT_STARTED;')
)
foreach ($Mutation in $Mutations) {
    Assert-Contract ($Mutation -ne $Source) `
        'A transient input-span mutation did not apply.'
    Assert-Contract (-not (Verify $Mutation)) `
        'A transient input-span mutation survived verification.'
}

Write-Host 'PASS: transient execution rejects wrapping and overlapping caller-owned inputs before mutation.'
