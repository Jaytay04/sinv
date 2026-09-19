[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourceRoot,
    [Parameter(Mandatory=$true)][string]$ConfigPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$ConfigPath = [IO.Path]::GetFullPath($ConfigPath)
Import-Module (Join-Path $SourceRoot 'HostRunner.Core.psm1') -Force
Import-Module (Join-Path $SourceRoot 'HostRunner.Broker.psm1') -Force

if (-not (Test-HrAdministrator)) {
    throw 'The host broker must run with an elevated Administrator token.'
}

function Protect-BrokerSessionRoot {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$OwnerSid
    )

    $Icacls = Join-Path $env:SystemRoot 'System32\icacls.exe'
    $AclOutput = @(& $Icacls $Path '/inheritance:r' '/grant:r' `
        '*S-1-5-18:(OI)(CI)(F)' `
        '*S-1-5-32-544:(OI)(CI)(F)' `
        "*${OwnerSid}:(OI)(CI)(RX)" '/T' '/C' 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not protect the broker session ACL: $($AclOutput -join '; ')"
    }
    $OwnerOutput = @(& $Icacls $Path '/setowner' '*S-1-5-18' `
        '/T' '/C' 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not assign the broker session owner: $($OwnerOutput -join '; ')"
    }
}

function Write-BrokerLog {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Message
    )

    $Line = '{0} {1}' -f [DateTime]::UtcNow.ToString('o'), $Message
    Add-Content -LiteralPath $Path -Value $Line -Encoding utf8
}

function Invoke-SecuredRunnerAction {
    param(
        [Parameter(Mandatory=$true)][string]$RunnerPath,
        [Parameter(Mandatory=$true)][string]$SecuredConfigPath,
        [Parameter(Mandatory=$true)][string]$Action,
        [Parameter(Mandatory=$true)][string]$LogPath,
        [string]$RunId,
        [int]$TimeoutSeconds,
        [switch]$AuthorizeLiveVmRun,
        [switch]$AuthorizeVmPowerOff,
        [switch]$AuthorizeSnapshotRevert
    )

    $RunnerParameters = @{
        Action = $Action
        ConfigPath = $SecuredConfigPath
    }
    if (-not [string]::IsNullOrWhiteSpace($RunId)) {
        $RunnerParameters.RunId = $RunId
    }
    if ($TimeoutSeconds -gt 0) {
        $RunnerParameters.TimeoutSeconds = $TimeoutSeconds
    }
    if ($AuthorizeLiveVmRun) {
        $RunnerParameters.AuthorizeLiveVmRun = $true
    }
    if ($AuthorizeVmPowerOff) {
        $RunnerParameters.AuthorizeVmPowerOff = $true
    }
    if ($AuthorizeSnapshotRevert) {
        $RunnerParameters.AuthorizeSnapshotRevert = $true
    }

    Write-BrokerLog -Path $LogPath -Message "BEGIN $Action"
    & $RunnerPath @RunnerParameters 2>&1 | ForEach-Object {
        $Line = $_
        Write-BrokerLog -Path $LogPath -Message ([string]$Line)
    }
    Write-BrokerLog -Path $LogPath -Message "END $Action"
}

function Set-BrokerState {
    param(
        [Parameter(Mandatory=$true)][string]$Status,
        [string]$RunId
    )

    $Value = [ordered]@{
        SchemaVersion = 1
        Status = $Status
        ProcessId = $PID
        ProcessStartedUtc = $ProcessStartedUtc
        SessionNonce = $SessionNonce
        OwnerSid = $OwnerSid
        SessionRoot = $SessionRoot
        ManifestSha256 = $ManifestSha256
        CurrentRunId = $RunId
        StartedUtc = $BrokerStartedUtc
        UpdatedUtc = [DateTime]::UtcNow.ToString('o')
    }
    Write-HrBrokerJsonAtomic -Path $Paths.State -Value $Value
}

$Mutex = [Threading.Mutex]::new($false, 'Local\HypersinvHostRunnerBroker')
$OwnsMutex = $false
$SessionRoot = $null
$Paths = $null
$SessionNonce = $null
$StatePublished = $false
try {
    $OwnsMutex = $Mutex.WaitOne(0)
    if (-not $OwnsMutex) {
        throw 'Another host broker is already running in this logon session.'
    }

    $Config = Import-HrConfig $ConfigPath
    $Paths = Get-HrBrokerPaths $Config
    New-Item -ItemType Directory -Force -Path $Paths.Root,
        $Paths.Responses, $Paths.Logs | Out-Null
    $OwnerSid = Get-HrCurrentUserSid
    $SessionNonce = [Guid]::NewGuid().ToString('N')
    $BrokerStartedUtc = [DateTime]::UtcNow.ToString('o')
    $ProcessStartedUtc = (Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')

    $ProgramDataRoot = Join-Path $env:ProgramData `
        'HypersinvHostRunner\Sessions'
    New-Item -ItemType Directory -Force -Path $ProgramDataRoot | Out-Null
    $SessionRoot = Join-Path $ProgramDataRoot `
        ("session-$PID-$SessionNonce")
    New-Item -ItemType Directory -Path $SessionRoot | Out-Null
    # Secure the empty directory first. Every later copy inherits usable
    # access entries from this root. Applying inheritance-only entries with
    # /T after files exist makes those entries inapplicable to the files
    # themselves and locks the elevated broker out of its own snapshot.
    Protect-BrokerSessionRoot -Path $SessionRoot -OwnerSid $OwnerSid

    $RequiredFiles = @(
        'Invoke-HypervisorHostRunner.ps1',
        'HostRunner.Core.psm1',
        'HostRunner.CredentialManager.psm1',
        'HostRunner.Vmware.psm1',
        'HostRunner.Debugger.psm1',
        'HostRunner.Broker.psm1')
    foreach ($Name in $RequiredFiles) {
        $Source = Join-Path $SourceRoot $Name
        if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
            throw "Required broker source is missing: $Source"
        }
        Copy-Item -LiteralPath $Source -Destination $SessionRoot
    }
    $SecuredConfigPath = Join-Path $SessionRoot 'host-runner.config.json'
    Copy-Item -LiteralPath $ConfigPath -Destination $SecuredConfigPath

    $ManifestPath = Join-Path $SessionRoot 'manifest.json'
    $Manifest = [ordered]@{
        SchemaVersion = 1
        SourceRoot = $SourceRoot
        OwnerSid = $OwnerSid
        CreatedUtc = $BrokerStartedUtc
        Files = @(Get-ChildItem -LiteralPath $SessionRoot -File |
            Sort-Object Name | ForEach-Object {
                [ordered]@{
                    Name = $_.Name
                    Sha256 = (Get-FileHash -LiteralPath $_.FullName `
                        -Algorithm SHA256).Hash
                }
            })
    }
    $Manifest | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $ManifestPath -Encoding utf8
    $ManifestSha256 = (Get-FileHash -LiteralPath $ManifestPath `
        -Algorithm SHA256).Hash

    $SecuredRunner = Join-Path $SessionRoot `
        'Invoke-HypervisorHostRunner.ps1'
    $SecuredConfig = Import-HrConfig $SecuredConfigPath
    foreach ($Stale in @($Paths.Request, $Paths.Active, $Paths.Stop)) {
        Remove-Item -LiteralPath $Stale -Force -ErrorAction SilentlyContinue
    }
    Set-BrokerState -Status READY
    $StatePublished = $true

    while ($true) {
        if (Test-Path -LiteralPath $Paths.Stop -PathType Leaf) {
            $StopRequest = Read-HrBrokerJson $Paths.Stop
            Assert-HrBrokerStopRequest -Request $StopRequest `
                -SessionNonce $SessionNonce -OwnerSid $OwnerSid | Out-Null
            Remove-Item -LiteralPath $Paths.Stop -Force
            break
        }

        if (-not (Test-Path -LiteralPath $Paths.Request -PathType Leaf)) {
            Start-Sleep -Milliseconds 250
            continue
        }
        if (Test-Path -LiteralPath $Paths.Active -PathType Leaf) {
            throw 'The broker found both pending and active requests.'
        }
        Move-Item -LiteralPath $Paths.Request -Destination $Paths.Active
        $Request = $null
        $RequestId = [Guid]::NewGuid().ToString('N')
        $RunId = $null
        $StartedUtc = [DateTime]::UtcNow.ToString('o')
        $Phase = 'ValidateRequest'
        $Status = 'FAIL'
        $PreserveVm = $true
        $ErrorText = $null
        $LogPath = Join-Path $Paths.Logs ("$RequestId.log")
        try {
            $Request = Read-HrBrokerJson $Paths.Active
            switch ([string]$Request.Action) {
                'RUN_PREPARED_GATE' {
                    Assert-HrBrokerRunRequest -Request $Request `
                        -SessionNonce $SessionNonce -OwnerSid $OwnerSid |
                        Out-Null
                    $RequestId = [string]$Request.RequestId
                    $RunId = [string]$Request.RunId
                    $LogPath = Join-Path $Paths.Logs ("$RequestId.log")
                    Set-BrokerState -Status BUSY -RunId $RunId
                    Write-BrokerLog -Path $LogPath `
                        -Message "RUN $RunId session=$SessionNonce"

                    $Phase = 'ValidatePreparedPlan'
                    $AllowedRoot = Split-Path -Parent `
                        (Split-Path -Parent $SourceRoot)
                    Assert-HrBrokerPreparedPlan -Config $SecuredConfig `
                        -RunId $RunId -AllowedRoot $AllowedRoot | Out-Null
                    $Phase = 'RevertSnapshot'
                    Invoke-SecuredRunnerAction -RunnerPath $SecuredRunner `
                        -SecuredConfigPath $SecuredConfigPath `
                        -Action RevertSnapshot -LogPath $LogPath `
                        -AuthorizeSnapshotRevert
                    $Phase = 'StartVm'
                    Invoke-SecuredRunnerAction -RunnerPath $SecuredRunner `
                        -SecuredConfigPath $SecuredConfigPath -Action StartVm `
                        -RunId $RunId -LogPath $LogPath -AuthorizeLiveVmRun
                    $Phase = 'BeginDebugLog'
                    Invoke-SecuredRunnerAction -RunnerPath $SecuredRunner `
                        -SecuredConfigPath $SecuredConfigPath `
                        -Action BeginDebugLog -RunId $RunId -LogPath $LogPath
                    $Phase = 'Submit'
                    Invoke-SecuredRunnerAction -RunnerPath $SecuredRunner `
                        -SecuredConfigPath $SecuredConfigPath -Action Submit `
                        -RunId $RunId -LogPath $LogPath -AuthorizeLiveVmRun
                    $Phase = 'Wait'
                    Invoke-SecuredRunnerAction -RunnerPath $SecuredRunner `
                        -SecuredConfigPath $SecuredConfigPath -Action Wait `
                        -RunId $RunId -LogPath $LogPath
                    $Phase = 'EndDebugLog'
                    Invoke-SecuredRunnerAction -RunnerPath $SecuredRunner `
                        -SecuredConfigPath $SecuredConfigPath `
                        -Action EndDebugLog -RunId $RunId -LogPath $LogPath
                    $Phase = 'StopVmAfterPass'
                    Invoke-SecuredRunnerAction -RunnerPath $SecuredRunner `
                        -SecuredConfigPath $SecuredConfigPath `
                        -Action StopConfiguredVm -LogPath $LogPath `
                        -AuthorizeVmPowerOff
                    $Phase = 'RestoreCleanSnapshot'
                    Invoke-SecuredRunnerAction -RunnerPath $SecuredRunner `
                        -SecuredConfigPath $SecuredConfigPath `
                        -Action RevertSnapshot -LogPath $LogPath `
                        -AuthorizeSnapshotRevert
                    $Phase = 'Complete'
                    $Status = 'PASS'
                    $PreserveVm = $false
                    break
                }
                'POWER_OFF_CONFIGURED_VM' {
                    Assert-HrBrokerPowerOffRequest -Request $Request `
                        -SessionNonce $SessionNonce -OwnerSid $OwnerSid |
                        Out-Null
                    $RequestId = [string]$Request.RequestId
                    $LogPath = Join-Path $Paths.Logs ("$RequestId.log")
                    Set-BrokerState -Status BUSY
                    Write-BrokerLog -Path $LogPath `
                        -Message "POWER_OFF_CONFIGURED_VM session=$SessionNonce"
                    $Phase = 'PowerOffConfiguredVm'
                    Invoke-SecuredRunnerAction -RunnerPath $SecuredRunner `
                        -SecuredConfigPath $SecuredConfigPath `
                        -Action StopConfiguredVm -LogPath $LogPath `
                        -AuthorizeVmPowerOff
                    $Phase = 'Complete'
                    $Status = 'PASS'
                    $PreserveVm = $false
                    break
                }
                default {
                    throw 'Broker request action is not recognized.'
                }
            }
        }
        catch {
            $ErrorText = [string]$_.Exception.Message
            if ($ErrorText.Length -gt 4096) {
                $ErrorText = $ErrorText.Substring(0, 4096)
            }
            Write-BrokerLog -Path $LogPath `
                -Message "FAIL phase=$Phase error=$ErrorText"
        }
        finally {
            $Response = [ordered]@{
                SchemaVersion = 1
                RequestId = $RequestId
                RunId = $RunId
                Status = $Status
                Phase = $Phase
                PreserveVm = $PreserveVm
                Error = $ErrorText
                BrokerLogPath = $LogPath
                StartedUtc = $StartedUtc
                CompletedUtc = [DateTime]::UtcNow.ToString('o')
            }
            $ResponsePath = Join-Path $Paths.Responses ("$RequestId.json")
            Write-HrBrokerJsonAtomic -Path $ResponsePath -Value $Response
            Remove-Item -LiteralPath $Paths.Active -Force `
                -ErrorAction SilentlyContinue
            Set-BrokerState -Status READY
        }
    }
}
catch {
    if ($null -ne $Paths) {
        try {
            Write-HrBrokerJsonAtomic -Path $Paths.StartupError -Value `
                ([ordered]@{
                    SchemaVersion = 1
                    ProcessId = $PID
                    Error = [string]$_.Exception.Message
                    FailedUtc = [DateTime]::UtcNow.ToString('o')
                })
        }
        catch { }
    }
    throw
}
finally {
    if ($StatePublished -and $null -ne $Paths) {
        try {
            $CurrentState = Read-HrBrokerJson $Paths.State
            if ($null -ne $CurrentState -and
                [string]$CurrentState.SessionNonce -ceq $SessionNonce) {
                Remove-Item -LiteralPath $Paths.State -Force `
                    -ErrorAction SilentlyContinue
            }
        }
        catch { }
    }
    if ($OwnsMutex) { $Mutex.ReleaseMutex() }
    $Mutex.Dispose()
    if (-not [string]::IsNullOrWhiteSpace($SessionRoot) -and
        (Test-Path -LiteralPath $SessionRoot -PathType Container)) {
        Remove-Item -LiteralPath $SessionRoot -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
