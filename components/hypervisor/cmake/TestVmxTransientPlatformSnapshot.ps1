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
function ExecuteBody([string]$Source) {
    $Start = $Source.IndexOf('VMX_LIFECYCLE_STAGE VmxTransientExecute',
        [StringComparison]::Ordinal)
    if ($Start -lt 0) { return '' }
    return $Source.Substring($Start)
}
function Verify([string]$Source) {
    $Body = ExecuteBody $Source
    $Tokens = @(
        'VMX_TRANSIENT_PLATFORM platform;',
        'if (!InputsValid(plat, ctx, reports)) return VMX_STAGE_NOT_STARTED;',
        'platform = *plat;',
        'if (!ValidatePlatform(&platform)) return VMX_STAGE_NOT_STARTED;',
        'saved_cr0 = platform.ReadCr0()',
        'platform.RaiseIrql()',
        'platform.Vmxon(ctx->VmxonPa)',
        'platform.PlanControls(&ctrl, &plan)',
        'platform.PlanHost(&plan, &proc, ctx)',
        'VmxApplyVmcsPlan(&plan, platform.Vmwrite',
        'platform.Launch(&ctx->Assembly)',
        'rc = platform.Vmxoff()',
        'platform.LowerIrql()'
    )
    $Offset = 0
    foreach ($Token in $Tokens) {
        $Found = $Body.IndexOf($Token, $Offset, [StringComparison]::Ordinal)
        if ($Found -lt 0) { return $false }
        $Offset = $Found + $Token.Length
    }
    return -not $Body.Contains('plat->')
}

$Source = Get-Content -Raw -LiteralPath $SourcePath
$Header = Get-Content -Raw -LiteralPath $HeaderPath
$Tests = Get-Content -Raw -LiteralPath $TestPath

Assert-Contract (Verify $Source) `
    'Transient callback snapshot is incomplete, bypassed, or misordered.'
foreach ($Token in @('complete platform table once',
    'immutable local snapshot', 'never rereads')) {
    Assert-Contract ($Header.Contains($Token)) "Header missing: $Token"
}
foreach ($Token in @('t_platform_snapshot_is_immutable',
    'caller platform was mutated', 'snapshot blocks poisoned callbacks',
    'snapshot path completes', 'snapshot path teardown')) {
    Assert-Contract ($Tests.Contains($Token)) "Test missing: $Token"
}

$Mutations = @(
    $Source.Replace('platform = *plat;', 'platform = (VMX_TRANSIENT_PLATFORM){0};'),
    $Source.Replace('if (!ValidatePlatform(&platform))',
        'if (!ValidatePlatform(plat))'),
    $Source.Replace('saved_cr0 = platform.ReadCr0()',
        'saved_cr0 = plat->ReadCr0()'),
    $Source.Replace('platform.Launch(&ctx->Assembly)',
        'plat->Launch(&ctx->Assembly)'),
    $Source.Replace('rc = platform.Vmxoff()', 'rc = plat->Vmxoff()')
)
foreach ($Mutation in $Mutations) {
    Assert-Contract ($Mutation -ne $Source) `
        'A transient callback-snapshot mutation did not apply.'
    Assert-Contract (-not (Verify $Mutation)) `
        'A transient callback-snapshot mutation survived verification.'
}

Write-Host 'PASS: transient execution validates and uses one frozen callback-table snapshot.'
