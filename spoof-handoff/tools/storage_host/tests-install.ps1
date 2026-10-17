# Synthetic action tests: native commands, device reads, file access and journal
# writes are mocked. These results do not establish a live installation or boot.
& {
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'run.ps1'),[ref]$tokens,[ref]$errors)
    foreach($name in @('Invoke-StorageHostInstallPackage','Invoke-StorageHostResumeInstall','Find-StorageHostInstalledInf','Get-StorageHostPackageMode','Set-StorageHostProperty','Assert-StorageHostBaselineIdentity','Assert-StorageHostPolicyEnabled')){
        $definitions=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
        Assert-Equal 1 $definitions.Count ('install test uses one production function: '+$name)
        . ([scriptblock]::Create($definitions[0].Extent.Text))
    }
    function Assert-StorageHostRecoveryCopy {}
    function Get-StorageHostActiveRelease {$script:StorageHostRelease=$script:InstallFixture.release;return $script:StorageHostRelease}
    function Get-StorageHostCertificateMembership {param($Thumbprint) return [pscustomobject]@{Root=$true;TrustedPublisher=$true}}
    function Write-StorageHostInventory {
        param($EvidenceDirectory,$Name)
        return [pscustomobject]@{inventory=[pscustomobject]@{
            windows=[pscustomobject]@{lastBootUpTime=$script:InstallFixture.bootTime}
            disk=[pscustomobject]@{pnpDeviceId='pinned-device';hardwareIds=@('pinned-hardware');serial='0000_0000_0100_0000_E4D2_5CDF_3227_5501.';wmiSerial='0000_0000_0100_0000_E4D2_5CDF_3227_5501.'}
            policy=[pscustomobject]@{secureBoot=$false;testSigning=[pscustomobject]@{exists=$true;value='yes'};codeIntegrity=[pscustomobject]@{success=$true;options=2}}
            fileIo=[pscustomobject]@{match=$true};storageFilter=[pscustomobject]@{present=$false};pnpStack=[pscustomobject]@{filterPresent=$false}
        }}
    }
    function Get-StorageHostOwnedInfCandidates {
        param($ExpectedSha256)
        if($script:InstallFixture.installed){return [pscustomobject]@{publishedName='oem9001.inf';sha256=$script:InstallFixture.installedInfSha256}}
    }
    function Invoke-StorageHostCapture {
        param($Program,$Arguments)
        $script:InstallFixture.commandCount++
        $script:InstallFixture.installed=$true
        return [pscustomobject]@{exitCode=3010;output=@('Synthetic PnPUtil success; reboot required')}
    }
    function Set-StorageHostPhase {param($Phase,$ActionName,$Message) $script:StorageHostState.phase=$Phase}
    function Save-StorageHostState {}
    function Add-StorageHostEvent {param($Kind,$Message,$Data)}
    function Write-StorageHostAtomicJson {
        param($Path,$Value)
        if($Path -like '*-pnputil-add.json'){$script:InstallFixture.receipt=$Value}
    }
    function Test-Path {param($LiteralPath,$PathType) return $true}
    function Get-FileHash {param($LiteralPath,$Algorithm) return [pscustomobject]@{Hash=('A'*64)}}
    function Get-Content {
        param($LiteralPath,[switch]$Raw)
        if($LiteralPath -ceq 'C:\synthetic-proof\transparent.json'){return ($transparentProof|ConvertTo-Json -Depth 8)}
        return ($script:InstallFixture.receipt|ConvertTo-Json -Depth 8)
    }
    function Get-ChildItem {
        param($LiteralPath,$Filter,[switch]$File,[switch]$Recurse)
        if($script:InstallFixture.includeReceipt){return [pscustomobject]@{FullName=('C:\synthetic-run\actions\'+$Filter)}}
    }
    foreach($mode in @('transparent','replacement')){
        $hash='C'*64
        $package=[pscustomobject]@{files=@([pscustomobject]@{name='client_storage_filter.inf';sha256=$hash})}
        $release=[pscustomobject]@{manifest=[pscustomobject]@{pnpInstanceId='pinned-device';hardwareId='pinned-hardware';packages=[pscustomobject]@{transparent=$package;replacement=$package}};packagePaths=@{transparent='C:\synthetic-release\transparent';replacement='C:\synthetic-release\replacement'}}
        $script:InstallFixture=[pscustomobject]@{release=$release;installed=$false;installedInfSha256=$hash;commandCount=0;receipt=$null;includeReceipt=$true;bootTime='2026-10-17T20:43:05.5000000Z'}
        $script:StorageHostState=[pscustomobject]@{phase=if($mode -ceq 'transparent'){'PolicyPendingReboot'}else{'TransparentRemoved'};ownedPackages=[pscustomobject]@{transparent=$null;replacement=$null};pendingOperation=$null;lastObservedBootTime=$null;verifiedTransparentProof=[pscustomobject]@{path='C:\synthetic-proof\transparent.json';sha256=('A'*64)}}
        $script:StorageHostRun='C:\synthetic-run'
        $script:StorageHostActionDirectory='C:\synthetic-run\actions\current'
        $pendingPhase=if($mode -ceq 'transparent'){'TransparentInstallPendingReboot'}else{'ReplacementInstallPendingReboot'}
        $inProgress=if($mode -ceq 'transparent'){'TransparentInstallInProgress'}else{'ReplacementInstallInProgress'}
        $Action=if($mode -ceq 'transparent'){'InstallTransparent'}else{'InstallReplacement'}
        $result=Invoke-StorageHostInstallPackage -Mode $mode -PendingPhase $pendingPhase
        Assert-Equal ($mode.ToUpperInvariant()+'_INSTALL_PENDING_REBOOT') $result.status ($mode+' fresh production install completes timestamp bookkeeping')
        Assert-Equal $pendingPhase $script:StorageHostState.phase ($mode+' install records its pending-reboot phase')
        Assert-Equal 1 $script:InstallFixture.commandCount ($mode+' synthetic install invokes the command once')
        Assert-Equal 'oem9001.inf' $script:StorageHostState.pendingOperation.publishedName ($mode+' install records exact package ownership')
        Assert-Equal 3010 $script:StorageHostState.pendingOperation.pnputilExitCode ($mode+' install preserves reboot-required status')
        Assert-True ([DateTime]::Parse($script:StorageHostState.pendingOperation.startedUtc) -is [DateTime]) ($mode+' UTC timestamp is parseable')

        # A stop after the post-install state save but before the phase save must
        # retain the original intent timestamp, older than its successful receipt.
        Assert-True ([DateTime]::Parse($script:StorageHostState.pendingOperation.startedUtc) -le [DateTime]::Parse($script:InstallFixture.receipt.capturedUtc)) ($mode+' post-install state preserves the pre-receipt intent time')
        $script:StorageHostState.phase=$inProgress
        $Action='ResumeInstall'
        $savedStateResume=Invoke-StorageHostResumeInstall
        Assert-Equal ($mode.ToUpperInvariant()+'_INSTALL_PENDING_REBOOT') $savedStateResume.status ($mode+' resume accepts the state-saved/pre-phase interruption window')

        # Recreate the user's pre-finalization intent, including its missing fields.
        $script:StorageHostState.phase=$inProgress
        $script:StorageHostState.lastObservedBootTime=$null
        $script:StorageHostState.pendingOperation=[pscustomobject]@{kind='install';mode=$mode;expectedInfSha256=$hash;expectedPnpInstanceId='pinned-device';bootTimeBefore=$script:InstallFixture.bootTime;startedUtc=([DateTime]::Parse($script:InstallFixture.receipt.capturedUtc).AddSeconds(-1).ToString('o'))}
        $Action='ResumeInstall'
        Assert-Equal $pendingPhase (Get-StorageHostAllowedTransition -Phase $inProgress -Action $Action) ($mode+' only the in-progress phase permits resume')
        $result=Invoke-StorageHostResumeInstall
        Assert-Equal ($mode.ToUpperInvariant()+'_INSTALL_PENDING_REBOOT') $result.status ($mode+' incomplete journal resumes from its existing successful receipt')
        Assert-True (-not $result.packageReinstalled -and -not $result.driverLoadVerified) ($mode+' journal resume makes no reinstall or loaded-driver claim')
        Assert-Equal 1 $script:InstallFixture.commandCount ($mode+' resume never invokes PnPUtil again')
        Assert-Equal $script:InstallFixture.bootTime $script:StorageHostState.lastObservedBootTime ($mode+' resume preserves the original pre-install boot boundary')
        Assert-Equal 3010 $script:StorageHostState.pendingOperation.pnputilExitCode ($mode+' resume recovers the recorded command exit code')

        $script:StorageHostState.phase=$inProgress
        $script:InstallFixture.includeReceipt=$false
        Assert-Throws {Invoke-StorageHostResumeInstall} ($mode+' resume rejects a missing receipt')
        $script:InstallFixture.includeReceipt=$true
        $script:InstallFixture.receipt.exitCode=5
        Assert-Throws {Invoke-StorageHostResumeInstall} ($mode+' resume rejects a failed PnPUtil receipt')
        $script:InstallFixture.receipt.exitCode=3010
        $script:InstallFixture.installedInfSha256='D'*64
        Assert-Throws {Invoke-StorageHostResumeInstall} ($mode+' resume rejects an installed INF hash mismatch')
        $script:InstallFixture.installedInfSha256=$hash
        $script:InstallFixture.bootTime='2026-10-17T21:43:05.5000000Z'
        Assert-Throws {Invoke-StorageHostResumeInstall} ($mode+' resume rejects a changed boot boundary')
        Assert-Equal 1 $script:InstallFixture.commandCount ($mode+' rejected resumes never reinstall')
    }
    Assert-Throws {Get-StorageHostAllowedTransition -Phase PolicyPendingReboot -Action ResumeInstall} 'resume cannot replace a normal initial installation'
}
