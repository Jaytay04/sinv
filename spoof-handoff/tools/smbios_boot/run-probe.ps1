param(
    [ValidateSet('Prepare','Status','Arm','Collect','Rollback','VerifyOriginal')]
    [string]$Mode='Status',
    [switch]$SevenFields
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(-not $SevenFields){throw 'Use spoof-handoff/commands/smbios-seven-probe.ps1 for the reviewed seven-field profile; the previous serial profile is closed.'}
$variantMode=$Mode
$variantWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$variantRepo=[IO.Path]::GetFullPath((Join-Path $variantWorktree '..\..\..'))
$variantPackage=Join-Path $variantRepo 'spoof-handoff'
$variantArtifacts=Join-Path $variantPackage 'artifacts\smbios-seven-probe'
$variantReports=Join-Path $variantPackage 'reports\smbios-boot-20261013\seven-fields'
$variantInventory=Join-Path $variantArtifacts 'inventory'
# The unchanged signer requires generated output beneath the main .codex-build.
# Keep that output inside this worktree's handoff package as well.
$variantSigned=Join-Path $variantWorktree 'spoof-handoff\artifacts\smbios-seven-probe\signed'
$variantLoader=Join-Path $variantRepo '.codex-build\efi_shim_loader'
$variantPowerShell=Join-Path $PSHOME 'powershell.exe'
$variantStatePath=Join-Path $variantArtifacts 'prepared.json'
$variantReportLeaf='HypersinvSmbiosPatch.txt'
$variantChain=Join-Path $variantRepo 'tools\efi_inventory\shim-chain-collector.ps1'
New-Item -ItemType Directory -Path $variantReports -Force|Out-Null

function Invoke-VariantChild([string]$Script,[string[]]$Arguments,[string]$Log) {
    Add-Content -LiteralPath $Log -Value ('START '+[DateTime]::UtcNow.ToString('o')+' '+$Script)
    # Windows PowerShell represents redirected native stderr as ErrorRecords.
    # Collect it without stopping before the child exit code can be inspected.
    $previousPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        $childOutput=@(& $variantPowerShell -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments 2>&1)
        $childExit=$LASTEXITCODE
    } finally { $ErrorActionPreference=$previousPreference }
    foreach($item in $childOutput){
        $line=[string]$item
        Add-Content -LiteralPath $Log -Value $line
        Write-Output $line
    }
    Add-Content -LiteralPath $Log -Value ('EXIT='+$childExit)
    if($childExit -ne 0){throw ('Step failed ('+$childExit+'): '+$Script+'. See '+$Log)}
}
function Import-VariantLibrary {
    . (Join-Path $PSScriptRoot 'snapshot-reference.ps1') -RepoRoot $variantRepo -OutputBoundary $variantPackage -LibraryOnly
    . $variantChain -InventoryBuildDir $variantInventory -LoaderBuildDir $variantLoader -SignedDirectory $variantSigned -LibraryOnly
}
function Assert-VariantPreparationInputs {
    Assert-SignedDirectoryTarget
    Assert-NoReparseAncestors $variantSigned $variantWorktree
    Assert-NoReparseAncestors $variantSigned ([IO.Path]::GetPathRoot($variantSigned))
    if($script:SignedDirectory -cne $variantSigned){throw 'The collector resolved a different signing destination.'}
    $inputs=Get-ShimSourcePaths
    foreach($path in @($inputs.Loader,$inputs.Collector,$inputs.Shim,$inputs.Certificate,$inputs.MokManager)){
        if(-not(Test-Path -LiteralPath $path -PathType Leaf) -or (Test-ReparsePoint $path)){throw ('Missing or unsafe signing input: '+$path)}
        Assert-NoReparseAncestors $path ([IO.Path]::GetPathRoot($path))
    }
    foreach($image in @($inputs.Loader,$inputs.Collector,$inputs.Shim,$inputs.MokManager)){Assert-EfiApplication $image}
    if((Get-Sha256 $inputs.Shim) -cne $script:ShimHash -or (Get-Sha256 $inputs.MokManager) -cne $script:MokManagerHash){throw 'Pinned shim or MokManager input differs.'}
    $signTool=Get-Signtool
    $certificate=Assert-CurrentUserSigningCertificate $inputs.Certificate
    $verifier=Get-AuthenticodeVerifier
    $installer=Get-InstallTool
    return [pscustomobject]@{result='PASS';signedDirectory=$variantSigned;signTool=$signTool;certificateThumbprint=$certificate.Thumbprint;verifier=$verifier;installer=$installer;signingOrStagingPerformed=$false}
}
function New-VariantReportPath([string]$Kind,[string]$Extension='json') {
    return Join-Path $variantReports ($Kind+'.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N')+'.'+$Extension)
}
function Get-VariantBootIdentity {
    $systems=@(Get-CimInstance -ClassName Win32_OperatingSystem -OperationTimeoutSec 15)
    if($systems.Count -ne 1 -or $null -eq $systems[0].LastBootUpTime){throw 'Cannot identify the current Windows boot.'}
    return $systems[0].LastBootUpTime.ToUniversalTime().ToString('o')
}
function Save-VariantState($State) {
    $temporary=$variantStatePath+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    [IO.File]::WriteAllText($temporary,($State|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    if(Test-Path -LiteralPath $variantStatePath){
        # Windows PowerShell binds a null string argument as an empty backup path.
        # A named backup also preserves the previous journal for recovery review.
        $backup=$variantStatePath+'.'+[Guid]::NewGuid().ToString('N')+'.bak'
        [IO.File]::Replace($temporary,$variantStatePath,$backup)
    }else{Move-Item -LiteralPath $temporary -Destination $variantStatePath}
}
function Read-VariantState {
    if(-not(Test-Path -LiteralPath $variantStatePath -PathType Leaf)){throw 'Preparation has not completed; do not arm.'}
    $state=Get-Content -LiteralPath $variantStatePath -Raw|ConvertFrom-Json
    if($state.schema -ne 1 -or $state.mask -ne 127){throw 'Unsupported probe state.'}
    foreach($item in $state.pinnedFiles){
        if((Get-FileHash -LiteralPath $item.path -Algorithm SHA256).Hash -cne $item.sha256){throw ('Prepared input changed: '+$item.path)}
    }
    return $state
}
function Invoke-VariantReadback([ValidateSet('original','seven')][string]$Expected) {
    $path=New-VariantReportPath ('windows-'+$Expected)
    & (Join-Path $variantArtifacts 'smbios_windows_readback.exe') --expect $Expected --output $path
    $probeExit=$LASTEXITCODE
    Write-Output ('WINDOWS READBACK '+$path)
    if($probeExit -ne 0){throw ('Native/WMI read-back failed; report: '+$path)}
    $result=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
    if($result.result -cne 'PASS' -or $result.mode -cne $Expected){throw 'Probe report does not match the requested verification.'}
    return $path
}
function Assert-VariantBaseline([string]$Snapshot) {
    $records=Get-Content -LiteralPath (Join-Path $Snapshot 'firmware-variables.json') -Raw|ConvertFrom-Json
    if(-not(Confirm-SecureBootUEFI)){throw 'Secure Boot must remain ON.'}
    Initialize-ReadyFirmwareReader
    [ReadyToArmFirmwareRead]::Enable()
    foreach($record in $records){
        if($record.name -notmatch '^(BootOrder|BootNext|Boot[0-9A-Fa-f]{4}|SecureBoot|SetupMode|PK|KEK|db|dbx)$'){continue}
        [uint32]$attributes=0;[int]$readError=0
        $data=[ReadyToArmFirmwareRead]::Read($record.name,$record.guid,[ref]$attributes,[ref]$readError)
        $encoded=if($null -ne $data){[Convert]::ToBase64String($data)}else{$null}
        if($readError -ne $record.win32Error -or $attributes -ne $record.attributes -or $encoded -cne $record.base64){throw ('Boot/security baseline drift: '+$record.name)}
    }
}
function Assert-VariantBootReport([string]$Path,[string]$BuildId) {
    $lines=[IO.File]::ReadAllLines($Path,[Text.Encoding]::ASCII)
    if($lines.Count -lt 12 -or $lines[0] -cne 'HSMBIOS001' -or $lines[1] -cne 'mask=127' -or
       $lines[2] -cne ('build_id='+$BuildId) -or $lines[-2] -cne 'result=applied' -or $lines[-1] -cne 'END'){throw 'Boot SMBIOS report is incomplete, failed, or from another build.'}
    if($lines[3] -notmatch '^entries=([12])$'){throw 'Invalid SMBIOS entry-point count.'}
    $entries=[int]$Matches[1]
    $baseline=Import-Csv -LiteralPath (Join-Path $variantPackage 'reports\client-originals.tsv') -Delimiter "`t"
    $names=@('system_serial','system_vendor','system_product','board_serial','board_vendor','board_product','system_uuid')
    $members=@('FakeSMBIOSSerial','FakeSMBIOSManufacturer','FakeSMBIOSProduct','FakeMotherboardSerial','FakeMotherboardManufacturer','FakeMotherboardProduct','FakeSMBIOSUUID')
    if($lines.Count -ne 6+8*$entries){throw 'Unexpected boot report records.'}
    for($entry=0;$entry -lt $entries;$entry++){
        $offset=4+8*$entry
        if($lines[$offset] -cnotmatch ('^entry\t'+$entry+'\t[23]\tEfiRuntimeServicesData$')){throw 'Invalid entry-point provenance.'}
        for($field=0;$field -lt 7;$field++){
            $pair=@($baseline|Where-Object member -CEQ $members[$field])
            if($pair.Count -ne 1){throw 'Ambiguous locked field map.'}
            $expected=$pair[0].replacement
            $wanted=@('field',[string]$entry,$names[$field],$pair[0].original,$expected)-join "`t"
            if($lines[$offset+1+$field] -cne $wanted){throw ('Boot-table read-back mismatch: '+$names[$field])}
        }
    }
}
function Save-VariantRoundTripProof($State,[string]$RestoredPath) {
    if(-not $State.allSevenLiveProven -or $State.mask -ne 127){throw 'No seven-field live substitution is recorded.'}
    $paths=@($State.originalReadback,$State.spoofReadback,$RestoredPath)
    $modes=@('original','seven','original')
    $reports=@()
    $evidence=@()
    for($index=0;$index -lt 3;$index++){
        $path=$paths[$index]
        $hash=Get-Sha256 $path
        $report=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
        if($report.schema -cne 'smbios-windows-readback.v1' -or $report.result -cne 'PASS' -or $report.mode -cne $modes[$index]){throw 'Round-trip evidence has an invalid schema, result, or expectation.'}
        if((Get-Sha256 $path) -cne $hash){throw 'Read-back evidence changed during proof validation.'}
        $reports+=@($report)
        $evidence+=@([ordered]@{path=$path;sha256=$hash;mode=$modes[$index]})
    }
    $boots=@($reports|ForEach-Object{[DateTimeOffset]::Parse($_.bootIdentity.approximateBootUtc)})
    if(($boots[1]-$boots[0]).TotalSeconds -lt 5 -or ($boots[2]-$boots[1]).TotalSeconds -lt 5){throw 'Round-trip evidence must come from three successive Windows boots.'}
    Assert-VariantBootReport $State.bootReadback $State.buildId
    $baseline=Import-Csv -LiteralPath (Join-Path $variantPackage 'reports\client-originals.tsv') -Delimiter "`t"
    $names=@('system_serial','system_vendor','system_product','board_serial','board_vendor','board_product','system_uuid')
    $members=@('FakeSMBIOSSerial','FakeSMBIOSManufacturer','FakeSMBIOSProduct','FakeMotherboardSerial','FakeMotherboardManufacturer','FakeMotherboardProduct','FakeSMBIOSUUID')
    $fields=@()
    for($field=0;$field -lt 7;$field++){
        $pair=@($baseline|Where-Object member -CEQ $members[$field])
        if($pair.Count -ne 1 -or $pair[0].original -ceq $pair[0].replacement){throw 'Locked field map is missing, ambiguous, or unchanged.'}
        for($index=0;$index -lt 3;$index++){
            $expected=if($index -eq 1){$pair[0].replacement}else{$pair[0].original}
            $row=$reports[$index].fields.($names[$field])
            if($row.originalKnown -cne $pair[0].original -or $row.expected -cne $expected -or $row.observedNative -cne $expected -or $row.observedWmi -cne $expected -or $row.pass -ne $true -or $row.nativeMatches -ne $true -or $row.wmiMatches -ne $true){throw ('Round-trip read-back mismatch: '+$names[$field])}
        }
        $fields+=@([ordered]@{field=$names[$field];originalNative=$reports[0].fields.($names[$field]).observedNative;originalWmi=$reports[0].fields.($names[$field]).observedWmi;spoofedNative=$reports[1].fields.($names[$field]).observedNative;spoofedWmi=$reports[1].fields.($names[$field]).observedWmi;restoredNative=$reports[2].fields.($names[$field]).observedNative;restoredWmi=$reports[2].fields.($names[$field]).observedWmi;nativeSource='GetSystemFirmwareTable(RSMB)';wmiSource=$(if($field -ge 3 -and $field -le 5){'ROOT\CIMV2:Win32_BaseBoard'}else{'ROOT\CIMV2:Win32_ComputerSystemProduct'})})
    }
    $proof=[ordered]@{schema=1;result='PASS';scope='Seven confirmed SMBIOS fields after signed boot and normal-boot restoration';mask=127;fields=$fields;readbacks=$evidence;bootEvidence=[ordered]@{path=$State.bootReadback;sha256=(Get-Sha256 $State.bootReadback)};deviceFieldsEnabled=$false;cpuSerialEnabled=$false;gpuSerialEnabled=$false;verifiedUtc=[DateTime]::UtcNow.ToString('o')}
    $proofPath=New-VariantReportPath 'seven-fields-round-trip-proof'
    [IO.File]::WriteAllText($proofPath,($proof|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    return $proofPath
}
function Complete-VariantPreparation($State) {
    Assert-Admin
    if($State.phase -cne 'staging-recovery-journal' -or $State.armed -or $State.rebooted -or $State.readyToArm){throw 'Only an unarmed, incomplete preparation journal can be finalized.'}
    Assert-VariantBaseline $State.snapshot
    $log=New-VariantReportPath 'prepare-finalize' 'log'
    Invoke-VariantChild $variantChain @('Status','-InventoryBuildDir',$variantInventory,'-LoaderBuildDir',$variantLoader,'-SignedDirectory',$variantSigned) $log
    $mount=Mount-Esp
    try{
        foreach($relative in @($variantReportLeaf,'HypersinvEfiInventory.txt','EFI\HypersinvShimInventory\chain-status.txt','EFI\HypersinvShimInventory\chain-diagnostic.txt')){
            if(Test-Path -LiteralPath (Join-Path $mount.EspRoot $relative)){throw ('Uncollected EFI output exists; preparation was not finalized: '+$relative)}
        }
    }finally{Dismount-Esp $mount}
    $null=@(Invoke-VariantReadback 'original')
    Assert-VariantBaseline $State.snapshot
    $State.phase='seven-fields-awaiting-user-boot';$State.readyToArm=$true
    Save-VariantState $State
    'SEVEN-FIELD PROBE STAGED: all seven confirmed SMBIOS substitutions enabled. No Arm or reboot. Snapshot: '+$State.snapshot
}
function Clear-VariantRollbackBootState([string]$Snapshot) {
    $log=New-VariantReportPath 'rollback-cleanup' 'log'
    $mount=Mount-Esp
    $alreadyClear=$false
    try {
        $bootBefore=Get-BootManagerHash $mount.EspRoot
        $installer=Get-InstallTool
        $previousPreference=$ErrorActionPreference
        try {
            $ErrorActionPreference='Continue'
            $output=@(& $installer oneshot --mode off --profile shim-inventory --esp-volume $mount.VolumeGuid 2>&1)
            $cleanupExit=$LASTEXITCODE
        }finally{$ErrorActionPreference=$previousPreference}
        $lines=@($output|ForEach-Object{([string]$_).Trim()})
        [IO.File]::WriteAllLines($log,[string[]]($lines+('EXIT='+$cleanupExit)),[Text.UTF8Encoding]::new($false))
        if($cleanupExit -ne 0){
            # The installer reaches this result only after BootNext is absent and
            # the complete Boot#### scan finds no owned option or read error.
            if($cleanupExit -ne 1 -or $lines.Count -ne 1 -or $lines[0] -cne 'BootNext cleanup failed: owned Boot#### entry was not found'){
                throw ('Owned one-time boot cleanup failed; ESP was not restored. See '+$log)
            }
            $alreadyClear=$true
        }
        if((Get-BootManagerHash $mount.EspRoot) -cne $bootBefore){throw 'Windows Boot Manager changed during rollback cleanup; ESP was not restored.'}
    }finally{Dismount-Esp $mount}
    Assert-VariantBaseline $Snapshot
    if($alreadyClear){'Owned one-time boot state already absent; baseline verified. '+$log}
    else{'Owned one-time boot state cleared; baseline verified. '+$log}
}
function Invoke-VariantArm($State) {
    Assert-Admin
    if(-not $State.readyToArm){throw 'This probe state is not ready to arm.'}
    Assert-VariantBaseline $State.snapshot
    $mount=Mount-Esp
    try{if(Test-Path -LiteralPath (Join-Path $mount.EspRoot $variantReportLeaf)){throw 'Collect the existing SMBIOS output before arming.'}}finally{Dismount-Esp $mount}
    $State.armedFromBoot=Get-VariantBootIdentity
    $State.armed=$false;$State.phase='arm-pending'
    Save-VariantState $State
    try {
        & $variantChain Arm -InventoryBuildDir $variantInventory -LoaderBuildDir $variantLoader -SignedDirectory $variantSigned
        if($LASTEXITCODE -ne 0){throw 'Arm failed.'}
        $State.armed=$true;$State.phase='armed-by-user-awaiting-new-boot'
        Save-VariantState $State
    } catch {
        $armError=$_.Exception.Message
        & $variantChain Disarm -InventoryBuildDir $variantInventory -LoaderBuildDir $variantLoader -SignedDirectory $variantSigned
        if($LASTEXITCODE -ne 0){throw ('Arm failed and owned cleanup failed; recovery journal retained. '+$armError)}
        Assert-VariantBaseline $State.snapshot
        $State.armed=$false;$State.phase='seven-fields-awaiting-user-boot'
        Save-VariantState $State
        throw $armError
    }
}

Set-Location -LiteralPath $variantRepo
. Import-VariantLibrary
$variantExecutionLog=New-VariantReportPath ('command-'+$variantMode) 'log'
trap {
    ($_|Out-String)|Add-Content -LiteralPath $variantExecutionLog
    throw $_
}
if($variantMode -eq 'Prepare'){
    Assert-Admin
    if(Test-Path -LiteralPath $variantStatePath){
        # Resume only verification and journal finalization; do not repeat Sign or Stage.
        $pending=Read-VariantState
        Complete-VariantPreparation $pending
        exit 0
    }
    $buildResult=Get-Content -LiteralPath (Join-Path $variantArtifacts 'build-verdict.json') -Raw|ConvertFrom-Json
    if($buildResult.result -cne 'PASS' -or $buildResult.activeBootMask -ne 127){throw 'Build/host validation must pass before preparation.'}
    foreach($item in $buildResult.pinnedFiles){if((Get-FileHash -LiteralPath $item.path).Hash -cne $item.sha256){throw ('Build input drift: '+$item.path)}}
    $preflight=Assert-VariantPreparationInputs
    [IO.File]::WriteAllText((New-VariantReportPath 'preparation-inputs'),($preflight|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
    $log=New-VariantReportPath 'prepare' 'log'
    $snapshot=Join-Path $variantReports ('recovery-before-stage.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
    $originalReport=@(Invoke-VariantReadback 'original')[-1]
    $snapshotArguments=@('-RepoRoot',$variantRepo,'-OutputBoundary',$variantPackage,'-SnapshotDirectory',$snapshot)
    $referenceReceipt=Join-Path $variantInventory 'shim-chain-stage.json'
    if(Test-Path -LiteralPath $referenceReceipt -PathType Leaf){$snapshotArguments+=@('-ReferenceReceiptPath',$referenceReceipt)}
    Invoke-VariantChild (Join-Path $PSScriptRoot 'snapshot-reference.ps1') $snapshotArguments $log
    Assert-VariantBaseline $snapshot
    Invoke-VariantChild $variantChain @('Sign','-InventoryBuildDir',$variantInventory,'-LoaderBuildDir',$variantLoader,'-SignedDirectory',$variantSigned) $log
    # A fresh snapshot owns its verification copies, so failed attempts never collide.
    $verificationStage=Join-Path $snapshot 'signature-stage'
    Assert-NoReparseAncestors $verificationStage $variantPackage
    New-Item -ItemType Directory -Path $verificationStage|Out-Null
    foreach($name in @('grubx64.efi','inventory.efi','MOK.cer')){Copy-Item -LiteralPath (Join-Path $variantSigned $name) -Destination (Join-Path $verificationStage $name)}
    Copy-Item -LiteralPath (Join-Path $variantRepo 'components\uefi-loader\src\vendor\shimx64.efi') -Destination (Join-Path $verificationStage 'shimx64.efi')
    Copy-Item -LiteralPath (Join-Path $variantInventory 'shim-packages\usr\lib\shim\mmx64.efi') -Destination (Join-Path $verificationStage 'mmx64.efi')
    Invoke-VariantChild (Join-Path $PSScriptRoot 'verify-signatures.ps1') @('-RepoRoot',$variantRepo,'-OutputBoundary',$variantPackage,'-SnapshotDirectory',$snapshot,'-ArtifactDirectory',$verificationStage) $log
    $mount=Mount-Esp
    try{if(Test-Path -LiteralPath (Join-Path $mount.EspRoot $variantReportLeaf)){throw 'Uncollected SMBIOS output exists; staging stopped.'}}finally{Dismount-Esp $mount}
    $state=[ordered]@{schema=1;phase='staging-recovery-journal';mask=127;buildId=$buildResult.buildId;preparedUtc=[DateTime]::UtcNow.ToString('o');snapshot=$snapshot;originalReadback=$originalReport;pinnedFiles=$buildResult.pinnedFiles;readyToArm=$false;armed=$false;armedFromBoot=$null;rebooted=$false;allSevenLiveProven=$false;rollbackFromBoot=$null;spoofReadback=$null;bootReadback=$null;roundTripProof=$null}
    $state.pinnedFiles+=@([ordered]@{path=$originalReport;sha256=(Get-Sha256 $originalReport)})
    foreach($name in @('snapshot.json','firmware-variables.json','esp-layout.json','system-bcd.export','signature-verdict.json')){
        $path=Join-Path $snapshot $name
        $state.pinnedFiles+=@([ordered]@{path=$path;sha256=(Get-Sha256 $path)})
    }
    Save-VariantState $state
    Invoke-VariantChild $variantChain @('Stage','-InventoryBuildDir',$variantInventory,'-LoaderBuildDir',$variantLoader,'-SignedDirectory',$variantSigned) $log
    Complete-VariantPreparation $state
    exit 0
}
$state=Read-VariantState
switch($variantMode){
    'Status'{
        $state|ConvertTo-Json -Depth 8
        if(-not $state.readyToArm){break}
        & $variantChain Status -InventoryBuildDir $variantInventory -LoaderBuildDir $variantLoader -SignedDirectory $variantSigned
        if($LASTEXITCODE -ne 0){throw 'Stage status failed.'}
    }
    'Arm'{
        Invoke-VariantArm $state
    }
    'Collect'{
        Assert-Admin
        if(-not $state.armed -or -not $state.armedFromBoot -or (Get-VariantBootIdentity) -ceq $state.armedFromBoot){throw 'A recorded user Arm and a new Windows boot are required for this proof.'}
        $capture=New-VariantReportPath 'boot-smbios' 'txt'
        $hasBootReport=$false
        $mount=Mount-Esp
        try{
            $source=Join-Path $mount.EspRoot $variantReportLeaf
            if(Test-Path -LiteralPath $source){
                if(-not(Test-Path -LiteralPath $source -PathType Leaf) -or (Test-ReparsePoint $source) -or (Get-Item -LiteralPath $source).Length -gt 64KB){throw 'Boot SMBIOS output is unsafe; preserved.'}
                $hash=Get-Sha256 $source
                Copy-Item -LiteralPath $source -Destination $capture
                if((Get-Sha256 $capture) -cne $hash -or (Get-Sha256 $source) -cne $hash){throw 'Boot output changed during capture.'}
                $hasBootReport=$true
            }
        }finally{Dismount-Esp $mount}
        $log=New-VariantReportPath 'collect' 'log'
        # Existing Collect captures the chain evidence and always preserves failing status.
        Invoke-VariantChild $variantChain @('Collect','-InventoryBuildDir',$variantInventory,'-LoaderBuildDir',$variantLoader,'-SignedDirectory',$variantSigned) $log
        if(-not $hasBootReport){throw 'The chain was collected, but no SMBIOS publication report was produced.'}
        Assert-VariantBootReport $capture $state.buildId
        $windowsReport=@(Invoke-VariantReadback 'seven')[-1]
        $mount=Mount-Esp
        try{
            $source=Join-Path $mount.EspRoot $variantReportLeaf
            if((Get-Sha256 $source) -cne $hash){throw 'Boot SMBIOS report changed before cleanup; preserved.'}
            Remove-Item -LiteralPath $source -Force
        }finally{Dismount-Esp $mount}
        $proof=[ordered]@{schema=1;result='PASS';scope='Seven confirmed SMBIOS fields; native RSMB plus ordinary WMI after signed boot';bootEvidence=$capture;windowsEvidence=$windowsReport;mask=127;remainingSixEnabled=$true;deviceFieldsEnabled=$false}
        $proofPath=New-VariantReportPath 'seven-fields-boot-proof'
        [IO.File]::WriteAllText($proofPath,($proof|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
        $state.pinnedFiles+=@([ordered]@{path=$capture;sha256=(Get-Sha256 $capture)},[ordered]@{path=$windowsReport;sha256=(Get-Sha256 $windowsReport)},[ordered]@{path=$proofPath;sha256=(Get-Sha256 $proofPath)})
        $state.rebooted=$true;$state.armed=$false;$state.readyToArm=$false;$state.allSevenLiveProven=$true;$state.spoofReadback=$windowsReport;$state.bootReadback=$capture;$state.phase='seven-fields-proven-normal-boot-restore-required'
        Save-VariantState $state
        'SEVEN-FIELD SIGNED BOOT PASS '+$proofPath
        'All seven substituted native and WMI values verified; rollback and a normal reboot are required to verify restoration.'
    }
    'Rollback'{
        Assert-Admin
        if($state.phase -ceq 'original-stage-restored-normal-reboot-required'){throw 'The original stage has already been restored; normal boot and VerifyOriginal remain.'}
        Clear-VariantRollbackBootState $state.snapshot
        $mount=Mount-Esp
        try{
            $before=Get-Content -LiteralPath (Join-Path $state.snapshot 'snapshot.json') -Raw|ConvertFrom-Json
            if($mount.VolumeGuid -cne $before.volumeGuid -or (Get-BootManagerHash $mount.EspRoot) -cne $before.bootManagerSha256){throw 'Original ESP/Windows Boot Manager identity mismatch.'}
            $saved=Join-Path $state.snapshot 'ESP\EFI\HypersinvShimInventory'
            if(@($before.stageFiles).Count -ne 5 -or (Get-Sha256 (Join-Path $saved 'manifest.sha256')) -cne $before.manifestSha256){throw 'Saved stage manifest differs from the snapshot.'}
            foreach($file in $before.stageFiles){
                if($script:ShimStageNames -notcontains $file.Name -or (Get-Sha256 (Join-Path $saved $file.Name)) -cne $file.Hash){throw 'Saved stage bytes differ from the snapshot.'}
            }
            $stagePath=Join-Path $mount.EspRoot 'EFI\HypersinvShimInventory'
            Assert-ShimStageNames $stagePath -AllowStatus
            $current=@{Files=@($script:ShimStageNames|ForEach-Object{Get-FileRecord $_ (Join-Path $stagePath $_)})}
            if([IO.File]::ReadAllText((Join-Path $stagePath 'manifest.sha256'),[Text.Encoding]::ASCII) -cne (New-ShimManifest $current)){throw 'Current owned-stage manifest mismatch.'}
            $prepared=Get-ShimStageSources
            foreach($file in $current.Files){
                $savedPath=Join-Path (Join-Path $state.snapshot 'ESP\EFI\HypersinvShimInventory') $file.Name
                $preparedFile=@($prepared.Files|Where-Object Name -CEQ $file.Name)
                if($preparedFile.Count -ne 1 -or ($file.Hash -cne (Get-Sha256 $savedPath) -and $file.Hash -cne $preparedFile[0].Hash)){throw 'Unexpected staged artifact; rollback stopped.'}
            }
            foreach($leaf in @($variantReportLeaf,'HypersinvEfiInventory.txt','EFI\HypersinvShimInventory\chain-status.txt','EFI\HypersinvShimInventory\chain-diagnostic.txt')){
                $output=Join-Path $mount.EspRoot $leaf
                if(-not(Test-Path -LiteralPath $output)){continue}
                if(-not(Test-Path -LiteralPath $output -PathType Leaf) -or (Test-ReparsePoint $output) -or (Get-Item -LiteralPath $output).Length -gt 64MB){throw 'Unsafe owned output was preserved.'}
                $outputHash=Get-Sha256 $output
                $savedOutput=New-VariantReportPath ('rollback-output-'+[IO.Path]::GetFileNameWithoutExtension($leaf)) 'txt'
                Copy-Item -LiteralPath $output -Destination $savedOutput
                if((Get-Sha256 $output) -cne $outputHash -or (Get-Sha256 $savedOutput) -cne $outputHash){throw 'Rollback output changed while being saved.'}
                Remove-Item -LiteralPath $output -Force
            }
            $variantRollbackSources=Get-ExistingShimStageSources $saved
            foreach($file in $variantRollbackSources.Files){$file.Path=Join-Path $saved $file.Name}
            $variantRollbackSources.CertificateThumbprint=$script:ShimCertThumbprint
            function Get-ShimStageSources {return $variantRollbackSources}
            Stage-ShimChainToRoot $mount.EspRoot $mount.VolumeGuid ''
        }finally{Dismount-Esp $mount}
        Assert-VariantBaseline $state.snapshot
        $state.readyToArm=$false
        $state.armed=$false
        $state.rollbackFromBoot=Get-VariantBootIdentity
        $state.phase='original-stage-restored-normal-reboot-required'
        Save-VariantState $state
        'ORIGINAL STAGE RESTORED. Run Restart-Computer for a normal firmware boot; current Windows SMBIOS remains cached until then.'
    }
    'VerifyOriginal'{
        Assert-Admin
        Assert-VariantBaseline $state.snapshot
        if($state.phase -cne 'original-stage-restored-normal-reboot-required' -or -not $state.rollbackFromBoot -or (Get-VariantBootIdentity) -ceq $state.rollbackFromBoot){throw 'Rollback and a later normal Windows boot are required before final original verification.'}
        $restored=@(Invoke-VariantReadback 'original')[-1]
        if($state.allSevenLiveProven){
            $proofPath=Save-VariantRoundTripProof $state $restored
            $state.roundTripProof=$proofPath
            $state.pinnedFiles+=@([ordered]@{path=$restored;sha256=(Get-Sha256 $restored)},[ordered]@{path=$proofPath;sha256=(Get-Sha256 $proofPath)})
            $state.phase='seven-fields-round-trip-complete'
            Save-VariantState $state
            'SEVEN-FIELD ROUND TRIP PASS '+$proofPath
        }else{'ORIGINALS VERIFIED; no substituted live proof exists, so no round-trip claim was recorded.'}
    }
}
