[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [Parameter(Mandatory=$true)][string]$StateRoot
)

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
$Suffix = [Guid]::NewGuid().ToString('N').Substring(0, 10)
$RunId = 'p7-installer-oneshot-host-' + `
    [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + $Suffix
$RunRoot = Join-Path (Join-Path $StateRoot 'installer-gates') $RunId
$Installer = Join-Path $ProjectRoot 'build\windows-static\ib-install.exe'
$OneShotTest = Join-Path $ProjectRoot `
    'build\windows-static\install_oneshot_test.exe'
$BootNextTest = Join-Path $ProjectRoot `
    'build\windows-static\install_bootnext_test.exe'
$BootManager = Join-Path $ProjectRoot `
    'build\windows\handoff_bootmgr_stub.efi'
$P6Gate = Join-Path $PSScriptRoot 'Invoke-InstallerHostGate.ps1'

New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null
foreach ($Path in @($Installer, $OneShotTest, $BootNextTest, $BootManager,
                     $P6Gate)) {
    Assert-Gate (Test-Path -LiteralPath $Path -PathType Leaf) `
        "Required P7 gate input is missing: $Path"
}

$OneShotUnit = Invoke-GateProcess -FilePath $OneShotTest `
    -ArgumentList @('--host-gate') -Label 'oneshot-unit'
Assert-Gate ($OneShotUnit.ExitCode -eq 0) `
    'One-shot transaction unit gate failed.'
$BootNextUnit = Invoke-GateProcess -FilePath $BootNextTest `
    -ArgumentList @('--host-gate') -Label 'bootnext-unit'
Assert-Gate ($BootNextUnit.ExitCode -eq 0) `
    'BootNext serialization/ownership unit gate failed.'

$P6 = & $P6Gate -ProjectRoot $ProjectRoot -StateRoot $StateRoot
Assert-Gate ($P6.Result -ceq 'PASS') 'P6 prerequisite gate failed.'
$Model = Join-Path ([string]$P6.Evidence) 'esp-model'
$BootDirectory = Join-Path $Model 'EFI\Microsoft\Boot'
New-Item -ItemType Directory -Force -Path $BootDirectory | Out-Null
$Target = Join-Path $BootDirectory 'bootmgfw.efi'
$Backup = Join-Path $BootDirectory 'bootmgfw.efi.bak'
$HashRecord = Join-Path $BootDirectory 'bootmgfw.sha256'
$Sibling = Join-Path $BootDirectory 'grubx64.efi'
$Shim = Join-Path $Model 'EFI\insecure-boot\shimx64.efi'
$Loader = Join-Path $Model 'EFI\insecure-boot\grubx64.efi'
Copy-Item -LiteralPath $BootManager -Destination $Target
$OriginalHash = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash

$Replace = Invoke-GateProcess -FilePath $Installer -ArgumentList @(
    'oneshot', '--mode', 'replace', '--test-root', $Model) `
    -Label 'replace'
Assert-Gate ($Replace.ExitCode -eq 0 -and
    $Replace.Stdout.Contains('SNAPSHOT REQUIRED')) `
    'One-shot replace did not complete with the snapshot reminder.'
Assert-Gate ((Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash -ceq
    (Get-FileHash -LiteralPath $Shim -Algorithm SHA256).Hash) `
    'One-shot target is not the staged shim.'
Assert-Gate ((Get-FileHash -LiteralPath $Sibling -Algorithm SHA256).Hash -ceq
    (Get-FileHash -LiteralPath $Loader -Algorithm SHA256).Hash) `
    'Sibling grubx64.efi is not the staged loader.'
Assert-Gate ((Get-FileHash -LiteralPath $Backup -Algorithm SHA256).Hash -ceq
    $OriginalHash) 'Boot-manager backup hash mismatch.'
Assert-Gate ((Get-Content -Raw -LiteralPath $HashRecord).Trim() -ceq
    $OriginalHash) 'Persistent original boot-manager hash is invalid.'

$BeforeRestore = Invoke-GateProcess -FilePath $Installer -ArgumentList @(
    'oneshot', '--mode', 'verify', '--test-root', $Model) `
    -Label 'verify-before-restore'
Assert-Gate ($BeforeRestore.ExitCode -ne 0 -and
    (($BeforeRestore.Stdout + $BeforeRestore.Stderr) -match 'BootNext fallback')) `
    'Pre-restore state did not fail closed with BootNext guidance.'

$Temporary = Join-Path $BootDirectory 'bootmgfw.efi.ibnew'
$Displaced = Join-Path $BootDirectory 'bootmgfw.efi.ibold'
Copy-Item -LiteralPath $Backup -Destination $Temporary
Move-Item -LiteralPath $Target -Destination $Displaced
Move-Item -LiteralPath $Temporary -Destination $Target
Assert-Gate ((Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash -ceq
    $OriginalHash) 'Simulated loader restore hash mismatch.'
Remove-Item -LiteralPath $Displaced,$Backup -Force

$Verify = Invoke-GateProcess -FilePath $Installer -ArgumentList @(
    'oneshot', '--mode', 'verify', '--test-root', $Model) `
    -Label 'verify-after-restore'
Assert-Gate ($Verify.ExitCode -eq 0) `
    'Post-boot restore verification failed.'
Assert-Gate (-not (Test-Path -LiteralPath $Sibling)) `
    'Owned Microsoft Boot sibling residue was not removed.'
Assert-Gate (Test-Path -LiteralPath $HashRecord -PathType Leaf) `
    'Post-boot verification removed the persistent hash record.'

$NoFirmware = Invoke-GateProcess -FilePath $Installer -ArgumentList @(
    'oneshot', '--mode', 'bootnext', '--test-root', $Model,
    '--partition-number', '1', '--partition-start', '2048',
    '--partition-size', '409600', '--partition-guid',
    '00112233-4455-6677-8899-AABBCCDDEEFF') -Label 'reject-test-firmware'
Assert-Gate ($NoFirmware.ExitCode -ne 0 -and
    (($NoFirmware.Stdout + $NoFirmware.Stderr) -match
        'Refusing firmware-variable writes')) `
    'Test-root mode did not refuse real firmware-variable mutation.'

$Result = [ordered]@{
    SchemaVersion = 1
    RunId = $RunId
    Status = 'PASS'
    P6RunId = [string]$P6.RunId
    OriginalBootManagerSha256 = $OriginalHash
    RestoredBootManagerSha256 = `
        (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash
    ShimSha256 = (Get-FileHash -LiteralPath $Shim -Algorithm SHA256).Hash
    LoaderSha256 = (Get-FileHash -LiteralPath $Loader -Algorithm SHA256).Hash
    CompletedUtc = [DateTime]::UtcNow.ToString('o')
}
Write-UhrAtomicJson -Path (Join-Path $RunRoot 'result.json') -Value $Result
[pscustomobject]@{ Result = 'PASS'; RunId = $RunId; Evidence = $RunRoot }
