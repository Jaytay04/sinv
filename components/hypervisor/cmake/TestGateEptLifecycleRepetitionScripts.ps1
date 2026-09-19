param(
    [Parameter(Mandatory=$true)][string]$Gate6Path,
    [Parameter(Mandatory=$true)][string]$Gate7Path,
    [Parameter(Mandatory=$true)][string]$DocPath)

$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Verify-Script([string]$Source, [int]$ExpectedCycles, [string]$Name) {
    foreach ($Token in @(
        ('$TotalCycles = {0}' -f $ExpectedCycles),
        '$Cycle -le $TotalCycles',
        "`$Gate5Script = Join-Path `$Stage 'gate5-ept-lifecycle.ps1'",
        '& sc.exe query hypersinv 2>&1',
        '$BeforeExit -ne 1060',
        'Start-Process',
        '-File `"$Gate5Script`"',
        '$Process.ExitCode -ne 0',
        "-Filter 'summary.txt'",
        "`$InnerText -notmatch '(?m)^Result=PASS\s*`$'",
        '$AfterExit -ne 1060',
        '$Summary.Add("Cycle${Cycle}=PASS")',
        '$Summary.Add("PassedCycles=$TotalCycles")',
        'tar.exe -a -c -f',
        'throw $Failure')) {
        Assert-True ($Source.Contains($Token)) "$Name missing token: $Token"
    }

    Assert-True ($Source.IndexOf('$BeforeExit -ne 1060') -lt
        $Source.IndexOf('Start-Process')) "$Name does not gate before starting"
    Assert-True ($Source.IndexOf('$Process.ExitCode -ne 0') -lt
        $Source.IndexOf('$AfterExit -ne 1060')) "$Name does not validate child before cleanup proof"
    Assert-True ($Source -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') "$Name mutates boot/signing/SCM or reboots"
}

$Gate6 = [IO.File]::ReadAllText((Resolve-Path $Gate6Path))
$Gate7 = [IO.File]::ReadAllText((Resolve-Path $Gate7Path))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))

Verify-Script $Gate6 10 'Gate 6'
Verify-Script $Gate7 100 'Gate 7'

foreach ($Token in @(
    'Gate 6 and Gate 7 repetition evidence',
    '10/10',
    '100/100',
    'gate6-ept-10cycles.ps1',
    'gate7-ept-100cycles.ps1',
    'd4e02c9')) {
    Assert-True ($Doc.Contains($Token)) "documentation missing token: $Token"
}

$Cases = @(
    @($Gate6, 10, 'Gate 6'),
    @($Gate7, 100, 'Gate 7'))
foreach ($Case in $Cases) {
    $Source = [string]$Case[0]
    $Count = [int]$Case[1]
    $Name = [string]$Case[2]
    $Mutations = @(
        $Source.Replace(('$TotalCycles = {0}' -f $Count), '$TotalCycles = 1'),
        $Source.Replace('$BeforeExit -ne 1060', '$false'),
        $Source.Replace('$Process.ExitCode -ne 0', '$false'),
        $Source.Replace("`$InnerText -notmatch '(?m)^Result=PASS\s*`$'", '$false'),
        $Source.Replace('$AfterExit -ne 1060', '$false'))
    foreach ($Mutation in $Mutations) {
        Assert-True ($Mutation -ne $Source) "$Name vacuous mutation"
        $Rejected = $false
        try { Verify-Script $Mutation $Count $Name } catch { $Rejected = $true }
        Assert-True $Rejected "$Name accepted safety mutation"
    }
}

'Verified Gate 6/7 repetition scripts: exact 10/100 counts, Gate 5 reuse, service-absence gates, stop-first failure behavior, and evidence archives.'
