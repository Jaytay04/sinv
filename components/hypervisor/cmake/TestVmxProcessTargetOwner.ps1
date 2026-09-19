param(
    [Parameter(Mandatory=$true)][string]$ProviderPath,
    [Parameter(Mandatory=$true)][string]$ProviderHeader,
    [Parameter(Mandatory=$true)][string]$OwnerPath,
    [Parameter(Mandatory=$true)][string]$OwnerHeader,
    [Parameter(Mandatory=$true)][string]$PolicyPath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DriverCMake
)
$ErrorActionPreference = 'Stop'
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Require([string]$Text, [string[]]$Tokens, [string]$Name) {
    foreach ($Token in $Tokens) {
        Assert ($Text.Contains($Token)) "$Name missing: $Token"
    }
}
function Verify([string[]]$Text) {
    $Provider = $Text[0]
    $ProviderH = $Text[1]
    $Owner = $Text[2]
    $OwnerH = $Text[3]
    $Policy = $Text[4]
    $Roadmap = $Text[5]
    $Build = $Text[6]

    Require $ProviderH @(
        'VmxPersistentProcessCr3TargetCapture(',
        'Generation',
        'No process pointer is',
        'retained and the output remains zero') 'provider header'
    Require $Provider @(
        'KeStackAttachProcess(',
        'KeUnstackDetachProcess(',
        'VmxCr3TargetSetBuild(',
        'Generation, &Staged') 'process CR3 capture'
    Require $OwnerH @(
        'PEPROCESS Process;',
        'ProcessStartKey;',
        'VmxProcessTargetOwnerConfigure(',
        'VmxProcessTargetOwnerActivate(',
        'VmxProcessTargetOwnerNotifyExit(',
        'VmxProcessTargetOwnerRelease(') 'owner header'
    Require $Owner @(
        'PsLookupProcessByProcessId(',
        'PsGetProcessStartKey(',
        'PsGetProcessExitStatus(Process) == STATUS_PENDING',
        'ObDereferenceObject(Process);',
        'VmxPersistentProcessCr3TargetCapture(',
        'InterlockedExchange(&Owner->State,',
        'HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED',
        'HYPERVISOR_PROCESS_TARGET_FLAG_ROOT_CAPTURED',
        'RtlSecureZeroMemory(&Owner->TargetSet') 'target owner'
    Assert (([regex]::Matches($Owner, 'ObDereferenceObject\(Process\);')).Count -eq 5) `
        'owner does not release every staged/final process reference path'
    Assert ($Owner -notmatch 'TargetSet\.Roots\s*\[') `
        'owner publishes raw CR3 roots'
    Require $Policy @(
        'VMX_PROCESS_TARGET_EVENT_ACTIVATE',
        'VMX_PROCESS_TARGET_EVENT_EXPIRE',
        'VMX_PROCESS_TARGET_EVENT_DEACTIVATE',
        'VMX_PROCESS_TARGET_EVENT_RELEASE') 'state policy'
    Require $Roadmap @(
        'Referenced Process Target Lifecycle',
        'PID reuse cannot retarget',
        'Raw process',
        'Gate 55') 'roadmap'
    Require $Build @('vmx_cr3_target_provider.c','vmx_process_target_owner.c') `
        'driver build'
}

$Paths = @($ProviderPath,$ProviderHeader,$OwnerPath,$OwnerHeader,$PolicyPath,
    $RoadmapPath,$DriverCMake)
$Text = @($Paths | ForEach-Object {
    [IO.File]::ReadAllText((Resolve-Path -LiteralPath $_))
})
Verify $Text

$Mutations = @(
    @{ I=0; T=$Text[0].Replace('KeStackAttachProcess(', 'KeNoAttachProcess(') },
    @{ I=2; T=$Text[2].Replace('PsLookupProcessByProcessId(', 'PsTrustNumericPid(') },
    @{ I=2; T=$Text[2].Replace('PsGetProcessStartKey(', 'PsGetProcessId(') },
    @{ I=2; T=([regex]::new(
        [regex]::Escape('ObDereferenceObject(Process);'))).Replace(
        $Text[2], '/* leaked process reference */', 1) },
    @{ I=2; T=$Text[2].Replace('HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED',
        'HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE') },
    @{ I=2; T=$Text[2].Replace('RtlSecureZeroMemory(&Owner->TargetSet',
        'RtlCopyMemory(&Owner->TargetSet') }
)
foreach ($Mutation in $Mutations) {
    Assert ($Mutation.T -ne $Text[$Mutation.I]) 'process-target mutation was vacuous'
    $Copy = $Text.Clone()
    $Copy[$Mutation.I] = $Mutation.T
    $Rejected = $false
    try { Verify $Copy } catch { $Rejected = $true }
    Assert $Rejected "process-target verifier accepted mutation $($Mutation.I)"
}

Write-Host 'PASS: referenced process-target ownership contracts verified.'
