param(
    [Parameter(Mandatory = $true)][string]$RootCMakePath,
    [Parameter(Mandatory = $true)][string]$TestCMakePath
)

$ErrorActionPreference = 'Stop'
$root = Get-Content -Raw -LiteralPath $RootCMakePath
$tests = Get-Content -Raw -LiteralPath $TestCMakePath

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Ordered([string]$Text, [string[]]$Tokens) {
    $at = 0
    foreach ($token in $Tokens) {
        $next = $Text.IndexOf($token, $at, [StringComparison]::Ordinal)
        if ($next -lt 0) { return $false }
        $at = $next + $token.Length
    }
    return $true
}

Assert-True (Ordered $root @(
    'option(HYPERSINV_BUILD_DRIVER',
    'option(HYPERSINV_BUILD_LOADER',
    'add_subdirectory(src/hypervisor)',
    'if(HYPERSINV_BUILD_DRIVER)',
    'add_subdirectory(src/driver)',
    'if(HYPERSINV_BUILD_LOADER)',
    'add_subdirectory(src/loader)',
    'add_subdirectory(src/tests)'
)) 'production targets must register before the test graph'

Assert-True (Ordered $tests @(
    'if(CMAKE_ASM_MASM_COMPILER)',
    'add_test(NAME test_vmx_persistent_abi_mirror',
    '-Ml64Path "${CMAKE_ASM_MASM_COMPILER}"',
    'endif()'
)) 'MASM ABI-mirror test must remain capability-gated'

$driverAt = $root.IndexOf('add_subdirectory(src/driver)',
    [StringComparison]::Ordinal)
$testsAt = $root.IndexOf('add_subdirectory(src/tests)',
    [StringComparison]::Ordinal)
Assert-True ($driverAt -ge 0 -and $testsAt -gt $driverAt) 'fresh-build ordering mutation escaped'

Write-Host 'PASS: fresh-build production/test graph ordering'
