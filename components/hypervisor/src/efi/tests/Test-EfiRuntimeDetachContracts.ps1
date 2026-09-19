[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$EfiSourceRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Ordered([string]$Text, [string[]]$Tokens, [string]$Name) {
    $Position = -1
    foreach ($Token in $Tokens) {
        $Next = $Text.IndexOf($Token, $Position + 1,
            [StringComparison]::Ordinal)
        if ($Next -lt 0) {
            throw "$Name is missing or misorders: $Token"
        }
        $Position = $Next
    }
}

$Root = [IO.Path]::GetFullPath($EfiSourceRoot)
$Asm = Get-Content -Raw -LiteralPath (Join-Path $Root 'efi_hypervisor.asm')
$Inc = Get-Content -Raw -LiteralPath (Join-Path $Root 'efi_hypervisor.inc')
$Probe = Get-Content -Raw -LiteralPath (
    Join-Path $Root 'uefi_hypervisor_post_ebs_probe.asm')

foreach ($Token in @(
        'HV_EFI_CALL_QUERY_RUNTIME_SESSION EQU 3',
        'HV_EFI_CALL_PREPARE_NATIVE_DETACH EQU 4',
        'HV_EFI_CALL_COMMIT_NATIVE_DETACH EQU 5',
        'CONTEXT_RUNTIME_TOKEN0          EQU 912',
        'CONTEXT_RUNTIME_GENERATION      EQU 944',
        'CONTEXT_RUNTIME_STATE           EQU 952')) {
    Assert-True ($Inc.Contains($Token)) "Runtime ABI contract missing: $Token"
}

$DebugStart = $Asm.IndexOf('HvEfiDebugOutByte PROC',
    [StringComparison]::Ordinal)
$DebugEnd = $Asm.IndexOf('HvEfiDebugOutByte ENDP', $DebugStart,
    [StringComparison]::Ordinal)
Assert-True ($DebugStart -ge 0 -and $DebugEnd -gt $DebugStart) `
    'EFI serial debug writer boundaries are missing.'
$Debug = $Asm.Substring($DebugStart, $DebugEnd - $DebugStart)
Assert-Ordered $Debug @(
    'cmp cx, SERIAL_DEBUG_PORT',
    'mov r9b, al',
    'mov r8d, 100000',
    'mov dx, SERIAL_DEBUG_PORT + 5',
    'in al, dx',
    'test al, 020h',
    'jnz DebugOutByteReady',
    'dec r8d',
    'jnz DebugOutByteWaitForThr',
    'mov al, r9b',
    'out dx, al') 'Bounded EFI serial THR readiness'

$PrepareStart = $Asm.IndexOf('ResidentPrepareNativeDetach:',
    [StringComparison]::Ordinal)
$PrepareEnd = $Asm.IndexOf('ResidentArmGuestInterruptGuard:', $PrepareStart,
    [StringComparison]::Ordinal)
Assert-True ($PrepareStart -ge 0 -and $PrepareEnd -gt $PrepareStart) `
    'Resident detach block boundaries are missing.'
$Prepare = $Asm.Substring($PrepareStart, $PrepareEnd - $PrepareStart)
Assert-Ordered $Prepare @(
    'call ResidentValidateRuntimeCaller',
    'CONTEXT_RUNTIME_TOKEN0',
    'CONTEXT_RUNTIME_TOKEN3',
    'CONTEXT_RUNTIME_GENERATION',
    'VMCS_HOST_CR3',
    'VMCS_HOST_RSP',
    'VMCS_HOST_RIP',
    'CONTEXT_RUNTIME_TOKEN0], 0',
    'HV_EFI_RUNTIME_STATE_PIVOT_ARMED',
    'ResidentAdvanceRip') 'Resident detach preparation'

Assert-True (-not $Prepare.Contains('vmxoff')) `
    'PREPARE must not execute VMXOFF before the guest-mapped pivot is armed.'
Assert-Ordered $Prepare @(
    'CONTEXT_DMA_ISOLATION_MODE',
    'lock dec dword ptr [r10 + PHYSICAL_IOMMU_DETACH_REMAINING]',
    'call ResidentTeardownPhysicalIommu',
    'ResidentTeardownPhysicalIommu:',
    'PHYSICAL_IOMMU_DMA_QUIESCED], 1',
    'ResidentDetachQuiescePciLoop:',
    'mov dword ptr [rdx + VTD_GCMD_OFFSET], 0',
    'ResidentDetachPollDisabled:',
    'mov qword ptr [rdx + VTD_RTADDR_OFFSET], rax',
    'ResidentDetachRestorePciLoop:',
    'PHYSICAL_IOMMU_DMA_QUIESCED], 0',
    'PHYSICAL_IOMMU_ENABLED], 0') 'Physical VT-d detach rollback'
Assert-True ($Prepare.Contains('jmp ResidentFatal')) `
    'Physical VT-d detach timeouts must fail-stop.'
Assert-Ordered $Probe @(
    'HV_EFI_CALL_QUERY_RUNTIME_SESSION',
    'HV_EFI_CALL_PREPARE_NATIVE_DETACH',
    'HV_EFI_CALL_COMMIT_NATIVE_DETACH',
    'PostEbsDetachPivot:',
    'vmclear qword ptr',
    'vmxoff',
    'mov cr4',
    'mov cr0',
    'mov dword ptr [r12], 1',
    'PostEbsDetachContinuation:',
    'UEFI_HYPERVISOR_NATIVE_DETACH:PASS') 'VM-only detach proof'

$Vmoff = $Probe.IndexOf('vmxoff', [StringComparison]::Ordinal)
$Ack = $Probe.IndexOf('mov dword ptr [r12], 1', $Vmoff,
    [StringComparison]::Ordinal)
Assert-True ($Ack -gt $Vmoff) `
    'A detach acknowledgement may be written only after VMXOFF succeeds.'

Write-Output 'PASS: EFI runtime detach assembly contracts verified.'
