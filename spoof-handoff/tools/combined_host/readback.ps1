Set-StrictMode -Version 2.0

Import-Module (Join-Path $PSScriptRoot 'model.psm1') -Force

function Resolve-CombinedPath {
    param([Parameter(Mandatory=$true)][string]$RepositoryRoot,[Parameter(Mandatory=$true)][string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $Path))
}

function Get-CombinedSha256 {
    param([Parameter(Mandatory=$true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Get-CombinedSourceHashSnapshot {
    param([Parameter(Mandatory=$true)][object[]]$Sources)
    $snapshots = [System.Collections.Generic.List[object]]::new()
    foreach ($source in $Sources) {
        $hash = $null
        $errorText = $null
        try { $hash = Get-CombinedSha256 ([string]$source.path) }
        catch { $errorText = $_.Exception.ToString() }
        $snapshots.Add([pscustomobject][ordered]@{ name=[string]$source.name; path=[string]$source.path; sha256=$hash; error=$errorText })
    }
    return @($snapshots.ToArray())
}

function Get-CombinedNativePathPreflight {
    param([Parameter(Mandatory=$true)][string]$RunDirectory)
    $nicOutput = Join-Path (Join-Path $RunDirectory 'p1') 'n'
    $sampleRunId = '0000000000000000000.00000000000000000000000000000000'
    $paths = @(
        (Join-Path $nicOutput ('native.' + $sampleRunId + '.json')),
        (Join-Path $nicOutput ('native.' + $sampleRunId + '.stdout.txt')),
        (Join-Path $nicOutput ('native.' + $sampleRunId + '.stderr.txt')),
        (Join-Path $nicOutput ('readback.' + $sampleRunId + '.json'))
    )
    $longest = $paths | Sort-Object Length -Descending | Select-Object -First 1
    return [pscustomobject][ordered]@{ maximumPathLength=[int]$longest.Length; limit=240; longestPath=[string]$longest; withinLimit=($longest.Length -le 240) }
}

function Assert-CombinedNativePathPreflight {
    param([Parameter(Mandatory=$true)][string]$RunDirectory)
    $result = Get-CombinedNativePathPreflight $RunDirectory
    if (-not $result.withinLimit) { throw "Combined output root is too long for the unchanged NIC native writer: predicted path length=$($result.maximumPathLength), limit=$($result.limit), path=$($result.longestPath). Choose a shorter OutputDirectory before invoking any reader." }
    return $result
}

function Get-CombinedWindowsBootTimeUtc {
    $osRows = @(Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop)
    if ($osRows.Count -ne 1 -or $null -eq $osRows[0].LastBootUpTime) { throw "Expected one Win32_OperatingSystem boot-time value; actual rows=$($osRows.Count)." }
    return $osRows[0].LastBootUpTime.ToUniversalTime().ToString('o')
}

function Invoke-CombinedSmbiosRead {
    param([string]$ReaderPath,[string]$EvidenceDirectory,[int]$PollIndex)
    $reportPath = Join-Path $EvidenceDirectory ('s.' + [Guid]::NewGuid().ToString('N') + '.json')
    $outputLines = @()
    $exitCode = $null
    $report = $null
    $parseError = $null
    try {
        $outputLines = @(& $ReaderPath --capture --output $reportPath 2>&1 | ForEach-Object { [string]$_ })
        $exitCode = $LASTEXITCODE
        if (Test-Path -LiteralPath $reportPath -PathType Leaf) {
            try { $report = Get-Content -LiteralPath $reportPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop }
            catch { $parseError = $_.Exception.ToString() }
        }
    }
    catch { $parseError = $_.Exception.ToString() }
    $evidence = [ordered]@{ path=$reportPath; exists=(Test-Path -LiteralPath $reportPath -PathType Leaf); sha256=$null; exitCode=$exitCode; output=@($outputLines); parseError=$parseError }
    if ($evidence.exists) { $evidence.sha256 = Get-CombinedSha256 $reportPath }
    return [pscustomobject]@{ report=$report; evidence=[pscustomobject]$evidence }
}

function Invoke-CombinedNicRead {
    param([string]$ReaderPath,[string]$NativeExePath,[string]$LockedMapPath,[string]$EvidenceDirectory,[string]$ExpectedMode,[int]$PollIndex)
    $nicDirectory = Join-Path $EvidenceDirectory 'n'
    New-Item -ItemType Directory -Path $nicDirectory -Force | Out-Null
    $powershellExe = Join-Path $PSHOME 'powershell.exe'
    if (-not (Test-Path -LiteralPath $powershellExe -PathType Leaf)) { throw "Windows PowerShell executable is missing: $powershellExe" }
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$ReaderPath,'-NativeExe',$NativeExePath,'-OutputDirectory',$nicDirectory,'-LockedMap',$LockedMapPath,'-ExpectedMode',$ExpectedMode)
    $lines = @()
    $exitCode = $null
    $reportPath = $null
    try {
        $lines = @(& $powershellExe @arguments 2>&1 | ForEach-Object { [string]$_ })
        $exitCode = $LASTEXITCODE
        $evidenceLines = @($lines | Where-Object { $_ -match '^READ-ONLY NIC EVIDENCE\s+' })
        if ($evidenceLines.Count -eq 1) { $reportPath = ($evidenceLines[0] -replace '^READ-ONLY NIC EVIDENCE\s+','').Trim() }
    }
    catch { $lines += $_.Exception.ToString() }
    $report = $null
    $parseError = $null
    if (-not [string]::IsNullOrWhiteSpace([string]$reportPath) -and (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        try { $report = Get-Content -LiteralPath $reportPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop }
        catch { $parseError = $_.Exception.ToString() }
    }
    $reportEvidence = [ordered]@{ path=$reportPath; exists=($null -ne $reportPath -and (Test-Path -LiteralPath $reportPath -PathType Leaf)); sha256=$null; exitCode=$exitCode; output=@($lines); parseError=$parseError; nativeEvidence=$null; nativeSha256=$null }
    if ($reportEvidence.exists) {
        $reportEvidence.sha256 = Get-CombinedSha256 $reportPath
        $nativePath = [string](Get-CombinedValue (Get-CombinedValue $report 'nativeData') 'capturePath')
        if ([string]::IsNullOrWhiteSpace($nativePath)) { $nativePath = [string](Get-CombinedValue $report 'nativeEvidence') }
        if (-not [string]::IsNullOrWhiteSpace($nativePath) -and (Test-Path -LiteralPath $nativePath -PathType Leaf)) {
            $reportEvidence.nativeEvidence = $nativePath
            $reportEvidence.nativeSha256 = Get-CombinedSha256 $nativePath
        }
    }
    return [pscustomobject]@{ report=$report; evidence=[pscustomobject]$reportEvidence }
}

function Invoke-CombinedStorageRead {
    param([string]$StorageReaderPath)
    . $StorageReaderPath
    $devicePath = '\\.\PHYSICALDRIVE0'
    $nativeSerial = [StorageHostReadNative]::ReadStorageSerial($devicePath)
    $diskRows = @(Get-CimInstance -ClassName Win32_DiskDrive -ErrorAction Stop | Where-Object { [string]$_.DeviceID -ceq $devicePath })
    if ($diskRows.Count -ne 1) { throw "Expected one fresh Win32_DiskDrive row for $devicePath; actual count=$($diskRows.Count)." }
    $wmiSerial = ([string]$diskRows[0].SerialNumber).Trim()
    return [pscustomobject][ordered]@{
        devicePath = $devicePath
        pnpDeviceId = [string]$diskRows[0].PNPDeviceID
        nativeSerial = $nativeSerial
        wmiSerial = $wmiSerial
        nativeSource = 'StorageHostReadNative.ReadStorageSerial; CreateFileW + IOCTL_STORAGE_QUERY_PROPERTY'
        wmiSource = 'Fresh Win32_DiskDrive CIM query'
        readOnly = $true
    }
}

function Invoke-CombinedReadbackRound {
    param([string]$RepositoryRoot,[string]$EvidenceDirectory,[string]$SmbiosReaderPath,[string]$NicReaderPath,[string]$NicNativeExePath,[string]$StorageReaderPath,[string]$LockedMapPath,[string]$ExpectedMode,[int]$PollIndex)
    $roundErrors = [System.Collections.Generic.List[object]]::new()
    $bootTimeStart = $null
    try { $bootTimeStart = Get-CombinedWindowsBootTimeUtc }
    catch { $roundErrors.Add([pscustomobject]@{ source='windows_boot_identity'; error=$_.Exception.ToString() }) }

    $smbiosResult = $null
    try {
        $smbiosResult = Invoke-CombinedSmbiosRead $SmbiosReaderPath $EvidenceDirectory $PollIndex
        if ($smbiosResult.evidence.exitCode -ne 0) { $roundErrors.Add([pscustomobject]@{ source='smbios_capture'; exitCode=$smbiosResult.evidence.exitCode; output=$smbiosResult.evidence.output; parseError=$smbiosResult.evidence.parseError }) }
        if ($null -eq $smbiosResult.report) { $roundErrors.Add([pscustomobject]@{ source='smbios_capture'; error='SMBIOS reader did not produce valid capture JSON.'; evidence=$smbiosResult.evidence }) }
    }
    catch { $roundErrors.Add([pscustomobject]@{ source='smbios_capture'; error=$_.Exception.ToString() }) }

    $storage = $null
    try { $storage = Invoke-CombinedStorageRead $StorageReaderPath }
    catch { $roundErrors.Add([pscustomobject]@{ source='storage_read'; error=$_.Exception.ToString() }) }

    $nicResult = $null
    try {
        $nicResult = Invoke-CombinedNicRead $NicReaderPath $NicNativeExePath $LockedMapPath $EvidenceDirectory $ExpectedMode $PollIndex
        if ($nicResult.evidence.exitCode -ne 0) { $roundErrors.Add([pscustomobject]@{ source='nic_readers'; exitCode=$nicResult.evidence.exitCode; output=$nicResult.evidence.output; parseError=$nicResult.evidence.parseError }) }
        if ($null -eq $nicResult.report) { $roundErrors.Add([pscustomobject]@{ source='nic_readers'; error='NIC reader did not produce valid report JSON.'; evidence=$nicResult.evidence }) }
    }
    catch { $roundErrors.Add([pscustomobject]@{ source='nic_readers'; error=$_.Exception.ToString() }) }
    $bootTimeEnd = $null
    try { $bootTimeEnd = Get-CombinedWindowsBootTimeUtc }
    catch { $roundErrors.Add([pscustomobject]@{ source='windows_boot_identity_end'; error=$_.Exception.ToString() }) }

    return [pscustomobject][ordered]@{
        windowsLastBootUpTimeUtcStart = $bootTimeStart
        windowsLastBootUpTimeUtcEnd = $bootTimeEnd
        smbios = if ($null -ne $smbiosResult) { $smbiosResult.report } else { $null }
        storage = $storage
        nic = if ($null -ne $nicResult) { $nicResult.report } else { $null }
        evidence = [pscustomobject][ordered]@{
            smbios = if ($null -ne $smbiosResult) { $smbiosResult.evidence } else { $null }
            smbiosMachineIdentity = if ($null -ne $smbiosResult) { Get-CombinedValue $smbiosResult.report 'machineIdentity' } else { $null }
            nic = if ($null -ne $nicResult) { $nicResult.evidence } else { $null }
            storageReadOnly = $true
        }
        acquisitionErrors = @($roundErrors.ToArray())
    }
}

function Invoke-CombinedFileIntegrityProbe {
    param([string]$Path)
    $bytes = New-Object byte[] 4096
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    $sha = [Security.Cryptography.SHA256]::Create()
    $writeError = $null
    $actualHash = $null
    $expectedHash = $null
    $lengthMatches = $false
    $removed = $false
    $ownedFileCreated = $false
    $pathExistedBefore = Test-Path -LiteralPath $Path -PathType Leaf
    try {
        $rng.GetBytes($bytes)
        $expectedHash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','')
        $stream = [IO.FileStream]::new($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None,4096,[IO.FileOptions]::WriteThrough)
        $ownedFileCreated = $true
        try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        $read = [IO.File]::ReadAllBytes($Path)
        $lengthMatches = $read.Length -eq $bytes.Length
        $actualHash = ([BitConverter]::ToString($sha.ComputeHash($read))).Replace('-','')
    }
    catch { $writeError = $_.Exception.ToString() }
    finally {
        $sha.Dispose()
        $rng.Dispose()
        try {
            if ($ownedFileCreated -and (Test-Path -LiteralPath $Path -PathType Leaf)) { Remove-Item -LiteralPath $Path -Force -ErrorAction Stop }
            $removed = $ownedFileCreated -and -not (Test-Path -LiteralPath $Path)
        }
        catch { if ($null -eq $writeError) { $writeError = $_.Exception.ToString() } }
    }
    return [pscustomobject][ordered]@{
        operation = 'repo-local file write/read SHA-256 integrity probe'
        path = $Path
        bytes = $bytes.Length
        expectedSha256 = $expectedHash
        actualSha256 = $actualHash
        lengthMatches = $lengthMatches
        match = ($null -eq $writeError -and $lengthMatches -and $expectedHash -ceq $actualHash)
        ownedFileCreated = $ownedFileCreated
        pathExistedBefore = $pathExistedBefore
        preexistingPathPreserved = ($pathExistedBefore -and -not $ownedFileCreated -and (Test-Path -LiteralPath $Path -PathType Leaf))
        removedAfterRead = $removed
        writeThrough = $true
        identifierSource = $false
        error = $writeError
    }
}

function Invoke-CombinedReadback {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$RepositoryRoot,
        [Parameter(Mandatory=$true)][string]$OutputDirectory,
        [Parameter(Mandatory=$true)][ValidateSet('Original','Replacement')][string]$ExpectedMode,
        [Parameter(Mandatory=$true)][string]$SmbiosReaderPath,
        [string]$NicReaderPath,
        [string]$NicNativeExePath,
        [string]$StorageReaderPath,
        [Parameter(Mandatory=$true)][string]$LockedMapPath
    )
    $captureStarted = [DateTime]::UtcNow.ToString('o')
    $repository = [IO.Path]::GetFullPath($RepositoryRoot)
    $reportRoot = [IO.Path]::GetFullPath((Join-Path $repository 'spoof-handoff\reports'))
    $outputRoot = Resolve-CombinedPath $repository $OutputDirectory
    if (-not $outputRoot.StartsWith(($reportRoot.TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Combined readback output must be under spoof-handoff\reports: $outputRoot"
    }
    $mapPath = Resolve-CombinedPath $repository $LockedMapPath
    $smbiosPath = Resolve-CombinedPath $repository $SmbiosReaderPath
    if ([string]::IsNullOrWhiteSpace($StorageReaderPath)) {
        $storagePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\storage_host\read.ps1'))
    } else { $storagePath = Resolve-CombinedPath $repository $StorageReaderPath }
    if ([string]::IsNullOrWhiteSpace($NicReaderPath)) { $NicReaderPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\nic_vm_probe\read-host.ps1')) }
    $nicReader = Resolve-CombinedPath $repository $NicReaderPath
    if ([string]::IsNullOrWhiteSpace($NicNativeExePath)) { $NicNativeExePath = Join-Path $repository 'spoof-handoff\artifacts\nic-reader-gap-20261017\native-build-vs18\Release\nic_native_read.exe' }
    $nicNative = Resolve-CombinedPath $repository $NicNativeExePath
    foreach ($requiredFile in @($mapPath,$smbiosPath,$storagePath,$nicReader,$nicNative)) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) { throw "Combined readback input does not exist: $requiredFile" }
    }

    $sources = @(
        [pscustomobject]@{ name='combinedCollectorScript'; path=(Join-Path $PSScriptRoot 'readback.ps1') },
        [pscustomobject]@{ name='combinedModelModule'; path=(Join-Path $PSScriptRoot 'model.psm1') },
        [pscustomobject]@{ name='lockedMap'; path=$mapPath },
        [pscustomobject]@{ name='smbiosCaptureReader'; path=$smbiosPath },
        [pscustomobject]@{ name='nicReaderScript'; path=$nicReader },
        [pscustomobject]@{ name='nicNativeReader'; path=$nicNative },
        [pscustomobject]@{ name='storageReaderScript'; path=$storagePath }
    )
    $sourceBefore = @(Get-CombinedSourceHashSnapshot $sources)
    if (@($sourceBefore | Where-Object { $null -eq $_.sha256 }).Count -gt 0) { throw 'Could not hash-pin every readback source before capture.' }
    $mapRows = Get-CombinedMapRows $mapPath
    $bindingMap = New-CombinedBindingMap $mapRows
    $mapHash = [string](@($sourceBefore | Where-Object { $_.name -ceq 'lockedMap' })[0].sha256)
    $runId = [Guid]::NewGuid().ToString('N')
    $runDirectory = Join-Path $outputRoot ('c.' + $runId)
    $pathPreflight = Assert-CombinedNativePathPreflight $runDirectory
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

    $poll1Directory = Join-Path $runDirectory 'p1'
    $poll2Directory = Join-Path $runDirectory 'p2'
    New-Item -ItemType Directory -Path $poll1Directory -Force | Out-Null
    New-Item -ItemType Directory -Path $poll2Directory -Force | Out-Null
    $polls = [System.Collections.Generic.List[object]]::new()
    $polls.Add((Invoke-CombinedReadbackRound $repository $poll1Directory $smbiosPath $nicReader $nicNative $storagePath $mapPath $ExpectedMode 1))
    $sourceBetween = @(Get-CombinedSourceHashSnapshot $sources)
    $fileProbePath = Join-Path $runDirectory ('i.' + [Guid]::NewGuid().ToString('N') + '.bin')
    $fileProbe = Invoke-CombinedFileIntegrityProbe $fileProbePath
    $polls.Add((Invoke-CombinedReadbackRound $repository $poll2Directory $smbiosPath $nicReader $nicNative $storagePath $mapPath $ExpectedMode 2))
    $sourceAfter = @(Get-CombinedSourceHashSnapshot $sources)
    $sourcePins = [System.Collections.Generic.List[object]]::new()
    foreach ($before in $sourceBefore) {
        $between = @($sourceBetween | Where-Object { $_.name -ceq $before.name })
        $after = @($sourceAfter | Where-Object { $_.name -ceq $before.name })
        $sourcePins.Add([pscustomobject][ordered]@{
            name=$before.name
            path=$before.path
            beforePollSha256=$before.sha256
            betweenPollsSha256=if ($between.Count -eq 1) { $between[0].sha256 } else { $null }
            afterPollSha256=if ($after.Count -eq 1) { $after[0].sha256 } else { $null }
            errors=@($before.error, $(if ($between.Count -eq 1) { $between[0].error } else { 'Source missing or duplicate at between-poll checkpoint.' }), $(if ($after.Count -eq 1) { $after[0].error } else { 'Source missing or duplicate at final checkpoint.' }) | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) })
        })
    }
    $finalBootTime = $null
    $finalBootTimeError = $null
    try { $finalBootTime = Get-CombinedWindowsBootTimeUtc }
    catch { $finalBootTimeError = $_.Exception.ToString() }
    $captureEnded = [DateTime]::UtcNow.ToString('o')
    $model = New-CombinedReadbackModel -BindingMap $bindingMap -Polls @($polls.ToArray()) -ExpectedMode $ExpectedMode -FileIntegrityProbe $fileProbe -LockedMapPath $mapPath -LockedMapSha256 $mapHash -CaptureStartedUtc $captureStarted -CaptureEndedUtc $captureEnded -FinalBootTimeUtc $finalBootTime -FinalBootIdentityError $finalBootTimeError -SourcePins @($sourcePins.ToArray())
    $reportPath = Join-Path $runDirectory 'combined-readback.json'
    $model | Add-Member -NotePropertyName outputPathPreflight -NotePropertyValue $pathPreflight
    [IO.File]::WriteAllText($reportPath,($model | ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
    $summary = [pscustomobject][ordered]@{ status=$model.status; reportPath=$reportPath; sha256=(Get-CombinedSha256 $reportPath); bindingCount=$bindingMap.bindings.Count; expectedMode=$ExpectedMode }
    if ($model.status -cne 'PASS') { throw "Combined readback failed; report=$reportPath; sha256=$($summary.sha256). Review failures for exact expected/actual values." }
    return $summary
}
