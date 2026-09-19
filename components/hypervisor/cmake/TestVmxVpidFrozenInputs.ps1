[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ExecutorSource,
    [Parameter(Mandatory=$true)][string]$ExecutorHeader,
    [Parameter(Mandatory=$true)][string]$ExecutorTest,
    [Parameter(Mandatory=$true)][string]$VmcsSource,
    [Parameter(Mandatory=$true)][string]$VmcsHeader,
    [Parameter(Mandatory=$true)][string]$VmcsTest
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
function FunctionBody([string]$Text, [string]$StartToken,
    [string]$EndToken) {
    $Start = $Text.IndexOf($StartToken, [StringComparison]::Ordinal)
    if ($Start -lt 0) { return '' }
    if ([string]::IsNullOrEmpty($EndToken)) { return $Text.Substring($Start) }
    $End = $Text.IndexOf($EndToken, $Start + $StartToken.Length,
        [StringComparison]::Ordinal)
    if ($End -lt 0) { return '' }
    return $Text.Substring($Start, $End - $Start)
}
function Verify-Executor([string]$Source) {
    $Body = FunctionBody $Source 'VMX_VPID_EXECUTION_STATUS VmxVpidExecuteInvalidation' ''
    if ([string]::IsNullOrEmpty($Body)) { return $false }
    return $Source.Contains('First <= UINTPTR_MAX - Size') -and
        $Source.Contains(
            'Separate(Descriptor, sizeof(*Descriptor), Report, sizeof(*Report))') -and
        (Ordered $Body @(
        'if (!InputsValid(Platform, Capabilities, Request, Descriptor, Report))',
        'PlatformSnapshot = *Platform;',
        'CapabilitiesSnapshot = *Capabilities;',
        'RequestSnapshot = *Request;',
        'DescriptorSnapshot = *Descriptor;',
        'VmxVpidValidateInvalidationDescriptor(',
        'PlatformSnapshot.GetCurrentIrql(',
        'PlatformSnapshot.IsVmxRootOwned(',
        'PlatformSnapshot.Execute('
    )) -and -not $Body.Contains('Platform->') -and
        -not $Body.Contains('Request->') -and
        -not $Body.Contains('Capabilities->') -and
        -not $Body.Contains('Descriptor->')
}
function Verify-Vmcs([string]$Source) {
    $Activate = FunctionBody $Source 'VMX_VPID_VMCS_STATUS VmxVpidVmcsActivate' `
        'VMX_VPID_VMCS_STATUS VmxVpidVmcsDeactivate'
    $Deactivate = FunctionBody $Source `
        'VMX_VPID_VMCS_STATUS VmxVpidVmcsDeactivate' ''
    if ([string]::IsNullOrEmpty($Activate) -or
        [string]::IsNullOrEmpty($Deactivate)) { return $false }
    $ActivateOk = Ordered $Activate @(
        '!ActivationInputsValid(Capabilities, Platform, Report)',
        'CapabilitiesSnapshot = *Capabilities;',
        'PlatformSnapshot = *Platform;',
        'PlatformValid(&PlatformSnapshot)',
        'VmxVpidValidateCapabilities(&CapabilitiesSnapshot)',
        'Read(&PlatformSnapshot',
        'Write(&PlatformSnapshot',
        'Rollback(&PlatformSnapshot'
    )
    $DeactivateOk = Ordered $Deactivate @(
        '!Separate(Platform, sizeof(*Platform), Report, sizeof(*Report))',
        'PlatformSnapshot = *Platform;',
        'PlatformValid(&PlatformSnapshot)',
        'Read(&PlatformSnapshot',
        'Write(&PlatformSnapshot'
    )
    return $Source.Contains('First <= UINTPTR_MAX - Size') -and
        $ActivateOk -and $DeactivateOk -and
        -not $Activate.Contains('Platform->') -and
        -not $Deactivate.Contains('Platform->')
}

$Executor = Get-Content -Raw -LiteralPath $ExecutorSource
$ExecutorH = Get-Content -Raw -LiteralPath $ExecutorHeader
$ExecutorTests = Get-Content -Raw -LiteralPath $ExecutorTest
$Vmcs = Get-Content -Raw -LiteralPath $VmcsSource
$VmcsH = Get-Content -Raw -LiteralPath $VmcsHeader
$VmcsTests = Get-Content -Raw -LiteralPath $VmcsTest

foreach ($Token in @('static int Span(', 'First <= UINTPTR_MAX - Size',
    'static int InputsValid(',
    'Separate(Platform, sizeof(*Platform), Report, sizeof(*Report))',
    'Separate(Capabilities, sizeof(*Capabilities),',
    'Separate(Request, sizeof(*Request),',
    'Separate(Descriptor, sizeof(*Descriptor), Report, sizeof(*Report))')) {
    Assert-Contract ($Executor.Contains($Token)) "Executor missing: $Token"
}
Assert-Contract (Verify-Executor $Executor) `
    'VPID executor preflight or frozen snapshots are incomplete.'
foreach ($Token in @('static int Span(', 'First <= UINTPTR_MAX - Size',
    'static int ActivationInputsValid(',
    'Separate(Platform, sizeof(*Platform), Report, sizeof(*Report))')) {
    Assert-Contract ($Vmcs.Contains($Token)) "VMCS coordinator missing: $Token"
}
Assert-Contract (Verify-Vmcs $Vmcs) `
    'VPID VMCS preflight or frozen snapshots are incomplete.'
foreach ($Text in @($ExecutorH, $VmcsH)) {
    Assert-Contract ($Text.Contains('non-wrapping')) `
        'Header does not document non-wrapping inputs.'
    Assert-Contract ($Text.Contains('frozen')) `
        'Header does not document frozen inputs.'
}
foreach ($Token in @('test_execution_input_spans',
    'exact platform/report overlap preserves',
    'capabilities/request overlap rejected',
    'wrapping capabilities preserve report',
    'test_execution_platform_snapshot',
    'execution snapshot blocks poison')) {
    Assert-Contract ($ExecutorTests.Contains($Token)) `
        "Executor test missing: $Token"
}
foreach ($Token in @('test_vmcs_input_spans',
    'exact VMCS platform/report overlap preserves',
    'capabilities/platform overlap rejected',
    'wrapping VMCS platform preserves report',
    'test_vmcs_platform_snapshot', 'VMCS snapshot blocks poison')) {
    Assert-Contract ($VmcsTests.Contains($Token)) "VMCS test missing: $Token"
}

$ExecutorMutations = @(
    $Executor.Replace('First <= UINTPTR_MAX - Size', 'Value != NULL'),
    $Executor.Replace('PlatformSnapshot = *Platform;',
        'PlatformSnapshot = (VMX_VPID_EXECUTION_PLATFORM){0};'),
    $Executor.Replace('PlatformSnapshot.Execute(', 'Platform->Execute('),
    $Executor.Replace(
        'Separate(Descriptor, sizeof(*Descriptor), Report, sizeof(*Report))',
        'Separate(Report, sizeof(*Report), Report, sizeof(*Report))')
)
foreach ($Mutation in $ExecutorMutations) {
    Assert-Contract ($Mutation -ne $Executor) `
        'An executor mutation did not apply.'
    Assert-Contract (-not (Verify-Executor $Mutation)) `
        'An executor mutation survived verification.'
}
$VmcsMutations = @(
    $Vmcs.Replace('First <= UINTPTR_MAX - Size', 'Value != NULL'),
    $Vmcs.Replace('PlatformSnapshot = *Platform;',
        'PlatformSnapshot = (VMX_VPID_VMCS_PLATFORM){0};'),
    $Vmcs.Replace('VmxVpidValidateCapabilities(&CapabilitiesSnapshot)',
        'VmxVpidValidateCapabilities(Capabilities)'),
    $Vmcs.Replace('Rollback(&PlatformSnapshot', 'Rollback(Platform')
)
foreach ($Mutation in $VmcsMutations) {
    Assert-Contract ($Mutation -ne $Vmcs) 'A VMCS mutation did not apply.'
    Assert-Contract (-not (Verify-Vmcs $Mutation)) `
        'A VMCS mutation survived verification.'
}

Write-Host 'PASS: VPID executor and VMCS coordinator preflight disjoint inputs and use frozen snapshots.'
