[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RunnerRoot = Split-Path -Parent $PSScriptRoot
$ModulePath = Join-Path $RunnerRoot 'HostRunner.Core.psm1'
Import-Module $ModulePath -Force
Import-Module (Join-Path $RunnerRoot 'HostRunner.CredentialManager.psm1') -Force
Import-Module (Join-Path $RunnerRoot 'HostRunner.Vmware.psm1') -Force
Import-Module (Join-Path $RunnerRoot 'HostRunner.Debugger.psm1') -Force
Import-Module (Join-Path $RunnerRoot 'HostRunner.Broker.psm1') -Force

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Throws([scriptblock]$Operation, [string]$Message) {
    try {
        & $Operation
    }
    catch {
        return
    }
    throw $Message
}

$MemoryState = Get-HrSystemMemoryState
Assert-True ($MemoryState.TotalPhysicalMb -gt 0 -and
        $MemoryState.AvailablePhysicalMb -gt 0 -and
        $MemoryState.CommitLimitMb -ge $MemoryState.CommitTotalMb -and
        $MemoryState.KernelTotalMb -ge $MemoryState.KernelPagedMb -and
        $MemoryState.KernelTotalMb -ge $MemoryState.KernelNonpagedMb) `
    'Native Windows memory telemetry returned an invalid snapshot.'
Assert-True ((Test-HrAdministrator) -is [bool]) `
    'Administrator-token detection did not return a Boolean.'

$BrokerStateFixture = [pscustomobject]@{
    SessionNonce = '0123456789abcdef0123456789abcdef'
}
$BrokerRequest = New-HrBrokerRunRequest -State $BrokerStateFixture `
    -RunId 'gate53-example-20260905-010203-0123456789' `
    -AuthorizeLiveVmRun -AuthorizeSnapshotRevert
$BrokerRequestObject = $BrokerRequest | ConvertTo-Json -Compress |
    ConvertFrom-Json
Assert-True (Assert-HrBrokerRunRequest -Request $BrokerRequestObject `
        -SessionNonce $BrokerStateFixture.SessionNonce `
        -OwnerSid (Get-HrCurrentUserSid)) `
    'Canonical broker request was rejected.'
Assert-True ($null -eq $BrokerRequestObject.PSObject.Properties['Command'] -and
        $null -eq $BrokerRequestObject.PSObject.Properties['Arguments'] -and
        $null -eq $BrokerRequestObject.PSObject.Properties['PackagePath']) `
    'Broker request exposes an arbitrary execution surface.'
Assert-Throws {
    New-HrBrokerRunRequest -State $BrokerStateFixture `
        -RunId 'gate53-example-20260905-010203-0123456789' `
        -AuthorizeLiveVmRun | Out-Null
} 'Broker request accepted only one live authorization.'
$BrokerRequestObject | Add-Member -NotePropertyName Command `
    -NotePropertyValue 'whoami'
Assert-Throws {
    Assert-HrBrokerRunRequest -Request $BrokerRequestObject `
        -SessionNonce $BrokerStateFixture.SessionNonce `
        -OwnerSid (Get-HrCurrentUserSid) | Out-Null
} 'Broker request accepted an extra command field.'
$BrokerRequestObject.PSObject.Properties.Remove('Command')
$BrokerRequestObject.SessionNonce = 'ffffffffffffffffffffffffffffffff'
Assert-Throws {
    Assert-HrBrokerRunRequest -Request $BrokerRequestObject `
        -SessionNonce $BrokerStateFixture.SessionNonce `
        -OwnerSid (Get-HrCurrentUserSid) | Out-Null
} 'Broker request accepted the wrong per-boot nonce.'

$PowerOffRequest = New-HrBrokerPowerOffRequest -State $BrokerStateFixture `
    -AuthorizeVmPowerOff
$PowerOffRequestObject = $PowerOffRequest | ConvertTo-Json -Compress |
    ConvertFrom-Json
Assert-True (Assert-HrBrokerPowerOffRequest `
        -Request $PowerOffRequestObject `
        -SessionNonce $BrokerStateFixture.SessionNonce `
        -OwnerSid (Get-HrCurrentUserSid)) `
    'Canonical broker power-off request was rejected.'
Assert-True ($null -eq $PowerOffRequestObject.PSObject.Properties['RunId'] -and
        $null -eq $PowerOffRequestObject.PSObject.Properties['VmxPath'] -and
        $null -eq $PowerOffRequestObject.PSObject.Properties['Command']) `
    'Broker power-off request exposes caller-selected execution state.'
Assert-Throws {
    New-HrBrokerPowerOffRequest -State $BrokerStateFixture | Out-Null
} 'Broker power-off request was accepted without explicit authorization.'
$PowerOffRequestObject | Add-Member -NotePropertyName VmxPath `
    -NotePropertyValue 'C:\VMs\other\other.vmx'
Assert-Throws {
    Assert-HrBrokerPowerOffRequest -Request $PowerOffRequestObject `
        -SessionNonce $BrokerStateFixture.SessionNonce `
        -OwnerSid (Get-HrCurrentUserSid) | Out-Null
} 'Broker power-off request accepted a caller-selected VM path.'
$PowerOffRequestObject.PSObject.Properties.Remove('VmxPath')
$PowerOffRequestObject.AuthorizeVmPowerOff = $false
Assert-Throws {
    Assert-HrBrokerPowerOffRequest -Request $PowerOffRequestObject `
        -SessionNonce $BrokerStateFixture.SessionNonce `
        -OwnerSid (Get-HrCurrentUserSid) | Out-Null
} 'Broker power-off request accepted a false authorization value.'

$Scripts = @(Get-ChildItem -LiteralPath $RunnerRoot -Recurse -File |
    Where-Object { $_.Extension -in @('.ps1', '.psm1') })
foreach ($Script in $Scripts) {
    $Tokens = $null
    $Errors = $null
    [Management.Automation.Language.Parser]::ParseFile(
        $Script.FullName,
        [ref]$Tokens,
        [ref]$Errors) | Out-Null
    Assert-True (@($Errors).Count -eq 0) "PowerShell parse failure: $($Script.FullName)"
}

Assert-True (Test-HrSafeGateName 'gate53-example.ps1') 'Canonical gate name rejected.'
foreach ($UnsafeName in @(
        '..\gate53-example.ps1',
        'Gate53-example.ps1',
        'gate53-example.ps1;whoami',
        'gate53_example.ps1')) {
    Assert-True (-not (Test-HrSafeGateName $UnsafeName)) `
        "Unsafe gate name accepted: $UnsafeName"
}

Assert-True (Test-HrCredentialTargetName `
        'HypersinvHostRunner/V3H-Gate4-MultiGroup') `
    'Canonical credential target rejected.'
foreach ($UnsafeTarget in @(
        'V3H-Gate4-MultiGroup',
        'HypersinvHostRunner/../other',
        'HypersinvHostRunner/name with spaces',
        'HypersinvHostRunner/name;whoami')) {
    Assert-True (-not (Test-HrCredentialTargetName $UnsafeTarget)) `
        "Unsafe credential target accepted: $UnsafeTarget"
}

$EmptyPassword = [Security.SecureString]::new()
try {
    Assert-Throws {
        Set-HrVmCredential -TargetName 'HypersinvHostRunner/fixture' `
            -Password $EmptyPassword
    } 'Credential Manager accepted an empty password.'
}
finally {
    $EmptyPassword.Dispose()
}

$OversizedPassword = [Security.SecureString]::new()
try {
    1..257 | ForEach-Object { $OversizedPassword.AppendChar('x') }
    Assert-Throws {
        Set-HrVmCredential -TargetName 'HypersinvHostRunner/fixture' `
            -Password $OversizedPassword
    } 'Credential Manager accepted a password larger than 512 bytes.'
}
finally {
    $OversizedPassword.Dispose()
}

$NulPassword = [Security.SecureString]::new()
try {
    $NulPassword.AppendChar('x')
    $NulPassword.AppendChar([char]0)
    Assert-Throws {
        Set-HrVmCredential -TargetName 'HypersinvHostRunner/fixture' `
            -Password $NulPassword
    } 'Credential Manager accepted a NUL-containing password.'
}
finally {
    $NulPassword.Dispose()
}

$FixtureVmx = 'C:\VMs\fixture\fixture.vmx'
$RevertTail = @(Get-HrVmrunOperationTail -Operation RevertSnapshot `
    -VmxPath $FixtureVmx -SnapshotName 'Gate6-HostRunner-Clean')
Assert-True (($RevertTail -join '|') -ceq `
        'revertToSnapshot|C:\VMs\fixture\fixture.vmx|Gate6-HostRunner-Clean') `
    'Snapshot-revert vmrun tail changed.'
$StartTail = @(Get-HrVmrunOperationTail -Operation Start `
    -VmxPath $FixtureVmx)
Assert-True (($StartTail -join '|') -ceq `
        'start|C:\VMs\fixture\fixture.vmx|nogui') `
    'VM-start vmrun tail changed or is no longer headless.'
$StopTail = @(Get-HrVmrunOperationTail -Operation Stop `
    -VmxPath $FixtureVmx)
Assert-True (($StopTail -join '|') -ceq `
        'stop|C:\VMs\fixture\fixture.vmx|hard') `
    'VM-stop vmrun tail changed or is no longer an exact hard power-off.'
Assert-Throws {
    Get-HrVmrunOperationTail -Operation RevertSnapshot `
        -VmxPath $FixtureVmx | Out-Null
} 'Snapshot revert accepted an empty snapshot name.'
$TestRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('hypersinv-host-runner-' + [Guid]::NewGuid().ToString('N'))
try {
    $Tools = Join-Path $TestRoot 'tools'
    $Mailbox = Join-Path $TestRoot 'mailbox'
    $Symbols = Join-Path $TestRoot 'symbols'
    $PackageDirectory = Join-Path $TestRoot 'package'
    foreach ($Directory in @($Tools, $Mailbox, $Symbols, $PackageDirectory)) {
        New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    }
    $Vmrun = Join-Path $Tools 'vmrun.exe'
    $Kd = Join-Path $Tools 'kd.exe'
    $Poolmon = Join-Path $Tools 'poolmon-fixture.ps1'
    $PoolTags = Join-Path $Tools 'pooltag.txt'
    $Vmx = Join-Path $TestRoot 'test.vmx'
    Set-Content -LiteralPath $Vmrun -Value 'fixture'
    Set-Content -LiteralPath $Kd -Value 'fixture'
    Set-Content -LiteralPath $PoolTags -Value 'Tag - fixture.sys'
    Set-Content -LiteralPath $Poolmon -Value @'
param(
    [string]$MapSwitch,
    [string]$TagPath,
    [string]$SnapshotSwitch,
    [string]$SnapshotPath
)
if ($MapSwitch -cne '-g' -or $SnapshotSwitch -cne '-n' -or
    -not (Test-Path -LiteralPath $TagPath -PathType Leaf)) {
    exit 7
}
Add-Content -LiteralPath "$SnapshotPath.calls" -Value 'call'
Set-Content -LiteralPath $SnapshotPath -Value 'fixture pool snapshot'
exit 0
'@
    Set-Content -LiteralPath $Vmx -Value 'displayName = "fixture"'
    Set-Content -LiteralPath (Join-Path $PackageDirectory 'payload.txt') `
        -Value 'fixture'

    $Package = Join-Path $TestRoot 'gate.zip'
    Compress-Archive -LiteralPath (Join-Path $PackageDirectory 'payload.txt') `
        -DestinationPath $Package
    $PackageHash = (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash
    $ConfigPath = Join-Path $TestRoot 'config.json'
    [ordered]@{
        SchemaVersion = 2
        Vm = [ordered]@{
            VmxPath = $Vmx
            DisplayName = 'fixture'
            SnapshotName = 'fixture'
            DebuggerPipe = '\\.\pipe\hypersinv_debug'
        }
        Tools = [ordered]@{
            VmrunPath = $Vmrun
            KdPath = $Kd
            PoolmonPath = $Poolmon
            PoolTagPath = $PoolTags
        }
        Mailbox = [ordered]@{ HostRoot = $Mailbox }
        Policy = [ordered]@{ MaximumRuntimeSeconds = 1800 }
        Debugger = [ordered]@{
            ConnectionTimeoutSeconds = 120
            MaximumStartupLogBytes = 262144
            MaximumRunLogBytes = 8388608
            HostMemoryReserveMb = 2048
            MaximumDebuggerWorkingSetMb = 512
            MinimumHostFreeMemoryMb = 2048
        }
        Credential = [ordered]@{
            TargetName = 'HypersinvHostRunner/fixture'
        }
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ConfigPath

    $Config = Import-HrConfig $ConfigPath
    Assert-True ($Config.Debugger.HostMemoryReserveMb -eq 2048) `
        'Configuration did not retain the host-memory reserve.'
    Assert-True ($Config.Debugger.MaximumDebuggerWorkingSetMb -eq 512) `
        'Configuration did not retain the debugger working-set limit.'
    Assert-True ($Config.Debugger.MinimumHostFreeMemoryMb -eq 2048) `
        'Configuration did not retain the minimum free-memory guard.'
    Assert-True ($Config.Debugger.HostMemoryBreachGraceSeconds -eq 2) `
        'Configuration did not inject the bounded memory-breach grace interval.'
    Assert-True ($Config.Debugger.PoolSnapshotTriggerNonpagedMb -eq 2048) `
        'Configuration did not inject the early nonpaged-pool snapshot threshold.'
    Assert-True (-not $Config.Debugger.StopAfterPoolSnapshot) `
        'Configuration did not default diagnostic stop-after-capture to false.'
    Assert-True ($Config.Debugger.MaximumRunLogBytes -eq 8388608) `
        'Configuration did not retain the per-run log ceiling.'
    Assert-True ($Config.Debugger.GuestMailboxReadinessTimeoutSeconds -eq 120 -and
            $Config.Debugger.GuestMailboxReadinessPollIntervalMilliseconds -eq 1000 -and
            $Config.Debugger.GuestMailboxStabilizationSeconds -eq 15) `
        'Guest-mailbox readiness defaults changed.'

    $InvalidPoolConfigPath = Join-Path $TestRoot 'invalid-pool-config.json'
    $InvalidPoolConfig = Get-Content -Raw -LiteralPath $ConfigPath |
        ConvertFrom-Json
    $InvalidPoolConfig.Tools.PoolTagPath = $null
    $InvalidPoolConfig | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $InvalidPoolConfigPath
    Assert-Throws {
        Import-HrConfig $InvalidPoolConfigPath | Out-Null
    } 'Configuration accepted Poolmon without its pool-tag map.'

    $InvalidTriggerConfigPath = Join-Path $TestRoot 'invalid-pool-trigger.json'
    $InvalidTriggerConfig = Get-Content -Raw -LiteralPath $ConfigPath |
        ConvertFrom-Json
    $InvalidTriggerConfig.Debugger | Add-Member `
        -NotePropertyName PoolSnapshotTriggerNonpagedMb -NotePropertyValue 999
    $InvalidTriggerConfig | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $InvalidTriggerConfigPath
    Assert-Throws {
        Import-HrConfig $InvalidTriggerConfigPath | Out-Null
    } 'Configuration accepted an unsafe nonpaged-pool snapshot threshold.'

    Set-Content -LiteralPath $Vmx -Value @(
        'displayName = "fixture"',
        'memsize = "4096"',
        'mks.enable3d = "FALSE"',
        'svga.graphicsMemoryKB = "262144"',
        'vmotion.svga.graphicsMemoryKB = "262144"',
        'serial0.present = "TRUE"',
        'serial0.fileType = "pipe"',
        'serial0.fileName = "\\.\pipe\hypersinv_debug"',
        'serial0.pipe.endPoint = "server"',
        'serial0.tryNoRxLoss = "FALSE"',
        'ethernet0.present = "FALSE"',
        'ethernet0.startConnected = "FALSE"'
    )
    Assert-True ((Get-HrVmxMemoryMb -VmxPath $Vmx) -eq 4096) `
        'VMX memory size was not parsed.'
    $NicState = Get-HrVmxNicState -VmxPath $Vmx
    Assert-True $NicState.Disconnected `
        'A VMX with an explicitly absent NIC was not accepted.'
    $GraphicsState = Get-HrVmxGraphicsState -VmxPath $Vmx
    Assert-True (-not $GraphicsState.Enable3d -and
            $GraphicsState.GraphicsMemoryMb -eq 256 -and
            $GraphicsState.CheckpointGraphicsMemoryMb -eq 256) `
        'A VMX with disabled 3D and 256 MB graphics memory was not parsed.'
    $SerialState = Get-HrVmxSerialPipeState -VmxPath $Vmx `
        -DebuggerPipe '\\.\pipe\hypersinv_debug'
    Assert-True $SerialState.Compatible `
        'A matching VMX serial server pipe was not accepted.'
    Set-Content -LiteralPath $Vmx -Value @(
        'displayName = "fixture"',
        'memsize = "4096"',
        'mks.enable3d = "TRUE"',
        'svga.graphicsMemoryKB = "8388608"',
        'vmotion.svga.graphicsMemoryKB = "8388608"',
        'serial0.present = "TRUE"',
        'serial0.fileType = "pipe"',
        'serial0.fileName = "\\.\pipe\hypersinv_debug"',
        'serial0.pipe.endPoint = "client"',
        'serial0.tryNoRxLoss = "TRUE"',
        'ethernet0.present = "TRUE"',
        'ethernet0.startConnected = "TRUE"'
    )
    Assert-True (-not (Get-HrVmxNicState -VmxPath $Vmx).Disconnected) `
        'A present and connected VMX NIC was accepted.'
    $UnsafeGraphicsState = Get-HrVmxGraphicsState -VmxPath $Vmx
    Assert-True ($UnsafeGraphicsState.Enable3d -and
            $UnsafeGraphicsState.GraphicsMemoryMb -eq 8192 -and
            $UnsafeGraphicsState.CheckpointGraphicsMemoryMb -eq 8192) `
        'An unsafe 8 GB/3D graphics configuration was not surfaced.'
    $UnsafeSerialState = Get-HrVmxSerialPipeState -VmxPath $Vmx `
        -DebuggerPipe '\\.\pipe\hypersinv_debug'
    Assert-True (-not $UnsafeSerialState.Compatible) `
        'An unsafe VMX serial endpoint/buffering mode was accepted for direct KD.'
    Set-Content -LiteralPath $Vmx -Value @(
        'serial0.present = "TRUE"',
        'serial0.fileType = "pipe"',
        'serial0.fileName = "\\.\pipe\hypersinv_debug"',
        'serial0.pipe.endPoint = "server"',
        'serial0.tryNoRxLoss = "TRUE"'
    )
    Assert-True (-not (Get-HrVmxSerialPipeState -VmxPath $Vmx `
                -DebuggerPipe '\\.\pipe\hypersinv_debug').Compatible) `
        'VMware lossless serial buffering was accepted for direct KD.'
    Set-Content -LiteralPath $Vmx -Value 'displayName = "fixture"'

    $DebuggerLog = Join-Path $TestRoot 'debugger-startup.log'
    $DebuggerErrorLog = Join-Path $TestRoot 'debugger-startup-error.log'
    $StickyState = Join-Path $TestRoot 'debugger-failure.state'
    Set-Content -LiteralPath $DebuggerErrorLog -Value '' -NoNewline
    Set-Content -LiteralPath $DebuggerLog -Value @(
        'Opened \\.\pipe\hypersinv_debug',
        'Kernel Debug Target Status: [no_debuggee]',
        'Waiting to reconnect...'
    )
    $DebuggerState = Get-HrDebuggerStartupState `
        -StandardOutputPath $DebuggerLog `
        -StandardErrorPath $DebuggerErrorLog -ProcessExited $false `
        -MaximumLogBytes 262144
    Assert-True ($DebuggerState.State -ceq 'DISCONNECTED') `
        'An initial no_debuggee marker was not classified as disconnected.'

    Set-Content -LiteralPath $DebuggerLog -Value (@(
        'kd: Could not write to pipe, 1450'
    ) + (1..100 | ForEach-Object { 'ordinary diagnostic output' }))
    $DebuggerState = Get-HrDebuggerStartupState `
        -StandardOutputPath $DebuggerLog `
        -StandardErrorPath $DebuggerErrorLog -ProcessExited $false `
        -MaximumLogBytes 262144 -StickyStatePath $StickyState
    Assert-True ($DebuggerState.State -ceq 'FATAL') `
        'A delayed fatal marker outside the bounded tail was missed.'
    Set-Content -LiteralPath $DebuggerLog -Value 'ordinary diagnostic output'
    $DebuggerState = Get-HrDebuggerStartupState `
        -StandardOutputPath $DebuggerLog `
        -StandardErrorPath $DebuggerErrorLog -ProcessExited $false `
        -MaximumLogBytes 262144 -StickyStatePath $StickyState
    Assert-True ($DebuggerState.State -ceq 'FATAL') `
        'A previously observed fatal debugger state was not sticky.'
    Remove-Item -LiteralPath $StickyState -Force

    Add-Content -LiteralPath $DebuggerLog -Value `
        'Connected to Windows 10 26100 x64 target at (fixture), ptr64 TRUE'
    $DebuggerState = Get-HrDebuggerStartupState `
        -StandardOutputPath $DebuggerLog `
        -StandardErrorPath $DebuggerErrorLog -ProcessExited $false `
        -MaximumLogBytes 262144
    Assert-True ($DebuggerState.State -ceq 'CONNECTED') `
        'The exact debugger connection marker was not recognized.'

    Add-Content -LiteralPath $DebuggerLog -Value @(
        'Kernel Debug Target Status: [no_debuggee]',
        'Waiting to reconnect...'
    )
    $DebuggerState = Get-HrDebuggerStartupState `
        -StandardOutputPath $DebuggerLog `
        -StandardErrorPath $DebuggerErrorLog -ProcessExited $false `
        -MaximumLogBytes 262144
    Assert-True ($DebuggerState.State -ceq 'DISCONNECTED') `
        'A disconnect after connection was accepted as connected.'

    Add-Content -LiteralPath $DebuggerLog -Value `
        'Connected to Windows 10 26100 x64 target at (fixture), ptr64 TRUE'
    $DebuggerState = Get-HrDebuggerStartupState `
        -StandardOutputPath $DebuggerLog `
        -StandardErrorPath $DebuggerErrorLog -ProcessExited $false `
        -MaximumLogBytes 262144
    Assert-True ($DebuggerState.State -ceq 'CONNECTED') `
        'A later connection after no_debuggee was not recognized.'

    Add-Content -LiteralPath $DebuggerLog -Value `
        'kd: Could not write to pipe, 1450'
    $DebuggerState = Get-HrDebuggerStartupState `
        -StandardOutputPath $DebuggerLog `
        -StandardErrorPath $DebuggerErrorLog -ProcessExited $false `
        -MaximumLogBytes 262144
    Assert-True ($DebuggerState.State -ceq 'FATAL') `
        'A fatal pipe error did not override a stale connection marker.'

    Set-Content -LiteralPath $DebuggerLog `
        -Value ([string]::new('x', 65537)) -NoNewline
    $DebuggerState = Get-HrDebuggerStartupState `
        -StandardOutputPath $DebuggerLog `
        -StandardErrorPath $DebuggerErrorLog -ProcessExited $false `
        -MaximumLogBytes 65536
    Assert-True ($DebuggerState.State -ceq 'FLOOD') `
        'An oversized debugger-startup log was accepted.'
    $BoundedTail = Get-HrBoundedLogTail -Path $DebuggerLog `
        -MaxLines 10 -MaxCharacters 128
    Assert-True ($BoundedTail.Length -eq 128) `
        'Debugger failure output was not bounded.'

    Set-Content -LiteralPath $DebuggerLog -Value 'Waiting to reconnect...'
    Set-Content -LiteralPath $DebuggerErrorLog -Value `
        'kd: Could not write to pipe, 1450'
    $DebuggerState = Get-HrDebuggerStartupState `
        -StandardOutputPath $DebuggerLog `
        -StandardErrorPath $DebuggerErrorLog -ProcessExited $false `
        -MaximumLogBytes 262144
    Assert-True ($DebuggerState.State -ceq 'FATAL') `
        'A fatal debugger stderr pipe error was accepted.'

    Set-Content -LiteralPath $DebuggerLog -Value '' -NoNewline
    Set-Content -LiteralPath $DebuggerErrorLog `
        -Value ([string]::new('x', 65537)) -NoNewline
    $DebuggerState = Get-HrDebuggerStartupState `
        -StandardOutputPath $DebuggerLog `
        -StandardErrorPath $DebuggerErrorLog -ProcessExited $false `
        -MaximumLogBytes 65536
    Assert-True ($DebuggerState.State -ceq 'FLOOD') `
        'An oversized debugger stderr log was accepted.'

    $HealthMarker = 'HV_RUNNER_HEALTH_gate53-fixture'
    Assert-True (-not (Test-HrDebuggerHealthOutput `
            -Text $HealthMarker -Marker $HealthMarker)) `
        'A local echo marker was accepted without a kernel target.'
    Assert-True (-not (Test-HrDebuggerHealthOutput `
            -Text "Kernel Debug Target Status: [no_debuggee]`n$HealthMarker" `
            -Marker $HealthMarker)) `
        'A no-debuggee response was accepted as a live kernel target.'
    $LiveHealth = @(
        'Windows 10 Kernel Version 26100 MP (4 procs) Free x64',
        'Product: WinNt, suite: TerminalServer SingleUserTS Personal',
        $HealthMarker
    ) -join "`n"
    Assert-True (Test-HrDebuggerHealthOutput `
            -Text $LiveHealth -Marker $HealthMarker) `
        'A live vertarget response was rejected.'
    $RunId = 'gate53-example-20260904-120000-0123456789'
    $Plan = New-HrRunPlan -Config $Config -PackagePath $Package `
        -ExpectedPackageSha256 $PackageHash -GateScript 'gate53-example.ps1' `
        -SymbolPath $Symbols -RunId $RunId
    $StatePath = Save-HrRunState -Config $Config -Plan $Plan
    Assert-True (Test-Path -LiteralPath $StatePath) 'Prepare did not persist state.'
    $BrokerValidatedPlan = Assert-HrBrokerPreparedPlan -Config $Config `
        -RunId $RunId -AllowedRoot $TestRoot
    Assert-True ($BrokerValidatedPlan.PackageSha256 -ceq $PackageHash) `
        'Broker did not revalidate the prepared package hash.'
    Assert-Throws {
        Assert-HrBrokerPreparedPlan -Config $Config -RunId $RunId `
            -AllowedRoot $PackageDirectory | Out-Null
    } 'Broker accepted prepared artifacts outside its active-worktree root.'

    $DebugDirectory = Join-Path $Mailbox "responses\$RunId"
    New-Item -ItemType Directory -Force -Path $DebugDirectory | Out-Null
    $CommandPath = Join-Path $DebugDirectory 'commands.txt'
    New-HrDebuggerCommandFile -Path $CommandPath -Commands @(
        ".echo HV_RUNNER_BEGIN_$RunId",
        'g'
    ) | Out-Null
    $CommandLines = @(Get-Content -LiteralPath $CommandPath)
    Assert-True ($CommandLines.Count -eq 2) 'Debugger commands were not line-separated.'
    Assert-True ($CommandLines[0] -ceq "`.echo HV_RUNNER_BEGIN_$RunId") `
        'The debugger run marker changed.'
    Assert-True ($CommandLines[1] -ceq 'g') `
        'The debugger startup command file does not resume the guest immediately.'

    $RunLogPath = Join-Path $DebugDirectory 'windbg.log'
    Set-Content -LiteralPath $RunLogPath -Value "HV_RUNNER_BEGIN_$RunId"
    $RunLogState = Get-HrDebuggerRunLogState -LogPath $RunLogPath `
        -RunId $RunId -MaximumLogBytes 262144
    Assert-True ($RunLogState.State -ceq 'READY' -and
            $RunLogState.MarkerFound -and -not $RunLogState.FatalFound) `
        'A valid per-run marker was not recognized.'
    Set-Content -LiteralPath $RunLogPath -Value @(
        'Kernel Debug Target Status: [no_debuggee]',
        "HV_RUNNER_BEGIN_$RunId"
    )
    $RunLogState = Get-HrDebuggerRunLogState -LogPath $RunLogPath `
        -RunId $RunId -MaximumLogBytes 262144
    Assert-True ($RunLogState.State -ceq 'READY' -and
            $RunLogState.MarkerFound -and -not $RunLogState.FatalFound) `
        'Historical no_debuggee output poisoned a connected run log.'
    Set-Content -LiteralPath $RunLogPath -Value 'ordinary diagnostic output'
    $RunLogState = Get-HrDebuggerRunLogState -LogPath $RunLogPath `
        -RunId $RunId -MaximumLogBytes 262144
    Assert-True ($RunLogState.State -ceq 'WAITING' -and
            -not $RunLogState.MarkerFound) `
        'A missing per-run marker was accepted.'
    Set-Content -LiteralPath $RunLogPath -Value 'kd: Could not write to pipe, 1450'
    $RunLogState = Get-HrDebuggerRunLogState -LogPath $RunLogPath `
        -RunId $RunId -MaximumLogBytes 262144
    Assert-True ($RunLogState.State -ceq 'FATAL' -and $RunLogState.FatalFound) `
        'A fatal per-run debugger error was not recognized.'
    Set-Content -LiteralPath $RunLogPath -Value @(
        "HV_RUNNER_BEGIN_$RunId",
        '*** Fatal System Error: 0x0000003b'
    )
    $RunLogState = Get-HrDebuggerRunLogState -LogPath $RunLogPath `
        -RunId $RunId -MaximumLogBytes 262144
    Assert-True ($RunLogState.State -ceq 'FATAL' -and
            $RunLogState.MarkerFound -and $RunLogState.FatalFound) `
        'A kernel bugcheck did not terminate the supervised debugger wait.'
    Set-Content -LiteralPath $RunLogPath `
        -Value ([string]::new('x', 65537)) -NoNewline
    $RunLogState = Get-HrDebuggerRunLogState -LogPath $RunLogPath `
        -RunId $RunId -MaximumLogBytes 65536
    Assert-True ($RunLogState.State -ceq 'FLOOD' -and
            $RunLogState.Length -gt 65536) `
        'An oversized per-run debugger log was accepted.'

    $KdArguments = @(Get-HrSingleKdArguments `
        -DebuggerPipe '\\.\pipe\hypersinv_debug' -RunId $RunId `
        -SymbolPath $Symbols -LogPath (Join-Path $DebugDirectory 'windbg.log') `
        -CommandFile $CommandPath)
    Assert-True (@($KdArguments | Where-Object { $_ -ceq '-k' }).Count -eq 1) `
        'Direct KD command line must contain exactly one -k.'
    Assert-True (($KdArguments -join '|') -ceq `
        ('-noshell|-b|-k|com:pipe,port=\\.\pipe\hypersinv_debug,baud=115200|-y|"' +
         $Symbols + '"|-logo|"' + (Join-Path $DebugDirectory 'windbg.log') + '"|-cfr|"' + $CommandPath + '"')) `
        'Direct KD command-line ordering/content changed.'
    Assert-True (($KdArguments -join '|') -notmatch 'resets=0|reconnect') `
        'VMware-incompatible KD reset/reconnect options were composed.'
    Assert-True (($KdArguments -join '|') -match '(^|\|)-b(\||$)' -and
            ($KdArguments -join '|') -notmatch '(^|\|)-bonc(\||$)' -and
            ($KdArguments -join '|') -notmatch '(^|\|)-secure(\||$)') `
        'KD must break when the connection is established and permit its pinned audit log.'

    $Published = Publish-HrRequest -Config $Config -Plan $Plan
    $Request = Get-Content -Raw -LiteralPath $Published.RequestPath |
        ConvertFrom-Json
    Assert-True ($null -eq $Request.PSObject.Properties['Command']) `
        'Request unexpectedly exposes a Command field.'
    Assert-True ($null -eq $Request.PSObject.Properties['Arguments']) `
        'Request unexpectedly exposes an Arguments field.'
    Assert-True ($Request.PackageSha256 -ceq $PackageHash) `
        'Published package hash changed.'
    Assert-Throws {
        Publish-HrRequest -Config $Config -Plan $Plan | Out-Null
    } 'Duplicate RunId was accepted.'
    Assert-Throws {
        New-HrRunPlan -Config $Config -PackagePath $Package `
            -ExpectedPackageSha256 ('0' * 64) `
            -GateScript 'gate53-example.ps1' -SymbolPath $Symbols
    } 'Package hash mismatch was accepted.'

    $RunnerPath = Join-Path $RunnerRoot 'Invoke-HypervisorHostRunner.ps1'
    Assert-Throws {
        & $RunnerPath -Action RevertSnapshot -ConfigPath $ConfigPath
    } 'Snapshot revert was accepted without explicit authorization.'
    Assert-Throws {
        & $RunnerPath -Action StartVm -ConfigPath $ConfigPath
    } 'VM start was accepted without explicit authorization.'
    Assert-Throws {
        & $RunnerPath -Action PowerOffPreservedVm -ConfigPath $ConfigPath
    } 'Brokered preserved-VM power-off was accepted without authorization.'
    Assert-Throws {
        & $RunnerPath -Action StopConfiguredVm -ConfigPath $ConfigPath
    } 'Secured configured-VM stop was accepted without authorization.'
    Assert-Throws {
        & $RunnerPath -Action RunPreparedGate -ConfigPath $ConfigPath `
            -RunId $Plan.RunId -AuthorizeLiveVmRun
    } 'Broker run was accepted without snapshot-revert authorization.'

    $ProductionScripts = @($Scripts | Where-Object {
        -not $_.FullName.StartsWith(
            $PSScriptRoot,
            [StringComparison]::OrdinalIgnoreCase)
    })
    $AllSource = ($ProductionScripts | ForEach-Object {
        Get-Content -Raw -LiteralPath $_.FullName
    }) -join "`n"
    foreach ($Forbidden in @(
            'Invoke-Expression',
            'ConvertTo-SecureString -AsPlainText',
            'cmdkey.exe',
            'deleteSnapshot',
            'vmrun.exe stop',
            'vmrun.exe reset')) {
        Assert-True ($AllSource -notmatch [Regex]::Escape($Forbidden)) `
            "Forbidden automation surface found: $Forbidden"
    }
    Assert-True ($AllSource -notmatch 'CheckToolsState|checkToolsState') `
        'The obsolete VMware Tools readiness operation remains in production code.'
    $RunnerSource = Get-Content -Raw -LiteralPath $RunnerPath
    $RunnerTokens = $null
    $RunnerErrors = $null
    $RunnerAst = [Management.Automation.Language.Parser]::ParseFile(
        $RunnerPath,
        [ref]$RunnerTokens,
        [ref]$RunnerErrors)
    $StopConfiguredVmAst = $RunnerAst.Find({
        param($Node)
        $Node -is [Management.Automation.Language.FunctionDefinitionAst] -and
            $Node.Name -ceq 'Stop-ExactConfiguredVm'
    }, $true)
    Assert-True ($null -ne $StopConfiguredVmAst) `
        'The exact configured-VM stop function was not found.'
    Invoke-Expression $StopConfiguredVmAst.Extent.Text
    $script:StopFixtureRunning = @('C:\VMs\other\other.vmx')
    $script:StopFixtureInvocations = 0
    function Stop-OwnedDebuggerIfPresent { param($Config) }
    function Get-RunningVmPaths { param($Config); return $script:StopFixtureRunning }
    function Invoke-ConfiguredEncryptedVmrun {
        param($Config, [string]$Operation)
        if ($Operation -cne 'Stop' -or
            [string]$Config.Vm.VmxPath -cne 'C:\VMs\fixture\fixture.vmx') {
            throw 'Stop fixture received a noncanonical operation or VM path.'
        }
        $script:StopFixtureInvocations++
        $script:StopFixtureRunning = @('C:\VMs\other\other.vmx')
    }
    $StopFixtureConfig = [pscustomobject]@{
        Vm = [pscustomobject]@{ VmxPath = 'C:\VMs\fixture\fixture.vmx' }
    }
    $AbsentStop = Stop-ExactConfiguredVm $StopFixtureConfig
    Assert-True ($AbsentStop -ceq 'ALREADY_STOPPED' -and
            $script:StopFixtureInvocations -eq 0) `
        'Configured-VM stop touched an unrelated running VM.'
    $script:StopFixtureRunning = @(
        'C:\VMs\fixture\fixture.vmx',
        'C:\VMs\other\other.vmx')
    $ExactStop = Stop-ExactConfiguredVm $StopFixtureConfig
    Assert-True ($ExactStop -ceq 'STOPPED' -and
            $script:StopFixtureInvocations -eq 1 -and
            $script:StopFixtureRunning.Count -eq 1 -and
            $script:StopFixtureRunning[0] -ceq 'C:\VMs\other\other.vmx') `
        'Configured-VM stop did not isolate the exact pinned VM path.'
    $ReadMailboxJsonAst = $RunnerAst.Find({
        param($Node)
        $Node -is [Management.Automation.Language.FunctionDefinitionAst] -and
            $Node.Name -ceq 'Read-GuestMailboxJson'
    }, $true)
    Assert-True ($null -ne $ReadMailboxJsonAst) `
        'The bounded guest-mailbox JSON reader was not found.'
    Invoke-Expression $ReadMailboxJsonAst.Extent.Text
    $PoolSnapshotAst = $RunnerAst.Find({
        param($Node)
        $Node -is [Management.Automation.Language.FunctionDefinitionAst] -and
            $Node.Name -ceq 'Invoke-DebuggerPoolSnapshotIfNeeded'
    }, $true)
    Assert-True ($null -ne $PoolSnapshotAst) `
        'The one-shot nonpaged-pool snapshot function was not found.'
    Invoke-Expression $PoolSnapshotAst.Extent.Text

    $PoolDiagnosticRoot = Join-Path $TestRoot 'pool-diagnostic'
    New-Item -ItemType Directory -Path $PoolDiagnosticRoot | Out-Null
    $PoolPaths = [pscustomobject]@{
        PoolSnapshot = Join-Path $PoolDiagnosticRoot 'pool.log'
        PoolSnapshotAttempted = Join-Path $PoolDiagnosticRoot 'pool.attempted'
        PoolSnapshotError = Join-Path $PoolDiagnosticRoot 'pool-error.txt'
    }
    $BelowState = Invoke-DebuggerPoolSnapshotIfNeeded -Config $Config `
        -Paths $PoolPaths -Memory ([pscustomobject]@{
            KernelNonpagedMb = 2047
            AvailablePhysicalMb = 8192
        })
    Assert-True ($BelowState -ceq 'BELOW_THRESHOLD' -and
            -not (Test-Path -LiteralPath $PoolPaths.PoolSnapshotAttempted)) `
        'Poolmon ran before the configured nonpaged-pool threshold.'
    $LowMemoryState = Invoke-DebuggerPoolSnapshotIfNeeded -Config $Config `
        -Paths $PoolPaths -Memory ([pscustomobject]@{
            KernelNonpagedMb = 4096
            AvailablePhysicalMb = 1024
        })
    Assert-True ($LowMemoryState -ceq 'SKIPPED_LOW_MEMORY' -and
            -not (Test-Path -LiteralPath $PoolPaths.PoolSnapshotAttempted)) `
        'Poolmon launched after host memory was already below the safety floor.'
    $CapturedState = Invoke-DebuggerPoolSnapshotIfNeeded -Config $Config `
        -Paths $PoolPaths -Memory ([pscustomobject]@{
            KernelNonpagedMb = 2048
            AvailablePhysicalMb = 8192
        })
    $RepeatedState = Invoke-DebuggerPoolSnapshotIfNeeded -Config $Config `
        -Paths $PoolPaths -Memory ([pscustomobject]@{
            KernelNonpagedMb = 4096
            AvailablePhysicalMb = 8192
        })
    $PoolCalls = @(Get-Content -LiteralPath "$($PoolPaths.PoolSnapshot).calls")
    Assert-True ($CapturedState -ceq 'CAPTURED' -and
            $RepeatedState -ceq 'ALREADY_ATTEMPTED' -and
            $PoolCalls.Count -eq 1 -and
            (Test-Path -LiteralPath $PoolPaths.PoolSnapshot -PathType Leaf)) `
        'The mapped Poolmon snapshot is not an exact one-shot capture.'
    $BomJsonPath = Join-Path $TestRoot 'utf8-bom-readiness.json'
    [ordered]@{ SchemaVersion = 0; RunId = 'fixture' } |
        ConvertTo-Json -Compress |
        Set-Content -LiteralPath $BomJsonPath -Encoding utf8
    $BomBytes = [IO.File]::ReadAllBytes($BomJsonPath)
    Assert-True ($BomBytes.Length -gt 3 -and $BomBytes[0] -eq 0xEF -and
            $BomBytes[1] -eq 0xBB -and $BomBytes[2] -eq 0xBF) `
        'The UTF-8 BOM fixture was not written as expected.'
    $BomJson = Read-GuestMailboxJson -Path $BomJsonPath
    Assert-True ($null -ne $BomJson -and $BomJson.SchemaVersion -eq 0 -and
            [string]$BomJson.RunId -ceq 'fixture') `
        'The bounded guest-mailbox JSON reader rejected a PowerShell UTF-8 BOM.'
    Assert-True ($RunnerSource -match `
            "RevertSnapshot requires -AuthorizeSnapshotRevert") `
        'Snapshot revert authorization guard is missing.'
    Assert-True ($RunnerSource -match `
            "StartVm requires -AuthorizeLiveVmRun") `
        'VM-start authorization guard is missing.'
    Assert-True ($RunnerSource -match `
            "PowerOffPreservedVm requires -AuthorizeVmPowerOff" -and
            $RunnerSource -match `
            "StopConfiguredVm requires -AuthorizeVmPowerOff") `
        'Configured-VM power-off authorization guards are missing.'
    Assert-True ($RunnerSource -match `
            'RunPreparedGate requires the prepared -RunId' -and
            $RunnerSource -match 'Invoke-HrBrokerPreparedGate') `
        'Per-boot broker gate action is missing or not prepared-state-only.'
    Assert-True ($RunnerSource -match "'WaitDebugger'") `
        'The debugger connection gate is missing.'
    Assert-True ($RunnerSource -match 'function Start-BootDebuggerConnection' -and
            $RunnerSource -notmatch `
            'function Start-PrebootDebuggerListener|function Wait-PrebootDebuggerConnection') `
        'The immediate post-vmrun direct-debugger lifecycle is missing.'
    Assert-True ($RunnerSource -match `
            "'StartDebugger' \{[\s\S]*attach KD immediately after vmrun") `
        'Unsafe standalone late attach is not rejected.'
    Assert-True ($RunnerSource -match `
            'Assert-RunDebugLogReady \$Config \$RunId') `
        'Gate submission does not require a per-run debugger handshake.'
    Assert-True ($RunnerSource -match `
            'if \(-not \$Process.WaitForExit\(15000\)\)') `
        'Owned debugger-process termination is not verified.'
    Assert-True ($RunnerSource -match `
            'Cannot verify ownership of debugger PID') `
        'Debugger process ownership does not fail closed.'
    Assert-True ($RunnerSource -match `
            'function Wait-HrSupervisedResult') `
        'Result wait does not have a supervised implementation.'
    Assert-True ($RunnerSource -match `
            'Wait-HrSupervisedResult[\s\S]*Assert-DebuggerHealth') `
        'Result wait does not poll debugger health.'
    Assert-True ($RunnerSource -match 'function Assert-DebuggerHealth') `
        'Reusable debugger health assertion is missing.'
    Assert-True ($RunnerSource -match `
            '\$RequireConnected\)[\s\S]*if \(\$Startup.State -ceq .DISCONNECTED.\)[\s\S]*Stop-DebuggerAfterStartupFailure') `
        'An explicit post-connection disconnect does not terminate the owned KD.'
    Assert-True ($RunnerSource -match `
            'Stop-OwnedProcess \$Process[\s\S]*Clear-DebuggerState \$Paths -Strict') `
        'Debugger ownership state can be cleared before verified termination.'
    Assert-True ($RunnerSource -match `
            'StartedPath = Join-Path[\s\S]*started\.json[\s\S]*ProcessStartedUtc[\s\S]*ExecutablePath') `
        'PID cleanup does not validate stored start metadata and executable path.'
    Assert-True ($RunnerSource -match `
            'function Set-DebuggerStarted[\s\S]*ExecutablePath = \$ExecutablePath[\s\S]*ProcessStartedUtc') `
        'Debugger startup does not persist executable path and process start time.'
    Assert-True ($RunnerSource -match `
            '\$Failure = "KD startup state[\s\S]*Cleanup failed; debugger ownership state was preserved') `
        'Startup cleanup failure is not reported together with the primary failure.'
    Assert-True ($RunnerSource -match `
            'Assert-DebuggerReady \$Config[\s\S]*Stop-DebuggerAfterStartupFailure \$Paths \$Process \$Failure') `
        'A failed connected-state assertion can leave the owned KD running.'
    Assert-True ($RunnerSource -match `
            'try \{[\s\S]*\$Process = Start-Process[\s\S]*Set-Content -LiteralPath \$Paths\.Pid[\s\S]*Set-DebuggerStarted') `
        'Post-Start-Process bookkeeping is not cleanup-protected.'
    Assert-True ($RunnerSource -notmatch 'catch \{\s*\}') `
        'Debugger cleanup failure is swallowed.'
    Assert-True ($RunnerSource -match 'failure\.state') `
        'Sticky debugger failure state is not persisted.'
    Assert-True ($RunnerSource -match 'MaximumDebuggerWorkingSetMb') `
        'Debugger working-set guard is missing.'
    Assert-True ($RunnerSource -match 'MinimumHostFreeMemoryMb') `
        'Runtime free-memory guard is missing.'
    Assert-True ($RunnerSource -match 'HostMemoryBreachGraceSeconds' -and
            $RunnerSource -match 'CriticalMb' -and
            $RunnerSource -match 'GraceDeadline') `
        'KD startup does not distinguish a transient soft-floor breach from a critical memory breach.'
    Assert-True ($RunnerSource -match 'kd-resource-samples\.csv' -and
            $RunnerSource -match 'PrivateMemorySize64' -and
            $RunnerSource -match 'HostAvailablePhysicalMb' -and
            $RunnerSource -match 'HostKernelNonpagedMb' -and
            $RunnerSource -match 'VmwareWorkingSetMb' -and
            $RunnerSource -match 'RunnerWorkingSetMb') `
        'KD startup does not retain bounded process/system memory diagnostics.'
    Assert-True ($RunnerSource -match 'pool-nonpaged-threshold\.log' -and
            $RunnerSource -match 'PoolSnapshotTriggerNonpagedMb' -and
            $RunnerSource -match '& \$PoolmonPath ''-g'' \$PoolTagPath ''-n''' -and
            $RunnerSource -match 'PoolSnapshotAttempted' -and
            $RunnerSource -match 'PoolSnapshotError' -and
            $RunnerSource -match 'StopAfterPoolSnapshot' -and
            $RunnerSource -match 'DIAGNOSTIC_CAPTURE') `
        'Early mapped Poolmon diagnostics are missing or not one-shot.'
    Assert-True ($RunnerSource -match 'WorkingSet64') `
        'Debugger working-set guard does not inspect the owned process.'
    Assert-True ($RunnerSource -match 'function Publish-GuestMailboxReadinessProbe' -and
            $RunnerSource -match 'function Wait-GuestMailboxReadiness') `
        'Guest-mailbox readiness gate is missing.'
    Assert-True ($RunnerSource -match 'SchemaVersion = 0' -and
            $RunnerSource -match 'Unsupported request schema: 0') `
        'Guest-mailbox readiness probe does not use the schema-zero rejection contract.'
    Assert-True ($RunnerSource -match '\$Temporary\s*=\s*\$RequestPath' -and
            $RunnerSource -match 'Move-Item -LiteralPath \$Temporary -Destination \$RequestPath') `
        'Guest-mailbox readiness probe is not published atomically.'
    Assert-True ($RunnerSource -match 'GuestMailboxReadinessTimeoutSeconds' -and
            $RunnerSource -match 'GuestMailboxReadinessPollIntervalMilliseconds' -and
            $RunnerSource -match 'GuestMailboxStabilizationSeconds') `
        'Guest-mailbox readiness is not bounded/configurable.'
    Assert-True ($RunnerSource -match 'ProcessingPath' -and
            $RunnerSource -match 'CompletedPath' -and
            $RunnerSource -match 'ResponseDirectory') `
        'Guest-mailbox readiness does not reject stale state.'
    $ReadinessBlock = [regex]::Match($RunnerSource,
        'function Publish-GuestMailboxReadinessProbe[\s\S]*?function Find-GuestMailboxReadinessResponse').Value
    Assert-True ($ReadinessBlock -notmatch '(?i)Package|Loader|Driver|\.exe') `
        'Readiness probe path contains gate execution fields.'
    Assert-True ($ReadinessBlock -notmatch 'Start-BootDebuggerConnection|Invoke-ConfiguredEncryptedVmrun|vmrun') `
        'Readiness probe performs live operations or starts KD.'
    $ReadinessWaitBlock = [regex]::Match($RunnerSource,
        'function Wait-GuestMailboxReadiness[\s\S]*?function Assert-CfosspeedStopped').Value
    Assert-True ($ReadinessWaitBlock -match 'Assert-HostFreeMemoryFloor' -and
        $ReadinessWaitBlock -match 'StabilizationDeadline') `
        'Guest-mailbox readiness/stabilization does not enforce host memory checks.'
    $ReadinessResponseBlock = [regex]::Match($RunnerSource,
        'function Find-GuestMailboxReadinessResponse[\s\S]*?function Wait-GuestMailboxReadiness').Value
    Assert-True ($ReadinessResponseBlock -match 'SchemaVersion -ne 1' -and
            $ReadinessResponseBlock -match 'Status.*REJECTED' -and
            $ReadinessResponseBlock -match 'Message.*Unsupported request schema: 0' -and
            $ReadinessResponseBlock -match 'Result\.RunId' -and
            $ReadinessResponseBlock -match 'Request\.RunId' -and
            $ReadinessResponseBlock -match 'RequestProperties\.Count -ne 2' -and
            $ReadinessResponseBlock -match 'ResultProperties\.Count -ne 5' -and
            $ReadinessResponseBlock -match 'CompletedUtc[\s\S]*PublishedUtc') `
        'Guest-mailbox readiness does not validate the exact response/request contract.'
    Assert-True ($ReadinessResponseBlock -match 'Probe\.ResultPath' -and
            $ReadinessResponseBlock -match 'Probe\.CompletedPath' -and
            $ReadinessResponseBlock -notmatch 'RejectedDirectory|request-\*\.json') `
        'Guest-mailbox readiness does not use the exact per-probe response/completed paths.'
    $CoreSource = Get-Content -Raw -LiteralPath `
        (Join-Path $RunnerRoot 'HostRunner.Core.psm1')
    Assert-True ($CoreSource -match 'GetPerformanceInfo' -and
            $CoreSource -match 'PhysicalAvailable' -and
            $RunnerSource -match 'Get-HrSystemMemoryState' -and
            $RunnerSource -notmatch 'Get-CimInstance Win32_OperatingSystem') `
        'Host-memory monitoring does not use native bounded system telemetry.'
    Assert-True ($RunnerSource -match 'Get-Process -Name .vmware-vmx.') `
        'Host-memory preflight does not reject another vmware-vmx process.'
    Assert-True ($RunnerSource -match 'Assert-NoUnexpectedDebuggerProcesses') `
        'Unexpected KD-process preflight is missing.'
    Assert-True ($RunnerSource -match 'Get-HrVmxGraphicsState' -and
            $RunnerSource -match 'GraphicsMemoryMb -gt 256' -and
            $RunnerSource -match 'CheckpointGraphicsMemoryMb -gt 256' -and
            $RunnerSource -match 'Disable 3D acceleration') `
        'Host preflight does not reject unsafe VM graphics allocation.'
    Assert-True ($RunnerSource -match 'Get-HrVmxSerialPipeState' -and
            $RunnerSource -match 'SerialPipeCompatible') `
        'Host preflight does not verify the exact VM serial pipe.'
    Assert-True ($RunnerSource -match 'Assert-CfosspeedStopped') `
        'cFosSpeed safety preflight is missing.'
    Assert-True ($RunnerSource -match 'function Assert-HostDebuggerElevation' -and
            $RunnerSource -match 'Test-HrAdministrator' -and
            $RunnerSource -match 'Microsoft requires elevation') `
        'Host debugger elevation is not enforced before live VM startup.'
    Assert-True ($RunnerSource -match 'will not rewrite the VMX') `
        'NIC remediation does not make the no-rewrite contract explicit.'
    Assert-True (($RunnerSource -split 'Publish-HrRequest').Count -gt 1 -and
            $RunnerSource -match `
            'Publish-HrRequest[\s\S]*Assert-DebuggerHealth[\s\S]*-RequireConnected') `
        'Post-publish debugger health assertion is missing.'

    $DebuggerSource = Get-Content -Raw -LiteralPath `
        (Join-Path $RunnerRoot 'HostRunner.Debugger.psm1')
    foreach ($ForbiddenDebuggerToken in @(
            '-server', '-remote', 'ServerPipe',
            'RemoteCommandTimeoutSeconds', 'Invoke-RemoteDebuggerClient',
            'remote health')) {
        Assert-True ($AllSource -notmatch [Regex]::Escape($ForbiddenDebuggerToken)) `
            "Forbidden debugger/remoting token found: $ForbiddenDebuggerToken"
    }
    Assert-True ($DebuggerSource -match 'function Get-HrSingleKdArguments') `
        'The pure single-KD argument composer is missing.'
    Assert-True (([regex]::Matches($DebuggerSource, "'-k'")).Count -eq 1) `
        'The debugger argument composer must contain exactly one -k.'
    Assert-True ($DebuggerSource -match "'-y'" -and
            $DebuggerSource -match "'-logo'" -and
            $DebuggerSource -match "'-cfr'" -and
            $DebuggerSource -notmatch "'-cf'") `
        'The debugger argument composer is missing required direct-mode arguments.'
    Assert-True ($DebuggerSource -match "'-b'" -and
            $DebuggerSource -notmatch "'-bonc'" -and
            $DebuggerSource -notmatch "'-secure'") `
        'KD startup does not break on connection or still suppresses the audit log.'
    Assert-True ($DebuggerSource -match 'Get-HrDebuggerRunLogState') `
        'Per-run log state scanner is missing.'
    Assert-True ($DebuggerSource -notmatch `
            'function Test-HrDebuggerRunLog[\s\S]*Get-Content -Raw') `
        'Per-run log validation must not read an unbounded complete file.'
    Assert-True ($DebuggerSource -notmatch `
            'function Get-HrDebuggerRunLogState[\s\S]*Get-Content -Raw') `
        'Per-run log scanning must remain streaming/bounded.'
    Assert-True ($DebuggerSource -notmatch 'Get-Content[^\r\n]*-Tail') `
        'Debugger log paths must not use unbounded line-tail reads.'
    $StartupScannerBlock = [regex]::Match($DebuggerSource,
        'function Get-HrDebuggerLogTextForScan[\s\S]*?function Get-HrDebuggerStartupState').Value
    Assert-True ($StartupScannerBlock -match '\$Stream\.Read\(' -and
            $StartupScannerBlock -match '\$Remaining\s*=\s*\$Length' -and
            $StartupScannerBlock -notmatch 'Get-Content -Raw') `
        'Startup log scanning must use fixed-size reads bounded by captured length.'
    $RunLogScannerBlock = [regex]::Match($DebuggerSource,
        'function Get-HrDebuggerRunLogState[\s\S]*?function ConvertTo-HrDebuggerPathArgument').Value
    Assert-True ($RunLogScannerBlock -match '\$Remaining\s*=\s*\$Length' -and
            $RunLogScannerBlock -match '\$Stream\.Read\(') `
        'Per-run log scanner does not enforce a captured byte bound.'
    Assert-True ($RunLogScannerBlock -notmatch 'ReadLine\(') `
        'Per-run log scanner must not follow a growing file to EOF.'
    Assert-True ($RunnerSource -match 'MaximumRunLogBytes') `
        'Per-run log byte ceiling is not enforced by the runner.'
    Assert-True ($RunnerSource -match "Get-HrSingleKdArguments") `
        'KD startup does not use the pure argument composer.'
    Assert-True ($RunnerSource.Contains('HV_RUNNER_BEGIN_$RunId')) `
        'KD startup does not emit the exact per-run marker.'
    $BootDebuggerBlock = [regex]::Match($RunnerSource,
        'function Start-BootDebuggerConnection[\s\S]*?function Wait-HrSupervisedResult').Value
    Assert-True ($BootDebuggerBlock -notmatch 'nt!|Kd_DEFAULT_Mask|Kd_IHVDRIVER_Mask' -and
            $BootDebuggerBlock -match `
            'New-HrDebuggerCommandFile[\s\S]*HV_RUNNER_BEGIN_\$RunId[\s\S]*''g''') `
        'Boot debugger startup still blocks on target-symbol edits before guest resume.'
    Assert-True ($RunnerSource -notmatch '(?i)Start-Process[\s\S]*-server|Start-Process[\s\S]*-remote') `
        'KD startup still exposes a debugger-server/remoting process.'

    $StartVmBlock = [regex]::Match($RunnerSource,
        "'StartVm' \{[\s\S]*?'Plan' \{").Value
    Assert-True ($StartVmBlock.Length -gt 0) 'StartVm action block was not found.'
    $StartCfosspeedIndex = $StartVmBlock.IndexOf('Assert-CfosspeedStopped')
    $StartElevationIndex = $StartVmBlock.IndexOf('Assert-HostDebuggerElevation')
    $StartStoppedIndex = $StartVmBlock.IndexOf('Assert-ConfiguredVmStopped')
    $StartVmrunIndex = $StartVmBlock.IndexOf('Invoke-ConfiguredEncryptedVmrun')
    $StartPreflightIndex = $StartVmBlock.IndexOf('Assert-HostResourcePreflight')
    $StartDebuggerIndex = $StartVmBlock.IndexOf('Start-BootDebuggerConnection')
    Assert-True ($StartElevationIndex -ge 0 -and
            $StartElevationIndex -lt $StartCfosspeedIndex -and
            $StartCfosspeedIndex -ge 0 -and
            $StartCfosspeedIndex -lt $StartStoppedIndex -and
            $StartCfosspeedIndex -lt $StartPreflightIndex -and
            $StartPreflightIndex -lt $StartStoppedIndex -and
            $StartPreflightIndex -lt $StartVmrunIndex) `
        'StartVm performs cFosSpeed detection after a vmrun call.'
    Assert-True ($StartDebuggerIndex -gt $StartVmrunIndex) `
        'StartVm does not attach KD immediately after vmrun starts the VM.'
    $FirstRunningIndex = $StartVmBlock.IndexOf('Assert-ConfiguredVmRunning')
    Assert-True ($FirstRunningIndex -gt $StartDebuggerIndex) `
        'VM visibility polling delays the direct KD attach.'
    Assert-True ($StartVmBlock -match `
            'Start-BootDebuggerConnection[\s\S]*Debugger = .CONNECTED.') `
        'StartVm does not require synchronous debugger connection.'
    $ProbePublishIndex = $StartVmBlock.IndexOf('Publish-GuestMailboxReadinessProbe')
    $ProbeWaitIndex = $StartVmBlock.IndexOf('Wait-GuestMailboxReadiness')
    Assert-True ($ProbePublishIndex -gt $StartDebuggerIndex -and
            $ProbeWaitIndex -gt $ProbePublishIndex -and
            $StartDebuggerIndex -gt $StartVmrunIndex) `
        'Mailbox readiness is not sequenced after the boot-time debugger handshake.'
    Assert-True ($StartVmBlock -match `
            'catch \{[\s\S]*Stop-OwnedDebuggerIfPresent') `
        'A VM-start or mailbox failure can leave the owned KD process running.'

    $StartDebuggerBlock = [regex]::Match($RunnerSource,
        "'StartDebugger' \{[\s\S]*?'WaitDebugger' \{").Value
    Assert-True ($StartDebuggerBlock -match 'Standalone StartDebugger is disabled' -and
            $StartDebuggerBlock -notmatch 'Start-Process|Start-BootDebuggerConnection|Invoke-ConfiguredEncryptedVmrun') `
        'Standalone StartDebugger can still perform a late attach or live operation.'

    $RevertBlock = [regex]::Match($RunnerSource,
        "'RevertSnapshot' \{[\s\S]*?'StartVm' \{").Value
    $RevertCfosspeedIndex = $RevertBlock.IndexOf('Assert-CfosspeedStopped')
    $RevertStoppedIndex = $RevertBlock.IndexOf('Assert-ConfiguredVmStopped')
    Assert-True ($RevertCfosspeedIndex -ge 0 -and
            $RevertCfosspeedIndex -lt $RevertStoppedIndex) `
        'RevertSnapshot performs cFosSpeed detection after a vmrun call.'

    $VmwareSource = Get-Content -Raw -LiteralPath `
        (Join-Path $RunnerRoot 'HostRunner.Vmware.psm1')
    Assert-True (([regex]::Matches(
                $VmwareSource, "'revertToSnapshot'")).Count -eq 1) `
        'Snapshot revert must have exactly one fixed vmrun operation token.'
    $ExpectedStartToken = "'start', " + '$VmxPath' + ", 'nogui'"
    Assert-True ($VmwareSource -match [Regex]::Escape($ExpectedStartToken)) `
        'VM start must remain headless.'
    $ExpectedStopToken = "'stop', " + '$VmxPath' + ", 'hard'"
    Assert-True ($VmwareSource -match [Regex]::Escape($ExpectedStopToken)) `
        'VM stop must remain an exact configured-path hard power-off.'
    Assert-True ($VmwareSource -notmatch `
            '(?i)Write-(Host|Output|Verbose|Debug).*Password') `
        'VMware module may expose the credential through output.'
    Assert-True ($VmwareSource -notmatch "'-vp'") `
        'Exported VMware helpers must not execute credential-bearing commands.'
    Assert-True ($VmwareSource -notmatch 'Get-HrVmCredential') `
        'Exported VMware helpers must not retrieve the stored credential.'
    Assert-True ($RunnerSource -match `
            'function Invoke-ConfiguredEncryptedVmrun') `
        'Credential-bearing vmrun execution escaped the guarded entry script.'
    $ExpectedCredentialPrefix = "@('-T', 'ws', '-vp', " + `
        '$PlainPassword' + ')'
    Assert-True ($RunnerSource -match `
            [Regex]::Escape($ExpectedCredentialPrefix)) `
        'Guarded vmrun invocation shape changed.'

    $DebuggerSource = Get-Content -Raw -LiteralPath `
        (Join-Path $RunnerRoot 'HostRunner.Debugger.psm1')
    Assert-True ($DebuggerSource -notmatch 'Stop-Process') `
        'The exported debugger classifier must remain side-effect free.'

    $BrokerModuleSource = Get-Content -Raw -LiteralPath `
        (Join-Path $RunnerRoot 'HostRunner.Broker.psm1')
    $BrokerWorkerSource = Get-Content -Raw -LiteralPath `
        (Join-Path $RunnerRoot 'Invoke-HypervisorHostBroker.ps1')
    Assert-True ($BrokerModuleSource -match '-Verb RunAs' -and
            $BrokerModuleSource -match '-WindowStyle Hidden' -and
            $BrokerModuleSource -notmatch `
                'Register-ScheduledTask|New-ScheduledTask|AtLogOn|AtStartup') `
        'Broker startup is not a one-UAC, current-boot-only process.'
    Assert-True ($BrokerModuleSource -match `
            "Action = 'RUN_PREPARED_GATE'" -and
            $BrokerModuleSource -match `
            "Action = 'POWER_OFF_CONFIGURED_VM'" -and
            $BrokerModuleSource -match `
                'Broker request does not match the exact schema' -and
            $BrokerModuleSource -match `
                'AuthorizeLiveVmRun.*AuthorizeSnapshotRevert') `
        'Broker request protocol is not exact and dual-authorized.'
    Assert-True ($BrokerWorkerSource -match `
            'Protect-BrokerSessionRoot' -and
            $BrokerWorkerSource -match '/inheritance:r' -and
            $BrokerWorkerSource -match '/setowner' -and
            $BrokerWorkerSource -match '\*S-1-5-18' -and
            $BrokerWorkerSource -match '\*S-1-5-32-544') `
        'Elevated broker code/config snapshot is not protected from standard-token edits.'
    $BrokerProtectIndex = $BrokerWorkerSource.LastIndexOf(
        'Protect-BrokerSessionRoot -Path $SessionRoot')
    $BrokerCopyIndex = $BrokerWorkerSource.IndexOf(
        'Copy-Item -LiteralPath $Source -Destination $SessionRoot')
    Assert-True ($BrokerProtectIndex -ge 0 -and
            $BrokerCopyIndex -gt $BrokerProtectIndex) `
        'Broker source snapshot is created before its directory is protected.'
    Assert-True ($BrokerWorkerSource -match `
            'param\([\s\S]*\$SourceRoot,[\s\S]*\$ConfigPath[\s\S]*\)\s*Set-StrictMode' -and
            $BrokerWorkerSource -notmatch `
                'Invoke-Expression|Start-Process|Register-ScheduledTask' -and
            $BrokerWorkerSource -match 'Assert-HrBrokerRunRequest') `
        'Elevated broker worker accepts a caller-controlled execution surface.'
    Assert-True ($BrokerWorkerSource -match `
            '\$RunnerParameters\s*=\s*@\{' -and
            $BrokerWorkerSource -match '& \$RunnerPath @RunnerParameters' -and
            $BrokerWorkerSource -notmatch '& \$RunnerPath @Arguments') `
        'Broker runner actions do not use named hashtable splatting.'
    $BrokerRevertIndex = $BrokerWorkerSource.IndexOf(
        "-Action RevertSnapshot")
    $BrokerStartIndex = $BrokerWorkerSource.IndexOf('-Action StartVm')
    $BrokerSubmitIndex = $BrokerWorkerSource.IndexOf('-Action Submit')
    $BrokerWaitIndex = $BrokerWorkerSource.IndexOf('-Action Wait')
    $BrokerEndIndex = $BrokerWorkerSource.IndexOf('-Action EndDebugLog')
    $BrokerStopIndex = $BrokerWorkerSource.IndexOf(
        '-Action StopConfiguredVm')
    $BrokerRestoreIndex = $BrokerWorkerSource.LastIndexOf(
        '-Action RevertSnapshot')
    Assert-True ($BrokerRevertIndex -ge 0 -and
            $BrokerStartIndex -gt $BrokerRevertIndex -and
            $BrokerSubmitIndex -gt $BrokerStartIndex -and
            $BrokerWaitIndex -gt $BrokerSubmitIndex -and
            $BrokerEndIndex -gt $BrokerWaitIndex -and
            $BrokerStopIndex -gt $BrokerEndIndex -and
            $BrokerRestoreIndex -gt $BrokerStopIndex) `
        'Broker orchestration does not preserve the fixed live-gate lifecycle.'
    $BrokerPowerOffBlock = [regex]::Match($BrokerWorkerSource,
        "'POWER_OFF_CONFIGURED_VM' \{[\s\S]*?default \{").Value
    Assert-True ($BrokerPowerOffBlock -match 'Assert-HrBrokerPowerOffRequest' -and
            $BrokerPowerOffBlock -match '-Action StopConfiguredVm' -and
            $BrokerPowerOffBlock -match '-AuthorizeVmPowerOff' -and
            $BrokerPowerOffBlock -notmatch 'RevertSnapshot|StartVm|Submit|Wait') `
        'Brokered preserved-VM recovery is not a stop-only exact action.'
    Assert-True ($BrokerWorkerSource -match `
            '\$PreserveVm = \$true' -and
            $BrokerWorkerSource -match `
                '\$Status = ''PASS''[\s\S]*\$PreserveVm = \$false') `
        'Broker failure is not preserve-by-default with cleanup only after PASS.'

    Write-Host 'PASS: host-runner static contracts verified.'
}
finally {
    if (Test-Path -LiteralPath $TestRoot) {
        Remove-Item -LiteralPath $TestRoot -Recurse -Force
    }
}
