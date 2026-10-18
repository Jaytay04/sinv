[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'CaptureOriginal', 'EnableTestMode',
        'InstallTransparent', 'VerifyTransparent', 'RemoveTransparent', 'VerifyTransparentRemoved',
        'InstallReplacement', 'VerifyReplacement', 'RemoveReplacement', 'VerifyReplacementRemoved',
        'StageSmbiosInventory', 'ArmSmbiosInventory', 'CleanupSmbiosInventory', 'VerifyCorrectedCollectorCleanup', 'RestorePolicy', 'FinalVerify'
    )]
    [string]$Action,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$GuestInventoryPath,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$StorageFixtureManifestPath,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$SmbiosFixtureManifestPath,

    [Parameter(Mandatory = $true)]
    [string]$SharedAssetsDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')]
    [string]$LifecycleId,

    [string]$FixtureCaptureRunDirectory,

    [string]$ReferenceRepo = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue

# Load the frozen production mailbox verifier without running gate assembly. Its
# LibraryOnly guard disables assembly and limits test-key injection to the test
# harness; the renderer obtains the real key from the existing DPAPI credential.
$script:MailboxVerifierPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'assemble-gate.ps1'))
if (-not (Test-Path -LiteralPath $script:MailboxVerifierPath -PathType Leaf)) {
    throw "Pinned mailbox verifier is missing: $script:MailboxVerifierPath"
}
$script:MailboxVerifierModule = New-Module -Name CombinedVmMailboxResponseVerifier -ArgumentList $script:MailboxVerifierPath -ScriptBlock {
    param([string]$VerifierPath)
    . $VerifierPath -LibraryOnly
    Export-ModuleMember -Function Assert-CombinedAssemblyResponseHmac
}
Import-Module $script:MailboxVerifierModule -Force -ErrorAction Stop
if ($null -eq (Get-Command -Name Assert-CombinedAssemblyResponseHmac -CommandType Function -ErrorAction SilentlyContinue)) {
    throw 'Mailbox verifier did not export required response-authentication function.'
}

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Assert-PathUnder([string]$Root, [string]$Path, [string]$Label) {
    $rootPath = (Get-FullPath $Root) + '\'
    $fullPath = Get-FullPath $Path
    if (-not $fullPath.StartsWith($rootPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must remain under $Root"
    }
    return $fullPath
}

function Assert-PathUnderRoots([string[]]$Roots, [string]$Path, [string]$Label) {
    $fullPath = Get-FullPath $Path
    foreach ($root in $Roots) {
        $rootPath = Get-FullPath $root
        if ($fullPath.StartsWith(($rootPath + '\'), [StringComparison]::OrdinalIgnoreCase)) {
            return $fullPath
        }
    }
    throw "$Label must remain under one of the authorized input roots."
}

function Assert-ExactPath([string]$Path, [string]$ExpectedPath, [string]$Label) {
    $fullPath = Get-FullPath $Path
    $expectedFullPath = Get-FullPath $ExpectedPath
    if (-not $fullPath.Equals($expectedFullPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be exactly: $expectedFullPath"
    }
    return $fullPath
}

function Assert-NoReparsePointComponents([string]$Root, [string]$Path, [string]$Label) {
    $rootPath = Get-FullPath $Root
    $rootItem = Get-Item -LiteralPath $rootPath -Force -ErrorAction Stop
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label cannot use a reparse-point root: $rootPath"
    }
    $fullPath = Assert-PathUnder $rootPath $Path $Label
    $relativePath = $fullPath.Substring($rootPath.Length).TrimStart('\')
    $currentPath = $rootPath
    foreach ($component in $relativePath.Split([char]'\', [StringSplitOptions]::RemoveEmptyEntries)) {
        $currentPath = Join-Path $currentPath $component
        if (-not (Test-Path -LiteralPath $currentPath)) { break }
        $item = Get-Item -LiteralPath $currentPath -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label cannot contain a reparse point: $currentPath"
        }
    }
    return $fullPath
}

function Get-RequiredHash($Object, [string]$Property, [string]$Label) {
    $value = [string]$Object.$Property
    if ($value -cnotmatch '^[A-Fa-f0-9]{64}$') { throw "$Label does not contain a valid SHA-256 value." }
    return $value.ToUpperInvariant()
}

function Add-PinnedAsset([Collections.Generic.List[object]]$Assets, [string]$Name, [string]$SourcePath) {
    if ($Name -cnotmatch '\A[A-Za-z0-9_.-]+\z') { throw "Invalid VM asset name: $Name" }
    $path = Assert-PathUnder $script:ReferenceRepoFullPath $SourcePath ('VM asset ' + $Name)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required VM asset is missing: $path" }
    $item = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
    $assetDirectory = Join-Path $script:SharedAssetsFullPath $hash.ToLowerInvariant()
    $sharedPath = Join-Path $assetDirectory $Name
    if (-not (Test-Path -LiteralPath $sharedPath -PathType Leaf)) {
        throw "Hash-pinned read-only VM asset is not staged: $sharedPath"
    }
    $sharedPath = Assert-NoReparsePointComponents $script:SharedAssetsFullPath $sharedPath ('Shared VM asset ' + $Name)
    $sharedItem = Get-Item -LiteralPath $sharedPath
    if ($sharedItem.Length -ne $item.Length -or
        (Get-FileHash -LiteralPath $sharedPath -Algorithm SHA256).Hash.ToUpperInvariant() -cne $hash) {
        throw "Shared VM asset differs from the source file: $Name"
    }
    $Assets.Add([ordered]@{
        name = $Name
        size = [long]$item.Length
        sha256 = $hash
        sourcePath = $path
        sharePath = $sharedPath
        uncPath = ('\\vmware-host\Shared Folders\CombinedFixture\' + $hash.ToLowerInvariant() + '\' + $Name)
    })
    return $hash
}

function Get-MapRows([string]$Path) {
    $rows = @(Import-Csv -LiteralPath $Path -Delimiter "`t")
    if ($rows.Count -lt 9) { throw 'The authoritative identifier map is incomplete.' }
    return ,$rows
}

function Get-UniqueReplacement($Rows, [string]$Member) {
    $matches = @($Rows | Where-Object { [string]$_.member -ceq $Member })
    if ($matches.Count -ne 1) { throw "Expected exactly one replacement-map row for $Member." }
    return [string]$matches[0].replacement
}

function Assert-CombinedCaptureArchive {
    param([Parameter(Mandatory = $true)][string]$ArchivePath, [Parameter(Mandatory = $true)][string]$MetadataPath)
    $archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $matches = @($archive.Entries | Where-Object { [string]$_.FullName -ceq 'kernel-metadata.json' })
        if ($matches.Count -ne 1 -or $matches[0].Length -gt 1MB) { throw 'CaptureActive guest archive must contain one bounded kernel-metadata.json member.' }
        $input = $matches[0].Open()
        $memory = [IO.MemoryStream]::new()
        try { $input.CopyTo($memory); $archivedBytes = $memory.ToArray() }
        finally { $memory.Dispose(); $input.Dispose() }
        $extractedBytes = [IO.File]::ReadAllBytes($MetadataPath)
        try {
            if ([Convert]::ToBase64String($archivedBytes) -cne [Convert]::ToBase64String($extractedBytes)) {
                throw 'Extracted CaptureActive metadata differs from the authenticated guest-evidence archive member.'
            }
        }
        finally { [Array]::Clear($archivedBytes, 0, $archivedBytes.Length); [Array]::Clear($extractedBytes, 0, $extractedBytes.Length) }
    }
    finally { $archive.Dispose() }
}

function Assert-CombinedCaptureRawBundle($Capture) {
    $bundle = $Capture.rawEvidenceBundle
    if ($null -eq $bundle -or [string]$bundle.format -cne 'zip' -or
        [int]$bundle.bytes -le 0 -or [int]$bundle.bytes -gt 4718592 -or
        [string]$bundle.sha256 -cnotmatch '\A[A-Fa-f0-9]{64}\z' -or
        [int]$bundle.fileCount -ne @($Capture.capturedFiles).Count) {
        throw 'CaptureActive raw evidence bundle metadata is invalid or incomplete.'
    }
    $bytes = [Convert]::FromBase64String([string]$bundle.base64)
    try {
        if ($bytes.Length -ne [int]$bundle.bytes) { throw 'CaptureActive raw evidence ZIP size mismatch.' }
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $actualHash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
        finally { $sha.Dispose() }
        if ($actualHash -cne ([string]$bundle.sha256).ToUpperInvariant()) { throw 'CaptureActive raw evidence ZIP hash mismatch.' }
        $memory = [IO.MemoryStream]::new($bytes, $false)
        try {
            $archive = [IO.Compression.ZipArchive]::new($memory, [IO.Compression.ZipArchiveMode]::Read, $false)
            try {
                $entries = @($archive.Entries)
                if ($entries.Count -ne [int]$bundle.fileCount) { throw 'CaptureActive raw evidence ZIP entry count mismatch.' }
                $seen = @{}
                foreach ($file in @($Capture.capturedFiles)) {
                    $leaf = [string]$file.archiveLeaf
                    if ($leaf -notmatch '\Af\.[0-9]{4}\.[A-Za-z0-9]{1,10}\z' -or $seen.ContainsKey($leaf)) { throw 'CaptureActive raw evidence has an unsafe or duplicate member name.' }
                    $seen[$leaf] = $true
                    $entry = @($entries | Where-Object { [string]$_.FullName -ceq $leaf })
                    if ($entry.Count -ne 1 -or [long]$entry[0].Length -ne [long]$file.bytes) { throw "CaptureActive raw evidence member size mismatch: $leaf" }
                    $stream = $entry[0].Open(); $memberMemory = [IO.MemoryStream]::new()
                    try { $stream.CopyTo($memberMemory); $memberBytes = $memberMemory.ToArray() }
                    finally { $memberMemory.Dispose(); $stream.Dispose() }
                    try {
                        $memberSha = [Security.Cryptography.SHA256]::Create()
                        try { $memberHash = ([BitConverter]::ToString($memberSha.ComputeHash($memberBytes))).Replace('-', '') }
                        finally { $memberSha.Dispose() }
                        if ($memberHash -cne ([string]$file.sha256).ToUpperInvariant()) { throw "CaptureActive raw evidence member hash mismatch: $leaf" }
                    }
                    finally { [Array]::Clear($memberBytes, 0, $memberBytes.Length) }
                }
            }
            finally { $archive.Dispose() }
        }
        finally { $memory.Dispose() }
    }
    finally { [Array]::Clear($bytes, 0, $bytes.Length) }
}

function Get-CombinedFixtureMailboxHmacKey([string]$RepositoryRoot) {
    $repository = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
    if (-not $repository.Equals('C:\Users\jayli\OneDrive\Documents\Projects\hsinv', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Mailbox credentials may be read only from the authorized repository.'
    }
    $configPath = Join-Path $repository 'tools\host-runner\host-runner.config.json'
    $corePath = Join-Path $repository 'tools\host-runner\HostRunner.Core.psm1'
    foreach ($path in @($configPath, $corePath)) {
        [void](Assert-NoReparsePointComponents $repository $path 'Mailbox credential input')
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Existing mailbox credential input is missing: $path" }
    }
    $credential = $null
    $keyCredential = $null
    $plainBase64 = $null
    $key = $null
    try {
        Import-Module $corePath -Force -ErrorAction Stop
        $credential = Import-UhrConfig -ConfigPath $configPath
        if (-not [IO.Path]::GetFullPath([string]$credential.ProjectRoot).TrimEnd('\').Equals($repository, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Mailbox credential configuration ProjectRoot differs from the authorized repository.'
        }
        $stateRoot = [IO.Path]::GetFullPath([string]$credential.StateRoot).TrimEnd('\')
        if (-not $stateRoot.StartsWith(($repository + '\'), [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Mailbox credential StateRoot must remain inside the authorized repository.'
        }
        [void](Assert-NoReparsePointComponents $repository $stateRoot 'Mailbox credential state root')
        $credentialPath = Get-UhrCredentialPath -Config $credential -FileName 'windows-clone-mailbox-key.xml'
        [void](Assert-NoReparsePointComponents $repository $credentialPath 'Mailbox HMAC credential')
        $keyCredential = Get-UhrCredential -Config $credential -FileName 'windows-clone-mailbox-key.xml'
        $plainBase64 = [string]$keyCredential.GetNetworkCredential().Password
        try { $key = [Convert]::FromBase64String($plainBase64) }
        catch { throw 'Stored mailbox HMAC credential is not valid base64.' }
        if ($key.Length -ne 32) { throw 'Stored mailbox HMAC credential must decode to exactly 32 bytes.' }
        $copy = [byte[]]::new($key.Length)
        [Array]::Copy($key, $copy, $key.Length)
        return ,$copy
    } finally {
        if ($null -ne $key -and $key.Length -gt 0) { [Array]::Clear($key, 0, $key.Length) }
        $plainBase64 = $null
        if ($null -ne $keyCredential -and $null -ne $keyCredential.Password) { $keyCredential.Password.Dispose() }
        $keyCredential = $null
        $credential = $null
    }
}

function Assert-CombinedFixtureMailboxAuthentication($Response, $ResponseMetadata, [byte[]]$HmacKey) {
    if ($null -eq $Response -or $null -eq $ResponseMetadata -or $null -eq $HmacKey -or $HmacKey.Length -ne 32) {
        throw 'CaptureActive mailbox authentication inputs are incomplete.'
    }
    $verified = Assert-CombinedAssemblyResponseHmac -Response $Response -Key $HmacKey -Label 'CaptureActive mailbox response'
    if ([string]$verified.status -cne 'PASS' -or
        [string]$verified.runId -cne [string]$ResponseMetadata.RunId -or
        [string]$verified.responseHmacSha256 -cne [string]$ResponseMetadata.ResponseHmacSha256 -or
        [string]$Response.EvidenceSha256 -cne [string]$ResponseMetadata.EvidenceSha256 -or
        [string]$Response.Status -cne [string]$ResponseMetadata.Status) {
        throw 'CaptureActive response HMAC verification or authenticated receipt binding failed.'
    }
    return [ordered]@{
        method = 'HMAC-SHA256'
        verified = $true
        sessionId = [string]$verified.runId
        responseHmacSha256 = [string]$verified.responseHmacSha256
        evidenceSha256 = [string]$Response.EvidenceSha256
        verificationSource = 'Assert-CombinedAssemblyResponseHmac with existing DPAPI-protected mailbox key'
    }
}

function Get-VerifiedFixtureCapture([string]$RunDirectory, [string]$WorktreeReportsRoot, [string]$ExpectedUuid, [string]$ExpectedPatchedUuid, $ExpectedSmbios, [string]$ExpectedBuildId, [string]$ExpectedFixtureLifecycleId) {
    $runRoot = Assert-PathUnder $WorktreeReportsRoot $RunDirectory 'FixtureCaptureRunDirectory'
    if (-not (Test-Path -LiteralPath $runRoot -PathType Container)) { throw "CaptureActive mailbox run directory does not exist: $runRoot" }
    $requiredPaths = [ordered]@{
        response = Join-Path $runRoot 'response.json'
        responseMetadata = Join-Path $runRoot 'response-metadata.json'
        transportMetadata = Join-Path $runRoot 'transport-metadata.json'
        archive = Join-Path $runRoot 'guest-evidence.zip'
        metadata = Join-Path $runRoot 'evidence\kernel-metadata.json'
    }
    foreach ($name in @($requiredPaths.Keys)) {
        $path = Assert-NoReparsePointComponents $worktreeRoot ([string]$requiredPaths[$name]) ('CaptureActive ' + $name)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "CaptureActive mailbox evidence is missing: $path" }
        $requiredPaths[$name] = $path
    }
    $response = Get-Content -LiteralPath $requiredPaths.response -Raw | ConvertFrom-Json -ErrorAction Stop
    $responseMetadata = Get-Content -LiteralPath $requiredPaths.responseMetadata -Raw | ConvertFrom-Json -ErrorAction Stop
    $transport = Get-Content -LiteralPath $requiredPaths.transportMetadata -Raw | ConvertFrom-Json -ErrorAction Stop
    $capture = Get-Content -LiteralPath $requiredPaths.metadata -Raw | ConvertFrom-Json -ErrorAction Stop
    $expectedFixtureJournalPath = 'C:\ProgramData\CombinedVmFixture\' + $ExpectedUuid.Replace('-', '').ToLowerInvariant() + '\' + $ExpectedFixtureLifecycleId + '\corrected-collector.journal.json'
    if ([string]$capture.schema -cne 'combined-vm-fixture-correction-action.v1' -or
        [string]$capture.internalAction -cne 'CaptureActive' -or [string]$capture.mode -cne 'corrected-collector' -or
        [string]$capture.status -cne 'PASS' -or [string]$capture.vm.uuid -cne $ExpectedPatchedUuid -or
        [string]$capture.journal.state -cne 'CAPTURED_AND_CLEANED' -or
        -not ([string]$capture.journal.path).Equals($expectedFixtureJournalPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The authenticated CaptureActive payload is not for the pinned fixture lifecycle.'
    }
    $runId = [string]$response.RunId
    $mailboxKey = Get-CombinedFixtureMailboxHmacKey -RepositoryRoot $repo
    try {
        $mailboxAuthentication = Assert-CombinedFixtureMailboxAuthentication -Response $response -ResponseMetadata $responseMetadata -HmacKey $mailboxKey
    } finally { [Array]::Clear($mailboxKey, 0, $mailboxKey.Length) }
    if ($runId -cnotmatch '\Auefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\z' -or
        [string]$response.Status -cne 'PASS' -or
        [string]$response.ResponseHmacSha256 -cnotmatch '\A[A-F0-9]{64}\z' -or
        [string]$response.EvidenceSha256 -cnotmatch '\A[A-F0-9]{64}\z' -or
        [string]$responseMetadata.RunId -cne $runId -or [string]$responseMetadata.Status -cne 'PASS' -or
        [string]$responseMetadata.ResponseHmacSha256 -cne [string]$response.ResponseHmacSha256 -or
        [string]$responseMetadata.EvidenceSha256 -cne [string]$response.EvidenceSha256 -or
        [string]$responseMetadata.EvidenceArchive -cne 'guest-evidence.zip' -or
        [string]$transport.RunId -cne $runId -or [string]$transport.Action -cne 'Probe' -or
        [string]$transport.ResultStatus -cne 'PASS') {
        throw 'CaptureActive mailbox response, HMAC-bearing receipt, and transport metadata do not form one PASS run.'
    }
    $archiveHash = (Get-FileHash -LiteralPath $requiredPaths.archive -Algorithm SHA256).Hash.ToUpperInvariant()
    $archiveBytes = [long](Get-Item -LiteralPath $requiredPaths.archive).Length
    if ($archiveHash -cne [string]$response.EvidenceSha256 -or $archiveBytes -ne [long]$response.EvidenceSize -or
        $archiveHash -cne [string]$responseMetadata.EvidenceSha256 -or $archiveBytes -ne [long]$responseMetadata.EvidenceSize) {
        throw 'CaptureActive guest-evidence archive differs from its authenticated response manifest.'
    }
    Assert-CombinedCaptureArchive -ArchivePath $requiredPaths.archive -MetadataPath $requiredPaths.metadata

    $cleanupProperty = $capture.PSObject.Properties['cleanup']
    if ($null -eq $cleanupProperty -or $null -eq $cleanupProperty.Value) {
        throw 'The authenticated CaptureActive payload is missing its cleanup record.'
    }
    $cleanup = $cleanupProperty.Value
    foreach ($field in @('attempted', 'result', 'pathBStageRemoved', 'trackedEspBaselineRestored', 'errors')) {
        if ($null -eq $cleanup.PSObject.Properties[$field]) {
            throw "The authenticated CaptureActive cleanup record is missing '$field'."
        }
    }
    if ($cleanup.attempted -ne $true -or
        [string]$cleanup.result -cne 'OWNED_ENTRIES_STAGE_AND_TRACKED_ESP_RESTORED' -or
        $cleanup.pathBStageRemoved -ne $true -or
        $cleanup.trackedEspBaselineRestored -ne $true -or
        @($cleanup.errors).Count -ne 0) {
        throw 'The authenticated CaptureActive payload does not prove Path-B removal and tracked ESP baseline restoration.'
    }

    if ([string]$capture.schema -cne 'combined-vm-fixture-correction-action.v1' -or
        [string]$capture.internalAction -cne 'CaptureActive' -or [string]$capture.mode -cne 'corrected-collector' -or
        [string]$capture.status -cne 'PASS' -or [string]$capture.vm.uuid -cne $ExpectedPatchedUuid -or
        [string]$capture.pathBAction -cne 'Collect' -or [string]$capture.journal.state -cne 'CAPTURED_AND_CLEANED' -or
        -not ([string]$capture.journal.path).Equals($expectedFixtureJournalPath, [StringComparison]::OrdinalIgnoreCase) -or
        [string]$capture.journal.sha256 -cnotmatch '\A[A-Fa-f0-9]{64}\z') {
        throw 'The authenticated CaptureActive payload is not a completed corrected-collector PASS for the pinned fixture lifecycle.'
    }
    Assert-CombinedCaptureRawBundle $capture
    if ($capture.firmware.secureBoot -ne $false -or
        [string]$capture.efi.status -cne '0x0000000000000000' -or
        [string]$capture.efi.statusSource -cne 'prelude.report.child_load_status' -or
        [string]$capture.efi.statusPhase -cne 'LoadImage(loader.efi)' -or
        $capture.efi.startStatusObserved -ne $false -or
        [string]$capture.collectorReport.result -cne 'applied' -or [int]$capture.collectorReport.mask -ne 127 -or
        [string]$capture.collectorReport.buildId -cne $ExpectedBuildId -or
        [string]$capture.windowsReadback.expectedMode -cne 'replacement') {
        throw 'CaptureActive EFI, collector build, or readback status does not match the expected completed patch.'
    }
    foreach ($field in $ExpectedSmbios.Keys) {
        $replacement = [string]$ExpectedSmbios[$field]
        $patchValue = $capture.collectorReport.values.PSObject.Properties['SMBIOS:' + $field]
        $readback = $capture.windowsReadback.fields.PSObject.Properties[$field]
        if ($null -eq $patchValue -or [string]$patchValue.Value -cne $replacement -or
            $null -eq $readback -or [string]$readback.Value.native -cne $replacement -or
            [string]$readback.Value.wmi -cne $replacement -or $readback.Value.nativeMatchesWmi -ne $true) {
            throw "CaptureActive replacement readback mismatch for $field."
        }
    }

    $captureFacts = [ordered]@{
        schema = [string]$capture.schema
        internalAction = [string]$capture.internalAction
        mode = [string]$capture.mode
        status = [string]$capture.status
        pathBAction = [string]$capture.pathBAction
        vm = $capture.vm
        bootIdentity = $capture.bootIdentity
        firmware = $capture.firmware
        esp = $capture.esp
        windowsReadback = $capture.windowsReadback
        collectorReport = $capture.collectorReport
        efi = $capture.efi
        journal = $capture.journal
        cleanup = $capture.cleanup
    }
    return [ordered]@{
        schema = 'combined-vm-corrected-cleanup-input.v1'
        lifecycleId = $ExpectedFixtureLifecycleId
        vmUuid = $ExpectedUuid
        captureMetadataSha256 = (Get-FileHash -LiteralPath $requiredPaths.metadata -Algorithm SHA256).Hash.ToUpperInvariant()
        capture = $captureFacts
        mailbox = [ordered]@{
            authentication = $mailboxAuthentication
            runId = $runId
            responseSha256 = (Get-FileHash -LiteralPath $requiredPaths.response -Algorithm SHA256).Hash.ToUpperInvariant()
            responseMetadataSha256 = (Get-FileHash -LiteralPath $requiredPaths.responseMetadata -Algorithm SHA256).Hash.ToUpperInvariant()
            responseHmacSha256 = [string]$response.ResponseHmacSha256
            archivePath = $requiredPaths.archive
            archiveSha256 = $archiveHash
            archiveBytes = $archiveBytes
            authenticatedResponseReceiptPath = $requiredPaths.responseMetadata
            verifierPath = $script:MailboxVerifierPath
            verifierSha256 = (Get-FileHash -LiteralPath $script:MailboxVerifierPath -Algorithm SHA256).Hash.ToUpperInvariant()
            mailboxProtocolPath = (Join-Path $PSScriptRoot '..\device_vm\invoke-mailbox.ps1')
            mailboxProtocolSha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '..\device_vm\invoke-mailbox.ps1') -Algorithm SHA256).Hash.ToUpperInvariant()
            credentialResolverPath = (Join-Path $repo 'tools\host-runner\HostRunner.Core.psm1')
            credentialResolverSha256 = (Get-FileHash -LiteralPath (Join-Path $repo 'tools\host-runner\HostRunner.Core.psm1') -Algorithm SHA256).Hash.ToUpperInvariant()
        }
    }
}

$repo = Get-FullPath $ReferenceRepo
$authorizedReferenceRepo = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
if (-not $repo.Equals((Get-FullPath $authorizedReferenceRepo), [StringComparison]::OrdinalIgnoreCase)) {
    throw "ReferenceRepo must remain the authorized main checkout: $authorizedReferenceRepo"
}
$script:ReferenceRepoFullPath = $repo
$reportsRoot = Join-Path $repo 'spoof-handoff\reports'
$artifactsRoot = Join-Path $repo 'spoof-handoff\artifacts'
$worktreeRoot = Join-Path $repo '.codex-build\worktrees\smbios-boot-serial-probe'
$worktreeRoot = Assert-NoReparsePointComponents $repo $worktreeRoot 'WorktreeRoot'
$worktreeReportsRoot = Join-Path $worktreeRoot 'spoof-handoff\reports'
$f6ReportsRoot = Join-Path $worktreeReportsRoot 'f6'
$worktreeArtifactsRoot = Join-Path $worktreeRoot 'spoof-handoff\artifacts'
$f6ArtifactsRoot = Join-Path $worktreeArtifactsRoot 'f6'
$expectedSharedAssets = Join-Path $f6ArtifactsRoot 'shared-readonly'
$expectedOutput = Join-Path $f6ReportsRoot 'runtime\lifecycle'
$guestInventoryPathFull = Assert-PathUnderRoots @($reportsRoot, $f6ReportsRoot) $GuestInventoryPath 'GuestInventoryPath'
$storageManifestPathFull = Assert-PathUnderRoots @($reportsRoot, $f6ReportsRoot, $artifactsRoot, $f6ArtifactsRoot) $StorageFixtureManifestPath 'StorageFixtureManifestPath'
$smbiosManifestPathFull = Assert-PathUnderRoots @($artifactsRoot, $f6ArtifactsRoot) $SmbiosFixtureManifestPath 'SmbiosFixtureManifestPath'
$script:SharedAssetsFullPath = Assert-ExactPath $SharedAssetsDirectory $expectedSharedAssets 'SharedAssetsDirectory'
$output = Assert-ExactPath $OutputDirectory $expectedOutput 'OutputDirectory'
if (-not (Test-Path -LiteralPath $script:SharedAssetsFullPath -PathType Container)) {
    throw "SharedAssetsDirectory does not exist: $script:SharedAssetsFullPath"
}
$script:SharedAssetsFullPath = Assert-NoReparsePointComponents $worktreeRoot $script:SharedAssetsFullPath 'SharedAssetsDirectory'
$output = Assert-NoReparsePointComponents $worktreeRoot $output 'OutputDirectory'

$inventoryText = Get-Content -LiteralPath $guestInventoryPathFull -Raw
$guestInventory = $inventoryText | ConvertFrom-Json
if ($guestInventory.schema -cne 'vm-device-inventory.v1' -or
    [string]$guestInventory.machine.manufacturer -cne 'VMware, Inc.' -or
    [string]$guestInventory.machine.model -cne 'VMware20,1' -or
    [string]$guestInventory.disk.pnpDeviceId -cnotmatch '^SCSI\\DISK&VEN_NVME&PROD_VMWARE_VIRTUAL_N\\' -or
    [string]$guestInventory.disk.serial -cne [string]$guestInventory.disk.wmiSerial -or
    [bool]$guestInventory.disk.isBoot -ne $true -or [bool]$guestInventory.disk.isSystem -ne $true) {
    throw 'Fresh VMware20,1 guest inventory with matching boot/system disk native and WMI values is required.'
}
$guestUuid = [Guid]::Empty
if (-not [Guid]::TryParse([string]$guestInventory.machine.uuid, [ref]$guestUuid)) {
    throw 'Fresh VM inventory UUID is invalid.'
}
$guestUuid = $guestUuid.ToString('D').ToUpperInvariant()

$storageManifestText = Get-Content -LiteralPath $storageManifestPathFull -Raw
$storageManifest = $storageManifestText | ConvertFrom-Json
if ($storageManifest.schema -cne 'combined-vm-storage-fixture.v1' -or
    $storageManifest.status -cne 'SIGNED_FIXTURES_READY' -or
    [string]$storageManifest.guestInventorySha256 -cne (Get-FileHash -LiteralPath $guestInventoryPathFull -Algorithm SHA256).Hash.ToUpperInvariant() -or
    [string]$storageManifest.machine.uuid -cne $guestUuid -or
    [string]$storageManifest.disk.pnpDeviceId -cne [string]$guestInventory.disk.pnpDeviceId -or
    [string]$storageManifest.disk.originalSerial -cne [string]$guestInventory.disk.serial -or
    [string]$storageManifest.disk.wmiOriginalSerial -cne [string]$guestInventory.disk.wmiSerial) {
    throw 'Storage fixture manifest does not match the fresh VMware guest inventory.'
}
foreach ($mode in @('transparent', 'enabled')) {
    $profile = $storageManifest.packages.$mode
    if ($null -eq $profile -or [int]$profile.startType -ne 0 -or
        [string]$profile.mode -cne $mode -or
        $profile.embeddedSignatureValid -ne $true -or $profile.embeddedPinnedSignerValid -ne $true -or
        $profile.catalogSignatureValid -ne $true -or $profile.catalogPinnedSignerValid -ne $true -or
        $profile.driverInstalledOrLoaded -ne $false -or $profile.bootConfigurationChanged -ne $false) {
        throw "Storage fixture $mode profile does not satisfy the boot-start signed package contract."
    }
}
$smbiosManifestText = Get-Content -LiteralPath $smbiosManifestPathFull -Raw
$smbiosManifest = $smbiosManifestText | ConvertFrom-Json
if ($smbiosManifest.schema -cne 'combined-vm-smbios-fixture.v1' -or
    $smbiosManifest.result -cne 'PASS' -or
    $smbiosManifest.mode -cne 'vm_only_smbios_fixture' -or
    [string]$smbiosManifest.guestCapture.sourceMetadata.sha256 -notmatch '^[A-Fa-f0-9]{64}$') {
    throw 'The VM-only SMBIOS fixture manifest is invalid or lacks source provenance.'
}
$capturePath = Assert-PathUnderRoots @($reportsRoot, $f6ReportsRoot) ([string]$smbiosManifest.guestCapture.path) 'SMBIOS guest capture'
if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf) -or
    (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash.ToUpperInvariant() -cne
        ([string]$smbiosManifest.guestCapture.sha256).ToUpperInvariant()) {
    throw 'The hash-pinned fresh VM SMBIOS capture is missing or changed.'
}
$capture = Get-Content -LiteralPath $capturePath -Raw | ConvertFrom-Json
if ([string]$capture.fields.system_uuid.observedNative -cne $guestUuid -or
    [string]$capture.fields.system_uuid.observedWmi -cne $guestUuid -or
    [string]$capture.fields.system_vendor.observedNative -cne 'VMware, Inc.' -or
    [string]$capture.fields.system_product.observedNative -cne 'VMware20,1') {
    throw 'Fresh SMBIOS fixture capture does not match the same VMware guest UUID and model.'
}

$mapPath = Join-Path $repo 'spoof-handoff\reports\client-originals.tsv'
$mapRows = Get-MapRows $mapPath
$smbiosMap = [ordered]@{
    system_serial = 'FakeSMBIOSSerial'
    system_vendor = 'FakeSMBIOSManufacturer'
    system_product = 'FakeSMBIOSProduct'
    board_serial = 'FakeMotherboardSerial'
    board_vendor = 'FakeMotherboardManufacturer'
    board_product = 'FakeMotherboardProduct'
    system_uuid = 'FakeSMBIOSUUID'
}
$smbiosOriginals = [ordered]@{}
$smbiosReplacements = [ordered]@{}
foreach ($field in $smbiosMap.Keys) {
    $entry = $capture.fields.PSObject.Properties[$field]
    if ($null -eq $entry -or $entry.Value.nativeMatchesWmi -ne $true -or
        [string]$entry.Value.observedNative -cne [string]$entry.Value.observedWmi) {
        throw "Fresh SMBIOS fixture has mismatched native/WMI values for $field."
    }
    $smbiosOriginals[$field] = [string]$entry.Value.observedNative
    $smbiosReplacements[$field] = Get-UniqueReplacement $mapRows $smbiosMap[$field]
}
if ($smbiosReplacements.system_vendor -cne 'NORTHLAB' -or
    $smbiosReplacements.system_product -cne 'NODE' -or
    $smbiosReplacements.system_uuid -cne 'A17F24D0-68E2-4A09-9C31-2B7D8450E6F9' -or
    [string]$storageManifest.disk.replacementSerial -cne (Get-UniqueReplacement $mapRows 'FakeDiskSerial')) {
    throw 'Fixture replacements drifted from the locked client-originals.tsv values.'
}
$smbiosBuildIdentity = [string]$smbiosManifest.buildIdentity.value
if ($smbiosBuildIdentity -cnotmatch '\A[A-F0-9]{32}\z') { throw 'The SMBIOS fixture manifest does not carry a pinned 32-character build identity.' }
$fixtureCorrectionInput = $null
if ($Action -ceq 'VerifyCorrectedCollectorCleanup') {
    if ([string]::IsNullOrWhiteSpace($FixtureCaptureRunDirectory)) { throw 'VerifyCorrectedCollectorCleanup requires the completed CaptureActive mailbox run directory.' }
    $fixtureCorrectionInput = Get-VerifiedFixtureCapture -RunDirectory $FixtureCaptureRunDirectory -WorktreeReportsRoot $worktreeReportsRoot -ExpectedUuid $guestUuid -ExpectedPatchedUuid $smbiosReplacements.system_uuid -ExpectedSmbios $smbiosReplacements -ExpectedBuildId $smbiosBuildIdentity -ExpectedFixtureLifecycleId 'smbios2-ep-correction-f12-r2-release-20261018'
} elseif (-not [string]::IsNullOrWhiteSpace($FixtureCaptureRunDirectory)) {
    throw 'FixtureCaptureRunDirectory is accepted only after a completed CaptureActive action for VerifyCorrectedCollectorCleanup.'
}

$sourcePaths = [ordered]@{}
foreach ($mode in @('transparent', 'enabled')) {
    foreach ($file in @($storageManifest.packages.$mode.files)) {
        if ([string]$file.name -notin @('client_storage_filter.inf', 'client_storage_filter.sys', 'client_storage_filter.cat')) {
            throw "Unexpected storage package file: $($file.name)"
        }
        if ([string]$file.path -notmatch '\\package\\[^\\]+$') { throw 'Storage package file path is outside its generated package directory.' }
        $sourcePaths[$mode + '-' + [string]$file.name] = [string]$file.path
    }
}
$sourcePaths['smbios_windows_readback.exe'] = [string]$smbiosManifest.windowsReadback
$sourcePaths['smbios-inventory.efi'] = [string]$smbiosManifest.collector
$loaderBuild = Join-Path $repo '.codex-build\efi_inventory_loader'
$inventoryBuild = Join-Path $repo '.codex-build\efi_inventory'
$sourcePaths['inventory_loader.efi'] = Join-Path $loaderBuild 'inventory_loader.efi'
$sourcePaths['ib-install.exe'] = Join-Path $loaderBuild 'ib-install.exe'
$sourcePaths['efi_inventory.exe'] = Join-Path $inventoryBuild 'efi_inventory.exe'
$sourcePaths['path-b-collector.ps1'] = Join-Path $repo 'tools\efi_inventory\path-b-collector.ps1'
$sourcePaths['storage-read.ps1'] = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\storage_host\read.ps1'))
$sourcePaths['storage-model.psm1'] = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\storage_host\model.psm1'))
$sourcePaths['hypersinv.cer'] = Join-Path $repo 'components\hypervisor\hypersinv.cer'
if ((Get-FileHash -LiteralPath $sourcePaths['hypersinv.cer'] -Algorithm SHA256).Hash.ToUpperInvariant() -cne
    'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE') {
    throw 'The public project certificate differs from its pinned SHA-256.'
}

$assets = [Collections.Generic.List[object]]::new()
foreach ($name in $sourcePaths.Keys) {
    [void](Add-PinnedAsset $assets $name ([string]$sourcePaths[$name]))
}
$assetLookup = [ordered]@{}
foreach ($asset in $assets) { $assetLookup[[string]$asset.name] = $asset }
$config = [ordered]@{
    schema = 'combined-vm-lifecycle-config.v1'
    action = $Action
    vmIdentity = [ordered]@{
        original = [ordered]@{ manufacturer = 'VMware, Inc.'; model = 'VMware20,1'; uuid = $guestUuid }
        patched = [ordered]@{ manufacturer = $smbiosReplacements.system_vendor; model = $smbiosReplacements.system_product; uuid = $smbiosReplacements.system_uuid }
    }
    disk = [ordered]@{
        pnpDeviceId = [string]$storageManifest.disk.pnpDeviceId
        hardwareIds = @($guestInventory.disk.hardwareIds | ForEach-Object { [string]$_ })
        number = [int]$storageManifest.disk.number
        originalNative = [string]$storageManifest.disk.originalSerial
        originalWmi = [string]$storageManifest.disk.wmiOriginalSerial
        replacement = [string]$storageManifest.disk.replacementSerial
        windowsBuild = [int]$storageManifest.disk.windowsBuild
        windowsUbr = [int]$storageManifest.disk.windowsUbr
    }
    smbios = [ordered]@{ originals = $smbiosOriginals; replacements = $smbiosReplacements }
    initialPolicy = [ordered]@{ secureBoot = $false; testSigning = 'no' }
    signer = [ordered]@{ thumbprint = [string]$storageManifest.signerThumbprint; certificateSha256 = [string]$assetLookup['hypersinv.cer'].sha256 }
    packages = [ordered]@{}
    assets = @($assets.ToArray())
    lifecycleId = $LifecycleId
    stateRoot = ('C:\ProgramData\CombinedVmLifecycle\' + $guestUuid.Replace('-', '').ToLowerInvariant() + '\' + $LifecycleId)
    pathBMountLetter = 'Z'
    shareRoot = '\\vmware-host\Shared Folders\CombinedFixture'
    expectedSmbiosSourceMetadataSha256 = ([string]$smbiosManifest.guestCapture.sourceMetadata.sha256).ToUpperInvariant()
    replacementMap = [ordered]@{ path = $mapPath; sha256 = (Get-FileHash -LiteralPath $mapPath -Algorithm SHA256).Hash.ToUpperInvariant() }
    fixturePins = [ordered]@{
        guestInventoryPath = $guestInventoryPathFull
        guestInventorySha256 = (Get-FileHash -LiteralPath $guestInventoryPathFull -Algorithm SHA256).Hash.ToUpperInvariant()
        storageFixtureManifestPath = $storageManifestPathFull
        storageFixtureManifestSha256 = (Get-FileHash -LiteralPath $storageManifestPathFull -Algorithm SHA256).Hash.ToUpperInvariant()
        smbiosFixtureManifestPath = $smbiosManifestPathFull
        smbiosFixtureManifestSha256 = (Get-FileHash -LiteralPath $smbiosManifestPathFull -Algorithm SHA256).Hash.ToUpperInvariant()
        smbiosCapturePath = $capturePath
        smbiosCaptureSha256 = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash.ToUpperInvariant()
        smbiosBuildIdentity = $smbiosBuildIdentity
    }
    fixtureCorrection = $fixtureCorrectionInput
}
foreach ($mode in @('transparent', 'enabled')) {
    $config.packages[$mode] = [ordered]@{
        mode = [string]$storageManifest.packages.$mode.mode
        startType = [int]$storageManifest.packages.$mode.startType
        signerThumbprint = [string]$storageManifest.packages.$mode.signerThumbprint
        inf = [string]$assetLookup[$mode + '-client_storage_filter.inf'].uncPath
        sys = [string]$assetLookup[$mode + '-client_storage_filter.sys'].uncPath
        cat = [string]$assetLookup[$mode + '-client_storage_filter.cat'].uncPath
        files = @(
            foreach ($suffix in @('inf','sys','cat')) {
                $name = $mode + '-client_storage_filter.' + $suffix
                [ordered]@{ name = 'client_storage_filter.' + $suffix; uncPath = [string]$assetLookup[$name].uncPath; sha256 = [string]$assetLookup[$name].sha256; sourceName = $name }
            }
        )
    }
}
$config.assets = @($assets.ToArray())
$configBytes = [Text.Encoding]::UTF8.GetBytes(($config | ConvertTo-Json -Depth 20 -Compress))
$configBase64 = [Convert]::ToBase64String($configBytes)
$templatePath = Join-Path $PSScriptRoot 'guest-lifecycle.template.ps1'
if (-not (Test-Path -LiteralPath $templatePath -PathType Leaf)) { throw "Guest lifecycle template is missing: $templatePath" }
$template = [IO.File]::ReadAllText($templatePath)
$token = '__LIFECYCLE_CONFIG_BASE64__'
if (([regex]::Matches($template, [regex]::Escape($token))).Count -ne 1) { throw 'Lifecycle config placeholder is missing or duplicated.' }
$scriptText = $template.Replace($token, $configBase64)
if ([regex]::IsMatch($scriptText, '__[A-Z0-9_]+__')) { throw 'Rendered lifecycle script retains an unresolved placeholder.' }
$tokens = $null
$parseErrors = $null
[void][Management.Automation.Language.Parser]::ParseInput($scriptText, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -gt 0) { throw ('Rendered lifecycle script has PowerShell parse errors: ' + (($parseErrors | ForEach-Object { $_.Message }) -join '; ')) }
[void][IO.Directory]::CreateDirectory($output)
$output = Assert-NoReparsePointComponents $worktreeRoot $output 'OutputDirectory'
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$scriptPath = Join-Path $output ('guest-lifecycle.' + $Action + '.' + $stamp + '.' + [Guid]::NewGuid().ToString('N') + '.ps1')
[IO.File]::WriteAllText($scriptPath, $scriptText, [Text.UTF8Encoding]::new($false))
$scriptHash = (Get-FileHash -LiteralPath $scriptPath -Algorithm SHA256).Hash.ToUpperInvariant()
$configPath = [IO.Path]::ChangeExtension($scriptPath, '.config.json')
$receiptPath = [IO.Path]::ChangeExtension($scriptPath, '.render.json')
[IO.File]::WriteAllText($configPath, ($config | ConvertTo-Json -Depth 20) + "`r`n", [Text.UTF8Encoding]::new($false))
$receipt = [ordered]@{
    schema = 'combined-vm-lifecycle-render.v1'
    action = $Action
    lifecycleId = $LifecycleId
    vmUuid = $guestUuid
    outputDirectory = $output
    templatePath = $templatePath
    templateSha256 = (Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash.ToUpperInvariant()
    renderedScriptPath = $scriptPath
    renderedScriptSha256 = $scriptHash
    renderedConfigPath = $configPath
    renderedConfigSha256 = (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToUpperInvariant()
    guestInventoryPath = $guestInventoryPathFull
    guestInventorySha256 = [string]$config.fixturePins.guestInventorySha256
    storageFixtureManifestPath = $storageManifestPathFull
    storageFixtureManifestSha256 = [string]$config.fixturePins.storageFixtureManifestSha256
    smbiosFixtureManifestPath = $smbiosManifestPathFull
    smbiosFixtureManifestSha256 = [string]$config.fixturePins.smbiosFixtureManifestSha256
    sourceMetadataSha256 = $config.expectedSmbiosSourceMetadataSha256
    replacementMapPath = $mapPath
    replacementMapSha256 = [string]$config.replacementMap.sha256
    fixtureCapture = if ($null -ne $fixtureCorrectionInput) { $fixtureCorrectionInput.mailbox } else { $null }
    assets = @($assets.ToArray())
    renderedUtc = [DateTime]::UtcNow.ToString('o')
}
[IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 20) + "`r`n", [Text.UTF8Encoding]::new($false))
Write-Output "RENDERED VM LIFECYCLE ACTION: $Action"
Write-Output "VM UUID: $guestUuid"
Write-Output "GUEST SCRIPT: $scriptPath"
Write-Output "SCRIPT SHA256: $scriptHash"
Write-Output "CONFIG: $configPath"
Write-Output "RENDER RECEIPT: $receiptPath"
