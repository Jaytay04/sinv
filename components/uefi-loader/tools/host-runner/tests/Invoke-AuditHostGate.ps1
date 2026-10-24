[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [Parameter(Mandatory=$true)][string]$StateRoot
)

# Retired with the loader rewrite: this gate's inputs lost their CMake
# targets when the replay loader's diagnostics and fixture generators were
# removed (ibaudit.exe, audit_dump_test.exe and audit_pcr_test.exe), so a clean build cannot satisfy it.  Refuse here, before
# the core module is imported, before a gate run directory exists and before
# the missing-binary assertion, so the reason is reported instead of a stale
# binary from an older build tree being used by accident.
throw 'The audit host gate is retired: its gate binaries no longer have build targets.'

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) `
    'HostRunner.Core.psm1') -Force

function Assert-Gate {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Invoke-GateProcess {
    param([string]$FilePath, [string[]]$ArgumentList, [string]$Label)
    $Result = Invoke-UhrProcess -FilePath $FilePath `
        -ArgumentList $ArgumentList -WorkingDirectory $ProjectRoot `
        -TimeoutSeconds 120
    [IO.File]::WriteAllText((Join-Path $RunRoot "$Label-stdout.txt"),
        $Result.Stdout, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $RunRoot "$Label-stderr.txt"),
        $Result.Stderr, [Text.UTF8Encoding]::new($false))
    return $Result
}

function Add-U16 {
    param([Collections.Generic.List[byte]]$Bytes, [uint16]$Value)
    $Bytes.Add([byte]($Value -band 0xff))
    $Bytes.Add([byte](($Value -shr 8) -band 0xff))
}

function Add-U32 {
    param([Collections.Generic.List[byte]]$Bytes, [uint32]$Value)
    for ($Index = 0; $Index -lt 4; $Index++) {
        $Bytes.Add([byte](($Value -shr (8 * $Index)) -band 0xff))
    }
}

function New-DumpFixture {
    param([string]$Path, [switch]$Tampered)
    $Bytes = [Collections.Generic.List[byte]]::new()
    1..16 | ForEach-Object { $Bytes.Add(0) }
    $PayloadStart = $Bytes.Count
    $Bytes.AddRange([Text.Encoding]::ASCII.GetBytes('Spec ID Event03'))
    $Bytes.Add(0)
    Add-U32 $Bytes 0
    $Bytes.Add(0); $Bytes.Add(2); $Bytes.Add(0); $Bytes.Add(4)
    Add-U32 $Bytes 1
    Add-U16 $Bytes 0x000b
    Add-U16 $Bytes 32
    $Bytes.Add(0)
    Add-U32 $Bytes 0
    Add-U32 $Bytes 5
    Add-U32 $Bytes 1
    Add-U16 $Bytes 0x000b
    $DigestOffset = $Bytes.Count
    $EventData = [Text.Encoding]::ASCII.GetBytes('boot')
    $Hasher = [Security.Cryptography.SHA256]::Create()
    try { $Digest = $Hasher.ComputeHash($EventData) }
    finally { $Hasher.Dispose() }
    if ($Tampered) { $Digest[0] = $Digest[0] -bxor 0x80 }
    $Bytes.AddRange($Digest)
    Add-U32 $Bytes ([uint32]$EventData.Length)
    $Bytes.AddRange($EventData)
    $PayloadSize = [uint32]($Bytes.Count - $PayloadStart)
    $Bytes[0] = [byte][char]'I'; $Bytes[1] = [byte][char]'B'
    $Bytes[2] = [byte][char]'L'; $Bytes[3] = [byte][char]'G'
    $Bytes[4] = 1; $Bytes[6] = 1; $Bytes[8] = 1
    for ($Index = 0; $Index -lt 4; $Index++) {
        $Bytes[12 + $Index] = [byte](($PayloadSize -shr (8 * $Index)) -band 0xff)
    }
    [IO.File]::WriteAllBytes($Path, $Bytes.ToArray())
    return $DigestOffset
}

$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$StateRoot = [IO.Path]::GetFullPath($StateRoot)
$RunId = 'p9-audit-host-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') +
    '-' + [Guid]::NewGuid().ToString('N').Substring(0, 10)
$RunRoot = Join-Path (Join-Path $StateRoot 'audit-gates') $RunId
$Audit = Join-Path $ProjectRoot 'build\windows-static\ibaudit.exe'
$DumpTest = Join-Path $ProjectRoot 'build\windows-static\audit_dump_test.exe'
$PcrTest = Join-Path $ProjectRoot 'build\windows-static\audit_pcr_test.exe'
$VarsTest = Join-Path $ProjectRoot 'build\windows-static\audit_vars_test.exe'
$TcgVersionTest = Join-Path $ProjectRoot `
    'build\windows-static\audit_tcg_version_test.exe'
$BootmgrTest = Join-Path $ProjectRoot `
    'build\windows-static\audit_bootmgr_test.exe'
$BaselineTest = Join-Path $ProjectRoot `
    'build\windows-static\audit_baseline_test.exe'
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

foreach ($Path in @($Audit, $DumpTest, $PcrTest, $VarsTest,
        $TcgVersionTest, $BootmgrTest, $BaselineTest)) {
    Assert-Gate (Test-Path -LiteralPath $Path -PathType Leaf) `
        "Required defensive-audit gate input is missing: $Path"
}

foreach ($Test in @(
        @{ Path = $DumpTest; Label = 'dump-unit' },
        @{ Path = $PcrTest; Label = 'pcr-unit' },
        @{ Path = $VarsTest; Label = 'vars-unit' },
        @{ Path = $TcgVersionTest; Label = 'tcg-version-unit' },
        @{ Path = $BootmgrTest; Label = 'bootmgr-unit' },
        @{ Path = $BaselineTest; Label = 'baseline-unit' })) {
    $TestResult = Invoke-GateProcess -FilePath $Test.Path `
        -ArgumentList @('--host-gate') -Label $Test.Label
    Assert-Gate ($TestResult.ExitCode -eq 0) `
        "Defensive-audit unit gate failed: $($Test.Label)"
}

$Clean = Join-Path $RunRoot 'clean-tcglog.ib'
$Tampered = Join-Path $RunRoot 'tampered-tcglog.ib'
[void](New-DumpFixture -Path $Clean)
[void](New-DumpFixture -Path $Tampered -Tampered)

$CleanResult = Invoke-GateProcess -FilePath $Audit `
    -ArgumentList @('dump', '--log', $Clean) -Label 'dump-clean'
Assert-Gate ($CleanResult.ExitCode -eq 0 -and
    $CleanResult.Stdout.Contains('Recomputed=SHA256 MATCH') -and
    $CleanResult.Stdout.Contains('StructuralFindings=0 DigestFindings=0')) `
    'ibaudit dump rejected the clean fixture.'

$TamperedResult = Invoke-GateProcess -FilePath $Audit `
    -ArgumentList @('dump', '--log', $Tampered) -Label 'dump-tampered'
Assert-Gate ($TamperedResult.ExitCode -eq 1 -and
    $TamperedResult.Stdout.Contains('DIGEST_MISMATCH EventNum=0 PCR=0')) `
    'ibaudit dump did not classify the tampered digest.'

$Result = [ordered]@{
    SchemaVersion = 1
    RunId = $RunId
    Status = 'PASS'
    AuditSha256 = (Get-FileHash -LiteralPath $Audit -Algorithm SHA256).Hash
    CleanFixtureSha256 = (Get-FileHash -LiteralPath $Clean -Algorithm SHA256).Hash
    TamperedFixtureSha256 = `
        (Get-FileHash -LiteralPath $Tampered -Algorithm SHA256).Hash
    CompletedUtc = [DateTime]::UtcNow.ToString('o')
}
Write-UhrAtomicJson -Path (Join-Path $RunRoot 'result.json') -Value $Result
[pscustomobject]@{ Result = 'PASS'; RunId = $RunId; Evidence = $RunRoot }
