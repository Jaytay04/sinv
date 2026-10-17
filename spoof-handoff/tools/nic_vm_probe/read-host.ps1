[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$NativeExe,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $false)]
    [string]$LockedMap
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-PropertyValue {
    param(
        [AllowNull()][object]$InputObject,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if ($null -eq $InputObject) { return $null }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function ConvertTo-ComparableMac {
    param([AllowNull()][object]$Value)
    if ($null -eq $Value) { return $null }
    $normalized = ([string]$Value) -replace '[^0-9A-Fa-f]', ''
    if ($normalized.Length -ne 12 -or $normalized -notmatch '\A[0-9A-Fa-f]{12}\z') { return $null }
    return $normalized.ToUpperInvariant()
}

function Normalize-Guid {
    param([AllowNull()][object]$Value)
    if ($null -eq $Value) { return $null }
    $text = ([string]$Value).Trim()
    if ([string]::IsNullOrWhiteSpace($text)) { return $null }
    $guidPattern = '(?i)(?<![0-9a-f])[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}(?![0-9a-f])'
    $matches = [regex]::Matches($text, $guidPattern)
    if ($matches.Count -ne 1) { return $null }
    try { return ([Guid]::Parse($matches[0].Value)).ToString('D').ToUpperInvariant() }
    catch { return $null }
}

function Get-NativeRowsByPnp {
    param([object[]]$Rows, [string]$PnpDeviceId)
    return @($Rows | Where-Object {
        $actual = Get-PropertyValue $_ 'pnpDeviceId'
        $null -ne $actual -and [string]::Equals([string]$actual, $PnpDeviceId, [StringComparison]::OrdinalIgnoreCase)
    })
}

function Get-MacObservation {
    param([AllowNull()][object]$Value, [string]$Expected)
    $normalizedValue = ConvertTo-ComparableMac $Value
    $normalizedExpected = ConvertTo-ComparableMac $Expected
    return [ordered]@{
        value = $Value
        normalizedValue = $normalizedValue
        matchesLockedOriginal = ($null -ne $normalizedValue -and $null -ne $normalizedExpected -and $normalizedValue -ceq $normalizedExpected)
    }
}

function Get-FailureRecord {
    param([string]$Operation, [System.Management.Automation.ErrorRecord]$ErrorRecord)
    return [ordered]@{
        operation = $Operation
        fullyQualifiedErrorId = $ErrorRecord.FullyQualifiedErrorId
        category = [string]$ErrorRecord.CategoryInfo.Category
        exceptionType = $ErrorRecord.Exception.GetType().FullName
        exception = $ErrorRecord.Exception.ToString()
    }
}

function Write-NewJson {
    param([string]$Path, [object]$Value)
    $encoding = [System.Text.UTF8Encoding]::new($false)
    $bytes = $encoding.GetBytes(($Value | ConvertTo-Json -Depth 14 -Compress))
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    }
    finally { $stream.Dispose() }
}

$scriptRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot '..\..\..'))
$resolvedNativeExe = [IO.Path]::GetFullPath($NativeExe)
$resolvedOutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if ([string]::IsNullOrWhiteSpace($LockedMap)) {
    $resolvedLockedMap = Join-Path $repositoryRoot 'spoof-handoff\reports\client-originals.tsv'
}
else {
    $resolvedLockedMap = [IO.Path]::GetFullPath($LockedMap)
}

New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force | Out-Null
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
$nativePath = Join-Path $resolvedOutputDirectory ('native.' + $runId + '.json')
$nativeStdoutPath = Join-Path $resolvedOutputDirectory ('native.' + $runId + '.stdout.txt')
$nativeStderrPath = Join-Path $resolvedOutputDirectory ('native.' + $runId + '.stderr.txt')
$reportPath = Join-Path $resolvedOutputDirectory ('readback.' + $runId + '.json')

$failures = [System.Collections.Generic.List[object]]::new()
$nativeStdout = ''
$nativeStderr = ''
$nativeExitCode = $null
$nativeData = $null
$nativeParseError = $null
if (-not (Test-Path -LiteralPath $resolvedNativeExe -PathType Leaf)) {
    $failures.Add([ordered]@{ operation = 'native_executable'; error = 'The native executable path does not exist.'; path = $resolvedNativeExe })
}
else {
    try {
        & $resolvedNativeExe --capture $nativePath 1> $nativeStdoutPath 2> $nativeStderrPath
        $nativeExitCode = $LASTEXITCODE
        if (Test-Path -LiteralPath $nativeStdoutPath -PathType Leaf) { $nativeStdout = [IO.File]::ReadAllText($nativeStdoutPath) }
        if (Test-Path -LiteralPath $nativeStderrPath -PathType Leaf) { $nativeStderr = [IO.File]::ReadAllText($nativeStderrPath) }
        if (Test-Path -LiteralPath $nativePath -PathType Leaf) {
            try { $nativeData = Get-Content -LiteralPath $nativePath -Raw | ConvertFrom-Json }
            catch { $nativeParseError = $_.Exception.ToString(); $failures.Add((Get-FailureRecord 'parse_native_json' $_)) }
        }
        else {
            $failures.Add([ordered]@{ operation = 'native_report'; error = 'Native executable did not create its requested report file.'; exitCode = $nativeExitCode })
        }
        if ($nativeExitCode -ne 0) {
            $failures.Add([ordered]@{ operation = 'native_process'; exitCode = $nativeExitCode; stdout = $nativeStdout; stderr = $nativeStderr })
        }
    }
    catch {
        $failures.Add((Get-FailureRecord 'run_native_executable' $_))
        if (Test-Path -LiteralPath $nativeStdoutPath -PathType Leaf) { $nativeStdout = [IO.File]::ReadAllText($nativeStdoutPath) }
        if (Test-Path -LiteralPath $nativeStderrPath -PathType Leaf) { $nativeStderr = [IO.File]::ReadAllText($nativeStderrPath) }
    }
}

$wmiWin32Rows = @()
$wmiWin32Error = $null
try {
    $wmiWin32Rows = @(Get-CimInstance -Namespace 'root\CIMV2' -ClassName 'Win32_NetworkAdapter' -OperationTimeoutSec 30 -ErrorAction Stop)
}
catch {
    $wmiWin32Error = Get-FailureRecord 'Get-CimInstance root/CIMV2:Win32_NetworkAdapter' $_
    $failures.Add($wmiWin32Error)
}

$wmiMsftRows = @()
$wmiMsftError = $null
try {
    $wmiMsftRows = @(Get-CimInstance -Namespace 'root\StandardCimv2' -ClassName 'MSFT_NetAdapter' -OperationTimeoutSec 30 -ErrorAction Stop)
}
catch {
    $wmiMsftError = Get-FailureRecord 'Get-CimInstance root/StandardCimv2:MSFT_NetAdapter' $_
    $failures.Add($wmiMsftError)
}

$plainAdapterRows = @()
$plainAdapterError = $null
try {
    $getNetAdapter = Get-Command -Name 'Get-NetAdapter' -ErrorAction Stop
    $plainAdapterRows = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop)
}
catch {
    $plainAdapterError = Get-FailureRecord 'Get-NetAdapter -IncludeHidden' $_
    $failures.Add($plainAdapterError)
}

$lockedRows = @()
$lockedMapError = $null
if (-not (Test-Path -LiteralPath $resolvedLockedMap -PathType Leaf)) {
    $lockedMapError = [ordered]@{ operation = 'locked_original_map'; error = 'The optional locked map path was not found.'; path = $resolvedLockedMap }
    $failures.Add($lockedMapError)
}
else {
    try {
        $lockedRows = @(Import-Csv -LiteralPath $resolvedLockedMap -Delimiter "`t" -ErrorAction Stop |
            Where-Object { $_.member -ceq 'FakeMacAddress' } |
            ForEach-Object {
                $separator = ([string]$_.device).IndexOf('|')
                if ($separator -lt 0 -or $separator -eq ([string]$_.device).Length - 1) { throw 'A locked MAC row has no PnP identity suffix.' }
                $original = [string]$_.original
                if ($null -eq (ConvertTo-ComparableMac $original)) { throw 'A locked MAC original is not a six-byte address.' }
                [pscustomobject]@{
                    mapDevice = [string]$_.device
                    pnpDeviceId = ([string]$_.device).Substring($separator + 1)
                    original = $original
                }
            })
    }
    catch {
        $lockedMapError = Get-FailureRecord 'read_locked_original_map' $_
        $failures.Add($lockedMapError)
        $lockedRows = @()
    }
}

$nativeGaaRows = @()
$nativeIfRows = @()
$nativeNdisRows = @()
if ($null -ne $nativeData) {
    $nativeGaaRows = @(Get-PropertyValue $nativeData.getAdaptersAddresses 'entries')
    $nativeIfRows = @(Get-PropertyValue $nativeData.getIfTable2 'entries')
    $nativeNdisRows = @(Get-PropertyValue $nativeData.setupDiNdisLanClass 'interfaces')
}

$targetEvaluations = @(
    foreach ($locked in $lockedRows) {
        $pnp = [string]$locked.pnpDeviceId
        $win32Matches = @($wmiWin32Rows | Where-Object {
            $actual = Get-PropertyValue $_ 'PNPDeviceID'
            $null -ne $actual -and [string]::Equals([string]$actual, $pnp, [StringComparison]::OrdinalIgnoreCase)
        })
        $ndisMatches = @(Get-NativeRowsByPnp $nativeNdisRows $pnp)
        $win32Guid = if ($win32Matches.Count -eq 1) { Get-PropertyValue $win32Matches[0] 'GUID' } else { $null }
        $ndisGuid = if ($ndisMatches.Count -eq 1) { Get-PropertyValue $ndisMatches[0] 'netCfgInstanceId' } else { $null }
        $joinGuid = if ($null -ne $ndisGuid) { $ndisGuid } else { $win32Guid }
        $joinGuidNormalized = Normalize-Guid $joinGuid

        $gaaMatches = @($nativeGaaRows | Where-Object {
            $adapterName = [string](Get-PropertyValue $_ 'adapterName')
            $candidate = Normalize-Guid $adapterName
            $null -ne $joinGuidNormalized -and $candidate -ceq $joinGuidNormalized
        })
        $gaaJoinSource = 'AdapterName GUID'
        if ($gaaMatches.Count -eq 0 -and $win32Matches.Count -eq 1) {
            $win32InterfaceIndex = Get-PropertyValue $win32Matches[0] 'InterfaceIndex'
            if ($null -ne $win32InterfaceIndex) {
                $gaaMatches = @($nativeGaaRows | Where-Object {
                    $candidateIndex = Get-PropertyValue $_ 'interfaceIndex'
                    $null -ne $candidateIndex -and [uint32]$candidateIndex -eq [uint32]$win32InterfaceIndex
                })
                $gaaJoinSource = 'unique InterfaceIndex fallback'
            }
        }
        if ($gaaMatches.Count -ne 1) { $gaaJoinSource = 'unmatched or ambiguous' }
        $ifTableMatches = @($nativeIfRows | Where-Object {
            $candidate = Normalize-Guid (Get-PropertyValue $_ 'interfaceGuid')
            $null -ne $joinGuidNormalized -and $candidate -ceq $joinGuidNormalized
        })
        $ifTableJoinSource = 'InterfaceGuid'
        if ($ifTableMatches.Count -eq 0 -and $win32Matches.Count -eq 1) {
            $win32InterfaceIndex = Get-PropertyValue $win32Matches[0] 'InterfaceIndex'
            if ($null -ne $win32InterfaceIndex) {
                $ifTableMatches = @($nativeIfRows | Where-Object {
                    $candidateIndex = Get-PropertyValue $_ 'interfaceIndex'
                    $null -ne $candidateIndex -and [uint32]$candidateIndex -eq [uint32]$win32InterfaceIndex
                })
                $ifTableJoinSource = 'unique InterfaceIndex fallback'
            }
        }
        if ($ifTableMatches.Count -ne 1) { $ifTableJoinSource = 'unmatched or ambiguous' }

        $msftGuidMatches = @($wmiMsftRows | Where-Object {
            $candidate = Normalize-Guid (Get-PropertyValue $_ 'InterfaceGuid')
            $null -ne $joinGuidNormalized -and $candidate -ceq $joinGuidNormalized
        })
        $msftJoinSource = 'InterfaceGuid'
        if ($msftGuidMatches.Count -eq 0 -and $win32Matches.Count -eq 1) {
            $win32InterfaceIndex = Get-PropertyValue $win32Matches[0] 'InterfaceIndex'
            if ($null -ne $win32InterfaceIndex) {
                $msftGuidMatches = @($wmiMsftRows | Where-Object {
                    $candidateIndex = Get-PropertyValue $_ 'InterfaceIndex'
                    $null -ne $candidateIndex -and [uint32]$candidateIndex -eq [uint32]$win32InterfaceIndex
                })
                $msftJoinSource = 'unique InterfaceIndex fallback'
            }
        }
        if ($msftGuidMatches.Count -ne 1) { $msftJoinSource = 'unmatched or ambiguous' }

        $plainGuidMatches = @($plainAdapterRows | Where-Object {
            $candidate = Normalize-Guid (Get-PropertyValue $_ 'InterfaceGuid')
            $null -ne $joinGuidNormalized -and $candidate -ceq $joinGuidNormalized
        })
        $plainJoinSource = 'InterfaceGuid'
        if ($plainGuidMatches.Count -eq 0 -and $win32Matches.Count -eq 1) {
            $win32InterfaceIndex = Get-PropertyValue $win32Matches[0] 'InterfaceIndex'
            if ($null -ne $win32InterfaceIndex) {
                $plainGuidMatches = @($plainAdapterRows | Where-Object {
                    $candidateIndex = Get-PropertyValue $_ 'InterfaceIndex'
                    $null -ne $candidateIndex -and [uint32]$candidateIndex -eq [uint32]$win32InterfaceIndex
                })
                $plainJoinSource = 'unique InterfaceIndex fallback'
            }
        }
        if ($plainGuidMatches.Count -ne 1) { $plainJoinSource = 'unmatched or ambiguous' }

        $win32Value = if ($win32Matches.Count -eq 1) { Get-PropertyValue $win32Matches[0] 'MACAddress' } else { $null }
        $gaaValue = if ($gaaMatches.Count -eq 1) { Get-PropertyValue $gaaMatches[0] 'physicalAddress' } else { $null }
        $ifValue = if ($ifTableMatches.Count -eq 1) { Get-PropertyValue $ifTableMatches[0] 'physicalAddress' } else { $null }
        $oidValue = $null
        $oidStatus = $null
        if ($ndisMatches.Count -eq 1) {
            $oidResult = Get-PropertyValue $ndisMatches[0] 'ethernetAddress'
            $oidValue = Get-PropertyValue $oidResult 'mac'
            $oidStatus = [ordered]@{
                ioctlSucceeded = Get-PropertyValue $oidResult 'ioctlSucceeded'
                win32Error = Get-PropertyValue $oidResult 'win32Error'
                win32ErrorText = Get-PropertyValue $oidResult 'win32ErrorText'
                bytesReturned = Get-PropertyValue $oidResult 'bytesReturned'
                outputBytesHex = Get-PropertyValue $oidResult 'outputBytesHex'
            }
        }
        $msftNetworkAddresses = if ($msftGuidMatches.Count -eq 1) { @(Get-PropertyValue $msftGuidMatches[0] 'NetworkAddresses') } else { @() }
        $msftPermanentAddress = if ($msftGuidMatches.Count -eq 1) { Get-PropertyValue $msftGuidMatches[0] 'PermanentAddress' } else { $null }
        $plainMac = if ($plainGuidMatches.Count -eq 1) { Get-PropertyValue $plainGuidMatches[0] 'MacAddress' } else { $null }
        $expected = [string]$locked.original

        $observations = [ordered]@{
            win32NetworkAdapter = Get-MacObservation $win32Value $expected
            msftNetAdapterNetworkAddresses = @($msftNetworkAddresses | ForEach-Object { Get-MacObservation $_ $expected })
            msftNetAdapterPermanentAddress = [ordered]@{
                value = $msftPermanentAddress
                comparedToLockedCurrentOriginal = $false
                note = 'PermanentAddress is recorded for context and is not treated as the current MAC.'
            }
            getAdaptersAddresses = Get-MacObservation $gaaValue $expected
            getIfTable2 = Get-MacObservation $ifValue $expected
            ndisOid8023CurrentAddress = Get-MacObservation $oidValue $expected
            getNetAdapter = Get-MacObservation $plainMac $expected
        }
        $requiredChecks = @(
            $observations.win32NetworkAdapter.matchesLockedOriginal,
            $observations.getAdaptersAddresses.matchesLockedOriginal,
            $observations.getIfTable2.matchesLockedOriginal,
            $observations.ndisOid8023CurrentAddress.matchesLockedOriginal,
            $observations.getNetAdapter.matchesLockedOriginal
        )
        $msftAddressMatch = @($observations.msftNetAdapterNetworkAddresses | Where-Object matchesLockedOriginal).Count -gt 0

        [ordered]@{
            mapDevice = $locked.mapDevice
            pnpDeviceId = $pnp
            lockedOriginal = $expected
            win32NetworkAdapterMatchCount = $win32Matches.Count
            setupDiNdisInterfaceMatchCount = $ndisMatches.Count
            getAdaptersAddressesMatchCount = $gaaMatches.Count
            getAdaptersAddressesJoinSource = $gaaJoinSource
            getIfTable2MatchCount = $ifTableMatches.Count
            getIfTable2JoinSource = $ifTableJoinSource
            msftNetAdapterMatchCount = $msftGuidMatches.Count
            msftNetAdapterJoinSource = $msftJoinSource
            getNetAdapterMatchCount = $plainGuidMatches.Count
            getNetAdapterJoinSource = $plainJoinSource
            netCfgInstanceId = $joinGuid
            observations = $observations
            ndisOidStatus = $oidStatus
            msftNetworkAddressesHasLockedOriginal = $msftAddressMatch
            allRequiredOriginalSourcesMatch = ($requiredChecks.Count -eq 5 -and @($requiredChecks | Where-Object { $_ -ne $true }).Count -eq 0)
        }
    }
)

$allTargetsMatch = $lockedRows.Count -eq 4 -and $targetEvaluations.Count -eq 4 -and
    @($targetEvaluations | Where-Object { $_.allRequiredOriginalSourcesMatch -ne $true }).Count -eq 0
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$report = [ordered]@{
    schema = 'nic-native-wmi-readback.v1'
    capturedUtc = [DateTime]::UtcNow.ToString('o')
    readOnly = $true
    replacementEnabled = $false
    replacementsLoaded = $false
    elevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    nativeExecutable = $resolvedNativeExe
    nativeExitCode = $nativeExitCode
    nativeStdout = $nativeStdout
    nativeStderr = $nativeStderr
    nativeParseError = $nativeParseError
    nativeEvidence = if (Test-Path -LiteralPath $nativePath -PathType Leaf) { $nativePath } else { $null }
    nativeStdoutEvidence = if (Test-Path -LiteralPath $nativeStdoutPath -PathType Leaf) { $nativeStdoutPath } else { $null }
    nativeStderrEvidence = if (Test-Path -LiteralPath $nativeStderrPath -PathType Leaf) { $nativeStderrPath } else { $null }
    nativeData = $nativeData
    lockedMap = $resolvedLockedMap
    lockedMacOriginalCount = $lockedRows.Count
    sourceReads = [ordered]@{
        win32NetworkAdapter = [ordered]@{ namespace = 'root/CIMV2'; class = 'Win32_NetworkAdapter'; count = $wmiWin32Rows.Count; error = $wmiWin32Error; rows = @($wmiWin32Rows | ForEach-Object {
            [ordered]@{
                Index = Get-PropertyValue $_ 'Index'
                DeviceID = Get-PropertyValue $_ 'DeviceID'
                InterfaceIndex = Get-PropertyValue $_ 'InterfaceIndex'
                GUID = Get-PropertyValue $_ 'GUID'
                PNPDeviceID = Get-PropertyValue $_ 'PNPDeviceID'
                Name = Get-PropertyValue $_ 'Name'
                MACAddress = Get-PropertyValue $_ 'MACAddress'
                PhysicalAdapter = Get-PropertyValue $_ 'PhysicalAdapter'
                NetEnabled = Get-PropertyValue $_ 'NetEnabled'
            }
        }) }
        msftNetAdapter = [ordered]@{ namespace = 'root/StandardCimv2'; class = 'MSFT_NetAdapter'; count = $wmiMsftRows.Count; error = $wmiMsftError; rows = @($wmiMsftRows | ForEach-Object {
            [ordered]@{
                Name = Get-PropertyValue $_ 'Name'
                InterfaceDescription = Get-PropertyValue $_ 'InterfaceDescription'
                InterfaceIndex = Get-PropertyValue $_ 'InterfaceIndex'
                InterfaceGuid = Get-PropertyValue $_ 'InterfaceGuid'
                PnPDeviceID = Get-PropertyValue $_ 'PnPDeviceID'
                NetworkAddresses = @(Get-PropertyValue $_ 'NetworkAddresses')
                PermanentAddress = Get-PropertyValue $_ 'PermanentAddress'
                MediaConnectState = Get-PropertyValue $_ 'MediaConnectState'
                State = Get-PropertyValue $_ 'State'
            }
        }) }
        getNetAdapter = [ordered]@{ command = 'Get-NetAdapter -IncludeHidden'; count = $plainAdapterRows.Count; error = $plainAdapterError; rows = @($plainAdapterRows | ForEach-Object {
            [ordered]@{
                Name = Get-PropertyValue $_ 'Name'
                InterfaceDescription = Get-PropertyValue $_ 'InterfaceDescription'
                InterfaceIndex = Get-PropertyValue $_ 'InterfaceIndex'
                InterfaceGuid = Get-PropertyValue $_ 'InterfaceGuid'
                MacAddress = Get-PropertyValue $_ 'MacAddress'
                Status = Get-PropertyValue $_ 'Status'
            }
        }) }
    }
    lockedOriginalEvaluations = $targetEvaluations
    allLockedOriginalsMatchReadOnlySources = $allTargetsMatch
    failures = @($failures)
    note = 'This report compares read-only observations only to the locked original values. It does not read, use, or assert any replacement values and does not change network configuration.'
}

Write-NewJson $reportPath $report
Write-Output ('READ-ONLY NIC EVIDENCE ' + $reportPath)
Write-Output ('Locked original rows=' + $lockedRows.Count + '; all required read paths agree=' + $allTargetsMatch)
if (-not $allTargetsMatch -or $failures.Count -ne 0) { exit 1 }
exit 0
