param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo='C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$package=Join-Path $repo 'spoof-handoff'
$worktree=Join-Path $repo '.codex-build\worktrees\smbios-boot-serial-probe'
$source=Join-Path $worktree 'spoof-handoff\tools\smbios_boot'
$statePath=Join-Path $package 'artifacts\smbios-seven-probe\prepared.json'
$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
. (Join-Path $source 'snapshot-reference.ps1') -RepoRoot $repo -OutputBoundary $package -LibraryOnly
. (Join-Path $repo 'tools\efi_inventory\shim-chain-collector.ps1') -InventoryBuildDir (Join-Path $package 'artifacts\smbios-seven-probe\inventory') -LoaderBuildDir (Join-Path $repo '.codex-build\efi_shim_loader') -SignedDirectory (Join-Path $worktree 'spoof-handoff\artifacts\smbios-seven-probe\signed') -LibraryOnly
Assert-Admin
Assert-NoReparseAncestors $OutputDirectory $package
if(Test-Path -LiteralPath $OutputDirectory){throw 'Audit directory must be new.'}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
$env:TEMP=Join-Path $OutputDirectory 'temporary'
$env:TMP=$env:TEMP
New-Item -ItemType Directory -Path $env:TEMP|Out-Null
function Save-AuditJson($Value,[string]$Name){[IO.File]::WriteAllText((Join-Path $OutputDirectory $Name),($Value|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))}
function Read-ImageRecord([string]$Path,[string]$CopyName){
    $item=Get-Item -LiteralPath $Path
    $hash=Get-Sha256 $Path
    $sig=Get-AuthenticodeSignature -LiteralPath $Path
    $chain=[Security.Cryptography.X509Certificates.X509Chain]::new()
    $chain.ChainPolicy.RevocationMode=[Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
    $chain.ChainPolicy.VerificationFlags=[Security.Cryptography.X509Certificates.X509VerificationFlags]::NoFlag
    $chainValid=$false;$elements=@();$errors=@()
    if($sig.SignerCertificate){
        $chainValid=$chain.Build($sig.SignerCertificate)
        $elements=@($chain.ChainElements|ForEach-Object{[ordered]@{subject=$_.Certificate.Subject;thumbprint=$_.Certificate.Thumbprint}})
        $errors=@($chain.ChainStatus|ForEach-Object{[string]$_.Status})
    }
    $subject=if($sig.SignerCertificate){$sig.SignerCertificate.Subject}else{$null}
    $thumbprint=if($sig.SignerCertificate){$sig.SignerCertificate.Thumbprint}else{$null}
    Copy-Item -LiteralPath $Path -Destination (Join-Path $OutputDirectory $CopyName)
    if((Get-Sha256 $Path) -cne $hash -or (Get-Sha256 (Join-Path $OutputDirectory $CopyName)) -cne $hash){throw 'Image changed during read-only capture.'}
    return [ordered]@{path=$Path;sha256=$hash;bytes=$item.Length;lastWriteUtc=$item.LastWriteTimeUtc.ToString('o');fileVersion=$item.VersionInfo.FileVersion;signatureStatus=[string]$sig.Status;signatureMessage=$sig.StatusMessage;signerSubject=$subject;signerThumbprint=$thumbprint;chainValidWithoutOnlineRevocation=$chainValid;chain=$elements;chainErrors=$errors;copy=$CopyName}
}
$mount=$null
try{
    $pins=@($state.pinnedFiles|ForEach-Object{[ordered]@{path=$_.path;expectedSha256=$_.sha256;actualSha256=(Get-Sha256 $_.path)}})
    Save-AuditJson $pins 'prepared-pins.json'
    Copy-Item -LiteralPath $statePath -Destination (Join-Path $OutputDirectory 'prepared-before.json')
    $receiptPath=Join-Path $package 'artifacts\smbios-seven-probe\inventory\shim-chain-stage.json'
    Copy-Item -LiteralPath $receiptPath -Destination (Join-Path $OutputDirectory 'receipt-before.json')
    $receipt=Get-Content -LiteralPath $receiptPath -Raw|ConvertFrom-Json
    $mount=Mount-Esp
    $live=Read-ImageRecord (Join-Path $mount.EspRoot 'EFI\Microsoft\Boot\bootmgfw.efi') 'live-bootmgfw.efi'
    $installed=Read-ImageRecord 'C:\Windows\Boot\EFI\bootmgfw.efi' 'installed-bootmgfw.efi'
    $old=Read-ImageRecord (Join-Path $state.snapshot 'ESP\EFI\Microsoft\Boot\bootmgfw.efi') 'snapshot-bootmgfw.efi'
    Save-AuditJson ([ordered]@{receiptHash=$receipt.bootManagerSha256;live=$live;installed=$installed;snapshot=$old;liveMatchesInstalled=($live.sha256 -ceq $installed.sha256);liveDiffersFromReceipt=($live.sha256 -cne $receipt.bootManagerSha256);volumeGuid=$mount.VolumeGuid}) 'boot-manager-comparison.json'
    $status=Get-MountedShimStageStatus $mount.EspRoot -AllowArtifacts
    Save-AuditJson ([ordered]@{bootManagerSha256=$status.BootHash;stageFiles=$status.Sources.Files;chainStatus=$status.ChainStatus}) 'stage-status.json'
    foreach($relative in @('HypersinvSmbiosPatch.txt','HypersinvEfiInventory.txt','EFI\HypersinvShimInventory\chain-status.txt','EFI\HypersinvShimInventory\chain-diagnostic.txt','EFI\HypersinvShimInventory\manifest.sha256')){
        $file=Join-Path $mount.EspRoot $relative
        if(Test-Path -LiteralPath $file -PathType Leaf){
            Copy-Item -LiteralPath $file -Destination (Join-Path $OutputDirectory ([IO.Path]::GetFileName($relative)))
        }
    }
    Initialize-ReadyFirmwareReader
    [ReadyToArmFirmwareRead]::Enable()
    $baseline=Get-Content -LiteralPath (Join-Path $state.snapshot 'firmware-variables.json') -Raw|ConvertFrom-Json
    $comparison=@(foreach($record in $baseline){
        $current=Read-VariableRecord $record.name $record.guid
        [ordered]@{name=$record.name;guid=$record.guid;matches=($record.win32Error -eq $current.win32Error -and $record.attributes -eq $current.attributes -and $record.base64 -ceq $current.base64);before=$record;current=$current}
    })
    Save-AuditJson $comparison 'firmware-comparison.json'
    # Capture nearby numbered options without changing BootNext or any option.
    foreach($number in 0..31){$null=Read-VariableRecord ('Boot{0:X4}' -f $number) '{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}'}
    Save-AuditJson $firmwareRecords.ToArray() 'firmware-current.json'
    & bcdedit.exe /enum firmware /v | Set-Content -LiteralPath (Join-Path $OutputDirectory 'bcd-firmware.txt')
    if($LASTEXITCODE -ne 0){throw 'Firmware BCD enumeration failed.'}
    if((Get-Sha256 (Join-Path $mount.EspRoot 'EFI\Microsoft\Boot\bootmgfw.efi')) -cne $live.sha256){throw 'Boot Manager changed during audit.'}
    $secure=Confirm-SecureBootUEFI
    Save-AuditJson ([ordered]@{result='PASS';secureBoot=$secure;windowsBoot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o');armRequested=$false;rebootRequested=$false;firmwareWritten=$false;espWritten=$false;capturedUtc=[DateTime]::UtcNow.ToString('o')}) 'audit-result.json'
}catch{
    ($_|Out-String)|Set-Content -LiteralPath (Join-Path $OutputDirectory 'audit-error.txt')
    throw
}finally{if($mount){Dismount-Esp $mount}}
$since=[DateTime]::UtcNow.AddHours(-36)
foreach($log in @('System','Microsoft-Windows-WindowsUpdateClient/Operational','Setup')){
    try{
        $filter=@{LogName=$log;StartTime=$since}
        if($log -ceq 'System'){$filter.Id=@(12,13,19,20,21,1074,6005,6006,41)}
        $events=@(Get-WinEvent -FilterHashtable $filter -MaxEvents 160 -ErrorAction Stop|ForEach-Object{[ordered]@{utc=$_.TimeCreated.ToUniversalTime().ToString('o');id=$_.Id;provider=$_.ProviderName;message=$_.Message}})
        Save-AuditJson $events ($log.Replace('/','-')+'-events.json')
    }catch{($_|Out-String)|Set-Content -LiteralPath (Join-Path $OutputDirectory ($log.Replace('/','-')+'-events-error.txt'))}
}
'READ-ONLY AUDIT '+$OutputDirectory
