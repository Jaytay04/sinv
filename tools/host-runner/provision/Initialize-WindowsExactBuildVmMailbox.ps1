[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$VmxPath,

    [Parameter(Mandatory = $true)]
    [string]$GuestCredentialPath,

    [Parameter(Mandatory = $true)]
    [string]$MailboxCredentialPath,

    [string]$SourceSnapshotName = 'UEFILoader-Win11-25H2-Clean',

    [string]$SnapshotName = 'UEFILoader-Win11-25H2-Mailbox',

    [string]$VmrunPath =
        'C:\Program Files\VMware\VMware Workstation\vmrun.exe',

    [string]$LeaseScript =
        'C:\Users\jayli\OneDrive\Documents\Projects\AgentSharedMailbox\Invoke-AgentVmLease.ps1',

    [string]$LeaseResource = 'vm-uefiloader-windows-exactbuild',

    [string]$ProjectRoot = '',

    [switch]$EnableSoftwareTpm,

    [switch]$AuthorizeLiveVmRun,
    [switch]$AuthorizeVmStop,
    [switch]$AuthorizeSnapshotRevert,
    [switch]$AuthorizeSnapshotCreate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $AuthorizeLiveVmRun -or -not $AuthorizeVmStop -or
    -not $AuthorizeSnapshotRevert -or -not $AuthorizeSnapshotCreate) {
    throw 'Mailbox bootstrap requires live-run, VM-stop, snapshot-revert, and snapshot-create authorization.'
}
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
}
$GuestSourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\guest'))
$SourceFiles = @(
    'Install-UEFILoaderGuestMailbox.ps1',
    'Invoke-UEFILoaderGuestInfo.ps1',
    'UEFILoaderGuestMailboxService.cs')
foreach ($Path in @($VmxPath, $GuestCredentialPath,
        $MailboxCredentialPath, $VmrunPath, $LeaseScript)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required mailbox-bootstrap input is missing: $Path"
    }
}
foreach ($Name in $SourceFiles) {
    $Path = Join-Path $GuestSourceRoot $Name
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required mailbox-bootstrap source is missing: $Path"
    }
}

$ResolvedVmx = [IO.Path]::GetFullPath($VmxPath)
$RunId = 'windows-mailbox-bootstrap-' +
    [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' +
    ([Guid]::NewGuid().ToString('N').Substring(0, 10))
$EvidenceRoot = Join-Path $ProjectRoot ".analysis\host-runner\runs\$RunId"
$TemporaryRoot = Join-Path $ProjectRoot '.analysis\host-runner\temporary'
$TemporaryKey = Join-Path $TemporaryRoot "$RunId-mailbox.key"
$GuestStage = 'C:\ProgramData\UEFILoaderBootstrap'
$GuestStatus = "$GuestStage\bootstrap-status.json"
$GuestUser = 'UEFILoaderVm'
$HostProbeHostName = 'uefiloader.bootstrap.host'
$HostProbeGuestName = 'guestinfo.uefiloader.bootstrap.host'
$HostProbeValue = 'SE9TVC1SRUFEWQ=='
$GuestProbeHostName = 'uefiloader.bootstrap.guest'
$GuestProbeGuestName = 'guestinfo.uefiloader.bootstrap.guest'
$GuestProbeValue = 'R1VFU1QtUkVBRFk='

$GuestCredential = Import-Clixml -LiteralPath $GuestCredentialPath
if ($GuestCredential -isnot [Management.Automation.PSCredential]) {
    throw 'The guest credential file is not a DPAPI-protected PSCredential.'
}
$GuestPassword = [string]$GuestCredential.GetNetworkCredential().Password

$MailboxCredential = Import-Clixml -LiteralPath $MailboxCredentialPath
if ($MailboxCredential -isnot [Management.Automation.PSCredential]) {
    throw 'The mailbox credential file is not a DPAPI-protected PSCredential.'
}
try {
    $MailboxKey = [Convert]::FromBase64String(
        $MailboxCredential.GetNetworkCredential().Password)
}
catch {
    throw 'The mailbox credential does not contain a valid base64 key.'
}
if ($MailboxKey.Length -ne 32) {
    [Array]::Clear($MailboxKey, 0, $MailboxKey.Length)
    throw 'The mailbox credential must contain exactly 32 key bytes.'
}

$Lease = $null
$StartupLease = $null
$Success = $false
$VmStopped = $false
$Reverted = $false

function Invoke-Vmrun {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$AllowFailure
    )
    $Output = @(& $VmrunPath -T ws @Arguments 2>&1)
    $ExitCode = $LASTEXITCODE
    if (-not $AllowFailure -and $ExitCode -ne 0) {
        throw "vmrun failed ($ExitCode): $([string]($Output -join ' '))"
    }
    return [pscustomobject]@{ ExitCode = $ExitCode; Output = $Output }
}

function Invoke-GuestProgram {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $Operation = @(
        '-gu', $GuestUser, '-gp', $GuestPassword,
        'runProgramInGuest', $ResolvedVmx) + $Arguments
    return Invoke-Vmrun -Arguments $Operation
}

function Test-VmRunning {
    $Listing = Invoke-Vmrun -Arguments @('list')
    return $null -ne ($Listing.Output | Where-Object {
            [string]$_ -ceq $ResolvedVmx } | Select-Object -First 1)
}

function Stop-ExactVm {
    if (-not (Test-VmRunning)) { $script:VmStopped = $true; return }
    try {
        [void](Invoke-GuestProgram -Arguments @(
                'C:\Windows\System32\shutdown.exe', '/s', '/t', '0', '/f',
                '/d', 'p:4:1', '/c', 'UEFILoader mailbox bootstrap complete.'))
    }
    catch { }
    $Deadline = [DateTime]::UtcNow.AddMinutes(3)
    while ([DateTime]::UtcNow -lt $Deadline) {
        if (-not (Test-VmRunning)) { $script:VmStopped = $true; return }
        Start-Sleep -Seconds 2
    }
    [void](Invoke-Vmrun -Arguments @('stop', $ResolvedVmx, 'soft') -AllowFailure)
    $Deadline = [DateTime]::UtcNow.AddMinutes(2)
    while ([DateTime]::UtcNow -lt $Deadline) {
        if (-not (Test-VmRunning)) { $script:VmStopped = $true; return }
        Start-Sleep -Seconds 2
    }
    [void](Invoke-Vmrun -Arguments @('stop', $ResolvedVmx, 'hard'))
    if (Test-VmRunning) { throw 'The mailbox-bootstrap VM remained running after hard stop.' }
    $script:VmStopped = $true
}

function Wait-GuestOperations {
    $Deadline = [DateTime]::UtcNow.AddMinutes(5)
    while ([DateTime]::UtcNow -lt $Deadline) {
        if (-not (Test-VmRunning)) {
            throw 'The mailbox-bootstrap VM stopped before VMware Tools became ready.'
        }
        $Probe = Invoke-Vmrun -Arguments @(
            '-gu', $GuestUser, '-gp', $GuestPassword,
            'listProcessesInGuest', $ResolvedVmx) -AllowFailure
        if ($Probe.ExitCode -eq 0) { return }
        Start-Sleep -Seconds 5
    }
    throw 'VMware Tools guest operations did not become ready.'
}

try {
    if (Test-VmRunning) { throw 'The mailbox-bootstrap VM is already running.' }
    $Running = (Invoke-Vmrun -Arguments @('list')).Output |
        Select-Object -Skip 1 |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    if (@($Running).Count -ne 0) {
        throw 'Mailbox bootstrap requires no other running VMware VM.'
    }
    $Drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($ResolvedVmx))
    if ($Drive.AvailableFreeSpace -lt 20GB) {
        throw 'Host free space is below the 20 GiB VM-test floor.'
    }

    $Snapshots = (Invoke-Vmrun -Arguments @('listSnapshots', $ResolvedVmx)).Output
    if ($Snapshots -notcontains $SourceSnapshotName) {
        throw "The source snapshot does not exist: $SourceSnapshotName"
    }
    if ($Snapshots -contains $SnapshotName) {
        throw "The target snapshot already exists: $SnapshotName"
    }

    $Lease = & $LeaseScript -Action Acquire -Resource $LeaseResource `
        -Agent 'Codex-UEFILoaderMailboxProvisioner' -Project $ProjectRoot `
        -RunId $RunId -LeaseMinutes 180
    [void](Invoke-Vmrun -Arguments @(
            'revertToSnapshot', $ResolvedVmx, $SourceSnapshotName))
    $Reverted = $true

    if ($EnableSoftwareTpm) {
        $VmxText = [IO.File]::ReadAllText($ResolvedVmx)
        $TpmPresent = [regex]::IsMatch(
            $VmxText, '(?im)^vtpm\.present\s*=\s*"TRUE"\s*$')
        if (-not $TpmPresent) {
            $AutoTpm = [regex]::Matches(
                $VmxText, '(?im)^managedVM\.autoAddVTPM\s*=.*$')
            if ($AutoTpm.Count -gt 1 -or
                ($AutoTpm.Count -eq 1 -and
                 $AutoTpm[0].Value -cnotmatch '=\s*"software"\s*$')) {
                throw 'The disposable VM has an ambiguous software-vTPM configuration.'
            }
            if ($AutoTpm.Count -eq 0) {
                $VmxText = $VmxText.TrimEnd("`r", "`n") +
                    "`r`nmanagedVM.autoAddVTPM = `"software`"`r`n"
                $TemporaryVmx = $ResolvedVmx + '.software-tpm.tmp'
                try {
                    [IO.File]::WriteAllText($TemporaryVmx, $VmxText,
                        [Text.Encoding]::ASCII)
                    Move-Item -LiteralPath $TemporaryVmx `
                        -Destination $ResolvedVmx -Force
                }
                finally {
                    if (Test-Path -LiteralPath $TemporaryVmx -PathType Leaf) {
                        Remove-Item -LiteralPath $TemporaryVmx -Force
                    }
                }
            }
        }
    }

    $StartupLease = & $LeaseScript -Action Acquire -Resource 'host-vm-startup' `
        -Agent 'Codex-UEFILoaderMailboxProvisioner' -Project $ProjectRoot `
        -RunId $RunId -LeaseMinutes 10
    try {
        [void](Invoke-Vmrun -Arguments @('start', $ResolvedVmx, 'nogui'))
    }
    finally {
        if ($null -ne $StartupLease) {
            & $LeaseScript -Action Release -Resource 'host-vm-startup' `
                -LeaseToken $StartupLease.LeaseToken | Out-Null
            $StartupLease = $null
        }
    }
    Wait-GuestOperations
    [void](Invoke-Vmrun -Arguments @(
            'writeVariable', $ResolvedVmx, 'guestVar',
            $HostProbeHostName, $HostProbeValue))

    [void](Invoke-GuestProgram -Arguments @(
            'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
            '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
            '-Command',
            "New-Item -ItemType Directory -Force -Path '$GuestStage' | Out-Null"))

    New-Item -ItemType Directory -Force -Path $TemporaryRoot | Out-Null
    [IO.File]::WriteAllBytes($TemporaryKey, $MailboxKey)
    foreach ($Name in $SourceFiles) {
        [void](Invoke-Vmrun -Arguments @(
                '-gu', $GuestUser, '-gp', $GuestPassword,
                'copyFileFromHostToGuest', $ResolvedVmx,
                (Join-Path $GuestSourceRoot $Name), "$GuestStage\$Name"))
    }
    [void](Invoke-Vmrun -Arguments @(
            '-gu', $GuestUser, '-gp', $GuestPassword,
            'copyFileFromHostToGuest', $ResolvedVmx,
            $TemporaryKey, "$GuestStage\uefiloader-mailbox.key"))
    Remove-Item -LiteralPath $TemporaryKey -Force
    [Array]::Clear($MailboxKey, 0, $MailboxKey.Length)
    $MailboxKey = $null

    [void](Invoke-GuestProgram -Arguments @(
            'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
            '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
            '-File', "$GuestStage\Install-UEFILoaderGuestMailbox.ps1"))

    $VerifyScript = @"
`$config = Get-Content -LiteralPath 'C:\ProgramData\UEFILoaderRunner\guest-runner.config.json' -Raw | ConvertFrom-Json
`$service = Get-Service -Name 'UEFILoaderGuestMailbox' -ErrorAction Stop
`$serviceKey = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\UEFILoaderGuestMailbox'
`$dependencies = @(`$service.ServicesDependedOn.Name)
`$tpm = Get-Tpm -ErrorAction Stop
`$hostProbe = (& ([string]`$config.VmtoolsdPath) --cmd 'info-get $HostProbeGuestName' 2>&1 | Out-String).Trim()
if (`$LASTEXITCODE -ne 0) { exit 10 }
& ([string]`$config.VmtoolsdPath) --cmd 'info-set $GuestProbeGuestName $GuestProbeValue' | Out-Null
if (`$LASTEXITCODE -ne 0) { exit 11 }
`$currentVersion = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
[ordered]@{
    SchemaVersion = 1
    CurrentBuild = [string]`$currentVersion.CurrentBuild
    UBR = [uint64]`$currentVersion.UBR
    ServiceState = [string]`$service.Status
    ServiceStart = [int]`$serviceKey.Start
    ServicePrincipal = [string]`$serviceKey.ObjectName
    ServiceDependsOn = [string[]]`$dependencies
    HostProbe = `$hostProbe
    TpmPresent = [bool]`$tpm.TpmPresent
    TpmReady = [bool]`$tpm.TpmReady
} | ConvertTo-Json | Set-Content -LiteralPath '$GuestStatus' -Encoding ascii
"@
    [void](Invoke-GuestProgram -Arguments @(
            'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
            '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
            '-Command', $VerifyScript))

    New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
    $HostStatus = Join-Path $EvidenceRoot 'bootstrap-status.json'
    [void](Invoke-Vmrun -Arguments @(
            '-gu', $GuestUser, '-gp', $GuestPassword,
            'copyFileFromGuestToHost', $ResolvedVmx,
            $GuestStatus, $HostStatus))
    $Status = Get-Content -LiteralPath $HostStatus -Raw | ConvertFrom-Json
    $GuestProbe = (Invoke-Vmrun -Arguments @(
            'readVariable', $ResolvedVmx, 'guestVar', $GuestProbeHostName)).Output
    $GuestProbeText = ([string]($GuestProbe -join "`n")).Trim()
    if ([int]$Status.SchemaVersion -ne 1 -or
        [uint64]$Status.CurrentBuild -ne 26200 -or
        [string]$Status.ServiceState -cne 'Running' -or
        [int]$Status.ServiceStart -ne 2 -or
        [string]$Status.ServicePrincipal -notin @('LocalSystem', '.\LocalSystem') -or
        @($Status.ServiceDependsOn) -notcontains 'VMTools' -or
        [string]$Status.HostProbe -cne $HostProbeValue -or
        $GuestProbeText -cne $GuestProbeValue -or
        ($EnableSoftwareTpm -and -not [bool]$Status.TpmPresent)) {
        throw ("The passwordless GuestInfo mailbox verification failed: " +
            "build=$($Status.CurrentBuild).$($Status.UBR); " +
            "state=$($Status.ServiceState); start=$($Status.ServiceStart); " +
            "principal=$($Status.ServicePrincipal); " +
            "dependencies=$(@($Status.ServiceDependsOn) -join ','); " +
            "hostProbe=$($Status.HostProbe); guestProbe=$GuestProbeText; " +
            "tpmPresent=$($Status.TpmPresent); tpmReady=$($Status.TpmReady)")
    }

    $CleanupScript = "Remove-Item -LiteralPath '$GuestStage' -Recurse -Force"
    [void](Invoke-GuestProgram -Arguments @(
            'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
            '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
            '-Command', $CleanupScript))
    foreach ($Name in @($HostProbeHostName, $GuestProbeHostName)) {
        [void](Invoke-Vmrun -Arguments @(
                'writeVariable', $ResolvedVmx, 'guestVar', $Name, 'MA=='))
    }

    Stop-ExactVm
    [void](Invoke-Vmrun -Arguments @('snapshot', $ResolvedVmx, $SnapshotName))
    $Snapshots = (Invoke-Vmrun -Arguments @('listSnapshots', $ResolvedVmx)).Output
    if ($Snapshots -notcontains $SnapshotName) {
        throw 'The mailbox snapshot was not listed after creation.'
    }

    $Success = $true
    [pscustomobject]@{
        Result = 'PASS'
        RunId = $RunId
        VmxPath = $ResolvedVmx
        SourceSnapshotName = $SourceSnapshotName
        SnapshotName = $SnapshotName
        CurrentBuild = [uint64]$Status.CurrentBuild
        UBR = [uint64]$Status.UBR
        ServiceState = [string]$Status.ServiceState
        GuestInfoRoundTrip = $true
        TpmPresent = [bool]$Status.TpmPresent
        TpmReady = [bool]$Status.TpmReady
        Evidence = $HostStatus
    }
}
finally {
    if ($null -ne $StartupLease) {
        try {
            & $LeaseScript -Action Release -Resource 'host-vm-startup' `
                -LeaseToken $StartupLease.LeaseToken | Out-Null
        }
        catch { }
    }
    if (Test-Path -LiteralPath $TemporaryKey -PathType Leaf) {
        try { Remove-Item -LiteralPath $TemporaryKey -Force } catch { }
    }
    if ($null -ne $MailboxKey) {
        [Array]::Clear($MailboxKey, 0, $MailboxKey.Length)
    }
    if (-not $Success) {
        try { Stop-ExactVm } catch { }
        if ($VmStopped -and $Reverted) {
            try {
                [void](Invoke-Vmrun -Arguments @(
                        'revertToSnapshot', $ResolvedVmx, $SourceSnapshotName))
            }
            catch { }
        }
    }
    if ($null -ne $Lease -and ($Success -or $VmStopped)) {
        try {
            & $LeaseScript -Action Release -Resource $LeaseResource `
                -LeaseToken $Lease.LeaseToken | Out-Null
        }
        catch { }
    }
    if (-not [string]::IsNullOrEmpty($GuestPassword)) {
        $GuestPassword = ('0' * $GuestPassword.Length)
    }
}
