Set-StrictMode -Version 2.0

$script:StorageHostOriginalSerial = '0000_0000_0100_0000_E4D2_5CDF_3227_5501.'
$script:StorageHostReplacementSerial = '1111_1111_1111_1111_A1B2_C3D4_5566_7788.'
$script:StorageHostPnpInstanceId = 'SCSI\DISK&VEN_NVME&PROD_INTEL_SSDPEKNU01\5&8432851&0&000000'
$script:StorageHostHardwareId = 'SCSI\DiskNVMe_________________________INTEL_SSDPEKNU010TZ____002C'

function Get-StorageHostAllowedTransition {
    param([Parameter(Mandatory=$true)][string]$Phase,[Parameter(Mandatory=$true)][string]$Action)
    if ($Action -ceq 'RecoverOriginal' -and $Phase -cne 'Complete') { return 'RecoveryReconciled' }
    $key = $Phase + '|' + $Action
    $transitions = @{
        'New|Prepare' = 'Prepared'
        'Prepared|EnableTestMode' = 'PolicyPendingReboot'
        'EnablingTestMode|EnableTestMode' = 'PolicyPendingReboot'
        'PolicyPendingReboot|InstallTransparent' = 'TransparentInstallPendingReboot'
        'TransparentInstallInProgress|Remove' = 'TransparentRemovalPendingReboot'
        'TransparentInstallInProgress|ResumeInstall' = 'TransparentInstallPendingReboot'
        'TransparentInstallPendingReboot|Remove' = 'TransparentRemovalPendingReboot'
        'TransparentInstallPendingReboot|VerifyTransparent' = 'TransparentVerified'
        'TransparentVerified|Remove' = 'TransparentRemovalPendingReboot'
        'TransparentRemovalInProgress|Remove' = 'TransparentRemovalPendingReboot'
        'TransparentRemovalPendingReboot|VerifyOriginal' = 'TransparentRemoved'
        'TransparentRemoved|InstallReplacement' = 'ReplacementInstallPendingReboot'
        'ReplacementInstallInProgress|Remove' = 'ReplacementRemovalPendingReboot'
        'ReplacementInstallInProgress|ResumeInstall' = 'ReplacementInstallPendingReboot'
        'ReplacementInstallPendingReboot|Remove' = 'ReplacementRemovalPendingReboot'
        'ReplacementInstallPendingReboot|VerifyReplacement' = 'ReplacementVerified'
        'ReplacementVerified|Remove' = 'ReplacementRemovalPendingReboot'
        'ReplacementRemovalInProgress|Remove' = 'ReplacementRemovalPendingReboot'
        'ReplacementRemovalPendingReboot|VerifyOriginal' = 'ReplacementRemoved'
        'ReplacementRemoved|RestorePolicy' = 'PolicyRestorePendingReboot'
        'RecoveryReconciled|RestorePolicy' = 'PolicyRestorePendingReboot'
        'Prepared|RestorePolicy' = 'PolicyRestorePendingReboot'
        'PolicyPendingReboot|RestorePolicy' = 'PolicyRestorePendingReboot'
        'EnablingTestMode|RestorePolicy' = 'PolicyRestorePendingReboot'
        'RestoringPolicy|RestorePolicy' = 'PolicyRestorePendingReboot'
        'TransparentRemoved|RestorePolicy' = 'PolicyRestorePendingReboot'
        'PolicyRestorePendingReboot|FinalVerify' = 'Complete'
    }
    if (-not $transitions.ContainsKey($key)) { throw "Action $Action is not allowed while phase is $Phase." }
    return $transitions[$key]
}

function Assert-StorageHostStartType {
    param([Parameter(Mandatory=$true)]$Manifest)
    if ([int]$Manifest.startType -ne 0) {
        throw ('Release StartType must be boot-start 0; actual=' + $Manifest.startType)
    }
}

function Assert-StorageHostReceipt {
    param([Parameter(Mandatory=$true)]$Receipt)
    if ([string]$Receipt.path -cne 'signature-verdict.json') { throw 'Pinned signature receipt path must be signature-verdict.json.' }
    if ([string]$Receipt.sha256 -notmatch '^[A-Fa-f0-9]{64}$') { throw 'Pinned signature receipt SHA-256 is missing or invalid.' }
    return $true
}

function Assert-StorageHostManifest {
    param([Parameter(Mandatory=$true)]$Manifest)
    if ($Manifest.schema -cne 'storage-host-release.v1') { throw 'Release schema is not storage-host-release.v1.' }
    if ($Manifest.thumbprint -cne '59DBD6780CFE60C020B3F4511211FEC22C397B85') { throw 'Release signer thumbprint differs from the pinned certificate.' }
    if ($Manifest.certSha256 -cne 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE') { throw 'Release certificate SHA-256 differs from the pinned certificate.' }
    if ($Manifest.originalSerial -cne $script:StorageHostOriginalSerial) { throw 'Release original serial differs from the locked host value.' }
    if ($Manifest.replacementSerial -cne $script:StorageHostReplacementSerial) { throw 'Release replacement serial differs from the locked value.' }
    if ($Manifest.pnpInstanceId -cne $script:StorageHostPnpInstanceId) { throw 'Release PnP instance ID differs from the pinned physical target.' }
    if ($Manifest.hardwareId -cne $script:StorageHostHardwareId) { throw 'Release hardware ID differs from the pinned INF target.' }
    Assert-StorageHostStartType $Manifest
    if ($Manifest.certificateFile -cne 'hypersinv.cer') { throw 'Release certificate file name is not pinned.' }
    if ($null -eq $Manifest.packages.transparent -or $null -eq $Manifest.packages.replacement) { throw 'Release must contain transparent and replacement packages.' }
    foreach ($packageName in @('transparent','replacement')) {
        $package = $Manifest.packages.$packageName
        if ([string]::IsNullOrWhiteSpace([string]$package.directory)) { throw "Package $packageName has no directory." }
        if (@($package.files).Count -ne 3) { throw "Package $packageName must list exactly SYS, INF and CAT." }
        $names = @($package.files | ForEach-Object { [string]$_.name } | Sort-Object)
        if (($names -join '|') -cne 'client_storage_filter.cat|client_storage_filter.inf|client_storage_filter.sys') {
            throw "Package $packageName file names differ from the three allowed files."
        }
        foreach ($file in $package.files) {
            if ([string]$file.sha256 -notmatch '^[A-Fa-f0-9]{64}$') { throw "Package $packageName has an invalid SHA-256 for $($file.name)." }
        }
    }
    return $true
}

function Test-StorageHostOwnedInf {
    param([string]$PublishedName,[string]$ExpectedSha256,[string]$ActualSha256,[string]$ExpectedPnpInstanceId,[string]$ActualPnpInstanceId)
    return ($PublishedName -match '^oem[0-9]+\.inf$' -and
        $ExpectedSha256 -match '^[A-Fa-f0-9]{64}$' -and
        $ExpectedSha256 -ceq $ActualSha256 -and
        $ExpectedPnpInstanceId -ceq $ActualPnpInstanceId)
}

function Test-StorageHostRecoveryDisk {
    param([int[]]$RecoveryDiskNumbers,[int[]]$WindowsDiskNumbers)
    if (@($RecoveryDiskNumbers).Count -eq 0) { return $false }
    foreach ($number in $RecoveryDiskNumbers) {
        if (@($WindowsDiskNumbers) -contains $number) { return $false }
    }
    return $true
}

function Get-StorageHostOfflineInfDecision {
    param([Parameter(Mandatory=$true)][int]$TransparentCompareExit,[Nullable[int]]$ReplacementCompareExit,[Nullable[int]]$DismExitCode)
    if ($TransparentCompareExit -eq 2) { return 'ComparisonFailure' }
    if ($TransparentCompareExit -eq 0) {
        if ($null -eq $DismExitCode) { return 'RemoveTransparent' }
        if ($DismExitCode -eq 0 -or $DismExitCode -eq 3010) { return 'RemovedTransparent' }
        return 'DismFailure'
    }
    if ($TransparentCompareExit -ne 1) { return 'ComparisonFailure' }
    if ($null -eq $ReplacementCompareExit) { return 'CompareReplacement' }
    if ($ReplacementCompareExit -eq 2) { return 'ComparisonFailure' }
    if ($ReplacementCompareExit -eq 0) {
        if ($null -eq $DismExitCode) { return 'RemoveReplacement' }
        if ($DismExitCode -eq 0 -or $DismExitCode -eq 3010) { return 'RemovedReplacement' }
        return 'DismFailure'
    }
    if ($ReplacementCompareExit -eq 1) { return 'NoMatch' }
    return 'ComparisonFailure'
}

function Get-StorageHostStackLines {
    param([string[]]$PnpUtilOutput)
    $lines = [Collections.Generic.List[string]]::new()
    $inStack = $false
    foreach ($line in $PnpUtilOutput) {
        if (-not $inStack) {
            if ($line -match '^\s*(?:Device\s+)?Stack\s*:\s*(.*)$') {
                $inStack = $true
                if (-not [string]::IsNullOrWhiteSpace($Matches[1])) { $lines.Add($Matches[1].Trim()) }
            }
            continue
        }
        if ([string]::IsNullOrWhiteSpace($line) -or $line -match '^\s*(?:Matching Drivers|Device Instance|Driver Name|Driver Version|Device Description)\s*:') { break }
        $lines.Add($line.Trim())
    }
    return @($lines.ToArray())
}

function Test-StorageHostStackContainsFilter {
    param([string[]]$PnpUtilOutput)
    $stack = @(Get-StorageHostStackLines -PnpUtilOutput $PnpUtilOutput)
    return (@($stack | Where-Object { $_ -match '(?i)(?:^|\\|\s)client_storage_filter(?:\.sys)?(?:\s|$)' }).Count -gt 0)
}

function Get-StorageHostTestSigningElement {
    param([string[]]$BcdLines)
    foreach ($line in $BcdLines) {
        if ($line -match '^\s*testsigning\s+(Yes|No)\s*$') {
            return [pscustomobject]@{ exists = $true; value = $Matches[1].ToLowerInvariant() }
        }
    }
    return [pscustomobject]@{ exists = $false; value = $null }
}

function Test-StorageHostPolicyRestore {
    param($Expected,$Actual,[bool]$ExpectedRoot,[bool]$ActualRoot,[bool]$ExpectedPublisher,[bool]$ActualPublisher,[int]$ExpectedCodeIntegrityOptions,[int]$ActualCodeIntegrityOptions)
    if ([bool]$Expected.exists -ne [bool]$Actual.exists) { return $false }
    if ([bool]$Expected.exists -and [string]$Expected.value -cne [string]$Actual.value) { return $false }
    if ($ExpectedRoot -ne $ActualRoot -or $ExpectedPublisher -ne $ActualPublisher) { return $false }
    if (($ExpectedCodeIntegrityOptions -band 0x2) -ne ($ActualCodeIntegrityOptions -band 0x2)) { return $false }
    return $true
}

function Test-StorageHostTransparentProof {
    param([Parameter(Mandatory=$true)]$Proof)
    if ([string]$Proof.expectedMode -cne 'Original' -or [string]$Proof.result -cne 'PASS') { return $false }
    if (@($Proof.mismatches).Count -ne 0) { return $false }
    if ([string]$Proof.nativeSerial -cne $script:StorageHostOriginalSerial -or [string]$Proof.wmiSerial -cne $script:StorageHostOriginalSerial) { return $false }
    if (-not [bool]$Proof.fileIo.match -or -not [bool]$Proof.filterService.present) { return $false }
    if (@($Proof.filterService.services | Where-Object { [string]$_.State -ceq 'Running' }).Count -eq 0) { return $false }
    if ($null -eq $Proof.filterService.registry -or $null -eq $Proof.filterService.registry.start -or [int]$Proof.filterService.registry.start -ne 0) { return $false }
    if (@($Proof.actualStack | Where-Object { [string]$_ -match '(?i)(?:^|\\|\s)client_storage_filter(?:\.sys)?(?:\s|$)' }).Count -eq 0) { return $false }
    if ([string]::IsNullOrWhiteSpace([string]$Proof.currentBootTime) -or [string]::IsNullOrWhiteSpace([string]$Proof.previousBootTime)) { return $false }
    return $true
}

function Assert-StorageHostExpectedSerial {
    param([string]$Mode,[string]$NativeSerial,[string]$WmiSerial)
    $expected = if ($Mode -ceq 'Replacement') { $script:StorageHostReplacementSerial } else { $script:StorageHostOriginalSerial }
    $mismatches = @()
    if ($NativeSerial -cne $expected) { $mismatches += "native serial expected=$expected actual=$NativeSerial" }
    if ($WmiSerial -cne $expected) { $mismatches += "WMI serial expected=$expected actual=$WmiSerial" }
    if ($mismatches.Count -gt 0) { throw ($mismatches -join '; ') }
    return $expected
}

Export-ModuleMember -Function Get-StorageHostAllowedTransition,Assert-StorageHostStartType,Assert-StorageHostReceipt,Assert-StorageHostManifest,Test-StorageHostOwnedInf,Test-StorageHostRecoveryDisk,Get-StorageHostOfflineInfDecision,Get-StorageHostStackLines,Test-StorageHostStackContainsFilter,Get-StorageHostTestSigningElement,Test-StorageHostPolicyRestore,Test-StorageHostTransparentProof,Assert-StorageHostExpectedSerial
