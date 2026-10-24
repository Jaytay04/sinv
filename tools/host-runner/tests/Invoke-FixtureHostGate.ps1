[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [Parameter(Mandatory=$true)][string]$StateRoot
)

# Retired with the loader rewrite: this gate's inputs lost their CMake
# targets when the replay loader's diagnostics and fixture generators were
# removed (fixture_gen.exe, fixture_suite_test.exe, audit_bootmgr_platform_test.exe and ibaudit.exe), so a clean build cannot satisfy it.  Refuse here, before
# the core module is imported, before a gate run directory exists and before
# the missing-binary assertion, so the reason is reported instead of a stale
# binary from an older build tree being used by accident.
throw 'The fixture host gate is retired: its gate binaries no longer have build targets.'

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

$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$StateRoot = [IO.Path]::GetFullPath($StateRoot)
$RunId = 'p10-fixtures-host-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') +
    '-' + [Guid]::NewGuid().ToString('N').Substring(0, 10)
$RunRoot = Join-Path (Join-Path $StateRoot 'fixture-gates') $RunId
$FixtureRoot = Join-Path $RunRoot 'fixtures'
$BuildRoot = Join-Path $ProjectRoot 'build\windows-static'
$Generator = Join-Path $BuildRoot 'fixture_gen.exe'
$Validator = Join-Path $BuildRoot 'fixture_suite_test.exe'
$Audit = Join-Path $BuildRoot 'ibaudit.exe'
$BootmgrPlatformTest = Join-Path $BuildRoot 'audit_bootmgr_platform_test.exe'
$SystemBootManager = Join-Path $env:SystemRoot 'Boot\EFI\bootmgfw.efi'
$DetectorTests = @(
    'audit_pcr_test.exe', 'audit_vars_test.exe', 'audit_bootmgr_test.exe',
    'audit_tcg_version_test.exe', 'audit_baseline_test.exe'
)
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

foreach ($Path in @($Generator, $Validator, $Audit, $BootmgrPlatformTest,
        $SystemBootManager) +
        @($DetectorTests | ForEach-Object { Join-Path $BuildRoot $_ })) {
    Assert-Gate (Test-Path -LiteralPath $Path -PathType Leaf) `
        "Required P10 gate input is missing: $Path"
}

$Generate = Invoke-GateProcess -FilePath $Generator `
    -ArgumentList @($FixtureRoot) -Label 'generate'
Assert-Gate ($Generate.ExitCode -eq 0 -and
    $Generate.Stdout.Contains('PASS fixture suite events=8')) `
    'Fixture generator failed.'

$CleanBootManager = Join-Path $FixtureRoot 'clean-bootmgfw.efi'
Copy-Item -LiteralPath $SystemBootManager -Destination $CleanBootManager
$BootmgrPlatform = Invoke-GateProcess -FilePath $BootmgrPlatformTest `
    -ArgumentList @($CleanBootManager) -Label 'bootmgr-platform-clean'
Assert-Gate ($BootmgrPlatform.ExitCode -eq 0 -and
    $BootmgrPlatform.Stdout.Contains('PASS: trusted Microsoft boot manager')) `
    'Production Microsoft boot-manager trust validation failed.'

$Validate = Invoke-GateProcess -FilePath $Validator `
    -ArgumentList @($FixtureRoot) -Label 'validate'
Assert-Gate ($Validate.ExitCode -eq 0 -and
    $Validate.Stdout.Contains('PASS: generated fixtures parse, fold')) `
    'Generated fixture validation failed.'

foreach ($TestName in $DetectorTests) {
    $TestPath = Join-Path $BuildRoot $TestName
    $Label = [IO.Path]::GetFileNameWithoutExtension($TestName)
    $Test = Invoke-GateProcess -FilePath $TestPath `
        -ArgumentList @('--fixture-gate') -Label $Label
    Assert-Gate ($Test.ExitCode -eq 0) "Attack/clean fixture test failed: $Label"
}

$Clean = Join-Path $FixtureRoot 'clean-tcglog.ib'
$Tampered = Join-Path $FixtureRoot 'tampered-digest.tcglog.ib'
$CleanResult = Invoke-GateProcess -FilePath $Audit `
    -ArgumentList @('dump', '--log', $Clean) -Label 'production-clean'
Assert-Gate ($CleanResult.ExitCode -eq 0 -and
    $CleanResult.Stdout.Contains('StructuralFindings=0 DigestFindings=0')) `
    'Production detector rejected generated clean fixture.'
$TamperedResult = Invoke-GateProcess -FilePath $Audit `
    -ArgumentList @('dump', '--log', $Tampered) -Label 'production-tampered'
Assert-Gate ($TamperedResult.ExitCode -eq 1 -and
    $TamperedResult.Stdout.Contains('DIGEST_MISMATCH EventNum=0 PCR=0')) `
    'Production detector did not classify generated tampered fixture.'

$Files = Get-ChildItem -LiteralPath $FixtureRoot -File | Sort-Object Name |
    ForEach-Object {
        [ordered]@{
            Name = $_.Name
            Size = $_.Length
            Sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    }
$Result = [ordered]@{
    SchemaVersion = 1
    RunId = $RunId
    Status = 'PASS'
    AuditSha256 = (Get-FileHash -LiteralPath $Audit -Algorithm SHA256).Hash
    Fixtures = @($Files)
    CompletedUtc = [DateTime]::UtcNow.ToString('o')
}
Write-UhrAtomicJson -Path (Join-Path $RunRoot 'result.json') -Value $Result
[pscustomobject]@{ Result = 'PASS'; RunId = $RunId; Evidence = $RunRoot }
