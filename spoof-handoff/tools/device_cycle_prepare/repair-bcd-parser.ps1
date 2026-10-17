[CmdletBinding()]
param([switch]$Apply,[string]$RepairDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$worktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$repo=[IO.Path]::GetFullPath((Join-Path $worktree '..\..\..'))
$handoff=Join-Path $repo 'spoof-handoff'
$pointerPath=Join-Path $handoff 'artifacts\device-reboot-cycle-20261016\prepared.json'
$thumb='59DBD6780CFE60C020B3F4511211FEC22C397B85'
$utf8=[Text.UTF8Encoding]::new($false)
function Save-RepairJson([string]$Path,$Value){[IO.File]::WriteAllText($Path,(ConvertTo-Json -InputObject $Value -Depth 20),$utf8)}
function Invoke-RepairCommand([string]$Name,[string]$Program,[string[]]$Arguments){
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Program
    foreach($argument in $Arguments){if($argument.Contains('"') -or $argument.EndsWith('\')){throw 'Unsupported native argument.'}}
    $start.Arguments=($Arguments|ForEach-Object{'"'+$_+'"'}) -join ' '
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start);$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$process.WaitForExit()
    $prefix=Join-Path $RepairDirectory $Name
    [IO.File]::WriteAllText($prefix+'.stdout.txt',$stdout.Result,$utf8);[IO.File]::WriteAllText($prefix+'.stderr.txt',$stderr.Result,$utf8)
    $result=[pscustomobject]@{program=$Program;arguments=$Arguments;exitCode=$process.ExitCode;stdout=$stdout.Result;stderr=$stderr.Result;stdoutPath=$prefix+'.stdout.txt';stderrPath=$prefix+'.stderr.txt'}
    Save-RepairJson ($prefix+'.command.json') $result
    return $result
}
function Assert-RepairExit($Result){if($Result.exitCode -ne 0){throw ('Command failed: exit='+$Result.exitCode+'; stdout='+$Result.stdoutPath+'; stderr='+$Result.stderrPath)}}
function Get-RepairTrust {
    @(foreach($store in @('Root','TrustedPublisher')){[pscustomobject]@{store=$store;thumbprints=@(Get-ChildItem -LiteralPath ('Cert:\LocalMachine\'+$store)|Sort-Object Thumbprint|ForEach-Object{$_.Thumbprint})}})
}
function Write-RepairAtomic([string]$Path,$Value){
    $temporary=$Path+'.repair.'+[Guid]::NewGuid().ToString('N');$backup=Join-Path $RepairDirectory ([IO.Path]::GetFileName($Path)+'.atomic-backup.'+[Guid]::NewGuid().ToString('N'))
    Save-RepairJson $temporary $Value;[IO.File]::Replace($temporary,$Path,$backup,$true)
}
$pointer=Get-Content -LiteralPath $pointerPath -Raw|ConvertFrom-Json
$config=Get-Content -LiteralPath $pointer.configPath -Raw|ConvertFrom-Json
$statePath=Join-Path $config.runDirectory 'state.json'
$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
if($state.phase -cne 'Prepared'){throw ('Parser repair requires untouched Prepared state; actual='+$state.phase)}
if(-not $Apply){
    $RepairDirectory=Join-Path $handoff ('reports\device-reboot-cycle-20261016\bcd-parser-repair.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($RepairDirectory)
    $moduleCopy=Join-Path $RepairDirectory 'runtime.psm1'
    Copy-Item -LiteralPath (Join-Path $worktree 'spoof-handoff\tools\device_cycle_runtime\runtime.psm1') -Destination $moduleCopy
    $certificate=Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$thumb)
    if(-not $certificate.HasPrivateKey){throw 'Existing private key unavailable.'}
    $signature=Set-AuthenticodeSignature -FilePath $moduleCopy -Certificate $certificate -HashAlgorithm SHA256
    if($null -eq $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -cne $thumb){throw 'Pinned signer was not produced.'}
    Save-RepairJson (Join-Path $RepairDirectory 'request.json') ([ordered]@{oldManifestSha256=$pointer.manifestSha256;newModuleSha256=(Get-FileHash -LiteralPath $moduleCopy).Hash;stateSha256=(Get-FileHash -LiteralPath $statePath).Hash;snapshotPath=$pointer.snapshotPath})
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Apply','-RepairDirectory',('"'+$RepairDirectory+'"'))
    $process=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -PassThru -Wait
    if($process.ExitCode){throw ('Signed parser repair failed: exit='+$process.ExitCode+'; inspect '+$RepairDirectory)}
    Get-Content -LiteralPath (Join-Path $RepairDirectory 'verdict.json') -Raw
    exit 0
}
$RepairDirectory=[IO.Path]::GetFullPath($RepairDirectory)
if(-not $RepairDirectory.StartsWith((Join-Path $handoff 'reports\device-reboot-cycle-20261016')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected repair output path.'}
$principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevation required to update protected signed payload.'}
$request=Get-Content -LiteralPath (Join-Path $RepairDirectory 'request.json') -Raw|ConvertFrom-Json
if($pointer.manifestSha256 -cne $request.oldManifestSha256 -or (Get-FileHash -LiteralPath $statePath).Hash -cne $request.stateSha256){throw 'Prepared state changed after signing; refusing repair.'}
$moduleCopy=Join-Path $RepairDirectory 'runtime.psm1'
if((Get-FileHash -LiteralPath $moduleCopy).Hash -cne $request.newModuleSha256){throw 'Repair module bytes changed after signing.'}
$lock=[IO.File]::Open((Join-Path $config.runDirectory 'runtime.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$trustBefore=Get-RepairTrust;$trustAfter=$null;$errorMessage=$null;$published=$false;$backedUp=$false;$restored=$false
$backupDirectory=Join-Path $RepairDirectory 'previous';[void][IO.Directory]::CreateDirectory($backupDirectory)
$moduleTarget=Join-Path $config.payloadDirectory 'runtime.psm1'
$receiptPath=Join-Path $config.runDirectory 'payload-signature-proof.json'
$preparationPath=Join-Path $config.runDirectory 'preparation-verdict.json'
$targets=@($moduleTarget,$receiptPath,$config.manifestPath,$pointerPath,$preparationPath)
try{
    if((Get-FileHash -LiteralPath $statePath).Hash -cne $request.stateSha256){throw 'Cycle state changed before the repair lock was acquired.'}
    $secureBootBefore=[bool](Confirm-SecureBootUEFI)
    $beforeBcd=Invoke-RepairCommand 'bcd-before' (Join-Path $env:SystemRoot 'System32\bcdedit.exe') @('/enum','all','/v');Assert-RepairExit $beforeBcd
    $currentBcd=Invoke-RepairCommand 'bcd-current' (Join-Path $env:SystemRoot 'System32\bcdedit.exe') @('/enum','{current}','/v');Assert-RepairExit $currentBcd
    foreach($store in $trustBefore){if($thumb -notin $store.thumbprints){Assert-RepairExit (Invoke-RepairCommand ('trust-add-'+$store.store) (Join-Path $env:SystemRoot 'System32\certutil.exe') @('-addstore',$store.store,$config.certificatePath))}}
    $signature=Get-AuthenticodeSignature -LiteralPath $moduleCopy
    if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -cne $thumb){throw ('Updated module signature failed: '+$signature.Status+'; '+$signature.StatusMessage)}
    Import-Module -Name $moduleCopy -Force -DisableNameChecking
    $setting=Get-DeviceCycleBcdSetting -Enumeration @($currentBcd.stdout -split "`r?`n") -Name testsigning
    Save-RepairJson (Join-Path $RepairDirectory 'live-bcd-parser-proof.json') ([ordered]@{result='PASS';bcdExitCode=$currentBcd.exitCode;lineCount=@($currentBcd.stdout -split "`r?`n").Count;blankLineCount=@(($currentBcd.stdout -split "`r?`n")|Where-Object{$_ -ceq ''}).Count;parsedTestSigning=$setting;signatureStatus=[string]$signature.Status;thumbprint=$thumb;source='live bcdedit /enum {current} /v; read only'})
    foreach($path in $targets){Copy-Item -LiteralPath $path -Destination (Join-Path $backupDirectory ([IO.Path]::GetFileName($path)))}
    $backedUp=$true
    Copy-Item -LiteralPath $moduleCopy -Destination $moduleTarget -Force
    $receiptDocument=Get-Content -LiteralPath $receiptPath -Raw|ConvertFrom-Json
    # Windows PowerShell can retain collection metadata on deserialized arrays.
    # Reconstruct plain rows before serializing the signature receipt again.
    if($null -ne $receiptDocument.PSObject.Properties['value']){$receiptDocument=$receiptDocument.value}
    $receipts=@(foreach($receipt in $receiptDocument){[pscustomobject][ordered]@{path=[string]$receipt.path;status=[string]$receipt.status;thumbprint=[string]$receipt.thumbprint;pass=[bool]$receipt.pass;sha256=[string]$receipt.sha256}})
    if($receipts.Count -ne 6){throw 'Expected six flat startup signature receipts.'}
    $rows=@($receipts|Where-Object{$_.path -ieq $moduleTarget});if($rows.Count -ne 1){throw 'Module signature receipt was not unique.'}
    $rows[0].sha256=$request.newModuleSha256;$rows[0].status='Valid';$rows[0].pass=$true;$rows[0].thumbprint=$thumb
    Write-RepairAtomic $receiptPath $receipts
    $receiptCheck=Get-Content -LiteralPath $receiptPath -Raw|ConvertFrom-Json
    if($null -ne $receiptCheck.PSObject.Properties['value'] -or @($receiptCheck).Count -ne 6){throw 'Updated signature receipts did not retain the flat six-row schema.'}
    $manifest=Get-Content -LiteralPath $config.manifestPath -Raw|ConvertFrom-Json
    foreach($path in @($moduleTarget,$receiptPath)){$pin=@($manifest.pins|Where-Object{$_.path -ieq $path});if($pin.Count -ne 1){throw ('Repair pin was not unique: '+$path)};$pin[0].sha256=(Get-FileHash -LiteralPath $path).Hash}
    Write-RepairAtomic $config.manifestPath $manifest
    $newHash=(Get-FileHash -LiteralPath $config.manifestPath).Hash
    $pointer.manifestSha256=$newHash;$pointer|Add-Member -MemberType NoteProperty -Name parserRepairEvidence -Value $RepairDirectory -Force
    Write-RepairAtomic $pointerPath $pointer
    $preparation=Get-Content -LiteralPath $preparationPath -Raw|ConvertFrom-Json;$preparation.manifestSha256=$newHash
    $preparation|Add-Member -MemberType NoteProperty -Name parserRepairEvidence -Value $RepairDirectory -Force
    Write-RepairAtomic $preparationPath $preparation
    $published=$true
}catch{$errorMessage=$_.Exception.ToString()}
finally{
    foreach($store in $trustBefore){if($thumb -notin $store.thumbprints -and (Test-Path -LiteralPath ('Cert:\LocalMachine\'+$store.store+'\'+$thumb))){try{Assert-RepairExit (Invoke-RepairCommand ('trust-restore-'+$store.store) (Join-Path $env:SystemRoot 'System32\certutil.exe') @('-delstore',$store.store,$thumb))}catch{$errorMessage=[string]$errorMessage+'; '+$_.Exception.ToString()}}}
    $trustAfter=Get-RepairTrust;$restored=(($trustBefore|ConvertTo-Json -Depth 5 -Compress) -ceq ($trustAfter|ConvertTo-Json -Depth 5 -Compress))
    Save-RepairJson (Join-Path $RepairDirectory 'trust-membership.json') ([ordered]@{before=$trustBefore;after=$trustAfter;restored=$restored})
    if(-not $restored){$errorMessage=[string]$errorMessage+'; machine trust membership did not restore.'}
    if($errorMessage -and $backedUp){foreach($path in $targets){Copy-Item -LiteralPath (Join-Path $backupDirectory ([IO.Path]::GetFileName($path))) -Destination $path -Force};$published=$false}
    $lock.Dispose()
}
try{
    if($errorMessage){throw $errorMessage}
    $status=Invoke-RepairCommand 'signed-status' (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $config.payloadDirectory 'runner.ps1'),'-Mode','Status','-ConfigPath',$pointer.configPath,'-ExpectedManifestSha256',$pointer.manifestSha256)
    if($status.exitCode -ne 2){throw ('Status expected exit=2 actual='+$status.exitCode+'; stderr='+$status.stderrPath)}
    $afterBcd=Invoke-RepairCommand 'bcd-after' (Join-Path $env:SystemRoot 'System32\bcdedit.exe') @('/enum','all','/v');Assert-RepairExit $afterBcd
    if($beforeBcd.stdout -cne $afterBcd.stdout -or $secureBootBefore -ne [bool](Confirm-SecureBootUEFI) -or (Get-FileHash -LiteralPath $statePath).Hash -cne $request.stateSha256){throw 'Read-only repair verification found a BCD, Secure Boot or phase-state difference.'}
    $verdict=[ordered]@{result='PASS';parserLiveBcdPass=$true;moduleSha256=$request.newModuleSha256;manifestSha256=$pointer.manifestSha256;phase='Prepared';stateUnchanged=$true;machineTrustRestored=$restored;bcdUnchanged=$true;secureBootUnchanged=$true;secureBoot=$secureBootBefore;preBiosSnapshotPreserved=$request.snapshotPath;noEnableExecuted=$true;noRestartExecuted=$true;ready=$false}
    Save-RepairJson (Join-Path $RepairDirectory 'verdict.json') $verdict
}catch{
    if($published -and $backedUp){foreach($path in $targets){Copy-Item -LiteralPath (Join-Path $backupDirectory ([IO.Path]::GetFileName($path))) -Destination $path -Force};$published=$false}
    Save-RepairJson (Join-Path $RepairDirectory 'verdict.json') ([ordered]@{result='FAIL';error=$_.Exception.ToString();published=$published;machineTrustRestored=$restored;ready=$false})
    [Console]::Error.WriteLine($_.Exception.ToString());exit 1
}
