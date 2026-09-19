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

function Slice([string]$Text, [string]$Begin, [string]$End) {
    $Start = $Text.IndexOf($Begin, [StringComparison]::Ordinal)
    if ($Start -lt 0) { throw "Missing begin marker: $Begin" }
    $Finish = if ([string]::IsNullOrEmpty($End)) {
        $Text.Length
    } else {
        $Text.IndexOf($End, $Start + $Begin.Length,
            [StringComparison]::Ordinal)
    }
    if ($Finish -lt 0) { throw "Missing end marker: $End" }
    return $Text.Substring($Start, $Finish - $Start)
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

function Test-SourceContract([string]$Source) {
    $Preflight = Slice $Source 'static bool IsPlanValidForApply' `
        'VMX_VMWRITE_RESULT VmxApplyVmcsPlan'
    if (-not (Ordered $Preflight @(
        'plan == NULL',
        'plan->Count <= 1',
        'plan->Count > VMX_VMCS_PLAN_CAPACITY',
        'plan->Writes[0].Field != 0',
        'plan->Writes[0].Value != 0',
        'i = 1; i < plan->Count',
        'plan->Writes[i].Field == 0',
        'j = i + 1; j < plan->Count',
        'plan->Writes[i].Field == plan->Writes[j].Field'))) { return $false }

    $Apply = Slice $Source 'VMX_VMWRITE_RESULT VmxApplyVmcsPlan' ''
    return Ordered $Apply @(
        'writer == NULL || !IsPlanValidForApply(plan)',
        'result.Outcome = VMX_VMWRITE_FAIL_INVALID',
        'return result',
        'for (i = 1; i < plan->Count; i++)',
        'writer(plan->Writes[i].Field, plan->Writes[i].Value)',
        'rc == VMX_VMWRITE_SUCCESS',
        'rc == VMX_VMWRITE_FAIL_VALID ? VMX_VMWRITE_FAIL_VALID',
        'VMX_VMWRITE_FAIL_INVALID',
        'result.FailedField = plan->Writes[i].Field',
        'result.Outcome == VMX_VMWRITE_FAIL_VALID')
}

$Source = Get-Content -Raw -LiteralPath $SourcePath
$Header = Get-Content -Raw -LiteralPath $HeaderPath
$Tests = Get-Content -Raw -LiteralPath $TestPath

Assert-Contract (Test-SourceContract $Source) `
    'VMCS-plan application preflight contract is incomplete.'
foreach ($Token in @(
    'Preflights the complete sentinel/count',
    'malformed or empty plan fails invalid',
    'normalizes any non-success/non-fail-valid writer result')) {
    Assert-Contract ($Header.Contains($Token)) "Header missing contract: $Token"
}
foreach ($Name in @(
    'test_adapter_preflights_complete_plan',
    'test_adapter_normalizes_unexpected_writer_result')) {
    Assert-Contract ($Tests.Contains($Name)) "Missing apply test: $Name"
}
Assert-Contract (([regex]::Matches($Tests,
    'Malformed plan performs no VMWRITE')).Count -ge 1) `
    'Malformed-plan tests do not prove a mutation-free writer boundary.'
Assert-Contract (([regex]::Matches($Tests,
    'Malformed plan performs no VMREAD')).Count -ge 1) `
    'Malformed-plan tests do not prove a mutation-free error-read boundary.'

$Mutations = @(
    $Source.Replace('plan->Count <= 1', 'plan->Count == 1'),
    $Source.Replace('plan->Count > VMX_VMCS_PLAN_CAPACITY',
        'plan->Count < VMX_VMCS_PLAN_CAPACITY'),
    $Source.Replace('plan->Writes[0].Field != 0', 'false'),
    $Source.Replace('plan->Writes[0].Value != 0', 'false'),
    $Source.Replace('if (plan->Writes[i].Field == 0) return false;',
        'if (false) return false;'),
    $Source.Replace(
        'if (plan->Writes[i].Field == plan->Writes[j].Field) return false;',
        'if (false) return false;'),
    $Source.Replace('writer == NULL || !IsPlanValidForApply(plan)',
        'writer == NULL'),
    $Source.Replace(
        'rc == VMX_VMWRITE_FAIL_VALID ? VMX_VMWRITE_FAIL_VALID :',
        'rc == VMX_VMWRITE_FAIL_VALID ? rc :')
)
foreach ($Mutation in $Mutations) {
    Assert-Contract ($Mutation -ne $Source) `
        'A VMCS-plan apply mutation did not modify the source.'
    Assert-Contract (-not (Test-SourceContract $Mutation)) `
        'A VMCS-plan apply mutation survived the static contract.'
}

Write-Host 'PASS: VMCS-plan application preflights all software invariants before the first writer call.'
