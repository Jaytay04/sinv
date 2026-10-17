param([ValidateSet('Build','Prepare','Status','Arm','Collect','Rollback','Verify')][string]$Mode='Status')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$readMode=$Mode
$readWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$readRepo=[IO.Path]::GetFullPath((Join-Path $readWorktree '..\..\..'))
$readPackage=Join-Path $readRepo 'spoof-handoff'
$readReports=Join-Path $readPackage 'reports\secureboot-read-20261016'
$readArtifacts=Join-Path $readPackage 'artifacts\secureboot-read-probe'
$readInventory=Join-Path $readArtifacts 'inventory'
$readSigned=Join-Path $readWorktree 'spoof-handoff\artifacts\secureboot-read-probe\signed'
$readLoader=Join-Path $readRepo '.codex-build\efi_shim_loader'
$readStatePath=Join-Path $readArtifacts 'prepared.json'
$readLeaf='HypersinvSecureBootRead.txt'
$readChain=Join-Path $readRepo 'tools\efi_inventory\shim-chain-collector.ps1'
New-Item -ItemType Directory -Path $readReports -Force|Out-Null
function New-ReadPath([string]$Kind,[string]$Extension='json'){Join-Path $readReports ($Kind+'.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N')+'.'+$Extension)}
function Invoke-ReadChild([string]$Script,[string[]]$Arguments,[string]$Log){
    $previous=$ErrorActionPreference
    try{$ErrorActionPreference='Continue';$output=@(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments 2>&1);$code=$LASTEXITCODE}finally{$ErrorActionPreference=$previous}
    [IO.File]::WriteAllLines($Log,[string[]](@($output|ForEach-Object{[string]$_})+('EXIT='+$code)),[Text.UTF8Encoding]::new($false))
    $output|ForEach-Object{Write-Output ([string]$_)}
    if($code){throw ('Step failed (exit '+$code+'): '+$Script+'; exact output: '+$Log)}
}
function Save-ReadState($State){
    $temporary=$readStatePath+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    [IO.File]::WriteAllText($temporary,($State|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
    if(Test-Path -LiteralPath $readStatePath){[IO.File]::Replace($temporary,$readStatePath,($readStatePath+'.'+[Guid]::NewGuid().ToString('N')+'.bak'))}
    else{Move-Item -LiteralPath $temporary -Destination $readStatePath}
}
function Read-ReadState {
    if(-not(Test-Path -LiteralPath $readStatePath -PathType Leaf)){throw 'The read probe is not prepared.'}
    $state=Get-Content -LiteralPath $readStatePath -Raw|ConvertFrom-Json
    if($state.schema -ne 1 -or $state.mode -cne 'read'){throw 'Unsupported read-probe state.'}
    foreach($pin in $state.pinnedFiles){if((Get-FileHash -LiteralPath $pin.path -Algorithm SHA256).Hash -cne $pin.sha256){throw ('Prepared input changed: '+$pin.path)}}
    return $state
}
function Get-ReadBootIdentity {(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')}
function Import-ReadLibrary {
    . (Join-Path $PSScriptRoot '..\smbios_boot\snapshot-reference.ps1') -RepoRoot $readRepo -OutputBoundary $readPackage -LibraryOnly
    . $readChain -InventoryBuildDir $readInventory -LoaderBuildDir $readLoader -SignedDirectory $readSigned -LibraryOnly
}
function Assert-ReadBaseline([string]$Snapshot){
    if(-not(Confirm-SecureBootUEFI)){throw 'Secure Boot must remain ON.'}
    Initialize-ReadyFirmwareReader;[ReadyToArmFirmwareRead]::Enable()
    foreach($record in (Get-Content -LiteralPath (Join-Path $Snapshot 'firmware-variables.json') -Raw|ConvertFrom-Json)){
        if($record.name -notmatch '^(BootOrder|BootNext|Boot[0-9A-Fa-f]{4}|SecureBoot|SetupMode|PK|KEK|db|dbx)$'){continue}
        [uint32]$attributes=0;[int]$errorCode=0
        $data=[ReadyToArmFirmwareRead]::Read($record.name,$record.guid,[ref]$attributes,[ref]$errorCode)
        $encoded=if($null -ne $data){[Convert]::ToBase64String($data)}else{$null}
        if($errorCode -ne $record.win32Error -or $attributes -ne $record.attributes -or $encoded -cne $record.base64){throw ('Boot/security mismatch: '+$record.name+'; expected bytes='+$record.base64+'; actual='+$encoded+'; Win32='+$errorCode)}
    }
}
function Read-ReadProbeReport([string]$Path,[string]$BuildId,[string]$ExpectedData,[uint32]$ExpectedAttributes){
    $bytes=[IO.File]::ReadAllBytes($Path)
    if($bytes.Length -gt 16384 -or @($bytes|Where-Object {$_ -gt 127 -or $_ -eq 0}).Count){throw 'Probe report is oversized or non-ASCII.'}
    $text=[Text.Encoding]::ASCII.GetString($bytes)
    if(-not $text.EndsWith("`r`n",[StringComparison]::Ordinal) -or $text.Replace("`r`n",'').Contains("`n")){throw 'Probe report has invalid line endings.'}
    $lines=$text.Substring(0,$text.Length-2).Split(@("`r`n"),[StringSplitOptions]::None)
    $keys=@('build_id','mode','name','guid','query_status','query_size','first_called','first_status','first_size','first_attributes','first_data','second_called','second_status','second_size','second_attributes','second_data','self_check','result')
    if($lines.Count -ne 20 -or $lines[0] -cne 'HSBREAD001' -or $lines[-1] -cne 'END'){throw 'Probe report is incomplete or has an unsupported schema.'}
    $values=[ordered]@{}
    for($index=0;$index -lt $keys.Count;$index++){
        $prefix=$keys[$index]+'='
        if(-not $lines[$index+1].StartsWith($prefix,[StringComparison]::Ordinal)){throw ('Unexpected probe field: expected '+$keys[$index])}
        $values[$keys[$index]]=$lines[$index+1].Substring($prefix.Length)
    }
    if($values.build_id -cne $BuildId -or $values.mode -cne 'read' -or $values.name -cne 'SecureBoot' -or $values.guid -cne '8be4df61-93ca-11d2-aa0d-00e098032b8c'){throw 'Probe identity or build mismatch.'}
    if($values.result -cne 'pass' -or $values.self_check -cne 'pass'){throw ('Read probe did not pass: query='+$values.query_status+'; first='+$values.first_status+'; second='+$values.second_status+'; self_check='+$values.self_check)}
    foreach($key in @('first_status','second_status')){if($values[$key] -cne '0x0000000000000000'){throw ('EFI read mismatch: '+$key+' expected=0x0000000000000000 actual='+$values[$key])}}
    if($values.query_status -cne '0x8000000000000005' -or $values.query_size -cne '1' -or $values.first_called -cne 'yes' -or $values.second_called -cne 'yes'){throw 'Unexpected size query or read-call evidence.'}
    foreach($prefix in @('first','second')){
        foreach($pair in @(@{key=$prefix+'_size';expected='1'},@{key=$prefix+'_data';expected=$ExpectedData},@{key=$prefix+'_attributes';expected=('0x{0:X8}' -f $ExpectedAttributes)})){
            if($values[$pair.key] -cne $pair.expected){throw ('Read mismatch: '+$pair.key+' expected='+$pair.expected+' actual='+$values[$pair.key])}
        }
    }
    return $values
}
function Clear-ReadOneShot([string]$Snapshot){
    $mount=Mount-Esp
    try{
        $before=Get-BootManagerHash $mount.EspRoot
        $previous=$ErrorActionPreference
        try{$ErrorActionPreference='Continue';$output=@(& (Get-InstallTool) oneshot --mode off --profile shim-inventory --esp-volume $mount.VolumeGuid 2>&1);$code=$LASTEXITCODE}finally{$ErrorActionPreference=$previous}
        $lines=@($output|ForEach-Object{([string]$_).Trim()})
        [IO.File]::WriteAllLines((New-ReadPath 'rollback-owned-cleanup' 'log'),[string[]]($lines+('EXIT='+$code)),[Text.UTF8Encoding]::new($false))
        if($code -ne 0 -and ($code -ne 1 -or $lines.Count -ne 1 -or $lines[0] -cne 'BootNext cleanup failed: owned Boot#### entry was not found')){throw 'Owned boot cleanup failed; no ESP restoration was performed.'}
        if((Get-BootManagerHash $mount.EspRoot) -cne $before){throw 'Windows Boot Manager changed during cleanup.'}
    }finally{Dismount-Esp $mount}
    Assert-ReadBaseline $Snapshot
}
Set-Location -LiteralPath $readRepo
if($readMode -eq 'Build'){& (Join-Path $PSScriptRoot 'build.ps1');exit $LASTEXITCODE}
. Import-ReadLibrary
$readExecutionLog=New-ReadPath ('command-'+$readMode) 'log'
trap{($_|Out-String)|Add-Content -LiteralPath $readExecutionLog;throw $_}
Assert-Admin
if($readMode -eq 'Prepare'){
    if(Test-Path -LiteralPath $readStatePath){throw 'A preparation journal already exists; use Status or Rollback instead of restaging.'}
    $build=Get-Content -LiteralPath (Join-Path $readArtifacts 'build-verdict.json') -Raw|ConvertFrom-Json
    if($build.result -cne 'PASS' -or $build.mode -cne 'read'){throw 'Build and host tests must pass first.'}
    foreach($pin in $build.pinnedFiles){if((Get-Sha256 $pin.path) -cne $pin.sha256){throw ('Build input changed: '+$pin.path)}}
    $snapshot=Join-Path $readReports ('recovery.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
    Invoke-ReadChild (Join-Path $PSScriptRoot 'snapshot.ps1') @('-RepositoryRoot',$readRepo,'-SnapshotDirectory',$snapshot) (New-ReadPath 'snapshot' 'log')
    Assert-ReadBaseline $snapshot
    Invoke-ReadChild $readChain @('Sign','-InventoryBuildDir',$readInventory,'-LoaderBuildDir',$readLoader,'-SignedDirectory',$readSigned) (New-ReadPath 'sign' 'log')
    $verifyStage=Join-Path $snapshot 'signature-stage';New-Item -ItemType Directory -Path $verifyStage|Out-Null
    foreach($name in @('grubx64.efi','inventory.efi','MOK.cer')){Copy-Item -LiteralPath (Join-Path $readSigned $name) -Destination (Join-Path $verifyStage $name)}
    Copy-Item -LiteralPath (Join-Path $readRepo 'components\uefi-loader\src\vendor\shimx64.efi') -Destination (Join-Path $verifyStage 'shimx64.efi')
    Copy-Item -LiteralPath (Join-Path $readInventory 'shim-packages\usr\lib\shim\mmx64.efi') -Destination (Join-Path $verifyStage 'mmx64.efi')
    Invoke-ReadChild (Join-Path $PSScriptRoot '..\smbios_boot\verify-signatures.ps1') @('-RepoRoot',$readRepo,'-OutputBoundary',$readPackage,'-SnapshotDirectory',$snapshot,'-ArtifactDirectory',$verifyStage) (New-ReadPath 'signatures' 'log')
    $pins=@($build.pinnedFiles)
    foreach($file in @(Get-ChildItem -LiteralPath $readSigned -File)){$pins+=@([ordered]@{path=$file.FullName;sha256=(Get-Sha256 $file.FullName)})}
    foreach($name in @('snapshot.json','firmware-variables.json','esp-layout.json','system-bcd.export','signature-verdict.json')){$path=Join-Path $snapshot $name;$pins+=@([ordered]@{path=$path;sha256=(Get-Sha256 $path)})}
    $state=[ordered]@{schema=1;mode='read';phase='staging';buildId=$build.buildId;snapshot=$snapshot;pinnedFiles=$pins;readyToArm=$false;armed=$false;armedFromBoot=$null;liveReadProven=$false;capture=$null;proof=$null}
    Save-ReadState $state
    Invoke-ReadChild $readChain @('Stage','-InventoryBuildDir',$readInventory,'-LoaderBuildDir',$readLoader,'-SignedDirectory',$readSigned) (New-ReadPath 'stage' 'log')
    Invoke-ReadChild $readChain @('Status','-InventoryBuildDir',$readInventory,'-LoaderBuildDir',$readLoader,'-SignedDirectory',$readSigned) (New-ReadPath 'status' 'log')
    Assert-ReadBaseline $snapshot
    $state.readyToArm=$true;$state.phase='read-probe-awaiting-user-boot';Save-ReadState $state
    'READ-ONLY PROBE STAGED; NOT ARMED. Recovery snapshot: '+$snapshot
    exit 0
}
$state=Read-ReadState
switch($readMode){
    'Status'{$state|ConvertTo-Json -Depth 9;Invoke-ReadChild $readChain @('Status','-InventoryBuildDir',$readInventory,'-LoaderBuildDir',$readLoader,'-SignedDirectory',$readSigned) (New-ReadPath 'status' 'log')}
    'Arm'{
        if(-not $state.readyToArm -or $state.armed){throw 'The read probe is not ready for a new Arm.'}
        Assert-ReadBaseline $state.snapshot
        $mount=Mount-Esp;try{if(Test-Path -LiteralPath (Join-Path $mount.EspRoot $readLeaf)){throw 'Uncollected read-probe output exists.'}}finally{Dismount-Esp $mount}
        $state.armedFromBoot=Get-ReadBootIdentity;$state.phase='arm-pending';Save-ReadState $state
        Invoke-ReadChild $readChain @('Arm','-InventoryBuildDir',$readInventory,'-LoaderBuildDir',$readLoader,'-SignedDirectory',$readSigned) (New-ReadPath 'arm' 'log')
        $state.armed=$true;$state.readyToArm=$false;$state.phase='user-armed';Save-ReadState $state
    }
    'Collect'{
        if(-not $state.armed -or (Get-ReadBootIdentity) -ceq $state.armedFromBoot){throw 'A user Arm and a subsequent Windows boot are required.'}
        $capture=New-ReadPath 'secureboot-read' 'txt';$hash=$null
        $mount=Mount-Esp
        try{$source=Join-Path $mount.EspRoot $readLeaf;if(Test-Path -LiteralPath $source){if((Test-ReparsePoint $source) -or -not(Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -gt 16KB){throw 'Unsafe read-probe output; preserved.'};$hash=Get-Sha256 $source;Copy-Item -LiteralPath $source -Destination $capture;if((Get-Sha256 $capture) -cne $hash -or (Get-Sha256 $source) -cne $hash){throw 'Read-probe output changed during capture.'}}}finally{Dismount-Esp $mount}
        $chainError=$null
        try{Invoke-ReadChild $readChain @('Collect','-InventoryBuildDir',$readInventory,'-LoaderBuildDir',$readLoader,'-SignedDirectory',$readSigned) (New-ReadPath 'collect' 'log')}catch{$chainError=$_.Exception.Message}
        if(-not $hash){throw ('No boot read-probe report exists. Chain result: '+$chainError)}
        $state.capture=$capture;Save-ReadState $state
        $expected=@((Get-Content -LiteralPath (Join-Path $state.snapshot 'firmware-variables.json') -Raw|ConvertFrom-Json)|Where-Object name -CEQ 'SecureBoot')
        if($expected.Count -ne 1){throw 'SecureBoot snapshot is ambiguous.'}
        $expectedHex=[BitConverter]::ToString([Convert]::FromBase64String($expected[0].base64)).Replace('-','')
        $fields=Read-ReadProbeReport $capture $state.buildId $expectedHex $expected[0].attributes
        if($chainError){throw ('Read capture saved, but the signed chain did not pass: '+$chainError)}
        Assert-ReadBaseline $state.snapshot
        $proof=New-ReadPath 'read-proof'
        [IO.File]::WriteAllText($proof,([ordered]@{result='PASS';scope='Boot-phase GetVariable repeated-read self-check only';fields=$fields;capture=$capture;captureSha256=$hash;variableWriteImplemented=$false;toggleProven=$false}|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
        $mount=Mount-Esp;try{$source=Join-Path $mount.EspRoot $readLeaf;if((Get-Sha256 $source) -cne $hash){throw 'Captured report changed before cleanup; preserved.'};Remove-Item -LiteralPath $source -Force}finally{Dismount-Esp $mount}
        $state.armed=$false;$state.liveReadProven=$true;$state.phase='read-proven-no-toggle-implemented';$state.proof=$proof;Save-ReadState $state
        'LIVE BOOT READ PASS '+$proof
        'Read access does not establish write access. No off/on mode or driver cycle exists.'
    }
    'Rollback'{
        Clear-ReadOneShot $state.snapshot
        $mount=Mount-Esp
        try{
            $snapshot=Get-Content -LiteralPath (Join-Path $state.snapshot 'snapshot.json') -Raw|ConvertFrom-Json
            if($mount.VolumeGuid -cne $snapshot.volumeGuid -or (Get-BootManagerHash $mount.EspRoot) -cne $snapshot.bootManagerSha256){throw 'ESP or Windows Boot Manager snapshot mismatch; no restore performed.'}
            $saved=Join-Path $state.snapshot 'ESP\EFI\HypersinvShimInventory'
            if((Get-Sha256 (Join-Path $saved 'manifest.sha256')) -cne $snapshot.manifestSha256){throw 'Saved original stage manifest changed.'}
            foreach($file in $snapshot.stageFiles){if((Get-Sha256 (Join-Path $saved $file.Name)) -cne $file.Hash){throw ('Saved original stage changed: '+$file.Name)}}
            $stageRoot=Join-Path $mount.EspRoot 'EFI\HypersinvShimInventory'
            Assert-ShimStageNames $stageRoot -AllowStatus
            $current=@{Files=@($script:ShimStageNames|ForEach-Object{Get-FileRecord $_ (Join-Path $stageRoot $_)})}
            if([IO.File]::ReadAllText((Join-Path $stageRoot 'manifest.sha256'),[Text.Encoding]::ASCII) -cne (New-ShimManifest $current)){throw 'Current stage manifest mismatch; no restore performed.'}
            $prepared=Get-ShimStageSources
            foreach($file in $current.Files){$wanted=@($prepared.Files|Where-Object Name -CEQ $file.Name);if($wanted.Count -ne 1 -or ($file.Hash -cne $wanted[0].Hash -and $file.Hash -cne (Get-Sha256 (Join-Path $saved $file.Name)))){throw ('Unexpected stage bytes: '+$file.Name)}}
            foreach($leaf in @($readLeaf,'HypersinvEfiInventory.txt','EFI\HypersinvShimInventory\chain-status.txt','EFI\HypersinvShimInventory\chain-diagnostic.txt')){
                $path=Join-Path $mount.EspRoot $leaf;if(-not(Test-Path -LiteralPath $path)){continue}
                if((Test-ReparsePoint $path) -or -not(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -gt 64MB){throw 'Unsafe output preserved during rollback.'}
                $hash=Get-Sha256 $path;$copy=New-ReadPath ('rollback-'+[IO.Path]::GetFileNameWithoutExtension($leaf)) 'txt';Copy-Item -LiteralPath $path -Destination $copy
                if((Get-Sha256 $copy) -cne $hash -or (Get-Sha256 $path) -cne $hash){throw 'Output changed during recovery capture.'};Remove-Item -LiteralPath $path -Force
            }
            $readRestoreSources=Get-ExistingShimStageSources $saved
            foreach($file in $readRestoreSources.Files){$file.Path=Join-Path $saved $file.Name}
            $readRestoreSources.CertificateThumbprint=$script:ShimCertThumbprint
            function Get-ShimStageSources {return $readRestoreSources}
            Stage-ShimChainToRoot $mount.EspRoot $mount.VolumeGuid ''
        }finally{Dismount-Esp $mount}
        Assert-ReadBaseline $state.snapshot
        $state.armed=$false;$state.readyToArm=$false;$state.phase='original-stage-restored';Save-ReadState $state
        'ORIGINAL STAGE RESTORED. Secure Boot and original boot settings verified.'
    }
    'Verify'{
        if($state.phase -cne 'original-stage-restored'){throw 'Rollback must restore the original stage first.'}
        Assert-ReadBaseline $state.snapshot
        $mount=Mount-Esp
        try{foreach($file in (Get-Content -LiteralPath (Join-Path $state.snapshot 'esp-layout.json') -Raw|ConvertFrom-Json)){if($file.sha256 -and (Get-Sha256 (Join-Path $mount.EspRoot $file.path)) -cne $file.sha256){throw ('Original ESP mismatch: '+$file.path)}}}finally{Dismount-Esp $mount}
        $before=[IO.File]::ReadAllLines((Join-Path $state.snapshot 'bcd-all.txt'));$after=@(& bcdedit.exe /enum all /v)
        if($LASTEXITCODE -or ($before -join "`n") -cne ($after -join "`n")){throw 'Original BCD enumeration mismatch.'}
        'ORIGINAL BOOT/SECURE BOOT SETTINGS AND SNAPSHOT ESP FILES VERIFIED. No variable-write proof is claimed.'
    }
}
