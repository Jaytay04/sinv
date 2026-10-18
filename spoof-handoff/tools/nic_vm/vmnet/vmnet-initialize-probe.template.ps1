[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [ValidateSet('Probe')][string]$Action = 'Probe',
    [string]$ExpectedProbeVerdict = 'NOT_MET'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:Result = [ordered]@{
    SchemaVersion = 1
    RunId = Split-Path -Leaf $RunRoot
    Status = 'FAIL'
    Message = 'VMnet miniport initialization probe did not complete.'
    ExpectedProbeVerdict = $ExpectedProbeVerdict
}
$script:Record = [ordered]@{
    schema = 'vmnet-miniport-init.v1'
    recordedUtc = [DateTime]::UtcNow.ToString('o')
    vmIdentity = $null
    preexistingTargets = @()
    packageInventoryBefore = $null
    installCommands = @()
    targetDevices = @()
    driverServices = @()
    packageInventoryAfter = $null
    restartRequired = $false
    readyForAddressReadback = $false
    failures = @()
}
$script:OwnedDeviceIds = [Collections.Generic.List[string]]::new()
$script:CommandRecords = [Collections.Generic.List[object]]::new()
$script:Failures = [Collections.Generic.List[object]]::new()
$script:StateRoot = Join-Path $env:ProgramData 'ClientNicVmnetProbe'
$script:StateCommitted = $false
$script:ScheduledRestart = $false

function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Write-Json([string]$Path, [object]$Value, [int]$Depth = 24) {
    Write-Utf8 $Path (ConvertTo-Json -InputObject $Value -Depth $Depth)
}

function Get-Sha256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}

function Quote-Argument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Invoke-RecordedProcess([string]$Program, [string[]]$Arguments, [int]$TimeoutSeconds = 90) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.Arguments = (($Arguments | ForEach-Object { Quote-Argument ([string]$_) }) -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = [DateTime]::UtcNow
    $stdout = ''
    $stderr = ''
    $exitCode = -1
    $timedOut = $false
    try {
        if (-not $process.Start()) { throw ('Process did not start: ' + $Program) }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
        if ($timedOut) { $process.Kill(); $process.WaitForExit(); $exitCode = -1 }
        else { $exitCode = $process.ExitCode }
        $stdout = [string]$stdoutTask.Result
        $stderr = [string]$stderrTask.Result
    }
    finally { $process.Dispose() }
    $record = [ordered]@{
        startedUtc = $started.ToString('o')
        program = $Program
        arguments = @($Arguments)
        exitCode = [int]$exitCode
        timedOut = [bool]$timedOut
        stdout = $stdout
        stderr = $stderr
    }
    $script:CommandRecords.Add($record)
    return $record
}

function Get-TargetPnpNodes([string]$HardwareId) {
    return @(
        Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction Stop |
            Where-Object {
                [string]$_.PNPClass -ceq 'Net' -and
                @($_.HardwareID | ForEach-Object { [string]$_ }) -contains $HardwareId
            } |
            ForEach-Object {
                [ordered]@{
                    instanceId = [string]$_.PNPDeviceID
                    name = [string]$_.Name
                    pnpClass = [string]$_.PNPClass
                    manufacturer = [string]$_.Manufacturer
                    service = [string]$_.Service
                    status = [string]$_.Status
                    configManagerErrorCode = [int]$_.ConfigManagerErrorCode
                    hardwareIds = @($_.HardwareID | ForEach-Object { [string]$_ })
                    hardwareId = $HardwareId
                }
            }
    )
}

function Get-PackageRows([string]$Text) {
    $rows = [Collections.Generic.List[object]]::new()
    foreach ($block in [regex]::Split($Text, '(?:\r?\n){2,}')) {
        $published = [regex]::Match($block, '(?im)^\s*Published Name\s*:\s*(\S+)\s*$')
        $original = [regex]::Match($block, '(?im)^\s*Original Name\s*:\s*(\S+)\s*$')
        if ($published.Success -and $original.Success) {
            $rows.Add([ordered]@{ publishedName = $published.Groups[1].Value; originalName = $original.Groups[1].Value })
        }
    }
    return $rows.ToArray()
}

function Get-TargetDetails([string]$HardwareId) {
    $nodes = @(Get-TargetPnpNodes $HardwareId)
    $out = [Collections.Generic.List[object]]::new()
    foreach ($node in $nodes) {
        $id = [string]$node.instanceId
        $signed = @(Get-CimInstance -ClassName Win32_PnPSignedDriver -ErrorAction Stop |
            Where-Object { [string]::Equals([string]$_.DeviceID, $id, [StringComparison]::OrdinalIgnoreCase) } |
            ForEach-Object {
                [ordered]@{
                    deviceId = [string]$_.DeviceID
                    deviceName = [string]$_.DeviceName
                    infName = [string]$_.InfName
                    driverProviderName = [string]$_.DriverProviderName
                    driverVersion = [string]$_.DriverVersion
                    isSigned = [bool]$_.IsSigned
                    signer = [string]$_.Signer
                }
            })
        $win32 = @(Get-CimInstance -ClassName Win32_NetworkAdapter -ErrorAction Stop |
            Where-Object { [string]::Equals([string]$_.PNPDeviceID, $id, [StringComparison]::OrdinalIgnoreCase) } |
            ForEach-Object { [ordered]@{ name = [string]$_.Name; pnpDeviceId = [string]$_.PNPDeviceID; guid = [string]$_.GUID; macAddress = [string]$_.MACAddress; netEnabled = $_.NetEnabled } })
        $msft = @(Get-CimInstance -Namespace 'root\StandardCimv2' -ClassName MSFT_NetAdapter -ErrorAction Stop |
            Where-Object { [string]::Equals([string]$_.PNPDeviceID, $id, [StringComparison]::OrdinalIgnoreCase) } |
            ForEach-Object { [ordered]@{ name = [string]$_.Name; pnpDeviceId = [string]$_.PNPDeviceID; interfaceGuid = [string]$_.InterfaceGuid; networkAddresses = @($_.NetworkAddresses); permanentAddress = [string]$_.PermanentAddress; status = [string]$_.Status } })
        $plain = @()
        if ($msft.Count -eq 1 -and $msft[0].interfaceGuid) {
            $guid = [Guid]::Empty
            if ([Guid]::TryParse([string]$msft[0].interfaceGuid, [ref]$guid)) {
                $plain = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop |
                    Where-Object {
                        $candidate = [Guid]::Empty
                        [Guid]::TryParse([string]$_.InterfaceGuid, [ref]$candidate) -and $candidate -eq $guid
                    } |
                    ForEach-Object { [ordered]@{ name = [string]$_.Name; interfaceGuid = [string]$_.InterfaceGuid; macAddress = [string]$_.MacAddress; status = [string]$_.Status; interfaceDescription = [string]$_.InterfaceDescription } })
            }
        }
        $out.Add([ordered]@{ pnp = $node; signedDriverRows = $signed; win32NetworkAdapterRows = $win32; msftNetAdapterRows = $msft; getNetAdapterRows = $plain })
    }
    return $out.ToArray()
}

function Remove-OwnedDevices([string]$DevconPath, [string[]]$BaselineIds) {
    $cleanup = [Collections.Generic.List[object]]::new()
    foreach ($hardwareId in @('*VMnetAdapter1', '*VMnetAdapter8')) {
        foreach ($node in @(Get-TargetPnpNodes $hardwareId)) {
            $id = [string]$node.instanceId
            if (@($BaselineIds | Where-Object { [string]::Equals([string]$_, $id, [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0) { continue }
            $command = Invoke-RecordedProcess $DevconPath @('remove', $id)
            $cleanup.Add([ordered]@{ operation = 'devcon-remove'; instanceId = $id; hardwareId = $hardwareId; command = $command })
        }
    }
    return $cleanup.ToArray()
}

try {
    $runFull = [IO.Path]::GetFullPath($RunRoot)
    if (-not $runFull.StartsWith('C:\ProgramData\UEFILoaderRunner\runs\', [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $runFull -PathType Container)) {
        throw 'RunRoot is outside the authenticated VM mailbox run directory.'
    }
    if ($ExpectedProbeVerdict -notin @('MET', 'NOT_MET')) { throw 'Unexpected probe verdict.' }
    if ($Action -cne 'Probe') { throw 'Unexpected mailbox action.' }
    $computer = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
    $product = Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop
    $vmUuid = ([Guid]::Parse([string]$product.UUID)).ToString('D').ToUpperInvariant()
    if ([string]$computer.Manufacturer -notmatch '^(?i:VMware(?:,\s*Inc\.)?)$' -or $vmUuid -cne 'D7C14D56-24D1-9894-8906-C0CE95A07790') {
        throw ('VM identity mismatch: manufacturer=' + [string]$computer.Manufacturer + '; uuid=' + [string]$product.UUID)
    }
    $script:Record.vmIdentity = [ordered]@{ manufacturer = [string]$computer.Manufacturer; model = [string]$computer.Model; uuid = $vmUuid }
    if (Test-Path -LiteralPath $script:StateRoot) { throw ('Refusing existing owned state root: ' + $script:StateRoot) }

    $assetJson = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('__ASSETS_JSON_BASE64__'))
    $assets = ConvertFrom-Json -InputObject $assetJson
    $expectedAssets = @('devcon.exe', 'netadapter.inf', 'vmnet.sys', 'vmnetadapter.cat', 'vmnetadapter.sys')
    if ((@($assets | ForEach-Object { [string]$_.name } | Sort-Object) -join "`n") -cne (($expectedAssets | Sort-Object) -join "`n")) {
        throw 'Embedded vendor asset set does not match the approved five-file package.'
    }

    [void](New-Item -ItemType Directory -Path $script:StateRoot)
    $assetsRoot = Join-Path $script:StateRoot 'assets'
    [void](New-Item -ItemType Directory -Path $assetsRoot)
    foreach ($asset in $assets) {
        $name = [string]$asset.name
        if ($name -notin $expectedAssets -or $name -match '[\\/:]' -or [IO.Path]::GetFileName($name) -cne $name) { throw ('Unsafe embedded asset name: ' + $name) }
        $bytes = [Convert]::FromBase64String([string]$asset.base64)
        try {
            if ($bytes.Length -ne [int]$asset.size -or (Get-Sha256 $bytes) -cne [string]$asset.sha256) { throw ('Embedded asset hash/size mismatch: ' + $name) }
            [IO.File]::WriteAllBytes((Join-Path $assetsRoot $name), $bytes)
        }
        finally { [Array]::Clear($bytes, 0, $bytes.Length) }
    }
    $script:Record.assetManifest = @($assets | ForEach-Object { [ordered]@{ name = [string]$_.name; size = [int]$_.size; sha256 = [string]$_.sha256 } })

    $targets = @(
        [ordered]@{ label = 'VMnet1'; hardwareId = '*VMnetAdapter1' },
        [ordered]@{ label = 'VMnet8'; hardwareId = '*VMnetAdapter8' }
    )
    $baselineNodes = [Collections.Generic.List[object]]::new()
    foreach ($target in $targets) {
        $rows = @(Get-TargetPnpNodes ([string]$target.hardwareId))
        foreach ($row in $rows) { $baselineNodes.Add($row) }
        if ($rows.Count -ne 0) { throw ('Refusing duplicate/preexisting guest node for ' + $target.label + ': ' + (ConvertTo-Json -InputObject $rows -Compress -Depth 8)) }
    }
    $script:Record.preexistingTargets = @($baselineNodes.ToArray())
    $baselineIds = @($baselineNodes | ForEach-Object { [string]$_.instanceId })
    $pnputil = Join-Path $env:SystemRoot 'System32\pnputil.exe'
    $before = Invoke-RecordedProcess $pnputil @('/enum-drivers', '/class', 'Net')
    $script:Record.packageInventoryBefore = [ordered]@{ command = $before; parsedNetadapterPackages = @(Get-PackageRows ([string]$before.stdout) | Where-Object { [string]::Equals([string]$_.originalName, 'netadapter.inf', [StringComparison]::OrdinalIgnoreCase) }) }

    $devcon = Join-Path $assetsRoot 'devcon.exe'
    $inf = Join-Path $assetsRoot 'netadapter.inf'
    foreach ($target in $targets) {
        $command = Invoke-RecordedProcess $devcon @('install', $inf, [string]$target.hardwareId)
        $script:Record.installCommands += [ordered]@{ label = [string]$target.label; hardwareId = [string]$target.hardwareId; command = $command }
        if ($command.timedOut -or $command.exitCode -notin @(0, 1)) { throw ('DevCon install failed for ' + $target.label + ': exit=' + $command.exitCode + '; stderr=' + [string]$command.stderr + '; stdout=' + [string]$command.stdout) }
        if ($command.exitCode -eq 1) { $script:Record.restartRequired = $true }
        $created = @(Get-TargetPnpNodes ([string]$target.hardwareId))
        if ($created.Count -ne 1) { throw ('Expected exactly one created PnP node after DevCon install for ' + $target.label + '; actual=' + $created.Count) }
        $createdId = [string]$created[0].instanceId
        if (@($baselineIds | Where-Object { [string]::Equals([string]$_, $createdId, [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0) { throw ('Created node identity existed in baseline: ' + $createdId) }
        if (-not $script:OwnedDeviceIds.Contains($createdId)) { $script:OwnedDeviceIds.Add($createdId) }
    }

    $after = Invoke-RecordedProcess $pnputil @('/enum-drivers', '/class', 'Net')
    $beforeNames = @($script:Record.packageInventoryBefore.parsedNetadapterPackages | ForEach-Object { [string]$_.publishedName })
    $afterRows = @(Get-PackageRows ([string]$after.stdout) | Where-Object { [string]::Equals([string]$_.originalName, 'netadapter.inf', [StringComparison]::OrdinalIgnoreCase) })
    $newPackages = @($afterRows | Where-Object { $_.publishedName -notin $beforeNames })
    $script:Record.packageInventoryAfter = [ordered]@{ command = $after; parsedNetadapterPackages = $afterRows; newlyPublished = $newPackages }
    $script:Record.targetDevices = @(foreach ($target in $targets) {
        $nodes = @(Get-TargetPnpNodes ([string]$target.hardwareId))
        $details = @(Get-TargetDetails ([string]$target.hardwareId))
        $pnputilRows = @(foreach ($node in $nodes) { Invoke-RecordedProcess $pnputil @('/enum-devices', '/instanceid', [string]$node.instanceId, '/drivers', '/stack') })
        [ordered]@{ label = [string]$target.label; hardwareId = [string]$target.hardwareId; nodeRows = $nodes; adapterDetails = $details; pnputilStackAndDriver = $pnputilRows }
    })
    $script:Record.driverServices = @(Get-CimInstance -ClassName Win32_SystemDriver -ErrorAction Stop |
        Where-Object { [string]$_.Name -match '(?i)^VMnet' } |
        ForEach-Object { [ordered]@{ name = [string]$_.Name; state = [string]$_.State; startMode = [string]$_.StartMode; pathName = [string]$_.PathName; serviceType = [string]$_.ServiceType } })

    $problems = [Collections.Generic.List[string]]::new()
    foreach ($target in $script:Record.targetDevices) {
        if (@($target.nodeRows).Count -ne 1) { $problems.Add(([string]$target.label + ' node count=' + @($target.nodeRows).Count)); continue }
        if ([int]$target.nodeRows[0].configManagerErrorCode -ne 0) { $problems.Add(([string]$target.label + ' ConfigManagerErrorCode=' + [string]$target.nodeRows[0].configManagerErrorCode)) }
        if (@($target.adapterDetails).Count -ne 1) { $problems.Add(([string]$target.label + ' adapter-detail count=' + @($target.adapterDetails).Count)); continue }
        $detail = $target.adapterDetails[0]
        if (@($detail.signedDriverRows).Count -ne 1 -or -not [bool]$detail.signedDriverRows[0].isSigned) { $problems.Add(([string]$target.label + ' signed driver rows=' + @($detail.signedDriverRows).Count)) }
        if (@($detail.win32NetworkAdapterRows).Count -ne 1 -or @($detail.msftNetAdapterRows).Count -ne 1 -or @($detail.getNetAdapterRows).Count -ne 1) {
            $problems.Add(([string]$target.label + ' network enumeration counts Win32=' + @($detail.win32NetworkAdapterRows).Count + ' MSFT=' + @($detail.msftNetAdapterRows).Count + ' Get-NetAdapter=' + @($detail.getNetAdapterRows).Count))
        }
    }
    $script:Record.initializationProblems = @($problems.ToArray())
    $script:Record.readyForAddressReadback = $problems.Count -eq 0
    if (-not $script:Record.readyForAddressReadback -and -not $script:Record.restartRequired) {
        throw ('VMnet miniport initialization was not healthy: ' + ($problems -join '; '))
    }

    $state = [ordered]@{
        schema = 'vmnet-miniport-owned-state.v1'
        vmUuid = $vmUuid
        stateRoot = $script:StateRoot
        assetsRoot = $assetsRoot
        devices = @($script:Record.targetDevices | ForEach-Object {
            [ordered]@{ label = [string]$_.label; hardwareId = [string]$_.hardwareId; instanceId = [string]$_.nodeRows[0].instanceId; driverInfName = if (@($_.adapterDetails[0].signedDriverRows).Count -eq 1) { [string]$_.adapterDetails[0].signedDriverRows[0].infName } else { $null } }
        })
        newlyPublishedPackages = @($newPackages)
        baselineInstanceIds = @($baselineIds)
        createdUtc = [DateTime]::UtcNow.ToString('o')
    }
    Write-Json (Join-Path $script:StateRoot 'journal.json') $state 16
    $script:StateCommitted = $true
    $script:Result.Status = 'PASS'
    $script:Result.Message = if ($script:Record.restartRequired) { 'Signed VMnet package installed; DevCon reports restart required before address read-back.' } else { 'Both guest VMnet miniports initialized and bound; address read-back is ready.' }
}
catch {
    $script:Result.Status = 'FAIL'
    $script:Result.Message = $_.Exception.Message
    $script:Failures.Add([ordered]@{ exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId; scriptStackTrace = $_.ScriptStackTrace; position = $_.InvocationInfo.PositionMessage })
}
finally {
    if (-not $script:StateCommitted -and (Test-Path -LiteralPath $script:StateRoot)) {
        try {
            $assetsRoot = Join-Path $script:StateRoot 'assets'
            $devcon = Join-Path $assetsRoot 'devcon.exe'
            $cleanupRows = @()
            if (Test-Path -LiteralPath $devcon -PathType Leaf) {
                $cleanupRows = @(Remove-OwnedDevices $devcon @($script:Record.preexistingTargets | ForEach-Object { [string]$_.instanceId }))
            }
            $script:Record.failedInstallCleanup = $cleanupRows
            $remainingOwned = [Collections.Generic.List[string]]::new()
            foreach ($hardwareId in @('*VMnetAdapter1', '*VMnetAdapter8')) {
                foreach ($node in @(Get-TargetPnpNodes $hardwareId)) {
                    $id = [string]$node.instanceId
                    if (@($script:Record.preexistingTargets | Where-Object { [string]::Equals([string]$_.instanceId, $id, [StringComparison]::OrdinalIgnoreCase) }).Count -eq 0) { $remainingOwned.Add($id) }
                }
            }
            $cleanupFailed = @($cleanupRows | Where-Object { [int]$_.command.exitCode -ne 0 -or [bool]$_.command.timedOut }).Count -gt 0 -or $remainingOwned.Count -gt 0
            $script:Record.failedInstallRemainingDeviceIds = @($remainingOwned.ToArray())
            if ($cleanupFailed) {
                $script:Record.failedInstallStatePreserved = $script:StateRoot
            }
            else {
                Remove-Item -LiteralPath $script:StateRoot -Recurse -Force
                $script:Record.failedInstallStateRemoved = $true
            }
        }
        catch { $script:Failures.Add([ordered]@{ source = 'failed_install_cleanup'; exception = $_.Exception.ToString() }) }
    }
    $script:Record.completedUtc = [DateTime]::UtcNow.ToString('o')
    $script:Record.commandRecords = @($script:CommandRecords.ToArray())
    $script:Record.ownedDeviceIds = @($script:OwnedDeviceIds.ToArray())
    $script:Record.failures = @($script:Failures.ToArray())
    try {
        Write-Json (Join-Path $RunRoot 'kernel-metadata.json') $script:Record 28
        $stdout = [Collections.Generic.List[string]]::new()
        foreach ($command in $script:CommandRecords) { $stdout.Add('COMMAND ' + $command.program + ' ' + ($command.arguments -join ' ') + ' EXIT=' + $command.exitCode); if ($command.stdout) { $stdout.Add([string]$command.stdout) } }
        Write-Utf8 (Join-Path $RunRoot 'stdout.txt') (($stdout.ToArray() -join [Environment]::NewLine) + [Environment]::NewLine)
        $stderr = [Collections.Generic.List[string]]::new()
        foreach ($command in $script:CommandRecords) { if ($command.stderr) { $stderr.Add('COMMAND ' + $command.program + ' STDERR:'); $stderr.Add([string]$command.stderr) } }
        foreach ($failure in $script:Failures) { if ($failure.exception) { $stderr.Add([string]$failure.exception) } }
        if ($script:Result.Status -eq 'FAIL') { $stderr.Add([string]$script:Result.Message) }
        Write-Utf8 (Join-Path $RunRoot 'stderr.txt') ($stderr.ToArray() -join [Environment]::NewLine)
        Write-Json (Join-Path $RunRoot 'result.json') $script:Result 8
    }
    catch {
        $script:Result.Status = 'FAIL'
        $script:Result.Message = 'Evidence finalization failed: ' + $_.Exception.ToString()
        try { Write-Utf8 (Join-Path $RunRoot 'stderr.txt') $script:Result.Message; Write-Json (Join-Path $RunRoot 'result.json') $script:Result 8 } catch { }
    }
}
if ($script:Result.Status -cne 'PASS') { exit 1 }
exit 0
