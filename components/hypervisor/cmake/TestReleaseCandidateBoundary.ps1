param(
 [Parameter(Mandatory=$true)][string]$GateScript,
 [Parameter(Mandatory=$true)][string]$Packager,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$ReadmePath,
 [Parameter(Mandatory=$true)][string]$RoadmapPath,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function Assert-T([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Assert-Parses([string]$Text,[string]$Name){$Tokens=$null;$Errors=$null;[void][Management.Automation.Language.Parser]::ParseInput($Text,[ref]$Tokens,[ref]$Errors);Assert-T ($Errors.Count-eq0) "$Name does not parse"}
function Verify([string]$Gate,[string]$Package,[string]$Doc,[string]$Readme,[string]$Roadmap,[string]$CMake){
 Assert-Parses $Gate 'Gate 35 script';Assert-Parses $Package 'Release packager'
 foreach($Token in @(
  'gate35-release-candidate-lifecycle.ps1','release-manifest.json','release-manifest.sha256',
  'hypersinv-research-vm-release-candidate','SourceCommit','feature/hypervisor-core',
  'WdkVersion-ne''10.0.28000.0''','BuildTargetCount-lt360','CTestCount-lt227',
  'DriverSigner-ne''CN=hypersinv''','PrivateKeyIncluded-ne$false',
  'Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256',
  '$RecordedManifestHash-ne$ActualManifestHash','Release manifest artifact count is invalid.',
  'hypervisor_driver.sys','hypervisor_driver.pdb','hypervisor_loader.exe','hypervisor_loader.pdb',
  'SignatureStatus]::Valid','Signer=CN=hypersinv','PrivateKeyIncluded=FALSE',
  'Invoke-Loader ''load'' ''load''','Invoke-Loader ''authoritative-status'' ''status''','Invoke-Loader ''unload'' ''unload''',
  'Probe=RELEASE_CANDIDATE_LIFECYCLE','$AfterService.ExitCode-ne1060','tar.exe -a -c -f $ZipPath')){
  Assert-T ($Gate.Contains($Token)) "Gate 35 missing token: $Token"
 }
 Assert-T ($Gate-notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 35 mutates signing, boot, SCM, or reboot state'
 foreach($Token in @(
  'status --porcelain --untracked-files=no',
  'Tracked working tree changes must be committed before packaging.',
  'gate35-release-candidate-lifecycle.ps1','& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver',
  'DriverSigner=''CN=hypersinv''','PrivateKeyIncluded=$false',
  'Gate34EvidenceArchiveSha256','Gate34WinDbgLogSha256',
  'release-manifest.json','release-manifest.sha256',
  "'(?i)^\.(pfx|p12|key|cer)$'",'tar.exe -a -c -f $ZipPath')){
  Assert-T ($Package.Contains($Token)) "Release packager missing token: $Token"
 }
 Assert-T (-not($Package-match'(?i)Copy-Item[^\r\n]+\$(Pfx|PfxPassword)')) 'Release packager copies private signing material into the stage'
 foreach($Token in @(
  'Gate 35: reviewed release-candidate lifecycle','gate35-release-candidate-lifecycle.ps1',
  'Gate 34 is accepted','one final package-provenance lifecycle',
  'Gate 35 is accepted','Runtime V3 research scope is complete')){
  Assert-T ($Doc.Contains($Token)) "Gate 35 documentation missing: $Token"
 }
 foreach($Token in @('test-signed research driver','Never bypass Driver Signature Enforcement','Gate 35',
  '418e9a163657899cfb48d5091e286a6465e35571','RELEASE_NOTES.md','ROADMAP_VNEXT.md')){
  Assert-T ($Readme.Contains($Token)) "README release boundary missing: $Token"
 }
 foreach($Token in @('Runtime V3 research scope complete','Gate 35','Gate 34 pressure',
  '42344FE95A5B636789ABF4CE5601E305EBB03C19EC039E34077884BBADCCDAE2')){
  Assert-T ($Roadmap.Contains($Token)) "Roadmap release boundary missing: $Token"
 }
 foreach($Token in @('test_gate35_release_candidate_boundary_contracts','TestReleaseCandidateBoundary.ps1','gate35-release-candidate-lifecycle.ps1','New-ResearchReleaseCandidate.ps1')){
  Assert-T ($CMake.Contains($Token)) "Gate 35 CMake missing: $Token"
 }
}
$Text=@($GateScript,$Packager,$DocPath,$ReadmePath,$RoadmapPath,$TestsCMake|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify @Text
$Mutations=@(
 @{Index=0;Text=$Text[0].Replace('$RecordedManifestHash-ne$ActualManifestHash','$false')},
 @{Index=0;Text=$Text[0].Replace('SignatureStatus]::Valid','SignatureStatus]::UnknownError')},
 @{Index=0;Text=$Text[0].Replace('$AfterService.ExitCode-ne1060','$false')},
 @{Index=0;Text=$Text[0].Replace('PrivateKeyIncluded-ne$false','PrivateKeyIncluded-ne$true')},
 @{Index=1;Text=$Text[1].Replace('status --porcelain --untracked-files=no','status --porcelain --untracked-files=all')},
 @{Index=1;Text=$Text[1].Replace('& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver','Write-Host signing-skipped')},
 @{Index=1;Text=$Text[1].Replace('PrivateKeyIncluded=$false','PrivateKeyIncluded=$true')},
 @{Index=1;Text=$Text[1].Replace("'(?i)^\.(pfx|p12|key|cer)$'","'(?i)^\.(txt)$'")})
foreach($Mutation in $Mutations){
 Assert-T ($Mutation.Text-ne$Text[$Mutation.Index]) 'Release candidate mutation was vacuous'
 $Copy=$Text.Clone();$Copy[$Mutation.Index]=$Mutation.Text;$Rejected=$false
 try{Verify @Copy}catch{$Rejected=$true}
 Assert-T $Rejected "Release candidate boundary accepted mutation index $($Mutation.Index)"
}
Write-Host 'PASS: Gate 35 preserves reviewed-commit provenance, signed artifacts, private-key exclusion, truthful status, and clean lifecycle evidence.'
