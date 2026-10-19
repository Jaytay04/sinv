param([switch]$BuildOnly,[switch]$ProofOnly)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($BuildOnly -and $ProofOnly) { throw 'BuildOnly and ProofOnly are mutually exclusive.' }
$clientPackageRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$clientBuildRoot = Join-Path $clientPackageRoot '.build\userspace'
$clientTempRoot = Join-Path $clientPackageRoot '.build\temporary'
foreach ($clientOwnedPath in @($clientBuildRoot,$clientTempRoot)) {
    $clientResolved = [IO.Path]::GetFullPath($clientOwnedPath)
    if (-not $clientResolved.StartsWith($clientPackageRoot.TrimEnd([char]92)+'\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Output directory is outside the package.'
    }
    $clientAncestor = $clientResolved
    while ($clientAncestor -and $clientAncestor.StartsWith($clientPackageRoot,[StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $clientAncestor) {
            if ((Get-Item -LiteralPath $clientAncestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Package output path contains a reparse point.'
            }
        }
        $clientAncestor = Split-Path -Parent $clientAncestor
    }
    New-Item -ItemType Directory -Path $clientResolved -Force | Out-Null
}
$env:TEMP = $clientTempRoot
$env:TMP = $clientTempRoot
Set-Location -LiteralPath $clientPackageRoot

function Invoke-ClientChecked([string]$Executable,[string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed (exit $LASTEXITCODE)." }
}

if (-not $ProofOnly) {
    $clientVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $clientInstallations = @()
    if (Test-Path -LiteralPath $clientVswhere) {
        $clientInstallations = @(& $clientVswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    }
    $clientInstallations += 'C:\Program Files\Microsoft Visual Studio\18\Community'
    $clientVcvars = $null
    foreach ($clientInstallation in $clientInstallations) {
        $clientCandidate = Join-Path $clientInstallation 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path -LiteralPath $clientCandidate) { $clientVcvars = $clientCandidate; break }
    }
    if (-not $clientVcvars) { throw 'The x64 Visual Studio C++ toolchain was not found.' }
    $clientEnvironment = & $env:ComSpec /d /s /c ('call "'+$clientVcvars+'" >nul && set')
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio environment setup failed.' }
    foreach ($clientLine in $clientEnvironment) {
        $clientEqual = $clientLine.IndexOf('=')
        if ($clientEqual -gt 0) {
            [Environment]::SetEnvironmentVariable($clientLine.Substring(0,$clientEqual),$clientLine.Substring($clientEqual+1),'Process')
        }
    }
    Invoke-ClientChecked 'cmake.exe' @('-S',$clientPackageRoot,'-B',$clientBuildRoot,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON')
    Invoke-ClientChecked 'cmake.exe' @('--build',$clientBuildRoot)
    & ctest.exe --test-dir $clientBuildRoot --output-on-failure | Tee-Object -FilePath (Join-Path $clientBuildRoot 'host-suites.log')
    if ($LASTEXITCODE -ne 0) { throw 'A host fixture suite failed; no live proof was started.' }
}
if ($BuildOnly) { Write-Output 'HOST FIXTURES PASS: the package-controlled live proof was not started.'; exit 0 }

$clientBaseline = Join-Path $clientPackageRoot 'reports\client-originals.tsv'
$clientProvenance = Get-Content -LiteralPath (Join-Path $clientPackageRoot 'reports\client-originals-provenance.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $clientBaseline -Algorithm SHA256).Hash -cne $clientProvenance.baselineSha256) {
    throw 'Captured-original baseline differs from its import provenance.'
}
$clientExecutable = Join-Path $clientBuildRoot 'client_identifier_host_test.exe'
if (-not (Test-Path -LiteralPath $clientExecutable -PathType Leaf)) { throw 'The package-controlled host harness is missing.' }
$clientProofPath = Join-Path $clientPackageRoot ('reports\client-host-proof.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.txt')
$clientStart = [Diagnostics.ProcessStartInfo]::new()
$clientStart.FileName = $clientExecutable
$clientStart.Arguments = '--baseline "'+$clientBaseline+'"'
$clientStart.WorkingDirectory = $clientPackageRoot
$clientStart.UseShellExecute = $false
$clientStart.CreateNoWindow = $true
$clientStart.RedirectStandardOutput = $true
$clientStart.RedirectStandardError = $true
$clientProcess = [Diagnostics.Process]::Start($clientStart)
$clientProofLines = [Collections.Generic.List[string]]::new()
try {
    while (-not $clientProcess.StandardOutput.EndOfStream) {
        $clientProofLine = $clientProcess.StandardOutput.ReadLine()
        $clientProofLines.Add($clientProofLine)
        Write-Output $clientProofLine
    }
    $clientErrorText = $clientProcess.StandardError.ReadToEnd()
    $clientProcess.WaitForExit()
    if ($clientErrorText) { $clientProofLines.Add($clientErrorText); Write-Output $clientErrorText }
    $clientProofLines.Add('HARNESS_EXIT='+$clientProcess.ExitCode)
    [IO.File]::WriteAllLines($clientProofPath,$clientProofLines,[Text.UTF8Encoding]::new($false))
    Write-Output ('PROOF REPORT '+$clientProofPath)
    if ($clientProcess.ExitCode -ne 0) { throw 'The live proof stopped at a field boundary; inspect the saved report.' }
} finally { $clientProcess.Dispose() }
