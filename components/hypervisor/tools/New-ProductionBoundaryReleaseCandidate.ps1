[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$SignTool,
    [Parameter(Mandatory=$true)][string]$Pfx,
    [Parameter(Mandatory=$true)][string]$PfxPassword,
    [Parameter(Mandatory=$true)][ValidateRange(453,10000)][int]$BuildTargetCount,
    [Parameter(Mandatory=$true)][ValidateRange(281,10000)][int]$CTestCount
)

$ErrorActionPreference='Stop'
foreach($Path in @($RepositoryRoot,$BuildDirectory,$OutputDirectory)){
    if(-not(Test-Path -LiteralPath $Path -PathType Container)){throw "Directory not found: $Path"}
}
foreach($Path in @($SignTool,$Pfx)){
    if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){throw "File not found: $Path"}
}

$Commit=(& git -C $RepositoryRoot rev-parse HEAD).Trim()
$Branch=(& git -C $RepositoryRoot branch --show-current).Trim()
if($LASTEXITCODE-ne0-or$Commit-notmatch'^[0-9a-f]{40}$'){throw 'Unable to resolve the source commit.'}
if($Branch-ne'feature/hypervisor-core'){throw "Unexpected release branch: $Branch"}
$Status=@(& git -C $RepositoryRoot status --porcelain --untracked-files=all)
if($LASTEXITCODE-ne0-or$Status.Count-ne0){throw 'Working tree must be clean before release packaging.'}
$Upstream=(& git -C $RepositoryRoot rev-parse '@{u}').Trim()
if($LASTEXITCODE-ne0-or$Commit-ne$Upstream){throw 'Release commit must be pushed before packaging.'}

$StageName="gate63-production-boundary-release-candidate-$($Commit.Substring(0,7))"
$Stage=Join-Path $OutputDirectory $StageName;$ZipPath="$Stage.zip"
if((Test-Path -LiteralPath $Stage)-or(Test-Path -LiteralPath $ZipPath)){throw "Release stage already exists: $Stage"}
New-Item -ItemType Directory -Path $Stage|Out-Null

$Sources=[ordered]@{
 'hypervisor_driver.sys'=Join-Path $BuildDirectory 'src\driver\hypervisor_driver.sys'
 'hypervisor_driver.pdb'=Join-Path $BuildDirectory 'src\driver\hypervisor_driver.pdb'
 'hypervisor_loader.exe'=Join-Path $BuildDirectory 'src\loader\hypervisor_loader.exe'
 'hypervisor_loader.pdb'=Join-Path $BuildDirectory 'src\loader\hypervisor_loader.pdb'
 'gate63-production-boundary-release-candidate.ps1'=Join-Path $RepositoryRoot 'setup\gate63-production-boundary-release-candidate.ps1'
}
foreach($Name in $Sources.Keys){
 if(-not(Test-Path -LiteralPath $Sources[$Name] -PathType Leaf)){throw "Release artifact missing: $($Sources[$Name])"}
 Copy-Item -LiteralPath $Sources[$Name] -Destination (Join-Path $Stage $Name)
}

$SignedDriver=Join-Path $Stage 'hypervisor_driver.sys'
& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver
if($LASTEXITCODE-ne0){throw "Driver signing failed: $LASTEXITCODE"}
$Signature=Get-AuthenticodeSignature -LiteralPath $SignedDriver
if($null-eq$Signature.SignerCertificate-or$Signature.SignerCertificate.Subject-ne'CN=hypersinv'){
 throw 'The staged driver was not signed by CN=hypersinv.'
}

$Artifacts=@(foreach($Name in $Sources.Keys){
 $Path=Join-Path $Stage $Name;$Item=Get-Item -LiteralPath $Path
 [ordered]@{Name=$Name;Sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash;Size=[int64]$Item.Length}
})
$Manifest=[ordered]@{
 SchemaVersion=1
 CandidateType='hypersinv-production-boundary-release-candidate'
 SourceCommit=$Commit
 SourceBranch=$Branch
 SourceCommitPushed=$true
 CreatedUtc=[DateTime]::UtcNow.ToString('o')
 WdkVersion='10.0.28000.0'
 BuildTargetCount=$BuildTargetCount
 CTestCount=$CTestCount
 DriverSigner='CN=hypersinv'
 PrivateKeyIncluded=$false
 LegacyBoundary='QUARANTINED'
 Gate62EvidenceArchiveSha256='CFB641B08A0D97D40E27A6387D56F5653F3A0B1C9DE4B96A98D8B716A65E23B8'
 Gate62WinDbgLogSha256='F3C0079504449C623D9432DB8567112D7E79A697C7BBD9CB10FF3CBA029B8F73'
 Artifacts=$Artifacts
}
$ManifestPath=Join-Path $Stage 'production-boundary-release-manifest.json'
$Manifest|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $ManifestPath -Encoding utf8
$ManifestHash=(Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash
$ManifestHash|Set-Content -LiteralPath (Join-Path $Stage 'production-boundary-release-manifest.sha256') -Encoding ascii
$ChecksumsPath=Join-Path $Stage 'SHA256SUMS.txt'
$ChecksumLines=@(Get-ChildItem -LiteralPath $Stage -File |
 Sort-Object Name |
 ForEach-Object{
  '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash,$_.Name
 })
$ChecksumLines|Set-Content -LiteralPath $ChecksumsPath -Encoding ascii
$Forbidden=@(Get-ChildItem -LiteralPath $Stage -File|Where-Object{$_.Extension-match'(?i)^\.(pfx|p12|key|cer)$'})
if($Forbidden.Count-ne0){throw "Private key material entered the release stage: $($Forbidden.Name -join ', ')"}

& tar.exe -a -c -f $ZipPath -C $OutputDirectory $StageName
if($LASTEXITCODE-ne0){throw "Release archive creation failed: $LASTEXITCODE"}
[pscustomobject]@{
 Stage=$Stage;Archive=$ZipPath;SourceCommit=$Commit;ManifestSha256=$ManifestHash
 ArchiveSha256=(Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash
 HostSignatureStatus=$Signature.Status;DriverSigner=$Signature.SignerCertificate.Subject
}
