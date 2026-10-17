[CmdletBinding()]
param([string]$ReportDirectory)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$runtimeRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$worktreeRoot = [IO.Path]::GetFullPath((Join-Path $runtimeRoot '..\..\..'))
$mainRepoRoot = [IO.Path]::GetFullPath((Join-Path $worktreeRoot '..\..\..'))
$worktreeHandoff = Join-Path $worktreeRoot 'spoof-handoff'
$mainHandoff = Join-Path $mainRepoRoot 'spoof-handoff'
if ([string]::IsNullOrWhiteSpace($ReportDirectory)) {
    $testStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $testNonce = [Guid]::NewGuid().ToString('N')
    $ReportDirectory = Join-Path $mainHandoff (Join-Path 'reports\device-reboot-cycle-20261016' ('runtime-tests.'+$testStamp+'.'+$testNonce))
}
$testReport = [IO.Path]::GetFullPath($ReportDirectory)
$allowedReportRoots = @($worktreeHandoff,$mainHandoff)
$underAllowedRoot = $false
foreach ($root in $allowedReportRoots) {
    $fullRoot = [IO.Path]::GetFullPath($root).TrimEnd('\')
    if ($testReport.StartsWith($fullRoot + '\',[StringComparison]::OrdinalIgnoreCase)) { $underAllowedRoot = $true; break }
}
if (-not $underAllowedRoot) { throw 'Runtime host tests write only under a scoped spoof-handoff directory.' }
if (Test-Path -LiteralPath $testReport) { throw "Test report directory already exists: $testReport" }
[void][IO.Directory]::CreateDirectory($testReport)
Import-Module -Name (Join-Path $runtimeRoot 'runtime.psm1') -Force -DisableNameChecking

$script:cases = New-Object 'System.Collections.Generic.List[object]'
$script:fixtureRoot = Join-Path $testReport 'fixtures'
[void][IO.Directory]::CreateDirectory($script:fixtureRoot)

function Add-CycleTest {
    param([string]$Name,[scriptblock]$Action)
    try { & $Action; $script:cases.Add([pscustomobject]@{name=$Name;pass=$true;detail='PASS'}) }
    catch { $script:cases.Add([pscustomobject]@{name=$Name;pass=$false;detail=$_.Exception.Message}) }
}

function Assert-CycleTest {
    param([bool]$Condition,[string]$Message)
    if (-not $Condition) { throw $Message }
}

Add-CycleTest 'allowed state transition reaches policy boot pending' {
    $state = [pscustomobject]@{phase='Prepared'}
    Set-DeviceCyclePhase $state 'Enabling' | Out-Null
    Set-DeviceCyclePhase $state 'PolicyBootPending' | Out-Null
    Assert-CycleTest ($state.phase -ceq 'PolicyBootPending') ('Unexpected phase: '+$state.phase)
}

Add-CycleTest 'invalid transition is rejected' {
    $state = [pscustomobject]@{phase='Prepared'}
    $threw = $false
    try { Set-DeviceCyclePhase $state 'Complete' | Out-Null } catch { $threw=$_.Exception.Message -like '*Invalid device-cycle transition*' }
    Assert-CycleTest $threw 'Prepared -> Complete was accepted.'
}

Add-CycleTest 'Prepared rollback follows RollingBack before awaiting Secure Boot re-enable' {
    $state = [pscustomobject]@{phase='Prepared'}
    $plan = Get-DeviceCycleRollbackDisposition -Phase $state.phase -NextAction $null
    Assert-CycleTest (-not $plan.noOp -and $plan.noMutation -and ($plan.transitionPhases -join ',') -ceq 'RollingBack,RolledBackAwaitingSecureBootOn') 'Prepared rollback did not plan both valid phase transitions.'
    foreach ($phase in $plan.transitionPhases) { Set-DeviceCyclePhase $state $phase | Out-Null }
    Assert-CycleTest ($state.phase -ceq 'RolledBackAwaitingSecureBootOn') ('Prepared rollback ended in unexpected phase: '+$state.phase)
}

Add-CycleTest 'completed rollback phases are no-op and preserve the BIOS-then-Verify action' {
    $nextAction = 'Re-enable Secure Boot in firmware, boot Windows, then run Mode Verify.'
    foreach ($phase in @('AwaitingSecureBootOn','RolledBackAwaitingSecureBootOn','Complete','RolledBack')) {
        $plan = Get-DeviceCycleRollbackDisposition -Phase $phase -NextAction $nextAction
        Assert-CycleTest ($plan.noOp -and -not $plan.noMutation -and $plan.phase -ceq $phase -and $plan.nextAction -ceq $nextAction) ('Rollback changed completed state or action for phase '+$phase)
    }
}

Add-CycleTest 'failure recovery route is allowed without claiming readiness' {
    $state = [pscustomobject]@{phase='Enabling';ready=$false;liveRoundTripProven=$false}
    Set-DeviceCyclePhase $state 'RecoveryBootPending' | Out-Null
    Assert-CycleTest ($state.phase -ceq 'RecoveryBootPending' -and -not $state.ready -and -not $state.liveRoundTripProven) 'Recovery state claimed ready or failed to queue recovery.'
}

Add-CycleTest 'BCD parser distinguishes enabled, disabled, and absent TESTSIGNING' {
    $enabled = Get-DeviceCycleBcdSetting -Enumeration @('identifier {abc}','testsigning Yes') -Name testsigning
    $disabled = Get-DeviceCycleBcdSetting -Enumeration @('identifier {abc}','testsigning No') -Name testsigning
    $absent = Get-DeviceCycleBcdSetting -Enumeration @('identifier {abc}') -Name testsigning
    Assert-CycleTest ($enabled.present -and $enabled.value) 'TESTSIGNING Yes did not parse.'
    Assert-CycleTest ($disabled.present -and -not $disabled.value) 'TESTSIGNING No did not parse.'
    Assert-CycleTest (-not $absent.present -and $null -eq $absent.value) 'Absent TESTSIGNING was not preserved as absent.'
}

Add-CycleTest 'BCD parser accepts the blank lines emitted by live bcdedit output' {
    foreach ($value in @('Yes','No',$null)) {
        $settingLine = if ($null -ne $value) { 'testsigning             '+$value } else { '' }
        $stdout = "`r`nWindows Boot Loader`r`n-------------------`r`nidentifier              {00000000-0000-0000-0000-000000000001}`r`n`r`n$settingLine`r`n"
        $lines = @($stdout -split "`r?`n")
        $actual = Get-DeviceCycleBcdSetting -Enumeration $lines -Name testsigning
        Assert-CycleTest ($actual.present -eq ($null -ne $value)) 'Blank lines changed TESTSIGNING presence.'
        if ($null -ne $value) { Assert-CycleTest ($actual.value -eq ($value -ceq 'Yes')) 'Blank lines changed TESTSIGNING value.' }
    }
}

Add-CycleTest 'BCD restore preserves a previously absent element' {
    $args = @(Get-DeviceCycleBcdRestoreArguments -ObjectId '{5ab}' -Original ([pscustomobject]@{present=$false;value=$null}))
    Assert-CycleTest (($args -join ' ') -ceq '/deletevalue {5ab} testsigning') ('Wrong restore arguments: '+($args -join ' '))
}

Add-CycleTest 'BCD restore preserves an existing enabled value' {
    $args = @(Get-DeviceCycleBcdRestoreArguments -ObjectId '{5ab}' -Original ([pscustomobject]@{present=$true;value=$true}))
    Assert-CycleTest (($args -join ' ') -ceq '/set {5ab} testsigning on') ('Wrong restore arguments: '+($args -join ' '))
}

Add-CycleTest 'trust cleanup removes only entries introduced by this run' {
    $original = @([pscustomobject]@{store='Cert:\LocalMachine\Root';certificatePresent=$true},[pscustomobject]@{store='Cert:\LocalMachine\TrustedPublisher';certificatePresent=$false})
    $plan = @(Get-DeviceCycleTrustRestorePlan $original)
    Assert-CycleTest ($plan.Count -eq 1 -and $plan[0] -ceq 'Cert:\LocalMachine\TrustedPublisher') ('Wrong store removal plan: '+($plan -join ','))
}

Add-CycleTest 'missing original store membership fails closed' {
    $threw = $false
    try { Get-DeviceCycleTrustRestorePlan @([pscustomobject]@{store='Cert:\LocalMachine\Root';certificatePresent=$false}) | Out-Null } catch { $threw=$_.Exception.Message -like '*must contain exactly one row*' }
    Assert-CycleTest $threw 'Missing TrustedPublisher baseline unexpectedly passed.'
}

Add-CycleTest 'exact new package hash is attributed to this invocation' {
    $before = @([pscustomobject]@{publishedName='oem1.inf';originalFile='C:\Windows\driverstore\old.inf';infSha256='A';provider='Old';class='SCSIAdapter';version='1'})
    $after = @($before + [pscustomobject]@{publishedName='oem88.inf';originalFile='C:\Windows\driverstore\client_storage_filter.inf';infSha256=('A' * 64);provider='Hypersinv';class='SCSIAdapter';version='1'})
    $result = Test-DeviceCycleOwnedPackageDelta -Before $before -After $after -InfName client_storage_filter.inf -InfSha256 ('A' * 64)
    Assert-CycleTest ($result.pass -and $result.owned.Count -eq 1 -and $result.owned[0].publishedName -ceq 'oem88.inf') 'Exact newly added OEM package was not attributed.'
}

Add-CycleTest 'package hash mismatch is never treated as owned' {
    $after = @([pscustomobject]@{publishedName='oem88.inf';originalFile='C:\Windows\driverstore\client_storage_filter.inf';infSha256=('B' * 64);provider='Hypersinv';class='SCSIAdapter';version='1'})
    $result = Test-DeviceCycleOwnedPackageDelta -Before @() -After $after -InfName client_storage_filter.inf -InfSha256 ('A' * 64)
    Assert-CycleTest (-not $result.pass -and $result.owned.Count -eq 0 -and $result.errors.Count -eq 1) 'Mismatched package INF hash was adopted.'
}

Add-CycleTest 'process failures preserve exact exit and evidence paths' {
    $threw = $false
    try { Test-DeviceCycleProcessResult ([pscustomobject]@{exitCode=5;executable='tool.exe';arguments=@('/x');stdoutPath='out.txt';stderrPath='err.txt'}) | Out-Null } catch { $threw=$_.Exception.Message -like '*exit=5*stderr=err.txt*' }
    Assert-CycleTest $threw 'Failed process result was not surfaced with its stderr evidence path.'
}

Add-CycleTest 'pre-trust Authenticode permits only the exact recorded untrusted-root UnknownError' {
    $thumbprint = 'A1B2C3D4'
    $message = 'A certificate chain processed, but terminated in a root certificate which is not trusted by the trust provider'
    $unknownExpected = Test-DeviceCyclePreTrustSignatureStatus -Status UnknownError -StatusMessage $message -ExpectedThumbprint $thumbprint -ActualThumbprint $thumbprint
    $unknownDifferent = Test-DeviceCyclePreTrustSignatureStatus -Status UnknownError -StatusMessage 'The file hash algorithm is invalid.' -ExpectedThumbprint $thumbprint -ActualThumbprint $thumbprint
    $invalid = Test-DeviceCyclePreTrustSignatureStatus -Status NotSigned -StatusMessage 'No signature.' -ExpectedThumbprint $thumbprint -ActualThumbprint $thumbprint
    $wrongSigner = Test-DeviceCyclePreTrustSignatureStatus -Status UnknownError -StatusMessage $message -ExpectedThumbprint $thumbprint -ActualThumbprint 'DEADBEEF'
    $valid = Test-DeviceCyclePreTrustSignatureStatus -Status Valid -StatusMessage 'Signature verified.' -ExpectedThumbprint $thumbprint -ActualThumbprint $thumbprint
    Assert-CycleTest ($unknownExpected.accepted -and $valid.accepted) 'Expected pre-trust states were not accepted.'
    Assert-CycleTest (-not $unknownDifferent.accepted -and -not $invalid.accepted -and -not $wrongSigner.accepted) 'Unexpected status or signer was accepted without trust.'
}

Add-CycleTest 'nonzero NIC install journals exact post-state ownership and requires cleanup' {
    $infHash = 'A' * 64
    $infPath = 'C:\package\client_nic_filter.inf'
    $delta = [pscustomobject]@{pass=$true;owned=@([pscustomobject]@{publishedName='oem88.inf';originalFile='C:\Windows\DriverStore\client_nic_filter.inf';infSha256=$infHash});errors=@()}
    $decision = Get-DeviceCycleNicInstallDisposition -ExitCode 1 -ComponentBeforePresent $false -ComponentAfterPresent $true -PackageDelta $delta -InfName 'client_nic_filter.inf' -InfPath $infPath -InfSha256 $infHash -CommandError '' -InventoryError '' -ComponentError ''
    Assert-CycleTest (-not $decision.pass) 'A nonzero netcfg exit was treated as a successful install.'
    Assert-CycleTest ($decision.cleanupRequired -and $decision.cleanupAction -ceq 'UnbindOwnedNetCfgComponentAndRemoveOwnedPackage') 'Proven partial install did not require both supported cleanup actions.'
    Assert-CycleTest ($decision.ownedNicComponent.owned -and $decision.ownedNicComponent.beforePresent -eq $false -and $decision.ownedNicComponent.afterPresent -eq $true) 'Absent-before/present-after NetCfg ownership was not recorded.'
    Assert-CycleTest ($decision.ownedNicPackages.Count -eq 1 -and $decision.ownedNicPackages[0].publishedName -ceq 'oem88.inf' -and $decision.ownedNicPackages[0].infSha256 -ceq $infHash) 'Exact attributable package identity/hash was not journaled for cleanup.'
    $intent = [ordered]@{componentId='HypersinvNicAddressFilter';infPath=$infPath;infSha256=$infHash;componentBefore=[pscustomobject]@{outputPath='before.txt'}}
    $observation = [ordered]@{netcfgAfter=[pscustomobject]@{outputPath='after.txt'};disposition=$decision}
    $state = [pscustomobject]@{ownedNicComponent=$null;ownedNicPackages=@();ownedNicPackage=$null;nicInstallObservation=$null}
    Set-DeviceCycleNicInstallJournal -State $state -Disposition $decision -Intent $intent -Observation $observation | Out-Null
    $cleanup = Get-DeviceCycleNicCleanupPlan -State $state
    Assert-CycleTest ($state.ownedNicComponent.owned -and $state.ownedNicPackages.Count -eq 1 -and $state.nicInstallObservation.disposition.exitCode -eq 1) 'Nonzero netcfg post-state was not copied to the run journal.'
    Assert-CycleTest ($cleanup.unbindComponent -and $cleanup.removeOwnedPackages -and $cleanup.ownedPackages.Count -eq 1) 'Journaled partial install did not reach the cleanup plan.'
}

Add-CycleTest 'NIC post-install query failure remains unowned and explicitly requires inspection' {
    $decision = Get-DeviceCycleNicInstallDisposition -ExitCode 1 -ComponentBeforePresent $false -ComponentAfterPresent $null -PackageDelta $null -InfName 'client_nic_filter.inf' -InfPath 'C:\package\client_nic_filter.inf' -InfSha256 ('A' * 64) -CommandError '' -InventoryError 'DISM query error' -ComponentError 'netcfg query error'
    Assert-CycleTest (-not $decision.pass -and -not $decision.cleanupRequired -and $decision.cleanupAction -ceq 'InspectPostInstallState') 'Unknown post-state was falsely attributed or marked clean.'
    Assert-CycleTest ($decision.failures -join ';' -like '*DISM query error*netcfg query error*') 'Exact post-query diagnostics were lost.'
}

Add-CycleTest 'PnPUtil accepts success and reboot-required while preserving other failures' {
    $success = Get-DeviceCyclePnpExitDisposition 0
    $restart = Get-DeviceCyclePnpExitDisposition 3010
    $failure = Get-DeviceCyclePnpExitDisposition 5
    Assert-CycleTest ($success.accepted -and -not $success.rebootRequired) 'PnPUtil exit 0 was not accepted as complete.'
    Assert-CycleTest ($restart.accepted -and $restart.rebootRequired) 'PnPUtil exit 3010 was not represented as reboot-required.'
    Assert-CycleTest (-not $failure.accepted -and -not $failure.rebootRequired -and $failure.exitCode -eq 5) 'A non-success PnPUtil exit was accepted or lost.'
}

Add-CycleTest 'cleanup can restore machine after a failed replacement proof without claiming readiness' {
    $decision = Get-DeviceCycleCleanupDecision -FiltersAbsent $true -NativeOriginals $true -WmiOriginals $true -RoundTripProven $false
    Assert-CycleTest ($decision.cleanupMayProceed -and $decision.machineRestored) 'Confirmed original reads and detached filters did not permit environment cleanup.'
    Assert-CycleTest (-not $decision.liveRoundTripProven -and -not $decision.ready) 'A failed native replacement proof incorrectly passed readiness.'
}

Add-CycleTest 'cleanup remains blocked while an owned filter is present' {
    $decision = Get-DeviceCycleCleanupDecision -FiltersAbsent $false -NativeOriginals $true -WmiOriginals $true -RoundTripProven $true
    Assert-CycleTest (-not $decision.cleanupMayProceed -and -not $decision.machineRestored -and -not $decision.ready) 'A still-present filter was treated as safely removed.'
}

Add-CycleTest 'failed enable before storage install rolls back without requiring a recovery reboot' {
    $decision = Get-DeviceCycleEnableFailureDisposition -StorageInstallStarted $false -OwnedStoragePackageCount 0 -OwnedStartupTaskPresent $false
    Assert-CycleTest ($decision.immediateRollback -and -not $decision.recoveryBootRequired -and $decision.action -ceq 'RestoreBcdTrustAndTask') 'No-install failure incorrectly retained reboot-only recovery state.'
}

Add-CycleTest 'failed enable after storage install keeps the signed recovery boot path' {
    $decision = Get-DeviceCycleEnableFailureDisposition -StorageInstallStarted $true -OwnedStoragePackageCount 0 -OwnedStartupTaskPresent $true
    Assert-CycleTest (-not $decision.immediateRollback -and $decision.recoveryBootRequired -and $decision.action -ceq 'RetainRecoveryTaskTrustAndPolicy') 'Possible storage mutation did not retain the recovery path.'
}

Add-CycleTest 'failed enable with possible storage mutation but no verified task is not mislabeled recoverable' {
    $decision = Get-DeviceCycleEnableFailureDisposition -StorageInstallStarted $true -OwnedStoragePackageCount 0 -OwnedStartupTaskPresent $false
    Assert-CycleTest (-not $decision.immediateRollback -and -not $decision.recoveryBootRequired -and $decision.action -ceq 'ManualRecoveryRequired') 'Missing recovery task was reported as an automatic recovery boot.'
}

Add-CycleTest 'round-trip readiness requires actual native proof and cleanup' {
    $proof = [pscustomobject]@{pass=$true;evidence=@([pscustomobject]@{mode='Original'},[pscustomobject]@{mode='Replacement'},[pscustomobject]@{mode='Original'})}
    $summary = New-DeviceCycleRoundTripSummary $proof
    $cleanup = [pscustomobject]@{pass=$true}
    $final = [pscustomobject]@{pass=$true}
    Assert-CycleTest (Assert-DeviceCycleRoundTripReady $summary $cleanup $final) 'Valid actual round-trip did not pass the final readiness gate.'
}

Add-CycleTest 'round-trip with no replacement proof fails readiness' {
    $proof = [pscustomobject]@{pass=$false;evidence=@()}
    $summary = New-DeviceCycleRoundTripSummary $proof
    $threw = $false
    try { Assert-DeviceCycleRoundTripReady $summary ([pscustomobject]@{pass=$true}) ([pscustomobject]@{pass=$true}) | Out-Null } catch { $threw=$_.Exception.Message -like '*actual native*round trip*' }
    Assert-CycleTest $threw 'Missing device read-back evidence was accepted as ready.'
}

Add-CycleTest 'round-trip with incomplete cleanup fails readiness' {
    $summary = [pscustomobject]@{result='PASS';liveNativeRoundTripProven=$true}
    $threw = $false
    try { Assert-DeviceCycleRoundTripReady $summary ([pscustomobject]@{pass=$false}) ([pscustomobject]@{pass=$true}) | Out-Null } catch { $threw=$_.Exception.Message -like '*cleanup*' }
    Assert-CycleTest $threw 'Unclean detach was accepted as ready.'
}

Add-CycleTest 'atomic state update keeps a named recoverable backup' {
    $path = Join-Path $script:fixtureRoot 'atomic-state.json'
    [IO.File]::WriteAllText($path,'{"generation":1}',[Text.UTF8Encoding]::new($false))
    Write-DeviceCycleJsonAtomic -Path $path -Value ([ordered]@{generation=2}) | Out-Null
    $current = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    $backups = @(Get-ChildItem -LiteralPath $script:fixtureRoot -Filter 'atomic-state.json.bak.*' -File)
    Assert-CycleTest ($current.generation -eq 2 -and $backups.Count -eq 1) 'Atomic replacement omitted the new state or its backup.'
    $old = Get-Content -LiteralPath $backups[0].FullName -Raw | ConvertFrom-Json
    Assert-CycleTest ($old.generation -eq 1) 'Named state backup does not contain the prior document.'
}

Add-CycleTest 'protected manifest verifies pinned files and rejects tampering' {
    $payload = Join-Path $script:fixtureRoot 'payload'
    [void][IO.Directory]::CreateDirectory($payload)
    $file = Join-Path $payload 'input.txt'
    [IO.File]::WriteAllText($file,'pinned',[Text.UTF8Encoding]::new($false))
    $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    $manifestPath = Join-Path $payload 'manifest.json'
    [IO.File]::WriteAllText($manifestPath,(ConvertTo-Json -InputObject ([ordered]@{schema=1;pins=@([ordered]@{path=$file;sha256=$hash})}) -Depth 5),[Text.UTF8Encoding]::new($false))
    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    $valid = Assert-DeviceCycleManifest -ManifestPath $manifestPath -PayloadDirectory $payload -RepoRoot $mainRepoRoot -ExpectedManifestSha256 $manifestHash
    Assert-CycleTest ($valid.pins.Count -eq 1 -and $valid.pins[0].sha256 -ceq $hash) 'Valid manifest did not pass.'
    [IO.File]::WriteAllText($file,'tampered',[Text.UTF8Encoding]::new($false))
    $threw = $false
    try { Assert-DeviceCycleManifest -ManifestPath $manifestPath -PayloadDirectory $payload -RepoRoot $mainRepoRoot -ExpectedManifestSha256 $manifestHash | Out-Null } catch { $threw=$_.Exception.Message -like '*Pinned payload file hash mismatch*' }
    Assert-CycleTest $threw 'Tampered pin was accepted.'
}

Add-CycleTest 'relative protected manifest references may resolve to readonly repo-root inputs' {
    $payload = Join-Path $script:fixtureRoot 'payload-repo-reference'
    [void][IO.Directory]::CreateDirectory($payload)
    $repoInput = Join-Path $testReport 'readonly-reference.txt'
    [IO.File]::WriteAllText($repoInput,'reference',[Text.UTF8Encoding]::new($false))
    $hash = (Get-FileHash -LiteralPath $repoInput -Algorithm SHA256).Hash
    $relative = 'spoof-handoff\reports\device-reboot-cycle-20261016\' + (Split-Path -Leaf $testReport) + '\readonly-reference.txt'
    $manifestPath = Join-Path $payload 'manifest.json'
    [IO.File]::WriteAllText($manifestPath,(ConvertTo-Json -InputObject ([ordered]@{schema=1;pins=@([ordered]@{path=$relative;sha256=$hash})}) -Depth 5),[Text.UTF8Encoding]::new($false))
    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    $valid = Assert-DeviceCycleManifest -ManifestPath $manifestPath -PayloadDirectory $payload -RepoRoot $mainRepoRoot -ExpectedManifestSha256 $manifestHash
    Assert-CycleTest ($valid.pins.Count -eq 1 -and [string]::Equals($valid.pins[0].path,$repoInput,[StringComparison]::OrdinalIgnoreCase)) 'Relative repo-root pin did not resolve to the exact readonly input.'
}

Add-CycleTest 'PowerShell 5 JSON top-level arrays remain flat when assigned before filtering' {
    $jsonPath = Join-Path $script:fixtureRoot 'top-level-array.json'
    $rows = @(
        [ordered]@{store='Root';targetPresent=$true},
        [ordered]@{store='TrustedPublisher';targetPresent=$false}
    )
    [IO.File]::WriteAllText($jsonPath,(ConvertTo-Json -InputObject $rows -Depth 5),[Text.UTF8Encoding]::new($false))
    $decoded = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $decoded = @($decoded)
    $legacyWrapped = @(Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json -ErrorAction Stop)
    $filtered = @($decoded | Where-Object { $_.store -ceq 'Root' })
    Assert-CycleTest ($decoded -is [Array] -and $decoded.Count -eq 2) ('Top-level JSON array shape changed: type={0}; count={1}' -f $decoded.GetType().FullName,@($decoded).Count)
    Assert-CycleTest ($decoded[0] -isnot [Array] -and $decoded[1] -isnot [Array]) 'Top-level JSON rows were nested inside another array.'
    Assert-CycleTest ($legacyWrapped.Count -eq 1 -and $legacyWrapped[0] -is [Array]) 'The regression fixture did not reproduce the old @(ConvertFrom-Json) nesting behavior.'
    Assert-CycleTest ($filtered.Count -eq 1 -and $filtered[0].targetPresent) 'Filtering JSON rows did not find exactly the Root membership record.'
}

Add-CycleTest 'current main baseline reference exposes all 1195 file pins' {
    $baselinePath = Join-Path $mainHandoff 'reports\bcd-live-variants-20261014\main-baseline-reference.json'
    if (-not [IO.File]::Exists($baselinePath)) { throw "Main baseline reference is missing: $baselinePath" }
    $baseline = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json -ErrorAction Stop
    $baselinePins = @((Get-DeviceCycleProperty $baseline 'files'))
    Assert-CycleTest ($baselinePins.Count -eq 1195) ('Baseline pin count differs: expected=1195 actual={0}' -f $baselinePins.Count)
}

Add-CycleTest 'firmware comparison includes every captured variable except BootCurrent' {
    $records = @(
        [pscustomobject]@{name='SecureBoot';guid='guid';base64='AQ=='},
        [pscustomobject]@{name='BootOrder';guid='guid';base64='AQAA'},
        [pscustomobject]@{name='BootNext';guid='guid';base64=$null},
        [pscustomobject]@{name='BootCurrent';guid='guid';base64='AQAA'},
        [pscustomobject]@{name='PK';guid='guid';base64='cGs='},
        [pscustomobject]@{name='KEK';guid='guid';base64='a2Vr'},
        [pscustomobject]@{name='db';guid='guid';base64='ZGI='},
        [pscustomobject]@{name='dbx';guid='guid';base64='ZGJ4'},
        [pscustomobject]@{name='OsIndications';guid='guid';base64='AA=='}
    )
    $selected = @(Get-DeviceCycleFirmwareRecordsToCompare -Records $records)
    $names = @($selected | ForEach-Object { [string]$_.name })
    Assert-CycleTest ($selected.Count -eq 8 -and $names -notcontains 'BootCurrent') ('Unexpected compared firmware row count/names: count={0}; names={1}' -f $selected.Count,($names -join ','))
    foreach ($required in @('SecureBoot','BootOrder','BootNext','PK','KEK','db','dbx','OsIndications')) {
        Assert-CycleTest ($names -contains $required) ('Captured variable was omitted from final comparison: '+$required)
    }
}

Add-CycleTest 'firmware comparison refuses an empty snapshot and a missing mandatory record' {
    $emptyFailed = $false
    try { Get-DeviceCycleFirmwareRecordsToCompare -Records @() | Out-Null } catch { $emptyFailed=$_.Exception.Message -like '*contains no records*' }
    $missingFailed = $false
    $records = @([pscustomobject]@{name='SecureBoot'},[pscustomobject]@{name='BootOrder'})
    try { Get-DeviceCycleFirmwareRecordsToCompare -Records $records | Out-Null } catch { $missingFailed=$_.Exception.Message -like '*BootNext*' }
    Assert-CycleTest ($emptyFailed -and $missingFailed) 'Empty or incomplete mandatory firmware records passed.'
}

$passed = @($script:cases | Where-Object { $_.pass }).Count
$failed = @($script:cases | Where-Object { -not $_.pass }).Count
$lines = @('Device runtime state and ownership host tests (no elevation, driver, service, BCD, firmware, ESP or reboot mutation).')
$lines += ('cases={0} passed={1} failed={2}' -f $script:cases.Count,$passed,$failed)
$lines += @($script:cases | ForEach-Object { '{0}: {1}{2}' -f $(if ($_.pass) {'PASS'} else {'FAIL'}),$_.name,$(if ($_.pass) {''} else {' - '+$_.detail}) })
$textPath = Join-Path $testReport 'runtime-tests.txt'
[IO.File]::WriteAllLines($textPath,[string[]]$lines,[Text.UTF8Encoding]::new($false))
$verdict = [ordered]@{schema='device-cycle-runtime-tests.v1';result=$(if ($failed -eq 0) {'PASS'} else {'FAIL'});capturedUtc=[DateTime]::UtcNow.ToString('o');testFile=[IO.Path]::GetFullPath($PSCommandPath);testSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash;modulePath=(Join-Path $runtimeRoot 'runtime.psm1');moduleSha256=(Get-FileHash -LiteralPath (Join-Path $runtimeRoot 'runtime.psm1') -Algorithm SHA256).Hash;passed=$passed;failed=$failed;cases=@($script:cases.ToArray());textPath=$textPath;textSha256=(Get-FileHash -LiteralPath $textPath -Algorithm SHA256).Hash}
$jsonPath = Join-Path $testReport 'runtime-tests.json'
[IO.File]::WriteAllText($jsonPath,(ConvertTo-Json -InputObject $verdict -Depth 12),[Text.UTF8Encoding]::new($false))
Get-Content -LiteralPath $textPath
if ($failed -ne 0) { exit 1 }
