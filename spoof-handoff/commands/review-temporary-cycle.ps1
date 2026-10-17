[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$ReportDirectory
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$cycleRepository=[IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
$cycleOutput=[IO.Path]::GetFullPath($ReportDirectory).TrimEnd('\')
$cyclePackage=Join-Path $cycleRepository 'spoof-handoff'
if(-not $cycleOutput.StartsWith($cyclePackage+'\reports\',[StringComparison]::OrdinalIgnoreCase)){
    throw 'Evidence must remain under spoof-handoff/reports.'
}
if(Test-Path -LiteralPath $cycleOutput){throw 'The evidence directory must be new.'}
$cyclePrincipal=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $cyclePrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
    throw 'The snapshot and offline BCD review require elevated PowerShell.'
}
New-Item -ItemType Directory -Path $cycleOutput | Out-Null
$cycleTemporary=Join-Path $cycleOutput 'temporary'
New-Item -ItemType Directory -Path $cycleTemporary | Out-Null
$env:TEMP=$cycleTemporary;$env:TMP=$cycleTemporary
. (Join-Path $PSScriptRoot '..\tools\smbios_boot\snapshot-reference.ps1') `
    -RepoRoot $cycleRepository -OutputBoundary $cyclePackage -LibraryOnly
$auditLog=Join-Path $cycleOutput 'audit.log'
function Write-CycleJson([string]$Name,$Value){
    [IO.File]::WriteAllText((Join-Path $cycleOutput $Name),($Value | ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($false))
}
function Invoke-CycleBcd([string[]]$Arguments,[string]$Log){
    $ErrorActionPreference='Continue'
    $cycleLines=@(& bcdedit.exe @Arguments 2>&1 | ForEach-Object {$_.ToString()})
    $cycleExit=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    [IO.File]::WriteAllLines((Join-Path $cycleOutput $Log),[string[]]$cycleLines,[Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{arguments=$Arguments;exitCode=$cycleExit;log=$Log}
}
$cycleMount=$null;$cycleFailure=$null;$cycleLayout=@();$cycleResults=@();$cycleChecks=@()
$cycleSecureBefore=Confirm-SecureBootUEFI
Initialize-ReadyFirmwareReader
[ReadyToArmFirmwareRead]::Enable()
$cycleOrder=Read-VariableRecord 'BootOrder' $globalGuid
$cycleNext=Read-VariableRecord 'BootNext' $globalGuid
$cycleCurrent=Read-VariableRecord 'BootCurrent' $globalGuid
$cycleEntries=@(Read-ReadyBootEntries $cycleOrder $cycleNext $cycleCurrent $globalGuid)
foreach($cycleName in @('SecureBoot','SetupMode','AuditMode','DeployedMode','PK','KEK')){
    $null=Read-VariableRecord $cycleName $globalGuid
}
foreach($cycleName in @('db','dbx')){$null=Read-VariableRecord $cycleName $imageGuid}
foreach($cycleName in @('MokListRT','MokListXRT','MokSBStateRT','SbatLevelRT','SbatLevel','SbatPolicy','MokIgnoreDB')){
    $null=Read-VariableRecord $cycleName $shimGuid
}
Write-CycleJson 'firmware-before.json' $firmwareRecords.ToArray()
Write-CycleJson 'boot-entries.json' $cycleEntries
$cycleBcdBefore=Invoke-CycleBcd @('/enum','all','/v') 'bcd-before.txt'
$cycleBcdCurrent=Invoke-CycleBcd @('/enum','{current}','/v') 'bcd-current.txt'
$cycleExportPath=Join-Path $cycleOutput 'system-bcd.export'
$cycleExport=Invoke-CycleBcd @('/export',$cycleExportPath) 'bcd-export.log'
if($cycleBcdBefore.exitCode -ne 0 -or $cycleBcdCurrent.exitCode -ne 0 -or $cycleExport.exitCode -ne 0){
    throw 'The active BCD could not be snapshotted.'
}
$cycleCurrentText=Get-Content -LiteralPath (Join-Path $cycleOutput 'bcd-current.txt') -Raw
$cycleIdMatch=[regex]::Match($cycleCurrentText,'(?m)^identifier\s+(\{[0-9A-Fa-f-]{36}\})\s*$')
if(-not $cycleIdMatch.Success){throw 'Cannot resolve the exact current OS entry identifier.'}
$cycleOsId=$cycleIdMatch.Groups[1].Value
$cycleOriginalOptions=@(foreach($cycleOption in @('testsigning','dbgsigning','debug','nointegritychecks','flightsigning','secureboot')){
    $cycleMatch=[regex]::Match($cycleCurrentText,('(?m)^'+$cycleOption+'\s+([^\r\n]+)'))
    [pscustomobject]@{name=$cycleOption;present=$cycleMatch.Success;value=$(if($cycleMatch.Success){$cycleMatch.Groups[1].Value}else{$null})}
})
Write-CycleJson 'original-state.json' ([ordered]@{
    capturedUtc=[DateTime]::UtcNow.ToString('o');secureBoot=$cycleSecureBefore;currentOsIdentifier=$cycleOsId
    currentEntryOptions=$cycleOriginalOptions;bcdExport=$cycleExportPath;bcdExportSha256=Get-Sha256 $cycleExportPath
    dbgsigning='No element present in current-entry listing; command/type support tested only in isolated copies.'
    baseBoard=Get-CimInstance Win32_BaseBoard | Select-Object Manufacturer,Product,Version
    bios=Get-CimInstance Win32_BIOS | Select-Object Manufacturer,SMBIOSBIOSVersion,ReleaseDate
    partitions=Get-Partition | Select-Object DiskNumber,PartitionNumber,GptType,Size,AccessPaths
    note='Absent BCD elements are retained as absent; setting OFF would not restore that representation.'
})
try {
    $cycleMount=Mount-Esp
    $cycleBackup=Join-Path $cycleOutput 'ESP'
    New-Item -ItemType Directory -Path $cycleBackup | Out-Null
    foreach($cycleRootItem in @(Get-ChildItem -LiteralPath $cycleMount.EspRoot -Force)){
        if($cycleRootItem.Name -eq 'System Volume Information'){continue}
        $cycleTree=if($cycleRootItem.PSIsContainer){@($cycleRootItem)+@(Get-ChildItem -LiteralPath $cycleRootItem.FullName -Recurse -Force)}else{@($cycleRootItem)}
        foreach($cycleItem in $cycleTree){
            if(Test-ReparsePoint $cycleItem.FullName){throw ('ESP reparse point: '+$cycleItem.FullName)}
            $cycleRelative=$cycleItem.FullName.Substring($cycleMount.EspRoot.Length)
            $cycleDestination=Join-Path $cycleBackup $cycleRelative
            if($cycleItem.PSIsContainer){
                New-Item -ItemType Directory -Path $cycleDestination -Force | Out-Null
                $cycleLayout+=[pscustomobject]@{path=$cycleRelative;directory=$true;bytes=0;sha256=$null;backup=$null}
            }elseif($cycleRelative -match '^EFI\\Microsoft\\Boot\\BCD(?:\.LOG[0-9]*)?$'){
                $cycleLayout+=[pscustomobject]@{path=$cycleRelative;directory=$false;bytes=$cycleItem.Length;sha256=$null;backup='system-bcd.export; active hive/logs use the consistent BCD export'}
            }else{
                $cycleHash=Get-Sha256 $cycleItem.FullName
                Copy-Item -LiteralPath $cycleItem.FullName -Destination $cycleDestination
                if((Get-Sha256 $cycleDestination) -cne $cycleHash -or (Get-Sha256 $cycleItem.FullName) -cne $cycleHash){
                    throw ('ESP snapshot drift: '+$cycleRelative)
                }
                $cycleLayout+=[pscustomobject]@{path=$cycleRelative;directory=$false;bytes=$cycleItem.Length;sha256=$cycleHash;backup=$cycleRelative}
            }
        }
    }
    Write-CycleJson 'esp-layout.json' ([ordered]@{volumeGuid=$cycleMount.VolumeGuid;entries=$cycleLayout;filesystemMetadataExcluded='System Volume Information'})
    foreach($cycleCase in @(
        @{name='secureboot-disable';type='secureboot';value='Disable'},
        @{name='secureboot-enable';type='secureboot';value='Enable'},
        @{name='dbgsigning-on';type='dbgsigning';value='ON'}
    )){
        $cycleStore=Join-Path $cycleOutput ('offline-'+$cycleCase.name+'.bcd')
        Copy-Item -LiteralPath $cycleExportPath -Destination $cycleStore
        $cycleResults+=Invoke-CycleBcd @('/store',$cycleStore,'/set',$cycleOsId,$cycleCase.type,$cycleCase.value) ($cycleCase.name+'.log')
    }
    Write-CycleJson 'offline-command-results.json' $cycleResults
    foreach($cycleFile in @($cycleLayout | Where-Object {$_.sha256})){
        $cycleActual=Get-Sha256 (Join-Path $cycleMount.EspRoot $cycleFile.path)
        $cycleChecks+=[pscustomobject]@{path=$cycleFile.path;expected=$cycleFile.sha256;actual=$cycleActual;matches=($cycleActual -ceq $cycleFile.sha256)}
    }
    if(@($cycleChecks | Where-Object matches -eq $false).Count -ne 0){throw 'ESP file drift; inspect esp-after-checks.json.'}
}catch{
    $cycleFailure=$_.Exception.ToString()
}finally{
    Write-CycleJson 'esp-after-checks.json' $cycleChecks
    if($cycleMount){Dismount-Esp $cycleMount}
    $cycleMountRemoved=($null -eq $cycleMount -or -not(Test-Path -LiteralPath $cycleMount.EspRoot))
    $cycleSecureAfter=Confirm-SecureBootUEFI
    [ReadyToArmFirmwareRead]::Enable()
    $cycleFirmwareEqual=$true
    try{Assert-UnchangedVariables}catch{$cycleFirmwareEqual=$false;$cycleFailure=$_.Exception.ToString()}
    $cycleBcdAfter=Invoke-CycleBcd @('/enum','all','/v') 'bcd-after.txt'
    $cycleBcdEqual=(Get-Sha256 (Join-Path $cycleOutput 'bcd-before.txt')) -ceq (Get-Sha256 (Join-Path $cycleOutput 'bcd-after.txt'))
    Write-CycleJson 'preservation.json' ([ordered]@{
        capturedUtc=[DateTime]::UtcNow.ToString('o');secureBootBefore=$cycleSecureBefore;secureBootAfter=$cycleSecureAfter
        firmwareByteIdentical=$cycleFirmwareEqual;bcdListingByteIdentical=$cycleBcdEqual;bcdAfterExit=$cycleBcdAfter.exitCode
        temporaryEspMountRemoved=$cycleMountRemoved;copiedEspFiles=@($cycleLayout | Where-Object {$_.sha256}).Count
        espFilesByteIdentical=(@($cycleChecks | Where-Object matches -eq $false).Count -eq 0)
        activeBcdMutated=$false;firmwareMutated=$false;certificateStoresMutated=$false;driverLoaded=$false
        serviceCreated=$false;resumeTaskCreated=$false;rebooted=$false;failure=$cycleFailure
        limits='No production cycle was started. No driver substitution or post-reboot restoration is claimed.'
    })
}
if($cycleFailure -or -not $cycleMountRemoved -or -not $cycleFirmwareEqual -or -not $cycleBcdEqual -or $cycleBcdAfter.exitCode -ne 0){
    throw 'Snapshot/preflight failed; inspect preservation.json before any next action.'
}
'SNAPSHOT AND OFFLINE COMMAND REVIEW COMPLETE. No active boot/security setting changed.'
