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
    $Append = Slice $Source 'static uint32_t AppendPlanWrites' `
        'uint32_t VmxPlanControlFields'
    if (-not (Ordered $Append @(
        'plan->Count == 0',
        'plan->Count > VMX_VMCS_PLAN_CAPACITY',
        'VMX_VMCS_PLAN_CAPACITY - plan->Count < write_count',
        'writes[i].Field == 0',
        'plan->Writes[j].Field == writes[i].Field',
        'writes[i].Field == writes[j].Field',
        'plan->Writes[plan->Count + i] = writes[i]',
        'plan->Count += write_count'))) { return $false }

    $Functions = @(
        @( 'uint32_t VmxPlanControlFields',
            'bool VmxVmcsPlanAddWrite' ),
        @( 'uint32_t VmxPlanGuestSegments',
            'uint32_t VmxPlanGuestMachineState' ),
        @( 'uint32_t VmxPlanGuestMachineState',
            'uint32_t VmxPlanHostState' ),
        @( 'uint32_t VmxPlanHostState',
            'VMX_VMWRITE_RESULT VmxApplyVmcsPlan' )
    )
    foreach ($Pair in $Functions) {
        $Body = Slice $Source $Pair[0] $Pair[1]
        if ($Body -notmatch 'AppendPlanWrites\(plan, writes, (field_count|count)\)') {
            return $false
        }
    }

    $Add = Slice $Source 'bool VmxVmcsPlanAddWrite' `
        'uint32_t VmxPlanGuestSegments'
    return $Add.Contains('AppendPlanWrites(plan, &write, 1) == 1')
}

$Source = Get-Content -Raw -LiteralPath $SourcePath
$Header = Get-Content -Raw -LiteralPath $HeaderPath
$Tests = Get-Content -Raw -LiteralPath $TestPath

Assert-Contract (Test-SourceContract $Source) `
    'Transactional VMCS-plan source contract is incomplete.'
Assert-Contract ($Header.Contains('byte-for-byte unchanged on failure')) `
    'Public VMCS-plan transactional contract is undocumented.'
foreach ($Name in @(
    'test_control_fields_duplicate_is_transactional',
    'test_control_fields_overflow_is_transactional',
    'test_guest_segments_failure_is_transactional',
    'test_machine_state_failure_is_transactional',
    'test_host_state_failure_is_transactional')) {
    Assert-Contract ($Tests.Contains($Name)) "Missing transaction test: $Name"
}
Assert-Contract (([regex]::Matches($Tests,
    'memcmp\(&plan, &before, sizeof\(plan\)\) == 0')).Count -ge 8) `
    'Failure tests do not prove byte-for-byte plan preservation.'

$Mutations = @(
    $Source.Replace(
        'VMX_VMCS_PLAN_CAPACITY - plan->Count < write_count',
        'VMX_VMCS_PLAN_CAPACITY - plan->Count > write_count'),
    $Source.Replace(
        'if (writes[i].Field == 0) return 0;',
        'if (false) return 0;'),
    $Source.Replace(
        'if (plan->Writes[j].Field == writes[i].Field) return 0;',
        'if (false) return 0;'),
    $Source.Replace(
        'if (writes[i].Field == writes[j].Field) return 0;',
        'if (false) return 0;'),
    $Source.Replace(
        'return AppendPlanWrites(plan, writes, count);',
        'return count;')
)
foreach ($Mutation in $Mutations) {
    Assert-Contract (-not (Test-SourceContract $Mutation)) `
        'A VMCS-plan transaction mutation survived the static contract.'
}

Write-Host 'PASS: VMCS-plan builders preflight complete appends and preserve caller state on failure.'
