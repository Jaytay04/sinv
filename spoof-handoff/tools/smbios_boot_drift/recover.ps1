param([Parameter(Mandatory=$true)][string]$AuditDirectory,[Parameter(Mandatory=$true)][string]$OutputDirectory,[switch]$Apply)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Assert-DriftEvidence($Evidence){
    if($Evidence.oldBootManagerSha256 -cne '25D8869E3C79083AE62C698A9DB9030748E3267BD53593357447F3F72761D1F2' -or $Evidence.currentBootManagerSha256 -cne '2D60A2203865122DAD22772136B3D905167D9DCF926D602BB5F48B3B6668ABEC'){throw 'This helper only handles the specifically reviewed Windows update.'}
    if($Evidence.signatureVerdict -cne 'PASS' -or $Evidence.secureBoot -ne $true -or $Evidence.bootNextAbsent -ne $true -or $Evidence.liveAndInstalledExecutableMatch -ne $true -or $Evidence.allOtherFirmwareMatches -ne $true -or $Evidence.allOldDbxRevocationsRetained -ne $true -or $Evidence.addedDbxRevocations -ne 2 -or $Evidence.currentDbxSha256 -cne '80E5BD4420231830E306F46ED94644D2604EE7D78798D65C01B42A844095B093'){throw 'The reviewed signature, boot, security or revocation evidence does not match.'}
}
function Assert-DriftFirmwareCoverage($Comparison,$Original){
    if(@($Comparison).Count -ne 19 -or @($Original).Count -ne 19){throw 'The complete 19-record firmware comparison is required.'}
    foreach($record in $Original){
        $pairs=@($Comparison|Where-Object{$_.name -ceq $record.name -and $_.guid -ceq $record.guid})
        if($pairs.Count -ne 1){throw 'Missing, duplicate or wrong-guid firmware record.'}
        $pair=$pairs[0]
        foreach($value in @($pair.before,$pair.current)){if($value.name -cne $record.name -or $value.guid -cne $record.guid){throw 'Firmware identity differs from the original snapshot.'}}
        if($pair.before.attributes -ne $record.attributes -or $pair.before.win32Error -ne $record.win32Error -or $pair.before.base64 -cne $record.base64){throw 'Comparison does not match the immutable original firmware snapshot.'}
        if($record.name -cne 'dbx' -and ($pair.current.attributes -ne $record.attributes -or $pair.current.win32Error -ne $record.win32Error -or $pair.current.base64 -cne $record.base64)){throw 'An unreviewed boot or security value changed.'}
    }
}
function Get-ImageBodyHash([string]$Path){
    $b=[IO.File]::ReadAllBytes($Path)
    if($b.Length -lt 256 -or $b.Length -gt 16MB -or [BitConverter]::ToUInt16($b,0) -ne 0x5A4D){throw 'Invalid bounded image.'}
    $pe=[BitConverter]::ToUInt32($b,60);$opt=[uint64]$pe+24
    if($opt+152 -gt $b.Length -or [BitConverter]::ToUInt32($b,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($b,$opt) -ne 0x20B){throw 'Invalid PE32+ image.'}
    [uint64]$cert=[BitConverter]::ToUInt32($b,$opt+144);[uint64]$size=[BitConverter]::ToUInt32($b,$opt+148)
    if($cert -lt $opt+152 -or $size -lt 8 -or $cert+$size -gt $b.Length){throw 'Invalid PE certificate range.'}
    $sha=[Security.Cryptography.SHA256]::Create()
    try{
        $null=$sha.TransformBlock($b,0,[int]($opt+64),$null,0)
        $null=$sha.TransformBlock($b,[int]($opt+68),76,$null,0)
        $null=$sha.TransformBlock($b,[int]($opt+152),[int]($cert-$opt-152),$null,0)
        $null=$sha.TransformFinalBlock($b,[int]($cert+$size),[int]($b.Length-$cert-$size))
        return [BitConverter]::ToString($sha.Hash).Replace('-','')
    }finally{$sha.Dispose()}
}
$repo='C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$package=Join-Path $repo 'spoof-handoff'
$worktree=Join-Path $repo '.codex-build\worktrees\smbios-boot-serial-probe'
$source=Join-Path $worktree 'spoof-handoff\tools\smbios_boot'
$statePath=Join-Path $package 'artifacts\smbios-seven-probe\prepared.json'
$inventory=Join-Path $package 'artifacts\smbios-seven-probe\inventory'
$recoveryReceiptPath=Join-Path $inventory 'shim-chain-stage.json'
$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
$stateHash=(Get-FileHash -LiteralPath $statePath).Hash
. (Join-Path $source 'snapshot-reference.ps1') -RepoRoot $repo -OutputBoundary $package -LibraryOnly
. (Join-Path $repo 'tools\efi_inventory\shim-chain-collector.ps1') -InventoryBuildDir $inventory -LoaderBuildDir (Join-Path $repo '.codex-build\efi_shim_loader') -SignedDirectory (Join-Path $worktree 'spoof-handoff\artifacts\smbios-seven-probe\signed') -LibraryOnly
Assert-Admin
Assert-NoReparseAncestors $AuditDirectory $package
Assert-NoReparseAncestors $OutputDirectory $package
if(Test-Path -LiteralPath $OutputDirectory){throw 'Recovery evidence directory must be new.'}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
$env:TEMP=Join-Path $OutputDirectory 'temporary';$env:TMP=$env:TEMP
New-Item -ItemType Directory -Path $env:TEMP|Out-Null
function Write-DriftJson($Value,[string]$Name){[IO.File]::WriteAllText((Join-Path $OutputDirectory $Name),($Value|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))}
$variantReports=$OutputDirectory
$variantPackage=$package
$variantStatePath=$statePath
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $source 'run-probe.ps1'),[ref]$null,[ref]$null)
foreach($name in @('New-VariantReportPath','Clear-VariantRollbackBootState','Assert-VariantBootReport')){
    $definition=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
    if($definition.Count -ne 1){throw 'Expected exactly one reviewed helper definition.'}
    Invoke-Expression $definition[0].Extent.Text
}
$comparison=Get-Content -LiteralPath (Join-Path $AuditDirectory 'firmware-comparison.json') -Raw|ConvertFrom-Json
$originalFirmware=Get-Content -LiteralPath (Join-Path $state.snapshot 'firmware-variables.json') -Raw|ConvertFrom-Json
Assert-DriftFirmwareCoverage $comparison $originalFirmware
$images=Get-Content -LiteralPath (Join-Path $AuditDirectory 'boot-manager-comparison.json') -Raw|ConvertFrom-Json
$body=Get-Content -LiteralPath (Join-Path $AuditDirectory 'executable-byte-comparison.json') -Raw|ConvertFrom-Json
$dbx=Get-Content -LiteralPath (Join-Path $AuditDirectory 'dbx-delta.json') -Raw|ConvertFrom-Json
$signature=Get-Content -LiteralPath (Join-Path $AuditDirectory 'current-signature-check\signature-verdict.json') -Raw|ConvertFrom-Json
$audit=Get-Content -LiteralPath (Join-Path $AuditDirectory 'audit-result.json') -Raw|ConvertFrom-Json
$next=@($comparison|Where-Object name -CEQ 'BootNext')
$evidence=[pscustomobject]@{oldBootManagerSha256=$images.receiptHash;currentBootManagerSha256=$images.live.sha256;signatureVerdict=$signature.verdict;secureBoot=$audit.secureBoot;bootNextAbsent=($next.Count -eq 1 -and -not $next[0].current.present -and $next[0].current.win32Error -eq 203);liveAndInstalledExecutableMatch=$body.liveAndInstalledEqualOutsideChecksumAndCertificates;allOtherFirmwareMatches=(@($comparison|Where-Object{$_.name -cne 'dbx' -and -not $_.matches}).Count -eq 0);allOldDbxRevocationsRetained=$dbx.allPreviousRevocationsRetained;addedDbxRevocations=$dbx.addedRevocations;currentDbxSha256=$dbx.currentSha256}
Assert-DriftEvidence $evidence
Write-DriftJson $evidence 'reviewed-evidence.json'
Initialize-ReadyFirmwareReader
[ReadyToArmFirmwareRead]::Enable()
function Assert-VariantBaseline([string]$Ignored){
    if(-not(Confirm-SecureBootUEFI)){throw 'Secure Boot must remain ON.'}
    [ReadyToArmFirmwareRead]::Enable()
    foreach($pair in $comparison){$wanted=$pair.current;$current=Read-VariableRecord $wanted.name $wanted.guid;if($current.attributes -ne $wanted.attributes -or $current.win32Error -ne $wanted.win32Error -or $current.base64 -cne $wanted.base64){Write-DriftJson ([ordered]@{wanted=$wanted;current=$current}) 'firmware-drift.json';throw ('Post-update firmware drift: '+$wanted.name)};if($wanted.name -ceq 'dbx' -and $current.sha256 -cne '80E5BD4420231830E306F46ED94644D2604EE7D78798D65C01B42A844095B093'){throw 'Current dbx differs from the reviewed update hash.'}}
}
$mount=$null
try{
    if((Get-Sha256 $statePath) -cne (Get-Sha256 (Join-Path $AuditDirectory 'prepared-before.json')) -or (Get-Sha256 $recoveryReceiptPath) -cne (Get-Sha256 (Join-Path $AuditDirectory 'receipt-before.json'))){throw 'Interrupted journal or receipt changed since the read-only audit.'}
    foreach($pin in $state.pinnedFiles){if((Get-Sha256 $pin.path) -cne $pin.sha256){throw ('Prepared pin drift: '+$pin.path)}}
    if($state.mask -ne 127 -or $state.allSevenLiveProven -or $state.phase -cne 'armed-by-user-awaiting-new-boot'){throw 'Unexpected interrupted trial state.'}
    if((Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o') -cne $audit.windowsBoot){throw 'Windows boot changed since the audit.'}
    foreach($key in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending','HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired')){if(Test-Path -LiteralPath $key){throw 'Windows servicing still requires a reboot; recovery stopped.'}}
    Assert-VariantBaseline ''
    $auditedCurrent=Get-Content -LiteralPath (Join-Path $AuditDirectory 'firmware-current.json') -Raw|ConvertFrom-Json
    $auditedOwned=@($auditedCurrent|Where-Object name -CEQ 'Boot0001')
    $liveOwned=Read-VariableRecord 'Boot0001' '{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}'
    if($auditedOwned.Count -ne 1 -or -not $liveOwned.present -or $liveOwned.base64 -cne $auditedOwned[0].base64 -or $liveOwned.attributes -ne $auditedOwned[0].attributes){throw 'Owned Boot0001 differs from the reviewed audit.'}
    Copy-Item -LiteralPath $statePath -Destination (Join-Path $OutputDirectory 'interrupted-prepared.copy.json')
    Copy-Item -LiteralPath $recoveryReceiptPath -Destination (Join-Path $OutputDirectory 'interrupted-receipt.json')
    $mount=Mount-Esp
    $wm=Join-Path $mount.EspRoot 'EFI\Microsoft\Boot\bootmgfw.efi'
    if((Get-Sha256 $wm) -cne $evidence.currentBootManagerSha256 -or $mount.VolumeGuid -cne $images.volumeGuid){throw 'ESP or Boot Manager differs from the reviewed audit.'}
    foreach($path in @($wm,'C:\Windows\Boot\EFI\bootmgfw.efi')){
        $sig=Get-AuthenticodeSignature -LiteralPath $path
        if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Thumbprint -cne '667BBA1E1F478221AA65DE4F75187A751B03A453'){throw 'Current Microsoft image signature differs from the reviewed signer.'}
        if((Get-ImageBodyHash $path) -cne 'F1CBD3773547541226DBDB42C42E04CF230F14A1B48E48DBD8AD695ADF7E955C'){throw 'Installed and ESP executable bodies do not match the reviewed Windows image.'}
    }
    $status=Get-MountedShimStageStatus $mount.EspRoot -AllowArtifacts
    $receipt=Read-ShimReceipt
    # Compare every receipt property without substituting the new Boot Manager hash.
    if($receipt.bootManagerSha256 -cne $evidence.oldBootManagerSha256){throw 'Historical receipt differs from the reviewed old hash.'}
    foreach($f in $status.Sources.Files){$property=$f.Name.Replace('.efi','').Replace('.cer','')+'Sha256';if($receipt.$property -cne $f.Hash){throw 'Staged payload differs from the interrupted receipt.'}}
    if($receipt.manifestSha256 -cne (Get-Sha256 (Join-Path $status.Paths.StageRoot 'manifest.sha256'))){throw 'Staged manifest differs from the interrupted receipt.'}
    $before=Get-Content -LiteralPath (Join-Path $state.snapshot 'snapshot.json') -Raw|ConvertFrom-Json
    $saved=Join-Path $state.snapshot 'ESP\EFI\HypersinvShimInventory'
    if(@($before.stageFiles).Count -ne 5 -or (Get-Sha256 (Join-Path $saved 'manifest.sha256')) -cne $before.manifestSha256){throw 'Original stage backup is invalid.'}
    foreach($file in $before.stageFiles){if((Get-Sha256 (Join-Path $saved $file.Name)) -cne $file.Hash){throw 'Original stage backup bytes changed.'}}
    $snapshotRoot=Join-Path $OutputDirectory 'ESP-before-cleanup'
    New-Item -ItemType Directory -Path $snapshotRoot|Out-Null
    $layout=@()
    foreach($rootItem in @(Get-ChildItem -LiteralPath $mount.EspRoot -Force)){
        if($rootItem.Name -eq 'System Volume Information'){continue}
        $tree=if($rootItem.PSIsContainer){@($rootItem)+@(Get-ChildItem -LiteralPath $rootItem.FullName -Recurse -Force)}else{@($rootItem)}
        foreach($item in $tree){
            if(Test-ReparsePoint $item.FullName){throw 'ESP reparse point was preserved.'}
            $relative=$item.FullName.Substring($mount.EspRoot.Length);$destination=Join-Path $snapshotRoot $relative
            if($item.PSIsContainer){New-Item -ItemType Directory -Path $destination -Force|Out-Null;continue}
            if($relative -match '^EFI\\Microsoft\\Boot\\BCD(?:\.LOG[0-9]*)?$'){continue}
            $hash=Get-Sha256 $item.FullName
            Copy-Item -LiteralPath $item.FullName -Destination $destination
            if((Get-Sha256 $destination) -cne $hash -or (Get-Sha256 $item.FullName) -cne $hash){throw 'ESP changed during the recovery snapshot.'}
            $layout+=@([pscustomobject]@{path=$relative;sha256=$hash;bytes=$item.Length})
        }
    }
    Write-DriftJson $layout 'esp-before-cleanup-layout.json'
    & bcdedit.exe /export (Join-Path $OutputDirectory 'system-bcd-before.export')
    if($LASTEXITCODE -ne 0){throw 'Fresh BCD export failed.'}
    Write-DriftJson @($comparison|ForEach-Object{$_.current}) 'firmware-before-cleanup.json'
    $bootReport=Join-Path $snapshotRoot 'HypersinvSmbiosPatch.txt'
    Assert-VariantBootReport $bootReport $state.buildId
    $originalPath=Join-Path $OutputDirectory 'windows-original-before-cleanup.json'
    & (Join-Path $package 'artifacts\smbios-seven-probe\smbios_windows_readback.exe') --expect original --output $originalPath
    if($LASTEXITCODE -ne 0){throw 'Original native/WMI values are not visible; recovery stopped.'}
    if(-not $Apply){'RECOVERY PREFLIGHT PASS: no changes applied.';exit 0}
}finally{if($mount){Dismount-Esp $mount;$mount=$null}}
# Apply removes only the exact native-installer-owned option; no boot activation.
Clear-VariantRollbackBootState ''
$owned=Read-VariableRecord 'Boot0001' '{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}'
if($owned.present -or $owned.win32Error -ne 203){throw 'The audited owned Boot0001 was not removed.'}
$mount=Mount-Esp
try{
    if((Get-BootManagerHash $mount.EspRoot) -cne $evidence.currentBootManagerSha256){throw 'Windows Boot Manager changed; ESP restoration stopped.'}
    foreach($leaf in @('HypersinvSmbiosPatch.txt','HypersinvEfiInventory.txt','EFI\HypersinvShimInventory\chain-status.txt','EFI\HypersinvShimInventory\chain-diagnostic.txt')){
        $live=Join-Path $mount.EspRoot $leaf;$copy=Join-Path (Join-Path $OutputDirectory 'ESP-before-cleanup') $leaf
        if(Test-Path -LiteralPath $live){if((Get-Sha256 $live) -cne (Get-Sha256 $copy)){throw 'Owned output changed after snapshot; preserved.'};Remove-Item -LiteralPath $live -Force}
    }
    $restoreSources=Get-ExistingShimStageSources $saved
    foreach($file in $restoreSources.Files){$file.Path=Join-Path $saved $file.Name}
    $restoreSources.CertificateThumbprint=$script:ShimCertThumbprint
    function Get-ShimStageSources {return $restoreSources}
    Stage-ShimChainToRoot $mount.EspRoot $mount.VolumeGuid ''
    $restoredStatus=Get-MountedShimStageStatus $mount.EspRoot -AllowArtifacts
    Assert-ShimReceipt (Read-ShimReceipt) $restoredStatus $mount.VolumeGuid ''
    if((Get-BootManagerHash $mount.EspRoot) -cne $evidence.currentBootManagerSha256){throw 'Windows Boot Manager changed during restoration; stop before fresh preparation.'}
}finally{Dismount-Esp $mount;$mount=$null}
Assert-VariantBaseline ''
if((Get-Sha256 $statePath) -cne $stateHash){throw 'Interrupted journal changed during recovery; preserved.'}
$archive=Join-Path $OutputDirectory 'interrupted-prepared.archived.json'
Assert-NoReparseAncestors $statePath $package
Assert-NoReparseAncestors $archive $package
[IO.File]::Move($statePath,$archive)
Write-DriftJson ([ordered]@{result='ORIGINAL_STAGE_RESTORED_WINDOWS_UPDATE_RETAINED';oldSnapshot=$state.snapshot;oldJournal=$archive;windowsBootManagerSha256=$evidence.currentBootManagerSha256;dbxSha256=$evidence.currentDbxSha256;allSevenLiveProven=$false;trialResult='INCONCLUSIVE_WINDOWS_UPDATE_RESTART_BEFORE_READBACK';ownedBootEntryRemoved='Boot0001';bootNextAbsent=$true;secureBoot=$true;armRequested=$false;rebootRequested=$false}) 'cleanup-result.json'
$prepareLog=Join-Path $OutputDirectory 'fresh-prepare.log'
$prior=$ErrorActionPreference
try{
    $ErrorActionPreference='Continue'
    & (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File (Join-Path $package 'commands\smbios-seven-probe.ps1') Prepare *>&1|ForEach-Object{Add-Content -LiteralPath $prepareLog -Value ([string]$_)}
    $exit=$LASTEXITCODE
}finally{$ErrorActionPreference=$prior}
if($exit -ne 0){throw ('Cleanup succeeded; fresh preparation failed. See '+$prepareLog)}
$fresh=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
if(-not $fresh.readyToArm -or $fresh.armed -or $fresh.rebooted){throw 'Fresh journal is not unarmed and ready.'}
$freshSnapshot=Get-Content -LiteralPath (Join-Path $fresh.snapshot 'snapshot.json') -Raw|ConvertFrom-Json
if($freshSnapshot.bootManagerSha256 -cne $evidence.currentBootManagerSha256){throw 'Windows Boot Manager changed during fresh preparation; do not arm.'}
Assert-VariantBaseline ''
Write-DriftJson ([ordered]@{result='READY_TO_ARM';newSnapshot=$fresh.snapshot;newJournal=$statePath;oldJournal=$archive;originalStageRestoredBeforeFreshSnapshot=$true;currentWindowsBootManagerRetained=$true;currentDbxRetained=$true;readyToArm=$true;armed=$false;rebooted=$false;allSevenLiveProven=$false;completedUtc=[DateTime]::UtcNow.ToString('o')}) 'recovery-result.json'
'WINDOWS UPDATE RECOVERY COMPLETE; RETRY READY. No Arm or reboot. '+$fresh.snapshot
