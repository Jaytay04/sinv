[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Prepare','EnableTestMode','InstallTransparent','VerifyTransparent','InstallReplacement','VerifyReplacement','Remove','VerifyOriginal','RecoverOriginal','RestorePolicy','FinalVerify','Status')][string]$Action,
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [string]$ReleaseDirectory,
    [string]$RunDirectory,
    [string]$RecoveryDirectory
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot 'model.psm1') -Force
. (Join-Path $PSScriptRoot 'read.ps1')

function Write-StorageHostReleaseEvidence {
    param($Release)
    $target=Join-Path $script:StorageHostActionDirectory 'release-verification.json'
    $evidence=[ordered]@{verifiedUtc=[DateTime]::UtcNow.ToString('o');manifestPath=$Release.manifestPath;manifestSha256=$Release.manifestSha256;certificatePath=$Release.certificatePath;thumbprint=$script:StorageHostCertThumb;packages=[ordered]@{}}
    foreach($name in @('transparent','replacement')){
        $path=$Release.packagePaths[$name]
        $evidence.packages[$name]=[ordered]@{directory=$path;files=@(foreach($file in $Release.manifest.packages.$name.files){[ordered]@{name=$file.name;sha256=$file.sha256}});signature='pinned PE image digest, embedded PKCS#7 signer and catalog signature verified'}
    }
    Write-StorageHostAtomicJson -Path $target -Value $evidence
}
function Test-StorageHostRecoveryPreparation {
    param([string]$RequestedPath,[string]$RunId)
    if ([string]::IsNullOrWhiteSpace($RequestedPath)) { throw 'Prepare requires -RecoveryDirectory on a different physical disk.' }
    $resolved=Assert-StorageHostExternalRecoveryTarget -Path $RequestedPath
    $runRecovery=Join-Path $resolved.path ('storage-host-recovery-'+$RunId)
    if (Test-Path -LiteralPath $runRecovery) { throw "Refusing to overwrite recovery directory: $runRecovery" }
    [void][IO.Directory]::CreateDirectory($runRecovery)
    return [pscustomobject]@{path=$runRecovery;target=$resolved}
}
function Copy-StorageHostRecoveryKit {
    param([string]$SnapshotDirectory,[string]$KitDirectory,[string]$Destination)
    [void][IO.Directory]::CreateDirectory($Destination)
    $files=[Collections.Generic.List[object]]::new()
    foreach($sourceRoot in @($SnapshotDirectory,$KitDirectory)){
        foreach($file in @(Get-ChildItem -LiteralPath $sourceRoot -File -Recurse)){
            $relative=$file.FullName.Substring($script:StorageHostRun.Length).TrimStart('\')
            $target=Join-Path $Destination $relative
            [void][IO.Directory]::CreateDirectory((Split-Path -Parent $target))
            Copy-Item -LiteralPath $file.FullName -Destination $target
            $expected=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
            $actual=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
            if($expected -cne $actual){throw "Recovery kit copy hash mismatch: $relative expected=$expected actual=$actual"}
            $files.Add([ordered]@{relativePath=$relative;sha256=$expected;bytes=[long]$file.Length})
        }
    }
    $manifest=[ordered]@{schema='storage-host-recovery-copy.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');destination=$Destination;files=@($files.ToArray())}
    $manifestPath=Join-Path $script:StorageHostRun 'recovery-copy-manifest.json'
    Write-StorageHostAtomicJson -Path $manifestPath -Value $manifest
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $Destination 'recovery-copy-manifest.json')
    $localHash=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    $externalHash=(Get-FileHash -LiteralPath (Join-Path $Destination 'recovery-copy-manifest.json') -Algorithm SHA256).Hash
    if($localHash -cne $externalHash){throw 'External recovery manifest hash differs from the local copy.'}
    return [pscustomobject]@{manifestPath=$manifestPath;manifestSha256=$localHash;fileCount=$files.Count;destination=$Destination}
}
function New-StorageHostSnapshot {
    param($Release,[string]$RecoveryPath,[string]$RunId,$RecoveryTarget)
    $snapshot=Join-Path $script:StorageHostRun 'snapshot'
    [void][IO.Directory]::CreateDirectory($snapshot)
    $inventoryResult=Write-StorageHostInventory -EvidenceDirectory $script:StorageHostActionDirectory -Name 'prepare-inventory.json'
    $inventory=$inventoryResult.inventory
    Assert-StorageHostBaselineIdentity -Inventory $inventory -RequireNoFilter -RequireOriginal
    if (-not $inventory.disk.isBoot -or -not $inventory.disk.isSystem -or [int]$inventory.disk.number -ne 0) {
        throw ('Pinned PHYSICALDRIVE0 must be the active boot/system disk; actual number='+$inventory.disk.number+' isBoot='+$inventory.disk.isBoot+' isSystem='+$inventory.disk.isSystem)
    }
    if ($null -eq $inventory.policy.secureBoot) { throw ('Secure Boot mode could not be read: '+$inventory.policy.secureBootReadError) }
    if (-not [bool]$inventory.policy.secureBootVariable.supported) { throw ('SecureBoot variable bytes could not be read: Win32Error='+$inventory.policy.secureBootVariable.win32Error+' '+$inventory.policy.secureBootVariable.win32ErrorText+'; privilegeError='+$inventory.policy.secureBootVariable.privilegeError) }
    if ([long]$inventory.policy.secureBootVariable.size -ne 1 -or [string]$inventory.policy.secureBootVariable.value -notin @('00','01')) { throw ('SecureBoot variable expected one byte 00 or 01; actual size='+$inventory.policy.secureBootVariable.size+' value='+$inventory.policy.secureBootVariable.value+' bytes='+$inventory.policy.secureBootVariable.bytesHex) }
    $expectedSecureBootValue=if([bool]$inventory.policy.secureBoot){'01'}else{'00'}
    if([string]$inventory.policy.secureBootVariable.value -cne $expectedSecureBootValue){throw ('SecureBoot variable/mode mismatch: Confirm-SecureBootUEFI='+$inventory.policy.secureBoot+' expected variable='+$expectedSecureBootValue+' actual='+$inventory.policy.secureBootVariable.value)}
    if (-not $inventory.policy.codeIntegrity.success) { throw ('NtQuerySystemInformation(SystemCodeIntegrityInformation) failed: '+$inventory.policy.codeIntegrity.ntStatus) }
    if ($inventory.policy.bcdCurrentExitCode -ne 0) { throw ('bcdedit current enumeration failed: '+($inventory.policy.bcdCurrentLines -join ' | ')) }
    $baselineTrust=Get-StorageHostCertificateMembership -Thumbprint $script:StorageHostCertThumb
    $baseline=[ordered]@{
        capturedUtc=[DateTime]::UtcNow.ToString('o')
        inventory=$inventory
        inventoryPath=$inventoryResult.path
        inventorySha256=$inventoryResult.sha256
        trustStores=$baselineTrust
        windowsDiskNumbers=@(Get-StorageHostWindowsDiskNumbers)
    }
    $bcdCurrent=Get-StorageHostBcdCommand -Arguments @('/enum','{current}','/v') -OutputPath (Join-Path $snapshot 'bcd-current.txt')
    $bcdAll=Get-StorageHostBcdCommand -Arguments @('/enum','all','/v') -OutputPath (Join-Path $snapshot 'bcd-all.txt')
    $bcdFirmware=Get-StorageHostBcdCommand -Arguments @('/enum','firmware','/v') -OutputPath (Join-Path $snapshot 'bcd-firmware.txt')
    $bcdExport=Join-Path $snapshot 'boot-store-export.bcd'
    $export=Invoke-StorageHostCapture -Program (Join-Path $env:SystemRoot 'System32\bcdedit.exe') -Arguments @('/export',$bcdExport)
    if($export.exitCode -ne 0 -or -not(Test-Path -LiteralPath $bcdExport -PathType Leaf)){
        [IO.File]::WriteAllLines((Join-Path $snapshot 'bcd-export-output.txt'),[string[]]$export.output,$script:StorageHostUtf8)
        throw ('bcdedit /export failed exit='+$export.exitCode+': '+($export.output -join ' | '))
    }
    [IO.File]::WriteAllLines((Join-Path $snapshot 'bcd-export-output.txt'),[string[]]$export.output,$script:StorageHostUtf8)
    $diskSnapshot=Get-StorageHostDiskSnapshot
    Write-StorageHostAtomicJson -Path (Join-Path $snapshot 'disk-layout.json') -Value $diskSnapshot
    $reagent=Get-StorageHostCommandStatus -Program (Join-Path $env:SystemRoot 'System32\reagentc.exe') -Arguments @('/info')
    Write-StorageHostAtomicJson -Path (Join-Path $snapshot 'reagentc-status.json') -Value $reagent
    $bitlocker=Get-StorageHostCommandStatus -Program (Join-Path $env:SystemRoot 'System32\manage-bde.exe') -Arguments @('-status')
    Write-StorageHostAtomicJson -Path (Join-Path $snapshot 'bitlocker-status-only.json') -Value $bitlocker
    $releaseCopy=Join-Path $snapshot 'release-manifest.json'
    Copy-Item -LiteralPath $Release.manifestPath -Destination $releaseCopy
    $certCopy=Join-Path $snapshot 'hypersinv.cer'
    Copy-Item -LiteralPath $Release.certificatePath -Destination $certCopy
    if((Get-FileHash -LiteralPath $certCopy -Algorithm SHA256).Hash -cne $script:StorageHostCertSha){throw 'Pinned certificate changed while preparing the snapshot.'}
    Write-StorageHostAtomicJson -Path (Join-Path $snapshot 'baseline.json') -Value $baseline

    $kit=Join-Path $script:StorageHostRun 'recovery-kit'
    [void][IO.Directory]::CreateDirectory($kit)
    foreach($name in @('transparent','replacement')) {
        $kitPackage=Join-Path $kit $name
        [void][IO.Directory]::CreateDirectory($kitPackage)
        Copy-Item -LiteralPath (Join-Path $Release.packagePaths[$name] 'client_storage_filter.inf') -Destination (Join-Path $kitPackage 'client_storage_filter.inf')
    }
    Copy-Item -LiteralPath $script:StorageHostRecoveryCmd -Destination (Join-Path $kit 'offline-recover.cmd')
    $recoveryGuide=Join-Path $script:StorageHostRepo 'spoof-handoff\docs\STORAGE_BOOT_RECOVERY.md'
    $runbook=Join-Path $script:StorageHostRepo 'spoof-handoff\docs\STORAGE_BAREMETAL_RUNBOOK.md'
    foreach($document in @(@{source=$recoveryGuide;name='STORAGE_BOOT_RECOVERY.md'},@{source=$runbook;name='STORAGE_BAREMETAL_RUNBOOK.md'})){
        if(-not(Test-Path -LiteralPath $document.source -PathType Leaf)){throw ('Required recovery document is missing before mutation: '+$document.source)}
        Copy-Item -LiteralPath $document.source -Destination (Join-Path $kit $document.name)
    }
    $kitReadme=@"
This kit contains the exact two release INF files, the run snapshot, the signed-release manifest, the boot recovery guide, the bare-metal runbook, and offline-recover.cmd.
In WinRE, identify the offline Windows volume and invoke offline-recover.cmd with its drive letter (for example D:).
The script requires typing REMOVE, compares each offline oem*.inf byte-for-byte against these two INFs, then asks DISM to remove only exact matches.
If no exact INF is found, DISM is unavailable, or any comparison/removal errors, stop and use the documented System Restore or full image restore procedure.
This kit is a recovery aid and is not a disk image or a guarantee that an offline driver removal alone will repair every startup failure.
"@
    [IO.File]::WriteAllText((Join-Path $kit 'README.txt'),$kitReadme,$script:StorageHostUtf8)
    $copy=Copy-StorageHostRecoveryKit -SnapshotDirectory $snapshot -KitDirectory $kit -Destination $RecoveryPath
    $baselineFileHash=(Get-FileHash -LiteralPath (Join-Path $snapshot 'baseline.json') -Algorithm SHA256).Hash
    return [pscustomobject]@{
        snapshotDirectory=$snapshot
        baseline=$baseline
        recoveryDirectory=$RecoveryPath
        recoveryManifestSha256=$copy.manifestSha256
        recoveryFileCount=$copy.fileCount
        recoveryTarget=$RecoveryTarget
        baselineFileSha256=$baselineFileHash
    }
}

function Invoke-StorageHostPrepare {
    if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) { throw 'Prepare requires -ReleaseDirectory pointing to the signed storage-host release.' }
    if (Test-Path -LiteralPath $script:StorageHostPointer -PathType Leaf) {
        $previous=Get-Content -LiteralPath $script:StorageHostPointer -Raw|ConvertFrom-Json
        $previousPath=[IO.Path]::GetFullPath((Join-Path $script:StorageHostReports ([string]$previous.runDirectory)))
        $previousStatePath=Join-Path $previousPath 'state.json'
        if (Test-Path -LiteralPath $previousStatePath -PathType Leaf) {
            $previousState=Get-Content -LiteralPath $previousStatePath -Raw|ConvertFrom-Json
            if ([string]$previousState.phase -cne 'Complete') { throw ('Previous run has not reached FinalVerify (phase='+$previousState.phase+'). Resume or use its recovery kit before preparing another run.') }
        }
    }
    $script:StorageHostRelease=Get-StorageHostRelease -Directory $ReleaseDirectory
    $runId=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N')
    $recovery=Test-StorageHostRecoveryPreparation -RequestedPath $RecoveryDirectory -RunId $runId
    $script:StorageHostRun=Join-Path $script:StorageHostReports $runId
    if(Test-Path -LiteralPath $script:StorageHostRun){throw 'Unique run path unexpectedly already exists.'}
    [void][IO.Directory]::CreateDirectory($script:StorageHostRun)
    $script:StorageHostActionDirectory=Join-Path $script:StorageHostRun ('actions\prepare.'+$runId)
    [void][IO.Directory]::CreateDirectory($script:StorageHostActionDirectory)
    Write-StorageHostReleaseEvidence -Release $script:StorageHostRelease
    $prepared=New-StorageHostSnapshot -Release $script:StorageHostRelease -RecoveryPath $recovery.path -RunId $runId -RecoveryTarget $recovery.target
    $manifest=(Get-Content -LiteralPath (Join-Path $script:StorageHostRun 'recovery-copy-manifest.json') -Raw|ConvertFrom-Json)
    $script:StorageHostState=[ordered]@{
        schema='storage-host-run.v1';runId=$runId;createdUtc=[DateTime]::UtcNow.ToString('o');updatedUtc=[DateTime]::UtcNow.ToString('o')
        phase='Prepared';lastAction='Prepare';requiresRecovery=$false
        repositoryRoot=$script:StorageHostRepo;releaseDirectory=$script:StorageHostRelease.directory;releaseManifestSha256=$script:StorageHostRelease.manifestSha256
        snapshotDirectory=$prepared.snapshotDirectory;snapshotBaselineSha256=$prepared.baselineFileSha256
        recoveryDirectory=$prepared.recoveryDirectory;recoveryManifestSha256=$prepared.recoveryManifestSha256
        baseline=$prepared.baseline;ownedPackages=[ordered]@{transparent=$null;replacement=$null};packageHistory=@();ownedTrustAdds=@();lastFailure=$null;policyEnable=$null;policyRestore=$null;pendingOperation=$null;lastObservedBootTime=$null;verifiedPackageMode=$null;verifiedTransparentProof=$null;verifiedReplacementProof=$null;recoveryOutcome=$null;recoveryEvidenceDegraded=$null;finalVerificationPath=$null;finalVerificationSha256=$null
    }
    Save-StorageHostState
    Add-StorageHostEvent -Kind 'prepared' -Message 'Fresh host, BCD, disk, policy and release snapshot captured; no driver or policy was changed.' -Data @{releaseManifestSha256=$script:StorageHostRelease.manifestSha256;recoveryDirectory=$prepared.recoveryDirectory;recoveryFileCount=$prepared.recoveryFileCount}
    [ordered]@{status='PREPARED';runDirectory=$script:StorageHostRun;recoveryDirectory=$prepared.recoveryDirectory;recoveryManifestSha256=$prepared.recoveryManifestSha256;secureBoot=$prepared.baseline.inventory.policy.secureBoot;testSigning=$prepared.baseline.inventory.policy.testSigning;disk0PnpInstanceId=$prepared.baseline.inventory.disk.pnpDeviceId;disk0Serial=$prepared.baseline.inventory.disk.serial;driverInstalled=$false;policyChanged=$false}
}

function Get-StorageHostStoreObject {
    param([string]$Name)
    return [Security.Cryptography.X509Certificates.X509Store]::new($Name,'LocalMachine')
}
function Invoke-StorageHostBcdMutation {
    param([string[]]$Arguments,[string]$LogName,[int[]]$AcceptedExitCodes=@(0))
    $capture=Invoke-StorageHostCapture -Program (Join-Path $env:SystemRoot 'System32\bcdedit.exe') -Arguments $Arguments
    Write-StorageHostAtomicJson -Path (Join-Path $script:StorageHostActionDirectory $LogName) -Value ([ordered]@{program='bcdedit.exe';arguments=$Arguments;exitCode=$capture.exitCode;output=$capture.output;capturedUtc=[DateTime]::UtcNow.ToString('o')})
    if($AcceptedExitCodes -notcontains $capture.exitCode){throw ('bcdedit '+($Arguments -join ' ')+' exited '+$capture.exitCode+': '+($capture.output -join ' | '))}
    return $capture
}
function Assert-StorageHostPolicyEnabled {
    param($Inventory)
    if ($null -eq $Inventory.policy.secureBoot -or [bool]$Inventory.policy.secureBoot) { throw ('Secure Boot must be OFF before enabling a test-signed driver; actual='+$Inventory.policy.secureBoot+' detail='+$Inventory.policy.secureBootReadError) }
    if (-not $Inventory.policy.testSigning.exists -or [string]$Inventory.policy.testSigning.value -cne 'yes') { throw ('BCD testsigning must be present and Yes after the required reboot; actual exists='+$Inventory.policy.testSigning.exists+' value='+$Inventory.policy.testSigning.value) }
    if (-not $Inventory.policy.codeIntegrity.success) { throw ('Effective Code Integrity state unreadable: NTSTATUS='+$Inventory.policy.codeIntegrity.ntStatus) }
    if (([int]$Inventory.policy.codeIntegrity.options -band 0x2) -eq 0) { throw ('Kernel TESTSIGNING bit 0x2 is not set; reboot into the pending policy before package installation. CodeIntegrityOptions=0x'+([int]$Inventory.policy.codeIntegrity.options).ToString('X8')) }
    $trust=Get-StorageHostCertificateMembership -Thumbprint $script:StorageHostCertThumb
    if (-not $trust.Root -or -not $trust.TrustedPublisher) { throw ('Pinned certificate trust required in LocalMachine Root and TrustedPublisher; actual Root='+$trust.Root+' TrustedPublisher='+$trust.TrustedPublisher) }
}
function Invoke-StorageHostEnableTestMode {
    Assert-StorageHostRecoveryCopy
    $baseline=$script:StorageHostState.baseline
    $inventoryResult=Write-StorageHostInventory -EvidenceDirectory $script:StorageHostActionDirectory -Name 'before-enable-inventory.json'
    $inventory=$inventoryResult.inventory
    if ($null -eq $inventory.policy.secureBoot) { throw ('Secure Boot state cannot be verified: '+$inventory.policy.secureBootReadError) }
    if ([bool]$inventory.policy.secureBoot) { throw 'Secure Boot is still ON. Disable Secure Boot manually in firmware, boot Windows, then rerun EnableTestMode.' }
    if ($inventory.disk.pnpDeviceId -cne $baseline.inventory.disk.pnpDeviceId) { throw 'Pinned disk PnP instance changed since Prepare.' }
    if ($inventory.disk.serial -cne $baseline.inventory.disk.serial -or $inventory.disk.wmiSerial -cne $baseline.inventory.disk.wmiSerial) { throw ('Disk serial drift before policy change: native='+$inventory.disk.serial+' WMI='+$inventory.disk.wmiSerial) }
    $script:StorageHostState.ownedTrustAdds=@()
    Set-StorageHostPhase -Phase 'EnablingTestMode' -ActionName $Action -Message 'Persisted intent before importing the pinned public certificate and setting BCD testsigning.'
    $publicCert=[Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes($script:StorageHostRelease.certificatePath))
    foreach($storeName in @('Root','TrustedPublisher')){
        $wasPresent=[bool]$baseline.trustStores.$storeName
        $store=Get-StorageHostStoreObject -Name $storeName
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        try{
            $currentlyPresent=@($store.Certificates|Where-Object {$_.Thumbprint -ceq $script:StorageHostCertThumb}).Count -gt 0
            if(-not $wasPresent -and -not $currentlyPresent){
                $store.Add($publicCert)
                $script:StorageHostState.ownedTrustAdds=@($script:StorageHostState.ownedTrustAdds)+@($storeName)
                $script:StorageHostState.updatedUtc=[DateTime]::UtcNow.ToString('o')
                Save-StorageHostState
                Add-StorageHostEvent -Kind 'trust-import' -Message 'Added the existing public signer certificate to one LocalMachine store.' -Data @{store=$storeName;thumbprint=$script:StorageHostCertThumb}
            } elseif($wasPresent -and -not $currentlyPresent) {
                throw ('Certificate was present in the Prepare snapshot but disappeared from LocalMachine '+$storeName+'; refusing to recreate an externally removed trust entry.')
            }
        } finally { $store.Close() }
    }
    [void](Invoke-StorageHostBcdMutation -Arguments @('/set','{current}','testsigning','on') -LogName 'bcd-test-signing-on.json')
    $afterTrust=Get-StorageHostCertificateMembership -Thumbprint $script:StorageHostCertThumb
    if(-not $afterTrust.Root -or -not $afterTrust.TrustedPublisher){throw ('Certificate import post-check failed: Root='+$afterTrust.Root+' TrustedPublisher='+$afterTrust.TrustedPublisher)}
    $script:StorageHostState.policyEnable=[ordered]@{capturedUtc=[DateTime]::UtcNow.ToString('o');secureBootChangedByScript=$false;bcdElementSet='testsigning=on';trustStoresAfter=$afterTrust;requiresManualReboot=$true}
    Set-StorageHostPhase -Phase 'PolicyPendingReboot' -ActionName $Action -Message 'Test-signing policy is staged. No reboot was issued; the user must reboot before installation.'
    return [ordered]@{status='POLICY_PENDING_REBOOT';runDirectory=$script:StorageHostRun;secureBoot=$inventory.policy.secureBoot;testSigningBcd='on';effectiveKernelTestSigning='not asserted until reboot';ownedTrustAdds=@($script:StorageHostState.ownedTrustAdds);rebootIssued=$false}
}

$script:StorageHostRepo=[IO.Path]::GetFullPath($RepositoryRoot)
$script:StorageHostReports=[IO.Path]::GetFullPath((Join-Path $script:StorageHostRepo 'spoof-handoff\reports\storage-host-runs'))
$script:StorageHostPointer=[IO.Path]::GetFullPath((Join-Path $script:StorageHostRepo 'spoof-handoff\reports\storage-host-prep-20261017\last-run.json'))
$script:StorageHostRecoveryCmd=Join-Path $PSScriptRoot 'offline-recover.cmd'
$script:StorageHostCertThumb='59DBD6780CFE60C020B3F4511211FEC22C397B85'
$script:StorageHostCertSha='B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'
$script:StorageHostSystemDisks=@()
$script:StorageHostRelease=$null
$script:StorageHostRun=$null
$script:StorageHostActionDirectory=$null
$script:StorageHostState=$null
$script:StorageHostUtf8=[Text.UTF8Encoding]::new($false)

function Assert-StorageHostWindows {
    if ($env:OS -cne 'Windows_NT') { throw 'This workflow supports Windows only.' }
}
function Test-StorageHostAdmin {
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    $principal=[Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Assert-StorageHostAdmin {
    if (-not (Test-StorageHostAdmin)) { throw 'This action requires an elevated PowerShell session.' }
}
function Assert-StorageHostInside {
    param([string]$Path,[string]$Root,[string]$Label)
    $full=[IO.Path]::GetFullPath($Path)
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
    if (-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw "$Label must remain under $Root : $full" }
    return $full
}
function Write-StorageHostAtomicJson {
    param([string]$Path,$Value)
    $temporary=$Path+'.tmp.'+[Guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllText($temporary,(ConvertTo-Json -InputObject $Value -Depth 20),$script:StorageHostUtf8)
    if (Test-Path -LiteralPath $Path) {
        $backup=$Path+'.backup.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N')
        [IO.File]::Replace($temporary,$Path,$backup,$true)
    } else {
        [IO.File]::Move($temporary,$Path)
    }
}
function Add-StorageHostEvent {
    param([string]$Kind,[string]$Message,[hashtable]$Data=@{})
    $event=[ordered]@{capturedUtc=[DateTime]::UtcNow.ToString('o');kind=$Kind;message=$Message;data=$Data}
    [IO.File]::AppendAllText((Join-Path $script:StorageHostRun 'journal.jsonl'),((ConvertTo-Json -InputObject $event -Compress -Depth 8)+[Environment]::NewLine),$script:StorageHostUtf8)
}
function Save-StorageHostState {
    Write-StorageHostAtomicJson -Path (Join-Path $script:StorageHostRun 'state.json') -Value $script:StorageHostState
}
function Set-StorageHostProperty {
    param($Object,[string]$Name,$Value)
    if ($Object -is [System.Collections.IDictionary]) { $Object[$Name]=$Value; return }
    $property=$Object.PSObject.Properties[$Name]
    if ($null -eq $property) { $Object | Add-Member -MemberType NoteProperty -Name $Name -Value $Value }
    else { $Object.$Name=$Value }
}
function Set-StorageHostPhase {
    param([string]$Phase,[string]$ActionName,[string]$Message)
    $script:StorageHostState.phase=$Phase
    $script:StorageHostState.lastAction=$ActionName
    $script:StorageHostState.updatedUtc=[DateTime]::UtcNow.ToString('o')
    Save-StorageHostState
    Add-StorageHostEvent -Kind 'phase' -Message $Message -Data @{phase=$Phase;action=$ActionName}
}
function Set-StorageHostPointer {
    $pointerDirectory=Split-Path -Parent $script:StorageHostPointer
    [void][IO.Directory]::CreateDirectory($pointerDirectory)
    $relative=$script:StorageHostRun.Substring($script:StorageHostReports.TrimEnd('\').Length).TrimStart('\')
    Write-StorageHostAtomicJson -Path $script:StorageHostPointer -Value ([ordered]@{schema='storage-host-last-run.v1';runDirectory=$relative;runId=(Split-Path -Leaf $script:StorageHostRun);updatedUtc=[DateTime]::UtcNow.ToString('o')})
}
function New-StorageHostActionDirectory {
    $id=$Action.ToLowerInvariant()+'.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N')
    $path=Join-Path $script:StorageHostRun ('actions\'+$id)
    [void][IO.Directory]::CreateDirectory($path)
    return $path
}
function Get-StorageHostRunDirectory {
    if (-not [string]::IsNullOrWhiteSpace($RunDirectory)) {
        $candidate=Assert-StorageHostInside -Path $RunDirectory -Root $script:StorageHostReports -Label 'RunDirectory'
    } else {
        if (-not (Test-Path -LiteralPath $script:StorageHostPointer -PathType Leaf)) { throw 'No prepared storage-host run exists. Run Prepare first.' }
        $pointer=Get-Content -LiteralPath $script:StorageHostPointer -Raw|ConvertFrom-Json
        if ($pointer.schema -cne 'storage-host-last-run.v1' -or [string]::IsNullOrWhiteSpace([string]$pointer.runDirectory)) { throw 'last-run pointer schema is invalid.' }
        $candidate=[IO.Path]::GetFullPath((Join-Path $script:StorageHostReports ([string]$pointer.runDirectory)))
        $candidate=Assert-StorageHostInside -Path $candidate -Root $script:StorageHostReports -Label 'last-run pointer'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $candidate 'state.json') -PathType Leaf)) { throw "Run state is missing: $candidate" }
    $script:StorageHostRun=$candidate
    $script:StorageHostState=Get-Content -LiteralPath (Join-Path $candidate 'state.json') -Raw|ConvertFrom-Json
    $script:StorageHostActionDirectory=New-StorageHostActionDirectory
    return $candidate
}
function Get-StorageHostRelease {
    param([Parameter(Mandatory=$true)][string]$Directory)
    $full=Assert-StorageHostInside -Path $Directory -Root (Join-Path $script:StorageHostRepo 'spoof-handoff\artifacts') -Label 'ReleaseDirectory'
    $manifestPath=Join-Path $full 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Release manifest missing: $manifestPath" }
    $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    [void](Assert-StorageHostManifest -Manifest $manifest)
    $manifestHash=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    [void](Assert-StorageHostReceipt -Receipt $manifest.signatureReceipt)
    $receiptPath=Join-Path $full ([string]$manifest.signatureReceipt.path)
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf) -or (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash -cne $manifest.signatureReceipt.sha256) { throw 'Release signature receipt is absent or its SHA-256 differs.' }
    $certPath=Join-Path $full ([string]$manifest.certificateFile)
    if (-not (Test-Path -LiteralPath $certPath -PathType Leaf) -or (Get-FileHash -LiteralPath $certPath -Algorithm SHA256).Hash -cne $script:StorageHostCertSha) { throw 'Release certificate file is absent or its SHA-256 differs.' }
    $cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes($certPath))
    if ($cert.Thumbprint -cne $script:StorageHostCertThumb) { throw 'Release certificate thumbprint differs from the pinned certificate.' }
    Import-Module (Join-Path $PSScriptRoot 'signatures.psm1') -Force
    $packagePaths=[ordered]@{}
    foreach($packageName in @('transparent','replacement')){
        $package=$manifest.packages.$packageName
        $relative=[string]$package.directory
        if ([IO.Path]::IsPathRooted($relative) -or $relative -match '(^|[\\/])\.\.([\\/]|$)') { throw "Package $packageName directory is not a safe relative path." }
        $packagePath=Assert-StorageHostInside -Path (Join-Path $full $relative) -Root $full -Label "Package $packageName"
        foreach($file in $package.files){
            $filePath=Join-Path $packagePath ([string]$file.name)
            if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) { throw "Release file missing: $filePath" }
            $actual=(Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash
            if ($actual -cne [string]$file.sha256) { throw "Release hash mismatch for $packageName/$($file.name): expected=$($file.sha256) actual=$actual" }
        }
        $infPath=Join-Path $packagePath 'client_storage_filter.inf'
        $infText=[IO.File]::ReadAllText($infPath)
        if ($infText -notmatch '(?im)^Class=Extension\s*$' -or $infText -notmatch '(?im)^StartType=0\s*$' -or $infText -notmatch '(?im)^AddFilter=client_storage_filter,,StorageFilter_Upper\s*$') {
            throw "INF $packageName must remain a StartType 0 extension filter package."
        }
        if ($infText.IndexOf([string]$manifest.hardwareId,[StringComparison]::OrdinalIgnoreCase) -lt 0) { throw "INF $packageName hardware ID differs from the pinned physical target." }
        $extensionMatch=[regex]::Match($infText,'(?im)^ExtensionId=\{([0-9A-F-]{36})\}\s*$')
        if (-not $extensionMatch.Success -or ('{'+$extensionMatch.Groups[1].Value+'}') -ine [string]$manifest.extensionId) { throw "INF $packageName extension ID differs from its release manifest." }
        $packagePaths[$packageName]=$packagePath
    }
    foreach($packageName in @('transparent','replacement')){
        $signatureEvidence=@(Test-StorageHostSignatures -PackageDirectory $packagePaths[$packageName] -CertificateFile $certPath)
        if ($signatureEvidence.Count -ne 1 -or -not $signatureEvidence[0].driver.imageDigestMatch -or -not $signatureEvidence[0].driver.pinnedCertificateSignatureValid -or -not $signatureEvidence[0].catalog.pinnedCertificateSignatureValid) {
            throw "Pinned native image and catalog signature validation failed for $packageName."
        }
    }
    return [pscustomobject]@{directory=$full;manifest=$manifest;manifestPath=$manifestPath;manifestSha256=$manifestHash;certificatePath=$certPath;packagePaths=$packagePaths}
}
function Get-StorageHostDriveDisks {
    param([Parameter(Mandatory=$true)][string]$DriveLetter)
    $partition=@(Get-Partition -DriveLetter $DriveLetter.TrimEnd(':') -ErrorAction Stop)
    return @($partition | ForEach-Object { [int]$_.DiskNumber } | Sort-Object -Unique)
}
function Get-StorageHostWindowsDiskNumbers {
    $numbers=[Collections.Generic.List[int]]::new()
    foreach($disk in @(Get-Disk | Where-Object { $_.IsSystem -or $_.IsBoot })) { $numbers.Add([int]$disk.Number) }
    $osLetter=$env:SystemDrive.TrimEnd(':')
    foreach($number in @(Get-StorageHostDriveDisks -DriveLetter $osLetter)){if(-not $numbers.Contains([int]$number)){$numbers.Add([int]$number)}}
    return @($numbers.ToArray()|Sort-Object -Unique)
}
function Assert-StorageHostExternalRecoveryTarget {
    param([Parameter(Mandatory=$true)][string]$Path,[int]$MinimumFreeBytes=104857600)
    $full=[IO.Path]::GetFullPath($Path)
    $root=[IO.Path]::GetPathRoot($full)
    if ($root -notmatch '^([A-Za-z]):\\$') { throw 'RecoveryDirectory must be on a local volume with a drive letter; network and mounted-folder paths are not accepted.' }
    $driveLetter=$Matches[1]
    $candidateDisks=Get-StorageHostDriveDisks -DriveLetter $driveLetter
    $windowsDisks=Get-StorageHostWindowsDiskNumbers
    if (-not (Test-StorageHostRecoveryDisk -RecoveryDiskNumbers $candidateDisks -WindowsDiskNumbers $windowsDisks)) {
        throw ('RecoveryDirectory must be on a different physical disk than Windows; recovery disk(s)='+($candidateDisks -join ',')+' Windows disk(s)='+($windowsDisks -join ','))
    }
    $volume=Get-Volume -DriveLetter $driveLetter -ErrorAction Stop
    if ([long]$volume.SizeRemaining -lt $MinimumFreeBytes) { throw ('External recovery volume has less than 100 MiB free: '+$volume.SizeRemaining) }
    return [pscustomobject]@{path=$full;driveLetter=$driveLetter;diskNumbers=$candidateDisks;windowsDiskNumbers=$windowsDisks;volume=[ordered]@{fileSystem=[string]$volume.FileSystem;label=[string]$volume.FileSystemLabel;size=[long]$volume.Size;sizeRemaining=[long]$volume.SizeRemaining;driveType=[string](Get-Volume -DriveLetter $driveLetter).DriveType}}
}
function Get-StorageHostBcdCommand {
    param([string[]]$Arguments,[string]$OutputPath)
    $capture=Invoke-StorageHostCapture -Program (Join-Path $env:SystemRoot 'System32\\bcdedit.exe') -Arguments $Arguments
    [IO.File]::WriteAllLines($OutputPath,[string[]]$capture.output,$script:StorageHostUtf8)
    if ($capture.exitCode -ne 0) { throw ('bcdedit '+($Arguments -join ' ')+' exited '+$capture.exitCode+': '+($capture.output -join ' | ')) }
    return $capture
}
function Get-StorageHostDiskSnapshot {
    $disks=@(Get-Disk | Select-Object Number,FriendlyName,SerialNumber,UniqueId,PartitionStyle,BusType,OperationalStatus,HealthStatus,IsBoot,IsSystem,IsReadOnly,IsOffline,Size)
    $partitions=@(Get-Partition | Select-Object DiskNumber,PartitionNumber,DriveLetter,Type,GptType,Guid,Offset,Size,IsBoot,IsSystem,IsReadOnly,IsHidden)
    $volumes=@(Get-Volume | Select-Object DriveLetter,FileSystemLabel,FileSystem,DriveType,HealthStatus,OperationalStatus,Size,SizeRemaining)
    return [ordered]@{capturedUtc=[DateTime]::UtcNow.ToString('o');disks=$disks;partitions=$partitions;volumes=$volumes}
}
function Get-StorageHostCommandStatus {
    param([string]$Program,[string[]]$Arguments)
    $capture=Invoke-StorageHostCapture -Program $Program -Arguments $Arguments
    return [ordered]@{program=$Program;arguments=$Arguments;exitCode=$capture.exitCode;output=$capture.output}
}
function Get-StorageHostCommandLine {
    param([string]$Program,[string[]]$Arguments,[string]$Path)
    $capture=Invoke-StorageHostCapture -Program $Program -Arguments $Arguments
    [IO.File]::WriteAllLines($Path,[string[]]$capture.output,$script:StorageHostUtf8)
    return $capture
}
function Test-StorageHostTrustStore {
    param([string]$Thumbprint,[string]$StoreName)
    $store=[Security.Cryptography.X509Certificates.X509Store]::new($StoreName,'LocalMachine')
    $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
    try { return (@($store.Certificates|Where-Object {$_.Thumbprint -ceq $Thumbprint}).Count -gt 0) }
    finally { $store.Close() }
}
function Get-StorageHostOwnedInfCandidates {
    param([string]$ExpectedSha256)
    $infRoot=Join-Path $env:SystemRoot 'INF'
    $matches=[Collections.Generic.List[object]]::new()
    foreach($candidate in @(Get-ChildItem -LiteralPath $infRoot -Filter 'oem*.inf' -File -ErrorAction Stop)){
        $hash=(Get-FileHash -LiteralPath $candidate.FullName -Algorithm SHA256).Hash
        if($hash -ceq $ExpectedSha256){$matches.Add([pscustomobject]@{publishedName=$candidate.Name;path=$candidate.FullName;sha256=$hash})}
    }
    return @($matches.ToArray())
}
function Assert-StorageHostBaselineIdentity {
    param($Inventory,[switch]$RequireNoFilter,[switch]$RequireOriginal)
    if ([string]$Inventory.disk.pnpDeviceId -cne [string]$script:StorageHostRelease.manifest.pnpInstanceId) { throw ('Pinned PnP instance mismatch: expected='+$script:StorageHostRelease.manifest.pnpInstanceId+' actual='+$Inventory.disk.pnpDeviceId) }
    if (@($Inventory.disk.hardwareIds|Where-Object { $_ -ieq [string]$script:StorageHostRelease.manifest.hardwareId }).Count -ne 1) { throw ('Pinned hardware ID missing: '+$script:StorageHostRelease.manifest.hardwareId) }
    if ($RequireOriginal) { [void](Assert-StorageHostExpectedSerial -Mode Original -NativeSerial ([string]$Inventory.disk.serial) -WmiSerial ([string]$Inventory.disk.wmiSerial)) }
    if ($RequireNoFilter) {
        if ($Inventory.storageFilter.present -or $Inventory.pnpStack.filterPresent) { throw ('Storage filter must be absent before testing; servicePresent='+$Inventory.storageFilter.present+' actualStackPresent='+$Inventory.pnpStack.filterPresent) }
    }
    if (-not $Inventory.fileIo.match) { throw ('1 MiB file I/O check mismatch: expectedSHA256='+$Inventory.fileIo.expectedSha256+' actualSHA256='+$Inventory.fileIo.actualSha256) }
}
function Assert-StorageHostRecoveryCopy {
    param([switch]$AllowMissingExternal)
    $recoveryRoot=[string]$script:StorageHostState.recoveryDirectory
    $manifestPath=Join-Path $script:StorageHostRun 'recovery-copy-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Local recovery copy manifest is missing.' }
    $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    if ($manifest.schema -cne 'storage-host-recovery-copy.v1') { throw 'Recovery copy manifest schema is invalid.' }
    $localManifestHash=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    if ($localManifestHash -cne [string]$script:StorageHostState.recoveryManifestSha256) { throw ('Local recovery-copy manifest SHA256 changed: expected='+$script:StorageHostState.recoveryManifestSha256+' actual='+$localManifestHash) }
    foreach($file in $manifest.files){
        $source=Join-Path $script:StorageHostRun ([string]$file.relativePath)
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Local recovery evidence file missing: $source" }
        $actual=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        if ($actual -cne [string]$file.sha256) { throw "Local recovery evidence hash mismatch: $source expected=$($file.sha256) actual=$actual" }
    }
    $externalError=$null
    try {
        if (-not (Test-Path -LiteralPath $recoveryRoot -PathType Container)) { throw "External recovery copy is missing: $recoveryRoot" }
        $externalManifestPath=Join-Path $recoveryRoot 'recovery-copy-manifest.json'
        if (-not (Test-Path -LiteralPath $externalManifestPath -PathType Leaf)) { throw 'External recovery-copy manifest is missing.' }
        $externalManifestHash=(Get-FileHash -LiteralPath $externalManifestPath -Algorithm SHA256).Hash
        if ($externalManifestHash -cne $localManifestHash) { throw ('External recovery-copy manifest SHA256 mismatch expected='+$localManifestHash+' actual='+$externalManifestHash) }
        foreach($file in $manifest.files){
            $source=Join-Path $recoveryRoot ([string]$file.relativePath)
            if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "External recovery evidence file missing: $source" }
            $actual=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
            if ($actual -cne [string]$file.sha256) { throw "External recovery evidence hash mismatch: $source expected=$($file.sha256) actual=$actual" }
        }
    } catch { $externalError=$_.Exception.Message }
    if ($externalError) {
        if (-not $AllowMissingExternal) { throw $externalError }
        $script:StorageHostState.recoveryEvidenceDegraded=[ordered]@{capturedUtc=[DateTime]::UtcNow.ToString('o');externalPath=$recoveryRoot;externalValidated=$false;reason=$externalError;localManifestSha256=$localManifestHash}
        Save-StorageHostState
        Add-StorageHostEvent -Kind 'external-recovery-copy-degraded' -Message 'Cleanup continued from hash-validated local state because the external recovery copy was unavailable or did not match.' -Data @{externalPath=$recoveryRoot;reason=$externalError;localManifestSha256=$localManifestHash}
        return [pscustomobject]@{manifest=$manifest;externalValidated=$false;degradedReason=$externalError}
    }
    $script:StorageHostState.recoveryEvidenceDegraded=$null
    return [pscustomobject]@{manifest=$manifest;externalValidated=$true;degradedReason=$null}
}
function Save-StorageHostFailure {
    param([string]$Message)
    if ($null -ne $script:StorageHostState -and $script:StorageHostRun) {
        $script:StorageHostState.lastFailure=[ordered]@{capturedUtc=[DateTime]::UtcNow.ToString('o');action=$Action;message=$Message}
        if ([string]$script:StorageHostState.phase -cne 'Prepared' -and [string]$script:StorageHostState.phase -cne 'Complete') { $script:StorageHostState.requiresRecovery=$true }
        try { Save-StorageHostState } catch {}
        try { Add-StorageHostEvent -Kind 'failure' -Message $Message -Data @{action=$Action} } catch {}
    }
    if ($script:StorageHostActionDirectory) {
        try { [IO.File]::WriteAllText((Join-Path $script:StorageHostActionDirectory 'failure.txt'),$Message+[Environment]::NewLine,$script:StorageHostUtf8) } catch {}
    }
}

function Invoke-StorageHostRestorePolicy {
    Assert-StorageHostRecoveryCopy -AllowMissingExternal | Out-Null
    $release=Get-StorageHostActiveRelease
    $baseline=$script:StorageHostState.baseline
    $before=Write-StorageHostInventory -EvidenceDirectory $script:StorageHostActionDirectory -Name 'before-policy-restore.json'
    $inventory=$before.inventory
    [void](Assert-StorageHostExpectedSerial -Mode Original -NativeSerial ([string]$inventory.disk.serial) -WmiSerial ([string]$inventory.disk.wmiSerial))
    if($inventory.storageFilter.present -or $null -ne $inventory.storageFilter.registry -or $inventory.pnpStack.filterPresent){throw ('Policy restore requires service and registry entry absent and filter absent from stack; servicePresent='+$inventory.storageFilter.present+' registryPresent='+($null -ne $inventory.storageFilter.registry)+' stackPresent='+$inventory.pnpStack.filterPresent)}
    if($inventory.policy.bcdCurrentExitCode -ne 0){throw ('Current BCD enumeration failed before restore: '+($inventory.policy.bcdCurrentLines -join ' | '))}
    $original=$baseline.inventory.policy.testSigning
    $current=$inventory.policy.testSigning
    $bcdChanged=([bool]$original.exists -ne [bool]$current.exists -or ([bool]$original.exists -and [string]$original.value -cne [string]$current.value))
    $currentTrust=Get-StorageHostCertificateMembership -Thumbprint $script:StorageHostCertThumb
    $trustChanged=([bool]$currentTrust.Root -ne [bool]$baseline.trustStores.Root -or [bool]$currentTrust.TrustedPublisher -ne [bool]$baseline.trustStores.TrustedPublisher)
    $script:StorageHostState.pendingOperation=[ordered]@{kind='restore-policy';startedUtc=[DateTime]::UtcNow.ToString('o');bootTimeBefore=$inventory.windows.lastBootUpTime}
    Set-StorageHostPhase -Phase 'RestoringPolicy' -ActionName $Action -Message 'Policy restore intent persisted before restoring only BCD and public trust values that differ from the captured baseline.'
    if($bcdChanged){
        if([bool]$original.exists){
            $setting=if([string]$original.value -ceq 'yes'){'on'}else{'off'}
            [void](Invoke-StorageHostBcdMutation -Arguments @('/set','{current}','testsigning',$setting) -LogName 'bcd-test-signing-restore.json')
        } else {
            [void](Invoke-StorageHostBcdMutation -Arguments @('/deletevalue','{current}','testsigning') -LogName 'bcd-test-signing-delete.json')
        }
    }
    $publicCert=[Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes($script:StorageHostRelease.certificatePath))
    foreach($storeName in @('Root','TrustedPublisher')){
        if(@($script:StorageHostState.ownedTrustAdds|Where-Object {$_ -ceq $storeName}).Count -eq 0){continue}
        if([bool]$baseline.trustStores.$storeName){throw ('Refusing to remove '+$storeName+' membership that existed before Prepare.')}
        $store=Get-StorageHostStoreObject -Name $storeName
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        try{
            $present=@($store.Certificates|Where-Object {$_.Thumbprint -ceq $script:StorageHostCertThumb}).Count -gt 0
            if($present){$store.Remove($publicCert)}
            Add-StorageHostEvent -Kind 'trust-remove' -Message 'Removed only the pinned public certificate membership added by this run.' -Data @{store=$storeName;thumbprint=$script:StorageHostCertThumb;wasPresent=$present}
        } finally { $store.Close() }
    }
    $afterBcd=Invoke-StorageHostCapture -Program (Join-Path $env:SystemRoot 'System32\bcdedit.exe') -Arguments @('/enum','{current}','/v')
    if($afterBcd.exitCode -ne 0){throw ('Post-restore bcdedit enumeration failed exit='+$afterBcd.exitCode+': '+($afterBcd.output -join ' | '))}
    $actualSigning=Get-StorageHostBcdElement -Lines $afterBcd.output
    $expectedSigning=$baseline.inventory.policy.testSigning
    if([bool]$expectedSigning.exists -ne [bool]$actualSigning.exists -or ([bool]$expectedSigning.exists -and [string]$expectedSigning.value -cne [string]$actualSigning.value)){
        throw ('BCDEdit testsigning restore mismatch expectedExists='+$expectedSigning.exists+' expectedValue='+$expectedSigning.value+' actualExists='+$actualSigning.exists+' actualValue='+$actualSigning.value)
    }
    $afterTrust=Get-StorageHostCertificateMembership -Thumbprint $script:StorageHostCertThumb
    foreach($storeName in @('Root','TrustedPublisher')){
        if([bool]$afterTrust.$storeName -ne [bool]$baseline.trustStores.$storeName){throw ('Trust store restore mismatch '+$storeName+' expected='+$baseline.trustStores.$storeName+' actual='+$afterTrust.$storeName)}
    }
    $script:StorageHostState.policyRestore=[ordered]@{capturedUtc=[DateTime]::UtcNow.ToString('o');expectedBcdTestSigning=$expectedSigning;actualBcdTestSigning=$actualSigning;trustStoresAfter=$afterTrust;secureBootChangedByScript=$false;manualFirmwareRestoreRequired=[bool]$baseline.inventory.policy.secureBoot;bcdChanged=$bcdChanged;trustChanged=$trustChanged;requiresReboot=($bcdChanged -or $trustChanged);externalRecoveryValidated=($null -eq $script:StorageHostState.recoveryEvidenceDegraded)}
    $script:StorageHostState.lastObservedBootTime=$inventory.windows.lastBootUpTime
    $script:StorageHostState.pendingOperation=$null
    Set-StorageHostPhase -Phase 'PolicyRestorePendingReboot' -ActionName $Action -Message 'Captured BCD testsigning and owned public certificate memberships are restored. No reboot was issued; after the requested reboot, restore Secure Boot in firmware if baseline was ON.'
    return [ordered]@{status=if($bcdChanged -or $trustChanged){'POLICY_RESTORED_PENDING_REBOOT'}else{'POLICY_ALREADY_BASELINE'};bcdTestSigning=$actualSigning;trustStores=$afterTrust;secureBootCurrent=$inventory.policy.secureBoot;secureBootRestoreRequired=[bool]$baseline.inventory.policy.secureBoot;rebootRequired=($bcdChanged -or $trustChanged);rebootIssued=$false;runDirectory=$script:StorageHostRun}
}
function Invoke-StorageHostFinalVerify {
    Assert-StorageHostRecoveryCopy -AllowMissingExternal | Out-Null
    $release=Get-StorageHostActiveRelease
    $baseline=$script:StorageHostState.baseline
    $result=Write-StorageHostInventory -EvidenceDirectory $script:StorageHostActionDirectory -Name 'final-native-wmi-readback.json'
    $inventory=$result.inventory
    $mismatches=[Collections.Generic.List[string]]::new()
    if([string]$inventory.disk.serial -cne [string]$baseline.inventory.disk.serial){$mismatches.Add('native serial expected='+$baseline.inventory.disk.serial+' actual='+$inventory.disk.serial)}
    if([string]$inventory.disk.wmiSerial -cne [string]$baseline.inventory.disk.wmiSerial){$mismatches.Add('WMI serial expected='+$baseline.inventory.disk.wmiSerial+' actual='+$inventory.disk.wmiSerial)}
    if([bool]$inventory.disk.isBoot -ne [bool]$baseline.inventory.disk.isBoot -or [bool]$inventory.disk.isSystem -ne [bool]$baseline.inventory.disk.isSystem){$mismatches.Add('disk boot/system flags expected='+$baseline.inventory.disk.isBoot+'/'+$baseline.inventory.disk.isSystem+' actual='+$inventory.disk.isBoot+'/'+$inventory.disk.isSystem)}
    if([string]$inventory.disk.pnpDeviceId -cne [string]$baseline.inventory.disk.pnpDeviceId){$mismatches.Add('PnP instance expected='+$baseline.inventory.disk.pnpDeviceId+' actual='+$inventory.disk.pnpDeviceId)}
    if(@($inventory.disk.hardwareIds|Where-Object {$_ -ieq [string]$release.manifest.hardwareId}).Count -ne 1){$mismatches.Add('hardware ID expected exactly once='+$release.manifest.hardwareId+' actual='+(@($inventory.disk.hardwareIds) -join '|'))}
    if($inventory.storageFilter.present){$mismatches.Add('storage service expected absent actual present')}
    if($null -ne $inventory.storageFilter.registry){$mismatches.Add('storage service registry key expected absent actual present Start='+$inventory.storageFilter.registry.start)}
    if($inventory.pnpStack.filterPresent){$mismatches.Add('selected disk stack expected no filter actual='+($inventory.pnpStack.stackLines -join ' > '))}
    if(-not $inventory.fileIo.match){$mismatches.Add('file I/O SHA256 expected='+$inventory.fileIo.expectedSha256+' actual='+$inventory.fileIo.actualSha256)}
    if($null -eq $inventory.policy.secureBoot -or [bool]$inventory.policy.secureBoot -ne [bool]$baseline.inventory.policy.secureBoot){$mismatches.Add('Secure Boot expected='+$baseline.inventory.policy.secureBoot+' actual='+$inventory.policy.secureBoot+' readError='+$inventory.policy.secureBootReadError)}
    $expectedTestSigning=$baseline.inventory.policy.testSigning
    $actualTestSigning=$inventory.policy.testSigning
    if([bool]$expectedTestSigning.exists -ne [bool]$actualTestSigning.exists -or ([bool]$expectedTestSigning.exists -and [string]$expectedTestSigning.value -cne [string]$actualTestSigning.value)){
        $mismatches.Add('BCD testsigning expected exists='+$expectedTestSigning.exists+' value='+$expectedTestSigning.value+' actual exists='+$actualTestSigning.exists+' value='+$actualTestSigning.value)
    }
    if(-not $inventory.policy.codeIntegrity.success){$mismatches.Add('Code Integrity query failed actual NTSTATUS='+$inventory.policy.codeIntegrity.ntStatus)}
    elseif((([long]$inventory.policy.codeIntegrity.options -band 0x2) -ne ([long]$baseline.inventory.policy.codeIntegrity.options -band 0x2))){$mismatches.Add('effective TESTSIGNING bit expected=0x'+([int]$baseline.inventory.policy.codeIntegrity.options).ToString('X8')+' actual=0x'+([int]$inventory.policy.codeIntegrity.options).ToString('X8'))}
    $trust=Get-StorageHostCertificateMembership -Thumbprint $script:StorageHostCertThumb
    foreach($storeName in @('Root','TrustedPublisher')){
        if([bool]$trust.$storeName -ne [bool]$baseline.trustStores.$storeName){$mismatches.Add('LocalMachine '+$storeName+' membership expected='+$baseline.trustStores.$storeName+' actual='+$trust.$storeName)}
    }
    $secureBefore=$baseline.inventory.policy.secureBootVariable
    $secureAfter=$inventory.policy.secureBootVariable
    if([bool]$secureBefore.supported){
        if(-not [bool]$secureAfter.supported){$mismatches.Add('SecureBoot firmware variable expected readable baseline actual read failed Win32Error='+$secureAfter.win32Error+' '+$secureAfter.win32ErrorText)}
        elseif([string]$secureAfter.bytesHex -cne [string]$secureBefore.bytesHex -or [long]$secureAfter.size -ne [long]$secureBefore.size -or [long]$secureAfter.attributes -ne [long]$secureBefore.attributes){
            $mismatches.Add('SecureBoot variable expected bytes='+$secureBefore.bytesHex+' size='+$secureBefore.size+' attrs=0x'+([long]$secureBefore.attributes).ToString('X')+' actual bytes='+$secureAfter.bytesHex+' size='+$secureAfter.size+' attrs=0x'+([long]$secureAfter.attributes).ToString('X'))
        }
    } elseif([bool]$secureAfter.supported){$mismatches.Add('SecureBoot variable baseline was not readable; actual bytes='+$secureAfter.bytesHex+'; no raw baseline comparison is possible')}
    if($null -ne $script:StorageHostState.policyRestore -and [bool]$script:StorageHostState.policyRestore.requiresReboot){
        try { Assert-StorageHostRebootedSince -PreviousBootTime ([string]$script:StorageHostState.lastObservedBootTime) -CurrentBootTime ([string]$inventory.windows.lastBootUpTime) -Context 'Final verification' }
        catch { $mismatches.Add($_.Exception.Message) }
    }
    foreach($name in @('transparent','replacement')){
        $owned=$script:StorageHostState.ownedPackages.$name
        if($null -ne $owned){$mismatches.Add('owned package expected absent actual tracked: '+$owned.publishedName)}
        $packageInfHash=[string](@($release.manifest.packages.$name.files|Where-Object {$_.name -ceq 'client_storage_filter.inf'})[0].sha256)
        $installed=Get-StorageHostOwnedInfCandidates -ExpectedSha256 $packageInfHash
        if(@($installed).Count){$mismatches.Add('release INF expected removed actual installed: '+(@($installed|ForEach-Object {$_.publishedName}) -join ','))}
    }
    $fullCycle=($null -ne $script:StorageHostState.verifiedTransparentProof -and $null -ne $script:StorageHostState.verifiedReplacementProof -and @($script:StorageHostState.packageHistory|Where-Object {$_.mode -ceq 'transparent'}).Count -gt 0 -and @($script:StorageHostState.packageHistory|Where-Object {$_.mode -ceq 'replacement'}).Count -gt 0)
    $testOutcome=if($fullCycle){'BOTH_PACKAGES_VERIFIED_AND_REMOVED'}elseif($null -ne $script:StorageHostState.recoveryOutcome){'ABORTED_THEN_ORIGINAL_STATE_RECONCILED'}else{'ABORTED_ORIGINAL_STATE_RESTORED_WITHOUT_FULL_TEST'}
    $report=[ordered]@{schema='storage-host-final-verification.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');result=if($mismatches.Count){'FAIL'}else{'PASS'};testOutcome=$testOutcome;recoveryOutcome=$script:StorageHostState.recoveryOutcome;verifiedTransparentProof=$script:StorageHostState.verifiedTransparentProof;verifiedReplacementProof=$script:StorageHostState.verifiedReplacementProof;baseline=$baseline.inventory;actual=$inventory;trustStoresExpected=$baseline.trustStores;trustStoresActual=$trust;packageRemoval=@{transparentInfSha256=@($release.manifest.packages.transparent.files|Where-Object {$_.name -ceq 'client_storage_filter.inf'})[0].sha256;replacementInfSha256=@($release.manifest.packages.replacement.files|Where-Object {$_.name -ceq 'client_storage_filter.inf'})[0].sha256};mismatches=@($mismatches.ToArray());noEspOrFirmwareWriteAttempted=$true}
    Write-StorageHostAtomicJson -Path (Join-Path $script:StorageHostActionDirectory 'final-verification.json') -Value $report
    if($mismatches.Count){throw ($mismatches -join '; ')}
    $script:StorageHostState.finalVerificationPath=Join-Path $script:StorageHostActionDirectory 'final-verification.json'
    $script:StorageHostState.finalVerificationSha256=(Get-FileHash -LiteralPath $script:StorageHostState.finalVerificationPath -Algorithm SHA256).Hash
    $script:StorageHostState.requiresRecovery=$false
    Set-StorageHostPhase -Phase 'Complete' -ActionName $Action -Message 'Original native and WMI reads, firmware mode/variable, BCD testsigning, trust-store membership, filter removal, file I/O and boot change checks match their captured baseline.'
    return [ordered]@{status='FINAL_VERIFY_PASS';testOutcome=$testOutcome;nativeSerial=$inventory.disk.serial;wmiSerial=$inventory.disk.wmiSerial;secureBoot=$inventory.policy.secureBoot;secureBootVariable=$inventory.policy.secureBootVariable;testSigning=$inventory.policy.testSigning;filterPresent=$inventory.pnpStack.filterPresent;trustStores=$trust;finalReport=$script:StorageHostState.finalVerificationPath;runDirectory=$script:StorageHostRun}
}

function Invoke-StorageHostAction {
    switch($Action){
        'Prepare' { return Invoke-StorageHostPrepare }
        default {
            if($Action -ne 'Status'){Assert-StorageHostAdmin}
            [void](Get-StorageHostRunDirectory)
            if([string]$script:StorageHostState.phase -ceq 'Complete' -and $Action -ne 'Status'){throw 'This run already completed; Prepare a new run to perform another test.'}
            if($Action -eq 'Status'){
                return [ordered]@{status='STATUS';runDirectory=$script:StorageHostRun;runId=$script:StorageHostState.runId;phase=$script:StorageHostState.phase;lastAction=$script:StorageHostState.lastAction;requiresRecovery=$script:StorageHostState.requiresRecovery;lastFailure=$script:StorageHostState.lastFailure;ownedPackages=$script:StorageHostState.ownedPackages;recoveryDirectory=$script:StorageHostState.recoveryDirectory}
            }
            [void](Get-StorageHostAllowedTransition -Phase ([string]$script:StorageHostState.phase) -Action $Action)
            if($Action -eq 'EnableTestMode'){
                return Invoke-StorageHostEnableTestMode
            }
            $null=Get-StorageHostActiveRelease
            switch($Action){
                'InstallTransparent' { return Invoke-StorageHostInstallPackage -Mode transparent -PendingPhase 'TransparentInstallPendingReboot' }
                'VerifyTransparent' { return Invoke-StorageHostVerifyInstalled -Mode transparent -ExpectedMode Original -NextPhase 'TransparentVerified' }
                'InstallReplacement' { return Invoke-StorageHostInstallPackage -Mode replacement -PendingPhase 'ReplacementInstallPendingReboot' }
                'VerifyReplacement' { return Invoke-StorageHostVerifyInstalled -Mode replacement -ExpectedMode Replacement -NextPhase 'ReplacementVerified' }
                'RecoverOriginal' { return Invoke-StorageHostRecoverOriginal }
                'Remove' {
                    $mode=if([string]$script:StorageHostState.phase -like 'Transparent*'){'transparent'}else{'replacement'}
                    $phase=if($mode -ceq 'transparent'){'TransparentRemovalPendingReboot'}else{'ReplacementRemovalPendingReboot'}
                    $pendingInstall=[string]$script:StorageHostState.phase -match '^(Transparent|Replacement)Install(?:InProgress|PendingReboot)$'
                    return Invoke-StorageHostRemovePackage -Mode $mode -RemovalPhase $phase -AllowNotActive:$pendingInstall
                }
                'VerifyOriginal' {
                    $next=if([string]$script:StorageHostState.phase -ceq 'TransparentRemovalPendingReboot'){'TransparentRemoved'}else{'ReplacementRemoved'}
                    return Invoke-StorageHostVerifyOriginalAfterRemoval -NextPhase $next
                }
                'RestorePolicy' { return Invoke-StorageHostRestorePolicy }
                'FinalVerify' { return Invoke-StorageHostFinalVerify }
                default { throw "Unhandled storage-host action: $Action" }
            }
        }
    }
}

function Get-StorageHostActiveRelease {
    if ([string]::IsNullOrWhiteSpace([string]$script:StorageHostState.releaseDirectory)) { throw 'Run state has no releaseDirectory.' }
    if (-not [string]::IsNullOrWhiteSpace($ReleaseDirectory) -and [IO.Path]::GetFullPath($ReleaseDirectory) -cne [IO.Path]::GetFullPath([string]$script:StorageHostState.releaseDirectory)) { throw 'Requested release differs from the pinned Prepare release.' }
    $release=Get-StorageHostRelease -Directory ([string]$script:StorageHostState.releaseDirectory)
    if ($release.manifestSha256 -cne [string]$script:StorageHostState.releaseManifestSha256) { throw 'Release manifest changed after Prepare.' }
    $script:StorageHostRelease=$release
    return $release
}

function Get-StorageHostPackageMode {
    param([string]$CurrentPhase)
    if ($CurrentPhase -match '^Transparent') { return 'transparent' }
    if ($CurrentPhase -match '^Replacement') { return 'replacement' }
    throw "Cannot identify package mode from phase $CurrentPhase."
}
function Assert-StorageHostRebootedSince {
    param([string]$PreviousBootTime,[string]$CurrentBootTime,[string]$Context)
    try {
        $before=[DateTimeOffset]::Parse($PreviousBootTime).ToUniversalTime()
        $after=[DateTimeOffset]::Parse($CurrentBootTime).ToUniversalTime()
    } catch { throw "$Context boot time could not be parsed: previous=$PreviousBootTime current=$CurrentBootTime" }
    if ($after -le $before) { throw "$Context requires a new Windows boot; previous bootTime=$PreviousBootTime current bootTime=$CurrentBootTime" }
}
function Save-StorageHostReadbackVerdict {
    param([string]$Name,[string]$ExpectedMode,$Inventory,[string[]]$Mismatches,[string]$PreviousBootTime)
    $record=[ordered]@{
        schema='storage-host-readback.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');action=$Action;expectedMode=$ExpectedMode
        originalSerial='0000_0000_0100_0000_E4D2_5CDF_3227_5501.';replacementSerial='1111_1111_1111_1111_A1B2_C3D4_5566_7788.'
        nativeSerial=$Inventory.disk.serial;wmiSerial=$Inventory.disk.wmiSerial;pnpInstanceId=$Inventory.disk.pnpDeviceId;hardwareIds=$Inventory.disk.hardwareIds
        filterService=$Inventory.storageFilter;actualStack=$Inventory.pnpStack.stackLines;fileIo=$Inventory.fileIo
        secureBoot=$Inventory.policy.secureBoot;testSigning=$Inventory.policy.testSigning;codeIntegrity=$Inventory.policy.codeIntegrity
        previousBootTime=$PreviousBootTime;currentBootTime=$Inventory.windows.lastBootUpTime;mismatches=@($Mismatches);result=if($Mismatches.Count){'FAIL'}else{'PASS'}
    }
    $path=Join-Path $script:StorageHostActionDirectory $Name
    Write-StorageHostAtomicJson -Path $path -Value $record
    return $record
}
function Get-StorageHostFilterMismatches {
    param($Inventory,[ValidateSet('Original','Replacement')][string]$Mode,[ValidateSet('Present','Absent')][string]$Filter)
    $mismatches=[Collections.Generic.List[string]]::new()
    $expected=if($Mode -ceq 'Replacement'){'1111_1111_1111_1111_A1B2_C3D4_5566_7788.'}else{'0000_0000_0100_0000_E4D2_5CDF_3227_5501.'}
    if([string]$Inventory.disk.serial -cne $expected){$mismatches.Add("native serial expected=$expected actual=$($Inventory.disk.serial)")}
    if([string]$Inventory.disk.wmiSerial -cne $expected){$mismatches.Add("WMI serial expected=$expected actual=$($Inventory.disk.wmiSerial)")}
    $pinned=$script:StorageHostRelease.manifest
    if([string]$Inventory.disk.pnpDeviceId -cne [string]$pinned.pnpInstanceId){$mismatches.Add('PnP instance expected='+$pinned.pnpInstanceId+' actual='+$Inventory.disk.pnpDeviceId)}
    if(@($Inventory.disk.hardwareIds|Where-Object {$_ -ieq [string]$pinned.hardwareId}).Count -ne 1){$mismatches.Add('hardware ID expected exactly once='+$pinned.hardwareId+' actual='+(@($Inventory.disk.hardwareIds) -join '|'))}
    $service=@($Inventory.storageFilter.services|Where-Object {$_.State -ceq 'Running'})
    $running=$service.Count -gt 0
    $stack=[bool]$Inventory.pnpStack.filterPresent
    if($Filter -ceq 'Present'){
        if(-not $running){$mismatches.Add('filter service expected=Running actual='+(($Inventory.storageFilter.services|ForEach-Object {$_.State}) -join ','))}
        if(-not $stack){$mismatches.Add('selected PHYSICALDRIVE0 Device Stack expected=client_storage_filter present actual='+($Inventory.pnpStack.stackLines -join ' > '))}
        if($null -eq $Inventory.storageFilter.registry -or $null -eq $Inventory.storageFilter.registry.start -or [int]$Inventory.storageFilter.registry.start -ne 0){$actual=if($null -eq $Inventory.storageFilter.registry){'<absent>'}else{[string]$Inventory.storageFilter.registry.start};$mismatches.Add('service StartType expected=0 actual='+$actual)}
    } else {
        if($running){$mismatches.Add('filter service expected not Running actual=Running')}
        if($stack){$mismatches.Add('selected PHYSICALDRIVE0 Device Stack expected filter absent actual='+($Inventory.pnpStack.stackLines -join ' > '))}
        if($Inventory.storageFilter.present){$mismatches.Add('storage filter service expected absent actual=present')}
        if($null -ne $Inventory.storageFilter.registry){$mismatches.Add('storage filter service registry key expected absent actual present Start='+$Inventory.storageFilter.registry.start)}
    }
    if(-not $Inventory.fileIo.match){$mismatches.Add('1 MiB file I/O SHA256 expected='+$Inventory.fileIo.expectedSha256+' actual='+$Inventory.fileIo.actualSha256)}
    if(-not $Inventory.policy.codeIntegrity.success){$mismatches.Add('Code Integrity query expected success actual NTSTATUS='+$Inventory.policy.codeIntegrity.ntStatus)}
    elseif(($Inventory.policy.codeIntegrity.options -band 0x2) -eq 0){$mismatches.Add('effective kernel TESTSIGNING bit expected=0x2 actual CodeIntegrityOptions=0x'+([int]$Inventory.policy.codeIntegrity.options).ToString('X8'))}
    if($null -eq $Inventory.policy.secureBoot -or [bool]$Inventory.policy.secureBoot){$mismatches.Add('Secure Boot expected OFF during test actual='+$Inventory.policy.secureBoot+' readError='+$Inventory.policy.secureBootReadError)}
    return @($mismatches.ToArray())
}
function Find-StorageHostInstalledInf {
    param([string]$Mode,[string]$ExpectedPnp)
    $release=$script:StorageHostRelease
    $infHash=[string](@($release.manifest.packages.$Mode.files|Where-Object {$_.name -ceq 'client_storage_filter.inf'})[0].sha256)
    $candidates=@(Get-StorageHostOwnedInfCandidates -ExpectedSha256 $infHash)
    if($candidates.Count -ne 1){throw ('Expected exactly one Windows\INF oem*.inf matching release INF hash '+$infHash+'; found '+$candidates.Count)}
    if(-not (Test-StorageHostOwnedInf -PublishedName $candidates[0].publishedName -ExpectedSha256 $infHash -ActualSha256 $candidates[0].sha256 -ExpectedPnpInstanceId ([string]$release.manifest.pnpInstanceId) -ActualPnpInstanceId $ExpectedPnp)){
        throw ('Installed INF ownership check failed for '+$candidates[0].publishedName+' expectedHash='+$infHash+' actualHash='+$candidates[0].sha256+' expectedPnp='+$release.manifest.pnpInstanceId+' actualPnp='+$ExpectedPnp)
    }
    return [pscustomobject]@{packageMode=$Mode;publishedName=$candidates[0].publishedName;infSha256=$infHash;pnpInstanceId=$ExpectedPnp}
}
function Invoke-StorageHostInstallPackage {
    param([ValidateSet('transparent','replacement')][string]$Mode,[string]$PendingPhase)
    Assert-StorageHostRecoveryCopy | Out-Null
    $release=Get-StorageHostActiveRelease
    if($Mode -ceq 'replacement'){
        $proofRef=$script:StorageHostState.verifiedTransparentProof
        if($null -eq $proofRef){throw 'Replacement install is blocked until a passing transparent-filter native/WMI read-back proof is persisted.'}
        $proofPath=[string]$proofRef.path
        if(-not(Test-Path -LiteralPath $proofPath -PathType Leaf)){throw ('Transparent verification proof is missing: '+$proofPath)}
        $proofHash=(Get-FileHash -LiteralPath $proofPath -Algorithm SHA256).Hash
        if($proofHash -cne [string]$proofRef.sha256){throw ('Transparent proof SHA-256 mismatch expected='+$proofRef.sha256+' actual='+$proofHash)}
        $proof=Get-Content -LiteralPath $proofPath -Raw|ConvertFrom-Json
        if(-not(Test-StorageHostTransparentProof -Proof $proof)){throw 'Persisted transparent verification no longer satisfies the native/WMI original-value, StartType 0, running-stack and file-I/O proof contract.'}
    }
    $before=Write-StorageHostInventory -EvidenceDirectory $script:StorageHostActionDirectory -Name ('before-'+$Mode+'-install.json')
    Assert-StorageHostBaselineIdentity -Inventory $before.inventory -RequireNoFilter -RequireOriginal
    Assert-StorageHostPolicyEnabled -Inventory $before.inventory
    $packagePath=$release.packagePaths[$Mode]
    $infPath=Join-Path $packagePath 'client_storage_filter.inf'
    $infHash=[string](@($release.manifest.packages.$Mode.files|Where-Object {$_.name -ceq 'client_storage_filter.inf'})[0].sha256)
    $already=Get-StorageHostOwnedInfCandidates -ExpectedSha256 $infHash
    if(@($already).Count){throw ('An INF with this release SHA already exists before this run; refusing ambiguous install. names='+(@($already|ForEach-Object {$_.publishedName}) -join ','))}
    if($before.inventory.storageFilter.present){throw 'client_storage_filter service already exists before installation.'}
    $phase=if($Mode -ceq 'transparent'){'TransparentInstallInProgress'}else{'ReplacementInstallInProgress'}
    $target=$PendingPhase
    $script:StorageHostState.pendingOperation=[ordered]@{mode=$Mode;kind='install';startedUtc=[DateTime]::UtcNow.ToString('o');expectedInfSha256=$infHash;expectedPnpInstanceId=$release.manifest.pnpInstanceId;bootTimeBefore=$before.inventory.windows.lastBootUpTime}
    Set-StorageHostPhase -Phase $phase -ActionName $Action -Message ('Install intent persisted before pnputil adds the '+$Mode+' StartType 0 storage extension package.')
    $capture=Invoke-StorageHostCapture -Program (Join-Path $env:SystemRoot 'System32\pnputil.exe') -Arguments @('/add-driver',$infPath,'/install')
    Write-StorageHostAtomicJson -Path (Join-Path $script:StorageHostActionDirectory ($Mode+'-pnputil-add.json')) -Value ([ordered]@{program='pnputil.exe';arguments=@('/add-driver',$infPath,'/install');exitCode=$capture.exitCode;output=$capture.output;capturedUtc=[DateTime]::UtcNow.ToString('o')})
    $owned=$null
    try { $owned=Find-StorageHostInstalledInf -Mode $Mode -ExpectedPnp ([string]$release.manifest.pnpInstanceId) }
    catch {
        if($capture.exitCode -eq 0 -or $capture.exitCode -eq 3010){throw}
        throw ('PnPUtil exited '+$capture.exitCode+' and exact package ownership was not established: '+$_.Exception.Message+'; output='+($capture.output -join ' | '))
    }
    Set-StorageHostProperty -Object $script:StorageHostState.ownedPackages -Name $Mode -Value $owned
    $script:StorageHostState.pendingOperation=[ordered]@{mode=$Mode;kind='install';startedUtc=[DateTime]::UtcNow().ToString('o');expectedInfSha256=$infHash;expectedPnpInstanceId=$release.manifest.pnpInstanceId;publishedName=$owned.publishedName;bootTimeBefore=$before.inventory.windows.lastBootUpTime;pnputilExitCode=$capture.exitCode}
    $script:StorageHostState.lastObservedBootTime=$before.inventory.windows.lastBootUpTime
    Save-StorageHostState
    Add-StorageHostEvent -Kind 'package-installed' -Message ('PnPUtil registered exact '+$Mode+' package; reboot is required before verification.') -Data @{publishedName=$owned.publishedName;infSha256=$owned.infSha256;exitCode=$capture.exitCode}
    if($capture.exitCode -notin @(0,3010)){throw ('pnputil /add-driver exited '+$capture.exitCode+': '+($capture.output -join ' | ')+'; exact INF is tracked for removal/recovery.')}
    Set-StorageHostPhase -Phase $target -ActionName $Action -Message 'Package registration complete; no reboot was issued.'
    return [ordered]@{status=($Mode.ToUpperInvariant()+'_INSTALL_PENDING_REBOOT');publishedName=$owned.publishedName;infSha256=$owned.infSha256;pnputilExitCode=$capture.exitCode;rebootIssued=$false;runDirectory=$script:StorageHostRun}
}
function Invoke-StorageHostVerifyInstalled {
    param([ValidateSet('transparent','replacement')][string]$Mode,[ValidateSet('Original','Replacement')][string]$ExpectedMode,[string]$NextPhase)
    Assert-StorageHostRecoveryCopy -AllowMissingExternal | Out-Null
    $previous=[string]$script:StorageHostState.lastObservedBootTime
    $result=Write-StorageHostInventory -EvidenceDirectory $script:StorageHostActionDirectory -Name 'fresh-native-wmi-readback.json'
    $inventory=$result.inventory
    $mismatches=[Collections.Generic.List[string]]::new()
    foreach($message in @(Get-StorageHostFilterMismatches -Inventory $inventory -Mode $ExpectedMode -Filter Present)){$mismatches.Add($message)}
    $owned=$script:StorageHostState.ownedPackages.$Mode
    if($null -eq $owned){$mismatches.Add('owned package record expected present actual absent')}
    else {
        $actualInf=Join-Path (Join-Path $env:SystemRoot 'INF') ([string]$owned.publishedName)
        if(-not(Test-Path -LiteralPath $actualInf -PathType Leaf)){$mismatches.Add('owned Windows INF expected file='+$actualInf+' actual=missing')}
        else{$actualHash=(Get-FileHash -LiteralPath $actualInf -Algorithm SHA256).Hash;if($actualHash -cne [string]$owned.infSha256){$mismatches.Add('owned INF SHA256 expected='+$owned.infSha256+' actual='+$actualHash)}}
    }
    try { Assert-StorageHostRebootedSince -PreviousBootTime $previous -CurrentBootTime ([string]$inventory.windows.lastBootUpTime) -Context 'Install verification' }
    catch { $mismatches.Add($_.Exception.Message) }
    [void](Save-StorageHostReadbackVerdict -Name 'verification.json' -ExpectedMode $ExpectedMode -Inventory $inventory -Mismatches @($mismatches.ToArray()) -PreviousBootTime $previous)
    if($mismatches.Count){throw ($mismatches -join '; ')}
    $script:StorageHostState.lastObservedBootTime=$inventory.windows.lastBootUpTime
    $script:StorageHostState.verifiedPackageMode=$Mode
    $script:StorageHostState.pendingOperation=$null
    $proofPath=Join-Path $script:StorageHostActionDirectory 'verification.json'
    $proofRef=[ordered]@{path=$proofPath;sha256=(Get-FileHash -LiteralPath $proofPath -Algorithm SHA256).Hash;verifiedUtc=[DateTime]::UtcNow.ToString('o');bootTime=$inventory.windows.lastBootUpTime;mode=$Mode}
    if($Mode -ceq 'transparent'){$script:StorageHostState.verifiedTransparentProof=$proofRef}else{$script:StorageHostState.verifiedReplacementProof=$proofRef}
    Set-StorageHostPhase -Phase $NextPhase -ActionName $Action -Message ('Fresh native IOCTL and WMI reads verified after reboot; selected disk stack and driver start state match '+$Mode+'.')
    return [ordered]@{status=($Mode.ToUpperInvariant()+'_VERIFIED');nativeSerial=$inventory.disk.serial;wmiSerial=$inventory.disk.wmiSerial;service=$inventory.storageFilter;actualStack=$inventory.pnpStack.stackLines;fileIoSha256=$inventory.fileIo.actualSha256;runDirectory=$script:StorageHostRun}
}
function Assert-StorageHostOwnedPackageForRemoval {
    param([string]$Mode,$Inventory,[switch]$AllowNotActive)
    $owned=$script:StorageHostState.ownedPackages.$Mode
    $expectedInfHash=[string](@($script:StorageHostRelease.manifest.packages.$Mode.files|Where-Object {$_.name -ceq 'client_storage_filter.inf'})[0].sha256)
    if($null -eq $owned){
        $pending=$script:StorageHostState.pendingOperation
        if($null -eq $pending -or [string]$pending.mode -cne $Mode -or [string]$pending.expectedInfSha256 -cne $expectedInfHash){throw ('No journaled owned package record exists for '+$Mode+' and no matching install intent can recover ownership.')}
        $candidates=@(Get-StorageHostOwnedInfCandidates -ExpectedSha256 $expectedInfHash)
        if($candidates.Count -ne 1){throw ('Install intent ownership recovery expected exactly one Windows INF with SHA256 '+$expectedInfHash+'; found '+$candidates.Count)}
        $owned=[pscustomobject]@{packageMode=$Mode;publishedName=$candidates[0].publishedName;infSha256=$expectedInfHash;pnpInstanceId=[string]$script:StorageHostRelease.manifest.pnpInstanceId}
        Set-StorageHostProperty -Object $script:StorageHostState.ownedPackages -Name $Mode -Value $owned
        Save-StorageHostState
    }
    if([string]$Inventory.disk.pnpDeviceId -cne [string]$script:StorageHostRelease.manifest.pnpInstanceId){throw ('Refusing package removal because current physical disk PnP instance differs from the pinned target: expected='+$script:StorageHostRelease.manifest.pnpInstanceId+' actual='+$Inventory.disk.pnpDeviceId)}
    if(@($Inventory.disk.hardwareIds|Where-Object {$_ -ieq [string]$script:StorageHostRelease.manifest.hardwareId}).Count -ne 1){throw ('Refusing package removal because pinned hardware ID is absent: '+$script:StorageHostRelease.manifest.hardwareId)}
    $infPath=Join-Path (Join-Path $env:SystemRoot 'INF') ([string]$owned.publishedName)
    if(-not(Test-Path -LiteralPath $infPath -PathType Leaf)){throw ('Owned INF expected at '+$infPath+' actual=missing')}
    $actual=(Get-FileHash -LiteralPath $infPath -Algorithm SHA256).Hash
    if(-not(Test-StorageHostOwnedInf -PublishedName ([string]$owned.publishedName) -ExpectedSha256 ([string]$owned.infSha256) -ActualSha256 $actual -ExpectedPnpInstanceId ([string]$script:StorageHostRelease.manifest.pnpInstanceId) -ActualPnpInstanceId ([string]$Inventory.disk.pnpDeviceId))){
        throw ('Owned INF check failed expected name='+$owned.publishedName+' SHA256='+$owned.infSha256+' actual='+$actual+' PnP expected='+$script:StorageHostRelease.manifest.pnpInstanceId+' actual='+$Inventory.disk.pnpDeviceId)
    }
    if(-not $Inventory.pnpStack.filterPresent -or @($Inventory.storageFilter.services|Where-Object {$_.State -ceq 'Running'}).Count -eq 0){
        Add-StorageHostEvent -Kind 'cleanup-driver-not-active' -Message 'The exact owned package remains removable from its published INF even though a running stack instance was not observed.' -Data @{mode=$Mode;publishedName=$owned.publishedName;stackPresent=[bool]$Inventory.pnpStack.filterPresent;servicePresent=[bool]$Inventory.storageFilter.present}
    }
    return $owned
}
function Invoke-StorageHostRemovePackage {
    param([string]$Mode,[string]$RemovalPhase,[switch]$AllowNotActive)
    Assert-StorageHostRecoveryCopy -AllowMissingExternal | Out-Null
    $release=Get-StorageHostActiveRelease
    $inventoryResult=Write-StorageHostInventory -EvidenceDirectory $script:StorageHostActionDirectory -Name 'before-remove-inventory.json'
    $inventory=$inventoryResult.inventory
    $owned=Assert-StorageHostOwnedPackageForRemoval -Mode $Mode -Inventory $inventory -AllowNotActive:$AllowNotActive
    $expectedMode=if($Mode -ceq 'replacement'){'Replacement'}else{'Original'}
    $expectedSerial=if($Mode -ceq 'replacement'){'1111_1111_1111_1111_A1B2_C3D4_5566_7788.'}else{'0000_0000_0100_0000_E4D2_5CDF_3227_5501.'}
    $script:StorageHostState.pendingOperation=[ordered]@{mode=$Mode;kind='remove';startedUtc=[DateTime]::UtcNow.ToString('o');publishedName=$owned.publishedName;infSha256=$owned.infSha256;bootTimeBefore=$inventory.windows.lastBootUpTime;observedExpectedMode=$expectedMode;observedNativeSerialExpected=$expectedSerial;observedNativeSerialActual=[string]$inventory.disk.serial;observedWmiSerialExpected=$expectedSerial;observedWmiSerialActual=[string]$inventory.disk.wmiSerial;observedStackPresent=[bool]$inventory.pnpStack.filterPresent;observedServicePresent=[bool]$inventory.storageFilter.present;observedFileIoMatch=[bool]$inventory.fileIo.match;cleanupDidNotRequireSuccessfulReadback=$true}
    $progressPhase=if($Mode -ceq 'transparent'){'TransparentRemovalInProgress'}else{'ReplacementRemovalInProgress'}
    Set-StorageHostPhase -Phase $progressPhase -ActionName $Action -Message 'Removal intent persisted before asking PnPUtil to uninstall the exact owned package.'
    $capture=Invoke-StorageHostCapture -Program (Join-Path $env:SystemRoot 'System32\pnputil.exe') -Arguments @('/delete-driver',[string]$owned.publishedName,'/uninstall')
    Write-StorageHostAtomicJson -Path (Join-Path $script:StorageHostActionDirectory ($Mode+'-pnputil-delete.json')) -Value ([ordered]@{program='pnputil.exe';arguments=@('/delete-driver',[string]$owned.publishedName,'/uninstall');exitCode=$capture.exitCode;output=$capture.output;capturedUtc=[DateTime]::UtcNow.ToString('o')})
    if($capture.exitCode -notin @(0,3010)){throw ('pnputil /delete-driver exited '+$capture.exitCode+': '+($capture.output -join ' | ')+'; ownership record retained for recovery.')}
    Set-StorageHostProperty -Object $script:StorageHostState.pendingOperation -Name 'pnputilExitCode' -Value $capture.exitCode
    $script:StorageHostState.lastObservedBootTime=$inventory.windows.lastBootUpTime
    Save-StorageHostState
    Set-StorageHostPhase -Phase $RemovalPhase -ActionName $Action -Message 'PnPUtil removal request completed; reboot is required before checking restoration.'
    return [ordered]@{status=($Mode.ToUpperInvariant()+'_REMOVAL_PENDING_REBOOT');publishedName=$owned.publishedName;pnputilExitCode=$capture.exitCode;rebootIssued=$false;runDirectory=$script:StorageHostRun}
}
function Invoke-StorageHostVerifyOriginalAfterRemoval {
    param([string]$NextPhase)
    Assert-StorageHostRecoveryCopy -AllowMissingExternal | Out-Null
    $pending=$script:StorageHostState.pendingOperation
    if($null -eq $pending -or $pending.kind -cne 'remove'){throw 'VerifyOriginal requires a completed owned package removal request.'}
    $mode=[string]$pending.mode
    $previous=[string]$pending.bootTimeBefore
    $result=Write-StorageHostInventory -EvidenceDirectory $script:StorageHostActionDirectory -Name 'fresh-native-wmi-readback.json'
    $inventory=$result.inventory
    $mismatches=[Collections.Generic.List[string]]::new()
    if([string]$inventory.disk.serial -cne '0000_0000_0100_0000_E4D2_5CDF_3227_5501.'){$mismatches.Add('native serial expected=0000_0000_0100_0000_E4D2_5CDF_3227_5501. actual='+$inventory.disk.serial)}
    if([string]$inventory.disk.wmiSerial -cne '0000_0000_0100_0000_E4D2_5CDF_3227_5501.'){$mismatches.Add('WMI serial expected=0000_0000_0100_0000_E4D2_5CDF_3227_5501. actual='+$inventory.disk.wmiSerial)}
    $release=Get-StorageHostActiveRelease
    if([string]$inventory.disk.pnpDeviceId -cne [string]$release.manifest.pnpInstanceId){$mismatches.Add('PnP instance expected='+$release.manifest.pnpInstanceId+' actual='+$inventory.disk.pnpDeviceId)}
    if(@($inventory.disk.hardwareIds|Where-Object {$_ -ieq [string]$release.manifest.hardwareId}).Count -ne 1){$mismatches.Add('hardware ID expected exactly once='+$release.manifest.hardwareId+' actual='+(@($inventory.disk.hardwareIds) -join '|'))}
    if($inventory.pnpStack.filterPresent){$mismatches.Add('selected PHYSICALDRIVE0 Device Stack expected no filter actual='+($inventory.pnpStack.stackLines -join ' > '))}
    $running=@($inventory.storageFilter.services|Where-Object {$_.State -ceq 'Running'}).Count -gt 0
    if($running){$mismatches.Add('filter service expected not Running actual=Running')}
    if($inventory.storageFilter.present){$mismatches.Add('filter service expected absent actual=present')}
    if($null -ne $inventory.storageFilter.registry){$mismatches.Add('filter service registry key expected absent actual present Start='+$inventory.storageFilter.registry.start)}
    if(-not $inventory.fileIo.match){$mismatches.Add('1 MiB file I/O SHA256 expected='+$inventory.fileIo.expectedSha256+' actual='+$inventory.fileIo.actualSha256)}
    $remaining=Get-StorageHostOwnedInfCandidates -ExpectedSha256 ([string]$pending.infSha256)
    if(@($remaining).Count){$mismatches.Add('owned package INF expected removed after uninstall/reboot actual matching names='+(@($remaining|ForEach-Object {$_.publishedName}) -join ','))}
    try { Assert-StorageHostRebootedSince -PreviousBootTime $previous -CurrentBootTime ([string]$inventory.windows.lastBootUpTime) -Context 'Removal verification' }
    catch { $mismatches.Add($_.Exception.Message) }
    [void](Save-StorageHostReadbackVerdict -Name 'verification-original.json' -ExpectedMode 'Original' -Inventory $inventory -Mismatches @($mismatches.ToArray()) -PreviousBootTime $previous)
    if($mismatches.Count){throw ($mismatches -join '; ')}
    $script:StorageHostState.packageHistory=@($script:StorageHostState.packageHistory)+@([ordered]@{mode=$mode;publishedName=$pending.publishedName;infSha256=$pending.infSha256;removedUtc=[DateTime]::UtcNow.ToString('o')})
    $script:StorageHostState.ownedPackages.$mode=$null
    $script:StorageHostState.pendingOperation=$null
    $script:StorageHostState.lastObservedBootTime=$inventory.windows.lastBootUpTime
    Set-StorageHostPhase -Phase $NextPhase -ActionName $Action -Message ('After reboot, native and WMI reads show the original and the removed driver is absent from the selected disk stack: '+$mode+'.')
    return [ordered]@{status='ORIGINAL_VERIFIED';nativeSerial=$inventory.disk.serial;wmiSerial=$inventory.disk.wmiSerial;service=$inventory.storageFilter;actualStack=$inventory.pnpStack.stackLines;fileIoSha256=$inventory.fileIo.actualSha256;removedPackage=$pending.publishedName;runDirectory=$script:StorageHostRun}
}

function Invoke-StorageHostRecoverOriginal {
    Assert-StorageHostRecoveryCopy -AllowMissingExternal | Out-Null
    $release=Get-StorageHostActiveRelease
    $result=Write-StorageHostInventory -EvidenceDirectory $script:StorageHostActionDirectory -Name 'recovery-native-wmi-readback.json'
    $inventory=$result.inventory
    $mismatches=[Collections.Generic.List[string]]::new()
    if([string]$inventory.disk.serial -cne '0000_0000_0100_0000_E4D2_5CDF_3227_5501.'){$mismatches.Add('native serial expected=0000_0000_0100_0000_E4D2_5CDF_3227_5501. actual='+$inventory.disk.serial)}
    if([string]$inventory.disk.wmiSerial -cne '0000_0000_0100_0000_E4D2_5CDF_3227_5501.'){$mismatches.Add('WMI serial expected=0000_0000_0100_0000_E4D2_5CDF_3227_5501. actual='+$inventory.disk.wmiSerial)}
    if([string]$inventory.disk.pnpDeviceId -cne [string]$release.manifest.pnpInstanceId){$mismatches.Add('PnP instance expected='+$release.manifest.pnpInstanceId+' actual='+$inventory.disk.pnpDeviceId)}
    if(@($inventory.disk.hardwareIds|Where-Object {$_ -ieq [string]$release.manifest.hardwareId}).Count -ne 1){$mismatches.Add('hardware ID expected exactly once='+$release.manifest.hardwareId+' actual='+(@($inventory.disk.hardwareIds) -join '|'))}
    if($inventory.storageFilter.present){$mismatches.Add('storage service expected absent actual present')}
    if($null -ne $inventory.storageFilter.registry){$mismatches.Add('storage service registry key expected absent actual present Start='+$inventory.storageFilter.registry.start)}
    if($inventory.pnpStack.filterPresent){$mismatches.Add('selected PHYSICALDRIVE0 Device Stack expected filter absent actual='+(@($inventory.pnpStack.stackLines) -join ' > '))}
    if(-not $inventory.fileIo.match){$mismatches.Add('1 MiB file I/O SHA256 expected='+$inventory.fileIo.expectedSha256+' actual='+$inventory.fileIo.actualSha256)}
    $remainingInf=[ordered]@{}
    foreach($name in @('transparent','replacement')){
        $hash=[string](@($release.manifest.packages.$name.files|Where-Object {$_.name -ceq 'client_storage_filter.inf'})[0].sha256)
        $matches=@(Get-StorageHostOwnedInfCandidates -ExpectedSha256 $hash)
        $remainingInf[$name]=@($matches|ForEach-Object {$_.publishedName})
        if($matches.Count){$mismatches.Add($name+' release INF expected no matching Windows\INF package; actual='+(@($matches|ForEach-Object {$_.publishedName}) -join ','))}
    }
    $ownedBefore=[ordered]@{transparent=$script:StorageHostState.ownedPackages.transparent;replacement=$script:StorageHostState.ownedPackages.replacement;pendingOperation=$script:StorageHostState.pendingOperation}
    $report=[ordered]@{schema='storage-host-recovery-reconciliation.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');result=if($mismatches.Count){'FAIL'}else{'PASS'};testOutcome='ABORTED_ORIGINAL_STATE_RECONCILED';recoveryMethod='external removal or System Restore; method not detected';nativeSerialExpected='0000_0000_0100_0000_E4D2_5CDF_3227_5501.';nativeSerialActual=$inventory.disk.serial;wmiSerialExpected='0000_0000_0100_0000_E4D2_5CDF_3227_5501.';wmiSerialActual=$inventory.disk.wmiSerial;pnpInstanceIdExpected=$release.manifest.pnpInstanceId;pnpInstanceIdActual=$inventory.disk.pnpDeviceId;hardwareIdExpected=$release.manifest.hardwareId;hardwareIdsActual=$inventory.disk.hardwareIds;storageService=$inventory.storageFilter;actualStack=$inventory.pnpStack.stackLines;releaseInfMatches=$remainingInf;ownedStateBeforeReconciliation=$ownedBefore;readbackPath=$result.path;readbackSha256=$result.sha256;inventory=$inventory;mismatches=@($mismatches.ToArray());noInstallOrTestSuccessClaim=$true}
    $reportPath=Join-Path $script:StorageHostActionDirectory 'recovery-reconciliation.json'
    Write-StorageHostAtomicJson -Path $reportPath -Value $report
    if($mismatches.Count){throw ($mismatches -join '; ')}
    foreach($name in @('transparent','replacement')){
        $owned=$script:StorageHostState.ownedPackages.$name
        if($null -ne $owned){
            $script:StorageHostState.packageHistory=@($script:StorageHostState.packageHistory)+@([ordered]@{mode=$name;publishedName=$owned.publishedName;infSha256=$owned.infSha256;reconciledAbsentUtc=[DateTime]::UtcNow.ToString('o');reconciliationReport=$reportPath;removalMethod='not detected; absence verified by live read'})
            Set-StorageHostProperty -Object $script:StorageHostState.ownedPackages -Name $name -Value $null
        }
    }
    $script:StorageHostState.pendingOperation=$null
    $script:StorageHostState.lastObservedBootTime=$inventory.windows.lastBootUpTime
    $script:StorageHostState.recoveryOutcome=[ordered]@{capturedUtc=[DateTime]::UtcNow.ToString('o');reconciliationReport=$reportPath;sha256=(Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash;testOutcome='ABORTED_ORIGINAL_STATE_RECONCILED';recoveryMethod='not detected';nativeWmiOriginalsVerified=$true;serviceRegistryAndStackAbsent=$true;releaseInfMatches=@{transparent=@();replacement=@()};noTestSuccessClaim=$true}
    $script:StorageHostState.requiresRecovery=$true
    Set-StorageHostPhase -Phase 'RecoveryReconciled' -ActionName $Action -Message 'After external recovery, live native and WMI values are original, the pinned filter service/registry/stack are absent, and neither release INF hash remains installed. No test success is inferred.'
    return [ordered]@{status='RECOVERY_RECONCILED';testOutcome='ABORTED_ORIGINAL_STATE_RECONCILED';nativeSerial=$inventory.disk.serial;wmiSerial=$inventory.disk.wmiSerial;service=$inventory.storageFilter;actualStack=$inventory.pnpStack.stackLines;releaseInfMatches=$remainingInf;recoveryReport=$reportPath;recoveryMethod='not detected';noTestSuccessClaim=$true;runDirectory=$script:StorageHostRun}
}

function Start-StorageHostWorkflow {
    try {
        Assert-StorageHostWindows
        if (-not (Test-Path -LiteralPath (Join-Path $script:StorageHostRepo 'spoof-handoff') -PathType Container)) { throw "RepositoryRoot is not the expected repository: $script:StorageHostRepo" }
        if ($Action -ne 'Status') { Assert-StorageHostAdmin }
        $summary=Invoke-StorageHostAction
        if ($script:StorageHostActionDirectory) {
            Write-StorageHostAtomicJson -Path (Join-Path $script:StorageHostActionDirectory 'action-summary.json') -Value $summary
            if ($Action -ne 'Status' -and $script:StorageHostRun -and (Test-Path -LiteralPath (Join-Path $script:StorageHostRun 'state.json') -PathType Leaf)) { Set-StorageHostPointer }
        }
        $summary | ConvertTo-Json -Depth 12
    } catch {
        $message=$_.Exception.Message
        Save-StorageHostFailure -Message $message
        if ($script:StorageHostActionDirectory) {
            try { Write-StorageHostAtomicJson -Path (Join-Path $script:StorageHostActionDirectory 'action-summary.json') -Value ([ordered]@{status='FAIL';action=$Action;capturedUtc=[DateTime]::UtcNow.ToString('o');error=$message}) } catch {}
        }
        if ($script:StorageHostRun -and (Test-Path -LiteralPath (Join-Path $script:StorageHostRun 'state.json') -PathType Leaf)) {
            try { Set-StorageHostPointer } catch {}
        }
        [Console]::Error.WriteLine(('STORAGE HOST ACTION FAILED: '+$message))
        exit 1
    }
}

Start-StorageHostWorkflow
