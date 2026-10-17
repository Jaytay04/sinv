[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$ReportDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$stageWorktree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$stageRepo = [IO.Path]::GetFullPath((Join-Path $stageWorktree '..\..\..'))
$stageBoundary = Join-Path $stageRepo 'spoof-handoff'
foreach ($stagePath in @($PackageDirectory,$ReportDirectory)) {
    if (-not ([IO.Path]::GetFullPath($stagePath)).StartsWith($stageBoundary+'\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Packages and reports must remain under spoof-handoff.'
    }
}
if (Test-Path -LiteralPath $ReportDirectory) { throw 'The stage report directory already exists.' }
New-Item -ItemType Directory -Path $ReportDirectory | Out-Null
function Invoke-StageCommand([string]$Name,[string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $env:SystemRoot 'System32\pnputil.exe'
    # Arguments are controlled package-file paths and published OEM INF names.
    foreach ($argument in $Arguments) {
        if ($argument.Contains('"') -or $argument.EndsWith('\')) { throw 'Unsupported command argument.' }
    }
    $start.Arguments = ($Arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $outputPath = Join-Path $ReportDirectory ($Name+'.stdout.txt')
    $errorPath = Join-Path $ReportDirectory ($Name+'.stderr.txt')
    [IO.File]::WriteAllText($outputPath,$stdout.Result,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($errorPath,$stderr.Result,[Text.UTF8Encoding]::new($false))
    [pscustomobject]@{program=$start.FileName;arguments=$Arguments;exitCode=$process.ExitCode;stdout=$outputPath;stderr=$errorPath}
}
function Get-StageDriverInventory {
    @(Get-WindowsDriver -Online | Sort-Object Driver | ForEach-Object {
        [pscustomobject]@{publishedName=$_.Driver;originalFile=$_.OriginalFileName;provider=$_.ProviderName;class=$_.ClassName;version=[string]$_.Version}
    })
}
function Save-StageJson([string]$Name,$Value) {
    [IO.File]::WriteAllText((Join-Path $ReportDirectory $Name),(ConvertTo-Json -InputObject $Value -Depth 8),[Text.UTF8Encoding]::new($false))
}
$stageBefore = @()
$stageOwned = @()
$stageCommands = @()
$stageCleanup = @()
$stageErrors = @()
$stageBeforeCaptured = $false
$stageClean = $false
try {
    $stageIdentity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not $stageIdentity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Stage-check requires elevated PowerShell; no Driver Store changes were attempted.'
    }
    $stageBefore = Get-StageDriverInventory
    $stageBeforeCaptured = $true
    Save-StageJson 'drivers-before.json' $stageBefore
    foreach ($stagePair in @(@('storage','client_storage_filter.inf'),@('nic','client_nic_filter.inf'))) {
        $stageInf = Join-Path (Join-Path $PackageDirectory $stagePair[0]) $stagePair[1]
        if (-not (Test-Path -LiteralPath $stageInf -PathType Leaf)) { throw ('Package INF missing: '+$stageInf) }
        $stageSourceHash = (Get-FileHash -LiteralPath $stageInf -Algorithm SHA256).Hash
        $stageCommand = Invoke-StageCommand ('stage-'+$stagePair[0]) @('/add-driver',$stageInf)
        $stageCommands += $stageCommand
        # Stage only: never /install, never bind or restart an adapter or disk.
        $stageAfterAdd = Get-StageDriverInventory
        foreach ($stageDriver in $stageAfterAdd) {
            if ($stageDriver.publishedName -in @($stageBefore | ForEach-Object {$_.publishedName}) -or
                $stageDriver.publishedName -in @($stageOwned | ForEach-Object {$_.publishedName})) { continue }
            if ([IO.Path]::GetFileName($stageDriver.originalFile) -cne $stagePair[1]) { continue }
            if ((Get-FileHash -LiteralPath $stageDriver.originalFile -Algorithm SHA256).Hash -cne $stageSourceHash) {
                throw ('New package has an unexpected INF hash: '+$stageDriver.publishedName)
            }
            $stageOwned += $stageDriver
        }
        Save-StageJson 'owned-staged-packages.json' $stageOwned
        if ($stageCommand.exitCode -ne 0) { $stageErrors += ('pnputil stage failed: exit='+$stageCommand.exitCode+'; stdout='+$stageCommand.stdout+'; stderr='+$stageCommand.stderr) }
    }
} catch {
    $stageErrors += $_.Exception.Message
} finally {
    foreach ($stageDriver in $stageOwned) {
        try {
            if ($stageDriver.publishedName -notmatch '^oem[0-9]+\.inf$') { throw 'Unexpected published INF name; refusing deletion.' }
            # Delete only an exact new package this invocation staged. No /force or /uninstall.
            $stageRemoval = Invoke-StageCommand ('remove-'+$stageDriver.publishedName) @('/delete-driver',$stageDriver.publishedName)
            $stageCleanup += $stageRemoval
            if ($stageRemoval.exitCode -ne 0) { throw ('Stage cleanup failed: '+$stageDriver.publishedName+'; exit='+$stageRemoval.exitCode) }
        } catch { $stageErrors += $_.Exception.Message }
    }
    if ($stageBeforeCaptured) {
        try {
            $stageFinal = Get-StageDriverInventory
            Save-StageJson 'drivers-after.json' $stageFinal
            $stageClean = (($stageBefore | ConvertTo-Json -Depth 5 -Compress) -ceq ($stageFinal | ConvertTo-Json -Depth 5 -Compress))
            if (-not $stageClean) { $stageErrors += 'Driver Store inventory differs after cleanup; inspect before/after records. No unrelated package was removed.' }
        } catch { $stageErrors += $_.Exception.Message }
    }
}
$stagePassed = ($stageCommands.Count -eq 2 -and $stageClean -and $stageErrors.Count -eq 0)
Save-StageJson 'verdict.json' ([ordered]@{
    result=$(if($stagePassed){'PASS'}else{'FAIL'});commands=$stageCommands;cleanup=$stageCleanup;errors=$stageErrors
    driverStoreInventoryRestored=$stageClean;installationAttempted=$false;bindingAttempted=$false
    driverRoundTripProven=$false;ownedPackages=$stageOwned
})
if (-not $stagePassed) { $stageErrors | ForEach-Object {[Console]::Error.WriteLine($_)}; exit 1 }
'PASS: both packages staged without installation and owned additions removed; Driver Store inventory restored.'
