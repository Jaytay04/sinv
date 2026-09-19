param(
    [Parameter(Mandatory=$true)][string]$GateScript,
    [Parameter(Mandatory=$true)][string]$Packager,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$ReadmePath,
    [Parameter(Mandatory=$true)][string]$ReleaseNotesPath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake
)
$ErrorActionPreference = 'Stop'
function A([bool]$Condition,[string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Parse([string]$Text,[string]$Name) {
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Text,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) "$Name does not parse"
}
function Has([string]$Text,[string[]]$Tokens,[string]$Name) {
    foreach($Token in $Tokens) {
        A ($Text.Contains($Token)) "$Name missing: $Token"
    }
}
function Verify([string[]]$Text) {
    $Gate=$Text[0];$Package=$Text[1];$Doc=$Text[2];$Readme=$Text[3]
    $Release=$Text[4];$Roadmap=$Text[5];$CMake=$Text[6]
    Parse $Gate 'Gate 42';Parse $Package 'Runtime V5 packager'
    Has $Gate @(
      'gate42-runtime-v5-release-candidate-lifecycle.ps1',
      'runtime-v5-release-manifest.json','runtime-v5-release-manifest.sha256',
      'hypersinv-runtime-v5-research-release-candidate',
      '$Manifest.SchemaVersion -ne 3','$Manifest.SourceCommitPushed -ne $true',
      '$Manifest.BuildTargetCount -lt 382','$Manifest.CTestCount -lt 245',
      '$Manifest.PrivateKeyIncluded -ne $false',
      'Gate40EvidenceArchiveSha256','Gate40WinDbgLogSha256',
      'Gate41EvidenceArchiveSha256','Gate41WinDbgLogSha256',
      'CA26D6B328CF5745F27DA29C0C11B8F16061692B29FC95635E65A398438F24AA',
      'CC45CA32BF1F355F9C5B3F982757B5FE99E0238481091EF8B5B5453A0239256E',
      '$RecordedManifestHash -ne $ActualManifestHash',
      'Runtime V5 manifest artifact count is invalid.',
      'SignatureStatus]::Valid','Get-ExactVpidRecords',
      'Get-ExactTelemetryRecords',
      'TELEMETRY CPU: index=0 group=0 number=0 generation=1 sequence=2 total=1',
      'TELEMETRY CPU: index=3 group=1 number=1 generation=1 sequence=2 total=1',
      "Invoke-Loader 'authoritative-status' 'status'",
      "Invoke-Loader 'vpid-status' 'vpid-status'",
      "Invoke-Loader 'telemetry-status' 'telemetry-status'",
      "Invoke-Loader 'unload' 'unload'",'$AfterUnload.ExitCode -ne 1060',
      '$FailureEvents.Count -ne 0',
      '$Dump.LastWriteTimeUtc -gt $GateStart.ToUniversalTime()',
      'Probe=RUNTIME_V5_RELEASE_CANDIDATE_LIFECYCLE',
      'TelemetryIdentity=EXACT_ACTIVE','TelemetryFailClosed=0,0,0,0',
      'PrivateKeyIncluded=FALSE','tar.exe -a -c -f $ZipPath') 'Gate 42'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 42 mutates signing, boot, SCM, or reboot state'

    Has $Package @(
      'status --porcelain --untracked-files=all','rev-parse ''@{u}''',
      '$Commit -ne $Upstream','release commit must be pushed before packaging',
      '[ValidateRange(382,10000)]','[ValidateRange(245,10000)]',
      'gate42-runtime-v5-release-candidate-lifecycle.ps1',
      '& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver',
      'SchemaVersion = 3','SourceCommitPushed = $true',
      'PrivateKeyIncluded = $false','Gate40EvidenceArchiveSha256',
      'Gate41EvidenceArchiveSha256','ReadmeSha256','ReleaseNotesSha256',
      'RoadmapSha256','ROADMAP_RUNTIME_V5.md',
      'runtime-v5-release-manifest.json',
      'runtime-v5-release-manifest.sha256','''(?i)^\.(pfx|p12|key|cer)$''',
      'tar.exe -a -c -f $ZipPath') 'Runtime V5 packager'
    A (-not ($Package -match '(?i)Copy-Item[^\r\n]+\$(Pfx|PfxPassword)')) `
      'Runtime V5 packager copies private signing material'

    Has $Doc @('Gate 42: Runtime V5 reviewed release-candidate lifecycle',
      'gate42-runtime-v5-release-candidate-lifecycle.ps1',
      'one final','no repetition','Do not retry LOAD') 'Gate 42 documentation'
    Has $Readme @('Reviewed Runtime V5 release',
      '88e957674b9323ea4aa32769265ebcef5fb42a57',
      'C2505BE97028A72191A7372475C9F02668D3FF3CCA444E60E3DE47AF1AED3DC0',
      'C4161C847E979039A9E0275E145513382F588BFC5611AD0C44DCCDB1D6D1B4D7') `
      'README Runtime V5 boundary'
    Has $Release @('Runtime V5 release candidate',
      '2419938514A22FB8E3EFFBF20201ED8DB20051854619EF99846D451DF998B846',
      'CC45CA32BF1F355F9C5B3F982757B5FE99E0238481091EF8B5B5453A0239256E',
      'Gate 42 completed on 2026-08-29',
      '2E52A110971373A69D9EB4E1A2B6C9A69349CD72F0C67D2C9F64C28DAACDF194',
      'C2505BE97028A72191A7372475C9F02668D3FF3CCA444E60E3DE47AF1AED3DC0',
      'C4161C847E979039A9E0275E145513382F588BFC5611AD0C44DCCDB1D6D1B4D7') `
      'Runtime V5 release notes'
    Has $Roadmap @('| V5E CPUID telemetry qualification | Complete',
      '| V5F Route-matrix qualification | Complete',
      '| V5G Reviewed release closure | Complete','Gate 40 is accepted',
      'Gate 41 is accepted','Gate 42 - Runtime V5 reviewed release candidate',
      'Runtime V5 is accepted and closed',
      'Runtime V5 research scope complete') `
      'Runtime V5 roadmap'
    Has $CMake @('test_runtime_v5_release_candidate_boundary_contracts',
      'TestRuntimeV5ReleaseCandidateBoundary.ps1',
      'gate42-runtime-v5-release-candidate-lifecycle.ps1',
      'New-RuntimeV5ResearchReleaseCandidate.ps1') 'Gate 42 CMake'
}

$Text=@(
 [IO.File]::ReadAllText((Resolve-Path $GateScript)),
 [IO.File]::ReadAllText((Resolve-Path $Packager)),
 [IO.File]::ReadAllText((Resolve-Path $DocPath)),
 [IO.File]::ReadAllText((Resolve-Path $ReadmePath)),
 [IO.File]::ReadAllText((Resolve-Path $ReleaseNotesPath)),
 [IO.File]::ReadAllText((Resolve-Path $RoadmapPath)),
 [IO.File]::ReadAllText((Resolve-Path $TestsCMake)))
Verify $Text

$Mutations=@(
 @{I=0;T=$Text[0].Replace('$RecordedManifestHash -ne $ActualManifestHash','$false')},
 @{I=0;T=$Text[0].Replace('SignatureStatus]::Valid','SignatureStatus]::UnknownError')},
 @{I=0;T=$Text[0].Replace("Invoke-Loader 'telemetry-status' 'telemetry-status'","Invoke-Loader 'telemetry-status' 'status'")},
 @{I=0;T=$Text[0].Replace('TelemetryIdentity=EXACT_ACTIVE','TelemetryIdentity=UNVERIFIED')},
 @{I=0;T=$Text[0].Replace('$AfterUnload.ExitCode -ne 1060','$false')},
 @{I=1;T=$Text[1].Replace('status --porcelain --untracked-files=all','status --porcelain --untracked-files=no')},
 @{I=1;T=$Text[1].Replace('$Commit -ne $Upstream','$false')},
 @{I=1;T=$Text[1].Replace('& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver','Write-Host signing-skipped')},
 @{I=1;T=$Text[1].Replace('PrivateKeyIncluded = $false','PrivateKeyIncluded = $true')})
foreach($Mutation in $Mutations) {
    A ($Mutation.T -ne $Text[$Mutation.I]) 'Runtime V5 release mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try { Verify $Copy } catch { $Rejected=$true }
    A $Rejected "Runtime V5 release boundary accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: Gate 42 binds a clean pushed Runtime V5 commit to fresh artifacts, accepted telemetry evidence, exact telemetry/VPID status, a test signature, private-key exclusion, and canonical lifecycle evidence.'
