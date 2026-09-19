param(
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$Gate14Path,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake)

$ErrorActionPreference = 'Stop'
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-Ordered([string]$Source, [string[]]$Tokens, [string]$Name) {
    $Last = -1
    foreach ($Token in $Tokens) {
        $At = $Source.IndexOf($Token, $Last + 1, [StringComparison]::Ordinal)
        Assert-True ($At -gt $Last) "$Name order changed at: $Token"
        $Last = $At
    }
}
function Verify([string]$Source, [string]$Doc, [string]$CMake) {
    $Tokens = $null
    $Errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Source, [ref]$Tokens, [ref]$Errors)
    Assert-True ($Errors.Count -eq 0) 'Gate 16 script does not parse'

    foreach ($Token in @(
        '$TotalCycles = 100',
        '$Gate14Script = Join-Path $Stage ''gate14-cr3-probe-diagnostic.ps1''',
        'Start-Transcript',
        '$Cycle -le $TotalCycles',
        '& sc.exe query hypersinv 2>&1',
        '$BeforeExit -ne 1060',
        'Start-Process',
        '-File `"$Gate14Script`"',
        '$Process.ExitCode -ne 0',
        '-Filter ''summary.txt''',
        '$InnerText -notmatch ''(?m)^Result=PASS\s*$''',
        '$InnerText -notmatch ''(?m)^ProbeStage=COMPLETE\s*$''',
        '$AfterTransitions -le $BeforeTransitions',
        '$Delta -ne ($AfterTransitions - $BeforeTransitions)',
        '$AfterExit -ne 1060',
        '$Summary.Add("Cycle${Cycle}=PASS;Before=$BeforeTransitions;After=$AfterTransitions;Delta=$Delta")',
        '$Summary.Add("PassedCycles=$TotalCycles")',
        'Stop-Transcript',
        'tar.exe -a -c -f $ZipPath',
        'throw $Failure')) {
        Assert-True ($Source.Contains($Token)) "Gate 16 missing token: $Token"
    }
    Assert-Ordered $Source @(
        '$BeforeExit -ne 1060',
        'Start-Process',
        '$Process.ExitCode -ne 0',
        '$InnerText -notmatch ''(?m)^Result=PASS\s*$''',
        '$AfterTransitions -le $BeforeTransitions',
        '$AfterExit -ne 1060',
        '$Summary.Add("Cycle${Cycle}=PASS;Before=$BeforeTransitions;After=$AfterTransitions;Delta=$Delta")') 'Gate 16 cycle'
    Assert-True ($Source -notmatch
        '(?im)\b(hypervisor_loader(?:\.exe)?|bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
        'Gate 16 bypasses Gate 14 or mutates signing/boot/SCM/reboot state'

    foreach ($Token in @(
        'Gate 16: 100-cycle CR3-load VM-exit probe',
        'gate16-cr3-probe-100cycles.ps1',
        '100/100',
        'Gate 14',
        'do not retry LOAD',
        'accepted Gate 15')) {
        Assert-True ($Doc.Contains($Token)) "Gate 16 documentation missing: $Token"
    }
    foreach ($Token in @(
        'test_gate16_cr3_probe_100cycles_script_contracts',
        'TestGate16Cr3Probe100CyclesScript.ps1',
        'gate16-cr3-probe-100cycles.ps1')) {
        Assert-True ($CMake.Contains($Token)) "Gate 16 CMake missing: $Token"
    }
}

$Source = [IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Gate14 = [IO.File]::ReadAllText((Resolve-Path $Gate14Path))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))
$CMake = [IO.File]::ReadAllText((Resolve-Path $TestsCMake))

foreach ($Token in @(
    '''Result=PASS''',
    '''ProbeStage=COMPLETE''',
    '''SUCCESS: One-shot CR3-load VM-exit probe verified.''')) {
    Assert-True ($Gate14.Contains($Token)) "Gate 14 no longer proves: $Token"
}
Verify $Source $Doc $CMake

$Mutations = @(
    $Source.Replace('$TotalCycles = 100', '$TotalCycles = 1'),
    $Source.Replace(
        '$Gate14Script = Join-Path $Stage ''gate14-cr3-probe-diagnostic.ps1''',
        '$Gate14Script = Join-Path $Stage ''gate14-cr3-status-observation.ps1'''),
    $Source.Replace('$BeforeExit -ne 1060', '$false'),
    $Source.Replace('$Process.ExitCode -ne 0', '$false'),
    $Source.Replace(
        '$InnerText -notmatch ''(?m)^Result=PASS\s*$''', '$false'),
    $Source.Replace(
        '$InnerText -notmatch ''(?m)^ProbeStage=COMPLETE\s*$''', '$false'),
    $Source.Replace('$AfterTransitions -le $BeforeTransitions', '$false'),
    $Source.Replace(
        '$Delta -ne ($AfterTransitions - $BeforeTransitions)', '$false'),
    $Source.Replace('$AfterExit -ne 1060', '$false'),
    $Source.Replace(
        'Stop-Transcript | Out-Null', '$TranscriptStarted = $false'),
    $Source.Replace(
        'tar.exe -a -c -f $ZipPath', 'tar.exe -tf $ZipPath'))
foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation -ne $Source) 'Gate 16 vacuous mutation'
    $Rejected = $false
    try { Verify $Mutation $Doc $CMake } catch { $Rejected = $true }
    Assert-True $Rejected 'Gate 16 accepted a safety mutation'
}

Write-Host 'PASS: Gate 16 repeats the complete Gate 14 CR3 probe exactly 100 times with service-absence, transition, stop-first, transcript, and archive gates.'
