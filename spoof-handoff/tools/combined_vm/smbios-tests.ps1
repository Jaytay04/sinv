[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$ReadbackExePath,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$ReferenceRepo,

    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Add-TestResult([string]$Name, [string]$Status, [string]$Detail) {
    $script:TestResults.Add([ordered]@{ name = $Name; status = $Status; detail = $Detail })
    if ($Status -ceq 'FAIL') { $script:TestFailures++ }
    if ($Status -ceq 'BLOCKED') { $script:TestBlocked++ }
}

function Invoke-ExpectedBuilderFailure([string]$Name, [string]$CapturePath, [string]$MapPath,
    [string]$ExpectedText = '') {
    $reference = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MapPath))
    $artifactRoot = Join-Path $reference 'spoof-handoff\artifacts'
    $output = Join-Path $artifactRoot ('builder-negative-' + $Name)
    $log = Join-Path $script:TemporaryDirectory ('builder-' + $Name + '.log')
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script:BuilderScript,
        '-ReferenceRepo', $reference, '-OutputDirectory', $output, '-CaptureJson', $CapturePath)
    $stdout = Join-Path $script:TemporaryDirectory ('builder-' + $Name + '.stdout')
    $stderr = Join-Path $script:TemporaryDirectory ('builder-' + $Name + '.stderr')
    $process = Start-Process -FilePath $script:PowerShellExe -ArgumentList $arguments `
        -WorkingDirectory $script:MainRepo -NoNewWindow -PassThru -Wait `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $exitCode = $process.ExitCode
    $text = @((Get-Content -LiteralPath $stdout -ErrorAction SilentlyContinue) +
        (Get-Content -LiteralPath $stderr -ErrorAction SilentlyContinue)) -join "`r`n"
    [IO.File]::WriteAllText($log, $text + "`r`n", [Text.UTF8Encoding]::new($false))
    if ($exitCode -eq 0) { throw "Builder unexpectedly accepted negative case '$Name'." }
    if (Test-Path -LiteralPath $output) { throw "Builder created output for rejected case '$Name'." }
    if ($ExpectedText -and $text -notmatch [regex]::Escape($ExpectedText)) {
        throw "Builder rejected '$Name' for an unexpected reason. See $log"
    }
    return "exit=$exitCode; $text"
}

function New-TestCapture([string]$Directory, [string]$Name,
    [switch]$MissingSystemVendor, [switch]$MismatchedNativeWmi,
    [switch]$NonVmIdentity) {
    $values = [ordered]@{
        system_serial = 'VM-UNIT-SERIAL-01'
        system_vendor = if ($NonVmIdentity) { 'Unit Test Vendor' } else { 'VMware, Inc.' }
        system_product = if ($NonVmIdentity) { 'Unit Test Model' } else { 'VMware20,1' }
        board_serial = 'VM-UNIT-BOARD-01'
        board_vendor = 'VMware, Inc.'
        board_product = 'VMware Virtual Platform'
        system_uuid = 'B7A8A6D5-AF3E-40E0-94F7-1A2D5A6C9E03'
    }
    $fields = [ordered]@{}
    foreach ($field in $values.Keys) {
        if ($MissingSystemVendor -and $field -ceq 'system_vendor') { continue }
        $observedWmi = [string]$values[$field]
        if ($MismatchedNativeWmi -and $field -ceq 'board_serial') { $observedWmi = 'DIFFERENT-BOARD-SERIAL' }
        $fields[$field] = [ordered]@{
            observedNative = [string]$values[$field]
            observedWmi = $observedWmi
            nativeMatchesWmi = ([string]$values[$field] -ceq $observedWmi)
        }
    }
    $systemVendorWmi = [string]$values.system_vendor
    $systemProductWmi = [string]$values.system_product
    $capture = [ordered]@{
        schema = 'smbios-windows-capture.v1'
        mode = 'capture'
        result = 'PASS'
        native = [ordered]@{ source = 'GetSystemFirmwareTable(RSMB)'; major = 3; minor = 6; tableBytes = 1024; structureCount = 20; error = $null }
        wmi = [ordered]@{
            source = 'ROOT\CIMV2'
            systemProductClass = 'Win32_ComputerSystemProduct'
            systemProductInstancesObserved = 1
            systemProductInstanceCountExact = $true
            systemProductError = $null
            baseboardClass = 'Win32_BaseBoard'
            baseboardInstancesObserved = 1
            baseboardInstanceCountExact = $true
            baseboardError = $null
        }
        machineIdentity = [ordered]@{
            source = 'SMBIOS Type 1 RSMB + Win32_ComputerSystemProduct'
            systemVendorNative = [string]$values.system_vendor
            systemVendorWmi = $systemVendorWmi
            systemProductNative = [string]$values.system_product
            systemProductWmi = $systemProductWmi
        }
        fields = $fields
        bootIdentity = [ordered]@{ uptimeMilliseconds = 1000; approximateBootUtc = '2026-10-18T00:00:00.000Z' }
        errors = @()
    }
    $path = Join-Path $Directory ($Name + '.json')
    [IO.File]::WriteAllText($path, ($capture | ConvertTo-Json -Depth 10) + "`n", [Text.UTF8Encoding]::new($false))
    return $path
}

function Set-MsvcEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'Visual Studio vswhere was not found.' }
    $installations = @(& $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    $vcvars = $null
    foreach ($installation in $installations) {
        $candidate = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $vcvars = $candidate; break }
    }
    if (-not $vcvars) { throw 'The x64 MSVC toolchain was not found.' }
    $environmentLines = & $env:ComSpec /d /s /c ('call "' + $vcvars + '" >nul && set')
    if ($LASTEXITCODE -ne 0) { throw 'MSVC environment initialization failed.' }
    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
        }
    }
}

$repo = Get-FullPath $ReferenceRepo
$reportsRoot = Join-Path $repo 'spoof-handoff\reports'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $reportsRoot ('combined-vm-smbios-tests\' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
$reportPrefix = (Get-FullPath $reportsRoot) + '\'
if (-not $output.StartsWith($reportPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Test reports must remain under spoof-handoff/reports.'
}
if (Test-Path -LiteralPath $output) { throw "Test output directory must be new: $output" }
[void](New-Item -ItemType Directory -Path $output -Force)
$script:TemporaryDirectory = Join-Path $output 'temporary'
[void](New-Item -ItemType Directory -Path $script:TemporaryDirectory -Force)
$script:TestResults = [Collections.Generic.List[object]]::new()
$script:TestFailures = 0
$script:TestBlocked = 0
$script:MainRepo = $repo
$script:PowerShellExe = Join-Path $PSHOME 'powershell.exe'
$script:BuilderScript = Join-Path $PSScriptRoot 'build-smbios-fixture.ps1'
$source = Join-Path (Get-FullPath (Join-Path $PSScriptRoot '..\smbios_boot')) ''
$source = Get-FullPath $source
$script:MainMap = Join-Path $repo 'spoof-handoff\reports\client-originals.tsv'
$script:HostCapture = Join-Path $output 'host-capture-readonly.json'
$script:SummaryPath = Join-Path $output 'smbios-tests.json'
$startUtc = [DateTime]::UtcNow.ToString('o')

try {
    try {
        $defaultStdout = Join-Path $output 'default-arguments.stdout.txt'
        $defaultStderr = Join-Path $output 'default-arguments.stderr.txt'
        $defaultProcess = Start-Process -FilePath ([IO.Path]::GetFullPath($ReadbackExePath)) `
            -WorkingDirectory $repo -NoNewWindow -PassThru -Wait `
            -RedirectStandardOutput $defaultStdout -RedirectStandardError $defaultStderr
        $defaultExit = $defaultProcess.ExitCode
        $defaultText = @((Get-Content -LiteralPath $defaultStdout -ErrorAction SilentlyContinue) +
            (Get-Content -LiteralPath $defaultStderr -ErrorAction SilentlyContinue)) -join "`r`n"
        [IO.File]::WriteAllText((Join-Path $output 'default-arguments.txt'), $defaultText + "`r`n", [Text.UTF8Encoding]::new($false))
        if ($defaultExit -eq 0 -or (Test-Path -LiteralPath $script:HostCapture)) {
            throw 'Readback CLI unexpectedly succeeded without a mode/output or created a report.'
        }
        Add-TestResult 'capture CLI requires explicit mode and output' 'PASS' "exit=$defaultExit; no report created"
    } catch { Add-TestResult 'capture CLI requires explicit mode and output' 'FAIL' $_.Exception.Message }

    try {
        $captureStdout = Join-Path $output 'capture-cli.stdout.txt'
        $captureStderr = Join-Path $output 'capture-cli.stderr.txt'
        $captureArguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',
            (Join-Path $PSScriptRoot 'capture-smbios.ps1'),'-ReadbackExePath',
            ([IO.Path]::GetFullPath($ReadbackExePath)),'-OutputPath',$script:HostCapture)
        $captureProcess = Start-Process -FilePath $script:PowerShellExe `
            -ArgumentList $captureArguments -WorkingDirectory $repo -NoNewWindow -PassThru -Wait `
            -RedirectStandardOutput $captureStdout -RedirectStandardError $captureStderr
        $captureExit = $captureProcess.ExitCode
        if (-not (Test-Path -LiteralPath $script:HostCapture -PathType Leaf)) {
            throw "Capture CLI returned exit $captureExit without writing the capture report."
        }
        $hostCaptureDocument = Get-Content -LiteralPath $script:HostCapture -Raw | ConvertFrom-Json
        $fieldNames = @('system_serial','system_vendor','system_product','board_serial','board_vendor','board_product','system_uuid')
        if ($hostCaptureDocument.schema -cne 'smbios-windows-capture.v1' -or
            $hostCaptureDocument.machineIdentity.systemVendorNative -cne $hostCaptureDocument.fields.system_vendor.observedNative -or
            $hostCaptureDocument.machineIdentity.systemVendorWmi -cne $hostCaptureDocument.fields.system_vendor.observedWmi -or
            $hostCaptureDocument.machineIdentity.systemProductNative -cne $hostCaptureDocument.fields.system_product.observedNative -or
            $hostCaptureDocument.machineIdentity.systemProductWmi -cne $hostCaptureDocument.fields.system_product.observedWmi) {
            throw 'Actual read-only host capture report failed schema or identity-metadata checks.'
        }
        foreach ($field in $fieldNames) {
            if ($hostCaptureDocument.fields.$field.nativeMatchesWmi -ne $true -or
                $hostCaptureDocument.fields.$field.observedNative -cne $hostCaptureDocument.fields.$field.observedWmi) {
                throw "Actual read-only host capture mismatched native/WMI field $field."
            }
        }
        if ($captureExit -eq 0 -and $hostCaptureDocument.result -ceq 'PASS') {
            Add-TestResult 'normal-process --capture reads RSMB and fresh WMI' 'PASS' 'Host capture is read-only and is not used as VM fixture input.'
        } elseif (($hostCaptureDocument.wmi.systemProductError -match 'HRESULT=0x80041003' -or
                   $hostCaptureDocument.wmi.baseboardError -match 'HRESULT=0x80041003') -and
                  $captureExit -ne 0) {
            Add-TestResult 'normal-process --capture reads RSMB and fresh WMI' 'BLOCKED' `
                'RSMB was readable; current process lacks WMI namespace read permission (HRESULT=0x80041003). The failed report is retained and was not used as VM fixture input.'
        } else {
            throw "Capture CLI exit/result mismatch: exit=$captureExit result=$($hostCaptureDocument.result)."
        }
    } catch { Add-TestResult 'normal-process --capture reads RSMB and fresh WMI' 'FAIL' $_.Exception.Message }

    $fakeReference = Join-Path $script:TemporaryDirectory 'fake-reference'
    $fakeReports = Join-Path $fakeReference 'spoof-handoff\reports'
    [void](New-Item -ItemType Directory -Path $fakeReports -Force)
    Copy-Item -LiteralPath $script:MainMap -Destination (Join-Path $fakeReports 'client-originals.tsv')
    $baseCapture = New-TestCapture $script:TemporaryDirectory 'base-capture'

    try {
        $missing = New-TestCapture $script:TemporaryDirectory 'missing-system-vendor' -MissingSystemVendor
        $detail = Invoke-ExpectedBuilderFailure 'missing-field' $missing (Join-Path $fakeReports 'client-originals.tsv') 'exactly one system_vendor'
        Add-TestResult 'fixture builder rejects missing field' 'PASS' $detail
    } catch { Add-TestResult 'fixture builder rejects missing field' 'FAIL' $_.Exception.Message }

    try {
        $duplicate = Get-Content -LiteralPath $baseCapture -Raw
        $line = [regex]::Match($duplicate, '(?m)^\s*"system_serial"\s*:\s*\{[^\r\n]*')
        if (-not $line.Success) { throw 'Could not create duplicate-field test input.' }
        $duplicate = $duplicate.Insert($line.Index + $line.Length, "`r`n" + $line.Value)
        $duplicatePath = Join-Path $script:TemporaryDirectory 'duplicate-system-serial.json'
        [IO.File]::WriteAllText($duplicatePath, $duplicate, [Text.UTF8Encoding]::new($false))
        $detail = Invoke-ExpectedBuilderFailure 'duplicate-field' $duplicatePath (Join-Path $fakeReports 'client-originals.tsv') 'exactly one system_serial'
        Add-TestResult 'fixture builder rejects duplicate field' 'PASS' $detail
    } catch { Add-TestResult 'fixture builder rejects duplicate field' 'FAIL' $_.Exception.Message }

    try {
        $mismatched = New-TestCapture $script:TemporaryDirectory 'mismatched-native-wmi' -MismatchedNativeWmi
        $detail = Invoke-ExpectedBuilderFailure 'native-wmi-mismatch' $mismatched (Join-Path $fakeReports 'client-originals.tsv') 'does not prove native/WMI agreement for board_serial'
        Add-TestResult 'fixture builder rejects native/WMI mismatch' 'PASS' $detail
    } catch { Add-TestResult 'fixture builder rejects native/WMI mismatch' 'FAIL' $_.Exception.Message }

    try {
        $driftMap = Join-Path $fakeReports 'client-originals-drift.tsv'
        $mapText = [IO.File]::ReadAllText((Join-Path $fakeReports 'client-originals.tsv'))
        $mapText = $mapText.Replace("FakeSMBIOSSerial`tSMBIOS Type 1`t3080-26`t4091-37", "FakeSMBIOSSerial`tSMBIOS Type 1`t3080-26`t9999-99")
        [IO.File]::WriteAllText($driftMap, $mapText, [Text.UTF8Encoding]::new($false))
        $tempRoot = Join-Path $script:TemporaryDirectory 'replacement-drift-reference'
        $tempReports = Join-Path $tempRoot 'spoof-handoff\reports'
        [void](New-Item -ItemType Directory -Path $tempReports -Force)
        Move-Item -LiteralPath $driftMap -Destination (Join-Path $tempReports 'client-originals.tsv')
        $detail = Invoke-ExpectedBuilderFailure 'replacement-drift' $baseCapture (Join-Path $tempReports 'client-originals.tsv') 'locked replacement for system_serial'
        Add-TestResult 'fixture builder rejects replacement drift' 'PASS' $detail
    } catch { Add-TestResult 'fixture builder rejects replacement drift' 'FAIL' $_.Exception.Message }

    try {
        $nonVm = New-TestCapture $script:TemporaryDirectory 'non-vm-identity' -NonVmIdentity
        $detail = Invoke-ExpectedBuilderFailure 'non-vm-identity' $nonVm (Join-Path $fakeReports 'client-originals.tsv') 'expected VMware virtual machine model'
        Add-TestResult 'fixture builder binds capture to VMware model' 'PASS' $detail
    } catch { Add-TestResult 'fixture builder binds capture to VMware model' 'FAIL' $_.Exception.Message }

    try {
        Set-MsvcEnvironment
        $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
        $invalidHeader = Join-Path $script:TemporaryDirectory 'missing-vm-flag.h'
        [IO.File]::WriteAllText($invalidHeader, "#define INVALID_PROFILE_HEADER`r`n", [Text.UTF8Encoding]::new($false))
        $cmakeCases = @(
            @{ Name = 'missing-header'; Args = @('-DHYPERSINV_VM_ONLY_SMBIOS_PROFILE=ON'); Expected = 'requires an existing HYPERSINV_VM_SMBIOS_VALUES_HEADER' },
            @{ Name = 'missing-vm-flag'; Args = @('-DHYPERSINV_VM_ONLY_SMBIOS_PROFILE=ON', ('-DHYPERSINV_VM_SMBIOS_VALUES_HEADER=' + $invalidHeader)); Expected = 'must refuse inclusion outside the VM-only profile' }
        )
        foreach ($case in $cmakeCases) {
            $buildDirectory = Join-Path $script:TemporaryDirectory ('cmake-' + $case.Name)
            $arguments = @('-S', $source, '-B', $buildDirectory, '-G', 'Ninja',
                ('-DREFERENCE_REPO=' + $repo), '-DBUILD_TESTING=OFF') + $case.Args
            $log = Join-Path $output ('cmake-' + $case.Name + '.txt')
            $previousErrorActionPreference = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                $text = @(& $cmake @arguments 2>&1 | ForEach-Object { [string]$_ }) -join "`r`n"
                $exitCode = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousErrorActionPreference
            }
            [IO.File]::WriteAllText($log, $text + "`r`n", [Text.UTF8Encoding]::new($false))
            $normalizedText = $text -replace '\s+', ' '
            if ($exitCode -eq 0 -or $normalizedText -notmatch [regex]::Escape($case.Expected)) {
                throw "CMake did not reject the '$($case.Name)' profile input as expected. See $log"
            }
            Add-TestResult ('CMake rejects ' + $case.Name) 'PASS' "exit=$exitCode; $($case.Expected)"
        }
    } catch { Add-TestResult 'CMake rejects missing VM profile header/guard' 'FAIL' $_.Exception.Message }
}
finally {
    $report = [ordered]@{
        schema = 'combined-vm-smbios-tests.v1'
        result = if ($script:TestFailures -ne 0) { 'FAIL' } elseif ($script:TestBlocked -ne 0) { 'BLOCKED_ENVIRONMENT' } else { 'PASS' }
        startedUtc = $startUtc
        completedUtc = [DateTime]::UtcNow.ToString('o')
        readbackExe = [ordered]@{ path = [IO.Path]::GetFullPath($ReadbackExePath); sha256 = (Get-FileHash -LiteralPath $ReadbackExePath -Algorithm SHA256).Hash }
        hostCapture = if (Test-Path -LiteralPath $script:HostCapture) { [ordered]@{ path = $script:HostCapture; sha256 = (Get-FileHash -LiteralPath $script:HostCapture -Algorithm SHA256).Hash; usedAsVmFixtureInput = $false } } else { $null }
        tests = @($script:TestResults.ToArray())
        blockedTests = $script:TestBlocked
        failedTests = $script:TestFailures
    }
    [IO.File]::WriteAllText($script:SummaryPath, ($report | ConvertTo-Json -Depth 10) + "`n", [Text.UTF8Encoding]::new($false))
    $tempFull = Get-FullPath $script:TemporaryDirectory
    $outputFull = (Get-FullPath $output) + '\'
    if ($tempFull.StartsWith($outputFull, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $tempFull)) {
        Remove-Item -LiteralPath $tempFull -Recurse -Force
    }
}

if ($script:TestFailures -ne 0) { throw "SMBIOS VM fixture tests failed; evidence: $script:SummaryPath" }
if ($script:TestBlocked -ne 0) { Write-Output "BLOCKED: pure tests passed but current process lacks WMI read permission; evidence: $script:SummaryPath"; exit 2 }
Write-Output "PASS: all SMBIOS VM fixture tests; evidence: $script:SummaryPath"
