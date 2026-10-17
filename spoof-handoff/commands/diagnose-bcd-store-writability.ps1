[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$ReportDirectory
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$storeRoot=[IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
$storePackage=Join-Path $storeRoot 'spoof-handoff'
$storeOutput=[IO.Path]::GetFullPath($ReportDirectory).TrimEnd('\')
if(-not $storeOutput.StartsWith($storePackage+'\reports\',[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence must remain under spoof-handoff/reports.'}
$storePrincipal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $storePrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevation is required.'}
if(Test-Path -LiteralPath $storeOutput){throw 'Evidence directory must be new.'}
New-Item -ItemType Directory -Path $storeOutput | Out-Null
$storeTemporary=Join-Path $storeOutput 'temporary'
New-Item -ItemType Directory -Path $storeTemporary | Out-Null
$env:TEMP=$storeTemporary;$env:TMP=$storeTemporary
. (Join-Path $PSScriptRoot '..\tools\smbios_boot\snapshot-reference.ps1') -RepoRoot $storeRoot -OutputBoundary $storePackage -LibraryOnly
$auditLog=Join-Path $storeOutput 'audit.log'
# Import process/log helpers only; the earlier diagnostic body is never invoked.
$variantOutput=$storeOutput
$variantCommands=[Collections.Generic.List[object]]::new()
$storeTokens=$null;$storeParseErrors=$null
$storeHelperPath=Join-Path $PSScriptRoot 'diagnose-live-bcd-variants.ps1'
$storeAst=[Management.Automation.Language.Parser]::ParseFile($storeHelperPath,[ref]$storeTokens,[ref]$storeParseErrors)
if($storeParseErrors.Count){throw 'The diagnostic helper does not parse.'}
foreach($helperName in @('Save-VariantJson','Invoke-VariantBcd','Get-VariantEsp')){
    $definitions=@($storeAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $helperName},$true))
    if($definitions.Count -ne 1){throw ('Cannot resolve helper: '+$helperName)}
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}
function Save-StoreJson([string]$Name,$Value){Save-VariantJson $Name $Value}
function Get-StoreOption([string]$Text,[string]$Name){
    $match=[regex]::Match($Text,('(?m)^'+[regex]::Escape($Name)+'\s+([^\r\n]+)'))
    [pscustomobject]@{name=$Name;present=$match.Success;value=$(if($match.Success){$match.Groups[1].Value.TrimEnd()}else{$null})}
}
$storeMount=$null;$storeFailure=$null;$storeFinalFailure=$null
$storeBefore=$null;$storeCurrentId=$null;$storeDescription=$null;$storeTestSigning=$null
$storeSecureBefore=$null;$storeSecureAfter=$null
$storeBcdEqual=$false;$storeFirmwareEqual=$false;$storeEspEqual=$false;$storeMountRemoved=$false
$storeEspBefore=@();$storeEspAfter=@();$storeBackupChecks=@();$storeLiteralResults=@();$storeRestores=@()
$storeDescriptionAttempted=$false;$storeTestAttempted=$false;$storeDescriptionResult=$null;$storeTestResult=$null
$storeExplicitMatches=$false;$storeFirmwareEnum=$null;$storeDbgobjEnum=$null
try{
    $storeBefore=Invoke-VariantBcd @('/enum','all','/v') 'baseline-all'
    $storeCurrent=Invoke-VariantBcd @('/enum','{current}','/v') 'baseline-current'
    $storeExportPath=Join-Path $storeOutput 'system-bcd.export'
    $storeExport=Invoke-VariantBcd @('/export',$storeExportPath) 'baseline-export'
    if($storeBefore.exitCode -ne 0 -or $storeCurrent.exitCode -ne 0 -or $storeExport.exitCode -ne 0){throw 'Cannot capture the live BCD baseline.'}
    $storeCurrentId=[regex]::Match($storeCurrent.stdout,'(?m)^identifier\s+(\{[0-9A-Fa-f-]{36}\})\s*$').Groups[1].Value
    if(-not $storeCurrentId){throw 'Cannot resolve the current loader GUID.'}
    $storeDescription=Get-StoreOption $storeCurrent.stdout 'description'
    $storeTestSigning=Get-StoreOption $storeCurrent.stdout 'testsigning'
    if(-not $storeDescription.present -or -not $storeDescription.value){throw 'Cannot resolve the original current-loader description.'}
    $storeSecureBefore=Confirm-SecureBootUEFI
    Initialize-ReadyFirmwareReader
    [ReadyToArmFirmwareRead]::Enable()
    $storeOrder=Read-VariableRecord 'BootOrder' $globalGuid
    $storeNext=Read-VariableRecord 'BootNext' $globalGuid
    $storeBootCurrent=Read-VariableRecord 'BootCurrent' $globalGuid
    $storeBootEntries=@(Read-ReadyBootEntries $storeOrder $storeNext $storeBootCurrent $globalGuid)
    foreach($name in @('SecureBoot','SetupMode','AuditMode','DeployedMode','PK','KEK')){$null=Read-VariableRecord $name $globalGuid}
    foreach($name in @('db','dbx')){$null=Read-VariableRecord $name $imageGuid}
    foreach($name in @('MokListRT','MokListXRT','MokSBStateRT','SbatLevelRT','SbatLevel','SbatPolicy','MokIgnoreDB')){$null=Read-VariableRecord $name $shimGuid}
    Save-StoreJson 'firmware-before.json' $firmwareRecords.ToArray()
    Save-StoreJson 'boot-entries.json' $storeBootEntries
    Save-StoreJson 'original-options.json' @($storeDescription,$storeTestSigning)
    # Capture the complete requested objects before introducing a temporary drive mapping.
    $storeFirmwareEnum=Invoke-VariantBcd @('/enum','firmware','/v') '01-enum-firmware-v'
    $storeDbgobjEnum=Invoke-VariantBcd @('/enum','{dbgobj}','/v') '02-enum-dbgobj-v'
    $storeMount=Mount-Esp
    $storeEspBefore=@(Get-VariantEsp $storeMount.EspRoot | Sort-Object path)
    $storeBackup=Join-Path $storeOutput 'ESP'
    New-Item -ItemType Directory -Path $storeBackup | Out-Null
    foreach($entry in $storeEspBefore){
        $destination=Join-Path $storeBackup $entry.path
        if($entry.directory){New-Item -ItemType Directory -Path $destination -Force | Out-Null;continue}
        if($entry.activeBcd){continue}
        $source=Join-Path $storeMount.EspRoot $entry.path
        Copy-Item -LiteralPath $source -Destination $destination
        $backupHash=Get-Sha256 $destination;$liveHash=Get-Sha256 $source
        $storeBackupChecks+=[pscustomobject]@{path=$entry.path;expected=$entry.sha256;backupActual=$backupHash;liveActual=$liveHash;matches=($backupHash -ceq $entry.sha256 -and $liveHash -ceq $entry.sha256)}
    }
    Save-StoreJson 'esp-before.json' $storeEspBefore
    Save-StoreJson 'esp-backup-verification.json' $storeBackupChecks
    if(@($storeBackupChecks | Where-Object matches -eq $false).Count){throw 'ESP backup drift; no setter has run.'}
    $storeBcdPath=Join-Path $storeMount.EspRoot 'EFI\Microsoft\Boot\BCD'
    $storeBcdItem=Get-Item -LiteralPath $storeBcdPath -Force
    $storeBcdAcl=Get-Acl -LiteralPath $storeBcdPath
    $storePartitions=@(Get-Partition | Where-Object {$_.GptType -eq '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}' -and @($_.AccessPaths) -contains $storeMount.VolumeGuid})
    if($storePartitions.Count -ne 1){throw 'Cannot resolve the exact mounted ESP partition.'}
    $storeDisk=Get-Disk -Number $storePartitions[0].DiskNumber
    Save-StoreJson 'store-location.json' ([ordered]@{
        bcdFile=$storeBcdPath;espVolume=$storeMount.VolumeGuid
        disk=$storeDisk | Select-Object Number,PartitionStyle,IsReadOnly,IsOffline,OperationalStatus
        partition=$storePartitions[0] | Select-Object DiskNumber,PartitionNumber,GptType,IsReadOnly,Size,AccessPaths
        fileAttributes=[string]$storeBcdItem.Attributes;fileReadOnly=(($storeBcdItem.Attributes -band [IO.FileAttributes]::ReadOnly) -ne 0)
        bcdAclOwner=$storeBcdAcl.Owner;bcdAclSddl=$storeBcdAcl.GetSecurityDescriptorSddlForm([Security.AccessControl.AccessControlSections]::All)
        policyNamedEspPaths=@($storeEspBefore | Where-Object {$_.path -match '(?i)(lock|polic|\.cip$|\.p7b$)'})
        note='Filenames and attributes do not establish a firmware lock. The valid same-value description write below separately tests BCD writability.'
    })
    Save-StoreJson 'snapshot.json' ([ordered]@{
        capturedUtc=[DateTime]::UtcNow.ToString('o');currentOsIdentifier=$storeCurrentId
        originalDescription=$storeDescription;originalTestSigning=$storeTestSigning;secureBoot=$storeSecureBefore
        bcdExport=$storeExportPath;bcdExportSha256=Get-Sha256 $storeExportPath;espBackup=$storeBackup
        backedUpEspFiles=$storeBackupChecks.Count;firmwareVariables='firmware-before.json';bootEntries='boot-entries.json'
        commandHelperPath=$storeHelperPath;commandHelperSha256=Get-Sha256 $storeHelperPath
        limits='File backup, excluding filesystem metadata and locked active BCD hive/logs. The live BCD has a consistent export. Not a sector image.'
    })
    $storeExplicit=Invoke-VariantBcd @('/store',$storeBcdPath,'/enum',$storeCurrentId,'/v') '03-explicit-store-current'
    $storeDefault=Invoke-VariantBcd @('/enum',$storeCurrentId,'/v') '04-default-store-current'
    $storeExplicitMatches=($storeExplicit.exitCode -eq 0 -and $storeDefault.exitCode -eq 0 -and $storeExplicit.stdout -ceq $storeDefault.stdout)
    if(-not $storeExplicitMatches){throw 'The explicit ESP BCD does not match the live current-loader object.'}
    foreach($case in @(
        @{name='05-literal-firmware-secureboot-on';arguments=@('/set','firmware','secureboot','on')},
        @{name='06-literal-firmware-dbgsigning-on';arguments=@('/set','firmware','dbgsigning','on')},
        @{name='07-literal-dbgobj-dbgsigning-on';arguments=@('/set','{dbgobj}','dbgsigning','on')}
    )){
        $result=Invoke-VariantBcd $case.arguments $case.name
        $storeLiteralResults+=$result
        Save-StoreJson 'literal-setters.json' $storeLiteralResults
        if($result.exitCode -eq 0){throw 'An unsupported literal setter unexpectedly succeeded. Stop for snapshot review; do not reboot.'}
    }
    # A documented datatype and real GUID distinguish a writable BCD from invalid commands.
    $storeDescriptionAttempted=$true
    Save-StoreJson 'control-journal.json' ([ordered]@{currentOsIdentifier=$storeCurrentId;descriptionAttempted=$true;originalDescription=$storeDescription;testSigningAttempted=$false;originalTestSigning=$storeTestSigning})
    $storeDescriptionResult=Invoke-VariantBcd @('/set',$storeCurrentId,'description',$storeDescription.value) '08-valid-same-description-write'
    $storeDescriptionRead=Invoke-VariantBcd @('/enum',$storeCurrentId,'/v') '09-after-same-description-write'
    $storeReadDescription=Get-StoreOption $storeDescriptionRead.stdout 'description'
    if($storeDescriptionRead.exitCode -ne 0 -or $storeReadDescription.value -cne $storeDescription.value){throw 'Same-value description control did not preserve its original value.'}
    # Use the supported signing option, restoring its original representation before exit.
    $storeTestAttempted=$true
    Save-StoreJson 'control-journal.json' ([ordered]@{currentOsIdentifier=$storeCurrentId;descriptionAttempted=$true;originalDescription=$storeDescription;testSigningAttempted=$true;originalTestSigning=$storeTestSigning})
    $storeTestResult=Invoke-VariantBcd @('/set',$storeCurrentId,'testsigning','on') '10-valid-testsigning-on'
    $null=Invoke-VariantBcd @('/enum',$storeCurrentId,'/v') '11-after-testsigning-attempt'
    $null=Invoke-VariantBcd @('/?','ID') '12-help-id'
    foreach($type in @('BOOTAPP','OSLOADER','BOOTMGR','FWBOOTMGR')){$null=Invoke-VariantBcd @('/?','TYPES',$type) ('help-types-'+$type.ToLowerInvariant())}
}catch{$storeFailure=$_.Exception.ToString()}
finally{
    try{
        if($storeTestAttempted){
            $current=Invoke-VariantBcd @('/enum',$storeCurrentId,'/v') 'cleanup-current-before'
            if($current.exitCode -ne 0){throw 'Cannot inspect current signing state for cleanup.'}
            $actual=Get-StoreOption $current.stdout 'testsigning'
            if($actual.present -ne $storeTestSigning.present -or $actual.value -cne $storeTestSigning.value){
                $arguments=if($storeTestSigning.present){@('/set',$storeCurrentId,'testsigning',$storeTestSigning.value)}else{@('/deletevalue',$storeCurrentId,'testsigning')}
                $restore=Invoke-VariantBcd $arguments 'restore-testsigning'
                $storeRestores+=$restore
                if($restore.exitCode -ne 0){throw 'The original TESTSIGNING representation could not be restored.'}
            }
        }
        if($storeDescriptionAttempted){
            $current=Invoke-VariantBcd @('/enum',$storeCurrentId,'/v') 'cleanup-description-read'
            if($current.exitCode -ne 0){throw 'Cannot inspect current description for cleanup.'}
            $actual=Get-StoreOption $current.stdout 'description'
            if($actual.value -cne $storeDescription.value){
                $restore=Invoke-VariantBcd @('/set',$storeCurrentId,'description',$storeDescription.value) 'restore-description'
                $storeRestores+=$restore
                if($restore.exitCode -ne 0){throw 'The original loader description could not be restored.'}
            }
        }
    }catch{$storeFinalFailure=$_.Exception.ToString()}
    Save-StoreJson 'restores.json' $storeRestores
    try{
        if($storeMount){
            $storeEspAfter=@(Get-VariantEsp $storeMount.EspRoot | Sort-Object path)
            Save-StoreJson 'esp-after.json' $storeEspAfter
            $storeEspEqual=($storeEspBefore.Count -gt 0 -and ($storeEspBefore | ConvertTo-Json -Depth 6 -Compress) -ceq ($storeEspAfter | ConvertTo-Json -Depth 6 -Compress))
            Dismount-Esp $storeMount
        }
        $final=Invoke-VariantBcd @('/enum','all','/v') 'final-all'
        $storeBcdEqual=($null -ne $storeBefore -and $final.exitCode -eq 0 -and $final.stdout -ceq $storeBefore.stdout -and $final.stderr -ceq $storeBefore.stderr)
        $storeSecureAfter=Confirm-SecureBootUEFI
        [ReadyToArmFirmwareRead]::Enable()
        Assert-UnchangedVariables
        $storeFirmwareEqual=$true
    }catch{$storeFinalFailure=$_.Exception.ToString()}
    if($storeMount -and (Test-Path -LiteralPath $storeMount.EspRoot)){Dismount-Esp $storeMount}
    $storeMountRemoved=($null -eq $storeMount -or -not(Test-Path -LiteralPath $storeMount.EspRoot))
    Save-StoreJson 'result.json' ([ordered]@{
        finishedUtc=[DateTime]::UtcNow.ToString('o');literalSetters=$storeLiteralResults;descriptionControl=$storeDescriptionResult
        supportedTestSigningAttempt=$storeTestResult;restores=$storeRestores;explicitStoreCurrentMatchesLive=$storeExplicitMatches
        storeWritabilityProven=($null -ne $storeDescriptionResult -and $storeDescriptionResult.exitCode -eq 0)
        secureBootBefore=$storeSecureBefore;secureBootAfter=$storeSecureAfter
        bcdListingByteIdentical=$storeBcdEqual;capturedFirmwareByteIdentical=$storeFirmwareEqual
        espFilesAndLayoutUnchanged=$storeEspEqual;backedUpEspFiles=$storeBackupChecks.Count;temporaryEspMountRemoved=$storeMountRemoved
        firmwareSecureBootLockProven=$false;rebooted=$false;driverInstalledOrLoaded=$false
        failure=$storeFailure;verificationFailure=$storeFinalFailure
        limits='A writable BCD and a rejected TESTSIGNING setting are distinct from firmware setup lock state. No firmware Boolean was written and no driver visibility proof was attempted. BCD listing equality is not identical exported-hive serialization.'
    })
}
if($storeFailure -or $storeFinalFailure -or -not $storeBcdEqual -or -not $storeFirmwareEqual -or -not $storeEspEqual -or -not $storeMountRemoved -or $storeSecureBefore -ne $storeSecureAfter){throw 'Audit or preservation check failed; inspect result.json.'}
'BCD STORE PROBE COMPLETE; ORIGINAL SETTINGS, CAPTURED FIRMWARE AND ESP FILES VERIFIED.'
