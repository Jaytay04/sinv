[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Stage,
    [Parameter(Mandatory=$true)][string]$EvidenceRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$Installer = Join-Path $Stage 'Install-UEFILoaderGuestMailbox.ps1'
$Worker = Join-Path $Stage 'Invoke-UEFILoaderGuestInfo.ps1'
$ServiceSource = Join-Path $Stage 'UEFILoaderGuestMailboxService.cs'
$KeySource = Join-Path $Stage 'uefiloader-mailbox.key'
foreach ($Path in @($Installer, $Worker, $ServiceSource, $KeySource)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Bootstrap input is missing: $Path"
    }
}

& $Installer -InstallRoot 'C:\ProgramData\UEFILoaderRunner' `
    -TaskName 'UEFILoaderGuestMailbox'

$ServiceName = 'UEFILoaderGuestMailbox'
$ConfigPath = 'C:\ProgramData\UEFILoaderRunner\guest-runner.config.json'
$InstalledWorker = 'C:\ProgramData\UEFILoaderRunner\Invoke-UEFILoaderGuestInfo.ps1'
$InstalledService = 'C:\ProgramData\UEFILoaderRunner\UEFILoaderGuestMailboxService.exe'
$InstalledKey = 'C:\ProgramData\UEFILoaderRunner\mailbox.key'
if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $InstalledWorker -PathType Leaf) -or
    -not (Test-Path -LiteralPath $InstalledService -PathType Leaf) -or
    -not (Test-Path -LiteralPath $InstalledKey -PathType Leaf) -or
    (Get-Item -LiteralPath $InstalledKey).Length -ne 32) {
    throw 'The UEFILoader Guest Variables installation is incomplete.'
}

$Config = Get-Content -Raw -LiteralPath $ConfigPath | ConvertFrom-Json
if ([string]$Config.GuestInfoPrefix -cne 'guestinfo.uefiloader' -or
    [string]$Config.LocalRunRoot -cne 'C:\ProgramData\UEFILoaderRunner\runs' -or
    [string]$Config.KeyPath -cne 'C:\ProgramData\UEFILoaderRunner\mailbox.key') {
    throw 'The installed Guest Variables configuration is not canonical.'
}

$HostProbeName = 'guestinfo.uefiloader.bootstrap.host'
$HostProbeValue = 'SE9TVC1SRUFEWQ=='
$GuestProbeName = 'guestinfo.uefiloader.bootstrap.guest'
$GuestProbeValue = 'R1VFU1QtUkVBRFk='
$HostProbeActual = (& ([string]$Config.VmtoolsdPath) --cmd `
    "info-get $HostProbeName" 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $HostProbeActual -cne $HostProbeValue) {
    throw "Guest vmtoolsd could not read the host Guest Variable: $HostProbeActual"
}
& ([string]$Config.VmtoolsdPath) --cmd `
    "info-set $GuestProbeName $GuestProbeValue" | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'Guest vmtoolsd could not publish the guest Guest Variable probe.'
}
Start-Sleep -Seconds 2
$Service = Get-CimInstance Win32_Service -Filter "Name='$ServiceName'"
$ServiceController = Get-Service -Name $ServiceName -ErrorAction Stop
$Dependencies = @($ServiceController.ServicesDependedOn.Name)
if ($null -eq $Service -or
    [string]$Service.StartName -notin @('LocalSystem', '.\LocalSystem') -or
    [string]$Service.StartMode -cne 'Auto' -or
    [string]$Service.State -cne 'Running' -or
    $Dependencies -notcontains 'VMTools') {
    throw 'The UEFILoader Guest Variables service is not running automatically as LocalSystem after VMware Tools.'
}

$WorkerReadyId = 'uefi-windows-probe-' +
    [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' +
    [Guid]::NewGuid().ToString('N').Substring(0, 10)
$WorkerRequest = [ordered]@{
    SchemaVersion = 0
    RunId = $WorkerReadyId
    Action = 'Probe'
    ExpectedProbeVerdict = 'NOT_MET'
    PackageSha256 = ('0' * 64)
    PackageSize = 0
    PackageChunkSize = 9000
    PackageChunkCount = 0
    TimeoutSeconds = 30
    SubmittedUtc = [DateTime]::UtcNow.ToString('o')
    RequestHmacSha256 = ('0' * 64)
}
$WorkerRequestBytes = [Text.Encoding]::UTF8.GetBytes(
    ($WorkerRequest | ConvertTo-Json -Compress -Depth 4))
try {
    $WorkerRequestManifest = [Convert]::ToBase64String($WorkerRequestBytes)
}
finally { [Array]::Clear($WorkerRequestBytes, 0, $WorkerRequestBytes.Length) }
& ([string]$Config.VmtoolsdPath) --cmd `
    'info-set guestinfo.uefiloader.response.manifest MA==' | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'Could not tombstone the worker response manifest.'
}
& ([string]$Config.VmtoolsdPath) --cmd `
    "info-set guestinfo.uefiloader.request.manifest $WorkerRequestManifest" |
    Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'Could not publish the worker readiness request.'
}
$WorkerDeadline = [DateTime]::UtcNow.AddSeconds(20)
$WorkerResponseManifest = $null
do {
    Start-Sleep -Milliseconds 500
    $WorkerResponseManifest = (& ([string]$Config.VmtoolsdPath) --cmd `
        'info-get guestinfo.uefiloader.response.manifest' 2>&1 |
        Out-String).Trim()
} while (($WorkerResponseManifest -ceq 'MA==' -or
        [string]::IsNullOrWhiteSpace($WorkerResponseManifest)) -and
    [DateTime]::UtcNow -lt $WorkerDeadline)
if ($WorkerResponseManifest -ceq 'MA==' -or
    [string]::IsNullOrWhiteSpace($WorkerResponseManifest)) {
    $Service = Get-CimInstance Win32_Service -Filter "Name='$ServiceName'"
    $WorkerStatusPath = 'C:\ProgramData\UEFILoaderRunner\worker-status.json'
    $WorkerStatus = if (Test-Path -LiteralPath $WorkerStatusPath -PathType Leaf) {
        (Get-Content -Raw -LiteralPath $WorkerStatusPath).Trim()
    } else { 'ABSENT' }
    throw "SYSTEM worker did not answer its local readiness request; ServiceState=$($Service.State); ServiceExitCode=$($Service.ExitCode); WorkerStatus=$WorkerStatus"
}
try {
    $WorkerResponseBytes = [Convert]::FromBase64String($WorkerResponseManifest)
    try {
        $WorkerResponse = [Text.Encoding]::UTF8.GetString(
            $WorkerResponseBytes) | ConvertFrom-Json
    }
    finally {
        [Array]::Clear($WorkerResponseBytes, 0, $WorkerResponseBytes.Length)
    }
}
catch { throw 'SYSTEM worker readiness response is not valid base64 JSON.' }
if ([string]$WorkerResponse.RunId -cne $WorkerReadyId -or
    [string]$WorkerResponse.Status -cne 'FAIL') {
    throw 'SYSTEM worker readiness response has the wrong identity or status.'
}
$ReadyIdBytes = [Text.Encoding]::UTF8.GetBytes($WorkerReadyId)
try { $ReadyIdValue = [Convert]::ToBase64String($ReadyIdBytes) }
finally { [Array]::Clear($ReadyIdBytes, 0, $ReadyIdBytes.Length) }
& ([string]$Config.VmtoolsdPath) --cmd `
    "info-set guestinfo.uefiloader.bootstrap.readyid $ReadyIdValue" | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'Could not publish the authenticated worker readiness identity.'
}

$VmTools = Get-Service -Name 'VMTools' -ErrorAction Stop
if ([string]$VmTools.Status -cne 'Running') {
    throw 'VMware Tools is not running after Guest Variables installation.'
}

$LegacyTask = Get-ScheduledTask -TaskName 'HypersinvGuestMailbox' `
    -ErrorAction SilentlyContinue
if ($null -ne $LegacyTask) {
    Disable-ScheduledTask -TaskName 'HypersinvGuestMailbox' | Out-Null
    $LegacyTask = Get-ScheduledTask -TaskName 'HypersinvGuestMailbox'
    if ([bool]$LegacyTask.Settings.Enabled) {
        throw 'The legacy shared-mailbox task could not be disabled.'
    }
}

& "$env:SystemRoot\System32\shutdown.exe" /s /t 30 /d p:4:1 /c `
    'UEFILoader Guest Variables clean snapshot preparation' | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to schedule the clean Windows shutdown.'
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$StatusPath = Join-Path $EvidenceRoot 'uefiloader-guestinfo-bootstrap.txt'
@(
    'Result=PASS'
    "ServiceName=$($Service.Name)"
    "ServiceState=$($Service.State)"
    "ServiceStartMode=$($Service.StartMode)"
    "ServicePrincipal=$($Service.StartName)"
    "ServiceDependsOn=$($Dependencies -join ',')"
    "GuestInfoPrefix=$($Config.GuestInfoPrefix)"
    "VmToolsStatus=$($VmTools.Status)"
    'HostToGuestVariable=True'
    'GuestToHostVariable=True'
    "WorkerReadyId=$WorkerReadyId"
    'GracefulShutdownScheduled=True'
    "LegacyTaskDisabled=$($null -eq $LegacyTask -or -not [bool]$LegacyTask.Settings.Enabled)"
    "WorkerSha256=$((Get-FileHash -LiteralPath $InstalledWorker -Algorithm SHA256).Hash)"
    "ServiceSha256=$((Get-FileHash -LiteralPath $InstalledService -Algorithm SHA256).Hash)"
) | Set-Content -LiteralPath $StatusPath -Encoding ascii

$ArchiveTemporary = Join-Path $Stage (
    'uefiloader-guestinfo-bootstrap-' + [Guid]::NewGuid().ToString('N') + '.zip')
[IO.Compression.ZipFile]::CreateFromDirectory(
    $EvidenceRoot, $ArchiveTemporary,
    [IO.Compression.CompressionLevel]::Optimal, $false)
$Archive = Join-Path $EvidenceRoot 'uefiloader-guestinfo-bootstrap.zip'
Move-Item -LiteralPath $ArchiveTemporary -Destination $Archive
Write-Host 'PASS: passwordless UEFILoader Guest Variables service bootstrapped; clean shutdown scheduled.'
