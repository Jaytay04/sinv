[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$RunnerRoot = Split-Path -Parent $PSScriptRoot
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $RunnerRoot)
$TestRoot = Join-Path $ProjectRoot (
    'build\host-runner-mailbox-tests\' + [Guid]::NewGuid().ToString('N'))
$MailboxRoot = Join-Path $TestRoot 'mailbox'
$LocalRoot = Join-Path $TestRoot 'local'
$GuestRunner = Join-Path $RunnerRoot `
    'guest\Invoke-UEFILoaderGuestMailbox.ps1'
$KeyPath = Join-Path $TestRoot 'mailbox.key'
$MailboxKey = [byte[]](0..31)

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Get-TestHmac([string]$Text) {
    $Data = [Text.Encoding]::UTF8.GetBytes($Text)
    $Hmac = [Security.Cryptography.HMACSHA256]::new($MailboxKey)
    try {
        return ([BitConverter]::ToString(
            $Hmac.ComputeHash($Data))).Replace('-', '')
    }
    finally { $Hmac.Dispose(); [Array]::Clear($Data, 0, $Data.Length) }
}

function New-TestPackage([string]$RunId, [switch]$Traversal) {
    $Stage = Join-Path $TestRoot "stage-$RunId"
    New-Item -ItemType Directory -Path $Stage | Out-Null
    [IO.File]::WriteAllText((Join-Path $Stage 'ib-tcg-dump.exe'), 'fixture')
    $Stub = @'
param([string]$RunRoot,[string]$Action,[string]$ExpectedProbeVerdict)
[IO.File]::WriteAllText((Join-Path $RunRoot 'stdout.txt'), "Action=$Action Verdict=$ExpectedProbeVerdict")
[ordered]@{SchemaVersion=1;RunId=(Split-Path -Leaf $RunRoot);Status='PASS';Message='fixture';CompletedUtc=[DateTime]::UtcNow.ToString('o')} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $RunRoot 'result.json') -Encoding utf8
exit 0
'@
    [IO.File]::WriteAllText(
        (Join-Path $Stage 'Invoke-WindowsGuestTest.ps1'), $Stub,
        [Text.UTF8Encoding]::new($false))
    $Sums = @('ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1') |
        ForEach-Object {
            '{0}  {1}' -f (Get-FileHash -LiteralPath (Join-Path $Stage $_) `
                -Algorithm SHA256).Hash, $_
        }
    [IO.File]::WriteAllText((Join-Path $Stage 'SHA256SUMS.txt'),
        (($Sums -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
    $ArtifactRoot = Join-Path $MailboxRoot "artifacts\$RunId"
    New-Item -ItemType Directory -Path $ArtifactRoot -Force | Out-Null
    $Package = Join-Path $ArtifactRoot 'package.zip'
    if (-not $Traversal) {
        [IO.Compression.ZipFile]::CreateFromDirectory($Stage, $Package)
    }
    else {
        $Archive = [IO.Compression.ZipFile]::Open(
            $Package, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($Name in @(
                    'ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1',
                    'SHA256SUMS.txt')) {
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $Archive, (Join-Path $Stage $Name), $Name) | Out-Null
            }
            $Entry = $Archive.CreateEntry('../escape.txt')
            $Writer = [IO.StreamWriter]::new($Entry.Open())
            try { $Writer.Write('escape') } finally { $Writer.Dispose() }
        }
        finally { $Archive.Dispose() }
    }
    return $Package
}

function Publish-TestRequest(
    [string]$RunId, [string]$Package, [string]$Action = 'Probe') {
    $RequestRoot = Join-Path $MailboxRoot 'requests'
    New-Item -ItemType Directory -Path $RequestRoot -Force | Out-Null
    $Request = [ordered]@{
        SchemaVersion = 1
        RunId = $RunId
        Action = $Action
        ExpectedProbeVerdict = 'NOT_MET'
        PackageRelativePath = "artifacts/$RunId/package.zip"
        PackageSha256 = (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash
        TimeoutSeconds = 30
        SubmittedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $Canonical = @(
        "SchemaVersion=$($Request.SchemaVersion)"
        "RunId=$($Request.RunId)"
        "Action=$($Request.Action)"
        "ExpectedProbeVerdict=$($Request.ExpectedProbeVerdict)"
        "PackageRelativePath=$($Request.PackageRelativePath)"
        "PackageSha256=$($Request.PackageSha256)"
        "TimeoutSeconds=$($Request.TimeoutSeconds)"
        "SubmittedUtc=$($Request.SubmittedUtc)"
    ) -join "`n"
    $Request.RequestHmacSha256 = Get-TestHmac $Canonical
    $Path = Join-Path $RequestRoot "$RunId.json"
    [IO.File]::WriteAllText($Path, ($Request | ConvertTo-Json -Depth 4),
        [Text.UTF8Encoding]::new($false))
}

function Invoke-One([string]$RunId) {
    & $GuestRunner -ConfigPath (Join-Path $TestRoot 'guest-config.json') `
        -Once -TestMode
    $ResultPath = Join-Path $MailboxRoot "responses\$RunId\result.json"
    Assert-True (Test-Path -LiteralPath $ResultPath -PathType Leaf) `
        "Result was not published: $RunId"
    return Get-Content -Raw -LiteralPath $ResultPath | ConvertFrom-Json
}

try {
    foreach ($Directory in @(
            $TestRoot, $MailboxRoot, (Join-Path $MailboxRoot 'requests'),
            (Join-Path $MailboxRoot 'responses'), $LocalRoot)) {
        New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    }
    $Config = [ordered]@{
        SchemaVersion = 1
        MailboxRoot = $MailboxRoot
        LocalRunRoot = $LocalRoot
        KeyPath = $KeyPath
        PollMilliseconds = 250
    }
    [IO.File]::WriteAllBytes($KeyPath, $MailboxKey)
    [IO.File]::WriteAllText((Join-Path $TestRoot 'guest-config.json'),
        ($Config | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))

    $PassId = 'uefi-windows-probe-20260906-120000-aaaaaaaaaa'
    $PassPackage = New-TestPackage $PassId
    Publish-TestRequest $PassId $PassPackage
    $Pass = Invoke-One $PassId
    Assert-True ($Pass.Status -ceq 'PASS') (
        'Valid mailbox request did not pass: ' +
        ($Pass | ConvertTo-Json -Compress))
    Assert-True (Test-Path -LiteralPath (
        Join-Path $MailboxRoot "responses\$PassId\stdout.txt")) `
        'Allowlisted evidence was not copied before the result.'

    $TamperId = 'uefi-windows-probe-20260906-120001-bbbbbbbbbb'
    $TamperPackage = New-TestPackage $TamperId
    Publish-TestRequest $TamperId $TamperPackage
    [IO.File]::AppendAllText($TamperPackage, 'tamper')
    $Tamper = Invoke-One $TamperId
    Assert-True ($Tamper.Status -ceq 'FAIL' -and
        $Tamper.Message -match 'hash') 'Tampered package was not rejected.'

    $ActionId = 'uefi-windows-probe-20260906-120002-cccccccccc'
    $ActionPackage = New-TestPackage $ActionId
    Publish-TestRequest $ActionId $ActionPackage 'Shell'
    $Action = Invoke-One $ActionId
    Assert-True ($Action.Status -ceq 'FAIL' -and
        $Action.Message -match 'allowlisted') 'Unsupported action was accepted.'

    $TraversalId = 'uefi-windows-probe-20260906-120003-dddddddddd'
    $TraversalPackage = New-TestPackage $TraversalId -Traversal
    Publish-TestRequest $TraversalId $TraversalPackage
    $TraversalResult = Invoke-One $TraversalId
    Assert-True ($TraversalResult.Status -ceq 'FAIL' -and
        $TraversalResult.Message -match 'allowlisted root files') `
        'Traversal package was not rejected.'
    Assert-True (-not (Test-Path -LiteralPath (
        Join-Path $TestRoot 'escape.txt'))) 'Traversal file escaped staging.'

    Write-Host 'PASS: Windows passwordless mailbox contracts verified.'
}
finally {
    if (Test-Path -LiteralPath $TestRoot -PathType Container) {
        $Resolved = [IO.Path]::GetFullPath($TestRoot)
        $ExpectedRoot = [IO.Path]::GetFullPath((
            Join-Path $ProjectRoot 'build\host-runner-mailbox-tests')).TrimEnd('\') + '\'
        if ($Resolved.StartsWith(
                $ExpectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $Resolved -Recurse -Force
        }
    }
}
