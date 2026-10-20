[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EfiSourceRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Get-SourceRegion([string]$Text, [string]$Start, [string]$End) {
    $first = $Text.IndexOf($Start, [StringComparison]::Ordinal)
    $last = if ($first -ge 0) {
        $Text.IndexOf($End, $first + $Start.Length, [StringComparison]::Ordinal)
    } else { -1 }
    Assert-True ($first -ge 0 -and $last -gt $first) "Missing source boundary: $Start"
    return $Text.Substring($first, $last - $first)
}

$root = [IO.Path]::GetFullPath($EfiSourceRoot)
$source = Get-Content -Raw -LiteralPath (Join-Path $root 'efi_hypervisor.c')
$prepare = Get-SourceRegion $source 'static void EFIAPI HvEfiApPrepareProcessorState(' 'static EFI_STATUS HvEfiPrepareApProcessorStates('
$activate = Get-SourceRegion $source 'static void EFIAPI HvEfiApActivateProcessor(' 'static EFI_STATUS HvEfiActivateApProcessors('
foreach ($callback in @($prepare, $activate)) {
    Assert-True ($callback -notmatch 'HvEfiBootDiagnostic|hv_boot_diagnostic') `
        'AP callbacks must not access the BSP/loader-owned startup record.'
}
$bspPrepare = Get-SourceRegion $source 'static EFI_STATUS HvEfiPrepareApProcessorStates(' 'static void EFIAPI HvEfiApActivateProcessor('
$memoryPolicy = Get-SourceRegion $bspPrepare 'HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_ALLOC_MEMORY_TYPES' 'HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_EPT_HIERARCHY'
$substeps = @(
    @('AP_ALLOC_MEMORY_TYPES', 'status = gBS->AllocatePool('),
    @('AP_ALLOC_DENY_RANGES', 'status = gBS->AllocatePool('),
    @('AP_GET_MEMORY_MAP', 'status = HvEfiGetMemoryMap('),
    @('AP_DESCRIPTOR_VERSION', 'descriptorVersion != HV_EFI_MEMORY_DESCRIPTOR_VERSION'),
    @('AP_MEMORY_TYPE_PLAN', '!HvEfiBuildEptMemoryTypePlan('),
    @('AP_DENY_INVENTORY', '!HvEfiBuildEptDenyInventory('),
    @('AP_SCANNER_ALLOWLIST', '!HvEfiBuildRuntimeScannerAllowlist(')
)
$previousOperation = -1
foreach ($substep in $substeps) {
    $marker = $memoryPolicy.IndexOf(('HV_EFI_BOOT_DIAGNOSTIC_DETAIL_' + $substep[0]), [StringComparison]::Ordinal)
    $operation = if ($marker -ge 0) { $memoryPolicy.IndexOf($substep[1], $marker, [StringComparison]::Ordinal) } else { -1 }
    Assert-True ($marker -gt $previousOperation -and $operation -gt $marker) `
        ('Memory-policy failure needs its own BSP diagnostic before ' + $substep[0])
    $previousOperation = $operation
}
Assert-True ($memoryPolicy -match 'return status;' -and $memoryPolicy -notmatch 'return EFI_ABORTED;') `
    'Memory-policy cleanup must preserve the failing EFI status.'
foreach ($name in @('efi_hypervisor.asm', 'efi_hypervisor.inc', 'efi_hypervisor.h')) {
    $text = Get-Content -Raw -LiteralPath (Join-Path $root $name)
    Assert-True ($text -notmatch 'HvEfiBootDiagnostic|hv_boot_diagnostic') `
        'Startup record pointers must not enter the resident assembly or context ABI.'
}

$cleanup = Get-SourceRegion $source 'static EFI_STATUS HvEfiFailAndFree(' '#include "gpu_vm_selftest.h"'
$capture = $cleanup.IndexOf('HvEfiBootDiagnosticCapture(', [StringComparison]::Ordinal)
$release = $cleanup.IndexOf('HvGpuBootReleaseBeforeLaunch(', [StringComparison]::Ordinal)
$freeContext = $cleanup.IndexOf('HvEfiFreeRuntime(contextPa,', [StringComparison]::Ordinal)
$returned = $cleanup.IndexOf('HvEfiBootDiagnosticReturnWithSnapshot(', [StringComparison]::Ordinal)
Assert-True ($capture -ge 0 -and $capture -lt $release -and
    $freeContext -gt $release -and $returned -gt $freeContext) `
    'Failure results must be captured before cleanup and finalized only after cleanup finishes.'
Assert-True ([regex]::Matches($cleanup, 'HvEfiBootDiagnosticReturn').Count -eq 1) `
    'Cleanup must not mark the payload returned before its terminal return path.'

$mainStart = $source.IndexOf('EFI_STATUS EFIAPI HvEfiMainBody(', [StringComparison]::Ordinal)
Assert-True ($mainStart -ge 0) 'Missing main startup body.'
$main = $source.Substring($mainStart)
$launch = $main.IndexOf('launchResult = HvEfiVmxLaunchAp(context);', [StringComparison]::Ordinal)
$capture = $main.LastIndexOf('HvEfiBootDiagnosticCapture(', [StringComparison]::Ordinal)
Assert-True ($capture -ge 0 -and $capture -lt $launch) `
    'Successful launch needs a context snapshot before the context becomes EPT-denied.'
$success = [regex]::Match($main, '(?s)if \(launchResult == 0\) \{(?<body>.*?)\r?\n    \}')
Assert-True ($success.Success -and
    $success.Groups['body'].Value -match 'HvEfiBootDiagnosticReturnWithSnapshot' -and
    $success.Groups['body'].Value -notmatch 'context\s*->|HvEfiBootDiagnosticReturn\s*\(') `
    'The successful guest return must use the saved snapshot without reading the denied context.'

Write-Output 'PASS: BSP startup diagnostic lifetime contracts'
