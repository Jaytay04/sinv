[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ReportDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$checkWorktree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$checkRepo = [IO.Path]::GetFullPath((Join-Path $checkWorktree '..\..\..'))
$checkPackage = Join-Path $checkRepo 'spoof-handoff'
$checkArtifacts = Join-Path $checkPackage 'artifacts\device-runtime-package-20261016'
if (-not ([IO.Path]::GetFullPath($ReportDirectory)).StartsWith($checkPackage+'\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Reports must remain under spoof-handoff.'
}
if (Test-Path -LiteralPath $ReportDirectory) { throw 'The check report directory already exists.' }
New-Item -ItemType Directory -Path $ReportDirectory | Out-Null
function Save-CheckJson([string]$Name,$Value) {
    [IO.File]::WriteAllText((Join-Path $ReportDirectory $Name),(ConvertTo-Json -InputObject $Value -Depth 10),[Text.UTF8Encoding]::new($false))
}
function Invoke-CheckProcess([string]$Name,[string]$Program,[string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    foreach ($argument in $Arguments) {
        if ($argument.Contains('"') -or $argument.EndsWith('\')) { throw 'Unsupported process argument.' }
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
    $outPath = Join-Path $ReportDirectory ($Name+'.stdout.txt')
    $errPath = Join-Path $ReportDirectory ($Name+'.stderr.txt')
    [IO.File]::WriteAllText($outPath,$stdout.Result,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($errPath,$stderr.Result,[Text.UTF8Encoding]::new($false))
    [pscustomobject]@{name=$Name;program=$Program;arguments=$Arguments;exitCode=$process.ExitCode;stdout=$outPath;stderr=$errPath}
}
$checkCommands = @()
$checkErrors = @()
$checkBlocks = @()
$checkStage = $null
$checkDisk = $null
try {
    $checkNative = Join-Path $checkPackage '.build\device-native-io-20261016\native_device_io.exe'
    $checkNativeReport = Join-Path $ReportDirectory 'native-io.json'
    $checkRead = Invoke-CheckProcess 'native-io' $checkNative @('--capture',$checkNativeReport)
    $checkCommands += $checkRead
    if ($checkRead.exitCode -ne 0) { $checkErrors += ('Native IOCTL/OID query failed: exit='+$checkRead.exitCode+'; stderr='+$checkRead.stderr) }
    $checkCapture = Get-Content -LiteralPath $checkNativeReport -Raw | ConvertFrom-Json
    $checkRows = Import-Csv -LiteralPath (Join-Path $checkPackage 'reports\client-originals.tsv') -Delimiter "`t"
    $checkOriginalDisk = @($checkRows | Where-Object {$_.member -ceq 'FakeDiskSerial'})
    $checkOriginalNics = @($checkRows | Where-Object {$_.member -ceq 'FakeMacAddress'})
    if ($checkOriginalDisk.Count -ne 1 -or $checkOriginalNics.Count -ne 4) { throw 'Locked device map must have exactly one disk and four selected adapters.' }
    if ($checkCapture.captureComplete -ne $true) { $checkErrors += 'Independent native capture is incomplete.' }
    if ($checkCapture.disk.serial -cne $checkOriginalDisk[0].original) {
        $checkErrors += ('Disk original mismatch: expected='+$checkOriginalDisk[0].original+'; actual='+$checkCapture.disk.serial)
    }
    foreach ($checkRow in $checkOriginalNics) {
        $checkPnp = ($checkRow.device -split '\|',2)[1]
        $checkMatch = @($checkCapture.adapters | Where-Object {$_.pnpDeviceId -ceq $checkPnp})
        if ($checkMatch.Count -ne 1) { $checkErrors += ('Native adapter identity count: '+$checkPnp+'; expected=1; actual='+$checkMatch.Count); continue }
        if ($checkMatch[0].mac -cne $checkRow.original) {
            $checkErrors += ('Adapter original mismatch: '+$checkPnp+'; expected='+$checkRow.original+'; actual='+$checkMatch[0].mac)
        }
    }
} catch { $checkErrors += $_.Exception.Message }
$checkSignTool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe'
foreach ($checkPair in @(@('storage','client_storage_filter.cat','client_storage_filter.inf'),@('nic','client_nic_filter.cat','client_nic_filter.inf'))) {
    try {
        $checkFolder = Join-Path $checkArtifacts $checkPair[0]
        $checkCat = Join-Path $checkFolder $checkPair[1]
        $checkVerify = Invoke-CheckProcess ('verify-'+$checkPair[0]) $checkSignTool @('verify','/v','/pa',$checkCat)
        $checkCommands += $checkVerify
        if ($checkVerify.exitCode -ne 0) { $checkErrors += ('Catalog /pa verification failed: '+$checkPair[0]+'; exit='+$checkVerify.exitCode+'; stderr='+$checkVerify.stderr) }
        # Verify catalog membership for the exact INF and driver as well as the catalog signature.
        foreach ($checkMember in @($checkPair[2],($checkPair[2] -replace '\.inf$','.sys'))) {
            $checkMembership = Invoke-CheckProcess ('member-'+$checkPair[0]+'-'+$checkMember) $checkSignTool @('verify','/v','/pa','/c',$checkCat,(Join-Path $checkFolder $checkMember))
            $checkCommands += $checkMembership
            if ($checkMembership.exitCode -ne 0) { $checkErrors += ('Catalog member verification failed: '+$checkMember+'; exit='+$checkMembership.exitCode+'; stderr='+$checkMembership.stderr) }
        }
    } catch { $checkErrors += $_.Exception.Message }
}
try {
    $checkStageDir = Join-Path $ReportDirectory 'stage-only'
    $checkStageProcess = Invoke-CheckProcess 'stage-only' (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'stage-check.ps1'),'-PackageDirectory',$checkArtifacts,'-ReportDirectory',$checkStageDir)
    $checkCommands += $checkStageProcess
    $checkStage = Get-Content -LiteralPath (Join-Path $checkStageDir 'verdict.json') -Raw | ConvertFrom-Json
    if ($checkStageProcess.exitCode -ne 0 -or $checkStage.result -cne 'PASS') { $checkErrors += ('Stage-only transaction failed: exit='+$checkStageProcess.exitCode+'; evidence='+$checkStageDir) }
} catch { $checkErrors += $_.Exception.Message }
try {
    $checkDisk = Get-Disk -Number 0 | Select-Object Number,FriendlyName,IsBoot,IsSystem
    Save-CheckJson 'disk-lifecycle-requirements.json' $checkDisk
    if ($checkDisk.IsBoot -or $checkDisk.IsSystem) {
        $checkBlocks += 'PHYSICALDRIVE0 is the running boot/system disk. Its PnP filter installation requires a restart; same-session attachment/removal cannot be proven by calling DriverUnload.'
    }
} catch { $checkErrors += $_.Exception.Message }
$checkBlocks += 'No live install/bind/read/remove/restore transaction was executed. Catalog staging and original-only reads are not clean-detach or replacement/restoration proof.'
$checkBlocks += 'The current NIC filter transforms OID response buffers and does not implement the required interface-provider cache/WMI value substitution.'
$checkVerdict = [ordered]@{
    result='NOT-READY';exitCode=2;scope='Actual package staging, Authenticode/member verification and independent live original-read checks; privileged filter lifecycle is blocked.'
    commands=$checkCommands;errors=$checkErrors;blockingRequirements=$checkBlocks
    nativeReport=$(if(Test-Path -LiteralPath (Join-Path $ReportDirectory 'native-io.json')){Join-Path $ReportDirectory 'native-io.json'}else{$null})
    stageVerdict=$checkStage;bootDisk=$checkDisk;driverInstalledOrBound=$false
    cleanDetachExecuted=$false;replacementRestorationProven=$false;readinessProven=$false
    certificatesChanged=$false;bcdChanged=$false;firmwareChanged=$false;espChanged=$false;rebooted=$false
}
Save-CheckJson 'verdict.json' $checkVerdict
foreach ($checkMessage in @($checkErrors)+@($checkBlocks)) { [Console]::Error.WriteLine($checkMessage) }
[Console]::Error.WriteLine('NOT-READY: see '+(Join-Path $ReportDirectory 'verdict.json'))
exit 2
