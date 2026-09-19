[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [ValidateSet('Ninja', 'Visual Studio 17 2022')]
    [string]$Generator = 'Ninja',
    [string]$SourceDir = '',
    [string]$BuildDir = '',
    [switch]$SkipTests,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Split-Path -Parent $PSScriptRoot
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $SourceDir (Join-Path 'build/combined' $Configuration.ToLowerInvariant())
}

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($null -eq $cmake) {
    throw 'cmake.exe was not found on PATH. Run from a CMake-enabled developer shell.'
}

if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
    throw "Source directory does not exist: $SourceDir"
}

function Invoke-CMake {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    Write-Host ('> cmake ' + ($Arguments -join ' '))
    & $cmake.Source @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "cmake failed with exit code $LASTEXITCODE"
    }
}

$configureArguments = @(
    '-S', $SourceDir,
    '-B', $BuildDir,
    '-G', $Generator,
    "-DCOMBINED_CONFIG=$Configuration",
    '-DCOMBINED_BUILD_UEFI_LOADER=ON',
    '-DCOMBINED_BUILD_HYPERVISOR_EFI=ON',
    "-DCOMBINED_BUILD_COMPONENT_TESTS=$([bool](-not $SkipTests))"
)

if ($Generator -eq 'Visual Studio 17 2022') {
    $configureArguments += @('-A', 'x64')
} else {
    $configureArguments += @("-DCMAKE_BUILD_TYPE=$Configuration")
}

Invoke-CMake -Arguments $configureArguments

if ($ConfigureOnly) {
    Write-Host "Configured combined build at $BuildDir"
    exit 0
}

# This target configures/builds both components, then requests the EFI target
# explicitly. The hypervisor driver and SCM loader are disabled by the root
# CMake contract; no live-load operation is performed here.
Invoke-CMake -Arguments @('--build', $BuildDir, '--target', 'combined_hypervisor_efi', '--config', $Configuration, '--parallel')

if (-not $SkipTests) {
    Invoke-CMake -Arguments @('--build', $BuildDir, '--target', 'combined_test', '--config', $Configuration, '--parallel')
}

Write-Host "Combined build completed in $BuildDir"
