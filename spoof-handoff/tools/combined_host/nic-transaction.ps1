Set-StrictMode -Version 2.0

$script:CombinedNicHelperNames = @(
    'Get-HostProperty',
    'ConvertTo-HostMacKey',
    'ConvertTo-HostCanonicalMac',
    'Get-HostAdapterSnapshots',
    'Get-HostLinkConnectedState',
    'Get-HostExactAdapter',
    'Write-HostJournal',
    'Write-HostJsonAtomic',
    'Invoke-HostAdapterRegistryApply',
    'Restore-HostAdapters',
    'Wait-HostAdapterLinksRestored'
)

function Assert-CombinedNicHelperBundle {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$RepositoryRoot)

    $bundlePath = Join-Path $RepositoryRoot 'spoof-handoff\reports\nic-vmnet-compatibility-20261018\production-address-functions.ps1'
    $parityPath = Join-Path $RepositoryRoot 'spoof-handoff\reports\nic-vmnet-compatibility-20261018\tested-function-parity.json'
    foreach ($path in @($bundlePath,$parityPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Tested NIC transaction input is missing: $path" }
    }
    $bundleHash = (Get-FileHash -LiteralPath $bundlePath -Algorithm SHA256).Hash.ToUpperInvariant()
    $parity = Get-Content -LiteralPath $parityPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $sourcePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\nic_vm\host-address-roundtrip.ps1'))
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Production NIC helper source is missing: $sourcePath" }
    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ([string]$parity.schema -cne 'nic-tested-function-parity.v1' -or
        [string]$parity.status -cne 'PASS' -or
        [int]$parity.count -ne 11 -or
        [string]$parity.bundleSha256 -cne $bundleHash -or
        [string]$parity.currentSourceSha256 -cne $sourceHash -or
        (@($parity.functions | Sort-Object -CaseSensitive) -join '|') -cne (@($script:CombinedNicHelperNames | Sort-Object -CaseSensitive) -join '|')) {
        throw "NIC helper parity bundle is stale or incomplete: bundle=$bundleHash parityBundle=$([string]$parity.bundleSha256) source=$sourceHash paritySource=$([string]$parity.currentSourceSha256) status=$([string]$parity.status) count=$([int]$parity.count)."
    }
    return [pscustomobject][ordered]@{ bundlePath=$bundlePath; bundleSha256=$bundleHash; parityPath=$parityPath; paritySha256=(Get-FileHash -LiteralPath $parityPath -Algorithm SHA256).Hash.ToUpperInvariant(); sourcePath=$sourcePath; sourceSha256=$sourceHash; functionCount=[int]$parity.count }
}

function Import-CombinedNicHelperBundle {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$RepositoryRoot)

    $evidence = Assert-CombinedNicHelperBundle -RepositoryRoot $RepositoryRoot
    $configuration = [pscustomobject]@{ bundlePath=$evidence.bundlePath; names=$script:CombinedNicHelperNames }
    $module = New-Module -Name ('CombinedHostNic_' + [Guid]::NewGuid().ToString('N')) -ArgumentList $configuration -ScriptBlock {
        param($Settings)
        . $Settings.bundlePath
        Export-ModuleMember -Function $Settings.names
    }
    Import-Module $module -Global -Force -ErrorAction Stop
    foreach ($name in $script:CombinedNicHelperNames) {
        if ($null -eq (Get-Command $name -ErrorAction SilentlyContinue)) { throw "Imported NIC helper is not callable: $name" }
    }
    return $evidence
}

function New-CombinedNicTransactionJournal {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][object[]]$Bindings,
        [Parameter(Mandatory=$true)][string]$JournalPath,
        [Parameter(Mandatory=$true)][object]$SourceEvidence
    )
    if (@($Bindings).Count -ne 4) { throw "NIC transaction requires exactly four locked adapter bindings; actual=$(@($Bindings).Count)." }
    $snapshots = @(Get-HostAdapterSnapshots -Bindings $Bindings)
    if ($snapshots.Count -ne 4) { throw "Fresh NIC snapshots must contain exactly four adapters; actual=$($snapshots.Count)." }
    foreach ($binding in $Bindings) {
        $matches = @($snapshots | Where-Object { [string]::Equals([string]$_.pnpDeviceId,[string]$binding.pnpDeviceId,[StringComparison]::OrdinalIgnoreCase) })
        if ($matches.Count -ne 1 -or [string]$matches[0].original -cne [string]$binding.original -or [string]$matches[0].replacement -cne [string]$binding.replacement) {
            throw "Fresh NIC transaction snapshot differs from the locked binding: pnp=$([string]$binding.pnpDeviceId)."
        }
    }
    $journal = [ordered]@{
        schema='combined-host-nic-transaction.v1'
        createdUtc=[DateTime]::UtcNow.ToString('o')
        updatedUtc=[DateTime]::UtcNow.ToString('o')
        journalPath=[IO.Path]::GetFullPath($JournalPath)
        helperBundle=$SourceEvidence
        mutationStarted=$false
        adapterSnapshots=$snapshots
        adapterProgress=@(foreach ($snapshot in $snapshots) { [ordered]@{pnpDeviceId=[string]$snapshot.pnpDeviceId;state='not-started'} })
        restoreErrors=@()
        restorationComplete=$false
    }
    Write-HostJournal $journal
    return $journal
}

function Invoke-CombinedNicTransactionApply {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][object]$Journal)
    foreach ($snapshot in @($Journal.adapterSnapshots)) {
        $progress = @($Journal.adapterProgress | Where-Object { [string]::Equals([string]$_.pnpDeviceId,[string]$snapshot.pnpDeviceId,[StringComparison]::OrdinalIgnoreCase) })
        if ($progress.Count -ne 1) { throw "NIC transaction progress row is missing or ambiguous: $([string]$snapshot.pnpDeviceId)" }
        [bool]$mutationStarted = [bool]$Journal.mutationStarted
        $result = Invoke-HostAdapterRegistryApply -Snapshot $snapshot -Journal $Journal -Progress $progress[0] -MutationStarted ([ref]$mutationStarted)
        $progress[0].applyResult = $result
        Write-HostJournal $Journal
    }
    return @($Journal.adapterProgress)
}

function Restore-CombinedNicTransaction {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][object]$Journal)
    $errors = @(Restore-HostAdapters -Journal $Journal)
    $Journal.restoreErrors = $errors
    $Journal.restorationComplete = ($errors.Count -eq 0)
    $Journal.restoredUtc = [DateTime]::UtcNow.ToString('o')
    Write-HostJournal $Journal
    if ($errors.Count -gt 0) {
        $detail = @($errors | ForEach-Object { ConvertTo-Json -InputObject $_ -Compress -Depth 12 }) -join ' | '
        throw "NIC transaction restore is incomplete; preserve journal=$([string]$Journal.journalPath); errors=$detail"
    }
    return $Journal
}
