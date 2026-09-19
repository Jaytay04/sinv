[CmdletBinding()]
param(
    [ValidateSet('Doctor', 'CredentialStatus', 'StoreVmCredential',
        'RemoveVmCredential', 'StartBroker', 'BrokerStatus',
        'RunPreparedGate', 'PowerOffPreservedVm', 'StopBroker',
        'RevertSnapshot', 'StartVm', 'StopConfiguredVm',
        'Plan', 'Prepare',
        'StartDebugger', 'WaitDebugger', 'BeginDebugLog', 'Submit', 'Wait',
        'EndDebugLog')]
    [string]$Action = 'Doctor',
    [string]$ConfigPath,
    [string]$PackagePath,
    [string]$ExpectedPackageSha256,
    [string]$GateScript,
    [string]$SymbolPath,
    [string]$RunId,
    [int]$TimeoutSeconds,
    [switch]$AuthorizeLiveVmRun,
    [switch]$AuthorizeVmPowerOff,
    [switch]$AuthorizeSnapshotRevert,
    [switch]$AuthorizeCredentialRemoval
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $PSScriptRoot 'host-runner.config.json'
}
Import-Module (Join-Path $PSScriptRoot 'HostRunner.Core.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'HostRunner.CredentialManager.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'HostRunner.Vmware.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'HostRunner.Debugger.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'HostRunner.Broker.psm1') -Force

function Invoke-ConfiguredEncryptedVmrun {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)]
        [ValidateSet('RevertSnapshot', 'Start', 'Stop')]
        [string]$Operation
    )

    $Password = Get-HrVmCredential $Config.Credential.TargetName
    $PasswordPointer = [IntPtr]::Zero
    $PlainPassword = $null
    $VmrunArguments = $null
    try {
        $PasswordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR(
            $Password)
        $PlainPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
            $PasswordPointer)
        $Tail = Get-HrVmrunOperationTail -Operation $Operation `
            -VmxPath $Config.Vm.VmxPath `
            -SnapshotName $Config.Vm.SnapshotName
        $VmrunArguments = @('-T', 'ws', '-vp', $PlainPassword) + $Tail
        $Output = @(& $Config.Tools.VmrunPath @VmrunArguments 2>&1)
        $ExitCode = $LASTEXITCODE
        $SafeOutput = @($Output | ForEach-Object {
            ([string]$_).Replace($PlainPassword, '<redacted>')
        })
        if ($ExitCode -ne 0) {
            $Detail = $SafeOutput -join [Environment]::NewLine
            throw "vmrun $Operation failed with exit code $ExitCode`: $Detail"
        }
        return [pscustomobject]@{
            ExitCode = $ExitCode
            Operation = $Operation
            Output = $SafeOutput
        }
    }
    finally {
        $VmrunArguments = $null
        $PlainPassword = $null
        if ($PasswordPointer -ne [IntPtr]::Zero) {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($PasswordPointer)
        }
        $Password.Dispose()
    }
}

function Get-RunningVmPaths {
    param($Config)

    $Output = & $Config.Tools.VmrunPath -T ws list 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "vmrun list failed: $($Output -join [Environment]::NewLine)"
    }
    return @($Output | Select-Object -Skip 1 | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    } | ForEach-Object { [IO.Path]::GetFullPath([string]$_) })
}

function Assert-ConfiguredVmRunning {
    param($Config)

    $Expected = [IO.Path]::GetFullPath([string]$Config.Vm.VmxPath)
    $Running = @(Get-RunningVmPaths $Config)
    if (-not ($Running | Where-Object {
            $_.Equals($Expected, [StringComparison]::OrdinalIgnoreCase)
        })) {
        throw 'The configured VM is not running. Start it from the clean checkpoint before publishing a gate.'
    }
}

function Assert-ConfiguredVmStopped {
    param($Config)

    $Expected = [IO.Path]::GetFullPath([string]$Config.Vm.VmxPath)
    $Running = @(Get-RunningVmPaths $Config)
    if ($Running | Where-Object {
            $_.Equals($Expected, [StringComparison]::OrdinalIgnoreCase)
        }) {
        throw 'The configured VM must be powered off for this operation.'
    }
}

function Get-HostFreeMemoryMb {
    return [int](Get-HrSystemMemoryState).AvailablePhysicalMb
}

function Assert-HostFreeMemoryFloor {
    param(
        $Config,
        [Parameter(Mandatory=$true)][string]$Phase
    )
    $AvailableMb = Get-HostFreeMemoryMb
    $MinimumMb = [int]$Config.Debugger.MinimumHostFreeMemoryMb
    if ($AvailableMb -lt $MinimumMb) {
        throw "Insufficient host memory during $Phase`: available=${AvailableMb} MB, required floor=${MinimumMb} MB. Preserve the VM; do not retry."
    }
    return [int]$AvailableMb
}

function Assert-HostResourcePreflight {
    param(
        $Config,
        [int]$ExpectedDebuggerPid = 0
    )

    # This function is deliberately read-only.  It refuses to alter the VMX,
    # stop another workload, or disable a host security/network component.
    $Nic = Get-HrVmxNicState -VmxPath $Config.Vm.VmxPath
    if (-not $Nic.Disconnected) {
        throw 'The configured VMX has a present or connected NIC. Disconnect/remove the NIC in VMware settings and retry; the runner will not rewrite the VMX.'
    }

    $Graphics = Get-HrVmxGraphicsState -VmxPath $Config.Vm.VmxPath
    if ($Graphics.Enable3d -or $Graphics.GraphicsMemoryMb -gt 256 -or
        $Graphics.CheckpointGraphicsMemoryMb -gt 256) {
        throw "Unsafe VM graphics configuration: enable3d=$($Graphics.Enable3d), graphicsMemory=$($Graphics.GraphicsMemoryMb) MB, checkpointGraphicsMemory=$($Graphics.CheckpointGraphicsMemoryMb) MB. Disable 3D acceleration and configure both graphics apertures to no more than 256 MB before retrying; the runner will not rewrite the VMX."
    }

    $Serial = Get-HrVmxSerialPipeState -VmxPath $Config.Vm.VmxPath `
        -DebuggerPipe $Config.Vm.DebuggerPipe
    if (-not $Serial.Compatible) {
        throw "Unsafe VM serial configuration: serial0 must be present, use the configured named pipe, declare pipe.endPoint=server, and set tryNoRxLoss=FALSE. The runner will not rewrite the VMX."
    }

    $VmMemoryMb = Get-HrVmxMemoryMb -VmxPath $Config.Vm.VmxPath
    $AvailableMb = Assert-HostFreeMemoryFloor -Config $Config -Phase 'VM preflight'
    $ReserveMb = [int]$Config.Debugger.HostMemoryReserveMb
    $RequiredMb = $VmMemoryMb + $ReserveMb
    if ($AvailableMb -lt $RequiredMb) {
        throw "Insufficient host memory for VM start: VMX=$VmMemoryMb MB, available=$AvailableMb MB, reserve=$ReserveMb MB. Close workloads or reduce the VM memory in the VM settings, then retry."
    }

    $OtherVmwareVmx = @(Get-Process -Name 'vmware-vmx' -ErrorAction SilentlyContinue)
    if ($OtherVmwareVmx.Count -ne 0) {
        throw "Another vmware-vmx process is already running (PIDs: $($OtherVmwareVmx.Id -join ', ')). Close it cleanly before starting the configured VM."
    }
    Assert-NoUnexpectedDebuggerProcesses -Config $Config `
        -ExpectedPid $ExpectedDebuggerPid

    return [pscustomobject]@{
        VmMemoryMb = $VmMemoryMb
        AvailableMb = $AvailableMb
        ReserveMb = $ReserveMb
        RequiredMb = $RequiredMb
        NicDisconnected = $Nic.Disconnected
        Graphics3dDisabled = -not $Graphics.Enable3d
        GraphicsMemoryMb = $Graphics.GraphicsMemoryMb
        CheckpointGraphicsMemoryMb = $Graphics.CheckpointGraphicsMemoryMb
        SerialPipeCompatible = $Serial.Compatible
    }
}

function Read-GuestMailboxJson {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [int]$MaximumBytes = 65536
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $Length = [long](Get-Item -LiteralPath $Path).Length
    if ($Length -le 0 -or $Length -gt $MaximumBytes) { return $null }
    $Stream = $null
    $Buffer = [byte[]]::new([int]$Length)
    try {
        $Stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
            [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $Read = 0
        while ($Read -lt $Buffer.Length) {
            $Count = $Stream.Read($Buffer, $Read, $Buffer.Length - $Read)
            if ($Count -le 0) { return $null }
            $Read += $Count
        }
    }
    finally {
        if ($null -ne $Stream) { $Stream.Dispose() }
    }
    try {
        $Text = [Text.Encoding]::UTF8.GetString($Buffer)
        # Windows PowerShell 5.1 writes an EF BB BF prefix for -Encoding utf8.
        # ConvertFrom-Json rejects the resulting U+FEFF when decoding a byte
        # buffer directly, so remove exactly one leading BOM before parsing.
        if ($Text.Length -gt 0 -and $Text[0] -eq [char]0xFEFF) {
            $Text = $Text.Substring(1)
        }
        return ($Text | ConvertFrom-Json)
    }
    catch {
        return $null
    }
}

function New-GuestMailboxReadinessProbe {
    $Nonce = [Guid]::NewGuid().ToString('N').Substring(0, 10)
    $RunId = 'gate0-host-runner-readiness-{0}-{1}' -f `
        (Get-Date -Format 'yyyyMMdd-HHmmss'), $Nonce
    if ($RunId -cnotmatch `
            '^gate0-host-runner-readiness-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw "Readiness RunId is not canonical: $RunId"
    }
    return [pscustomobject]@{
        SchemaVersion = 0
        RunId = $RunId
    }
}

function Publish-GuestMailboxReadinessProbe {
    param($Config)

    $Root = Get-HrFullPath $Config.Mailbox.HostRoot
    $Requests = Join-Path $Root 'requests'
    $Responses = Join-Path $Root 'responses'
    foreach ($Directory in @($Requests, $Responses)) {
        New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    }

    $Probe = New-GuestMailboxReadinessProbe
    $RunId = [string]$Probe.RunId
    $RequestPath = Join-Path $Requests ($RunId + '.json')
    $Temporary = $RequestPath + '.tmp'
    $ProcessingPath = $RequestPath + '.processing'
    $CompletedPath = $RequestPath + '.completed'
    $ResponseDirectory = Join-Path $Responses $RunId
    if ((Test-Path -LiteralPath $RequestPath) -or
        (Test-Path -LiteralPath $Temporary) -or
        (Test-Path -LiteralPath $ProcessingPath) -or
        (Test-Path -LiteralPath $CompletedPath) -or
        (Test-Path -LiteralPath $ResponseDirectory)) {
        throw "Readiness probe has stale mailbox state: $RunId"
    }

    $PublishedUtc = [DateTime]::UtcNow
    $Probe | ConvertTo-Json -Compress -Depth 3 |
        Set-Content -LiteralPath $Temporary -Encoding utf8
    Move-Item -LiteralPath $Temporary -Destination $RequestPath
    return [pscustomobject]@{
        RunId = $RunId
        RequestPath = $RequestPath
        CompletedPath = $CompletedPath
        ResponseDirectory = $ResponseDirectory
        ResultPath = Join-Path $ResponseDirectory 'result.json'
        PublishedUtc = $PublishedUtc
    }
}

function Find-GuestMailboxReadinessResponse {
    param(
        [Parameter(Mandatory=$true)]$Probe
    )

    $Result = Read-GuestMailboxJson -Path $Probe.ResultPath
    $Request = Read-GuestMailboxJson -Path $Probe.CompletedPath
    if ($null -eq $Result -or $null -eq $Request) { return $null }

    $ResultProperties = @($Result.PSObject.Properties.Name)
    if ($ResultProperties.Count -ne 5 -or
        $ResultProperties -notcontains 'SchemaVersion' -or
        $ResultProperties -notcontains 'RunId' -or
        $ResultProperties -notcontains 'Status' -or
        $ResultProperties -notcontains 'Message' -or
        $ResultProperties -notcontains 'CompletedUtc' -or
        $Result.SchemaVersion -ne 1 -or
        [string]$Result.RunId -cne [string]$Probe.RunId -or
        [string]$Result.Status -cne 'REJECTED' -or
        [string]$Result.Message -cne 'Unsupported request schema: 0') {
        return $null
    }

    $RequestProperties = @($Request.PSObject.Properties.Name)
    if ($RequestProperties.Count -ne 2 -or
        $RequestProperties -notcontains 'SchemaVersion' -or
        $RequestProperties -notcontains 'RunId' -or
        $Request.SchemaVersion -ne 0 -or
        [string]$Request.RunId -cne [string]$Probe.RunId) {
        return $null
    }

    $CompletedUtc = [DateTime]::MinValue
    if (-not [DateTime]::TryParse([string]$Result.CompletedUtc,
            [ref]$CompletedUtc) -or
        $CompletedUtc.ToUniversalTime() -lt
            ([DateTime]$Probe.PublishedUtc).ToUniversalTime()) {
        return $null
    }

    return [pscustomobject]@{
        Result = $Result
        Request = $Request
        ResultPath = $Probe.ResultPath
        RequestPath = $Probe.CompletedPath
    }
}

function Wait-GuestMailboxReadiness {
    param(
        $Config,
        [Parameter(Mandatory=$true)]$Probe
    )

    $PollMs = [int]$Config.Debugger.GuestMailboxReadinessPollIntervalMilliseconds
    $Deadline = [DateTime]::UtcNow.AddSeconds(
        [int]$Config.Debugger.GuestMailboxReadinessTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $Deadline) {
        [void](Assert-HostFreeMemoryFloor -Config $Config `
            -Phase 'guest mailbox readiness')
        $Response = Find-GuestMailboxReadinessResponse -Probe $Probe
        if ($null -ne $Response) {
            $StabilizationDeadline = [DateTime]::UtcNow.AddSeconds(
                [int]$Config.Debugger.GuestMailboxStabilizationSeconds)
            while ([DateTime]::UtcNow -lt $StabilizationDeadline) {
                [void](Assert-HostFreeMemoryFloor -Config $Config `
                    -Phase 'guest mailbox stabilization')
                Start-Sleep -Milliseconds $PollMs
            }
            return [pscustomobject]@{
                Result = 'READY'
                ProbeRunId = $Probe.RunId
                Response = $Response.Result
                StabilizationSeconds = [int]$Config.Debugger.GuestMailboxStabilizationSeconds
            }
        }
        Start-Sleep -Milliseconds $PollMs
    }
    throw "Guest mailbox readiness response did not satisfy the exact rejection contract within $($Config.Debugger.GuestMailboxReadinessTimeoutSeconds) seconds. Preserve the VM; do not launch KD or retry."
}

function Assert-CfosspeedStopped {
    param()

    # cFosSpeed installs a kernel network filter.  Its 2021 builds have
    # caused host DRIVER_IRQL_NOT_LESS_OR_EQUAL failures during high I/O
    # activity.  Detection is read-only; the runner never disables, removes,
    # or changes the service.
    $Drivers = @(Get-CimInstance Win32_SystemDriver -ErrorAction Stop |
        Where-Object {
            ([string]$_.Name -match '(?i)cfosspeed') -or
            ([string]$_.PathName -match '(?i)cfosspeed')
        })
    $Services = @(Get-CimInstance Win32_Service -ErrorAction Stop |
        Where-Object {
            ([string]$_.Name -match '(?i)cfosspeed') -or
            ([string]$_.DisplayName -match '(?i)cFosSpeed') -or
            ([string]$_.PathName -match '(?i)cfosspeed')
        })
    $Running = @($Drivers + $Services | Where-Object {
        [string]$_.State -match '(?i)^Running$'
    })
    if ($Running.Count -ne 0) {
        $Names = @($Running | ForEach-Object { $_.Name }) -join ', '
        throw "cFosSpeed is active ($Names). Update or uninstall/disable cFosSpeed manually before live VM testing; the host runner will not change it."
    }
}

function Assert-HostDebuggerElevation {
    if (-not (Test-HrAdministrator)) {
        throw 'Host debugger startup requires an elevated Administrator PowerShell process. Refusing to start the VM because Microsoft requires elevation for kernel debugging over a VM serial pipe.'
    }
}

function Get-DebuggerPaths {
    param($Config)

    $Directory = Join-Path $Config.Mailbox.HostRoot 'debugger'
    return [pscustomobject]@{
        Directory = $Directory
        Pid = Join-Path $Directory 'kd.pid'
        Started = Join-Path $Directory 'started.json'
        Ready = Join-Path $Directory 'ready.json'
        Sticky = Join-Path $Directory 'failure.state'
        StandardOutput = Join-Path $Directory 'kd-stdout.log'
        StandardError = Join-Path $Directory 'kd-stderr.log'
        ResourceSamples = Join-Path $Directory 'kd-resource-samples.csv'
        PoolSnapshot = Join-Path $Directory 'pool-nonpaged-threshold.log'
        PoolSnapshotAttempted = Join-Path $Directory `
            'pool-nonpaged-threshold.attempted'
        PoolSnapshotError = Join-Path $Directory `
            'pool-nonpaged-threshold-error.txt'
    }
}

function Get-ConfiguredKdProcess {
    param($Config, [Parameter(Mandatory=$true)][string]$PidPath)

    if (-not (Test-Path -LiteralPath $PidPath -PathType Leaf)) {
        return $null
    }
    $PidText = (Get-Content -Raw -LiteralPath $PidPath).Trim()
    if ($PidText -cnotmatch '^[0-9]{1,10}$') {
        throw "Debugger PID file is malformed: $PidPath"
    }
    $StartedPath = Join-Path (Split-Path -Parent $PidPath) 'started.json'
    if (-not (Test-Path -LiteralPath $StartedPath -PathType Leaf)) {
        throw "Cannot verify ownership of debugger PID $PidText`: started.json is absent."
    }
    try {
        $Started = Get-Content -Raw -LiteralPath $StartedPath | ConvertFrom-Json
    }
    catch {
        throw "Cannot verify ownership of debugger PID $PidText`: started.json is malformed."
    }
    $StoredExecutablePath = $null
    try {
        $StoredExecutablePath = [IO.Path]::GetFullPath(
            [string]$Started.ExecutablePath)
    }
    catch {
        throw "Cannot verify ownership of debugger PID $PidText`: started.json executable path is invalid."
    }
    $ExpectedExecutablePath = [IO.Path]::GetFullPath(
        [string]$Config.Tools.KdPath)
    if ($Started.SchemaVersion -ne 1 -or
        $Started.ProcessId -ne [int]$PidText -or
        [string]::IsNullOrWhiteSpace([string]$Started.ProcessStartedUtc) -or
        -not $StoredExecutablePath.Equals(
            $ExpectedExecutablePath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cannot verify ownership of debugger PID $PidText`: started.json does not match PID, executable, and start time."
    }
    $Process = Get-Process -Id ([int]$PidText) -ErrorAction SilentlyContinue
    if ($null -eq $Process) {
        return $null
    }
    $ExpectedName = [IO.Path]::GetFileNameWithoutExtension(
        [string]$Config.Tools.KdPath)
    if ($Process.ProcessName -cne $ExpectedName) {
        throw "Debugger PID belongs to another process: $($Process.ProcessName)"
    }
    $ActualPath = $null
    $ProcessStartedUtc = $null
    try {
        $ActualPath = $Process.Path
        $ProcessStartedUtc = $Process.StartTime.ToUniversalTime().ToString('o')
    }
    catch {
        throw "Cannot verify ownership of debugger PID $($Process.Id): $($_.Exception.Message)"
    }
    if ([string]::IsNullOrWhiteSpace($ActualPath) -or
        -not ([IO.Path]::GetFullPath($ActualPath).Equals(
            $ExpectedExecutablePath,
            [StringComparison]::OrdinalIgnoreCase))) {
        throw "Debugger PID uses an unexpected executable: $ActualPath"
    }
    if ($Started.SchemaVersion -ne 1 -or
        $Started.ProcessId -ne $Process.Id -or
        [string]$Started.ProcessStartedUtc -cne $ProcessStartedUtc -or
        -not $StoredExecutablePath.Equals(
            [IO.Path]::GetFullPath($ActualPath),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cannot verify ownership of debugger PID $($Process.Id): started.json does not match PID, executable, and start time."
    }
    return $Process
}

function Stop-OwnedProcess {
    param([Parameter(Mandatory=$true)]$Process)

    $Process.Refresh()
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction Stop
        # KD can take several seconds to unwind its named-pipe transport and
        # flush redirected output after termination is requested.  Keep the
        # wait bounded, but do not report a cleanup failure while that normal
        # teardown is still in progress.
        if (-not $Process.WaitForExit(15000)) {
            throw "Failed to terminate owned process $($Process.Id)."
        }
        $Process.Refresh()
        if (-not $Process.HasExited) {
            throw "Owned process $($Process.Id) remained active after termination."
        }
    }
}

function Clear-DebuggerState {
    param(
        $Paths,
        [switch]$Strict
    )

    foreach ($Path in @($Paths.Pid, $Paths.Started, $Paths.Ready, $Paths.Sticky)) {
        if (-not (Test-Path -LiteralPath $Path)) {
            continue
        }
        if ($Strict) {
            Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
        }
        else {
            Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
        }
    }
}

function Archive-PreviousDebuggerLogs {
    param($Paths)

    $Existing = @(@($Paths.StandardOutput, $Paths.StandardError,
            $Paths.ResourceSamples, $Paths.PoolSnapshot,
            $Paths.PoolSnapshotAttempted, $Paths.PoolSnapshotError) |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($Existing.Count -eq 0) {
        return
    }
    $History = Join-Path $Paths.Directory 'history'
    New-Item -ItemType Directory -Force -Path $History | Out-Null
    $Stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    foreach ($Path in $Existing) {
        $Name = [IO.Path]::GetFileNameWithoutExtension($Path)
        $Extension = [IO.Path]::GetExtension($Path)
        Move-Item -LiteralPath $Path -Destination `
            (Join-Path $History "$Name-$Stamp$Extension")
    }
}

function Set-DebuggerReady {
    param($Paths, $Process, [string]$RunId, [string]$LogPath)

    $Temporary = "$($Paths.Ready).tmp"
    [ordered]@{
        SchemaVersion = 1
        RunId = $RunId
        ProcessId = $Process.Id
        ProcessStartedUtc = $Process.StartTime.ToUniversalTime().ToString('o')
        LogPath = $LogPath
        ConnectedUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath $Temporary -Encoding utf8
    Move-Item -LiteralPath $Temporary -Destination $Paths.Ready -Force
}

function Set-DebuggerStarted {
    param($Paths, $Process)

    $ExecutablePath = $null
    try { $ExecutablePath = [IO.Path]::GetFullPath($Process.Path) }
    catch {
        throw "Cannot record debugger ownership for PID $($Process.Id): $($_.Exception.Message)"
    }
    if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
        throw "Cannot record debugger ownership for PID $($Process.Id): executable path is unavailable."
    }
    $Temporary = "$($Paths.Started).tmp"
    [ordered]@{
        SchemaVersion = 1
        ProcessId = $Process.Id
        ExecutablePath = $ExecutablePath
        ProcessStartedUtc = $Process.StartTime.ToUniversalTime().ToString('o')
        StartedUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath $Temporary -Encoding utf8
    Move-Item -LiteralPath $Temporary -Destination $Paths.Started -Force
}

function Assert-NoUnexpectedDebuggerProcesses {
    param(
        $Config,
        [int]$ExpectedPid = 0
    )

    $ExpectedName = [IO.Path]::GetFileNameWithoutExtension(
        [string]$Config.Tools.KdPath)
    $Processes = @(Get-Process -Name $ExpectedName -ErrorAction SilentlyContinue)
    foreach ($Process in $Processes) {
        if ($ExpectedPid -le 0 -or $Process.Id -ne $ExpectedPid) {
            throw "Unexpected debugger process is already running: PID $($Process.Id)."
        }
    }
}

function Assert-DebuggerReady {
    param(
        $Config,
        [Parameter(Mandatory=$true)][string]$RunId
    )

    $Paths = Get-DebuggerPaths $Config
    if (-not (Test-Path -LiteralPath $Paths.Ready -PathType Leaf)) {
        throw 'The headless debugger has not completed its connection gate.'
    }
    $Ready = Get-Content -Raw -LiteralPath $Paths.Ready | ConvertFrom-Json
    $Process = Get-ConfiguredKdProcess $Config $Paths.Pid
    $ExpectedLogPath = Join-Path $Config.Mailbox.HostRoot `
        "responses\$RunId\windbg.log"
    $RunLogState = Get-HrDebuggerRunLogState -LogPath $ExpectedLogPath `
        -RunId $RunId -MaximumLogBytes $Config.Debugger.MaximumRunLogBytes
    if ($RunLogState.State -in @('FATAL', 'FLOOD')) {
        throw "The per-run debugger log is unsafe for ${RunId}: state=$($RunLogState.State), bytes=$($RunLogState.Length)."
    }
    if ($null -eq $Process -or $Ready.SchemaVersion -ne 1 -or
        [string]$Ready.RunId -cne $RunId -or
        $Ready.ProcessId -ne $Process.Id -or
        [string]$Ready.ProcessStartedUtc -cne `
            $Process.StartTime.ToUniversalTime().ToString('o') -or
        -not ([IO.Path]::GetFullPath([string]$Ready.LogPath).Equals(
            [IO.Path]::GetFullPath($ExpectedLogPath),
            [StringComparison]::OrdinalIgnoreCase)) -or
        $RunLogState.State -cne 'READY') {
        throw 'The debugger readiness record is stale.'
    }
    $ConnectedUtc = [DateTime]::MinValue
    if (-not [DateTime]::TryParse(
            [string]$Ready.ConnectedUtc, [ref]$ConnectedUtc)) {
        throw 'The debugger readiness timestamp is invalid.'
    }
    $ConnectedUtc = $ConnectedUtc.ToUniversalTime()
    if ($ConnectedUtc -lt $Process.StartTime.ToUniversalTime() -or
        $ConnectedUtc -gt [DateTime]::UtcNow.AddMinutes(1)) {
        throw 'The debugger readiness timestamp is stale.'
    }
    return $Process
}

function Invoke-DebuggerPoolSnapshotIfNeeded {
    param($Config, $Paths, $Memory)

    $PoolmonPath = [string]$Config.Tools.PoolmonPath
    $PoolTagPath = [string]$Config.Tools.PoolTagPath
    if ([string]::IsNullOrWhiteSpace($PoolmonPath) -or
        [string]::IsNullOrWhiteSpace($PoolTagPath)) {
        return 'DISABLED'
    }
    $TriggerMb = [int]$Config.Debugger.PoolSnapshotTriggerNonpagedMb
    if ([int]$Memory.KernelNonpagedMb -lt $TriggerMb) {
        return 'BELOW_THRESHOLD'
    }
    if ([int]$Memory.AvailablePhysicalMb -lt
        [int]$Config.Debugger.MinimumHostFreeMemoryMb) {
        return 'SKIPPED_LOW_MEMORY'
    }
    if (Test-Path -LiteralPath $Paths.PoolSnapshotAttempted -PathType Leaf) {
        return 'ALREADY_ATTEMPTED'
    }

    try {
        [ordered]@{
            SchemaVersion = 1
            AttemptedUtc = [DateTime]::UtcNow.ToString('o')
            KernelNonpagedMb = [int]$Memory.KernelNonpagedMb
            TriggerMb = $TriggerMb
        } | ConvertTo-Json -Compress | Set-Content `
            -LiteralPath $Paths.PoolSnapshotAttempted -Encoding ascii

        & $PoolmonPath '-g' $PoolTagPath '-n' $Paths.PoolSnapshot | Out-Null
        $ExitCode = $LASTEXITCODE
        if ($ExitCode -ne 0 -or
            -not (Test-Path -LiteralPath $Paths.PoolSnapshot -PathType Leaf)) {
            throw "Poolmon snapshot failed with exit code $ExitCode."
        }
        return 'CAPTURED'
    }
    catch {
        $Message = [string]$_.Exception.Message
        if ($Message.Length -gt 2048) {
            $Message = $Message.Substring(0, 2048)
        }
        Set-Content -LiteralPath $Paths.PoolSnapshotError -Encoding utf8 `
            -Value $Message
        return 'FAILED'
    }
}

function Write-DebuggerResourceSample {
    param($Config, $Paths, $Process)

    $Process.Refresh()
    $WorkingSetMb = [int][math]::Ceiling(
        [double]$Process.WorkingSet64 / 1MB)
    $PrivateMemoryMb = [int][math]::Ceiling(
        [double]$Process.PrivateMemorySize64 / 1MB)
    $RunnerProcess = Get-Process -Id $PID -ErrorAction Stop
    $VmwareProcesses = @(Get-Process -Name 'vmware-vmx' `
        -ErrorAction SilentlyContinue)
    $VmwareWorkingSetBytes = 0L
    $VmwarePrivateMemoryBytes = 0L
    foreach ($VmwareProcess in $VmwareProcesses) {
        $VmwareProcess.Refresh()
        $VmwareWorkingSetBytes += $VmwareProcess.WorkingSet64
        $VmwarePrivateMemoryBytes += $VmwareProcess.PrivateMemorySize64
    }
    $VmwareWorkingSetMb = [int][math]::Ceiling(
        [double]$VmwareWorkingSetBytes / 1MB)
    $VmwarePrivateMemoryMb = [int][math]::Ceiling(
        [double]$VmwarePrivateMemoryBytes / 1MB)
    $Memory = Get-HrSystemMemoryState
    if (-not (Test-Path -LiteralPath $Paths.ResourceSamples -PathType Leaf)) {
        Set-Content -LiteralPath $Paths.ResourceSamples -Encoding ascii `
            -Value ('Utc,KdProcessId,KdWorkingSetMb,KdPrivateMemoryMb,' +
                'RunnerProcessId,RunnerWorkingSetMb,RunnerPrivateMemoryMb,' +
                'VmwareProcessCount,VmwareWorkingSetMb,VmwarePrivateMemoryMb,' +
                'HostAvailablePhysicalMb,HostCommitTotalMb,HostCommitLimitMb,' +
                'HostSystemCacheMb,HostKernelTotalMb,HostKernelPagedMb,' +
                'HostKernelNonpagedMb,HostHandleCount,HostProcessCount,' +
                'HostThreadCount')
    }
    if ((Get-Item -LiteralPath $Paths.ResourceSamples).Length -lt 1048576) {
        Add-Content -LiteralPath $Paths.ResourceSamples -Encoding ascii `
            -Value (('{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},' +
                '{10},{11},{12},{13},{14},{15},{16},{17},{18},{19}') -f
                [DateTime]::UtcNow.ToString('o'), $Process.Id,
                $WorkingSetMb, $PrivateMemoryMb, $RunnerProcess.Id,
                [int][math]::Ceiling($RunnerProcess.WorkingSet64 / 1MB),
                [int][math]::Ceiling($RunnerProcess.PrivateMemorySize64 / 1MB),
                $VmwareProcesses.Count, $VmwareWorkingSetMb,
                $VmwarePrivateMemoryMb, $Memory.AvailablePhysicalMb,
                $Memory.CommitTotalMb, $Memory.CommitLimitMb,
                $Memory.SystemCacheMb, $Memory.KernelTotalMb,
                $Memory.KernelPagedMb, $Memory.KernelNonpagedMb,
                $Memory.HandleCount, $Memory.ProcessCount,
                $Memory.ThreadCount)
    }
    $PoolSnapshotState = Invoke-DebuggerPoolSnapshotIfNeeded `
        -Config $Config -Paths $Paths -Memory $Memory
    return [pscustomobject]@{
        WorkingSetMb = $WorkingSetMb
        PrivateMemoryMb = $PrivateMemoryMb
        AvailableMb = [int]$Memory.AvailablePhysicalMb
        KernelNonpagedMb = [int]$Memory.KernelNonpagedMb
        PoolSnapshotState = $PoolSnapshotState
    }
}

function Assert-DebuggerStartupResources {
    param($Config, $Paths, $Process)

    $Sample = Write-DebuggerResourceSample -Config $Config `
        -Paths $Paths -Process $Process
    $WorkingSetMb = $Sample.WorkingSetMb
    $PrivateMemoryMb = $Sample.PrivateMemoryMb
    $AvailableMb = $Sample.AvailableMb
    if ([bool]$Config.Debugger.StopAfterPoolSnapshot -and
        $Sample.PoolSnapshotState -in @('CAPTURED', 'FAILED')) {
        $Failure = [pscustomobject]@{
            State = 'DIAGNOSTIC_CAPTURE'
            Length = 0
            Tail = "One-shot Poolmon state=$($Sample.PoolSnapshotState) at nonpaged=$($Sample.KernelNonpagedMb) MB. Startup was intentionally stopped."
        }
        Stop-DebuggerAfterStartupFailure $Paths $Process $Failure
    }
    $MinimumMb = [int]$Config.Debugger.MinimumHostFreeMemoryMb
    $CriticalMb = [int][math]::Max(512, [math]::Floor($MinimumMb / 2))
    $MaximumWorkingSetMb = [int]$Config.Debugger.MaximumDebuggerWorkingSetMb
    if ($WorkingSetMb -gt $MaximumWorkingSetMb) {
        $Failure = [pscustomobject]@{
            State = 'RESOURCE_LIMIT'
            Length = 0
            Tail = "KD working set ${WorkingSetMb} MB exceeds ${MaximumWorkingSetMb} MB; private=${PrivateMemoryMb} MB, host-free=${AvailableMb} MB."
        }
        Stop-DebuggerAfterStartupFailure $Paths $Process $Failure
    }
    if ($AvailableMb -lt $CriticalMb) {
        $Failure = [pscustomobject]@{
            State = 'RESOURCE_LIMIT'
            Length = 0
            Tail = "Host free memory ${AvailableMb} MB is below the critical ${CriticalMb} MB floor."
        }
        Stop-DebuggerAfterStartupFailure $Paths $Process $Failure
    }
    if ($AvailableMb -lt $MinimumMb) {
        # VMware can briefly charge the restored snapshot's memory while the
        # guest and KD establish their boot-time connection.  Confirm a soft
        # floor breach for a short bounded interval; never tolerate a breach
        # of the critical floor above.
        $GraceDeadline = [DateTime]::UtcNow.AddSeconds(
            [int]$Config.Debugger.HostMemoryBreachGraceSeconds)
        do {
            Start-Sleep -Milliseconds 250
            $Sample = Write-DebuggerResourceSample -Config $Config `
                -Paths $Paths `
                -Process $Process
            $WorkingSetMb = $Sample.WorkingSetMb
            $PrivateMemoryMb = $Sample.PrivateMemoryMb
            $AvailableMb = $Sample.AvailableMb
            if ($WorkingSetMb -gt $MaximumWorkingSetMb) {
                $Failure = [pscustomobject]@{
                    State = 'RESOURCE_LIMIT'
                    Length = 0
                    Tail = "KD working set ${WorkingSetMb} MB exceeds ${MaximumWorkingSetMb} MB during startup grace; private=${PrivateMemoryMb} MB, host-free=${AvailableMb} MB."
                }
                Stop-DebuggerAfterStartupFailure $Paths $Process $Failure
            }
            if ($AvailableMb -ge $MinimumMb) { break }
            if ($AvailableMb -lt $CriticalMb) { break }
        } while ([DateTime]::UtcNow -lt $GraceDeadline)
    }
    if ($AvailableMb -lt $MinimumMb) {
        $Failure = [pscustomobject]@{
            State = 'RESOURCE_LIMIT'
            Length = 0
            Tail = "Host free memory ${AvailableMb} MB remained below ${MinimumMb} MB after the bounded startup grace interval."
        }
        Stop-DebuggerAfterStartupFailure $Paths $Process $Failure
    }

}

function Assert-DebuggerHealth {
    param(
        $Config,
        [Parameter(Mandatory=$true)][string]$RunId,
        [switch]$RequireConnected
    )

    $Paths = Get-DebuggerPaths $Config
    $Process = Get-ConfiguredKdProcess $Config $Paths.Pid
    if ($null -eq $Process) {
        throw 'The owned headless debugger process is not running; debugger ownership state was preserved.'
    }
    $Process.Refresh()
    $RunLogPath = Join-Path $Config.Mailbox.HostRoot "responses\$RunId\windbg.log"
    $RunLogState = Get-HrDebuggerRunLogState -LogPath $RunLogPath `
        -RunId $RunId -MaximumLogBytes $Config.Debugger.MaximumRunLogBytes
    if ($RunLogState.State -in @('FATAL', 'FLOOD')) {
        $Failure = [pscustomobject]@{
            State = $RunLogState.State
            Length = $RunLogState.Length
            Tail = $RunLogState.Tail
        }
        Stop-DebuggerAfterStartupFailure $Paths $Process $Failure
    }
    $WorkingSetMb = [math]::Ceiling([double]$Process.WorkingSet64 / 1MB)
    $MaximumWorkingSetMb = [int]$Config.Debugger.MaximumDebuggerWorkingSetMb
    if ($WorkingSetMb -gt $MaximumWorkingSetMb) {
        $Failure = [pscustomobject]@{
            State = 'RESOURCE_LIMIT'
            Length = 0
            Tail = "KD working set ${WorkingSetMb} MB exceeds ${MaximumWorkingSetMb} MB."
        }
        Stop-DebuggerAfterStartupFailure $Paths $Process $Failure
    }
    $FreeMemoryMb = Get-HostFreeMemoryMb
    $MinimumFreeMemoryMb = [int]$Config.Debugger.MinimumHostFreeMemoryMb
    if ($FreeMemoryMb -lt $MinimumFreeMemoryMb) {
        $Failure = [pscustomobject]@{
            State = 'RESOURCE_LIMIT'
            Length = 0
            Tail = "Host free memory ${FreeMemoryMb} MB is below ${MinimumFreeMemoryMb} MB."
        }
        Stop-DebuggerAfterStartupFailure $Paths $Process $Failure
    }
    $Startup = Get-HrDebuggerStartupState `
        -StandardOutputPath $Paths.StandardOutput `
        -StandardErrorPath $Paths.StandardError `
        -ProcessExited $Process.HasExited `
        -MaximumLogBytes $Config.Debugger.MaximumStartupLogBytes `
        -StickyStatePath $Paths.Sticky
    if ($Startup.State -in @('FATAL', 'FLOOD', 'EXITED')) {
        Stop-DebuggerAfterStartupFailure $Paths $Process $Startup
    }
    if ($RequireConnected) {
        if ($Startup.State -ceq 'DISCONNECTED') {
            # An explicit post-connection no_debuggee/reconnect marker is
            # fatal. A marker-free WAITING tail is allowed because long target
            # output may have moved the original connection marker outside the
            # bounded startup tail; ready.json retains that verified handshake.
            Stop-DebuggerAfterStartupFailure $Paths $Process $Startup
        }
        try {
            $ReadyProcess = Assert-DebuggerReady $Config -RunId $RunId
        }
        catch {
            $Failure = [pscustomobject]@{
                State = 'NOT_READY'
                Length = $Startup.Length
                Tail = $Startup.Tail
            }
            Stop-DebuggerAfterStartupFailure $Paths $Process $Failure
        }
        return $ReadyProcess
    }
    if ($RunLogState.State -ne 'READY') {
        throw "The per-run debugger log is not valid for $RunId."
    }
    return $Process
}

function Set-RunDebugLogReady {
    param($Config, [string]$RunId, $Process, [string]$LogPath)

    $Path = Join-Path $Config.Mailbox.HostRoot `
        "responses\$RunId\debugger-log-ready.json"
    $Temporary = "$Path.tmp"
    [ordered]@{
        SchemaVersion = 1
        RunId = $RunId
        ProcessId = $Process.Id
        ProcessStartedUtc = $Process.StartTime.ToUniversalTime().ToString('o')
        LogPath = $LogPath
        ReadyUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath $Temporary -Encoding utf8
    Move-Item -LiteralPath $Temporary -Destination $Path -Force
}

function Assert-RunDebugLogReady {
    param($Config, [string]$RunId, $Process)

    $Path = Join-Path $Config.Mailbox.HostRoot `
        "responses\$RunId\debugger-log-ready.json"
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "The debugger log handshake is absent for $RunId."
    }
    $Ready = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    $ExpectedLogPath = Join-Path $Config.Mailbox.HostRoot `
        "responses\$RunId\windbg.log"
    $RunLogState = Get-HrDebuggerRunLogState -LogPath $ExpectedLogPath `
        -RunId $RunId -MaximumLogBytes $Config.Debugger.MaximumRunLogBytes
    if ($Ready.SchemaVersion -ne 1 -or
        [string]$Ready.RunId -cne $RunId -or
        $Ready.ProcessId -ne $Process.Id -or
        [string]$Ready.ProcessStartedUtc -cne `
            $Process.StartTime.ToUniversalTime().ToString('o') -or
        -not ([IO.Path]::GetFullPath([string]$Ready.LogPath).Equals(
            [IO.Path]::GetFullPath($ExpectedLogPath),
            [StringComparison]::OrdinalIgnoreCase)) -or
        $RunLogState.State -cne 'READY') {
        throw "The debugger log handshake is stale for $RunId."
    }
}

function Stop-DebuggerAfterStartupFailure {
    param($Paths, $Process, $Startup)

    $Tail = if ([string]::IsNullOrWhiteSpace($Startup.Tail)) {
        '<no debugger output>'
    }
    else {
        $Startup.Tail
    }
    $Failure = "KD startup state $($Startup.State); log bytes=$($Startup.Length). $Tail"
    if ($null -ne $Process) {
        try {
            Stop-OwnedProcess $Process
        }
        catch {
            throw "$Failure. Cleanup failed; debugger ownership state was preserved: $($_.Exception.Message)"
        }
    }
    # Do not remove PID/start metadata until termination has been verified.
    # Stop-OwnedProcess throws on any cleanup failure, preserving ownership
    # evidence for manual recovery.
    try {
        Clear-DebuggerState $Paths -Strict
    }
    catch {
        throw "$Failure. KD termination was verified, but debugger state cleanup failed: $($_.Exception.Message)"
    }
    throw $Failure
}

function Stop-OwnedDebuggerIfPresent {
    param($Config)

    $Paths = Get-DebuggerPaths $Config
    if (Test-Path -LiteralPath $Paths.Pid -PathType Leaf) {
        $Process = Get-ConfiguredKdProcess $Config $Paths.Pid
        if ($null -eq $Process) {
            throw 'Debugger ownership state exists but the recorded process is absent; state was preserved for manual inspection.'
        }
        Stop-OwnedProcess $Process
        # Ownership files are removed only after the verified stop above.
        Clear-DebuggerState $Paths -Strict
    }
    # Never leave an untracked KD process behind after a VM transition.
    Assert-NoUnexpectedDebuggerProcesses -Config $Config
}

function Stop-ExactConfiguredVm {
    param($Config)

    Stop-OwnedDebuggerIfPresent $Config
    $Expected = [IO.Path]::GetFullPath([string]$Config.Vm.VmxPath)
    $Running = @(Get-RunningVmPaths $Config)
    if (-not ($Running | Where-Object {
            $_.Equals($Expected, [StringComparison]::OrdinalIgnoreCase)
        })) {
        return 'ALREADY_STOPPED'
    }

    Invoke-ConfiguredEncryptedVmrun -Config $Config -Operation Stop |
        Out-Null
    $Deadline = [DateTime]::UtcNow.AddSeconds(60)
    do {
        $Running = @(Get-RunningVmPaths $Config)
        if (-not ($Running | Where-Object {
                $_.Equals($Expected, [StringComparison]::OrdinalIgnoreCase)
            })) {
            return 'STOPPED'
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw 'The exact configured VM did not stop within 60 seconds.'
}

function Start-BootDebuggerConnection {
    param(
        $Config,
        [Parameter(Mandatory=$true)][string]$RunId
    )

    # vmrun has already created VMware's server pipe, but no mailbox or VM
    # visibility polling has delayed this attach.  Microsoft excludes VMware
    # from KD's resets=0/reconnect modes, so this is one immediate direct KD
    # process rather than a pre-boot listener or reconnecting client.
    $Plan = Read-HrRunState -Config $Config -RunId $RunId
    if ([string]$Plan.RunId -cne $RunId) {
        throw "Prepared run state does not match RunId: $RunId"
    }
    if (-not (Test-Path -LiteralPath $Plan.SymbolPath -PathType Container)) {
        throw "Prepared symbol directory is missing: $($Plan.SymbolPath)"
    }
    $Paths = Get-DebuggerPaths $Config
    $RunDirectory = Join-Path $Config.Mailbox.HostRoot "responses\$RunId"
    New-Item -ItemType Directory -Force -Path $RunDirectory | Out-Null
    $LogPath = Join-Path $RunDirectory 'windbg.log'
    $CommandFile = Join-Path $RunDirectory 'debugger-startup.txt'
    New-HrDebuggerCommandFile -Path $CommandFile -Commands @(
        ".echo HV_RUNNER_BEGIN_$RunId",
        'g'
    ) | Out-Null
    $KdArguments = Get-HrSingleKdArguments `
        -DebuggerPipe $Config.Vm.DebuggerPipe -RunId $RunId `
        -SymbolPath $Plan.SymbolPath -LogPath $LogPath `
        -CommandFile $CommandFile
    New-Item -ItemType Directory -Force -Path $Paths.Directory | Out-Null
    Assert-NoUnexpectedDebuggerProcesses -Config $Config
    if (Test-Path -LiteralPath $Paths.Pid) {
        $ExistingProcess = Get-ConfiguredKdProcess $Config $Paths.Pid
        if ($null -eq $ExistingProcess) {
            throw 'Debugger ownership state exists but the recorded process is absent; state was preserved for manual inspection.'
        }
        throw "KD process is already running with PID $($ExistingProcess.Id)."
    }
    Clear-DebuggerState $Paths
    Archive-PreviousDebuggerLogs $Paths
    $Process = $null
    try {
        # Keep every operation after process creation inside this protected
        # scope.  If PID/started bookkeeping fails, the process is cleaned up
        # before this action returns and ownership metadata is preserved until
        # termination is verified.
        $Process = Start-Process -FilePath $Config.Tools.KdPath `
            -ArgumentList $KdArguments -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput $Paths.StandardOutput `
            -RedirectStandardError $Paths.StandardError
        Set-Content -LiteralPath $Paths.Pid -Value $Process.Id -Encoding ascii
        Set-DebuggerStarted -Paths $Paths -Process $Process
        Start-Sleep -Milliseconds 500
        $Process.Refresh()
        Assert-DebuggerStartupResources -Config $Config -Paths $Paths `
            -Process $Process
        $Startup = Get-HrDebuggerStartupState `
            -StandardOutputPath $Paths.StandardOutput `
            -StandardErrorPath $Paths.StandardError `
            -ProcessExited $Process.HasExited `
            -MaximumLogBytes $Config.Debugger.MaximumStartupLogBytes `
            -StickyStatePath $Paths.Sticky
        if ($Startup.State -in @('FATAL', 'FLOOD', 'EXITED')) {
            Stop-DebuggerAfterStartupFailure $Paths $Process $Startup
        }
        $Deadline = [DateTime]::UtcNow.AddSeconds(
            [int]$Config.Debugger.ConnectionTimeoutSeconds)
        do {
            $Process.Refresh()
            Assert-DebuggerStartupResources -Config $Config -Paths $Paths `
                -Process $Process
            $Startup = Get-HrDebuggerStartupState `
                -StandardOutputPath $Paths.StandardOutput `
                -StandardErrorPath $Paths.StandardError `
                -ProcessExited $Process.HasExited `
                -MaximumLogBytes $Config.Debugger.MaximumStartupLogBytes `
                -StickyStatePath $Paths.Sticky
            $RunLogState = Get-HrDebuggerRunLogState -LogPath $LogPath `
                -RunId $RunId -MaximumLogBytes $Config.Debugger.MaximumRunLogBytes
            if ($RunLogState.State -in @('FATAL', 'FLOOD')) {
                $RunLogFailure = [pscustomobject]@{
                    State = $RunLogState.State
                    Length = $RunLogState.Length
                    Tail = $RunLogState.Tail
                }
                Stop-DebuggerAfterStartupFailure $Paths $Process $RunLogFailure
            }
            if ($Startup.State -ceq 'CONNECTED' -and
                $RunLogState.State -ceq 'READY') {
                Set-DebuggerReady $Paths $Process $RunId $LogPath
                Set-RunDebugLogReady $Config $RunId $Process $LogPath
                Assert-DebuggerHealth -Config $Config -RunId $RunId `
                    -RequireConnected | Out-Null
                return $Process
            }
            if ($Startup.State -in @('FATAL', 'FLOOD', 'EXITED')) {
                Stop-DebuggerAfterStartupFailure $Paths $Process $Startup
            }
            Start-Sleep -Milliseconds 250
        } while ([DateTime]::UtcNow -lt $Deadline)

        $TimedOut = [pscustomobject]@{
            State = 'TIMEOUT'
            Length = if ($null -eq $Startup) { 0 } else { $Startup.Length }
            Tail = if ($null -eq $Startup) { '' } else { $Startup.Tail }
        }
        Stop-DebuggerAfterStartupFailure $Paths $Process $TimedOut
    }
    catch {
        # Cleanup is restricted to the KD process launched by this helper.
        # The VM is deliberately left untouched for evidence preservation.
        $PrimaryFailure = $_.Exception.Message
        if ($null -eq $Process) {
            try {
                Clear-DebuggerState -Paths $Paths -Strict
            }
            catch {
                throw "KD startup failed: $PrimaryFailure. Debugger state cleanup also failed and was preserved: $($_.Exception.Message)"
            }
            throw
        }
        if (-not $Process.HasExited) {
            try {
                Stop-OwnedProcess $Process
            }
            catch {
                throw "KD startup failed: $PrimaryFailure. Cleanup failed; debugger ownership state was preserved: $($_.Exception.Message)"
            }
        }
        try {
            Clear-DebuggerState -Paths $Paths -Strict
        }
        catch {
            throw "KD startup failed: $PrimaryFailure. KD termination was verified, but debugger state cleanup failed: $($_.Exception.Message)"
        }
        throw
    }
}

function Wait-HrSupervisedResult {
    param(
        $Config,
        [Parameter(Mandatory=$true)][string]$RunId,
        [int]$TimeoutSeconds
    )

    if ($RunId -cnotmatch '^gate[0-9]+-[a-z0-9-]+-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw "RunId is not canonical: $RunId"
    }
    if ($TimeoutSeconds -le 0) {
        $TimeoutSeconds = [int]$Config.Policy.MaximumRuntimeSeconds + 60
    }
    $ResultPath = Join-Path $Config.Mailbox.HostRoot `
        "responses\$RunId\result.json"
    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $Deadline) {
        # Supervise the owned KD process on every mailbox poll.  A gate result
        # is accepted only while the debugger remains healthy.
        Assert-DebuggerHealth -Config $Config -RunId $RunId `
            -RequireConnected | Out-Null
        if (Test-Path -LiteralPath $ResultPath -PathType Leaf) {
            return Get-Content -Raw -LiteralPath $ResultPath | ConvertFrom-Json
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Timed out waiting for guest result. Preserve the VM; do not retry: $RunId"
}

$Config = Import-HrConfig $ConfigPath

switch ($Action) {
    'Doctor' {
        $VmxText = Get-Content -Raw -LiteralPath $Config.Vm.VmxPath
        if ($VmxText -notmatch '(?m)^displayName\s*=\s*"([^\"]+)"') {
            throw 'The VMX does not expose a displayName.'
        }
        $DisplayName = $Matches[1]
        if ($DisplayName -cne $Config.Vm.DisplayName) {
            throw "VM display-name mismatch: $DisplayName"
        }

        $Vmsd = [IO.Path]::ChangeExtension($Config.Vm.VmxPath, '.vmsd')
        if (-not (Test-Path -LiteralPath $Vmsd -PathType Leaf)) {
            throw "Snapshot metadata not found: $Vmsd"
        }
        $SnapshotText = Get-Content -Raw -LiteralPath $Vmsd
        $EscapedSnapshot = [Regex]::Escape([string]$Config.Vm.SnapshotName)
        if ($SnapshotText -notmatch "(?m)^snapshot[0-9]+\.displayName\s*=\s*`"$EscapedSnapshot`"\r?$") {
            throw "Configured snapshot is absent: $($Config.Vm.SnapshotName)"
        }

        $Encrypted = $VmxText -match '(?m)^vmx\.encryptionType\s*='
        $Running = @(Get-RunningVmPaths $Config)
        [pscustomobject]@{
            Result = 'PASS'
            Vm = $Config.Vm.VmxPath
            DisplayName = $DisplayName
            Snapshot = $Config.Vm.SnapshotName
            Encrypted = $Encrypted
            CredentialStored = Test-HrVmCredential `
                $Config.Credential.TargetName
            Running = [bool]($Running | Where-Object {
                $_.Equals(
                    [IO.Path]::GetFullPath([string]$Config.Vm.VmxPath),
                    [StringComparison]::OrdinalIgnoreCase)
            })
            Mailbox = $Config.Mailbox.HostRoot
            Kd = $Config.Tools.KdPath
            Vmrun = $Config.Tools.VmrunPath
        } | Format-List
        break
    }

    'CredentialStatus' {
        [pscustomobject]@{
            Result = 'PASS'
            TargetName = $Config.Credential.TargetName
            Stored = Test-HrVmCredential $Config.Credential.TargetName
        } | Format-List
        break
    }

    'StoreVmCredential' {
        $Credential = Get-Credential `
            -UserName $Config.Vm.DisplayName `
            -Message 'Enter the encrypted VMware test-VM password.'
        if ($null -eq $Credential) {
            throw 'VM credential entry was cancelled.'
        }
        try {
            Set-HrVmCredential -TargetName $Config.Credential.TargetName `
                -Password $Credential.Password
        }
        finally {
            $Credential.Password.Dispose()
        }
        [pscustomobject]@{
            Result = 'STORED'
            TargetName = $Config.Credential.TargetName
        } | Format-List
        break
    }

    'RemoveVmCredential' {
        if (-not $AuthorizeCredentialRemoval) {
            throw 'RemoveVmCredential requires -AuthorizeCredentialRemoval.'
        }
        $Removed = Remove-HrVmCredential $Config.Credential.TargetName
        [pscustomobject]@{
            Result = if ($Removed) { 'REMOVED' } else { 'ABSENT' }
            TargetName = $Config.Credential.TargetName
        } | Format-List
        break
    }

    'StartBroker' {
        $State = Start-HrBroker -Config $Config -SourceRoot $PSScriptRoot `
            -ConfigPath $ConfigPath
        [pscustomobject]@{
            Result = 'READY'
            ProcessId = $State.ProcessId
            Status = $State.Status
            StartedUtc = $State.StartedUtc
            SessionRoot = $State.SessionRoot
        } | Format-List
        break
    }

    'BrokerStatus' {
        $State = Get-HrBrokerState $Config
        if ($null -eq $State) {
            [pscustomobject]@{ Result = 'ABSENT' } | Format-List
        }
        else {
            [pscustomobject]@{
                Result = 'RUNNING'
                ProcessId = $State.ProcessId
                Status = $State.Status
                CurrentRunId = $State.CurrentRunId
                StartedUtc = $State.StartedUtc
            } | Format-List
        }
        break
    }

    'RunPreparedGate' {
        if ([string]::IsNullOrWhiteSpace($RunId)) {
            throw 'RunPreparedGate requires the prepared -RunId.'
        }
        # Fail before publishing to the elevated broker if the prepared state
        # is absent or malformed. The secured runner validates it again.
        $AllowedRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
        Assert-HrBrokerPreparedPlan -Config $Config -RunId $RunId `
            -AllowedRoot $AllowedRoot | Out-Null
        $Response = Invoke-HrBrokerPreparedGate -Config $Config `
            -RunId $RunId -TimeoutSeconds $TimeoutSeconds `
            -AuthorizeLiveVmRun:$AuthorizeLiveVmRun `
            -AuthorizeSnapshotRevert:$AuthorizeSnapshotRevert
        $Response | Format-List
        if ([string]$Response.Status -cne 'PASS') {
            throw "Broker gate result is $($Response.Status) during $($Response.Phase). Preserve the VM and debugger."
        }
        break
    }

    'PowerOffPreservedVm' {
        if (-not $AuthorizeVmPowerOff) {
            throw 'PowerOffPreservedVm requires -AuthorizeVmPowerOff.'
        }
        $Response = Invoke-HrBrokerPowerOffConfiguredVm -Config $Config `
            -TimeoutSeconds $TimeoutSeconds `
            -AuthorizeVmPowerOff
        $Response | Format-List
        if ([string]$Response.Status -cne 'PASS') {
            throw "Broker power-off result is $($Response.Status) during $($Response.Phase)."
        }
        break
    }

    'StopBroker' {
        Stop-HrBroker -Config $Config | Format-List
        break
    }

    'RevertSnapshot' {
        if (-not $AuthorizeSnapshotRevert) {
            throw 'RevertSnapshot requires -AuthorizeSnapshotRevert.'
        }
        Assert-CfosspeedStopped
        Stop-OwnedDebuggerIfPresent $Config
        Assert-ConfiguredVmStopped $Config
        Invoke-ConfiguredEncryptedVmrun -Config $Config `
            -Operation RevertSnapshot | Out-Null
        Assert-ConfiguredVmStopped $Config
        [pscustomobject]@{
            Result = 'REVERTED'
            Vm = $Config.Vm.VmxPath
            Snapshot = $Config.Vm.SnapshotName
        } | Format-List
        break
    }

    'StartVm' {
        if (-not $AuthorizeLiveVmRun) {
            throw 'StartVm requires -AuthorizeLiveVmRun.'
        }
        if ([string]::IsNullOrWhiteSpace($RunId)) {
            throw 'StartVm requires the prepared -RunId.'
        }
        $Plan = Read-HrRunState -Config $Config -RunId $RunId
        if (-not (Test-Path -LiteralPath $Plan.SymbolPath -PathType Container)) {
            throw "Prepared symbol directory is missing: $($Plan.SymbolPath)"
        }
        Assert-HostDebuggerElevation
        Assert-CfosspeedStopped
        # All read-only safety checks run before the first vmrun invocation.
        Assert-HostResourcePreflight -Config $Config | Out-Null
        Assert-ConfiguredVmStopped $Config
        $DebuggerProcess = $null
        try {
            Assert-ConfiguredVmStopped $Config
            Invoke-ConfiguredEncryptedVmrun -Config $Config `
                -Operation Start | Out-Null
            # VMware owns the pipe server. Start the ONE direct KD client
            # immediately after vmrun creates it, before VM visibility or
            # mailbox polling can miss the boot-time handshake.
            $DebuggerProcess = Start-BootDebuggerConnection $Config `
                -RunId $RunId
            $Started = $false
            $Deadline = [DateTime]::UtcNow.AddSeconds(60)
            do {
                try {
                    Assert-ConfiguredVmRunning $Config
                    $Started = $true
                }
                catch {
                    $Started = $false
                    Start-Sleep -Milliseconds 500
                }
            } while (-not $Started -and [DateTime]::UtcNow -lt $Deadline)
            if (-not $Started) {
                throw 'vmrun returned success, but the configured VM did not become visible within 60 seconds.'
            }
            # Only after the boot-time debugger handshake succeeds do we
            # publish the non-executing schema-zero mailbox readiness probe.
            $GuestMailboxProbe = Publish-GuestMailboxReadinessProbe -Config $Config
            $GuestMailbox = Wait-GuestMailboxReadiness -Config $Config `
                -Probe $GuestMailboxProbe
        }
        catch {
            $PrimaryFailure = $_.Exception.Message
            try {
                Stop-OwnedDebuggerIfPresent $Config
            }
            catch {
                throw "$PrimaryFailure. KD cleanup failed; ownership evidence was preserved: $($_.Exception.Message)"
            }
            throw
        }
        [pscustomobject]@{
            Result = 'STARTED'
            Vm = $Config.Vm.VmxPath
            Debugger = 'CONNECTED'
            DebuggerProcessId = $DebuggerProcess.Id
            RunId = $RunId
            GuestMailbox = $GuestMailbox.Result
            GuestMailboxStabilizationSeconds = $GuestMailbox.StabilizationSeconds
        } | Format-List
        break
    }

    'StopConfiguredVm' {
        if (-not $AuthorizeVmPowerOff) {
            throw 'StopConfiguredVm requires -AuthorizeVmPowerOff.'
        }
        # This fixed action is reserved for the already-elevated broker. It
        # accepts no caller-selected VM path and never restores a snapshot.
        Assert-HostDebuggerElevation
        $StopResult = Stop-ExactConfiguredVm $Config
        [pscustomobject]@{
            Result = $StopResult
            Vm = [IO.Path]::GetFullPath([string]$Config.Vm.VmxPath)
            SnapshotRestored = $false
        } | Format-List
        break
    }

    'Plan' {
        New-HrRunPlan -Config $Config -PackagePath $PackagePath `
            -ExpectedPackageSha256 $ExpectedPackageSha256 `
            -GateScript $GateScript -SymbolPath $SymbolPath -RunId $RunId |
            Format-List
        break
    }

    'Prepare' {
        $Plan = New-HrRunPlan -Config $Config -PackagePath $PackagePath `
            -ExpectedPackageSha256 $ExpectedPackageSha256 `
            -GateScript $GateScript -SymbolPath $SymbolPath -RunId $RunId
        $StatePath = Save-HrRunState -Config $Config -Plan $Plan
        [pscustomobject]@{
            Result = 'PREPARED'
            RunId = $Plan.RunId
            StatePath = $StatePath
            PackageSha256 = $Plan.PackageSha256
        } | Format-List
        break
    }

    'StartDebugger' {
        throw 'Standalone StartDebugger is disabled: StartVm must attach KD immediately after vmrun creates the VMware pipe.'
    }

    'WaitDebugger' {
        if ([string]::IsNullOrWhiteSpace($RunId)) {
            throw 'WaitDebugger requires the prepared -RunId.'
        }
        Assert-ConfiguredVmRunning $Config
        $Process = Assert-DebuggerHealth -Config $Config -RunId $RunId -RequireConnected
        [pscustomobject]@{
            Result = 'CONNECTED'
            ProcessId = $Process.Id
        } | Format-List
        break
    }

    'BeginDebugLog' {
        if ([string]::IsNullOrWhiteSpace($RunId)) {
            throw 'BeginDebugLog requires -RunId.'
        }
        $DebuggerProcess = Assert-DebuggerHealth -Config $Config `
            -RunId $RunId -RequireConnected
        # Logging and the run marker are established by the one KD process at
        # startup. This action is intentionally assertion-only and idempotent.
        Assert-RunDebugLogReady $Config $RunId $DebuggerProcess
        Write-Host "Debugger log already active for $RunId"
        break
    }

    'Submit' {
        if (-not $AuthorizeLiveVmRun) {
            throw 'Submit requires the explicit -AuthorizeLiveVmRun switch.'
        }
        if ([string]::IsNullOrWhiteSpace($RunId)) {
            throw 'Submit requires the prepared -RunId.'
        }
        Assert-ConfiguredVmRunning $Config
        $DebuggerProcess = Assert-DebuggerHealth -Config $Config `
            -RunId $RunId -RequireConnected
        Assert-RunDebugLogReady $Config $RunId $DebuggerProcess
        $Plan = Read-HrRunState -Config $Config -RunId $RunId
        if (-not (Test-Path -LiteralPath $Plan.PackagePath -PathType Leaf)) {
            throw "Prepared package is missing: $($Plan.PackagePath)"
        }
        $CurrentHash = Get-HrSha256 $Plan.PackagePath
        if ($CurrentHash -cne $Plan.PackageSha256) {
            throw "Prepared package changed: expected $($Plan.PackageSha256), actual $CurrentHash"
        }
        if (-not (Test-Path -LiteralPath $Plan.SymbolPath -PathType Container)) {
            throw "Prepared symbol directory is missing: $($Plan.SymbolPath)"
        }
        Publish-HrRequest -Config $Config -Plan $Plan | Format-List
        # Publishing is a one-way mailbox operation.  Revalidate ownership
        # and target health immediately afterward before reporting success.
        Assert-DebuggerHealth -Config $Config -RunId $RunId `
            -RequireConnected | Out-Null
        break
    }

    'Wait' {
        if ([string]::IsNullOrWhiteSpace($RunId)) {
            throw 'Wait requires -RunId.'
        }
        $Result = Wait-HrSupervisedResult -Config $Config -RunId $RunId `
            -TimeoutSeconds $TimeoutSeconds
        $Result | Format-List
        if ([string]$Result.Status -cne 'PASS') {
            throw "Gate result is $($Result.Status). Preserve the VM and debugger; do not retry."
        }
        break
    }

    'EndDebugLog' {
        if ([string]::IsNullOrWhiteSpace($RunId)) {
            throw 'EndDebugLog requires -RunId.'
        }
        $DebuggerProcess = Assert-DebuggerHealth -Config $Config `
            -RunId $RunId -RequireConnected
        Assert-RunDebugLogReady $Config $RunId $DebuggerProcess
        $LogDirectory = Join-Path $Config.Mailbox.HostRoot "responses\$RunId"
        $LogPath = Join-Path $LogDirectory 'windbg.log'
        # Stopping the exact owned process closes its -logo stream. Preserve
        # the log and clear ownership only after termination is verified.
        Stop-OwnedProcess $DebuggerProcess
        $RunLogState = Get-HrDebuggerRunLogState -LogPath $LogPath `
            -RunId $RunId -MaximumLogBytes $Config.Debugger.MaximumRunLogBytes
        if ($RunLogState.State -ne 'READY') {
            throw "The debugger log failed validation after KD termination: $LogPath"
        }
        Clear-DebuggerState (Get-DebuggerPaths $Config) -Strict
        Remove-Item -LiteralPath (Join-Path $LogDirectory `
            'debugger-log-ready.json') -Force -ErrorAction SilentlyContinue
        Write-Host "Debugger log closed for $RunId"
        break
    }
}
