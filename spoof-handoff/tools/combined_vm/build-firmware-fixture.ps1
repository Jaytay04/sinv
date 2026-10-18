[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$ReferenceRepo,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string]$ManifestPath,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$PathBLoader,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$PathBCollector,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$CollectorManifestPath,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$PathBStageEvidence
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Assert-UnderRoot([string]$Root, [string]$Path, [string]$Label) {
    $rootPath = (Get-FullPath $Root) + '\'
    $pathFull = Get-FullPath $Path
    if (-not $pathFull.StartsWith($rootPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must remain under $Root"
    }
}

function Set-MsvcEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Visual Studio vswhere.exe was not found.'
    }
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
            [Environment]::SetEnvironmentVariable(
                $line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
        }
    }
}

function Invoke-Logged([string]$Program, [string[]]$Arguments, [string]$LogPath) {
    $previousPreference = $ErrorActionPreference
    try {
        # Native tools may emit routine diagnostics on stderr. Capture those as
        # log text while preserving the real process exit code as the verdict.
        $ErrorActionPreference = 'Continue'
        $lines = @(& $Program @Arguments 2>&1 | ForEach-Object { [string]$_ })
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    foreach ($line in $lines) {
        Add-Content -LiteralPath $LogPath -Value $line
        Write-Output $line
    }
    if ($exitCode -ne 0) {
        throw "$Program failed with exit code $exitCode. See $LogPath"
    }
}

function Get-FilePin([string]$Path, [string]$Label) {
    $full = [IO.Path]::GetFullPath($Path)
    $file = Get-Item -LiteralPath $full -ErrorAction Stop
    return [ordered]@{
        name = $Label
        path = $full
        bytes = [long]$file.Length
        sha256 = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToUpperInvariant()
    }
}

function Assert-PathBLoaderInput([string]$Path, [string]$StageEvidencePath) {
    $expectedLoaderBytes = 7168L
    $expectedLoaderSha256 = '24B321ECAABB33EC3DAD346F2E8941F0E035D87056712128410F75598FFFFE6A'
    $expectedStageEvidenceSha256 = '435951FE616A645360BD979FF12989B66FBC72FB55ADB6EF454A630A7E2BB263'
    $expectedRunId = 'uefi-windows-probe-20261018-041850-9958a7a427'

    $loaderPath = [IO.Path]::GetFullPath($Path)
    if ([IO.Path]::GetFileName($loaderPath) -cne 'inventory_loader.efi' -or
        [IO.Path]::GetFileName([IO.Path]::GetDirectoryName($loaderPath)) -cne 'efi_inventory_loader') {
        throw 'PathBLoader must be the dedicated .codex-build\efi_inventory_loader\inventory_loader.efi artifact.'
    }
    $loaderFile = Get-Item -LiteralPath $loaderPath -ErrorAction Stop
    if ([long]$loaderFile.Length -ne $expectedLoaderBytes) {
        throw "PathBLoader length mismatch: expected $expectedLoaderBytes bytes, got $($loaderFile.Length)."
    }
    $loaderSha256 = (Get-FileHash -LiteralPath $loaderPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($loaderSha256 -cne $expectedLoaderSha256) {
        throw "PathBLoader SHA-256 mismatch: expected $expectedLoaderSha256, got $loaderSha256."
    }

    $stageEvidence = [IO.Path]::GetFullPath($StageEvidencePath)
    $runDirectory = Split-Path -Parent (Split-Path -Parent $stageEvidence)
    if ([IO.Path]::GetFileName($runDirectory) -cne $expectedRunId) {
        throw "PathBStageEvidence must come from authenticated run $expectedRunId."
    }
    $stageEvidenceSha256 = (Get-FileHash -LiteralPath $stageEvidence -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($stageEvidenceSha256 -cne $expectedStageEvidenceSha256) {
        throw "PathBStageEvidence SHA-256 mismatch: expected $expectedStageEvidenceSha256, got $stageEvidenceSha256."
    }
    $evidence = Get-Content -LiteralPath $stageEvidence -Raw | ConvertFrom-Json -ErrorAction Stop
    if ($evidence.schema -cne 'combined-vm-pathb-diagnostic-stage-arm.v1' -or
        $evidence.status -cne 'PASS' -or
        $evidence.lifecycleId -cne 'smbios-failure-phase-r5-20261018' -or
        [int]$evidence.pathB.stageExitCode -ne 0 -or
        [int]$evidence.pathB.armExitCode -ne 0 -or
        [string]$evidence.pathB.receipt.loaderSha256 -cne $expectedLoaderSha256) {
        throw 'PathBStageEvidence does not prove the expected successful dedicated-loader stage and arm.'
    }
    $stagedLoader = @($evidence.esp.afterStage.stageFiles | Where-Object { $_.name -ceq 'loader.efi' })
    if ($stagedLoader.Count -ne 1 -or
        [long]$stagedLoader[0].bytes -ne $expectedLoaderBytes -or
        [string]$stagedLoader[0].sha256 -cne $expectedLoaderSha256) {
        throw 'PathBStageEvidence ESP file list does not match the pinned dedicated loader.'
    }

    return [pscustomobject]@{
        expectedLoaderBytes = $expectedLoaderBytes
        expectedLoaderSha256 = $expectedLoaderSha256
        sourceReceiptPath = $stageEvidence
        sourceReceiptSha256 = $stageEvidenceSha256
        sourceRunId = $expectedRunId
        sourceLifecycleId = [string]$evidence.lifecycleId
        sourceReceiptLoaderSha256 = [string]$evidence.pathB.receipt.loaderSha256
        sourceStageDirectory = [string]$evidence.pathB.receipt.stageDirectory
        sourceEspStageLoaderSha256 = [string]$stagedLoader[0].sha256
    }
}

function Get-SourcePins([string]$SourceRoot, [string]$VendorRoot,
                        [string]$GuardSource, [string]$BuilderPath) {
    $pins = [Collections.Generic.List[object]]::new()
    $sourcePrefix = [IO.Path]::GetFullPath($SourceRoot).TrimEnd('\') + '\'
    foreach ($file in @(Get-ChildItem -LiteralPath $SourceRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.c', '.h', '.cpp', '.hpp') -or $_.Name -ceq 'CMakeLists.txt' } |
        Sort-Object FullName)) {
        $relative = $file.FullName.Substring($sourcePrefix.Length).Replace('\', '/')
        $pins.Add([ordered]@{
            path = 'source/' + $relative
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
        })
    }
    foreach ($name in @('Uefi.h', 'efi_crt.c')) {
        $path = Join-Path $VendorRoot $name
        $pins.Add([ordered]@{
            path = 'uefi-support/' + $name
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
        })
    }
    foreach ($input in @(
        [pscustomobject]@{ name = 'production-guard'; path = $GuardSource },
        [pscustomobject]@{ name = 'builder'; path = $BuilderPath }
    )) {
        $pins.Add([ordered]@{
            path = $input.name + '/' + [IO.Path]::GetFileName($input.path)
            sha256 = (Get-FileHash -LiteralPath $input.path -Algorithm SHA256).Hash.ToUpperInvariant()
        })
    }
    $orderedPins = @($pins.ToArray() | Sort-Object -Property path -CaseSensitive)
    $canonical = (@($orderedPins | ForEach-Object { [string]$_.path + "`t" + [string]$_.sha256 }) -join "`n") + "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $setHash = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical)))).Replace('-', '')
    } finally { $sha.Dispose() }
    return [pscustomobject]@{ files = $orderedPins; sha256 = $setHash }
}

$repo = Get-FullPath $ReferenceRepo
$output = [IO.Path]::GetFullPath($OutputDirectory)
$manifest = [IO.Path]::GetFullPath($ManifestPath)
$artifactRoot = Join-Path $repo 'spoof-handoff\artifacts'
$reportRoot = Join-Path $repo 'spoof-handoff\reports'
Assert-UnderRoot $artifactRoot $output 'OutputDirectory'
Assert-UnderRoot $reportRoot $manifest 'ManifestPath'
if (Test-Path -LiteralPath $output) { throw "OutputDirectory must be new: $output" }
if (Test-Path -LiteralPath $manifest) { throw "ManifestPath must be new: $manifest" }

$sourceRoot = Join-Path $PSScriptRoot 'smbios_fixture'
$vendorRoot = Join-Path $repo 'components\uefi-loader\src\vendor'
$guardSource = Join-Path $repo 'spoof-handoff\tools\smbios_boot\efi_smbios.c'
$loaderEvidence = Assert-PathBLoaderInput $PathBLoader $PathBStageEvidence
$collectorManifest = Get-Content -LiteralPath $CollectorManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ($collectorManifest.schema -cne 'combined-vm-smbios-fixture.v1' -or
    $collectorManifest.result -cne 'PASS' -or
    $collectorManifest.mode -cne 'vm_only_smbios_fixture' -or
    [IO.Path]::GetFullPath([string]$collectorManifest.collector) -cne [IO.Path]::GetFullPath($PathBCollector)) {
    throw 'PathBCollector must match the successful pinned VM-only collector manifest.'
}
$guardHash = (Get-FileHash -LiteralPath $guardSource -Algorithm SHA256).Hash.ToUpperInvariant()
if ($guardHash -cne '7C7493E79C781A7A27FC390987A4858AFC21DF2DE7DB53BDE5E7A255FCACA58A') {
    throw "The production SMBIOS guard source drifted: $guardHash"
}
$sourcePinsBefore = Get-SourcePins $sourceRoot $vendorRoot $guardSource $PSCommandPath

$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ctest = (Get-Command ctest.exe -ErrorAction Stop).Source
Set-MsvcEnvironment
[void][IO.Directory]::CreateDirectory($output)
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $manifest))
$buildDirectory = $output
$buildLog = Join-Path (Split-Path -Parent $manifest) 'build.log'
[IO.File]::WriteAllText($buildLog, '', [Text.UTF8Encoding]::new($false))
Invoke-Logged $cmake @('-S', $sourceRoot, '-B', $buildDirectory, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release') $buildLog
Invoke-Logged $cmake @('--build', $buildDirectory, '--target',
    'smbios2_ep_prelude', 'smbios2_fixture_tests') $buildLog

$testLog = Join-Path (Split-Path -Parent $manifest) 'ctest.txt'
Invoke-Logged $ctest @('--test-dir', $buildDirectory, '--output-on-failure') $testLog

$preludePath = Join-Path $buildDirectory 'smbios2-ep-prelude.efi'
$modelTestsPath = Join-Path $buildDirectory 'smbios2_fixture_tests.exe'
if (-not (Test-Path -LiteralPath $preludePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $modelTestsPath -PathType Leaf)) {
    throw 'The EFI prelude or pure model test executable is missing.'
}
$sourcePinsAfter = Get-SourcePins $sourceRoot $vendorRoot $guardSource $PSCommandPath
if ($sourcePinsBefore.sha256 -cne $sourcePinsAfter.sha256 -or
    (ConvertTo-Json -InputObject $sourcePinsBefore.files -Depth 8 -Compress) -cne
    (ConvertTo-Json -InputObject $sourcePinsAfter.files -Depth 8 -Compress)) {
    throw 'A pinned fixture source changed during the build or tests.'
}
$buildId = ([Guid]::NewGuid().ToString('N')).ToUpperInvariant()
$preludePin = Get-FilePin $preludePath 'smbios2-ep-prelude'
$loaderPin = Get-FilePin $PathBLoader 'path-b-loader'
$collectorPin = Get-FilePin $PathBCollector 'path-b-collector'
$manifestObject = [ordered]@{
    schema = 'vm-smbios2-ep-prelude-build.v1'
    result = 'PASS'
    buildId = $buildId
    referenceRepo = $repo
    build = [ordered]@{
        sourceSetSha256 = $sourcePinsAfter.sha256
        sources = @($sourcePinsAfter.files)
        cmakeLog = $buildLog
        ctestLog = $testLog
        ctestResult = 'PASS'
    }
    productionGuard = [ordered]@{
        path = $guardSource
        sha256 = $guardHash
        unchanged = $true
    }
    prelude = [ordered]@{
        path = $preludePin.path
        bytes = $preludePin.bytes
        sha256 = $preludePin.sha256
        unsigned = $true
        fixedEspPath = 'EFI\HypersinvInventory\fixture-correction\smbios2-ep-prelude.efi'
        modes = @('correction-only', 'corrected-collector')
        modeEncoding = 'ASCII exact bytes without terminator'
    }
    evidence = [ordered]@{
        reportSchema = 'combined-vm-smbios2-ep-correction.v1'
        reportFile = 'report.txt'
        files = @('eps2-before.bin', 'eps2-after.bin', 'eps3-before.bin',
            'eps3-after.bin', 'table-before.bin', 'table-after.bin')
        reportStatusFields = @('result', 'phase', 'cause_status', 'mode_delete_status',
            'memory_map_probe_observed', 'memory_map_probe_status',
            'memory_map_probe_input_bytes', 'memory_map_probe_required_bytes',
            'memory_map_probe_map_key', 'memory_map_probe_descriptor_size',
            'memory_map_probe_descriptor_version', 'memory_map_attempt_count',
            'memory_map_descriptor_count', 'memory_map_normalization_observed',
            'memory_map_normalization_changed', 'memory_map_failure_predicate',
            'memory_map_failure_descriptor_index_observed',
            'memory_map_failure_descriptor_index',
            'memory_map_failure_descriptor_observed',
            'memory_map_failure_descriptor_type',
            'memory_map_failure_physical_start',
            'memory_map_failure_virtual_start',
            'memory_map_failure_number_of_pages',
            'memory_map_failure_attribute', 'memory_map_failure_previous_end',
            'memory_map_failure_computed_end_observed',
            'memory_map_failure_computed_end', 'memory_map_final_free_observed',
            'memory_map_final_free_status',
            'configuration_publish_observed', 'configuration_publish_status',
            'configuration_rollback_observed', 'configuration_rollback_status',
            'allocated_page_free_observed', 'allocated_page_free_status',
            'child_load_observed', 'child_load_status', 'child_start_status_observed',
            'child_start_status', 'child_unload_observed', 'child_unload_status',
            'child_device_path_bytes', 'child_loaded_image_observed',
            'child_loaded_image_protocol_status',
            'child_loaded_image_device_handle_observed',
            'child_loaded_image_device_handle_matches_parent',
            'child_loaded_image_file_path_present',
            'child_loaded_image_load_options_present',
            'child_loaded_image_load_options_size',
            'child_loaded_device_path_observed',
            'child_loaded_device_path_protocol_guid',
            'child_loaded_device_path_protocol_status',
            'child_loaded_device_path_bytes',
            'child_loaded_device_path_matches_request',
            'child_exit_data_observed', 'child_exit_data_size_bytes',
            'child_exit_data_prefix_captured', 'child_exit_data_prefix_bytes',
            'child_exit_data_prefix_hex', 'child_exit_data_free_observed',
            'child_exit_data_free_status',
            'table_baseline_free_observed', 'table_baseline_free_status')
        memoryMapAttemptFieldPrefix = 'memory_map_attempt_<1..3>_'
        memoryMapAttemptFields = @('allocate_observed', 'allocate_status', 'capacity',
            'get_observed', 'get_status', 'input_bytes', 'returned_bytes', 'map_key',
            'descriptor_size', 'descriptor_version', 'buffer_free_observed',
            'buffer_free_status')
        memoryMapDiagnosticContract = 'Each raw EFI call status is 16-digit hex; local validation failure records the first exact predicate and descriptor index/fields where applicable. The owned map buffer is sorted by PhysicalStart only when needed using full descriptor strides; normalization observed/changed are explicit. Descriptor-local bounds and overlap are validated; the firmware table is untouched.'
        efiStatuses = '16-digit hexadecimal status values, including high bit'
        childImageContract = 'LoadImage receives the ESP device handle path plus one fixed owned file path. The pre-StartImage report records child LoadedImage and EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL metadata. StartImage receives non-null initialized output pointers; an EFI error may include at most 64 bytes of a validated <=4096-byte even-length exit-data prefix.'
        changedOffsets = 'only indexes 4,8,9 may differ; report lists actual differences; offset 21 is preserved'
        tableSha256 = 'computed by the authenticated host evidence collector from both raw table files'
    }
    pathB = [ordered]@{
        loaderInputValidation = [ordered]@{
            expectedFileName = 'inventory_loader.efi'
            expectedBytes = $loaderEvidence.expectedLoaderBytes
            expectedSha256 = $loaderEvidence.expectedLoaderSha256
            sourceReceiptPath = $loaderEvidence.sourceReceiptPath
            sourceReceiptSha256 = $loaderEvidence.sourceReceiptSha256
            sourceRunId = $loaderEvidence.sourceRunId
            sourceLifecycleId = $loaderEvidence.sourceLifecycleId
            receiptLoaderSha256 = $loaderEvidence.sourceReceiptLoaderSha256
            receiptStageDirectory = $loaderEvidence.sourceStageDirectory
            espStageLoaderSha256 = $loaderEvidence.sourceEspStageLoaderSha256
            validated = $true
        }
        collectorManifest = [ordered]@{
            path = [IO.Path]::GetFullPath($CollectorManifestPath)
            sha256 = (Get-FileHash -LiteralPath $CollectorManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
        }
        loader = $loaderPin
        collector = $collectorPin
        payloadsUnmodifiedByPreludeBuild = $true
    }
    tests = [ordered]@{
        pureFixtureModel = 'PASS'
        childImageHandoffMockCases = @(
            'fixed ESP device path plus owned file path is passed to LoadImage',
            'child DeviceHandle and LoadedImageDevicePath match the request',
            'StartImage receives non-null initialized output arguments',
            'bounded error exit data is captured and released',
            'unowned child target is rejected'
        )
        testExecutable = $modelTestsPath
        testExecutableSha256 = (Get-FileHash -LiteralPath $modelTestsPath -Algorithm SHA256).Hash.ToUpperInvariant()
    }
}
[IO.File]::WriteAllText($manifest, ($manifestObject | ConvertTo-Json -Depth 12) + "`n", [Text.UTF8Encoding]::new($false))
$manifestHash = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToUpperInvariant()
Write-Output "PASS: VM-only SMBIOS2 EPS prelude build and pure tests."
Write-Output "Prelude: $($preludePin.path) SHA256=$($preludePin.sha256)"
Write-Output "Build manifest: $manifest SHA256=$manifestHash"
