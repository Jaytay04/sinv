[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProfileJson,
    [Parameter(Mandatory = $true)][ValidateSet('transparent', 'enabled')][string]$Mode,
    [ValidateSet('1', '2')][string]$RegistrationRevision = '2',
    [switch]$VmDiagnostics
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$nicWorktree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$nicRepository = [IO.Path]::GetFullPath((Join-Path $nicWorktree '..\..\..'))
$nicArtifactBase = [IO.Path]::GetFullPath((Join-Path $nicRepository 'spoof-handoff\artifacts\nic-vm-20261017\build'))
$nicReportBase = [IO.Path]::GetFullPath((Join-Path $nicRepository 'spoof-handoff\reports\nic-vm-20261017\build'))
$nicSource = [IO.Path]::GetFullPath((Join-Path $nicWorktree 'spoof-handoff\tools\device_filter'))
$nicInfSource = [IO.Path]::GetFullPath((Join-Path $nicWorktree 'spoof-handoff\tools\device_install\nic\client_nic_filter.inf'))
$nicOriginalsPath = [IO.Path]::GetFullPath((Join-Path $nicRepository 'spoof-handoff\reports\client-originals.tsv'))
$nicThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
$nicCertificateDigest = 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'

function Assert-UnderRoot([string]$Path, [string]$Root, [string]$Label) {
    $nicFullPath = [IO.Path]::GetFullPath($Path)
    $nicFullRoot = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $nicFullPath.StartsWith($nicFullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw ($Label + ' must remain under ' + $Root + ': ' + $nicFullPath)
    }
}

function Get-RequiredProperty($Object, [string]$Name, [string]$Label) {
    if ($null -eq $Object) { throw ('Guest profile is missing ' + $Label + '.') }
    $nicProperty = $Object.PSObject.Properties[$Name]
    if ($null -eq $nicProperty -or $null -eq $nicProperty.Value) {
        throw ('Guest profile is missing ' + $Label + '.')
    }
    return $nicProperty.Value
}

function ConvertTo-MacBytes([string]$Value, [string]$Label) {
    if ($Value -notmatch '^(?i:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2})$') {
        throw ($Label + ' must be six colon-separated hexadecimal bytes: ' + $Value)
    }
    return ,([byte[]]@($Value.Split(':') | ForEach-Object { [Convert]::ToByte($_, 16) }))
}

function Test-UnicastMac([byte[]]$Address) {
    if ($Address.Count -ne 6 -or (($Address[0] -band 1) -ne 0)) { return $false }
    $nonzero = $false
    $allOnes = $true
    foreach ($nicByte in $Address) {
        if ($nicByte -ne 0) { $nonzero = $true }
        if ($nicByte -ne 255) { $allOnes = $false }
    }
    return $nonzero -and -not $allOnes
}

function ConvertTo-CByteInitializer([byte[]]$Address) {
    return '{' + (($Address | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', ') + '}'
}

function ConvertTo-MacKey([byte[]]$Address) {
    return [BitConverter]::ToString($Address).Replace('-', '')
}

function Get-WorktreeRelativePath([string]$Path) {
    $nicFullPath = [IO.Path]::GetFullPath($Path)
    Assert-UnderRoot $nicFullPath $nicWorktree 'Source evidence path'
    return $nicFullPath.Substring([IO.Path]::GetFullPath($nicWorktree).TrimEnd([IO.Path]::DirectorySeparatorChar).Length + 1)
}

function ConvertTo-CStringLiteral([string]$Value) {
    $nicEscaped = $Value.Replace('\', '\\').Replace('"', '\"')
    return '"' + $nicEscaped + '"'
}

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Write-Manifest($Manifest, [string]$ReportPath) {
    Write-Utf8NoBom $ReportPath ($Manifest | ConvertTo-Json -Depth 12)
}

function Invoke-Logged([string]$Program, [string[]]$Arguments, [string]$LogPath) {
    $nicPriorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $nicLines = @(& $Program @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $nicExitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $nicPriorPreference }
    [IO.File]::WriteAllLines($LogPath, [string[]]$nicLines, [Text.UTF8Encoding]::new($false))
    if ($nicExitCode -ne 0) {
        throw ('Command failed (' + $nicExitCode + '): ' + $Program + ' ' + ($Arguments -join ' ') + '; see ' + $LogPath)
    }
}

function Invoke-Diagnostic([string]$Program, [string[]]$Arguments, [string]$LogPath) {
    $nicPriorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $nicLines = @(& $Program @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $nicExitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $nicPriorPreference }
    [IO.File]::WriteAllLines($LogPath, [string[]]$nicLines, [Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{ exitCode = $nicExitCode; log = $LogPath; output = [string[]]$nicLines }
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
    $nicTokens = $null
    $nicParseErrors = $null
    $nicAst = [Management.Automation.Language.Parser]::ParseFile($ParserScript, [ref]$nicTokens, [ref]$nicParseErrors)
    if (@($nicParseErrors).Count -ne 0) { throw 'The existing native signature parser contains syntax errors.' }
    $nicNativeAst = [Management.Automation.Language.Parser]::ParseFile($NativeVerifierScript, [ref]$nicTokens, [ref]$nicParseErrors)
    if (@($nicParseErrors).Count -ne 0) { throw 'The existing native signature verifier contains syntax errors.' }
    foreach ($nicFunctionName in @('Hex', 'HashBytes', 'DerNode', 'Read-Pe', 'Assert-NativeDriver', 'Test-SignedNativeDriver')) {
        $nicFunctionAst = if ($nicFunctionName -in @('Assert-NativeDriver', 'Test-SignedNativeDriver')) { $nicNativeAst } else { $nicAst }
        $nicDefinitions = @($nicFunctionAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $nicFunctionName }, $true))
        if ($nicDefinitions.Count -ne 1) { throw ('Expected exactly one existing signature verifier function: ' + $nicFunctionName) }
        $nicDefinition = $nicDefinitions[0].Extent.Text
        if ($nicFunctionName -ceq 'Read-Pe') {
            $nicDefinition = $nicDefinition.Replace('function Read-Pe', 'function Read-NativePe').Replace('Assert-EfiApplication $Image', 'Assert-NativeDriver $Image')
        }
        . ([scriptblock]::Create($nicDefinition))
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
    $nicCms = [Security.Cryptography.Pkcs.SignedCms]::new()
    $nicCms.Decode([IO.File]::ReadAllBytes($CatalogPath))
    $nicCms.CheckSignature($true)
    if ($nicCms.SignerInfos.Count -ne 1) { throw 'Expected one signed NIC catalog signer.' }
    $nicSigner = $nicCms.SignerInfos[0].Certificate
    if ($null -eq $nicSigner -or $nicSigner.Thumbprint -cne $Thumbprint -or
        [Convert]::ToBase64String($nicSigner.RawData) -cne [Convert]::ToBase64String($PinnedCertificate.RawData)) {
        throw 'NIC catalog signature does not use the pinned package signer.'
    }
    [DeviceFilterCertificateEdge]::Verify($nicSigner, $PinnedCertificate)
    return [pscustomobject]@{
        path = $CatalogPath
        sha256 = (Get-FileHash -LiteralPath $CatalogPath -Algorithm SHA256).Hash
        signerThumbprint = $nicSigner.Thumbprint
        pkcs7SignatureValid = $true
        pinnedCertificateSignatureValid = $true
    }
}

if (-not (Test-Path -LiteralPath $ProfileJson -PathType Leaf)) {
    throw ('VM guest profile JSON was not found: ' + $ProfileJson)
}
foreach ($nicRequiredFile in @($nicInfSource, $nicOriginalsPath, (Join-Path $nicWorktree 'spoof-handoff\tools\device_vm\build-storage.ps1'),
        (Join-Path $nicWorktree 'spoof-handoff\tools\smbios_boot\verify-signatures.ps1'),
        (Join-Path $nicWorktree 'spoof-handoff\tools\device_filter\sign.ps1'))) {
    if (-not (Test-Path -LiteralPath $nicRequiredFile -PathType Leaf)) { throw ('Required reviewed build input is missing: ' + $nicRequiredFile) }
}

$nicProfilePath = [IO.Path]::GetFullPath($ProfileJson)
Assert-UnderRoot $nicProfilePath (Join-Path $nicRepository 'spoof-handoff\reports') 'VM profile JSON'
try { $nicProfile = [IO.File]::ReadAllText($nicProfilePath) | ConvertFrom-Json }
catch { throw ('VM guest profile JSON is invalid: ' + $_.Exception.Message) }
$nicSyntheticProperty = $nicProfile.PSObject.Properties['synthetic']
if ($null -ne $nicSyntheticProperty -and [bool]$nicSyntheticProperty.Value) {
    throw 'Synthetic guest inventory cannot produce a VM NIC package.'
}

$nicMachine = Get-RequiredProperty $nicProfile 'machine' 'VM machine metadata'
$nicManufacturer = [string](Get-RequiredProperty $nicMachine 'manufacturer' 'VM manufacturer sentinel')
if ($nicManufacturer -notmatch '^(?i:VMware(?:,\s*Inc\.)?)$') {
    throw 'The inventory does not carry a VMware manufacturer sentinel.'
}
$nicUuidText = [string](Get-RequiredProperty $nicMachine 'uuid' 'VM UUID sentinel')
$nicParsedUuid = [Guid]::Empty
if (-not [Guid]::TryParse($nicUuidText, [ref]$nicParsedUuid) -or $nicParsedUuid -eq [Guid]::Empty) {
    throw 'VM UUID must be a non-empty canonical GUID.'
}
$nicWindows = Get-RequiredProperty $nicProfile 'windows' 'Windows build metadata'
$nicWindowsBuild = [int](Get-RequiredProperty $nicWindows 'build' 'Windows build number')
$nicWindowsUbr = [int](Get-RequiredProperty $nicWindows 'ubr' 'Windows UBR')
if ($nicWindowsBuild -lt 1 -or $nicWindowsUbr -lt 0) { throw 'Windows build must be positive and UBR must be non-negative.' }
$nicProfileBindings = @(Get-RequiredProperty $nicProfile 'bindings' 'four VM adapter bindings')
if ($nicProfileBindings.Count -ne 4) { throw ('Expected exactly four VM adapter bindings; found ' + $nicProfileBindings.Count + '.') }

$nicApprovedRows = @(Import-Csv -LiteralPath $nicOriginalsPath -Delimiter "`t" | Where-Object { $_.member -ceq 'FakeMacAddress' })
if ($nicApprovedRows.Count -ne 4) { throw ('Expected four approved MAC mappings in client-originals.tsv; found ' + $nicApprovedRows.Count + '.') }
$nicApproved = @{}
$nicHostIds = @{}
foreach ($nicRow in $nicApprovedRows) {
    $nicParts = $nicRow.device -split '\|', 2
    if ($nicParts.Count -ne 2 -or -not $nicParts[1]) { throw 'An approved MAC row has no exact host PnP pin.' }
    $nicOriginalBytes = ConvertTo-MacBytes ([string]$nicRow.original) 'Approved original MAC'
    $nicReplacementBytes = ConvertTo-MacBytes ([string]$nicRow.replacement) 'Approved replacement MAC'
    if (-not (Test-UnicastMac $nicOriginalBytes) -or -not (Test-UnicastMac $nicReplacementBytes) -or
        (ConvertTo-MacKey $nicOriginalBytes) -ceq (ConvertTo-MacKey $nicReplacementBytes)) {
        throw 'The approved TSV contains an invalid, non-unicast, or unchanged MAC mapping.'
    }
    $nicKey = ConvertTo-MacKey $nicOriginalBytes
    if ($nicApproved.ContainsKey($nicKey)) { throw 'The approved TSV contains duplicate original MAC values.' }
    $nicApproved[$nicKey] = [pscustomobject]@{
        originalText = [string]$nicRow.original
        replacementText = [string]$nicRow.replacement
        original = $nicOriginalBytes
        replacement = $nicReplacementBytes
    }
    $nicHostIds[$nicParts[1]] = $true
}

$nicSeenIds = @{}
$nicSeenOriginals = @{}
$nicNormalizedBindings = [Collections.Generic.List[object]]::new()
foreach ($nicBinding in $nicProfileBindings) {
    $nicInstanceId = [string](Get-RequiredProperty $nicBinding 'pnpDeviceId' 'guest PnP device ID')
    if ($nicInstanceId.Length -lt 1 -or $nicInstanceId.Length -gt 191 -or $nicInstanceId -notmatch '^[\x20-\x7e]+$' -or
        $nicInstanceId.Contains('"') -or $nicInstanceId.Contains('%')) {
        throw 'Guest PnP device ID must be 1 to 191 safe printable ASCII characters.'
    }
    if ($nicSeenIds.ContainsKey($nicInstanceId)) { throw ('Duplicate guest PnP device ID: ' + $nicInstanceId) }
    if ($nicHostIds.ContainsKey($nicInstanceId)) { throw ('VM profile PnP ID equals a production host pin: ' + $nicInstanceId) }
    $nicSeenIds[$nicInstanceId] = $true

    $nicOriginal = ConvertTo-MacBytes ([string](Get-RequiredProperty $nicBinding 'original' 'guest native original MAC')) 'Guest native original MAC'
    $nicReplacement = ConvertTo-MacBytes ([string](Get-RequiredProperty $nicBinding 'replacement' 'approved replacement MAC')) 'Guest replacement MAC'
    if (-not (Test-UnicastMac $nicOriginal) -or -not (Test-UnicastMac $nicReplacement)) {
        throw ('Guest adapter contains a non-unicast MAC: ' + $nicInstanceId)
    }
    $nicOriginalKey = ConvertTo-MacKey $nicOriginal
    if ($nicSeenOriginals.ContainsKey($nicOriginalKey)) { throw 'VM profile contains duplicate original MAC addresses.' }
    $nicSeenOriginals[$nicOriginalKey] = $true
    if (-not $nicApproved.ContainsKey($nicOriginalKey)) { throw ('Guest original MAC is not one of the captured approved originals: ' + $nicInstanceId) }
    $nicApprovedValue = $nicApproved[$nicOriginalKey]
    if ((ConvertTo-MacKey $nicReplacement) -cne (ConvertTo-MacKey $nicApprovedValue.replacement)) {
        throw ('Replacement for guest adapter does not match client-originals.tsv: ' + $nicInstanceId)
    }
    $nicNormalizedBindings.Add([pscustomobject]@{
        pnpDeviceId = $nicInstanceId
        originalText = $nicApprovedValue.originalText
        replacementText = $nicApprovedValue.replacementText
        original = $nicOriginal
        replacement = $nicReplacement
    })
}
if ($nicNormalizedBindings.Count -ne 4 -or $nicSeenOriginals.Count -ne $nicApproved.Count) {
    throw 'VM profile must map every approved original exactly once.'
}
$nicBindingsByOriginal = @{}
foreach ($nicNormalized in $nicNormalizedBindings) {
    $nicBindingsByOriginal[(ConvertTo-MacKey $nicNormalized.original)] = $nicNormalized
}
$nicNormalizedBindings.Clear()
foreach ($nicApprovedRow in $nicApprovedRows) {
    $nicApprovedOriginal = ConvertTo-MacBytes ([string]$nicApprovedRow.original) 'Approved original MAC'
    $nicApprovedKey = ConvertTo-MacKey $nicApprovedOriginal
    if (-not $nicBindingsByOriginal.ContainsKey($nicApprovedKey)) {
        throw 'VM profile is missing an approved adapter binding.'
    }
    $nicNormalizedBindings.Add($nicBindingsByOriginal[$nicApprovedKey])
}

$nicUtcNow = [DateTime]::UtcNow
$nicRunId = $nicUtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
$nicArtifactDirectory = Join-Path $nicArtifactBase $nicRunId
$nicReportDirectory = Join-Path $nicReportBase $nicRunId
Assert-UnderRoot $nicArtifactDirectory (Join-Path $nicRepository 'spoof-handoff\artifacts') 'NIC artifact directory'
Assert-UnderRoot $nicReportDirectory (Join-Path $nicRepository 'spoof-handoff\reports') 'NIC report directory'
if ((Test-Path -LiteralPath $nicArtifactDirectory) -or (Test-Path -LiteralPath $nicReportDirectory)) {
    throw 'Generated NIC build output directory already exists.'
}
New-Item -ItemType Directory -Path $nicArtifactDirectory | Out-Null
New-Item -ItemType Directory -Path $nicReportDirectory | Out-Null
$nicBuildDirectory = Join-Path $nicArtifactDirectory 'cmake'
$nicPackageDirectory = Join-Path $nicArtifactDirectory 'package'
$nicTemporaryDirectory = Join-Path $nicArtifactDirectory 'temporary'
New-Item -ItemType Directory -Path $nicPackageDirectory | Out-Null
New-Item -ItemType Directory -Path $nicTemporaryDirectory | Out-Null

$nicHeaderLines = [Collections.Generic.List[string]]::new()
$nicHeaderLines.Add('#pragma once')
$nicHeaderLines.Add('/* VM-only profile generated from validated VMware guest inventory. */')
$nicHeaderLines.Add('#define NIC_POLICY_PROFILE_BINDINGS \')
$nicHeaderLines.Add('{ \')
for ($nicIndex = 0; $nicIndex -lt $nicNormalizedBindings.Count; $nicIndex++) {
    $nicItem = $nicNormalizedBindings[$nicIndex]
    $nicSuffix = if ($nicIndex -lt ($nicNormalizedBindings.Count - 1)) { ',' } else { '' }
    $nicHeaderLines.Add(('    {{ {0}, {1}, {2} }}{3} \' -f
        (ConvertTo-CStringLiteral $nicItem.pnpDeviceId),
        (ConvertTo-CByteInitializer $nicItem.original),
        (ConvertTo-CByteInitializer $nicItem.replacement),
        $nicSuffix))
}
$nicHeaderLines.Add('}')
$nicProfileHeader = Join-Path $nicArtifactDirectory 'nic_vm_profile.h'
Write-Utf8NoBom $nicProfileHeader (($nicHeaderLines -join "`r`n") + "`r`n")

$nicPackageInf = Join-Path $nicPackageDirectory 'client_nic_filter.inf'
Copy-Item -LiteralPath $nicInfSource -Destination $nicPackageInf
$nicGuestProfileHash = (Get-FileHash -LiteralPath $nicProfilePath -Algorithm SHA256).Hash
$nicApprovedMapHash = (Get-FileHash -LiteralPath $nicOriginalsPath -Algorithm SHA256).Hash
$nicSourcePaths = @(
    (Join-Path $nicWorktree 'spoof-handoff\tools\device_filter\CMakeLists.txt'),
    (Join-Path $nicWorktree 'spoof-handoff\tools\device_filter\nic\nic_policy.c'),
    (Join-Path $nicWorktree 'spoof-handoff\tools\device_filter\nic\nic_policy.h'),
    (Join-Path $nicWorktree 'spoof-handoff\tools\device_filter\nic\nic_filter.c'),
    (Join-Path $nicWorktree 'spoof-handoff\tools\device_filter\nic\test_vm_profile.c'),
    $nicInfSource
)
$nicSourceEvidence = @($nicSourcePaths | ForEach-Object {
    if (-not (Test-Path -LiteralPath $_ -PathType Leaf)) { throw ('NIC source file missing: ' + $_) }
    [pscustomobject]@{ path = (Get-WorktreeRelativePath $_); sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
})
$nicManifest = [ordered]@{
    schema = 'vm-nic-profile-build.v1'
    status = 'building'
    capturedUtc = $nicUtcNow.ToString('o')
    runId = $nicRunId
    profileScope = 'validated VMware guest only'
    vmDiagnosticsEnabled = [bool]$VmDiagnostics
    vmDiagnosticsRegistryPath = '\Registry\Machine\System\CurrentControlSet\Services\HypersinvNicAddressFilter\Parameters\NicVmDiagnostic'
    vmDiagnosticsValueNames = @('Stage', 'Status', 'RegistrationRevision', 'SystemTime', 'WriteStatus')
    vmDiagnosticsCmakeOption = if ($VmDiagnostics) { 'DEVICE_FILTER_NIC_VM_DIAGNOSTICS=ON' } else { 'DEVICE_FILTER_NIC_VM_DIAGNOSTICS=OFF' }
    vmDiagnosticsCompileDefinition = if ($VmDiagnostics) { 'NIC_FILTER_VM_DIAGNOSTICS=1' } else { $null }
    vmDiagnosticsStageCodes = [ordered]@{
        entry = 1
        policyValidationFailure = 2
        registerFilterDriverReturned = 3
        driverEntrySuccess = 4
    }
    nicRegistrationRevision = [int]$RegistrationRevision
    nicRegistrationCompileDefinition = 'NIC_FILTER_REGISTRATION_REVISION=' + $RegistrationRevision
    sourceWorktree = $nicWorktree
    guestProfilePath = $nicProfilePath
    guestProfileSha256 = $nicGuestProfileHash
    guestManufacturer = $nicManufacturer
    guestUuid = $nicParsedUuid.ToString('D').ToUpperInvariant()
    windowsBuild = $nicWindowsBuild
    windowsUbr = $nicWindowsUbr
    mode = $Mode
    approvedMapPath = $nicOriginalsPath
    approvedMapSha256 = $nicApprovedMapHash
    profileHeaderPath = $nicProfileHeader
    profileHeaderSha256 = (Get-FileHash -LiteralPath $nicProfileHeader -Algorithm SHA256).Hash
    artifactDirectory = $nicArtifactDirectory
    reportDirectory = $nicReportDirectory
    bindings = @($nicNormalizedBindings | ForEach-Object {
        [pscustomobject]@{ pnpDeviceId = $_.pnpDeviceId; original = $_.originalText; replacement = $_.replacementText }
    })
    sourceFiles = $nicSourceEvidence
    vmDiagnosticsSource = [pscustomobject]@{
        path = 'spoof-handoff/tools/device_filter/nic/nic_filter.c'
        sha256 = ($nicSourceEvidence | Where-Object { $_.path -ceq 'spoof-handoff\tools\device_filter\nic\nic_filter.c' } | Select-Object -First 1 -ExpandProperty sha256)
    }
    driverInstalledOrLoaded = $false
    hostTrustStoresChanged = $false
    bootConfigurationChanged = $false
}
$nicManifestPath = Join-Path $nicReportDirectory 'build-profile.json'
Write-Manifest $nicManifest $nicManifestPath

$nicOriginalTemp = $env:TEMP
$nicOriginalTmp = $env:TMP
$env:TEMP = $nicTemporaryDirectory
$env:TMP = $nicTemporaryDirectory
try {
    $nicVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $nicVsWhere -PathType Leaf)) { throw 'Visual Studio vswhere.exe was not found.' }
    $nicInstallations = @(& $nicVsWhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio C++ toolchain discovery failed.' }
    $nicVcvars = $null
    foreach ($nicInstallation in $nicInstallations) {
        $nicCandidate = Join-Path $nicInstallation 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path -LiteralPath $nicCandidate -PathType Leaf) { $nicVcvars = $nicCandidate; break }
    }
    if (-not $nicVcvars) { throw 'The installed x64 MSVC toolchain is unavailable.' }
    $nicEnvironment = @(& $env:ComSpec /d /s /c ('call "' + $nicVcvars + '" >nul && set'))
    if ($LASTEXITCODE -ne 0) { throw 'MSVC toolchain initialization failed.' }
    foreach ($nicLine in $nicEnvironment) {
        $nicEquals = $nicLine.IndexOf('=')
        if ($nicEquals -gt 0) {
            [Environment]::SetEnvironmentVariable($nicLine.Substring(0, $nicEquals), $nicLine.Substring($nicEquals + 1), 'Process')
        }
    }

    $nicCmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    $nicCtest = (Get-Command ctest.exe -ErrorAction Stop).Source
    $nicConfigureArguments = @(
        '-S', $nicSource,
        '-B', $nicBuildDirectory,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DBUILD_TESTING=ON',
        '-DDEVICE_FILTER_BUILD_KERNEL=ON',
        ('-DDEVICE_FILTER_ENABLE_REWRITE=' + $(if ($Mode -ceq 'enabled') { 'ON' } else { 'OFF' })),
        ('-DDEVICE_FILTER_NIC_VM_DIAGNOSTICS=' + $(if ($VmDiagnostics) { 'ON' } else { 'OFF' })),
        ('-DDEVICE_FILTER_NIC_REGISTRATION_REVISION=' + $RegistrationRevision),
        ('-DDEVICE_FILTER_NIC_PROFILE_HEADER=' + $nicProfileHeader)
    )
    Invoke-Logged $nicCmake $nicConfigureArguments (Join-Path $nicReportDirectory 'cmake-configure.log')
    Invoke-Logged $nicCmake @('--build', $nicBuildDirectory, '--target', 'nic_address_round_trip', 'nic_profile_round_trip', 'client_nic_filter', '--parallel', '2') (Join-Path $nicReportDirectory 'cmake-build.log')
    Invoke-Logged $nicCtest @('--test-dir', $nicBuildDirectory, '--output-on-failure', '-R', '^(nic_address_round_trip|nic_profile_round_trip)$') (Join-Path $nicReportDirectory 'nic-host-tests.log')

    $nicWdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
    $nicWdkVersion = '10.0.28000.0'
    $nicInfVerif = Join-Path $nicWdkRoot ('Tools\' + $nicWdkVersion + '\x64\InfVerif.exe')
    $nicInf2Cat = Join-Path $nicWdkRoot ('bin\' + $nicWdkVersion + '\x86\Inf2Cat.exe')
    $nicSignTool = Join-Path $nicWdkRoot ('bin\' + $nicWdkVersion + '\x64\signtool.exe')
    foreach ($nicTool in @($nicInfVerif, $nicInf2Cat, $nicSignTool)) {
        if (-not (Test-Path -LiteralPath $nicTool -PathType Leaf)) { throw ('Required WDK tool is missing: ' + $nicTool) }
    }
    Invoke-Logged $nicInfVerif @('/w', '/v', $nicPackageInf) (Join-Path $nicReportDirectory 'infverif.log')

    $nicPackageSys = Join-Path $nicPackageDirectory 'client_nic_filter.sys'
    Copy-Item -LiteralPath (Join-Path $nicBuildDirectory 'client_nic_filter.sys') -Destination $nicPackageSys
    $nicSigner = Get-ChildItem Cert:\CurrentUser\My | Where-Object {
        $_.Thumbprint -ceq $nicThumbprint -and $_.HasPrivateKey
    } | Select-Object -First 1
    if ($null -eq $nicSigner) { throw ('Pinned existing signing certificate/private key unavailable in CurrentUser\My: ' + $nicThumbprint) }
    $nicPublicCertificatePath = Join-Path $nicRepository 'components\hypervisor\hypersinv.cer'
    if ((Get-FileHash -LiteralPath $nicPublicCertificatePath -Algorithm SHA256).Hash -cne $nicCertificateDigest) {
        throw 'Checked-in public signer certificate does not match its pinned digest.'
    }
    $nicPinnedCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes($nicPublicCertificatePath))
    if ($nicPinnedCertificate.Thumbprint -cne $nicThumbprint -or
        $nicPinnedCertificate.NotBefore.ToUniversalTime() -gt [DateTime]::UtcNow -or
        $nicPinnedCertificate.NotAfter.ToUniversalTime() -le [DateTime]::UtcNow) {
        throw 'Pinned existing signer is outside its validity interval or does not match its thumbprint.'
    }
    Invoke-Logged $nicSignTool @('sign', '/fd', 'SHA256', '/s', 'My', '/sha1', $nicThumbprint, '/v', $nicPackageSys) (Join-Path $nicReportDirectory 'signtool-sign-sys.log')
    $nicInf2CatInputs = @(
        [pscustomobject]@{ name = 'client_nic_filter.inf'; sha256 = (Get-FileHash -LiteralPath $nicPackageInf -Algorithm SHA256).Hash },
        [pscustomobject]@{ name = 'client_nic_filter.sys'; sha256 = (Get-FileHash -LiteralPath $nicPackageSys -Algorithm SHA256).Hash }
    )
    Invoke-Logged $nicInf2Cat @(('/driver:' + $nicPackageDirectory), '/os:10_GE_X64', '/verbose') (Join-Path $nicReportDirectory 'inf2cat.log')
    $nicCatalog = Join-Path $nicPackageDirectory 'client_nic_filter.cat'
    if (-not (Test-Path -LiteralPath $nicCatalog -PathType Leaf)) { throw 'Inf2Cat did not produce the expected NIC catalog.' }
    Invoke-Logged $nicSignTool @('sign', '/fd', 'SHA256', '/s', 'My', '/sha1', $nicThumbprint, '/v', $nicCatalog) (Join-Path $nicReportDirectory 'signtool-sign-catalog.log')

    $nicSignatureParser = Join-Path $nicWorktree 'spoof-handoff\tools\smbios_boot\verify-signatures.ps1'
    $nicNativeVerifier = Join-Path $nicWorktree 'spoof-handoff\tools\device_filter\sign.ps1'
    $nicDriverSignature = Get-PinnedDriverSignatureEvidence $nicPackageSys $nicSignatureParser $nicThumbprint $nicPinnedCertificate $nicNativeVerifier
    $nicCatalogSignature = Get-PinnedCatalogSignatureEvidence $nicCatalog $nicThumbprint $nicPinnedCertificate
    $nicKernelDiagnostic = Invoke-Diagnostic $nicSignTool @('verify', '/kp', '/v', $nicPackageSys) (Join-Path $nicReportDirectory 'signtool-kernel-diagnostic.log')
    $nicCatalogPolicyDiagnostic = Invoke-Diagnostic $nicSignTool @('verify', '/pa', '/v', $nicCatalog) (Join-Path $nicReportDirectory 'signtool-catalog-diagnostic.log')
    $nicManifest.status = 'signed VM-targeted NIC package prepared'
    $nicManifest.hostTests = 'PASS: default host map and generated VM profile round trips'
    $nicManifest.infVerification = 'PASS'
    $nicManifest.signingCertificateThumbprint = $nicThumbprint
    $nicManifest.publicCertificateSha256 = $nicCertificateDigest
    $nicManifest.embeddedDriverSignature = $nicDriverSignature
    $nicManifest.catalogSignature = $nicCatalogSignature
    $nicManifest.inf2CatInputHashes = $nicInf2CatInputs
    $nicManifest.kernelAdmissionProven = $false
    $nicManifest.kernelAdmissionDiagnostic = $nicKernelDiagnostic
    $nicManifest.catalogPolicyDiagnostic = $nicCatalogPolicyDiagnostic
    $nicManifest.packageFiles = @(
        [pscustomobject]@{ name = 'client_nic_filter.sys'; sha256 = (Get-FileHash -LiteralPath $nicPackageSys -Algorithm SHA256).Hash },
        [pscustomobject]@{ name = 'client_nic_filter.inf'; sha256 = (Get-FileHash -LiteralPath $nicPackageInf -Algorithm SHA256).Hash },
        [pscustomobject]@{ name = 'client_nic_filter.cat'; sha256 = (Get-FileHash -LiteralPath $nicCatalog -Algorithm SHA256).Hash }
    )
    Write-Manifest $nicManifest $nicManifestPath
    'VM NIC PACKAGE BUILT AND SIGNED. No driver was installed or loaded, and no trust or boot settings were changed.'
}
catch {
    $nicManifest.status = 'failed'
    $nicManifest.failure = $_.Exception.Message
    Write-Manifest $nicManifest $nicManifestPath
    throw
}
finally {
    $env:TEMP = $nicOriginalTemp
    $env:TMP = $nicOriginalTmp
}
