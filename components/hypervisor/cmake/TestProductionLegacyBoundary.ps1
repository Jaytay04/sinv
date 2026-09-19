param(
    [Parameter(Mandatory = $true)][string]$CoreCMakePath,
    [Parameter(Mandatory = $true)][string]$TestCMakePath,
    [Parameter(Mandatory = $true)][string]$PublicVmxHeaderPath,
    [Parameter(Mandatory = $true)][string]$PublicEptHeaderPath,
    [Parameter(Mandatory = $true)][string]$CpuFeaturesPath,
    [Parameter(Mandatory = $true)][string]$DriverSourcePath
)

$ErrorActionPreference = 'Stop'

function Assert-Contract([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Read-RequiredFile([string]$Path) {
    Assert-Contract (Test-Path -LiteralPath $Path -PathType Leaf) `
        "Required file is missing: $Path"
    return [IO.File]::ReadAllText((Resolve-Path -LiteralPath $Path))
}

function Verify-Boundary([string[]]$Text) {
    $CoreCMake, $TestCMake, $PublicVmx, $PublicEpt, $CpuFeatures, $DriverText = $Text

    Assert-Contract ($CoreCMake -notmatch '(?m)^\s*vmx_vmexit\.c\s*$') `
        'The historical VM-exit model must not be linked into hypervisor_core.'
    Assert-Contract ($TestCMake -match '(?m)^\s*\.\./hypervisor/vmx_vmexit\.c\s*$') `
        'The historical VM-exit model must remain isolated in test_vmexit.'

    $ForbiddenPublicVmx = @(
        'VmxHandleExit', 'VmxAssignVpid',
        'VmxInvVpidSingleContext', 'VmxInvVpidAllContexts'
    )
    foreach ($Symbol in $ForbiddenPublicVmx) {
        Assert-Contract ($PublicVmx -notmatch "\b$([regex]::Escape($Symbol))\b") `
            "Legacy symbol remains in the public VMX API: $Symbol"
    }

    $ForbiddenCpuIo = @(
        'CpuHasVmx', 'CpuHasEpt', 'CpuHasVpid', 'CpuHasUnrestrictedGuest',
        'CpuReadVmcsRevisionId', 'CpuHasCet', 'CpuLogicalProcessorCount',
        'MtrrReadFixedRanges', 'MtrrReadVariableRanges'
    )
    foreach ($Symbol in $ForbiddenCpuIo) {
        Assert-Contract ($CpuFeatures -notmatch "\b$([regex]::Escape($Symbol))\b") `
            "Unsupported hardware wrapper remains in the pure core: $Symbol"
        Assert-Contract ($PublicEpt -notmatch "\b$([regex]::Escape($Symbol))\b") `
            "Unsupported hardware wrapper remains in the public EPT API: $Symbol"
    }

    foreach ($Symbol in $ForbiddenPublicVmx + $ForbiddenCpuIo) {
        Assert-Contract ($DriverText -notmatch "\b$([regex]::Escape($Symbol))\b") `
            "Production driver references a quarantined legacy symbol: $Symbol"
    }
}

$DriverText = (Get-ChildItem -LiteralPath $DriverSourcePath -Recurse -File |
    Where-Object Extension -in '.c', '.h', '.cpp', '.asm' |
    ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
$Text = @(
    (Read-RequiredFile $CoreCMakePath),
    (Read-RequiredFile $TestCMakePath),
    (Read-RequiredFile $PublicVmxHeaderPath),
    (Read-RequiredFile $PublicEptHeaderPath),
    (Read-RequiredFile $CpuFeaturesPath),
    $DriverText
)

Verify-Boundary $Text

$Mutations = @(
    @{ Index = 0; Value = $Text[0].Replace(
        "    ept_manager.c", "    vmx_vmexit.c`n    ept_manager.c") },
    @{ Index = 1; Value = $Text[1] -replace
        '(?m)^\s*\.\./hypervisor/vmx_vmexit\.c\s*\r?\n', '' },
    @{ Index = 2; Value = $Text[2].Replace(
        '/* Fast-path handlers */', "int VmxHandleExit(vm_exit_context_t* ctx);`n`n/* Fast-path handlers */") },
    @{ Index = 4; Value = $Text[4] + "`nbool CpuHasVmx(void) { return false; }`n" },
    @{ Index = 5; Value = $Text[5] + "`nvoid DriverBoundaryMutation(void) { (void)VmxHandleExit; }`n" }
)

$MutationNumber = 0
foreach ($Mutation in $Mutations) {
    $MutationNumber++
    Assert-Contract ($Mutation.Value -ne $Text[$Mutation.Index]) `
        "Boundary mutation $MutationNumber was vacuous."
    $Copy = $Text.Clone()
    $Copy[$Mutation.Index] = $Mutation.Value
    $Rejected = $false
    try { Verify-Boundary $Copy } catch { $Rejected = $true }
    Assert-Contract $Rejected "Boundary mutation $MutationNumber was accepted."
}

Write-Host 'PASS: production legacy boundary and mutations verified.'
