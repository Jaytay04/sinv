[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [Parameter(Mandatory=$true)][string]$StateRoot
)

# Retired with the loader rewrite: this gate's inputs lost their CMake
# targets when the replay loader's diagnostics and fixture generators were
# removed (handoff_fixture_gen.exe and handoff_payload_stub.efi), so a clean build cannot satisfy it.  Refuse here, before
# the core module is imported, before a gate run directory exists and before
# the missing-binary assertion, so the reason is reported instead of a stale
# binary from an older build tree being used by accident.
throw 'The installer host gate is retired: its gate binaries no longer have build targets.'

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

function New-EphemeralCodeSigningCertificate {
    param(
        [Parameter(Mandatory=$true)][string]$Subject,
        [Parameter(Mandatory=$true)][string]$CerPath,
        [string]$PfxPath,
        [string]$Password
    )
    $Rsa = [Security.Cryptography.RSA]::Create(2048)
    $Certificate = $null
    try {
        $Request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
            $Subject, $Rsa, [Security.Cryptography.HashAlgorithmName]::SHA256,
            [Security.Cryptography.RSASignaturePadding]::Pkcs1)
        $Usages = [Security.Cryptography.OidCollection]::new()
        [void]$Usages.Add([Security.Cryptography.Oid]::new(
            '1.3.6.1.5.5.7.3.3', 'Code Signing'))
        $Request.CertificateExtensions.Add(
            [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new(
                $Usages, $false))
        $Request.CertificateExtensions.Add(
            [Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
                [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature,
                $true))
        $Certificate = $Request.CreateSelfSigned(
            [DateTimeOffset]::UtcNow.AddMinutes(-5),
            [DateTimeOffset]::UtcNow.AddDays(2))
        [IO.File]::WriteAllBytes($CerPath, $Certificate.Export(
            [Security.Cryptography.X509Certificates.X509ContentType]::Cert))
        if (-not [string]::IsNullOrWhiteSpace($PfxPath)) {
            [IO.File]::WriteAllBytes($PfxPath, $Certificate.Export(
                [Security.Cryptography.X509Certificates.X509ContentType]::Pfx,
                $Password))
        }
        return $Certificate.Thumbprint
    }
    finally {
        if ($null -ne $Certificate) { $Certificate.Dispose() }
        $Rsa.Dispose()
    }
}

$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$StateRoot = [IO.Path]::GetFullPath($StateRoot)
$Suffix = [Guid]::NewGuid().ToString('N').Substring(0, 10)
$RunId = 'p6-installer-host-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') +
    '-' + $Suffix
$RunRoot = Join-Path (Join-Path $StateRoot 'installer-gates') $RunId
$Installer = Join-Path $ProjectRoot 'build\windows-static\ib-install.exe'
$Verifier = Join-Path $ProjectRoot `
    'build\windows-static\install_authenticode_test.exe'
$Generator = Join-Path $ProjectRoot 'build\windows\handoff_fixture_gen.exe'
$Loader = Join-Path $ProjectRoot 'build\windows\loader.efi'
$Payload = Join-Path $ProjectRoot 'build\windows\handoff_payload_stub.efi'
$Shim = Join-Path $ProjectRoot 'src\vendor\shimx64.efi'
$SignTool = Get-ChildItem `
    'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe' `
    -File -ErrorAction Stop | Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
$Pfx = Join-Path $RunRoot 'mok-test.pfx'
$Cer = Join-Path $RunRoot 'mok-test.cer'
$WrongCer = Join-Path $RunRoot 'wrong-signer.cer'
$Signed = Join-Path $RunRoot 'signed'
$Log = Join-Path $RunRoot 'tcglog.ib'
$EspModel = Join-Path $RunRoot 'esp-model'
$NegativeModel = Join-Path $RunRoot 'negative-model'
$WrongSignerModel = Join-Path $RunRoot 'wrong-signer-model'
$PasswordName = 'IB_INSTALL_GATE_PFX_PASSWORD'
$PasswordPlain = [Guid]::NewGuid().ToString('N')
$Thumbprint = $null
$WrongThumbprint = $null
$Succeeded = $false

New-Item -ItemType Directory -Force `
    -Path @($RunRoot, $EspModel, $NegativeModel, $WrongSignerModel) |
    Out-Null
try {
    foreach ($Path in @($Installer, $Verifier, $Generator, $Loader, $Payload, $Shim,
                         $SignTool)) {
        Assert-Gate (Test-Path -LiteralPath $Path -PathType Leaf) `
            "Required P6 gate input is missing: $Path"
    }

    $Thumbprint = New-EphemeralCodeSigningCertificate `
        -Subject "CN=UEFILoader P6 Gate $Suffix" -CerPath $Cer `
        -PfxPath $Pfx -Password $PasswordPlain
    $WrongThumbprint = New-EphemeralCodeSigningCertificate `
        -Subject "CN=UEFILoader P6 Gate Wrong $Suffix" -CerPath $WrongCer
    [Environment]::SetEnvironmentVariable($PasswordName, $PasswordPlain,
        [EnvironmentVariableTarget]::Process)

    $Generated = Invoke-GateProcess -FilePath $Generator `
        -ArgumentList @($Log) -Label 'generate-log'
    Assert-Gate ($Generated.ExitCode -eq 0 -and
        (Test-Path -LiteralPath $Log -PathType Leaf)) `
        'Reference-log fixture generation failed.'

    $Sign = Invoke-GateProcess -FilePath $Installer -ArgumentList @(
        'sign', '--signtool', $SignTool, '--pfx', $Pfx,
        '--password-env', $PasswordName, '--signer-cert', $Cer,
        '--loader', $Loader,
        '--payload', $Payload, '--out-dir', $Signed) -Label 'sign'
    Assert-Gate ($Sign.ExitCode -eq 0) 'Actual Authenticode sign gate failed.'

    $Verify = Invoke-GateProcess -FilePath $Verifier -ArgumentList @(
        (Join-Path $Signed 'loader.efi'), $Cer, $WrongCer) `
        -Label 'verify-authenticode'
    Assert-Gate ($Verify.ExitCode -eq 0) `
        'Authenticode integrity and signer-pinning verification failed.'

    $Stage = Invoke-GateProcess -FilePath $Installer -ArgumentList @(
        'stage', '--signer-cert', $Cer, '--shim', $Shim,
        '--loader', (Join-Path $Signed 'loader.efi'), '--log', $Log,
        '--payload', (Join-Path $Signed 'payload.efi'),
        '--test-root', $EspModel) -Label 'stage'
    Assert-Gate ($Stage.ExitCode -eq 0) 'Safe ESP-model staging failed.'

    $StageRoot = Join-Path $EspModel 'EFI\insecure-boot'
    $Expected = [ordered]@{
        'shimx64.efi' = $Shim
        'grubx64.efi' = Join-Path $Signed 'loader.efi'
        'tcglog.ib' = $Log
        'payload.efi' = Join-Path $Signed 'payload.efi'
    }
    foreach ($Entry in $Expected.GetEnumerator()) {
        $Actual = Join-Path $StageRoot $Entry.Key
        Assert-Gate (Test-Path -LiteralPath $Actual -PathType Leaf) `
            "Staged file missing: $($Entry.Key)"
        Assert-Gate ((Get-Item -LiteralPath $Actual).Length -eq
            (Get-Item -LiteralPath $Entry.Value).Length) `
            "Staged size mismatch: $($Entry.Key)"
        Assert-Gate ((Get-FileHash -LiteralPath $Actual -Algorithm SHA256).Hash -ceq
            (Get-FileHash -LiteralPath $Entry.Value -Algorithm SHA256).Hash) `
            "Staged hash mismatch: $($Entry.Key)"
    }
    $Manifest = Join-Path $StageRoot 'manifest.sha256'
    Assert-Gate (Test-Path -LiteralPath $Manifest -PathType Leaf) `
        'Manifest commit marker is missing.'
    foreach ($Name in $Expected.Keys) {
        Assert-Gate ((Get-Content -Raw -LiteralPath $Manifest).Contains($Name)) `
            "Manifest omits $Name."
    }

    $TamperedLoader = Join-Path $RunRoot 'tampered-loader.efi'
    Copy-Item -LiteralPath (Join-Path $Signed 'loader.efi') `
        -Destination $TamperedLoader
    $TamperedBytes = [IO.File]::ReadAllBytes($TamperedLoader)
    Assert-Gate ($TamperedBytes.Length -gt 512) `
        'Signed loader is unexpectedly small.'
    $TamperedBytes[512] = $TamperedBytes[512] -bxor 1
    [IO.File]::WriteAllBytes($TamperedLoader, $TamperedBytes)
    $Negative = Invoke-GateProcess -FilePath $Installer -ArgumentList @(
        'stage', '--signer-cert', $Cer, '--shim', $Shim,
        '--loader', $TamperedLoader, '--log', $Log,
        '--payload', (Join-Path $Signed 'payload.efi'),
        '--test-root', $NegativeModel) -Label 'reject-unsigned'
    Assert-Gate ($Negative.ExitCode -ne 0) `
        'Tampered Authenticode image was accepted by staging.'
    $NegativeEfi = Join-Path $NegativeModel 'EFI'
    Assert-Gate (-not (Test-Path -LiteralPath $NegativeEfi)) `
        'Signature failure mutated the ESP model.'

    $WrongSigner = Invoke-GateProcess -FilePath $Installer -ArgumentList @(
        'stage', '--signer-cert', $WrongCer, '--shim', $Shim,
        '--loader', (Join-Path $Signed 'loader.efi'), '--log', $Log,
        '--payload', (Join-Path $Signed 'payload.efi'),
        '--test-root', $WrongSignerModel) -Label 'reject-wrong-signer'
    Assert-Gate ($WrongSigner.ExitCode -ne 0) `
        'Image signed by a different certificate was accepted.'
    Assert-Gate (-not (Test-Path -LiteralPath `
        (Join-Path $WrongSignerModel 'EFI'))) `
        'Signer mismatch mutated the ESP model.'

    $Enroll = Invoke-GateProcess -FilePath $Installer `
        -ArgumentList @('enroll-mok') -Label 'reject-mok-write'
    Assert-Gate ($Enroll.ExitCode -ne 0 -and
        (($Enroll.Stdout + $Enroll.Stderr) -match 'MokManager') -and
        (($Enroll.Stdout + $Enroll.Stderr) -match 'MokList')) `
        'Direct MokList enrollment was not explicitly refused.'

    $SignedLoader = Join-Path $Signed 'loader.efi'
    $SignedPayload = Join-Path $Signed 'payload.efi'
    $Result = [ordered]@{
        SchemaVersion = 1
        RunId = $RunId
        Status = 'PASS'
        SignerThumbprint = $Thumbprint
        ShimSha256 = (Get-FileHash -LiteralPath $Shim -Algorithm SHA256).Hash
        LoaderSha256 = (Get-FileHash -LiteralPath $SignedLoader `
            -Algorithm SHA256).Hash
        PayloadSha256 = (Get-FileHash -LiteralPath $SignedPayload `
            -Algorithm SHA256).Hash
        ManifestSha256 = (Get-FileHash -LiteralPath $Manifest -Algorithm SHA256).Hash
        CompletedUtc = [DateTime]::UtcNow.ToString('o')
    }
    Write-UhrAtomicJson -Path (Join-Path $RunRoot 'result.json') -Value $Result
    $Succeeded = $true
    [pscustomobject]@{
        Result = 'PASS'
        RunId = $RunId
        Evidence = $RunRoot
    }
}
finally {
    [Environment]::SetEnvironmentVariable($PasswordName, $null,
        [EnvironmentVariableTarget]::Process)
    $PasswordPlain = $null
    if (Test-Path -LiteralPath $Pfx -PathType Leaf) {
        Remove-Item -LiteralPath $Pfx -Force
    }
    if (Test-Path -LiteralPath $Cer -PathType Leaf) {
        Remove-Item -LiteralPath $Cer -Force
    }
    if (Test-Path -LiteralPath $WrongCer -PathType Leaf) {
        Remove-Item -LiteralPath $WrongCer -Force
    }
    if (-not $Succeeded) {
        [IO.File]::WriteAllText((Join-Path $RunRoot 'FAILED.txt'),
            'P6 installer host gate did not complete.',
            [Text.UTF8Encoding]::new($false))
    }
}
