[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$VmxPath,

    [Parameter(Mandatory = $true)]
    [string]$CredentialPath,

    [string]$SnapshotName = 'UEFILoader-Win11-25H2-Clean',

    [string]$VmrunPath =
        'C:\Program Files\VMware\VMware Workstation\vmrun.exe',

    [string]$LeaseScript =
        'C:\Users\jayli\OneDrive\Documents\Projects\AgentSharedMailbox\Invoke-AgentVmLease.ps1',

    [string]$LeaseResource = 'vm-uefiloader-windows-exactbuild',

    [string]$ProjectRoot = '',

    [switch]$AuthorizeLiveVmRun,
    [switch]$AuthorizeVmStop,
    [switch]$AuthorizeSnapshotCreate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $AuthorizeLiveVmRun -or -not $AuthorizeVmStop -or
    -not $AuthorizeSnapshotCreate) {
    throw 'VM installation requires live-run, VM-stop, and snapshot-create authorization.'
}
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
}
foreach ($Path in @($VmxPath, $CredentialPath, $VmrunPath, $LeaseScript)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required VM-installation input is missing: $Path"
    }
}
$ResolvedVmx = [IO.Path]::GetFullPath($VmxPath)
$VmRoot = Split-Path -Parent $ResolvedVmx
$SerialLog = Join-Path $VmRoot 'provision-serial.log'
$AnswerDisk = Join-Path $VmRoot 'autounattend.flp'
$ResultPath = Join-Path $VmRoot 'provision-result.json'
$RunId = 'windows-vm-provision-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') +
    '-' + ([Guid]::NewGuid().ToString('N').Substring(0, 10))
$Credential = Import-Clixml -LiteralPath $CredentialPath
if ($Credential -isnot [Management.Automation.PSCredential]) {
    throw 'The VM credential file is not a DPAPI-protected PSCredential.'
}
$NetworkCredential = $Credential.GetNetworkCredential()
$UserName = [string]$Credential.UserName
$PlainPassword = [string]$NetworkCredential.Password
$Lease = $null
$StartupLease = $null
$Success = $false
$VmStopped = $false

function Invoke-Vmrun {
    param([Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$AllowFailure)
    $Output = @(& $VmrunPath -T ws @Arguments 2>&1)
    $ExitCode = $LASTEXITCODE
    if (-not $AllowFailure -and $ExitCode -ne 0) {
        throw "vmrun failed ($ExitCode): $([string]($Output -join ' '))"
    }
    return [pscustomobject]@{ ExitCode = $ExitCode; Output = $Output }
}

function Test-VmRunning {
    $Listing = Invoke-Vmrun -Arguments @('list')
    foreach ($Line in $Listing.Output) {
        if ([string]$Line -ceq $ResolvedVmx) { return $true }
    }
    return $false
}

function Read-ExactBytes {
    param([IO.Stream]$Stream, [int]$Count)
    $Buffer = [byte[]]::new($Count)
    $Offset = 0
    while ($Offset -lt $Count) {
        $Read = $Stream.Read($Buffer, $Offset, $Count - $Offset)
        if ($Read -le 0) { throw 'The local VNC connection closed unexpectedly.' }
        $Offset += $Read
    }
    return ,$Buffer
}

function Get-BigEndianUInt32 {
    param([byte[]]$Bytes, [int]$Offset)
    return [uint32](
        ([uint32]$Bytes[$Offset] -shl 24) -bor
        ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 8) -bor
        [uint32]$Bytes[$Offset + 3])
}

function Send-VncBootKey {
    param([int]$Port = 5909)
    $Deadline = [DateTime]::UtcNow.AddSeconds(10)
    $Client = $null
    while ([DateTime]::UtcNow -lt $Deadline -and $null -eq $Client) {
        $Candidate = $null
        try {
            $Candidate = [Net.Sockets.TcpClient]::new()
            $Candidate.Connect('127.0.0.1', $Port)
            $Client = $Candidate
        }
        catch {
            if ($null -ne $Candidate) { $Candidate.Dispose() }
            Start-Sleep -Milliseconds 100
        }
    }
    if ($null -eq $Client) { throw 'The localhost-only VMware VNC endpoint did not open.' }
    try {
        $Stream = $Client.GetStream()
        $Version = [Text.Encoding]::ASCII.GetString((Read-ExactBytes $Stream 12))
        if ($Version -notmatch '^RFB 003\.(003|007|008)\n$') {
            throw "Unsupported VMware VNC protocol version: $Version"
        }
        $Reply = [Text.Encoding]::ASCII.GetBytes(
            $(if ($Version.Contains('003.003')) { 'RFB 003.003' + "`n" }
              else { 'RFB 003.008' + "`n" }))
        $Stream.Write($Reply, 0, $Reply.Length)
        if ($Version.Contains('003.003')) {
            $Security = Get-BigEndianUInt32 `
                -Bytes (Read-ExactBytes $Stream 4) -Offset 0
            if ($Security -ne 1) { throw 'VMware VNC did not offer no-auth localhost security.' }
        }
        else {
            $Count = (Read-ExactBytes $Stream 1)[0]
            if ($Count -eq 0) { throw 'VMware VNC rejected the security handshake.' }
            $Types = Read-ExactBytes $Stream $Count
            if ($Types -notcontains [byte]1) {
                throw 'VMware VNC did not offer no-auth localhost security.'
            }
            $Stream.WriteByte(1)
            $SecurityResult = Get-BigEndianUInt32 `
                -Bytes (Read-ExactBytes $Stream 4) -Offset 0
            if ($SecurityResult -ne 0) { throw 'VMware VNC security handshake failed.' }
        }
        $Stream.WriteByte(1)
        $ServerInit = Read-ExactBytes $Stream 24
        $NameLength = Get-BigEndianUInt32 $ServerInit 20
        if ($NameLength -gt 4096) { throw 'VMware VNC server name is unreasonably large.' }
        if ($NameLength -gt 0) { [void](Read-ExactBytes $Stream ([int]$NameLength)) }
        $Down = [byte[]](4, 1, 0, 0, 0, 0, 0, 32)
        $Up = [byte[]](4, 0, 0, 0, 0, 0, 0, 32)
        $KeyDeadline = [DateTime]::UtcNow.AddSeconds(8)
        while ([DateTime]::UtcNow -lt $KeyDeadline) {
            $Stream.Write($Down, 0, $Down.Length)
            $Stream.Write($Up, 0, $Up.Length)
            $Stream.Flush()
            Start-Sleep -Milliseconds 200
        }
    }
    finally { $Client.Dispose() }
}

function Wait-SerialMarker {
    param([string]$Marker, [int]$TimeoutMinutes)
    $Deadline = [DateTime]::UtcNow.AddMinutes($TimeoutMinutes)
    while ([DateTime]::UtcNow -lt $Deadline) {
        if (-not (Test-VmRunning)) {
            throw "The provisioning VM stopped before serial marker $Marker."
        }
        if (Test-Path -LiteralPath $SerialLog -PathType Leaf) {
            try {
                $RawText = Get-Content -LiteralPath $SerialLog -Raw -Encoding ascii
                $Text = if ($null -eq $RawText) { '' } else { [string]$RawText }
            }
            catch { $Text = '' }
            if ($Text -match 'SETUP_VMWARE_TOOLS_(MISSING|FAILED_[0-9]+)') {
                throw "Guest provisioning reported failure: $($Matches[0])"
            }
            if ($Text.Contains($Marker)) { return $Text }
        }
        Start-Sleep -Seconds 5
    }
    throw "Timed out waiting for provisioning serial marker $Marker."
}

function Stop-ExactVm {
    if (-not (Test-VmRunning)) { return }
    [void](Invoke-Vmrun -Arguments @('stop', $ResolvedVmx, 'soft') -AllowFailure)
    $Deadline = [DateTime]::UtcNow.AddMinutes(3)
    while ([DateTime]::UtcNow -lt $Deadline) {
        if (-not (Test-VmRunning)) { return }
        Start-Sleep -Seconds 2
    }
    [void](Invoke-Vmrun -Arguments @('stop', $ResolvedVmx, 'hard'))
    if (Test-VmRunning) { throw 'The provisioning VM remained running after hard stop.' }
}

function Set-VmxProvisionedState {
    $Keys = @(
        'sata0:1.present', 'sata0:1.deviceType', 'sata0:1.fileName',
        'floppy0.present', 'floppy0.fileType', 'floppy0.fileName',
        'floppy0.startConnected', 'ethernet0.present',
        'ethernet0.connectionType', 'ethernet0.startConnected',
        'serial0.present', 'serial0.fileType', 'serial0.fileName',
        'serial0.yieldOnMsrRead', 'RemoteDisplay.vnc.enabled',
        'RemoteDisplay.vnc.ip', 'RemoteDisplay.vnc.port', 'bios.bootOrder')
    $Patterns = @($Keys | ForEach-Object {
        '^\s*' + [Regex]::Escape($_) + '\s*='
    })
    $Lines = [Collections.Generic.List[string]]::new()
    foreach ($Line in (Get-Content -LiteralPath $ResolvedVmx)) {
        $Matched = $false
        foreach ($Pattern in $Patterns) {
            if ($Line -match $Pattern) { $Matched = $true; break }
        }
        if (-not $Matched) { $Lines.Add([string]$Line) }
    }
    $Lines.Add('sata0:1.present = "FALSE"')
    $Lines.Add('floppy0.present = "FALSE"')
    $Lines.Add('ethernet0.present = "FALSE"')
    $Lines.Add('serial0.present = "FALSE"')
    $Lines.Add('RemoteDisplay.vnc.enabled = "FALSE"')
    $Lines.Add('bios.bootOrder = "hdd"')
    [IO.File]::WriteAllLines($ResolvedVmx, $Lines, [Text.Encoding]::ASCII)
}

try {
    if (Test-VmRunning) { throw 'The provisioning VM is already running.' }
    $Running = (Invoke-Vmrun -Arguments @('list')).Output |
        Select-Object -Skip 1 | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    if (@($Running).Count -ne 0) {
        throw 'Fresh Windows provisioning requires no other running VMware VM.'
    }
    $Drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($ResolvedVmx))
    if ($Drive.AvailableFreeSpace -lt 20GB) {
        throw 'Host free space is below the 20 GiB VM-test floor.'
    }
    $Lease = & $LeaseScript -Action Acquire -Resource $LeaseResource `
        -Agent 'Codex-UEFILoaderWindowsProvisioner' -Project $ProjectRoot `
        -RunId $RunId -LeaseMinutes 180
    $StartupLease = & $LeaseScript -Action Acquire -Resource 'host-vm-startup' `
        -Agent 'Codex-UEFILoaderWindowsProvisioner' -Project $ProjectRoot `
        -RunId $RunId -LeaseMinutes 10
    try {
        [void](Invoke-Vmrun -Arguments @('start', $ResolvedVmx, 'nogui'))
        Send-VncBootKey
    }
    finally {
        if ($null -ne $StartupLease) {
            & $LeaseScript -Action Release -Resource 'host-vm-startup' `
                -LeaseToken $StartupLease.LeaseToken | Out-Null
            $StartupLease = $null
        }
    }
    [void](Wait-SerialMarker -Marker 'SETUP_OOBE_READY' -TimeoutMinutes 60)
    [void](Invoke-Vmrun -Arguments @('installTools', $ResolvedVmx))
    [void](Wait-SerialMarker -Marker 'SETUP_VMWARE_TOOLS_READY' -TimeoutMinutes 20)

    $ToolsDeadline = [DateTime]::UtcNow.AddMinutes(10)
    $ToolsReady = $false
    while ([DateTime]::UtcNow -lt $ToolsDeadline) {
        if (Test-VmRunning) {
            $Probe = Invoke-Vmrun -Arguments @('-gu', $UserName, '-gp',
                $PlainPassword, 'listProcessesInGuest', $ResolvedVmx) -AllowFailure
            if ($Probe.ExitCode -eq 0) { $ToolsReady = $true; break }
        }
        Start-Sleep -Seconds 5
    }
    if (-not $ToolsReady) { throw 'VMware Tools guest operations did not become ready.' }
    [void](Invoke-Vmrun -Arguments @('-gu', $UserName, '-gp', $PlainPassword,
        'copyFileFromGuestToHost', $ResolvedVmx,
        'C:\ProgramData\UEFILoaderProvisioned.json', $ResultPath))
    $ProvisionResult = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
    if ([int]$ProvisionResult.SchemaVersion -ne 1 -or
        [uint64]$ProvisionResult.CurrentBuild -ne 26200 -or
        -not [bool]$ProvisionResult.TpmPresent) {
        throw "The fresh guest is not Windows build 26200: $($ProvisionResult.CurrentBuild).$($ProvisionResult.UBR)"
    }

    Stop-ExactVm
    $VmStopped = $true
    Set-VmxProvisionedState
    if (Test-Path -LiteralPath $AnswerDisk -PathType Leaf) {
        $ResolvedAnswer = [IO.Path]::GetFullPath($AnswerDisk)
        if (-not $ResolvedAnswer.StartsWith(
                $VmRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Unsafe answer-disk cleanup path.'
        }
        Remove-Item -LiteralPath $ResolvedAnswer -Force
    }
    [void](Invoke-Vmrun -Arguments @('snapshot', $ResolvedVmx, $SnapshotName))
    $Snapshots = (Invoke-Vmrun -Arguments @('listSnapshots', $ResolvedVmx)).Output
    if ($Snapshots -notcontains $SnapshotName) {
        throw 'The clean provisioning snapshot was not listed after creation.'
    }
    $Success = $true
    [pscustomobject]@{
        Result = 'PASS'
        RunId = $RunId
        VmxPath = $ResolvedVmx
        SnapshotName = $SnapshotName
        CurrentBuild = [uint64]$ProvisionResult.CurrentBuild
        UBR = [uint64]$ProvisionResult.UBR
        DisplayVersion = [string]$ProvisionResult.DisplayVersion
        TpmPresent = [bool]$ProvisionResult.TpmPresent
        TpmReady = [bool]$ProvisionResult.TpmReady
        Evidence = $ResultPath
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
    if (-not $Success) {
        try { Stop-ExactVm; $VmStopped = $true } catch { }
    }
    if ($null -ne $Lease -and ($Success -or $VmStopped)) {
        try {
            & $LeaseScript -Action Release -Resource $LeaseResource `
                -LeaseToken $Lease.LeaseToken | Out-Null
        }
        catch { }
    }
    if (-not [string]::IsNullOrEmpty($PlainPassword)) {
        $PlainPassword = ('0' * $PlainPassword.Length)
    }
    $NetworkCredential.Password = ''
}
