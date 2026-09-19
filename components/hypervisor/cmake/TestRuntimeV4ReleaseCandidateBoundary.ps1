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

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Parses([string]$Text, [string]$Name) {
    $Tokens = $null
    $Errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Text, [ref]$Tokens, [ref]$Errors)
    Assert-True ($Errors.Count -eq 0) "$Name does not parse"
}

function Verify(
    [string]$Gate,
    [string]$Package,
    [string]$Doc,
    [string]$Readme,
    [string]$ReleaseNotes,
    [string]$Roadmap,
    [string]$CMake
) {
    Assert-Parses $Gate 'Gate 39 script'
    Assert-Parses $Package 'Runtime V4 release packager'

    foreach ($Token in @(
        'gate39-runtime-v4-release-candidate-lifecycle.ps1',
        'runtime-v4-release-manifest.json',
        'runtime-v4-release-manifest.sha256',
        'hypersinv-runtime-v4-research-release-candidate',
        '$Manifest.SchemaVersion -ne 2',
        '$Manifest.SourceCommitPushed -ne $true',
        '$Manifest.BuildTargetCount -lt 379',
        '$Manifest.CTestCount -lt 240',
        '$Manifest.PrivateKeyIncluded -ne $false',
        'Gate36EvidenceArchiveSha256',
        'Gate37EvidenceArchiveSha256',
        'Gate38EvidenceArchiveSha256',
        '31426B484F5C126F58A4C15086742738D7FB9E10447F7C56C55C99D5B799197E',
        '$RecordedManifestHash -ne $ActualManifestHash',
        'Runtime V4 manifest artifact count is invalid.',
        'SignatureStatus]::Valid',
        'Get-ExactVpidRecords',
        'index=0 group=0 number=0 vpid=1 observed=1 owner=1',
        'index=3 group=1 number=1 vpid=4 observed=4 owner=1',
        'Invoke-Loader ''load'' ''load''',
        'Invoke-Loader ''authoritative-status'' ''status''',
        'Invoke-Loader ''vpid-status'' ''vpid-status''',
        'Invoke-Loader ''unload'' ''unload''',
        '$AfterUnload.ExitCode -ne 1060',
        '$FailureEvents.Count -ne 0',
        '$Dump.LastWriteTimeUtc -gt $GateStart.ToUniversalTime()',
        'Probe=RUNTIME_V4_RELEASE_CANDIDATE_LIFECYCLE',
        'VpidIdentity=EXACT_ACTIVE',
        'PrivateKeyIncluded=FALSE',
        'tar.exe -a -c -f $ZipPath'
    )) {
        Assert-True ($Gate.Contains($Token)) "Gate 39 missing token: $Token"
    }

    Assert-True ($Gate -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 39 mutates signing, boot, SCM, or reboot state'

    foreach ($Token in @(
        'status --porcelain --untracked-files=all',
        'Tracked and untracked working tree changes must be committed or removed',
        'rev-parse ''@{u}''',
        '$Commit -ne $Upstream',
        'release commit must be pushed before packaging',
        '[ValidateRange(379,10000)]',
        '[ValidateRange(240,10000)]',
        'gate39-runtime-v4-release-candidate-lifecycle.ps1',
        '& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver',
        'SourceCommitPushed = $true',
        'PrivateKeyIncluded = $false',
        'Gate36EvidenceArchiveSha256',
        'Gate37EvidenceArchiveSha256',
        'Gate38EvidenceArchiveSha256',
        'ReadmeSha256',
        'ReleaseNotesSha256',
        'RoadmapSha256',
        'runtime-v4-release-manifest.json',
        'runtime-v4-release-manifest.sha256',
        '''(?i)^\.(pfx|p12|key|cer)$''',
        'tar.exe -a -c -f $ZipPath'
    )) {
        Assert-True ($Package.Contains($Token)) "Runtime V4 packager missing: $Token"
    }

    Assert-True (-not ($Package -match
        '(?i)Copy-Item[^\r\n]+\$(Pfx|PfxPassword)')) 'Runtime V4 packager copies private signing material'

    foreach ($Token in @(
        'Gate 39: Runtime V4 reviewed release-candidate lifecycle',
        'gate39-runtime-v4-release-candidate-lifecycle.ps1',
        'one final package-provenance lifecycle',
        'no repetition',
        'do not retry LOAD'
    )) {
        Assert-True ($Doc.Contains($Token)) "Gate 39 documentation missing: $Token"
    }

    foreach ($Token in @(
        'Reviewed Runtime V4 release',
        'Gate 39',
        '50482275082187953CEC856BB5D71423B66EB47AD6C0D4060646081BCD2E424A'
    )) {
        Assert-True ($Readme.Contains($Token)) "README Runtime V4 boundary missing: $Token"
    }
    foreach ($Token in @(
        'Runtime V4 reviewed release',
        '3bd85015967c7bd12f06e92abf98772c3a59d771',
        '9F2551137027E9FC1820ED77EE724ABDF8372ABC700A1BB205EDC9828D871591',
        'BEB4753D566541E8BFE1D7F3FAD18F32BC39B7059D80EDB12EA349222511ACEC'
    )) {
        Assert-True ($ReleaseNotes.Contains($Token)) "Release notes missing: $Token"
    }
    foreach ($Token in @(
        '| V4H Interoperability and release closure | Complete',
        'Gate 38 run is accepted',
        'Gate 39 is the final necessary Runtime V4 live boundary',
        'Runtime V4 is complete'
    )) {
        Assert-True ($Roadmap.Contains($Token)) "Runtime V4 roadmap missing: $Token"
    }
    foreach ($Token in @(
        'test_runtime_v4_release_candidate_boundary_contracts',
        'TestRuntimeV4ReleaseCandidateBoundary.ps1',
        'gate39-runtime-v4-release-candidate-lifecycle.ps1',
        'New-RuntimeV4ResearchReleaseCandidate.ps1'
    )) {
        Assert-True ($CMake.Contains($Token)) "Gate 39 CMake missing: $Token"
    }
}

$Text = @(
    [IO.File]::ReadAllText((Resolve-Path $GateScript)),
    [IO.File]::ReadAllText((Resolve-Path $Packager)),
    [IO.File]::ReadAllText((Resolve-Path $DocPath)),
    [IO.File]::ReadAllText((Resolve-Path $ReadmePath)),
    [IO.File]::ReadAllText((Resolve-Path $ReleaseNotesPath)),
    [IO.File]::ReadAllText((Resolve-Path $RoadmapPath)),
    [IO.File]::ReadAllText((Resolve-Path $TestsCMake))
)
Verify @Text

$Mutations = @(
    @{Index=0; Text=$Text[0].Replace(
        '$RecordedManifestHash -ne $ActualManifestHash', '$false')},
    @{Index=0; Text=$Text[0].Replace(
        'SignatureStatus]::Valid', 'SignatureStatus]::UnknownError')},
    @{Index=0; Text=$Text[0].Replace(
        '$Manifest.SourceCommitPushed -ne $true', '$false')},
    @{Index=0; Text=$Text[0].Replace(
        'Invoke-Loader ''vpid-status'' ''vpid-status''',
        'Invoke-Loader ''vpid-status'' ''status''')},
    @{Index=0; Text=$Text[0].Replace(
        'index=3 group=1 number=1 vpid=4 observed=4 owner=1',
        'index=3 group=1 number=1 vpid=4 observed=0 owner=1')},
    @{Index=0; Text=$Text[0].Replace(
        '$AfterUnload.ExitCode -ne 1060', '$false')},
    @{Index=0; Text=$Text[0].Replace(
        '$FailureEvents.Count -ne 0', '$false')},
    @{Index=1; Text=$Text[1].Replace(
        'status --porcelain --untracked-files=all',
        'status --porcelain --untracked-files=no')},
    @{Index=1; Text=$Text[1].Replace(
        '$Commit -ne $Upstream', '$false')},
    @{Index=1; Text=$Text[1].Replace(
        '& $SignTool sign /fd SHA256 /f $Pfx /p $PfxPassword $SignedDriver',
        'Write-Host signing-skipped')},
    @{Index=1; Text=$Text[1].Replace(
        'PrivateKeyIncluded = $false', 'PrivateKeyIncluded = $true')},
    @{Index=1; Text=$Text[1].Replace(
        '''(?i)^\.(pfx|p12|key|cer)$''', '''(?i)^\.(txt)$''')}
)

foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation.Text -ne $Text[$Mutation.Index]) `
        'Runtime V4 release mutation was vacuous'
    $Copy = $Text.Clone()
    $Copy[$Mutation.Index] = $Mutation.Text
    $Rejected = $false
    try { Verify @Copy }
    catch { $Rejected = $true }
    Assert-True $Rejected `
        "Runtime V4 release boundary accepted mutation index $($Mutation.Index)"
}

Write-Host 'PASS: Gate 39 binds a clean pushed Runtime V4 commit to fresh artifacts, exact accepted evidence, a test signature, exact VPID status, private-key exclusion, and clean lifecycle evidence.'
