[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RunRoot,
    [Parameter(Mandatory=$true)]
    [ValidateSet('Probe', 'Capture')]
    [string]$Action,
    [Parameter(Mandatory=$true)]
    [ValidateSet('MET', 'NOT_MET')]
    [string]$ExpectedProbeVerdict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RunId = Split-Path -Leaf $RunRoot
if ($RunId -cnotmatch `
        '^uefi-windows-(probe|capture)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
    throw "Unsafe RunId: $RunId"
}
$ResultPath = Join-Path $RunRoot 'result.json'
$Result = [ordered]@{
    SchemaVersion = 1
    RunId = $RunId
    Status = 'FAIL'
    Message = 'Windows guest workflow failed.'
    CompletedUtc = $null
}

function Write-Result {
    $Result.CompletedUtc = [DateTime]::UtcNow.ToString('o')
    $Temporary = $ResultPath + '.tmp'
    $Result | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $Temporary -Encoding utf8
    Move-Item -LiteralPath $Temporary -Destination $ResultPath -Force
}

try {
    $NestedPrep = $Action -eq 'Capture' -and
        $ExpectedProbeVerdict -eq 'MET'
    $Expected = @{}
    foreach ($Line in Get-Content -LiteralPath (Join-Path $RunRoot 'SHA256SUMS.txt')) {
        if ($Line -cnotmatch '^([0-9A-F]{64})  ([A-Za-z0-9_.-]+)$') {
            throw "Malformed checksum line: $Line"
        }
        $Expected[$Matches[2]] = $Matches[1]
    }
    $RequiredFiles = @('ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1')
    foreach ($Name in $RequiredFiles) {
        $Path = Join-Path $RunRoot $Name
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "Staged file is missing: $Name"
        }
        $Actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        if ($Expected[$Name] -cne $Actual) {
            throw "Checksum mismatch: $Name"
        }
    }

    $Executable = Join-Path $RunRoot 'ib-tcg-dump.exe'
    $Stdout = Join-Path $RunRoot 'stdout.txt'
    $Stderr = Join-Path $RunRoot 'stderr.txt'
    if ($Action -eq 'Probe') {
        $ArgumentText = 'probe'
    }
    else {
        $ArgumentText = 'capture --out "{0}" --full "{1}"' -f
            (Join-Path $RunRoot 'tcglog.ib'),
            (Join-Path $RunRoot 'tcglog-full.bin')
    }
    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Executable
    $StartInfo.Arguments = $ArgumentText
    $StartInfo.WorkingDirectory = $RunRoot
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $StartInfo
    if (-not $Process.Start()) { throw 'ib-tcg-dump did not start.' }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit(300000)) {
        $Result.Message = 'ib-tcg-dump exceeded its five-minute deadline; it was not terminated.'
        Write-Result
        exit 124
    }
    $Process.WaitForExit()
    $ExitCode = [int]$Process.ExitCode
    $Output = [string]$StdoutTask.Result
    $ErrorOutput = [string]$StderrTask.Result
    [IO.File]::WriteAllText($Stdout, $Output, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($Stderr, $ErrorOutput,
        [Text.UTF8Encoding]::new($false))
    if ($Action -eq 'Probe') {
        $ExpectedText = if ($ExpectedProbeVerdict -eq 'MET') {
            'verdict: empty-bank precondition MET'
        }
        else {
            'verdict: empty-bank precondition NOT MET'
        }
        if ($Output -notmatch [regex]::Escape($ExpectedText)) {
            throw "Probe output did not contain the configured verdict: $ExpectedText"
        }
        $ExpectedExitCode = if ($ExpectedProbeVerdict -eq 'MET') { 0 } else { 1 }
        if ($ExitCode -ne $ExpectedExitCode) {
            throw "Probe exit code $ExitCode did not match $ExpectedExitCode."
        }
    }
    else {
        if ($ExitCode -ne 0) {
            throw "Capture exited with code $ExitCode."
        }
        foreach ($Name in @('tcglog.ib', 'tcglog-full.bin')) {
            $Path = Join-Path $RunRoot $Name
            if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
                (Get-Item -LiteralPath $Path).Length -eq 0) {
                throw "Capture artifact is absent or empty: $Name"
            }
        }
    }

    if ($NestedPrep) {
        $BitLockerOutput = Join-Path $RunRoot 'bitlocker-status.txt'
        $ManageBde = Join-Path $env:SystemRoot 'System32\manage-bde.exe'
        $BitLockerText = (& $ManageBde -status C: 2>&1 | Out-String)
        $BitLockerText |
            Set-Content -LiteralPath $BitLockerOutput -Encoding utf8
        if ($LASTEXITCODE -ne 0) {
            throw "manage-bde status exited with code $LASTEXITCODE."
        }

        $BcdEdit = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
        & $BcdEdit /debug '{current}' off | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /debug off exited with code $LASTEXITCODE."
        }
        & $BcdEdit /bootdebug '{current}' off | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /bootdebug off exited with code $LASTEXITCODE."
        }

        $EspDrive = $null
        foreach ($Letter in @('R','S','T','U','V','W','X','Y','Z')) {
            if (-not (Test-Path -LiteralPath ($Letter + ':\'))) {
                $EspDrive = $Letter + ':'
                break
            }
        }
        if ($null -eq $EspDrive) { throw 'No bounded ESP mount letter is free.' }
        try {
            & "$env:SystemRoot\System32\mountvol.exe" $EspDrive /S | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "mountvol /S exited with code $LASTEXITCODE."
            }
            $BootManager = $EspDrive + '\EFI\Microsoft\Boot\bootmgfw.efi'
            if (-not (Test-Path -LiteralPath $BootManager -PathType Leaf)) {
                throw 'The Windows boot manager was not found on the mounted ESP.'
            }
            $BootManagerHash = (Get-FileHash -LiteralPath $BootManager `
                -Algorithm SHA256).Hash
            $BootManagerHash |
                Set-Content -LiteralPath (Join-Path $RunRoot 'bootmgfw.sha256') `
                    -Encoding ascii
        }
        finally {
            if ($null -ne $EspDrive) {
                & "$env:SystemRoot\System32\mountvol.exe" $EspDrive /D |
                    Out-Null
            }
        }

        $PersistentRoot = Join-Path $env:ProgramData 'UEFILoaderNestedE2E'
        New-Item -ItemType Directory -Path $PersistentRoot -Force | Out-Null
        $StartupScript = Join-Path $PersistentRoot 'Invoke-NestedE2ECapture.ps1'
        $StartupScriptText = [Text.Encoding]::UTF8.GetString(
            [Convert]::FromBase64String('__NESTED_SCRIPT_BASE64__'))
        [IO.File]::WriteAllText($StartupScript, $StartupScriptText,
            [Text.UTF8Encoding]::new($false))
        $TaskCommand = '"{0}" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{1}"' -f `
            "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe", `
            $StartupScript
        & "$env:SystemRoot\System32\schtasks.exe" /Create /TN `
            'UEFILoader-Nested-E2E' /SC ONSTART /RU SYSTEM /RL HIGHEST `
            /TR $TaskCommand /F | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "schtasks /Create exited with code $LASTEXITCODE."
        }
        & "$env:SystemRoot\System32\schtasks.exe" /Query /TN `
            'UEFILoader-Nested-E2E' | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw 'The nested E2E startup task could not be verified.'
        }
        [IO.File]::AppendAllText($Stdout,
            "`nNESTED_PREP_BOOTMGFW_SHA256=$BootManagerHash`n" +
            "NESTED_PREP_TASK=INSTALLED`n" +
            "NESTED_PREP_KERNEL_DEBUG=DISABLED`n" +
            "NESTED_PREP_BITLOCKER_BEGIN`n$BitLockerText" +
            "NESTED_PREP_BITLOCKER_END`n",
            [Text.UTF8Encoding]::new($false))
    }

    $Result.Status = 'PASS'
    $Result.Message = if ($Action -eq 'Probe') {
        "Windows TPM/TBS probe matched $ExpectedProbeVerdict."
    }
    elseif ($NestedPrep) {
        'Windows reference log captured and one-shot nested E2E startup task installed.'
    }
    else {
        'Windows TBS capture produced both bounded and full logs.'
    }
    Write-Result
    exit 0
}
catch {
    $Result.Message = $_.Exception.Message
    Write-Result
    exit 1
}
