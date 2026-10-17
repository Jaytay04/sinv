# Synthetic action-level tests. Every host and mutation dependency is replaced;
# the only certificate operation parses the repository's existing public CER file.
& {
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'run.ps1'),[ref]$tokens,[ref]$errors)
    foreach($name in @('Invoke-StorageHostAction','Invoke-StorageHostEnableTestMode','Get-StorageHostActiveRelease')){
        $definitions=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
        Assert-Equal 1 $definitions.Count ('action test uses one production function: '+$name)
        . ([scriptblock]::Create($definitions[0].Extent.Text))
    }
    $script:EnableFixture=[pscustomobject]@{calls=[Collections.Generic.List[string]]::new();stores=@{};manifestSha256='pinned-manifest'}
    foreach($name in @('Root','TrustedPublisher')){
        $store=[pscustomobject]@{name=$name;Certificates=[Collections.Generic.List[object]]::new()}
        $store|Add-Member ScriptMethod Open {param($flags) $script:EnableFixture.calls.Add('open-'+$this.name)}
        $store|Add-Member ScriptMethod Close {}
        $store|Add-Member ScriptMethod Add {param($certificate) $this.Certificates.Add($certificate);$script:EnableFixture.calls.Add('import-'+$this.name)}
        $script:EnableFixture.stores[$name]=$store
    }
    $fixtureCertificate=Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))) 'components\hypervisor\hypersinv.cer'
    $script:EnableFixture|Add-Member NoteProperty certificatePath $fixtureCertificate
    $script:StorageHostState=[pscustomobject]@{
        phase='Prepared';releaseDirectory='synthetic-release';releaseManifestSha256='pinned-manifest'
        ownedTrustAdds=@();updatedUtc=$null;policyEnable=$null
        baseline=[pscustomobject]@{
            inventory=[pscustomobject]@{disk=[pscustomobject]@{pnpDeviceId='pinned-device';serial='original';wmiSerial='original'}}
            trustStores=[pscustomobject]@{Root=$false;TrustedPublisher=$false}
        }
    }
    $script:StorageHostCertThumb='59DBD6780CFE60C020B3F4511211FEC22C397B85'
    $script:StorageHostRelease=$null
    $script:StorageHostRun='synthetic-run'
    $script:StorageHostActionDirectory='synthetic-action'
    $Action='EnableTestMode'
    $ReleaseDirectory=$null
    function Assert-StorageHostAdmin {}
    function Get-StorageHostRunDirectory {return 'synthetic-run'}
    function Assert-StorageHostRecoveryCopy {$script:EnableFixture.calls.Add('recovery-validated')}
    function Get-StorageHostRelease {
        param($Directory)
        $script:EnableFixture.calls.Add('release-validated')
        return [pscustomobject]@{manifestSha256=$script:EnableFixture.manifestSha256;certificatePath=$script:EnableFixture.certificatePath}
    }
    function Write-StorageHostInventory {
        param($EvidenceDirectory,$Name)
        return [pscustomobject]@{inventory=[pscustomobject]@{policy=[pscustomobject]@{secureBoot=$false};disk=$script:StorageHostState.baseline.inventory.disk}}
    }
    function Set-StorageHostPhase {
        param($Phase,$ActionName,$Message)
        $script:EnableFixture.calls.Add('phase-'+$Phase)
        $script:StorageHostState.phase=$Phase
    }
    function Get-StorageHostStoreObject {param($Name) return $script:EnableFixture.stores[$Name]}
    function Save-StorageHostState {$script:EnableFixture.calls.Add('saved-ownership-'+(@($script:StorageHostState.ownedTrustAdds) -join ','))}
    function Add-StorageHostEvent {param($Kind,$Message,$Data) $script:EnableFixture.calls.Add('event-'+$Kind)}
    function Invoke-StorageHostBcdMutation {param($Arguments,$LogName) $script:EnableFixture.calls.Add('bcd-test-signing-on');return [pscustomobject]@{exitCode=0}}
    function Get-StorageHostCertificateMembership {
        param($Thumbprint)
        return [pscustomobject]@{Root=($script:EnableFixture.stores.Root.Certificates.Count -gt 0);TrustedPublisher=($script:EnableFixture.stores.TrustedPublisher.Certificates.Count -gt 0)}
    }

    $result=Invoke-StorageHostAction
    Assert-Equal 'POLICY_PENDING_REBOOT' $result.status 'fresh invocation loads the pinned release before reading certificatePath'
    Assert-Equal 'PolicyPendingReboot' $script:StorageHostState.phase 'synthetic policy action reaches its pending-reboot phase'
    Assert-Equal @('Root','TrustedPublisher') @($script:StorageHostState.ownedTrustAdds) 'synthetic imports retain exact trust ownership'
    Assert-True ($script:EnableFixture.calls.IndexOf('release-validated') -lt $script:EnableFixture.calls.IndexOf('phase-EnablingTestMode')) 'release validation precedes policy intent'
    Assert-True ($script:EnableFixture.calls.IndexOf('saved-ownership-Root') -lt $script:EnableFixture.calls.IndexOf('import-Root')) 'ownership intent is saved before synthetic Root import'

    # Model a fresh process resuming after trust imports, before the BCD step.
    $script:StorageHostRelease=$null
    $script:StorageHostState.phase='EnablingTestMode'
    $result=Invoke-StorageHostAction
    Assert-Equal 'POLICY_PENDING_REBOOT' $result.status 'interrupted policy enable may safely retry with release revalidation'
    Assert-Equal @('Root','TrustedPublisher') @($script:StorageHostState.ownedTrustAdds) 'retry preserves ownership needed for rollback'
    Assert-Equal 2 @($script:EnableFixture.calls|Where-Object {$_ -like 'import-*'}).Count 'retry does not import duplicate trust entries'

    $script:StorageHostRelease=$null
    $script:StorageHostState.phase='Prepared'
    $script:EnableFixture.manifestSha256='changed-manifest'
    $callsBefore=$script:EnableFixture.calls.Count
    Assert-Throws {Invoke-StorageHostAction} 'changed release is rejected before policy work'
    Assert-Equal 'Prepared' $script:StorageHostState.phase 'release rejection leaves the phase unchanged'
    $newCalls=@($script:EnableFixture.calls|Select-Object -Skip $callsBefore)
    Assert-True (@($newCalls|Where-Object {$_ -match '^(phase-|open-|import-|bcd-)'}).Count -eq 0) 'release rejection performs no synthetic trust or BCD mutations'
}
