[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$EfiSourceRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$Root = [IO.Path]::GetFullPath($EfiSourceRoot)
$Asm = Get-Content -Raw -LiteralPath (Join-Path $Root 'efi_hypervisor.asm')
$Inc = Get-Content -Raw -LiteralPath (Join-Path $Root 'efi_hypervisor.inc')
$Header = Get-Content -Raw -LiteralPath (Join-Path $Root 'efi_hypervisor.h')
$Source = Get-Content -Raw -LiteralPath (Join-Path $Root 'efi_hypervisor.c')
$AsmNormalized = [regex]::Replace($Asm, '\\\s*\r?\n\s*', ' ')

$DispatchStart = $Asm.IndexOf('HvEfiResidentVmExit PROC',
    [StringComparison]::Ordinal)
$DispatchEnd = $Asm.IndexOf('ResidentRdmsr:', $DispatchStart,
    [StringComparison]::Ordinal)
Assert-True ($DispatchStart -ge 0 -and $DispatchEnd -gt $DispatchStart) `
    'VM-exit dispatch boundaries are missing.'
$Dispatch = $Asm.Substring($DispatchStart, $DispatchEnd - $DispatchStart)
Assert-True ([regex]::IsMatch($Dispatch,
    '(?m)^\s*cmp eax, EXIT_REASON_XSETBV\r?\n\s*je ResidentInstructionExit\s*$')) `
    'XSETBV must read the VM-exit instruction length before advancing guest RIP.'

$InstructionStart = $Asm.IndexOf('ResidentInstructionExit:',
    [StringComparison]::Ordinal)
$XsetbvStart = $Asm.IndexOf('ResidentXsetbv:', $InstructionStart,
    [StringComparison]::Ordinal)
$HealthStart = $Asm.IndexOf('ResidentHealthCpuid:', $XsetbvStart,
    [StringComparison]::Ordinal)
Assert-True ($InstructionStart -ge 0 -and $XsetbvStart -gt $InstructionStart -and
    $HealthStart -gt $XsetbvStart) 'Instruction handler boundaries are missing.'
$Instruction = $Asm.Substring($InstructionStart,
    $XsetbvStart - $InstructionStart)
$Xsetbv = $Asm.Substring($XsetbvStart, $HealthStart - $XsetbvStart)
$LengthRead = $Instruction.IndexOf('VMCS_VM_EXIT_INSTRUCTION_LEN',
    [StringComparison]::Ordinal)
$LengthSave = $Instruction.IndexOf('mov r9d, eax',
    [StringComparison]::Ordinal)
$XsetbvBranch = $Instruction.IndexOf('je ResidentXsetbv',
    [StringComparison]::Ordinal)
Assert-True ($LengthRead -ge 0 -and $LengthSave -gt $LengthRead -and
    $XsetbvBranch -gt $LengthSave) `
    'XSETBV branch must follow the VM-exit instruction-length read.'
Assert-True ($Xsetbv.Contains('jmp ResidentAdvanceRip')) `
    'XSETBV must use the common instruction-length-based RIP advance.'

$Unsupported = $Asm.IndexOf('ResidentUnsupportedMsr:',
    [StringComparison]::Ordinal)
$MsrAdvance = $Asm.IndexOf('ResidentMsrAdvance:',
    [StringComparison]::Ordinal)
Assert-True ($Unsupported -gt $MsrAdvance) `
    'Unsupported MSRs must not fall through to the RIP-advance path.'
Assert-True ([regex]::IsMatch($Asm,
    '(?m)^ResidentFatal:\r?\n\s*mov dword ptr \[r11 \+ CONTEXT_FAILURE\], 1')) `
    'Fatal VM exits must enter the terminal failure path.'

$PrepareStart = $Asm.IndexOf('ResidentPrepareNativeDetach:',
    [StringComparison]::Ordinal)
$PrepareEnd = $Asm.IndexOf('ResidentRuntimeScan:', $PrepareStart,
    [StringComparison]::Ordinal)
Assert-True ($PrepareStart -ge 0 -and $PrepareEnd -gt $PrepareStart) `
    'Native-detach preparation boundaries are missing.'
$Prepare = $Asm.Substring($PrepareStart, $PrepareEnd - $PrepareStart)
$PrepareInstructions = [regex]::Replace($Prepare, '\\\s*\r?\n\s*', ' ')
$PrepareInstructions = ($PrepareInstructions -split "`r?`n" |
    ForEach-Object { ($_ -replace ';.*$', '').Trim() }) -join "`n"
Assert-True (-not [regex]::IsMatch($PrepareInstructions,
    '(?im)^vmcall\b')) `
    'Native-detach preparation must not add guest-side VMCALL exits.'

$SsStart = $PrepareInstructions.IndexOf('mov ecx, VMCS_GUEST_SS_SELECTOR',
    [StringComparison]::Ordinal)
$TrStart = $PrepareInstructions.IndexOf('mov ecx, VMCS_GUEST_TR_SELECTOR',
    $SsStart, [StringComparison]::Ordinal)
Assert-True ($SsStart -ge 0 -and $TrStart -gt $SsStart) `
    'Guest SS validation boundaries are missing.'
$GuestSs = $PrepareInstructions.Substring($SsStart, $TrStart - $SsStart)
Assert-True ([regex]::IsMatch($GuestSs,
    '(?s)mov ecx, VMCS_GUEST_SS_SELECTOR\s+vmread rax, rcx\s+jbe ResidentFatal\s+test eax, 7\s+jnz ResidentFatal\s+RESIDENT_VMWRITE_REG VMCS_HOST_SS_SELECTOR, rax')) `
    'Guest SS must retain VMREAD/RPL-TI validation and write the host selector.'
Assert-True (-not [regex]::IsMatch($GuestSs, '(?i)test eax, 0fff8h')) `
    'A null guest SS selector must be allowed for 64-bit host state.'

$CsStart = $PrepareInstructions.IndexOf('mov ecx, VMCS_GUEST_CS_SELECTOR',
    [StringComparison]::Ordinal)
$SsStartAfterCs = $PrepareInstructions.IndexOf('mov ecx, VMCS_GUEST_SS_SELECTOR',
    $CsStart, [StringComparison]::Ordinal)
$GuestCs = $PrepareInstructions.Substring($CsStart, $SsStartAfterCs - $CsStart)
$GuestTr = $PrepareInstructions.Substring($TrStart)
Assert-True ([regex]::IsMatch($GuestCs,
    '(?s)test eax, 0FFF8h\s+jz ResidentFatal')) `
    'Guest CS must continue rejecting a null selector.'
Assert-True ([regex]::IsMatch($GuestTr,
    '(?s)test eax, 0FFF8h\s+jz ResidentFatal')) `
    'Guest TR must continue rejecting a null selector.'
Assert-True ($Header.Contains(
    '#define HV_EFI_VM_EXIT_HOST_ADDR_SPACE_SIZE (UINT32_C(1) << 9)')) `
    'The host address-space-size control must denote 64-bit host mode.'
Assert-True ([regex]::IsMatch($Source,
    '(?s)HvEfiAdjustControls\(HV_EFI_VM_EXIT_HOST_ADDR_SPACE_SIZE\s*\|')) `
    'Null host SS is allowed only because VM-exit host address-space-size is enabled.'

Assert-True ($Source.Contains(
    'HvEfiWriteField(HV_EFI_VMCS_CTRL_EXCEPTION_BITMAP, 0)')) `
    'The VMCS must not intercept #DB/#BP without supported handlers.'
Assert-True ($Source.Contains(
    'HvEfiWriteField(HV_EFI_VMCS_GUEST_DR7, context->Dr7)')) `
    'The VMCS must preserve guest DR7.'

Write-Output 'PASS: EFI VM-exit source contracts'
