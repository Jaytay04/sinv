[CmdletBinding()]
param(
    [string]$InputManifestPath,
    [string]$RepositoryRoot,
    [string]$WorktreeRoot,
    [string]$OutputDirectory,
    [switch]$LibraryOnly
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue
$script:CombinedAssemblyLibraryOnly = [bool]$LibraryOnly
$script:CombinedAssemblyUnitTestMailboxKey = $null

function Get-CombinedAssemblySha256 {
    param([Parameter(Mandatory=$true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Get-CombinedAssemblyValue {
    param([Parameter(Mandatory=$true)][object]$Object,[Parameter(Mandatory=$true)][string]$Name)
    if ($null -eq $Object) { return $null }
    if ($Object -is [Collections.IDictionary]) {
        if ($Object.Contains($Name)) { return $Object[$Name] }
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -ne $property) { return $property.Value }
    return $null
}

function Assert-CombinedAssemblyNoReparsePath {
    param([Parameter(Mandatory=$true)][string]$Root,[Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)][string]$Label)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    if (-not (Test-Path -LiteralPath $rootFull -PathType Container)) { throw "$Label authorized evidence root is missing: $rootFull" }
    $rootItem = Get-Item -LiteralPath $rootFull -Force -ErrorAction Stop
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "$Label authorized evidence root is a reparse point: $rootFull" }
    $pathFull = [IO.Path]::GetFullPath($Path)
    if (-not $pathFull.StartsWith(($rootFull + '\'),[StringComparison]::OrdinalIgnoreCase)) { throw "$Label must remain below ${rootFull}: $pathFull" }
    $current = $pathFull
    while ($current.StartsWith(($rootFull + '\'),[StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "$Label contains a reparse point: $current" }
        }
        if ((Split-Path -Parent $current) -ceq $rootFull) { break }
        $current = Split-Path -Parent $current
    }
    return $pathFull
}

function Get-CombinedAssemblyReferenceFile {
    param(
        [Parameter(Mandatory=$true)][object]$Reference,
        [Parameter(Mandatory=$true)][string]$Label,
        [Parameter(Mandatory=$true)][string[]]$AllowedRoots
    )
    $pathText = [string](Get-CombinedAssemblyValue $Reference 'path')
    $expectedHash = [string](Get-CombinedAssemblyValue $Reference 'sha256')
    if ([string]::IsNullOrWhiteSpace($pathText) -or -not [IO.Path]::IsPathRooted($pathText) -or $expectedHash -notmatch '\A[A-Fa-f0-9]{64}\z') { throw "$Label must contain an absolute path and SHA-256." }
    $path = [IO.Path]::GetFullPath($pathText)
    $matchedRoot = @($AllowedRoots | ForEach-Object { [IO.Path]::GetFullPath($_).TrimEnd('\') } | Where-Object { $path.StartsWith(($_ + '\'),[StringComparison]::OrdinalIgnoreCase) })
    if ($matchedRoot.Count -ne 1) { throw "$Label path is outside its single authorized evidence root: $path" }
    $null = Assert-CombinedAssemblyNoReparsePath -Root $matchedRoot[0] -Path $path -Label $Label
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "$Label file does not exist: $path" }
    $actualHash = Get-CombinedAssemblySha256 $path
    if ($actualHash -cne $expectedHash.ToUpperInvariant()) { throw "$Label SHA-256 mismatch: expected=$expectedHash actual=$actualHash path=$path" }
    return [pscustomobject][ordered]@{path=$path;sha256=$actualHash;bytes=[long](Get-Item -LiteralPath $path).Length}
}

function Assert-CombinedAssemblyAuthenticationSourcePins {
    param([Parameter(Mandatory=$true)][object[]]$SourcePins,[Parameter(Mandatory=$true)][string]$RepositoryRoot,[Parameter(Mandatory=$true)][string]$WorktreeRoot)
    $required = @(
        [pscustomobject]@{name='mailbox-protocol';path=(Join-Path $WorktreeRoot 'spoof-handoff\tools\device_vm\invoke-mailbox.ps1')},
        [pscustomobject]@{name='dpapi-credential-resolver';path=(Join-Path $RepositoryRoot 'tools\host-runner\HostRunner.Core.psm1')}
    )
    $result = [Collections.Generic.List[object]]::new()
    foreach ($expected in $required) {
        $expectedPath = [IO.Path]::GetFullPath($expected.path)
        $matches = @($SourcePins | Where-Object {
            $pinPath = [string](Get-CombinedAssemblyValue $_ 'path')
            [IO.Path]::IsPathRooted($pinPath) -and [IO.Path]::GetFullPath($pinPath).Equals($expectedPath,[StringComparison]::OrdinalIgnoreCase)
        })
        if ($matches.Count -ne 1) { throw "VM gate sourcePins must bind exactly one $($expected.name) source at $expectedPath; actual=$($matches.Count)." }
        $pinHash = [string](Get-CombinedAssemblyValue $matches[0] 'sha256')
        if ($pinHash -notmatch '\A[A-Fa-f0-9]{64}\z') { throw "VM gate $($expected.name) source pin has no valid SHA-256." }
        if (-not (Test-Path -LiteralPath $expectedPath -PathType Leaf)) { throw "Pinned $($expected.name) source is missing: $expectedPath" }
        $actualHash = Get-CombinedAssemblySha256 $expectedPath
        if ($pinHash.ToUpperInvariant() -cne $actualHash) { throw "Pinned $($expected.name) source changed: expected=$pinHash actual=$actualHash path=$expectedPath" }
        $result.Add([pscustomobject][ordered]@{name=$expected.name;path=$expectedPath;sha256=$actualHash})
    }
    return @($result.ToArray())
}

function Copy-CombinedAssemblyFile {
    param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$DestinationPath,[Parameter(Mandatory=$true)][string]$ExpectedSha256)
    $sourceBytes = [IO.File]::ReadAllBytes($SourcePath)
    if ((Get-CombinedAssemblySha256 $SourcePath) -cne $ExpectedSha256.ToUpperInvariant()) { throw "Source changed while preparing gate evidence: $SourcePath" }
    $parent = Split-Path -Parent $DestinationPath
    [void][IO.Directory]::CreateDirectory($parent)
    if (Test-Path -LiteralPath $DestinationPath) { throw "Refusing to overwrite assembled evidence: $DestinationPath" }
    [IO.File]::WriteAllBytes($DestinationPath,$sourceBytes)
    $actualHash = Get-CombinedAssemblySha256 $DestinationPath
    if ($actualHash -cne $ExpectedSha256.ToUpperInvariant() -or (Get-Item -LiteralPath $DestinationPath).Length -ne $sourceBytes.Length) { throw "Copied evidence does not match source bytes: $DestinationPath" }
    return [pscustomobject][ordered]@{path=$DestinationPath;sha256=$actualHash;bytes=[long]$sourceBytes.Length}
}

function Read-CombinedAssemblyZipEntry {
    param([Parameter(Mandatory=$true)][IO.Compression.ZipArchiveEntry]$Entry,[Parameter(Mandatory=$true)][long]$MaximumBytes,[Parameter(Mandatory=$true)][string]$Label)
    if ($Entry.Length -lt 0 -or $Entry.Length -gt $MaximumBytes) { throw "$Label exceeds its size limit: $($Entry.Length)" }
    $input = $Entry.Open()
    $memory = [IO.MemoryStream]::new()
    try {
        $input.CopyTo($memory)
        if ($memory.Length -ne $Entry.Length) { throw "$Label expanded length differs from its ZIP entry." }
        return ,([byte[]]$memory.ToArray())
    } finally { $memory.Dispose(); $input.Dispose() }
}

function ConvertFrom-CombinedAssemblyJsonBytes {
    param([Parameter(Mandatory=$true)][byte[]]$Bytes,[Parameter(Mandatory=$true)][string]$Label)
    try {
        $text = [Text.UTF8Encoding]::new($false,$true).GetString($Bytes)
        return $text | ConvertFrom-Json -ErrorAction Stop
    } catch { throw "$Label is not valid UTF-8 JSON: $($_.Exception.Message)" }
}

function Get-CombinedAssemblyCanonicalJson {
    param([Parameter(Mandatory=$true)][AllowNull()][AllowEmptyCollection()][object]$Value)
    if ($null -eq $Value) { return 'null' }
    if ($Value -is [string]) { return ConvertTo-Json -InputObject $Value -Compress }
    if ($Value -is [bool] -or $Value -is [ValueType]) { return ConvertTo-Json -InputObject $Value -Compress }
    if ($Value -is [Collections.IDictionary]) {
        $parts = [Collections.Generic.List[string]]::new()
        foreach ($key in @($Value.Keys | Sort-Object -CaseSensitive)) {
            $parts.Add((ConvertTo-Json -InputObject ([string]$key) -Compress) + ':' + (Get-CombinedAssemblyCanonicalJson $Value[$key]))
        }
        return '{' + ($parts -join ',') + '}'
    }
    if ($Value -is [System.Array] -or $Value -is [Collections.IList]) {
        $parts = [Collections.Generic.List[string]]::new()
        foreach ($item in $Value) { $parts.Add((Get-CombinedAssemblyCanonicalJson $item)) }
        return '[' + ($parts -join ',') + ']'
    }
    $props = @($Value.PSObject.Properties | Where-Object { $_.MemberType -in @('NoteProperty','Property','AliasProperty','ScriptProperty') } | Sort-Object Name -CaseSensitive)
    $object = [ordered]@{}
    foreach ($property in $props) { $object[[string]$property.Name] = $property.Value }
    return Get-CombinedAssemblyCanonicalJson $object
}

function Get-CombinedAssemblyCanonicalMailboxResponse {
    param([Parameter(Mandatory=$true)][object]$Response)
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

function Set-CombinedAssemblyUnitTestMailboxKey {
    param([Parameter(Mandatory=$true)][byte[]]$Key)
    if (-not $script:CombinedAssemblyLibraryOnly) { throw 'Unit-test mailbox keys are available only in LibraryOnly mode.' }
    if ($Key.Length -ne 32) { throw 'Unit-test mailbox key must contain exactly 32 bytes.' }
    if ($null -ne $script:CombinedAssemblyUnitTestMailboxKey) { [Array]::Clear($script:CombinedAssemblyUnitTestMailboxKey,0,$script:CombinedAssemblyUnitTestMailboxKey.Length) }
    $script:CombinedAssemblyUnitTestMailboxKey = [byte[]]::new($Key.Length)
    [Array]::Copy($Key,$script:CombinedAssemblyUnitTestMailboxKey,$Key.Length)
}

function Get-CombinedAssemblyMailboxHmacKey {
    param([Parameter(Mandatory=$true)][string]$RepositoryRoot)
    if ($script:CombinedAssemblyLibraryOnly) {
        if ($null -eq $script:CombinedAssemblyUnitTestMailboxKey -or $script:CombinedAssemblyUnitTestMailboxKey.Length -ne 32) { throw 'LibraryOnly tests must inject a unit mailbox key before validating a mailbox.' }
        $testKey = [byte[]]::new($script:CombinedAssemblyUnitTestMailboxKey.Length)
        [Array]::Copy($script:CombinedAssemblyUnitTestMailboxKey,$testKey,$testKey.Length)
        return ,$testKey
    }

    $repository = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
    $configPath = Join-Path $repository 'tools\host-runner\host-runner.config.json'
    $corePath = Join-Path $repository 'tools\host-runner\HostRunner.Core.psm1'
    [void](Assert-CombinedAssemblyNoReparsePath -Root $repository -Path $configPath -Label 'Mailbox credential configuration')
    [void](Assert-CombinedAssemblyNoReparsePath -Root $repository -Path $corePath -Label 'Mailbox credential resolver')
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf) -or -not (Test-Path -LiteralPath $corePath -PathType Leaf)) { throw 'Existing host-runner mailbox credential configuration or resolver is missing.' }

    $credential = $null
    $keyCredential = $null
    $plainBase64 = $null
    $key = $null
    try {
        Import-Module $corePath -Force -ErrorAction Stop
        $credential = Import-UhrConfig -ConfigPath $configPath
        if (-not [IO.Path]::GetFullPath([string]$credential.ProjectRoot).TrimEnd('\').Equals($repository,[StringComparison]::OrdinalIgnoreCase)) { throw 'Host-runner credential configuration ProjectRoot differs from the authorized repository.' }
        $stateRoot = [IO.Path]::GetFullPath([string]$credential.StateRoot).TrimEnd('\')
        if (-not $stateRoot.StartsWith(($repository + '\'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Host-runner credential StateRoot must remain inside the repository.' }
        [void](Assert-CombinedAssemblyNoReparsePath -Root $repository -Path $stateRoot -Label 'Mailbox credential state root')
        $keyCredential = Get-UhrCredential -Config $credential -FileName 'windows-clone-mailbox-key.xml'
        $plainBase64 = [string]$keyCredential.GetNetworkCredential().Password
        try { $key = [Convert]::FromBase64String($plainBase64) } catch { throw 'Stored mailbox HMAC credential is not valid base64.' }
        if ($key.Length -ne 32) { [Array]::Clear($key,0,$key.Length); $key = $null; throw 'Stored mailbox HMAC credential must decode to exactly 32 bytes.' }
        $copy = [byte[]]::new($key.Length)
        [Array]::Copy($key,$copy,$key.Length)
        return ,$copy
    } finally {
        if ($null -ne $key -and $key.Length -gt 0) { [Array]::Clear($key,0,$key.Length) }
        $plainBase64 = $null
        if ($null -ne $keyCredential -and $null -ne $keyCredential.Password) { $keyCredential.Password.Dispose() }
        $keyCredential = $null
        $credential = $null
    }
}

function Get-CombinedAssemblyHmacSha256Hex {
    param([Parameter(Mandatory=$true)][byte[]]$Key,[Parameter(Mandatory=$true)][string]$Text)
    if ($Key.Length -ne 32) { throw 'Mailbox HMAC key has an invalid length.' }
    $data = [Text.Encoding]::UTF8.GetBytes($Text)
    $hmac = [Security.Cryptography.HMACSHA256]::new($Key)
    try { return ([BitConverter]::ToString($hmac.ComputeHash($data))).Replace('-','') }
    finally { $hmac.Dispose(); [Array]::Clear($data,0,$data.Length) }
}

function Test-CombinedAssemblyFixedHexEqual {
    param([Parameter(Mandatory=$true)][string]$Left,[Parameter(Mandatory=$true)][string]$Right)
    if ($Left -cnotmatch '\A[0-9A-F]{64}\z' -or $Right -cnotmatch '\A[0-9A-F]{64}\z') { return $false }
    $difference = 0
    for ($index = 0; $index -lt 64; $index++) { $difference = $difference -bor ([int][char]$Left[$index] -bxor [int][char]$Right[$index]) }
    return $difference -eq 0
}

function Assert-CombinedAssemblyResponseHmac {
    param([Parameter(Mandatory=$true)][object]$Response,[Parameter(Mandatory=$true)][byte[]]$Key,[Parameter(Mandatory=$true)][string]$Label)
    $expectedProperties = @('CompletedUtc','EvidenceChunkCount','EvidenceChunkSize','EvidenceSha256','EvidenceSize','MessageBase64','ResponseHmacSha256','RunId','SchemaVersion','Status') | Sort-Object -CaseSensitive
    $actualProperties = @($Response.PSObject.Properties.Name) | Sort-Object -CaseSensitive
    if (($actualProperties -join "`n") -cne ($expectedProperties -join "`n")) { throw "$Label response manifest does not match the pinned exact schema." }
    if ([int]$Response.SchemaVersion -ne 1 -or [string]$Response.RunId -notmatch '\Auefi-[A-Za-z0-9-]{1,100}\z' -or [string]$Response.Status -cnotin @('PASS','FAIL') -or [string]$Response.EvidenceSha256 -cnotmatch '\A[0-9A-F]{64}\z' -or [string]$Response.ResponseHmacSha256 -cnotmatch '\A[0-9A-F]{64}\z') { throw "$Label response identity, digest, status, or HMAC field is invalid." }
    $size = [long]$Response.EvidenceSize
    $chunkSize = [int]$Response.EvidenceChunkSize
    $chunkCount = [int]$Response.EvidenceChunkCount
    if ($size -lt 0 -or $size -gt 33554432 -or $chunkCount -lt 0 -or $chunkCount -gt 4096 -or ($size -eq 0 -and ($chunkCount -ne 0 -or $chunkSize -ne 0 -or [string]$Response.EvidenceSha256 -cne ('0' * 64))) -or ($size -gt 0 -and ($chunkSize -lt 1 -or $chunkSize -gt 9000 -or [int][Math]::Ceiling($size / [double]$chunkSize) -ne $chunkCount))) { throw "$Label response evidence size/chunk bounds are inconsistent." }
    $messageBase64 = [string]$Response.MessageBase64
    if ($messageBase64 -cnotmatch '\A(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?\z') { throw "$Label response message is not canonical base64." }
    $messageBytes = [Convert]::FromBase64String($messageBase64)
    if ([Convert]::ToBase64String($messageBytes) -cne $messageBase64 -or $messageBytes.Length -gt 4096) { [Array]::Clear($messageBytes,0,$messageBytes.Length); throw "$Label response message is noncanonical or exceeds its limit." }
    try { $utf8 = [Text.UTF8Encoding]::new($false,$true); $message = $utf8.GetString($messageBytes) } catch { throw "$Label response message is not valid UTF-8." }
    finally { [Array]::Clear($messageBytes,0,$messageBytes.Length) }
    try { $completed = [DateTime]::Parse([string]$Response.CompletedUtc,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind) } catch { throw "$Label response completion time is invalid." }
    $computed = Get-CombinedAssemblyHmacSha256Hex -Key $Key -Text (Get-CombinedAssemblyCanonicalMailboxResponse $Response)
    if (-not (Test-CombinedAssemblyFixedHexEqual -Left $computed -Right ([string]$Response.ResponseHmacSha256))) { throw "$Label response cryptographic HMAC verification failed." }
    return [pscustomobject][ordered]@{status='PASS';runId=[string]$Response.RunId;responseHmacSha256=$computed;completedUtc=$completed.ToUniversalTime().ToString('o');message=$message}
}

function ConvertTo-CombinedAssemblyUtc {
    param([Parameter(Mandatory=$true)][string]$Text,[Parameter(Mandatory=$true)][string]$Label)
    $parsed = [DateTimeOffset]::MinValue
    if (-not [DateTimeOffset]::TryParse($Text,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind,[ref]$parsed)) { throw "$Label is not a parseable ISO-8601 timestamp: $Text" }
    return $parsed.ToUniversalTime()
}

function Get-CombinedAssemblyNestedBundle {
    param([Parameter(Mandatory=$true)][object]$Metadata,[Parameter(Mandatory=$true)][string]$Label,[Parameter(Mandatory=$true)][string]$OutputDirectory)
    $bundle = Get-CombinedAssemblyValue $Metadata 'rawEvidenceBundle'
    $base64 = [string](Get-CombinedAssemblyValue $bundle 'base64')
    $expectedBytes = Get-CombinedAssemblyValue $bundle 'bytes'
    $expectedSha = [string](Get-CombinedAssemblyValue $bundle 'sha256')
    $expectedCount = Get-CombinedAssemblyValue $bundle 'fileCount'
    $records = @(Get-CombinedAssemblyValue $Metadata 'capturedFiles')
    if ([string](Get-CombinedAssemblyValue $bundle 'format') -cne 'zip' -or $null -eq $expectedBytes -or [long]$expectedBytes -le 0 -or [long]$expectedBytes -gt 16777216 -or $base64.Length -gt 11184812 -or $expectedSha -notmatch '\A[A-Fa-f0-9]{64}\z' -or $null -eq $expectedCount -or $records.Count -ne [int]$expectedCount -or $records.Count -lt 1 -or $records.Count -gt 32) { throw "$Label rawEvidenceBundle metadata is missing, inconsistent, or outside bounds." }
    try { $bytes = [Convert]::FromBase64String($base64) } catch { throw "$Label rawEvidenceBundle base64 is invalid: $($_.Exception.Message)" }
    if ([Convert]::ToBase64String($bytes) -cne $base64 -or $bytes.Length -ne [long]$expectedBytes) { throw "$Label rawEvidenceBundle base64 or byte count differs from its metadata." }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $actualSha = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','') } finally { $sha.Dispose() }
    if ($actualSha -cne $expectedSha.ToUpperInvariant()) { throw "$Label rawEvidenceBundle SHA-256 mismatch: expected=$expectedSha actual=$actualSha" }
    $stream = [IO.MemoryStream]::new($bytes,$false)
    $zip = [IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Read,$false)
    $result = [Collections.Generic.List[object]]::new()
    try {
        if ($zip.Entries.Count -ne $records.Count) { throw "$Label nested ZIP entry count differs: metadata=$($records.Count) archive=$($zip.Entries.Count)." }
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        [long]$total = 0
        foreach ($record in $records) {
            $leaf = [string](Get-CombinedAssemblyValue $record 'archiveLeaf')
            $declared = Get-CombinedAssemblyValue $record 'bytes'
            $recordSha = [string](Get-CombinedAssemblyValue $record 'sha256')
            $guestPath = [string](Get-CombinedAssemblyValue $record 'guestPath')
            if ($leaf -notmatch '\Af\.[0-9]{4}\.[A-Za-z0-9]{1,10}\z' -or -not $seen.Add($leaf) -or $null -eq $declared -or [long]$declared -lt 0 -or [long]$declared -gt 8388608 -or $recordSha -notmatch '\A[A-Fa-f0-9]{64}\z' -or [string]::IsNullOrWhiteSpace($guestPath)) { throw "$Label capturedFiles has an invalid leaf, path, size, or SHA-256." }
            $entry = @($zip.Entries | Where-Object { [string]$_.FullName -ceq $leaf })
            if ($entry.Count -ne 1 -or [long]$entry[0].Length -ne [long]$declared) { throw "$Label nested archive member is missing, duplicated, or has a different size: $leaf" }
            $memberBytes = Read-CombinedAssemblyZipEntry -Entry $entry[0] -MaximumBytes 8388608 -Label "$Label nested member $leaf"
            $memberSha = [Security.Cryptography.SHA256]::Create()
            try { $memberHash = ([BitConverter]::ToString($memberSha.ComputeHash($memberBytes))).Replace('-','') } finally { $memberSha.Dispose() }
            if ($memberHash -cne $recordSha.ToUpperInvariant()) { throw "$Label nested member SHA-256 differs from capturedFiles: $leaf expected=$recordSha actual=$memberHash" }
            $total += [long]$memberBytes.Length
            if ($total -gt 33554432) { throw "$Label nested evidence exceeds 32 MiB expanded." }
            $target = Join-Path $OutputDirectory $leaf
            if (Test-Path -LiteralPath $target) { throw "$Label refuses to overwrite nested evidence: $target" }
            [void][IO.Directory]::CreateDirectory($OutputDirectory)
            [IO.File]::WriteAllBytes($target,$memberBytes)
            if ((Get-CombinedAssemblySha256 $target) -cne $recordSha.ToUpperInvariant()) { throw "$Label extracted member changed while writing: $leaf" }
            $result.Add([pscustomobject][ordered]@{name=[string](Get-CombinedAssemblyValue $record 'name');guestPath=$guestPath;archiveLeaf=$leaf;path=$target;sha256=$recordSha.ToUpperInvariant();bytes=[long]$declared})
        }
        foreach ($entry in $zip.Entries) { if (-not $seen.Contains([string]$entry.FullName)) { throw "$Label nested ZIP contains an unpinned member: $($entry.FullName)" } }
    } finally { $zip.Dispose(); $stream.Dispose() }
    return @($result.ToArray())
}

function Get-CombinedAssemblyMailbox {
    param(
        [Parameter(Mandatory=$true)][object]$Capture,
        [Parameter(Mandatory=$true)][string]$Label,
        [Parameter(Mandatory=$true)][string]$OutputDirectory,
        [Parameter(Mandatory=$true)][string[]]$ReportRoots,
        [Parameter(Mandatory=$true)][string[]]$ArtifactRoots,
        [Parameter(Mandatory=$true)][string]$RepositoryRoot
    )
    $archiveFile = Get-CombinedAssemblyReferenceFile -Reference (Get-CombinedAssemblyValue $Capture 'archive') -Label "$Label evidence archive" -AllowedRoots $ReportRoots
    $responseFile = Get-CombinedAssemblyReferenceFile -Reference (Get-CombinedAssemblyValue $Capture 'response') -Label "$Label mailbox response" -AllowedRoots $ReportRoots
    $receiptFile = Get-CombinedAssemblyReferenceFile -Reference (Get-CombinedAssemblyValue $Capture 'responseMetadata') -Label "$Label mailbox response metadata" -AllowedRoots $ReportRoots
    $transportRef = Get-CombinedAssemblyValue $Capture 'transportMetadata'
    $transportFile = $null
    if ($null -ne $transportRef) { $transportFile = Get-CombinedAssemblyReferenceFile -Reference $transportRef -Label "$Label transport metadata" -AllowedRoots $ReportRoots }
    if ($archiveFile.bytes -le 0 -or $archiveFile.bytes -gt 33554432) { throw "$Label outer guest-evidence archive exceeds 32 MiB." }
    $response = Get-Content -LiteralPath $responseFile.path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $receipt = Get-Content -LiteralPath $receiptFile.path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $runId = [string](Get-CombinedAssemblyValue $receipt 'RunId')
    $hmac = [string](Get-CombinedAssemblyValue $receipt 'ResponseHmacSha256')
    $evidenceSha = [string](Get-CombinedAssemblyValue $receipt 'EvidenceSha256')
    $receiptProperties = @('CompletedUtc','EvidenceArchive','EvidenceChunkCount','EvidenceChunkSize','EvidenceSha256','EvidenceSize','Message','ResponseHmacSha256','ResponseManifestVariable','RunId','SchemaVersion','Status') | Sort-Object -CaseSensitive
    $actualReceiptProperties = @($receipt.PSObject.Properties.Name) | Sort-Object -CaseSensitive
    if (($actualReceiptProperties -join "`n") -cne ($receiptProperties -join "`n")) { throw "$Label response metadata does not match the pinned exact receipt schema." }
    $mailboxKey = Get-CombinedAssemblyMailboxHmacKey -RepositoryRoot $RepositoryRoot
    try { $auth = Assert-CombinedAssemblyResponseHmac -Response $response -Key $mailboxKey -Label $Label }
    finally { [Array]::Clear($mailboxKey,0,$mailboxKey.Length) }
    if ([string]$response.Status -cne 'PASS' -or [string](Get-CombinedAssemblyValue $receipt 'Status') -cne 'PASS' -or [int](Get-CombinedAssemblyValue $receipt 'SchemaVersion') -ne 1 -or [string]::IsNullOrWhiteSpace($runId) -or $hmac -cnotmatch '\A[0-9A-F]{64}\z' -or $evidenceSha -cnotmatch '\A[0-9A-F]{64}\z') { throw "$Label mailbox response was authenticated but did not record a valid PASS." }
    foreach ($field in @('SchemaVersion','RunId','Status','ResponseHmacSha256','EvidenceSha256','EvidenceSize','EvidenceChunkSize','EvidenceChunkCount','CompletedUtc')) {
        if ([string](Get-CombinedAssemblyValue $response $field) -cne [string](Get-CombinedAssemblyValue $receipt $field)) { throw "$Label response.json and response-metadata.json differ at authenticated field $field." }
    }
    if ([string](Get-CombinedAssemblyValue $receipt 'Message') -cne [string]$auth.message -or [string](Get-CombinedAssemblyValue $receipt 'ResponseManifestVariable') -cne 'uefiloader.response.manifest') { throw "$Label response metadata message or response-manifest identity differs from the authenticated response." }
    if ($archiveFile.sha256 -cne $evidenceSha.ToUpperInvariant() -or [string](Get-CombinedAssemblyValue $receipt 'EvidenceArchive') -cne (Split-Path -Leaf $archiveFile.path) -or [long](Get-CombinedAssemblyValue $receipt 'EvidenceSize') -ne $archiveFile.bytes) { throw "$Label outer archive does not match the authenticated mailbox digest, size, and name." }
    if ($null -ne $transportFile) {
        $transport = Get-Content -LiteralPath $transportFile.path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        foreach ($hashName in @('GuestScriptSha256','CompanionExeSha256','PackageSha256')) { if ([string](Get-CombinedAssemblyValue $transport $hashName) -notmatch '\A[A-Fa-f0-9]{64}\z') { throw "$Label transport metadata lacks a valid $hashName pin." } }
        if ([string](Get-CombinedAssemblyValue $transport 'RunId') -cne $runId -or [string](Get-CombinedAssemblyValue $transport 'Action') -cne 'Probe' -or [string](Get-CombinedAssemblyValue $transport 'ExpectedProbeVerdict') -cne 'NOT_MET' -or [string](Get-CombinedAssemblyValue $transport 'ResultStatus') -cne 'PASS' -or [string]::IsNullOrWhiteSpace([string](Get-CombinedAssemblyValue $transport 'VmxPath')) -or [long](Get-CombinedAssemblyValue $transport 'PackageSize') -le 0) { throw "$Label transport metadata does not bind this successful Probe run to a VMX and package." }
    } else { throw "$Label has no hash-bound outer transport metadata." }
    $archiveStream = [IO.Compression.ZipFile]::OpenRead($archiveFile.path)
    try {
        $allowedOuter = @('kernel-metadata.json','stdout.txt','stderr.txt','tcglog.ib','tcglog-full.bin')
        $entries = @($archiveStream.Entries)
        $metadataEntries = @($entries | Where-Object { [string]$_.FullName -ceq 'kernel-metadata.json' })
        if ($metadataEntries.Count -ne 1) { throw "$Label outer authenticated ZIP must contain exactly one kernel-metadata.json; actual=$($metadataEntries.Count)." }
        foreach ($entry in $entries) {
            if ($allowedOuter -cnotcontains [string]$entry.FullName -or $entry.FullName.Contains('/') -or $entry.FullName.Contains('\') -or $entry.Length -gt 8388608) { throw "$Label outer ZIP contains an unexpected, unsafe, or oversized member: $($entry.FullName)" }
        }
        $metadataBytes = Read-CombinedAssemblyZipEntry -Entry $metadataEntries[0] -MaximumBytes 8388608 -Label "$Label kernel-metadata.json"
    } finally { $archiveStream.Dispose() }
    $metadata = ConvertFrom-CombinedAssemblyJsonBytes -Bytes $metadataBytes -Label "$Label kernel-metadata.json"
    $metadataSha = [Security.Cryptography.SHA256]::Create()
    try { $metadataHash = ([BitConverter]::ToString($metadataSha.ComputeHash($metadataBytes))).Replace('-','') } finally { $metadataSha.Dispose() }
    $expectedSchema = [string](Get-CombinedAssemblyValue $Capture 'expectedSchema')
    if ([string]::IsNullOrWhiteSpace($expectedSchema) -or [string](Get-CombinedAssemblyValue $metadata 'schema') -cne $expectedSchema) { throw "$Label guest metadata schema mismatch: expected=$expectedSchema actual=$([string](Get-CombinedAssemblyValue $metadata 'schema'))." }
    if ($expectedSchema -ceq 'combined-vm-reader-phase.v1') {
        if ([string](Get-CombinedAssemblyValue $metadata 'status') -cne 'PASS') { throw "$Label guest reader phase status is not PASS." }
        $expectedPhase = [string](Get-CombinedAssemblyValue $Capture 'expectedPhase')
        if ([string](Get-CombinedAssemblyValue $metadata 'phase') -cne $expectedPhase -or (Get-CombinedAssemblyValue $metadata 'hostExecutionPermitted') -ne $false -or (Get-CombinedAssemblyValue $metadata 'storageLifecycleOwnedByOuterRunner') -ne $true) { throw "$Label guest phase metadata does not match the requested read-only phase." }
        $phaseFailure = [string](Get-CombinedAssemblyValue $metadata 'failure')
        if (@(Get-CombinedAssemblyValue $metadata 'nicRestoreErrors').Count -gt 0 -or -not [string]::IsNullOrWhiteSpace($phaseFailure)) { throw "$Label guest phase metadata contains failure or NIC restoration errors." }
    } elseif ($expectedSchema -ceq 'combined-vm-fixture-correction-action.v1') {
        if ([string](Get-CombinedAssemblyValue $metadata 'status') -cne 'PASS') { throw "$Label guest fixture action status is not PASS." }
        $expectedMode = [string](Get-CombinedAssemblyValue $Capture 'expectedMode')
        $expectedAction = if ($expectedMode -ceq 'correction-only') { 'CaptureCorrectionOnly' } else { 'CaptureActive' }
        if ([string](Get-CombinedAssemblyValue $metadata 'mode') -cne $expectedMode -or [string](Get-CombinedAssemblyValue $metadata 'internalAction') -cne $expectedAction -or @(Get-CombinedAssemblyValue $metadata 'errors').Count -gt 0 -or (Get-CombinedAssemblyValue (Get-CombinedAssemblyValue $metadata 'cleanup') 'attempted') -ne $true -or @(Get-CombinedAssemblyValue (Get-CombinedAssemblyValue $metadata 'cleanup') 'errors').Count -gt 0) { throw "$Label fixture metadata mode/action/cleanup does not prove a completed capture." }
    } elseif ($expectedSchema -ceq 'combined-vm-lifecycle-metadata.v1') {
        if ([string](Get-CombinedAssemblyValue $metadata 'action') -cne 'FinalVerify' -or [string](Get-CombinedAssemblyValue $metadata 'outcomeStatus') -cne 'FINAL_VERIFY_PASS') { throw "$Label lifecycle metadata is not a passing FinalVerify result." }
    } else { throw "$Label schema is not supported by this assembler: $expectedSchema" }
    $outputRefRoot = Join-Path $OutputDirectory $Label
    [void][IO.Directory]::CreateDirectory($outputRefRoot)
    $copiedArchive = Copy-CombinedAssemblyFile -SourcePath $archiveFile.path -DestinationPath (Join-Path $outputRefRoot 'guest-evidence.zip') -ExpectedSha256 $archiveFile.sha256
    $copiedResponse = Copy-CombinedAssemblyFile -SourcePath $responseFile.path -DestinationPath (Join-Path $outputRefRoot 'response.json') -ExpectedSha256 $responseFile.sha256
    $copiedReceipt = Copy-CombinedAssemblyFile -SourcePath $receiptFile.path -DestinationPath (Join-Path $outputRefRoot 'response-metadata.json') -ExpectedSha256 $receiptFile.sha256
    $copiedTransport = $null
    if ($null -ne $transportFile) { $copiedTransport = Copy-CombinedAssemblyFile -SourcePath $transportFile.path -DestinationPath (Join-Path $outputRefRoot 'transport-metadata.json') -ExpectedSha256 $transportFile.sha256 }
    # Persist exact authenticated archive bytes, not a reserialized object.
    $metadataPath = Join-Path $outputRefRoot 'kernel-metadata.json'
    [IO.File]::WriteAllBytes($metadataPath,$metadataBytes)
    if ((Get-CombinedAssemblySha256 $metadataPath) -cne $metadataHash) { throw "$Label kernel metadata changed while copying the authenticated outer member." }
    # The source metadata is embedded in the authenticated outer archive. Store those exact bytes, not a reserialized object.
    return [pscustomobject][ordered]@{
        runId=$runId;responseHmacSha256=$hmac.ToUpperInvariant();evidenceSha256=$evidenceSha.ToUpperInvariant()
        authentication=[ordered]@{method='HMAC-SHA256';verified=$true;sessionId=$runId;responseHmacSha256=$hmac.ToUpperInvariant();evidenceSha256=$evidenceSha.ToUpperInvariant();verificationSource='local cryptographic recomputation using the pinned canonical response protocol and existing DPAPI credential'}
        metadata=$metadata;metadataPath=$metadataPath;metadataSha256=$metadataHash
        archive=[ordered]@{path=$copiedArchive.path;sha256=$copiedArchive.sha256}
        response=[ordered]@{path=$copiedResponse.path;sha256=$copiedResponse.sha256}
        receipt=[ordered]@{path=$copiedReceipt.path;sha256=$copiedReceipt.sha256}
        transport=if ($null -ne $copiedTransport) { [ordered]@{path=$copiedTransport.path;sha256=$copiedTransport.sha256} } else { $null }
        metadataBytes=$metadataBytes
        responseMetadata=$receipt
    }
}

function Get-CombinedAssemblyCaptureInput {
    param([Parameter(Mandatory=$true)][object]$InputObject,[Parameter(Mandatory=$true)][string]$Label)
    $capture = Get-CombinedAssemblyValue $InputObject 'capture'
    if ($null -eq $capture) { throw "$Label has no capture receipt." }
    return $capture
}

function Assert-CombinedAssemblyActiveNicRestore {
    param([Parameter(Mandatory=$true)][object]$Metadata,[Parameter(Mandatory=$true)][object[]]$Members,[Parameter(Mandatory=$true)][string]$FixtureMapPath)
    if ((Get-CombinedAssemblyValue $Metadata 'nicMutationStarted') -ne $true -or @(Get-CombinedAssemblyValue $Metadata 'nicRestoreErrors').Count -ne 0) { throw 'Active phase did not record a started NIC mutation with clean restore completion.' }
    $restore = Get-CombinedAssemblyValue $Metadata 'nicOriginalReadback'
    $restoreContent = Get-CombinedAssemblyValue $restore 'content'
    $restorePath = [string](Get-CombinedAssemblyValue $restore 'path')
    $restoreSha = [string](Get-CombinedAssemblyValue $restore 'sha256')
    if ([string]::IsNullOrWhiteSpace($restorePath) -or $restoreSha -notmatch '\A[A-Fa-f0-9]{64}\z' -or $null -eq $restoreContent) { throw 'Active phase lacks a fresh hash-pinned NIC originals readback after restoration.' }
    $raw = @($Members | Where-Object { [string]$_.guestPath -ceq $restorePath -and [string]$_.sha256 -ceq $restoreSha.ToUpperInvariant() })
    if ($raw.Count -ne 1) { throw 'Active NIC originals readback is not bound to exactly one authenticated nested raw evidence member.' }
    $rawContent = Get-Content -LiteralPath $raw[0].path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ((Get-CombinedAssemblyCanonicalJson $rawContent) -cne (Get-CombinedAssemblyCanonicalJson $restoreContent)) { throw 'Active embedded NIC originals readback differs from its independently archived raw JSON.' }
    if ([string](Get-CombinedAssemblyValue $restoreContent 'schema') -cne 'nic-native-wmi-readback.v1' -or [string](Get-CombinedAssemblyValue $restoreContent 'expectedMode') -cne 'Original' -or (Get-CombinedAssemblyValue $restoreContent 'allLockedExpectedValuesMatchReadOnlySources') -ne $true -or (Get-CombinedAssemblyValue $restoreContent 'allLockedOriginalsMatchReadOnlySources') -ne $true -or @(Get-CombinedAssemblyValue $restoreContent 'failures').Count -ne 0) { throw 'Active NIC originals readback schema/mode/aggregate status is not a complete PASS.' }
    $mapRows = @(Import-Csv -LiteralPath $FixtureMapPath -Delimiter "`t" -ErrorAction Stop)
    # The production fixture-map writer uses member=FakeMacAddress and stores
    # adapter:<ifIndex>|<PnP ID> in device. Include malformed adapter-shaped
    # rows in the candidate set so they fail closed instead of disappearing.
    $expected = @($mapRows | Where-Object {
        [string]$_.member -ceq 'FakeMacAddress' -or [string]$_.device -match '\Aadapter:'
    })
    $evaluations = @(Get-CombinedAssemblyValue $restoreContent 'lockedOriginalEvaluations')
    if ($expected.Count -ne 4 -or $evaluations.Count -ne 4) { throw "Active NIC restore coverage must contain exactly four VM adapters: expected=$($expected.Count) actual=$($evaluations.Count)." }
    $seenPnp = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $seenInterfaceIndices = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($mapRow in $expected) {
        $deviceParts = [string]$mapRow.device -split '\|',2
        if ([string]$mapRow.member -cne 'FakeMacAddress' -or $deviceParts.Count -ne 2 -or
            [string]$deviceParts[0] -notmatch '\Aadapter:[0-9]+\z' -or
            [string]::IsNullOrWhiteSpace([string]$deviceParts[1])) {
            throw 'VM NIC fixture map rows must use member=FakeMacAddress and device=adapter:<ifIndex>|<PnP ID>.'
        }
        $ifIndex = ([string]$deviceParts[0]).Substring('adapter:'.Length)
        if (-not $seenInterfaceIndices.Add($ifIndex) -or -not $seenPnp.Add([string]$deviceParts[1])) {
            throw 'VM NIC fixture map contains a duplicate adapter interface index or PnP identity.'
        }
    }
    $readerCounters = [ordered]@{
        ndisOid8023CurrentAddress='setupDiNdisInterfaceMatchCount'
        getAdaptersAddresses='getAdaptersAddressesMatchCount'
        getAdaptersInfoAddress='getAdaptersInfoMatchCount'
        getIfTable2='getIfTable2MatchCount'
        win32NetworkAdapter='win32NetworkAdapterMatchCount'
        msftNetAdapterNetworkAddresses='msftNetAdapterMatchCount'
        getNetAdapter='getNetAdapterMatchCount'
    }
    $allReaders = [Collections.Generic.List[object]]::new()
    foreach ($expectedRow in $expected) {
        $deviceParts = [string]$expectedRow.device -split '\|',2
        if ($deviceParts.Count -ne 2 -or [string]::IsNullOrWhiteSpace($deviceParts[1])) { throw "VM NIC fixture map device is malformed: $([string]$expectedRow.device)" }
        $pnp = $deviceParts[1]
        $rows = @($evaluations | Where-Object { [string](Get-CombinedAssemblyValue $_ 'pnpDeviceId') -ceq $pnp })
        if ($rows.Count -ne 1 -or [string](Get-CombinedAssemblyValue $rows[0] 'expectedMode') -cne 'Original') { throw "Active NIC restore PnP identity/mode mismatch: $pnp" }
        $row = $rows[0]
        $expectedMac = ([string]$expectedRow.original -replace '[^A-Fa-f0-9]','').ToUpperInvariant()
        if ($expectedMac -notmatch '\A[0-9A-F]{12}\z') { throw "VM fixture map has an invalid original MAC for $pnp." }
        $observations = Get-CombinedAssemblyValue $row 'observations'
        foreach ($reader in $readerCounters.Keys) {
            $items = @(Get-CombinedAssemblyValue $observations $reader)
            $count = Get-CombinedAssemblyValue $row $readerCounters[$reader]
            if ($items.Count -ne 1 -or [int]$count -ne 1) { throw "Active NIC restore reader cardinality differs: $pnp/$reader" }
            $actualMac = ([string](Get-CombinedAssemblyValue $items[0] 'value') -replace '[^A-Fa-f0-9]','').ToUpperInvariant()
            if ($actualMac -cne $expectedMac) { throw "Active NIC original mismatch: pnp=$pnp reader=$reader expected=$([string]$expectedRow.original) actual=$([string](Get-CombinedAssemblyValue $items[0] 'value'))." }
        }
        $oid = Get-CombinedAssemblyValue $row 'ndisOidStatus'
        if ((Get-CombinedAssemblyValue $oid 'ioctlSucceeded') -ne $true -or [int](Get-CombinedAssemblyValue $oid 'bytesReturned') -ne 6 -or [int](Get-CombinedAssemblyValue $oid 'win32Error') -ne 0) { throw "Active NIC restore direct OID read failed: $pnp" }
        $dot11 = Get-CombinedAssemblyValue $row 'dot11CurrentAddressCheck'
        if ((Get-CombinedAssemblyValue $dot11 'applicable') -eq $true -and ((Get-CombinedAssemblyValue $dot11 'ioctlSucceeded') -ne $true -or (([string](Get-CombinedAssemblyValue $dot11 'actual') -replace '[^A-Fa-f0-9]','').ToUpperInvariant() -cne $expectedMac))) { throw "Active NIC restore DOT11 read mismatch: $pnp" }
        $allReaders.Add([ordered]@{pnpDeviceId=$pnp;original=[string]$expectedRow.original;readerCount=$readerCounters.Count;oidStatus='PASS'})
    }
    return [pscustomobject][ordered]@{status='PASS';path=$raw[0].path;sha256=$raw[0].sha256;adapterCount=$expected.Count;readerCount=$readerCounters.Count;adapters=@($allReaders.ToArray())}
}

function New-CombinedAssemblyGuestCaptureWrapper {
    param([Parameter(Mandatory=$true)][object]$Mailbox,[Parameter(Mandatory=$true)][string]$Path)
    $value = [ordered]@{
        schema='combined-vm-authenticated-capture.v1'
        metadata=[ordered]@{path=$Mailbox.metadataPath;sha256=$Mailbox.metadataSha256}
        mailboxAuthentication=$Mailbox.authentication
        mailboxReceipt=[ordered]@{path=$Mailbox.receipt.path;sha256=$Mailbox.receipt.sha256}
        archive=[ordered]@{path=$Mailbox.archive.path;sha256=$Mailbox.archive.sha256}
    }
    $json = $value | ConvertTo-Json -Depth 12
    [IO.File]::WriteAllText($Path,$json,[Text.UTF8Encoding]::new($false))
    return [pscustomobject][ordered]@{path=$Path;sha256=(Get-CombinedAssemblySha256 $Path)}
}

function Get-CombinedAssemblyPhase {
    param([Parameter(Mandatory=$true)][string]$Name,[Parameter(Mandatory=$true)][object]$InputPhase,[Parameter(Mandatory=$true)][string]$OutputDirectory,[Parameter(Mandatory=$true)][string[]]$ReportRoots,[Parameter(Mandatory=$true)][string[]]$ArtifactRoots,[Parameter(Mandatory=$true)][string]$FixtureMapPath,[Parameter(Mandatory=$true)][string]$RepositoryRoot)
    $expectedPhase = @{original='Original';active='Active';restored='Restored'}[$Name]
    $capture = Get-CombinedAssemblyCaptureInput -InputObject $InputPhase -Label $Name
    $capture | Add-Member -Force -NotePropertyName expectedSchema -NotePropertyValue 'combined-vm-reader-phase.v1'
    $capture | Add-Member -Force -NotePropertyName expectedPhase -NotePropertyValue $expectedPhase
    $mailbox = Get-CombinedAssemblyMailbox -Capture $capture -Label $Name -OutputDirectory $OutputDirectory -ReportRoots $ReportRoots -ArtifactRoots $ArtifactRoots -RepositoryRoot $RepositoryRoot
    $metadata = $mailbox.metadata
    $bundleDirectory = Join-Path $OutputDirectory ('nested\' + $Name)
    $members = @(Get-CombinedAssemblyNestedBundle -Metadata $metadata -Label $Name -OutputDirectory $bundleDirectory)
    $readbackMembers = @($members | Where-Object { [IO.Path]::GetFileName([string]$_.guestPath) -ceq 'combined-readback.json' })
    if ($readbackMembers.Count -ne 1) { throw "$Name nested evidence must contain exactly one actual combined-readback.json; actual=$($readbackMembers.Count)." }
    $readbackPath = $readbackMembers[0].path
    $readbackContent = Get-Content -LiteralPath $readbackPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $embeddedReadback = Get-CombinedAssemblyValue $metadata 'combinedReadbackContent'
    if ($null -eq $embeddedReadback -or (Get-CombinedAssemblyCanonicalJson $embeddedReadback) -cne (Get-CombinedAssemblyCanonicalJson $readbackContent)) { throw "$Name embedded combinedReadbackContent differs from its independently archived combined-readback.json." }
    $nicRestoreProof = $null
    if ($Name -ceq 'active') { $nicRestoreProof = Assert-CombinedAssemblyActiveNicRestore -Metadata $metadata -Members $members -FixtureMapPath $FixtureMapPath }
    $wrapperPath = Join-Path (Split-Path -Parent $mailbox.metadataPath) 'authenticated-capture.json'
    $wrapper = New-CombinedAssemblyGuestCaptureWrapper -Mailbox $mailbox -Path $wrapperPath
    $phaseBoot = Get-CombinedAssemblyValue $metadata 'bootIdentity'
    if ([string]::IsNullOrWhiteSpace([string](Get-CombinedAssemblyValue $phaseBoot 'bootId')) -or [string]::IsNullOrWhiteSpace([string](Get-CombinedAssemblyValue $phaseBoot 'windowsLastBootUpTimeUtc'))) { throw "$Name guest metadata lacks a boot identity." }
    $phase = [ordered]@{
        bootIdentity=[ordered]@{bootId=[string]$phaseBoot.bootId;lastBootUpTimeUtc=[string]$phaseBoot.windowsLastBootUpTimeUtc;windowsLastBootUpTimeUtc=[string]$phaseBoot.windowsLastBootUpTimeUtc}
        combinedReadback=[ordered]@{path=$readbackPath;sha256=$readbackMembers[0].sha256}
        guestCapture=$wrapper
        mailboxReceipt=$mailbox.receipt
        mailboxResponse=$mailbox.response
        archive=$mailbox.archive
        transportMetadata=if ($null -ne $mailbox.transport) { $mailbox.transport } else { $null }
        rawEvidence=@($members | ForEach-Object { [ordered]@{path=$_.path;sha256=$_.sha256} })
    }
    if ($null -ne $nicRestoreProof) { $phase.nicRestoreEvidence=$nicRestoreProof }
    return [pscustomobject][ordered]@{phase=$phase;mailbox=$mailbox;metadata=$metadata;readback=$readbackContent;members=$members}
}

function Get-CombinedAssemblyActiveNicRestoreEvidence {
    param([Parameter(Mandatory=$true)][object]$ActivePhaseResult)
    $activePhase = Get-CombinedAssemblyValue $ActivePhaseResult 'phase'
    if ($null -eq $activePhase) { throw 'Restoration flags cannot be derived because the cold original matrix or active NIC restore evidence did not pass.' }
    $evidence = Get-CombinedAssemblyValue $activePhase 'nicRestoreEvidence'
    if ($null -eq $evidence -or [string](Get-CombinedAssemblyValue $evidence 'status') -cne 'PASS') {
        throw 'Restoration flags cannot be derived because the cold original matrix or active NIC restore evidence did not pass.'
    }
    return $evidence
}

function Get-CombinedAssemblyFixtureCorrection {
    param([Parameter(Mandatory=$true)][ValidateSet('correctionOnly','active')][string]$Name,[Parameter(Mandatory=$true)][object]$InputFixture,[Parameter(Mandatory=$true)][string]$OutputDirectory,[Parameter(Mandatory=$true)][string[]]$ReportRoots,[Parameter(Mandatory=$true)][string[]]$ArtifactRoots,[Parameter(Mandatory=$true)][string]$RepositoryRoot)
    $expectedMode = if ($Name -ceq 'correctionOnly') { 'correction-only' } else { 'corrected-collector' }
    $capture = Get-CombinedAssemblyCaptureInput -InputObject $InputFixture -Label "fixtureCorrection.$Name"
    $capture | Add-Member -Force -NotePropertyName expectedSchema -NotePropertyValue 'combined-vm-fixture-correction-action.v1'
    $capture | Add-Member -Force -NotePropertyName expectedMode -NotePropertyValue $expectedMode
    $mailbox = Get-CombinedAssemblyMailbox -Capture $capture -Label ("fixtureCorrection\" + $Name) -OutputDirectory $OutputDirectory -ReportRoots $ReportRoots -ArtifactRoots $ArtifactRoots -RepositoryRoot $RepositoryRoot
    $metadata = $mailbox.metadata
    $expectedNames = @('report.txt','eps2-before.bin','eps2-after.bin','eps3-before.bin','eps3-after.bin','table-before.bin','table-after.bin','smbios2-ep-prelude.efi')
    if ($Name -ceq 'active') { $expectedNames += 'HypersinvSmbiosPatch.txt' }
    $members = @(Get-CombinedAssemblyNestedBundle -Metadata $metadata -Label ("fixtureCorrection\" + $Name) -OutputDirectory (Join-Path $OutputDirectory ('nested\fixture-' + $Name)))
    $captured = @(Get-CombinedAssemblyValue $metadata 'capturedFiles')
    if ($captured.Count -ne $expectedNames.Count -or $members.Count -ne $expectedNames.Count) { throw "fixtureCorrection.$Name exact raw evidence count differs: expected=$($expectedNames.Count) actual=$($members.Count)." }
    $fileRefs = [ordered]@{}
    $expectedKeys = [ordered]@{eps2Before='eps2-before.bin';eps2After='eps2-after.bin';eps3Before='eps3-before.bin';eps3After='eps3-after.bin';tableBefore='table-before.bin';tableAfter='table-after.bin'}
    foreach ($key in $expectedKeys.Keys) {
        $member = @($members | Where-Object { [IO.Path]::GetFileName([string]$_.guestPath) -ceq [string]$expectedKeys[$key] })
        if ($member.Count -ne 1) { throw "fixtureCorrection.$Name is missing exactly one $($expectedKeys[$key]) raw member." }
        $fileRefs[$key] = [ordered]@{path=$member[0].path;bytes=$member[0].bytes;sha256=$member[0].sha256;archiveEntry=$member[0].archiveLeaf}
    }
    $reportMember = @($members | Where-Object { [IO.Path]::GetFileName([string]$_.guestPath) -ceq 'report.txt' })
    if ($reportMember.Count -ne 1) { throw "fixtureCorrection.$Name must contain one prelude report." }
    $preludeInput = Get-CombinedAssemblyReferenceFile -Reference (Get-CombinedAssemblyValue $InputFixture 'prelude') -Label "fixtureCorrection.$Name prelude" -AllowedRoots $ArtifactRoots
    $manifestInput = Get-CombinedAssemblyReferenceFile -Reference (Get-CombinedAssemblyValue $InputFixture 'buildManifest') -Label "fixtureCorrection.$Name build manifest" -AllowedRoots $ReportRoots
    $copiedManifest = Copy-CombinedAssemblyFile -SourcePath $manifestInput.path -DestinationPath (Join-Path $OutputDirectory ('fixtureCorrection\' + $Name + '\fixture-build.json')) -ExpectedSha256 $manifestInput.sha256
    $sourcePins = @(Get-CombinedAssemblyValue $InputFixture 'sourcePins')
    if ($sourcePins.Count -eq 0) { throw "fixtureCorrection.$Name must provide the build's actual source pins." }
    $boot = Get-CombinedAssemblyValue $metadata 'bootIdentity'
    $correction = [ordered]@{
        mode=$expectedMode
        mailbox=[ordered]@{runId=$mailbox.runId;responseHmacSha256=$mailbox.responseHmacSha256;evidenceSha256=$mailbox.evidenceSha256;evidenceArchivePath=$mailbox.archive.path;evidenceArchiveSha256=$mailbox.archive.sha256;responsePath=$mailbox.response.path;responseSha256=$mailbox.response.sha256}
        mailboxReceipt=$mailbox.receipt
        mailboxAuthentication=$mailbox.authentication
        bootIdentity=[ordered]@{bootId=[string]$boot.bootId;windowsLastBootUpTimeUtc=[string]$boot.windowsLastBootUpTimeUtc;lastBootUpTimeUtc=[string]$boot.lastBootUpTimeUtc}
        prelude=[ordered]@{path=$preludeInput.path;sha256=$preludeInput.sha256}
        buildManifest=[ordered]@{path=$copiedManifest.path;sha256=$copiedManifest.sha256}
        sourcePins=$sourcePins
        report=[ordered]@{path=$reportMember[0].path;bytes=$reportMember[0].bytes;sha256=$reportMember[0].sha256;archiveEntry=$reportMember[0].archiveLeaf}
        files=$fileRefs
    }
    if ($Name -ceq 'active') {
        $collectorMember = @($members | Where-Object { [IO.Path]::GetFileName([string]$_.guestPath) -ceq 'HypersinvSmbiosPatch.txt' })
        if ($collectorMember.Count -ne 1) { throw 'fixtureCorrection.active must contain one actual HypersinvSmbiosPatch.txt report.' }
        $collectorMetadata = Get-CombinedAssemblyValue $metadata 'collectorReport'
        if ($null -eq $collectorMetadata) { throw 'fixtureCorrection.active authenticated metadata has no collectorReport binding.' }
        $correction.collectorReport = [ordered]@{path=$collectorMember[0].path;bytes=$collectorMember[0].bytes;sha256=$collectorMember[0].sha256;archiveEntry=$collectorMember[0].archiveLeaf;status=[string]$collectorMetadata.status;result=[string]$collectorMetadata.result;mask=[int]$collectorMetadata.mask;buildId=[string]$collectorMetadata.buildId;efiStatus=[string]$collectorMetadata.efiStatus;efiStatusSource=[string]$collectorMetadata.efiStatusSource;efiStatusPhase=[string]$collectorMetadata.efiStatusPhase;startStatusObserved=[bool]$collectorMetadata.startStatusObserved;values=$collectorMetadata.values;bootIdentity=$collectorMetadata.bootIdentity;bootIdentityProvenance=$collectorMetadata.bootIdentityProvenance}
    }
    return [pscustomobject][ordered]@{fixture=$correction;mailbox=$mailbox;metadata=$metadata;members=$members}
}

function Assert-CombinedAssemblyFinalLifecycle {
    param([Parameter(Mandatory=$true)][object]$Mailbox,[Parameter(Mandatory=$true)][object]$ColdRestore,[Parameter(Mandatory=$true)][object]$OriginalPhase,[Parameter(Mandatory=$true)][object]$RestoredPhase,[Parameter(Mandatory=$true)][string[]]$ReportRoots)
    $metadata = $Mailbox.metadata
    $record = Get-CombinedAssemblyValue $metadata 'result'
    if ([string](Get-CombinedAssemblyValue $record 'schema') -cne 'combined-vm-lifecycle-result.v1' -or [string](Get-CombinedAssemblyValue $record 'action') -cne 'FinalVerify' -or [string](Get-CombinedAssemblyValue $record 'status') -cne 'FINAL_VERIFY_PASS' -or [string](Get-CombinedAssemblyValue $record 'failure')) { throw 'FinalVerify mailbox metadata does not contain a successful lifecycle result.' }
    $details = Get-CombinedAssemblyValue $record 'result'
    $journal = Get-CombinedAssemblyValue $metadata 'journal'
    if ([string](Get-CombinedAssemblyValue $journal 'schema') -cne 'combined-vm-lifecycle-journal.v1' -or
        [string](Get-CombinedAssemblyValue $journal 'status') -cne 'FINAL_VERIFY_PASS') {
        throw 'FinalVerify metadata lacks its authenticated terminal lifecycle journal and captured baseline.'
    }
    $secureBoot = Get-CombinedAssemblyValue $details 'secureBoot'
    $baselineSecureBoot = Get-CombinedAssemblyValue $journal 'secureBoot'
    $baselineVariable = Get-CombinedAssemblyValue $baselineSecureBoot 'variable'
    $finalVariable = Get-CombinedAssemblyValue $secureBoot 'variable'
    if ([string](Get-CombinedAssemblyValue $details 'status') -cne 'FINAL_VERIFY_PASS' -or
        (Get-CombinedAssemblyValue $secureBoot 'enabled') -ne (Get-CombinedAssemblyValue $baselineSecureBoot 'enabled') -or
        (Get-CombinedAssemblyValue $baselineVariable 'supported') -ne $true -or
        [int](Get-CombinedAssemblyValue $baselineVariable 'win32Error') -ne 0 -or
        (Get-CombinedAssemblyValue $finalVariable 'supported') -ne $true -or
        [int](Get-CombinedAssemblyValue $finalVariable 'win32Error') -ne 0) {
        throw 'FinalVerify Secure Boot state does not match the authenticated lifecycle baseline.'
    }
    foreach ($property in @('size','attributes','bytesHex','bytesBase64')) {
        if ([string](Get-CombinedAssemblyValue $finalVariable $property) -cne [string](Get-CombinedAssemblyValue $baselineVariable $property)) {
            throw "FinalVerify SecureBoot variable $property differs from the authenticated lifecycle baseline."
        }
    }

    $baselineCodeIntegrity = Get-CombinedAssemblyValue $journal 'initialCodeIntegrity'
    $finalCodeIntegrity = Get-CombinedAssemblyValue $details 'effectiveCodeIntegrity'
    $originalMetadata = Get-CombinedAssemblyValue $OriginalPhase 'metadata'
    $restoredMetadata = Get-CombinedAssemblyValue $RestoredPhase 'metadata'
    foreach ($phase in @([pscustomobject]@{label='original';metadata=$originalMetadata},[pscustomobject]@{label='cold-restored';metadata=$restoredMetadata})) {
        $phaseMetadata = Get-CombinedAssemblyValue $phase 'metadata'
        $phaseCi = Get-CombinedAssemblyValue $phaseMetadata 'codeIntegrity'
        if ([string](Get-CombinedAssemblyValue $phaseMetadata 'status') -cne 'PASS' -or
            (Get-CombinedAssemblyValue $phaseMetadata 'secureBoot') -ne (Get-CombinedAssemblyValue $baselineSecureBoot 'enabled')) {
            throw "The $([string]$phase.label) phase Secure Boot state differs from the authenticated original baseline."
        }
        if ((Get-CombinedAssemblyValue $phaseCi 'success') -ne (Get-CombinedAssemblyValue $baselineCodeIntegrity 'success') -or
            [uint32](Get-CombinedAssemblyValue $phaseCi 'options') -ne [uint32](Get-CombinedAssemblyValue $baselineCodeIntegrity 'options')) {
            throw "The $([string]$phase.label) phase Code Integrity state differs from the authenticated original baseline."
        }
    }
    if ((Get-CombinedAssemblyValue $finalCodeIntegrity 'success') -ne $true -or
        [uint32](Get-CombinedAssemblyValue $finalCodeIntegrity 'options') -ne [uint32](Get-CombinedAssemblyValue $baselineCodeIntegrity 'options') -or
        ([uint32](Get-CombinedAssemblyValue $finalCodeIntegrity 'options') -band 2) -ne 0) {
        throw 'FinalVerify effective Code Integrity policy differs from the authenticated baseline or still has test-signing enabled.'
    }

    $baselineTestSigning = Get-CombinedAssemblyValue $journal 'initialTestSigning'
    $finalTestSigning = Get-CombinedAssemblyValue $details 'bcdTestSigning'
    if ((Get-CombinedAssemblyValue $finalTestSigning 'exists') -ne (Get-CombinedAssemblyValue $baselineTestSigning 'exists') -or
        ((Get-CombinedAssemblyValue $baselineTestSigning 'exists') -eq $true -and
         [string](Get-CombinedAssemblyValue $finalTestSigning 'value') -cne [string](Get-CombinedAssemblyValue $baselineTestSigning 'value'))) {
        throw 'FinalVerify BCD test-signing element differs from the authenticated original baseline.'
    }
    $initialBcdExportHash = [string](Get-CombinedAssemblyValue $journal 'bcdExportSha256')
    $finalSnapshot = Get-CombinedAssemblyValue $journal 'finalSnapshot'
    $finalBcdExportHash = [string](Get-CombinedAssemblyValue $finalSnapshot 'bcdExportSha256')
    if ($initialBcdExportHash -notmatch '\A[A-Fa-f0-9]{64}\z' -or $finalBcdExportHash -notmatch '\A[A-Fa-f0-9]{64}\z' -or
        (Get-CombinedAssemblyValue $details 'bcdNonTestSigningPropertiesIdentical') -ne $true -or
        (Get-CombinedAssemblyValue $details 'bcdExportByteIdentityIsEvidenceOnly') -ne $true -or
        (Get-CombinedAssemblyValue $finalSnapshot 'bcdExportByteIdentityIsEvidenceOnly') -ne $true -or
        (Get-CombinedAssemblyValue $details 'bcdExportByteIdentical') -ne ($initialBcdExportHash -ceq $finalBcdExportHash) -or
        @(Get-CombinedAssemblyValue $details 'espDelta').Count -ne 0 -or @(Get-CombinedAssemblyValue $details 'filterServices').Count -ne 0) {
        throw 'FinalVerify does not prove BCD semantic restoration, evidence-only export classification, ESP restoration, and filter removal.'
    }
    $trust = Get-CombinedAssemblyValue $details 'trust'
    if ((Get-CombinedAssemblyValue $trust 'Root') -ne $false -or (Get-CombinedAssemblyValue $trust 'TrustedPublisher') -ne $false) { throw 'FinalVerify lifecycle details do not prove signer trust removal.' }
    $coldSchema = [string](Get-CombinedAssemblyValue $ColdRestore 'schema')
    if ($coldSchema -cne 'combined-vm-cold-snapshot-restore.v1' -or [string](Get-CombinedAssemblyValue $ColdRestore 'status') -cne 'PASS') { throw 'Restored cold-snapshot receipt is absent or has an unsupported schema/status.' }
    $snapshot = Get-CombinedAssemblyValue $ColdRestore 'snapshot'
    $snapshotFile = Get-CombinedAssemblyReferenceFile -Reference $snapshot -Label 'cold-snapshot artifact' -AllowedRoots $ReportRoots
    if ([string]::IsNullOrWhiteSpace([string](Get-CombinedAssemblyValue $snapshot 'id'))) { throw 'Cold-snapshot receipt lacks a pinned snapshot artifact and identity.' }
    if ([IO.Path]::GetExtension($snapshotFile.path) -notin @('.vmsd','.vmsn')) { throw 'Cold-snapshot artifact must be the actual .vmsd metadata or relevant .vmsn file, not a VM disk.' }
    if ([string](Get-CombinedAssemblyValue $snapshot 'id') -cne 'Combined-Clean4-20261018') { throw 'Cold-snapshot receipt does not identify the expected Combined-Clean4-20261018 snapshot.' }
    $snapshotInventory = Get-CombinedAssemblyValue $ColdRestore 'snapshotInventory'
    if ($null -eq $snapshotInventory -or $null -eq (Get-CombinedAssemblyValue $snapshotInventory 'stdout') -or $null -eq (Get-CombinedAssemblyValue $snapshotInventory 'stderr') -or [int](Get-CombinedAssemblyValue $snapshotInventory 'exitCode') -ne 0 -or [string](Get-CombinedAssemblyValue $snapshotInventory 'command') -notmatch '(?i)(?:^|\s)listSnapshots(?:\s|$)' -or [string](Get-CombinedAssemblyValue $snapshotInventory 'stdout') -notmatch [Regex]::Escape([string](Get-CombinedAssemblyValue $snapshot 'id'))) { throw 'Cold-snapshot receipt lacks successful vmrun listSnapshots output containing the exact snapshot id.' }
    $inventoryStarted = ConvertTo-CombinedAssemblyUtc -Text ([string](Get-CombinedAssemblyValue $snapshotInventory 'startedUtc')) -Label 'snapshot inventory startedUtc'
    $inventoryCompleted = ConvertTo-CombinedAssemblyUtc -Text ([string](Get-CombinedAssemblyValue $snapshotInventory 'completedUtc')) -Label 'snapshot inventory completedUtc'
    if ($inventoryCompleted -lt $inventoryStarted) { throw 'Snapshot inventory completion precedes its start.' }
    $stepTimes = @{}
    foreach ($stepName in @('restore','start')) {
        $step = Get-CombinedAssemblyValue $ColdRestore $stepName
        if ($null -eq $step -or $null -eq (Get-CombinedAssemblyValue $step 'stdout') -or $null -eq (Get-CombinedAssemblyValue $step 'stderr') -or [int](Get-CombinedAssemblyValue $step 'exitCode') -ne 0 -or [string]::IsNullOrWhiteSpace([string](Get-CombinedAssemblyValue $step 'command'))) { throw "Cold-snapshot $stepName receipt lacks captured command output or a successful exit code." }
        $command = [string](Get-CombinedAssemblyValue $step 'command')
        if ($stepName -ceq 'restore' -and ($command -notmatch '(?i)(?:^|\s)revertToSnapshot(?:\s|$)' -or $command -notmatch [Regex]::Escape([string](Get-CombinedAssemblyValue $snapshot 'id')))) { throw 'Cold-snapshot restore command does not invoke revertToSnapshot with the exact recorded snapshot id.' }
        if ($stepName -ceq 'start' -and ($command -notmatch '(?i)(?:^|\s)start(?:\s|$)' -or $command -notmatch '(?i)(?:^|\s)nogui(?:\s|$)')) { throw 'Cold-snapshot start command does not invoke vmrun start with nogui.' }
        $stepTimes[$stepName + 'Started'] = ConvertTo-CombinedAssemblyUtc -Text ([string](Get-CombinedAssemblyValue $step 'startedUtc')) -Label "$stepName startedUtc"
        $stepTimes[$stepName + 'Completed'] = ConvertTo-CombinedAssemblyUtc -Text ([string](Get-CombinedAssemblyValue $step 'completedUtc')) -Label "$stepName completedUtc"
        if ($stepTimes[$stepName + 'Completed'] -lt $stepTimes[$stepName + 'Started']) { throw "Cold-snapshot $stepName completion precedes its start." }
    }
    if ($stepTimes.restoreStarted -lt $inventoryCompleted -or $stepTimes.startStarted -lt $stepTimes.restoreCompleted) { throw 'Cold-snapshot list, restore, and start command timestamps are out of order.' }
    $restoredBoot = Get-CombinedAssemblyValue $ColdRestore 'bootIdentity'
    $phaseBoot = Get-CombinedAssemblyValue (Get-CombinedAssemblyValue $RestoredPhase 'phase') 'bootIdentity'
    $restoredMetadata = $RestoredPhase.metadata
    if ([string]::IsNullOrWhiteSpace([string](Get-CombinedAssemblyValue $restoredBoot 'bootId')) -or [string](Get-CombinedAssemblyValue $restoredBoot 'bootId') -cne [string](Get-CombinedAssemblyValue $phaseBoot 'bootId') -or [string](Get-CombinedAssemblyValue $restoredBoot 'lastBootUpTimeUtc') -cne [string](Get-CombinedAssemblyValue $phaseBoot 'lastBootUpTimeUtc')) { throw 'Restored phase boot identity does not match the recorded cold-snapshot start receipt.' }
    $finalBootTime = [string](Get-CombinedAssemblyValue (Get-CombinedAssemblyValue $details 'bootIdentity') 'lastBootUpTimeUtc')
    $restoredBootTime = [string](Get-CombinedAssemblyValue $restoredBoot 'lastBootUpTimeUtc')
    if ([string]::IsNullOrWhiteSpace($finalBootTime) -or [string]::IsNullOrWhiteSpace($restoredBootTime) -or $finalBootTime -ceq $restoredBootTime) { throw 'FinalVerify and cold-restored phase do not have distinct Windows boot identities.' }
    $finalVerifyCompleted = ConvertTo-CombinedAssemblyUtc -Text ([string](Get-CombinedAssemblyValue $metadata 'completedUtc')) -Label 'FinalVerify metadata completedUtc'
    $restoredCaptureCompleted = ConvertTo-CombinedAssemblyUtc -Text ([string](Get-CombinedAssemblyValue $restoredMetadata 'completedUtc')) -Label 'cold-restored phase completedUtc'
    if ($finalVerifyCompleted -gt $stepTimes.restoreStarted -or $restoredCaptureCompleted -lt $stepTimes.startCompleted) { throw 'FinalVerify/restore/cold-restored capture timestamps are not ordered around the snapshot transition.' }
    return [pscustomobject][ordered]@{status='PASS';bootIdentity=$restoredBoot;details=$details;bcdExportEvidence=[ordered]@{baselineSha256=$initialBcdExportHash;finalSha256=$finalBcdExportHash;byteIdentical=($initialBcdExportHash -ceq $finalBcdExportHash);classification='evidence-only; raw export hash difference is reported, not treated as a semantic BCD mismatch'};finalVerifyCompletedUtc=$finalVerifyCompleted.ToString('o');coldRestoreStartedUtc=$stepTimes.restoreStarted.ToString('o');coldStartCompletedUtc=$stepTimes.startCompleted.ToString('o');coldCaptureCompletedUtc=$restoredCaptureCompleted.ToString('o');snapshotId=[string](Get-CombinedAssemblyValue $snapshot 'id')}
}

function Invoke-CombinedVmGateAssembly {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$InputManifestPath,[Parameter(Mandatory=$true)][string]$RepositoryRoot,[Parameter(Mandatory=$true)][string]$WorktreeRoot,[Parameter(Mandatory=$true)][string]$OutputDirectory)
    if ($script:CombinedAssemblyLibraryOnly) { throw 'Gate assembly is disabled in LibraryOnly mode; test keys cannot produce gate evidence.' }
    $repository = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
    $worktree = [IO.Path]::GetFullPath($WorktreeRoot).TrimEnd('\')
    $worktreeReports = Join-Path $worktree 'spoof-handoff\reports'
    $worktreeArtifacts = Join-Path $worktree 'spoof-handoff\artifacts'
    $repositoryReports = Join-Path $repository 'spoof-handoff\reports'
    $inputPath = Assert-CombinedAssemblyNoReparsePath -Root $worktreeReports -Path $InputManifestPath -Label 'Gate assembly input manifest'
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { throw "Gate assembly input manifest does not exist: $inputPath" }
    $output = Assert-CombinedAssemblyNoReparsePath -Root $worktreeReports -Path $OutputDirectory -Label 'Gate assembly output directory'
    if (Test-Path -LiteralPath $output) { if ((Get-Item -LiteralPath $output -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Gate assembly output is a reparse point.' }; if (@(Get-ChildItem -LiteralPath $output -Force).Count -gt 0) { throw "Refusing a populated gate assembly output directory: $output" } }
    [void][IO.Directory]::CreateDirectory($output)
    $inputHash = Get-CombinedAssemblySha256 $inputPath
    $input = Get-Content -LiteralPath $inputPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ([string](Get-CombinedAssemblyValue $input 'schema') -cne 'combined-vm-gate-assembly-input.v1') { throw 'Input schema must be combined-vm-gate-assembly-input.v1.' }
    $candidatePath = Join-Path $output '.gate-candidate.json'
    $finalPath = Join-Path $output 'combined-vm-gate.json'
    try {
        $vmMapFile = Get-CombinedAssemblyReferenceFile -Reference (Get-CombinedAssemblyValue $input 'vmFixtureMap') -Label 'VM fixture map' -AllowedRoots @($worktreeReports)
        $vmMapCopy = Copy-CombinedAssemblyFile -SourcePath $vmMapFile.path -DestinationPath (Join-Path $output 'vm-fixture-map.tsv') -ExpectedSha256 $vmMapFile.sha256
        $sourcePins = @(Get-CombinedAssemblyValue $input 'sourcePins')
        if ($sourcePins.Count -eq 0) { throw 'Input has no pinned VM gate source set.' }
        $sourceMap = [ordered]@{}
        foreach ($pin in $sourcePins) {
            $path = [string](Get-CombinedAssemblyValue $pin 'path')
            $hash = [string](Get-CombinedAssemblyValue $pin 'sha256')
            if ([string]::IsNullOrWhiteSpace($path) -or $hash -notmatch '\A[A-Fa-f0-9]{64}\z') { throw 'Input source pin lacks a path or valid SHA-256.' }
            $key = [IO.Path]::GetFullPath($path)
            if ($sourceMap.Contains($key)) { throw "Input repeats a source pin: $key" }
            $sourceMap[$key] = $pin
        }
        $authenticationSourcePins = @(Assert-CombinedAssemblyAuthenticationSourcePins -SourcePins $sourcePins -RepositoryRoot $repository -WorktreeRoot $worktree)
        $assemblyVerifierPath = [IO.Path]::GetFullPath($PSCommandPath)
        $assemblyVerifierSha256 = Get-CombinedAssemblySha256 $assemblyVerifierPath
        $phaseInput = Get-CombinedAssemblyValue $input 'phases'
        $phaseResults = [ordered]@{}
        foreach ($phaseName in @('original','active','restored')) {
            $phaseRecord = Get-CombinedAssemblyValue $phaseInput $phaseName
            if ($null -eq $phaseRecord) { throw "Input is missing phase $phaseName." }
            $phaseResults[$phaseName] = Get-CombinedAssemblyPhase -Name $phaseName -InputPhase $phaseRecord -OutputDirectory $output -ReportRoots @($repositoryReports,$worktreeReports) -ArtifactRoots @($worktreeArtifacts) -FixtureMapPath $vmMapFile.path -RepositoryRoot $repository
        }
        $fixtureInput = Get-CombinedAssemblyValue $input 'fixtureCorrection'
        $correctionOnlyInput = Get-CombinedAssemblyValue $fixtureInput 'correctionOnly'
        $activeCorrectionInput = Get-CombinedAssemblyValue $fixtureInput 'active'
        if ($null -eq $correctionOnlyInput -or $null -eq $activeCorrectionInput) { throw 'Input must contain both actual fixtureCorrection.correctionOnly and fixtureCorrection.active captures.' }
        $correctionOnly = Get-CombinedAssemblyFixtureCorrection -Name correctionOnly -InputFixture $correctionOnlyInput -OutputDirectory $output -ReportRoots @($repositoryReports,$worktreeReports) -ArtifactRoots @($worktreeArtifacts) -RepositoryRoot $repository
        $activeCorrection = Get-CombinedAssemblyFixtureCorrection -Name active -InputFixture $activeCorrectionInput -OutputDirectory $output -ReportRoots @($repositoryReports,$worktreeReports) -ArtifactRoots @($worktreeArtifacts) -RepositoryRoot $repository
        $phaseResults.active.phase['efi'] = [ordered]@{status=[string]$activeCorrection.metadata.efi.childLoadStatus;startStatusObserved=$false;statusSource='prelude.report.child_load_status';statusPhase='LoadImage(loader.efi)'}
        if ([string]::IsNullOrWhiteSpace([string]$activeCorrection.metadata.efi.childLoadStatus)) { throw 'Active correction metadata contains no observed loader LoadImage status.' }
        $finalVerifyCapture = Get-CombinedAssemblyCaptureInput -InputObject (Get-CombinedAssemblyValue $input 'finalVerify') -Label 'FinalVerify lifecycle'
        $finalVerifyCapture | Add-Member -Force -NotePropertyName expectedSchema -NotePropertyValue 'combined-vm-lifecycle-metadata.v1'
        $finalVerify = Get-CombinedAssemblyMailbox -Capture $finalVerifyCapture -Label 'finalVerify' -OutputDirectory $output -ReportRoots @($repositoryReports,$worktreeReports) -ArtifactRoots @($worktreeArtifacts) -RepositoryRoot $repository
        $coldRestoreRef = Get-CombinedAssemblyReferenceFile -Reference (Get-CombinedAssemblyValue $input 'coldSnapshotRestore') -Label 'cold-snapshot restore receipt' -AllowedRoots @($worktreeReports)
        $coldRestore = Get-Content -LiteralPath $coldRestoreRef.path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        $snapshotRef = Get-CombinedAssemblyValue $coldRestore 'snapshot'
        $snapshotInput = Get-CombinedAssemblyReferenceFile -Reference $snapshotRef -Label 'cold snapshot artifact' -AllowedRoots @($repositoryReports,$worktreeReports)
        $snapshotCopy = Copy-CombinedAssemblyFile -SourcePath $snapshotInput.path -DestinationPath (Join-Path $output ('cold-snapshot-artifact' + [IO.Path]::GetExtension($snapshotInput.path))) -ExpectedSha256 $snapshotInput.sha256
        $coldRestoreCopy = Copy-CombinedAssemblyFile -SourcePath $coldRestoreRef.path -DestinationPath (Join-Path $output 'cold-snapshot-restore.json') -ExpectedSha256 $coldRestoreRef.sha256
        $finalVerification = Assert-CombinedAssemblyFinalLifecycle -Mailbox $finalVerify -ColdRestore $coldRestore -OriginalPhase $phaseResults.original -RestoredPhase $phaseResults.restored -ReportRoots @($repositoryReports,$worktreeReports)
        $restoredReadback = $phaseResults.restored.readback
        $restoredPhase = $phaseResults.restored.phase
        $activeNicRestoreEvidence = Get-CombinedAssemblyActiveNicRestoreEvidence -ActivePhaseResult $phaseResults.active
        if ([string](Get-CombinedAssemblyValue $restoredReadback 'expectedMode') -cne 'Original' -or [string](Get-CombinedAssemblyValue $restoredReadback 'status') -cne 'PASS' -or (Get-CombinedAssemblyValue $restoredReadback 'simultaneousReadbackPass') -ne $true -or (Get-CombinedAssemblyValue $restoredReadback 'stableAcrossPolls') -ne $true) { throw 'Restoration flags cannot be derived because the cold original matrix or active NIC restore evidence did not pass.' }
        $restoredPhase['deviceState'] = [ordered]@{storageFilterPresent=(@(Get-CombinedAssemblyValue $finalVerification.details 'filterServices').Count -gt 0);nicRegistryRestored=($activeNicRestoreEvidence.status -ceq 'PASS' -and [string](Get-CombinedAssemblyValue $restoredReadback 'status') -ceq 'PASS')}
        $restoredPhase['coldSnapshotBoot'] = $true
        $restoredPhase['finalVerify'] = [ordered]@{metadataPath=$finalVerify.metadataPath;metadataSha256=$finalVerify.metadataSha256;archive=$finalVerify.archive;response=$finalVerify.response;receipt=$finalVerify.receipt;details=$finalVerification.details}
        $candidate = [ordered]@{
            schema='combined-vm-gate.v1';status='PASS';assemblySchema='combined-vm-gate-assembly-input.v1';input=[ordered]@{path=$inputPath;sha256=$inputHash}
            vmFixtureMap=[ordered]@{path=$vmMapCopy.path;sha256=$vmMapCopy.sha256}
            sourcePins=$sourcePins
            authenticationVerifier=[ordered]@{schema='combined-vm-mailbox-authentication.v1';algorithm='HMAC-SHA256';canonicalResponseSource='pinned invoke-mailbox.ps1 Get-CanonicalResponse field order and UTF-8 newline format';credentialResolverSource='pinned HostRunner.Core.psm1 DPAPI PSCredential windows-clone-mailbox-key.xml';keyBytes=32;responseHmacRecomputedForEveryCapture=$true;sourcePins=$authenticationSourcePins;assembler=[ordered]@{path=$assemblyVerifierPath;sha256=$assemblyVerifierSha256}}
            phases=[ordered]@{original=$phaseResults.original.phase;active=$phaseResults.active.phase;restored=$restoredPhase}
            fixtureCorrection=[ordered]@{correctionOnly=$correctionOnly.fixture;active=$activeCorrection.fixture}
            restoration=[ordered]@{coldSnapshotRestore=[ordered]@{path=$coldRestoreCopy.path;sha256=$coldRestoreCopy.sha256};coldSnapshotArtifact=[ordered]@{path=$snapshotCopy.path;sha256=$snapshotCopy.sha256};finalVerify=[ordered]@{metadataPath=$finalVerify.metadataPath;metadataSha256=$finalVerify.metadataSha256;archive=$finalVerify.archive;response=$finalVerify.response;receipt=$finalVerify.receipt}}
        }
        [IO.File]::WriteAllText($candidatePath,($candidate | ConvertTo-Json -Depth 60),[Text.UTF8Encoding]::new($false))
        $validatorPath = Join-Path $worktree 'spoof-handoff\tools\combined_host\run.ps1'
        if (-not (Test-Path -LiteralPath $validatorPath -PathType Leaf)) { throw "Existing host validator is missing: $validatorPath" }
        . $validatorPath -Action Status -RepositoryRoot $repository -WorktreeRoot $worktree -LibraryOnly
        $firstValidation = Assert-CombinedVmGateEvidence -Repository $repository -Worktree $worktree -GatePath $candidatePath
        Move-Item -LiteralPath $candidatePath -Destination $finalPath -ErrorAction Stop
        try { $finalValidation = Assert-CombinedVmGateEvidence -Repository $repository -Worktree $worktree -GatePath $finalPath }
        catch { Remove-Item -LiteralPath $finalPath -Force -ErrorAction SilentlyContinue; throw }
        $receipt = [ordered]@{schema='combined-vm-gate-assembly-receipt.v1';status='PASS';gatePath=$finalPath;gateSha256=$finalValidation.sha256;inputPath=$inputPath;inputSha256=$inputHash;validatorSchema='combined-vm-gate.v1';validatorBindingCount=$finalValidation.bindingCount;activeBootId=$finalValidation.activeBootId;validatorPath=$validatorPath;validatorSha256=(Get-CombinedAssemblySha256 $validatorPath);authenticationVerifierPath=$assemblyVerifierPath;authenticationVerifierSha256=$assemblyVerifierSha256;authenticationNote='Every mailbox response HMAC was recomputed against the existing DPAPI-protected 32-byte key using the pinned canonical response protocol before PASS metadata was emitted.'}
        $receiptPath = Join-Path $output 'assembly-receipt.json'
        [IO.File]::WriteAllText($receiptPath,($receipt | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
        return [pscustomobject][ordered]@{status='PASS';gatePath=$finalPath;gateSha256=$finalValidation.sha256;receiptPath=$receiptPath;receiptSha256=(Get-CombinedAssemblySha256 $receiptPath);bindingCount=$finalValidation.bindingCount;activeBootId=$finalValidation.activeBootId}
    } catch {
        if (Test-Path -LiteralPath $candidatePath) { Remove-Item -LiteralPath $candidatePath -Force -ErrorAction SilentlyContinue }
        $failure = [ordered]@{schema='combined-vm-gate-assembly-failure.v1';status='FAIL';inputPath=$inputPath;inputSha256=$inputHash;error=$_.Exception.ToString();scriptStackTrace=$_.ScriptStackTrace;completedUtc=[DateTime]::UtcNow.ToString('o')}
        $failurePath = Join-Path $output 'assembly-failure.json'
        [IO.File]::WriteAllText($failurePath,($failure | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
        throw "Combined VM gate assembly failed closed; evidence=$failurePath; $($_.Exception.Message)"
    }
}

if (-not $LibraryOnly) {
    foreach ($name in @('InputManifestPath','RepositoryRoot','WorktreeRoot','OutputDirectory')) {
        if ([string]::IsNullOrWhiteSpace([string](Get-Variable -Name $name -ValueOnly))) { throw "- $name is required unless -LibraryOnly is specified." }
    }
    $result = Invoke-CombinedVmGateAssembly -InputManifestPath $InputManifestPath -RepositoryRoot $RepositoryRoot -WorktreeRoot $WorktreeRoot -OutputDirectory $OutputDirectory
    $result | ConvertTo-Json -Depth 12
}
