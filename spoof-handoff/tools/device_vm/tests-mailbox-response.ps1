[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$sourcePath = Join-Path $PSScriptRoot 'invoke-mailbox.ps1'
$tokens = $null
$parseErrors = $null
$sourceAst = [Management.Automation.Language.Parser]::ParseFile($sourcePath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) {
    throw ('Mailbox source has PowerShell parse errors: ' + (($parseErrors | ForEach-Object { $_.ToString() }) -join "`n"))
}

$functionNames = @(
    'ConvertFrom-CanonicalBase64', 'Write-JsonFile', 'Get-HmacSha256Hex',
    'Test-FixedHexEqual', 'Get-CanonicalResponse', 'Get-GuestChunkName',
    'Read-ManifestJson', 'Get-ResponseMessage', 'Copy-VerifiedEvidenceArchive',
    'Get-ResponseEvidence', 'Assert-ExactResponseSchema', 'Receive-Response',
    'Get-CurrentRunIdFromManifest', 'Clear-OwnedGuestVariables',
    'ConvertTo-SafeDiagnostic'
)
foreach ($name in $functionNames) {
    $matches = @($sourceAst.FindAll({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name
    }, $true))
    if ($matches.Count -ne 1) { throw ('Expected exactly one source function AST for ' + $name + '; found=' + $matches.Count) }
    . ([scriptblock]::Create($matches[0].Extent.Text))
}

$script:ChunkBytes = 9000
$script:ChunkChars = 12000
$script:MaxEvidenceBytes = 32MB
$script:MaxEvidenceFileBytes = 8MB
$script:MaxManifestChars = 65536
$script:Tombstone = 'MA=='
$script:ResponseManifestName = 'uefiloader.response.manifest'
$script:RequestManifestName = 'uefiloader.request.manifest'
$script:RequestChunkPrefix = 'uefiloader.request.chunk.'
$script:ResponseChunkPrefix = 'uefiloader.response.chunk.'
$script:AllowedEvidence = @('stdout.txt', 'stderr.txt', 'tcglog.ib', 'tcglog-full.bin', 'kernel-metadata.json')
$script:RunId = 'uefi-windows-probe-20261017-235959-abcdef0123'
$script:MailboxKey = [byte[]](1..32)
$script:MailboxKeyBase64 = [Convert]::ToBase64String($script:MailboxKey)
$script:VmPassword = ''
$WaitTimeoutSeconds = 30

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
if ($repositoryRoot -match '[\\/]\.codex-build[\\/]worktrees[\\/][^\\/]+$') {
    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot '..\..\..'))
}
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'spoof-handoff\reports\nic-vmnet-compatibility-20261018')).TrimEnd('\') + '\'
$testRoot = [IO.Path]::GetFullPath((Join-Path $fixtureRoot ('nic-mailbox-response-tests.' + [Guid]::NewGuid().ToString('N'))))
if (-not $testRoot.StartsWith($fixtureRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Test fixture directory escaped the repository report directory.'
}
[void][IO.Directory]::CreateDirectory($testRoot)

function Invoke-VmrunVariable {
    param(
        [string]$Verb,
        [string]$VariableName,
        [AllowEmptyString()][string]$Value,
        [int]$TimeoutSeconds = 25
    )
    if ($Verb -ceq 'readVariable') {
        $script:MockReads.Add($VariableName)
        if ($VariableName -ceq $script:ResponseManifestName) { return $script:MockManifest }
        if ($script:MockChunks.ContainsKey($VariableName)) { return [string]$script:MockChunks[$VariableName] }
        return $null
    }
    if ($Verb -ceq 'writeVariable') {
        $script:MockWrites.Add([ordered]@{ name = $VariableName; value = $Value })
        if ($VariableName -ceq $script:ResponseManifestName) { $script:MockManifest = $Value }
        return $true
    }
    throw ('Unexpected transport verb in pure test: ' + $Verb)
}

function Initialize-TestCase([string]$Name) {
    $script:RunDirectory = Join-Path $testRoot $Name
    [void][IO.Directory]::CreateDirectory($script:RunDirectory)
    $script:ResponseManifestOwned = $false
    $script:OwnedRequestNames = [Collections.Generic.List[string]]::new()
    $script:OwnedResponseNames = [Collections.Generic.List[string]]::new()
    $script:CleanupAttempts = [Collections.Generic.List[string]]::new()
    $script:VmrunErrors = [Collections.Generic.List[object]]::new()
    $script:MockManifest = $null
    $script:MockChunks = @{}
    $script:MockReads = [Collections.Generic.List[string]]::new()
    $script:MockWrites = [Collections.Generic.List[object]]::new()
}

function New-TestEvidenceZip([string]$EntryName, [string]$Contents) {
    $stream = [IO.MemoryStream]::new()
    $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
    try {
        $entry = $archive.CreateEntry($EntryName)
        $entryStream = $entry.Open()
        try {
            $writer = [IO.StreamWriter]::new($entryStream, [Text.UTF8Encoding]::new($false))
            try { $writer.Write($Contents) }
            finally { $writer.Dispose() }
        }
        finally { $entryStream.Dispose() }
    }
    finally { $archive.Dispose() }
    try { return ,$stream.ToArray() }
    finally { $stream.Dispose() }
}

function Set-TestResponse([string]$Status, [string]$Message, [byte[]]$EvidenceBytes) {
    $evidenceHash = '0' * 64
    $chunkCount = 0
    $chunkSize = 0
    if ($EvidenceBytes.Length -gt 0) {
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $evidenceHash = ([BitConverter]::ToString($sha.ComputeHash($EvidenceBytes))).Replace('-', '') }
        finally { $sha.Dispose() }
        $chunkSize = $script:ChunkBytes
        $chunkCount = [int][Math]::Ceiling($EvidenceBytes.Length / [double]$chunkSize)
        for ($index = 0; $index -lt $chunkCount; $index++) {
            $offset = $index * $chunkSize
            $length = [Math]::Min($chunkSize, $EvidenceBytes.Length - $offset)
            $chunk = New-Object byte[] $length
            try {
                [Array]::Copy($EvidenceBytes, $offset, $chunk, 0, $length)
                $chunkName = Get-GuestChunkName 'response' $script:RunId $index
                $script:MockChunks[$chunkName] = [Convert]::ToBase64String($chunk)
            }
            finally { [Array]::Clear($chunk, 0, $chunk.Length) }
        }
    }
    $response = [ordered]@{
        SchemaVersion = 1
        RunId = $script:RunId
        Status = $Status
        MessageBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Message))
        CompletedUtc = [DateTime]::UtcNow.ToString('o')
        EvidenceSha256 = $evidenceHash
        EvidenceSize = [long]$EvidenceBytes.Length
        EvidenceChunkSize = [int]$chunkSize
        EvidenceChunkCount = [int]$chunkCount
    }
    $response.ResponseHmacSha256 = Get-HmacSha256Hex -Key $script:MailboxKey -Text (Get-CanonicalResponse $response)
    $manifestBytes = [Text.Encoding]::UTF8.GetBytes(($response | ConvertTo-Json -Compress -Depth 5))
    try { $script:MockManifest = [Convert]::ToBase64String($manifestBytes) }
    finally { [Array]::Clear($manifestBytes, 0, $manifestBytes.Length) }
    return $response
}

function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

try {
    Initialize-TestCase 'empty-fail'
    $guestFailure = 'fixed Windows guest gate did not publish result.json'
    $response = Set-TestResponse 'FAIL' $guestFailure ([byte[]]@())
    $caught = $null
    try { [void](Receive-Response -Deadline ([DateTime]::UtcNow.AddMinutes(1))) }
    catch { $caught = $_ }
    Assert-Test ($null -ne $caught) 'Authenticated FAIL response with empty evidence must throw.'
    Assert-Test ($caught.Exception.Message -ceq ('Guest worker returned FAIL: ' + $guestFailure)) ('Expected exact guest failure; actual=' + $caught.Exception.Message)
    $metadataPath = Join-Path $script:RunDirectory 'response-metadata.json'
    Assert-Test (Test-Path -LiteralPath $metadataPath -PathType Leaf) 'Failure response metadata was not persisted before surfacing the guest failure.'
    $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    Assert-Test ($metadata.Status -ceq 'FAIL' -and [long]$metadata.EvidenceSize -eq 0 -and $null -eq $metadata.EvidenceArchive) 'Empty failure metadata has incorrect status/size/archive fields.'
    Assert-Test ($script:ResponseManifestOwned -and $response.Status -ceq 'FAIL') 'Authenticated failed response ownership was not retained for cleanup.'
    Clear-OwnedGuestVariables
    Assert-Test ($script:CleanupAttempts.Count -eq 1 -and $script:CleanupAttempts[0] -ceq $script:ResponseManifestName) 'Response manifest cleanup was not preserved after an authenticated FAIL.'
    Assert-Test ($script:MockWrites.Count -eq 1 -and $script:MockWrites[0].value -ceq $script:Tombstone) 'Response manifest cleanup did not write its tombstone.'

    Initialize-TestCase 'nonempty-pass'
    $evidence = New-TestEvidenceZip 'stdout.txt' 'validated evidence'
    $null = Set-TestResponse 'PASS' 'guest passed' $evidence
    $metadata = Receive-Response -Deadline ([DateTime]::UtcNow.AddMinutes(1))
    Assert-Test ($metadata.Status -ceq 'PASS' -and $metadata.EvidenceArchive -ceq 'guest-evidence.zip') 'Nonempty successful response metadata changed.'
    $archivePath = Join-Path $script:RunDirectory 'guest-evidence.zip'
    $stdoutPath = Join-Path (Join-Path $script:RunDirectory 'evidence') 'stdout.txt'
    Assert-Test ((Test-Path -LiteralPath $archivePath -PathType Leaf) -and (Test-Path -LiteralPath $stdoutPath -PathType Leaf)) 'Verified nonempty evidence archive was not retained/extracted.'
    Assert-Test (([IO.File]::ReadAllText($stdoutPath)) -ceq 'validated evidence') 'Extracted success evidence differs from the authenticated archive.'

    Initialize-TestCase 'bad-hmac'
    $null = Set-TestResponse 'FAIL' 'authenticated message' ([byte[]]@())
    $tampered = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($script:MockManifest)) | ConvertFrom-Json
    $tampered.ResponseHmacSha256 = ('A' * 64)
    $tamperedBytes = [Text.Encoding]::UTF8.GetBytes(($tampered | ConvertTo-Json -Compress -Depth 5))
    try { $script:MockManifest = [Convert]::ToBase64String($tamperedBytes) }
    finally { [Array]::Clear($tamperedBytes, 0, $tamperedBytes.Length) }
    $caught = $null
    try { [void](Receive-Response -Deadline ([DateTime]::UtcNow.AddMinutes(1))) }
    catch { $caught = $_ }
    Assert-Test ($null -ne $caught -and $caught.Exception.Message -ceq 'Guest response authentication failed.') 'Tampered response HMAC was not rejected exactly.'
    Assert-Test (-not (Test-Path -LiteralPath (Join-Path $script:RunDirectory 'response-metadata.json') -PathType Leaf)) 'Metadata was persisted for an unauthenticated response.'
    Clear-OwnedGuestVariables
    Assert-Test ($script:MockWrites.Count -eq 0) 'Cleanup modified an unauthenticated response manifest.'

    Write-Output 'PASS: authenticated empty-evidence FAIL preserves metadata, exact guest failure, and owned-variable cleanup.'
    Write-Output 'PASS: nonempty authenticated success archive behavior remains unchanged.'
    Write-Output 'PASS: tampered HMAC response is rejected and not cleaned as owned.'
}
finally {
    if (Test-Path -LiteralPath $testRoot -PathType Container) {
        $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
        if (-not $resolvedTestRoot.StartsWith($fixtureRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing to remove test directory outside the repository report directory.'
        }
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
