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
