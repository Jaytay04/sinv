param(
    [Parameter(Mandatory=$true)][string]$AbiPath,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$PolicyPath,
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
    $Abi = $Text[0]
    $Interface = $Text[1]
    $Policy = $Text[2]
    $RequestFrame = $Text[3]
    $ResponseFrame = $Text[4]
    $Ioctl = $Text[5]
    $Loader = $Text[6]
    $Gate = $Text[7]
    $Roadmap = $Text[8]
    $Doc = $Text[9]

    Require-Tokens $Abi @(
        'HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST_SIZE'
        'HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST'
        'sizeof(HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST) == 16'
    ) 'release ABI'
    Require-Tokens $Interface @(
        'IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE'
        '0x81A, METHOD_BUFFERED, FILE_ANY_ACCESS'
    ) 'release IOCTL'
    Require-Tokens $Policy @(
        'VmxProcessTargetReleaseRequestValidate('
        'HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST_SIZE'
    ) 'release policy'
    Require-Tokens $RequestFrame @(
        'IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE'
        'sizeof(HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST)'
    ) 'release request frame'
    Require-Tokens $ResponseFrame @(
        'IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE'
        'HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE'
    ) 'release response frame'
    Assert-Ordered $Ioctl @(
        'static NTSTATUS IoctlDispatchProcessTargetRelease('
        'VmxProcessTargetReleaseRequestValidate('
        'VmxPersistentDriverOwnerQueryPhase('
        'Phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED'
        'VmxProcessTargetOwnerRelease('
        'VmxProcessTargetOwnerQuery('
        'Response.State != HYPERVISOR_PROCESS_TARGET_STATE_EMPTY'
        'IoctlCompleteBufferedResponse(Request, &Response, sizeof(Response))'
    ) 'detached release dispatch'
    Require-Tokens $Ioctl @(
        'Response.ProcessId != 0'
        'Response.ProcessStartKey != 0'
        'Response.TargetGeneration != 0'
        'Response.Flags != 0'
        'Response.Reserved != 0'
    ) 'canonical empty response'
    Assert-Ordered $Loader @(
        'static BOOL ReleaseProcessTarget(BOOL ExpectBusy)'
        'IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE'
        'SUCCESS: Process target released to canonical EMPTY state.'
        'static int CmdLoadInternal('
        'if (verify_detached_rebind)'
        'ReleaseProcessTarget(FALSE)'
        'ConfigureCurrentProcessTarget()'
        'Sending IOCTL_LOAD'
        'ReleaseProcessTarget(TRUE)'
        'READY: Process target is ACTIVE'
    ) 'loader rebind transaction'
    Require-Tokens $Loader @(
        'load-target-self-rebind'
        'target-release'
        'SUCCESS: Active process-target release was denied.'
    ) 'loader commands'
    Require-Tokens $Gate @(
        'gate56-process-target-rebind'
        "-ArgumentList 'load-target-self-rebind'"
        'SUCCESS: Process target released to canonical EMPTY state.'
        'SUCCESS: Active process-target release was denied.'
        'if ($null -eq $OutText) { $OutText = [string]::Empty }'
        'Count -ne 2'
        'DetachedRelease=EMPTY'
        'ReconfiguredState=CONFIGURED'
        'ActiveRelease=DENIED_BUSY'
        'ExitState=EXPIRED'
        "Invoke-Loader 'unload' 'unload'"
        'Result=PASS'
        'tar.exe -a -c -f'
    ) 'Gate 56'
    Require-Tokens $Roadmap @(
        'Explicit Process Target Rebinding'
        'ACTIVE release'
        'remains denied'
        'Gate 56 is one deterministic disposable-VM run'
    ) 'Runtime V18 roadmap'
    Require-Tokens $Doc @(
        'Gate 56: explicit process-target rebind'
        'gate56-process-target-rebind.ps1'
        'load-target-self-rebind'
    ) 'operator documentation'

    $Errors = $null
    [Management.Automation.Language.Parser]::ParseInput(
        $Gate, [ref]$null, [ref]$Errors) | Out-Null
    Assert-True ($Errors.Count -eq 0) `
        ('Gate 56 parse errors: ' + ($Errors.Message -join '; '))
}

$Paths = @($AbiPath, $InterfacePath, $PolicyPath, $RequestFramePath,
    $ResponseFramePath, $IoctlPath, $LoaderPath, $GatePath, $RoadmapPath,
    $DocPath)
$Text = @($Paths | ForEach-Object {
    [IO.File]::ReadAllText((Resolve-Path -LiteralPath $_))
})
Verify $Text

$Mutations = @(
    @{ Index=2; Text=$Text[2].Replace(
        'HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST_SIZE,',
        'HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST_SIZE,') },
    @{ Index=5; Text=$Text[5].Replace(
        'Phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED', 'FALSE') },
    @{ Index=5; Text=$Text[5].Replace(
        'VmxProcessTargetOwnerRelease(', 'VmxProcessTargetOwnerQuery(') },
    @{ Index=6; Text=$Text[6].Replace(
        'ReleaseProcessTarget(TRUE)', 'ReleaseProcessTarget(FALSE)') },
    @{ Index=7; Text=$Text[7].Replace(
        'if ($null -eq $OutText) { $OutText = [string]::Empty }',
        'if ($null -ne $OutText) { $OutText = [string]::Empty }') },
    @{ Index=7; Text=$Text[7].Replace(
        'ActiveRelease=DENIED_BUSY', 'ActiveRelease=ACCEPTED') }
)
foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation.Text -ne $Text[$Mutation.Index]) `
        'process-target rebind mutation was vacuous'
    $Copy = $Text.Clone()
    $Copy[$Mutation.Index] = $Mutation.Text
    $Rejected = $false
    try { Verify $Copy } catch { $Rejected = $true }
    Assert-True $Rejected `
        "process-target rebind verifier accepted mutation $($Mutation.Index)"
}

Write-Host 'PASS: exact detached process-target release, rebind, active denial, Gate 56 lifecycle, and mutation rejection verified.'
