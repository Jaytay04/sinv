param(
    [Parameter(Mandatory=$true)][string]$DriverCMakePath,
    [Parameter(Mandatory=$true)][string]$DriverMainPath,
    [Parameter(Mandatory=$true)][string]$IoctlPath,
    [Parameter(Mandatory=$true)][string]$ReadmePath)

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
function Verify([string]$DriverCMake, [string]$DriverMain,
    [string]$Ioctl, [string]$Readme) {
    Assert (-not $DriverCMake.Contains('core_mgmt.c')) 'Legacy core_mgmt.c remains in supported driver target'
    Assert (-not $DriverMain.Contains('HYPERVISOR_MAX_CORES')) 'Driver entry retains fixed-core limit'
    Assert (-not $DriverMain.Contains('g_driver_state')) 'Driver entry retains duplicate runtime state'
    Assert (-not $Ioctl.Contains('VmxGetDriverState')) 'IOCTL path retains legacy runtime state query'
    Assert (-not $Ioctl.Contains('KeQueryActiveProcessorCount(NULL)')) 'Status path retains group-zero core count'

    $Status = Section $Ioctl 'IoctlDispatchStatus(' 'IoctlDispatchProbe('
    foreach ($Token in @(
        'VmxPersistentDriverOwnerAcquireStatusSnapshot(',
        'RtlCopyMemory(&header, snapshot.Buffer, sizeof(header))',
        'header.ContextCount',
        'response.active_cores = header.ContextCount',
        'VmxEptHookOwnerValidate(&device_context->EptHooks)',
        'response.hook_count = device_context->EptHooks.ActiveCount',
        'VmxPersistentDriverOwnerReleaseStatusSnapshot(',
        'ERR_PERSISTENT_LIFECYCLE_FAILED')) {
        Assert ($Status.Contains($Token)) "Missing authoritative legacy-status token: $Token"
    }

    $Probe = Section $Ioctl 'IoctlDispatchProbe(' 'IoctlDispatchLaunchProbe('
    $Launch = Section $Ioctl 'IoctlDispatchLaunchProbe(' 'IoctlDispatchPersistentStatusQuery('
    foreach ($Part in @($Probe, $Launch)) {
        Assert ($Part.Contains('VmxPersistentDriverOwnerQueryOccupied(')) 'Transient path lacks authoritative occupancy query'
        Assert (-not $Part.Contains('VmxGetDriverState')) 'Transient path retained duplicate legacy guard'
    }
    Assert ($Readme.Contains('Legacy fixed-array implementation (not compiled)')) 'README does not mark legacy source excluded'
}

$DriverCMake = [IO.File]::ReadAllText((Resolve-Path $DriverCMakePath))
$DriverMain = [IO.File]::ReadAllText((Resolve-Path $DriverMainPath))
$Ioctl = [IO.File]::ReadAllText((Resolve-Path $IoctlPath))
$Readme = [IO.File]::ReadAllText((Resolve-Path $ReadmePath))
Verify $DriverCMake $DriverMain $Ioctl $Readme

$Mutations = @(
    @($DriverCMake.Replace('driver_main.c', "driver_main.c`n        core_mgmt.c"), $DriverMain, $Ioctl, $Readme),
    @($DriverCMake, $DriverMain + "`n#define HYPERVISOR_MAX_CORES 64", $Ioctl, $Readme),
    @($DriverCMake, $DriverMain, $Ioctl.Replace('response.active_cores = header.ContextCount', 'response.active_cores = KeQueryActiveProcessorCount(NULL)'), $Readme),
    @($DriverCMake, $DriverMain, $Ioctl.Replace('VmxPersistentDriverOwnerQueryOccupied(', 'VmxGetDriverState(); /*'), $Readme),
    @($DriverCMake, $DriverMain, $Ioctl.Replace('VmxPersistentDriverOwnerReleaseStatusSnapshot(', 'RemovedStatusSnapshotRelease('), $Readme)
)

for ($Index = 0; $Index -lt $Mutations.Count; ++$Index) {
    $Rejected = $false
    try {
        Verify $Mutations[$Index][0] $Mutations[$Index][1] `
            $Mutations[$Index][2] $Mutations[$Index][3]
    }
    catch { $Rejected = $true }
    Assert $Rejected "Accepted mutation $Index"
}

Write-Output 'Verified legacy fixed-64 core management is excluded and compatibility status derives from authenticated dynamic Runtime/EPT state.'
