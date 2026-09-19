[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ExecutorSource,
    [Parameter(Mandatory=$true)][string]$ExecutorHeader,
    [Parameter(Mandatory=$true)][string]$ExecutorTest,
    [Parameter(Mandatory=$true)][string]$CoordinatorSource,
    [Parameter(Mandatory=$true)][string]$CoordinatorHeader,
    [Parameter(Mandatory=$true)][string]$CoordinatorTest
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
function Verify-Executor([string]$Source) {
    $Start = $Source.IndexOf(
        'VMX_EPT_EXECUTION_STATUS VmxEptExecuteInvalidation',
        [StringComparison]::Ordinal)
    if ($Start -lt 0) { return $false }
    $Body = $Source.Substring($Start)
    return $Source.Contains('first <= UINTPTR_MAX - size') -and
        $Source.Contains('static int InputsValid(') -and
        $Source.Contains(
            'Separate(descriptor, sizeof(*descriptor), report, sizeof(*report))') -and
        (Ordered $Body @(
            '!InputsValid(platform, capabilities, request, descriptor, report)',
            'platform_snapshot = *platform;',
            'capabilities_snapshot = *capabilities;',
            'request_snapshot = *request;',
            'descriptor_snapshot = *descriptor;',
            'VmxEptValidateInvalidationDescriptor(',
            'platform_snapshot.GetCurrentIrql(',
            'platform_snapshot.IsVmxRootOwned(',
            'platform_snapshot.Execute('
        )) -and -not $Body.Contains('platform->') -and
        -not $Body.Contains('capabilities->') -and
        -not $Body.Contains('request->') -and
        -not $Body.Contains('descriptor->')
}
function Verify-Coordinator([string]$Source) {
    $Start = $Source.IndexOf(
        'VMX_EPT_INVALIDATION_COORDINATOR_STATUS VmxEptCoordinateInvalidation',
        [StringComparison]::Ordinal)
    if ($Start -lt 0) { return $false }
    $Body = $Source.Substring($Start)
    return Ordered $Body @(
        'VmxEptSessionValidateInvalidationExecution(',
        'identity_snapshot = *identity;',
        'platform_snapshot = *platform;',
        'VmxEptSessionBuildInvalidationForIdentity(',
        'owner, &identity_snapshot, &request, &descriptor)',
        'VmxEptExecuteInvalidation(',
        '&platform_snapshot, &owner->Storage->Domain.Capabilities,',
        'VmxEptSessionAcceptInvalidation(',
        'owner, &identity_snapshot, &staged_report)'
    )
}

$Executor = Get-Content -Raw -LiteralPath $ExecutorSource
$ExecutorH = Get-Content -Raw -LiteralPath $ExecutorHeader
$ExecutorTests = Get-Content -Raw -LiteralPath $ExecutorTest
$Coordinator = Get-Content -Raw -LiteralPath $CoordinatorSource
$CoordinatorH = Get-Content -Raw -LiteralPath $CoordinatorHeader
$CoordinatorTests = Get-Content -Raw -LiteralPath $CoordinatorTest

Assert-Contract (Verify-Executor $Executor) `
    'EPT executor preflight or frozen snapshots are incomplete.'
foreach ($Token in @('complete, non-wrapping, pairwise-disjoint',
    'frozen platform')) {
    Assert-Contract ($ExecutorH.Contains($Token)) `
        "EPT executor header missing: $Token"
}
foreach ($Token in @('test_execution_input_spans',
    'exact platform/report overlap preserves',
    'capabilities/request overlap rejected',
    'wrapping capabilities preserve report',
    'test_execution_platform_snapshot',
    'caller execution platform was mutated')) {
    Assert-Contract ($ExecutorTests.Contains($Token)) `
        "EPT executor test missing: $Token"
}

Assert-Contract (Verify-Coordinator $Coordinator) `
    'EPT invalidation coordinator does not retain one identity/platform snapshot.'
foreach ($Token in @('freezes the admitted identity and callback table',
    'same identity snapshot')) {
    Assert-Contract ($CoordinatorH.Contains($Token)) `
        "EPT coordinator header missing: $Token"
}
foreach ($Token in @('test_identity_snapshot',
    'caller identity was mutated by callback',
    'frozen identity accepts completed generation')) {
    Assert-Contract ($CoordinatorTests.Contains($Token)) `
        "EPT coordinator test missing: $Token"
}

$ExecutorMutations = @(
    $Executor.Replace('first <= UINTPTR_MAX - size', 'value != NULL'),
    $Executor.Replace('platform_snapshot = *platform;',
        'platform_snapshot = (VMX_EPT_EXECUTION_PLATFORM){0};'),
    $Executor.Replace('platform_snapshot.Execute(', 'platform->Execute('),
    $Executor.Replace(
        'Separate(descriptor, sizeof(*descriptor), report, sizeof(*report))',
        'Separate(report, sizeof(*report), report, sizeof(*report))')
)
foreach ($Mutation in $ExecutorMutations) {
    Assert-Contract ($Mutation -ne $Executor) 'An executor mutation did not apply.'
    Assert-Contract (-not (Verify-Executor $Mutation)) `
        'An executor mutation survived verification.'
}
$CoordinatorMutations = @(
    $Coordinator.Replace('identity_snapshot = *identity;',
        'identity_snapshot = (VMX_PROCESSOR_IDENTITY){0};'),
    $Coordinator.Replace('&platform_snapshot, &owner->Storage->Domain.Capabilities,',
        'platform, &owner->Storage->Domain.Capabilities,'),
    $Coordinator.Replace('owner, &identity_snapshot, &staged_report)',
        'owner, identity, &staged_report)')
)
foreach ($Mutation in $CoordinatorMutations) {
    Assert-Contract ($Mutation -ne $Coordinator) `
        'A coordinator mutation did not apply.'
    Assert-Contract (-not (Verify-Coordinator $Mutation)) `
        'A coordinator mutation survived verification.'
}

Write-Host 'PASS: EPT invalidation preflights disjoint inputs and retains frozen executor and coordinator values.'
