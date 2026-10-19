param(
    [ValidateSet('Build','Sign','Stage','Status','Arm','Collect','Disarm')]
    [string]$Mode = 'Status',
    [string]$LoaderBuildDir = '.codex-build/efi_shim_loader',
    [string]$InventoryBuildDir = '.codex-build/efi_inventory',
    [string]$SignedDirectory = '.codex-build/efi_inventory/shim-signed',
    [ValidatePattern('^[A-Za-z]$')]
    [string]$MountLetter = 'S',
    [string]$TestRoot,
    [switch]$LibraryOnly
)

# Preserve caller options before loading the common build, mount, and path helpers.
$shimMode = $Mode
$shimLoaderBuildDir = $LoaderBuildDir
$shimInventoryBuildDir = $InventoryBuildDir
$shimSignedDirectory = $SignedDirectory
$shimMountLetter = $MountLetter
$shimTestRoot = $TestRoot
$shimLibraryOnly = $LibraryOnly
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$pathB = Join-Path $PSScriptRoot 'path-b-collector.ps1'
. $pathB -Mode Status -LoaderBuildDir $shimLoaderBuildDir `
    -InventoryBuildDir $shimInventoryBuildDir -MountLetter $shimMountLetter -LibraryOnly

$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$script:LoaderBuild = Resolve-RepoPath $shimLoaderBuildDir
$script:InventoryBuild = Resolve-RepoPath $shimInventoryBuildDir
$script:SignedDirectory = Resolve-RepoPath $shimSignedDirectory
$script:MountLetter = $shimMountLetter.ToUpperInvariant()
$script:ShimReceiptPath = Join-Path $script:InventoryBuild 'shim-chain-stage.json'
$script:ShimDirectoryName = 'HypersinvShimInventory'
$script:ShimStageNames = @('shimx64.efi','grubx64.efi','inventory.efi','mmx64.efi','MOK.cer')
$script:ShimOwnedNames = @($script:ShimStageNames + 'manifest.sha256')
$script:ShimStatusName = 'chain-status.txt'
$script:ShimReportName = 'HypersinvEfiInventory.txt'
$script:ShimCertThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
$script:ShimHash = '6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B'
$script:MokManagerHash = 'D2FA8E52FDDC99DAD94A0009FEE23CB2478C28373B777D50B2F784EB4E96F88E'
$script:ShimPackageHash = 'BA9B5D80E5D886C30664F2BEBFB5C2FCCE3B9B40F16FC46CBA49C19A91C8059C'
$script:ShimPackageUrl = 'https://archive.ubuntu.com/ubuntu/pool/main/s/shim-signed/shim-signed_1.58+15.8-0ubuntu1_amd64.deb'
$script:BootManagerRelative = 'EFI\Microsoft\Boot\bootmgfw.efi'

function Get-ShimSourcePaths {
    $loader = Join-Path $script:LoaderBuild 'loader.efi'
    $collector = Join-Path (Join-Path $script:InventoryBuild 'efi') 'inventory.efi'
    $shim = Join-Path (Join-Path $script:RepoRoot 'components\uefi-loader\src\vendor') 'shimx64.efi'
    $certificate = Join-Path (Join-Path $script:RepoRoot 'components\hypervisor') 'hypersinv.cer'
    $mokManager = Join-Path (Join-Path (Join-Path $script:InventoryBuild 'shim-packages') 'usr\lib\shim') 'mmx64.efi'
    return @{ Loader = $loader; Collector = $collector; Shim = $shim; Certificate = $certificate; MokManager = $mokManager }
}

function Get-FileRecord([string]$Name, [string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or (Test-ReparsePoint $Path)) {
        throw "Missing or unsafe shim-chain file: $Path"
    }
    return [pscustomobject]@{
        Name = $Name
        Path = [IO.Path]::GetFullPath($Path)
        Size = [long](Get-Item -LiteralPath $Path).Length
        Hash = Get-Sha256 $Path
    }
}

function Get-CertificateThumbprint([string]$Path) {
    try {
        $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes($Path))
        return $certificate.Thumbprint.Replace(' ','').ToUpperInvariant()
    } catch {
        throw "Could not read public MOK certificate DER: $Path. $($_.Exception.Message)"
    }
}

function Get-AuthenticodeVerifier {
    $verifier = Join-Path $script:LoaderBuild 'install_authenticode_test.exe'
    if (-not (Test-Path -LiteralPath $verifier -PathType Leaf)) {
        throw "Missing Authenticode signer verifier: $verifier. Run Build first."
    }
    return $verifier
}

function Assert-MokAuthenticode([string]$Image, [string]$Certificate) {
    Assert-EfiApplication $Image
    $verifier = Get-AuthenticodeVerifier
    $output = @(& $verifier $Image $Certificate 2>&1)
    if ($LASTEXITCODE -ne 0) {
        $message = ($output | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
        throw "MOK Authenticode verification failed for $Image. $message"
    }
}

function Get-Signtool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $roots = @(
        [Environment]::GetEnvironmentVariable('WindowsSdkDir'),
        [Environment]::GetEnvironmentVariable('WindowsSdkBinPath'),
        [IO.Path]::Combine([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86),'Windows Kits\10\bin'),
        [IO.Path]::Combine([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles),'Windows Kits\10\bin')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } | Select-Object -Unique
    foreach ($root in $roots) {
        $direct = Join-Path $root 'x64\signtool.exe'
        if (Test-Path -LiteralPath $direct -PathType Leaf) { return $direct }
        foreach ($version in (Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending)) {
            $candidate = Join-Path $version.FullName 'x64\signtool.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }
    throw 'Windows SDK signtool.exe was not found on PATH or in the Windows Kits 10 SDK.'
}

function Assert-CurrentUserSigningCertificate([string]$CertificatePath) {
    if ((Get-CertificateThumbprint $CertificatePath) -cne $script:ShimCertThumbprint) {
        throw "The public MOK certificate does not match the pinned signing certificate thumbprint $($script:ShimCertThumbprint)."
    }
    $matches = @(Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' -ErrorAction Stop |
        Where-Object { $_.Thumbprint.Replace(' ','').ToUpperInvariant() -ceq $script:ShimCertThumbprint })
    if ($matches.Count -ne 1 -or -not $matches[0].HasPrivateKey) {
        throw 'The matching certificate with its private key must already exist in CurrentUser\My. The script will not import keys or change trust stores.'
    }
    if ($matches[0].NotAfter.ToUniversalTime() -le [DateTime]::UtcNow) {
        throw 'The CurrentUser signing certificate has expired.'
    }
    if ($matches[0].PublicKey.Oid.Value -cne '1.2.840.113549.1.1.1') {
        throw 'The MOK signing certificate must use an RSA public key.'
    }
    return $matches[0]
}

function Read-ArDataArchive([string]$PackagePath, [string]$Destination) {
    $bytes = [IO.File]::ReadAllBytes($PackagePath)
    if ($bytes.Length -lt 8 -or [Text.Encoding]::ASCII.GetString($bytes,0,8) -cne "!<arch>`n") {
        throw 'Pinned shim package is not a valid ar archive.'
    }
    $offset = 8
    $found = $false
    while ($offset -lt $bytes.Length) {
        $arTerminator = [string][char]96 + [char]10
        if (($bytes.Length - $offset) -lt 60 -or [Text.Encoding]::ASCII.GetString($bytes,$offset + 58,2) -cne $arTerminator) {
            throw 'Pinned shim package contains a malformed ar member header.'
        }
        $name = [Text.Encoding]::ASCII.GetString($bytes,$offset,16).Trim().TrimEnd([char]'/')
        $sizeText = [Text.Encoding]::ASCII.GetString($bytes,$offset + 48,10).Trim()
        $memberSize = 0L
        if (-not [long]::TryParse($sizeText,[ref]$memberSize) -or $memberSize -lt 0 -or
            $memberSize -gt 64MB -or ($bytes.Length - ($offset + 60)) -lt $memberSize) {
            throw 'Pinned shim package has an invalid or oversized ar member.'
        }
        $dataOffset = $offset + 60
        if ($name -ceq 'data.tar.xz') {
            $data = New-Object byte[] ([int]$memberSize)
            [Array]::Copy($bytes,$dataOffset,$data,0,$data.Length)
            [IO.File]::WriteAllBytes($Destination,$data)
            $found = $true
            break
        }
        $offset = $dataOffset + [int]$memberSize
        if (($memberSize % 2) -ne 0) { $offset++ }
    }
    if (-not $found) { throw 'Pinned shim package has no data.tar.xz member.' }
}

function Ensure-PinnedMokManager {
    $paths = Get-ShimSourcePaths
    $cacheDirectory = Split-Path -Parent $paths.MokManager
    Assert-NoReparseAncestors $cacheDirectory $script:InventoryBuild
    New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null
    Assert-NoReparseAncestors $cacheDirectory $script:InventoryBuild
    if (Test-Path -LiteralPath $paths.MokManager -PathType Leaf) {
        if ((Get-Sha256 $paths.MokManager) -cne $script:MokManagerHash) {
            throw "Cached mmx64.efi does not match the pinned Ubuntu image: $($paths.MokManager)"
        }
        return $paths.MokManager
    }
    if (Test-Path -LiteralPath $paths.MokManager) { throw 'Cached mmx64.efi exists but is not a regular file.' }

    $packageDirectory = Join-Path $script:InventoryBuild 'shim-packages'
    $packagePath = Join-Path $packageDirectory 'shim-signed_1.58+15.8-0ubuntu1_amd64.deb'
    $tempRoot = Join-Path $packageDirectory ('.extract-' + [Guid]::NewGuid().ToString('N'))
    Assert-NoReparseAncestors $packagePath $script:InventoryBuild
    if (Test-Path -LiteralPath $packagePath -PathType Leaf) {
        if ((Get-Sha256 $packagePath) -cne $script:ShimPackageHash) {
            throw "Cached Ubuntu shim package does not match its pinned SHA-256: $packagePath"
        }
    } else {
        $download = $packagePath + '.' + [Guid]::NewGuid().ToString('N') + '.download'
        try {
            Invoke-WebRequest -Uri $script:ShimPackageUrl -OutFile $download -UseBasicParsing
            if ((Get-Sha256 $download) -cne $script:ShimPackageHash) {
                throw 'Downloaded Ubuntu shim package failed its pinned SHA-256 check.'
            }
            Move-Item -LiteralPath $download -Destination $packagePath
        } finally {
            if (Test-Path -LiteralPath $download -PathType Leaf) { Remove-Item -LiteralPath $download -Force }
        }
    }

    Assert-NoReparseAncestors $tempRoot $script:InventoryBuild
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    try {
        $dataTar = Join-Path $tempRoot 'data.tar.xz'
        Read-ArDataArchive $packagePath $dataTar
        $tar = Get-Command tar.exe -ErrorAction SilentlyContinue
        if (-not $tar) { throw 'Windows tar.exe is required to extract the pinned mmx64.efi package member.' }
        $listing = @(& $tar.Source '-tf' $dataTar)
        if ($LASTEXITCODE -ne 0) { throw 'Could not list the pinned shim package data archive.' }
        $member = $null
        foreach ($candidate in $listing) {
            $value = ([string]$candidate).Trim().Replace([char]92,[char]47)
            if ($value.StartsWith('/') -or $value -match '(^|/)\.\.(?:/|$)') { throw 'Pinned shim package contains an unsafe archive path.' }
            $normalized = $value.TrimStart([char]'.',[char]'/')
            if ($normalized -ceq 'usr/lib/shim/mmx64.efi') {
                if ($member) { throw 'Pinned shim package repeats the expected MokManager member.' }
                $member = $value
            }
        }
        if (-not $member) { throw 'Pinned shim package data archive has no usr/lib/shim/mmx64.efi member.' }
        $extractRoot = Join-Path $tempRoot 'extract'
        New-Item -ItemType Directory -Path $extractRoot | Out-Null
        & $tar.Source '-xf' $dataTar '-C' $extractRoot $member
        if ($LASTEXITCODE -ne 0) { throw 'Could not extract the pinned MokManager archive member.' }
        $relative = $member.TrimStart([char]'.',[char]'/').Replace('/',[IO.Path]::DirectorySeparatorChar)
        $extracted = Join-Path $extractRoot $relative
        if (-not (Test-Path -LiteralPath $extracted -PathType Leaf) -or (Test-ReparsePoint $extracted)) {
            throw 'Pinned MokManager extraction did not produce a regular file.'
        }
        Assert-EfiApplication $extracted
        if ((Get-Sha256 $extracted) -cne $script:MokManagerHash) { throw 'Extracted MokManager failed its pinned SHA-256 check.' }
        $temporaryManager = $paths.MokManager + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
        Copy-Item -LiteralPath $extracted -Destination $temporaryManager
        if ((Get-Sha256 $temporaryManager) -cne $script:MokManagerHash) { throw 'Cached MokManager copy failed its SHA-256 read-back.' }
        Move-Item -LiteralPath $temporaryManager -Destination $paths.MokManager
        return $paths.MokManager
    } finally {
        if (Test-Path -LiteralPath $tempRoot -PathType Container) {
            Assert-NoReparseAncestors $tempRoot $packageDirectory
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

function Get-SignedDirectoryNames {
    return @('grubx64.efi','inventory.efi','MOK.cer','signed-source.json')
}

function Assert-ExactRegularDirectory([string]$Path, [string[]]$Names, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container) -or (Test-ReparsePoint $Path)) {
        throw "$Description is missing or unsafe: $Path"
    }
    Assert-NoReparseAncestors $Path ([IO.Path]::GetPathRoot($Path))
    $actual = @(Get-ChildItem -LiteralPath $Path -Force)
    if ($actual.Count -ne $Names.Count) { throw "$Description contains an unexpected or missing entry." }
    foreach ($item in $actual) {
        if ($Names -notcontains $item.Name -or $item.PSIsContainer -or (Test-ReparsePoint $item.FullName)) {
            throw "$Description contains unowned or unsafe content: $($item.FullName)"
        }
    }
    foreach ($name in $Names) {
        if (-not (Test-Path -LiteralPath (Join-Path $Path $name) -PathType Leaf)) {
            throw "$Description is missing $name."
        }
    }
}

function Read-SignedSource([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or (Test-ReparsePoint $Path)) {
        throw "Missing or unsafe signed-source receipt: $Path"
    }
    try { return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json) }
    catch { throw "Invalid signed-source receipt: $Path. $($_.Exception.Message)" }
}

function Assert-SignedDirectory {
    Assert-ExactRegularDirectory $script:SignedDirectory (Get-SignedDirectoryNames) 'Local signed artifact directory'
    $paths = Get-ShimSourcePaths
    foreach ($path in @($paths.Loader,$paths.Collector,$paths.Shim,$paths.Certificate)) {
        Assert-NoReparseAncestors $path ([IO.Path]::GetPathRoot($path))
    }
    Assert-EfiApplication $paths.Loader
    Assert-EfiApplication $paths.Collector
    Assert-EfiApplication $paths.Shim
    Assert-EfiApplication $paths.MokManager
    if ((Get-Sha256 $paths.Shim) -cne $script:ShimHash) { throw 'Vendored shimx64.efi does not match its pinned SHA-256.' }
    if ((Get-Sha256 $paths.MokManager) -cne $script:MokManagerHash) { throw 'Cached mmx64.efi does not match its pinned SHA-256.' }
    if ((Get-CertificateThumbprint $paths.Certificate) -cne $script:ShimCertThumbprint) {
        throw 'The repository public MOK certificate does not match the pinned thumbprint.'
    }

    $signedLoader = Join-Path $script:SignedDirectory 'grubx64.efi'
    $signedCollector = Join-Path $script:SignedDirectory 'inventory.efi'
    $signedCertificate = Join-Path $script:SignedDirectory 'MOK.cer'
    if ((Get-Sha256 $signedCertificate) -cne (Get-Sha256 $paths.Certificate)) {
        throw 'Local MOK.cer differs from the repository public certificate.'
    }
    $metadata = Read-SignedSource (Join-Path $script:SignedDirectory 'signed-source.json')
    $expected = @{
        schema = 1
        certificateThumbprint = $script:ShimCertThumbprint
        certificateSha256 = Get-Sha256 $signedCertificate
        shimSha256 = $script:ShimHash
        mokManagerSha256 = $script:MokManagerHash
        unsignedLoaderSha256 = Get-Sha256 $paths.Loader
        unsignedInventorySha256 = Get-Sha256 $paths.Collector
        grubx64Sha256 = Get-Sha256 $signedLoader
        inventorySha256 = Get-Sha256 $signedCollector
    }
    foreach ($name in $expected.Keys) {
        if ([string]$metadata.$name -cne [string]$expected[$name]) {
            throw "Signed artifact receipt differs from current input: $name. Run Sign again."
        }
    }
    Assert-MokAuthenticode $signedLoader $signedCertificate
    Assert-MokAuthenticode $signedCollector $signedCertificate
    return @{ Paths = $paths; SignedLoader = $signedLoader; SignedCollector = $signedCollector; Certificate = $signedCertificate }
}

function New-SignedSourceJson([hashtable]$Values) {
    $record = [ordered]@{
        schema = 1
        certificateThumbprint = $Values.CertificateThumbprint
        certificateSha256 = $Values.CertificateSha256
        shimSha256 = $Values.ShimSha256
        mokManagerSha256 = $Values.MokManagerSha256
        unsignedLoaderSha256 = $Values.UnsignedLoaderSha256
        unsignedInventorySha256 = $Values.UnsignedInventorySha256
        grubx64Sha256 = $Values.GrubSha256
        inventorySha256 = $Values.InventorySha256
    }
    return ($record | ConvertTo-Json -Depth 3) + [Environment]::NewLine
}

function Assert-SignedDirectoryTarget {
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $script:RepoRoot '.codex-build')).TrimEnd([char]92) + [IO.Path]::DirectorySeparatorChar
    if (-not $script:SignedDirectory.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)) {
        throw "SignedDirectory must remain under .codex-build: $($script:SignedDirectory)"
    }
    Assert-NoReparseAncestors $script:SignedDirectory $buildRoot.TrimEnd([char]92)
}

function Save-SignedDirectory([string]$PreparedDirectory) {
    Assert-SignedDirectoryTarget
    $parent = Split-Path -Parent $script:SignedDirectory
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    Assert-NoReparseAncestors $parent ([IO.Path]::GetPathRoot($parent))
    $backup = $null
    if (Test-Path -LiteralPath $script:SignedDirectory) {
        Assert-ExactRegularDirectory $script:SignedDirectory (Get-SignedDirectoryNames) 'Existing local signed artifact directory'
        $backup = Join-Path $parent ('shim-signed-backup.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N'))
        Move-Item -LiteralPath $script:SignedDirectory -Destination $backup
    }
    try { Move-Item -LiteralPath $PreparedDirectory -Destination $script:SignedDirectory }
    catch {
        if ($backup -and (Test-Path -LiteralPath $backup -PathType Container) -and
            -not (Test-Path -LiteralPath $script:SignedDirectory)) {
            Move-Item -LiteralPath $backup -Destination $script:SignedDirectory
        }
        throw
    }
    if ($backup) { Write-Output "PRESERVED previous signed artifacts at $backup" }
}

function Invoke-Sign {
    Assert-SignedDirectoryTarget
    $inputs = Get-ShimSourcePaths
    foreach ($path in @($inputs.Loader,$inputs.Collector,$inputs.Shim,$inputs.Certificate)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing signing input: $path. Run Build first." }
        Assert-NoReparseAncestors $path ([IO.Path]::GetPathRoot($path))
    }
    Assert-EfiApplication $inputs.Loader
    Assert-EfiApplication $inputs.Collector
    Assert-EfiApplication $inputs.Shim
    if ((Get-Sha256 $inputs.Shim) -cne $script:ShimHash) { throw 'Vendored shimx64.efi does not match its pinned SHA-256.' }
    $null = Ensure-PinnedMokManager
    $signingCertificate = Assert-CurrentUserSigningCertificate $inputs.Certificate
    $certificateHash = Get-Sha256 $inputs.Certificate
    $unsignedLoaderHash = Get-Sha256 $inputs.Loader
    $unsignedCollectorHash = Get-Sha256 $inputs.Collector
    $shimHash = Get-Sha256 $inputs.Shim
    $mokManagerHash = Get-Sha256 $inputs.MokManager
    if ($mokManagerHash -cne $script:MokManagerHash) { throw 'Pinned MokManager cache failed verification.' }
    $signTool = Get-Signtool
    $parent = Split-Path -Parent $script:SignedDirectory
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    Assert-NoReparseAncestors $parent ([IO.Path]::GetPathRoot($parent))
    $prepared = Join-Path $parent ('shim-signed.prepare-' + [Guid]::NewGuid().ToString('N'))
    Assert-NoReparseAncestors $prepared ([IO.Path]::GetPathRoot($prepared))
    New-Item -ItemType Directory -Path $prepared | Out-Null
    try {
        $signedLoader = Join-Path $prepared 'grubx64.efi'
        $signedCollector = Join-Path $prepared 'inventory.efi'
        $signedCertificate = Join-Path $prepared 'MOK.cer'
        Copy-Item -LiteralPath $inputs.Loader -Destination $signedLoader
        Copy-Item -LiteralPath $inputs.Collector -Destination $signedCollector
        Copy-Item -LiteralPath $inputs.Certificate -Destination $signedCertificate
        foreach ($image in @($signedLoader,$signedCollector)) {
            & $signTool sign '/s' 'My' '/sha1' $script:ShimCertThumbprint '/fd' 'SHA256' $image
            if ($LASTEXITCODE -ne 0) { throw "signtool failed while signing $image (exit $LASTEXITCODE)." }
        }
        if ((Get-Sha256 $inputs.Loader) -cne $unsignedLoaderHash -or
            (Get-Sha256 $inputs.Collector) -cne $unsignedCollectorHash -or
            (Get-Sha256 $inputs.Shim) -cne $shimHash -or
            (Get-Sha256 $inputs.MokManager) -cne $mokManagerHash) {
            throw 'A signing source changed while signing; prepared output was discarded.'
        }
        Assert-MokAuthenticode $signedLoader $signedCertificate
        Assert-MokAuthenticode $signedCollector $signedCertificate
        $values = @{
            CertificateThumbprint = $signingCertificate.Thumbprint.Replace(' ','').ToUpperInvariant()
            CertificateSha256 = $certificateHash
            ShimSha256 = $shimHash
            MokManagerSha256 = $mokManagerHash
            UnsignedLoaderSha256 = $unsignedLoaderHash
            UnsignedInventorySha256 = $unsignedCollectorHash
            GrubSha256 = Get-Sha256 $signedLoader
            InventorySha256 = Get-Sha256 $signedCollector
        }
        [IO.File]::WriteAllText((Join-Path $prepared 'signed-source.json'),(New-SignedSourceJson $values),[Text.Encoding]::UTF8)
        Assert-ExactRegularDirectory $prepared (Get-SignedDirectoryNames) 'Prepared signed artifact directory'
        Save-SignedDirectory $prepared
        Write-Output "SIGNED AND VERIFIED $script:SignedDirectory"
        Write-Output "  certificate thumbprint=$($values.CertificateThumbprint)"
        Write-Output "  grubx64.efi sha256=$($values.GrubSha256)"
        Write-Output "  inventory.efi sha256=$($values.InventorySha256)"
    } catch {
        $signFailure = $_.Exception.Message
        if (Test-Path -LiteralPath $prepared -PathType Container) {
            try { Remove-PreparedSignedDirectory $prepared }
            catch { throw "Signing failed: $signFailure. Prepared output was preserved because cleanup could not prove ownership: $($_.Exception.Message)" }
        }
        throw
    }
}

function Remove-PreparedSignedDirectory([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $expectedParent = [IO.Path]::GetFullPath((Split-Path -Parent $script:SignedDirectory))
    $leaf = [IO.Path]::GetFileName($full)
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $script:RepoRoot '.codex-build')).TrimEnd([char]92)
    if (-not [string]::Equals([IO.Path]::GetDirectoryName($full),$expectedParent,[StringComparison]::OrdinalIgnoreCase) -or
        $leaf -notmatch '^shim-signed\.prepare-[0-9a-f]{32}$' -or
        -not $full.StartsWith(($buildRoot + [IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)) {
        throw "Prepared signed directory is outside its generated transaction location: $full"
    }
    if (-not (Test-Path -LiteralPath $full -PathType Container) -or (Test-ReparsePoint $full)) {
        throw "Prepared signed directory is missing or unsafe: $full"
    }
    Assert-NoReparseAncestors $full $buildRoot
    $allowed = Get-SignedDirectoryNames
    foreach ($item in @(Get-ChildItem -LiteralPath $full -Force)) {
        if ($allowed -notcontains $item.Name -or $item.PSIsContainer -or (Test-ReparsePoint $item.FullName)) {
            throw "Prepared signed directory contains unowned or unsafe content: $($item.FullName)"
        }
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $full -Force)) { Remove-Item -LiteralPath $item.FullName -Force }
    Remove-Item -LiteralPath $full -Force
}

function Get-ShimStageSources {
    $signed = Assert-SignedDirectory
    $paths = $signed.Paths
    $filePaths = @(
        @{ Name='shimx64.efi'; Path=$paths.Shim },
        @{ Name='grubx64.efi'; Path=$signed.SignedLoader },
        @{ Name='inventory.efi'; Path=$signed.SignedCollector },
        @{ Name='mmx64.efi'; Path=$paths.MokManager },
        @{ Name='MOK.cer'; Path=$signed.Certificate }
    )
    $files = @()
    foreach ($item in $filePaths) { $files += Get-FileRecord $item.Name $item.Path }
    return @{ Files=$files; LoaderHash=$files[1].Hash; CollectorHash=$files[2].Hash; CertificateThumbprint=$script:ShimCertThumbprint }
}

function New-ShimManifest([hashtable]$Sources) {
    $nl = [string][char]13 + [char]10
    $manifest = 'schema=1' + $nl
    foreach ($file in $Sources.Files) { $manifest += ('{0} size={1} sha256={2}' -f $file.Name,$file.Size,$file.Hash) + $nl }
    return $manifest
}

function Assert-ShimStageNames([string]$StageRoot, [switch]$AllowStatus) {
    if (-not (Test-Path -LiteralPath $StageRoot)) { return }
    if (-not (Test-Path -LiteralPath $StageRoot -PathType Container) -or (Test-ReparsePoint $StageRoot)) {
        throw "Shim stage directory is missing or unsafe: $StageRoot"
    }
    Assert-NoReparseAncestors $StageRoot ([IO.Path]::GetPathRoot($StageRoot))
    $allowed = if ($AllowStatus) { @($script:ShimOwnedNames + $script:ShimStatusName) } else { $script:ShimOwnedNames }
    foreach ($item in @(Get-ChildItem -LiteralPath $StageRoot -Force)) {
        if ($allowed -notcontains $item.Name -or $item.PSIsContainer -or (Test-ReparsePoint $item.FullName)) {
            throw "Unowned or unsafe shim stage entry: $($item.FullName)"
        }
    }
}

function Assert-ShimStageFiles([string]$StageRoot, [hashtable]$Sources, [switch]$AllowStatus) {
    Assert-ShimStageNames $StageRoot -AllowStatus:$AllowStatus
    $expectedNames = @($script:ShimOwnedNames | Sort-Object)
    if ($AllowStatus -and (Test-Path -LiteralPath (Join-Path $StageRoot $script:ShimStatusName) -PathType Leaf)) {
        $expectedNames += $script:ShimStatusName
        $expectedNames = @($expectedNames | Sort-Object)
    }
    $actualNames = @(Get-ChildItem -LiteralPath $StageRoot -Force | Select-Object -ExpandProperty Name | Sort-Object)
    if (($actualNames -join "`n") -cne ($expectedNames -join "`n")) { throw 'Shim stage does not contain exactly the five pinned images/certificate and manifest.' }
    foreach ($file in $Sources.Files) {
        $path = Join-Path $StageRoot $file.Name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Test-ReparsePoint $path) -or
            (Get-Item -LiteralPath $path).Length -ne $file.Size -or (Get-Sha256 $path) -cne $file.Hash) {
            throw "Staged shim-chain file differs from its source: $($file.Name)"
        }
        if ($file.Name -in @('shimx64.efi','grubx64.efi','inventory.efi','mmx64.efi')) { Assert-EfiApplication $path }
    }
    $certificate = Join-Path $StageRoot 'MOK.cer'
    if ((Get-CertificateThumbprint $certificate) -cne $script:ShimCertThumbprint) { throw 'Staged MOK.cer does not match the pinned public certificate.' }
    Assert-MokAuthenticode (Join-Path $StageRoot 'grubx64.efi') $certificate
    Assert-MokAuthenticode (Join-Path $StageRoot 'inventory.efi') $certificate
    $actualManifest = [IO.File]::ReadAllText((Join-Path $StageRoot 'manifest.sha256'),[Text.Encoding]::ASCII)
    if ($actualManifest -cne (New-ShimManifest $Sources)) { throw 'Staged shim-chain manifest does not match the exact schema=1 format.' }
}

function Get-ExistingShimStageSources([string]$StageRoot) {
    Assert-ShimStageNames $StageRoot
    $records = @()
    foreach ($name in $script:ShimStageNames) { $records += Get-FileRecord $name (Join-Path $StageRoot $name) }
    if ($records[0].Hash -cne $script:ShimHash -or $records[3].Hash -cne $script:MokManagerHash) {
        throw 'Existing shim stage does not contain the pinned shim and MokManager images.'
    }
    if ((Get-CertificateThumbprint (Join-Path $StageRoot 'MOK.cer')) -cne $script:ShimCertThumbprint) {
        throw 'Existing shim stage MOK.cer does not match the pinned public certificate.'
    }
    foreach ($name in @('shimx64.efi','grubx64.efi','inventory.efi','mmx64.efi')) {
        Assert-EfiApplication (Join-Path $StageRoot $name)
    }
    Assert-MokAuthenticode (Join-Path $StageRoot 'grubx64.efi') (Join-Path $StageRoot 'MOK.cer')
    Assert-MokAuthenticode (Join-Path $StageRoot 'inventory.efi') (Join-Path $StageRoot 'MOK.cer')
    $sources = @{ Files=$records }
    $manifest = [IO.File]::ReadAllText((Join-Path $StageRoot 'manifest.sha256'),[Text.Encoding]::ASCII)
    if ($manifest -cne (New-ShimManifest $sources)) { throw 'Existing shim stage is not a wholly owned schema=1 directory.' }
    return $sources
}

function Assert-ShimStageDirectory([string]$EspRoot, [switch]$AllowArtifacts) {
    $efiRoot = Join-Path $EspRoot 'EFI'
    $stageRoot = Join-Path $efiRoot $script:ShimDirectoryName
    Assert-NoReparseAncestors $efiRoot $EspRoot
    if ((Test-Path -LiteralPath $efiRoot) -and -not (Test-Path -LiteralPath $efiRoot -PathType Container)) {
        throw 'ESP EFI path exists but is not a directory.'
    }
    # Recognize the firmware-created status leaf first so Stage/Arm can reject
    # it as stale output explicitly instead of misclassifying it as user data.
    Assert-ShimStageNames $stageRoot -AllowStatus
    $report = Join-Path $EspRoot $script:ShimReportName
    $chainStatus = Join-Path $stageRoot $script:ShimStatusName
    foreach ($path in @($report,$chainStatus)) {
        if (Test-Path -LiteralPath $path) {
            if (-not $AllowArtifacts) { throw "Uncollected prior EFI test output exists: $path" }
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Test-ReparsePoint $path)) {
                throw "EFI test output is not a safe regular file: $path"
            }
        }
    }
    return @{ EfiRoot=$efiRoot; StageRoot=$stageRoot; Report=$report; ChainStatus=$chainStatus }
}

function Assert-ExistingShimStageOwned([string]$StageRoot) {
    $prior = Get-ExistingShimStageSources $StageRoot
    $actual = @(Get-ChildItem -LiteralPath $StageRoot -Force)
    if ($actual.Count -ne $script:ShimOwnedNames.Count) { throw 'Existing shim stage is incomplete or contains unowned files.' }
    return $prior
}

function Save-ShimStageBackup([string]$StageRoot, [hashtable]$Sources) {
    Assert-NoReparseAncestors $script:InventoryBuild ([IO.Path]::GetPathRoot($script:InventoryBuild))
    New-Item -ItemType Directory -Path $script:InventoryBuild -Force | Out-Null
    Assert-NoReparseAncestors $script:InventoryBuild ([IO.Path]::GetPathRoot($script:InventoryBuild))
    $backup = Join-Path $script:InventoryBuild ('shim-chain-backup.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    try {
        foreach ($name in $script:ShimOwnedNames) { Copy-Item -LiteralPath (Join-Path $StageRoot $name) -Destination (Join-Path $backup $name) }
        Assert-ShimStageFiles $backup $Sources
        return $backup
    } catch {
        if (Test-Path -LiteralPath $backup) { Remove-OwnedShimDirectory $backup }
        throw
    }
}

function Remove-OwnedShimDirectory([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return }
    Assert-ShimStageNames $Path
    foreach ($item in @(Get-ChildItem -LiteralPath $Path -Force)) { Remove-Item -LiteralPath $item.FullName -Force }
    Remove-Item -LiteralPath $Path -Force
}

function Write-ShimReceipt([string]$VolumeGuid, [string]$EspRoot, [string]$BootHash,
                           [hashtable]$Sources, [string]$TestRootValue) {
    Assert-NoReparseAncestors $script:InventoryBuild ([IO.Path]::GetPathRoot($script:InventoryBuild))
    New-Item -ItemType Directory -Path $script:InventoryBuild -Force | Out-Null
    Assert-NoReparseAncestors $script:InventoryBuild ([IO.Path]::GetPathRoot($script:InventoryBuild))
    if (Test-ReparsePoint $script:ShimReceiptPath) { throw "Shim-chain receipt is a reparse point: $script:ShimReceiptPath" }
    $receipt = [ordered]@{
        schema=1
        stagedUtc=[DateTime]::UtcNow.ToString('o')
        volumeGuid=$VolumeGuid
        testRoot=$TestRootValue
        espRoot=$EspRoot
        stageDirectory=Join-Path (Join-Path $EspRoot 'EFI') $script:ShimDirectoryName
        bootManagerSha256=$BootHash
        certificateThumbprint=$Sources.CertificateThumbprint
        manifestSha256=(Get-Sha256 (Join-Path (Join-Path (Join-Path $EspRoot 'EFI') $script:ShimDirectoryName) 'manifest.sha256'))
    }
    foreach ($file in $Sources.Files) { $receipt[$file.Name.Replace('.efi','').Replace('.cer','') + 'Sha256']=$file.Hash }
    $temporary = $script:ShimReceiptPath + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    Assert-NoReparseAncestors $temporary $script:InventoryBuild
    [IO.File]::WriteAllText($temporary,($receipt | ConvertTo-Json -Depth 4) + [Environment]::NewLine,[Text.Encoding]::UTF8)
    if (Test-Path -LiteralPath $script:ShimReceiptPath -PathType Leaf) {
        $backup = $script:ShimReceiptPath + '.' + [Guid]::NewGuid().ToString('N') + '.bak'
        Assert-NoReparseAncestors $backup $script:InventoryBuild
        [IO.File]::Replace($temporary,$script:ShimReceiptPath,$backup)
        if (Test-Path -LiteralPath $backup -PathType Leaf) {
            try { Remove-Item -LiteralPath $backup -Force }
            catch { Write-Warning "Shim-chain receipt was committed but its temporary backup could not be removed: $backup" }
        }
    } else { Move-Item -LiteralPath $temporary -Destination $script:ShimReceiptPath }
}

function Read-ShimReceipt {
    Assert-NoReparseAncestors $script:ShimReceiptPath ([IO.Path]::GetPathRoot($script:ShimReceiptPath))
    if (Test-ReparsePoint $script:ShimReceiptPath) { throw 'Shim-chain receipt is a reparse point.' }
    if (-not (Test-Path -LiteralPath $script:ShimReceiptPath -PathType Leaf)) {
        throw "No shim-chain stage receipt exists at $script:ShimReceiptPath. Run Stage first."
    }
    try { $receipt = Get-Content -LiteralPath $script:ShimReceiptPath -Raw | ConvertFrom-Json }
    catch { throw "Invalid shim-chain stage receipt: $($_.Exception.Message)" }
    if ($receipt.schema -ne 1 -or -not $receipt.volumeGuid -or -not $receipt.stageDirectory -or
        [string]$receipt.bootManagerSha256 -notmatch '^[A-Fa-f0-9]{64}$' -or
        [string]$receipt.manifestSha256 -notmatch '^[A-Fa-f0-9]{64}$' -or
        [string]$receipt.certificateThumbprint -cne $script:ShimCertThumbprint) {
        throw 'Shim-chain stage receipt is incomplete or invalid.'
    }
    foreach ($name in $script:ShimStageNames) {
        $property = $name.Replace('.efi','').Replace('.cer','') + 'Sha256'
        if ([string]$receipt.$property -notmatch '^[A-Fa-f0-9]{64}$') { throw "Shim-chain receipt is missing $property." }
    }
    return $receipt
}

function Assert-ShimReceipt($Receipt, [hashtable]$Status, [string]$ExpectedVolumeGuid, [string]$ExpectedTestRoot) {
    if ([string]$Receipt.volumeGuid -cne $ExpectedVolumeGuid) { throw 'Current ESP volume differs from the shim-chain stage receipt.' }
    if ([string]$Receipt.bootManagerSha256 -cne $Status.BootHash) { throw 'Windows Boot Manager SHA-256 differs from the shim-chain receipt.' }
    if ([string]$Receipt.manifestSha256 -cne (Get-Sha256 (Join-Path $Status.Paths.StageRoot 'manifest.sha256'))) {
        throw 'Staged shim-chain manifest differs from the stage receipt.'
    }
    if ([string]$Receipt.certificateThumbprint -cne $Status.Sources.CertificateThumbprint) { throw 'MOK certificate differs from the shim-chain receipt.' }
    foreach ($file in $Status.Sources.Files) {
        $property = $file.Name.Replace('.efi','').Replace('.cer','') + 'Sha256'
        if ([string]$Receipt.$property -cne $file.Hash) { throw "Staged $($file.Name) differs from the shim-chain receipt." }
    }
    $actualStage = [IO.Path]::GetFullPath($Status.Paths.StageRoot)
    if ([IO.Path]::GetFullPath([string]$Receipt.stageDirectory) -cne $actualStage) { throw 'Staged directory path differs from the shim-chain receipt.' }
    if ($ExpectedTestRoot) {
        if ([IO.Path]::GetFullPath([string]$Receipt.testRoot) -cne [IO.Path]::GetFullPath($ExpectedTestRoot)) { throw 'Test root differs from the shim-chain receipt.' }
    } elseif ($Receipt.testRoot) { throw 'A host-test receipt cannot be used to verify a live ESP.' }
}

function Stage-ShimChainToRoot([string]$EspRoot, [string]$VolumeGuid, [string]$TestRootValue) {
    $sources = Get-ShimStageSources
    $bootBefore = Get-BootManagerHash $EspRoot
    $paths = Assert-ShimStageDirectory $EspRoot
    if (-not (Test-Path -LiteralPath $paths.EfiRoot)) { New-Item -ItemType Directory -Path $paths.EfiRoot | Out-Null }
    $localBackup = $null
    if (Test-Path -LiteralPath $paths.StageRoot) {
        $prior = Assert-ExistingShimStageOwned $paths.StageRoot
        $localBackup = Save-ShimStageBackup $paths.StageRoot $prior
    }
    $token = [Guid]::NewGuid().ToString('N')
    $tempRoot = Join-Path $paths.EfiRoot ('.HypersinvShimInventory.stage-' + $token)
    $backupRoot = Join-Path $paths.EfiRoot ('.HypersinvShimInventory.backup-' + $token)
    Assert-NoReparseAncestors $tempRoot $EspRoot
    Assert-NoReparseAncestors $backupRoot $EspRoot
    Assert-NoReparseAncestors $paths.StageRoot $EspRoot
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    $newMoved = $false
    $oldMoved = $false
    try {
        foreach ($file in $sources.Files) { Copy-Item -LiteralPath $file.Path -Destination (Join-Path $tempRoot $file.Name) }
        [IO.File]::WriteAllText((Join-Path $tempRoot 'manifest.sha256'),(New-ShimManifest $sources),[Text.Encoding]::ASCII)
        Assert-ShimStageFiles $tempRoot $sources
        if (Test-Path -LiteralPath $paths.StageRoot) {
            Move-Item -LiteralPath $paths.StageRoot -Destination $backupRoot
            $oldMoved = $true
        }
        Move-Item -LiteralPath $tempRoot -Destination $paths.StageRoot
        $newMoved = $true
        Assert-NoReparseAncestors $paths.StageRoot $EspRoot
        Assert-ShimStageFiles $paths.StageRoot $sources
        $bootAfter = Get-BootManagerHash $EspRoot
        if ($bootAfter -cne $bootBefore) { throw 'Windows Boot Manager SHA-256 changed during shim-chain staging.' }
        Write-ShimReceipt $VolumeGuid $EspRoot $bootBefore $sources $TestRootValue
        if ($oldMoved) {
            try { Remove-OwnedShimDirectory $backupRoot }
            catch { Write-Warning "Shim-chain staging succeeded but ESP backup cleanup failed: $($_.Exception.Message)" }
        }
        Write-Output "STAGED AND VERIFIED $($paths.StageRoot)"
        if ($localBackup) { Write-Output "PRESERVED previous stage at $localBackup" }
        Write-Output "  shimx64.efi sha256=$($sources.Files[0].Hash)"
        Write-Output "  grubx64.efi sha256=$($sources.Files[1].Hash)"
        Write-Output "  inventory.efi sha256=$($sources.Files[2].Hash)"
        Write-Output '  BootNext, Secure Boot, and MOK variables were not changed.'
    } catch {
        if ($newMoved -and (Test-Path -LiteralPath $paths.StageRoot)) { Remove-OwnedShimDirectory $paths.StageRoot }
        if ($oldMoved -and (Test-Path -LiteralPath $backupRoot) -and -not (Test-Path -LiteralPath $paths.StageRoot)) {
            Move-Item -LiteralPath $backupRoot -Destination $paths.StageRoot
        }
        if (Test-Path -LiteralPath $tempRoot) { Remove-OwnedShimDirectory $tempRoot }
        throw
    }
}

function Get-MountedShimStageStatus([string]$EspRoot, [switch]$AllowArtifacts) {
    $sources = Get-ShimStageSources
    $paths = Assert-ShimStageDirectory $EspRoot -AllowArtifacts:$AllowArtifacts
    Assert-ShimStageFiles $paths.StageRoot $sources -AllowStatus:$AllowArtifacts
    $bootHash = Get-BootManagerHash $EspRoot
    $chainStatus = $null
    if (Test-Path -LiteralPath $paths.ChainStatus -PathType Leaf) {
        $chainStatus = [IO.File]::ReadAllText($paths.ChainStatus,[Text.Encoding]::ASCII)
    }
    return @{ Paths=$paths; Sources=$sources; BootHash=$bootHash; ChainStatus=$chainStatus }
}

function Assert-SecureBootEnabled([bool]$SecureBoot) {
    if (-not $SecureBoot) { throw 'Secure Boot must be enabled before arming the signed shim/MOK chain.' }
}

function Assert-SecureBootEnabledFromFirmware {
    try { $secureBoot = [bool](Confirm-SecureBootUEFI) }
    catch { throw "Cannot verify Secure Boot state; refusing to arm. $($_.Exception.Message)" }
    Assert-SecureBootEnabled $secureBoot
}

function Invoke-ShimOneShot([ValidateSet('bootnext','off')][string]$OneShotMode,[string]$VolumeGuid) {
    $installer = Get-InstallTool
    & $installer oneshot --mode $OneShotMode --profile shim-inventory --esp-volume $VolumeGuid
    if ($LASTEXITCODE -ne 0) { throw "ib-install oneshot --mode $OneShotMode --profile shim-inventory failed (exit $LASTEXITCODE)." }
}

function Invoke-ShimChainStatus([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or (Test-ReparsePoint $Path)) {
        throw "No safe shim-chain status record exists at $Path."
    }
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -gt 4096) { throw 'Shim-chain status record is oversized.' }
    $contents = [Text.Encoding]::ASCII.GetString($bytes)
    $prefix = "schema=1`r`nsecure_boot=1`r`nshim_validation=1`r`ncollector_success="
    if (-not $contents.StartsWith($prefix,[StringComparison]::Ordinal) -or
        ($contents -cne ($prefix + "1`r`n") -and $contents -cne ($prefix + "0`r`n"))) {
        throw 'Shim-chain status does not match the exact schema=1 Secure Boot/MOK success format.'
    }
    return @{ Text=$contents; Success=$contents.EndsWith("collector_success=1`r`n",[StringComparison]::Ordinal); Hash=Get-Sha256 $Path }
}

function Invoke-ShimStatus {
    if ($shimTestRoot) {
        $espRoot = Assert-TestRoot $shimTestRoot
        $receipt = Read-ShimReceipt
        $status = Get-MountedShimStageStatus $espRoot -AllowArtifacts
        Assert-ShimReceipt $receipt $status 'TEST-ROOT' $espRoot
        Write-Output "STAGED AND VERIFIED $($status.Paths.StageRoot)"
        Write-Output "  Boot Manager sha256=$($status.BootHash)"
        if ($status.ChainStatus) { Write-Output "CHAIN STATUS $($status.ChainStatus.Trim())" }
        Write-Output '  Status does not indicate whether BootNext is armed.'
        return
    }
    $receipt = Read-ShimReceipt
    $mount = Mount-Esp
    try {
        $status = Get-MountedShimStageStatus $mount.EspRoot -AllowArtifacts
        Assert-ShimReceipt $receipt $status $mount.VolumeGuid ''
        Write-Output "STAGED AND VERIFIED $($status.Paths.StageRoot)"
        Write-Output "  Boot Manager sha256=$($status.BootHash)"
        if ($status.ChainStatus) { Write-Output "CHAIN STATUS $($status.ChainStatus.Trim())" }
        Write-Output '  Status does not indicate whether BootNext is armed.'
    } finally { Dismount-Esp $mount }
}

function Invoke-ShimArm {
    Assert-Admin
    Assert-SecureBootEnabledFromFirmware
    $receipt = Read-ShimReceipt
    $mount = Mount-Esp
    try {
        if ($receipt.volumeGuid -cne $mount.VolumeGuid) { throw 'Current system ESP volume differs from the shim-chain receipt.' }
        $status = Get-MountedShimStageStatus $mount.EspRoot
        Assert-ShimReceipt $receipt $status $mount.VolumeGuid ''
        $bootBefore = Get-BootManagerHash $mount.EspRoot
        if ((Test-Path -LiteralPath $status.Paths.Report) -or (Test-Path -LiteralPath $status.Paths.ChainStatus)) {
            throw 'Prior EFI test output must be collected before arming again.'
        }
        Invoke-ArmWithRollback {
            Invoke-ShimOneShot 'bootnext' $mount.VolumeGuid
        } {
            $verified = Get-MountedShimStageStatus $mount.EspRoot
            Assert-ShimReceipt $receipt $verified $mount.VolumeGuid ''
            Assert-SecureBootEnabledFromFirmware
            if ((Get-BootManagerHash $mount.EspRoot) -cne $bootBefore) { throw 'Windows Boot Manager SHA-256 changed while arming.' }
        } {
            Invoke-ShimOneShot 'off' $mount.VolumeGuid
            if ((Get-BootManagerHash $mount.EspRoot) -cne $bootBefore) { throw 'Windows Boot Manager SHA-256 changed during rollback.' }
        }
        Write-Output "ARMED one-time signed shim/MOK inventory boot on $($mount.VolumeGuid)"
        Write-Output 'No reboot was started. Reboot when ready; BootNext selects the shim inventory entry once.'
    } finally { Dismount-Esp $mount }
}

function Invoke-ShimDisarm {
    Assert-Admin
    $mount = Mount-Esp
    try {
        $bootBefore = Get-BootManagerHash $mount.EspRoot
        Invoke-ShimOneShot 'off' $mount.VolumeGuid
        if ((Get-BootManagerHash $mount.EspRoot) -cne $bootBefore) { throw 'Windows Boot Manager SHA-256 changed while disarming.' }
        Write-Output 'Owned signed shim/MOK BootNext and Boot#### state cleared. Staged files were preserved.'
    } finally { Dismount-Esp $mount }
}

function Invoke-ShimCollect {
    Assert-Admin
    $receipt = Read-ShimReceipt
    $mount = Mount-Esp
    try {
        if ($receipt.volumeGuid -cne $mount.VolumeGuid) { throw 'Current system ESP volume differs from the shim-chain receipt.' }
        $status = Get-MountedShimStageStatus $mount.EspRoot -AllowArtifacts
        Assert-ShimReceipt $receipt $status $mount.VolumeGuid ''
        $chain = Invoke-ShimChainStatus $status.Paths.ChainStatus
        $reportExists = Test-Path -LiteralPath $status.Paths.Report
        if ($reportExists -and (-not (Test-Path -LiteralPath $status.Paths.Report -PathType Leaf) -or (Test-ReparsePoint $status.Paths.Report))) {
            throw 'Collector report is not a safe regular file.'
        }
        if ($chain.Success -and -not $reportExists) { throw 'Collector reported success but the inventory report is missing.' }
        if ($reportExists) {
            $reportSize = (Get-Item -LiteralPath $status.Paths.Report).Length
            if ($reportSize -gt 64MB -or ($chain.Success -and $reportSize -lt 20)) { throw 'Collector report size is outside the valid capture bounds.' }
        }
        Assert-NoReparseAncestors $script:InventoryBuild ([IO.Path]::GetPathRoot($script:InventoryBuild))
        New-Item -ItemType Directory -Path $script:InventoryBuild -Force | Out-Null
        $captureDir = Join-Path $script:InventoryBuild 'captures'
        if ((Test-Path -LiteralPath $captureDir) -and -not (Test-Path -LiteralPath $captureDir -PathType Container)) { throw 'Capture path exists but is not a directory.' }
        Assert-NoReparseAncestors $captureDir $script:InventoryBuild
        New-Item -ItemType Directory -Path $captureDir -Force | Out-Null
        $suffix = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
        $statusCapture = Join-Path $captureDir ('HypersinvShimChainStatus.' + $suffix + '.txt')
        $reportCapture = Join-Path $captureDir ('HypersinvEfiInventory.' + $suffix + '.txt')
        $merged = Join-Path $captureDir ('HypersinvInventory.' + $suffix + '.txt')
        $statusHash = Get-Sha256 $status.Paths.ChainStatus
        Copy-Item -LiteralPath $status.Paths.ChainStatus -Destination $statusCapture
        if ((Get-Sha256 $statusCapture) -cne $statusHash -or (Get-Sha256 $status.Paths.ChainStatus) -cne $statusHash) {
            throw 'Shim-chain status changed while being copied; ESP artifacts were preserved.'
        }
        $reportHash = $null
        if ($reportExists) {
            $reportHash = Get-Sha256 $status.Paths.Report
            Copy-Item -LiteralPath $status.Paths.Report -Destination $reportCapture
            if ((Get-Sha256 $reportCapture) -cne $reportHash -or (Get-Sha256 $status.Paths.Report) -cne $reportHash) {
                throw 'Collector report changed while being copied; ESP artifacts were preserved.'
            }
        }
        if ($chain.Success) {
            $scanner = Join-Path $script:InventoryBuild 'efi_inventory.exe'
            if (-not (Test-Path -LiteralPath $scanner -PathType Leaf)) { throw "Missing scanner: $scanner. Run Build first." }
            & $scanner scan $reportCapture $merged
            if ($LASTEXITCODE -ne 0) { throw "efi_inventory scan failed (exit $LASTEXITCODE); ESP artifacts were preserved." }
            if (-not (Test-Path -LiteralPath $merged -PathType Leaf) -or (Get-Item -LiteralPath $merged).Length -lt 20) {
                throw 'Scanner did not create a complete local inventory report; ESP artifacts were preserved.'
            }
        }
        if ((Get-Sha256 $statusCapture) -cne $statusHash -or
            (Get-Sha256 $status.Paths.ChainStatus) -cne $statusHash) {
            throw 'Local shim-chain status read-back failed; ESP artifacts were preserved.'
        }
        if ($reportHash -and ((Get-Sha256 $reportCapture) -cne $reportHash -or
            (Get-Sha256 $status.Paths.Report) -cne $reportHash)) {
            throw 'Local collector report read-back failed; ESP artifacts were preserved.'
        }
        $bootBefore = Get-BootManagerHash $mount.EspRoot
        Invoke-ShimOneShot 'off' $mount.VolumeGuid
        if ((Get-BootManagerHash $mount.EspRoot) -cne $bootBefore) { throw 'Windows Boot Manager SHA-256 changed during cleanup.' }
        if ((Get-Sha256 $status.Paths.ChainStatus) -cne $statusHash) { throw 'Shim-chain status changed before cleanup; preserving ESP artifacts.' }
        if ($reportHash -and (Get-Sha256 $status.Paths.Report) -cne $reportHash) { throw 'Collector report changed before cleanup; preserving ESP artifacts.' }
        Remove-Item -LiteralPath $status.Paths.ChainStatus -Force
        if ($reportHash) { Remove-Item -LiteralPath $status.Paths.Report -Force }
        if ($chain.Success) {
            Write-Output "COLLECTED $reportCapture"
            Write-Output "SCANNED   $merged"
            Write-Output "CHAIN     $statusCapture"
            Write-Output '  Verified success artifacts were copied; owned BootNext state was cleared.'
        } else {
            Write-Output "COLLECTED FAILURE STATUS $statusCapture"
            if ($reportCapture -and (Test-Path -LiteralPath $reportCapture)) { Write-Output "RAW REPORT $reportCapture" }
            Write-Output '  The collector reported failure. Owned BootNext state and unchanged ESP output were cleared so the test can be armed for a retry.'
            throw 'EFI collector reported failure; failure artifacts were captured and owned one-time boot state was cleared.'
        }
    } finally { Dismount-Esp $mount }
}

function Invoke-ShimBuild {
    Invoke-Build
    foreach ($path in @((Join-Path $script:LoaderBuild 'loader.efi'),
                        (Join-Path (Join-Path $script:InventoryBuild 'efi') 'inventory.efi'),
                        (Join-Path $script:LoaderBuild 'install_authenticode_test.exe'))) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Build did not produce a required signed-chain input: $path" }
    }
    Write-Output 'Production loader, collector, Authenticode verifier, and host tests are ready for Sign.'
}

if (-not $shimLibraryOnly) {
    switch ($shimMode) {
        'Build' { Invoke-ShimBuild }
        'Sign' { Invoke-Sign }
        'Stage' {
            if ($shimTestRoot) {
                $root = Assert-TestRoot $shimTestRoot
                Stage-ShimChainToRoot $root 'TEST-ROOT' $root
            } else {
                Assert-Admin
                $mount = Mount-Esp
                try { Stage-ShimChainToRoot $mount.EspRoot $mount.VolumeGuid '' }
                finally { Dismount-Esp $mount }
            }
        }
        'Status' { Invoke-ShimStatus }
        'Arm' {
            if ($shimTestRoot) { throw '-TestRoot is allowed only for Stage and Status; it never arms firmware.' }
            Invoke-ShimArm
        }
        'Collect' {
            if ($shimTestRoot) { throw '-TestRoot is allowed only for Stage and Status; it never collects firmware output.' }
            Invoke-ShimCollect
        }
        'Disarm' {
            if ($shimTestRoot) { throw '-TestRoot is allowed only for Stage and Status; it never changes firmware state.' }
            Invoke-ShimDisarm
        }
    }
}
