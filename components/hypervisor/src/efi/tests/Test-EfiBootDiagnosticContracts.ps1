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
$physicalIommu = Get-SourceRegion $source 'static EFI_STATUS HvEfiPreparePhysicalIommu(' 'typedef struct HV_EFI_AP_PREPARATION_CONTEXT'
$iommuSubsteps = @(
    @('ARGUMENT', 'context == NULL'),
    @('STATE', 'state == NULL'),
    @('ALLOC_MAPPED', 'status = gBS->AllocatePool('),
    @('ALLOC_DENIED', 'status = gBS->AllocatePool('),
    @('MEMORY_DESCRIPTOR', 'for (index = 0;'),
    @('RMRR_OWNER', 'for (index = 0;'),
    @('IDENTITY_DOMAIN', 'iommuStatus = HvEfiIommuBuildLegacyIdentityDomainWithAdjustedWidth('),
    @('GPU_DOMAIN', 'status = HvEfiGpuBuildDmaDomain('),
    @('RUNTIME_INSTALL', 'iommuStatus = HvEfiIommuInstallRuntimeIsolationWithAdjustedWidthAndPciPolicy(')
)
$previousOperation = -1
foreach ($substep in $iommuSubsteps) {
    $marker = $physicalIommu.IndexOf(('HV_EFI_BOOT_DIAGNOSTIC_IOMMU_' + $substep[0]), [StringComparison]::Ordinal)
    $operation = if ($marker -ge 0) { $physicalIommu.IndexOf($substep[1], $marker, [StringComparison]::Ordinal) } else { -1 }
    Assert-True ($marker -gt $previousOperation -and $operation -gt $marker) `
        ('Physical IOMMU needs a BSP diagnostic before ' + $substep[0])
    $previousOperation = $operation
}
$selectWidth = $physicalIommu.IndexOf('HvEfiIommuSelectLegacyAdjustedWidth(', [StringComparison]::Ordinal)
$buildTables = $physicalIommu.IndexOf('HvEfiIommuBuildLegacyIdentityDomainWithAdjustedWidth(', [StringComparison]::Ordinal)
Assert-True ($selectWidth -gt 0 -and $selectWidth -lt $buildTables -and
    $physicalIommu -match 'capabilities, state->Dmar\.UnitCount, 39, &adjustedAddressBits' -and
    $physicalIommu -match 'eptDenyRangeCount, 1,\s*adjustedAddressBits, &state->Tables' -and
    $physicalIommu -match '&state->DmaQuiesced, adjustedAddressBits, &installDiagnostic' -and
    $physicalIommu -match 'Configurations\[index\]\.AddressBits = 39;') `
    'Physical VT-d must select one common table format before construction and retain its 39-bit DMA limit through installation.'
$installResult = $physicalIommu.IndexOf('installDiagnostic.PrimaryStatus, installDiagnostic.FinalStatus', [StringComparison]::Ordinal)
$failStop = $physicalIommu.IndexOf('HvEfiFailStop();', [StringComparison]::Ordinal)
$freePlan = $physicalIommu.IndexOf('gBS->FreePool(denied);', [StringComparison]::Ordinal)
Assert-True ($installResult -gt $previousOperation -and $failStop -gt $installResult -and $freePlan -gt $failStop) `
    'Runtime IOMMU cause must be captured before fail-stop or plan cleanup.'
Assert-True ($physicalIommu -match 'if \(iommuStatus == HV_EFI_IOMMU_RUNTIME_ROLLBACK_FAILED \|\|\s*state->Runtime.State == HV_EFI_IOMMU_RUNTIME_ENABLED') `
    'Unverified PCI restoration must fail-stop even when remapper activation has not begun.'
$capabilityResult = $physicalIommu.IndexOf('hv_efi_boot_diagnostic_iommu_capability_detail(', [StringComparison]::Ordinal)
Assert-True ($capabilityResult -gt $installResult -and $capabilityResult -lt $failStop -and
    $physicalIommu -match 'installDiagnostic\.Stage == HV_EFI_IOMMU_INSTALL_STAGE_PROGRAM_PLAN' -and
    $physicalIommu -match 'installDiagnostic\.PrimaryStatus ==\s*HV_EFI_IOMMU_RUNTIME_REMAPPING_UNSUPPORTED') `
    'Capability observations must be captured on the BSP after the matching failed plan and before cleanup.'
Assert-True ($physicalIommu -match 'return EFI_ERROR\(status\) \? status :' -and
    [regex]::Matches($bspPrepare, 'return EFI_ERROR\(status\) \? status : EFI_ABORTED;').Count -eq 2) `
    'GPU/IOMMU EFI errors must survive AP preparation and its pool cleanup.'
$pciPolicyCheck = $physicalIommu.IndexOf('HvEfiIommuValidatePciQuiescePolicy(&pciPolicy)', [StringComparison]::Ordinal)
$firstAllocation = $physicalIommu.IndexOf('status = gBS->AllocatePool(', [StringComparison]::Ordinal)
Assert-True ($pciPolicyCheck -gt 0 -and $pciPolicyCheck -lt $firstAllocation -and
    $physicalIommu -match 'pciPolicy.Kind = HV_EFI_IOMMU_PCI_POLICY_ALDER_LAKE_S_HOST_BRIDGE;' -and
    $physicalIommu -match 'HvEfiCpuid\(0, 0, cpuid\)' -and
    $physicalIommu -match 'CpuVendorEbx = cpuid\[1\]' -and
    $physicalIommu -match 'CpuVendorEdx = cpuid\[3\]' -and
    $physicalIommu -match 'CpuVendorEcx = cpuid\[2\]' -and
    $physicalIommu -match 'CpuSignature = cpuid\[0\]' -and
    $physicalIommu -match 'CpuFeatureEcx = cpuid\[2\]' -and
    $physicalIommu -match 'EnabledProcessorCount = context->EnabledProcessorCount' -and
    $physicalIommu -match 'adjustedAddressBits, &installDiagnostic, &pciPolicy\)') `
    'The physical-only quiesce policy must validate raw CPU identity before allocation and reach the installer explicitly.'
$pciAccess = Get-SourceRegion $source 'static int HvEfiPhysicalEcamAddressValid(' 'static int HvEfiInstallPhysicalAcpiShadow('
Assert-True ($pciAccess -match 'segment->Segment == 0' -and
    $pciAccess -match 'segment->StartBus == 0' -and $pciAccess -match '\(relative >> 12\) == 0' -and
    $pciAccess -match 'HvEfiIommuPciConfigAccessAllowed\(' -and
    $pciAccess -match 'HvEfiPhysicalEcamAddressValid\(state, address, 0\)' -and
    $pciAccess -match 'HvEfiPhysicalEcamAddressValid\(state, address, 1\)') `
    'Physical ECAM callbacks must use the tested access boundary and distinguish exact CPU-function metadata reads from writes.'
$gpuDomain = Get-SourceRegion $source 'static EFI_STATUS HvEfiGpuBuildDmaDomain(' 'static EFI_STATUS HvEfiPreparePhysicalIommu('
$gpuPool = Get-SourceRegion $gpuDomain '#define GPU_POOL' 'HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_PROFILE'
Assert-True ($gpuPool -match 'HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_ALLOCATE' -and
    $gpuPool -match 'status = gBS->AllocatePool' -and $gpuPool -match 'EFI_ERROR\(status\)') `
    'GPU allocation diagnostics must preserve the actual EFI status.'
$gpuSubsteps = @(
    @('PROFILE', 'gpu == NULL'),
    @('UNIT_CAPABILITY', 'unit->Segment != 0'),
    @('UNIT_SCOPE', 'for (scope = 0;'),
    @('PCI_CAPACITY', 'deviceCount == HV_EFI_PHYSICAL_IOMMU_MAX_PCI_STATES'),
    @('PCI_OWNER', 'for (unitIndex = 0;'),
    @('NOT_FOUND', 'gpuIndex == UINT32_MAX'),
    @('RMRR_OWNER', 'rootOwner < 0'),
    @('RMRR_SCOPE', 'HvEfiGpuScopeBdf('),
    @('RMRR_REQUESTER', 'd == deviceCount'),
    @('MAPPING_CAPACITY', 'mappingCapacity == 0'),
    @('IDENTITY_PLAN', 'plannerStatus = HvEfiIommuBuildIdentityPlan('),
    @('ROOT_PLAN', 'plannerStatus = HvEfiIommuBuildRootDevicePlan('),
    @('DOMAIN_ID', 'for (i = 0;'),
    @('TABLES', 'runtimeStatus = HvEfiIommuBuildRootDeviceDomain(')
)
$previousOperation = -1
foreach ($substep in $gpuSubsteps) {
    $marker = $gpuDomain.IndexOf(('HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_' + $substep[0]), [StringComparison]::Ordinal)
    $operation = if ($marker -ge 0) { $gpuDomain.IndexOf($substep[1], $marker, [StringComparison]::Ordinal) } else { -1 }
    Assert-True ($marker -gt $previousOperation -and $operation -gt $marker) `
        ('GPU DMA failure needs a separate diagnostic before ' + $substep[0])
    $previousOperation = $operation
}
Assert-True ([regex]::Matches($gpuDomain, '\(uint32_t\)plannerStatus, \(uint32_t\)plannerStatus').Count -eq 2 -and
    $gpuDomain -match '\(uint32_t\)runtimeStatus, \(uint32_t\)runtimeStatus') `
    'GPU planners and table construction must retain their exact result enums.'
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
