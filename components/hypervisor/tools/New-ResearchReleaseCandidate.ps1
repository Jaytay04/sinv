[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$RepositoryRoot,
 [Parameter(Mandatory=$true)][string]$BuildDirectory,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [Parameter(Mandatory=$true)][string]$SignTool,
 [Parameter(Mandatory=$true)][string]$Pfx,
 [Parameter(Mandatory=$true)][string]$PfxPassword,
 [Parameter(Mandatory=$true)][ValidateRange(1,10000)][int]$BuildTargetCount,
 [Parameter(Mandatory=$true)][ValidateRange(1,10000)][int]$CTestCount)
$ErrorActionPreference='Stop'
foreach($Path in @($RepositoryRoot,$BuildDirectory)){
 if(-not(Test-Path -LiteralPath $Path -PathType Container)){throw "Directory not found: $Path"}
}
foreach($Path in @($SignTool,$Pfx)){
 if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){throw "File not found: $Path"}
}
$Commit=(& git -C $RepositoryRoot rev-parse HEAD).Trim()
$Branch=(& git -C $RepositoryRoot branch --show-current).Trim()
if($LASTEXITCODE-ne0-or$Commit-notmatch'^[0-9a-f]{40}$'){throw 'Unable to resolve the source commit.'}
if($Branch-ne'feature/hypervisor-core'){throw "Unexpected release branch: $Branch"}
$TrackedStatus=@(& git -C $RepositoryRoot status --porcelain --untracked-files=no)
if($LASTEXITCODE-ne0-or$TrackedStatus.Count-ne0){throw 'Tracked working tree changes must be committed before packaging.'}
$ShortCommit=$Commit.Substring(0,7)
$StageName="gate35-release-candidate-$ShortCommit"
$Stage=Join-Path $OutputDirectory $StageName
$ZipPath="$Stage.zip"
if(Test-Path -LiteralPath $Stage){throw "Release stage already exists: $Stage"}
New-Item -ItemType Directory -Force -Path $Stage|Out-Null
$Sources=[ordered]@{
 'hypervisor_driver.sys'=Join-Path $BuildDirectory 'src\driver\hypervisor_driver.sys'
 'hypervisor_driver.pdb'=Join-Path $BuildDirectory 'src\driver\hypervisor_driver.pdb'
 'hypervisor_loader.exe'=Join-Path $BuildDirectory 'src\loader\hypervisor_loader.exe'
 'hypervisor_loader.pdb'=Join-Path $BuildDirectory 'src\loader\hypervisor_loader.pdb'
 'gate35-release-candidate-lifecycle.ps1'=Join-Path $RepositoryRoot 'setup\gate35-release-candidate-lifecycle.ps1'
}
foreach($Name in $Sources.Keys){
 $Source=$Sources[$Name]
 if(-not(Test-Path -LiteralPath $Source -PathType Leaf)){throw "Release artifact missing: $Source"}
 Copy-Item -LiteralPath $Source -Destination (Join-Path $Stage $Name)
}
$SignedDriver=Join-Path $Stage 'hypervisor_driver.sys'
& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver
if($LASTEXITCODE-ne0){throw "Driver signing failed: $LASTEXITCODE"}
$Signature=Get-AuthenticodeSignature -LiteralPath $SignedDriver
if($Signature.SignerCertificate.Subject-ne'CN=hypersinv'){throw "Unexpected driver signer: $($Signature.SignerCertificate.Subject)"}
$Artifacts=@()
foreach($Name in $Sources.Keys){
 $Path=Join-Path $Stage $Name
 $Item=Get-Item -LiteralPath $Path
 $Hash=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
 $Artifacts+=[ordered]@{Name=$Name;Sha256=$Hash;Size=[int64]$Item.Length}
}
$Manifest=[ordered]@{
 SchemaVersion=1
 CandidateType='hypersinv-research-vm-release-candidate'
 SourceCommit=$Commit
 SourceBranch=$Branch
 CreatedUtc=[DateTime]::UtcNow.ToString('o')
 WdkVersion='10.0.28000.0'
 BuildTargetCount=$BuildTargetCount
 CTestCount=$CTestCount
 DriverSigner='CN=hypersinv'
 PrivateKeyIncluded=$false
 Gate34EvidenceArchiveSha256='224FB2E9247212EF6C21E52A8AAD1C78E8A08E4D9C0766A841930603C3413D8B'
 Gate34WinDbgLogSha256='4E5D572C5D43F71B416F2879477C104C4AAECE58D549A8729BA52EC3A3F6115B'
 Artifacts=$Artifacts
}
$ManifestPath=Join-Path $Stage 'release-manifest.json'
$Manifest|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $ManifestPath -Encoding utf8
$ManifestHash=(Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash
$ManifestHash|Set-Content -LiteralPath (Join-Path $Stage 'release-manifest.sha256') -Encoding ascii
$Forbidden=@(Get-ChildItem -LiteralPath $Stage -File|Where-Object{$_.Extension-match'(?i)^\.(pfx|p12|key|cer)$'})
if($Forbidden.Count-ne0){throw "Private key or certificate material entered the release stage: $($Forbidden.Name -join ', ')"}
if(Test-Path -LiteralPath $ZipPath){throw "Release archive already exists: $ZipPath"}
& tar.exe -a -c -f $ZipPath -C $OutputDirectory $StageName
if($LASTEXITCODE-ne0){throw "Release archive creation failed: $LASTEXITCODE"}
Write-Host "Stage=$Stage"
Write-Host "Archive=$ZipPath"
Write-Host "SourceCommit=$Commit"
Write-Host "ManifestSha256=$ManifestHash"
Write-Host "ArchiveSha256=$((Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash)"
Write-Host "HostSignatureStatus=$($Signature.Status)"
Write-Host "DriverSigner=$($Signature.SignerCertificate.Subject)"
