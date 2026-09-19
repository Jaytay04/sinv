param(
    [Parameter(Mandatory=$true)][string]$MainPath,
    [Parameter(Mandatory=$true)][string]$ServicePath,
    [Parameter(Mandatory=$true)][string]$HeaderPath,
    [Parameter(Mandatory=$true)][string]$TestPath)

$ErrorActionPreference = 'Stop'
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Section([string]$Text, [string]$Begin, [string]$End) {
    $Start = $Text.IndexOf($Begin)
    $Stop = $Text.IndexOf($End, $Start + 1)
    Assert ($Start -ge 0 -and $Stop -gt $Start) "Missing section: $Begin"
    $Text.Substring($Start, $Stop - $Start)
}
function Verify([string]$Main, [string]$Service, [string]$Header, [string]$Tests) {
    foreach ($Token in @(
        'LOADER_OPERATOR_RUNTIME_UNVERIFIED',
        'LOADER_OPERATOR_RUNTIME_DETACHED',
        'LOADER_OPERATOR_RUNTIME_READY',
        'LOADER_OPERATOR_RUNTIME_RESIDUAL',
        'LoaderClassifyOperatorRuntime')) {
        Assert ($Header.Contains($Token)) "Missing header token: $Token"
    }

    $Policy = Section $Service 'LOADER_OPERATOR_RUNTIME_CLASS LoaderClassifyOperatorRuntime' 'LOADER_SCM_CLEANUP_POLICY LoaderDecideScmCleanup'
    foreach ($Token in @(
        '!persistent_query_valid || !ept_query_valid',
        'LOADER_PERSISTENT_WIRE_DETACHED &&',
        'ept_classification == LOADER_EPT_WIRE_DETACHED',
        'LOADER_PERSISTENT_WIRE_LOADED &&',
        'ept_classification == LOADER_EPT_WIRE_READY',
        'return LOADER_OPERATOR_RUNTIME_RESIDUAL;')) {
        Assert ($Policy.Contains($Token)) "Missing policy token: $Token"
    }

    foreach ($Token in @(
        'TestOperatorRuntimeClassification',
        'matching detached evidence is authoritative',
        'matching ready evidence is authoritative',
        'contradictory canonical evidence is residual',
        'failed Runtime query cannot claim readiness',
        'malformed Runtime evidence is unverified',
        'TEST_RUN(TestOperatorRuntimeClassification);')) {
        Assert ($Tests.Contains($Token)) "Missing test token: $Token"
    }

    $Command = Section $Main 'static int CmdStatus(void)' 'int wmain('
    foreach ($Token in @(
        'QueryPersistentStatus(',
        'QueryEptStatus(',
        'QueryCr3Status(',
        'LoaderClassifyOperatorRuntime(',
        'Persistent Runtime: %s',
        'EPT Runtime:        %s',
        'Combined Readiness: %s',
        'CR3 Observation:    %s',
        'Live Gate Evidence: EXTERNAL (not encoded by runtime status)',
        'LOADER_OPERATOR_RUNTIME_READY ||',
        'LOADER_OPERATOR_RUNTIME_DETACHED) &&',
        'cr3_valid')) {
        Assert ($Command.Contains($Token)) "Missing status command token: $Token"
    }
    Assert (-not $Command.Contains('IOCTL_HYPERVISOR_STATUS')) 'Status command used legacy 0x804 surface'
    Assert (-not $Command.Contains('VMX root active')) 'Status command overclaimed VMX root activity'
}

$Main = [IO.File]::ReadAllText((Resolve-Path $MainPath))
$Service = [IO.File]::ReadAllText((Resolve-Path $ServicePath))
$Header = [IO.File]::ReadAllText((Resolve-Path $HeaderPath))
$Tests = [IO.File]::ReadAllText((Resolve-Path $TestPath))
Verify $Main $Service $Header $Tests

$Mutations = @(
    $Service.Replace('!persistent_query_valid || !ept_query_valid', '0 && !persistent_query_valid'),
    $Service.Replace('ept_classification == LOADER_EPT_WIRE_READY', 'ept_classification == LOADER_EPT_WIRE_DETACHED'),
    $Main.Replace('persistent_valid = QueryPersistentStatus(', 'persistent_valid = TRUE; /*'),
    $Main.Replace('ept_valid = QueryEptStatus(&ept_classification);', 'ept_valid = TRUE;'),
    $Main.Replace('cr3_valid = QueryCr3Status(', 'cr3_valid = TRUE; /*'),
    $Main.Replace('"  Live Gate Evidence: EXTERNAL (not encoded by runtime status)\n"', '"  Live Gate Evidence: VERIFIED\n"')
)

for ($Index = 0; $Index -lt $Mutations.Count; ++$Index) {
    $MutatedMain = $Main
    $MutatedService = $Service
    if ($Index -lt 2) { $MutatedService = $Mutations[$Index] }
    else { $MutatedMain = $Mutations[$Index] }
    Assert ($Mutations[$Index] -ne $(if ($Index -lt 2) { $Service } else { $Main })) "Vacuous mutation $Index"
    $Rejected = $false
    try { Verify $MutatedMain $MutatedService $Header $Tests }
    catch { $Rejected = $true }
    Assert $Rejected "Accepted mutation $Index"
}

Write-Output 'Verified authoritative operator status composition, fail-closed evidence classification, and external live-gate disclosure.'
