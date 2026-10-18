[CmdletBinding(DefaultParameterSetName = 'Fixture')]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$ReferenceRepo,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true, ParameterSetName = 'Fixture')]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$CaptureJson,

    [Parameter(ParameterSetName = 'Fixture')]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$SourceMetadataPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'PrepareCapture')]
    [switch]$PrepareCapture
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Assert-UnderRoot([string]$Root, [string]$Path, [string]$Description) {
    $rootFull = (Get-FullPath $Root) + '\'
    $pathFull = Get-FullPath $Path
    if (-not $pathFull.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must remain under $Root"
    }
}

function ConvertTo-CStringLiteral([string]$Value, [string]$Field) {
    if ([string]::IsNullOrEmpty($Value) -or $Value.Length -ge 64) {
        throw "SMBIOS value for $Field is empty or does not fit SM_VALUE_CHARS."
    }
    $builder = [Text.StringBuilder]::new()
    foreach ($character in $Value.ToCharArray()) {
        $number = [int]$character
        if ($number -lt 0x20 -or $number -gt 0x7e) {
            throw "SMBIOS value for $Field is not printable ASCII."
        }
        if ($character -eq '"' -or $character -eq '\') {
            [void]$builder.Append('\')
        }
        [void]$builder.Append($character)
    }
    return '"' + $builder.ToString() + '"'
}

function Get-TsvLockedMap([string]$Path) {
    $rows = @(Import-Csv -LiteralPath $Path -Delimiter "`t")
    $fieldRows = [ordered]@{
        system_serial = 'FakeSMBIOSSerial'
        system_vendor = 'FakeSMBIOSManufacturer'
        system_product = 'FakeSMBIOSProduct'
        board_serial = 'FakeMotherboardSerial'
        board_vendor = 'FakeMotherboardManufacturer'
        board_product = 'FakeMotherboardProduct'
        system_uuid = 'FakeSMBIOSUUID'
    }
    $lockedReplacements = [ordered]@{
        system_serial = '4091-37'
        system_vendor = 'NORTHLAB'
        system_product = 'NODE'
        board_serial = '08E4821_MD2F297027'
        board_vendor = 'Example Devices Corporation'
        board_product = 'Client Reference Board (CB-4200)'
        system_uuid = 'A17F24D0-68E2-4A09-9C31-2B7D8450E6F9'
    }
    $map = [ordered]@{}
    foreach ($field in $fieldRows.Keys) {
        $matches = @($rows | Where-Object { $_.member -ceq $fieldRows[$field] })
        if ($matches.Count -ne 1 -or
            [string]$matches[0].replacement -cne [string]$lockedReplacements[$field]) {
            throw "The authoritative TSV does not contain exactly the locked replacement for $field."
        }
        $map[$field] = [pscustomobject]@{
            Member = $fieldRows[$field]
            Replacement = [string]$matches[0].replacement
        }
    }
    return $map
}

function Get-FieldValue($Object, [string]$Name, [string]$Field) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value -or
        $property.Value -isnot [string] -or [string]::IsNullOrEmpty($property.Value)) {
        throw "Capture report has no non-empty string $Name for $Field."
    }
    return [string]$property.Value
}

function Assert-CaptureFieldKeys([string]$Json) {
    $required = @(
        'system_serial', 'system_vendor', 'system_product', 'board_serial',
        'board_vendor', 'board_product', 'system_uuid'
    )
    $fieldsMatch = [regex]::Match($Json, '"fields"\s*:\s*\{')
    if (-not $fieldsMatch.Success) { throw 'Capture report has no fields object.' }
    $open = $fieldsMatch.Index + $fieldsMatch.Length - 1
    $depth = 0
    $inString = $false
    $escaped = $false
    $close = -1
    for ($index = $open; $index -lt $Json.Length; $index++) {
        $character = $Json[$index]
        if ($inString) {
            if ($escaped) { $escaped = $false; continue }
            if ($character -eq '\') { $escaped = $true; continue }
            if ($character -eq '"') { $inString = $false }
            continue
        }
        if ($character -eq '"') { $inString = $true; continue }
        if ($character -eq '{') { $depth++; continue }
        if ($character -eq '}') {
            $depth--
            if ($depth -eq 0) { $close = $index; break }
        }
    }
    if ($close -le $open) { throw 'Capture report fields object is malformed.' }
    $body = $Json.Substring($open + 1, $close - $open - 1)
    $keys = @([regex]::Matches($body, '(?m)^\s*"(?<name>[^"\\]+)"\s*:\s*\{') |
        ForEach-Object { $_.Groups['name'].Value })
    foreach ($name in $required) {
        if (@($keys | Where-Object { $_ -ceq $name }).Count -ne 1) {
            throw "Capture report must contain exactly one $name field."
        }
    }
    foreach ($name in $keys) {
        if ($name -cnotin $required) { throw "Capture report contains an unexpected SMBIOS field: $name" }
    }
    if ($keys.Count -ne $required.Count) {
        throw "Capture report must contain exactly seven SMBIOS fields; found $($keys.Count)."
    }
}

function New-VmHeader($Capture, $LockedMap) {
    if ($Capture.schema -cne 'smbios-windows-capture.v1' -or
        $Capture.mode -cne 'capture' -or $Capture.result -cne 'PASS' -or
        $Capture.native.source -cne 'GetSystemFirmwareTable(RSMB)' -or
        $Capture.wmi.source -cne 'ROOT\CIMV2' -or
        $Capture.wmi.systemProductClass -cne 'Win32_ComputerSystemProduct' -or
        $Capture.wmi.baseboardClass -cne 'Win32_BaseBoard' -or
        $Capture.wmi.systemProductInstanceCountExact -ne $true -or
        $Capture.wmi.baseboardInstanceCountExact -ne $true -or
        $Capture.wmi.systemProductInstancesObserved -ne 1 -or
        $Capture.wmi.baseboardInstancesObserved -ne 1) {
        throw 'Capture report lacks a successful, single-instance RSMB/WMI read for the expected classes.'
    }

    $names = @(
        'system_serial', 'system_vendor', 'system_product', 'board_serial',
        'board_vendor', 'board_product', 'system_uuid'
    )
    $originals = [ordered]@{}
    foreach ($field in $names) {
        $entry = $Capture.fields.PSObject.Properties[$field]
        if ($null -eq $entry -or $entry.Value.nativeMatchesWmi -ne $true) {
            throw "Capture report does not prove native/WMI agreement for $field."
        }
        $native = Get-FieldValue $entry.Value 'observedNative' $field
        $wmi = Get-FieldValue $entry.Value 'observedWmi' $field
        if ($native -cne $wmi) { throw "Capture values differ for $field." }
        if ($native.Length -ge 64) { throw "Capture value exceeds SMBIOS field capacity for $field." }
        if ($field -ceq 'system_uuid' -and
            $native -cnotmatch '^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$') {
            throw 'Captured VM system UUID is not in canonical 36-character form.'
        }
        if ($native -ceq [string]$LockedMap[$field].Replacement) {
            throw "Captured original already equals the locked replacement for $field."
        }
        $originals[$field] = $native
    }

    if ($Capture.machineIdentity.source -cne 'SMBIOS Type 1 RSMB + Win32_ComputerSystemProduct' -or
        [string]$Capture.machineIdentity.systemVendorNative -cne [string]$originals.system_vendor -or
        [string]$Capture.machineIdentity.systemVendorWmi -cne [string]$originals.system_vendor -or
        [string]$Capture.machineIdentity.systemProductNative -cne [string]$originals.system_product -or
        [string]$Capture.machineIdentity.systemProductWmi -cne [string]$originals.system_product) {
        throw 'Capture report VM identity metadata does not match the fresh SMBIOS/WMI system vendor and product.'
    }
    if ([string]$originals.system_vendor -cne 'VMware, Inc.' -or
        [string]$originals.system_product -cne 'VMware20,1') {
        throw 'Capture report does not identify the expected VMware virtual machine model.'
    }

    $header = [Collections.Generic.List[string]]::new()
    $header.Add('#ifndef HYPERSINV_VM_SMBIOS_VALUES_H')
    $header.Add('#define HYPERSINV_VM_SMBIOS_VALUES_H')
    $header.Add('')
    $header.Add('#include "smbios_table.h"')
    $header.Add('#ifndef HYPERSINV_VM_ONLY_SMBIOS_PROFILE')
    $header.Add('#error This values header is only valid in an explicit VM-only profile.')
    $header.Add('#endif')
    $header.Add('')
    $header.Add('static const SM_VALUES sm_vm_originals = {{')
    foreach ($field in $names) {
        $header.Add('    ' + (ConvertTo-CStringLiteral $originals[$field] $field) + ',')
    }
    $header.Add('}, 0, 0, 0};')
    $header.Add('')
    $header.Add('static const SM_VALUES sm_vm_replacements = {{')
    foreach ($field in $names) {
        $header.Add('    ' + (ConvertTo-CStringLiteral ([string]$LockedMap[$field].Replacement) $field) + ',')
    }
    $header.Add('}, 0, 0, 0};')
    $header.Add('')
    $header.Add('#define sm_host_originals sm_vm_originals')
    $header.Add('#define sm_host_replacements sm_vm_replacements')
    $header.Add('static const char *const sm_field_names[SM_FIELD_COUNT] = {')
    $header.Add('    "system_serial", "system_vendor", "system_product", "board_serial",')
    $header.Add('    "board_vendor", "board_product", "system_uuid"')
    $header.Add('};')
    $header.Add('#define SM_ACTIVE_BOOT_MASK SM_MASK_ALL')
    $header.Add('#define SM_DISK_SERIAL_ENABLED 0')
    $header.Add('#define SM_ADAPTER_MAC_ENABLED 0')
    $header.Add('#define SM_CPU_SERIAL_ENABLED 0')
    $header.Add('#define SM_GPU_SERIAL_ENABLED 0')
    $header.Add('')
    $header.Add('#endif')
    return ,$header.ToArray()
}

function Initialize-MsvcEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Visual Studio vswhere was not found.'
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
            [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
        }
    }
}

function Invoke-Logged([string]$Program, [string[]]$Arguments, [string]$LogPath) {
    & $Program @Arguments *>&1 | ForEach-Object {
        $line = [string]$_
        Add-Content -LiteralPath $LogPath -Value $line
        Write-Output $line
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE. See $LogPath"
    }
}

function Get-CompileSourcePins([string]$SourceRoot, [string]$ReferenceRoot, [string]$BuilderPath) {
    $pins = [Collections.Generic.List[object]]::new()
    $sourceFull = [IO.Path]::GetFullPath($SourceRoot).TrimEnd('\') + '\'
    $referenceFull = [IO.Path]::GetFullPath($ReferenceRoot).TrimEnd('\') + '\'
    $sourceFiles = @(Get-ChildItem -LiteralPath $SourceRoot -Recurse -File -ErrorAction Stop |
        Where-Object { $_.Extension -in @('.c', '.h', '.cpp', '.hpp', '.in') -or $_.Name -ceq 'CMakeLists.txt' })
    foreach ($file in $sourceFiles) {
        $relative = $file.FullName.Substring($sourceFull.Length).Replace('\', '/')
        $pins.Add([ordered]@{
            path = 'source/' + $relative
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
        })
    }

    $vendorRoot = Join-Path $ReferenceRoot 'components\uefi-loader\src\vendor'
    $vendorFull = [IO.Path]::GetFullPath($vendorRoot).TrimEnd('\') + '\'
    $vendorFiles = @(Get-ChildItem -LiteralPath $vendorRoot -Recurse -File -ErrorAction Stop |
        Where-Object { $_.Extension -in @('.c', '.h', '.cpp', '.hpp', '.in') })
    foreach ($file in $vendorFiles) {
        $relative = $file.FullName.Substring($vendorFull.Length).Replace('\', '/')
        $pins.Add([ordered]@{
            path = 'reference/vendor/' + $relative
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
        })
    }

    $efiReferenceRoot = Join-Path $ReferenceRoot 'tools\efi_inventory\efi'
    $efiReferenceFull = [IO.Path]::GetFullPath($efiReferenceRoot).TrimEnd('\') + '\'
    $efiReferenceFiles = @(Get-ChildItem -LiteralPath $efiReferenceRoot -Recurse -File -ErrorAction Stop |
        Where-Object { $_.Extension -in @('.c', '.h', '.cpp', '.hpp', '.in') -or $_.Name -ceq 'CMakeLists.txt' })
    foreach ($file in $efiReferenceFiles) {
        $relative = $file.FullName.Substring($efiReferenceFull.Length).Replace('\', '/')
        $pins.Add([ordered]@{
            path = 'reference/efi_inventory/' + $relative
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
        })
    }

    $builderFull = [IO.Path]::GetFullPath($BuilderPath)
    $pins.Add([ordered]@{
        path = 'builder/' + [IO.Path]::GetFileName($builderFull)
        sha256 = (Get-FileHash -LiteralPath $builderFull -Algorithm SHA256).Hash.ToUpperInvariant()
    })

    $sortedPins = @($pins.ToArray() | Sort-Object -Property path -CaseSensitive)
    $canonicalLines = @($sortedPins | ForEach-Object { [string]$_.path + "`t" + [string]$_.sha256 })
    $canonical = ($canonicalLines -join "`n") + "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $sourceSetHash = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical)))).Replace('-', '')
    } finally { $sha.Dispose() }

    return [pscustomobject]@{ files = $sortedPins; sha256 = $sourceSetHash }
}

$repo = Get-FullPath $ReferenceRepo
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\smbios_boot'))
$mapPath = Join-Path $repo 'spoof-handoff\reports\client-originals.tsv'
$artifactsRoot = Join-Path $repo 'spoof-handoff\artifacts'
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $mapPath -PathType Leaf)) {
    throw 'Reference repository is missing spoof-handoff/reports/client-originals.tsv.'
}
Assert-UnderRoot $artifactsRoot $output 'OutputDirectory'
if (Test-Path -LiteralPath $output) {
    throw "OutputDirectory must be new for each profile build: $output"
}
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ctest = (Get-Command ctest.exe -ErrorAction Stop).Source
$ninja = (Get-Command ninja.exe -ErrorAction Stop).Source
$mode = 'capture_tool'
$headerPath = $null
$headerLines = $null
$capture = $null
$capturePath = $null
$captureHash = $null
$sourceMetadataPin = $null
$lockedMap = $null
if ($PSCmdlet.ParameterSetName -eq 'Fixture') {
    $capturePath = [IO.Path]::GetFullPath($CaptureJson)
    $captureHash = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash
    $captureText = Get-Content -LiteralPath $capturePath -Raw
    Assert-CaptureFieldKeys $captureText
    $capture = $captureText | ConvertFrom-Json
    if ($SourceMetadataPath) {
        $sourceMetadataFullPath = [IO.Path]::GetFullPath($SourceMetadataPath)
        $sourceMetadataText = Get-Content -LiteralPath $sourceMetadataFullPath -Raw
        $sourceMetadata = $sourceMetadataText | ConvertFrom-Json
        $sourceCapture = $null
        $sourceCapturePointer = $null
        $rootCaptureProperty = $sourceMetadata.PSObject.Properties['smbiosCapture']
        $observationsProperty = $sourceMetadata.PSObject.Properties['observations']
        if ($null -ne $rootCaptureProperty -and $null -ne $rootCaptureProperty.Value) {
            $sourceCapture = $rootCaptureProperty.Value
            $sourceCapturePointer = 'smbiosCapture'
        } elseif ($null -ne $observationsProperty -and $null -ne $observationsProperty.Value) {
            $smbiosProperty = $observationsProperty.Value.PSObject.Properties['smbios']
            if ($null -ne $smbiosProperty -and $null -ne $smbiosProperty.Value) {
                $rawCaptureProperty = $smbiosProperty.Value.PSObject.Properties['rawCapture']
                if ($null -ne $rawCaptureProperty -and $null -ne $rawCaptureProperty.Value) {
                    $sourceCapture = $rawCaptureProperty.Value
                    $sourceCapturePointer = 'observations.smbios.rawCapture'
                }
            }
        }
        if ($null -eq $sourceCapture) {
            throw 'Source metadata does not contain a supported SMBIOS capture projection.'
        }
        $sourceCaptureJson = ConvertTo-Json -InputObject $sourceCapture -Depth 50 -Compress
        $projectedCaptureJson = ConvertTo-Json -InputObject $capture -Depth 50 -Compress
        if ($sourceCaptureJson -cne $projectedCaptureJson) {
            throw 'CaptureJson does not exactly match the SMBIOS capture embedded in SourceMetadataPath.'
        }
        $sourceMetadataPin = [ordered]@{
            path = $sourceMetadataFullPath
            sha256 = (Get-FileHash -LiteralPath $sourceMetadataFullPath -Algorithm SHA256).Hash.ToUpperInvariant()
            projectionPointer = $sourceCapturePointer
        }
    }
    $lockedMap = Get-TsvLockedMap $mapPath
    $headerLines = New-VmHeader $capture $lockedMap
    $mode = 'vm_only_smbios_fixture'
} else {
    $lockedMap = Get-TsvLockedMap $mapPath
}

$compileSourcesBefore = Get-CompileSourcePins $source $repo $PSCommandPath

[void](New-Item -ItemType Directory -Path $output -Force)
$buildRoot = Join-Path $output 'build'
$buildLog = Join-Path $output 'build.log'
$configure = @('-S', $source, '-B', $buildRoot, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', ('-DREFERENCE_REPO=' + $repo), '-DBUILD_TESTING=ON')
if ($mode -eq 'vm_only_smbios_fixture') {
    $headerPath = Join-Path $output 'smbios_values.h'
    [IO.File]::WriteAllText($headerPath, ($headerLines -join "`r`n") + "`r`n", [Text.UTF8Encoding]::new($false))
    $configure += @('-DHYPERSINV_VM_ONLY_SMBIOS_PROFILE=ON', ('-DHYPERSINV_VM_SMBIOS_VALUES_HEADER=' + $headerPath))
}
Initialize-MsvcEnvironment
New-Item -ItemType File -Path $buildLog | Out-Null
Invoke-Logged $cmake $configure $buildLog
if ($mode -eq 'capture_tool') {
    Invoke-Logged $cmake @('--build', $buildRoot, '--target', 'smbios_windows_readback', 'smbios_table_tests', 'efi_smbios_tests') $buildLog
} else {
    Invoke-Logged $cmake @('--build', $buildRoot) $buildLog
}
$testLog = Join-Path $output 'ctest.txt'
$ctestResult = & $ctest --test-dir $buildRoot --output-on-failure 2>&1
$ctestExitCode = $LASTEXITCODE
[IO.File]::WriteAllLines($testLog, @($ctestResult | ForEach-Object { [string]$_ }), [Text.UTF8Encoding]::new($false))
if ($ctestExitCode -ne 0) { throw "CTest failed with exit code $ctestExitCode. See $testLog" }

$compileSourcesAfter = Get-CompileSourcePins $source $repo $PSCommandPath
if ($compileSourcesBefore.sha256 -cne $compileSourcesAfter.sha256 -or
    (ConvertTo-Json -InputObject $compileSourcesBefore.files -Depth 8 -Compress) -cne
    (ConvertTo-Json -InputObject $compileSourcesAfter.files -Depth 8 -Compress)) {
    throw 'A pinned SMBIOS build source changed while the fixture was being built or tested.'
}

$readback = Join-Path $buildRoot 'smbios_windows_readback.exe'
$collector = Join-Path $buildRoot 'efi\inventory.efi'
if (-not (Test-Path -LiteralPath $readback -PathType Leaf)) { throw 'Build did not produce smbios_windows_readback.exe.' }
if ($mode -eq 'vm_only_smbios_fixture' -and -not (Test-Path -LiteralPath $collector -PathType Leaf)) {
    throw 'VM-only build did not produce efi/inventory.efi.'
}
$files = @($readback)
if ($mode -eq 'vm_only_smbios_fixture') { $files += $collector; $files += $headerPath }
$pins = @($files | ForEach-Object {
    $item = Get-Item -LiteralPath $_
    [ordered]@{ path = $_; bytes = $item.Length; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
})
$buildIdentityPath = Join-Path $buildRoot 'build-id.txt'
if (-not (Test-Path -LiteralPath $buildIdentityPath -PathType Leaf)) { throw 'Build identity file is missing.' }
$buildIdentity = (Get-Content -LiteralPath $buildIdentityPath -Raw -ErrorAction Stop).Trim()
if ($buildIdentity -cnotmatch '\A[A-F0-9]{32}\z') { throw 'Build identity file is malformed.' }
$manifest = [ordered]@{
    schema = 'combined-vm-smbios-fixture.v1'
    result = 'PASS'
    mode = $mode
    sourceTree = $source
    referenceRepo = $repo
    replacementMap = [ordered]@{ path = $mapPath; sha256 = (Get-FileHash -LiteralPath $mapPath -Algorithm SHA256).Hash }
    guestCapture = if ($mode -eq 'vm_only_smbios_fixture') {
        [ordered]@{ path = $capturePath; sha256 = $captureHash; sourceMetadata = $sourceMetadataPin }
    } else { $null }
    compileSourceSetSha256 = [string]$compileSourcesAfter.sha256
    compileSources = @($compileSourcesAfter.files)
    buildIdentity = [ordered]@{
        value = $buildIdentity
        path = $buildIdentityPath
        sha256 = (Get-FileHash -LiteralPath $buildIdentityPath -Algorithm SHA256).Hash.ToUpperInvariant()
    }
    valuesHeader = if ($headerPath) { $headerPath } else { $null }
    collector = if ($mode -eq 'vm_only_smbios_fixture') { $collector } else { $null }
    windowsReadback = $readback
    hostSmbiosTests = 'PASS'
    files = $pins
}
$manifestPath = Join-Path $output 'fixture-build.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
Write-Output "PASS: $mode build and existing SMBIOS tests. Evidence: $manifestPath"
