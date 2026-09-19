param(
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$Gate11Path,
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
    Assert-True ($Errors.Count -eq 0) 'Gate 13 script does not parse'

    foreach ($Token in @(
        '$TotalCycles = 100',
        '$Gate11Script = Join-Path $Stage ''gate11-ept-violation-roundtrip.ps1''',
        'Start-Transcript',
        '$Cycle -le $TotalCycles',
        '& sc.exe query hypersinv 2>&1',
        '$BeforeExit -ne 1060',
        'Start-Process',
        '-File `"$Gate11Script`"',
        '$Process.ExitCode -ne 0',
        '-Filter ''summary.txt''',
        '$InnerText -notmatch ''(?m)^Result=PASS\s*$''',
        '$InnerText -notmatch ''(?m)^ViolationRoundtrip=PASS\s*$''',
        '$AfterExit -ne 1060',
        '$Summary.Add("Cycle${Cycle}=PASS")',
        '$Summary.Add("PassedCycles=$TotalCycles")',
        'Stop-Transcript',
        'tar.exe -a -c -f $ZipPath',
        'throw $Failure')) {
        Assert-True ($Source.Contains($Token)) "Gate 13 missing token: $Token"
    }
    Assert-Ordered $Source @(
        '$BeforeExit -ne 1060',
        'Start-Process',
        '$Process.ExitCode -ne 0',
        '$InnerText -notmatch ''(?m)^Result=PASS\s*$''',
        '$AfterExit -ne 1060',
        '$Summary.Add("Cycle${Cycle}=PASS")') 'Gate 13 cycle'
    Assert-True ($Source -notmatch
        '(?im)\b(hypervisor_loader(?:\.exe)?|bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
        'Gate 13 bypasses Gate 11 or mutates signing/boot/SCM/reboot state'

    foreach ($Token in @(
        'Gate 13: 100-cycle EPT-violation VM-exit stress',
        'gate13-ept-violation-roundtrip-100cycles.ps1',
        '100/100',
        'Gate 11',
        'do not retry LOAD',
        'accepted Gate 12')) {
        Assert-True ($Doc.Contains($Token)) "Gate 13 documentation missing: $Token"
    }
    foreach ($Token in @(
        'test_gate13_ept_violation_roundtrip_100cycles_script_contracts',
        'TestGate13EptViolationRoundtrip100CyclesScript.ps1',
        'gate13-ept-violation-roundtrip-100cycles.ps1')) {
        Assert-True ($CMake.Contains($Token)) "Gate 13 CMake missing: $Token"
    }
}

$Source = [IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Gate11 = [IO.File]::ReadAllText((Resolve-Path $Gate11Path))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))
$CMake = [IO.File]::ReadAllText((Resolve-Path $TestsCMake))

Assert-True ($Gate11.Contains(
    '$RoundtripProof = ''SUCCESS: One-shot EPT violation VM-exit roundtrip verified.''')) `
    'Gate 11 no longer proves the EPT-violation VM-exit roundtrip'
Assert-True ($Gate11.Contains(
    '@(''Result=PASS'',''ViolationRoundtrip=PASS'')')) `
    'Gate 11 no longer publishes the exact accepted summary'
Verify $Source $Doc $CMake

$Mutations = @(
    $Source.Replace('$TotalCycles = 100', '$TotalCycles = 1'),
    $Source.Replace(
        '$Gate11Script = Join-Path $Stage ''gate11-ept-violation-roundtrip.ps1''',
        '$Gate11Script = Join-Path $Stage ''gate8-ept-hook-roundtrip.ps1'''),
    $Source.Replace('$BeforeExit -ne 1060', '$false'),
    $Source.Replace('$Process.ExitCode -ne 0', '$false'),
    $Source.Replace(
        '$InnerText -notmatch ''(?m)^Result=PASS\s*$''', '$false'),
    $Source.Replace(
        '$InnerText -notmatch ''(?m)^ViolationRoundtrip=PASS\s*$''', '$false'),
    $Source.Replace('$AfterExit -ne 1060', '$false'),
    $Source.Replace(
        'Stop-Transcript | Out-Null', '$TranscriptStarted = $false'),
    $Source.Replace(
        'tar.exe -a -c -f $ZipPath', 'tar.exe -tf $ZipPath'))
foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation -ne $Source) 'Gate 13 vacuous mutation'
    $Rejected = $false
    try { Verify $Mutation $Doc $CMake } catch { $Rejected = $true }
    Assert-True $Rejected 'Gate 13 accepted a safety mutation'
}

Write-Host 'PASS: Gate 13 repeats the complete Gate 11 transaction exactly 100 times with service-absence, complete-summary, stop-first, transcript, and archive gates.'