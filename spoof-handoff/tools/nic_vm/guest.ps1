[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [string]$Action = 'Probe',
    [string]$ExpectedProbeVerdict = 'NOT_MET'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:CommandRecords = [Collections.Generic.List[object]]::new()
$script:MutationRecords = [Collections.Generic.List[object]]::new()
$script:Failures = [Collections.Generic.List[object]]::new()
$script:Config = $null
$script:Result = [ordered]@{
    SchemaVersion = 1
    RunId = Split-Path -Leaf $RunRoot
    Status = 'FAIL'
    Message = 'The NIC VM phase did not finish.'
    ExpectedProbeVerdict = $ExpectedProbeVerdict
}
$script:Record = [ordered]@{
    schema = 'vm-nic-phase.v1'
    capturedUtc = [DateTime]::UtcNow.ToString('o')
    phase = $null
    readOnly = $false
    hostOrGuestSettingsChanged = $false
    commandRecords = @()
    mutationRecords = @()
    failures = @()
}

function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Write-Json([string]$Path, [object]$Value, [int]$Depth = 24) {
    Write-Utf8 $Path (ConvertTo-Json -InputObject $Value -Depth $Depth)
}

function Get-Sha256Hex([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}

function Invoke-RecordedProcess {
    param([Parameter(Mandatory = $true)][string]$Program,
          [Parameter(Mandatory = $true)][string[]]$Arguments,
          [int[]]$AcceptedExitCodes = @(0))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.Arguments = (($Arguments | ForEach-Object {
        '"' + ([string]$_).Replace('"', '\"') + '"'
    }) -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $stdout = ''
    $stderr = ''
    $exitCode = $null
    try {
        if (-not $process.Start()) { throw ('Process did not start: ' + $Program) }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(120000)) {
            try { $process.Kill() } catch { }
            $stdout = $stdoutTask.Result
            $stderr = $stderrTask.Result
            $exitCode = -1
            throw ('Process timed out after 120 seconds: ' + $Program)
        }
        $stdout = $stdoutTask.Result
        $stderr = $stderrTask.Result
        $exitCode = $process.ExitCode
    }
    catch {
        if ($null -eq $exitCode) { $exitCode = -1 }
        if ($process -and $process.HasExited) {
            try { $stdout = $process.StandardOutput.ReadToEnd() } catch { }
            try { $stderr = $process.StandardError.ReadToEnd() } catch { }
        }
        $record = [ordered]@{ program = $Program; arguments = @($Arguments); exitCode = $exitCode; stdout = $stdout; stderr = $stderr; exception = $_.Exception.ToString() }
        $script:CommandRecords.Add($record)
        throw
    }
    finally { $process.Dispose() }
    $record = [ordered]@{ program = $Program; arguments = @($Arguments); exitCode = $exitCode; stdout = $stdout; stderr = $stderr }
    $script:CommandRecords.Add($record)
    if ($AcceptedExitCodes -notcontains $exitCode) {
        throw ('Command failed: ' + $Program + ' exit=' + $exitCode + '; stderr=' + $stderr.Trim() + '; stdout=' + $stdout.Trim())
    }
    return $record
}

function Get-BcdSnapshot {
    $exe = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
    $command = Invoke-RecordedProcess $exe @('/enum', '{current}')
    $match = [regex]::Match([string]$command.stdout, '(?im)^\s*testsigning\s+(Yes|No|On|Off)\s*$')
    $exists = $match.Success
    $value = if (-not $exists) { $null } elseif ($match.Groups[1].Value -match '^(?i:Yes|On)$') { 'on' } else { 'off' }
    return [ordered]@{ exitCode = $command.exitCode; stdout = $command.stdout; stderr = $command.stderr; testSigning = [ordered]@{ exists = $exists; value = $value } }
}

function Get-SecureBootSnapshot {
    try { return [ordered]@{ value = [bool](Confirm-SecureBootUEFI); error = $null } }
    catch { return [ordered]@{ value = $null; error = $_.Exception.ToString() } }
}

function Get-TrustMembership([string]$Thumbprint) {
    $membership = [ordered]@{}
    foreach ($name in @('Root', 'TrustedPublisher')) {
        $store = [Security.Cryptography.X509Certificates.X509Store]::new($name, 'LocalMachine')
        try {
            $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
            $membership[$name] = [bool](@($store.Certificates | Where-Object { $_.Thumbprint -ceq $Thumbprint }).Count -gt 0)
        }
        finally { $store.Close() }
    }
    return $membership
}

function Get-CodeIntegritySnapshot {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NicVmIntegrity {
    [StructLayout(LayoutKind.Sequential)] struct CI { public uint Length; public uint Flags; }
    [DllImport("ntdll.dll")] static extern uint NtQuerySystemInformation(int kind, ref CI data, int size, out uint returned);
    public static uint[] Read() { CI data=new CI(); data.Length=8; uint returned; uint status=NtQuerySystemInformation(103,ref data,8,out returned); return new uint[]{status,data.Flags,returned}; }
}
'@
    $value = [NicVmIntegrity]::Read()
    return [ordered]@{ ntStatus = ('0x{0:X8}' -f $value[0]); optionsHex = ('0x{0:X8}' -f $value[1]); returnedLength = $value[2]; testSigning = if ($value[0] -eq 0) { [bool]($value[1] -band 2) } else { $null } }
}

function Get-NormalizedGuid([AllowNull()][object]$Value) {
    if ($null -eq $Value) { return $null }
    $match = [regex]::Match([string]$Value, '(?i)(?<![0-9a-f])[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}(?![0-9a-f])')
    if (-not $match.Success) { return $null }
    try { return ([Guid]::Parse($match.Value)).ToString('D').ToUpperInvariant() } catch { return $null }
}

function ConvertTo-MacKey([AllowNull()][object]$Value) {
    if ($null -eq $Value) { return $null }
    $text = ([string]$Value) -replace '[^0-9A-Fa-f]', ''
    if ($text -notmatch '\A[0-9A-Fa-f]{12}\z') { return $null }
    return $text.ToUpperInvariant()
}

function Get-PropertyValue([object]$InputObject, [string]$Name) {
    if ($null -eq $InputObject) { return $null }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Get-MatchingRows([object[]]$Rows, [string]$Pnp, [string]$GuidProperty = '') {
    if ($GuidProperty) {
        $wanted = Get-NormalizedGuid $GuidProperty
        return @($Rows | Where-Object { (Get-NormalizedGuid (Get-PropertyValue $_ 'GUID')) -ceq $wanted -or (Get-NormalizedGuid (Get-PropertyValue $_ 'InterfaceGuid')) -ceq $wanted })
    }
    return @($Rows | Where-Object {
        [string]::Equals([string](Get-PropertyValue $_ 'PNPDeviceID'), $Pnp, [StringComparison]::OrdinalIgnoreCase) -or
        [string]::Equals([string](Get-PropertyValue $_ 'pnpDeviceId'), $Pnp, [StringComparison]::OrdinalIgnoreCase)
    })
}

function Assert-Profile {
    $profile = $script:Config.profile
    if ($null -eq $profile -or $null -eq $profile.machine -or $null -eq $profile.bindings) { throw 'Embedded VM profile is missing machine or binding records.' }
    if ([string]$profile.machine.manufacturer -notmatch '^(?i:VMware(?:,\s*Inc\.)?)$') { throw 'Embedded profile is not a VMware guest profile.' }
    $bindings = @($profile.bindings)
    if ($bindings.Count -ne 4) { throw ('Expected exactly four profile adapters; found ' + $bindings.Count) }
    $excludedPnp = @($script:Config.excludedPnpIds)
    $seen = @{}
    $seenGuids = @{}
    foreach ($binding in $bindings) {
        $pnp = [string]$binding.pnpDeviceId
        if ($pnp.Length -lt 1 -or $pnp.Length -gt 260 -or $pnp -notmatch '^[\x20-\x7e]+$' -or $pnp.Contains('"') -or $pnp.Contains('%')) { throw ('Profile PnP identity is not a bounded safe string: ' + $pnp) }
        if (@($excludedPnp | Where-Object { [string]::Equals([string]$_, $pnp, [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0) { throw ('Profile includes an excluded production host PnP: ' + $pnp) }
        $key = $pnp.ToUpperInvariant()
        if ($seen.ContainsKey($key)) { throw ('Duplicate profile PnP ID: ' + $pnp) }
        $seen[$key] = $true
        $interfaceGuid = Get-NormalizedGuid $binding.interfaceGuid
        if (-not $interfaceGuid) { throw ('Profile interface GUID is missing or invalid: ' + $pnp) }
        if ($seenGuids.ContainsKey($interfaceGuid)) { throw ('Duplicate profile interface GUID: ' + $interfaceGuid) }
        $seenGuids[$interfaceGuid] = $true
        if ((ConvertTo-MacKey $binding.original) -eq $null -or (ConvertTo-MacKey $binding.replacement) -eq $null -or
            (ConvertTo-MacKey $binding.original) -ceq (ConvertTo-MacKey $binding.replacement)) { throw ('Invalid original/replacement MAC pair in profile: ' + $pnp) }
    }
    return $bindings
}

function Assert-VmIdentity([bool]$RequireUuid) {
    $machine = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
    $product = Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop
    $manufacturer = [string]$machine.Manufacturer
    $actualUuid = [string]$product.UUID
    if ($manufacturer -notmatch '^(?i:VMware(?:,\s*Inc\.)?)$') { throw ('Mutation requires a VMware guest; actual manufacturer=' + $manufacturer) }
    if ($RequireUuid -and [string]::IsNullOrWhiteSpace([string]$script:Config.expectedVmUuid)) { throw 'Mutation phase requires a nonempty expected VM UUID.' }
    if (-not [string]::IsNullOrWhiteSpace([string]$script:Config.expectedVmUuid) -and $actualUuid -cne [string]$script:Config.expectedVmUuid) {
        throw ('VM UUID mismatch: expected=' + [string]$script:Config.expectedVmUuid + ' actual=' + $actualUuid)
    }
    if ($RequireUuid -and [string]$script:Config.profile.machine.uuid -cne $actualUuid) { throw 'Live VM UUID differs from the embedded profile UUID.' }
    return [ordered]@{ manufacturer = $manufacturer; model = [string]$machine.Model; uuid = $actualUuid }
}

function Assert-MutationPreconditions {
    $identity = Assert-VmIdentity $true
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'NIC VM mutation phase requires an elevated guest process.' }
    $secure = Get-SecureBootSnapshot
    if ($null -ne $secure.error) { throw ('Cannot establish VM Secure Boot state: ' + $secure.error) }
    if ($secure.value -ne $false) { throw 'VM Secure Boot must be OFF before NIC filter, trust, or test-signing mutations.' }
    $identity.secureBoot = $secure
    $script:Record.host = $identity
}

function Write-Journal([object]$Journal) {
    $path = Join-Path 'C:\ProgramData\ClientNicVmTest' 'journal.json'
    $temporary = $path + '.new'
    Write-Json $temporary $Journal 16
    if (Test-Path -LiteralPath $path) { [IO.File]::Replace($temporary, $path, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $path) }
}

function Read-Journal {
    $path = Join-Path 'C:\ProgramData\ClientNicVmTest' 'journal.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw 'Owned NIC VM journal is absent; restore cannot infer prior policy.' }
    $parsedJournal = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    $journal = [ordered]@{}
    foreach ($property in $parsedJournal.PSObject.Properties) { $journal[$property.Name] = $property.Value }
    if ([string]$journal.schema -cne 'client-nic-vm-journal.v1' -or [string]$journal.vmUuid -cne [string]$script:Record.host.uuid) { throw 'NIC VM journal schema or VM identity mismatch.' }
    if ([string]$journal.certificateThumbprint -cne [string]$script:Config.publicCertificate.thumbprint -or
        [string]$journal.certificateSha256 -cne [string]$script:Config.publicCertificate.sha256 -or
        [bool]$journal.baselineDirectoryExisted) { throw 'NIC VM journal trust pin or owned-root baseline is invalid.' }
    return $journal
}

function Get-NativeCapture {
    $nativePath = Join-Path $RunRoot 'nic_native_read.exe'
    $nativeBytes = [Convert]::FromBase64String([string]$script:Config.nativeExe.base64)
    $nativeHash = Get-Sha256Hex $nativeBytes
    if ($nativeHash -cne [string]$script:Config.nativeExe.sha256) { throw 'Embedded native NIC probe hash mismatch.' }
    [IO.File]::WriteAllBytes($nativePath, $nativeBytes)
    $reportPath = Join-Path $RunRoot ('native.' + [Guid]::NewGuid().ToString('N') + '.json')
    $nativeCommand = Invoke-RecordedProcess $nativePath @('--capture', $reportPath)
    $data = $null
    if (Test-Path -LiteralPath $reportPath -PathType Leaf) {
        try { $data = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json }
        catch { $script:Failures.Add([ordered]@{ source = 'native_json'; exception = $_.Exception.ToString(); path = $reportPath }) }
    }
    else { $script:Failures.Add([ordered]@{ source = 'native_json'; error = 'Native probe did not create its requested CREATE_NEW report.'; exitCode = $nativeCommand.exitCode }) }
    return [ordered]@{ executableSha256 = $nativeHash; reportPath = $reportPath; exitCode = $nativeCommand.exitCode; stdout = $nativeCommand.stdout; stderr = $nativeCommand.stderr; data = $data }
}

function Get-FreshWindowsReads {
    $reads = [ordered]@{}
    foreach ($entry in @(
        [pscustomobject]@{ key = 'win32NetworkAdapter'; namespace = 'root\CIMV2'; class = 'Win32_NetworkAdapter' },
        [pscustomobject]@{ key = 'msftNetAdapter'; namespace = 'root\StandardCimv2'; class = 'MSFT_NetAdapter' }
    )) {
        try {
            $rows = @(Get-CimInstance -Namespace $entry.namespace -ClassName $entry.class -OperationTimeoutSec 30 -ErrorAction Stop)
            $reads[$entry.key] = [ordered]@{ namespace = $entry.namespace; class = $entry.class; error = $null; rows = @($rows | ForEach-Object {
                [ordered]@{
                    Name = Get-PropertyValue $_ 'Name'; Description = Get-PropertyValue $_ 'Description';
                    DeviceID = Get-PropertyValue $_ 'DeviceID'; Index = Get-PropertyValue $_ 'Index';
                    InterfaceIndex = Get-PropertyValue $_ 'InterfaceIndex'; GUID = Get-PropertyValue $_ 'GUID';
                    InterfaceGuid = Get-PropertyValue $_ 'InterfaceGuid'; PNPDeviceID = Get-PropertyValue $_ 'PNPDeviceID';
                    MACAddress = Get-PropertyValue $_ 'MACAddress'; NetworkAddresses = @(Get-PropertyValue $_ 'NetworkAddresses');
                    PermanentAddress = Get-PropertyValue $_ 'PermanentAddress'; NetEnabled = Get-PropertyValue $_ 'NetEnabled';
                    PhysicalAdapter = Get-PropertyValue $_ 'PhysicalAdapter'
                }
            }) }
        }
        catch {
            $failure = [ordered]@{ source = $entry.key; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId }
            $script:Failures.Add($failure)
            $reads[$entry.key] = [ordered]@{ namespace = $entry.namespace; class = $entry.class; error = $failure; rows = @() }
        }
    }
    try {
        $rows = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop | ForEach-Object {
            [ordered]@{ Name = $_.Name; InterfaceDescription = $_.InterfaceDescription; InterfaceIndex = $_.InterfaceIndex; InterfaceGuid = $_.InterfaceGuid; MacAddress = $_.MacAddress; Status = [string]$_.Status }
        })
        $reads.getNetAdapter = [ordered]@{ command = 'Get-NetAdapter -IncludeHidden'; error = $null; rows = $rows }
    }
    catch {
        $failure = [ordered]@{ source = 'getNetAdapter'; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId }
        $script:Failures.Add($failure)
        $reads.getNetAdapter = [ordered]@{ command = 'Get-NetAdapter -IncludeHidden'; error = $failure; rows = @() }
    }
    try {
        $bindings = @(Get-NetAdapterBinding -Name '*' -AllBindings -ErrorAction Stop | ForEach-Object {
            [ordered]@{ Name = $_.Name; DisplayName = $_.DisplayName; ComponentID = $_.ComponentID; Enabled = [bool]$_.Enabled }
        })
        $reads.adapterBindings = [ordered]@{ command = 'Get-NetAdapterBinding -Name * -AllBindings'; error = $null; rows = $bindings }
    }
    catch {
        $failure = [ordered]@{ source = 'adapterBindings'; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId }
        $script:Failures.Add($failure)
        $reads.adapterBindings = [ordered]@{ command = 'Get-NetAdapterBinding -Name * -AllBindings'; error = $failure; rows = @() }
    }
    return $reads
}

function Get-Evaluations([object]$NativeData, [object]$WindowsReads, [object[]]$ProfileBindings, [string]$ExpectedMode, [ValidateSet('CurrentSeven','LegacySix')][string]$ReaderSet = 'CurrentSeven') {
    if ($ExpectedMode -notin @('Original', 'Replacement')) { throw 'Readback ExpectedMode must be Original or Replacement.' }
    $ndis = @($NativeData.setupDiNdisLanClass.interfaces)
    $gaa = @($NativeData.getAdaptersAddresses.entries)
    $gaiData = Get-PropertyValue $NativeData 'getAdaptersInfo'
    $gai = @(Get-PropertyValue $gaiData 'entries')
    $ifTable = @($NativeData.getIfTable2.entries)
    $win32 = @($WindowsReads.win32NetworkAdapter.rows)
    $msft = @($WindowsReads.msftNetAdapter.rows)
    $plain = @($WindowsReads.getNetAdapter.rows)
    $evaluations = [Collections.Generic.List[object]]::new()
    foreach ($binding in $ProfileBindings) {
        $pnp = [string]$binding.pnpDeviceId
        $profileGuid = Get-NormalizedGuid $binding.interfaceGuid
        $ndisMatches = @($ndis | Where-Object { [string]::Equals([string]$_.pnpDeviceId, $pnp, [StringComparison]::OrdinalIgnoreCase) })
        $guid = if ($ndisMatches.Count -eq 1) { Get-NormalizedGuid $ndisMatches[0].netCfgInstanceId } else { $null }
        $winMatches = @($win32 | Where-Object { [string]::Equals([string]$_.PNPDeviceID, $pnp, [StringComparison]::OrdinalIgnoreCase) })
        $targetGuid = $profileGuid
        $gaaMatches = @($gaa | Where-Object { (Get-NormalizedGuid $_.adapterName) -ceq $targetGuid })
        $gaiMatches = @($gai | Where-Object { $null -ne $_ -and (Get-NormalizedGuid $_.adapterName) -ceq $targetGuid })
        $ifMatches = @($ifTable | Where-Object { (Get-NormalizedGuid $_.interfaceGuid) -ceq $targetGuid })
        $msftMatches = @($msft | Where-Object { (Get-NormalizedGuid $_.InterfaceGuid) -ceq $targetGuid -or [string]::Equals([string]$_.PNPDeviceID, $pnp, [StringComparison]::OrdinalIgnoreCase) })
        $plainMatches = @($plain | Where-Object { (Get-NormalizedGuid $_.InterfaceGuid) -ceq $targetGuid })
        $expectedText = if ($ExpectedMode -ceq 'Original') { [string]$binding.original } else { [string]$binding.replacement }
        $expectedKey = ConvertTo-MacKey $expectedText
        $oid = if ($ndisMatches.Count -eq 1) { $ndisMatches[0].ethernetAddress } else { $null }
        $dot11 = if ($ndisMatches.Count -eq 1) { $ndisMatches[0].dot11Address } else { $null }
        $observed = [ordered]@{
            oid8023Current = [string](Get-PropertyValue $oid 'mac')
            oid8023Win32Error = Get-PropertyValue $oid 'win32Error'
            oid8023IoctlSucceeded = Get-PropertyValue $oid 'ioctlSucceeded'
            oid8023BytesHex = Get-PropertyValue $oid 'outputBytesHex'
            oidDot11Current = [string](Get-PropertyValue $dot11 'mac')
            oidDot11Win32Error = Get-PropertyValue $dot11 'win32Error'
            oidDot11IoctlSucceeded = Get-PropertyValue $dot11 'ioctlSucceeded'
            getAdaptersAddresses = if ($gaaMatches.Count -eq 1) { [string]$gaaMatches[0].physicalAddress } else { $null }
            getAdaptersInfo = if ($gaiMatches.Count -eq 1) { [string]$gaiMatches[0].address } else { $null }
            getIfTable2 = if ($ifMatches.Count -eq 1) { [string]$ifMatches[0].physicalAddress } else { $null }
            win32Wmi = if ($winMatches.Count -eq 1) { [string]$winMatches[0].MACAddress } else { $null }
            msftNetworkAddresses = if ($msftMatches.Count -eq 1) { @($msftMatches[0].NetworkAddresses) } else { @() }
            msftPermanentAddress = if ($msftMatches.Count -eq 1) { [string]$msftMatches[0].PermanentAddress } else { $null }
            getNetAdapter = if ($plainMatches.Count -eq 1) { [string]$plainMatches[0].MacAddress } else { $null }
        }
        $checks = [Collections.Generic.List[object]]::new()
        function Add-MacCheck([string]$Source, [object]$Actual, [string]$Needle) {
            $actualText = @(if ($null -eq $Actual) { } elseif ($Actual -is [array]) { $Actual | ForEach-Object { [string]$_ } } else { [string]$Actual })
            $valid = $actualText.Count -gt 0 -and @($actualText | Where-Object { (ConvertTo-MacKey $_) -cne $expectedKey }).Count -eq 0
            $checks.Add([ordered]@{ source = $Source; expected = $expectedText; actual = $Actual; matchCount = $actualText.Count; matchesExpected = $valid; detail = $Needle })
        }
        Add-MacCheck 'OID_802_3_CURRENT_ADDRESS' $observed.oid8023Current ('ioctlSucceeded=' + $observed.oid8023IoctlSucceeded + '; win32Error=' + $observed.oid8023Win32Error + '; bytes=' + $observed.oid8023BytesHex)
        Add-MacCheck 'GetAdaptersAddresses' $observed.getAdaptersAddresses ('matchCount=' + $gaaMatches.Count + '; adapterNameGuid=' + $targetGuid)
        if ($ReaderSet -ceq 'CurrentSeven') {
            Add-MacCheck 'GetAdaptersInfo' $observed.getAdaptersInfo ('matchCount=' + $gaiMatches.Count + '; adapterNameGuid=' + $targetGuid + '; independent IP Helper API')
        }
        Add-MacCheck 'GetIfTable2' $observed.getIfTable2 ('matchCount=' + $ifMatches.Count + '; interfaceGuid=' + $targetGuid)
        Add-MacCheck 'Win32_NetworkAdapter.MACAddress' $observed.win32Wmi ('matchCount=' + $winMatches.Count)
        Add-MacCheck 'MSFT_NetAdapter.NetworkAddresses' $observed.msftNetworkAddresses ('matchCount=' + $msftMatches.Count + '; PermanentAddress is informational only.')
        Add-MacCheck 'Get-NetAdapter.MacAddress' $observed.getNetAdapter ('matchCount=' + $plainMatches.Count)
        $identityChecks = @(
            [ordered]@{ source = 'profile interface GUID'; expected = $profileGuid; actual = $guid; matchesExpected = ($ndisMatches.Count -eq 1 -and $guid -ceq $profileGuid) },
            [ordered]@{ source = 'Win32_NetworkAdapter GUID'; expected = $profileGuid; actual = if ($winMatches.Count -eq 1) { Get-NormalizedGuid $winMatches[0].GUID } else { $null }; matchesExpected = ($winMatches.Count -eq 1 -and (Get-NormalizedGuid $winMatches[0].GUID) -ceq $profileGuid) },
            [ordered]@{ source = 'MSFT_NetAdapter GUID'; expected = $profileGuid; actual = if ($msftMatches.Count -eq 1) { Get-NormalizedGuid $msftMatches[0].InterfaceGuid } else { $null }; matchesExpected = ($msftMatches.Count -eq 1 -and (Get-NormalizedGuid $msftMatches[0].InterfaceGuid) -ceq $profileGuid) }
        )
        $allMatch = @($checks | Where-Object { $_.matchesExpected -ne $true }).Count -eq 0 -and
            @($identityChecks | Where-Object { $_.matchesExpected -ne $true }).Count -eq 0 -and
            $ndisMatches.Count -eq 1 -and $winMatches.Count -eq 1 -and $targetGuid
        $evaluations.Add([ordered]@{
            pnpDeviceId = $pnp
            profileInterfaceGuid = $profileGuid
            netCfgInstanceGuid = $guid
            expectedMode = $ExpectedMode
            expected = $expectedText
            setupDiNdisMatchCount = $ndisMatches.Count
            win32WmiMatchCount = $winMatches.Count
            getAdaptersAddressesMatchCount = $gaaMatches.Count
            getIfTable2MatchCount = $ifMatches.Count
            msftNetAdapterMatchCount = $msftMatches.Count
            getNetAdapterMatchCount = $plainMatches.Count
            observations = $observed
            checks = @($checks.ToArray())
            identityChecks = $identityChecks
            dot11Status = [ordered]@{ ioctlSucceeded = $observed.oidDot11IoctlSucceeded; win32Error = $observed.oidDot11Win32Error; mac = $observed.oidDot11Current; interpretation = 'Recorded independently; unsupported DOT11 OID is not treated as an Ethernet-address result.' }
            allRequiredSourcesMatch = [bool]$allMatch
        })
    }
    return @($evaluations.ToArray())
}

function Get-FilterBindingEvaluations([object]$WindowsReads, [object[]]$ProfileBindings) {
    $win32 = @($WindowsReads.win32NetworkAdapter.rows)
    $plain = @($WindowsReads.getNetAdapter.rows)
    $bindings = @($WindowsReads.adapterBindings.rows)
    $evaluations = [Collections.Generic.List[object]]::new()
    foreach ($profileBinding in $ProfileBindings) {
        $pnp = [string]$profileBinding.pnpDeviceId
        $expectedGuid = Get-NormalizedGuid $profileBinding.interfaceGuid
        $winMatches = @($win32 | Where-Object { [string]::Equals([string]$_.PNPDeviceID, $pnp, [StringComparison]::OrdinalIgnoreCase) })
        $adapterMatches = @($plain | Where-Object { (Get-NormalizedGuid $_.InterfaceGuid) -ceq $expectedGuid })
        $adapterName = if ($adapterMatches.Count -eq 1) { [string]$adapterMatches[0].Name } else { $null }
        $filterMatches = @($bindings | Where-Object { [string]::Equals([string]$_.Name, $adapterName, [StringComparison]::OrdinalIgnoreCase) -and [string]$_.ComponentID -ceq 'HypersinvNicAddressFilter' })
        $wmiGuid = if ($winMatches.Count -eq 1) { Get-NormalizedGuid $winMatches[0].GUID } else { $null }
        $enabled = $filterMatches.Count -eq 1 -and [bool]$filterMatches[0].Enabled
        $evaluations.Add([ordered]@{
            pnpDeviceId = $pnp
            expectedGuid = $expectedGuid
            win32MatchCount = $winMatches.Count
            win32Guid = $wmiGuid
            plainAdapterMatchCount = $adapterMatches.Count
            adapterName = $adapterName
            filterBindingMatchCount = $filterMatches.Count
            filterBindings = $filterMatches
            enabled = $enabled
            matchesExpected = ($winMatches.Count -eq 1 -and $wmiGuid -ceq $expectedGuid -and $adapterMatches.Count -eq 1 -and $filterMatches.Count -eq 1 -and $enabled)
        })
    }
    return @($evaluations.ToArray())
}

function Invoke-PreparePolicy {
    Assert-MutationPreconditions
    $secure = Get-SecureBootSnapshot
    $bcd = Get-BcdSnapshot
    if ($bcd.exitCode -ne 0) { throw 'Could not read current BCD test-signing element.' }
    $certificateBytes = [Convert]::FromBase64String([string]$script:Config.publicCertificate.base64)
    $certificateHash = Get-Sha256Hex $certificateBytes
    if ($certificateHash -cne [string]$script:Config.publicCertificate.sha256) { throw 'Embedded public certificate hash mismatch.' }
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($certificateBytes)
    if ($certificate.Thumbprint -cne [string]$script:Config.publicCertificate.thumbprint) { throw 'Public certificate is not the pinned existing signer.' }
    $ownedRoot = 'C:\ProgramData\ClientNicVmTest'
    if (Test-Path -LiteralPath $ownedRoot) { throw 'C:\ProgramData\ClientNicVmTest must be absent before PreparePolicy so the original guest state is recorded without adoption.' }
    $trust = Get-TrustMembership $certificate.Thumbprint
    $journal = [ordered]@{
        schema = 'client-nic-vm-journal.v1'
        createdUtc = [DateTime]::UtcNow.ToString('o')
        vmUuid = [string]$script:Record.host.uuid
        baselineDirectoryExisted = $false
        secureBoot = $secure
        testSigning = $bcd.testSigning
        trustMembership = $trust
        certificateThumbprint = $certificate.Thumbprint
        certificateSha256 = $certificateHash
        expectedPnpIds = @($script:Config.profile.bindings | ForEach-Object { [string]$_.pnpDeviceId })
        package = $null
        publishedInf = $null
        installState = 'not-started'
    }
    [void](New-Item -ItemType Directory -Path $ownedRoot)
    Write-Journal $journal
    foreach ($storeName in @('Root', 'TrustedPublisher')) {
        if (-not [bool]$trust[$storeName]) {
            $store = [Security.Cryptography.X509Certificates.X509Store]::new($storeName, 'LocalMachine')
            try { $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite); $store.Add($certificate) }
            finally { $store.Close() }
            $script:MutationRecords.Add([ordered]@{ operation = 'certificate-import'; store = $storeName; thumbprint = $certificate.Thumbprint; presentBefore = $false; presentAfter = $true })
            $script:Record.hostOrGuestSettingsChanged = $true
        }
    }
    $command = Invoke-RecordedProcess (Join-Path $env:SystemRoot 'System32\bcdedit.exe') @('/set', '{current}', 'testsigning', 'on')
    $script:Record.hostOrGuestSettingsChanged = $true
    $journal.installState = 'policy-prepared'
    $journal.policyPreparedUtc = [DateTime]::UtcNow.ToString('o')
    Write-Journal $journal
}

function Get-ExactPublishedInf([string]$ExpectedHash) {
    $matches = [Collections.Generic.List[string]]::new()
    foreach ($file in @(Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' -File -ErrorAction Stop)) {
        try { if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ceq $ExpectedHash) { $matches.Add($file.Name) } }
        catch { $script:Failures.Add([ordered]@{ source = 'published_inf_hash'; path = $file.FullName; exception = $_.Exception.ToString() }) }
    }
    return @($matches.ToArray())
}

function Invoke-Install {
    Assert-MutationPreconditions
    $journal = Read-Journal
    if ([string]$journal.installState -notin @('policy-prepared', 'install-failed')) { throw ('NIC package install state is not ready: ' + [string]$journal.installState) }
    $policy = Get-BcdSnapshot
    if ($policy.exitCode -ne 0 -or -not $policy.testSigning.exists -or $policy.testSigning.value -cne 'on') { throw 'VM test-signing must be effective before installing the signed NIC filter.' }
    $packageRoot = Join-Path 'C:\ProgramData\ClientNicVmTest' 'package'
    if (Test-Path -LiteralPath $packageRoot) { throw 'Owned package directory already exists; remove or restore the previous VM run first.' }
    [void](New-Item -ItemType Directory -Path $packageRoot)
    $package = [ordered]@{ files = @(); directory = $packageRoot; infSha256 = $null; sysSha256 = $null; catSha256 = $null }
    foreach ($file in @($script:Config.package.files)) {
        if ([string]$file.name -notin @('client_nic_filter.sys', 'client_nic_filter.inf', 'client_nic_filter.cat')) { throw ('Unexpected package member: ' + [string]$file.name) }
        $bytes = [Convert]::FromBase64String([string]$file.base64)
        $hash = Get-Sha256Hex $bytes
        if ($hash -cne [string]$file.sha256) { throw ('Embedded package hash mismatch: ' + [string]$file.name) }
        $target = Join-Path $packageRoot ([string]$file.name)
        [IO.File]::WriteAllBytes($target, $bytes)
        $package.files += [ordered]@{ name = [string]$file.name; sha256 = $hash; size = $bytes.Length }
        switch ([string]$file.name) {
            'client_nic_filter.inf' { $package.infSha256 = $hash }
            'client_nic_filter.sys' { $package.sysSha256 = $hash }
            'client_nic_filter.cat' { $package.catSha256 = $hash }
        }
    }
    if ($package.files.Count -ne 3 -or -not $package.infSha256 -or -not $package.sysSha256 -or -not $package.catSha256) { throw 'Install package must contain exactly the pinned SYS, INF, and CAT.' }
    $driverSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $packageRoot 'client_nic_filter.sys')
    $catalogSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $packageRoot 'client_nic_filter.cat')
    $signatureEvidence = [ordered]@{
        driverStatus = [string]$driverSignature.Status
        driverStatusMessage = [string]$driverSignature.StatusMessage
        driverSignerThumbprint = if ($driverSignature.SignerCertificate) { [string]$driverSignature.SignerCertificate.Thumbprint } else { $null }
        catalogStatus = [string]$catalogSignature.Status
        catalogStatusMessage = [string]$catalogSignature.StatusMessage
        catalogSignerThumbprint = if ($catalogSignature.SignerCertificate) { [string]$catalogSignature.SignerCertificate.Thumbprint } else { $null }
    }
    $script:Record.packageSignatures = $signatureEvidence
    if ($driverSignature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
        $signatureEvidence.driverSignerThumbprint -cne [string]$script:Config.publicCertificate.thumbprint) {
        throw ('Driver signature is not valid under the pinned signer: status=' + $signatureEvidence.driverStatus + ' signer=' + $signatureEvidence.driverSignerThumbprint)
    }
    if ($catalogSignature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
        $signatureEvidence.catalogSignerThumbprint -cne [string]$script:Config.publicCertificate.thumbprint) {
        throw ('Catalog signature is not valid under the pinned signer: status=' + $signatureEvidence.catalogStatus + ' signer=' + $signatureEvidence.catalogSignerThumbprint)
    }
    $journal.package = $package
    $journal.installState = 'install-pending'
    $journal.installIntentUtc = [DateTime]::UtcNow.ToString('o')
    Write-Journal $journal
    $script:Record.hostOrGuestSettingsChanged = $true
    $netcfg = Join-Path $env:SystemRoot 'System32\netcfg.exe'
    [void](Invoke-RecordedProcess $netcfg @('/l', (Join-Path $packageRoot 'client_nic_filter.inf'), '/c', 's', '/i', 'HypersinvNicAddressFilter') @(0, 3010))
    $published = @(Get-ExactPublishedInf ([string]$package.infSha256))
    if ($published.Count -ne 1) {
        $journal.installState = 'installed-published-inf-unresolved'
        $journal.publishedInfMatches = $published
        Write-Journal $journal
        throw ('Installed INF ownership could not be uniquely established by exact SHA-256; matching count=' + $published.Count)
    }
    $journal.publishedInf = $published[0]
    $journal.installState = 'installed'
    $journal.installedUtc = [DateTime]::UtcNow.ToString('o')
    Write-Journal $journal
}

function Invoke-Start {
    Assert-MutationPreconditions
    $journal = Read-Journal
    if ($journal.installState -cne 'installed') { throw 'Only the owned installed NIC package may be started.' }
    $services = @(Get-CimInstance Win32_SystemDriver -Filter "Name='HypersinvNicAddressFilter'")
    if ($services.Count -ne 1) { throw 'Expected exactly one owned NIC service before starting.' }
    $image = [Environment]::ExpandEnvironmentVariables([string]$services[0].PathName).Trim('"')
    if (-not (Test-Path -LiteralPath $image -PathType Leaf) -or (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -cne [string]$journal.package.sysSha256) { throw 'Installed NIC image differs from the journal package hash.' }
    if ($services[0].State -cne 'Running') {
        [void](Invoke-RecordedProcess (Join-Path $env:SystemRoot 'System32\sc.exe') @('start','HypersinvNicAddressFilter'))
        $script:Record.hostOrGuestSettingsChanged = $true
    }
    $after = @(Get-CimInstance Win32_SystemDriver -Filter "Name='HypersinvNicAddressFilter'")
    if ($after.Count -ne 1 -or $after[0].State -cne 'Running') { throw 'The owned NIC driver did not become Running.' }
}

function Invoke-Remove {
    param([switch]$PreserveConfiguredAddress)
    Assert-MutationPreconditions
    $journal = Read-Journal
    if ($PreserveConfiguredAddress -and (-not $journal.Contains('addressState') -or $journal.addressState -cne 'applied')) { throw 'Unfiltered comparison requires an applied four-adapter address journal.' }
    if (-not $PreserveConfiguredAddress -and $journal.Contains('addressState') -and $journal.addressState -notin @('captured', 'restored')) { throw ('Restore the saved adapter-address settings before filter removal; state=' + $journal.addressState) }
    if ([string]$journal.installState -notin @('install-pending', 'installed', 'installed-published-inf-unresolved', 'remove-pending', 'removal-incomplete')) { throw ('No owned NIC install to remove; state=' + [string]$journal.installState) }
    $priorInstallState = [string]$journal.installState
    $journal.installState = 'remove-pending'
    $journal.removeIntentUtc = [DateTime]::UtcNow.ToString('o')
    Write-Journal $journal
    $script:Record.hostOrGuestSettingsChanged = $true
    $netcfgPath = Join-Path $env:SystemRoot 'System32\netcfg.exe'
    $componentQuery = Invoke-RecordedProcess $netcfgPath @('/q', 'HypersinvNicAddressFilter') @(0, 1)
    $netcfgRemoval = $null
    if ($componentQuery.exitCode -eq 0) {
        $netcfgRemoval = Invoke-RecordedProcess $netcfgPath @('/u', 'HypersinvNicAddressFilter') @(0, 3010)
    }
    else {
        $script:MutationRecords.Add([ordered]@{ operation = 'netcfg-remove'; component = 'HypersinvNicAddressFilter'; alreadyAbsent = $true; queryExitCode = $componentQuery.exitCode })
    }
    $journal.removalRebootRequired = ($null -ne $netcfgRemoval -and $netcfgRemoval.exitCode -eq 3010)
    if ([string]::IsNullOrWhiteSpace([string]$journal.publishedInf) -and $priorInstallState -eq 'installed-published-inf-unresolved') {
        $hashMatches = @(Get-ExactPublishedInf ([string]$journal.package.infSha256))
        if ($hashMatches.Count -gt 1) { throw ('Cannot safely attribute one owned published INF after netcfg removal; exact-hash matches=' + ($hashMatches -join ',')) }
        if ($hashMatches.Count -eq 1) { $journal.publishedInf = $hashMatches[0]; Write-Journal $journal }
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$journal.publishedInf)) {
        $ownedInf = [string]$journal.publishedInf
        if ($ownedInf -notmatch '^(?i:oem[0-9]+\.inf)$') { throw 'Journal published INF name is invalid.' }
        $ownedPath = Join-Path (Join-Path $env:SystemRoot 'INF') $ownedInf
        if (Test-Path -LiteralPath $ownedPath -PathType Leaf) {
            $actualHash = (Get-FileHash -LiteralPath $ownedPath -Algorithm SHA256).Hash
            if ($actualHash -cne [string]$journal.package.infSha256) { throw ('Refusing to remove non-owned published INF: expected=' + [string]$journal.package.infSha256 + ' actual=' + $actualHash) }
            $removeInf = Invoke-RecordedProcess (Join-Path $env:SystemRoot 'System32\pnputil.exe') @('/delete-driver', $ownedInf, '/uninstall') @(0, 3010)
            $journal.infRemovalRebootRequired = ($removeInf.exitCode -eq 3010)
            if (Test-Path -LiteralPath $ownedPath -PathType Leaf) {
                $remainingHash = (Get-FileHash -LiteralPath $ownedPath -Algorithm SHA256).Hash
                if ($remainingHash -ceq [string]$journal.package.infSha256) {
                    $journal.installState = 'removal-incomplete'
                    Write-Journal $journal
                    throw ('Owned published INF remains after deletion request; exit=' + $removeInf.exitCode + ' sha256=' + $remainingHash)
                }
            }
        }
        else { $script:MutationRecords.Add([ordered]@{ operation = 'published-inf-remove'; file = $ownedInf; alreadyAbsent = $true }) }
    }
    $remainingService = @(Get-CimInstance -ClassName Win32_SystemDriver -ErrorAction Stop | Where-Object { $_.Name -ceq 'HypersinvNicAddressFilter' })
    $remainingBindings = @(Get-NetAdapterBinding -Name '*' -AllBindings -ErrorAction Stop | Where-Object { $_.ComponentID -ceq 'HypersinvNicAddressFilter' })
    if ($remainingService.Count -ne 0 -or $remainingBindings.Count -ne 0) {
        $journal.installState = 'removal-incomplete'
        $journal.removeVerification = [ordered]@{ remainingService = $remainingService.Count; remainingBindings = $remainingBindings.Count }
        Write-Journal $journal
        throw ('NIC filter teardown incomplete: remaining service rows=' + $remainingService.Count + '; adapter binding rows=' + $remainingBindings.Count)
    }
    $journal.installState = 'removed'
    $journal.removedUtc = [DateTime]::UtcNow.ToString('o')
    Write-Journal $journal
}

function Restore-Policy {
    Assert-MutationPreconditions
    $journal = Read-Journal
    if ($journal.Contains('addressState') -and $journal.addressState -notin @('captured', 'restored')) { throw ('Restore adapter-address state before policy cleanup; state=' + $journal.addressState) }
    if ($journal.installState -notin @('removed', 'not-started', 'policy-prepared', 'install-failed')) { throw ('Remove the owned NIC package before restoring policy; state=' + [string]$journal.installState) }
    $script:Record.hostOrGuestSettingsChanged = $true
    $thumb = [string]$journal.certificateThumbprint
    foreach ($storeName in @('Root', 'TrustedPublisher')) {
        $store = [Security.Cryptography.X509Certificates.X509Store]::new($storeName, 'LocalMachine')
        try {
            $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
            $matches = @($store.Certificates | Where-Object { $_.Thumbprint -ceq $thumb })
            $baselineMembership = [bool]$journal.trustMembership.PSObject.Properties[$storeName].Value
            if ($baselineMembership) {
                if ($matches.Count -eq 0) {
                    $certBytes = [Convert]::FromBase64String([string]$script:Config.publicCertificate.base64)
                    $store.Add([Security.Cryptography.X509Certificates.X509Certificate2]::new($certBytes))
                    $script:MutationRecords.Add([ordered]@{ operation = 'certificate-restore'; store = $storeName; thumbprint = $thumb; presentBefore = $false; restored = $true })
                }
            }
            else {
                foreach ($match in $matches) { $store.Remove($match) }
                $script:MutationRecords.Add([ordered]@{ operation = 'certificate-restore'; store = $storeName; thumbprint = $thumb; presentBefore = ($matches.Count -gt 0); restored = $true })
            }
        }
        finally { $store.Close() }
    }
    $original = $journal.testSigning
    $current = Get-BcdSnapshot
    if ($current.exitCode -ne 0) { throw 'Could not read current BCD before restoring test-signing.' }
    if ([bool]$original.exists) {
        if (-not $current.testSigning.exists -or [string]$current.testSigning.value -cne [string]$original.value) {
            $value = if ([string]$original.value -ceq 'on') { 'on' } else { 'off' }
            [void](Invoke-RecordedProcess (Join-Path $env:SystemRoot 'System32\bcdedit.exe') @('/set', '{current}', 'testsigning', $value))
        }
    }
    elseif ($current.testSigning.exists) {
        [void](Invoke-RecordedProcess (Join-Path $env:SystemRoot 'System32\bcdedit.exe') @('/deletevalue', '{current}', 'testsigning'))
    }
    $journal.installState = 'restored'
    $journal.restoredUtc = [DateTime]::UtcNow.ToString('o')
    Write-Journal $journal
    $script:Record.restoredJournal = $journal
    if (-not [bool]$journal.baselineDirectoryExisted) {
        $ownedRoot = 'C:\ProgramData\ClientNicVmTest'
        if (Test-Path -LiteralPath $ownedRoot -PathType Container) {
            $files = @(Get-ChildItem -LiteralPath $ownedRoot -Recurse -File -Force)
            $allowed = @('journal.json', 'package\client_nic_filter.sys', 'package\client_nic_filter.inf', 'package\client_nic_filter.cat')
            foreach ($file in $files) {
                if (-not $file.FullName.StartsWith(($ownedRoot.TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase)) { throw 'Owned cleanup escaped the client NIC VM test root.' }
                $relative = $file.FullName.Substring($ownedRoot.TrimEnd('\').Length + 1).Replace('/', '\')
                if ($relative -notin $allowed) { throw ('Refusing to remove an unrecognized file from the owned VM state directory: ' + $relative) }
            }
            if (@($files | Where-Object { $_.FullName -match '\\package\\' }).Count -gt 0) {
                $ownedPackage = [string]$journal.package.directory
                foreach ($member in @($journal.package.files)) {
                    $memberPath = Join-Path $ownedPackage ([string]$member.name)
                    if (-not (Test-Path -LiteralPath $memberPath -PathType Leaf) -or (Get-FileHash -LiteralPath $memberPath -Algorithm SHA256).Hash -cne [string]$member.sha256) {
                        throw ('Owned package file changed or is missing; refusing state-root deletion: ' + [string]$member.name)
                    }
                }
            }
            Remove-Item -LiteralPath $ownedRoot -Recurse -Force
            $script:MutationRecords.Add([ordered]@{ operation = 'owned-state-cleanup'; path = $ownedRoot; removed = $true })
        }
    }
}

__NIC_ADAPTER_ADDRESS_FUNCTIONS__

function Invoke-AddressSnapshot {
    Assert-MutationPreconditions
    $journal = Read-Journal
    if ($journal.Contains('addressSnapshot')) { throw 'An adapter-address snapshot already exists; restore it before another trial.' }
    $snapshot = Get-NicAddressSnapshot -ProfileBindings @($script:Config.profile.bindings) -ExpectedVmUuid ([string]$script:Config.expectedVmUuid)
    if (@($snapshot.adapters).Count -ne 4) { throw 'Adapter-address snapshot did not cover all four profile devices.' }
    $journal.addressSnapshot = $snapshot
    $journal.addressState = 'captured'
    $journal.addressSnapshotUtc = [DateTime]::UtcNow.ToString('o')
    Write-Journal $journal
    $script:Record.addressSnapshot = $snapshot
}

function Invoke-AddressRestore {
    Assert-MutationPreconditions
    $journal = Read-Journal
    if (-not $journal.Contains('addressSnapshot') -or @($journal.addressSnapshot.adapters).Count -ne 4) { throw 'Four saved adapter-address snapshots are required for restoration.' }
    $journal.addressState = 'restore-pending'
    Write-Journal $journal
    $operations = [Collections.Generic.List[object]]::new()
    $errors = [Collections.Generic.List[string]]::new()
    foreach ($adapter in @($journal.addressSnapshot.adapters)) {
        try {
            $persist = { param($intent) $journal.addressIntent = $intent; Write-Journal $journal }
            $result = Restore-NicAddressSnapshot -Snapshot $adapter -ExpectedVmUuid ([string]$script:Config.expectedVmUuid) -PersistIntent $persist
            $operations.Add($result)
        } catch { $errors.Add($_.Exception.ToString()) }
    }
    $journal.addressState = if ($errors.Count -eq 0) { 'restored' } else { 'restore-incomplete' }
    $journal.addressRestoreResults = @($operations.ToArray())
    Write-Journal $journal
    $script:Record.addressRestore = [ordered]@{ results = @($operations.ToArray()); errors = @($errors.ToArray()); state = $journal.addressState }
    $script:Record.hostOrGuestSettingsChanged = $true
    if ($errors.Count -gt 0) { throw ('Adapter-address restoration incomplete: ' + ($errors -join '; ')) }
}

function Invoke-AddressApply {
    Assert-MutationPreconditions
    $journal = Read-Journal
    if ($journal.installState -cne 'installed' -or -not $journal.Contains('addressSnapshot') -or $journal.addressState -cne 'captured') { throw 'An installed filter and fresh four-adapter snapshot are required before address configuration.' }
    $journal.addressState = 'apply-pending'
    Write-Journal $journal
    $operations = [Collections.Generic.List[object]]::new()
    try {
        foreach ($adapter in @($journal.addressSnapshot.adapters)) {
            $persist = { param($intent) $journal.addressIntent = $intent; Write-Journal $journal }
            $result = Invoke-NicAddressApply -Snapshot $adapter -ExpectedVmUuid ([string]$script:Config.expectedVmUuid) -PersistIntent $persist
            $operations.Add($result)
            $journal.addressApplyResults = @($operations.ToArray())
            Write-Journal $journal
        }
        $journal.addressState = 'applied'
        Write-Journal $journal
        $script:Record.addressApply = @($operations.ToArray())
        $script:Record.hostOrGuestSettingsChanged = $true
    } catch {
        $originalError = $_
        $script:Record.addressApplyFailure = [ordered]@{ exception = $_.Exception.ToString(); position = $_.InvocationInfo.PositionMessage; completedOperations = @($operations.ToArray()) }
        try { Invoke-AddressRestore } catch { $script:Record.addressApplyRecoveryError = $_.Exception.ToString() }
        throw $originalError
    }
}

function Invoke-Phase {
    $phase = [string]$script:Config.phase
    $script:Record.phase = $phase
    $script:Record.expectedProbeVerdict = $ExpectedProbeVerdict
    $script:Record.expectedMode = [string]$script:Config.expectedMode
    $script:Record.readOnly = $phase -ceq 'Inventory' -or $phase -ceq 'Readback'
    if ($phase -notin @('Inventory', 'PreparePolicy', 'Install', 'Start', 'AddressSnapshot', 'AddressApply', 'AddressRestore', 'RemoveForAddressReadback', 'Readback', 'Remove', 'RestorePolicy', 'Reboot', 'Shutdown')) { throw ('Unsupported phase: ' + $phase) }
    $profileBindings = Assert-Profile
    $identity = Assert-VmIdentity ($phase -ne 'Inventory')
    $script:Record.host = $identity
    if ($phase -in @('PreparePolicy', 'Install', 'Start', 'AddressSnapshot', 'AddressApply', 'AddressRestore', 'RemoveForAddressReadback', 'Remove', 'RestorePolicy', 'Reboot', 'Shutdown')) {
        Assert-MutationPreconditions
        $script:Record.host = $identity
    }
    switch ($phase) {
        'Inventory' { }
        'PreparePolicy' { Invoke-PreparePolicy; $script:Record.hostOrGuestSettingsChanged = $true }
        'Install' { Invoke-Install; $script:Record.hostOrGuestSettingsChanged = $true }
        'Start' { Invoke-Start }
        'AddressSnapshot' { Invoke-AddressSnapshot }
        'AddressApply' { Invoke-AddressApply }
        'AddressRestore' { Invoke-AddressRestore }
        'RemoveForAddressReadback' { Invoke-Remove -PreserveConfiguredAddress; $script:Record.hostOrGuestSettingsChanged = $true }
        'Remove' { Invoke-Remove; $script:Record.hostOrGuestSettingsChanged = $true }
        'RestorePolicy' { Restore-Policy; $script:Record.hostOrGuestSettingsChanged = $true }
        'Reboot' {
            $script:Record.hostOrGuestSettingsChanged = $true
            [void](Invoke-RecordedProcess (Join-Path $env:SystemRoot 'System32\shutdown.exe') @('/r', '/t', '30', '/f', '/d', 'p:4:1', '/c', 'Disposable NIC filter VM phase'))
        }
        'Shutdown' {
            $script:Record.hostOrGuestSettingsChanged = $true
            [void](Invoke-RecordedProcess (Join-Path $env:SystemRoot 'System32\shutdown.exe') @('/s', '/t', '30', '/f', '/d', 'p:4:1', '/c', 'Disposable NIC filter VM phase complete'))
        }
        'Readback' { }
    }
    $script:Record.secureBoot = Get-SecureBootSnapshot
    $script:Record.bcd = Get-BcdSnapshot
    if ($phase -eq 'Readback' -and $script:Config.filterState -in @('Absent', 'AbsentConfigured')) { $script:Record.codeIntegrity = Get-CodeIntegritySnapshot }
    $script:Record.trustMembership = Get-TrustMembership ([string]$script:Config.publicCertificate.thumbprint)
    $script:Record.nativeCapture = Get-NativeCapture
    $script:Record.windowsReads = Get-FreshWindowsReads
    if ($phase -eq 'Install') {
        $script:Record.filterBindingEvaluations = @(Get-FilterBindingEvaluations $script:Record.windowsReads $profileBindings)
        $script:Record.allProfileFiltersBound = $script:Record.filterBindingEvaluations.Count -eq 4 -and @($script:Record.filterBindingEvaluations | Where-Object { $_.matchesExpected -ne $true }).Count -eq 0
        if (-not $script:Record.allProfileFiltersBound) {
            $bindingMismatches = @($script:Record.filterBindingEvaluations | Where-Object { -not $_.matchesExpected } | ForEach-Object { $_.pnpDeviceId + ': expected=' + $_.expectedGuid + ' actual=' + $_.win32Guid + '; filterBindingRows=' + $_.filterBindingMatchCount + '; enabled=' + $_.enabled })
            throw ('NIC filter binding read-back mismatch: ' + ($bindingMismatches -join '; '))
        }
    }
    $script:Record.adapterServices = @(Get-CimInstance -ClassName Win32_SystemDriver -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '(?i)HypersinvNicAddressFilter' } | Select-Object Name, State, StartMode, PathName)
    $script:Record.driverImageEvidence = @(foreach ($service in $script:Record.adapterServices) {
        $imagePath = [Environment]::ExpandEnvironmentVariables([string]$service.PathName).Trim('"')
        if (Test-Path -LiteralPath $imagePath -PathType Leaf) {
            [ordered]@{ path = $imagePath; sha256 = (Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash; size = (Get-Item -LiteralPath $imagePath).Length }
        } else { [ordered]@{ path = $imagePath; error = 'Reported driver image is not reachable for hashing.' } }
    })
    $diagnosticPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\HypersinvNicAddressFilter\Parameters\NicVmDiagnostic'
    if (Test-Path -LiteralPath $diagnosticPath) {
        $diagnostic = Get-ItemProperty -LiteralPath $diagnosticPath -ErrorAction Stop
        $script:Record.driverEntryDiagnostics = [ordered]@{ registryPath = $diagnosticPath; stage = [int]$diagnostic.Stage; statusRaw = [long]$diagnostic.Status; writeStatusRaw = [long]$diagnostic.WriteStatus }
        if ($null -ne $diagnostic.PSObject.Properties['RegistrationRevision']) { $script:Record.driverEntryDiagnostics.registrationRevision = [int]$diagnostic.RegistrationRevision }
        foreach ($valueName in @('Status','WriteStatus')) {
            $valueBits = [uint32]([long]$diagnostic.$valueName -band [long]4294967295)
            $script:Record.driverEntryDiagnostics[$valueName + 'Hex'] = ('0x{0:X8}' -f $valueBits)
        }
        $script:Record.driverEntryDiagnostics.systemTime = [long]$diagnostic.SystemTime
        $script:Record.driverEntryDiagnostics.utc = [DateTime]::FromFileTimeUtc([long]$diagnostic.SystemTime).ToString('o')
    }
    if ($phase -eq 'Readback') {
        $script:Record.requestedFilterState = [string]$script:Config.filterState
        $componentBindings = @($script:Record.windowsReads.adapterBindings.rows | Where-Object { $_.ComponentID -ceq 'HypersinvNicAddressFilter' })
        if ($script:Config.filterState -ceq 'Present') {
            if ($script:Record.adapterServices.Count -ne 1 -or $script:Record.adapterServices[0].State -cne 'Running') { throw 'The NIC filter must be Running for a bound-filter read-back proof.' }
            foreach ($binding in $profileBindings) {
                $adapter = @($script:Record.windowsReads.getNetAdapter.rows | Where-Object { (Get-NormalizedGuid $_.InterfaceGuid) -ceq (Get-NormalizedGuid $binding.interfaceGuid) })
                if ($adapter.Count -ne 1) { throw ('Cannot uniquely identify the adapter binding: ' + $binding.pnpDeviceId) }
                $enabled = @($componentBindings | Where-Object { $_.Name -ceq $adapter[0].Name -and $_.Enabled })
                if ($enabled.Count -ne 1) { throw ('Expected one enabled filter binding for ' + $binding.pnpDeviceId + '; actual=' + $enabled.Count) }
            }
        } elseif ($script:Config.filterState -in @('Absent', 'AbsentConfigured')) {
            if ($script:Record.adapterServices.Count -ne 0 -or $componentBindings.Count -ne 0) { throw 'The NIC filter service and bindings must be absent for removal proof.' }
            if ($script:Config.filterState -ceq 'Absent' -and ($script:Record.codeIntegrity.ntStatus -cne '0x00000000' -or $script:Record.codeIntegrity.testSigning -ne $false)) { throw 'A normal boot with kernel test-signing OFF is required for final removal proof.' }
        }
        if ($script:Config.filterState -ceq 'AbsentConfigured') {
            $addressJournal = Read-Journal
            if ($addressJournal.installState -cne 'removed' -or $addressJournal.addressState -cne 'applied' -or $script:Config.expectedMode -cne 'Replacement') { throw 'Unfiltered address proof requires the filter removed and all saved address overrides still applied.' }
            $script:Record.addressJournalState = $addressJournal.addressState
        }
        $script:Record.evaluations = @(Get-Evaluations $script:Record.nativeCapture.data $script:Record.windowsReads $profileBindings ([string]$script:Config.expectedMode))
        $script:Record.requiredReaderSet = 'CurrentSeven'
        $script:Record.allTargetsMatch = $script:Record.evaluations.Count -eq 4 -and @($script:Record.evaluations | Where-Object { $_.allRequiredSourcesMatch -ne $true }).Count -eq 0
        if (-not $script:Record.allTargetsMatch) {
            $mismatches = @($script:Record.evaluations | ForEach-Object { $_.checks | Where-Object { -not $_.matchesExpected } } | ForEach-Object { $_.source + ': expected=' + $_.expected + ' actual=' + (ConvertTo-Json -InputObject $_.actual -Compress) + ' detail=' + $_.detail })
            $identityMismatches = @($script:Record.evaluations | ForEach-Object { $_.identityChecks | Where-Object { -not $_.matchesExpected } } | ForEach-Object { $_.source + ': expected=' + $_.expected + ' actual=' + $_.actual })
            $mismatches += $identityMismatches
            throw ('NIC read-back mismatch: ' + ($mismatches -join '; '))
        }
    }
    if ($phase -eq 'PreparePolicy') {
        $trustOkay = $script:Record.trustMembership.Root -and $script:Record.trustMembership.TrustedPublisher
        if (-not $script:Record.bcd.testSigning.exists -or $script:Record.bcd.testSigning.value -cne 'on' -or -not $trustOkay) {
            throw ('Policy preparation read-back mismatch: testSigning expected=on actual=' + $script:Record.bcd.testSigning.value + '; pinned Root/TrustedPublisher expected=true actual=' + $trustOkay)
        }
    }
    if ($phase -eq 'RestorePolicy') {
        $journal = $script:Record.restoredJournal
        $expectedTestSigning = $journal.testSigning
        $actualTestSigning = $script:Record.bcd.testSigning
        $testSigningMatches = ([bool]$expectedTestSigning.exists -eq [bool]$actualTestSigning.exists) -and
            (-not [bool]$expectedTestSigning.exists -or [string]$expectedTestSigning.value -ceq [string]$actualTestSigning.value)
        $trustMatches = $true
        foreach ($storeName in @('Root', 'TrustedPublisher')) {
            $expectedMembership = [bool]$journal.trustMembership.PSObject.Properties[$storeName].Value
            if ([bool]$script:Record.trustMembership[$storeName] -ne $expectedMembership) { $trustMatches = $false }
        }
        $secureBootMatches = $null -eq $script:Record.secureBoot.error -and [bool]$script:Record.secureBoot.value -eq [bool]$journal.secureBoot.value
        $stateRootAbsent = -not (Test-Path -LiteralPath 'C:\ProgramData\ClientNicVmTest')
        $script:Record.restoreVerification = [ordered]@{
            testSigningExpected = $expectedTestSigning; testSigningActual = $actualTestSigning; testSigningMatches = $testSigningMatches
            trustExpected = $journal.trustMembership; trustActual = $script:Record.trustMembership; trustMatches = $trustMatches
            secureBootExpected = $journal.secureBoot; secureBootActual = $script:Record.secureBoot; secureBootMatches = $secureBootMatches
            ownedStateRootExpectedAbsent = $true; ownedStateRootActualAbsent = $stateRootAbsent
        }
        if (-not $testSigningMatches -or -not $trustMatches -or -not $secureBootMatches -or -not $stateRootAbsent) {
            throw ('Policy restore verification mismatch: testSigning expected=' + (ConvertTo-Json -InputObject $expectedTestSigning -Compress) + ' actual=' + (ConvertTo-Json -InputObject $actualTestSigning -Compress) + '; trustMatches=' + $trustMatches + '; secureBootMatches=' + $secureBootMatches + '; ownedRootAbsent=' + $stateRootAbsent)
        }
    }
    if ($phase -eq 'Inventory') { $script:Record.profileBindings = @($profileBindings); $script:Record.inventoryOnly = $true }
    if ($script:Failures.Count -gt 0) { throw ('One or more independent inventory reads failed; see exact failures in kernel-metadata.json: ' + @($script:Failures | ForEach-Object { $_.source }) -join ', ') }
}

try {
    if ($Action -cne 'Probe') { throw 'Authenticated guest action must be Probe.' }
    if ($ExpectedProbeVerdict -notin @('MET', 'NOT_MET')) { throw 'ExpectedProbeVerdict must be MET or NOT_MET.' }
    $fullRunRoot = [IO.Path]::GetFullPath($RunRoot)
    $allowedRunRoot = 'C:\ProgramData\UEFILoaderRunner\runs\'
    if (-not $fullRunRoot.StartsWith($allowedRunRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $fullRunRoot -match '[\r\n"]' -or -not (Test-Path -LiteralPath $fullRunRoot -PathType Container)) { throw 'RunRoot is outside the authenticated VM mailbox run directory.' }
    $rawConfig = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('__NIC_CONFIG_BASE64__'))
    $script:Config = $rawConfig | ConvertFrom-Json
    Invoke-Phase
    $script:Result.Status = 'PASS'
    $script:Result.Message = 'NIC VM phase completed; inspect kernel-metadata.json for source-by-source evidence.'
}
catch {
    $script:Result.Status = 'FAIL'
    $script:Result.Message = $_.Exception.Message
    $script:Failures.Add([ordered]@{ source = 'phase'; exception = $_.Exception.ToString(); fullyQualifiedErrorId = $_.FullyQualifiedErrorId; scriptStackTrace = $_.ScriptStackTrace; position = $_.InvocationInfo.PositionMessage })
    if ($script:Record.host -and [string]$script:Record.host.manufacturer -match '^(?i:VMware(?:,\s*Inc\.)?)$') {
        if (-not $script:Record.Contains('secureBoot')) { try { $script:Record.secureBoot = Get-SecureBootSnapshot } catch { $script:Failures.Add([ordered]@{ source = 'failure_secure_boot_snapshot'; exception = $_.Exception.ToString() }) } }
        if (-not $script:Record.Contains('bcd')) { try { $script:Record.bcd = Get-BcdSnapshot } catch { $script:Failures.Add([ordered]@{ source = 'failure_bcd_snapshot'; exception = $_.Exception.ToString() }) } }
        if (-not $script:Record.Contains('trustMembership') -and $script:Config) { try { $script:Record.trustMembership = Get-TrustMembership ([string]$script:Config.publicCertificate.thumbprint) } catch { $script:Failures.Add([ordered]@{ source = 'failure_trust_snapshot'; exception = $_.Exception.ToString() }) } }
        if (-not $script:Record.Contains('nativeCapture') -and $script:Config) { try { $script:Record.nativeCapture = Get-NativeCapture } catch { $script:Failures.Add([ordered]@{ source = 'failure_native_capture'; exception = $_.Exception.ToString() }) } }
        if (-not $script:Record.Contains('windowsReads')) { try { $script:Record.windowsReads = Get-FreshWindowsReads } catch { $script:Failures.Add([ordered]@{ source = 'failure_windows_capture'; exception = $_.Exception.ToString() }) } }
    }
}
finally {
    $script:Record.completedUtc = [DateTime]::UtcNow.ToString('o')
    $script:Record.commandRecords = @($script:CommandRecords.ToArray())
    $script:Record.mutationRecords = @($script:MutationRecords.ToArray())
    $script:Record.failures = @($script:Failures.ToArray())
    $metadataPath = Join-Path $RunRoot 'kernel-metadata.json'
    $stdoutPath = Join-Path $RunRoot 'stdout.txt'
    $stderrPath = Join-Path $RunRoot 'stderr.txt'
    try {
        Write-Json $metadataPath $script:Record 28
        $stdout = [Collections.Generic.List[string]]::new()
        foreach ($command in $script:CommandRecords) { $stdout.Add('COMMAND ' + $command.program + ' ' + ($command.arguments -join ' ') + ' EXIT=' + $command.exitCode); if ($command.stdout) { $stdout.Add([string]$command.stdout) } }
        if ($script:Result.Status -eq 'PASS') { $stdout.Add([string]$script:Result.Message) }
        Write-Utf8 $stdoutPath (($stdout.ToArray() -join [Environment]::NewLine) + [Environment]::NewLine)
        $stderr = [Collections.Generic.List[string]]::new()
        foreach ($command in $script:CommandRecords) { if ($command.stderr) { $stderr.Add('COMMAND ' + $command.program + ' STDERR:'); $stderr.Add([string]$command.stderr) } }
        foreach ($failure in $script:Failures) { if ($failure.exception) { $stderr.Add([string]$failure.exception) } }
        if ($script:Result.Status -eq 'FAIL') { $stderr.Add([string]$script:Result.Message) }
        Write-Utf8 $stderrPath ($stderr.ToArray() -join [Environment]::NewLine)
        Write-Json (Join-Path $RunRoot 'result.json') $script:Result 8
    }
    catch {
        $script:Result.Status = 'FAIL'
        $script:Result.Message = 'Evidence finalization failed: ' + $_.Exception.ToString()
        try { Write-Utf8 $stderrPath $script:Result.Message; Write-Json (Join-Path $RunRoot 'result.json') $script:Result 8 } catch { }
    }
}
if ($script:Result.Status -cne 'PASS') { exit 1 }
exit 0
