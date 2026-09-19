param(
    [Parameter(Mandatory=$true)][string]$AbiPath,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$PolicyPath,
    [Parameter(Mandatory=$true)][string]$OwnerPath,
    [Parameter(Mandatory=$true)][string]$OwnerHeaderPath,
    [Parameter(Mandatory=$true)][string]$ContextPath,
    [Parameter(Mandatory=$true)][string]$DriverMainPath,
    [Parameter(Mandatory=$true)][string]$RequestFramePath,
    [Parameter(Mandatory=$true)][string]$ResponseFramePath,
    [Parameter(Mandatory=$true)][string]$IoctlPath,
    [Parameter(Mandatory=$true)][string]$LoaderPath,
    [Parameter(Mandatory=$true)][string]$GatePath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DocPath
)
$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Require-Tokens([string]$Text, [string[]]$Tokens, [string]$Name) {
    foreach ($Token in $Tokens) {
        Assert-True ($Text.Contains($Token)) "$Name missing: $Token"
    }
}

function Assert-Ordered(
    [string]$Text,
    [string[]]$Tokens,
    [string]$Name
) {
    $Previous = -1
    foreach ($Token in $Tokens) {
        $Current = $Text.IndexOf($Token, $Previous + 1,
            [StringComparison]::Ordinal)
        Assert-True ($Current -gt $Previous) "$Name order changed at: $Token"
        $Previous = $Current
    }
}

function Verify([string[]]$Text) {
    $Abi = $Text[0]; $Interface = $Text[1]; $Policy = $Text[2]
    $Owner = $Text[3]; $OwnerHeader = $Text[4]; $Context = $Text[5]
    $DriverMain = $Text[6]; $RequestFrame = $Text[7]
    $ResponseFrame = $Text[8]; $Ioctl = $Text[9]; $Loader = $Text[10]
    $Gate = $Text[11]; $Roadmap = $Text[12]; $Doc = $Text[13]

    Require-Tokens $Abi @(
        'HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST_SIZE'
        'HYPERVISOR_PROCESS_TARGET_STATUS_STALE'
        'HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST'
        'sizeof(HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST) == 40'
        'TargetGeneration) == 32') 'matched-release ABI'
    Require-Tokens $Interface @(
        'IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED'
        '0x81B, METHOD_BUFFERED, FILE_ANY_ACCESS') 'matched-release IOCTL'
    Require-Tokens $Policy @(
        'VmxProcessTargetReleaseMatchedRequestValidate('
        'HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST_SIZE'
        'Request->ProcessId == 0'
        'Request->ProcessStartKey == 0'
        'Request->TargetGeneration == 0') 'matched request policy'
    Require-Tokens $OwnerHeader @(
        'VMX_PROCESS_TARGET_OWNER_MISMATCH = -7'
        'VmxProcessTargetOwnerReleaseMatched(') 'matched owner surface'
    Assert-Ordered $Owner @(
        'VmxProcessTargetOwnerReleaseMatched('
        'HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED'
        'Owner->ProcessId != ProcessId'
        'Owner->ProcessStartKey != ProcessStartKey'
        'Owner->TargetSet.Generation != TargetGeneration'
        'return VMX_PROCESS_TARGET_OWNER_MISMATCH;'
        'Status = ReleaseState(Owner, State);') 'compare before release'
    Require-Tokens $Context @('uint64_t ProcessTargetGeneration;') `
        'device generation owner'
    Require-Tokens $DriverMain @(
        'persistent_context->ProcessTargetGeneration = 0;') `
        'generation initialization'
    Require-Tokens $RequestFrame @(
        'IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED'
        'sizeof(HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST)') `
        'request frame'
    Require-Tokens $ResponseFrame @(
        'IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED'
        'HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE') 'response frame'
    Assert-Ordered $Ioctl @(
        'static NTSTATUS IoctlDispatchProcessTargetConfigure('
        'Context->ProcessTargetGeneration == UINT64_MAX'
        'TargetGeneration = Context->ProcessTargetGeneration + 1;'
        'VmxProcessTargetOwnerConfigure('
        'Context->ProcessTargetGeneration = TargetGeneration;') `
        'successful generation publication'
    Assert-Ordered $Ioctl @(
        'static NTSTATUS IoctlDispatchProcessTargetReleaseMatched('
        'VmxProcessTargetReleaseMatchedRequestValidate('
        'Phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED'
        'VmxProcessTargetOwnerReleaseMatched('
        'VMX_PROCESS_TARGET_OWNER_MISMATCH'
        'Response.Status = HYPERVISOR_PROCESS_TARGET_STATUS_STALE'
        'Response.State != HYPERVISOR_PROCESS_TARGET_STATE_EMPTY') `
        'matched release dispatch'
    Require-Tokens $Loader @(
        'load-target-self-matched-rebind'
        'ReleaseProcessTargetMatched('
        'HYPERVISOR_PROCESS_TARGET_STATUS_STALE'
        'SUCCESS: Stale generation-matched process-target release was '
        'denied without mutation.\n'
        'SUCCESS: Generation-matched process target released to '
        'canonical EMPTY state.\n'
        'rebound_target.TargetGeneration =='
        'SUCCESS: Process-target rebind published a new generation.'
        'SUCCESS: Active generation-matched process-target release was '
        'denied.\n') `
        'loader matched transaction'
    Require-Tokens $Gate @(
        'gate57-process-target-matched-release'
        "-ArgumentList 'load-target-self-matched-rebind'"
        'Process Target: status=6 state=1'
        'StaleRelease=DENIED_NO_MUTATION'
        'MatchedRelease=EMPTY'
        'GenerationAdvanced=YES'
        'ActiveMatchedRelease=DENIED_BUSY'
        'ExitState=EXPIRED'
        'Result=PASS') 'Gate 57'
    Require-Tokens $Roadmap @(
        'Generation-Matched Process Target Release'
        'device-lifetime binding epoch'
        'same-process'
        'Gate 57 is one deterministic disposable-VM run') 'Runtime V19 roadmap'
    Require-Tokens $Doc @(
        'Gate 57: generation-matched process-target release'
        'gate57-process-target-matched-release.ps1'
        'load-target-self-matched-rebind') 'operator documentation'

    $Errors = $null
    [Management.Automation.Language.Parser]::ParseInput(
        $Gate, [ref]$null, [ref]$Errors) | Out-Null
    Assert-True ($Errors.Count -eq 0) `
        ('Gate 57 parse errors: ' + ($Errors.Message -join '; '))
}

$Paths = @($AbiPath,$InterfacePath,$PolicyPath,$OwnerPath,$OwnerHeaderPath,
    $ContextPath,$DriverMainPath,$RequestFramePath,$ResponseFramePath,
    $IoctlPath,$LoaderPath,$GatePath,$RoadmapPath,$DocPath)
$Text = @($Paths | ForEach-Object {
    [IO.File]::ReadAllText((Resolve-Path -LiteralPath $_))
})
Verify $Text

$Mutations = @(
    @{ I=2; T=$Text[2].Replace('Request->TargetGeneration == 0', 'FALSE') },
    @{ I=3; T=$Text[3].Replace(
        'Owner->TargetSet.Generation != TargetGeneration', 'FALSE') },
    @{ I=9; T=$Text[9].Replace(
        'Context->ProcessTargetGeneration + 1',
        'Context->ProcessTargetGeneration') },
    @{ I=9; T=$Text[9].Replace(
        'Response.Status = HYPERVISOR_PROCESS_TARGET_STATUS_STALE',
        'Response.Status = HYPERVISOR_PROCESS_TARGET_STATUS_SUCCESS') },
    @{ I=10; T=$Text[10].Replace(
        'rebound_target.TargetGeneration ==',
        'rebound_target.TargetGeneration !=') },
    @{ I=11; T=$Text[11].Replace(
        'GenerationAdvanced=YES', 'GenerationAdvanced=NO') }
)
foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation.T -ne $Text[$Mutation.I]) `
        'matched-release mutation was vacuous'
    $Copy = $Text.Clone(); $Copy[$Mutation.I] = $Mutation.T
    $Rejected = $false
    try { Verify $Copy } catch { $Rejected = $true }
    Assert-True $Rejected `
        "matched-release verifier accepted mutation $($Mutation.I)"
}

Write-Host 'PASS: generation-matched process-target release and Gate 57 contracts verified.'
