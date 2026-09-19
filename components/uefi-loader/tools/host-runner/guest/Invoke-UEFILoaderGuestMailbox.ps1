[CmdletBinding()]
param(
    [string]$ConfigPath = 'C:\ProgramData\UEFILoaderRunner\guest-runner.config.json',
    [switch]$Once,
    [switch]$TestMode
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$ExpectedMailboxRoot = '\\vmware-host\Shared Folders\UEFILoaderRunner'
$ExpectedInstallRoot = 'C:\ProgramData\UEFILoaderRunner'

function Read-MailboxKey([string]$Path) {
    $FixedKey = [IO.Path]::GetFullPath($Path) -ceq
        'C:\ProgramData\UEFILoaderRunner\mailbox.key'
    $TestKey = $TestMode -and $Once -and
        (Test-PathWithin (Split-Path -Parent $ConfigPath) $Path)
    if (-not $FixedKey -and -not $TestKey) {
        throw 'The mailbox key path is not the fixed installed path.'
    }
    $Key = [IO.File]::ReadAllBytes($Path)
    if ($Key.Length -ne 32) {
        [Array]::Clear($Key, 0, $Key.Length)
        throw 'The mailbox authentication key must contain exactly 32 bytes.'
    }
    return $Key
}

function Get-HmacSha256Hex([byte[]]$Key, [string]$Text) {
    $Data = [Text.Encoding]::UTF8.GetBytes($Text)
    $Hmac = [Security.Cryptography.HMACSHA256]::new($Key)
    try {
        return ([BitConverter]::ToString(
            $Hmac.ComputeHash($Data))).Replace('-', '')
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
        "PackageRelativePath=$($Request.PackageRelativePath)"
        "PackageSha256=$($Request.PackageSha256)"
        "TimeoutSeconds=$($Request.TimeoutSeconds)"
        "SubmittedUtc=$($Request.SubmittedUtc)"
    ) -join "`n"
}

function Get-CanonicalResponse($Result) {
    $Lines = [Collections.Generic.List[string]]::new()
    $Lines.Add("SchemaVersion=$($Result.SchemaVersion)")
    $Lines.Add("RunId=$($Result.RunId)")
    $Lines.Add("Status=$($Result.Status)")
    $MessageBytes = [Text.Encoding]::UTF8.GetBytes([string]$Result.Message)
    try { $Lines.Add('MessageBase64=' + [Convert]::ToBase64String($MessageBytes)) }
    finally { [Array]::Clear($MessageBytes, 0, $MessageBytes.Length) }
    $Lines.Add("CompletedUtc=$($Result.CompletedUtc)")
    foreach ($Evidence in @($Result.Evidence | Sort-Object Name)) {
        $Lines.Add("Evidence=$($Evidence.Name)|$($Evidence.Sha256)|$($Evidence.Length)")
    }
    return $Lines -join "`n"
}

function Test-SafeRunId([string]$Value) {
    return $Value -cmatch `
        '^uefi-windows-(probe|capture)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$'
}

function Test-PathWithin([string]$Root, [string]$Path) {
    $RootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return [IO.Path]::GetFullPath($Path).StartsWith(
        $RootFull, [StringComparison]::OrdinalIgnoreCase)
}

function Write-AtomicJson($Value, [string]$Path) {
    $Parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
        New-Item -ItemType Directory -Path $Parent | Out-Null
    }
    $Temporary = $Path + '.tmp-' + [Guid]::NewGuid().ToString('N')
    try {
        $Value | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath $Temporary -Encoding utf8
        Move-Item -LiteralPath $Temporary -Destination $Path
    }
    finally {
        if (Test-Path -LiteralPath $Temporary -PathType Leaf) {
            Remove-Item -LiteralPath $Temporary -Force
        }
    }
}

function Assert-ExactRequest($Request, [byte[]]$Key) {
    $Expected = @(
        'Action', 'ExpectedProbeVerdict', 'PackageRelativePath',
        'PackageSha256', 'RequestHmacSha256', 'RunId', 'SchemaVersion',
        'SubmittedUtc', 'TimeoutSeconds') | Sort-Object
    $Actual = @($Request.PSObject.Properties.Name) | Sort-Object
    if (($Actual -join "`n") -cne ($Expected -join "`n")) {
        throw 'Request does not match the exact schema.'
    }
    if ($Request.SchemaVersion -ne 1) {
        throw "Unsupported request schema: $($Request.SchemaVersion)"
    }
    if (-not (Test-SafeRunId ([string]$Request.RunId))) {
        throw "Unsafe request RunId: $($Request.RunId)"
    }
    if ([string]$Request.Action -notin @('Probe', 'Capture')) {
        throw 'Request action is not allowlisted.'
    }
    $ExpectedAction = if ([string]$Request.RunId -match '-probe-') {
        'Probe'
    } else { 'Capture' }
    if ([string]$Request.Action -cne $ExpectedAction) {
        throw 'Request action does not match its RunId.'
    }
    if ([string]$Request.ExpectedProbeVerdict -notin @('MET', 'NOT_MET')) {
        throw 'ExpectedProbeVerdict is invalid.'
    }
    if ([int]$Request.TimeoutSeconds -lt 30 -or
        [int]$Request.TimeoutSeconds -gt 600) {
        throw 'Request timeout is outside the allowed range.'
    }
    if ([string]$Request.PackageRelativePath -cne
        "artifacts/$($Request.RunId)/package.zip") {
        throw 'PackageRelativePath is not canonical.'
    }
    if ([string]$Request.PackageSha256 -cnotmatch '^[0-9A-F]{64}$') {
        throw 'PackageSha256 is invalid.'
    }
    $Submitted = [DateTime]::Parse(
        [string]$Request.SubmittedUtc,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    if ([Math]::Abs(([DateTime]::UtcNow - $Submitted).TotalMinutes) -gt 15) {
        throw 'The authenticated mailbox request is stale or from the future.'
    }
    $ExpectedHmac = Get-HmacSha256Hex $Key (Get-CanonicalRequest $Request)
    if (-not (Test-FixedHexEqual $ExpectedHmac `
            ([string]$Request.RequestHmacSha256))) {
        throw 'Request authentication failed.'
    }
}

function Expand-VerifiedPackage($Config, $Request, [string]$RunRoot) {
    $PackagePath = Join-Path $Config.MailboxRoot `
        ([string]$Request.PackageRelativePath).Replace('/', '\')
    if (-not (Test-PathWithin $Config.MailboxRoot $PackagePath) -or
        -not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
        throw 'The package is absent or escaped the mailbox.'
    }
    if (Test-Path -LiteralPath $RunRoot) {
        throw 'The local RunId has already been used.'
    }
    New-Item -ItemType Directory -Path $RunRoot | Out-Null
    $LocalPackage = Join-Path $RunRoot 'package.zip'
    $InputPackage = [IO.File]::Open(
        $PackagePath, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
        $OutputPackage = [IO.File]::Open(
            $LocalPackage, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
            [IO.FileShare]::None)
        try { $InputPackage.CopyTo($OutputPackage) }
        finally { $OutputPackage.Dispose() }
    }
    finally { $InputPackage.Dispose() }
    if ((Get-FileHash -LiteralPath $LocalPackage -Algorithm SHA256).Hash -cne
        [string]$Request.PackageSha256) {
        throw 'The local package snapshot hash does not match the request.'
    }
    $Allowed = @(
        'ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1', 'SHA256SUMS.txt')
    $Archive = [IO.Compression.ZipFile]::OpenRead($LocalPackage)
    try {
        $Names = @($Archive.Entries | ForEach-Object { $_.FullName })
        $ActualNames = (@($Names | Sort-Object) -join "`n")
        $ExpectedNames = (@($Allowed | Sort-Object) -join "`n")
        if ($ActualNames -cne $ExpectedNames) {
            throw 'The package must contain exactly the allowlisted root files.'
        }
        foreach ($Entry in $Archive.Entries) {
            if ($Entry.FullName.Contains('/') -or
                $Entry.FullName.Contains('\') -or
                [IO.Path]::IsPathRooted($Entry.FullName) -or
                $Entry.FullName.Contains(':')) {
                throw "Unsafe ZIP entry: $($Entry.FullName)"
            }
            $Target = Join-Path $RunRoot $Entry.FullName
            if (-not (Test-PathWithin $RunRoot $Target)) {
                throw "ZIP entry escaped the run root: $($Entry.FullName)"
            }
            $Input = $Entry.Open()
            try {
                $Output = [IO.File]::Open(
                    $Target, [IO.FileMode]::CreateNew,
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
        if ($Expected.ContainsKey($Matches[2])) {
            throw "Duplicate checksum entry: $($Matches[2])"
        }
        $Expected[$Matches[2]] = $Matches[1]
    }
    foreach ($Name in @('ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1')) {
        if (-not $Expected.ContainsKey($Name) -or
            (Get-FileHash -LiteralPath (Join-Path $RunRoot $Name) `
                -Algorithm SHA256).Hash -cne $Expected[$Name]) {
            throw "Package checksum validation failed: $Name"
        }
    }
}

function Publish-SignedResult(
    [byte[]]$Key, [string]$ResponseDirectory, [string]$RunId,
    [string]$Status, [string]$Message, [object[]]$Evidence) {
    $Envelope = [ordered]@{
        SchemaVersion = 1
        RunId = $RunId
        Status = $Status
        Message = $Message
        CompletedUtc = [DateTime]::UtcNow.ToString('o')
        Evidence = @($Evidence | Sort-Object Name)
    }
    $Envelope.ResponseHmacSha256 = Get-HmacSha256Hex $Key `
        (Get-CanonicalResponse $Envelope)
    Write-AtomicJson $Envelope (Join-Path $ResponseDirectory 'result.json')
}

function Publish-Failure(
    [byte[]]$Key, [string]$ResponseDirectory, [string]$RunId,
    [string]$Message) {
    Publish-SignedResult -Key $Key -ResponseDirectory $ResponseDirectory `
        -RunId $RunId -Status 'FAIL' -Message $Message -Evidence @()
}

function Invoke-FixedGuestGate(
    [string]$PowerShell, [string[]]$Arguments, [string]$WorkingDirectory,
    [int]$TimeoutSeconds, [string]$StdoutPath, [string]$StderrPath) {
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $PowerShell
    $Info.Arguments = ($Arguments -join ' ')
    $Info.WorkingDirectory = $WorkingDirectory
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    if (-not $Process.Start()) { throw 'Failed to start the fixed guest gate.' }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    $Exited = $Process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $Exited) {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            $TaskKill = "$env:SystemRoot\System32\taskkill.exe"
            $Termination = & $TaskKill /PID $Process.Id /T /F 2>&1
            $TerminationExitCode = $LASTEXITCODE
            $Process.Refresh()
            if ($TerminationExitCode -ne 0 -and -not $Process.HasExited) {
                throw "Failed to terminate timed-out guest gate process tree: $((($Termination | Out-String).Trim()))"
            }
        }
        if (-not $Process.WaitForExit(10000)) {
            throw 'The timed-out guest gate process did not terminate.'
        }
        [IO.File]::WriteAllText($StdoutPath, [string]$StdoutTask.Result)
        [IO.File]::WriteAllText($StderrPath, [string]$StderrTask.Result)
        $ProcessId = $Process.Id
        $Process.Dispose()
        return [pscustomobject]@{
            TimedOut = $true
            ProcessId = $ProcessId
            Terminated = $true
            ExitCode = $null
        }
    }
    [IO.File]::WriteAllText($StdoutPath, [string]$StdoutTask.Result)
    [IO.File]::WriteAllText($StderrPath, [string]$StderrTask.Result)
    $Result = [pscustomobject]@{
        TimedOut = $false
        ProcessId = $Process.Id
        ExitCode = $Process.ExitCode
    }
    $Process.Dispose()
    return $Result
}

function Invoke-Request($Config, [byte[]]$Key, [string]$RequestPath) {
    $Request = Get-Content -Raw -LiteralPath $RequestPath | ConvertFrom-Json
    $RunId = [string]$Request.RunId
    if (-not (Test-SafeRunId $RunId)) {
        throw "Unsafe request RunId: $RunId"
    }
    $ResponseDirectory = Join-Path $Config.MailboxRoot "responses\$RunId"
    $RunRoot = Join-Path $Config.LocalRunRoot $RunId
    try {
        Assert-ExactRequest $Request $Key
        Expand-VerifiedPackage -Config $Config -Request $Request -RunRoot $RunRoot
        New-Item -ItemType Directory -Force -Path $ResponseDirectory | Out-Null
        $PowerShell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
        $Arguments = @(
            '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
            '-File', ('"{0}"' -f (Join-Path $RunRoot 'Invoke-WindowsGuestTest.ps1')),
            '-RunRoot', ('"{0}"' -f $RunRoot),
            '-Action', [string]$Request.Action,
            '-ExpectedProbeVerdict', [string]$Request.ExpectedProbeVerdict)
        $TransportStdout = Join-Path $ResponseDirectory 'transport-stdout.txt'
        $TransportStderr = Join-Path $ResponseDirectory 'transport-stderr.txt'
        $Process = Invoke-FixedGuestGate -PowerShell $PowerShell `
            -Arguments $Arguments -WorkingDirectory $RunRoot `
            -TimeoutSeconds ([int]$Request.TimeoutSeconds) `
            -StdoutPath $TransportStdout -StderrPath $TransportStderr
        if ($Process.TimedOut) {
            $Quarantine = Join-Path $Config.LocalRunRoot 'QUARANTINED.json'
            Write-AtomicJson ([ordered]@{
                SchemaVersion = 1
                RunId = $RunId
                ProcessId = $Process.ProcessId
                Message = 'The fixed Windows guest gate exceeded its deadline.'
                CreatedUtc = [DateTime]::UtcNow.ToString('o')
            }) $Quarantine
            Publish-Failure $Key $ResponseDirectory $RunId `
                'The fixed Windows guest gate exceeded its deadline; its process tree was terminated and the mailbox was quarantined.'
            return 'QUARANTINE'
        }
        $LocalResult = Join-Path $RunRoot 'result.json'
        if (-not (Test-Path -LiteralPath $LocalResult -PathType Leaf)) {
            throw 'The fixed Windows guest gate did not publish result.json.'
        }
        $Result = Get-Content -Raw -LiteralPath $LocalResult | ConvertFrom-Json
        if ($Result.SchemaVersion -ne 1 -or $Result.RunId -cne $RunId -or
            [string]$Result.Status -notin @('PASS', 'FAIL')) {
            throw 'The fixed Windows guest result identity/schema is invalid.'
        }
        $Evidence = @()
        foreach ($Name in @('stdout.txt', 'stderr.txt', 'tcglog.ib',
                'tcglog-full.bin')) {
            $Source = Join-Path $RunRoot $Name
            if (Test-Path -LiteralPath $Source -PathType Leaf) {
                $Destination = Join-Path $ResponseDirectory $Name
                Copy-Item -LiteralPath $Source -Destination $Destination
                $Item = Get-Item -LiteralPath $Destination
                $Evidence += [pscustomobject][ordered]@{
                    Name = $Name
                    Sha256 = (Get-FileHash -LiteralPath $Destination `
                        -Algorithm SHA256).Hash
                    Length = $Item.Length
                }
            }
        }
        Publish-SignedResult -Key $Key -ResponseDirectory $ResponseDirectory `
            -RunId $RunId -Status ([string]$Result.Status) `
            -Message ([string]$Result.Message) -Evidence $Evidence
        return [string]$Result.Status
    }
    catch {
        Publish-Failure $Key $ResponseDirectory $RunId $_.Exception.Message
        return 'FAIL'
    }
}

if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw "Guest-runner configuration not found: $ConfigPath"
}
$Config = Get-Content -Raw -LiteralPath $ConfigPath | ConvertFrom-Json
$FixedPaths = [string]$Config.MailboxRoot -ceq $ExpectedMailboxRoot -and
    [IO.Path]::GetFullPath([string]$Config.LocalRunRoot) -ceq
        (Join-Path $ExpectedInstallRoot 'runs') -and
    [IO.Path]::GetFullPath([string]$Config.KeyPath) -ceq
        (Join-Path $ExpectedInstallRoot 'mailbox.key')
$TestRoot = [IO.Path]::GetFullPath((Split-Path -Parent $ConfigPath))
$BoundedTestPaths = $TestMode -and $Once -and
    (Test-PathWithin $TestRoot ([string]$Config.MailboxRoot)) -and
    (Test-PathWithin $TestRoot ([string]$Config.LocalRunRoot)) -and
    (Test-PathWithin $TestRoot ([string]$Config.KeyPath))
if (($Config.PSObject.Properties.Name | Sort-Object) -join "`n" -cne
        ((@('KeyPath', 'LocalRunRoot', 'MailboxRoot', 'PollMilliseconds',
            'SchemaVersion') | Sort-Object) -join "`n") -or
    $Config.SchemaVersion -ne 1 -or
    (-not $FixedPaths -and -not $BoundedTestPaths) -or
    [int]$Config.PollMilliseconds -lt 250 -or
    [int]$Config.PollMilliseconds -gt 5000) {
    throw 'Guest-runner configuration is invalid.'
}
if (-not $TestMode) {
    $MailboxDeadline = [DateTime]::UtcNow.AddMinutes(3)
    while (-not (Test-Path -LiteralPath $ExpectedMailboxRoot -PathType Container)) {
        if ([DateTime]::UtcNow -ge $MailboxDeadline) {
            throw 'The VMware shared-folder mailbox did not become available.'
        }
        Start-Sleep -Seconds 1
    }
}
$MailboxKey = Read-MailboxKey ([string]$Config.KeyPath)
foreach ($Directory in @(
        $Config.MailboxRoot, (Join-Path $Config.MailboxRoot 'requests'),
        (Join-Path $Config.MailboxRoot 'responses'),
        $Config.LocalRunRoot)) {
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        New-Item -ItemType Directory -Path $Directory | Out-Null
    }
}
$QuarantinePath = Join-Path $Config.LocalRunRoot 'QUARANTINED.json'
if (Test-Path -LiteralPath $QuarantinePath -PathType Leaf) {
    throw "The guest mailbox is quarantined: $QuarantinePath"
}

do {
    $Request = Get-ChildItem -LiteralPath (Join-Path $Config.MailboxRoot 'requests') `
        -Filter '*.json' -File | Sort-Object CreationTimeUtc |
        Select-Object -First 1
    if ($null -eq $Request) {
        if ($Once) { break }
        Start-Sleep -Milliseconds ([int]$Config.PollMilliseconds)
        continue
    }
    $Processing = $Request.FullName + '.processing'
    Move-Item -LiteralPath $Request.FullName -Destination $Processing
    try {
        $Outcome = Invoke-Request -Config $Config -Key $MailboxKey `
            -RequestPath $Processing
        Move-Item -LiteralPath $Processing -Destination (
            $Processing -replace '\.processing$', '.completed')
    }
    catch {
        $RejectedRoot = Join-Path $Config.MailboxRoot 'rejected'
        if (-not (Test-Path -LiteralPath $RejectedRoot -PathType Container)) {
            New-Item -ItemType Directory -Path $RejectedRoot | Out-Null
        }
        $Rejected = Join-Path $RejectedRoot (
            [IO.Path]::GetFileName($Processing) + '.rejected')
        Move-Item -LiteralPath $Processing -Destination $Rejected
        if ($Once) { throw }
        continue
    }
    if ($Outcome -ceq 'QUARANTINE') {
        throw 'A guest gate timed out; the mailbox is quarantined.'
    }
} while (-not $Once)
