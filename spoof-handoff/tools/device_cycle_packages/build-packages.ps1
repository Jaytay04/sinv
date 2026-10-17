[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$cycleSource = [IO.Path]::GetFullPath($PSScriptRoot)
$cycleTools = [IO.Path]::GetFullPath((Join-Path $cycleSource '..'))
$cycleHandoff = [IO.Path]::GetFullPath((Join-Path $cycleTools '..'))
$cycleWorktree = [IO.Path]::GetFullPath((Join-Path $cycleHandoff '..'))
$cycleRepository = [IO.Path]::GetFullPath((Join-Path $cycleWorktree '..\..\..'))
$cyclePackageRoot = Join-Path $cycleRepository 'spoof-handoff'
$cycleOutputRoot = Join-Path $cyclePackageRoot 'artifacts\device-reboot-cycle-20261016\packages'
$cycleReportRoot = Join-Path $cyclePackageRoot 'reports\device-reboot-cycle-20261016\packaging'
$cycleSignedInputRoot = Join-Path $cyclePackageRoot 'artifacts\device-filters-20261014\signed-final\enabled'
$cycleCertificateFile = Join-Path $cycleRepository 'components\hypervisor\hypersinv.cer'
$cycleThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
$cycleCertificateSha256 = 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'
$cycleStorageSha256 = '6E862E7EF6D360CC7AD021AF18162431856E9CB680E306E08DE60450A6D06302'
$cycleNicSha256 = '3B57CAC5EA1EF966052BA287C2EB2D188C6F26C90FD5C7A2314B52BC13F20E15'
$cycleStorageHardwareId = 'SCSI\DiskNVMe_________________________INTEL_SSDPEKNU010TZ____002C'
$cycleStorageInstanceId = 'SCSI\DISK&VEN_NVME&PROD_INTEL_SSDPEKNU01\5&8432851&0&000000'
$cycleWindowsKits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$cycleInf2Cat = Join-Path $cycleWindowsKits 'bin\10.0.28000.0\x86\Inf2Cat.exe'
$cycleInfVerif = Join-Path $cycleWindowsKits 'Tools\10.0.28000.0\x64\InfVerif.exe'
$cycleSignTool = Join-Path $cycleWindowsKits 'bin\10.0.28000.0\x86\signtool.exe'
$cycleUtf8 = [Text.UTF8Encoding]::new($false)
$cycleFailures = New-Object 'System.Collections.Generic.List[string]'
$cycleCommands = New-Object 'System.Collections.Generic.List[object]'
$cyclePackages = New-Object 'System.Collections.Generic.List[object]'

foreach ($cyclePath in @($cycleOutputRoot, $cycleReportRoot)) {
    if (-not $cyclePath.StartsWith($cyclePackageRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'All package and evidence paths must remain under spoof-handoff.'
    }
}
if (Test-Path -LiteralPath $cycleOutputRoot) { throw ('Refusing to overwrite package output: ' + $cycleOutputRoot) }
if (Test-Path -LiteralPath $cycleReportRoot) { throw ('Refusing to overwrite package evidence: ' + $cycleReportRoot) }
foreach ($cycleTool in @($cycleInf2Cat, $cycleInfVerif, $cycleSignTool)) {
    if (-not (Test-Path -LiteralPath $cycleTool -PathType Leaf)) { throw ('Required Windows Kit tool is missing: ' + $cycleTool) }
}

if ((Get-FileHash -LiteralPath $cycleCertificateFile -Algorithm SHA256).Hash -cne $cycleCertificateSha256) {
    throw 'Pinned existing public signing certificate hash differs.'
}
$cyclePublicCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($cycleCertificateFile)
if ($cyclePublicCertificate.Thumbprint -cne $cycleThumbprint) { throw 'Pinned existing certificate thumbprint differs.' }
$cyclePrivateCertificate = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $cycleThumbprint) -ErrorAction Stop
if (-not $cyclePrivateCertificate.HasPrivateKey) { throw 'The existing CurrentUser certificate has no accessible private key.' }
$cycleDisk = Get-Disk -Number 0 | Select-Object Number, FriendlyName, IsBoot, IsSystem
if (-not $cycleDisk.IsBoot -or -not $cycleDisk.IsSystem) {
    throw ('Captured disk role changed; expected disk 0 IsBoot=True and IsSystem=True, actual IsBoot=' + $cycleDisk.IsBoot + '; IsSystem=' + $cycleDisk.IsSystem)
}
$cycleDevice = Get-PnpDevice -InstanceId $cycleStorageInstanceId -ErrorAction Stop | Select-Object Status, Class, FriendlyName, InstanceId
$cycleHardwareProperty = Get-PnpDeviceProperty -InstanceId $cycleStorageInstanceId -KeyName 'DEVPKEY_Device_HardwareIds' -ErrorAction Stop
$cycleCurrentHardwareIds = @($cycleHardwareProperty.Data)
$cycleTrustMembership = @()
foreach ($store in @('Cert:\CurrentUser\Root', 'Cert:\LocalMachine\Root', 'Cert:\CurrentUser\TrustedPublisher', 'Cert:\LocalMachine\TrustedPublisher')) {
    $present = @(Get-ChildItem -LiteralPath $store -ErrorAction SilentlyContinue | Where-Object { $_.Thumbprint -eq $cycleThumbprint }).Count -gt 0
    $cycleTrustMembership += [pscustomobject]@{ store = $store; certificatePresent = $present }
}
$cycleServiceGroups = @((Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\ServiceGroupOrder' -Name List -ErrorAction Stop).List)
if ($cycleDevice.Status -cne 'OK' -or $cycleDevice.InstanceId -cne $cycleStorageInstanceId) {
    throw 'The captured disk PnP instance is not present and ready.'
}
if ($cycleCurrentHardwareIds -cnotcontains $cycleStorageHardwareId) {
    throw ('Captured hardware ID is not present on the current target device: ' + $cycleStorageHardwareId)
}

New-Item -ItemType Directory -Path $cycleOutputRoot | Out-Null
New-Item -ItemType Directory -Path $cycleReportRoot | Out-Null

function ConvertTo-CycleNativeArgument([string]$Value) {
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]'\') { $slashes++; continue }
        if ($character -eq [char]'"') {
            [void]$builder.Append(('\' * (($slashes * 2) + 1)))
            [void]$builder.Append('"')
            $slashes = 0
            continue
        }
        if ($slashes -gt 0) { [void]$builder.Append(('\' * $slashes)); $slashes = 0 }
        [void]$builder.Append($character)
    }
    if ($slashes -gt 0) { [void]$builder.Append(('\' * ($slashes * 2))) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-CycleTool {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$ToolPath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ToolPath
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.Arguments = (($Arguments | ForEach-Object { ConvertTo-CycleNativeArgument $_ }) -join ' ')
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw ('Failed to start tool: ' + $ToolPath) }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $exitCode = $process.ExitCode
    $process.Dispose()

    [IO.File]::WriteAllText((Join-Path $cycleReportRoot ($Name + '.stdout.txt')), $stdout, $cycleUtf8)
    [IO.File]::WriteAllText((Join-Path $cycleReportRoot ($Name + '.stderr.txt')), $stderr, $cycleUtf8)
    [IO.File]::WriteAllText((Join-Path $cycleReportRoot ($Name + '.exit.txt')), ($exitCode.ToString() + "`n"), $cycleUtf8)
    $cycleCommands.Add([pscustomobject]@{
        name = $Name
        executable = $ToolPath
        arguments = $Arguments
        workingDirectory = $WorkingDirectory
        stdout = ($Name + '.stdout.txt')
        stderr = ($Name + '.stderr.txt')
        exitCodeFile = ($Name + '.exit.txt')
        exitCode = $exitCode
    })
    return $exitCode
}

$cyclePackageInputs = @(
    [pscustomobject]@{
        name = 'storage'
        infName = 'client_storage_filter.inf'
        infSource = Join-Path $cycleSource 'client_storage_filter_pnp.inf'
        sysName = 'client_storage_filter.sys'
        expectedSysSha256 = $cycleStorageSha256
        expectedServiceStartType = 0
        installModel = 'Device-specific declarative AddFilter upper filter'
        hardwareId = $cycleStorageHardwareId
        instanceId = $cycleStorageInstanceId
    },
    [pscustomobject]@{
        name = 'nic'
        infName = 'client_nic_filter.inf'
        infSource = Join-Path $cycleTools 'device_install\nic\client_nic_filter.inf'
        sysName = 'client_nic_filter.sys'
        expectedSysSha256 = $cycleNicSha256
        expectedServiceStartType = 3
        installModel = 'NDIS NetService component installed through netcfg'
        hardwareId = 'HypersinvNicAddressFilter'
        instanceId = 'ROOT\VMWARE\0000;ROOT\VMWARE\0001;PCI\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\3&11583659&1&A3;PCI\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\4&3FA6681&0&00E2'
    }
)

foreach ($package in $cyclePackageInputs) {
    $folder = Join-Path $cycleOutputRoot $package.name
    New-Item -ItemType Directory -Path $folder | Out-Null
    $infPath = Join-Path $folder $package.infName
    $sysSource = Join-Path $cycleSignedInputRoot $package.sysName
    $sysPath = Join-Path $folder $package.sysName
    $catPath = Join-Path $folder ([IO.Path]::ChangeExtension($package.infName, '.cat'))

    $actualSourceHash = (Get-FileHash -LiteralPath $sysSource -Algorithm SHA256).Hash
    if ($actualSourceHash -cne $package.expectedSysSha256) {
        $cycleFailures.Add($package.name + ' SYS source SHA256 mismatch: expected=' + $package.expectedSysSha256 + '; actual=' + $actualSourceHash)
        continue
    }
    Copy-Item -LiteralPath $sysSource -Destination $sysPath
    Copy-Item -LiteralPath $package.infSource -Destination $infPath
    $copiedHash = (Get-FileHash -LiteralPath $sysPath -Algorithm SHA256).Hash
    if ($copiedHash -cne $package.expectedSysSha256) {
        $cycleFailures.Add($package.name + ' copied SYS SHA256 mismatch: expected=' + $package.expectedSysSha256 + '; actual=' + $copiedHash)
        continue
    }

    $infText = [IO.File]::ReadAllText($infPath)
    $expectedStartText = 'StartType=' + $package.expectedServiceStartType
    if (-not $infText.Contains($expectedStartText)) {
        $cycleFailures.Add($package.name + ' INF does not declare expected service StartType=' + $package.expectedServiceStartType)
    }
    if ($package.name -eq 'storage' -and $infText -notmatch [regex]::Escape($package.hardwareId)) {
        $cycleFailures.Add('Storage INF target hardware ID differs from the captured package ID.')
    }

    $infVerifyExit = Invoke-CycleTool -Name ($package.name + '-infverif') -ToolPath $cycleInfVerif -Arguments @('/w', '/v', '/wbuild', '10.0.18362', $infPath) -WorkingDirectory $folder
    if ($infVerifyExit -ne 0) { $cycleFailures.Add($package.name + ' InfVerif exited ' + $infVerifyExit) }
    $inf2CatArguments = @('/driver:' + $folder)
    $inf2CatArguments += '/os:10_GE_X64'
    $inf2CatArguments += '/verbose'
    $inf2CatExit = Invoke-CycleTool -Name ($package.name + '-inf2cat') -ToolPath $cycleInf2Cat -Arguments $inf2CatArguments -WorkingDirectory $folder
    if ($inf2CatExit -ne 0 -or -not (Test-Path -LiteralPath $catPath -PathType Leaf)) {
        $cycleFailures.Add($package.name + ' Inf2Cat failed or did not produce the declared catalog; exit=' + $inf2CatExit)
        continue
    }
    $signExit = Invoke-CycleTool -Name ($package.name + '-signtool-sign') -ToolPath $cycleSignTool -Arguments @('sign', '/v', '/fd', 'SHA256', '/s', 'My', '/sha1', $cycleThumbprint, $catPath) -WorkingDirectory $folder
    if ($signExit -ne 0) {
        $cycleFailures.Add($package.name + ' signtool sign exited ' + $signExit)
        continue
    }
    $verifyCatExit = Invoke-CycleTool -Name ($package.name + '-signtool-verify-cat-pa') -ToolPath $cycleSignTool -Arguments @('verify', '/v', '/pa', $catPath) -WorkingDirectory $folder
    $verifyInfExit = Invoke-CycleTool -Name ($package.name + '-signtool-verify-inf-pa') -ToolPath $cycleSignTool -Arguments @('verify', '/v', '/pa', '/c', $catPath, $infPath) -WorkingDirectory $folder
    $verifySysExit = Invoke-CycleTool -Name ($package.name + '-signtool-verify-sys-pa') -ToolPath $cycleSignTool -Arguments @('verify', '/v', '/pa', '/c', $catPath, $sysPath) -WorkingDirectory $folder
    $catSignature = Get-AuthenticodeSignature -FilePath $catPath
    $sysSignature = Get-AuthenticodeSignature -FilePath $sysPath
    $catSigner = if ($catSignature.SignerCertificate) { $catSignature.SignerCertificate.Thumbprint } else { $null }
    $sysSigner = if ($sysSignature.SignerCertificate) { $sysSignature.SignerCertificate.Thumbprint } else { $null }
    if ($catSigner -cne $cycleThumbprint) { $cycleFailures.Add($package.name + ' catalog signer thumbprint mismatch: actual=' + $catSigner) }
    if ($sysSigner -cne $cycleThumbprint) { $cycleFailures.Add($package.name + ' embedded SYS signer thumbprint mismatch: actual=' + $sysSigner) }

    $cyclePackages.Add([pscustomobject]@{
        name = $package.name
        inf = $package.infName
        infSha256 = (Get-FileHash -LiteralPath $infPath -Algorithm SHA256).Hash
        catalog = (Split-Path $catPath -Leaf)
        catalogSha256 = (Get-FileHash -LiteralPath $catPath -Algorithm SHA256).Hash
        sys = $package.sysName
        sysSha256 = $copiedHash
        sourceSysSha256 = $actualSourceHash
        serviceStartType = $package.expectedServiceStartType
        loadOrderGroup = $null
        installModel = $package.installModel
        hardwareId = $package.hardwareId
        capturedInstanceId = $package.instanceId
        infVerifExitCode = $infVerifyExit
        inf2CatExitCode = $inf2CatExit
        catalogSigningExitCode = $signExit
        verifyCatalogPaExitCode = $verifyCatExit
        verifyInfMemberPaExitCode = $verifyInfExit
        verifySysMemberPaExitCode = $verifySysExit
        catalogSignerThumbprint = $catSigner
        catalogSignatureTrustStatus = [string]$catSignature.Status
        catalogSignatureTrustMessage = $catSignature.StatusMessage
        sysEmbeddedSignerThumbprint = $sysSigner
        sysSignatureTrustStatus = [string]$sysSignature.Status
        sysSignatureTrustMessage = $sysSignature.StatusMessage
        livePnPInstallOrBinding = $false
    })
}

$cycleFileRows = @()
foreach ($file in Get-ChildItem -LiteralPath $cycleOutputRoot -File -Recurse) {
    $cycleFileRows += [pscustomobject]@{
        path = $file.FullName.Substring($cycleRepository.Length + 1).Replace('\', '/')
        size = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}
$cycleTrustFailures = @($cyclePackages | Where-Object {
    $_.catalogSignatureTrustStatus -ne 'Valid' -or $_.verifyCatalogPaExitCode -ne 0 -or
    $_.verifyInfMemberPaExitCode -ne 0 -or $_.verifySysMemberPaExitCode -ne 0
})
$cycleResult = if ($cycleFailures.Count -eq 0) { 'PACKAGE_PREPARED_TRUST_UNVERIFIED' } else { 'PACKAGE_BUILD_FAILED' }
$cycleManifest = [ordered]@{
    schema = 'device-reboot-cycle-packaging-20261016-v1'
    capturedUtc = [DateTime]::UtcNow.ToString('o')
    result = $cycleResult
    certificate = [ordered]@{
        thumbprint = $cycleThumbprint
        derSha256 = $cycleCertificateSha256
        certificateFileSha256 = (Get-FileHash -LiteralPath $cycleCertificateFile -Algorithm SHA256).Hash
        signingKeySource = 'Existing CurrentUser\\My certificate/private key. No trust-store edits were made.'
        trustMembershipBeforeBuild = $cycleTrustMembership
    }
    storagePnPAssessment = [ordered]@{
        disk = $cycleDisk
        device = $cycleDevice
        currentHardwareIds = $cycleCurrentHardwareIds
        isBootAndSystemDisk = ($cycleDisk.IsBoot -and $cycleDisk.IsSystem)
        evidence = 'Fresh Get-Disk, Get-PnpDevice and DEVPKEY_Device_HardwareIds reads from this package build.'
        serviceStartType = 0
        rationale = 'The registered filter participates in the boot-disk stack. In the isolated Windows 26200.9445 VM, the transparent demand-start package reproduced bugcheck 0x7B; the boot-start package with identical executable code booted and attached successfully. This establishes the VM start-policy fix, not physical-host compatibility.'
        restartPlan = 'Keep physical-host activation disabled. Validate boot, actual filter-stack attachment, native and WMI substitution, package removal, and clean restoration in the disposable VM before reviewing any deployment.'
        loadOrderGroup = $null
        currentServiceGroupOrder = $cycleServiceGroups
        pnpFilterGroupPresent = ($cycleServiceGroups -contains 'PnP Filter')
        loadOrderGroupRationale = 'Omitted because the device filter uses declarative AddFilter ordering and the captured host ServiceGroupOrder list has no PnP Filter group; no unsupported group is introduced.'
        capturedHardwareId = $cycleStorageHardwareId
        capturedDeviceInstanceId = $cycleStorageInstanceId
        freshHardwareIdsRead = $true
    }
    packages = $cyclePackages.ToArray()
    commands = $cycleCommands.ToArray()
    trustGate = [ordered]@{
        rootTrustPresentAtBuild = (@($cycleTrustMembership | Where-Object { $_.store -match '\\Root$' -and $_.certificatePresent }).Count -gt 0)
        signtoolPaTrustPassed = ($cycleTrustFailures.Count -eq 0)
        trustFailures = @($cycleTrustFailures | ForEach-Object { $_.name + ': ' + $_.catalogSignatureTrustMessage })
        interpretation = 'Catalog signing and signer thumbprint are recorded. Authenticode /pa is expected to report an untrusted self-signed root while the public certificate is absent from Root. No certificates were imported.'
    }
    liveChanges = [ordered]@{
        pnputilExecuted = $false
        netcfgExecuted = $false
        servicesChanged = $false
        certificateStoresChanged = $false
        bcdChanged = $false
        espOrFirmwareChanged = $false
        rebooted = $false
    }
    failures = $cycleFailures.ToArray()
    files = $cycleFileRows
} | ConvertTo-Json -Depth 14
[IO.File]::WriteAllText((Join-Path $cycleReportRoot 'packaging-manifest.json'), $cycleManifest, $cycleUtf8)

if ($cycleFailures.Count -ne 0) { throw ('Package construction failed: ' + ($cycleFailures -join '; ') + '; see ' + $cycleReportRoot) }
'PACKAGE FILES PREPARED. /pa trust status is recorded separately; no package was installed or bound.'
