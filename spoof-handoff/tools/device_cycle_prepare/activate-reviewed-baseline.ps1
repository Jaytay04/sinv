[CmdletBinding()]
param([switch]$Apply,[string]$ReportDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$worktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$repo=[IO.Path]::GetFullPath((Join-Path $worktree '..\..\..'))
$handoff=Join-Path $repo 'spoof-handoff'
$reports=Join-Path $handoff 'reports\device-reboot-cycle-20261016'
$pointerPath=Join-Path $handoff 'artifacts\device-reboot-cycle-20261016\prepared.json'
$proposalPath=Join-Path $reports 'proposed-main-baseline-reference.json'
$utf8=[Text.UTF8Encoding]::new($false)
function Save-ActivationJson([string]$Path,$Value){[IO.File]::WriteAllText($Path,(ConvertTo-Json -InputObject $Value -Depth 20),$utf8)}
$pointer=Get-Content -LiteralPath $pointerPath -Raw|ConvertFrom-Json
$config=Get-Content -LiteralPath $pointer.configPath -Raw|ConvertFrom-Json
$statePath=Join-Path $config.runDirectory 'state.json'
$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
if($state.phase -cne 'Prepared'){throw ('Baseline activation requires Prepared state; actual='+$state.phase)}
if(-not $Apply){
    $ReportDirectory=Join-Path $reports ('baseline-activation.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($ReportDirectory)
    Save-ActivationJson (Join-Path $ReportDirectory 'request.json') ([ordered]@{expectedManifestSha256=$pointer.manifestSha256;expectedStateSha256=(Get-FileHash -LiteralPath $statePath).Hash;expectedProposalSha256=(Get-FileHash -LiteralPath $proposalPath).Hash;authorization='User approved activating the reviewed cycle baseline while preserving the reference document and original baseline.'})
    $process=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Apply','-ReportDirectory',('"'+$ReportDirectory+'"')) -Verb RunAs -WindowStyle Hidden -PassThru -Wait
    if($process.ExitCode){throw ('Baseline activation failed: exit='+$process.ExitCode+'; inspect '+$ReportDirectory)}
    Get-Content -LiteralPath (Join-Path $ReportDirectory 'verdict.json') -Raw
    exit 0
}
$ReportDirectory=[IO.Path]::GetFullPath($ReportDirectory)
if(-not $ReportDirectory.StartsWith($reports+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected report path.'}
$principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevation required for protected package metadata.'}
$request=Get-Content -LiteralPath (Join-Path $ReportDirectory 'request.json') -Raw|ConvertFrom-Json
$newBaselinePath=Join-Path $config.payloadDirectory 'reviewed-main-baseline-reference.json'
if(Test-Path -LiteralPath $newBaselinePath){throw 'An active reviewed baseline already exists; inspect its activation evidence.'}
Import-Module -Name (Join-Path $config.payloadDirectory 'runtime.psm1') -Force -DisableNameChecking
$lock=[IO.File]::Open((Join-Path $config.runDirectory 'runtime.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$backedUp=$false;$baselineCreated=$false;$success=$false
$backup=Join-Path $ReportDirectory 'previous';[void][IO.Directory]::CreateDirectory($backup)
$preparationPath=Join-Path $config.runDirectory 'preparation-verdict.json'
$targets=@($pointer.configPath,$config.manifestPath,$pointerPath,$preparationPath)
try{
    if($pointer.manifestSha256 -cne $request.expectedManifestSha256 -or (Get-FileHash -LiteralPath $statePath).Hash -cne $request.expectedStateSha256 -or (Get-FileHash -LiteralPath $proposalPath).Hash -cne $request.expectedProposalSha256){throw 'Approved inputs or cycle state changed before activation.'}
    $oldVerified=Assert-DeviceCycleManifest -ManifestPath $config.manifestPath -PayloadDirectory $config.payloadDirectory -RepoRoot $repo -ExpectedManifestSha256 $request.expectedManifestSha256
    $proposal=Get-Content -LiteralPath $proposalPath -Raw|ConvertFrom-Json
    $originalPath=[string]$proposal.originalBaselinePath
    if((Get-FileHash -LiteralPath $originalPath).Hash -cne $proposal.originalBaselineSha256){throw 'Original baseline changed; activation refused.'}
    $original=Get-Content -LiteralPath $originalPath -Raw|ConvertFrom-Json
    if($proposal.files.Count -ne 1195 -or $original.files.Count -ne 1195){throw 'Expected 1195 baseline entries.'}
    $changes=@();$files=@(foreach($pin in $proposal.files){
        $old=@($original.files|Where-Object{$_.path -ceq $pin.path});if($old.Count -ne 1){throw ('Baseline path is not unique: '+$pin.path)}
        $path=[IO.Path]::GetFullPath((Join-Path $repo $pin.path));Assert-DeviceCycleNoReparsePath -Path $path -Boundary $repo
        $actual=(Get-FileHash -LiteralPath $path).Hash
        if($actual -cne $pin.sha256){throw ('Approved baseline mismatch: '+$pin.path+'; expected='+$pin.sha256+'; actual='+$actual)}
        if($pin.sha256 -cne $old[0].sha256){$changes+=[pscustomobject]@{path=$pin.path;originalSha256=$old[0].sha256;approvedSha256=$pin.sha256}}
        [pscustomobject]@{path=[string]$pin.path;sha256=[string]$pin.sha256}
    })
    if($changes.Count -ne 1){throw ('Reviewed proposal should contain exactly one changed reference: actual='+$changes.Count)}
    $head=(& git -c ('safe.directory='+$repo) -C $repo rev-parse HEAD).Trim();if($LASTEXITCODE){throw 'HEAD read failed.'}
    $indexHash=(Get-FileHash -LiteralPath (Join-Path $repo '.git\index')).Hash
    if($head -cne $proposal.head -or $indexHash -cne $proposal.indexSha256){throw 'Main HEAD or index differs from the approved proposal.'}
    $secureBootBefore=[bool](Confirm-SecureBootUEFI)
    $bcdBefore=@(& bcdedit.exe /enum all /v);if($LASTEXITCODE){throw 'Read-only BCD enumeration failed.'}
    [IO.File]::WriteAllLines((Join-Path $ReportDirectory 'bcd-before.txt'),[string[]]$bcdBefore,$utf8)
    foreach($path in $targets){Copy-Item -LiteralPath $path -Destination (Join-Path $backup ([IO.Path]::GetFileName($path)))}
    $backedUp=$true
    $active=[ordered]@{schema=1;reviewOnly=$false;active=$true;capturedUtc=[DateTime]::UtcNow.ToString('o');originalBaselinePath=$originalPath;originalBaselineSha256=$proposal.originalBaselineSha256;approvedProposalSha256=$request.expectedProposalSha256;approval=$request.authorization;approvedChanges=$changes;head=$head;indexSha256=$indexHash;files=$files}
    Save-ActivationJson $newBaselinePath $active;$baselineCreated=$true
    $ownerOutput=@(& icacls.exe $newBaselinePath /setowner '*S-1-5-32-544' /Q 2>&1);$ownerExit=$LASTEXITCODE
    Save-ActivationJson (Join-Path $ReportDirectory 'baseline-owner.json') ([ordered]@{exitCode=$ownerExit;output=$ownerOutput})
    if($ownerExit){throw 'Protected baseline owner update failed.'}
    $config.baselineReferencePath=$newBaselinePath
    Write-DeviceCycleJsonAtomic -Path $pointer.configPath -Value $config|Out-Null
    $manifest=Get-Content -LiteralPath $config.manifestPath -Raw|ConvertFrom-Json
    $configPin=@($manifest.pins|Where-Object{$_.path -ieq $pointer.configPath});if($configPin.Count -ne 1){throw 'Config pin is not unique.'}
    $configPin[0].sha256=(Get-FileHash -LiteralPath $pointer.configPath).Hash
    $manifest.pins=@($manifest.pins)+@([pscustomobject]@{path=$newBaselinePath;sha256=(Get-FileHash -LiteralPath $newBaselinePath).Hash})
    Write-DeviceCycleJsonAtomic -Path $config.manifestPath -Value $manifest|Out-Null
    $newManifestHash=(Get-FileHash -LiteralPath $config.manifestPath).Hash
    $pointer.manifestSha256=$newManifestHash
    $pointer|Add-Member -MemberType NoteProperty -Name baselineActivationEvidence -Value $ReportDirectory -Force
    Write-DeviceCycleJsonAtomic -Path $pointerPath -Value $pointer|Out-Null
    $prepared=Get-Content -LiteralPath $preparationPath -Raw|ConvertFrom-Json;$prepared.manifestSha256=$newManifestHash
    $prepared|Add-Member -MemberType NoteProperty -Name baselineActivationEvidence -Value $ReportDirectory -Force
    Write-DeviceCycleJsonAtomic -Path $preparationPath -Value $prepared|Out-Null
    $verified=Assert-DeviceCycleManifest -ManifestPath $config.manifestPath -PayloadDirectory $config.payloadDirectory -RepoRoot $repo -ExpectedManifestSha256 $newManifestHash
    $bcdAfter=@(& bcdedit.exe /enum all /v);if($LASTEXITCODE){throw 'After-state BCD enumeration failed.'}
    [IO.File]::WriteAllLines((Join-Path $ReportDirectory 'bcd-after.txt'),[string[]]$bcdAfter,$utf8)
    if(($bcdBefore -join "`n") -cne ($bcdAfter -join "`n") -or $secureBootBefore -ne [bool](Confirm-SecureBootUEFI) -or (Get-FileHash -LiteralPath $statePath).Hash -cne $request.expectedStateSha256 -or (Get-FileHash -LiteralPath $originalPath).Hash -cne $proposal.originalBaselineSha256){throw 'BCD, Secure Boot, cycle state or original baseline changed across activation.'}
    $verdict=[ordered]@{result='PASS';activeBaselinePath=$newBaselinePath;activeBaselineSha256=(Get-FileHash -LiteralPath $newBaselinePath).Hash;baselineEntries=$files.Count;approvedChanges=$changes;originalBaselineUnchanged=$true;mainHeadAndIndexUnchanged=$true;manifestSha256=$newManifestHash;manifestPinsVerified=$verified.pins.Count;stateUnchanged=$true;phase='Prepared';secureBoot=$secureBootBefore;bcdUnchanged=$true;onlyRepoMetadataChanged=$true;noEnableExecuted=$true;noRestartExecuted=$true;ready=$false}
    Save-ActivationJson (Join-Path $ReportDirectory 'verdict.json') $verdict
    $success=$true
}catch{
    $failure=$_.Exception.ToString()
    if($backedUp){foreach($path in $targets){Copy-Item -LiteralPath (Join-Path $backup ([IO.Path]::GetFileName($path))) -Destination $path -Force}}
    if($baselineCreated){[IO.File]::Delete($newBaselinePath)}
    Save-ActivationJson (Join-Path $ReportDirectory 'verdict.json') ([ordered]@{result='FAIL';error=$failure;metadataRestored=$backedUp;noEnableExecuted=$true;noRestartExecuted=$true;ready=$false})
    [Console]::Error.WriteLine($failure)
}finally{$lock.Dispose()}
if(-not $success){exit 1}
