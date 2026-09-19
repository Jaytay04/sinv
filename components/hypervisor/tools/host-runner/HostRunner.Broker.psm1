Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-HrBrokerPaths {
    param([Parameter(Mandatory=$true)]$Config)

    $Root = Join-Path ([IO.Path]::GetFullPath(
            [string]$Config.Mailbox.HostRoot)) 'broker'
    return [pscustomobject]@{
        Root = $Root
        State = Join-Path $Root 'state.json'
        Request = Join-Path $Root 'request.json'
        Active = Join-Path $Root 'active.json'
        Stop = Join-Path $Root 'stop.json'
        StartupError = Join-Path $Root 'startup-error.json'
        Responses = Join-Path $Root 'responses'
        Logs = Join-Path $Root 'logs'
    }
}

function Get-HrCurrentUserSid {
    return [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
}

function Test-HrBrokerRunId {
    param([Parameter(Mandatory=$true)][string]$RunId)

    return $RunId -cmatch `
        '^gate[0-9]+-[a-z0-9-]+-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$'
}

function Write-HrBrokerJsonAtomic {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)]$Value
    )

    $Directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    $Temporary = '{0}.{1}.tmp' -f $Path, [Guid]::NewGuid().ToString('N')
    try {
        $Value | ConvertTo-Json -Depth 8 -Compress |
            Set-Content -LiteralPath $Temporary -Encoding utf8
        Move-Item -LiteralPath $Temporary -Destination $Path -Force
    }
    finally {
        Remove-Item -LiteralPath $Temporary -Force -ErrorAction SilentlyContinue
    }
}

function Read-HrBrokerJson {
    param([Parameter(Mandatory=$true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    $Length = [long](Get-Item -LiteralPath $Path).Length
    if ($Length -le 0 -or $Length -gt 65536) {
        throw "Broker JSON has an invalid size: $Path"
    }
    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
}

function Test-HrBrokerStateProcess {
    param($State)

    if ($null -eq $State -or $State.SchemaVersion -ne 1 -or
        [string]$State.Status -notin @('READY', 'BUSY') -or
        [string]::IsNullOrWhiteSpace([string]$State.SessionNonce) -or
        [string]$State.SessionNonce -cnotmatch '^[a-f0-9]{32}$' -or
        [int]$State.ProcessId -le 0) {
        return $false
    }
    try {
        $Process = Get-Process -Id ([int]$State.ProcessId) -ErrorAction Stop
        return $Process.ProcessName -ceq 'powershell' -and
            $Process.StartTime.ToUniversalTime().ToString('o') -ceq
                [string]$State.ProcessStartedUtc
    }
    catch {
        return $false
    }
}

function Get-HrBrokerState {
    param([Parameter(Mandatory=$true)]$Config)

    $Paths = Get-HrBrokerPaths $Config
    $State = Read-HrBrokerJson $Paths.State
    if (-not (Test-HrBrokerStateProcess $State)) {
        return $null
    }
    return $State
}

function New-HrBrokerRunRequest {
    param(
        [Parameter(Mandatory=$true)]$State,
        [Parameter(Mandatory=$true)][string]$RunId,
        [switch]$AuthorizeLiveVmRun,
        [switch]$AuthorizeSnapshotRevert
    )

    if (-not (Test-HrBrokerRunId $RunId)) {
        throw "RunId is not canonical: $RunId"
    }
    if (-not $AuthorizeLiveVmRun -or -not $AuthorizeSnapshotRevert) {
        throw 'RunPreparedGate requires both -AuthorizeLiveVmRun and -AuthorizeSnapshotRevert.'
    }
    return [ordered]@{
        SchemaVersion = 1
        SessionNonce = [string]$State.SessionNonce
        RequestId = [Guid]::NewGuid().ToString('N')
        Action = 'RUN_PREPARED_GATE'
        RunId = $RunId
        RequestedBySid = Get-HrCurrentUserSid
        AuthorizeLiveVmRun = $true
        AuthorizeSnapshotRevert = $true
        CreatedUtc = [DateTime]::UtcNow.ToString('o')
    }
}

function Assert-HrBrokerRunRequest {
    param(
        [Parameter(Mandatory=$true)]$Request,
        [Parameter(Mandatory=$true)][string]$SessionNonce,
        [Parameter(Mandatory=$true)][string]$OwnerSid,
        [datetime]$NowUtc = [DateTime]::UtcNow
    )

    $Expected = @(
        'SchemaVersion', 'SessionNonce', 'RequestId', 'Action', 'RunId',
        'RequestedBySid', 'AuthorizeLiveVmRun',
        'AuthorizeSnapshotRevert', 'CreatedUtc')
    $Actual = @($Request.PSObject.Properties.Name)
    if ($Actual.Count -ne $Expected.Count -or
        @($Expected | Where-Object { $_ -cnotin $Actual }).Count -ne 0) {
        throw 'Broker request does not match the exact schema.'
    }
    if ($Request.SchemaVersion -ne 1 -or
        [string]$Request.SessionNonce -cne $SessionNonce -or
        [string]$Request.RequestId -cnotmatch '^[a-f0-9]{32}$' -or
        [string]$Request.Action -cne 'RUN_PREPARED_GATE' -or
        -not (Test-HrBrokerRunId ([string]$Request.RunId)) -or
        [string]$Request.RequestedBySid -cne $OwnerSid -or
        $Request.AuthorizeLiveVmRun -ne $true -or
        $Request.AuthorizeSnapshotRevert -ne $true) {
        throw 'Broker request authorization or identity is invalid.'
    }
    $CreatedUtc = [datetime]::MinValue
    if (-not [datetime]::TryParse(
            [string]$Request.CreatedUtc,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind,
            [ref]$CreatedUtc)) {
        throw 'Broker request timestamp is invalid.'
    }
    $Age = $NowUtc.ToUniversalTime() - $CreatedUtc.ToUniversalTime()
    if ($Age.TotalMinutes -gt 5 -or $Age.TotalMinutes -lt -1) {
        throw 'Broker request is stale or from the future.'
    }
    return $true
}

function New-HrBrokerPowerOffRequest {
    param(
        [Parameter(Mandatory=$true)]$State,
        [switch]$AuthorizeVmPowerOff
    )

    if (-not $AuthorizeVmPowerOff) {
        throw 'PowerOffPreservedVm requires -AuthorizeVmPowerOff.'
    }
    return [ordered]@{
        SchemaVersion = 1
        SessionNonce = [string]$State.SessionNonce
        RequestId = [Guid]::NewGuid().ToString('N')
        Action = 'POWER_OFF_CONFIGURED_VM'
        RequestedBySid = Get-HrCurrentUserSid
        AuthorizeVmPowerOff = $true
        CreatedUtc = [DateTime]::UtcNow.ToString('o')
    }
}

function Assert-HrBrokerPowerOffRequest {
    param(
        [Parameter(Mandatory=$true)]$Request,
        [Parameter(Mandatory=$true)][string]$SessionNonce,
        [Parameter(Mandatory=$true)][string]$OwnerSid,
        [datetime]$NowUtc = [DateTime]::UtcNow
    )

    $Expected = @('SchemaVersion', 'SessionNonce', 'RequestId', 'Action',
        'RequestedBySid', 'AuthorizeVmPowerOff', 'CreatedUtc')
    $Actual = @($Request.PSObject.Properties.Name)
    if ($Actual.Count -ne $Expected.Count -or
        @($Expected | Where-Object { $_ -cnotin $Actual }).Count -ne 0 -or
        $Request.SchemaVersion -ne 1 -or
        [string]$Request.SessionNonce -cne $SessionNonce -or
        [string]$Request.RequestId -cnotmatch '^[a-f0-9]{32}$' -or
        [string]$Request.Action -cne 'POWER_OFF_CONFIGURED_VM' -or
        [string]$Request.RequestedBySid -cne $OwnerSid -or
        $Request.AuthorizeVmPowerOff -ne $true) {
        throw 'Broker power-off request is invalid.'
    }
    $CreatedUtc = [datetime]::MinValue
    if (-not [datetime]::TryParse(
            [string]$Request.CreatedUtc,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind,
            [ref]$CreatedUtc)) {
        throw 'Broker power-off request timestamp is invalid.'
    }
    $Age = $NowUtc.ToUniversalTime() - $CreatedUtc.ToUniversalTime()
    if ($Age.TotalMinutes -gt 5 -or $Age.TotalMinutes -lt -1) {
        throw 'Broker power-off request is stale or from the future.'
    }
    return $true
}

function New-HrBrokerStopRequest {
    param([Parameter(Mandatory=$true)]$State)

    return [ordered]@{
        SchemaVersion = 1
        SessionNonce = [string]$State.SessionNonce
        RequestId = [Guid]::NewGuid().ToString('N')
        Action = 'STOP_BROKER'
        RequestedBySid = Get-HrCurrentUserSid
        CreatedUtc = [DateTime]::UtcNow.ToString('o')
    }
}

function Assert-HrBrokerStopRequest {
    param(
        [Parameter(Mandatory=$true)]$Request,
        [Parameter(Mandatory=$true)][string]$SessionNonce,
        [Parameter(Mandatory=$true)][string]$OwnerSid
    )

    $Expected = @('SchemaVersion', 'SessionNonce', 'RequestId', 'Action',
        'RequestedBySid', 'CreatedUtc')
    $Actual = @($Request.PSObject.Properties.Name)
    if ($Actual.Count -ne $Expected.Count -or
        @($Expected | Where-Object { $_ -cnotin $Actual }).Count -ne 0 -or
        $Request.SchemaVersion -ne 1 -or
        [string]$Request.SessionNonce -cne $SessionNonce -or
        [string]$Request.RequestId -cnotmatch '^[a-f0-9]{32}$' -or
        [string]$Request.Action -cne 'STOP_BROKER' -or
        [string]$Request.RequestedBySid -cne $OwnerSid) {
        throw 'Broker stop request is invalid.'
    }
    return $true
}

function Assert-HrBrokerPreparedPlan {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)][string]$AllowedRoot
    )

    $Prepared = Read-HrRunState -Config $Config -RunId $RunId
    $ExpectedProperties = @('SchemaVersion', 'RunId', 'GateScript',
        'PackagePath', 'PackageSha256', 'SymbolPath', 'TimeoutSeconds')
    $ActualProperties = @($Prepared.PSObject.Properties.Name)
    if ($ActualProperties.Count -ne $ExpectedProperties.Count -or
        @($ExpectedProperties | Where-Object {
            $_ -cnotin $ActualProperties
        }).Count -ne 0) {
        throw 'Prepared run state does not match the exact schema.'
    }

    $AllowedRoot = [IO.Path]::GetFullPath($AllowedRoot)
    $PackagePath = [IO.Path]::GetFullPath([string]$Prepared.PackagePath)
    $SymbolPath = [IO.Path]::GetFullPath([string]$Prepared.SymbolPath)
    if (-not (Test-HrPathWithin -Root $AllowedRoot -Path $PackagePath) -or
        -not (Test-HrPathWithin -Root $AllowedRoot -Path $SymbolPath)) {
        throw 'Prepared package and symbols must remain inside the active worktree.'
    }

    $Validated = New-HrRunPlan -Config $Config `
        -PackagePath $PackagePath `
        -ExpectedPackageSha256 ([string]$Prepared.PackageSha256) `
        -GateScript ([string]$Prepared.GateScript) `
        -SymbolPath $SymbolPath -RunId $RunId
    foreach ($Property in $ExpectedProperties) {
        if ([string]$Prepared.$Property -cne [string]$Validated.$Property) {
            throw "Prepared run state failed broker validation: $Property"
        }
    }

    $MailboxRoot = [IO.Path]::GetFullPath([string]$Config.Mailbox.HostRoot)
    $PublishedPaths = @(
        (Join-Path $MailboxRoot "artifacts\$RunId"),
        (Join-Path $MailboxRoot "requests\$RunId.json"),
        (Join-Path $MailboxRoot "responses\$RunId\result.json"))
    if (@($PublishedPaths | Where-Object {
            Test-Path -LiteralPath $_
        }).Count -ne 0) {
        throw 'The prepared RunId was already published or completed.'
    }
    return $Validated
}

function Start-HrBroker {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)][string]$SourceRoot,
        [Parameter(Mandatory=$true)][string]$ConfigPath
    )

    $Existing = Get-HrBrokerState $Config
    if ($null -ne $Existing) {
        return $Existing
    }
    $SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
    $ConfigPath = [IO.Path]::GetFullPath($ConfigPath)
    $Worker = Join-Path $SourceRoot 'Invoke-HypervisorHostBroker.ps1'
    foreach ($Path in @($Worker, $ConfigPath)) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "Broker startup file is missing: $Path"
        }
        if ($Path.Contains('"')) {
            throw "Broker startup path contains a quote: $Path"
        }
    }

    $Paths = Get-HrBrokerPaths $Config
    New-Item -ItemType Directory -Force -Path $Paths.Root,
        $Paths.Responses, $Paths.Logs | Out-Null
    foreach ($Stale in @($Paths.State, $Paths.Request, $Paths.Active,
            $Paths.Stop, $Paths.StartupError)) {
        Remove-Item -LiteralPath $Stale -Force -ErrorAction SilentlyContinue
    }

    $PowerShell = Join-Path $env:SystemRoot `
        'System32\WindowsPowerShell\v1.0\powershell.exe'
    $Arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -SourceRoot "{1}" -ConfigPath "{2}"' -f `
        $Worker, $SourceRoot, $ConfigPath
    try {
        Start-Process -FilePath $PowerShell -ArgumentList $Arguments `
            -Verb RunAs -WindowStyle Hidden | Out-Null
    }
    catch {
        throw "Elevated broker startup was cancelled or failed: $($_.Exception.Message)"
    }

    $Deadline = [DateTime]::UtcNow.AddSeconds(60)
    do {
        Start-Sleep -Milliseconds 250
        $State = Get-HrBrokerState $Config
        if ($null -ne $State) {
            return $State
        }
        if (Test-Path -LiteralPath $Paths.StartupError -PathType Leaf) {
            $Failure = Read-HrBrokerJson $Paths.StartupError
            throw "Elevated broker startup failed: $($Failure.Error)"
        }
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw 'The elevated broker did not become ready within 60 seconds.'
}

function Invoke-HrBrokerPreparedGate {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)][string]$RunId,
        [int]$TimeoutSeconds,
        [switch]$AuthorizeLiveVmRun,
        [switch]$AuthorizeSnapshotRevert
    )

    $State = Get-HrBrokerState $Config
    if ($null -eq $State) {
        throw 'The per-boot elevated broker is not running. Start it once with -Action StartBroker.'
    }
    if ([string]$State.Status -cne 'READY') {
        throw "The elevated broker is not idle: $($State.Status)"
    }
    if ([string]$State.OwnerSid -cne (Get-HrCurrentUserSid)) {
        throw 'The elevated broker belongs to a different Windows user.'
    }
    $Paths = Get-HrBrokerPaths $Config
    if ((Test-Path -LiteralPath $Paths.Request) -or
        (Test-Path -LiteralPath $Paths.Active)) {
        throw 'A broker request is already pending or active.'
    }
    $Request = New-HrBrokerRunRequest -State $State -RunId $RunId `
        -AuthorizeLiveVmRun:$AuthorizeLiveVmRun `
        -AuthorizeSnapshotRevert:$AuthorizeSnapshotRevert
    $ResponsePath = Join-Path $Paths.Responses `
        ("$($Request.RequestId).json")
    Remove-Item -LiteralPath $ResponsePath -Force -ErrorAction SilentlyContinue
    Write-HrBrokerJsonAtomic -Path $Paths.Request -Value $Request

    if ($TimeoutSeconds -le 0) {
        $TimeoutSeconds = [int]$Config.Policy.MaximumRuntimeSeconds + 600
    }
    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if (Test-Path -LiteralPath $ResponsePath -PathType Leaf) {
            return Read-HrBrokerJson $ResponsePath
        }
        if ($null -eq (Get-HrBrokerState $Config)) {
            throw 'The elevated broker exited before publishing a result. Preserve the VM and debugger.'
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "Timed out waiting for the elevated broker. Preserve the VM and debugger: $RunId"
}

function Invoke-HrBrokerPowerOffConfiguredVm {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [int]$TimeoutSeconds = 120,
        [switch]$AuthorizeVmPowerOff
    )

    $State = Get-HrBrokerState $Config
    if ($null -eq $State) {
        throw 'The per-boot elevated broker is not running. Start it once with -Action StartBroker.'
    }
    if ([string]$State.Status -cne 'READY') {
        throw "The elevated broker is not idle: $($State.Status)"
    }
    if ([string]$State.OwnerSid -cne (Get-HrCurrentUserSid)) {
        throw 'The elevated broker belongs to a different Windows user.'
    }
    if ($TimeoutSeconds -le 0) {
        $TimeoutSeconds = 120
    }
    elseif ($TimeoutSeconds -gt 300) {
        throw 'Power-off response timeout cannot exceed 300 seconds.'
    }

    $Paths = Get-HrBrokerPaths $Config
    if ((Test-Path -LiteralPath $Paths.Request) -or
        (Test-Path -LiteralPath $Paths.Active)) {
        throw 'A broker request is already pending or active.'
    }
    $Request = New-HrBrokerPowerOffRequest -State $State `
        -AuthorizeVmPowerOff:$AuthorizeVmPowerOff
    $ResponsePath = Join-Path $Paths.Responses `
        ("$($Request.RequestId).json")
    Remove-Item -LiteralPath $ResponsePath -Force -ErrorAction SilentlyContinue
    Write-HrBrokerJsonAtomic -Path $Paths.Request -Value $Request

    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if (Test-Path -LiteralPath $ResponsePath -PathType Leaf) {
            return Read-HrBrokerJson $ResponsePath
        }
        if ($null -eq (Get-HrBrokerState $Config)) {
            throw 'The elevated broker exited before publishing the power-off result.'
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw 'Timed out waiting for the configured VM to power off.'
}

function Stop-HrBroker {
    param([Parameter(Mandatory=$true)]$Config)

    $State = Get-HrBrokerState $Config
    if ($null -eq $State) {
        return [pscustomobject]@{ Result = 'ABSENT' }
    }
    if ([string]$State.Status -cne 'READY') {
        throw 'The broker is running a gate. It cannot be stopped until the gate finishes.'
    }
    $Paths = Get-HrBrokerPaths $Config
    Write-HrBrokerJsonAtomic -Path $Paths.Stop `
        -Value (New-HrBrokerStopRequest -State $State)
    $Deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 250
        if (-not (Test-HrBrokerStateProcess $State)) {
            return [pscustomobject]@{ Result = 'STOPPED' }
        }
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw 'The broker did not stop within 15 seconds.'
}

Export-ModuleMember -Function @(
    'Get-HrBrokerPaths',
    'Get-HrCurrentUserSid',
    'Test-HrBrokerRunId',
    'Write-HrBrokerJsonAtomic',
    'Read-HrBrokerJson',
    'Test-HrBrokerStateProcess',
    'Get-HrBrokerState',
    'New-HrBrokerRunRequest',
    'Assert-HrBrokerRunRequest',
    'New-HrBrokerPowerOffRequest',
    'Assert-HrBrokerPowerOffRequest',
    'New-HrBrokerStopRequest',
    'Assert-HrBrokerStopRequest',
    'Assert-HrBrokerPreparedPlan',
    'Start-HrBroker',
    'Invoke-HrBrokerPreparedGate',
    'Invoke-HrBrokerPowerOffConfiguredVm',
    'Stop-HrBroker'
)
