param([Parameter(Mandatory=$true)][string]$RepositoryRoot,[Parameter(Mandatory=$true)][string]$SnapshotDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$readRoot=[IO.Path]::GetFullPath($RepositoryRoot)
$readBoundary=Join-Path $readRoot 'spoof-handoff'
$readSnapshot=[IO.Path]::GetFullPath($SnapshotDirectory)
if(-not $readSnapshot.StartsWith($readBoundary+'\reports\',[StringComparison]::OrdinalIgnoreCase)){throw 'Snapshot must remain under handoff reports.'}
. (Join-Path $PSScriptRoot '..\smbios_boot\snapshot-reference.ps1') -RepoRoot $readRoot -OutputBoundary $readBoundary -LibraryOnly
Assert-Admin
if(Test-Path -LiteralPath $readSnapshot){throw 'Snapshot destination must be new.'}
New-Item -ItemType Directory -Path $readSnapshot|Out-Null
$env:TEMP=Join-Path $readSnapshot 'temporary';New-Item -ItemType Directory -Path $env:TEMP|Out-Null;$env:TMP=$env:TEMP
$auditLog=Join-Path $readSnapshot 'audit.log'
function Save-ReadSnapshot([string]$Name,$Value){[IO.File]::WriteAllText((Join-Path $readSnapshot $Name),(ConvertTo-Json -InputObject $Value -Depth 14),[Text.UTF8Encoding]::new($false))}
$readMount=$null
try{
    if(-not(Confirm-SecureBootUEFI)){throw 'Secure Boot must remain ON.'}
    Initialize-ReadyFirmwareReader;[ReadyToArmFirmwareRead]::Enable()
    $order=Read-VariableRecord 'BootOrder' $globalGuid;$next=Read-VariableRecord 'BootNext' $globalGuid;$current=Read-VariableRecord 'BootCurrent' $globalGuid
    $entries=@(Read-ReadyBootEntries $order $next $current $globalGuid)
    if($next.present){throw 'BootNext already exists; no staging is allowed.'}
    foreach($name in @('SecureBoot','SetupMode','AuditMode','DeployedMode','PK','KEK')){$null=Read-VariableRecord $name $globalGuid}
    foreach($name in @('db','dbx')){$null=Read-VariableRecord $name $imageGuid}
    foreach($name in @('MokListRT','MokListXRT','MokSBStateRT','SbatLevelRT','SbatLevel','SbatPolicy','MokIgnoreDB')){$null=Read-VariableRecord $name $shimGuid}
    Save-ReadSnapshot 'firmware-variables.json' $firmwareRecords.ToArray();Save-ReadSnapshot 'boot-entries.json' $entries
    $bcd=@(& bcdedit.exe /enum all /v);if($LASTEXITCODE){throw 'BCD enumeration failed.'}
    [IO.File]::WriteAllLines((Join-Path $readSnapshot 'bcd-all.txt'),[string[]]$bcd)
    & bcdedit.exe /export (Join-Path $readSnapshot 'system-bcd.export')|Out-Null;if($LASTEXITCODE){throw 'BCD export failed.'}
    $readMount=Mount-Esp
    $stagePath=Join-Path $readMount.EspRoot 'EFI\HypersinvShimInventory'
    $sources=Get-ExistingShimStageSources $stagePath
    foreach($relative in @('HypersinvSecureBootRead.txt','HypersinvEfiInventory.txt','EFI\HypersinvShimInventory\chain-status.txt','EFI\HypersinvShimInventory\chain-diagnostic.txt')){if(Test-Path -LiteralPath (Join-Path $readMount.EspRoot $relative)){throw ('Uncollected output exists: '+$relative)}}
    $layout=@();$backup=Join-Path $readSnapshot 'ESP';New-Item -ItemType Directory -Path $backup|Out-Null
    foreach($rootItem in @(Get-ChildItem -LiteralPath $readMount.EspRoot -Force)){
        if($rootItem.Name -eq 'System Volume Information'){continue}
        $tree=if($rootItem.PSIsContainer){@($rootItem)+@(Get-ChildItem -LiteralPath $rootItem.FullName -Recurse -Force)}else{@($rootItem)}
        foreach($item in $tree){
            if(Test-ReparsePoint $item.FullName){throw 'ESP reparse point refused.'}
            $relative=$item.FullName.Substring($readMount.EspRoot.Length);$destination=Join-Path $backup $relative
            $hash=$null
            if($item.PSIsContainer){New-Item -ItemType Directory -Path $destination -Force|Out-Null}
            elseif($relative -notmatch '^EFI\\Microsoft\\Boot\\BCD(?:\.LOG[0-9]*)?$'){
                $hash=Get-Sha256 $item.FullName;Copy-Item -LiteralPath $item.FullName -Destination $destination
                if((Get-Sha256 $destination) -cne $hash -or (Get-Sha256 $item.FullName) -cne $hash){throw ('ESP snapshot drift: '+$relative)}
            }
            $layout+=@([ordered]@{path=$relative;directory=$item.PSIsContainer;sha256=$hash})
        }
    }
    Save-ReadSnapshot 'esp-layout.json' $layout
    Save-ReadSnapshot 'snapshot.json' ([ordered]@{schema=1;volumeGuid=$readMount.VolumeGuid;bootManagerSha256=(Get-BootManagerHash $readMount.EspRoot);stageFiles=$sources.Files;manifestSha256=(Get-Sha256 (Join-Path $stagePath 'manifest.sha256'));secureBoot=$true;bootNextPresent=$false;capturedUtc=[DateTime]::UtcNow.ToString('o');limits='File backup; active BCD hive/logs use system-bcd.export and filesystem metadata is excluded.'})
    Assert-UnchangedVariables
}finally{if($readMount){Dismount-Esp $readMount}}
$after=@(& bcdedit.exe /enum all /v);if($LASTEXITCODE -or ($after -join "`n") -cne ($bcd -join "`n")){throw 'BCD changed across snapshot.'}
'FRESH SNAPSHOT VERIFIED. No firmware variable, BCD or ESP file changed.'
