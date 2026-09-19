[CmdletBinding()]
param(
    [string]$SourceDirectory = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

if ([String]::IsNullOrWhiteSpace($SourceDirectory)) {
    $SourceDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
}

function Assert-Contains([string]$Path, [string]$Needle) {
    $text = Get-Content -LiteralPath $Path -Raw
    if ($text.IndexOf($Needle, [StringComparison]::Ordinal) -lt 0) {
        throw "Missing '$Needle' in $Path"
    }
}

function Assert-NotContains([string]$Path, [string]$Needle) {
    $text = Get-Content -LiteralPath $Path -Raw
    if ($text.IndexOf($Needle, [StringComparison]::Ordinal) -ge 0) {
        throw "Unexpected '$Needle' in $Path"
    }
}

$source = Join-Path $SourceDirectory 'hvr1_service.cpp'
$cmake = Join-Path $SourceDirectory 'CMakeLists.txt'
$readme = Join-Path $SourceDirectory 'README.md'
$auth = Join-Path $SourceDirectory 'hvr1_auth.c'
foreach ($path in @($source, $cmake, $readme, $auth)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing service contract file: $path"
    }
}

Assert-Contains $source 'CreateFileW'
Assert-Contains $source 'GENERIC_READ | GENERIC_WRITE'
Assert-Contains $source 'DeviceIoControl'
Assert-Contains $source 'HVR1_IOCTL_SUBMIT'
Assert-Contains $source 'HVR1_IOCTL_QUERY_STATUS'
Assert-Contains $source 'HVR1_IOCTL_GET_MAILBOX'
Assert-Contains $source 'Hvr1ComputeMailboxAuthTag'
Assert-Contains $auth 'BCryptHash'
Assert-Contains $source 'SecureZeroMemory(&descriptor'
Assert-Contains $source 'HVR1_COMPANION_STATE_OFFLINE'
Assert-Contains $source 'ExpectedCpuCount == status.Value.AcknowledgedCpuCount'
Assert-Contains $source 'selftest-shutdown'
Assert-Contains $source 'arm-recovery-test'
Assert-Contains $source 'HVR1_CRASH_RECOVERY_ARMED=PASS'
Assert-Contains $source 'HVR1_CRASH_RECOVERY_PRIOR_BOOT=PASS'
Assert-Contains $source 'HVR1_POWER_REQUEST_HELD=PASS'
Assert-Contains $source 'HVR1_POWER_REQUEST_RELEASED_AFTER_OFFLINE=PASS'
Assert-Contains $source 'HVR1_AUTH_INVALID_TAG_REJECTED=PASS'
Assert-Contains $source 'HVR1_AUTH_VALID_SHUTDOWN=PASS'
Assert-Contains $source 'HVR1_AUTH_REPLAY_REJECTED=PASS'
Assert-Contains $source 'HVR1_AUTH_SELFTEST=PASS'
Assert-Contains $source 'SERVICE_ACCEPT_POWEREVENT'
Assert-Contains $source 'SERVICE_ACCEPT_PRESHUTDOWN'
Assert-Contains $source 'SERVICE_CONTROL_POWEREVENT'
Assert-Contains $source 'SERVICE_ACCEPT_SHUTDOWN'
Assert-Contains $source 'PBT_APMSUSPEND'
Assert-Contains $source 'PBT_APMSTANDBY'
Assert-Contains $source 'SERVICE_CONTROL_PRESHUTDOWN'
Assert-Contains $source 'SERVICE_CONTROL_SHUTDOWN'
Assert-Contains $source 'PowerCreateRequest'
Assert-Contains $source 'PowerSetRequest(powerRequest, PowerRequestSystemRequired)'
Assert-Contains $source 'PowerRequestExecutionRequired'
Assert-Contains $source 'PowerClearRequest'
Assert-Contains $source 'RegSetValueExW'
Assert-Contains $source 'RegQueryValueExW'
Assert-Contains $source 'ERROR_INVALID_STATE'
Assert-Contains $source 'RecoveryState::Quarantined'
Assert-Contains $source 'refusing unsafe reuse'
Assert-Contains $source 'REG_OPTION_VOLATILE'
Assert-Contains $source 'recovered a stale lifecycle from a prior boot'
Assert-Contains $source 'Hvr1ServiceMain'
Assert-Contains $source 'StartServiceCtrlDispatcherW'
Assert-NotContains $source 'CreateServiceW'
Assert-NotContains $source 'StartServiceW'
Assert-NotContains $source 'OpenSCManagerW'

Assert-Contains $cmake 'hvr1_runtime_service'
Assert-Contains $cmake 'hvr1_service.cpp'
Assert-Contains $cmake 'Advapi32.lib'
Assert-Contains $cmake 'Bcrypt.lib'
Assert-Contains $cmake 'PowrProf.lib'
Assert-Contains $readme 'no vetoable query-suspend'
Assert-Contains $readme 'system-required and execution-required power requests'
Assert-Contains $readme 'A restart during the same boot'

$compilerPath = $null
$pathCompiler = Get-Command cl.exe -ErrorAction SilentlyContinue
if ($null -ne $pathCompiler -and $pathCompiler.Name -ieq 'cl.exe') {
    $compilerPath = $pathCompiler.Source
}
if ($null -eq $compilerPath) {
    $vsRoots = @(
        (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    ) | Where-Object { -not [String]::IsNullOrWhiteSpace($_) -and (Test-Path $_) }
    foreach ($vsRoot in $vsRoots) {
        $candidate = Get-ChildItem -LiteralPath $vsRoot -Filter cl.exe -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\Hostx64\\x64\\cl\.exe$' } |
            Select-Object -First 1
        if ($null -ne $candidate) {
            $compilerPath = $candidate.FullName
            break
        }
    }
}
if ($null -ne $compilerPath) {
    $tempDirectory = Join-Path ([IO.Path]::GetTempPath()) ('hvr1-service-contract-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tempDirectory | Out-Null
    try {
        $objectPath = Join-Path $tempDirectory 'hvr1_service.obj'
        $um = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include\10.0.28000.0\um'
        $shared = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include\10.0.28000.0\shared'
        $ucrt = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include\10.0.28000.0\ucrt'
        $msvcInclude = Split-Path (Split-Path (Split-Path (Split-Path $compilerPath -Parent) -Parent) -Parent) -Parent
        $msvcInclude = Join-Path $msvcInclude 'include'
        & $compilerPath /nologo /std:c++17 /EHsc /W4 /WX /permissive- /DUNICODE /D_UNICODE `
            /DWIN32_LEAN_AND_MEAN /DNTDDI_VERSION=0x0A00000C "/I$SourceDirectory" "/I$um" "/I$shared" "/I$ucrt" `
            "/I$msvcInclude" /c $source "/Fo$objectPath"
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $objectPath)) {
            throw 'Win32 service source compilation failed'
        }
    } finally {
        Remove-Item -LiteralPath $tempDirectory -Recurse -Force
    }
} else {
    Write-Output 'MSVC cl.exe unavailable; source-only service checks still run'
}

Write-Output 'HVR1 service source contracts: PASS'
