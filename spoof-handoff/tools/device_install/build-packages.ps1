[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$devicePackageSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$deviceWorktree = [IO.Path]::GetFullPath((Join-Path $devicePackageSource '..'))
$deviceRepository = [IO.Path]::GetFullPath((Join-Path $deviceWorktree '..\..\..'))
$devicePackageRoot = Join-Path $deviceRepository 'spoof-handoff'
$deviceOutputRoot = Join-Path $devicePackageRoot 'artifacts\device-runtime-package-20261016'
$deviceReportRoot = Join-Path $devicePackageRoot 'reports\device-runtime-package-20261016\packaging'
$deviceInputRoot = Join-Path $devicePackageRoot 'artifacts\device-filters-20261014\signed-final\enabled'
$deviceCertificate = Join-Path $deviceRepository 'components\hypervisor\hypersinv.cer'
$deviceCertificateThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
$deviceCertificateSha256 = 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'
$deviceStorageSha256 = '6E862E7EF6D360CC7AD021AF18162431856E9CB680E306E08DE60450A6D06302'
$deviceNicSha256 = '3B57CAC5EA1EF966052BA287C2EB2D188C6F26C90FD5C7A2314B52BC13F20E15'
$deviceToolRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.28000.0'
$deviceInf2Cat = Join-Path $deviceToolRoot 'x86\Inf2Cat.exe'
$deviceInfVerif = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Tools\10.0.28000.0\x64\InfVerif.exe'
$deviceSignTool = Join-Path $deviceToolRoot 'x86\signtool.exe'
$deviceUtf8 = [Text.UTF8Encoding]::new($false)
$deviceFailures = New-Object 'System.Collections.Generic.List[string]'
$deviceCommandResults = New-Object 'System.Collections.Generic.List[object]'

foreach ($devicePath in @($deviceOutputRoot, $deviceReportRoot)) {
    if (-not $devicePath.StartsWith($devicePackageRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'All package and report outputs must remain under spoof-handoff.'
    }
}
if (Test-Path -LiteralPath $deviceOutputRoot) {
    throw ('Refusing to overwrite existing package output: ' + $deviceOutputRoot)
}
if (Test-Path -LiteralPath $deviceReportRoot -PathType Leaf) {
    throw ('The package report path is not a directory: ' + $deviceReportRoot)
}
if (Test-Path -LiteralPath $deviceReportRoot -PathType Container) {
    $deviceExistingReportFiles = @(Get-ChildItem -LiteralPath $deviceReportRoot -File -Force)
    if ($deviceExistingReportFiles.Count -ne 0) {
        throw ('Refusing to overwrite existing report files: ' + $deviceReportRoot)
    }
}

foreach ($deviceTool in @($deviceInf2Cat, $deviceInfVerif, $deviceSignTool)) {
    if (-not (Test-Path -LiteralPath $deviceTool -PathType Leaf)) {
        throw ('Required Windows Kit tool is unavailable: ' + $deviceTool)
    }
}

if ((Get-FileHash -LiteralPath $deviceCertificate -Algorithm SHA256).Hash -cne $deviceCertificateSha256) {
    throw 'The existing public signing certificate hash does not match its pinned value.'
}
$deviceCertificateObject = [Security.Cryptography.X509Certificates.X509Certificate2]::new($deviceCertificate)
if ($deviceCertificateObject.Thumbprint -cne $deviceCertificateThumbprint) {
    throw 'The existing public signing certificate thumbprint does not match its pinned value.'
}
$devicePrivateCertificate = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $deviceCertificateThumbprint) -ErrorAction Stop
if (-not $devicePrivateCertificate.HasPrivateKey) {
    throw 'The existing CurrentUser signing certificate has no accessible private key.'
}

New-Item -ItemType Directory -Path $deviceOutputRoot | Out-Null
if (-not (Test-Path -LiteralPath $deviceReportRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $deviceReportRoot | Out-Null
}

function Invoke-DeviceTool {
    param(
        [Parameter(Mandatory = $true)][string]$ToolPath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$EvidenceName,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    $deviceStartInfo = [Diagnostics.ProcessStartInfo]::new()
    $deviceStartInfo.FileName = $ToolPath
    $deviceStartInfo.WorkingDirectory = $WorkingDirectory
    $deviceStartInfo.UseShellExecute = $false
    $deviceStartInfo.CreateNoWindow = $true
    $deviceStartInfo.RedirectStandardOutput = $true
    $deviceStartInfo.RedirectStandardError = $true
    $deviceStartInfo.Arguments = ($Arguments -join ' ')

    $deviceProcess = [Diagnostics.Process]::new()
    $deviceProcess.StartInfo = $deviceStartInfo
    if (-not $deviceProcess.Start()) {
        throw ('Could not start tool: ' + $ToolPath)
    }
    $deviceStdoutTask = $deviceProcess.StandardOutput.ReadToEndAsync()
    $deviceStderrTask = $deviceProcess.StandardError.ReadToEndAsync()
    $deviceProcess.WaitForExit()
    $deviceStdout = $deviceStdoutTask.GetAwaiter().GetResult()
    $deviceStderr = $deviceStderrTask.GetAwaiter().GetResult()
    $deviceExitCode = $deviceProcess.ExitCode
    $deviceProcess.Dispose()

    [IO.File]::WriteAllText((Join-Path $deviceReportRoot ($EvidenceName + '.stdout.txt')), $deviceStdout, $deviceUtf8)
    [IO.File]::WriteAllText((Join-Path $deviceReportRoot ($EvidenceName + '.stderr.txt')), $deviceStderr, $deviceUtf8)
    [IO.File]::WriteAllText((Join-Path $deviceReportRoot ($EvidenceName + '.exit.txt')), ($deviceExitCode.ToString() + "`n"), $deviceUtf8)
    $deviceCommandResults.Add([pscustomobject]@{
        name = $EvidenceName
        executable = $ToolPath
        arguments = $Arguments
        workingDirectory = $WorkingDirectory
        stdoutFile = ($EvidenceName + '.stdout.txt')
        stderrFile = ($EvidenceName + '.stderr.txt')
        exitCodeFile = ($EvidenceName + '.exit.txt')
        exitCode = $deviceExitCode
        stdout = $deviceStdout
        stderr = $deviceStderr
    })
    return $deviceExitCode
}

$devicePackages = @(
    [pscustomobject]@{
        name = 'storage'
        infSource = Join-Path $PSScriptRoot 'storage\client_storage_filter.inf'
        imageName = 'client_storage_filter.sys'
        expectedImageSha256 = $deviceStorageSha256
    },
    [pscustomobject]@{
        name = 'nic'
        infSource = Join-Path $PSScriptRoot 'nic\client_nic_filter.inf'
        imageName = 'client_nic_filter.sys'
        expectedImageSha256 = $deviceNicSha256
    }
)
$devicePackageResults = New-Object 'System.Collections.Generic.List[object]'

foreach ($devicePackage in $devicePackages) {
    $devicePackageDirectory = Join-Path $deviceOutputRoot $devicePackage.name
    New-Item -ItemType Directory -Path $devicePackageDirectory | Out-Null
    $deviceInfName = 'client_' + $devicePackage.name + '_filter.inf'
    $deviceInfPath = Join-Path $devicePackageDirectory $deviceInfName
    $deviceImagePath = Join-Path $devicePackageDirectory $devicePackage.imageName
    $deviceCatalogPath = Join-Path $devicePackageDirectory ('client_' + $devicePackage.name + '_filter.cat')
    Copy-Item -LiteralPath $devicePackage.infSource -Destination $deviceInfPath

    $deviceSourceImagePath = Join-Path $deviceInputRoot $devicePackage.imageName
    $deviceSourceImageSha = (Get-FileHash -LiteralPath $deviceSourceImagePath -Algorithm SHA256).Hash
    if ($deviceSourceImageSha -cne $devicePackage.expectedImageSha256) {
        $deviceFailures.Add($devicePackage.name + ': source SYS hash mismatch; expected ' + $devicePackage.expectedImageSha256 + ', actual ' + $deviceSourceImageSha)
        continue
    }
    Copy-Item -LiteralPath $deviceSourceImagePath -Destination $deviceImagePath
    $devicePackageImageSha = (Get-FileHash -LiteralPath $deviceImagePath -Algorithm SHA256).Hash
    if ($devicePackageImageSha -cne $devicePackage.expectedImageSha256) {
        $deviceFailures.Add($devicePackage.name + ': copied SYS hash mismatch; expected ' + $devicePackage.expectedImageSha256 + ', actual ' + $devicePackageImageSha)
    }

    $deviceInfVerifExit = Invoke-DeviceTool -ToolPath $deviceInfVerif -Arguments @('/w', '/v', '/wbuild', '10.0.18362', $deviceInfPath) -EvidenceName ($devicePackage.name + '-infverif') -WorkingDirectory $devicePackageDirectory
    if ($deviceInfVerifExit -ne 0) {
        $deviceFailures.Add($devicePackage.name + ': InfVerif /w exited ' + $deviceInfVerifExit)
    }

    $deviceInf2CatExit = Invoke-DeviceTool -ToolPath $deviceInf2Cat -Arguments @('/driver:' + $devicePackageDirectory, '/os:10_GE_X64', '/verbose') -EvidenceName ($devicePackage.name + '-inf2cat') -WorkingDirectory $devicePackageDirectory
    if ($deviceInf2CatExit -ne 0) {
        $deviceFailures.Add($devicePackage.name + ': Inf2Cat exited ' + $deviceInf2CatExit)
        continue
    }
    if (-not (Test-Path -LiteralPath $deviceCatalogPath -PathType Leaf)) {
        $deviceFailures.Add($devicePackage.name + ': Inf2Cat returned zero without producing the declared catalog.')
        continue
    }

    $deviceSignExit = Invoke-DeviceTool -ToolPath $deviceSignTool -Arguments @('sign', '/v', '/fd', 'SHA256', '/s', 'My', '/sha1', $deviceCertificateThumbprint, $deviceCatalogPath) -EvidenceName ($devicePackage.name + '-signtool-sign-cat') -WorkingDirectory $devicePackageDirectory
    if ($deviceSignExit -ne 0) {
        $deviceFailures.Add($devicePackage.name + ': signtool sign exited ' + $deviceSignExit)
        continue
    }

    $deviceVerifyCatExit = Invoke-DeviceTool -ToolPath $deviceSignTool -Arguments @('verify', '/pa', '/v', $deviceCatalogPath) -EvidenceName ($devicePackage.name + '-signtool-verify-cat-pa') -WorkingDirectory $devicePackageDirectory
    if ($deviceVerifyCatExit -ne 0) {
        $deviceFailures.Add($devicePackage.name + ': signtool verify catalog /pa exited ' + $deviceVerifyCatExit)
    }
    $deviceVerifyMemberExit = Invoke-DeviceTool -ToolPath $deviceSignTool -Arguments @('verify', '/pa', '/v', '/c', $deviceCatalogPath, $deviceImagePath) -EvidenceName ($devicePackage.name + '-signtool-verify-member-pa') -WorkingDirectory $devicePackageDirectory
    if ($deviceVerifyMemberExit -ne 0) {
        $deviceFailures.Add($devicePackage.name + ': signtool catalog-member verify /pa exited ' + $deviceVerifyMemberExit)
    }

    $devicePackageResults.Add([pscustomobject]@{
        name = $devicePackage.name
        inf = $deviceInfName
        infSha256 = (Get-FileHash -LiteralPath $deviceInfPath -Algorithm SHA256).Hash
        catalog = (Split-Path $deviceCatalogPath -Leaf)
        catalogSha256 = (Get-FileHash -LiteralPath $deviceCatalogPath -Algorithm SHA256).Hash
        driver = $devicePackage.imageName
        driverSha256 = $devicePackageImageSha
        infVerifExitCode = $deviceInfVerifExit
        inf2CatExitCode = $deviceInf2CatExit
        signExitCode = $deviceSignExit
        catalogVerifyPaExitCode = $deviceVerifyCatExit
        memberVerifyPaExitCode = $deviceVerifyMemberExit
        pnputilStageCheckPerformed = $false
        driverInstalledOrBound = $false
    })
}

$deviceFiles = @()
foreach ($deviceFile in Get-ChildItem -LiteralPath $deviceOutputRoot -File -Recurse) {
    $deviceFiles += [pscustomobject]@{
        path = $deviceFile.FullName.Substring($deviceRepository.Length + 1).Replace('\', '/')
        length = $deviceFile.Length
        sha256 = (Get-FileHash -LiteralPath $deviceFile.FullName -Algorithm SHA256).Hash
    }
}
$deviceResultValue = if ($deviceFailures.Count -eq 0) { 'PASS' } else { 'FAIL' }
$deviceManifest = [ordered]@{
    schema = 'device-runtime-package-20261016-v1'
    capturedUtc = [DateTime]::UtcNow.ToString('o')
    result = $deviceResultValue
    osTarget = '10_GE_X64 (Windows 11 24H2+ x64 catalog target)'
    certificate = [ordered]@{
        thumbprint = $deviceCertificateThumbprint
        derSha256 = $deviceCertificateSha256
        source = 'Existing CurrentUser\\My certificate and private key; no store changes.'
    }
    sourceWorktree = $deviceWorktree
    packages = $devicePackageResults.ToArray()
    commands = $deviceCommandResults.ToArray()
    files = $deviceFiles
    failures = $deviceFailures.ToArray()
    pnputilStageCheck = [ordered]@{
        performed = $false
        reason = 'Not run: this packaging step is restricted to repository-local outputs; Driver Store staging changes live host state.'
    }
    liveInstallOrBinding = $false
} | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText((Join-Path $deviceReportRoot 'packaging-manifest.json'), $deviceManifest, $deviceUtf8)

if ($deviceFailures.Count -ne 0) {
    throw ('Package build/verification failed: ' + ($deviceFailures -join '; ') + '; see ' + $deviceReportRoot)
}

'PACKAGE INF VALIDATION, CATALOG GENERATION, SIGNING, AND STATIC VERIFICATION PASS. No install or binding performed.'
