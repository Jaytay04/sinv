[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RunRoot,
    [string]$Action='Probe',
    [string]$ExpectedProbeVerdict='NOT_MET'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
function Get-CombinedAssetProperty {
    param($Asset,[string]$Name)
    $property=$Asset.PSObject.Properties[$Name]
    if ($null -ne $property) { return $property.Value }
    return $null
}
function New-CombinedVmRawBundle {
    param([string]$Directory,[object[]]$Files)
    Add-Type -AssemblyName System.IO.Compression
    $memory=[IO.MemoryStream]::new()
    try {
        $zip=[IO.Compression.ZipArchive]::new($memory,[IO.Compression.ZipArchiveMode]::Create,$true)
        try {
            foreach($file in $Files) {
                if ([string]$file.archiveLeaf -notmatch '\Af\.[0-9]{4}\.[A-Za-z0-9]{1,10}\z') { throw 'Unsafe raw evidence archive name.' }
                $path=Join-Path $Directory ([string]$file.archiveLeaf)
                if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -cne [string]$file.sha256) { throw 'Raw evidence changed before nested archive creation.' }
                $entry=$zip.CreateEntry([string]$file.archiveLeaf,[IO.Compression.CompressionLevel]::Optimal)
                $output=$entry.Open()
                $input=[IO.File]::OpenRead($path)
                try { $input.CopyTo($output) } finally { $input.Dispose();$output.Dispose() }
            }
        } finally { $zip.Dispose() }
        $bytes=$memory.ToArray()
        $sha=[Security.Cryptography.SHA256]::Create()
        try { $hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','') } finally { $sha.Dispose() }
        return [ordered]@{format='zip';sha256=$hash;bytes=$bytes.Length;fileCount=$Files.Count;base64=[Convert]::ToBase64String($bytes)}
    } finally { $memory.Dispose() }
}
function Assert-CombinedVmPhaseIdentity {
    param($System,$Product,$ExpectedIdentity)
    $differences=[Collections.Generic.List[string]]::new()
    if ([string]$System.Manufacturer -cne [string]$ExpectedIdentity.manufacturer) { $differences.Add('manufacturer expected="'+[string]$ExpectedIdentity.manufacturer+'" actual="'+[string]$System.Manufacturer+'"') }
    if ([string]$System.Model -cne [string]$ExpectedIdentity.model) { $differences.Add('model expected="'+[string]$ExpectedIdentity.model+'" actual="'+[string]$System.Model+'"') }
    $actualUuid=[Guid]::Empty
    $expectedUuid=[Guid]::Empty
    if (-not [Guid]::TryParse([string]$Product.UUID,[ref]$actualUuid) -or -not [Guid]::TryParse([string]$ExpectedIdentity.uuid,[ref]$expectedUuid) -or $actualUuid -ne $expectedUuid) { $differences.Add('uuid expected="'+[string]$ExpectedIdentity.uuid+'" actual="'+[string]$Product.UUID+'"') }
    if ($differences.Count -gt 0) { throw ('Current SMBIOS identity differs from the hash-bound expected VM phase: '+($differences.ToArray() -join '; ')) }
}
function Assert-CombinedVmNicOriginalReport {
    param($Report,[object[]]$Bindings)
    if ([string]$Report.schema -cne 'nic-native-wmi-readback.v1' -or [string]$Report.expectedMode -cne 'Original' -or $Report.allLockedExpectedValuesMatchReadOnlySources -ne $true -or $Report.allLockedOriginalsMatchReadOnlySources -ne $true -or @($Report.failures).Count -ne 0 -or @($Report.lockedOriginalEvaluations).Count -ne 4 -or $Bindings.Count -ne 4) { throw 'NIC original report schema, mode or complete four-adapter coverage is invalid.' }
    $counts=@{ndisOid8023CurrentAddress='setupDiNdisInterfaceMatchCount';getAdaptersAddresses='getAdaptersAddressesMatchCount';getAdaptersInfoAddress='getAdaptersInfoMatchCount';getIfTable2='getIfTable2MatchCount';win32NetworkAdapter='win32NetworkAdapterMatchCount';msftNetAdapterNetworkAddresses='msftNetAdapterMatchCount';getNetAdapter='getNetAdapterMatchCount'}
    foreach ($binding in $Bindings) {
        $rows=@($Report.lockedOriginalEvaluations | Where-Object pnpDeviceId -CEQ $binding.pnpDeviceId)
        if ($rows.Count -ne 1) { throw ('NIC restore PnP identity cardinality: expected=1 actual='+$rows.Count+' pnp='+$binding.pnpDeviceId) }
        $row=$rows[0]
        if ([string]$row.expectedMode -cne 'Original') { throw ('NIC restored row is not Original mode: '+$binding.pnpDeviceId) }
        $expected=ConvertTo-CombinedComparableValue NIC mac $binding.original
        foreach ($reader in @($binding.readers)) {
            $observations=@(Get-CombinedValue $row.observations $reader)
            if ($observations.Count -ne 1 -or [int](Get-CombinedValue $row $counts[$reader]) -ne 1) { throw ('NIC restored reader cardinality differs: '+$binding.pnpDeviceId+'/'+$reader) }
            $actual=ConvertTo-CombinedComparableValue NIC mac (Get-CombinedValue $observations[0] 'value')
            if ($null -eq $actual -or $actual -cne $expected) { throw ('NIC original readback mismatch: pnp='+$binding.pnpDeviceId+' reader='+$reader+' expected='+$binding.original+' actual='+(Get-CombinedValue $observations[0] 'value')) }
        }
        if ($row.ndisOidStatus.ioctlSucceeded -ne $true -or [int]$row.ndisOidStatus.bytesReturned -ne 6 -or [int]$row.ndisOidStatus.win32Error -ne 0) { throw ('NIC original direct IOCTL failed: '+$binding.pnpDeviceId) }
        if ($row.dot11CurrentAddressCheck.applicable -eq $true -and ($row.dot11CurrentAddressCheck.ioctlSucceeded -ne $true -or (ConvertTo-CombinedComparableValue NIC mac $row.dot11CurrentAddressCheck.actual) -cne $expected)) { throw ('NIC original DOT11 mismatch: '+$binding.pnpDeviceId) }
    }
    return $true
}
$runRootFull=[IO.Path]::GetFullPath($RunRoot)
if (-not $runRootFull.StartsWith('C:\ProgramData\UEFILoaderRunner\runs\',[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $runRootFull -PathType Container)) { throw 'RunRoot must be an existing authenticated guest mailbox run directory.' }
$result=[ordered]@{SchemaVersion=1;RunId=(Split-Path -Leaf $runRootFull);Status='FAIL';Message='Combined VM phase did not complete.';ExpectedProbeVerdict=$ExpectedProbeVerdict}
$metadata=[ordered]@{schema='combined-vm-reader-phase.v1';phase='__PHASE__';startedUtc=[DateTime]::UtcNow.ToString('o');status='FAIL';hostExecutionPermitted=$false;storageLifecycleOwnedByOuterRunner=$true;nicMutationStarted=$false;nicRestoreErrors=@();capturedFiles=@()}
$shortRoot=$null
$journal=$null
$failure=$null
$mutationStarted=$false
$embeddedAssets=@()
try {
    if (-not $runRootFull.StartsWith('C:\ProgramData\UEFILoaderRunner\runs\',[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $runRootFull -PathType Container)) { throw 'RunRoot must be an existing authenticated guest mailbox run directory.' }
    $phase='__PHASE__'
    if ($phase -notin @('Original','Active','Restored')) { throw 'Invalid rendered combined phase.' }
    $profileJson=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('__PROFILE_BASE64__'))
    $profile=$profileJson | ConvertFrom-Json
    $systems=@(Get-CimInstance Win32_ComputerSystem -ErrorAction Stop)
    $products=@(Get-CimInstance Win32_ComputerSystemProduct -ErrorAction Stop)
    if ($systems.Count -ne 1 -or $products.Count -ne 1) { throw 'Expected one live VM system and product identity.' }
    $metadata.phaseIdentity=[ordered]@{expected=$profile.expectedIdentity;observed=[ordered]@{manufacturer=[string]$systems[0].Manufacturer;model=[string]$systems[0].Model;uuid=[string]$products[0].UUID}}
    Assert-CombinedVmPhaseIdentity $systems[0] $products[0] $profile.expectedIdentity
    # The disk model and PnP guard remain unchanged by SMBIOS substitutions.
    # They also prevent this VM-only template from running on the Intel host.
    $disks=@(Get-CimInstance Win32_DiskDrive -ErrorAction Stop | Where-Object DeviceID -CEQ '\\.\PHYSICALDRIVE0')
    if ($disks.Count -ne 1 -or [string]$disks[0].PNPDeviceID -cne [string]$profile.diskPnp -or [string]$disks[0].Model -notmatch '(?i)VMware') { throw 'Exact VMware NVMe disk identity is required before any VM action.' }
    $adapters=@(Get-CimInstance -Namespace root\StandardCimv2 -ClassName MSFT_NetAdapter -ErrorAction Stop)
    foreach ($pnp in @($profile.nicPnp)) {
        if ([string]$pnp -notmatch '^(?:ROOT\\NET\\[0-9]{4}|PCI\\VEN_15AD&DEV_07B0&)' -or @($adapters | Where-Object PnPDeviceID -CEQ $pnp).Count -ne 1) { throw ('Expected exact VMware VMnet/vmxnet3 identity: '+$pnp) }
    }
    $shortRoot=Join-Path 'C:\ProgramData\CI' ([Guid]::NewGuid().ToString('N').Substring(0,16))
    if (Test-Path -LiteralPath $shortRoot) { throw 'Refusing to reuse a VM phase directory.' }
    [void][IO.Directory]::CreateDirectory($shortRoot)
    $tools=Join-Path $shortRoot 't'
    [void][IO.Directory]::CreateDirectory($tools)
    $assetsJson=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('__ASSETS_BASE64__'))
    # Assignment preserves the parsed array itself on Windows PowerShell 5.1;
    # wrapping the pipeline in @() would introduce an extra array level.
    $embeddedAssets=ConvertFrom-Json -InputObject $assetsJson
    foreach ($asset in $embeddedAssets) {
        if ([string]$asset.name -notmatch '\A[A-Za-z0-9_.-]+\z') { throw 'Invalid embedded asset name.' }
        $assetPath=Join-Path $tools ([string]$asset.name)
        $sourcePath=Get-CombinedAssetProperty $asset 'sourcePath'
        if ($sourcePath) {
            if ([string]$sourcePath -notmatch '\A\\\\vmware-host\\Shared Folders\\CombinedFixture\\[0-9a-f]{64}\\[A-Za-z0-9_.-]+\z' -or [IO.Path]::GetFileName([string]$sourcePath) -cne [string]$asset.name) { throw 'VM fixture source must be the exact read-only HGFS asset path.' }
            $bytes=[IO.File]::ReadAllBytes([string]$sourcePath)
        } else { $bytes=[Convert]::FromBase64String([string]$asset.base64) }
        try { [IO.File]::WriteAllBytes($assetPath,$bytes) } finally { [Array]::Clear($bytes,0,$bytes.Length) }
        if ((Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash -cne [string]$asset.sha256) { throw ('Embedded asset hash mismatch: '+$asset.name) }
    }
    $metadata.inputsVerified=@(foreach($asset in $embeddedAssets){[ordered]@{name=[string]$asset.name;sha256=[string]$asset.sha256;guestPath=(Join-Path $tools ([string]$asset.name))}})
    $env:TEMP=Join-Path $shortRoot 'tmp'
    $env:TMP=$env:TEMP
    [void][IO.Directory]::CreateDirectory($env:TEMP)
    . (Join-Path $tools 'readback.ps1')
    . (Join-Path $tools 'address-functions.ps1')
    . (Join-Path $tools 'storage-read.ps1')
    $metadata.secureBoot=Confirm-SecureBootUEFI
    $metadata.codeIntegrity=[StorageHostReadNative]::ReadCodeIntegrityOptions()
    $mapPath=Join-Path $tools 'locked.tsv'
    $bindings=New-CombinedBindingMap (Get-CombinedMapRows $mapPath)
    $mapHash=(Get-FileHash -LiteralPath $mapPath -Algorithm SHA256).Hash
    if ($mapHash -cne [string]$profile.mapSha256) { throw 'VM fixture map changed while deploying the phase.' }
    $nicBindings=@($bindings.bindings | Where-Object category -CEQ 'NIC')
    if ($phase -ceq 'Active') {
        if ($metadata.secureBoot) { throw 'The VM storage test phase requires Secure Boot OFF.' }
        if ($metadata.codeIntegrity.success -ne $true -or ([uint32]$metadata.codeIntegrity.options -band 2) -ne 2) { throw 'Effective kernel test-signing must be enabled for the active VM storage phase.' }
        $services=@(Get-CimInstance Win32_SystemDriver -Filter "Name='client_storage_filter'" -ErrorAction Stop)
        if ($services.Count -ne 1 -or [string]$services[0].State -cne 'Running' -or [string]$services[0].StartMode -cne 'Boot') { throw 'The preinstalled boot-start storage filter must be running for the active VM phase.' }
        $snapshots=@(Get-HostAdapterSnapshots $nicBindings)
        if ($snapshots.Count -ne 4) { throw 'All four original NIC snapshots are required before apply.' }
        $journal=[ordered]@{schema='combined-vm-nic-transaction.v1';journalPath=(Join-Path $shortRoot 'nic-journal.json');adapterSnapshots=$snapshots;adapterProgress=@();mutationStarted=$false;linkRestoreChecks=@();updatedUtc=$null}
        foreach ($snapshot in $snapshots) { $journal.adapterProgress+=@([ordered]@{pnpDeviceId=[string]$snapshot.pnpDeviceId;state='original-snapshotted'}) }
        Write-HostJournal $journal
        foreach ($snapshot in $snapshots) {
            $progress=@($journal.adapterProgress | Where-Object pnpDeviceId -CEQ $snapshot.pnpDeviceId)[0]
            $null=Invoke-HostAdapterRegistryApply -Snapshot $snapshot -Journal $journal -Progress $progress -MutationStarted ([ref]$mutationStarted)
        }
    }
    $expectedMode=if($phase -ceq 'Active'){'Replacement'}else{'Original'}
    $readback=Invoke-CombinedReadback -RepositoryRoot $shortRoot -OutputDirectory (Join-Path $shortRoot 'spoof-handoff\reports\o') -ExpectedMode $expectedMode -SmbiosReaderPath (Join-Path $tools 'smbios_windows_readback.exe') -NicReaderPath (Join-Path $tools 'nic-read.ps1') -NicNativeExePath (Join-Path $tools 'nic_native_read.exe') -StorageReaderPath (Join-Path $tools 'storage-read.ps1') -LockedMapPath $mapPath
    $metadata.combinedReadback=$readback
    $metadata.combinedReadbackContent=Get-Content -LiteralPath $readback.reportPath -Raw | ConvertFrom-Json
    $metadata.bootIdentity=$metadata.combinedReadbackContent.bootIdentity
    if ($readback.status -cne 'PASS') { throw 'Combined reader phase did not pass.' }
}
catch { $failure=$_.Exception.ToString() }
finally {
    $metadata.nicMutationStarted=[bool]$mutationStarted
    if ($null -ne $journal -and $mutationStarted) {
        try { $metadata.nicRestoreErrors=@(Restore-HostAdapters $journal) }
        catch { $metadata.nicRestoreErrors+=@([ordered]@{stage='restore-nic-transaction';error=$_.Exception.ToString()}) }
        try {
            $nicDirectory=Join-Path $shortRoot 'n'
            $output=@(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tools 'nic-read.ps1') -NativeExe (Join-Path $tools 'nic_native_read.exe') -OutputDirectory $nicDirectory -LockedMap (Join-Path $tools 'locked.tsv') -ExpectedMode Original 2>&1 | ForEach-Object{[string]$_})
            $exitCode=$LASTEXITCODE
            $evidenceLines=@($output | Where-Object{$_ -match '^READ-ONLY NIC EVIDENCE\s+'})
            if ($exitCode -ne 0 -or $evidenceLines.Count -ne 1) { throw ('NIC originals after restore failed; exit='+$exitCode+'; output='+($output -join ' | ')) }
            $nicPath=($evidenceLines[0] -replace '^READ-ONLY NIC EVIDENCE\s+','').Trim()
            $nicOriginals=Get-Content -LiteralPath $nicPath -Raw | ConvertFrom-Json
            $null=Assert-CombinedVmNicOriginalReport -Report $nicOriginals -Bindings $nicBindings
            $metadata.nicOriginalReadback=[ordered]@{path=$nicPath;sha256=(Get-FileHash -LiteralPath $nicPath -Algorithm SHA256).Hash;content=$nicOriginals}
        }
        catch { $metadata.nicRestoreErrors+=@([ordered]@{stage='read-nic-originals';error=$_.Exception.ToString()}) }
    }
    if (@($metadata.nicRestoreErrors).Count -gt 0 -and [string]::IsNullOrWhiteSpace($failure)) { $failure='NIC restoration or original readback failed; see nicRestoreErrors.' }
    if ($null -ne $shortRoot) {
        try {
            $captured=[Collections.Generic.List[object]]::new()
            $fileIndex=0
            foreach ($file in @(Get-ChildItem -LiteralPath $shortRoot -File -Recurse -ErrorAction Stop | Sort-Object FullName)) {
                # Inputs are already pinned in the authenticated render request
                # and readback. Do not upload them or compiler scratch binaries
                # again through the bounded Guest Variables evidence channel.
                if ($file.FullName.StartsWith(($tools.TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase) -or $file.FullName.StartsWith(((Join-Path $shortRoot 'tmp').TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)) { continue }
                # Flatten evidence names so the authenticated archive stays below
                # native and PowerShell path limits, preserving the source map.
                $fileIndex++
                $leaf='f.'+$fileIndex.ToString('D4')+$file.Extension
                $destination=Join-Path $runRootFull $leaf
                if (Test-Path -LiteralPath $destination) { throw ('Evidence destination already exists: '+$destination) }
                Copy-Item -LiteralPath $file.FullName -Destination $destination -ErrorAction Stop
                $sourceHash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
                if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -cne $sourceHash) { throw ('Evidence copy hash mismatch: '+$file.FullName) }
                $captured.Add([ordered]@{guestPath=$file.FullName;archiveLeaf=$leaf;sha256=$sourceHash;bytes=[long]$file.Length})
            }
            $metadata.capturedFiles=@($captured.ToArray())
            # The fixed mailbox archives kernel-metadata.json only. Carry the
            # exact raw children inside it rather than relying on extra files.
            $metadata.rawEvidenceBundle=New-CombinedVmRawBundle -Directory $runRootFull -Files @($captured.ToArray())
            if ([string]::IsNullOrWhiteSpace($failure) -and @($metadata.nicRestoreErrors).Count -eq 0) {
                $resolvedShortRoot=[IO.Path]::GetFullPath($shortRoot).TrimEnd('\')
                if ((Split-Path -Parent $resolvedShortRoot) -cne 'C:\ProgramData\CI' -or (Split-Path -Leaf $resolvedShortRoot) -notmatch '\A[0-9a-f]{16}\z') { throw 'Temporary VM evidence cleanup refused an unexpected absolute path.' }
                $cleanupItems=@(Get-Item -LiteralPath $resolvedShortRoot) + @(Get-ChildItem -LiteralPath $resolvedShortRoot -Recurse -Force)
                if (@($cleanupItems | Where-Object {($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0}).Count -ne 0) { throw 'Temporary VM evidence cleanup refused a reparse point.' }
                Remove-Item -LiteralPath $resolvedShortRoot -Recurse -Force -ErrorAction Stop
                $metadata.temporaryEvidenceRemoved=-not (Test-Path -LiteralPath $resolvedShortRoot)
                if (-not $metadata.temporaryEvidenceRemoved) { throw 'Temporary VM evidence directory remains after cleanup.' }
            } else {
                $metadata.temporaryEvidenceRemoved=$false
                $metadata.temporaryEvidencePreservedForOuterRecovery=$shortRoot
            }
        }
        catch { if ([string]::IsNullOrWhiteSpace($failure)) { $failure=$_.Exception.ToString() } else { $failure+='; evidence-copy error: '+$_.Exception.ToString() } }
    }
    $metadata.failure=$failure
    $metadata.completedUtc=[DateTime]::UtcNow.ToString('o')
    $metadata.fullStorageAndSmbiosRestorationClaimed=$false
    if ([string]::IsNullOrWhiteSpace($failure)) {
        $metadata.status='PASS'
        $result.Status='PASS'
        $result.Message='Combined reader phase passed; active-phase NIC values restored. Storage, EFI, policy and normal-boot restoration belong to the outer VM lifecycle.'
    } else { $result.Message=$failure }
    [IO.File]::WriteAllText((Join-Path $runRootFull 'kernel-metadata.json'),($metadata | ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $runRootFull 'stdout.txt'),[string]$result.Message,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $runRootFull 'stderr.txt'),$(if($result.Status -ceq 'PASS'){''}else{[string]$result.Message}),[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $runRootFull 'result.json'),($result | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
}
if ($result.Status -cne 'PASS') { exit 1 }
