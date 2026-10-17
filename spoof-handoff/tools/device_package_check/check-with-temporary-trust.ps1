[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ReportDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$trustWorktree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$trustRepo = [IO.Path]::GetFullPath((Join-Path $trustWorktree '..\..\..'))
if (-not ([IO.Path]::GetFullPath($ReportDirectory)).StartsWith((Join-Path $trustRepo 'spoof-handoff')+'\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Reports must remain under spoof-handoff.'
}
if (Test-Path -LiteralPath $ReportDirectory) { throw 'The trust report directory already exists.' }
$trustIdentity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $trustIdentity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'This temporary machine-store transaction requires elevated PowerShell.'
}
$trustCert = Join-Path $trustRepo 'components\hypervisor\hypersinv.cer'
$trustThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
if ((Get-FileHash -LiteralPath $trustCert -Algorithm SHA256).Hash -cne 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE') { throw 'The signing certificate differs from its pinned bytes.' }
New-Item -ItemType Directory -Path $ReportDirectory | Out-Null
function Get-TrustCheckSnapshot {
    @(foreach ($store in @('Root','TrustedPublisher')) {
        $certificates = @(Get-ChildItem -LiteralPath ('Cert:\LocalMachine\'+$store) | Sort-Object Thumbprint | ForEach-Object {$_.Thumbprint})
        [pscustomobject]@{store=$store;thumbprints=$certificates;targetPresent=($trustThumbprint -in $certificates)}
    })
}
function Save-TrustCheckJson([string]$Name,$Value) {
    [IO.File]::WriteAllText((Join-Path $ReportDirectory $Name),(ConvertTo-Json -InputObject $Value -Depth 10),[Text.UTF8Encoding]::new($false))
}
function Invoke-TrustCheckTool([string]$Name,[string]$Program,[string[]]$Arguments) {
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$Program
    foreach ($argument in $Arguments) { if ($argument.Contains('"') -or $argument.EndsWith('\')) { throw 'Unsupported process argument.' } }
    $start.Arguments=($Arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $outPath=Join-Path $ReportDirectory ($Name+'.stdout.txt'); $errPath=Join-Path $ReportDirectory ($Name+'.stderr.txt')
    [IO.File]::WriteAllText($outPath,$stdout.Result,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($errPath,$stderr.Result,[Text.UTF8Encoding]::new($false))
    [pscustomobject]@{program=$Program;arguments=$Arguments;exitCode=$process.ExitCode;stdout=$outPath;stderr=$errPath}
}
$trustBefore=Get-TrustCheckSnapshot
Save-TrustCheckJson 'trust-before.json' $trustBefore
$trustCommands=@(); $trustErrors=@(); $trustCleanup=@(); $trustCheckExit=$null; $trustRestored=$false
try {
    foreach ($store in $trustBefore) {
        if ($store.targetPresent) { continue }
        $command=Invoke-TrustCheckTool ('add-'+$store.store) (Join-Path $env:SystemRoot 'System32\certutil.exe') @('-addstore',$store.store,$trustCert)
        $trustCommands+=$command
        if ($command.exitCode -ne 0) { throw ('Certificate import failed: '+$store.store+'; exit='+$command.exitCode+'; stderr='+$command.stderr) }
    }
    $trustCheck=Invoke-TrustCheckTool 'package-check' (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'check.ps1'),'-ReportDirectory',(Join-Path $ReportDirectory 'checks'))
    $trustCommands+=$trustCheck; $trustCheckExit=$trustCheck.exitCode
} catch { $trustErrors+=$_.Exception.Message }
finally {
    foreach ($store in $trustBefore) {
        if ($store.targetPresent) { continue }
        try {
            if (Test-Path -LiteralPath ('Cert:\LocalMachine\'+$store.store+'\'+$trustThumbprint)) {
                $cleanup=Invoke-TrustCheckTool ('remove-'+$store.store) (Join-Path $env:SystemRoot 'System32\certutil.exe') @('-delstore',$store.store,$trustThumbprint)
                $trustCleanup+=$cleanup
                if ($cleanup.exitCode -ne 0) { throw ('Certificate cleanup failed: '+$store.store+'; exit='+$cleanup.exitCode) }
            }
        } catch { $trustErrors+=$_.Exception.Message }
    }
    try {
        $trustAfter=Get-TrustCheckSnapshot
        Save-TrustCheckJson 'trust-after.json' $trustAfter
        $trustRestored=(($trustBefore | ConvertTo-Json -Depth 6 -Compress) -ceq ($trustAfter | ConvertTo-Json -Depth 6 -Compress))
        if (-not $trustRestored) { $trustErrors+='Certificate store membership differs; inspect before/after records. No pre-existing certificate was deleted.' }
    } catch { $trustErrors+=$_.Exception.Message }
}
Save-TrustCheckJson 'verdict.json' ([ordered]@{
    result=$(if($trustErrors.Count -eq 0 -and $trustRestored){'RECOVERED'}else{'RECOVERY-FAILED'})
    packageCheckExitCode=$trustCheckExit;commands=$trustCommands;cleanup=$trustCleanup;errors=$trustErrors
    machineStoresRestored=$trustRestored;certificate=$trustCert;thumbprint=$trustThumbprint
    firmwareChanged=$false;bcdChanged=$false;driverInstallationAttempted=$false
    note='Only temporary certificate trust and stage-only Driver Store validation were authorized here. A recovered transaction is not a successful device cycle.'
})
if ($trustErrors.Count -gt 0 -or -not $trustRestored) { $trustErrors | ForEach-Object {[Console]::Error.WriteLine($_)}; exit 1 }
if ($null -eq $trustCheckExit) { exit 1 }
exit $trustCheckExit
