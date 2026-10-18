[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [ValidateSet('Probe')][string]$Action = 'Probe',
    [ValidateSet('MET', 'NOT_MET')][string]$ExpectedProbeVerdict = 'NOT_MET'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:Result = [ordered]@{ SchemaVersion = 1; RunId = Split-Path -Leaf $RunRoot; Status = 'FAIL'; Message = 'VMnet registry round trip did not complete.'; ExpectedProbeVerdict = $ExpectedProbeVerdict }
$script:Record = [ordered]@{
    schema = 'vmnet-four-target-address-roundtrip.v1'
    startedUtc = [DateTime]::UtcNow.ToString('o')
    vmIdentity = $null
    stateJournal = $null
    nativeExeSha256 = 'BF553F39E4E7024C75272C24215D995858627AEDD5E946A4B1289A8033DA1642'
    productionHelperBundleSha256 = 'E7B1B841372B890C0D1638E6CCB11D41B5F257A64FBF491B3FEF811B29A212D8'
    lockedBindings = @(
        [ordered]@{ label = 'VMnet8'; hardwareId = '*VMnetAdapter8'; original = '00:50:56:C0:00:08'; replacement = '02:90:10:20:30:40' },
        [ordered]@{ label = 'VMnet1'; hardwareId = '*VMnetAdapter1'; original = '00:50:56:C0:00:01'; replacement = '02:90:10:20:30:41' },
        [ordered]@{ label = 'vmxnet3-locked-target-42'; hardwareId = $null; original = 'C4:BD:E5:32:06:D1'; replacement = '02:90:10:20:30:42' },
        [ordered]@{ label = 'vmxnet3-locked-target-43'; hardwareId = $null; original = 'D8:BB:C1:D8:57:20'; replacement = '02:90:10:20:30:43' }
    )
    registrySnapshots = @()
    mutationRecords = @()
    phases = [ordered]@{}
    checks = @()
    failures = @()
    restored = $false
}
$script:Failures = [Collections.Generic.List[object]]::new()
$script:Mutations = [Collections.Generic.List[object]]::new()
$script:Checks = [Collections.Generic.List[object]]::new()
$script:Phases = [ordered]@{}
$script:Targets = @()
$script:MutationStarted = $false
$script:HostJournal = $null
$script:NativeExePath = Join-Path $RunRoot 'nic_native_read.exe'
$script:NativeSha256 = 'BF553F39E4E7024C75272C24215D995858627AEDD5E946A4B1289A8033DA1642'
$script:ProductionHelpersPath = Join-Path $RunRoot 'production-address-functions.ps1'
$script:ProductionHelpersSha256 = 'E7B1B841372B890C0D1638E6CCB11D41B5F257A64FBF491B3FEF811B29A212D8'

function Write-Utf8([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false)) }
function Write-Json([string]$Path, [object]$Value, [int]$Depth = 32) { Write-Utf8 $Path (ConvertTo-Json -InputObject $Value -Depth $Depth) }
function Normalize-Guid([object]$Value) { try { return ([Guid]::Parse([string]$Value)).ToString('D').ToUpperInvariant() } catch { return $null } }
function Normalize-Mac([object]$Value) { if ($null -eq $Value) { return $null }; $text = ([string]$Value) -replace '[^0-9A-Fa-f]', ''; if ($text -notmatch '\A[0-9A-Fa-f]{12}\z') { return $null }; return $text.ToUpperInvariant() }
function Get-Sha256([byte[]]$Bytes) { $sha = [Security.Cryptography.SHA256]::Create(); try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '') } finally { $sha.Dispose() } }
function Get-Prop([object]$InputObject, [string]$Name) { if ($null -eq $InputObject) { return $null }; $p = $InputObject.PSObject.Properties[$Name]; if ($null -eq $p) { return $null }; return $p.Value }

function Invoke-NativeProcess([string]$Exe, [string[]]$ArgumentList) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Exe
    $info.Arguments = (($ArgumentList | ForEach-Object { '"' + ([string]$_).Replace('"', '\"') + '"' }) -join ' ')
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw 'Native reader did not start.' }
        $outTask = $process.StandardOutput.ReadToEndAsync()
        $errTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) { $process.Kill(); $process.WaitForExit(); throw 'Native reader timed out after 60 seconds.' }
        return [ordered]@{ exitCode = [int]$process.ExitCode; stdout = [string]$outTask.Result; stderr = [string]$errTask.Result }
    }
    finally { $process.Dispose() }
}

function Get-NativeCapture([string]$Phase) {
    $reportPath = Join-Path $RunRoot ('native-' + $Phase + '.' + [Guid]::NewGuid().ToString('N') + '.json')
    $command = Invoke-NativeProcess $script:NativeExePath @('--capture', $reportPath)
    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) { throw ('Native reader did not create capture for ' + $Phase + '; exit=' + $command.exitCode + '; stderr=' + $command.stderr) }
    $rawText = [IO.File]::ReadAllText($reportPath)
    $data = ConvertFrom-Json -InputObject $rawText
    Remove-Item -LiteralPath $reportPath -Force
    if ($command.exitCode -ne 0) { throw ('Native reader failed for ' + $Phase + '; exit=' + $command.exitCode + '; stderr=' + $command.stderr) }
    return [ordered]@{ exitCode = $command.exitCode; stdout = $command.stdout; stderr = $command.stderr; sha256 = Get-Sha256 ([Text.Encoding]::UTF8.GetBytes($rawText)); data = $data }
}

function Get-WindowsReads {
    $win32 = @(Get-CimInstance -Namespace 'root\CIMV2' -ClassName Win32_NetworkAdapter -OperationTimeoutSec 30 -ErrorAction Stop | ForEach-Object {
        [ordered]@{ name = [string]$_.Name; pnpDeviceId = [string]$_.PNPDeviceID; guid = [string]$_.GUID; macAddress = [string]$_.MACAddress; netEnabled = $_.NetEnabled }
    })
    $msft = @(Get-CimInstance -Namespace 'root\StandardCimv2' -ClassName MSFT_NetAdapter -OperationTimeoutSec 30 -ErrorAction Stop | ForEach-Object {
        [ordered]@{ name = [string]$_.Name; pnpDeviceId = [string]$_.PNPDeviceID; interfaceGuid = [string]$_.InterfaceGuid; networkAddresses = @($_.NetworkAddresses | ForEach-Object { [string]$_ }); permanentAddress = [string]$_.PermanentAddress; status = [string]$_.Status; mediaConnectState = $_.MediaConnectState }
    })
    $plain = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop | ForEach-Object {
        [ordered]@{ name = [string]$_.Name; interfaceGuid = [string]$_.InterfaceGuid; macAddress = [string]$_.MacAddress; status = [string]$_.Status; interfaceDescription = [string]$_.InterfaceDescription }
    })
    return [ordered]@{ win32NetworkAdapter = $win32; msftNetAdapter = $msft; getNetAdapter = $plain }
}

function Get-TargetsFromOriginal([object]$NativeData, [object]$WindowsReads) {
    $targets = [Collections.Generic.List[object]]::new()
    $interfaces = @($NativeData.setupDiNdisLanClass.interfaces)
    if ($interfaces.Count -ne 4) { throw ('Expected exactly four guest SetupDi NDIS interfaces; actual=' + $interfaces.Count) }
    $allPnp = @($interfaces | ForEach-Object { ([string]$_.pnpDeviceId).ToUpperInvariant() })
    $allGuid = @($interfaces | ForEach-Object { Normalize-Guid $_.netCfgInstanceId })
    if (@($allPnp | Sort-Object -Unique).Count -ne 4 -or @($allGuid | Sort-Object -Unique).Count -ne 4) { throw 'Guest native PnP and interface GUID identities must each be unique across all four adapters.' }
    foreach ($binding in $script:Record.lockedBindings) {
        $expectedOriginal = Normalize-Mac $binding.original
        $ndis = @($interfaces | Where-Object { (Normalize-Mac $_.ethernetAddress.mac) -ceq $expectedOriginal })
        if ($ndis.Count -ne 1) { throw ('Original native PnP rows for ' + $binding.label + ' expected=1 actual=' + $ndis.Count) }
        $pnpId = [string]$ndis[0].pnpDeviceId
        $guid = Normalize-Guid $ndis[0].netCfgInstanceId
        if (-not $guid) { throw ('Native NetCfgInstanceId is missing for ' + $binding.label) }
        if ($binding.hardwareId) {
            $owned = @($script:Record.stateJournal.devices | Where-Object { [string]$_.hardwareId -ceq [string]$binding.hardwareId -and [string]::Equals([string]$_.instanceId, $pnpId, [StringComparison]::OrdinalIgnoreCase) })
            if ($owned.Count -ne 1) { throw ('Expected exact owned VMnet journal identity for ' + $binding.label + '; actual=' + $owned.Count) }
        }
        $wmi = @($WindowsReads.win32NetworkAdapter | Where-Object { [string]::Equals([string]$_.pnpDeviceId, $pnpId, [StringComparison]::OrdinalIgnoreCase) })
        $msft = @($WindowsReads.msftNetAdapter | Where-Object { (Normalize-Guid $_.interfaceGuid) -ceq $guid -and [string]::Equals([string]$_.pnpDeviceId, $pnpId, [StringComparison]::OrdinalIgnoreCase) })
        $plain = @($WindowsReads.getNetAdapter | Where-Object { (Normalize-Guid $_.interfaceGuid) -ceq $guid })
        if ($wmi.Count -ne 1 -or (Normalize-Guid $wmi[0].guid) -cne $guid -or $msft.Count -ne 1 -or $plain.Count -ne 1) {
            throw ('Original Windows identity join failed for ' + $binding.label + ': Win32=' + $wmi.Count + ' MSFT=' + $msft.Count + ' Get-NetAdapter=' + $plain.Count)
        }
        $entry = [ordered]@{ label = [string]$binding.label; hardwareId = [string]$binding.hardwareId; pnpDeviceId = $pnpId; interfaceGuid = $guid; original = [string]$binding.original; replacement = [string]$binding.replacement }
        $actualOriginal = Normalize-Mac $ndis[0].ethernetAddress.mac
        if ($actualOriginal -cne $expectedOriginal) { throw ('Original OID mismatch for ' + $binding.label + ': expected=' + $binding.original + ' actual=' + [string]$ndis[0].ethernetAddress.mac) }
        if ((Normalize-Mac $wmi[0].macAddress) -cne $expectedOriginal -or (Normalize-Mac $plain[0].macAddress) -cne $expectedOriginal) { throw ('Original Windows MAC mismatch for ' + $binding.label) }
        $targets.Add($entry)
    }
    return @($targets.ToArray())
}

function Get-PhaseObservations([string]$PhaseName, [string]$Mode, [object]$NativeCapture, [object]$WindowsReads) {
    $native = $NativeCapture.data
    $rows = [Collections.Generic.List[object]]::new()
    foreach ($target in $script:Targets) {
        $pnp = [string]$target.pnpDeviceId
        $guid = [string]$target.interfaceGuid
        $ndis = @($native.setupDiNdisLanClass.interfaces | Where-Object { [string]::Equals([string]$_.pnpDeviceId, $pnp, [StringComparison]::OrdinalIgnoreCase) })
        $gaa = @($native.getAdaptersAddresses.entries | Where-Object { (Normalize-Guid $_.adapterName) -ceq $guid })
        $gai = @($native.getAdaptersInfo.entries | Where-Object { (Normalize-Guid $_.adapterName) -ceq $guid })
        $ift = @($native.getIfTable2.entries | Where-Object { (Normalize-Guid $_.interfaceGuid) -ceq $guid })
        $win32 = @($WindowsReads.win32NetworkAdapter | Where-Object { [string]::Equals([string]$_.pnpDeviceId, $pnp, [StringComparison]::OrdinalIgnoreCase) })
        $msft = @($WindowsReads.msftNetAdapter | Where-Object { (Normalize-Guid $_.interfaceGuid) -ceq $guid -and [string]::Equals([string]$_.pnpDeviceId, $pnp, [StringComparison]::OrdinalIgnoreCase) })
        $plain = @($WindowsReads.getNetAdapter | Where-Object { (Normalize-Guid $_.interfaceGuid) -ceq $guid })
        $expected = if ($Mode -ceq 'Original') { [string]$target.original } else { [string]$target.replacement }
        $expectedKey = Normalize-Mac $expected
        $values = [ordered]@{
            OID_802_3_CURRENT_ADDRESS = if ($ndis.Count -eq 1) { [string]$ndis[0].ethernetAddress.mac } else { $null }
            GetAdaptersAddresses = if ($gaa.Count -eq 1) { [string]$gaa[0].physicalAddress } else { $null }
            GetAdaptersInfo = if ($gai.Count -eq 1) { [string]$gai[0].address } else { $null }
            GetIfTable2 = if ($ift.Count -eq 1) { [string]$ift[0].physicalAddress } else { $null }
            'Win32_NetworkAdapter.MACAddress' = if ($win32.Count -eq 1) { [string]$win32[0].macAddress } else { $null }
            'MSFT_NetAdapter.NetworkAddresses' = if ($msft.Count -eq 1) { @($msft[0].networkAddresses) } else { @() }
            'Get-NetAdapter.MacAddress' = if ($plain.Count -eq 1) { [string]$plain[0].macAddress } else { $null }
        }
        $readerCounts = [ordered]@{ ndis = $ndis.Count; getAdaptersAddresses = $gaa.Count; getAdaptersInfo = $gai.Count; getIfTable2 = $ift.Count; win32 = $win32.Count; msft = $msft.Count; getNetAdapter = $plain.Count }
        foreach ($reader in $values.Keys) {
            $actual = $values[$reader]
            [object[]]$actualRows = @()
            if ($reader -ceq 'MSFT_NetAdapter.NetworkAddresses') { $actualRows = @($actual | ForEach-Object { [string]$_ }) }
            elseif ($null -ne $actual) { $actualRows = @([string]$actual) }
            $pass = $actualRows.Count -eq 1 -and (Normalize-Mac $actualRows[0]) -ceq $expectedKey
            if ($reader -ceq 'OID_802_3_CURRENT_ADDRESS') {
                $pass = $pass -and [bool]$ndis[0].ethernetAddress.ioctlSucceeded -and [int]$ndis[0].ethernetAddress.win32Error -eq 0 -and [int]$ndis[0].ethernetAddress.bytesReturned -eq 6
            }
            $check = [ordered]@{ phase = $PhaseName; target = [string]$target.label; pnpDeviceId = $pnp; interfaceGuid = $guid; reader = [string]$reader; expected = $expected; actual = $actual; readerMatchCount = $actualRows.Count; readerSourceRowCount = switch ($reader) { 'OID_802_3_CURRENT_ADDRESS' { $ndis.Count } 'GetAdaptersAddresses' { $gaa.Count } 'GetAdaptersInfo' { $gai.Count } 'GetIfTable2' { $ift.Count } 'Win32_NetworkAdapter.MACAddress' { $win32.Count } 'MSFT_NetAdapter.NetworkAddresses' { $msft.Count } default { $plain.Count } }; pass = [bool]$pass }
            $script:Checks.Add($check)
        }
        $rows.Add([ordered]@{ target = $target; readerCounts = $readerCounts; observations = $values })
    }
    $script:Phases[$PhaseName] = [ordered]@{ mode = $Mode; capturedUtc = [DateTime]::UtcNow.ToString('o'); nativeCapture = $NativeCapture; windowsReads = $WindowsReads; targetObservations = $rows.ToArray() }
}

try {
    $runFull = [IO.Path]::GetFullPath($RunRoot)
    if (-not $runFull.StartsWith('C:\ProgramData\UEFILoaderRunner\runs\', [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $runFull -PathType Container)) { throw 'RunRoot is outside the authenticated VM mailbox run directory.' }
    if ($Action -cne 'Probe') { throw 'Unexpected mailbox action.' }
    $computer = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
    $product = Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop
    $uuid = Normalize-Guid $product.UUID
    if ([string]$computer.Manufacturer -notmatch '^(?i:VMware(?:,\s*Inc\.)?)$' -or $uuid -cne 'D7C14D56-24D1-9894-8906-C0CE95A07790') { throw ('VM identity mismatch: ' + [string]$computer.Manufacturer + '/' + [string]$product.UUID) }
    $script:Record.vmIdentity = [ordered]@{ manufacturer = [string]$computer.Manufacturer; model = [string]$computer.Model; uuid = $uuid }
    $journalPath = 'C:\ProgramData\ClientNicVmnetProbe\journal.json'
    if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf)) { throw 'VMnet init journal is absent.' }
    $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
    if ([string]$journal.schema -cne 'vmnet-miniport-owned-state.v1' -or [string]$journal.vmUuid -cne $uuid) { throw 'VMnet init journal schema/identity mismatch.' }
    $script:Record.stateJournal = $journal
    $computer2 = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $computer2.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'VMnet address proof requires elevated guest execution.' }
    $productionHelperBytes = [Convert]::FromBase64String('__PRODUCTION_HELPERS_BASE64__')
    try {
        if ((Get-Sha256 $productionHelperBytes) -cne $script:ProductionHelpersSha256) { throw 'Embedded production adapter helper hash mismatch.' }
        [IO.File]::WriteAllBytes($script:ProductionHelpersPath, $productionHelperBytes)
    }
    finally { [Array]::Clear($productionHelperBytes, 0, $productionHelperBytes.Length) }
    . $script:ProductionHelpersPath
    $nativeBytes = [Convert]::FromBase64String('__NATIVE_READER_BASE64__')
    try {
        if ((Get-Sha256 $nativeBytes) -cne $script:NativeSha256) { throw 'Embedded native reader hash mismatch.' }
        [IO.File]::WriteAllBytes($script:NativeExePath, $nativeBytes)
    }
    finally { [Array]::Clear($nativeBytes, 0, $nativeBytes.Length) }

    $nativeOriginal = Get-NativeCapture 'original'
    $windowsOriginal = Get-WindowsReads
    $boundTargets = @(Get-TargetsFromOriginal $nativeOriginal.data $windowsOriginal)
    $bindings = @($boundTargets | ForEach-Object { [ordered]@{ pnpDeviceId = [string]$_.pnpDeviceId; original = [string]$_.original; replacement = [string]$_.replacement } })
    $adapterSnapshots = @(Get-HostAdapterSnapshots -Bindings $bindings)
    if ($adapterSnapshots.Count -ne 4) { throw ('Production adapter snapshot expected four unique adapters; actual=' + $adapterSnapshots.Count) }
    $script:Targets = @()
    foreach ($boundTarget in $boundTargets) {
        $snapshotMatches = @($adapterSnapshots | Where-Object { [string]::Equals([string]$_.pnpDeviceId, [string]$boundTarget.pnpDeviceId, [StringComparison]::OrdinalIgnoreCase) })
        if ($snapshotMatches.Count -ne 1) { throw ('Production adapter snapshot identity did not resolve exactly once for ' + [string]$boundTarget.label + '; count=' + $snapshotMatches.Count) }
        $snapshot = $snapshotMatches[0]
        if ([bool]$snapshot.networkAddress.present) { throw ('NetworkAddress override already exists for ' + [string]$boundTarget.label + '; refusing to overwrite; type=' + [string]$snapshot.networkAddress.kind + ' value=' + [string]$snapshot.networkAddress.rawValue) }
        $snapshot.label = [string]$boundTarget.label
        $snapshot.hardwareId = [string]$boundTarget.hardwareId
        $script:Targets += ,([ordered]@{ label = [string]$boundTarget.label; hardwareId = [string]$boundTarget.hardwareId; pnpDeviceId = [string]$boundTarget.pnpDeviceId; interfaceGuid = [string]$snapshot.interfaceGuid; original = [string]$boundTarget.original; replacement = [string]$boundTarget.replacement; snapshot = $snapshot })
    }
    $script:Record.targets = $script:Targets
    $script:Record.registrySnapshots = @($adapterSnapshots | ForEach-Object { [ordered]@{ pnpDeviceId = [string]$_.pnpDeviceId; interfaceGuid = [string]$_.interfaceGuid; registrySubKey = [string]$_.registrySubKey; registryKeyName = [string]$_.registryKeyName; registryPnpDeviceId = [string]$_.registryPnpDeviceId; networkAddress = $_.networkAddress; linkConnected = [bool]$_.linkConnected; mediaConnectState = [string]$_.mediaConnectState } })
    $script:HostJournal = [ordered]@{
        schema = 'vmnet-production-adapter-apply.v1'
        journalPath = (Join-Path $RunRoot 'adapter-restore-journal.json')
        vmUuid = $uuid
        mutationStarted = $false
        adapterSnapshots = $adapterSnapshots
        adapterProgress = @($script:Targets | ForEach-Object { [ordered]@{ pnpDeviceId = [string]$_.pnpDeviceId; label = [string]$_.label; state = 'snapshotted'; applyIntentUtc = $null; targetMac = $null; registryIntent = $null; addressIntent = $null; appliedUtc = $null; actualMac = $null; registryActual = $null; restoreIntentUtc = $null; restoreState = $null; restoreActualMac = $null; restoreRegistry = $null; restoreAdminStatus = $null; restoreCompletedUtc = $null; restoreError = $null } })
        linkRestoreChecks = @()
        updatedUtc = [DateTime]::UtcNow.ToString('o')
    }
    Write-HostJournal $script:HostJournal
    $script:Record.hostAdapterJournal = $script:HostJournal
    Get-PhaseObservations 'original' 'Original' $nativeOriginal $windowsOriginal
    if (@($script:Checks | Where-Object { $_.phase -ceq 'original' -and -not $_.pass }).Count -gt 0) { throw 'At least one fresh original readback did not match the locked original.' }

    foreach ($target in $script:Targets) {
        $progress = @($script:HostJournal.adapterProgress | Where-Object { [string]::Equals([string]$_.pnpDeviceId, [string]$target.pnpDeviceId, [StringComparison]::OrdinalIgnoreCase) })
        if ($progress.Count -ne 1) { throw ('Production apply journal progress did not resolve exactly once for ' + [string]$target.label) }
        $applyResult = Invoke-HostAdapterRegistryApply -Snapshot $target.snapshot -Journal $script:HostJournal -Progress $progress[0] -MutationStarted ([ref]$script:MutationStarted)
        $script:Mutations.Add([ordered]@{ timeUtc = [DateTime]::UtcNow.ToString('o'); target = [string]$target.label; pnpDeviceId = [string]$applyResult.pnpDeviceId; interfaceGuid = [string]$applyResult.interfaceGuid; registrySubKey = [string]$applyResult.registrySubKey; valueName = 'NetworkAddress'; registryKind = [string]$applyResult.registryValueKind; writtenValue = [string]$applyResult.registryValue; expected = [string]$applyResult.expected; actual = [string]$applyResult.actual; matchesExpected = [bool]$applyResult.matchesExpected })
    }
    $nativeActive = Get-NativeCapture 'active'
    $windowsActive = Get-WindowsReads
    Get-PhaseObservations 'filteredActive' 'Replacement' $nativeActive $windowsActive
    if (@($script:Checks | Where-Object { $_.phase -ceq 'filteredActive' -and -not $_.pass }).Count -gt 0) { throw 'At least one active reader did not return the exact locked replacement.' }
    $script:Result.Status = 'PASS'
    $script:Result.Message = 'All four locked guest adapters returned original, replacement, and restored MAC values through all seven raw reader paths.'
}
catch {
    $script:Result.Status = 'FAIL'
    $script:Result.Message = $_.Exception.Message
    $script:Failures.Add([ordered]@{ exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId; scriptStackTrace = $_.ScriptStackTrace; position = $_.InvocationInfo.PositionMessage })
}
finally {
    if (($script:MutationStarted -or ($null -ne $script:HostJournal -and [bool]$script:HostJournal.mutationStarted)) -and $script:Targets.Count -gt 0) {
        $restoreErrors = @(Restore-HostAdapters -Journal $script:HostJournal)
        foreach ($restoreError in $restoreErrors) { $script:Failures.Add([ordered]@{ source = 'production_restore'; detail = $restoreError }) }
        try {
            $script:Record.restoredAdaptersRestarted = $true
            $restoredSnapshots = @(Get-HostAdapterSnapshots -Bindings $bindings)
            foreach ($target in $script:Targets) {
                $restoredMatches = @($restoredSnapshots | Where-Object { [string]::Equals([string]$_.pnpDeviceId, [string]$target.pnpDeviceId, [StringComparison]::OrdinalIgnoreCase) })
                if ($restoredMatches.Count -ne 1) { throw ('Restored production snapshot identity did not resolve exactly once for ' + [string]$target.label + '; count=' + $restoredMatches.Count) }
                $restoredSnapshot = $restoredMatches[0]
                if (-not (Test-NicAddressRegistryValueSnapshotEqual $target.snapshot.networkAddress $restoredSnapshot.networkAddress)) { $script:Failures.Add([ordered]@{ source = 'registry_restore_verify'; target = [string]$target.label; expected = $target.snapshot.networkAddress; actual = $restoredSnapshot.networkAddress }) }
            }
            $script:Record.restoredAdapterSnapshots = $restoredSnapshots
        }
        catch { $script:Failures.Add([ordered]@{ source = 'production_restore_snapshot'; exception = $_.Exception.ToString() }) }
        try {
            $nativeRestored = Get-NativeCapture 'restored'
            $windowsRestored = Get-WindowsReads
            Get-PhaseObservations 'restored' 'Original' $nativeRestored $windowsRestored
            $restoreChecks = @($script:Checks | Where-Object { $_.phase -ceq 'restored' -and -not $_.pass })
            if ($restoreChecks.Count -gt 0) { foreach ($check in $restoreChecks) { $script:Failures.Add([ordered]@{ source = 'restored_reader_mismatch'; detail = $check }) } }
            $script:Record.restored = $restoreChecks.Count -eq 0 -and $restoreErrors.Count -eq 0 -and @($script:Failures | Where-Object { $_.source -in @('registry_restore_verify','production_restore_snapshot') }).Count -eq 0
        }
        catch { $script:Failures.Add([ordered]@{ source = 'restored_readback'; exception = $_.Exception.ToString() }) }
    }
    foreach ($cleanupPath in @($script:NativeExePath, $script:ProductionHelpersPath)) { if (Test-Path -LiteralPath $cleanupPath -PathType Leaf) { try { Remove-Item -LiteralPath $cleanupPath -Force } catch { $script:Failures.Add([ordered]@{ source = 'probe_asset_cleanup'; path = $cleanupPath; exception = $_.Exception.ToString() }) } } }
    $script:Record.completedUtc = [DateTime]::UtcNow.ToString('o')
    $script:Record.mutationRecords = @($script:Mutations.ToArray())
    $script:Record.phases = $script:Phases
    $script:Record.checks = @($script:Checks.ToArray())
    $script:Record.failures = @($script:Failures.ToArray())
    $summary = [Collections.Generic.List[string]]::new()
    foreach ($check in $script:Checks) { $summary.Add(($check.phase + ' ' + $check.target + ' ' + $check.reader + ' expected=' + $check.expected + ' actual=' + (ConvertTo-Json -InputObject $check.actual -Compress -Depth 6) + ' pass=' + $check.pass)) }
    try {
        Write-Json (Join-Path $RunRoot 'kernel-metadata.json') $script:Record 32
        Write-Utf8 (Join-Path $RunRoot 'stdout.txt') (($summary.ToArray() -join [Environment]::NewLine) + [Environment]::NewLine)
        $errorLines = [Collections.Generic.List[string]]::new()
        foreach ($failure in $script:Failures) { if ($failure.exception) { $errorLines.Add([string]$failure.exception) } }
        if ($script:Result.Status -eq 'FAIL') { $errorLines.Add([string]$script:Result.Message) }
        Write-Utf8 (Join-Path $RunRoot 'stderr.txt') ($errorLines.ToArray() -join [Environment]::NewLine)
        Write-Json (Join-Path $RunRoot 'result.json') $script:Result 8
    }
    catch { $script:Result.Status = 'FAIL'; $script:Result.Message = 'Evidence finalization failed: ' + $_.Exception.ToString(); try { Write-Json (Join-Path $RunRoot 'result.json') $script:Result 8 } catch { } }
}
if ($script:Result.Status -cne 'PASS' -or -not $script:Record.restored) { exit 1 }
exit 0
