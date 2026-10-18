[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$VmxPath,

    [Parameter(Mandatory = $true)]
    [string]$GuestScriptPath,

    [Parameter(Mandatory = $true)]
    [string]$CompanionExePath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [ValidateSet('Storage', 'Nic')]
    [string]$TestProfile = 'Storage',

    [ValidateSet('Probe')]
    [string]$Action = 'Probe',

    [ValidateSet('MET', 'NOT_MET')]
    [string]$ExpectedProbeVerdict,

    [ValidateRange(30, 280)]
    [int]$GuestTimeoutSeconds = 240,

    [ValidateRange(30, 300)]
    [int]$WaitTimeoutSeconds = 300,

    [ValidatePattern('^uefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$')]
    [string]$ResumeRunId,

    [string]$ConfigPath = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\tools\host-runner\host-runner.config.json'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$script:MainRepoRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$script:EvidenceNamespace = if ($TestProfile -ceq 'Nic') { 'nic-vm-20261017' } else { 'device-vm-20261017' }
$script:ExpectedOutputDirectory = Join-Path $script:MainRepoRoot `
    ('spoof-handoff\reports\' + $script:EvidenceNamespace)
$script:ExpectedVmxRoot = Join-Path $script:MainRepoRoot `
    ('spoof-handoff\artifacts\' + $script:EvidenceNamespace)
$script:ChunkBytes = 9000
$script:ChunkChars = 12000
$script:MaxPackageBytes = 4MB
$script:MaxEvidenceBytes = 32MB
$script:MaxEvidenceFileBytes = 8MB
$script:MaxManifestChars = 65536
$script:Tombstone = 'MA=='
$script:RequestManifestName = 'uefiloader.request.manifest'
$script:ResponseManifestName = 'uefiloader.response.manifest'
$script:WorkerStatusName = 'uefiloader.worker.status'
$script:RequestChunkPrefix = 'uefiloader.request.chunk.'
$script:ResponseChunkPrefix = 'uefiloader.response.chunk.'
$script:AllowedEvidence = @(
    'stdout.txt', 'stderr.txt', 'tcglog.ib', 'tcglog-full.bin',
    'kernel-metadata.json'
)
$script:OwnedRequestNames = [Collections.Generic.List[string]]::new()
$script:OwnedResponseNames = [Collections.Generic.List[string]]::new()
$script:CleanupAttempts = [Collections.Generic.List[string]]::new()
$script:VmrunErrors = [Collections.Generic.List[object]]::new()
$script:ResponseManifestOwned = $false
$script:RunDirectory = $null
$script:RunId = $null
$script:VmPassword = $null
$script:MailboxKey = $null
$script:MailboxKeyBase64 = $null
$script:MailboxKeyCredential = $null
$script:VmCredential = $null
$script:Config = $null
$script:VmrunPath = $null

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Test-PathWithin([string]$Root, [string]$Path) {
    $RootFull = (Get-FullPath $Root) + '\'
    $PathFull = Get-FullPath $Path
    return $PathFull.StartsWith($RootFull, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NotReparsePoint([string]$Path, [string]$Description) {
    $Item = Get-Item -LiteralPath $Path -Force
    if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description may not be a reparse point: $Path"
    }
    return $Item
}

function Assert-NoReparsePath([string]$Root, [string]$Path, [string]$Description) {
    $RootFull = Get-FullPath $Root
    $PathFull = Get-FullPath $Path
    if (-not $PathFull.Equals($RootFull, [StringComparison]::OrdinalIgnoreCase) -and
        -not (Test-PathWithin $RootFull $PathFull)) {
        throw "$Description escaped its allowed root."
    }
    $Relative = if ($PathFull.Equals($RootFull, [StringComparison]::OrdinalIgnoreCase)) {
        ''
    } else {
        $PathFull.Substring($RootFull.Length).TrimStart('\')
    }
    $Current = $RootFull
    if (Test-Path -LiteralPath $Current) {
        [void](Assert-NotReparsePoint $Current $Description)
    }
    foreach ($Part in @($Relative -split '\\' | Where-Object { $_.Length -gt 0 })) {
        $Current = Join-Path $Current $Part
        if (Test-Path -LiteralPath $Current) {
            [void](Assert-NotReparsePoint $Current $Description)
        }
    }
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

function ConvertFrom-CanonicalBase64([string]$Value, [int]$MaxBytes, [string]$Label) {
    if ($null -eq $Value -or
        $Value -cnotmatch '^[A-Za-z0-9+/]*={0,2}$' -or
        ($Value.Length % 4) -ne 0) {
        throw "$Label is not canonical base64."
    }
    if ($Value.Length -eq 0) { return ,([byte[]]@()) }
    try { $Bytes = [Convert]::FromBase64String($Value) }
    catch { throw "$Label is not valid base64." }
    if ($Bytes.Length -gt $MaxBytes -or
        [Convert]::ToBase64String($Bytes) -cne $Value) {
        [Array]::Clear($Bytes, 0, $Bytes.Length)
        throw "$Label exceeds its bound or is not canonical."
    }
    return ,$Bytes
}

function ConvertTo-Base64Chunks([byte[]]$Bytes, [int]$ChunkSize) {
    $Chunks = [Collections.Generic.List[string]]::new()
    for ($Offset = 0; $Offset -lt $Bytes.Length; $Offset += $ChunkSize) {
        $Length = [Math]::Min($ChunkSize, $Bytes.Length - $Offset)
        $Chunk = New-Object byte[] $Length
        try {
            [Array]::Copy($Bytes, $Offset, $Chunk, 0, $Length)
            $Encoded = [Convert]::ToBase64String($Chunk)
            if ($Encoded.Length -gt $script:ChunkChars) {
                throw 'A Guest Variables chunk exceeded its encoded bound.'
            }
            $Chunks.Add($Encoded)
        }
        finally { [Array]::Clear($Chunk, 0, $Chunk.Length) }
    }
    return ,$Chunks.ToArray()
}

function Write-JsonFile([string]$Path, $Value) {
    $Json = $Value | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText($Path, $Json + "`n", [Text.UTF8Encoding]::new($false))
}

function ConvertTo-SafeDiagnostic([string]$Text, [string[]]$Secrets = @()) {
    $Safe = [string]$Text
    foreach ($Secret in $Secrets) {
        if (-not [string]::IsNullOrEmpty($Secret)) {
            $Safe = $Safe.Replace($Secret, '<redacted>')
        }
    }
    # vmrun diagnostics are retained, but never persist a long guest-variable
    # value if a VMware CLI version includes it in an error message.
    return [regex]::Replace($Safe, '[A-Za-z0-9+/]{256,}={0,2}', '<base64-value-redacted>')
}

function ConvertTo-NativeArgument([string]$Value) {
    if ($null -eq $Value -or $Value.Length -eq 0) { return '""' }
    if ($Value -notmatch '[\s"]') { return $Value }
    $Escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $Escaped = [regex]::Replace($Escaped, '(\\+)$', '$1$1')
    return '"' + $Escaped + '"'
}

function Invoke-VmrunVariable(
    [ValidateSet('readVariable', 'writeVariable')][string]$Verb,
    [string]$VariableName,
    [AllowEmptyString()][string]$Value,
    [int]$TimeoutSeconds = 25
) {
    if ($VariableName -cnotmatch '^uefiloader\.(worker\.status|request\.manifest|response\.manifest|request\.chunk\.uefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.[0-9]{6}|response\.chunk\.uefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.[0-9]{6})$') {
        throw 'Guest Variables name is outside the exact probe mailbox namespace.'
    }
    if ($Verb -eq 'writeVariable' -and
        ($null -eq $Value -or $Value.Length -gt $script:ChunkChars -or
            $Value -cnotmatch '^[A-Za-z0-9+/=]*$')) {
        throw 'Guest Variables writes must be bounded base64 text.'
    }

    $Arguments = @('-T', 'ws')
    if (-not [string]::IsNullOrWhiteSpace($script:VmPassword)) {
        $Arguments += @('-vp', $script:VmPassword)
    }
    $Arguments += @($Verb, $script:VmxFullPath, 'guestVar', $VariableName)
    if ($Verb -eq 'writeVariable') { $Arguments += $Value }
    $SecretValues = @($script:VmPassword, $script:MailboxKeyBase64, $Value)
    try {
        $Result = Invoke-UhrProcess -FilePath $script:VmrunPath `
            -ArgumentList $Arguments -WorkingDirectory $script:MainRepoRoot `
            -TimeoutSeconds $TimeoutSeconds -Redactions $SecretValues
    }
    finally {
        $Arguments = $null
        $SecretValues = $null
    }

    if ($Result.TimedOut) {
        $Diagnostic = ConvertTo-SafeDiagnostic $Result.Stderr `
            @($script:VmPassword, $script:MailboxKeyBase64, $Value)
        $script:VmrunErrors.Add([pscustomobject]@{
            Utc = [DateTime]::UtcNow.ToString('o')
            Operation = $Verb
            Variable = $VariableName
            ExitCode = -1
            TimedOut = $true
            Stderr = $Diagnostic
        })
        throw "vmrun $Verb timed out for $VariableName."
    }
    if ($Verb -eq 'writeVariable' -and $Result.ExitCode -ne 0) {
        $Diagnostic = ConvertTo-SafeDiagnostic ($Result.Stderr + "`n" + $Result.Stdout) `
            @($script:VmPassword, $script:MailboxKeyBase64, $Value)
        $script:VmrunErrors.Add([pscustomobject]@{
            Utc = [DateTime]::UtcNow.ToString('o')
            Operation = $Verb
            Variable = $VariableName
            ExitCode = [int]$Result.ExitCode
            TimedOut = $false
            Stderr = $Diagnostic
        })
        throw "vmrun $Verb failed for $VariableName with exit code $($Result.ExitCode): $Diagnostic"
    }
    if ($Verb -eq 'readVariable') {
        $Output = ([string]$Result.Stdout).Trim()
        if ($Result.ExitCode -ne 0) {
            $Diagnostic = ConvertTo-SafeDiagnostic ($Result.Stderr + "`n" + $Output) `
                @($script:VmPassword, $script:MailboxKeyBase64)
            if ($Diagnostic -match '(?i)(no value|unknown property|not found|not set|does not exist|unable to read guest info variable)') {
                return $null
            }
            $script:VmrunErrors.Add([pscustomobject]@{
                Utc = [DateTime]::UtcNow.ToString('o')
                Operation = $Verb
                Variable = $VariableName
                ExitCode = [int]$Result.ExitCode
                TimedOut = $false
                Stderr = $Diagnostic
            })
            throw "vmrun $Verb failed for $VariableName with exit code $($Result.ExitCode): $Diagnostic"
        }
        if ([string]::IsNullOrWhiteSpace($Output)) { return $null }
        return $Output
    }
    return $true
}

function Get-GuestChunkName([string]$Direction, [string]$RunId, [int]$Index) {
    if ($Direction -notin @('request', 'response') -or
        $RunId -cnotmatch '^uefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$' -or
        $Index -lt 0 -or $Index -gt 4095) {
        throw 'Unsafe Guest Variables chunk identity.'
    }
    $Prefix = if ($Direction -eq 'request') {
        $script:RequestChunkPrefix
    } else {
        $script:ResponseChunkPrefix
    }
    return "$Prefix$RunId.$('{0:D6}' -f $Index)"
}

function Read-ManifestJson([string]$Encoded, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Encoded) -or
        $Encoded -ceq $script:Tombstone) { return $null }
    if ($Encoded.Length -gt $script:MaxManifestChars) {
        throw "$Label exceeds its encoded size bound."
    }
    $Bytes = ConvertFrom-CanonicalBase64 $Encoded $script:MaxManifestChars $Label
    try { return [Text.Encoding]::UTF8.GetString($Bytes) | ConvertFrom-Json }
    catch { throw "$Label is not valid UTF-8 JSON." }
    finally { [Array]::Clear($Bytes, 0, $Bytes.Length) }
}

function Assert-WorkerReady {
    $Encoded = Invoke-VmrunVariable -Verb readVariable `
        -VariableName $script:WorkerStatusName
    if ($null -eq $Encoded -or $Encoded -ceq $script:Tombstone) {
        throw 'The pre-existing Guest Variables worker has not published a status.'
    }
    $Status = Read-ManifestJson $Encoded 'Worker status'
    if ($null -eq $Status) {
        throw 'Guest Variables worker status is empty or a tombstone.'
    }
    $ExpectedProperties = @(
        'SchemaVersion', 'StartedUtc', 'State', 'UpdatedUtc',
        'WorkerHmacSha256', 'WorkerSid'
    ) | Sort-Object
    $ActualProperties = @($Status.PSObject.Properties.Name) | Sort-Object
    if (($ActualProperties -join "`n") -cne ($ExpectedProperties -join "`n") -or
        [int]$Status.SchemaVersion -ne 1 -or
        [string]$Status.State -cne 'READY' -or
        [string]$Status.WorkerSid -cne 'S-1-5-18' -or
        [string]$Status.WorkerHmacSha256 -cnotmatch '^[0-9A-F]{64}$') {
        throw 'Guest Variables worker status schema or identity is invalid.'
    }
    try {
        $Started = [DateTime]::Parse([string]$Status.StartedUtc,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
        $Updated = [DateTime]::Parse([string]$Status.UpdatedUtc,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    }
    catch { throw 'Guest Variables worker status timestamps are invalid.' }
    if ($Started -gt $Updated -or
        [Math]::Abs(([DateTime]::UtcNow - $Updated).TotalSeconds) -gt 15) {
        throw 'Guest Variables worker status is stale or from the future.'
    }
    $ExpectedHmac = Get-HmacSha256Hex -Key $script:MailboxKey `
        -Text (Get-CanonicalWorkerStatus $Status)
    if (-not (Test-FixedHexEqual $ExpectedHmac `
            ([string]$Status.WorkerHmacSha256))) {
        throw 'Guest Variables worker status authentication failed.'
    }
}

function Assert-MailboxIdle {
    foreach ($Name in @($script:RequestManifestName, $script:ResponseManifestName)) {
        $Value = Invoke-VmrunVariable -Verb readVariable -VariableName $Name
        if ($null -ne $Value -and $Value -cne $script:Tombstone) {
            throw "Guest Variables mailbox is not idle; refusing to overwrite $Name."
        }
    }
}

function Get-ResponseMessage($Response) {
    $MessageBytes = ConvertFrom-CanonicalBase64 `
        ([string]$Response.MessageBase64) 4096 'Response message'
    try {
        $Message = [Text.Encoding]::UTF8.GetString($MessageBytes)
        if ([Text.Encoding]::UTF8.GetByteCount($Message) -gt 4096) {
            throw 'Response message exceeds its bound.'
        }
        return $Message
    }
    finally { [Array]::Clear($MessageBytes, 0, $MessageBytes.Length) }
}

function Copy-VerifiedEvidenceArchive([byte[]]$Bytes, [string]$Destination) {
    if ($Bytes.Length -gt $script:MaxEvidenceBytes) {
        throw 'Guest evidence archive exceeds the 32 MiB limit.'
    }
    [IO.File]::WriteAllBytes($Destination, $Bytes)
    $EvidenceDirectory = Join-Path $script:RunDirectory 'evidence'
    [void](New-Item -ItemType Directory -Path $EvidenceDirectory)
    $Archive = [IO.Compression.ZipFile]::OpenRead($Destination)
    try {
        $Names = @($Archive.Entries | ForEach-Object { $_.FullName })
        if (@($Names | Where-Object { $_ -cnotin $script:AllowedEvidence }).Count -gt 0 -or
            @($Names | Sort-Object -Unique).Count -ne $Names.Count) {
            throw 'Guest evidence ZIP contains an unallowlisted or duplicate file.'
        }
        $ExpandedBytes = [int64]0
        foreach ($Entry in $Archive.Entries) {
            if ([string]::IsNullOrWhiteSpace($Entry.FullName) -or
                $Entry.FullName.Contains('/') -or
                $Entry.FullName.Contains('\\') -or
                [IO.Path]::IsPathRooted($Entry.FullName) -or
                $Entry.FullName.Contains(':') -or
                $Entry.Length -lt 0 -or
                $Entry.Length -gt $script:MaxEvidenceFileBytes) {
                throw "Guest evidence entry is unsafe or too large: $($Entry.FullName)"
            }
            $ExpandedBytes += [int64]$Entry.Length
            if ($ExpandedBytes -gt $script:MaxEvidenceBytes) {
                throw 'Expanded guest evidence exceeds the 32 MiB limit.'
            }
            $Target = Join-Path $EvidenceDirectory $Entry.FullName
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
}

function Get-ResponseEvidence($Response, [DateTime]$Deadline) {
    $Size = [int64]$Response.EvidenceSize
    $ChunkSize = [int]$Response.EvidenceChunkSize
    $ChunkCount = [int]$Response.EvidenceChunkCount
    if ($Size -lt 0 -or $Size -gt $script:MaxEvidenceBytes -or
        $ChunkCount -lt 0 -or $ChunkCount -gt 4096 -or
        ($Size -eq 0 -and $ChunkCount -ne 0) -or
        ($Size -gt 0 -and ($ChunkSize -lt 1 -or
            $ChunkSize -gt $script:ChunkBytes))) {
        throw 'Guest response evidence bounds are invalid.'
    }
    $ExpectedCount = if ($Size -eq 0) { 0 } else {
        [int][Math]::Ceiling($Size / [double]$ChunkSize)
    }
    if ($ExpectedCount -ne $ChunkCount) {
        throw 'Guest response evidence chunk count is inconsistent.'
    }
    if ($ChunkCount -eq 0) {
        if ([string]$Response.EvidenceSha256 -cne ('0' * 64)) {
            throw 'Empty guest evidence has an unexpected SHA256.'
        }
        return ,([byte[]]@())
    }
    $Output = [IO.MemoryStream]::new()
    try {
        for ($Index = 0; $Index -lt $ChunkCount; $Index++) {
            if ([DateTime]::UtcNow -ge $Deadline) {
                throw 'Timed out reading Guest Variables response chunks.'
            }
            $Name = Get-GuestChunkName 'response' $script:RunId $Index
            $script:OwnedResponseNames.Add($Name)
            $Chunk = Invoke-VmrunVariable -Verb readVariable -VariableName $Name
            if ($null -eq $Chunk -or $Chunk.Length -gt $script:ChunkChars) {
                throw "Guest response chunk is missing or oversized: $Index"
            }
            $Decoded = ConvertFrom-CanonicalBase64 $Chunk $ChunkSize `
                "Guest response chunk $Index"
            try {
                $ExpectedLength = [int][Math]::Min(
                    $ChunkSize, $Size - ([int64]$Index * $ChunkSize))
                if ($Decoded.Length -ne $ExpectedLength) {
                    throw "Guest response chunk has the wrong size: $Index"
                }
                $Output.Write($Decoded, 0, $Decoded.Length)
            }
            finally { [Array]::Clear($Decoded, 0, $Decoded.Length) }
        }
        if ($Output.Length -ne $Size) {
            throw 'Guest response evidence size differs from its manifest.'
        }
        $Bytes = $Output.ToArray()
        $Sha = [Security.Cryptography.SHA256]::Create()
        try {
            $ActualHash = ([BitConverter]::ToString($Sha.ComputeHash($Bytes))).Replace('-', '')
        }
        finally { $Sha.Dispose() }
        if ($ActualHash -cne [string]$Response.EvidenceSha256) {
            [Array]::Clear($Bytes, 0, $Bytes.Length)
            throw 'Guest response evidence SHA256 mismatch.'
        }
        return ,$Bytes
    }
    finally { $Output.Dispose() }
}

function Assert-ExactResponseSchema($Response) {
    $Expected = @(
        'CompletedUtc', 'EvidenceChunkCount', 'EvidenceChunkSize',
        'EvidenceSha256', 'EvidenceSize', 'MessageBase64',
        'ResponseHmacSha256', 'RunId', 'SchemaVersion', 'Status'
    ) | Sort-Object
    $Actual = @($Response.PSObject.Properties.Name) | Sort-Object
    if (($Actual -join "`n") -cne ($Expected -join "`n")) {
        throw 'Guest response manifest does not match the exact schema.'
    }
    if ([int]$Response.SchemaVersion -ne 1 -or
        [string]$Response.RunId -cne $script:RunId -or
        [string]$Response.Status -notin @('PASS', 'FAIL') -or
        [string]$Response.ResponseHmacSha256 -cnotmatch '^[0-9A-F]{64}$' -or
        [string]$Response.EvidenceSha256 -cnotmatch '^[0-9A-F]{64}$') {
        throw 'Guest response identity or schema is invalid.'
    }
    [DateTime]::Parse([string]$Response.CompletedUtc,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind) | Out-Null
    $ExpectedHmac = Get-HmacSha256Hex -Key $script:MailboxKey `
        -Text (Get-CanonicalResponse $Response)
    if (-not (Test-FixedHexEqual $ExpectedHmac `
            ([string]$Response.ResponseHmacSha256))) {
        throw 'Guest response authentication failed.'
    }
}

function Publish-Request([string]$PackagePath, [string]$PackageHash,
    [byte[]]$PackageBytes, [string]$ExpectedVerdict, [int]$TimeoutSeconds,
    [DateTime]$Deadline) {
    $Chunks = ConvertTo-Base64Chunks -Bytes $PackageBytes -ChunkSize $script:ChunkBytes
    if ($Chunks.Count -lt 1 -or $Chunks.Count -gt 4096) {
        throw 'Request package chunk count is outside the 1..4096 bound.'
    }
    $Request = [ordered]@{
        SchemaVersion = 1
        RunId = $script:RunId
        Action = 'Probe'
        ExpectedProbeVerdict = $ExpectedVerdict
        PackageSha256 = $PackageHash
        PackageSize = [long]$PackageBytes.Length
        PackageChunkSize = [int]$script:ChunkBytes
        PackageChunkCount = [int]$Chunks.Count
        TimeoutSeconds = [int]$TimeoutSeconds
        SubmittedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $Request.RequestHmacSha256 = Get-HmacSha256Hex -Key $script:MailboxKey `
        -Text (Get-CanonicalRequest $Request)
    $ManifestBytes = [Text.Encoding]::UTF8.GetBytes(
        ($Request | ConvertTo-Json -Compress -Depth 5))
    try {
        $ManifestValue = [Convert]::ToBase64String($ManifestBytes)
        if ($ManifestValue.Length -gt $script:MaxManifestChars) {
            throw 'Request manifest exceeds its encoded size bound.'
        }
        Write-JsonFile (Join-Path $script:RunDirectory 'request.json') $Request
        Write-JsonFile (Join-Path $script:RunDirectory 'request-metadata.json') `
            ([ordered]@{
                RunId = $script:RunId
                Action = 'Probe'
                ExpectedProbeVerdict = $ExpectedVerdict
                PackageFile = 'request-package.zip'
                PackageSha256 = $PackageHash
                PackageSize = [long]$PackageBytes.Length
                PackageChunkSize = $script:ChunkBytes
                PackageChunkCount = $Chunks.Count
                TimeoutSeconds = $TimeoutSeconds
                SubmittedUtc = $Request.SubmittedUtc
                RequestHmacSha256 = $Request.RequestHmacSha256
                RequestManifestVariable = $script:RequestManifestName
                RequestChunkVariableNames = @(
                    for ($ChunkIndex = 0; $ChunkIndex -lt $Chunks.Count; $ChunkIndex++) {
                        Get-GuestChunkName 'request' $script:RunId $ChunkIndex
                    }
                )
            })
        for ($Index = 0; $Index -lt $Chunks.Count; $Index++) {
            if ([DateTime]::UtcNow -ge $Deadline) {
                throw 'Timed out publishing request package chunks.'
            }
            $Name = Get-GuestChunkName 'request' $script:RunId $Index
            $script:OwnedRequestNames.Add($Name)
            [void](Invoke-VmrunVariable -Verb writeVariable `
                -VariableName $Name -Value $Chunks[$Index])
        }
        # The signed manifest is the request commit marker and is written last.
        [void](Invoke-VmrunVariable -Verb writeVariable `
            -VariableName $script:RequestManifestName -Value $ManifestValue)
        $script:OwnedRequestNames.Add($script:RequestManifestName)
    }
    finally { [Array]::Clear($ManifestBytes, 0, $ManifestBytes.Length) }
}

function Receive-Response([DateTime]$Deadline) {
    $PickupDeadline = [DateTime]::UtcNow.AddSeconds(
        [Math]::Min(300, [Math]::Max(30, $WaitTimeoutSeconds)))
    if ($PickupDeadline -gt $Deadline) { $PickupDeadline = $Deadline }
    while ([DateTime]::UtcNow -lt $PickupDeadline) {
        $Encoded = Invoke-VmrunVariable -Verb readVariable `
            -VariableName $script:ResponseManifestName
        if ($null -eq $Encoded -or $Encoded -ceq $script:Tombstone) {
            Start-Sleep -Milliseconds 500
            continue
        }
        $Response = Read-ManifestJson $Encoded 'Guest response manifest'
        if ($null -eq $Response) {
            Start-Sleep -Milliseconds 500
            continue
        }
        Write-JsonFile (Join-Path $script:RunDirectory 'response.json') $Response
        Assert-ExactResponseSchema $Response
        $Message = Get-ResponseMessage $Response
        $EvidenceBytes = Get-ResponseEvidence -Response $Response `
            -Deadline $Deadline
        try {
            $EvidencePath = $null
            if ($EvidenceBytes.Length -gt 0) {
                $EvidencePath = Join-Path $script:RunDirectory 'guest-evidence.zip'
                Copy-VerifiedEvidenceArchive -Bytes $EvidenceBytes `
                    -Destination $EvidencePath
            }
            $Metadata = [ordered]@{
                SchemaVersion = [int]$Response.SchemaVersion
                RunId = [string]$Response.RunId
                Status = [string]$Response.Status
                Message = $Message
                CompletedUtc = [string]$Response.CompletedUtc
                EvidenceSha256 = [string]$Response.EvidenceSha256
                EvidenceSize = [long]$Response.EvidenceSize
                EvidenceChunkSize = [int]$Response.EvidenceChunkSize
                EvidenceChunkCount = [int]$Response.EvidenceChunkCount
                ResponseHmacSha256 = [string]$Response.ResponseHmacSha256
                EvidenceArchive = if ($EvidencePath) { 'guest-evidence.zip' } else { $null }
                ResponseManifestVariable = $script:ResponseManifestName
            }
            Write-JsonFile (Join-Path $script:RunDirectory 'response-metadata.json') $Metadata
            $script:ResponseManifestOwned = $true
            if ([string]$Response.Status -cne 'PASS') {
                throw "Guest worker returned FAIL: $Message"
            }
            return $Metadata
        }
        finally {
            if ($null -ne $EvidenceBytes -and $EvidenceBytes.Length -gt 0) {
                [Array]::Clear($EvidenceBytes, 0, $EvidenceBytes.Length)
            }
        }
    }
    throw 'Timed out waiting for the authenticated Guest Variables response.'
}

function Get-CurrentRunIdFromManifest([string]$Encoded) {
    try {
        $Manifest = Read-ManifestJson $Encoded 'Mailbox cleanup manifest'
        if ($null -ne $Manifest -and [string]$Manifest.RunId -ceq $script:RunId) {
            return $true
        }
    }
    catch { }
    return $false
}

function Clear-OwnedGuestVariables {
    $NamesToClear = [Collections.Generic.List[string]]::new()
    foreach ($Name in @($script:OwnedRequestNames | Select-Object -Unique)) {
        if ($Name -ceq $script:RequestManifestName) {
            $Current = $null
            try {
                $Current = Invoke-VmrunVariable -Verb readVariable `
                    -VariableName $script:RequestManifestName
            }
            catch { continue }
            if (-not (Get-CurrentRunIdFromManifest $Current)) { continue }
        }
        $NamesToClear.Add($Name)
    }
    foreach ($Name in @($script:OwnedResponseNames | Select-Object -Unique)) {
        $NamesToClear.Add($Name)
    }
    if ($script:ResponseManifestOwned) {
        $Current = $null
        try {
            $Current = Invoke-VmrunVariable -Verb readVariable `
                -VariableName $script:ResponseManifestName
        }
        catch { $Current = $null }
        if (Get-CurrentRunIdFromManifest $Current) {
            $NamesToClear.Add($script:ResponseManifestName)
        }
    }
    foreach ($Name in @($NamesToClear | Select-Object -Unique)) {
        try {
            [void](Invoke-VmrunVariable -Verb writeVariable `
                -VariableName $Name -Value $script:Tombstone)
            $script:CleanupAttempts.Add($Name)
        }
        catch {
            $script:VmrunErrors.Add([pscustomobject]@{
                Utc = [DateTime]::UtcNow.ToString('o')
                Operation = 'cleanup'
                Variable = $Name
                ExitCode = $null
                TimedOut = $false
                Stderr = ConvertTo-SafeDiagnostic $_.Exception.Message `
                    @($script:VmPassword, $script:MailboxKeyBase64)
            })
        }
    }
}

function New-RequestPackage([string]$SourceGuestScript, [string]$SourceCompanion) {
    $StageDirectory = Join-Path $script:RunDirectory 'stage'
    [void](New-Item -ItemType Directory -Path $StageDirectory)
    $GuestTarget = Join-Path $StageDirectory 'Invoke-WindowsGuestTest.ps1'
    $CompanionTarget = Join-Path $StageDirectory 'ib-tcg-dump.exe'
    Copy-Item -LiteralPath $SourceGuestScript -Destination $GuestTarget
    Copy-Item -LiteralPath $SourceCompanion -Destination $CompanionTarget
    $GuestHash = (Get-FileHash -LiteralPath $GuestTarget -Algorithm SHA256).Hash
    $CompanionHash = (Get-FileHash -LiteralPath $CompanionTarget -Algorithm SHA256).Hash
    $SumsPath = Join-Path $StageDirectory 'SHA256SUMS.txt'
    $Sums = @(
        "$CompanionHash  ib-tcg-dump.exe"
        "$GuestHash  Invoke-WindowsGuestTest.ps1"
    ) -join "`n"
    [IO.File]::WriteAllText($SumsPath, $Sums + "`n", [Text.UTF8Encoding]::new($false))
    $PackagePath = Join-Path $script:RunDirectory 'request-package.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory(
        $StageDirectory, $PackagePath,
        [IO.Compression.CompressionLevel]::Optimal, $false)
    $PackageItem = Get-Item -LiteralPath $PackagePath
    if ($PackageItem.Length -le 0 -or $PackageItem.Length -gt $script:MaxPackageBytes) {
        throw 'Request ZIP must be greater than zero and at most 4 MiB.'
    }
    $Archive = [IO.Compression.ZipFile]::OpenRead($PackagePath)
    try {
        $Names = @($Archive.Entries | ForEach-Object { $_.FullName })
        $Expected = @('ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1', 'SHA256SUMS.txt')
        if (($Names | Sort-Object) -join "`n" -cne (($Expected | Sort-Object) -join "`n") -or
            @($Names | Sort-Object -Unique).Count -ne 3) {
            throw 'Request ZIP does not contain exactly the three required root entries.'
        }
        foreach ($Entry in $Archive.Entries) {
            if ($Entry.Length -lt 0 -or $Entry.Length -gt $script:MaxPackageBytes) {
                throw "Request ZIP entry exceeds its bound: $($Entry.FullName)"
            }
        }
    }
    finally { $Archive.Dispose() }
    return [pscustomobject]@{
        Path = $PackagePath
        Hash = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash
        Size = [long]$PackageItem.Length
        GuestScriptSha256 = $GuestHash
        CompanionSha256 = $CompanionHash
    }
}

function Assert-InputFiles {
    $VmxFull = Get-FullPath $VmxPath
    if (-not (Test-Path -LiteralPath $VmxFull -PathType Leaf)) {
        throw 'The explicit clone VMX does not exist.'
    }
    $VmxDirectory = Split-Path -Parent $VmxFull
    $ExpectedVmParent = Split-Path -Parent $VmxDirectory
    $VmDirectoryName = Split-Path -Leaf $VmxDirectory
    if (-not (Test-PathWithin $script:ExpectedVmxRoot $VmxFull) -or
        -not $ExpectedVmParent.Equals((Get-FullPath $script:ExpectedVmxRoot),
            [StringComparison]::OrdinalIgnoreCase) -or
        $VmDirectoryName -cnotmatch '^vm-[A-Za-z0-9._-]{1,80}$' -or
        [IO.Path]::GetExtension($VmxFull) -cne '.vmx') {
        throw 'VMX must be a direct child of a vm-* directory under the designated clone root.'
    }
    Assert-NoReparsePath -Root $script:ExpectedVmxRoot `
        -Path $VmxFull -Description 'Clone VMX path'
    $script:VmxFullPath = $VmxFull

    $GuestFull = Get-FullPath $GuestScriptPath
    $CompanionFull = Get-FullPath $CompanionExePath
    foreach ($InputFile in @(
        [pscustomobject]@{ Path = $GuestFull; Name = 'Guest script' },
        [pscustomobject]@{ Path = $CompanionFull; Name = 'Companion executable' }
    )) {
        if (-not (Test-Path -LiteralPath $InputFile.Path -PathType Leaf)) {
            throw "$($InputFile.Name) does not exist."
        }
        if (-not (Test-PathWithin $script:MainRepoRoot $InputFile.Path)) {
            throw "$($InputFile.Name) must be inside the main repository."
        }
        Assert-NoReparsePath -Root $script:MainRepoRoot `
            -Path $InputFile.Path -Description $InputFile.Name
    }
    if ([IO.Path]::GetFileName($CompanionFull) -cne 'ib-tcg-dump.exe') {
        throw 'CompanionExePath must identify the supplied ib-tcg-dump.exe file.'
    }
    $script:GuestScriptFullPath = $GuestFull
    $script:CompanionFullPath = $CompanionFull

    $OutputFull = Get-FullPath $OutputDirectory
    if (-not $OutputFull.Equals((Get-FullPath $script:ExpectedOutputDirectory),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw ('OutputDirectory must be exactly the designated ' + $TestProfile + ' VM report directory in the main checkout.')
    }
    $OutputParent = Split-Path -Parent $OutputFull
    if (-not (Test-Path -LiteralPath $OutputParent -PathType Container)) {
        throw 'The designated main-repository report parent is missing.'
    }
    Assert-NoReparsePath -Root $script:MainRepoRoot `
        -Path $OutputParent -Description 'Report output parent'
    if (-not (Test-Path -LiteralPath $OutputFull -PathType Container)) {
        [void](New-Item -ItemType Directory -Path $OutputFull)
    }
    Assert-NoReparsePath -Root $script:MainRepoRoot `
        -Path $OutputFull -Description 'Report output directory'
    $script:OutputFullPath = $OutputFull
}

function Initialize-ConfigurationAndCredentials {
    if (-not (Test-PathWithin $script:MainRepoRoot $ConfigPath)) {
        throw 'ConfigPath must remain inside the main repository.'
    }
    $CorePath = Join-Path $script:MainRepoRoot 'tools\host-runner\HostRunner.Core.psm1'
    Import-Module $CorePath -Force
    $script:Config = Import-UhrConfig -ConfigPath $ConfigPath
    if (-not (Get-FullPath ([string]$script:Config.ProjectRoot)).Equals(
            (Get-FullPath $script:MainRepoRoot),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Host-runner configuration ProjectRoot does not match the main checkout.'
    }
    $script:VmrunPath = [string]$script:Config.Tools.VmrunPath
    if (-not (Test-Path -LiteralPath $script:VmrunPath -PathType Leaf)) {
        throw 'Configured VMware vmrun executable is missing.'
    }

    $script:VmCredential = Get-UhrCredential -Config $script:Config `
        -FileName 'windows-vm.xml'
    $script:VmPassword = $script:VmCredential.GetNetworkCredential().Password
    $script:MailboxKeyCredential = Get-UhrCredential -Config $script:Config `
        -FileName 'windows-clone-mailbox-key.xml'
    try {
        $script:MailboxKey = [Convert]::FromBase64String(
            $script:MailboxKeyCredential.GetNetworkCredential().Password)
    }
    catch { throw 'Stored Windows clone mailbox key is malformed.' }
    if ($script:MailboxKey.Length -ne 32) {
        [Array]::Clear($script:MailboxKey, 0, $script:MailboxKey.Length)
        throw 'Stored Windows clone mailbox key must contain exactly 32 bytes.'
    }
    $script:MailboxKeyBase64 = [Convert]::ToBase64String($script:MailboxKey)
    if ([string]::IsNullOrWhiteSpace($ExpectedProbeVerdict)) {
        $ExpectedProbeVerdict = [string]$script:Config.Profiles.Windows.ExpectedProbeVerdict
    }
    if ($ExpectedProbeVerdict -notin @('MET', 'NOT_MET')) {
        throw 'ExpectedProbeVerdict must be MET or NOT_MET.'
    }
    $script:ExpectedVerdict = $ExpectedProbeVerdict
}

function Write-FinalEvidence([string]$FailureMessage) {
    if ($null -eq $script:RunDirectory -or
        -not (Test-Path -LiteralPath $script:RunDirectory -PathType Container)) {
        return
    }
    if ($script:VmrunErrors.Count -gt 0) {
        Write-JsonFile (Join-Path $script:RunDirectory 'vmrun-errors.json') `
            @($script:VmrunErrors)
    }
    Write-JsonFile (Join-Path $script:RunDirectory 'cleanup.json') `
        ([ordered]@{
            RecordedUtc = [DateTime]::UtcNow.ToString('o')
            ClearedWithTombstone = @($script:CleanupAttempts)
            Errors = @($script:VmrunErrors | Where-Object {
                $_.Operation -ceq 'cleanup'
            })
        })
    if (-not [string]::IsNullOrWhiteSpace($FailureMessage)) {
        Write-JsonFile (Join-Path $script:RunDirectory 'error.json') `
            ([ordered]@{
                RunId = $script:RunId
                RecordedUtc = [DateTime]::UtcNow.ToString('o')
                Error = ConvertTo-SafeDiagnostic $FailureMessage `
                    @($script:VmPassword, $script:MailboxKeyBase64)
                CleanupErrors = @($script:VmrunErrors | Where-Object {
                    $_.Operation -ceq 'cleanup'
                })
            })
    }
}

$FailureMessage = $null
try {
    Assert-InputFiles
    $Stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $Suffix = [Guid]::NewGuid().ToString('N').Substring(0, 10)
    $script:RunId = "uefi-windows-probe-$Stamp-$Suffix"
    if ($ResumeRunId) {
        $PreviousRequest = Join-Path (Join-Path $script:OutputFullPath $ResumeRunId) 'request.json'
        if (-not (Test-Path -LiteralPath $PreviousRequest -PathType Leaf)) {
            throw 'Resume requires the original locally recorded request.'
        }
        $RecordedRequest = Get-Content -LiteralPath $PreviousRequest -Raw | ConvertFrom-Json
        if ([string]$RecordedRequest.RunId -cne $ResumeRunId) { throw 'Recorded request identity differs from ResumeRunId.' }
        $script:RunId = $ResumeRunId
    }
    if ($script:RunId -cnotmatch '^uefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw 'Generated probe RunId did not match the worker contract.'
    }
    $script:RunDirectory = Join-Path $script:OutputFullPath $script:RunId
    if ($ResumeRunId) { $script:RunDirectory += '.resume-' + $Stamp + '-' + $Suffix }
    $script:StartedUtc = [DateTime]::UtcNow.ToString('o')
    [void](New-Item -ItemType Directory -Path $script:RunDirectory)
    Assert-NoReparsePath -Root $script:ExpectedOutputDirectory `
        -Path $script:RunDirectory -Description 'Run evidence directory'
    Initialize-ConfigurationAndCredentials
    if ($ResumeRunId) {
        $ResponseMetadata = Receive-Response -Deadline ([DateTime]::UtcNow.AddSeconds($WaitTimeoutSeconds))
        Write-JsonFile (Join-Path $script:RunDirectory 'transport-resume.json') ([ordered]@{runId=$ResumeRunId;vmxPath=$script:VmxFullPath;status=$ResponseMetadata.Status;newRequestPublished=$false})
        Write-Output ('Authenticated response recovered: ' + $script:RunDirectory)
        return
    }
    Assert-MailboxIdle
    Assert-WorkerReady

    $Package = New-RequestPackage -SourceGuestScript $script:GuestScriptFullPath `
        -SourceCompanion $script:CompanionFullPath
    $PackageBytes = [IO.File]::ReadAllBytes($Package.Path)
    try {
        $Deadline = [DateTime]::UtcNow.AddSeconds($WaitTimeoutSeconds)
        $RequestTimeout = [Math]::Min($GuestTimeoutSeconds,
            [Math]::Max(30, $WaitTimeoutSeconds - 10))
        [void](Invoke-VmrunVariable -Verb writeVariable `
            -VariableName $script:ResponseManifestName -Value $script:Tombstone)
        $script:ResponseManifestOwned = $true
        Publish-Request -PackagePath $Package.Path -PackageHash $Package.Hash `
            -PackageBytes $PackageBytes -ExpectedVerdict $script:ExpectedVerdict `
            -TimeoutSeconds $RequestTimeout -Deadline $Deadline
        $ResponseMetadata = Receive-Response -Deadline $Deadline
        Write-JsonFile (Join-Path $script:RunDirectory 'transport-metadata.json') `
            ([ordered]@{
                RunId = $script:RunId
                Action = 'Probe'
                VmxPath = $script:VmxFullPath
                GuestScriptSha256 = $Package.GuestScriptSha256
                CompanionExeSha256 = $Package.CompanionSha256
                PackageSha256 = $Package.Hash
                PackageSize = $Package.Size
                ExpectedProbeVerdict = $script:ExpectedVerdict
                StartedUtc = $script:StartedUtc
                ResultStatus = $ResponseMetadata.Status
                CompletedUtc = $ResponseMetadata.CompletedUtc
                OutputDirectory = $script:RunDirectory
            })
        Write-Output "PASS: authenticated Guest Variables probe $($script:RunId) completed with status $($ResponseMetadata.Status)."
        Write-Output "Evidence: $script:RunDirectory"
    }
    finally {
        if ($null -ne $PackageBytes -and $PackageBytes.Length -gt 0) {
            [Array]::Clear($PackageBytes, 0, $PackageBytes.Length)
        }
    }
}
catch {
    $FailureMessage = $_.Exception.Message
    throw
}
finally {
    try { Clear-OwnedGuestVariables }
    catch {
        $script:VmrunErrors.Add([pscustomobject]@{
            Utc = [DateTime]::UtcNow.ToString('o')
            Operation = 'cleanup'
            Variable = '<owned variables>'
            ExitCode = $null
            TimedOut = $false
            Stderr = ConvertTo-SafeDiagnostic $_.Exception.Message `
                @($script:VmPassword, $script:MailboxKeyBase64)
        })
    }
    Write-FinalEvidence -FailureMessage $FailureMessage
    if ($null -ne $script:MailboxKey -and $script:MailboxKey.Length -gt 0) {
        [Array]::Clear($script:MailboxKey, 0, $script:MailboxKey.Length)
    }
    if ($null -ne $script:VmCredential -and
        $null -ne $script:VmCredential.Password) {
        $script:VmCredential.Password.Dispose()
    }
    if ($null -ne $script:MailboxKeyCredential -and
        $null -ne $script:MailboxKeyCredential.Password) {
        $script:MailboxKeyCredential.Password.Dispose()
    }
    $script:VmPassword = $null
    $script:MailboxKeyBase64 = $null
    $script:VmCredential = $null
    $script:MailboxKeyCredential = $null
}
