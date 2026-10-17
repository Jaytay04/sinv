$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$restoreWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$restoreRepo=[IO.Path]::GetFullPath((Join-Path $restoreWorktree '..\..\..'))
$restorePackage=Join-Path $restoreRepo 'spoof-handoff'
$restoreStatePath=Join-Path $restorePackage 'artifacts\secureboot-read-probe\prepared.json'
$restoreState=Get-Content -LiteralPath $restoreStatePath -Raw|ConvertFrom-Json
if($restoreState.schema -ne 1 -or $restoreState.mode -cne 'read' -or $restoreState.phase -cne 'original-stage-restored' -or $restoreState.armed -or $restoreState.readyToArm){throw 'The read probe must already be rolled back and disarmed.'}
foreach($pin in $restoreState.pinnedFiles){if((Get-FileHash -LiteralPath $pin.path -Algorithm SHA256).Hash -cne $pin.sha256){throw ('Pinned probe input changed: '+$pin.path)}}
$restoreSnapshot=[IO.Path]::GetFullPath($restoreState.snapshot)
if(-not $restoreSnapshot.StartsWith($restorePackage+'\reports\',[StringComparison]::OrdinalIgnoreCase)){throw 'Snapshot is outside handoff reports.'}
. (Join-Path $PSScriptRoot 'esp-policy.ps1')
. (Join-Path $PSScriptRoot '..\smbios_boot\snapshot-reference.ps1') -RepoRoot $restoreRepo -OutputBoundary $restorePackage -LibraryOnly
Assert-Admin
$restoreOutput=Join-Path $restorePackage ('reports\secureboot-read-20261016\restore-verification.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $restoreOutput|Out-Null
$env:TEMP=Join-Path $restoreOutput 'temporary';New-Item -ItemType Directory -Path $env:TEMP|Out-Null;$env:TMP=$env:TEMP
$restoreVerdict=[ordered]@{schema=1;result='FAIL';capturedUtc=[DateTime]::UtcNow.ToString('o');statePath=$restoreStatePath;stateSha256=(Get-Sha256 $restoreStatePath);snapshot=$restoreSnapshot;scope='Original boot state and immutable ESP files; boot-status drift recorded separately';secureBootOn=$false;firmware=@();esp=@();bcdMatches=$false;error=$null}
$restoreMount=$null
try {
    if(-not(Confirm-SecureBootUEFI)){throw 'Secure Boot is not ON.'}
    $restoreVerdict.secureBootOn=$true
    Initialize-ReadyFirmwareReader;[ReadyToArmFirmwareRead]::Enable()
    $restoreFirmware=@(foreach($record in (Get-Content -LiteralPath (Join-Path $restoreSnapshot 'firmware-variables.json') -Raw|ConvertFrom-Json)){
        if($record.name -notmatch '^(BootOrder|BootNext|Boot[0-9A-Fa-f]{4}|SecureBoot|SetupMode|PK|KEK|db|dbx)$'){continue}
        [uint32]$attributes=0;[int]$errorCode=0
        $data=[ReadyToArmFirmwareRead]::Read($record.name,$record.guid,[ref]$attributes,[ref]$errorCode)
        $encoded=if($null -ne $data){[Convert]::ToBase64String($data)}else{$null}
        [pscustomobject][ordered]@{name=$record.name;guid=$record.guid;expectedBase64=$record.base64;actualBase64=$encoded;expectedAttributes=$record.attributes;actualAttributes=$attributes;expectedWin32=$record.win32Error;actualWin32=$errorCode;pass=($errorCode -eq $record.win32Error -and $attributes -eq $record.attributes -and $encoded -ceq $record.base64)}
    })
    $restoreVerdict.firmware=$restoreFirmware
    $restoreBadFirmware=@($restoreFirmware|Where-Object {-not $_.pass})
    if($restoreBadFirmware.Count){$r=$restoreBadFirmware[0];throw ('Firmware mismatch: '+$r.name+' expected='+$r.expectedBase64+' actual='+$r.actualBase64+' Win32='+$r.actualWin32)}
    $restoreExpected=@(ConvertFrom-RestoreLayoutJson ([IO.File]::ReadAllText((Join-Path $restoreSnapshot 'esp-layout.json'))))
    $restoreMetadata=Get-Content -LiteralPath (Join-Path $restoreSnapshot 'snapshot.json') -Raw|ConvertFrom-Json
    foreach($file in $restoreExpected){
        $relative=ConvertTo-RestorePath $file.path
        if($file.sha256 -and (Get-Sha256 (Join-Path (Join-Path $restoreSnapshot 'ESP') $relative)) -cne $file.sha256){throw ('Saved ESP backup changed: '+$relative)}
    }
    $restoreMount=Mount-Esp
    if($restoreMount.VolumeGuid -cne $restoreMetadata.volumeGuid){throw 'Mounted ESP differs from snapshot volume.'}
    $restoreActual=@(foreach($rootItem in @(Get-ChildItem -LiteralPath $restoreMount.EspRoot -Force)){
        if($rootItem.Name -eq 'System Volume Information'){continue}
        $tree=if($rootItem.PSIsContainer){@($rootItem)+@(Get-ChildItem -LiteralPath $rootItem.FullName -Recurse -Force)}else{@($rootItem)}
        foreach($item in $tree){
            if(Test-ReparsePoint $item.FullName){throw ('ESP reparse point refused: '+$item.FullName)}
            $relative=$item.FullName.Substring($restoreMount.EspRoot.Length)
            $hash=if(-not $item.PSIsContainer -and $relative -notmatch '^EFI\\Microsoft\\Boot\\BCD(?:\.LOG[0-9]*)?$'){Get-Sha256 $item.FullName}else{$null}
            [pscustomobject]@{path=$relative;directory=$item.PSIsContainer;sha256=$hash}
        }
    })
    $restoreVerdict.esp=@(Compare-RestoreEspFiles $restoreExpected $restoreActual)
    $restoreBootStatus=Join-Path $restoreMount.EspRoot 'EFI\Microsoft\Boot\BOOTSTAT.DAT'
    $restoreCopy=Join-Path $restoreOutput 'current-BOOTSTAT.DAT'
    Copy-Item -LiteralPath $restoreBootStatus -Destination $restoreCopy
    $restoreBootStatusRow=@($restoreVerdict.esp|Where-Object path -IEQ 'EFI\Microsoft\Boot\BOOTSTAT.DAT')
    if($restoreBootStatusRow.Count -ne 1 -or (Get-Sha256 $restoreCopy) -cne $restoreBootStatusRow[0].actualSha256 -or (Get-Sha256 $restoreBootStatus) -cne $restoreBootStatusRow[0].actualSha256){throw 'Boot-status data changed during evidence copy; retry verification.'}
    $restoreBadFiles=@($restoreVerdict.esp|Where-Object {-not $_.pass})
    if($restoreBadFiles.Count){$r=$restoreBadFiles[0];throw ('ESP mismatch: '+$r.path+' class='+$r.classification+' expected='+$r.expectedSha256+' actual='+$r.actualSha256)}
    if((Get-BootManagerHash $restoreMount.EspRoot) -cne $restoreMetadata.bootManagerSha256){throw 'Windows Boot Manager differs from snapshot.'}
    Dismount-Esp $restoreMount;$restoreMount=$null
    $before=[IO.File]::ReadAllLines((Join-Path $restoreSnapshot 'bcd-all.txt'))
    $after=@(& bcdedit.exe /enum all /v);$restoreBcdExit=$LASTEXITCODE
    [IO.File]::WriteAllLines((Join-Path $restoreOutput 'bcd-all.txt'),[string[]]$after,[Text.UTF8Encoding]::new($false))
    $restoreVerdict.bcdMatches=($restoreBcdExit -eq 0 -and ($before -join "`n") -ceq ($after -join "`n"))
    if(-not $restoreVerdict.bcdMatches){throw 'Original BCD enumeration mismatch.'}
    if((Get-Sha256 $restoreStatePath) -cne $restoreVerdict.stateSha256){throw 'Preparation journal changed during verification.'}
    $restoreVerdict.result='PASS'
} catch { $restoreVerdict.error=$_.Exception.Message;throw }
finally {
    if($restoreMount){Dismount-Esp $restoreMount}
    [IO.File]::WriteAllText((Join-Path $restoreOutput 'verdict.json'),(ConvertTo-Json -InputObject $restoreVerdict -Depth 12),[Text.UTF8Encoding]::new($false))
}
'RESTORED BOOT STATE PASS '+(Join-Path $restoreOutput 'verdict.json')
'Secure Boot ON; boot configuration and immutable ESP files match. BOOTSTAT.DAT drift is recorded; no ESP files were changed.'
