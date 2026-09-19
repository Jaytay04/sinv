[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$SignTool,
    [Parameter(Mandatory=$true)][string]$Pfx,
    [Parameter(Mandatory=$true)][string]$PfxPassword
)

$ErrorActionPreference='Stop'
foreach($Path in @($RepositoryRoot,$BuildDirectory,$OutputDirectory)){
    if(-not(Test-Path -LiteralPath $Path -PathType Container)){
        throw "Directory not found: $Path"
    }
}
foreach($Path in @($SignTool,$Pfx)){
    if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){
        throw "File not found: $Path"
    }
}

$Commit=(& git -C $RepositoryRoot rev-parse HEAD).Trim()
$Branch=(& git -C $RepositoryRoot branch --show-current).Trim()
if($LASTEXITCODE-ne0-or$Commit-notmatch'^[0-9a-f]{40}$'){
    throw 'Unable to resolve the source commit.'
}
if($Branch-ne'feature/hypervisor-core'){throw "Unexpected branch: $Branch"}
$Status=@(& git -C $RepositoryRoot status --porcelain --untracked-files=all)
if($LASTEXITCODE-ne0-or$Status.Count-ne0){
    throw 'Working tree must be clean before Gate 68 packaging.'
}
$Upstream=(& git -C $RepositoryRoot rev-parse '@{u}').Trim()
if($LASTEXITCODE-ne0-or$Commit-ne$Upstream){
    throw 'Gate 68 source commit must be pushed before packaging.'
}

$StageName="gate68-vmcs-binding-attestation-$($Commit.Substring(0,7))"
$Stage=Join-Path $OutputDirectory $StageName
$ZipPath="$Stage.zip"
if((Test-Path -LiteralPath $Stage)-or(Test-Path -LiteralPath $ZipPath)){
    throw "Gate 68 stage already exists: $Stage"
}
New-Item -ItemType Directory -Path $Stage|Out-Null

$Sources=[ordered]@{
    'hypervisor_driver.sys'=Join-Path $BuildDirectory 'src\driver\hypervisor_driver.sys'
    'hypervisor_driver.pdb'=Join-Path $BuildDirectory 'src\driver\hypervisor_driver.pdb'
    'hypervisor_loader.exe'=Join-Path $BuildDirectory 'src\loader\hypervisor_loader.exe'
    'hypervisor_loader.pdb'=Join-Path $BuildDirectory 'src\loader\hypervisor_loader.pdb'
    'gate68-vmcs-binding-attestation.ps1'=Join-Path $RepositoryRoot 'setup\gate68-vmcs-binding-attestation.ps1'
}
foreach($Name in $Sources.Keys){
    if(-not(Test-Path -LiteralPath $Sources[$Name] -PathType Leaf)){
        throw "Gate 68 artifact missing: $($Sources[$Name])"
    }
    Copy-Item -LiteralPath $Sources[$Name] -Destination (Join-Path $Stage $Name)
}

$SignedDriver=Join-Path $Stage 'hypervisor_driver.sys'
& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver
if($LASTEXITCODE-ne0){throw "Driver signing failed: $LASTEXITCODE"}
$Signature=Get-AuthenticodeSignature -LiteralPath $SignedDriver
if($null-eq$Signature.SignerCertificate-or
   $Signature.SignerCertificate.Subject-ne'CN=hypersinv'){
    throw 'The staged driver was not signed by CN=hypersinv.'
}

$Commit|Set-Content -LiteralPath (Join-Path $Stage 'SOURCE_COMMIT.txt') -Encoding ascii
$ChecksumsPath=Join-Path $Stage 'SHA256SUMS.txt'
$ChecksumLines=@(Get-ChildItem -LiteralPath $Stage -File |
    Sort-Object Name |
    ForEach-Object{
        '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash,$_.Name
    })
$ChecksumLines|Set-Content -LiteralPath $ChecksumsPath -Encoding ascii
$Forbidden=@(Get-ChildItem -LiteralPath $Stage -File|
    Where-Object{$_.Extension-match'(?i)^\.(pfx|p12|key|cer)$'})
if($Forbidden.Count-ne0){
    throw "Private key material entered Gate 68 stage: $($Forbidden.Name -join ', ')"
}

& tar.exe -a -c -f $ZipPath -C $OutputDirectory $StageName
if($LASTEXITCODE-ne0){throw "Gate 68 archive creation failed: $LASTEXITCODE"}
[pscustomobject]@{
    Stage=$Stage
    Archive=$ZipPath
    SourceCommit=$Commit
    ArchiveSha256=(Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash
    HostSignatureStatus=$Signature.Status
    DriverSigner=$Signature.SignerCertificate.Subject
}

