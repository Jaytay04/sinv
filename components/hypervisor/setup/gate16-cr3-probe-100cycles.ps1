[CmdletBinding()]
param(
    [string]$Stage = 'C:\V3H',
    [string]$EvidenceRoot = 'C:\V3H\evidence'
)

$ErrorActionPreference = 'Stop'
$TotalCycles = 100
$Gate14Script = Join-Path $Stage 'gate14-cr3-probe-diagnostic.ps1'
$PowerShellExe =
    "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"

if (-not (Test-Path -LiteralPath $Gate14Script -PathType Leaf)) {
    throw "Missing Gate 14 script: $Gate14Script"
}

$RunName = 'gate16-cr3-probe-100cycles-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence = Join-Path $EvidenceRoot $RunName
$ZipPath = "$RunEvidence.zip"
$TranscriptPath = Join-Path $RunEvidence 'console.log'
$SummaryPath = Join-Path $RunEvidence 'gate16-summary.txt'
$Summary = [System.Collections.Generic.List[string]]::new()
$Failure = $null
$TranscriptStarted = $false

New-Item -ItemType Directory -Force -Path $RunEvidence | Out-Null

try {
    Start-Transcript -LiteralPath $TranscriptPath -Force | Out-Null
    $TranscriptStarted = $true

    for ($Cycle = 1; $Cycle -le $TotalCycles; $Cycle++) {
        Write-Host "`n========== CYCLE ${Cycle}/${TotalCycles} =========="

        $CycleRoot = Join-Path $RunEvidence ('cycle-{0:D3}' -f $Cycle)
        New-Item -ItemType Directory -Force -Path $CycleRoot | Out-Null

        $BeforeOutput = & sc.exe query hypersinv 2>&1
        $BeforeExit = $LASTEXITCODE
        $BeforeOutput |
            Out-File (Join-Path $CycleRoot 'service-before-wrapper.txt') `
                -Encoding utf8

        if ($BeforeExit -ne 1060) {
            throw "Cycle ${Cycle}: service was present before LOAD. Stop without recovery."
        }

        $Stdout = Join-Path $CycleRoot 'runner-stdout.txt'
        $Stderr = Join-Path $CycleRoot 'runner-stderr.txt'
        $Arguments =
            "-NoProfile -ExecutionPolicy Bypass " +
            "-File `"$Gate14Script`" " +
            "-Stage `"$Stage`" " +
            "-EvidenceRoot `"$CycleRoot`""

        $Process = Start-Process `
            -FilePath $PowerShellExe `
            -ArgumentList $Arguments `
            -Wait `
            -PassThru `
            -NoNewWindow `
            -RedirectStandardOutput $Stdout `
            -RedirectStandardError $Stderr

        Get-Content -LiteralPath $Stdout | Write-Host
        Get-Content -LiteralPath $Stderr | Write-Host

        if ($Process.ExitCode -ne 0) {
            throw "Cycle ${Cycle}: Gate 14 runner failed with exit $($Process.ExitCode). Do not retry or unload manually."
        }

        $InnerSummary = Get-ChildItem `
            -LiteralPath $CycleRoot `
            -Recurse `
            -File `
            -Filter 'summary.txt' |
            Select-Object -First 1

        if (-not $InnerSummary) {
            throw "Cycle ${Cycle}: inner summary was not produced."
        }

        $InnerText = Get-Content -Raw -LiteralPath $InnerSummary.FullName
        $BeforeMatch = [regex]::Match(
            $InnerText, '(?m)^BeforeTransitions=(\d+)\s*$')
        $AfterMatch = [regex]::Match(
            $InnerText, '(?m)^AfterTransitions=(\d+)\s*$')
        $DeltaMatch = [regex]::Match(
            $InnerText, '(?m)^Delta=(\d+)\s*$')
        if ($InnerText -notmatch '(?m)^Result=PASS\s*$' -or
            $InnerText -notmatch '(?m)^ProbeStage=COMPLETE\s*$' -or
            -not $BeforeMatch.Success -or
            -not $AfterMatch.Success -or
            -not $DeltaMatch.Success) {
            throw "Cycle ${Cycle}: inner Gate 14 result was not a complete PASS."
        }

        $BeforeTransitions = [uint64]$BeforeMatch.Groups[1].Value
        $AfterTransitions = [uint64]$AfterMatch.Groups[1].Value
        $Delta = [uint64]$DeltaMatch.Groups[1].Value
        if ($AfterTransitions -le $BeforeTransitions -or
            $Delta -ne ($AfterTransitions - $BeforeTransitions)) {
            throw "Cycle ${Cycle}: inner Gate 14 transition proof was inconsistent."
        }

        $AfterOutput = & sc.exe query hypersinv 2>&1
        $AfterExit = $LASTEXITCODE
        $AfterOutput |
            Out-File (Join-Path $CycleRoot 'service-after-wrapper.txt') `
                -Encoding utf8

        if ($AfterExit -ne 1060) {
            throw "Cycle ${Cycle}: service removal was not confirmed."
        }

        $Summary.Add("Cycle${Cycle}=PASS;Before=$BeforeTransitions;After=$AfterTransitions;Delta=$Delta")
        Write-Host "PASS: cycle ${Cycle}/${TotalCycles}"
    }

    $Summary.Insert(0, 'Result=PASS')
    $Summary.Add("PassedCycles=$TotalCycles")
}
catch {
    $Failure = $_
    $Summary.Insert(0, 'Result=FAIL')
    $Summary.Add("Error=$($_.Exception.Message)")
    Write-Host $_.Exception.Message -ForegroundColor Red
}
finally {
    $Summary | Set-Content -LiteralPath $SummaryPath -Encoding utf8

    if ($TranscriptStarted) {
        Stop-Transcript | Out-Null
    }

    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }

    & tar.exe -a -c -f $ZipPath -C $EvidenceRoot $RunName
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Evidence archive failed: $ZipPath"
    }
    else {
        Write-Host "Evidence: $ZipPath"
    }
}

if ($Failure) {
    throw $Failure
}

Write-Host 'PASS: all 100 CR3-load VM-exit probe cycles completed.'
