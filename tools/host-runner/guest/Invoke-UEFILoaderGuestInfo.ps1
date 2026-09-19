[CmdletBinding()]
param(
    [string]$ConfigPath = 'C:\ProgramData\UEFILoaderRunner\guest-runner.config.json',
    [switch]$Once,
    [switch]$TestMode
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$ExpectedInstallRoot = 'C:\ProgramData\UEFILoaderRunner'
$ExpectedGuestInfoPrefix = 'guestinfo.uefiloader'
$ExpectedKeyPath = Join-Path $ExpectedInstallRoot 'mailbox.key'
$ExpectedRunRoot = Join-Path $ExpectedInstallRoot 'runs'
$WorkerStatusPath = Join-Path $ExpectedInstallRoot 'worker-status.json'
$ExpectedPowerShell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$MaxPackageBytes = 4MB
$MaxEvidenceBytes = 32MB
$MaxEvidenceFileBytes = 8MB
$MaxMessageBytes = 4096
$MaxChunkBytes = 9000
$MaxChunkCount = 4096
$ChunkNameWidth = 6
# Commit markers: guestinfo.uefiloader.request.manifest and
# guestinfo.uefiloader.response.manifest. Request/response chunks are scoped
# by the safe RunId below and are written before their corresponding marker.
$RequestManifestName = "$ExpectedGuestInfoPrefix.request.manifest"
$ResponseManifestName = "$ExpectedGuestInfoPrefix.response.manifest"
$WorkerStatusName = "$ExpectedGuestInfoPrefix.worker.status"

function Test-PathWithin([string]$Root, [string]$Path) {
    $RootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $PathFull = [IO.Path]::GetFullPath($Path)
    return $PathFull.StartsWith($RootFull, [StringComparison]::OrdinalIgnoreCase)
}

function Write-AtomicJson($Value, [string]$Path) {
    $Parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
        New-Item -ItemType Directory -Path $Parent | Out-Null
    }
    $Temporary = $Path + '.tmp-' + [Guid]::NewGuid().ToString('N')
    try {
        $Value | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $Temporary -Encoding utf8
        Move-Item -LiteralPath $Temporary -Destination $Path -Force
    }
    finally {
        if (Test-Path -LiteralPath $Temporary -PathType Leaf) {
            Remove-Item -LiteralPath $Temporary -Force
        }
    }
}

function Get-HmacSha256Hex([byte[]]$Key, [string]$Text) {
    $Data = [Text.Encoding]::UTF8.GetBytes($Text)
    $Hmac = [Security.Cryptography.HMACSHA256]::new($Key)
    try {
        return ([BitConverter]::ToString($Hmac.ComputeHash($Data))).Replace('-', '')
    }
    finally {
        $Hmac.Dispose()
        [Array]::Clear($Data, 0, $Data.Length)
    }
}

function Test-FixedHexEqual([string]$Left, [string]$Right) {
    if ($Left -cnotmatch '^[0-9A-F]{64}$' -or
        $Right -cnotmatch '^[0-9A-F]{64}$') { return $false }
    $Difference = 0
    for ($Index = 0; $Index -lt 64; $Index++) {
        $Difference = $Difference -bor (
            [int][char]$Left[$Index] -bxor [int][char]$Right[$Index])
    }
    return $Difference -eq 0
}

function Get-CanonicalRequest($Request) {
    return @(
        "SchemaVersion=$($Request.SchemaVersion)"
        "RunId=$($Request.RunId)"
        "Action=$($Request.Action)"
        "ExpectedProbeVerdict=$($Request.ExpectedProbeVerdict)"
        "PackageSha256=$($Request.PackageSha256)"
        "PackageSize=$($Request.PackageSize)"
        "PackageChunkSize=$($Request.PackageChunkSize)"
        "PackageChunkCount=$($Request.PackageChunkCount)"
        "TimeoutSeconds=$($Request.TimeoutSeconds)"
        "SubmittedUtc=$($Request.SubmittedUtc)"
    ) -join "`n"
}

function Get-CanonicalResponse($Response) {
    return @(
        "SchemaVersion=$($Response.SchemaVersion)"
        "RunId=$($Response.RunId)"
        "Status=$($Response.Status)"
        "MessageBase64=$($Response.MessageBase64)"
        "CompletedUtc=$($Response.CompletedUtc)"
        "EvidenceSha256=$($Response.EvidenceSha256)"
        "EvidenceSize=$($Response.EvidenceSize)"
        "EvidenceChunkSize=$($Response.EvidenceChunkSize)"
        "EvidenceChunkCount=$($Response.EvidenceChunkCount)"
    ) -join "`n"
}

function Get-CanonicalWorkerStatus($Status) {
    return @(
        "SchemaVersion=$($Status.SchemaVersion)"
        "State=$($Status.State)"
        "WorkerSid=$($Status.WorkerSid)"
        "StartedUtc=$($Status.StartedUtc)"
        "UpdatedUtc=$($Status.UpdatedUtc)"
    ) -join "`n"
}

function Test-SafeRunId([string]$Value) {
    return $Value -cmatch '^uefi-windows-(probe|capture)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$'
}

function Get-ChunkName([string]$Direction, [string]$RunId, [int]$Index) {
    if ($Direction -notin @('request', 'response') -or -not (Test-SafeRunId $RunId) -or
        $Index -lt 0 -or $Index -ge $MaxChunkCount) {
        throw 'Unsafe Guest Variables chunk name.'
    }
    return "$ExpectedGuestInfoPrefix.$Direction.chunk.$RunId.$('{0:D6}' -f $Index)"
}

function Get-ConfigValue([object]$Config, [string]$Name) {
    $Property = $Config.PSObject.Properties[$Name]
    if ($null -eq $Property) { throw "Guest Variables configuration is missing $Name." }
    return $Property.Value
}

function Read-MailboxKey([string]$Path) {
    $FullPath = [IO.Path]::GetFullPath($Path)
    $Allowed = $FullPath -ceq $ExpectedKeyPath
    if (-not $Allowed -and -not $TestMode) {
        throw 'The Guest Variables key path is not the fixed installed path.'
    }
    if (-not $Allowed -and -not (Test-PathWithin (Split-Path -Parent $ConfigPath) $FullPath)) {
        throw 'The test Guest Variables key path escaped its test root.'
    }
    $Key = [IO.File]::ReadAllBytes($FullPath)
    if ($Key.Length -ne 32) {
        [Array]::Clear($Key, 0, $Key.Length)
        throw 'The Guest Variables authentication key must contain exactly 32 bytes.'
    }
    return $Key
}

function Invoke-Vmtoolsd([string]$VmtoolsdPath, [ValidateSet('info-get', 'info-set')][string]$Verb,
    [string]$Name, [string]$Value) {
    if ($Name -notmatch '^guestinfo\.uefiloader\.(worker\.status|request\.manifest|response\.manifest|request\.chunk\.[a-z0-9-]+\.[0-9]{6}|response\.chunk\.[a-z0-9-]+\.[0-9]{6})$') {
        throw "Unsafe Guest Variables name: $Name"
    }
    if ($Verb -eq 'info-set' -and $Value -notmatch '^[A-Za-z0-9+/=]*$') {
        throw 'Guest Variables values must be base64 or empty.'
    }
    $Command = if ($Verb -eq 'info-get') { "$Verb $Name" } else { "$Verb $Name $Value" }
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $VmtoolsdPath
    $Info.Arguments = '--cmd "' + $Command.Replace('"', '\"') + '"'
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    if (-not $Process.Start()) { throw 'Failed to start VMware Tools.' }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit(15000)) {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            & "$env:SystemRoot\System32\taskkill.exe" /PID $Process.Id /T /F | Out-Null
        }
        throw "vmtoolsd $Verb timed out."
    }
    $Stdout = [string]$StdoutTask.Result
    $Stderr = [string]$StderrTask.Result
    $ExitCode = $Process.ExitCode
    $Process.Dispose()
    if ($Verb -eq 'info-get') {
        $Trimmed = $Stdout.Trim()
        if ($ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($Trimmed) -or
            $Trimmed -match '(?i)(no value|unknown property|not found)') {
            return $null
        }
        return $Trimmed
    }
    if ($ExitCode -ne 0) {
        throw "vmtoolsd info-set failed: $($Stderr.Trim())"
    }
    return $true
}

function ConvertFrom-Base64Exact([string]$Value, [int]$MaxBytes, [string]$Description) {
    if ([string]::IsNullOrEmpty($Value) -or $Value -notmatch '^[A-Za-z0-9+/]*={0,2}$' -or
        ($Value.Length % 4) -ne 0) {
        throw "$Description is not canonical base64."
    }
    try { $Bytes = [Convert]::FromBase64String($Value) }
    catch { throw "$Description is not valid base64." }
    if ($Bytes.Length -gt $MaxBytes -or
        [Convert]::ToBase64String($Bytes) -cne $Value) {
        [Array]::Clear($Bytes, 0, $Bytes.Length)
        throw "$Description exceeds its bounded base64 representation."
    }
    return $Bytes
}

function Assert-ExactRequest($Request, [byte[]]$Key) {
    $Expected = @(
        'Action', 'ExpectedProbeVerdict', 'PackageChunkCount',
        'PackageChunkSize', 'PackageSha256', 'PackageSize',
        'RequestHmacSha256', 'RunId', 'SchemaVersion', 'SubmittedUtc',
        'TimeoutSeconds') | Sort-Object
    $Actual = @($Request.PSObject.Properties.Name) | Sort-Object
    if (($Actual -join "`n") -cne ($Expected -join "`n")) {
        throw 'Request manifest does not match the exact schema.'
    }
    if ([int]$Request.SchemaVersion -ne 1) { throw 'Unsupported request schema.' }
    if (-not (Test-SafeRunId ([string]$Request.RunId))) { throw 'Unsafe request RunId.' }
    if ([string]$Request.Action -notin @('Probe', 'Capture')) { throw 'Request action is not allowlisted.' }
    $ExpectedAction = if ([string]$Request.RunId -match '-probe-') { 'Probe' } else { 'Capture' }
    if ([string]$Request.Action -cne $ExpectedAction) { throw 'Request action does not match its RunId.' }
    if ([string]$Request.ExpectedProbeVerdict -notin @('MET', 'NOT_MET')) { throw 'Invalid expected probe verdict.' }
    $PackageSize = [int64]$Request.PackageSize
    $ChunkSize = [int]$Request.PackageChunkSize
    $ChunkCount = [int]$Request.PackageChunkCount
    if ($PackageSize -lt 1 -or $PackageSize -gt $MaxPackageBytes -or
        $ChunkSize -lt 256 -or $ChunkSize -gt $MaxChunkBytes -or
        $ChunkCount -lt 1 -or $ChunkCount -gt $MaxChunkCount -or
        [int64]$ChunkCount * $ChunkSize -lt $PackageSize -or
        [int64]($ChunkCount - 1) * $ChunkSize -ge $PackageSize) {
        throw 'Request package bounds are invalid.'
    }
    if ([string]$Request.PackageSha256 -cnotmatch '^[0-9A-F]{64}$') { throw 'PackageSha256 is invalid.' }
    if ([int]$Request.TimeoutSeconds -lt 30 -or [int]$Request.TimeoutSeconds -gt 600) {
        throw 'Request timeout is outside the allowed range.'
    }
    try {
        $Submitted = [DateTime]::Parse([string]$Request.SubmittedUtc,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    }
    catch { throw 'SubmittedUtc is invalid.' }
    if ([Math]::Abs(([DateTime]::UtcNow - $Submitted).TotalMinutes) -gt 15) {
        throw 'The authenticated request is stale or from the future.'
    }
    $ExpectedHmac = Get-HmacSha256Hex $Key (Get-CanonicalRequest $Request)
    if (-not (Test-FixedHexEqual $ExpectedHmac ([string]$Request.RequestHmacSha256))) {
        throw 'Request authentication failed.'
    }
}

function Receive-RequestPackage($Config, $Request, [string]$RunRoot) {
    if (Test-Path -LiteralPath $RunRoot) { throw 'The local RunId has already been used.' }
    $PackagePath = Join-Path $RunRoot 'package.zip'
    New-Item -ItemType Directory -Path $RunRoot | Out-Null
    $Output = [IO.File]::Open($PackagePath, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $Total = [int64]0
        for ($Index = 0; $Index -lt [int]$Request.PackageChunkCount; $Index++) {
            $Name = Get-ChunkName 'request' ([string]$Request.RunId) $Index
            $Chunk = Invoke-Vmtoolsd ([string]$Config.VmtoolsdPath) 'info-get' $Name $null
            if ($null -eq $Chunk) { throw "Incomplete package chunk: $Index" }
            $Bytes = ConvertFrom-Base64Exact $Chunk ([int]$Request.PackageChunkSize) "Package chunk $Index"
            $ExpectedLength = if ($Index -eq [int]$Request.PackageChunkCount - 1) {
                [int]$Request.PackageSize - ([int]$Request.PackageChunkSize * $Index)
            } else { [int]$Request.PackageChunkSize }
            if ($Bytes.Length -ne $ExpectedLength) {
                [Array]::Clear($Bytes, 0, $Bytes.Length)
                throw "Package chunk $Index has the wrong size."
            }
            $Output.Write($Bytes, 0, $Bytes.Length)
            $Total += $Bytes.Length
            [Array]::Clear($Bytes, 0, $Bytes.Length)
        }
    }
    finally { $Output.Dispose() }
    if ($Total -ne [int64]$Request.PackageSize) { throw 'Package size does not match its manifest.' }
    if ((Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash -cne
        [string]$Request.PackageSha256) { throw 'Package SHA256 does not match its manifest.' }
    return $PackagePath
}

function Expand-VerifiedPackage([string]$PackagePath, [string]$RunRoot) {
    $Allowed = @('ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1', 'SHA256SUMS.txt')
    $Archive = [IO.Compression.ZipFile]::OpenRead($PackagePath)
    try {
        $Names = @($Archive.Entries | ForEach-Object { $_.FullName })
        if ((@($Names | Sort-Object) -join "`n") -cne (@($Allowed | Sort-Object) -join "`n")) {
            throw 'The package must contain exactly the allowlisted root files.'
        }
        $ExpandedBytes = [int64]0
        foreach ($Entry in $Archive.Entries) {
            if ($Entry.FullName.Contains('/') -or $Entry.FullName.Contains('\') -or
                [IO.Path]::IsPathRooted($Entry.FullName) -or $Entry.FullName.Contains(':')) {
                throw "Unsafe ZIP entry: $($Entry.FullName)"
            }
            if ($Entry.Length -lt 0 -or $Entry.Length -gt $MaxPackageBytes) {
                throw "ZIP entry is too large: $($Entry.FullName)"
            }
            $ExpandedBytes += [int64]$Entry.Length
            if ($ExpandedBytes -gt $MaxPackageBytes) {
                throw 'Expanded package exceeds the bounded package size.'
            }
            $Target = Join-Path $RunRoot $Entry.FullName
            if (-not (Test-PathWithin $RunRoot $Target)) { throw 'ZIP entry escaped the run root.' }
            $Input = $Entry.Open()
            try {
                $Output = [IO.File]::Open($Target, [IO.FileMode]::CreateNew,
                    [IO.FileAccess]::Write, [IO.FileShare]::None)
                try { $Input.CopyTo($Output) }
                finally { $Output.Dispose() }
            }
            finally { $Input.Dispose() }
        }
    }
    finally { $Archive.Dispose() }
    $Expected = @{}
    foreach ($Line in Get-Content -LiteralPath (Join-Path $RunRoot 'SHA256SUMS.txt')) {
        if ($Line -cnotmatch '^([0-9A-F]{64})  (ib-tcg-dump\.exe|Invoke-WindowsGuestTest\.ps1)$') {
            throw "Invalid checksum line: $Line"
        }
        if ($Expected.ContainsKey($Matches[2])) { throw 'Duplicate checksum entry.' }
        $Expected[$Matches[2]] = $Matches[1]
    }
    foreach ($Name in @('ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1')) {
        $Path = Join-Path $RunRoot $Name
        if (-not $Expected.ContainsKey($Name) -or
            (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -cne $Expected[$Name]) {
            throw "Package checksum validation failed: $Name"
        }
    }
}

function Invoke-FixedGuestGate([string]$RunRoot, [string]$Action,
    [string]$ExpectedProbeVerdict, [int]$TimeoutSeconds, [string]$StdoutPath,
    [string]$StderrPath) {
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $ExpectedPowerShell
    $Args = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
        '-File', (Join-Path $RunRoot 'Invoke-WindowsGuestTest.ps1'),
        '-RunRoot', $RunRoot, '-Action', $Action,
        '-ExpectedProbeVerdict', $ExpectedProbeVerdict)
    $Quote = { param([string]$Value) '"' + $Value.Replace('"', '\"') + '"' }
    $Info.Arguments = (($Args | ForEach-Object { & $Quote ([string]$_) }) -join ' ')
    $Info.WorkingDirectory = $RunRoot
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    if (-not $Process.Start()) { throw 'Failed to start the fixed guest gate.' }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    # Invoke-WindowsGuestTest has its own five-minute child deadline. Keep the
    # supervisor below that deadline so taskkill always owns the whole process
    # tree instead of allowing an orphaned ib-tcg-dump.exe to survive it.
    $EffectiveTimeoutSeconds = [Math]::Min($TimeoutSeconds, 290)
    $Exited = $Process.WaitForExit($EffectiveTimeoutSeconds * 1000)
    if (-not $Exited) {
        $Pid = $Process.Id
        $Process.Refresh()
        if (-not $Process.HasExited) {
            & "$env:SystemRoot\System32\taskkill.exe" /PID $Pid /T /F | Out-Null
        }
        if (-not $Process.WaitForExit(10000)) { throw 'Timed-out guest gate did not terminate.' }
        [IO.File]::WriteAllText($StdoutPath, [string]$StdoutTask.Result)
        [IO.File]::WriteAllText($StderrPath, [string]$StderrTask.Result)
        $Process.Dispose()
        return [pscustomobject]@{ TimedOut = $true; ProcessId = $Pid; ExitCode = $null }
    }
    [IO.File]::WriteAllText($StdoutPath, [string]$StdoutTask.Result)
    [IO.File]::WriteAllText($StderrPath, [string]$StderrTask.Result)
    $Result = [pscustomobject]@{ TimedOut = $false; ProcessId = $Process.Id; ExitCode = $Process.ExitCode }
    $Process.Dispose()
    return $Result
}

function New-BoundedEvidenceArchive([string]$RunRoot, [string]$ArchivePath) {
    $Allowed = @('stdout.txt', 'stderr.txt', 'tcglog.ib', 'tcglog-full.bin',
        'kernel-metadata.json')
    $Selected = @()
    $Total = [int64]0
    foreach ($Name in $Allowed) {
        $Path = Join-Path $RunRoot $Name
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { continue }
        $Item = Get-Item -LiteralPath $Path
        if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            $Item.Length -gt $MaxEvidenceFileBytes) { throw "Evidence file is unsafe or too large: $Name" }
        $Total += $Item.Length
        if ($Total -gt $MaxEvidenceBytes) { throw 'Evidence exceeds the bounded archive size.' }
        $Selected += [pscustomobject]@{ Name = $Name; Path = $Path }
    }
    $Archive = [IO.Compression.ZipFile]::Open($ArchivePath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($File in $Selected) {
            $Entry = $Archive.CreateEntry($File.Name, [IO.Compression.CompressionLevel]::Optimal)
            $Input = [IO.File]::OpenRead($File.Path)
            $Output = $Entry.Open()
            try { $Input.CopyTo($Output) }
            finally { $Output.Dispose(); $Input.Dispose() }
        }
    }
    finally { $Archive.Dispose() }
    $Info = Get-Item -LiteralPath $ArchivePath
    if ($Info.Length -gt $MaxEvidenceBytes) { throw 'Compressed evidence exceeds the bounded archive size.' }
    return $Info
}

function Publish-Response($Config, [byte[]]$Key, [string]$RunId, [string]$Status,
    [string]$Message, [string]$EvidencePath) {
    if ([Text.Encoding]::UTF8.GetByteCount($Message) -gt $MaxMessageBytes) { $Message = $Message.Substring(0, 1000) }
    $EvidenceSize = [int64]0
    $EvidenceHash = ('0' * 64)
    $EvidenceChunkSize = $MaxChunkBytes
    $EvidenceBytes = [byte[]]@()
    if (-not [string]::IsNullOrWhiteSpace($EvidencePath) -and
        (Test-Path -LiteralPath $EvidencePath -PathType Leaf)) {
        $EvidenceBytes = [IO.File]::ReadAllBytes($EvidencePath)
        if ($EvidenceBytes.Length -gt $MaxEvidenceBytes) { throw 'Evidence archive is too large.' }
        $EvidenceSize = $EvidenceBytes.Length
        $EvidenceHash = (Get-FileHash -LiteralPath $EvidencePath -Algorithm SHA256).Hash
    }
    $ChunkCount = if ($EvidenceSize -eq 0) { 0 } else {
        [int][Math]::Ceiling($EvidenceSize / [double]$EvidenceChunkSize)
    }
    for ($Index = 0; $Index -lt $ChunkCount; $Index++) {
        $Offset = $Index * $EvidenceChunkSize
        $Length = [Math]::Min($EvidenceChunkSize, $EvidenceBytes.Length - $Offset)
        $Chunk = New-Object byte[] $Length
        [Array]::Copy($EvidenceBytes, $Offset, $Chunk, 0, $Length)
        try {
            Invoke-Vmtoolsd ([string]$Config.VmtoolsdPath) 'info-set' (Get-ChunkName 'response' $RunId $Index) ([Convert]::ToBase64String($Chunk)) | Out-Null
        }
        finally { [Array]::Clear($Chunk, 0, $Chunk.Length) }
    }
    $MessageBytes = [Text.Encoding]::UTF8.GetBytes($Message)
    try {
        $Response = [ordered]@{
            SchemaVersion = 1
            RunId = $RunId
            Status = $Status
            MessageBase64 = [Convert]::ToBase64String($MessageBytes)
            CompletedUtc = [DateTime]::UtcNow.ToString('o')
            EvidenceSha256 = $EvidenceHash
            EvidenceSize = $EvidenceSize
            EvidenceChunkSize = $EvidenceChunkSize
            EvidenceChunkCount = $ChunkCount
        }
    }
    finally { [Array]::Clear($MessageBytes, 0, $MessageBytes.Length) }
    $Response.ResponseHmacSha256 = Get-HmacSha256Hex $Key (Get-CanonicalResponse $Response)
    $ManifestBytes = [Text.Encoding]::UTF8.GetBytes(($Response | ConvertTo-Json -Compress -Depth 6))
    try {
        Invoke-Vmtoolsd ([string]$Config.VmtoolsdPath) 'info-set' $ResponseManifestName ([Convert]::ToBase64String($ManifestBytes)) | Out-Null
    }
    finally {
        [Array]::Clear($ManifestBytes, 0, $ManifestBytes.Length)
        if ($EvidenceBytes.Length -gt 0) { [Array]::Clear($EvidenceBytes, 0, $EvidenceBytes.Length) }
    }
}

function Publish-Failure($Config, [byte[]]$Key, [string]$RunId, [string]$Message) {
    Publish-Response -Config $Config -Key $Key -RunId $RunId -Status 'FAIL' -Message $Message -EvidencePath $null
}

function Publish-WorkerStatus($Config, [byte[]]$Key, [string]$StartedUtc) {
    $Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not $Identity.IsSystem -or $Identity.User.Value -cne 'S-1-5-18') {
        throw 'The Guest Variables worker is not running as LocalSystem.'
    }
    $Status = [ordered]@{
        SchemaVersion = 1
        State = 'READY'
        WorkerSid = $Identity.User.Value
        StartedUtc = $StartedUtc
        UpdatedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $Status.WorkerHmacSha256 = Get-HmacSha256Hex $Key `
        (Get-CanonicalWorkerStatus $Status)
    $Bytes = [Text.Encoding]::UTF8.GetBytes(
        ($Status | ConvertTo-Json -Compress -Depth 4))
    try {
        Invoke-Vmtoolsd ([string]$Config.VmtoolsdPath) 'info-set' `
            $WorkerStatusName ([Convert]::ToBase64String($Bytes)) | Out-Null
    }
    finally { [Array]::Clear($Bytes, 0, $Bytes.Length) }
}

if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) { throw "Guest Variables configuration not found: $ConfigPath" }
$Config = Get-Content -Raw -LiteralPath $ConfigPath | ConvertFrom-Json
$ExpectedConfigProperties = @('GuestInfoPrefix', 'KeyPath', 'LocalRunRoot', 'PollMilliseconds', 'SchemaVersion', 'VmtoolsdPath') | Sort-Object
if ((@($Config.PSObject.Properties.Name) | Sort-Object) -join "`n" -cne ($ExpectedConfigProperties -join "`n") -or
    [int]$Config.SchemaVersion -ne 1 -or [string]$Config.GuestInfoPrefix -cne $ExpectedGuestInfoPrefix -or
    [string]$Config.LocalRunRoot -cne $ExpectedRunRoot -or
    [string]$Config.KeyPath -cne $ExpectedKeyPath -or
    [int]$Config.PollMilliseconds -lt 250 -or [int]$Config.PollMilliseconds -gt 5000 -or
    [string]$Config.VmtoolsdPath -notin @(
        'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe',
        'C:\Program Files (x86)\VMware\VMware Tools\vmtoolsd.exe')) {
    throw 'Guest Variables configuration path or value is not canonical.'
}
$Key = Read-MailboxKey ([string]$Config.KeyPath)
foreach ($Directory in @([string]$Config.LocalRunRoot)) {
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { New-Item -ItemType Directory -Path $Directory | Out-Null }
}
$WorkerIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$WorkerStartedUtc = [DateTime]::UtcNow.ToString('o')
$StatusDeadline = [DateTime]::UtcNow.AddSeconds(180)
do {
    try {
        Publish-WorkerStatus -Config $Config -Key $Key `
            -StartedUtc $WorkerStartedUtc
        break
    }
    catch {
        if ([DateTime]::UtcNow -ge $StatusDeadline) { throw }
        Start-Sleep -Seconds 1
    }
} while ($true)
$NextWorkerStatusUtc = [DateTime]::UtcNow.AddSeconds(5)
Write-AtomicJson ([ordered]@{
    SchemaVersion = 1
    Phase = 'Polling'
    Identity = $WorkerIdentity.Name
    IsSystem = $WorkerIdentity.IsSystem
    UpdatedUtc = [DateTime]::UtcNow.ToString('o')
}) $WorkerStatusPath
$QuarantinePath = Join-Path $Config.LocalRunRoot 'QUARANTINED.json'
if (Test-Path -LiteralPath $QuarantinePath -PathType Leaf) { throw "The Guest Variables runner is quarantined: $QuarantinePath" }

$LastManifest = $null
try {
    do {
        if ([DateTime]::UtcNow -ge $NextWorkerStatusUtc) {
            try {
                Publish-WorkerStatus -Config $Config -Key $Key `
                    -StartedUtc $WorkerStartedUtc
            }
            catch {
                Write-AtomicJson ([ordered]@{
                    SchemaVersion = 1
                    Phase = 'HeartbeatFailed'
                    Message = $_.Exception.Message
                    UpdatedUtc = [DateTime]::UtcNow.ToString('o')
                }) $WorkerStatusPath
            }
            $NextWorkerStatusUtc = [DateTime]::UtcNow.AddSeconds(5)
        }
        $ManifestValue = Invoke-Vmtoolsd ([string]$Config.VmtoolsdPath) 'info-get' $RequestManifestName $null
        if ($null -eq $ManifestValue -or $ManifestValue -ceq $LastManifest) {
            if ($Once) { break }
            Start-Sleep -Milliseconds ([int]$Config.PollMilliseconds)
            continue
        }
        $LastManifest = $ManifestValue
        Write-AtomicJson ([ordered]@{
            SchemaVersion = 1
            Phase = 'ManifestSeen'
            ManifestLength = $ManifestValue.Length
            UpdatedUtc = [DateTime]::UtcNow.ToString('o')
        }) $WorkerStatusPath
        $Request = $null
        $RunId = $null
        try {
            $ManifestBytes = ConvertFrom-Base64Exact $ManifestValue 65536 'Request manifest'
            try { $Request = [Text.Encoding]::UTF8.GetString($ManifestBytes) | ConvertFrom-Json }
            finally { [Array]::Clear($ManifestBytes, 0, $ManifestBytes.Length) }
            $RunId = [string]$Request.RunId
        }
        catch {
            Write-AtomicJson ([ordered]@{
                SchemaVersion = 1
                Phase = 'ManifestRejected'
                Message = $_.Exception.Message
                UpdatedUtc = [DateTime]::UtcNow.ToString('o')
            }) $WorkerStatusPath
            # A malformed commit marker cannot safely identify a response
            # channel. Mark it consumed and keep the SYSTEM worker alive.
            if ($Once) { throw }
            continue
        }
        try {
            Assert-ExactRequest $Request $Key
            Write-AtomicJson ([ordered]@{
                SchemaVersion = 1
                Phase = 'RequestAccepted'
                RunId = $RunId
                UpdatedUtc = [DateTime]::UtcNow.ToString('o')
            }) $WorkerStatusPath
            $RunRoot = Join-Path $Config.LocalRunRoot $RunId
            $PackagePath = Receive-RequestPackage -Config $Config -Request $Request -RunRoot $RunRoot
            Expand-VerifiedPackage -PackagePath $PackagePath -RunRoot $RunRoot
            $ResponseDirectory = $RunRoot
            # Keep the supervisor's PowerShell output separate from the fixed
            # gate's allowlisted stdout/stderr evidence files.
            $Process = Invoke-FixedGuestGate -RunRoot $RunRoot -Action ([string]$Request.Action) -ExpectedProbeVerdict ([string]$Request.ExpectedProbeVerdict) -TimeoutSeconds ([int]$Request.TimeoutSeconds) -StdoutPath (Join-Path $ResponseDirectory 'runner-stdout.txt') -StderrPath (Join-Path $ResponseDirectory 'runner-stderr.txt')
            if ($Process.TimedOut) {
                Write-AtomicJson ([ordered]@{ SchemaVersion = 1; RunId = $RunId; ProcessId = $Process.ProcessId; Message = 'The fixed Windows guest gate exceeded its deadline.'; CreatedUtc = [DateTime]::UtcNow.ToString('o') }) $QuarantinePath
                Publish-Failure -Config $Config -Key $Key -RunId $RunId -Message 'The fixed Windows guest gate exceeded its deadline; its process tree was terminated and the Guest Variables runner was quarantined.'
                throw 'The Guest Variables runner is quarantined after a timeout.'
            }
            $ResultPath = Join-Path $RunRoot 'result.json'
            if (-not (Test-Path -LiteralPath $ResultPath -PathType Leaf)) { throw 'The fixed Windows guest gate did not publish result.json.' }
            $Result = Get-Content -Raw -LiteralPath $ResultPath | ConvertFrom-Json
            if ([int]$Result.SchemaVersion -ne 1 -or [string]$Result.RunId -cne $RunId -or [string]$Result.Status -notin @('PASS', 'FAIL')) { throw 'The fixed Windows guest result is invalid.' }
            $ArchivePath = Join-Path $RunRoot 'evidence.zip'
            $Archive = New-BoundedEvidenceArchive -RunRoot $RunRoot -ArchivePath $ArchivePath
            Publish-Response -Config $Config -Key $Key -RunId $RunId -Status ([string]$Result.Status) -Message ([string]$Result.Message) -EvidencePath $ArchivePath
            Write-AtomicJson ([ordered]@{
                SchemaVersion = 1
                Phase = 'ResponsePublished'
                RunId = $RunId
                UpdatedUtc = [DateTime]::UtcNow.ToString('o')
            }) $WorkerStatusPath
        }
        catch {
            $RequestFailure = $_.Exception.Message
            if ($RunId -and (Test-SafeRunId $RunId)) {
                try {
                    Publish-Failure -Config $Config -Key $Key -RunId $RunId `
                        -Message $RequestFailure
                    Write-AtomicJson ([ordered]@{
                        SchemaVersion = 1
                        Phase = 'FailurePublished'
                        RunId = $RunId
                        Message = $RequestFailure
                        UpdatedUtc = [DateTime]::UtcNow.ToString('o')
                    }) $WorkerStatusPath
                }
                catch {
                    Write-AtomicJson ([ordered]@{
                        SchemaVersion = 1
                        Phase = 'FailurePublishFailed'
                        RunId = $RunId
                        Message = $RequestFailure
                        PublishMessage = $_.Exception.Message
                        UpdatedUtc = [DateTime]::UtcNow.ToString('o')
                    }) $WorkerStatusPath
                }
            }
            if ($RequestFailure -like '*quarantined*') { throw }
        }
        if ($Once) { break }
    } while ($true)
}
finally { [Array]::Clear($Key, 0, $Key.Length) }
