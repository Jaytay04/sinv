[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$NativeExe,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $false)]
    [string]$LockedMap,

    [Parameter(Mandatory = $false)]
    [ValidateSet('Original', 'Replacement')]
    [string]$ExpectedMode = 'Original'
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
    param([AllowNull()][object]$Value, [string]$Expected, [string]$LockedOriginal)
    $normalizedValue = ConvertTo-ComparableMac $Value
    $normalizedExpected = ConvertTo-ComparableMac $Expected
    $normalizedOriginal = ConvertTo-ComparableMac $LockedOriginal
    return [ordered]@{
        value = $Value
        normalizedValue = $normalizedValue
        expected = $Expected
        normalizedExpected = $normalizedExpected
        matchesExpected = ($null -ne $normalizedValue -and $null -ne $normalizedExpected -and $normalizedValue -ceq $normalizedExpected)
        matchesLockedOriginal = ($null -ne $normalizedValue -and $null -ne $normalizedOriginal -and $normalizedValue -ceq $normalizedOriginal)
    }
}

function Compare-MacAddresses {
    param([AllowNull()][object]$Left, [AllowNull()][object]$Right)
    $leftKey = ConvertTo-ComparableMac $Left
    $rightKey = ConvertTo-ComparableMac $Right
    $equal = $null
    if ($null -ne $leftKey -and $null -ne $rightKey) { $equal = $leftKey -ceq $rightKey }
    return [ordered]@{ leftValue = $Left; rightValue = $Right; leftComparable = $leftKey; rightComparable = $rightKey; equal = $equal }
}

function Get-Dot11CurrentAddressCheck {
    param(
        [AllowNull()][object]$OidResult,
        [string]$Expected,
        [string]$LockedOriginal
    )

    if ($null -eq $OidResult) {
        return [ordered]@{
            api = 'OID_DOT11_CURRENT_ADDRESS'
            applicable = $false
            status = 'unavailable'
            ioctlSucceeded = $null
            win32Error = $null
            win32ErrorText = 'No matching NDIS interface or Dot11 OID result was captured.'
            bytesReturned = $null
            outputBytesHex = $null
            actual = $null
            normalizedActual = $null
            expectedMac = $Expected
            matchesExpected = $null
            matchesLockedOriginal = $null
        }
    }

    $ioctlSucceeded = Get-PropertyValue $OidResult 'ioctlSucceeded'
    $actual = Get-PropertyValue $OidResult 'mac'
    $normalizedActual = ConvertTo-ComparableMac $actual
    $matchesExpected = $null
    $matchesLockedOriginal = $null
    if ($ioctlSucceeded -eq $true) {
        $expectedObservation = Get-MacObservation $actual $Expected $LockedOriginal
        $matchesExpected = $expectedObservation.matchesExpected
        $matchesLockedOriginal = $expectedObservation.matchesLockedOriginal
    }

    return [ordered]@{
        api = 'OID_DOT11_CURRENT_ADDRESS'
        applicable = ($ioctlSucceeded -eq $true)
        status = if ($ioctlSucceeded -eq $true) { 'supported' } else { 'not_applicable' }
        ioctlSucceeded = $ioctlSucceeded
        win32Error = Get-PropertyValue $OidResult 'win32Error'
        win32ErrorText = Get-PropertyValue $OidResult 'win32ErrorText'
        bytesReturned = Get-PropertyValue $OidResult 'bytesReturned'
        outputBytesHex = Get-PropertyValue $OidResult 'outputBytesHex'
        actual = $actual
        normalizedActual = $normalizedActual
        expectedMac = $Expected
        matchesExpected = $matchesExpected
        matchesLockedOriginal = $matchesLockedOriginal
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
                $replacement = [string]$_.replacement
                if ($ExpectedMode -ceq 'Replacement' -and $null -eq (ConvertTo-ComparableMac $replacement)) { throw 'A locked MAC replacement is not a six-byte address.' }
                [pscustomobject]@{
                    mapDevice = [string]$_.device
                    pnpDeviceId = ([string]$_.device).Substring($separator + 1)
                    original = $original
                    replacement = $replacement
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
$nativeAdaptersInfo = $null
$nativeAdaptersInfoRows = @()
if ($null -ne $nativeData) {
    $nativeGaaRows = @(Get-PropertyValue $nativeData.getAdaptersAddresses 'entries')
    $nativeIfRows = @(Get-PropertyValue $nativeData.getIfTable2 'entries')
    $nativeAdaptersInfo = Get-PropertyValue $nativeData 'getAdaptersInfo'
    $nativeAdaptersInfoRows = @(Get-PropertyValue $nativeAdaptersInfo 'entries')
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

        $infoGuidMatches = @($nativeAdaptersInfoRows | Where-Object {
            $candidate = Normalize-Guid (Get-PropertyValue $_ 'adapterName')
            $null -ne $joinGuidNormalized -and $candidate -ceq $joinGuidNormalized
        })
        $infoInterfaceIndex = if ($win32Matches.Count -eq 1) { Get-PropertyValue $win32Matches[0] 'InterfaceIndex' } else { $null }
        $infoIndexMatches = @()
        if ($null -ne $infoInterfaceIndex) {
            $infoIndexMatches = @($nativeAdaptersInfoRows | Where-Object {
                $candidateIndex = Get-PropertyValue $_ 'index'
                $null -ne $candidateIndex -and [uint32]$candidateIndex -eq [uint32]$infoInterfaceIndex
            })
        }
        $infoMatches = @()
        $infoJoinSource = 'unmatched or ambiguous'
        if ($infoGuidMatches.Count -eq 1) {
            $infoMatches = $infoGuidMatches
            $infoJoinSource = if ($infoIndexMatches.Count -eq 1 -and [uint32](Get-PropertyValue $infoMatches[0] 'index') -eq [uint32]$infoInterfaceIndex) { 'AdapterName GUID and InterfaceIndex' } else { 'AdapterName GUID' }
        }
        elseif ($infoGuidMatches.Count -eq 0 -and $infoIndexMatches.Count -eq 1) {
            $infoMatches = $infoIndexMatches
            $infoJoinSource = 'unique Win32 InterfaceIndex'
        }
        $infoAddress = if ($infoMatches.Count -eq 1) { Get-PropertyValue $infoMatches[0] 'address' } else { $null }

        $win32Value = if ($win32Matches.Count -eq 1) { Get-PropertyValue $win32Matches[0] 'MACAddress' } else { $null }
        $gaaValue = if ($gaaMatches.Count -eq 1) { Get-PropertyValue $gaaMatches[0] 'physicalAddress' } else { $null }
        $ifValue = if ($ifTableMatches.Count -eq 1) { Get-PropertyValue $ifTableMatches[0] 'physicalAddress' } else { $null }
        $oidValue = $null
        $oidStatus = $null
        $dot11Result = $null
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
            $dot11Result = Get-PropertyValue $ndisMatches[0] 'dot11Address'
        }
        $msftNetworkAddresses = if ($msftGuidMatches.Count -eq 1) { @(Get-PropertyValue $msftGuidMatches[0] 'NetworkAddresses') } else { @() }
        $msftPermanentAddress = if ($msftGuidMatches.Count -eq 1) { Get-PropertyValue $msftGuidMatches[0] 'PermanentAddress' } else { $null }
        $plainMac = if ($plainGuidMatches.Count -eq 1) { Get-PropertyValue $plainGuidMatches[0] 'MacAddress' } else { $null }
        $expected = if ($ExpectedMode -ceq 'Replacement') { [string]$locked.replacement } else { [string]$locked.original }
        $dot11CurrentAddressCheck = Get-Dot11CurrentAddressCheck $dot11Result $expected ([string]$locked.original)

        $permanentComparisons = [ordered]@{
            getAdaptersInfoAddress = Compare-MacAddresses $infoAddress $msftPermanentAddress
            getAdaptersAddressesPhysicalAddress = Compare-MacAddresses $gaaValue $msftPermanentAddress
            getIfTable2PhysicalAddress = Compare-MacAddresses $ifValue $msftPermanentAddress
            win32NetworkAdapterMacAddress = Compare-MacAddresses $win32Value $msftPermanentAddress
            getNetAdapterMacAddress = Compare-MacAddresses $plainMac $msftPermanentAddress
            ndisOid8023CurrentAddress = Compare-MacAddresses $oidValue $msftPermanentAddress
            interpretation = 'These are byte-normalized equality comparisons only; no API value is assumed to mean current or permanent unless its API names that property.'
        }

        $observations = [ordered]@{
            getAdaptersInfoAddress = Get-MacObservation $infoAddress $expected ([string]$locked.original)
            win32NetworkAdapter = Get-MacObservation $win32Value $expected ([string]$locked.original)
            msftNetAdapterNetworkAddresses = @($msftNetworkAddresses | ForEach-Object { Get-MacObservation $_ $expected ([string]$locked.original) })
            msftNetAdapterPermanentAddress = [ordered]@{
                value = $msftPermanentAddress
                comparedToLockedCurrentOriginal = $false
                note = 'PermanentAddress is recorded for context and is not treated as the current MAC.'
            }
            getAdaptersAddresses = Get-MacObservation $gaaValue $expected ([string]$locked.original)
            getIfTable2 = Get-MacObservation $ifValue $expected ([string]$locked.original)
            ndisOid8023CurrentAddress = Get-MacObservation $oidValue $expected ([string]$locked.original)
            ndisOidDot11CurrentAddress = $dot11CurrentAddressCheck
            getNetAdapter = Get-MacObservation $plainMac $expected ([string]$locked.original)
            permanentAddressComparisons = $permanentComparisons
        }
        $msftExpectedAddressMatch = $observations.msftNetAdapterNetworkAddresses.Count -gt 0 -and @($observations.msftNetAdapterNetworkAddresses | Where-Object { -not $_.matchesExpected }).Count -eq 0
        $msftOriginalAddressMatch = $observations.msftNetAdapterNetworkAddresses.Count -gt 0 -and @($observations.msftNetAdapterNetworkAddresses | Where-Object { -not $_.matchesLockedOriginal }).Count -eq 0
        $requiredChecks = @(
            $observations.getAdaptersInfoAddress.matchesExpected,
            $observations.win32NetworkAdapter.matchesExpected,
            $observations.getAdaptersAddresses.matchesExpected,
            $observations.getIfTable2.matchesExpected,
            $observations.ndisOid8023CurrentAddress.matchesExpected,
            $observations.getNetAdapter.matchesExpected,
            $msftExpectedAddressMatch
        )
        $dot11ApplicableMismatch = $dot11CurrentAddressCheck.applicable -and $dot11CurrentAddressCheck.matchesExpected -ne $true
        $dot11ResultUnavailable = $dot11CurrentAddressCheck.status -ceq 'unavailable'
        [ordered]@{
            mapDevice = $locked.mapDevice
            pnpDeviceId = $pnp
            expectedMode = $ExpectedMode
            lockedOriginal = [string]$locked.original
            lockedReplacement = [string]$locked.replacement
            expectedMac = $expected
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
            getAdaptersInfoMatchCount = $infoMatches.Count
            getAdaptersInfoIndexMatchCount = $infoIndexMatches.Count
            getAdaptersInfoGuidMatchCount = $infoGuidMatches.Count
            getAdaptersInfoJoinSource = $infoJoinSource
            getAdaptersInfoCapture = [ordered]@{
                api = Get-PropertyValue $nativeAdaptersInfo 'api'
                status = Get-PropertyValue $nativeAdaptersInfo 'status'
                statusHex = Get-PropertyValue $nativeAdaptersInfo 'statusHex'
                statusText = Get-PropertyValue $nativeAdaptersInfo 'statusText'
                complete = Get-PropertyValue $nativeAdaptersInfo 'complete'
                adapterIndex = if ($infoMatches.Count -eq 1) { Get-PropertyValue $infoMatches[0] 'index' } else { $null }
                adapterName = if ($infoMatches.Count -eq 1) { Get-PropertyValue $infoMatches[0] 'adapterName' } else { $null }
                addressLength = if ($infoMatches.Count -eq 1) { Get-PropertyValue $infoMatches[0] 'addressLength' } else { $null }
                addressBytesHex = if ($infoMatches.Count -eq 1) { Get-PropertyValue $infoMatches[0] 'addressBytesHex' } else { $null }
                addressSemantics = Get-PropertyValue $nativeAdaptersInfo 'addressSemantics'
                note = 'GetAdaptersInfo is a separate IPv4 information API; its Address bytes are reported without relabeling them current or permanent.'
            }
            netCfgInstanceId = $joinGuid
            observations = $observations
            ndisOidStatus = $oidStatus
            dot11CurrentAddressCheck = $dot11CurrentAddressCheck
            dot11CurrentAddressApplicable = $dot11CurrentAddressCheck.applicable
            dot11CurrentAddressMismatch = $dot11ApplicableMismatch
            msftNetworkAddressesHasLockedOriginal = $msftOriginalAddressMatch
            msftNetworkAddressesHasExpectedValue = $msftExpectedAddressMatch
            requiredOriginalSourceCount = 7
            requiredExpectedSourceCount = 7
            allRequiredExpectedSourcesMatch = ($requiredChecks.Count -eq 7 -and @($requiredChecks | Where-Object { $_ -ne $true }).Count -eq 0 -and -not $dot11ApplicableMismatch -and -not $dot11ResultUnavailable)
            allRequiredOriginalSourcesMatch = ($ExpectedMode -ceq 'Original' -and $requiredChecks.Count -eq 7 -and @($requiredChecks | Where-Object { $_ -ne $true }).Count -eq 0 -and -not $dot11ApplicableMismatch -and -not $dot11ResultUnavailable)
        }
    }
)

$allLockedExpectedValuesMatchReadOnlySources = $lockedRows.Count -eq 4 -and $targetEvaluations.Count -eq 4 -and
    @($targetEvaluations | Where-Object { $_.allRequiredExpectedSourcesMatch -ne $true }).Count -eq 0
$allLockedOriginalsMatchReadOnlySources = $ExpectedMode -ceq 'Original' -and $allLockedExpectedValuesMatchReadOnlySources
$dot11SupportedCount = @($targetEvaluations | Where-Object { $_.dot11CurrentAddressCheck.applicable -eq $true }).Count
$dot11NotApplicableCount = @($targetEvaluations | Where-Object { $_.dot11CurrentAddressCheck.status -ceq 'not_applicable' }).Count
$dot11UnavailableCount = @($targetEvaluations | Where-Object { $_.dot11CurrentAddressCheck.status -ceq 'unavailable' }).Count
$allApplicableDot11CurrentAddressesMatchExpected = $null
if ($dot11SupportedCount -gt 0) {
    $allApplicableDot11CurrentAddressesMatchExpected = @($targetEvaluations | Where-Object {
        $_.dot11CurrentAddressCheck.applicable -eq $true -and $_.dot11CurrentAddressCheck.matchesExpected -ne $true
    }).Count -eq 0
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$report = [ordered]@{
    schema = 'nic-native-wmi-readback.v1'
    capturedUtc = [DateTime]::UtcNow.ToString('o')
    readOnly = $true
    replacementEnabled = $false
    replacementsLoaded = $false
    expectedMode = $ExpectedMode
    replacementValuesLoadedForComparison = ($lockedRows.Count -gt 0)
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
    nativeApiDiagnostics = [ordered]@{
        getAdaptersInfo = if ($null -ne $nativeAdaptersInfo) { [ordered]@{ api = Get-PropertyValue $nativeAdaptersInfo 'api'; status = Get-PropertyValue $nativeAdaptersInfo 'status'; statusHex = Get-PropertyValue $nativeAdaptersInfo 'statusHex'; statusText = Get-PropertyValue $nativeAdaptersInfo 'statusText'; complete = Get-PropertyValue $nativeAdaptersInfo 'complete'; entries = $nativeAdaptersInfoRows.Count } } else { [ordered]@{ api = 'GetAdaptersInfo(IP_ADAPTER_INFO)'; status = $null; complete = $false; error = 'Native capture has no GetAdaptersInfo field.' } }
        ndisOidReads = 'Existing read-only IOCTL_NDIS_QUERY_GLOBAL_STATS observations: OID_802_3_CURRENT_ADDRESS and OID_DOT11_CURRENT_ADDRESS; exact statuses and bytes remain in nativeData.setupDiNdisLanClass.interfaces.'
    }
    lockedMap = $resolvedLockedMap
    lockedMacOriginalCount = $lockedRows.Count
    lockedMacReplacementCount = $lockedRows.Count
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
    allLockedExpectedValuesMatchReadOnlySources = $allLockedExpectedValuesMatchReadOnlySources
    allLockedOriginalsMatchReadOnlySources = $allLockedOriginalsMatchReadOnlySources
    dot11CurrentAddressDiagnostics = [ordered]@{
        supportedCount = $dot11SupportedCount
        notApplicableCount = $dot11NotApplicableCount
        unavailableCount = $dot11UnavailableCount
        allApplicableDot11CurrentAddressesMatchExpected = $allApplicableDot11CurrentAddressesMatchExpected
        note = 'An unsupported/not-applicable OID is reported with its exact Win32 status and is not treated as a successful MAC comparison. When the OID succeeds, its six-byte address must match the selected expected value.'
    }
    failures = @($failures)
    note = if ($ExpectedMode -ceq 'Replacement') { 'This report compares read-only observations to the locked replacement values from the map. Values are used only as comparison expectations; observed values are not transformed and network configuration is not changed.' } else { 'This report compares read-only observations to the locked original values. Replacement values are loaded only for reporting and are not used as comparison expectations in Original mode. Network configuration is not changed.' }
}

Write-NewJson $reportPath $report
Write-Output ('READ-ONLY NIC EVIDENCE ' + $reportPath)
Write-Output ('Expected mode=' + $ExpectedMode + '; locked MAC rows=' + $lockedRows.Count + '; all required read paths agree=' + $allLockedExpectedValuesMatchReadOnlySources)
if (-not $allLockedExpectedValuesMatchReadOnlySources -or $failures.Count -ne 0) { exit 1 }
exit 0
