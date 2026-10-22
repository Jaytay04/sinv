[CmdletBinding(SupportsShouldProcess = $true)]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# A preview must not discover tools, read the workspace, or create evidence.
if ($WhatIfPreference) {
    [pscustomobject]@{
        Action = 'BuildBootChainDiagnostic'
        Preview = $true
        Succeeded = $true
        FilesystemChangesPerformed = $false
        NativeCommandsExecuted = $false
        VmStarted = $false
        Note = 'Build, test, and package an unsigned boot-only candidate. No signing, ESP access, firmware changes, VM startup, or reboot.'
    } | ConvertTo-Json -Depth 5
    return
}
if (-not $PSCmdlet.ShouldProcess('Workspace-local boot-chain build and evidence directories', 'Build, test, and package unsigned EFI candidate')) {
    return
}

function Write-BootChainJson {
    param([string]$Path, [object]$Value)
    $json = ConvertTo-Json -InputObject $Value -Depth 12
    [IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, (New-Object Text.UTF8Encoding($false)))
}

function Get-BootChainPin {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    [pscustomobject]@{
        Path = $item.FullName
        Sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        Bytes = $item.Length
    }
}

function Invoke-BootChainNative {
    param([string]$Program, [string[]]$Arguments, [string]$Label)
    if (-not (Test-Path -LiteralPath $Program -PathType Leaf)) { throw "Native tool is unavailable: $Program" }
    $stdout = Join-Path $script:evidenceDirectory ($Label + '.stdout.txt')
    $stderr = Join-Path $script:evidenceDirectory ($Label + '.stderr.txt')
    $script:nativeCommandsExecuted = $true
    # Windows PowerShell 5 can turn redirected native stderr into an error
    # record. Use the exit code for success while retaining both output logs.
    $previousActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments 1> $stdout 2> $stderr
        $code = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousActionPreference }
    Write-BootChainJson -Path (Join-Path $script:evidenceDirectory ($Label + '.receipt.json')) -Value ([pscustomobject]@{
        Program = $Program
        Arguments = $Arguments
        ExitCode = $code
        Stdout = $stdout
        Stderr = $stderr
    })
    if ($code -ne 0) { throw "$Label failed with exit $code. See $stdout and $stderr." }
}

function Initialize-BootChainToolchain {
    # Initialize this PowerShell process only; install nothing and change no profile.
    $vswhere = Get-Command vswhere.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $vswherePath = if ($null -ne $vswhere) { $vswhere.Source } else {
        Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    }
    Invoke-BootChainNative -Program $vswherePath -Arguments @('-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath') -Label 'vswhere'
    $installation = @(Get-Content -LiteralPath (Join-Path $script:evidenceDirectory 'vswhere.stdout.txt') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($installation.Count -ne 1) { throw 'Exactly one latest Visual Studio C++ installation is required.' }
    $modulePath = Join-Path $installation[0].Trim() 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    Import-Module -Name $modulePath -ErrorAction Stop
    Enter-VsDevShell -VsInstallPath $installation[0].Trim() -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    $paths = [ordered]@{}
    foreach ($tool in @('cl.exe', 'dumpbin.exe', 'cmake.exe', 'ctest.exe', 'ninja.exe')) {
        $paths[$tool] = (Get-Command $tool -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    }
    [pscustomobject]@{ VisualStudio = $installation[0].Trim(); Tools = [pscustomobject]$paths }
}

$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidenceDirectory = $null
$buildDirectory = $null
$nativeCommandsExecuted = $false
try {
    # Keep short build paths in the shared workspace even when invoked in a worktree.
    $git = (Get-Command git.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $nativeCommandsExecuted = $true
    $commonDirectory = & $git -C $sourceRoot rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commonDirectory)) { throw 'Cannot resolve the shared Git workspace.' }
    $workspaceRoot = Split-Path -Parent ([IO.Path]::GetFullPath([string]$commonDirectory))
    $runId = [guid]::NewGuid().ToString('N')
    $evidenceDirectory = Join-Path $workspaceRoot ('.analysis\boot-chain-diagnostic\' + $runId)
    $buildDirectory = Join-Path $workspaceRoot ('.codex-build\bc\' + $runId.Substring(0, 10))
    if ((Test-Path -LiteralPath $evidenceDirectory) -or (Test-Path -LiteralPath $buildDirectory)) { throw 'Unique output directories already exist.' }
    [IO.Directory]::CreateDirectory($evidenceDirectory) | Out-Null
    [IO.Directory]::CreateDirectory($buildDirectory) | Out-Null

    Invoke-BootChainNative -Program $git -Arguments @('-C', $sourceRoot, 'rev-parse', 'HEAD') -Label 'source-head'
    Invoke-BootChainNative -Program $git -Arguments @('-C', $sourceRoot, 'status', '--porcelain', '--untracked-files=all') -Label 'source-status'
    $sourcePins = @(
        'components/uefi-loader/CMakeLists.txt',
        'components/uefi-loader/src/loader/boot_chain.c',
        'components/uefi-loader/src/loader/boot_chain.h',
        'components/uefi-loader/src/loader/boot_chain_entry.c',
        'components/uefi-loader/src/loader/boot_restore.c',
        'components/uefi-loader/src/loader/boot_restore.h',
        'components/uefi-loader/src/loader/efi_file.c',
        'components/uefi-loader/src/loader/efi_file.h',
        'components/uefi-loader/src/loader/sbat.c',
        'components/uefi-loader/src/common/sha256.c',
        'components/uefi-loader/src/common/sha256.h',
        'components/uefi-loader/src/vendor/efi_crt.c',
        'components/uefi-loader/src/vendor/Uefi.h',
        'components/uefi-loader/src/fixtures/tests/boot_chain_test.c',
        'tools/boot_chain_diagnostic/Build-BootChainDiagnostic.ps1'
    ) | ForEach-Object { Get-BootChainPin -Path (Join-Path $sourceRoot $_) }
    $toolchain = Initialize-BootChainToolchain
    Write-BootChainJson -Path (Join-Path $evidenceDirectory 'toolchain.json') -Value $toolchain

    Invoke-BootChainNative -Program $toolchain.Tools.'cmake.exe' -Arguments @(
        '-S', (Join-Path $sourceRoot 'components\uefi-loader'), '-B', $buildDirectory,
        '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DIB_STATIC_RUNTIME=ON',
        '-DIB_BUILD_BOOT_CHAIN_LOADER=ON', '-DIB_QEMU_DEBUG_PORTS=OFF',
        '-DIB_PAYLOAD_REPLAY_INDEPENDENT=OFF', '-DIB_FIRMWARE_STORAGE_PROBE=OFF'
    ) -Label 'configure'
    # All host fixtures are required by the component CTest registration.
    Invoke-BootChainNative -Program $toolchain.Tools.'cmake.exe' -Arguments @('--build', $buildDirectory, '--parallel', '4') -Label 'build'
    Invoke-BootChainNative -Program $toolchain.Tools.'ctest.exe' -Arguments @('--test-dir', $buildDirectory, '--output-on-failure', '--no-tests=error') -Label 'ctest'

    $artifact = Join-Path $buildDirectory 'boot_chain_loader.efi'
    Invoke-BootChainNative -Program $toolchain.Tools.'dumpbin.exe' -Arguments @('/headers', $artifact) -Label 'efi-headers'
    $packageDirectory = Join-Path $evidenceDirectory 'package'
    [IO.Directory]::CreateDirectory($packageDirectory) | Out-Null
    $packagedArtifact = Join-Path $packageDirectory 'boot_chain_loader.efi'
    Copy-Item -LiteralPath $artifact -Destination $packagedArtifact -ErrorAction Stop
    $artifactPin = Get-BootChainPin -Path $artifact
    $packagePin = Get-BootChainPin -Path $packagedArtifact
    if ($artifactPin.Sha256 -ne $packagePin.Sha256 -or $artifactPin.Bytes -ne $packagePin.Bytes) { throw 'Packaged artifact differs from the build.' }
    foreach ($sourcePin in $sourcePins) {
        $currentPin = Get-BootChainPin -Path $sourcePin.Path
        if ($currentPin.Sha256 -ne $sourcePin.Sha256 -or $currentPin.Bytes -ne $sourcePin.Bytes) {
            throw "Source changed during build/test: $($sourcePin.Path)"
        }
    }
    $result = [pscustomobject]@{
        SchemaVersion = 1
        Action = 'BuildBootChainDiagnostic'
        Status = 'BUILD_AND_TESTS_PASS_UNSIGNED_NOT_BOOT_VALIDATED'
        Succeeded = $true
        GeneratedUtc = [DateTime]::UtcNow.ToString('o')
        SourceRoot = $sourceRoot
        SourceCommit = (Get-Content -LiteralPath (Join-Path $evidenceDirectory 'source-head.stdout.txt') -Raw).Trim()
        SourceDirty = -not [string]::IsNullOrWhiteSpace((Get-Content -LiteralPath (Join-Path $evidenceDirectory 'source-status.stdout.txt') -Raw))
        SourcePins = @($sourcePins)
        BuildDirectory = $buildDirectory
        EvidenceDirectory = $evidenceDirectory
        Artifact = $packagePin
        BootMode = 'BOOT_CHAIN_ONLY'
        Signature = 'UNSIGNED'
        Replay = 'NOT_REQUESTED'
        Payload = 'NOT_REQUESTED'
        BootValidated = $false
        NativeCommandsExecuted = $nativeCommandsExecuted
        EspAccessed = $false
        FirmwareChanged = $false
        VmStarted = $false
        RebootPerformed = $false
        NextStep = 'Review and independently sign/verify the candidate before a separately authorized disposable-VM replacement/restore test. No deployment is performed by this script.'
    }
    Write-BootChainJson -Path (Join-Path $evidenceDirectory 'result.json') -Value $result
    $result | ConvertTo-Json -Depth 12
}
catch {
    $failure = [pscustomobject]@{
        Action = 'BuildBootChainDiagnostic'
        Status = 'BUILD_OR_TEST_FAILED'
        Succeeded = $false
        Error = $_.Exception.Message
        BuildDirectory = $buildDirectory
        EvidenceDirectory = $evidenceDirectory
        NativeCommandsExecuted = $nativeCommandsExecuted
        EspAccessed = $false
        FirmwareChanged = $false
        VmStarted = $false
        RebootPerformed = $false
    }
    if ($null -ne $evidenceDirectory -and (Test-Path -LiteralPath $evidenceDirectory -PathType Container)) {
        Write-BootChainJson -Path (Join-Path $evidenceDirectory 'failure.json') -Value $failure
    }
    $failure | ConvertTo-Json -Depth 5
    exit 1
}
