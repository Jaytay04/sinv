[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$SignTool,
    [Parameter(Mandatory=$true)][string]$Pfx,
    [Parameter(Mandatory=$true)][string]$PfxPassword,
    [Parameter(Mandatory=$true)]
    [ValidateRange(382,10000)][int]$BuildTargetCount,
    [Parameter(Mandatory=$true)]
    [ValidateRange(245,10000)][int]$CTestCount
)

$ErrorActionPreference = 'Stop'

foreach ($Path in @($RepositoryRoot, $BuildDirectory)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Directory not found: $Path"
    }
}
foreach ($Path in @($SignTool, $Pfx)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "File not found: $Path"
    }
}

$Commit = (& git -C $RepositoryRoot rev-parse HEAD).Trim()
$Branch = (& git -C $RepositoryRoot branch --show-current).Trim()
if ($LASTEXITCODE -ne 0 -or $Commit -notmatch '^[0-9a-f]{40}$') {
    throw 'Unable to resolve the source commit.'
}
if ($Branch -ne 'feature/hypervisor-core') {
    throw "Unexpected release branch: $Branch"
}

$Status = @(& git -C $RepositoryRoot status --porcelain --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $Status.Count -ne 0) {
    throw 'Tracked and untracked working tree changes must be committed or removed before Runtime V5 packaging.'
}

$Upstream = (& git -C $RepositoryRoot rev-parse '@{u}').Trim()
if ($LASTEXITCODE -ne 0 -or $Upstream -notmatch '^[0-9a-f]{40}$') {
    throw 'The release branch must have a resolvable upstream.'
}
if ($Commit -ne $Upstream) {
    throw 'The Runtime V5 release commit must be pushed before packaging.'
}

$ShortCommit = $Commit.Substring(0, 7)
$StageName = "gate42-runtime-v5-release-candidate-$ShortCommit"
$Stage = Join-Path $OutputDirectory $StageName
$ZipPath = "$Stage.zip"
if ((Test-Path -LiteralPath $Stage) -or
    (Test-Path -LiteralPath $ZipPath)) {
    throw "Runtime V5 release stage already exists: $Stage"
}
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

$Sources = [ordered]@{
    'hypervisor_driver.sys' =
        Join-Path $BuildDirectory 'src\driver\hypervisor_driver.sys'
    'hypervisor_driver.pdb' =
        Join-Path $BuildDirectory 'src\driver\hypervisor_driver.pdb'
    'hypervisor_loader.exe' =
        Join-Path $BuildDirectory 'src\loader\hypervisor_loader.exe'
    'hypervisor_loader.pdb' =
        Join-Path $BuildDirectory 'src\loader\hypervisor_loader.pdb'
    'gate42-runtime-v5-release-candidate-lifecycle.ps1' =
        Join-Path $RepositoryRoot `
            'setup\gate42-runtime-v5-release-candidate-lifecycle.ps1'
}
foreach ($Name in $Sources.Keys) {
    $Source = $Sources[$Name]
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Release artifact missing: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination (Join-Path $Stage $Name)
}

$SignedDriver = Join-Path $Stage 'hypervisor_driver.sys'
& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver
if ($LASTEXITCODE -ne 0) {
    throw "Driver signing failed: $LASTEXITCODE"
}
$Signature = Get-AuthenticodeSignature -LiteralPath $SignedDriver
if ($null -eq $Signature.SignerCertificate -or
    $Signature.SignerCertificate.Subject -ne 'CN=hypersinv') {
    throw 'The staged driver was not signed by CN=hypersinv.'
}

$Artifacts = @()
foreach ($Name in $Sources.Keys) {
    $Path = Join-Path $Stage $Name
    $Item = Get-Item -LiteralPath $Path
    $Hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    $Artifacts += [ordered]@{
        Name = $Name
        Sha256 = $Hash
        Size = [int64]$Item.Length
    }
}

$Manifest = [ordered]@{
    SchemaVersion = 3
    CandidateType = 'hypersinv-runtime-v5-research-release-candidate'
    SourceCommit = $Commit
    SourceBranch = $Branch
    SourceCommitPushed = $true
    CreatedUtc = [DateTime]::UtcNow.ToString('o')
    WdkVersion = '10.0.28000.0'
    BuildTargetCount = $BuildTargetCount
    CTestCount = $CTestCount
    DriverSigner = 'CN=hypersinv'
    PrivateKeyIncluded = $false
    Gate40EvidenceArchiveSha256 =
        '476AFDC89A38DC0398EAF25E469F88EAC9055D4C5C93382876AE03B0625140C2'
    Gate40WinDbgLogSha256 =
        '2419938514A22FB8E3EFFBF20201ED8DB20051854619EF99846D451DF998B846'
    Gate41EvidenceArchiveSha256 =
        'CA26D6B328CF5745F27DA29C0C11B8F16061692B29FC95635E65A398438F24AA'
    Gate41WinDbgLogSha256 =
        'CC45CA32BF1F355F9C5B3F982757B5FE99E0238481091EF8B5B5453A0239256E'
    ReadmeSha256 = (Get-FileHash -LiteralPath `
        (Join-Path $RepositoryRoot 'README.md') -Algorithm SHA256).Hash
    ReleaseNotesSha256 = (Get-FileHash -LiteralPath `
        (Join-Path $RepositoryRoot 'RELEASE_NOTES.md') -Algorithm SHA256).Hash
    RoadmapSha256 = (Get-FileHash -LiteralPath `
        (Join-Path $RepositoryRoot 'ROADMAP_RUNTIME_V5.md') `
        -Algorithm SHA256).Hash
    Artifacts = $Artifacts
}

$ManifestPath = Join-Path $Stage 'runtime-v5-release-manifest.json'
$Manifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $ManifestPath -Encoding utf8
$ManifestHash =
    (Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash
$ManifestHash | Set-Content -LiteralPath `
    (Join-Path $Stage 'runtime-v5-release-manifest.sha256') -Encoding ascii

$Forbidden = @(Get-ChildItem -LiteralPath $Stage -File | Where-Object {
    $_.Extension -match '(?i)^\.(pfx|p12|key|cer)$'
})
if ($Forbidden.Count -ne 0) {
    throw "Private key or certificate material entered the release stage: $($Forbidden.Name -join ', ')"
}

& tar.exe -a -c -f $ZipPath -C $OutputDirectory $StageName
if ($LASTEXITCODE -ne 0) {
    throw "Release archive creation failed: $LASTEXITCODE"
}

Write-Host "Stage=$Stage"
Write-Host "Archive=$ZipPath"
Write-Host "SourceCommit=$Commit"
Write-Host "ManifestSha256=$ManifestHash"
Write-Host "ArchiveSha256=$((Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash)"
Write-Host "HostSignatureStatus=$($Signature.Status)"
Write-Host "DriverSigner=$($Signature.SignerCertificate.Subject)"

