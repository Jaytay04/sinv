Set-StrictMode -Version 2.0

$script:GpuRetirementRunId = 'gpu-20261020T031023Z-9a2a92a17bf54b4faf4b60c6b7231ec7'
$script:GpuRetirementAuditRelativePath = '.analysis\gpu-baremetal-readiness\334426c80d1d47b2bb454d2c25e65d41\readiness.json'
$script:GpuRetirementAuditSha256 = '2F85A1B86522193941D7A447F5C5187D25C15F2B4FC06773F142885F3C175E3F'
$script:GpuRetirementJournalSha256 = 'AE27720BC55501DD92E0C5B8A34800AF1CA0C479787AA8DBF68C0A0D5FF33EA0'
$script:GpuRetirementArchiveSha256 = '885120E8BB08700AA7F9E22E033B6484540ECFB64234536EBC8149163A80FB99'
$script:GpuRetirementSourceCommit = 'c6e65a2b265d1f3c13ac83bfe61c69281907c003'
$script:GpuRetirementProbeSha256 = 'CAF1F64AA5C0C8DD72E5721643B854B2CC06BAD6C2AB51B3D77BB59229187A46'
$script:GpuRetirementProbeBytes = 172544L
$script:GpuRetirementAuditToolSha256 = '5C7350E5E01BA2FA6B1E59C4EC81702DA758ECC70F6E47A1D2F99EB58DA9F229'
$script:GpuRetirementAuditToolBytes = 240128L
$script:GpuRetirementReferenceLogSha256 = '16485B96E4D4CDD0CAD2DDFA6DE8D82B154A5D1071ACC164170933CE3EA3FCA1'
$script:GpuRetirementReferenceLogBytes = 40838L
$script:GpuRetirementAuditBytes = 10575L
$script:GpuRetirementJournalBytes = 44826L
$script:GpuRetirementArchiveBytes = 533L
$script:GpuRetirementNames = @('grubx64.efi', 'handoff-status.txt', 'manifest.sha256', 'payload.efi', 'shimx64.efi', 'tcglog.ib')

function Get-GpuStageRetirementPin {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $attributes = [IO.File]::GetAttributes($fullPath)
    if (($attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
        ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Expected a regular non-reparse file: $fullPath"
    }
    $stream = New-Object IO.FileStream($fullPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $length = [long]$stream.Length
        $hash = ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
    return [pscustomobject]@{ Path = $fullPath; Sha256 = $hash; Bytes = $length }
}

function Assert-GpuStageRetirementPin {
    param([Parameter(Mandatory = $true)][object]$Pin)

    if ($null -eq $Pin -or $null -eq $Pin.PSObject.Properties['Path'] -or
        [string]$Pin.Sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw 'A file pin is malformed.'
    }
    $expectedBytes = 0L
    if (-not [long]::TryParse([string]$Pin.Bytes, [ref]$expectedBytes) -or $expectedBytes -lt 0) {
        throw 'A file pin has an invalid byte count.'
    }
    $actual = Get-GpuStageRetirementPin -Path ([string]$Pin.Path)
    if ($actual.Bytes -ne $expectedBytes -or
        -not [string]::Equals($actual.Sha256, [string]$Pin.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw "A reviewed evidence file changed: $($Pin.Path)"
    }
    return $actual
}

function Get-GpuRetirementProperty {
    param([AllowNull()][object]$Object, [Parameter(Mandatory = $true)][string]$Name)
    if ($null -eq $Object -or $null -eq $Object.PSObject.Properties[$Name]) { return $null }
    return $Object.PSObject.Properties[$Name].Value
}

function Assert-GpuRetirementEqual {
    param([string]$Expected, [string]$Actual, [string]$Description)
    if (-not [string]::Equals($Expected, $Actual, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description does not match the reviewed retirement target."
    }
}

function Assert-GpuStageRetirementEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Audit,
        [Parameter(Mandatory = $true)][object]$Journal,
        [Parameter(Mandatory = $true)][object]$ArchivedSelector,
        [Parameter(Mandatory = $true)][string]$ExpectedAuditSha256,
        [Parameter(Mandatory = $true)][string]$ExpectedRunId,
        [Parameter(Mandatory = $true)][string]$ExpectedRunDirectory,
        [Parameter(Mandatory = $true)][string]$ExpectedJournalPath,
        [Parameter(Mandatory = $true)][string]$ExpectedSourceRoot
    )

    if ($Audit.SchemaVersion -ne 1 -or $Audit.Status -cne 'BLOCKED_READINESS') {
        throw 'The reviewed readiness audit has an unsupported schema or status.'
    }
    if ($ExpectedAuditSha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw 'The expected audit pin is malformed.'
    }
    if ($Journal.SchemaVersion -ne 1 -or [string]$Journal.RunId -cne $ExpectedRunId -or
        [string]$ExpectedRunId -cne $script:GpuRetirementRunId) {
        throw 'Journal identity is outside the fixed reviewed retirement target.'
    }
    if ([string]$Journal.SourceCommit -cne $script:GpuRetirementSourceCommit) {
        throw 'Archived source commit differs from the reviewed completed run.'
    }
    Assert-GpuRetirementEqual -Expected $ExpectedRunDirectory -Actual ([string]$Journal.RunDirectory) -Description 'Journal run directory'
    Assert-GpuRetirementEqual -Expected $ExpectedSourceRoot -Actual ([string]$Journal.SourceRoot) -Description 'Journal source root'
    # Schema 1 records JournalPath on the selector; the journal records RunDirectory.
    Assert-GpuRetirementEqual -Expected ([IO.Path]::GetFullPath($ExpectedJournalPath)) -Actual ([IO.Path]::GetFullPath([string]$Journal.RunDirectory + '\journal.json')) -Description 'Journal path'
    if ([string]$Journal.Phase -cne 'Complete' -or $null -ne $Journal.InProgress) {
        throw 'The reviewed run is not complete or has an action in progress.'
    }
    if ($null -eq $Journal.LastAction -or [string]$Journal.LastAction.Action -cne 'FinalVerify' -or
        $Journal.LastAction.Succeeded -ne $true) {
        throw 'The reviewed run lacks a successful final verification as its last action.'
    }
    if ([string]::IsNullOrWhiteSpace([string]$Journal.DisarmedBootId)) { throw 'The run has no recorded disarm boot boundary.' }

    $verification = $Journal.FinalVerification
    if ($null -eq $verification -or $verification.ResetBoundaryPassed -ne $true -or
        $verification.SecureBoot -ne $true -or $verification.ActivationAttempted -ne $true -or
        $null -eq $verification.Health -or $verification.Health.Passed -ne $true -or
        [string]$verification.Health.ExpectedResult -cne 'ABSENT' -or
        [string]$verification.Health.Aggregate -cne 'ABSENT' -or
        [int]$verification.Health.CpuCount -ne [int]$Journal.Host.LogicalCpus -or
        [int]$verification.Health.ExpectedCpus -ne [int]$Journal.Host.LogicalCpus -or
        $null -eq $verification.Esp -or $verification.Esp.MatchesBaseline -ne $true -or
        $null -eq $verification.FirmwareEnumeration -or $verification.FirmwareEnumeration.TextEqual -ne $true -or
        @($verification.Errors).Count -ne 0) {
        throw 'Final verification does not prove the required reset, ABSENT health, Secure Boot, ESP, and firmware baseline gates.'
    }

    if ($ArchivedSelector.SchemaVersion -ne 1 -or [string]$ArchivedSelector.RunId -cne $ExpectedRunId) {
        throw 'The archived selector is not for the fixed reviewed run.'
    }
    Assert-GpuRetirementEqual -Expected $ExpectedRunDirectory -Actual ([string]$ArchivedSelector.RunDirectory) -Description 'Archived selector run directory'
    Assert-GpuRetirementEqual -Expected $ExpectedJournalPath -Actual ([string]$ArchivedSelector.JournalPath) -Description 'Archived selector journal path'
    Assert-GpuRetirementEqual -Expected $ExpectedSourceRoot -Actual ([string]$ArchivedSelector.SourceRoot) -Description 'Archived selector source root'

    if ($Audit.ExistingStage.Exists -ne $true -or $Audit.ExistingStage.Complete -ne $true -or
        [int]$Audit.ExistingStage.EntryCount -ne 6 -or [int]$Audit.ExistingStage.FileCount -ne 6 -or
        @($Audit.ExistingStage.UnknownEntries).Count -ne 0 -or
        @($Audit.ExistingStage.UnsafeEntries).Count -ne 0 -or @($Audit.ExistingStage.Errors).Count -ne 0 -or
        $Audit.Esp.MountStateAfterCleanup -cne 'Unmounted' -or
        $Audit.ExistingResidue.Clear -ne $true -or @($Audit.ExistingResidue.Residues).Count -ne 0) {
        throw 'Readiness audit does not contain a complete, safe six-file stage inventory and clean mount/residue state.'
    }

    $auditEntries = @($Audit.ExistingStage.Entries)
    if ($auditEntries.Count -ne 6) { throw 'Readiness audit entry count is not exactly six.' }
    $entryMap = @{}
    foreach ($entry in $auditEntries) {
        $relativePath = [string]$entry.RelativePath
        if ($entry.Kind -cne 'File' -or $entry.Hashed -ne $true -or
            $relativePath -notmatch '^EFI\\insecure-boot\\([A-Za-z0-9._-]+)$') {
            throw 'Readiness audit contains an unknown or unsafe stage entry.'
        }
        $name = $Matches[1]
        if ($script:GpuRetirementNames -cnotcontains $name -or $entryMap.ContainsKey($name)) {
            throw "Readiness audit contains an unknown or duplicate stage entry: $relativePath"
        }
        if ([string]$entry.Sha256 -notmatch '^[0-9A-Fa-f]{64}$' -or [long]$entry.Bytes -lt 0) {
            throw "Readiness audit pin is malformed: $relativePath"
        }
        $entryMap[$name] = [pscustomobject]@{ Name=$name; RelativePath=$relativePath; Sha256=[string]$entry.Sha256; Bytes=[long]$entry.Bytes }
    }
    foreach ($name in $script:GpuRetirementNames) { if (-not $entryMap.ContainsKey($name)) { throw "Readiness audit is missing $name." } }

    $recordedStageFiles = @($Journal.Stage.Files)
    if ($recordedStageFiles.Count -ne 5) { throw 'The archived run must record exactly five installer-staged files.' }
    $recordedMap = @{}
    foreach ($file in $recordedStageFiles) {
        $path = [string]$file.RelativePath
        if ($path -notmatch '^EFI\\insecure-boot\\([A-Za-z0-9._-]+)$') { throw 'Journal stage path is malformed.' }
        $name = $Matches[1]
        if ($name -ceq 'handoff-status.txt' -or $recordedMap.ContainsKey($name) -or -not $entryMap.ContainsKey($name)) {
            throw "Journal has an unexpected staged file: $path"
        }
        if ([string]$file.Sha256 -cne $entryMap[$name].Sha256 -or [long]$file.Bytes -ne $entryMap[$name].Bytes) {
            throw "Journal stage pin differs from the reviewed audit: $path"
        }
        $recordedMap[$name] = $true
    }
    foreach ($name in $script:GpuRetirementNames) {
        if ($name -cne 'handoff-status.txt' -and -not $recordedMap.ContainsKey($name)) { throw "Journal stage is missing $name." }
    }

    $collectionFiles = @($Journal.Collection.Esp.StageFiles)
    if ($collectionFiles.Count -ne 6) { throw 'The archived collection must record exactly six stage files.' }
    $collectionMap = @{}
    foreach ($file in $collectionFiles) {
        $name = [string]$file.Name
        $pin = $file.Pin
        if ($script:GpuRetirementNames -cnotcontains $name -or $collectionMap.ContainsKey($name) -or
            $null -eq $pin -or [string]$pin.Sha256 -cne $entryMap[$name].Sha256 -or
            [long]$pin.Bytes -ne $entryMap[$name].Bytes) {
            throw "Collected stage pin differs from the reviewed audit: $name"
        }
        $collectionMap[$name] = $true
    }
    foreach ($name in $script:GpuRetirementNames) { if (-not $collectionMap.ContainsKey($name)) { throw "Collection is missing $name." } }

    if ($null -eq $Audit.Esp.Identity -or $null -eq $Journal.Backup) { throw 'Reviewed ESP identity or backup is missing.' }
    Assert-GpuRetirementEqual -Expected ([string]$Journal.Backup.EspVolume) -Actual ([string]$Audit.Esp.Identity.Volume) -Description 'Audit ESP volume'
    Assert-GpuRetirementEqual -Expected ([string]$Journal.Backup.EspPartitionGuid) -Actual ([string]$Audit.Esp.Identity.PartitionGuid) -Description 'Audit ESP partition GUID'
    if ([int]$Journal.Backup.EspDiskNumber -ne [int]$Audit.Esp.Identity.DiskNumber -or
        -not [string]::Equals([string]$Journal.Backup.BootManagerSha256, [string]$Audit.Esp.BootManagerPin.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Audit ESP disk or boot-manager pin differs from the archived backup.'
    }

    return @($script:GpuRetirementNames | ForEach-Object { $entryMap[$_] })
}

function Get-GpuStageRetirementLiveInventory {
    param([Parameter(Mandatory = $true)][string]$StageDirectory)

    $directory = [IO.Path]::GetFullPath($StageDirectory)
    if (-not [IO.Directory]::Exists($directory)) { throw 'The reviewed stage directory is missing.' }
    $directoryAttributes = [IO.File]::GetAttributes($directory)
    if (($directoryAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'The stage directory is a reparse point.' }
    $items = @([IO.Directory]::EnumerateFileSystemEntries($directory))
    if ($items.Count -ne 6) { throw "The live stage contains $($items.Count) entries; exactly six reviewed files are required." }
    $found = @{}
    foreach ($itemPath in $items) {
        $attributes = [IO.File]::GetAttributes($itemPath)
        if (($attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "The live stage contains a directory or reparse point: $itemPath"
        }
        $name = [IO.Path]::GetFileName($itemPath)
        if ($script:GpuRetirementNames -cnotcontains $name -or $found.ContainsKey($name)) {
            throw "The live stage contains an unknown or duplicate file: $name"
        }
        $found[$name] = Get-GpuStageRetirementPin -Path $itemPath
    }
    foreach ($name in $script:GpuRetirementNames) { if (-not $found.ContainsKey($name)) { throw "The live stage is missing $name." } }
    return $found
}

function Assert-GpuStageRetirementLivePins {
    param([hashtable]$LivePins, [object[]]$ExpectedPins)
    foreach ($expected in $ExpectedPins) {
        if (-not $LivePins.ContainsKey([string]$expected.Name)) { throw "Live stage is missing $($expected.Name)." }
        $actual = $LivePins[[string]$expected.Name]
        if ($actual.Bytes -ne [long]$expected.Bytes -or
            -not [string]::Equals($actual.Sha256, [string]$expected.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Live stage file differs from reviewed pin: $($expected.Name)"
        }
    }
}

function Assert-GpuStageRetirementBootSelection {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Baseline,
        [Parameter(Mandatory = $true)][string]$IntegrityOutput,
        [Parameter(Mandatory = $true)][string]$ExpectedBootManagerSha256,
        [Parameter(Mandatory = $true)][string]$ExpectedReferenceLogSha256,
        [Parameter(Mandatory = $true)][bool]$CommandSucceeded
    )

    if (-not $CommandSucceeded) { throw 'ibaudit failed to read the current firmware boot selection.' }
    if ($null -eq $Baseline -or $Baseline.schema -ne 1) { throw 'ibaudit returned a missing or unsupported boot-selection baseline.' }
    if ($null -ne $Baseline.boot_next) { throw 'BootNext is set; refusing stage retirement.' }
    if (-not [string]::Equals([string]$Baseline.bootmgfw_sha256, $ExpectedBootManagerSha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ibaudit observed a different Microsoft boot-manager hash.'
    }
    if (-not [string]::Equals([string]$Baseline.log_sha256, $ExpectedReferenceLogSha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ibaudit observed a different reference TPM log hash.'
    }
    $order = @($Baseline.boot_order)
    if ($order.Count -lt 1 -or $order.Count -gt 128) { throw 'BootOrder is empty or exceeds the 128-entry retirement bound.' }
    foreach ($entry in $order) {
        if ($entry -isnot [int] -and $entry -isnot [long] -and $entry -isnot [uint16] -and $entry -isnot [uint32]) {
            throw 'BootOrder contains a non-numeric entry.'
        }
        if ([long]$entry -lt 0 -or [long]$entry -gt 65535) { throw 'BootOrder contains an out-of-range entry.' }
    }
    $integrityLine = ([string]$IntegrityOutput).Trim()
    $integrityMatch = [regex]::Match($integrityLine, '^BootmgfwSha256=([0-9A-Fa-f]{64}) Findings=0x([0-9A-Fa-f]{8})$')
    if (-not $integrityMatch.Success -or
        -not [string]::Equals($integrityMatch.Groups[1].Value, $ExpectedBootManagerSha256, [StringComparison]::OrdinalIgnoreCase) -or
        $integrityMatch.Groups[2].Value -cne '00000000') {
        throw 'bootmgr-integrity did not confirm the expected Microsoft boot manager with zero findings.'
    }
    return [pscustomobject]@{
        Schema = 1
        BootNext = $null
        BootOrder = [uint16[]]@($order | ForEach-Object { [uint16]$_ })
        BootManagerSha256 = ([string]$Baseline.bootmgfw_sha256).ToUpperInvariant()
        ReferenceLogSha256 = ([string]$Baseline.log_sha256).ToUpperInvariant()
        IntegrityFindings = '00000000'
    }
}

function Assert-GpuStageRetirementBootSelectionStable {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Before,
        [Parameter(Mandatory = $true)][object]$Current,
        [Parameter(Mandatory = $true)][string]$Checkpoint
    )
    if ($null -ne $Current.BootNext -or $null -ne $Before.BootNext -or
        @($Before.BootOrder).Count -ne @($Current.BootOrder).Count) {
        throw "Raw BootNext/BootOrder changed or became invalid $Checkpoint."
    }
    for ($i=0; $i -lt @($Before.BootOrder).Count; $i++) {
        if ([int]$Before.BootOrder[$i] -ne [int]$Current.BootOrder[$i]) {
            throw "Raw BootOrder changed $Checkpoint."
        }
    }
    if ($Before.BootManagerSha256 -cne $Current.BootManagerSha256 -or
        $Before.ReferenceLogSha256 -cne $Current.ReferenceLogSha256 -or
        $Before.IntegrityFindings -cne $Current.IntegrityFindings) {
        throw "Raw boot-selection baseline changed $Checkpoint."
    }
}

function Copy-GpuStageRetirementFile {
    param([string]$Source, [string]$Destination, [object]$ExpectedPin)

    $before = Get-GpuStageRetirementPin -Path $Source
    if ($before.Bytes -ne [long]$ExpectedPin.Bytes -or $before.Sha256 -cne [string]$ExpectedPin.Sha256) {
        throw "Source changed before evidence copy: $Source"
    }
    if ([IO.File]::Exists($Destination) -or [IO.Directory]::Exists($Destination)) { throw "Evidence copy destination already exists: $Destination" }
    $input = New-Object IO.FileStream($Source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $output = $null
    try {
        if ($input.Length -gt 33554432) { throw "Stage file exceeds the 32 MiB copy limit: $Source" }
        $output = New-Object IO.FileStream($Destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $input.CopyTo($output)
        $output.Flush($true)
    }
    finally {
        if ($null -ne $output) { $output.Dispose() }
        $input.Dispose()
    }
    $copyPin = Get-GpuStageRetirementPin -Path $Destination
    $after = Get-GpuStageRetirementPin -Path $Source
    foreach ($pin in @($copyPin, $after)) {
        if ($pin.Bytes -ne [long]$ExpectedPin.Bytes -or $pin.Sha256 -cne [string]$ExpectedPin.Sha256) {
            throw "Stage file or evidence copy changed during copy: $Source"
        }
    }
    return [pscustomobject]@{ Name=[IO.Path]::GetFileName($Source); Source=$after; Copy=$copyPin }
}

function Get-GpuStageRetirementFirmwareText {
    param([Parameter(Mandatory = $true)][object]$Observation)
    if ($null -ne $Observation.PSObject.Properties['Text']) { return [string]$Observation.Text }
    return [string]$Observation
}

function Write-GpuStageRetirementJson {
    param([string]$Path, [object]$Value)
    $json = ConvertTo-Json -InputObject $Value -Depth 32 -Compress
    $bytes = (New-Object Text.UTF8Encoding($false)).GetBytes($json + "`n")
    $stream = New-Object IO.FileStream($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    return Get-GpuStageRetirementPin -Path $Path
}

function Invoke-GpuStageRetirementBootSelectionCapture {
    param(
        [object]$State,
        [string]$ActionDirectory,
        [string]$Checkpoint,
        [object]$PlatformModule,
        [scriptblock]$NativeCommand,
        [string]$ExpectedToolHash,
        [long]$ExpectedToolBytes,
        [string]$ExpectedLogHash,
        [long]$ExpectedLogBytes
    )

    $auditToolPath = [IO.Path]::GetFullPath((Join-Path ([string]$State.Paths.LoaderBuild) 'ibaudit.exe'))
    $auditToolPin = Get-GpuStageRetirementPin -Path $auditToolPath
    if ($auditToolPin.Sha256 -cne $ExpectedToolHash -or $auditToolPin.Bytes -ne $ExpectedToolBytes) { throw 'Pinned ibaudit.exe differs from the immutable reviewed build artifact.' }
    $referenceLog = $State.Backup.ReferenceLog
    $referencePin = Get-GpuStageRetirementPin -Path ([string]$referenceLog.Path)
    if ($referencePin.Sha256 -cne $ExpectedLogHash -or $referencePin.Bytes -ne $ExpectedLogBytes -or
        $referencePin.Sha256 -cne [string]$referenceLog.Sha256 -or $referencePin.Bytes -ne [long]$referenceLog.Bytes) { throw 'Pinned archived TPM reference log differs from the immutable reviewed file.' }
    $baselinePath = Join-Path $ActionDirectory ($Checkpoint + '-boot-selection.json')
    if ($null -ne $NativeCommand) {
        $baselineResult = & $NativeCommand $auditToolPath @('baseline','--log',[string]$referenceLog.Path,'--output',$baselinePath,'--esp-root','S:\') $ActionDirectory ($Checkpoint + '-baseline')
    }
    else {
        $baselineResult = & $PlatformModule { param($p,$log,$out,$dir,$label) Invoke-GpuNativeCommand -Program $p -Arguments @('baseline','--log',$log,'--output',$out,'--esp-root','S:\') -ActionDirectory $dir -Label $label } $auditToolPath ([string]$referenceLog.Path) $baselinePath $ActionDirectory ($Checkpoint + '-baseline')
    }
    if (-not $baselineResult.Succeeded -or -not [IO.File]::Exists($baselinePath)) { throw "ibaudit baseline failed at $Checkpoint." }
    $baseline = Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    if ($null -ne $NativeCommand) {
        $integrity = & $NativeCommand $auditToolPath @('bootmgr-integrity','--expected-hash',[string]$State.Backup.BootManagerSha256,'--esp-root','S:\') $ActionDirectory ($Checkpoint + '-bootmgr-integrity')
    }
    else {
        $integrity = & $PlatformModule { param($p,$hash,$dir,$label) Invoke-GpuNativeCommand -Program $p -Arguments @('bootmgr-integrity','--expected-hash',$hash,'--esp-root','S:\') -ActionDirectory $dir -Label $label } $auditToolPath ([string]$State.Backup.BootManagerSha256) $ActionDirectory ($Checkpoint + '-bootmgr-integrity')
    }
    if ($null -eq $integrity -or -not $integrity.Succeeded -or -not [IO.File]::Exists([string]$integrity.StdoutPath) -or
        -not [IO.File]::Exists([string]$integrity.StderrPath)) { throw "ibaudit boot-manager integrity failed at $Checkpoint." }
    $integrityText = ([IO.File]::ReadAllText($integrity.StdoutPath)).Trim()
    if (-not [string]::IsNullOrWhiteSpace([IO.File]::ReadAllText($integrity.StderrPath))) { throw "ibaudit boot-manager integrity wrote unexpected stderr at $Checkpoint." }
    $validated = Assert-GpuStageRetirementBootSelection -Baseline $baseline -IntegrityOutput $integrityText -ExpectedBootManagerSha256 ([string]$State.Backup.BootManagerSha256) -ExpectedReferenceLogSha256 ([string]$referenceLog.Sha256) -CommandSucceeded ([bool]$baselineResult.Succeeded)
    return [pscustomobject]@{ Baseline=$validated; BaselinePath=$baselinePath; BaselinePin=(Get-GpuStageRetirementPin -Path $baselinePath); IntegrityOutput=$integrityText; IntegrityReceipt=$integrity.ReceiptPath; AuditToolPin=$auditToolPin; ReferenceLogPin=$referencePin }
}

function New-GpuStageRetirementPlatformOperations {
    param(
        [string]$ToolRoot,
        [scriptblock]$NativeCommand,
        [string]$ExpectedAuditToolSha256 = $script:GpuRetirementAuditToolSha256,
        [long]$ExpectedAuditToolBytes = $script:GpuRetirementAuditToolBytes,
        [string]$ExpectedReferenceLogSha256 = $script:GpuRetirementReferenceLogSha256,
        [long]$ExpectedReferenceLogBytes = $script:GpuRetirementReferenceLogBytes
    )

    $statePath = Join-Path $ToolRoot 'GpuHostState.psm1'
    $platformPath = Join-Path $ToolRoot 'GpuHostPlatform.psm1'
    $stateModule = Import-Module $statePath -Force -DisableNameChecking -PassThru -ErrorAction Stop
    $platformModule = Import-Module $platformPath -Force -DisableNameChecking -PassThru -ErrorAction Stop
    $retirementModule = $ExecutionContext.SessionState.Module
    return @{
        VerifySource = { param($SourceRoot,$ReviewedCommit)
            $branch = (@(& git -C $SourceRoot branch --show-current 2>&1) -join '').Trim()
            if ($LASTEXITCODE -ne 0 -or $branch -cne 'feat/smbios-boot-serial-probe') { throw 'Run retirement from feat/smbios-boot-serial-probe.' }
            $changes = @(& git -C $SourceRoot status --porcelain --untracked-files=all 2>&1)
            if ($LASTEXITCODE -ne 0 -or $changes.Count -ne 0) { throw 'The feature worktree must be clean before retirement.' }
            $diff = @(& git -C $SourceRoot diff --quiet ($ReviewedCommit + '..HEAD') -- 'components/uefi-loader/src/audit' 2>&1)
            if ($LASTEXITCODE -ne 0) { throw 'The ibaudit source subsystem changed after the reviewed build commit.' }
            return $true
        }.GetNewClosure()
        AssertAdministrator = { & $platformModule { Assert-GpuAdministrator } }.GetNewClosure()
        GetCurrentSnapshot = { & $platformModule { Get-GpuCurrentMachineSnapshot -IncludeSecureBoot } }.GetNewClosure()
        AssertSameMachine = { param($State,$Current) & $platformModule { param($s,$c) Assert-GpuSameMachine -State $s -Current $c -RequireCpuCount } $State $Current }.GetNewClosure()
        AssertSecureBoot = { param($Current) & $platformModule { param($c) Assert-GpuSecureBootOn -Current $c } $Current }.GetNewClosure()
        AssertBackup = { param($State) & $platformModule { param($s) Assert-GpuBackupState -State $s | Out-Null; Assert-GpuRecoveryLocation -State $s -RequireRunDirectory } $State }.GetNewClosure()
        MountEsp = { param($ActionDirectory) $identity = & $platformModule { param($d) Get-GpuEspMount -ActionDirectory $d } $ActionDirectory; return [pscustomobject]@{ Identity=$identity; Root='S:\'; ActionDirectory=$ActionDirectory; Acquired=$true } }.GetNewClosure()
        DismountEsp = { param($Mount)
            & $platformModule { param($d) Dismount-GpuEspMount -ActionDirectory $d } $Mount.ActionDirectory
            $drive = Get-PSDrive -Name S -ErrorAction SilentlyContinue
            $logicalDrives = [Environment]::GetLogicalDrives()
            if ($null -ne $drive -or (Test-Path -LiteralPath 'S:\') -or @($logicalDrives | Where-Object { $_ -ieq 'S:\' }).Count -gt 0) {
                throw 'The action-owned S: ESP mount remains visible after unmount.'
            }
        }.GetNewClosure()
        AssertMountedEsp = { param($Mount,$Backup) & $platformModule { param($m,$b) Assert-GpuMountedEspMatchesBackup -MountedEsp $m.Identity -Backup $b -Description 'Stage retirement' } $Mount $Backup }.GetNewClosure()
        CaptureFirmware = { param($ActionDirectory) $program = & $platformModule { Get-GpuApplicationPath 'bcdedit.exe' }; $result = & $platformModule { param($p,$d) Invoke-GpuNativeCommand -Program $p -Arguments @('/enum','firmware','/v') -ActionDirectory $d -Label 'stage-retirement-firmware' } $program $ActionDirectory; if (-not $result.Succeeded) { throw "Firmware enumeration failed with exit $($result.ExitCode)." }; & $retirementModule { param($r) [pscustomobject]@{ Text=[IO.File]::ReadAllText($r.StdoutPath); Pin=(Get-GpuStageRetirementPin -Path $r.StdoutPath); ReceiptPath=$r.ReceiptPath } } $result }.GetNewClosure()
        CaptureBootSelection = { param($State,$ActionDirectory,$Checkpoint) & $retirementModule { param($s,$d,$c,$platform,$native,$toolHash,$toolBytes,$logHash,$logBytes) Invoke-GpuStageRetirementBootSelectionCapture -State $s -ActionDirectory $d -Checkpoint $c -PlatformModule $platform -NativeCommand $native -ExpectedToolHash $toolHash -ExpectedToolBytes $toolBytes -ExpectedLogHash $logHash -ExpectedLogBytes $logBytes } $State $ActionDirectory $Checkpoint $platformModule $NativeCommand $ExpectedAuditToolSha256 $ExpectedAuditToolBytes $ExpectedReferenceLogSha256 $ExpectedReferenceLogBytes }.GetNewClosure()
        CaptureHealth = { param($State,$ActionDirectory) $toolPaths = & $platformModule { param($s) Get-GpuExpectedToolPaths -State $s } $State; $probePin = $State.Tools.Probe; if ([IO.Path]::GetFullPath([string]$probePin.Path) -cne [IO.Path]::GetFullPath([string]$toolPaths.Probe)) { throw 'Pinned probe path differs from the archived run path.' }; & $stateModule { param($pin) Assert-GpuFilePin -Pin $pin } $probePin; $result = & $platformModule { param($p,$d) Invoke-GpuNativeCommand -Program $p -Arguments @('health') -ActionDirectory $d -Label 'stage-retirement-absent-health' -AllowedExitCodes @(0) } ([string]$probePin.Path) $ActionDirectory; $lines = @((Get-Content -LiteralPath $result.StdoutPath -ErrorAction Stop) + (Get-Content -LiteralPath $result.StderrPath -ErrorAction Stop)); $verdict = & $stateModule { param($l,$n) Get-GpuHealthVerdict -Lines $l -ExpectedCpus $n -ExpectedResult ABSENT } ([string[]]$lines) ([int]$State.Host.LogicalCpus); return [pscustomobject]@{ CommandSucceeded=[bool]$result.Succeeded; Verdict=$verdict; ReceiptPath=$result.ReceiptPath } }.GetNewClosure()
        AssertPathWithin = { param($Path,$Root,$MustExist) $args = @{Path=$Path;Root=$Root}; if ($MustExist) {$args.MustExist=$true}; & $stateModule { param($a) Assert-GpuPathWithin @a } $args }.GetNewClosure()
        CopyFile = { param($s,$d,$p) & $retirementModule { param($source,$destination,$pin) Copy-GpuStageRetirementFile -Source $source -Destination $destination -ExpectedPin $pin } $s $d $p }.GetNewClosure()
    }
}

function Invoke-GpuStageRetirement {
    [CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
    param(
        [Parameter(Mandatory = $true)][string]$ToolRoot,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [switch]$Apply,
        [hashtable]$Operations,
        [string]$ExpectedAuditSha256 = $script:GpuRetirementAuditSha256,
        [long]$ExpectedAuditBytes = $script:GpuRetirementAuditBytes,
        [string]$ExpectedRunId = $script:GpuRetirementRunId,
        [string]$ExpectedJournalSha256 = $script:GpuRetirementJournalSha256,
        [long]$ExpectedJournalBytes = $script:GpuRetirementJournalBytes,
        [string]$ExpectedArchiveSha256 = $script:GpuRetirementArchiveSha256,
        [long]$ExpectedArchiveBytes = $script:GpuRetirementArchiveBytes,
        [string]$ExpectedProbeSha256 = $script:GpuRetirementProbeSha256,
        [long]$ExpectedProbeBytes = $script:GpuRetirementProbeBytes,
        [string]$ExpectedAuditToolSha256 = $script:GpuRetirementAuditToolSha256,
        [long]$ExpectedAuditToolBytes = $script:GpuRetirementAuditToolBytes,
        [string]$ExpectedReferenceLogSha256 = $script:GpuRetirementReferenceLogSha256,
        [long]$ExpectedReferenceLogBytes = $script:GpuRetirementReferenceLogBytes
    )

    $ErrorActionPreference = 'Stop'
    $ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
    $WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
    $SourceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
    $hasMockOperations = $null -ne $Operations
    if (-not $hasMockOperations) {
        if ($ExpectedAuditSha256 -cne $script:GpuRetirementAuditSha256 -or $ExpectedAuditBytes -ne $script:GpuRetirementAuditBytes -or
            $ExpectedRunId -cne $script:GpuRetirementRunId -or $ExpectedJournalSha256 -cne $script:GpuRetirementJournalSha256 -or
            $ExpectedJournalBytes -ne $script:GpuRetirementJournalBytes -or $ExpectedArchiveSha256 -cne $script:GpuRetirementArchiveSha256 -or
            $ExpectedArchiveBytes -ne $script:GpuRetirementArchiveBytes -or $ExpectedProbeSha256 -cne $script:GpuRetirementProbeSha256 -or
            $ExpectedProbeBytes -ne $script:GpuRetirementProbeBytes -or $ExpectedAuditToolSha256 -cne $script:GpuRetirementAuditToolSha256 -or
            $ExpectedAuditToolBytes -ne $script:GpuRetirementAuditToolBytes -or $ExpectedReferenceLogSha256 -cne $script:GpuRetirementReferenceLogSha256 -or
            $ExpectedReferenceLogBytes -ne $script:GpuRetirementReferenceLogBytes) {
            throw 'Production retirement evidence pins are immutable; test-only overrides require an explicit mock operations table.'
        }
        $Operations = New-GpuStageRetirementPlatformOperations -ToolRoot $ToolRoot
    }

    $report = [ordered]@{
        SchemaVersion = 1; Status = 'STAGE_RETIREMENT_FAILED'; Succeeded = $false
        RunId = $ExpectedRunId; ApplyRequested = [bool]$Apply; ApplyApproved = $false
        RemovedFiles = @(); Copies = @(); Errors = @(); CleanupErrors = @()
        LocalReceiptDirectory = $null; ExternalReceiptDirectory = $null
        OriginalEvidencePins = $null; ReviewedStagePins = $null; Esp = $null
        FirmwareBaselineEqual = $false; Health = $null; NextAction = 'blocked; preserve the stage and all evidence'
        FirmwareObservations = @(); BootSelectionCaptures = @(); HealthReceipt = $null
        EspUnmounted = $false; EspFilesChanged = $false; HostBootStateChanged = $false
    }
    $lock = $null
    $mount = $null
    $localReceipt = $null
    $externalReceipt = $null
    $receiptId = [guid]::NewGuid().ToString('N')
    $stateRoot = Join-Path $WorkspaceRoot '.analysis\gpu-baremetal'
    $runDirectory = Join-Path (Join-Path $stateRoot 'runs') $ExpectedRunId
    $journalPath = Join-Path $runDirectory 'journal.json'
    $archivePath = Join-Path (Join-Path $stateRoot 'archive') ($ExpectedRunId + '.json')
    $auditPath = Join-Path $WorkspaceRoot $script:GpuRetirementAuditRelativePath
    $selectorPath = Join-Path $stateRoot 'host-prepared.json'
    $localRoot = Join-Path $WorkspaceRoot '.analysis\gpu-stage-retirement'
    $localDirectory = Join-Path $localRoot $receiptId
    $stageDirectory = $null
    $sourcePins = @()
    $auditPin = $null; $journalPin = $null; $archivePin = $null
    $journal = $null; $audit = $null; $archive = $null
    $expectedStage = @(); $mountedIdentity = $null
    $beforeFirmware = $null

    try {
        & $Operations['VerifySource'] $SourceRoot $script:GpuRetirementSourceCommit | Out-Null

        $sourceFiles = @(
            (Join-Path $ToolRoot 'Retire-GpuCompletedStage.ps1'),
            (Join-Path $ToolRoot 'GpuStageRetirement.psm1'),
            (Join-Path $ToolRoot 'GpuHostState.psm1'),
            (Join-Path $ToolRoot 'GpuHostPlatform.psm1'),
            (Join-Path $ToolRoot 'Get-GpuBaremetalReadiness.ps1')
        )
        foreach ($sourceFile in $sourceFiles) { & $Operations['AssertPathWithin'] $sourceFile $SourceRoot $true | Out-Null }
        foreach ($sourceFile in $sourceFiles) { $sourcePins += Get-GpuStageRetirementPin -Path $sourceFile }
        & $Operations['AssertPathWithin'] $auditPath $WorkspaceRoot $true | Out-Null
        & $Operations['AssertPathWithin'] $journalPath $stateRoot $true | Out-Null
        & $Operations['AssertPathWithin'] $archivePath (Join-Path $stateRoot 'archive') $true | Out-Null
        $auditPin = Get-GpuStageRetirementPin -Path $auditPath
        if ($auditPin.Sha256 -cne $ExpectedAuditSha256 -or $auditPin.Bytes -ne $ExpectedAuditBytes) { throw 'The reviewed readiness audit hash or length differs from the fixed approved evidence.' }
        $audit = Get-Content -LiteralPath $auditPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $journalPin = Get-GpuStageRetirementPin -Path $journalPath
        if ($journalPin.Sha256 -cne $ExpectedJournalSha256 -or $journalPin.Bytes -ne $ExpectedJournalBytes) { throw 'The archived journal hash or byte length differs from the fixed reviewed run.' }
        $journal = Get-Content -LiteralPath $journalPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $archivePin = Get-GpuStageRetirementPin -Path $archivePath
        if ($archivePin.Sha256 -cne $ExpectedArchiveSha256 -or $archivePin.Bytes -ne $ExpectedArchiveBytes) { throw 'The archived selector hash or byte length differs from the fixed reviewed run.' }
        $archive = Get-Content -LiteralPath $archivePath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $expectedStage = @(Assert-GpuStageRetirementEvidence -Audit $audit -Journal $journal -ArchivedSelector $archive `
            -ExpectedAuditSha256 $ExpectedAuditSha256 -ExpectedRunId $ExpectedRunId `
            -ExpectedRunDirectory $runDirectory -ExpectedJournalPath $journalPath -ExpectedSourceRoot $SourceRoot)
        $probePath = [IO.Path]::GetFullPath((Join-Path ([string]$journal.Paths.LoaderBuild) 'ib-tcg-dump.exe'))
        if ([IO.Path]::GetFullPath([string]$journal.Tools.Probe.Path) -cne $probePath -or
            [string]$journal.Tools.Probe.Sha256 -cne $ExpectedProbeSha256 -or [long]$journal.Tools.Probe.Bytes -ne $ExpectedProbeBytes) {
            throw 'The archived health probe differs from the fixed reviewed build artifact.'
        }
        $auditToolPath = [IO.Path]::GetFullPath((Join-Path ([string]$journal.Paths.LoaderBuild) 'ibaudit.exe'))
        $auditToolPin = Get-GpuStageRetirementPin -Path $auditToolPath
        if ($auditToolPin.Sha256 -cne $ExpectedAuditToolSha256 -or $auditToolPin.Bytes -ne $ExpectedAuditToolBytes) {
            throw 'The archived ibaudit.exe differs from the fixed reviewed build artifact.'
        }
        $referenceLogPin = Get-GpuStageRetirementPin -Path ([string]$journal.Backup.ReferenceLog.Path)
        if ($referenceLogPin.Sha256 -cne $ExpectedReferenceLogSha256 -or $referenceLogPin.Bytes -ne $ExpectedReferenceLogBytes -or
            [string]$journal.Backup.ReferenceLog.Sha256 -cne $ExpectedReferenceLogSha256 -or [long]$journal.Backup.ReferenceLog.Bytes -ne $ExpectedReferenceLogBytes) {
            throw 'The archived TPM reference log differs from the fixed reviewed evidence.'
        }
        $report.OriginalEvidencePins = [pscustomobject]@{ Audit=$auditPin; Journal=$journalPin; ArchivedSelector=$archivePin; Source=$sourcePins; Ibaudit=$auditToolPin; ReferenceLog=$referenceLogPin; Probe=$journal.Tools.Probe }
        $report.ReviewedStagePins = $expectedStage

        & $Operations['AssertAdministrator']
        $current = & $Operations['GetCurrentSnapshot']
        & $Operations['AssertSameMachine'] $journal $current
        & $Operations['AssertSecureBoot'] $current
        if ([string]::Equals([string]$current.BootId, [string]$journal.DisarmedBootId, [StringComparison]::Ordinal)) {
            throw 'A full host reset after disarm has not been observed.'
        }
        $recovery = & $Operations['AssertBackup'] $journal
        if ($null -eq $recovery -or $null -eq $recovery.RunDirectory) { throw 'The external backup evidence could not be confirmed.' }
        if ([IO.Path]::GetFullPath([string]$recovery.RunDirectory) -cne [IO.Path]::GetFullPath([string]$journal.RecoveryRunDirectory)) {
            throw 'Validated recovery directory differs from the archived run.'
        }

        $stateDirectory = Join-Path $WorkspaceRoot '.analysis\gpu-baremetal'
        if (-not [IO.Directory]::Exists($stateDirectory)) { throw 'The existing host state directory is missing.' }
        & $Operations['AssertPathWithin'] $stateDirectory $WorkspaceRoot $true | Out-Null
        $lockPath = Join-Path $stateDirectory 'host-step.lock'
        & $Operations['AssertPathWithin'] $lockPath $stateDirectory $false | Out-Null
        if (Test-Path -LiteralPath $lockPath -PathType Container) { throw 'Host-step lock path is a directory.' }
        $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        if (Test-Path -LiteralPath $selectorPath) { throw 'An active host selector exists; retirement requires the archived run only.' }

        & $Operations['AssertPathWithin'] $localRoot $WorkspaceRoot $false | Out-Null
        if (-not [IO.Directory]::Exists($localRoot)) { [void][IO.Directory]::CreateDirectory($localRoot) }
        & $Operations['AssertPathWithin'] $localDirectory $localRoot $false | Out-Null
        if ([IO.Directory]::Exists($localDirectory) -or [IO.File]::Exists($localDirectory)) { throw 'The unique local receipt directory unexpectedly already exists.' }
        [void][IO.Directory]::CreateDirectory($localDirectory)
        $report.LocalReceiptDirectory = $localDirectory
        $actions = Join-Path $localDirectory 'observations'
        [void][IO.Directory]::CreateDirectory($actions)
        $mount = & $Operations['MountEsp'] $actions
        if ($null -eq $mount -or $mount.Acquired -ne $true) { throw 'The system ESP was not acquired by this retirement action.' }
        $mountedIdentity = $mount.Identity
        $stageDirectory = Join-Path ([string]$mount.Root) 'EFI\insecure-boot'
        & $Operations['AssertPathWithin'] $stageDirectory ([string]$mount.Root) $true | Out-Null
        $backup = $journal.Backup
        & $Operations['AssertMountedEsp'] $mount $backup | Out-Null
        Assert-GpuRetirementEqual -Expected ([string]$audit.Esp.Identity.Volume) -Actual ([string]$mountedIdentity.Volume) -Description 'Mounted ESP volume'
        Assert-GpuRetirementEqual -Expected ([string]$audit.Esp.Identity.DiskUniqueId) -Actual ([string]$mountedIdentity.DiskUniqueId) -Description 'Mounted ESP physical disk identity'
        if ([int]$audit.Esp.Identity.DiskNumber -ne [int]$mountedIdentity.DiskNumber -or
            -not [string]::Equals([string]$audit.Esp.Identity.PartitionGuid, [string]$mountedIdentity.PartitionGuid, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Mounted ESP disk number or partition GUID differs from the reviewed audit.'
        }
        $live = Get-GpuStageRetirementLiveInventory -StageDirectory $stageDirectory
        Assert-GpuStageRetirementLivePins -LivePins $live -ExpectedPins $expectedStage

        $firmwareBaseline = Get-GpuStageRetirementPin -Path ([string]$backup.FirmwareEnumeration.Path)
        if ($firmwareBaseline.Sha256 -cne [string]$backup.FirmwareEnumeration.Sha256 -or $firmwareBaseline.Bytes -ne [long]$backup.FirmwareEnumeration.Bytes) {
            throw 'Saved firmware enumeration baseline pin is invalid.'
        }
        $baselineText = [IO.File]::ReadAllText([string]$backup.FirmwareEnumeration.Path)
        $beforeFirmware = & $Operations['CaptureFirmware'] $actions
        $beforeFirmwareText = Get-GpuStageRetirementFirmwareText -Observation $beforeFirmware
        $report.FirmwareObservations += [pscustomobject]@{ Checkpoint='initial'; Text=$beforeFirmwareText; Pin=$beforeFirmware.Pin; ReceiptPath=$beforeFirmware.ReceiptPath }
        if (-not [string]::Equals([string]$beforeFirmwareText, [string]$baselineText, [StringComparison]::Ordinal)) {
            throw 'Current firmware text differs from the saved mounted-ESP baseline.'
        }
        $liveHealth = & $Operations['CaptureHealth'] $journal $actions
        if ($liveHealth.CommandSucceeded -ne $true -or $null -eq $liveHealth.Verdict -or
            $liveHealth.Verdict.Passed -ne $true -or [string]$liveHealth.Verdict.ExpectedResult -cne 'ABSENT' -or
            [string]$liveHealth.Verdict.Aggregate -cne 'ABSENT' -or
            [int]$liveHealth.Verdict.CpuCount -ne [int]$current.LogicalCpus -or
            [int]$liveHealth.Verdict.ExpectedCpus -ne [int]$current.LogicalCpus) {
            throw 'Current pinned probe did not confirm ABSENT on every expected CPU.'
        }
        $report.Health = $liveHealth.Verdict
        $report.HealthReceipt = $liveHealth.ReceiptPath
        $bootSelectionBefore = & $Operations['CaptureBootSelection'] $journal $actions 'before-removal-checkpoint'
        $report.BootSelectionBefore = $bootSelectionBefore.Baseline
        $report.BootSelectionCaptures += $bootSelectionBefore
        $report.FirmwareBaselineEqual = $true
        $report.Esp = [pscustomobject]@{ Identity=$mountedIdentity; BootManagerSha256=[string]$backup.BootManagerSha256 }

        $recoveryStageRoot = Join-Path ([string]$journal.RecoveryRunDirectory) 'stage-retirement'
        & $Operations['AssertPathWithin'] $recoveryStageRoot ([string]$journal.RecoveryRunDirectory) $false | Out-Null
        if (-not [IO.Directory]::Exists($recoveryStageRoot)) { [void][IO.Directory]::CreateDirectory($recoveryStageRoot) }
        $externalDirectory = Join-Path $recoveryStageRoot $receiptId
        & $Operations['AssertPathWithin'] $externalDirectory $recoveryStageRoot $false | Out-Null
        if ([IO.Directory]::Exists($externalDirectory) -or [IO.File]::Exists($externalDirectory)) { throw 'The unique external receipt directory unexpectedly already exists.' }
        [void][IO.Directory]::CreateDirectory($externalDirectory)
        & $Operations['AssertPathWithin'] $externalDirectory $recoveryStageRoot $true | Out-Null
        $report.ExternalReceiptDirectory = $externalDirectory
        $externalReceipt = $externalDirectory

        # Review mode also makes independently verified local and external copies.
        # Only explicit -Apply followed by ShouldProcess can remove the six files.
        $localCopies = Join-Path $localDirectory 'copies'
        $externalCopies = Join-Path $externalDirectory 'copies'
        & $Operations['AssertPathWithin'] $localCopies $localDirectory $false | Out-Null
        & $Operations['AssertPathWithin'] $externalCopies $externalDirectory $false | Out-Null
        [void][IO.Directory]::CreateDirectory($localCopies)
        [void][IO.Directory]::CreateDirectory($externalCopies)
        foreach ($expected in $expectedStage) {
            $source = Join-Path $stageDirectory $expected.Name
            $localCopyPath = Join-Path $localCopies $expected.Name
            $externalCopyPath = Join-Path $externalCopies $expected.Name
            & $Operations['AssertPathWithin'] $localCopyPath $localCopies $false | Out-Null
            & $Operations['AssertPathWithin'] $externalCopyPath $externalCopies $false | Out-Null
            $copyOperation = $Operations['CopyFile']
            $localCopy = & $copyOperation $source $localCopyPath $expected
            $externalCopy = & $copyOperation $source $externalCopyPath $expected
            $report.Copies += [pscustomobject]@{ Name=$expected.Name; Source=$localCopy.Source; Local=$localCopy.Copy; External=$externalCopy.Copy }
        }
        $intentKind = if ($Apply) { 'GPU_STAGE_RETIREMENT_INTENT' } else { 'GPU_STAGE_RETIREMENT_REVIEW' }
        $intent = [pscustomobject]@{
            SchemaVersion=1; Kind=$intentKind; RunId=$ExpectedRunId; ReceiptId=$receiptId
            AuditPin=$auditPin; JournalPin=$journalPin; ArchivedSelectorPin=$archivePin; SourcePins=$sourcePins
            EspIdentity=$mountedIdentity; BootManagerSha256=[string]$backup.BootManagerSha256
            FirmwareBaseline=$firmwareBaseline; StagePins=$expectedStage; Copies=$report.Copies
            Health=$liveHealth.Verdict; CreatedUtc=[DateTime]::UtcNow.ToString('o')
        }
        $recordLeaf = if ($Apply) { 'intent.json' } else { 'review.json' }
        $localIntentPath = Join-Path $localDirectory $recordLeaf
        $externalIntentPath = Join-Path $externalDirectory $recordLeaf
        $localIntentPin = Write-GpuStageRetirementJson -Path $localIntentPath -Value $intent
        $externalIntentPin = Write-GpuStageRetirementJson -Path $externalIntentPath -Value $intent
        if ($localIntentPin.Sha256 -cne $externalIntentPin.Sha256 -or $localIntentPin.Bytes -ne $externalIntentPin.Bytes) {
            throw 'Local and external retirement review/intent records differ.'
        }
        $report.ReviewOrIntentPins = [pscustomobject]@{ Local=$localIntentPin; External=$externalIntentPin }

        if (-not $Apply) {
            $report.Status = 'REVIEWED_STAGE_READY_FOR_RETIREMENT'
            $report.Succeeded = $true
            $report.NextAction = 'review both verified-copy receipts, then rerun with -Apply to retire only the six pinned files'
        }
        elseif (-not $PSCmdlet.ShouldProcess('S:\EFI\insecure-boot', 'Retire the exact six reviewed files from the completed archived stage')) {
            $report.Status = 'REVIEWED_STAGE_READY_FOR_RETIREMENT'
            $report.Succeeded = $true
            $report.NextAction = 'review the saved evidence, then rerun this command with -Apply'
        }
        else {
            $report.ApplyApproved = $true
            # Re-pin copied evidence and review/intent records immediately before removal.
            foreach ($pin in @($auditPin,$journalPin,$archivePin,$auditToolPin,$referenceLogPin) + @($sourcePins)) { Assert-GpuStageRetirementPin -Pin $pin | Out-Null }
            $probeCurrent = Get-GpuStageRetirementPin -Path ([string]$journal.Tools.Probe.Path)
            if ($probeCurrent.Sha256 -cne $ExpectedProbeSha256 -or $probeCurrent.Bytes -ne $ExpectedProbeBytes) { throw 'Pinned health probe changed before deletion.' }
            foreach ($copySet in $report.Copies) {
                foreach ($copyPin in @($copySet.Local,$copySet.External)) {
                    $verifiedCopy = Assert-GpuStageRetirementPin -Pin $copyPin
                    if ($verifiedCopy.Sha256 -cne [string]$copySet.Source.Sha256 -or $verifiedCopy.Bytes -ne [long]$copySet.Source.Bytes) {
                        throw "A verified retirement evidence copy changed before deletion: $($copySet.Name)"
                    }
                }
            }
            $localIntentCurrent = Assert-GpuStageRetirementPin -Pin $localIntentPin
            $externalIntentCurrent = Assert-GpuStageRetirementPin -Pin $externalIntentPin
            if ($localIntentCurrent.Sha256 -cne $externalIntentCurrent.Sha256 -or $localIntentCurrent.Bytes -ne $externalIntentCurrent.Bytes) {
                throw 'Retirement review/intent records changed before deletion.'
            }
            if (Test-Path -LiteralPath $selectorPath) { throw 'An active host selector appeared before removal.' }
            $recovery = & $Operations['AssertBackup'] $journal
            if ([IO.Path]::GetFullPath([string]$recovery.RunDirectory) -cne [IO.Path]::GetFullPath([string]$journal.RecoveryRunDirectory)) { throw 'Recovery evidence location changed before removal.' }
            & $Operations['AssertMountedEsp'] $mount $backup | Out-Null
            $live = Get-GpuStageRetirementLiveInventory -StageDirectory $stageDirectory
            Assert-GpuStageRetirementLivePins -LivePins $live -ExpectedPins $expectedStage
            $immediateFirmware = & $Operations['CaptureFirmware'] $actions
            $immediateFirmwareText = Get-GpuStageRetirementFirmwareText -Observation $immediateFirmware
            $report.FirmwareObservations += [pscustomobject]@{ Checkpoint='immediate-predelete'; Text=$immediateFirmwareText; Pin=$immediateFirmware.Pin; ReceiptPath=$immediateFirmware.ReceiptPath }
            if (-not [string]::Equals([string]$immediateFirmwareText, [string]$baselineText, [StringComparison]::Ordinal)) { throw 'Firmware enumeration changed immediately before stage removal.' }
            $bootSelectionPreDelete = & $Operations['CaptureBootSelection'] $journal $actions 'immediate-predelete'
            Assert-GpuStageRetirementBootSelectionStable -Before $bootSelectionBefore.Baseline -Current $bootSelectionPreDelete.Baseline -Checkpoint 'immediately before deletion'
            $report.BootSelectionPreDelete = $bootSelectionPreDelete.Baseline
            $report.BootSelectionCaptures += $bootSelectionPreDelete

            $report.Status = 'STAGE_RETIREMENT_FAILED'
            $report.Succeeded = $false
            foreach ($expected in $expectedStage) {
                $target = Join-Path $stageDirectory $expected.Name
                & $Operations['AssertPathWithin'] $target $stageDirectory $true | Out-Null
                $actual = Get-GpuStageRetirementPin -Path $target
                if ($actual.Bytes -ne [long]$expected.Bytes -or $actual.Sha256 -cne [string]$expected.Sha256) {
                    throw "Per-file pin changed before deletion: $($expected.Name)"
                }
                try {
                    if ($Operations.ContainsKey('DeleteFile')) { & $Operations['DeleteFile'] $target } else { [IO.File]::Delete($target) }
                }
                catch {
                    if (-not [IO.File]::Exists($target) -and $report.RemovedFiles -cnotcontains $expected.Name) {
                        $report.RemovedFiles += $expected.Name
                        $report.EspFilesChanged = $true
                    }
                    throw
                }
                if ([IO.File]::Exists($target)) { throw "Exact stage file remains after delete: $($expected.Name)" }
                $report.RemovedFiles += $expected.Name
                $report.EspFilesChanged = $true
            }
            if (@([IO.Directory]::EnumerateFileSystemEntries($stageDirectory)).Count -ne 0) { throw 'The reviewed stage directory is not empty after the six file removals.' }
            & $Operations['AssertPathWithin'] $stageDirectory ([string]$mount.Root) $true | Out-Null
            [IO.Directory]::Delete($stageDirectory, $false)
            if ([IO.Directory]::Exists($stageDirectory)) { throw 'The exact stage directory remains after nonrecursive removal.' }
            $postFirmware = & $Operations['CaptureFirmware'] $actions
            $postFirmwareText = Get-GpuStageRetirementFirmwareText -Observation $postFirmware
            $report.FirmwareObservations += [pscustomobject]@{ Checkpoint='after-removal'; Text=$postFirmwareText; Pin=$postFirmware.Pin; ReceiptPath=$postFirmware.ReceiptPath }
            if (-not [string]::Equals([string]$postFirmwareText, [string]$baselineText, [StringComparison]::Ordinal)) { throw 'Firmware enumeration changed after stage removal.' }
            $bootSelectionAfter = & $Operations['CaptureBootSelection'] $journal $actions 'after-removal'
            Assert-GpuStageRetirementBootSelectionStable -Before $bootSelectionBefore.Baseline -Current $bootSelectionAfter.Baseline -Checkpoint 'after deletion'
            $report.BootSelectionAfter = $bootSelectionAfter.Baseline
            $report.BootSelectionCaptures += $bootSelectionAfter
            $bootPin = Get-GpuStageRetirementPin -Path (Join-Path ([string]$mount.Root) 'EFI\Microsoft\Boot\bootmgfw.efi')
            if ($bootPin.Sha256 -cne [string]$backup.BootManagerSha256) { throw 'Microsoft boot manager changed during stage retirement.' }
            $report.Status = 'STAGE_RETIRED'
            $report.Succeeded = $true
            $report.NextAction = 'rerun Get-GpuBaremetalReadiness.ps1 and preserve both retirement receipts'
        }
    }
    catch {
        $report.Status = 'STAGE_RETIREMENT_FAILED'
        $report.Succeeded = $false
        $report.Errors += $_.Exception.Message
        $report.NextAction = 'blocked; preserve the stage and all evidence; do not retry Apply after a partial removal'
    }
    finally {
        if ($null -ne $mount -and $mount.Acquired -eq $true) {
            try { & $Operations['DismountEsp'] $mount }
            catch {
                $report.CleanupErrors += $_.Exception.Message
                $report.Status = 'STAGE_RETIREMENT_FAILED'
                $report.Succeeded = $false
                $report.NextAction = 'blocked; the ESP unmount failed; inspect evidence before further action'
            }
            if ($report.CleanupErrors.Count -eq 0) { $report.EspUnmounted = $true }
        }
        if ($null -ne $lock) { $lock.Dispose(); $lock = $null }
    }

    if ($null -ne $report.LocalReceiptDirectory) {
        try {
            $result = [pscustomobject]$report
            $localPin = Write-GpuStageRetirementJson -Path (Join-Path $localDirectory 'result.json') -Value $result
            if ($null -ne $report.ExternalReceiptDirectory) {
                $externalPin = Write-GpuStageRetirementJson -Path (Join-Path $externalReceipt 'result.json') -Value $result
                if ($localPin.Sha256 -cne $externalPin.Sha256) { throw 'Local and external result receipts differ.' }
            }
            $report.LocalResultPin = $localPin
        }
        catch {
            $report.Status = 'STAGE_RETIREMENT_FAILED'
            $report.Succeeded = $false
            $report.Errors += ('Result receipt write failed: ' + $_.Exception.Message)
            $report.NextAction = 'blocked; preserve all current evidence and inspect receipt-write failure'
        }
    }
    return [pscustomobject]$report
}

Export-ModuleMember -Function @('Assert-GpuStageRetirementEvidence','Get-GpuStageRetirementLiveInventory','Assert-GpuStageRetirementLivePins','Invoke-GpuStageRetirement')
