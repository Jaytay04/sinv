param(
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake
)

$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Ordered(
    [string]$Source,
    [string[]]$Tokens,
    [string]$Name
) {
    $Last = -1
    foreach ($Token in $Tokens) {
        $At = $Source.IndexOf(
            $Token, $Last + 1, [StringComparison]::Ordinal)
        Assert-True ($At -gt $Last) "$Name order changed at: $Token"
        $Last = $At
    }
}

function Verify(
    [string]$Source,
    [string]$Doc,
    [string]$Roadmap,
    [string]$CMake
) {
    $Tokens = $null
    $Errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Source, [ref]$Tokens, [ref]$Errors)
    Assert-True ($Errors.Count -eq 0) 'Gate 38 script does not parse'

    foreach ($Token in @(
        '$GateScript = Join-Path $Stage ''gate38-vpid-cr3-residency-interoperability.ps1''',
        '$ManifestPath = Join-Path $Stage ''SHA256SUMS.txt''',
        'Assert-StageManifest',
        '$Records.Count -ne $ExpectedNames.Count',
        'Manifest hash mismatch',
        'Gate38ProcessorGroups',
        '[uint16]$Group',
        '$GroupCount -ne 2',
        '$TotalProcessors -ne 4',
        '$Recovery.DebugInfoType -ne 2',
        '$Recovery.AutoReboot',
        '-not $Recovery.WriteDebugInfo',
        '$BeforeService.ExitCode -ne 1060',
        'Invoke-Loader ''load'' ''load''',
        'INFO: Exact EPT READY status is verified.',
        'Invoke-Loader ''vpid-before'' ''vpid-status''',
        'index=0 group=0 number=0 vpid=1 observed=1 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022',
        'index=3 group=1 number=1 vpid=4 observed=4 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022',
        'Invoke-Loader ''cr3-before'' ''cr3-status''',
        'class=INACTIVE',
        'targets=1',
        'residents=0 transitions=0',
        'Invoke-Loader ''cr3-residency-probe'' ''cr3-residency-probe''',
        '$BeforeTransitions -ge $TargetTransitions',
        '$TargetTransitions -ge $AfterTransitions',
        '$Residency -ne ''0/1/0''',
        '$Matched -ne ''4294967295/0/4294967295''',
        '$Writes -ne ''1/1/1''',
        'Invoke-Loader ''cr3-after'' ''cr3-status''',
        'class=OBSERVED',
        'Invoke-Loader ''vpid-after'' ''vpid-status''',
        '$VpidBeforeRecords -join "`n"',
        '$VpidAfterRecords -join "`n"',
        'Invoke-Loader ''unload'' ''unload''',
        '$AfterUnload.ExitCode -ne 1060',
        '$FailureEvents.Count -ne 0',
        '$Dump.LastWriteTimeUtc -gt $GateStart.ToUniversalTime()',
        '''Probe=VPID_CR3_RESIDENCY_INTEROPERABILITY''',
        '''VpidIdentityStable=VERIFIED''',
        '''Cr3Residency=0/1/0''',
        '''ServiceAfterUnload=ABSENT''',
        'Stop-Transcript',
        'tar.exe -a -c -f $ZipPath',
        'throw $Failure'
    )) {
        Assert-True ($Source.Contains($Token)) "Gate 38 missing token: $Token"
    }

    Assert-Ordered $Source @(
        '$BeforeService.ExitCode -ne 1060',
        'Invoke-Loader ''load'' ''load''',
        'Invoke-Loader ''vpid-before'' ''vpid-status''',
        'Invoke-Loader ''cr3-before'' ''cr3-status''',
        'Invoke-Loader ''cr3-residency-probe'' ''cr3-residency-probe''',
        'Invoke-Loader ''cr3-after'' ''cr3-status''',
        'Invoke-Loader ''vpid-after'' ''vpid-status''',
        'Invoke-Loader ''unload'' ''unload''',
        '$AfterUnload.ExitCode -ne 1060',
        '$FailureEvents.Count -ne 0',
        '$Dump.LastWriteTimeUtc -gt $GateStart.ToUniversalTime()',
        '''Result=PASS'''
    ) 'Gate 38 transaction'

    Assert-True ($Source -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 38 mutates signing, boot, SCM, or reboot state'

    foreach ($Token in @(
        'Gate 38: VPID/CR3 residency interoperability',
        'gate38-vpid-cr3-residency-interoperability.ps1',
        'VpidIdentityStable=VERIFIED',
        'single integration run',
        'do not retry LOAD'
    )) {
        Assert-True ($Doc.Contains($Token)) "Gate 38 documentation missing: $Token"
    }

    foreach ($Token in @(
        '| V4G Lifecycle qualification | Complete',
        '| V4H Interoperability and release closure | Complete',
        'Gate 37 evidence',
        'Gate 38'
    )) {
        Assert-True ($Roadmap.Contains($Token)) "Runtime V4 roadmap missing: $Token"
    }

    foreach ($Token in @(
        'test_gate38_vpid_cr3_residency_interoperability_script_contracts',
        'TestGate38VpidCr3ResidencyInteroperabilityScript.ps1',
        'gate38-vpid-cr3-residency-interoperability.ps1'
    )) {
        Assert-True ($CMake.Contains($Token)) "Gate 38 CMake missing: $Token"
    }
}

$Source = [IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))
$Roadmap = [IO.File]::ReadAllText((Resolve-Path $RoadmapPath))
$CMake = [IO.File]::ReadAllText((Resolve-Path $TestsCMake))

Verify $Source $Doc $Roadmap $CMake

$Mutations = @(
    $Source.Replace('$GroupCount -ne 2', '$false'),
    $Source.Replace('$Recovery.DebugInfoType -ne 2', '$false'),
    $Source.Replace('Assert-StageManifest', 'Write-Host'),
    $Source.Replace('$BeforeService.ExitCode -ne 1060', '$false'),
    $Source.Replace(
        'Invoke-Loader ''vpid-before'' ''vpid-status''',
        'Invoke-Loader ''vpid-before'' ''status'''),
    $Source.Replace(
        'index=3 group=1 number=1 vpid=4 observed=4 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022',
        'index=3 group=1 number=1 vpid=4 observed=0 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022'),
    $Source.Replace(
        'Invoke-Loader ''cr3-before'' ''cr3-status''',
        'Invoke-Loader ''cr3-before'' ''status'''),
    $Source.Replace('$Residency -ne ''0/1/0''', '$false'),
    $Source.Replace(
        '$Matched -ne ''4294967295/0/4294967295''', '$false'),
    $Source.Replace('$Writes -ne ''1/1/1''', '$false'),
    $Source.Replace(
        'Invoke-Loader ''vpid-after'' ''vpid-status''',
        'Invoke-Loader ''vpid-after'' ''status'''),
    $Source.Replace(
        '($VpidAfterRecords -join "`n")',
        '($VpidBeforeRecords -join "`n")'),
    $Source.Replace('Invoke-Loader ''unload'' ''unload''',
        'Invoke-Loader ''unload'' ''status'''),
    $Source.Replace('$AfterUnload.ExitCode -ne 1060', '$false'),
    $Source.Replace('$FailureEvents.Count -ne 0', '$false'),
    $Source.Replace(
        '$Dump.LastWriteTimeUtc -gt $GateStart.ToUniversalTime()',
        '$false'),
    $Source.Replace('Stop-Transcript | Out-Null',
        '$TranscriptStarted = $false'),
    $Source.Replace('tar.exe -a -c -f $ZipPath',
        'tar.exe -tf $ZipPath')
)

foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation -ne $Source) 'Gate 38 vacuous mutation'
    $Rejected = $false
    try {
        Verify $Mutation $Doc $Roadmap $CMake
    }
    catch {
        $Rejected = $true
    }
    Assert-True $Rejected 'Gate 38 accepted a safety mutation'
}

Write-Host 'PASS: Gate 38 requires stable exact VPID identity across one exact CR3 residency roundtrip, clean unload, event checks, dump checks, and archived evidence.'
