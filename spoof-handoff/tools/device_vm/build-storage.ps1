[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GuestInventory,
    [Parameter(Mandatory = $true)][string]$ReplacementSerial,
    [Parameter(Mandatory = $true)][ValidateSet('transparent', 'enabled')][string]$Mode,
    [Parameter(Mandatory = $true)][ValidateSet('0', '3')][string]$StartType,
    [switch]$HostTestsOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$deviceWorktree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$deviceRepository = [IO.Path]::GetFullPath((Join-Path $deviceWorktree '..\..\..'))
$deviceArtifactBase = [IO.Path]::GetFullPath((Join-Path $deviceRepository 'spoof-handoff\artifacts\device-vm-20261017\build'))
$deviceReportBase = [IO.Path]::GetFullPath((Join-Path $deviceRepository 'spoof-handoff\reports\device-vm-20261017\build'))
$deviceSource = [IO.Path]::GetFullPath((Join-Path $deviceWorktree 'spoof-handoff\tools\device_filter'))
$deviceTemplatePath = [IO.Path]::GetFullPath((Join-Path $deviceWorktree 'spoof-handoff\tools\device_cycle_packages\client_storage_filter_pnp.inf'))
$deviceThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'

function Assert-UnderRoot([string]$Path, [string]$Root, [string]$Label) {
    $deviceFullPath = [IO.Path]::GetFullPath($Path)
    $deviceFullRoot = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $deviceFullPath.StartsWith($deviceFullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw ($Label + ' must remain under ' + $Root + ': ' + $deviceFullPath)
    }
}

function Get-RequiredProperty($Object, [string[]]$Names, [string]$Label) {
    foreach ($deviceName in $Names) {
        $deviceProperty = $Object.PSObject.Properties[$deviceName]
        if ($null -ne $deviceProperty -and $null -ne $deviceProperty.Value) {
            return $deviceProperty.Value
        }
    }
    throw ('Guest inventory is missing ' + $Label + '.')
}

function Get-RequiredBoolean($Object, [string[]]$Names, [string]$Label) {
    $deviceValue = Get-RequiredProperty $Object $Names $Label
    if ($deviceValue -is [bool]) { return $deviceValue }
    if ([string]$deviceValue -match '^(?i:true|false)$') { return ([string]$deviceValue -ieq 'true') }
    throw ($Label + ' must be a Boolean value.')
}

function ConvertTo-CStringLiteral([string]$Value) {
    $deviceEscaped = $Value.Replace('\', '\\').Replace('"', '\"')
    return '"' + $deviceEscaped + '"'
}

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Invoke-Logged([string]$Program, [string[]]$Arguments, [string]$LogPath) {
    $devicePriorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $deviceLines = @(& $Program @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $deviceExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $devicePriorPreference
    }
    [IO.File]::WriteAllLines($LogPath, [string[]]$deviceLines, [Text.UTF8Encoding]::new($false))
    if ($deviceExitCode -ne 0) {
        throw ('Command failed (' + $deviceExitCode + '): ' + $Program + ' ' + ($Arguments -join ' ') + '; see ' + $LogPath)
    }
}

function Invoke-Diagnostic([string]$Program, [string[]]$Arguments, [string]$LogPath) {
    $devicePriorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $deviceLines = @(& $Program @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $deviceExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $devicePriorPreference
    }
    [IO.File]::WriteAllLines($LogPath, [string[]]$deviceLines, [Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{ exitCode = $deviceExitCode; log = $LogPath; output = [string[]]$deviceLines }
}

function Get-PinnedDriverSignatureEvidence(
    [string]$Image,
    [string]$ParserScript,
    [string]$Thumbprint,
    [Security.Cryptography.X509Certificates.X509Certificate2]$PinnedCertificate,
    [string]$NativeVerifierScript) {
    Add-Type -AssemblyName System.Security
    if (-not ('DeviceFilterCertificateEdge' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;
public static class DeviceFilterCertificateEdge {
    [DllImport("crypt32.dll",SetLastError=true)] static extern bool CryptVerifyCertificateSignatureEx(IntPtr p,uint e,uint t,IntPtr s,uint i,IntPtr c,uint f,IntPtr x);
    public static void Verify(X509Certificate2 subject,X509Certificate2 issuer) {
        if(!CryptVerifyCertificateSignatureEx(IntPtr.Zero,1,2,subject.Handle,2,issuer.Handle,0,IntPtr.Zero)) throw new Win32Exception();
    }
}
'@
    }
    [DeviceFilterCertificateEdge]::Verify($PinnedCertificate, $PinnedCertificate)
    $deviceTokens = $null
    $deviceParseErrors = $null
    $deviceAst = [Management.Automation.Language.Parser]::ParseFile($ParserScript, [ref]$deviceTokens, [ref]$deviceParseErrors)
    if (@($deviceParseErrors).Count -ne 0) { throw 'The existing native signature parser contains syntax errors.' }
    $deviceNativeAst = [Management.Automation.Language.Parser]::ParseFile($NativeVerifierScript, [ref]$deviceTokens, [ref]$deviceParseErrors)
    if (@($deviceParseErrors).Count -ne 0) { throw 'The existing native signature verifier contains syntax errors.' }
    foreach ($deviceFunctionName in @('Hex', 'HashBytes', 'DerNode', 'Read-Pe', 'Assert-NativeDriver', 'Test-SignedNativeDriver')) {
        $deviceFunctionAst = if ($deviceFunctionName -in @('Assert-NativeDriver','Test-SignedNativeDriver')) { $deviceNativeAst } else { $deviceAst }
        $deviceDefinitions = @($deviceFunctionAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $deviceFunctionName }, $true))
        if ($deviceDefinitions.Count -ne 1) { throw ('Expected exactly one existing signature verifier function: ' + $deviceFunctionName) }
        $deviceDefinition = $deviceDefinitions[0].Extent.Text
        if ($deviceFunctionName -ceq 'Read-Pe') {
            $deviceDefinition = $deviceDefinition.Replace('function Read-Pe', 'function Read-NativePe').Replace('Assert-EfiApplication $Image', 'Assert-NativeDriver $Image')
        }
        . ([scriptblock]::Create($deviceDefinition))
    }
    $script:signThumbprint = $Thumbprint
    $script:signCertificate = $PinnedCertificate
    return Test-SignedNativeDriver $Image
}

function Get-PinnedCatalogSignatureEvidence(
    [string]$CatalogPath,
    [string]$Thumbprint,
    [Security.Cryptography.X509Certificates.X509Certificate2]$PinnedCertificate) {
    Add-Type -AssemblyName System.Security
    $deviceCms = [Security.Cryptography.Pkcs.SignedCms]::new()
    $deviceCms.Decode([IO.File]::ReadAllBytes($CatalogPath))
    $deviceCms.CheckSignature($true)
    if ($deviceCms.SignerInfos.Count -ne 1) { throw 'Expected one signed catalog signer.' }
    $deviceSigner = $deviceCms.SignerInfos[0].Certificate
    if ($null -eq $deviceSigner -or $deviceSigner.Thumbprint -cne $Thumbprint -or
        [Convert]::ToBase64String($deviceSigner.RawData) -cne [Convert]::ToBase64String($PinnedCertificate.RawData)) {
        throw 'Catalog signature does not use the pinned VM package signer.'
    }
    [DeviceFilterCertificateEdge]::Verify($deviceSigner, $PinnedCertificate)
    return [pscustomobject]@{
        path = $CatalogPath
        sha256 = (Get-FileHash -LiteralPath $CatalogPath -Algorithm SHA256).Hash
        signerThumbprint = $deviceSigner.Thumbprint
        pkcs7SignatureValid = $true
        pinnedCertificateSignatureValid = $true
    }
}

if (-not (Test-Path -LiteralPath $GuestInventory -PathType Leaf)) {
    throw ('Guest inventory JSON was not found: ' + $GuestInventory)
}
$deviceGuestInventoryPath = [IO.Path]::GetFullPath($GuestInventory)
if (-not (Test-Path -LiteralPath $deviceTemplatePath -PathType Leaf)) {
    throw ('Reviewed storage AddFilter template was not found: ' + $deviceTemplatePath)
}

$deviceInventoryText = [IO.File]::ReadAllText($deviceGuestInventoryPath)
try { $deviceInventory = $deviceInventoryText | ConvertFrom-Json }
catch { throw ('Guest inventory JSON is invalid: ' + $_.Exception.Message) }

$deviceSynthetic = $false
$deviceSyntheticProperty = $deviceInventory.PSObject.Properties['synthetic']
if ($null -ne $deviceSyntheticProperty -and $deviceSyntheticProperty.Value -is [bool]) {
    $deviceSynthetic = $deviceSyntheticProperty.Value
}
if ($HostTestsOnly -and -not $deviceSynthetic) {
    throw 'HostTestsOnly accepts only a guest inventory explicitly marked synthetic=true.'
}
if (-not $HostTestsOnly -and $deviceSynthetic) {
    throw 'Synthetic inventory cannot produce a driver package. Use HostTestsOnly for synthetic fixtures.'
}

$deviceMachine = $deviceInventory
$deviceMachineProperty = $deviceInventory.PSObject.Properties['machine']
if ($null -ne $deviceMachineProperty -and $null -ne $deviceMachineProperty.Value) {
    $deviceMachine = $deviceMachineProperty.Value
}
$deviceModel = [string](Get-RequiredProperty $deviceMachine @('model') 'model sentinel')
$deviceVendor = [string](Get-RequiredProperty $deviceMachine @('manufacturer', 'vendor') 'VM vendor sentinel')
if ($deviceModel -notin @('VMware Virtual Platform','VMware20,1') -or $deviceVendor -notmatch '^(?i:VMware(?:,\s*Inc\.)?)$') {
    throw 'The inventory does not carry a supported VMware model and VMware vendor sentinel.'
}
$deviceWindows = Get-RequiredProperty $deviceInventory @('windows') 'Windows build metadata'
$deviceWindowsBuild = [int](Get-RequiredProperty $deviceWindows @('build') 'Windows build number')
$deviceWindowsUbr = [int](Get-RequiredProperty $deviceWindows @('ubr') 'Windows UBR')
if ($deviceWindowsBuild -lt 1 -or $deviceWindowsUbr -lt 0) {
    throw 'Windows build and UBR metadata must be non-negative, with a positive build number.'
}

$deviceDisk = Get-RequiredProperty $deviceInventory @('disk', 'disk0') 'disk0'
$deviceDiskNumber = [int](Get-RequiredProperty $deviceDisk @('number') 'disk number')
if ($deviceDiskNumber -ne 0) {
    throw 'The selected storage profile must describe disk number 0.'
}
$deviceIsBoot = Get-RequiredBoolean $deviceDisk @('isBoot', 'isBootDisk', 'boot') 'disk0 boot flag'
$deviceIsSystem = Get-RequiredBoolean $deviceDisk @('isSystem', 'isSystemDisk', 'system') 'disk0 system flag'
if (-not $deviceIsBoot -or -not $deviceIsSystem) {
    throw 'disk0 must be positively identified as both the boot disk and the system disk.'
}

$deviceOriginalSerial = [string](Get-RequiredProperty $deviceDisk @('serial') 'disk0 original serial')
$deviceWmiSerial = [string](Get-RequiredProperty $deviceDisk @('wmiSerial') 'disk0 fresh WMI serial')
if (-not [string]::Equals($deviceOriginalSerial, $deviceWmiSerial, [StringComparison]::Ordinal)) {
    throw ('Fresh native and WMI disk serials differ: native="' + $deviceOriginalSerial + '" WMI="' + $deviceWmiSerial + '".')
}
$deviceInstanceId = [string](Get-RequiredProperty $deviceDisk @('pnpDeviceId', 'instanceId') 'disk0 PnP instanceId')
$deviceHardwareIds = @(Get-RequiredProperty $deviceDisk @('hardwareIds') 'disk0 HardwareIds')
$devicePath = [string](Get-RequiredProperty $deviceDisk @('devicePath') 'disk0 device path')
if (-not [string]::Equals($devicePath, '\\.\PHYSICALDRIVE0', [StringComparison]::OrdinalIgnoreCase)) {
    throw ('The inventory devicePath must identify \\.\PHYSICALDRIVE0; found "' + $devicePath + '".')
}
if ($deviceOriginalSerial.Length -lt 1 -or $deviceOriginalSerial.Length -gt 128 -or $deviceOriginalSerial -notmatch '^[\x20-\x7e]+$') {
    throw 'disk0 serial must contain 1 to 128 printable ASCII characters.'
}
if ($ReplacementSerial.Length -ne $deviceOriginalSerial.Length -or $ReplacementSerial -notmatch '^[\x20-\x7e]+$') {
    throw 'ReplacementSerial must be printable ASCII and have the same length as the guest serial.'
}
if ([string]::Equals($ReplacementSerial, $deviceOriginalSerial, [StringComparison]::Ordinal)) {
    throw 'ReplacementSerial must differ from the guest serial.'
}
if ($deviceInstanceId -notmatch '^(?i:SCSI\\DISK&VEN_(?:VMWARE&PROD_[A-Z0-9_.&-]+|NVME&PROD_VMWARE_[A-Z0-9_.&-]+)\\[A-Z0-9_.&-]+)$') {
    throw 'disk0 instanceId must identify a VMware virtual SCSI or NVMe disk.'
}
$deviceHardwareId = $null
foreach ($deviceCandidateId in $deviceHardwareIds) {
    $deviceCandidateText = [string]$deviceCandidateId
    if ($deviceCandidateText -match '^(?i:SCSI\\Disk(?:VMware[A-Z0-9_.&-]*|NVMe_+VMware[A-Z0-9_.&-]*))$') {
        $deviceHardwareId = $deviceCandidateText
        break
    }
}
if (-not $deviceHardwareId) {
    throw 'disk0 HardwareIds must include a VMware virtual disk hardware ID.'
}
if ($deviceInstanceId -match '[\r\n%"]' -or $deviceHardwareId -match '[\r\n%",]') {
    throw 'Guest PnP identifiers contain characters that are unsafe for generated C or INF source.'
}

$deviceRunId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
$deviceArtifactDirectory = Join-Path $deviceArtifactBase $deviceRunId
$deviceReportDirectory = Join-Path $deviceReportBase $deviceRunId
Assert-UnderRoot $deviceArtifactDirectory (Join-Path $deviceRepository 'spoof-handoff\artifacts') 'Artifact directory'
Assert-UnderRoot $deviceReportDirectory (Join-Path $deviceRepository 'spoof-handoff\reports') 'Report directory'
if ((Test-Path -LiteralPath $deviceArtifactDirectory) -or (Test-Path -LiteralPath $deviceReportDirectory)) {
    throw 'Generated build output directory already exists.'
}
New-Item -ItemType Directory -Path $deviceArtifactDirectory | Out-Null
New-Item -ItemType Directory -Path $deviceReportDirectory | Out-Null
$deviceBuildDirectory = Join-Path $deviceArtifactDirectory 'cmake'
$devicePackageDirectory = Join-Path $deviceArtifactDirectory 'package'
$deviceTemporaryDirectory = Join-Path $deviceArtifactDirectory 'temporary'
New-Item -ItemType Directory -Path $devicePackageDirectory | Out-Null
New-Item -ItemType Directory -Path $deviceTemporaryDirectory | Out-Null

$deviceHeaderComment = if ($HostTestsOnly) { 'Synthetic host-only test profile. Not a deployable package.' } else { 'VM-only profile generated from validated guest inventory.' }
$deviceHeader = @(
    '#pragma once',
    ('/* ' + $deviceHeaderComment + ' */'),
    ('#define DEVICE_FILTER_STORAGE_ORIGINAL_SERIAL ' + (ConvertTo-CStringLiteral $deviceOriginalSerial)),
    ('#define DEVICE_FILTER_STORAGE_REPLACEMENT_SERIAL ' + (ConvertTo-CStringLiteral $ReplacementSerial)),
    ('#define DEVICE_FILTER_STORAGE_TARGET_INSTANCE_ID L' + (ConvertTo-CStringLiteral $deviceInstanceId)),
    ''
) -join "`r`n"
$deviceProfileHeader = Join-Path $deviceArtifactDirectory 'device_storage_profile.h'
Write-Utf8NoBom $deviceProfileHeader $deviceHeader

$deviceInfText = [IO.File]::ReadAllText($deviceTemplatePath)
$deviceTemplateId = 'SCSI\DiskNVMe_________________________INTEL_SSDPEKNU010TZ____002C'
$deviceTemplateExtension = 'ExtensionId={3D285AA2-BB24-4EF2-9A06-4BC36519E14D}'
if (-not $deviceInfText.Contains($deviceTemplateId) -or -not $deviceInfText.Contains($deviceTemplateExtension) -or -not $deviceInfText.Contains('StartType=0')) {
    throw 'The reviewed AddFilter template no longer matches the expected version; refusing to transform it.'
}
$deviceVmExtensionId = [Guid]::NewGuid().ToString('B').ToUpperInvariant()
$deviceInfText = $deviceInfText.Replace($deviceTemplateId, $deviceHardwareId)
$deviceInfText = $deviceInfText.Replace($deviceTemplateExtension, 'ExtensionId=' + $deviceVmExtensionId)
$deviceInfText = $deviceInfText.Replace('StartType=0', 'StartType=' + $StartType)
$deviceInfText = $deviceInfText.Replace('Intel SSDPEKNU010TZ boot-device storage filter', 'VMware virtual boot-disk storage filter')
$deviceInfText = $deviceInfText.Replace('Hypersinv storage boot-filter package', 'VMware virtual boot-disk storage package')
if ($deviceInfText.Contains($deviceTemplateId) -or $deviceInfText.Contains($deviceTemplateExtension) -or
    -not $deviceInfText.Contains('ExtensionId=' + $deviceVmExtensionId) -or
    -not $deviceInfText.Contains('StartType=' + $StartType) -or
    $deviceInfText -match '(?i)INTEL') {
    throw 'Generated INF contains a stale host identity or does not contain the selected VM profile values.'
}
$deviceInfPath = Join-Path $devicePackageDirectory 'client_storage_filter.inf'
[IO.File]::WriteAllText($deviceInfPath, $deviceInfText, [Text.Encoding]::ASCII)

$deviceInventoryHash = (Get-FileHash -LiteralPath $deviceGuestInventoryPath -Algorithm SHA256).Hash
$deviceManifest = [ordered]@{
    status = 'prepared'
    capturedUtc = [DateTime]::UtcNow.ToString('o')
    runId = $deviceRunId
    sourceWorktree = $deviceWorktree
    guestInventoryPath = $deviceGuestInventoryPath
    guestInventorySha256 = $deviceInventoryHash
    syntheticInventory = $deviceSynthetic
    mode = $Mode
    startType = [int]$StartType
    windowsBuild = $deviceWindowsBuild
    windowsUbr = $deviceWindowsUbr
    diskNumber = $deviceDiskNumber
    disk0IsBoot = $deviceIsBoot
    disk0IsSystem = $deviceIsSystem
    disk0DevicePath = $devicePath
    disk0HardwareId = $deviceHardwareId
    disk0InstanceId = $deviceInstanceId
    originalSerial = $deviceOriginalSerial
    wmiOriginalSerial = $deviceWmiSerial
    replacementSerial = $ReplacementSerial
    infExtensionId = $deviceVmExtensionId
    artifactDirectory = $deviceArtifactDirectory
    reportDirectory = $deviceReportDirectory
    hostTestsOnly = [bool]$HostTestsOnly
    driverInstalledOrLoaded = $false
    bootConfigurationChanged = $false
}
Write-Utf8NoBom (Join-Path $deviceReportDirectory 'build-profile.json') ($deviceManifest | ConvertTo-Json -Depth 6)

$deviceOriginalTemp = $env:TEMP
$deviceOriginalTmp = $env:TMP
$env:TEMP = $deviceTemporaryDirectory
$env:TMP = $deviceTemporaryDirectory
try {
    $deviceVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $deviceVsWhere -PathType Leaf)) { throw 'Visual Studio vswhere.exe was not found.' }
    $deviceInstallations = @(& $deviceVsWhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio C++ toolchain discovery failed.' }
    $deviceVcvars = $null
    foreach ($deviceInstallation in $deviceInstallations) {
        $deviceCandidateVcvars = Join-Path $deviceInstallation 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path -LiteralPath $deviceCandidateVcvars -PathType Leaf) { $deviceVcvars = $deviceCandidateVcvars; break }
    }
    if (-not $deviceVcvars) { throw 'The installed x64 MSVC toolchain is unavailable.' }
    $deviceEnvironmentLines = @(& $env:ComSpec /d /s /c ('call "' + $deviceVcvars + '" >nul && set'))
    if ($LASTEXITCODE -ne 0) { throw 'MSVC toolchain initialization failed.' }
    foreach ($deviceLine in $deviceEnvironmentLines) {
        $deviceEquals = $deviceLine.IndexOf('=')
        if ($deviceEquals -gt 0) {
            [Environment]::SetEnvironmentVariable($deviceLine.Substring(0, $deviceEquals), $deviceLine.Substring($deviceEquals + 1), 'Process')
        }
    }

    $deviceCmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    $deviceCtest = (Get-Command ctest.exe -ErrorAction Stop).Source
    $deviceNinja = (Get-Command ninja.exe -ErrorAction Stop).Source
    $deviceConfigureArguments = @(
        '-S', $deviceSource,
        '-B', $deviceBuildDirectory,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DBUILD_TESTING=ON',
        ('-DDEVICE_FILTER_BUILD_KERNEL=' + $(if ($HostTestsOnly) { 'OFF' } else { 'ON' })),
        ('-DDEVICE_FILTER_ENABLE_REWRITE=' + $(if ($Mode -ceq 'enabled') { 'ON' } else { 'OFF' })),
        ('-DDEVICE_FILTER_STORAGE_PROFILE_HEADER=' + $deviceProfileHeader)
    )
    Invoke-Logged $deviceCmake $deviceConfigureArguments (Join-Path $deviceReportDirectory 'cmake-configure.log')
    $deviceBuildArguments = @('--build', $deviceBuildDirectory, '--target', 'storage_serial_round_trip')
    if (-not $HostTestsOnly) { $deviceBuildArguments += 'client_storage_filter' }
    $deviceBuildArguments += @('--parallel', '2')
    Invoke-Logged $deviceCmake $deviceBuildArguments (Join-Path $deviceReportDirectory 'cmake-build.log')
    Invoke-Logged $deviceCtest @('--test-dir', $deviceBuildDirectory, '--output-on-failure', '-R', '^storage_serial_round_trip$') (Join-Path $deviceReportDirectory 'storage-host-test.log')

    $deviceWdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
    $deviceWdkVersion = '10.0.28000.0'
    $deviceInfVerif = Join-Path $deviceWdkRoot ('Tools\' + $deviceWdkVersion + '\x64\InfVerif.exe')
    if (-not (Test-Path -LiteralPath $deviceInfVerif -PathType Leaf)) { throw ('Required WDK tool is missing: ' + $deviceInfVerif) }
    Invoke-Logged $deviceInfVerif @('/w', '/v', $deviceInfPath) (Join-Path $deviceReportDirectory 'infverif.log')

    $deviceManifest.status = 'storage host tests passed'
    $deviceManifest.storageHostTest = 'PASS'
    $deviceManifest.infVerification = 'PASS'
    $deviceManifest.generatedProfileHeaderSha256 = (Get-FileHash -LiteralPath $deviceProfileHeader -Algorithm SHA256).Hash
    $deviceManifest.generatedInfSha256 = (Get-FileHash -LiteralPath $deviceInfPath -Algorithm SHA256).Hash
    if ($HostTestsOnly) {
        $deviceManifest.package = 'not built; host test only'
        Write-Utf8NoBom (Join-Path $deviceReportDirectory 'build-profile.json') ($deviceManifest | ConvertTo-Json -Depth 6)
        'SYNTHETIC STORAGE PROFILE HOST TEST PASS. Generated INF passed InfVerif; no driver was built, signed, installed, or loaded.'
        return
    }

    $deviceInf2Cat = Join-Path $deviceWdkRoot ('bin\' + $deviceWdkVersion + '\x86\Inf2Cat.exe')
    $deviceSignTool = Join-Path $deviceWdkRoot ('bin\' + $deviceWdkVersion + '\x64\signtool.exe')
    $deviceSignatureParser = Join-Path $deviceWorktree 'spoof-handoff\tools\smbios_boot\verify-signatures.ps1'
    $deviceSignerScript = Join-Path $deviceWorktree 'spoof-handoff\tools\device_filter\sign.ps1'
    foreach ($deviceTool in @($deviceInf2Cat, $deviceSignTool)) {
        if (-not (Test-Path -LiteralPath $deviceTool -PathType Leaf)) { throw ('Required WDK tool is missing: ' + $deviceTool) }
    }
    foreach ($deviceReference in @($deviceSignatureParser, $deviceSignerScript)) {
        if (-not (Test-Path -LiteralPath $deviceReference -PathType Leaf)) { throw ('Existing signature verifier reference is missing: ' + $deviceReference) }
    }
    $deviceBuiltSys = Join-Path $deviceBuildDirectory 'client_storage_filter.sys'
    if (-not (Test-Path -LiteralPath $deviceBuiltSys -PathType Leaf)) { throw 'The storage .sys build output is missing.' }
    $devicePackageSys = Join-Path $devicePackageDirectory 'client_storage_filter.sys'
    Copy-Item -LiteralPath $deviceBuiltSys -Destination $devicePackageSys

    $deviceSigningCertificate = Get-ChildItem Cert:\CurrentUser\My | Where-Object {
        $_.Thumbprint -eq $deviceThumbprint -and $_.HasPrivateKey
    } | Select-Object -First 1
    if ($null -eq $deviceSigningCertificate) {
        throw ('The pinned signing certificate with a private key is unavailable in CurrentUser\My: ' + $deviceThumbprint)
    }
    $devicePublicCertificatePath = Join-Path $deviceRepository 'components\hypervisor\hypersinv.cer'
    if ((Get-FileHash -LiteralPath $devicePublicCertificatePath -Algorithm SHA256).Hash -cne 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE') {
        throw 'The checked-in public signer certificate does not match its pinned digest.'
    }
    $devicePinnedCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes($devicePublicCertificatePath))
    if ($devicePinnedCertificate.Thumbprint -cne $deviceThumbprint -or
        $devicePinnedCertificate.NotBefore.ToUniversalTime() -gt [DateTime]::UtcNow -or
        $devicePinnedCertificate.NotAfter.ToUniversalTime() -le [DateTime]::UtcNow) {
        throw 'The checked-in signing certificate is unpinned or outside its validity interval.'
    }
    Invoke-Logged $deviceSignTool @('sign', '/fd', 'SHA256', '/s', 'My', '/sha1', $deviceThumbprint, '/v', $devicePackageSys) (Join-Path $deviceReportDirectory 'signtool-sign-sys.log')
    $deviceInf2CatInputHashes = @(
        [pscustomobject]@{ name = 'client_storage_filter.inf'; sha256 = (Get-FileHash -LiteralPath $deviceInfPath -Algorithm SHA256).Hash },
        [pscustomobject]@{ name = 'client_storage_filter.sys'; sha256 = (Get-FileHash -LiteralPath $devicePackageSys -Algorithm SHA256).Hash }
    )
    Invoke-Logged $deviceInf2Cat @(('/driver:' + $devicePackageDirectory), '/os:10_GE_X64', '/verbose') (Join-Path $deviceReportDirectory 'inf2cat.log')
    $deviceCatalog = Join-Path $devicePackageDirectory 'client_storage_filter.cat'
    if (-not (Test-Path -LiteralPath $deviceCatalog -PathType Leaf)) { throw 'Inf2Cat did not produce the expected catalog.' }
    Invoke-Logged $deviceSignTool @('sign', '/fd', 'SHA256', '/s', 'My', '/sha1', $deviceThumbprint, '/v', $deviceCatalog) (Join-Path $deviceReportDirectory 'signtool-sign-catalog.log')
    $deviceDriverSignature = Get-PinnedDriverSignatureEvidence $devicePackageSys $deviceSignatureParser $deviceThumbprint $devicePinnedCertificate $deviceSignerScript
    $deviceCatalogSignature = Get-PinnedCatalogSignatureEvidence $deviceCatalog $deviceThumbprint $devicePinnedCertificate
    $deviceKernelAdmission = Invoke-Diagnostic $deviceSignTool @('verify', '/kp', '/v', $devicePackageSys) (Join-Path $deviceReportDirectory 'signtool-kernel-admission.log')
    $deviceCatalogPolicy = Invoke-Diagnostic $deviceSignTool @('verify', '/pa', '/v', $deviceCatalog) (Join-Path $deviceReportDirectory 'signtool-catalog-policy.log')
    $deviceCatalogMembership = Invoke-Diagnostic $deviceSignTool @('verify', '/kp', '/v', '/c', $deviceCatalog, $devicePackageSys) (Join-Path $deviceReportDirectory 'signtool-catalog-membership.log')

    $deviceManifest.status = 'signed VM-targeted storage package prepared'
    $deviceManifest.storageHostTest = 'PASS'
    $deviceManifest.package = $devicePackageDirectory
    $deviceManifest.signingCertificateThumbprint = $deviceThumbprint
    $deviceManifest.embeddedDriverSignature = $deviceDriverSignature
    $deviceManifest.catalogSignature = $deviceCatalogSignature
    $deviceManifest.inf2CatInputHashes = $deviceInf2CatInputHashes
    $deviceManifest.signtoolKernelPolicyCheckPassed = ($deviceKernelAdmission.exitCode -eq 0)
    $deviceManifest.kernelDriverLoadProven = $false
    $deviceManifest.kernelAdmissionDiagnostic = $deviceKernelAdmission
    $deviceManifest.catalogPolicyDiagnostic = $deviceCatalogPolicy
    $deviceManifest.catalogMembershipDiagnostic = $deviceCatalogMembership
    $deviceManifest.hostTrustStoresChanged = $false
    $deviceManifest.driverInstalledOrLoaded = $false
    $deviceManifest.bootConfigurationChanged = $false
    $deviceManifest.packageFiles = @(
        [pscustomobject]@{ name = 'client_storage_filter.sys'; sha256 = (Get-FileHash -LiteralPath $devicePackageSys -Algorithm SHA256).Hash },
        [pscustomobject]@{ name = 'client_storage_filter.inf'; sha256 = (Get-FileHash -LiteralPath $deviceInfPath -Algorithm SHA256).Hash },
        [pscustomobject]@{ name = 'client_storage_filter.cat'; sha256 = (Get-FileHash -LiteralPath $deviceCatalog -Algorithm SHA256).Hash }
    )
    Write-Utf8NoBom (Join-Path $deviceReportDirectory 'build-profile.json') ($deviceManifest | ConvertTo-Json -Depth 8)
    'VM-TARGETED STORAGE PACKAGE BUILT AND SIGNED. It was not installed or loaded; no VM or host boot state was changed.'
}
finally {
    $env:TEMP = $deviceOriginalTemp
    $env:TMP = $deviceOriginalTmp
}
