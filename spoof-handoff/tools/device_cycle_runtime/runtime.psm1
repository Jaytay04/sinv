Set-StrictMode -Version 2.0

function Get-DeviceCycleProperty {
    param([AllowNull()][object]$InputObject,[Parameter(Mandatory=$true)][string]$Name)
    if ($null -eq $InputObject) { return $null }
    foreach ($property in $InputObject.PSObject.Properties) { if ($property.Name -ieq $Name) { return $property.Value } }
    return $null
}

function Get-DeviceCycleFullPath {
    param([Parameter(Mandatory=$true)][string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'A required path is empty.' }
    return [IO.Path]::GetFullPath($Path)
}

function Test-DeviceCyclePathWithin {
    param([Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)][string]$Boundary)
    $fullPath = Get-DeviceCycleFullPath $Path
    $fullBoundary = (Get-DeviceCycleFullPath $Boundary).TrimEnd([IO.Path]::DirectorySeparatorChar,[IO.Path]::AltDirectorySeparatorChar)
    return $fullPath.StartsWith($fullBoundary + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)
}

function Assert-DeviceCycleNoReparsePath {
    param([Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)][string]$Boundary)
    $fullPath = Get-DeviceCycleFullPath $Path
    $fullBoundary = Get-DeviceCycleFullPath $Boundary
    if (-not ($fullPath.Equals($fullBoundary,[StringComparison]::OrdinalIgnoreCase) -or (Test-DeviceCyclePathWithin $fullPath $fullBoundary))) {
        throw "Path escapes its boundary: $fullPath"
    }
    $cursor = $fullPath
    while ($cursor -and ($cursor.Equals($fullBoundary,[StringComparison]::OrdinalIgnoreCase) -or $cursor.StartsWith($fullBoundary.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase))) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point is not allowed in a protected path: $cursor" }
        }
        if ($cursor.Equals($fullBoundary,[StringComparison]::OrdinalIgnoreCase)) { break }
        $cursor = Split-Path -Parent $cursor
    }
}

function Get-DeviceCycleTransitionTable {
    return @{
        Prepared=@('Enabling','RollingBack')
        Enabling=@('PolicyBootPending','RecoveryBootPending','RollingBack','RolledBackAwaitingSecureBootOn')
        PolicyBootPending=@('Running','RecoveryBootPending','RollingBack')
        Running=@('RemovalBootPending','RecoveryBootPending')
        RemovalBootPending=@('AwaitingSecureBootOn','RecoveryBootPending','RollingBack')
        RecoveryBootPending=@('AwaitingSecureBootOn','RolledBackAwaitingSecureBootOn','RecoveryBootPending')
        RollingBack=@('RemovalBootPending','RecoveryBootPending','RolledBackAwaitingSecureBootOn')
        AwaitingSecureBootOn=@('Complete','RecoveryBootPending')
        RolledBackAwaitingSecureBootOn=@('RolledBack','RecoveryBootPending')
        Complete=@()
        RolledBack=@()
    }
}

function Set-DeviceCyclePhase {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)][string]$NextPhase,[switch]$AllowSame)
    $table = Get-DeviceCycleTransitionTable
    $current = [string]$State.phase
    if ($AllowSame -and $current -ceq $NextPhase) { return $State }
    if (-not $table.ContainsKey($current) -or $table[$current] -notcontains $NextPhase) { throw "Invalid device-cycle transition: $current -> $NextPhase" }
    $State.phase = $NextPhase
    $State | Add-Member -MemberType NoteProperty -Name updatedUtc -Value ([DateTime]::UtcNow.ToString('o')) -Force
    return $State
}

function Get-DeviceCycleBcdSetting {
    param([Parameter(Mandatory=$true)][AllowEmptyString()][string[]]$Enumeration,[Parameter(Mandatory=$true)][string]$Name)
    $pattern = '^\s*' + [regex]::Escape($Name) + '\s+(Yes|No)\s*$'
    foreach ($line in $Enumeration) {
        if ($line -match $pattern) { return [pscustomobject]@{present=$true;value=($Matches[1] -ieq 'Yes');raw=$line.Trim()} }
    }
    return [pscustomobject]@{present=$false;value=$null;raw=$null}
}

function Get-DeviceCycleBcdRestoreArguments {
    param([Parameter(Mandatory=$true)][string]$ObjectId,[Parameter(Mandatory=$true)]$Original)
    if ([bool]$Original.present) { return @('/set',$ObjectId,'testsigning',$(if ([bool]$Original.value) {'on'} else {'off'})) }
    return @('/deletevalue',$ObjectId,'testsigning')
}

function Get-DeviceCycleTrustRestorePlan {
    param([Parameter(Mandatory=$true)]$OriginalMembership)
    $removals = @()
    foreach ($store in @('Cert:\LocalMachine\Root','Cert:\LocalMachine\TrustedPublisher')) {
        $entry = @($OriginalMembership | Where-Object { $_.store -ceq $store })
        if ($entry.Count -ne 1) { throw "Original trust membership must contain exactly one row for $store." }
        if (-not [bool]$entry[0].certificatePresent) { $removals += $store }
    }
    return $removals
}

function Test-DeviceCycleOwnedPackageDelta {
    param([Parameter(Mandatory=$true)][AllowEmptyCollection()][object[]]$Before,[Parameter(Mandatory=$true)][AllowEmptyCollection()][object[]]$After,[Parameter(Mandatory=$true)][string]$InfName,[Parameter(Mandatory=$true)][string]$InfSha256)
    $beforePublished = @($Before | ForEach-Object { [string]$_.publishedName })
    $owned = @()
    $errors = @()
    foreach ($row in $After) {
        if ($beforePublished -contains [string]$row.publishedName) { continue }
        if ([IO.Path]::GetFileName([string]$row.originalFile) -ine $InfName) { continue }
        $actual = [string]$row.infSha256
        if ($actual -cne $InfSha256) { $errors += ("owned package INF hash expected={0} actual={1} published={2}" -f $InfSha256,$actual,$row.publishedName); continue }
        if ([string]$row.publishedName -notmatch '^oem[0-9]+\.inf$') { $errors += ('owned package published name is invalid: '+$row.publishedName); continue }
        $owned += $row
    }
    return [pscustomobject]@{owned=$owned;errors=$errors;pass=($errors.Count -eq 0 -and $owned.Count -le 1)}
}

function Test-DeviceCycleProcessResult {
    param([Parameter(Mandatory=$true)]$Result)
    if ([int]$Result.exitCode -ne 0) {
        throw ("Command failed: {0} {1}; exit={2}; stdout={3}; stderr={4}" -f $Result.executable,($Result.arguments -join ' '),$Result.exitCode,$Result.stdoutPath,$Result.stderrPath)
    }
    return $Result
}

function Get-DeviceCyclePnpExitDisposition {
    param([Parameter(Mandatory=$true)][int]$ExitCode)
    if ($ExitCode -eq 0) { return [pscustomobject]@{accepted=$true;rebootRequired=$false;exitCode=$ExitCode} }
    if ($ExitCode -eq 3010) { return [pscustomobject]@{accepted=$true;rebootRequired=$true;exitCode=$ExitCode} }
    return [pscustomobject]@{accepted=$false;rebootRequired=$false;exitCode=$ExitCode}
}

function Get-DeviceCycleCleanupDecision {
    param(
        [Parameter(Mandatory=$true)][bool]$FiltersAbsent,
        [Parameter(Mandatory=$true)][bool]$NativeOriginals,
        [Parameter(Mandatory=$true)][bool]$WmiOriginals,
        [Parameter(Mandatory=$true)][bool]$RoundTripProven
    )
    $machineRestored = ($FiltersAbsent -and $NativeOriginals -and $WmiOriginals)
    return [pscustomobject]@{
        cleanupMayProceed=$machineRestored
        machineRestored=$machineRestored
        liveRoundTripProven=($machineRestored -and $RoundTripProven)
        ready=($machineRestored -and $RoundTripProven)
    }
}

function Get-DeviceCycleEnableFailureDisposition {
    param(
        [Parameter(Mandatory=$true)][bool]$StorageInstallStarted,
        [Parameter(Mandatory=$true)][int]$OwnedStoragePackageCount,
        [Parameter(Mandatory=$true)][bool]$OwnedStartupTaskPresent
    )
    $storageMayBeActive = ($StorageInstallStarted -or $OwnedStoragePackageCount -gt 0)
    if (-not $storageMayBeActive) {
        return [pscustomobject]@{immediateRollback=$true;recoveryBootRequired=$false;action='RestoreBcdTrustAndTask';reason='No storage package install was attempted or attributed to this run.'}
    }
    if ($OwnedStartupTaskPresent) {
        return [pscustomobject]@{immediateRollback=$false;recoveryBootRequired=$true;action='RetainRecoveryTaskTrustAndPolicy';reason='A run-owned storage package may be active and the signed recovery task is present.'}
    }
    return [pscustomobject]@{immediateRollback=$false;recoveryBootRequired=$false;action='ManualRecoveryRequired';reason='A storage package may be active but no verified run-owned recovery task is available.'}
}

function Get-DeviceCycleFirmwareRecordsToCompare {
    param([Parameter(Mandatory=$true)][AllowEmptyCollection()][object[]]$Records)
    $rows = @($Records)
    if ($rows.Count -eq 0) { throw 'The fresh firmware snapshot contains no records.' }
    $recordNames = @($rows | ForEach-Object { [string](Get-DeviceCycleProperty $_ 'name') })
    $requiredNames = @('SecureBoot','BootOrder','BootNext')
    $missing = @($requiredNames | Where-Object { $recordNames -notcontains $_ })
    if ($missing.Count -gt 0) { throw ('Fresh firmware snapshot omits mandatory variable records: '+($missing -join ', ')) }
    return @($rows | Where-Object { [string](Get-DeviceCycleProperty $_ 'name') -ine 'BootCurrent' })
}

function Get-DeviceCycleRollbackDisposition {
    param([Parameter(Mandatory=$true)][string]$Phase,[AllowNull()][string]$NextAction)
    if ($Phase -in @('Complete','RolledBack','AwaitingSecureBootOn','RolledBackAwaitingSecureBootOn')) {
        return [pscustomobject]@{noOp=$true;noMutation=$false;phase=$Phase;nextAction=$NextAction;transitionPhases=@()}
    }
    if ($Phase -ceq 'Prepared') {
        return [pscustomobject]@{noOp=$false;noMutation=$true;phase=$Phase;nextAction=$NextAction;transitionPhases=@('RollingBack','RolledBackAwaitingSecureBootOn')}
    }
    return [pscustomobject]@{noOp=$false;noMutation=$false;phase=$Phase;nextAction=$NextAction;transitionPhases=@()}
}

function Test-DeviceCyclePreTrustSignatureStatus {
    param(
        [Parameter(Mandatory=$true)][string]$Status,
        [AllowEmptyString()][string]$StatusMessage,
        [Parameter(Mandatory=$true)][string]$ExpectedThumbprint,
        [AllowEmptyString()][string]$ActualThumbprint
    )
    $rootUntrustedMessage = 'A certificate chain processed, but terminated in a root certificate which is not trusted by the trust provider'
    if ([string]::IsNullOrWhiteSpace($ActualThumbprint) -or $ActualThumbprint -ine $ExpectedThumbprint) {
        return [pscustomobject]@{accepted=$false;reason='Signer thumbprint does not match the pinned certificate.'}
    }
    if ($Status -in @('Valid','NotTrusted')) {
        return [pscustomobject]@{accepted=$true;reason='Signer matches the pinned certificate and Authenticode status is Valid or NotTrusted.'}
    }
    if ($Status -ceq 'UnknownError' -and $StatusMessage -ceq $rootUntrustedMessage) {
        return [pscustomobject]@{accepted=$true;reason='The pinned signer is validly recorded, and current trust is absent with the exact expected untrusted-root message.'}
    }
    return [pscustomobject]@{accepted=$false;reason=('Unexpected Authenticode status/message: status={0}; message={1}' -f $Status,$StatusMessage)}
}

function Get-DeviceCycleNicInstallDisposition {
    param(
        [Parameter(Mandatory=$true)][int]$ExitCode,
        [Parameter(Mandatory=$true)][bool]$ComponentBeforePresent,
        [AllowNull()][object]$ComponentAfterPresent,
        [AllowNull()][object]$PackageDelta,
        [Parameter(Mandatory=$true)][string]$InfName,
        [Parameter(Mandatory=$true)][string]$InfPath,
        [Parameter(Mandatory=$true)][string]$InfSha256,
        [AllowEmptyString()][string]$CommandError,
        [AllowEmptyString()][string]$InventoryError,
        [AllowEmptyString()][string]$ComponentError
    )
    $componentKnown = ($null -ne $ComponentAfterPresent -and [string]::IsNullOrWhiteSpace($ComponentError))
    $componentAfter = ($componentKnown -and [bool]$ComponentAfterPresent)
    $componentOwned = (-not $ComponentBeforePresent -and $componentAfter)
    $ownedPackages = @()
    if ($null -ne $PackageDelta) {
        foreach ($row in @($PackageDelta.owned)) {
            if ([IO.Path]::GetFileName([string]$row.originalFile) -ine $InfName) { continue }
            if ([string]$row.infSha256 -cne $InfSha256) { continue }
            if ([string]$row.publishedName -notmatch '^oem[0-9]+\.inf$') { continue }
            $ownedPackages += [ordered]@{publishedName=[string]$row.publishedName;originalFile=[string]$row.originalFile;infSha256=$InfSha256;infPath=$InfPath}
        }
    }
    $failures = New-Object 'System.Collections.Generic.List[string]'
    if (-not [string]::IsNullOrWhiteSpace($CommandError)) { $failures.Add(('netcfg launch error: '+$CommandError)) }
    if ($ExitCode -ne 0) { $failures.Add(('netcfg install exit={0}' -f $ExitCode)) }
    if (-not [string]::IsNullOrWhiteSpace($InventoryError)) { $failures.Add(('post-install Driver Store query failed: '+$InventoryError)) }
    if (-not [string]::IsNullOrWhiteSpace($ComponentError)) { $failures.Add(('post-install NetCfg query failed: '+$ComponentError)) }
    if ($ComponentBeforePresent) { $failures.Add('NetCfg component existed before this invocation; it is not owned.') }
    if (-not $componentKnown) { $failures.Add('Post-install NetCfg state is unknown; ownership requires inspection.') }
    elseif (-not $componentAfter) { $failures.Add('NetCfg component is absent after installation.') }
    if ($null -eq $PackageDelta) { $failures.Add('Post-install Driver Store ownership delta is unknown; package ownership requires inspection.') }
    else {
        if (-not [bool]$PackageDelta.pass) { $failures.Add(('Driver Store ownership delta did not pass: '+(@($PackageDelta.errors) -join '; '))) }
        if (@($PackageDelta.owned).Count -ne 1 -or $ownedPackages.Count -ne 1) { $failures.Add(('Expected one exact attributable NIC package; matched={0}' -f $ownedPackages.Count)) }
    }
    $cleanupAction = if ($componentOwned -and $ownedPackages.Count -gt 0) {'UnbindOwnedNetCfgComponentAndRemoveOwnedPackage'} elseif ($componentOwned) {'UnbindOwnedNetCfgComponent'} elseif ($ownedPackages.Count -gt 0) {'RemoveOwnedPackage'} else {'InspectPostInstallState'}
    return [pscustomobject]@{
        pass=($failures.Count -eq 0)
        exitCode=$ExitCode
        componentKnown=$componentKnown
        componentBeforePresent=$ComponentBeforePresent
        componentAfterPresent=$(if ($componentKnown) {$componentAfter} else {$null})
        ownedNicComponent=[ordered]@{componentId='HypersinvNicAddressFilter';owned=$componentOwned;beforePresent=$ComponentBeforePresent;afterPresent=$(if ($componentKnown) {$componentAfter} else {$null})}
        ownedNicPackages=$ownedPackages
        cleanupRequired=($componentOwned -or $ownedPackages.Count -gt 0)
        cleanupAction=$cleanupAction
        failures=@($failures.ToArray())
    }
}

function Set-DeviceCycleNicInstallJournal {
    param(
        [Parameter(Mandatory=$true)]$State,
        [Parameter(Mandatory=$true)]$Disposition,
        [Parameter(Mandatory=$true)]$Intent,
        [Parameter(Mandatory=$true)]$Observation
    )
    $componentAfterPath = if ($null -ne $Observation.netcfgAfter) {[string]$Observation.netcfgAfter.outputPath} else {$null}
    $State | Add-Member -MemberType NoteProperty -Name ownedNicComponent -Value ([ordered]@{
        componentId='HypersinvNicAddressFilter'
        owned=[bool]$Disposition.ownedNicComponent.owned
        beforePresent=[bool]$Disposition.ownedNicComponent.beforePresent
        afterPresent=$Disposition.ownedNicComponent.afterPresent
        beforePath=[string]$Intent.componentBefore.outputPath
        afterPath=$componentAfterPath
        infPath=[string]$Intent.infPath
        infSha256=[string]$Intent.infSha256
    }) -Force
    $packages = @($Disposition.ownedNicPackages)
    $State | Add-Member -MemberType NoteProperty -Name ownedNicPackages -Value $packages -Force
    $State | Add-Member -MemberType NoteProperty -Name ownedNicPackage -Value $(if ($packages.Count -eq 1) {$packages[0]} else {$null}) -Force
    $State | Add-Member -MemberType NoteProperty -Name nicInstallObservation -Value $Observation -Force
    return $State
}

function Get-DeviceCycleNicCleanupPlan {
    param([Parameter(Mandatory=$true)]$State)
    $componentOwned = ($null -ne $State.ownedNicComponent -and [bool]$State.ownedNicComponent.owned)
    $packages = @($State.ownedNicPackages)
    if ($packages.Count -eq 0 -and $null -ne $State.ownedNicPackage) { $packages = @($State.ownedNicPackage) }
    return [pscustomobject]@{
        unbindComponent=$componentOwned
        removeOwnedPackages=($packages.Count -gt 0)
        ownedPackages=$packages
        cleanupRequired=($componentOwned -or $packages.Count -gt 0)
    }
}

function New-DeviceCycleRoundTripSummary {
    param([Parameter(Mandatory=$true)]$Proof)
    $evidence = @($Proof.evidence)
    $checks = Get-DeviceCycleProperty $Proof 'checks'
    return [pscustomobject]@{
        schema='device-cycle-native-round-trip-summary.v1'
        result=$(if ($Proof.pass -and $evidence.Count -eq 3) {'PASS'} else {'FAIL'})
        mode='Original->Replacement->Original'
        liveNativeRoundTripProven=([bool]$Proof.pass -and $evidence.Count -eq 3)
        evidence=$evidence
        checks=@($checks)
        wmiReplacementClaim=$false
        wmiReplacementLimit='WMI is evaluated after removal for original values only; active cached WMI values are not presented as live replacement evidence.'
    }
}

function Assert-DeviceCycleRoundTripReady {
    param([Parameter(Mandatory=$true)]$RoundTrip,[Parameter(Mandatory=$true)]$Cleanup,[Parameter(Mandatory=$true)]$FinalChecks)
    if ([string]$RoundTrip.result -cne 'PASS' -or -not [bool]$RoundTrip.liveNativeRoundTripProven) { throw 'Readiness gate failed: an actual native original -> replacement -> original round trip is not proven.' }
    if (-not [bool]$Cleanup.pass) { throw 'Readiness gate failed: machine cleanup is not proven.' }
    if (-not [bool]$FinalChecks.pass) { throw 'Readiness gate failed: final verification checks are not all green.' }
    return $true
}

function Write-DeviceCycleJsonAtomic {
    param([Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)]$Value)
    $full = [IO.Path]::GetFullPath($Path)
    $parent = Split-Path -Parent $full
    if (-not [IO.Directory]::Exists($parent)) { [void][IO.Directory]::CreateDirectory($parent) }
    $temp = $full + '.tmp.' + [Guid]::NewGuid().ToString('N')
    $backup = $full + '.bak.' + [Guid]::NewGuid().ToString('N')
    $json = ConvertTo-Json -InputObject $Value -Depth 30
    [IO.File]::WriteAllText($temp,$json,[Text.UTF8Encoding]::new($false))
    if ([IO.File]::Exists($full)) { [IO.File]::Replace($temp,$full,$backup,$true) }
    else { [IO.File]::Move($temp,$full) }
    return $full
}

function Assert-DeviceCycleManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$ManifestPath,
        [Parameter(Mandatory=$true)][string]$PayloadDirectory,
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [string]$ExpectedManifestSha256
    )
    $manifestFull = Get-DeviceCycleFullPath $ManifestPath
    $payloadFull = Get-DeviceCycleFullPath $PayloadDirectory
    $repoFull = Get-DeviceCycleFullPath $RepoRoot
    Assert-DeviceCycleNoReparsePath $manifestFull $payloadFull
    if (-not [IO.File]::Exists($manifestFull)) { throw "Protected payload manifest missing: $manifestFull" }
    $manifestHash = (Get-FileHash -LiteralPath $manifestFull -Algorithm SHA256).Hash
    if (-not [string]::IsNullOrWhiteSpace($ExpectedManifestSha256) -and $manifestHash -cne $ExpectedManifestSha256.ToUpperInvariant()) {
        throw "Protected manifest hash mismatch: expected=$ExpectedManifestSha256 actual=$manifestHash"
    }
    $manifest = Get-Content -LiteralPath $manifestFull -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([int]$manifest.schema -ne 1 -or $null -eq $manifest.pins -or @($manifest.pins).Count -eq 0) { throw 'Protected manifest schema/pins are invalid.' }
    $verified = New-Object 'System.Collections.Generic.List[object]'
    foreach ($pin in @($manifest.pins)) {
        $pathText = [string]$pin.path
        $expected = [string]$pin.sha256
        if ($expected -notmatch '^[A-Fa-f0-9]{64}$') { throw "Malformed protected pin hash for $pathText" }
        if ([IO.Path]::IsPathRooted($pathText)) { $path = [IO.Path]::GetFullPath($pathText) }
        else {
            $payloadCandidate = [IO.Path]::GetFullPath((Join-Path $payloadFull $pathText))
            $repoCandidate = [IO.Path]::GetFullPath((Join-Path $repoFull $pathText))
            if ([IO.File]::Exists($payloadCandidate)) { $path = $payloadCandidate } else { $path = $repoCandidate }
        }
        Assert-DeviceCycleNoReparsePath $path $repoFull
        if (-not [IO.File]::Exists($path)) { throw "Pinned payload file missing: $path" }
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actual -cne $expected.ToUpperInvariant()) { throw "Pinned payload file hash mismatch: $path expected=$expected actual=$actual" }
        $verified.Add([pscustomobject]@{path=$path;sha256=$actual})
    }
    return [pscustomobject]@{path=$manifestFull;sha256=$manifestHash;schema=[int]$manifest.schema;pins=@($verified.ToArray())}
}

Export-ModuleMember -Function Get-DeviceCycleProperty,Get-DeviceCycleFullPath,Test-DeviceCyclePathWithin,Assert-DeviceCycleNoReparsePath,Get-DeviceCycleTransitionTable,Set-DeviceCyclePhase,Get-DeviceCycleBcdSetting,Get-DeviceCycleBcdRestoreArguments,Get-DeviceCycleTrustRestorePlan,Test-DeviceCycleOwnedPackageDelta,Test-DeviceCycleProcessResult,Get-DeviceCyclePnpExitDisposition,Get-DeviceCycleCleanupDecision,Get-DeviceCycleEnableFailureDisposition,Get-DeviceCycleFirmwareRecordsToCompare,Get-DeviceCycleRollbackDisposition,Test-DeviceCyclePreTrustSignatureStatus,Get-DeviceCycleNicInstallDisposition,Set-DeviceCycleNicInstallJournal,Get-DeviceCycleNicCleanupPlan,New-DeviceCycleRoundTripSummary,Assert-DeviceCycleRoundTripReady,Write-DeviceCycleJsonAtomic,Assert-DeviceCycleManifest
