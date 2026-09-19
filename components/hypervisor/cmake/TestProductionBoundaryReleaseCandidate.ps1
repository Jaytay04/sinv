param(
 [Parameter(Mandatory=$true)][string]$GatePath,
 [Parameter(Mandatory=$true)][string]$PackagerPath,
 [Parameter(Mandatory=$true)][string]$RoadmapPath,
 [Parameter(Mandatory=$true)][string]$DocPath)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Has([string]$Text,[string[]]$Tokens,[string]$Name){foreach($Token in $Tokens){A $Text.Contains($Token) "$Name missing: $Token"}}
function Parse([string]$Text,[string]$Name){$Tokens=$null;$Errors=$null;[void][Management.Automation.Language.Parser]::ParseInput($Text,[ref]$Tokens,[ref]$Errors);A ($Errors.Count-eq0) "$Name does not parse"}
function Verify([string[]]$Text){
 $Gate,$Packager,$Roadmap,$Doc=$Text
 Parse $Gate 'Gate 63';Parse $Packager 'release packager'
 Has $Gate @('gate63-production-boundary-release-candidate.ps1',
  'production-boundary-release-manifest.json','production-boundary-release-manifest.sha256',
  'hypersinv-production-boundary-release-candidate','$Manifest.SourceCommitPushed-ne$true',
  '$Manifest.BuildTargetCount-lt453','$Manifest.CTestCount-lt281',
  '$Manifest.LegacyBoundary-ne''QUARANTINED''','$Manifest.PrivateKeyIncluded-ne$false',
  'SignatureStatus]::Valid',"Invoke-Loader 'load' 'load'",
  "Invoke-Loader 'authoritative-status' 'status'","Invoke-Loader 'unload' 'unload'",
  'Probe=PRODUCTION_BOUNDARY_RELEASE_CANDIDATE','LegacyBoundary=QUARANTINED',
  'ServiceAfterUnload=ABSENT','PrivateKeyIncluded=FALSE','tar.exe -a -c -f') 'Gate 63'
 A ($Gate-notmatch'(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 63 mutates signing, boot, SCM, or reboot state'
 Has $Packager @('status --porcelain --untracked-files=all','rev-parse ''@{u}''',
  'Release commit must be pushed before packaging.','[ValidateRange(453,10000)]',
  '[ValidateRange(281,10000)]','& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver',
  'PrivateKeyIncluded=$false','LegacyBoundary=''QUARANTINED''',
  'Gate62EvidenceArchiveSha256','Gate62WinDbgLogSha256',
  '$ChecksumsPath=Join-Path $Stage ''SHA256SUMS.txt''','Get-ChildItem -LiteralPath $Stage -File',
  '''{0}  {1}'' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash,$_.Name',
  '''(?i)^\.(pfx|p12|key|cer)$''','tar.exe -a -c -f $ZipPath') 'packager'
 A ($Packager-notmatch'(?i)Copy-Item[^\r\n]+\$(Pfx|PfxPassword)') 'Packager copies private signing material'
 Has $Roadmap @('Production Boundary Release Closure','Gate 63 accepted','One lifecycle is proportionate') 'roadmap'
 Has $Doc @('Gate 63: production-boundary release candidate','One run is sufficient','Do not retry LOAD') 'test documentation'
}
$Text=@($GatePath,$PackagerPath,$RoadmapPath,$DocPath|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace('$Manifest.SourceCommitPushed-ne$true','$false')},
 @{I=0;T=$Text[0].Replace('SignatureStatus]::Valid','SignatureStatus]::UnknownError')},
 @{I=0;T=$Text[0].Replace("Invoke-Loader 'unload' 'unload'","Invoke-Loader 'unload' 'status'")},
 @{I=1;T=$Text[1].Replace('status --porcelain --untracked-files=all','status --porcelain --untracked-files=no')},
 @{I=1;T=$Text[1].Replace('& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver','Write-Host signing-skipped')},
 @{I=1;T=$Text[1].Replace('PrivateKeyIncluded=$false','PrivateKeyIncluded=$true')},
 @{I=1;T=$Text[1].Replace('$ChecksumsPath=Join-Path $Stage ''SHA256SUMS.txt''','$ChecksumsPath=Join-Path $Stage ''CHECKSUMS.txt''')})
$Number=0
foreach($Mutation in $Mutations){$Number++;A ($Mutation.T-ne$Text[$Mutation.I]) "Mutation $Number was vacuous";$Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false;try{Verify $Copy}catch{$Rejected=$true};A $Rejected "Mutation $Number was accepted"}
Write-Host 'PASS: production-boundary release candidate contracts verified.'
