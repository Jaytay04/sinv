[CmdletBinding()]
param([ValidateSet('Build','Snapshot')][string]$Mode='Build',[string]$ConfigPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$prepWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$prepRepo=[IO.Path]::GetFullPath((Join-Path $prepWorktree '..\..\..'))
$prepPackage=Join-Path $prepRepo 'spoof-handoff'
$prepRoot=Join-Path $prepPackage 'artifacts\device-reboot-cycle-20261016'
$prepReports=Join-Path $prepPackage 'reports\device-reboot-cycle-20261016'
$prepThumb='59DBD6780CFE60C020B3F4511211FEC22C397B85'
$prepCert=Join-Path $prepRepo 'components\hypervisor\hypersinv.cer'
$prepUtf8=[Text.UTF8Encoding]::new($false)
function Save-PrepJson([string]$Path,$Value){[IO.File]::WriteAllText($Path,(ConvertTo-Json -InputObject $Value -Depth 16),$prepUtf8)}
function Invoke-PrepTool([string]$Program,[string[]]$Arguments,[string]$LogPrefix){
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Program
    foreach($argument in $Arguments){if($argument.Contains('"') -or $argument.EndsWith('\')){throw 'Unsupported native argument.'}}
    $start.Arguments=($Arguments|ForEach-Object{'"'+$_+'"'}) -join ' '
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start);$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$process.WaitForExit()
    [IO.File]::WriteAllText($LogPrefix+'.stdout.txt',$stdout.Result,$prepUtf8)
    [IO.File]::WriteAllText($LogPrefix+'.stderr.txt',$stderr.Result,$prepUtf8)
    $result=[pscustomobject]@{program=$Program;arguments=$Arguments;exitCode=$process.ExitCode;stdout=$LogPrefix+'.stdout.txt';stderr=$LogPrefix+'.stderr.txt'}
    Save-PrepJson ($LogPrefix+'.command.json') $result
    return $result
}
function Assert-PrepExit($Result){if($Result.exitCode -ne 0){throw ('Command failed: exit='+$Result.exitCode+'; stdout='+$Result.stdout+'; stderr='+$Result.stderr)}}
function Get-PrepTrust {
    @(foreach($store in @('Root','TrustedPublisher')){
        $thumbprints=@(Get-ChildItem -LiteralPath ('Cert:\LocalMachine\'+$store)|Sort-Object Thumbprint|ForEach-Object{$_.Thumbprint})
        [pscustomobject]@{store=$store;thumbprints=$thumbprints;targetPresent=($prepThumb -in $thumbprints)}
    })
}
if((Get-FileHash -LiteralPath $prepCert -Algorithm SHA256).Hash -cne 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'){throw 'Existing certificate bytes changed.'}
if($Mode -ceq 'Build'){
    $prepPointer=Join-Path $prepRoot 'prepared.json'
    if(Test-Path -LiteralPath $prepPointer){
        $oldPointer=Get-Content -LiteralPath $prepPointer -Raw|ConvertFrom-Json
        $oldState=Get-Content -LiteralPath (Join-Path $oldPointer.runDirectory 'state.json') -Raw|ConvertFrom-Json
        if($oldState.phase -notin @('Prepared','Verified','Complete','FinalVerified')){throw ('Existing cycle is not idle: '+$oldState.phase+'; retain its recovery state.')}
    }
    $prepId=[Guid]::NewGuid().ToString('N')
    $prepRun=Join-Path $prepReports ('cycle.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+$prepId)
    $prepPayload=Join-Path $prepRoot ('payload.'+$prepId)
    New-Item -ItemType Directory -Path $prepRun,$prepPayload -Force|Out-Null
    $prepCopies=@(
        @('tools\device_cycle_runtime\runner.ps1','runner.ps1'),
        @('tools\device_cycle_runtime\runtime.psm1','runtime.psm1'),
        @('tools\device_cycle_proof\device-cycle-proof.psm1','device-cycle-proof.psm1'),
        @('tools\smbios_boot\snapshot-reference.ps1','snapshot-reference.ps1'),
        @('tools\secureboot_restore_verify\esp-policy.ps1','esp-policy.ps1')
    )
    foreach($copy in $prepCopies){Copy-Item -LiteralPath (Join-Path (Join-Path $prepWorktree 'spoof-handoff') $copy[0]) -Destination (Join-Path $prepPayload $copy[1])}
    Copy-Item -LiteralPath $prepCert -Destination (Join-Path $prepPayload 'hypersinv.cer')
    Copy-Item -LiteralPath (Join-Path $prepPackage '.build\device-native-io-20261016\native_device_io.exe') -Destination (Join-Path $prepPayload 'native_device_io.exe')
    Copy-Item -LiteralPath (Join-Path $prepRoot 'packages') -Destination (Join-Path $prepPayload 'packages') -Recurse
    $certificate=Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$prepThumb)
    if(-not $certificate.HasPrivateKey){throw 'The existing certificate private key is unavailable.'}
    $prepSigning=@(foreach($file in @(Get-ChildItem -LiteralPath $prepPayload -File|Where-Object{$_.Extension -in @('.ps1','.psm1','.exe')})){
        $signature=Set-AuthenticodeSignature -FilePath $file.FullName -Certificate $certificate -HashAlgorithm SHA256
        if($null -eq $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -cne $prepThumb){throw ('Signing did not produce the pinned signer: '+$file.Name+'; status='+$signature.Status)}
        [pscustomobject]@{path=$file.FullName;status=[string]$signature.Status;thumbprint=$signature.SignerCertificate.Thumbprint;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash}
    })
    Save-PrepJson (Join-Path $prepRun 'signing.json') $prepSigning
    $prepConfig=[ordered]@{
        schema=1;repoRoot=$prepRepo;worktreeRoot=$prepWorktree;runId=$prepId;runDirectory=$prepRun
        payloadDirectory=$prepPayload;snapshotPath=(Join-Path $prepRun 'recovery-before-bios')
        packageDirectory=(Join-Path $prepPayload 'packages');readerPath=(Join-Path $prepPayload 'native_device_io.exe')
        lockedTsvPath=(Join-Path $prepPackage 'reports\client-originals.tsv')
        proofModulePath=(Join-Path $prepPayload 'device-cycle-proof.psm1')
        snapshotLibraryPath=(Join-Path $prepPayload 'snapshot-reference.ps1');espPolicyPath=(Join-Path $prepPayload 'esp-policy.ps1')
        certificatePath=(Join-Path $prepPayload 'hypersinv.cer');certificateThumbprint=$prepThumb;taskName=('HypersinvDeviceCycle-'+$prepId)
        manifestPath=(Join-Path $prepPayload 'manifest.json')
        testVerifierPath=(Join-Path $prepWorktree 'spoof-handoff\tools\device_runtime_probe\verify-existing.ps1')
        baselineReferencePath=(Join-Path $prepPackage 'reports\bcd-live-variants-20261014\main-baseline-reference.json')
        smbiosStatePath=(Join-Path $prepPackage 'artifacts\smbios-seven-probe\prepared.json')
    }
    $ConfigPath=Join-Path $prepPayload 'config.json'
    Save-PrepJson $ConfigPath $prepConfig
    $prepSnapshotArgs=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Mode','Snapshot','-ConfigPath',('"'+$ConfigPath+'"'))
    try{
        $process=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $prepSnapshotArgs -Verb RunAs -WindowStyle Hidden -PassThru -Wait
        if($process.ExitCode -ne 0){throw ('Elevated preparation failed: exit='+$process.ExitCode+'; inspect '+$prepRun)}
    }catch{Save-PrepJson (Join-Path $prepRun 'preparation-launch-error.json') ([ordered]@{error=$_.Exception.Message;configPath=$ConfigPath;liveCycleExecuted=$false});throw}
    Get-Content -LiteralPath $prepPointer -Raw
    exit 0
}
if(-not $ConfigPath){throw 'Snapshot mode requires ConfigPath.'}
$prepConfig=Get-Content -LiteralPath $ConfigPath -Raw|ConvertFrom-Json
$prepRun=[IO.Path]::GetFullPath($prepConfig.runDirectory)
$prepPayload=[IO.Path]::GetFullPath($prepConfig.payloadDirectory)
foreach($path in @($prepRun,$prepPayload,$ConfigPath)){if(-not ([IO.Path]::GetFullPath($path)).StartsWith($prepPackage+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Preparation path escaped spoof-handoff.'}}
$prepAdmin=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if(-not $prepAdmin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Snapshot mode requires elevated PowerShell.'}
$prepBefore=Get-PrepTrust
Save-PrepJson (Join-Path $prepRun 'prepare-trust-before.json') $prepBefore
$prepError=$null;$prepRecovered=$false;$prepSignatures=@()
try{
    if(-not(Confirm-SecureBootUEFI)){throw 'Prepare must run before the BIOS visit, with Secure Boot ON.'}
    $snapshotSource=Join-Path $prepWorktree 'spoof-handoff\tools\secureboot_read\snapshot.ps1'
    Assert-PrepExit (Invoke-PrepTool (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',$snapshotSource,'-RepositoryRoot',$prepRepo,'-SnapshotDirectory',$prepConfig.snapshotPath) (Join-Path $prepRun 'fresh-snapshot'))
    foreach($store in $prepBefore){if(-not $store.targetPresent){Assert-PrepExit (Invoke-PrepTool (Join-Path $env:SystemRoot 'System32\certutil.exe') @('-addstore',$store.store,$prepCert) (Join-Path $prepRun ('add-'+$store.store)))}}
    foreach($file in @(Get-ChildItem -LiteralPath $prepPayload -File|Where-Object{$_.Extension -in @('.ps1','.psm1','.exe')})){
        $signature=Get-AuthenticodeSignature -LiteralPath $file.FullName
        $pass=($signature.Status -eq 'Valid' -and $signature.SignerCertificate.Thumbprint -ceq $prepThumb)
        $prepSignatures+=[ordered]@{path=$file.FullName;status=[string]$signature.Status;thumbprint=$signature.SignerCertificate.Thumbprint;pass=$pass;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash}
        if(-not $pass){throw ('Payload signature failed: '+$file.Name+'; status='+$signature.Status+'; message='+$signature.StatusMessage)}
    }
    $signTool='C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe'
    foreach($kind in @('storage','nic')){
        $base='client_'+$kind+'_filter';$folder=Join-Path $prepConfig.packageDirectory $kind;$cat=Join-Path $folder ($base+'.cat')
        Assert-PrepExit (Invoke-PrepTool $signTool @('verify','/v','/pa',$cat) (Join-Path $prepRun ('catalog-'+$kind)))
        foreach($ext in @('inf','sys')){Assert-PrepExit (Invoke-PrepTool $signTool @('verify','/v','/pa','/c',$cat,(Join-Path $folder ($base+'.'+$ext))) (Join-Path $prepRun ('member-'+$kind+'-'+$ext)))}
    }
    $prepStageHelper=Join-Path $prepWorktree 'spoof-handoff\tools\device_package_check\stage-check.ps1'
    Assert-PrepExit (Invoke-PrepTool (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',$prepStageHelper,'-PackageDirectory',$prepConfig.packageDirectory,'-ReportDirectory',(Join-Path $prepRun 'stage-only')) (Join-Path $prepRun 'stage-only'))
    Import-Module $prepConfig.proofModulePath -Force
    $prepNative=Join-Path $prepRun 'native-before.json'
    Assert-PrepExit (Invoke-PrepTool $prepConfig.readerPath @('--capture',$prepNative) (Join-Path $prepRun 'native-before'))
    $prepProof=Test-DeviceCycleNativeCapture -CapturePath $prepNative -LockedTsvPath $prepConfig.lockedTsvPath -Mode Original -NativeReaderPath $prepConfig.readerPath
    Save-PrepJson (Join-Path $prepRun 'native-before-proof.json') $prepProof
    [void](Assert-DeviceCycleProof $prepProof)
    Save-PrepJson (Join-Path $prepRun 'payload-signature-proof.json') $prepSignatures
}catch{$prepError=$_.Exception.Message}
finally{
    foreach($store in $prepBefore){if(-not $store.targetPresent -and (Test-Path -LiteralPath ('Cert:\LocalMachine\'+$store.store+'\'+$prepThumb))){
        try{Assert-PrepExit (Invoke-PrepTool (Join-Path $env:SystemRoot 'System32\certutil.exe') @('-delstore',$store.store,$prepThumb) (Join-Path $prepRun ('restore-'+$store.store)))}catch{$prepError=[string]$prepError+'; '+$_.Exception.Message}
    }}
    $prepAfter=Get-PrepTrust
    Save-PrepJson (Join-Path $prepRun 'prepare-trust-after.json') $prepAfter
    $prepRecovered=(($prepBefore|ConvertTo-Json -Depth 6 -Compress) -ceq ($prepAfter|ConvertTo-Json -Depth 6 -Compress))
    if(-not $prepRecovered){$prepError=[string]$prepError+'; machine certificate membership differs after preparation.'}
}
if($prepError){Save-PrepJson (Join-Path $prepRun 'preparation-verdict.json') ([ordered]@{result='FAIL';error=$prepError;machineTrustRestored=$prepRecovered;liveCycleExecuted=$false});[Console]::Error.WriteLine($prepError);exit 1}
try {
$prepState=[ordered]@{schema=1;phase='Prepared';runId=$prepConfig.runId;createdUtc=[DateTime]::UtcNow.ToString('o');snapshotPath=$prepConfig.snapshotPath;originalBeforePath=(Join-Path $prepRun 'native-before.json');originalBeforeProofPath=(Join-Path $prepRun 'native-before-proof.json');ready=$false;liveRoundTripProven=$false}
Save-PrepJson (Join-Path $prepRun 'state.json') $prepState
$prepPinPaths=@((Get-ChildItem -LiteralPath $prepPayload -Recurse -File).FullName)
$prepPinPaths+=@((Get-ChildItem -LiteralPath $prepConfig.snapshotPath -Recurse -File|Where-Object{$_.Name -notmatch '\.log$'}).FullName)
$prepPinPaths+=@($prepCert,$prepConfig.lockedTsvPath,$prepConfig.baselineReferencePath,$prepConfig.smbiosStatePath,$prepConfig.testVerifierPath,(Join-Path $prepRun 'native-before.json'),(Join-Path $prepRun 'native-before-proof.json'),(Join-Path $prepRun 'payload-signature-proof.json'))
# Include the snapshot library's immutable source receipts and referenced inputs.
$prepSmbiosState=Get-Content -LiteralPath $prepConfig.smbiosStatePath -Raw|ConvertFrom-Json
$prepPinPaths+=@($prepSmbiosState.pinnedFiles|ForEach-Object{$_.path})
$prepPins=@(foreach($path in ($prepPinPaths|Sort-Object -Unique)){[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}})
Save-PrepJson $prepConfig.manifestPath ([ordered]@{schema=1;pins=$prepPins})
$prepManifestHash=(Get-FileHash -LiteralPath $prepConfig.manifestPath -Algorithm SHA256).Hash
foreach($protected in @($prepPayload,$prepRun)){
    # Set inheritable directory rules once. Applying inheritance removal to each
    # child would strip file ACEs before the owner transition.
    Assert-PrepExit (Invoke-PrepTool (Join-Path $env:SystemRoot 'System32\icacls.exe') @($protected,'/inheritance:r','/grant:r','*S-1-5-18:(OI)(CI)F','*S-1-5-32-544:(OI)(CI)F','*S-1-5-32-545:(OI)(CI)RX','/Q') (Join-Path $prepRun ('protect-'+[IO.Path]::GetFileName($protected))))
    # An elevated administrator can assign its enabled owner group without
    # requiring the additional privilege needed to assign SYSTEM ownership.
    Assert-PrepExit (Invoke-PrepTool (Join-Path $env:SystemRoot 'System32\icacls.exe') @($protected,'/setowner','*S-1-5-32-544','/T','/Q') (Join-Path $prepRun ('owner-'+[IO.Path]::GetFileName($protected))))
}
$prepPointer=Join-Path $prepRoot 'prepared.json'
if(Test-Path -LiteralPath $prepPointer){Copy-Item -LiteralPath $prepPointer -Destination (Join-Path $prepRun 'previous-prepared-pointer.json')}
Save-PrepJson $prepPointer ([ordered]@{schema=1;configPath=$ConfigPath;manifestSha256=$prepManifestHash;runDirectory=$prepRun;snapshotPath=$prepConfig.snapshotPath;phase='Prepared';preparationPass=$true;ready=$false;liveRoundTripProven=$false;signatureEvidence=(Join-Path $prepRun 'payload-signature-proof.json');machineTrustRestored=$prepRecovered})
Save-PrepJson (Join-Path $prepRun 'preparation-verdict.json') ([ordered]@{result='PREPARED';machineTrustRestored=$prepRecovered;signaturesVerified=$true;rawOriginalsVerified=$true;freshSecureBootOnSnapshot=$true;liveCycleExecuted=$false;ready=$false;manifestSha256=$prepManifestHash})
'PREPARED: signed payload and fresh Secure Boot ON recovery snapshot. No filter installation, task, BCD change or reboot. Live round-trip readiness remains unproven.'
} catch {
    Save-PrepJson (Join-Path $prepRun 'preparation-verdict.json') ([ordered]@{result='FAIL';error=$_.Exception.Message;machineTrustRestored=$prepRecovered;liveCycleExecuted=$false;ready=$false})
    [Console]::Error.WriteLine($_.Exception.ToString())
    exit 1
}
