[CmdletBinding()]
param(
    [ValidateSet(
        'Doctor', 'Build', 'CredentialStatus', 'StoreGuestCredential',
        'StoreVmCredential', 'RemoveGuestCredential', 'RemoveVmCredential',
        'RunInstallerHostGate', 'RunInstallerOneShotHostGate',
        'RunAuditHostGate', 'RunFixtureHostGate',
        'RunLinuxPositivePath', 'RunLinuxReplayDryRun', 'RunLinuxPeRehash',
        'RunLinuxDivergenceManifest', 'RunLinuxHandoffMechanics',
        'RunLinuxProductionHandoff', 'RunLinuxOneShotCycle',
        'RunLinuxNestedDiskProbe', 'RunLinuxNestedWindowsE2E',
        'RunWslNestedWindowsE2E', 'RunWslBootNextE2E',
        'RunWindowsProbe', 'RunWindowsCapture', 'RunWindowsNestedPrep',
        'CreateWindowsSnapshot',
        'ClearQuarantine')]
    [string]$Action = 'Doctor',

    [ValidateSet('Linux', 'Windows', 'All')]
    [string]$Profile = 'All',

    [string]$ConfigPath,
    [ValidatePattern('^[A-Za-z0-9._-]{1,64}$')]
    [string]$WslDistribution = 'Ubuntu',
    [switch]$SkipBuild,
    [switch]$AuthorizeLiveVmRun,
    [switch]$AuthorizeVmStop,
    [switch]$AuthorizeSnapshotRevert,
    [switch]$AuthorizeSnapshotCreate,
    [string]$SnapshotName,
    [switch]$AuthorizeCredentialRemoval,
    [switch]$AuthorizeQuarantineClear
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $PSScriptRoot 'host-runner.config.json'
}
Import-Module (Join-Path $PSScriptRoot 'HostRunner.Core.psm1') -Force
$Config = Import-UhrConfig -ConfigPath $ConfigPath

function Get-ProfileConfig {
    param([Parameter(Mandatory=$true)][ValidateSet('Linux', 'Windows')][string]$Name)
    $Value = $Config.Profiles.$Name
    if (-not $Value.Enabled) { throw "The $Name profile is disabled." }
    return $Value
}

function Test-SharedFolderMailbox {
    param([Parameter(Mandatory=$true)]$Selected)
    return $null -ne $Selected.PSObject.Properties['GuestTransport'] -and
        [string]$Selected.GuestTransport -ceq 'SharedFolderMailbox'
}

function Get-NestedPreparedDiskRoot {
    $Root = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA `
        'UEFILoader\PreparedDisks'))
    $ExpectedParent = [IO.Path]::GetFullPath($env:LOCALAPPDATA)
    if (-not (Test-UhrPathWithin -Root $ExpectedParent -Path $Root)) {
        throw 'The nested E2E disk root escaped LocalAppData.'
    }
    return $Root
}

function Get-LatestNestedPreparedDisk {
    $Root = Get-NestedPreparedDiskRoot
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw 'No prepared standalone Windows VMDK directory is available.'
    }
    $Candidates = @(Get-ChildItem -LiteralPath $Root -File `
        -Filter 'uefi-windows-capture-*.vmdk' | Where-Object {
            $_.Length -gt 1GB -and $_.BaseName -cmatch `
            '^uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$'
        } | Sort-Object LastWriteTimeUtc -Descending)
    if ($Candidates.Count -lt 1) {
        throw 'No prepared standalone Windows VMDK is available.'
    }
    return $Candidates[0]
}

function Test-VmwareGuestInfo {
    param([Parameter(Mandatory=$true)]$Selected)
    return $null -ne $Selected.PSObject.Properties['GuestTransport'] -and
        [string]$Selected.GuestTransport -ceq 'VmwareGuestInfo'
}

function Assert-ConfiguredWindowsMailbox {
    param([Parameter(Mandatory=$true)]$Selected)
    if (-not (Test-SharedFolderMailbox $Selected)) { return }
    $MailboxRoot = [IO.Path]::GetFullPath([string]$Selected.MailboxHostRoot)
    if (-not (Test-Path -LiteralPath $MailboxRoot -PathType Container)) {
        throw "The configured Windows mailbox directory does not exist: $MailboxRoot"
    }
    if (((Get-Item -LiteralPath $MailboxRoot).Attributes -band
            [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'The configured Windows mailbox root may not be a reparse point.'
    }
    $Entries = @{}
    foreach ($Line in Get-Content -LiteralPath ([string]$Selected.VmxPath)) {
        if ($Line -cmatch '^([A-Za-z0-9_.:]+)\s*=\s*"(.*)"\s*$') {
            $Entries[$Matches[1]] = $Matches[2]
        }
    }
    $Matched = $false
    for ($Index = 0; $Index -lt 32; $Index++) {
        $Prefix = "sharedFolder$Index"
        if ($Entries["$Prefix.guestName"] -cne 'UEFILoaderRunner') {
            continue
        }
        $HostPath = [IO.Path]::GetFullPath([string]$Entries["$Prefix.hostPath"])
        if (-not $HostPath.Equals(
                $MailboxRoot, [StringComparison]::OrdinalIgnoreCase) -or
            $Entries["$Prefix.present"] -cne 'TRUE' -or
            $Entries["$Prefix.enabled"] -cne 'TRUE' -or
            $Entries["$Prefix.writeAccess"] -cne 'TRUE') {
            throw 'The UEFILoaderRunner shared folder mapping is incomplete or points to the wrong host path.'
        }
        $Matched = $true
        break
    }
    if (-not $Matched) {
        throw 'The clone VMX does not contain the dedicated UEFILoaderRunner shared folder.'
    }
}

function Get-QuarantinePath {
    param([Parameter(Mandatory=$true)][ValidateSet('Linux', 'Windows')][string]$Name)
    return Join-Path $Config.StateRoot ("QUARANTINED-{0}.json" -f $Name.ToLowerInvariant())
}

function Assert-NotQuarantined {
    param([Parameter(Mandatory=$true)][ValidateSet('Linux', 'Windows')][string]$Name)
    $Path = Get-QuarantinePath $Name
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        throw "The $Name profile is quarantined. Inspect the VM and $Path before clearing it."
    }
}

function Get-CredentialSummary {
    param([Parameter(Mandatory=$true)][ValidateSet('Linux', 'Windows')][string]$Name)
    $Selected = Get-ProfileConfig $Name
    $GuestCredentialRequired = -not (
        (Test-SharedFolderMailbox $Selected) -or
        (Test-VmwareGuestInfo $Selected))
    [pscustomobject]@{
        Profile = $Name
        GuestTransport = if ($null -ne $Selected.PSObject.Properties['GuestTransport']) {
            [string]$Selected.GuestTransport
        } else { 'VmrunGuest' }
        GuestCredentialRequired = $GuestCredentialRequired
        GuestCredential = $GuestCredentialRequired -and
            (Test-UhrCredential -Config $Config `
                -FileName ([string]$Selected.GuestCredentialFile))
        MailboxCredential = ((Test-SharedFolderMailbox $Selected) -or
            (Test-VmwareGuestInfo $Selected)) -and
            (Test-UhrCredential -Config $Config `
                -FileName ([string]$Selected.MailboxCredentialFile))
        VmCredentialRequired = -not [string]::IsNullOrWhiteSpace(
            [string]$Selected.VmCredentialFile)
        VmCredential = Test-UhrCredential -Config $Config `
            -FileName ([string]$Selected.VmCredentialFile)
    }
}

function Assert-ProfileCredentialsReady {
    param([Parameter(Mandatory=$true)]$Selected)
    if (-not (Test-SharedFolderMailbox $Selected) -and
        -not (Test-VmwareGuestInfo $Selected)) {
        $Guest = Get-UhrCredential -Config $Config `
            -FileName ([string]$Selected.GuestCredentialFile)
        if ($Guest.UserName -cne [string]$Selected.GuestUser) {
            throw "Stored guest credential user '$($Guest.UserName)' does not match '$($Selected.GuestUser)'."
        }
        $Guest = $null
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$Selected.VmCredentialFile)) {
        [void](Get-UhrCredential -Config $Config `
            -FileName ([string]$Selected.VmCredentialFile))
    }
    if ((Test-SharedFolderMailbox $Selected) -or (Test-VmwareGuestInfo $Selected)) {
        [void](Get-MailboxKeyBytes $Selected)
        if (Test-SharedFolderMailbox $Selected) {
            Assert-ConfiguredWindowsMailbox $Selected
        }
    }
}

function Get-MailboxKeyBytes {
    param([Parameter(Mandatory=$true)]$Selected)
    $Credential = Get-UhrCredential -Config $Config `
        -FileName ([string]$Selected.MailboxCredentialFile)
    try {
        $Bytes = [Convert]::FromBase64String(
            $Credential.GetNetworkCredential().Password)
    }
    catch {
        throw 'The stored Windows mailbox key is malformed.'
    }
    finally { $Credential = $null }
    if ($Bytes.Length -ne 32) {
        if ($null -ne $Bytes) { [Array]::Clear($Bytes, 0, $Bytes.Length) }
        throw 'The stored Windows mailbox key must contain exactly 32 bytes.'
    }
    return $Bytes
}

function Get-HmacSha256Hex {
    param(
        [Parameter(Mandatory=$true)][byte[]]$Key,
        [Parameter(Mandatory=$true)][string]$Text
    )
    $Data = [Text.Encoding]::UTF8.GetBytes($Text)
    $Hmac = [Security.Cryptography.HMACSHA256]::new($Key)
    try {
        return ([BitConverter]::ToString(
            $Hmac.ComputeHash($Data))).Replace('-', '')
    }
    finally {
        $Hmac.Dispose()
        [Array]::Clear($Data, 0, $Data.Length)
    }
}

function Test-FixedHexEqual {
    param([string]$Left, [string]$Right)
    if ($Left -cnotmatch '^[0-9A-F]{64}$' -or
        $Right -cnotmatch '^[0-9A-F]{64}$') { return $false }
    $Difference = 0
    for ($Index = 0; $Index -lt 64; $Index++) {
        $Difference = $Difference -bor (
            [int][char]$Left[$Index] -bxor [int][char]$Right[$Index])
    }
    return $Difference -eq 0
}

function Get-CanonicalMailboxRequest {
    param($Request)
    return @(
        "SchemaVersion=$($Request.SchemaVersion)"
        "RunId=$($Request.RunId)"
        "Action=$($Request.Action)"
        "ExpectedProbeVerdict=$($Request.ExpectedProbeVerdict)"
        "PackageRelativePath=$($Request.PackageRelativePath)"
        "PackageSha256=$($Request.PackageSha256)"
        "TimeoutSeconds=$($Request.TimeoutSeconds)"
        "SubmittedUtc=$($Request.SubmittedUtc)"
    ) -join "`n"
}

function Get-CanonicalMailboxResponse {
    param($Result)
    $Lines = [Collections.Generic.List[string]]::new()
    $Lines.Add("SchemaVersion=$($Result.SchemaVersion)")
    $Lines.Add("RunId=$($Result.RunId)")
    $Lines.Add("Status=$($Result.Status)")
    $MessageBytes = [Text.Encoding]::UTF8.GetBytes([string]$Result.Message)
    try { $Lines.Add('MessageBase64=' + [Convert]::ToBase64String($MessageBytes)) }
    finally { [Array]::Clear($MessageBytes, 0, $MessageBytes.Length) }
    $Lines.Add("CompletedUtc=$($Result.CompletedUtc)")
    foreach ($Evidence in @($Result.Evidence | Sort-Object Name)) {
        $EvidenceName = [string]$Evidence.PSObject.Properties['Name'].Value
        $Lines.Add("Evidence=$EvidenceName|$($Evidence.Sha256)|$($Evidence.Length)")
    }
    return $Lines -join "`n"
}

function Get-WindowsMailboxPaths {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$RunId
    )
    if (-not (Test-UhrRunId $RunId) -or
        $RunId -cnotmatch '^uefi-windows-(probe|capture|nested-prep)-') {
        throw "Unsafe Windows mailbox RunId: $RunId"
    }
    $Root = [IO.Path]::GetFullPath([string]$Selected.MailboxHostRoot)
    return [pscustomobject]@{
        Root = $Root
        Requests = Join-Path $Root 'requests'
        Artifacts = Join-Path $Root 'artifacts'
        Responses = Join-Path $Root 'responses'
        Request = Join-Path (Join-Path $Root 'requests') "$RunId.json"
        ArtifactDirectory = Join-Path (Join-Path $Root 'artifacts') $RunId
        Package = Join-Path (Join-Path (Join-Path $Root 'artifacts') $RunId) `
            'package.zip'
        ResponseDirectory = Join-Path (Join-Path $Root 'responses') $RunId
        Result = Join-Path (Join-Path (Join-Path $Root 'responses') $RunId) `
            'result.json'
    }
}

function Publish-WindowsMailboxRequest {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)]$Paths,
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)][ValidateSet('Probe', 'Capture')]
        [string]$GuestAction
    )
    $Mailbox = Get-WindowsMailboxPaths -Selected $Selected -RunId $RunId
    foreach ($Directory in @(
            $Mailbox.Root, $Mailbox.Requests, $Mailbox.Artifacts,
            $Mailbox.Responses)) {
        if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
            New-Item -ItemType Directory -Path $Directory | Out-Null
        }
    }
    foreach ($Path in @(
            $Mailbox.Request, $Mailbox.ArtifactDirectory,
            $Mailbox.ResponseDirectory)) {
        if (Test-Path -LiteralPath $Path) {
            throw "Windows mailbox RunId has already been published: $RunId"
        }
    }
    New-Item -ItemType Directory -Path $Mailbox.ArtifactDirectory | Out-Null
    [IO.Compression.ZipFile]::CreateFromDirectory(
        [string]$Paths.Stage, $Mailbox.Package,
        [IO.Compression.CompressionLevel]::Optimal, $false)
    $PackageHash = (Get-FileHash -LiteralPath $Mailbox.Package `
        -Algorithm SHA256).Hash
    $Request = [ordered]@{
        SchemaVersion = 1
        RunId = $RunId
        Action = $GuestAction
        ExpectedProbeVerdict = [string]$Selected.ExpectedProbeVerdict
        PackageRelativePath = "artifacts/$RunId/package.zip"
        PackageSha256 = $PackageHash
        TimeoutSeconds = [Math]::Min(
            [int]$Config.Policy.OperationTimeoutSeconds, 600)
        SubmittedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $Key = Get-MailboxKeyBytes $Selected
    try {
        $Request.RequestHmacSha256 = Get-HmacSha256Hex -Key $Key `
            -Text (Get-CanonicalMailboxRequest $Request)
    }
    finally { [Array]::Clear($Key, 0, $Key.Length) }
    Write-UhrAtomicJson -Path $Mailbox.Request -Value $Request
    return $Mailbox
}

function Wait-WindowsMailboxResult {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)]$Mailbox,
        [Parameter(Mandatory=$true)][string]$RunId
    )
    $Deadline = [DateTime]::UtcNow.AddSeconds(
        [Math]::Min([int]$Config.Policy.OperationTimeoutSeconds, 600) + 60)
    $PickupDeadline = [DateTime]::UtcNow.AddSeconds(90)
    $ProcessingRequest = $Mailbox.Request + '.processing'
    $CompletedRequest = $Mailbox.Request + '.completed'
    $PickedUp = $false
    while ([DateTime]::UtcNow -lt $Deadline) {
        if (Test-Path -LiteralPath $Mailbox.Result -PathType Leaf) {
            $Item = Get-Item -LiteralPath $Mailbox.Result
            if ($Item.Length -le 0 -or $Item.Length -gt 65536) {
                throw 'Windows mailbox result size is invalid.'
            }
            $Result = Get-Content -Raw -LiteralPath $Mailbox.Result |
                ConvertFrom-Json
            if ($Result.SchemaVersion -ne 1 -or $Result.RunId -cne $RunId) {
                throw 'Windows mailbox result identity/schema mismatch.'
            }
            $ExpectedProperties = @(
                'CompletedUtc', 'Evidence', 'Message', 'ResponseHmacSha256',
                'RunId', 'SchemaVersion', 'Status') | Sort-Object
            $ActualProperties = @($Result.PSObject.Properties.Name) | Sort-Object
            if (($ExpectedProperties -join "`n") -cne
                ($ActualProperties -join "`n") -or
                [string]$Result.Status -notin @('PASS', 'FAIL') -or
                [string]$Result.ResponseHmacSha256 -cnotmatch
                    '^[0-9A-F]{64}$') {
                throw 'Windows mailbox response does not match the exact schema.'
            }
            [DateTime]::Parse(
                [string]$Result.CompletedUtc,
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind) | Out-Null
            $AllowedEvidence = @(
                'stdout.txt', 'stderr.txt', 'tcglog.ib', 'tcglog-full.bin')
            $Seen = @{}
            foreach ($Evidence in @($Result.Evidence)) {
                $EvidenceProperties = @($Evidence.PSObject.Properties.Name) |
                    Sort-Object
                if (($EvidenceProperties -join "`n") -cne
                        ((@('Length', 'Name', 'Sha256') | Sort-Object) -join "`n") -or
                    [string]$Evidence.PSObject.Properties['Name'].Value -notin $AllowedEvidence -or
                    $Seen.ContainsKey([string]$Evidence.PSObject.Properties['Name'].Value) -or
                    [string]$Evidence.Sha256 -cnotmatch '^[0-9A-F]{64}$' -or
                    [long]$Evidence.Length -lt 0) {
                    throw 'Windows mailbox evidence manifest is invalid.'
                }
                $Seen[[string]$Evidence.PSObject.Properties['Name'].Value] = $true
            }
            $Key = Get-MailboxKeyBytes $Selected
            try {
                $ExpectedHmac = Get-HmacSha256Hex -Key $Key `
                    -Text (Get-CanonicalMailboxResponse $Result)
            }
            finally { [Array]::Clear($Key, 0, $Key.Length) }
            if (-not (Test-FixedHexEqual $ExpectedHmac `
                    ([string]$Result.ResponseHmacSha256))) {
                throw 'Windows mailbox response authentication failed.'
            }
            return $Result
        }
        if (-not $PickedUp -and (
                (Test-Path -LiteralPath $ProcessingRequest -PathType Leaf) -or
                (Test-Path -LiteralPath $CompletedRequest -PathType Leaf))) {
            $PickedUp = $true
        }
        if (-not $PickedUp -and [DateTime]::UtcNow -ge $PickupDeadline) {
            throw "Windows mailbox did not pick up request within 90 seconds: $RunId"
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Timed out waiting for Windows mailbox result: $RunId"
}

function Copy-WindowsMailboxEvidence {
    param(
        [Parameter(Mandatory=$true)]$Mailbox,
        [Parameter(Mandatory=$true)]$Paths,
        [Parameter(Mandatory=$true)]$Result
    )
    foreach ($Evidence in @($Result.Evidence)) {
        $Name = [string]$Evidence.PSObject.Properties['Name'].Value
        $Source = Join-Path $Mailbox.ResponseDirectory $Name
        if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
            throw "Windows mailbox evidence is missing: $Name"
        }
        $Temporary = Join-Path $Paths.Evidence (
            $Name + '.tmp-' + [Guid]::NewGuid().ToString('N'))
        try {
            Copy-Item -LiteralPath $Source -Destination $Temporary
            $Item = Get-Item -LiteralPath $Temporary
            $Hash = (Get-FileHash -LiteralPath $Temporary -Algorithm SHA256).Hash
            if ($Item.Length -ne [long]$Evidence.Length -or
                $Hash -cne [string]$Evidence.Sha256) {
                throw "Windows mailbox evidence authentication failed: $Name"
            }
            Move-Item -LiteralPath $Temporary -Destination (
                Join-Path $Paths.Evidence $Name)
        }
        finally {
            if (Test-Path -LiteralPath $Temporary -PathType Leaf) {
                Remove-Item -LiteralPath $Temporary -Force
            }
        }
    }
    Write-UhrAtomicJson -Path (Join-Path $Paths.Evidence 'result.json') `
        -Value $Result
}

# VMware Guest Variables transport.  The guest task polls the same guestVar
# namespace through VMware Tools.  Request data is committed chunk-first and
# the manifest is written last; responses use the inverse read-side contract.
$script:VmwareGuestInfoPrefix = 'uefiloader'
$script:VmwareGuestInfoRequestManifest = 'uefiloader.request.manifest'
$script:VmwareGuestInfoResponseManifest = 'uefiloader.response.manifest'
$script:VmwareGuestInfoWorkerStatus = 'uefiloader.worker.status'
$script:VmwareGuestInfoRequestChunkPrefix = 'uefiloader.request.chunk.'
$script:VmwareGuestInfoResponseChunkPrefix = 'uefiloader.response.chunk.'
$script:VmwareGuestInfoChunkBytes = 9000
$script:VmwareGuestInfoChunkChars = 12000
$script:VmwareGuestInfoMaxPackageBytes = 1MB
$script:VmwareGuestInfoMaxEvidenceBytes = 32MB
$script:VmwareGuestInfoMaxManifestChars = 65536
$script:VmwareGuestInfoTombstone = 'MA=='

function Get-VmwareGuestInfoBase {
    param([Parameter(Mandatory=$true)][string]$RunId)
    if (-not (Test-UhrRunId $RunId) -or
        $RunId -cnotmatch '^uefi-windows-(probe|capture|nested-prep)-') {
        throw "Unsafe Windows Guest Variables RunId: $RunId"
    }
    return $script:VmwareGuestInfoPrefix
}

function Get-VmwareGuestInfoRequestChunkName {
    param(
        [Parameter(Mandatory=$true)][string]$Base,
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)][int]$Index
    )
    if (-not (Test-UhrRunId $RunId) -or $Index -lt 0 -or $Index -gt 4095) {
        throw 'Unsafe VMware Guest Variables request chunk identity.'
    }
    return "$($script:VmwareGuestInfoRequestChunkPrefix)$RunId.$('{0:D6}' -f $Index)"
}

function Get-VmwareGuestInfoRequestManifestName {
    param([Parameter(Mandatory=$true)][string]$Base)
    if ($Base -cne $script:VmwareGuestInfoPrefix) {
        throw 'Unexpected VMware Guest Variables prefix.'
    }
    return $script:VmwareGuestInfoRequestManifest
}

function Get-VmwareGuestInfoResponseManifestName {
    param([Parameter(Mandatory=$true)][string]$Base)
    if ($Base -cne $script:VmwareGuestInfoPrefix) {
        throw 'Unexpected VMware Guest Variables prefix.'
    }
    return $script:VmwareGuestInfoResponseManifest
}

function Get-VmwareGuestInfoResponseChunkName {
    param(
        [Parameter(Mandatory=$true)][string]$Base,
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)][string]$EvidenceName,
        [Parameter(Mandatory=$true)][int]$Index
    )
    if ($EvidenceName -notin @(
            'evidence.zip', 'stdout.txt', 'stderr.txt', 'tcglog.ib', 'tcglog-full.bin')) {
        throw "Windows Guest Variables evidence is not allowlisted: $EvidenceName"
    }
    if (-not (Test-UhrRunId $RunId) -or $Index -lt 0 -or $Index -gt 4095) {
        throw 'Unsafe VMware Guest Variables response chunk identity.'
    }
    return "$($script:VmwareGuestInfoResponseChunkPrefix)$RunId.$('{0:D6}' -f $Index)"
}

function Invoke-VmwareGuestInfoWrite {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$VariableName,
        [Parameter(Mandatory=$true)][AllowEmptyString()][string]$Value,
        [int]$TimeoutSeconds = 30,
        [switch]$AllowFailure
    )
    if ($VariableName -cne $script:VmwareGuestInfoWorkerStatus -and
        $VariableName -notmatch '^uefiloader\.(request|response)\.manifest$' -and
        $VariableName -notmatch '^uefiloader\.(request|response)\.chunk\.uefi-windows-(probe|capture|nested-prep)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.[0-9]{6}$') {
        throw "Unsafe VMware Guest Variables name: $VariableName"
    }
    if ($Value.Length -gt $script:VmwareGuestInfoChunkChars -and
        $VariableName -match '\.chunk\.') {
        throw "VMware Guest Variables chunk exceeds $($script:VmwareGuestInfoChunkChars) characters."
    }
    return Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
        'writeVariable', [string]$Selected.VmxPath, 'guestVar',
        $VariableName, $Value) -TimeoutSeconds $TimeoutSeconds -AllowFailure:$AllowFailure
}

function Invoke-VmwareGuestInfoRead {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$VariableName,
        [int]$TimeoutSeconds = 30
    )
    if ($VariableName -cne $script:VmwareGuestInfoWorkerStatus -and
        $VariableName -notmatch '^uefiloader\.(request|response)\.manifest$' -and
        $VariableName -notmatch '^uefiloader\.(request|response)\.chunk\.uefi-windows-(probe|capture|nested-prep)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.[0-9]{6}$') {
        throw "Unsafe VMware Guest Variables name: $VariableName"
    }
    $Result = Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
        'readVariable', [string]$Selected.VmxPath, 'guestVar',
        $VariableName) -TimeoutSeconds $TimeoutSeconds -AllowFailure
    if ($Result.ExitCode -ne 0 -or $Result.TimedOut) { return $null }
    $Value = ([string]$Result.Stdout).Trim()
    if ($Value.Length -eq 0) { return $null }
    return $Value
}

function Clear-VmwareGuestInfoVariables {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string[]]$VariableNames
    )
    foreach ($Name in @($VariableNames | Sort-Object -Unique)) {
        if ([string]::IsNullOrWhiteSpace($Name)) { continue }
        try {
            [void](Invoke-VmwareGuestInfoWrite -Selected $Selected `
                -VariableName $Name -Value $script:VmwareGuestInfoTombstone `
                -AllowFailure)
        }
        catch {
            # Cleanup is best effort.  The exact variable set is bounded and
            # never falls back to a broad VM or variable-store operation.
        }
    }
}

function Get-CanonicalVmwareGuestInfoRequest {
    param($Request)
    return @(
        "SchemaVersion=$($Request.SchemaVersion)"
        "RunId=$($Request.RunId)"
        "Action=$($Request.Action)"
        "ExpectedProbeVerdict=$($Request.ExpectedProbeVerdict)"
        "PackageSha256=$($Request.PackageSha256)"
        "PackageSize=$($Request.PackageSize)"
        "PackageChunkSize=$($Request.PackageChunkSize)"
        "PackageChunkCount=$($Request.PackageChunkCount)"
        "TimeoutSeconds=$($Request.TimeoutSeconds)"
        "SubmittedUtc=$($Request.SubmittedUtc)"
    ) -join "`n"
}

function Get-CanonicalVmwareGuestInfoResponse {
    param($Result)
    $Lines = [Collections.Generic.List[string]]::new()
    $Lines.Add("SchemaVersion=$($Result.SchemaVersion)")
    $Lines.Add("RunId=$($Result.RunId)")
    $Lines.Add("Status=$($Result.Status)")
    $Lines.Add("MessageBase64=$($Result.MessageBase64)")
    $Lines.Add("CompletedUtc=$($Result.CompletedUtc)")
    $Lines.Add("EvidenceSha256=$($Result.EvidenceSha256)")
    $Lines.Add("EvidenceSize=$($Result.EvidenceSize)")
    $Lines.Add("EvidenceChunkSize=$($Result.EvidenceChunkSize)")
    $Lines.Add("EvidenceChunkCount=$($Result.EvidenceChunkCount)")
    return $Lines -join "`n"
}

function Get-CanonicalVmwareGuestInfoWorkerStatus {
    param($Status)
    return @(
        "SchemaVersion=$($Status.SchemaVersion)"
        "State=$($Status.State)"
        "WorkerSid=$($Status.WorkerSid)"
        "StartedUtc=$($Status.StartedUtc)"
        "UpdatedUtc=$($Status.UpdatedUtc)"
    ) -join "`n"
}

function Wait-VmwareGuestInfoWorkerReady {
    param([Parameter(Mandatory=$true)]$Selected)

    Invoke-VmwareGuestInfoWrite -Selected $Selected `
        -VariableName $script:VmwareGuestInfoWorkerStatus `
        -Value $script:VmwareGuestInfoTombstone | Out-Null
    $ReadyTimeoutSeconds = [int]$Config.Policy.GuestReadyTimeoutSeconds
    $Deadline = [DateTime]::UtcNow.AddSeconds($ReadyTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $Deadline) {
        $Encoded = Invoke-VmwareGuestInfoRead -Selected $Selected `
            -VariableName $script:VmwareGuestInfoWorkerStatus
        if ($null -eq $Encoded -or
            $Encoded -ceq $script:VmwareGuestInfoTombstone) {
            Start-Sleep -Milliseconds 500
            continue
        }
        try {
            $Bytes = [Convert]::FromBase64String($Encoded)
            if ([Convert]::ToBase64String($Bytes) -cne $Encoded) {
                throw 'non-canonical base64'
            }
            $Status = [Text.Encoding]::UTF8.GetString($Bytes) |
                ConvertFrom-Json
        }
        catch {
            throw 'Windows Guest Variables worker status is invalid base64 JSON.'
        }
        finally {
            if ($null -ne $Bytes) {
                [Array]::Clear($Bytes, 0, $Bytes.Length)
            }
        }
        $ExpectedProperties = @(
            'SchemaVersion', 'StartedUtc', 'State', 'UpdatedUtc',
            'WorkerHmacSha256', 'WorkerSid') | Sort-Object
        $ActualProperties = @($Status.PSObject.Properties.Name) | Sort-Object
        if (($ExpectedProperties -join "`n") -cne
                ($ActualProperties -join "`n") -or
            [int]$Status.SchemaVersion -ne 1 -or
            [string]$Status.State -cne 'READY' -or
            [string]$Status.WorkerSid -cne 'S-1-5-18' -or
            [string]$Status.WorkerHmacSha256 -cnotmatch '^[0-9A-F]{64}$') {
            throw 'Windows Guest Variables worker status schema is invalid.'
        }
        try {
            $Started = [DateTime]::Parse(
                [string]$Status.StartedUtc,
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
            $Updated = [DateTime]::Parse(
                [string]$Status.UpdatedUtc,
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
        }
        catch { throw 'Windows Guest Variables worker status time is invalid.' }
        if ($Started -gt $Updated -or
            [Math]::Abs(([DateTime]::UtcNow - $Updated).TotalSeconds) -gt 15) {
            throw 'Windows Guest Variables worker status is stale or from the future.'
        }
        $Key = Get-MailboxKeyBytes $Selected
        try {
            $ExpectedHmac = Get-HmacSha256Hex -Key $Key `
                -Text (Get-CanonicalVmwareGuestInfoWorkerStatus $Status)
        }
        finally { [Array]::Clear($Key, 0, $Key.Length) }
        if (-not (Test-FixedHexEqual $ExpectedHmac `
                ([string]$Status.WorkerHmacSha256))) {
            throw 'Windows Guest Variables worker status authentication failed.'
        }
        return $Status
    }
    throw "Windows Guest Variables SYSTEM worker did not publish a fresh authenticated startup heartbeat within $ReadyTimeoutSeconds seconds."
}

function ConvertTo-VmwareGuestInfoChunks {
    param([Parameter(Mandatory=$true)][byte[]]$Bytes)
    $Chunks = [Collections.Generic.List[string]]::new()
    for ($Offset = 0; $Offset -lt $Bytes.Length; $Offset +=
            $script:VmwareGuestInfoChunkBytes) {
        $Length = [Math]::Min(
            $script:VmwareGuestInfoChunkBytes, $Bytes.Length - $Offset)
        $Chunk = New-Object byte[] $Length
        try {
            [Array]::Copy($Bytes, $Offset, $Chunk, 0, $Length)
            $Encoded = [Convert]::ToBase64String($Chunk)
            if ($Encoded.Length -gt $script:VmwareGuestInfoChunkChars) {
                throw 'Windows Guest Variables chunk exceeded the encoded bound.'
            }
            $Chunks.Add($Encoded)
        }
        finally { [Array]::Clear($Chunk, 0, $Chunk.Length) }
    }
    return $Chunks.ToArray()
}

function Publish-WindowsGuestInfoRequest {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)]$Paths,
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)][ValidateSet('Probe', 'Capture')]
        [string]$GuestAction,
        [switch]$NestedPrep
    )
    $Base = Get-VmwareGuestInfoBase $RunId
    $PackagePath = Join-Path $Paths.RunRoot 'guest-info-package.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory(
        [string]$Paths.Stage, $PackagePath,
        [IO.Compression.CompressionLevel]::Optimal, $false)
    $PackageBytes = [IO.File]::ReadAllBytes($PackagePath)
    $VariableNames = [Collections.Generic.List[string]]::new()
    try {
        if ($PackageBytes.Length -le 0 -or
            $PackageBytes.Length -gt $script:VmwareGuestInfoMaxPackageBytes) {
            throw 'Windows Guest Variables package must be greater than zero and at most 1 MiB.'
        }
        $Chunks = ConvertTo-VmwareGuestInfoChunks -Bytes $PackageBytes
        if ($Chunks.Count -lt 1 -or $Chunks.Count -gt 256) {
            throw 'Windows Guest Variables package chunk count is outside the bounded range.'
        }
        $ResponseManifestName = Get-VmwareGuestInfoResponseManifestName $Base
        Invoke-VmwareGuestInfoWrite -Selected $Selected `
            -VariableName $ResponseManifestName `
            -Value $script:VmwareGuestInfoTombstone | Out-Null
        $VariableNames.Add($ResponseManifestName)
        $Request = [ordered]@{
            SchemaVersion = 1
            RunId = $RunId
            Action = $GuestAction
            ExpectedProbeVerdict = if ($NestedPrep) {
                'MET'
            } else { [string]$Selected.ExpectedProbeVerdict }
            PackageSha256 = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash
            PackageSize = [long]$PackageBytes.Length
            PackageChunkSize = [int]$script:VmwareGuestInfoChunkBytes
            PackageChunkCount = [int]$Chunks.Count
            TimeoutSeconds = [Math]::Min(
                [int]$Config.Policy.OperationTimeoutSeconds, 600)
            SubmittedUtc = [DateTime]::UtcNow.ToString('o')
        }
        $Key = Get-MailboxKeyBytes $Selected
        try {
            $Request.RequestHmacSha256 = Get-HmacSha256Hex -Key $Key `
                -Text (Get-CanonicalVmwareGuestInfoRequest $Request)
        }
        finally { [Array]::Clear($Key, 0, $Key.Length) }
        $WriteDeadline = [DateTime]::UtcNow.AddSeconds(
            [Math]::Min([int]$Config.Policy.OperationTimeoutSeconds, 600) + 60)
        for ($Index = 0; $Index -lt $Chunks.Count; $Index++) {
            if ([DateTime]::UtcNow -ge $WriteDeadline) {
                throw 'Timed out publishing Windows Guest Variables request chunks.'
            }
            $Name = Get-VmwareGuestInfoRequestChunkName -Base $Base `
                -RunId $RunId -Index $Index
            $VariableNames.Add($Name)
            Invoke-VmwareGuestInfoWrite -Selected $Selected -VariableName $Name `
                -Value $Chunks[$Index] | Out-Null
        }
        $ManifestName = Get-VmwareGuestInfoRequestManifestName $Base
        $ManifestJson = $Request | ConvertTo-Json -Compress -Depth 5
        $ManifestBytes = [Text.Encoding]::UTF8.GetBytes($ManifestJson)
        try { $Manifest = [Convert]::ToBase64String($ManifestBytes) }
        finally { [Array]::Clear($ManifestBytes, 0, $ManifestBytes.Length) }
        if ($Manifest.Length -gt $script:VmwareGuestInfoMaxManifestChars) {
            throw 'Windows Guest Variables request manifest is too large.'
        }
        $ManifestName = Get-VmwareGuestInfoRequestManifestName $Base
        Invoke-VmwareGuestInfoWrite -Selected $Selected `
            -VariableName $ManifestName -Value $Manifest | Out-Null
        $VariableNames.Add($ManifestName)
        return [pscustomobject]@{
            Base = $Base
            RequestVariableNames = @($VariableNames)
            ResponseManifestName = $ResponseManifestName
            RunId = $RunId
        }
    }
    catch {
        Clear-VmwareGuestInfoVariables -Selected $Selected `
            -VariableNames @($VariableNames)
        throw
    }
    finally {
        if ($null -ne $PackageBytes) {
            [Array]::Clear($PackageBytes, 0, $PackageBytes.Length)
        }
    }
}

function Wait-WindowsGuestInfoResult {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)]$Transport,
        [Parameter(Mandatory=$true)]$Paths,
        [Parameter(Mandatory=$true)][string]$RunId
    )
    return Wait-WindowsGuestInfoEvidence @PSBoundParameters
}
function Wait-WindowsGuestInfoEvidence {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)]$Transport,
        [Parameter(Mandatory=$true)]$Paths,
        [Parameter(Mandatory=$true)][string]$RunId
    )
    $Deadline = [DateTime]::UtcNow.AddSeconds(
        [Math]::Min([int]$Config.Policy.OperationTimeoutSeconds, 600) + 60)
    $PickupSeconds = if ($RunId -match '-nested-prep-') { 300 } else { 60 }
    $PickupDeadline = [DateTime]::UtcNow.AddSeconds($PickupSeconds)
    $ResponseManifest = Get-VmwareGuestInfoResponseManifestName $Transport.Base
    $ResponseNames = [Collections.Generic.List[string]]::new()
    $ResponseNames.Add($ResponseManifest)
    while ([DateTime]::UtcNow -lt $Deadline) {
        $ManifestText = Invoke-VmwareGuestInfoRead -Selected $Selected `
            -VariableName $ResponseManifest
        if ($null -eq $ManifestText -or
            $ManifestText -ceq $script:VmwareGuestInfoTombstone) {
            if ([DateTime]::UtcNow -ge $PickupDeadline) {
                throw "Windows Guest Variables response was not ready within $PickupSeconds seconds: $RunId"
            }
            Start-Sleep -Milliseconds 500
            continue
        }
        if ($ManifestText.Length -gt $script:VmwareGuestInfoMaxManifestChars) {
            throw 'Windows Guest Variables response manifest is too large.'
        }
        try {
            $ManifestBytes = [Convert]::FromBase64String($ManifestText)
            $ManifestJson = [Text.Encoding]::UTF8.GetString($ManifestBytes)
            $Raw = $ManifestJson | ConvertFrom-Json
        }
        catch { throw 'Windows Guest Variables response manifest is invalid base64 JSON.' }
        finally {
            if ($null -ne $ManifestBytes) {
                [Array]::Clear($ManifestBytes, 0, $ManifestBytes.Length)
            }
        }
        $ExpectedProperties = @(
            'CompletedUtc', 'EvidenceChunkCount', 'EvidenceChunkSize',
            'EvidenceSha256', 'EvidenceSize', 'MessageBase64',
            'ResponseHmacSha256', 'RunId', 'SchemaVersion', 'Status') |
            Sort-Object
        $ActualProperties = @($Raw.PSObject.Properties.Name) | Sort-Object
        if (($ExpectedProperties -join "`n") -cne ($ActualProperties -join "`n")) {
            throw 'Windows Guest Variables response does not match the exact schema.'
        }
        if ([int]$Raw.SchemaVersion -ne 1 -or
            [string]$Raw.RunId -cne $RunId -or
            [string]$Raw.Status -notin @('PASS', 'FAIL') -or
            [string]$Raw.ResponseHmacSha256 -cnotmatch '^[0-9A-F]{64}$' -or
            [string]$Raw.EvidenceSha256 -cnotmatch '^[0-9A-F]{64}$') {
            throw 'Windows Guest Variables response identity or schema is invalid.'
        }
        [DateTime]::Parse([string]$Raw.CompletedUtc,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind) | Out-Null
        try {
            $MessageBytes = [Convert]::FromBase64String([string]$Raw.MessageBase64)
            $Message = [Text.Encoding]::UTF8.GetString($MessageBytes)
        }
        catch { throw 'Windows Guest Variables response message is not base64.' }
        finally {
            if ($null -ne $MessageBytes) {
                [Array]::Clear($MessageBytes, 0, $MessageBytes.Length)
            }
        }
        if ([Text.Encoding]::UTF8.GetByteCount($Message) -gt 4096) {
            throw 'Windows Guest Variables response message is too large.'
        }
        $EvidenceSize = [int64]$Raw.EvidenceSize
        $EvidenceChunkSize = [int]$Raw.EvidenceChunkSize
        $EvidenceChunkCount = [int]$Raw.EvidenceChunkCount
        if ($EvidenceSize -lt 0 -or
            $EvidenceSize -gt $script:VmwareGuestInfoMaxEvidenceBytes -or
            $EvidenceChunkCount -lt 0 -or $EvidenceChunkCount -gt 4096 -or
            ($EvidenceSize -eq 0 -and $EvidenceChunkCount -ne 0) -or
            ($EvidenceSize -gt 0 -and
                ($EvidenceChunkSize -lt 1 -or $EvidenceChunkSize -gt $script:VmwareGuestInfoChunkBytes))) {
            throw 'Windows Guest Variables evidence bounds are invalid.'
        }
        $ExpectedCount = if ($EvidenceSize -eq 0) { 0 } else {
            [int][Math]::Ceiling($EvidenceSize / [double]$EvidenceChunkSize)
        }
        if ($ExpectedCount -ne $EvidenceChunkCount) {
            throw 'Windows Guest Variables evidence chunk count is inconsistent.'
        }
        $Key = Get-MailboxKeyBytes $Selected
        try {
            $ExpectedHmac = Get-HmacSha256Hex -Key $Key `
                -Text (Get-CanonicalVmwareGuestInfoResponse $Raw)
        }
        finally { [Array]::Clear($Key, 0, $Key.Length) }
        if (-not (Test-FixedHexEqual $ExpectedHmac `
                ([string]$Raw.ResponseHmacSha256))) {
            throw 'Windows Guest Variables response authentication failed.'
        }
        for ($Index = 0; $Index -lt $EvidenceChunkCount; $Index++) {
            $ResponseNames.Add((Get-VmwareGuestInfoResponseChunkName `
                -Base $Transport.Base -RunId $RunId -EvidenceName 'evidence.zip' `
                -Index $Index))
        }
        $EvidenceBytes = [Collections.Generic.List[byte]]::new()
        try {
            for ($Index = 0; $Index -lt $EvidenceChunkCount; $Index++) {
                if ([DateTime]::UtcNow -ge $Deadline) {
                    throw 'Timed out reading Windows Guest Variables evidence.'
                }
                $Name = Get-VmwareGuestInfoResponseChunkName -Base $Transport.Base `
                    -RunId $RunId -EvidenceName 'evidence.zip' -Index $Index
                $Chunk = Invoke-VmwareGuestInfoRead -Selected $Selected -VariableName $Name
                if ($null -eq $Chunk -or $Chunk.Length -gt $script:VmwareGuestInfoChunkChars) {
                    throw "Windows Guest Variables evidence chunk is missing or too large: $Index"
                }
                try { $Decoded = [Convert]::FromBase64String($Chunk) }
                catch { throw "Windows Guest Variables evidence chunk is invalid base64: $Index" }
                $ExpectedLength = [Math]::Min(
                    $EvidenceChunkSize,
                    [int]($EvidenceSize - ($Index * $EvidenceChunkSize)))
                if ($Decoded.Length -ne $ExpectedLength -or
                    [Convert]::ToBase64String($Decoded) -cne $Chunk) {
                    [Array]::Clear($Decoded, 0, $Decoded.Length)
                    throw "Windows Guest Variables evidence chunk length is invalid: $Index"
                }
                foreach ($Byte in $Decoded) { $EvidenceBytes.Add($Byte) }
                [Array]::Clear($Decoded, 0, $Decoded.Length)
            }
            if ($EvidenceBytes.Count -ne $EvidenceSize) {
                throw 'Windows Guest Variables evidence size does not match the manifest.'
            }
            if ($EvidenceSize -eq 0) {
                $EmptySha256 = [Security.Cryptography.SHA256]::Create()
                try {
                    $EmptyEvidenceHash = $EmptySha256.ComputeHash([byte[]]@())
                }
                finally { $EmptySha256.Dispose() }
                try {
                    $EmptyEvidenceHashText = ([BitConverter]::ToString(
                        $EmptyEvidenceHash)).Replace('-', '')
                }
                finally { [Array]::Clear($EmptyEvidenceHash, 0, $EmptyEvidenceHash.Length) }
                if ([string]$Raw.EvidenceSha256 -cne ('0' * 64) -and
                    [string]$Raw.EvidenceSha256 -cne $EmptyEvidenceHashText) {
                    throw 'Windows Guest Variables empty evidence hash is invalid.'
                }
            }
            $EvidenceArray = $EvidenceBytes.ToArray()
            try {
                if ($EvidenceSize -gt 0) {
                    $Sha256 = [Security.Cryptography.SHA256]::Create()
                    try {
                        $ActualHash = ([BitConverter]::ToString(
                            $Sha256.ComputeHash($EvidenceArray))).Replace('-', '')
                    }
                    finally { $Sha256.Dispose() }
                    if ($ActualHash -cne [string]$Raw.EvidenceSha256) {
                        throw 'Windows Guest Variables evidence SHA256 mismatch.'
                    }
                }
                if ($EvidenceSize -gt 0) {
                    $ArchivePath = Join-Path $Paths.RunRoot 'guest-info-evidence.zip'
                    [IO.File]::WriteAllBytes($ArchivePath, $EvidenceArray)
                    $Allowed = @('stdout.txt', 'stderr.txt', 'tcglog.ib',
                        'tcglog-full.bin', 'bitlocker-status.txt',
                        'bootmgfw.sha256')
                    $Archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
                    try {
                        $Names = @($Archive.Entries | ForEach-Object { $_.FullName })
                        if (@($Names | Where-Object { $_ -notin $Allowed }).Count -ne 0 -or
                            @($Names | Sort-Object -Unique).Count -ne $Names.Count) {
                            throw 'Windows Guest Variables evidence archive contains unsafe entries.'
                        }
                        foreach ($Entry in $Archive.Entries) {
                            if ($Entry.FullName.Contains('/') -or
                                $Entry.FullName.Contains('\') -or
                                [IO.Path]::IsPathRooted($Entry.FullName) -or
                                $Entry.FullName.Contains(':')) {
                                throw "Unsafe evidence ZIP entry: $($Entry.FullName)"
                            }
                            $Target = Join-Path $Paths.Evidence $Entry.FullName
                            if (-not (Test-UhrPathWithin -Root $Paths.Evidence -Path $Target)) {
                                throw "Evidence ZIP entry escaped the run evidence root: $($Entry.FullName)"
                            }
                            $Input = $Entry.Open()
                            try {
                                $Output = [IO.File]::Open($Target, [IO.FileMode]::CreateNew,
                                    [IO.FileAccess]::Write, [IO.FileShare]::None)
                                try { $Input.CopyTo($Output) }
                                finally { $Output.Dispose() }
                            }
                            finally { $Input.Dispose() }
                        }
                    }
                    finally { $Archive.Dispose() }
                }
            }
            finally { [Array]::Clear($EvidenceArray, 0, $EvidenceArray.Length) }
        }
        finally { $EvidenceBytes.Clear() }
        $Result = [ordered]@{
            SchemaVersion = 1
            RunId = $RunId
            Status = [string]$Raw.Status
            Message = $Message
            CompletedUtc = [string]$Raw.CompletedUtc
            Evidence = @()
        }
        Write-UhrAtomicJson -Path (Join-Path $Paths.Evidence 'result.json') -Value $Result
        return [pscustomobject]@{
            Result = [pscustomobject]$Result
            ResponseVariableNames = @($ResponseNames)
        }
    }
}

function Get-PlainCredential {
    param(
        [Parameter(Mandatory=$true)][string]$FileName,
        [Parameter(Mandatory=$true)][AllowEmptyString()][string]$ExpectedUser
    )
    $Credential = Get-UhrCredential -Config $Config -FileName $FileName
    if (-not [string]::IsNullOrWhiteSpace($ExpectedUser) -and
        $Credential.UserName -cne $ExpectedUser) {
        throw "Stored credential user '$($Credential.UserName)' does not match '$ExpectedUser'."
    }
    return [pscustomobject]@{
        UserName = $Credential.UserName
        Password = $Credential.GetNetworkCredential().Password
    }
}

function Invoke-ConfiguredVmrun {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string[]]$Operation,
        [switch]$GuestAuthentication,
        [int]$TimeoutSeconds = ([int]$Config.Policy.OperationTimeoutSeconds),
        [switch]$AllowFailure
    )
    $Arguments = @('-T', 'ws')
    $Secrets = @()
    $VmCredential = $null
    $GuestCredential = $null
    if (-not [string]::IsNullOrWhiteSpace([string]$Selected.VmCredentialFile)) {
        $VmCredential = Get-PlainCredential `
            -FileName ([string]$Selected.VmCredentialFile) -ExpectedUser ''
        $Arguments += @('-vp', $VmCredential.Password)
        $Secrets += $VmCredential.Password
    }
    if ($GuestAuthentication) {
        $GuestCredential = Get-PlainCredential `
            -FileName ([string]$Selected.GuestCredentialFile) `
            -ExpectedUser ([string]$Selected.GuestUser)
        $Arguments += @('-gu', $GuestCredential.UserName, '-gp',
            $GuestCredential.Password)
        $Secrets += $GuestCredential.Password
    }
    $Arguments += $Operation
    try {
        $Result = Invoke-UhrProcess -FilePath $Config.Tools.VmrunPath `
            -ArgumentList $Arguments -WorkingDirectory $Config.ProjectRoot `
            -TimeoutSeconds $TimeoutSeconds -Redactions $Secrets
    }
    finally {
        $Arguments = $null
        $Secrets = $null
        $VmCredential = $null
        $GuestCredential = $null
    }
    if ($Result.TimedOut) {
        throw "vmrun operation timed out: $($Operation[0])"
    }
    if (-not $AllowFailure -and $Result.ExitCode -ne 0) {
        $Detail = ($Result.Stdout + "`n" + $Result.Stderr).Trim()
        throw "vmrun $($Operation[0]) failed with exit code $($Result.ExitCode): $Detail"
    }
    return $Result
}

function Get-RunningVmPaths {
    $Result = Invoke-UhrProcess -FilePath $Config.Tools.VmrunPath `
        -ArgumentList @('-T', 'ws', 'list') `
        -WorkingDirectory $Config.ProjectRoot -TimeoutSeconds 30
    if ($Result.ExitCode -ne 0 -or $Result.TimedOut) {
        throw "vmrun list failed: $($Result.Stderr.Trim())"
    }
    $Lines = @($Result.Stdout -split "`r?`n")
    return @($Lines | Select-Object -Skip 1 | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    } | ForEach-Object { [IO.Path]::GetFullPath($_.Trim()) })
}

function Test-ConfiguredVmRunning {
    param([Parameter(Mandatory=$true)]$Selected)
    $Expected = [IO.Path]::GetFullPath([string]$Selected.VmxPath)
    $Matches = @(Get-RunningVmPaths | Where-Object {
        $_.Equals($Expected, [StringComparison]::OrdinalIgnoreCase)
    })
    return $Matches.Count -ne 0
}

function Get-ConfiguredVmDescriptors {
    $Descriptors = [Collections.Generic.List[object]]::new()
    foreach ($Name in @('Linux', 'Windows')) {
        $ProfileConfig = $Config.Profiles.$Name
        if (-not $ProfileConfig.Enabled) { continue }
        $Descriptors.Add([pscustomobject]@{
            Name = $Name
            VmxPath = [IO.Path]::GetFullPath([string]$ProfileConfig.VmxPath)
            LeaseResource = [string]$ProfileConfig.LeaseResource
        })
    }
    if ($null -ne $Config.PSObject.Properties['AdditionalKnownVms']) {
        foreach ($Additional in @($Config.AdditionalKnownVms)) {
            if ($null -eq $Additional -or
                [string]::IsNullOrWhiteSpace([string]$Additional.Name) -or
                [string]::IsNullOrWhiteSpace([string]$Additional.VmxPath) -or
                -not [IO.Path]::IsPathRooted([string]$Additional.VmxPath) -or
                -not (Test-Path -LiteralPath ([string]$Additional.VmxPath) `
                    -PathType Leaf) -or
                [string]$Additional.LeaseResource -cnotmatch
                    '^vm-[a-z0-9][a-z0-9.-]{1,61}$') {
                throw 'AdditionalKnownVms contains an invalid descriptor.'
            }
            $Path = [IO.Path]::GetFullPath([string]$Additional.VmxPath)
            if (@($Descriptors | Where-Object {
                        $_.VmxPath.Equals(
                            $Path, [StringComparison]::OrdinalIgnoreCase) -or
                        $_.LeaseResource -ceq [string]$Additional.LeaseResource
                    }).Count -ne 0) {
                throw 'AdditionalKnownVms duplicates a configured path or lease resource.'
            }
            $Descriptors.Add([pscustomobject]@{
                Name = [string]$Additional.Name
                VmxPath = $Path
                LeaseResource = [string]$Additional.LeaseResource
            })
        }
    }
    return @($Descriptors)
}

function Assert-ConcurrentVmSetSafe {
    param([Parameter(Mandatory=$true)]$Selected)
    $Expected = [IO.Path]::GetFullPath([string]$Selected.VmxPath)
    $Running = @(Get-RunningVmPaths)
    if (@($Running | Where-Object {
                $_.Equals($Expected, [StringComparison]::OrdinalIgnoreCase)
            }).Count -ne 0) {
        throw 'The configured VM is already running; refuse to attach to or stop a VM not started by this run.'
    }
    if ($Running.Count -ge [int]$Config.Policy.MaxConcurrentVms) {
        throw "The host already has $($Running.Count) running VMs; the configured limit is $($Config.Policy.MaxConcurrentVms)."
    }
    $Known = @(Get-ConfiguredVmDescriptors)
    foreach ($RunningPath in $Running) {
        $Matches = @($Known | Where-Object {
            $_.VmxPath.Equals(
                $RunningPath, [StringComparison]::OrdinalIgnoreCase)
        })
        if ($Matches.Count -ne 1) {
            throw "An unrecognized VM is running; fail closed before concurrent testing: $RunningPath"
        }
        $Status = & $Config.SharedLease.ScriptPath -Action Status `
            -Resource $Matches[0].LeaseResource
        if ($Status.Status -cne 'ACTIVE') {
            throw "Running VM '$RunningPath' does not have an active '$($Matches[0].LeaseResource)' lease."
        }
    }
}

function Get-ConfiguredVmMemoryMib {
    param([Parameter(Mandatory=$true)]$Selected)
    $Vmx = Get-Content -Raw -LiteralPath ([string]$Selected.VmxPath)
    $MemoryMatches = [regex]::Matches(
        $Vmx, '(?im)^memsize\s*=\s*"([0-9]+)"\s*$')
    if ($MemoryMatches.Count -ne 1) {
        throw 'The configured VMX must contain exactly one numeric memsize entry.'
    }
    $MemoryMib = [int]::Parse(
        $MemoryMatches[0].Groups[1].Value,
        [Globalization.CultureInfo]::InvariantCulture)
    if ($MemoryMib -lt 512 -or $MemoryMib -gt 32768) {
        throw "The configured VM memory is outside the safe range: $MemoryMib MiB."
    }
    return $MemoryMib
}

function Assert-HostCapacity {
    param([Parameter(Mandatory=$true)]$Selected)
    try {
        Add-Type -AssemblyName Microsoft.VisualBasic
        $AvailableBytes = [uint64](
            [Microsoft.VisualBasic.Devices.ComputerInfo]::new().AvailablePhysicalMemory)
    }
    catch {
        throw "Unable to determine available host memory; fail closed: $($_.Exception.Message)"
    }
    if ($AvailableBytes -eq 0) {
        throw 'Unable to determine available host memory; fail closed.'
    }
    $FreeMib = [uint64][Math]::Floor(
        [double]$AvailableBytes / 1MB)
    $VmMib = Get-ConfiguredVmMemoryMib $Selected
    $RequiredMib = [uint64]$VmMib +
        [uint64]$Config.Policy.MinimumHostReserveMib
    if ($FreeMib -lt $RequiredMib) {
        throw "Insufficient host memory: $FreeMib MiB free; need $RequiredMib MiB for the VM and reserve."
    }
}

function Invoke-HostBuild {
    $LogRoot = Join-Path $Config.StateRoot 'build'
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $Command = 'call {0} -arch=x64 -host_arch=x64' -f `
        [string]$Config.Tools.VsDevCmdPath
    $Command += ' && cmake --preset windows'
    $Command += ' && cmake --build --preset windows'
    $Command += ' && ctest --test-dir build\windows --output-on-failure'
    $Command += ' && cmake --preset windows-static'
    $Command += ' && cmake --build --preset windows-static'
    $Result = Invoke-UhrProcess -FilePath $env:ComSpec `
        -ArgumentList @('/d', '/c', $Command) `
        -WorkingDirectory $Config.ProjectRoot `
        -TimeoutSeconds ([int]$Config.Policy.BuildTimeoutSeconds)
    $Stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    [IO.File]::WriteAllText(
        (Join-Path $LogRoot "$Stamp-stdout.log"), $Result.Stdout,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText(
        (Join-Path $LogRoot "$Stamp-stderr.log"), $Result.Stderr,
        [Text.UTF8Encoding]::new($false))
    if ($Result.TimedOut) { throw 'The host build timed out.' }
    if ($Result.ExitCode -ne 0) {
        throw "The host build failed with exit code $($Result.ExitCode)."
    }
    # The replay-era smoke images (replay/pe_rehash/divergence/handoff),
    # handoff_fixture_gen.exe and ibaudit.exe went away with the loader
    rewrite: their CMake targets were built from src/loader sources that no
    longer exist, so a clean build cannot produce them.  Requiring them here
    would fail every clean run and only a stale binary left in the build tree
    could satisfy them, so they are not required at all.
    foreach ($Artifact in @(
            'build\windows\loader.efi',
            'build\windows\tcglog_efi_smoke.efi',
            'build\windows\handoff_payload_stub.efi',
            'build\windows\handoff_bootmgr_stub.efi',
            'build\windows-static\ib-tcg-dump.exe',
            'build\windows-static\install_authenticode_test.exe',
            'build\windows-static\ib-install.exe')) {
        $Path = Join-Path $Config.ProjectRoot $Artifact
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "Expected build artifact is missing: $Path"
        }
    }
    return [pscustomobject]@{
        Result = 'PASS'
        LoaderSha256 = (Get-FileHash -LiteralPath `
            (Join-Path $Config.ProjectRoot 'build\windows\loader.efi') `
            -Algorithm SHA256).Hash
        TcglogEfiSmokeSha256 = (Get-FileHash -LiteralPath `
            (Join-Path $Config.ProjectRoot `
                'build\windows\tcglog_efi_smoke.efi') `
            -Algorithm SHA256).Hash
        TcgDumpSha256 = (Get-FileHash -LiteralPath `
            (Join-Path $Config.ProjectRoot `
                'build\windows-static\ib-tcg-dump.exe') `
            -Algorithm SHA256).Hash
        InstallerSha256 = (Get-FileHash -LiteralPath `
            (Join-Path $Config.ProjectRoot `
                'build\windows-static\ib-install.exe') `
            -Algorithm SHA256).Hash
    }
}

function New-HostStage {
    param(
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)]
        [ValidateSet(
            'LinuxPositive', 'LinuxReplayDryRun', 'LinuxPeRehash',
            'LinuxDivergenceManifest', 'LinuxHandoffMechanics',
            'LinuxProductionHandoff', 'LinuxOneShotCycle',
            'LinuxNestedDiskProbe', 'LinuxNestedWindowsE2E',
            'LinuxBootNextE2E',
            'WindowsProbe', 'WindowsCapture', 'WindowsNestedPrep')]
        [string]$Workflow
    )
    if (-not (Test-UhrRunId $RunId)) { throw "Unsafe RunId: $RunId" }
    $RunRoot = Join-Path (Join-Path $Config.StateRoot 'runs') $RunId
    $Stage = Join-Path $RunRoot 'stage'
    $Evidence = Join-Path $RunRoot 'evidence'
    if (Test-Path -LiteralPath $RunRoot) {
        throw "RunId already exists: $RunId"
    }
    New-Item -ItemType Directory -Force -Path $Stage,$Evidence | Out-Null
    # Retired with the loader rewrite.  These workflows stage loader binaries
    whose CMake targets were built from src/loader sources that no longer exist:
    the replay/pe_rehash/divergence/handoff smoke images, and ibaudit.exe, which
    the nested-Windows branch hashes into nested-SHA256SUMS.txt and hands to the
    guest.  Refusing here reports the reason instead of failing on a missing file
    deep inside staging.
    if ($Workflow -in @(
            'LinuxReplayDryRun', 'LinuxPeRehash',
            'LinuxDivergenceManifest', 'LinuxHandoffMechanics',
            'LinuxNestedWindowsE2E', 'LinuxBootNextE2E')) {
        throw "Workflow '$Workflow' is retired: its loader artifacts no longer have build targets."
    }
    if ($Workflow -in @(
            'LinuxPositive', 'LinuxReplayDryRun', 'LinuxPeRehash',
            'LinuxDivergenceManifest', 'LinuxHandoffMechanics',
            'LinuxProductionHandoff', 'LinuxOneShotCycle',
            'LinuxNestedDiskProbe', 'LinuxNestedWindowsE2E',
            'LinuxBootNextE2E')) {
        $EfiSource = switch ($Workflow) {
            'LinuxPositive' { 'build\windows\loader.efi' }
            'LinuxReplayDryRun' { 'build\windows\replay_efi_smoke.efi' }
            'LinuxPeRehash' { 'build\windows\pe_rehash_efi_smoke.efi' }
            'LinuxDivergenceManifest' {
                'build\windows\divergence_efi_smoke.efi'
            }
            'LinuxHandoffMechanics' {
                'build\windows\handoff_efi_smoke.efi'
            }
            'LinuxProductionHandoff' { 'build\windows\loader.efi' }
            'LinuxOneShotCycle' { 'build\windows\loader.efi' }
            'LinuxNestedDiskProbe' { 'build\windows\loader.efi' }
            'LinuxNestedWindowsE2E' { 'build\windows\loader.efi' }
            'LinuxBootNextE2E' { 'build\windows\loader.efi' }
        }
        $Files = @(
            @{ Source = $EfiSource; Name = 'loader.efi' },
            @{ Source = 'tools\host-runner\guest\Invoke-LinuxPositivePath.sh'; Name = 'Invoke-LinuxPositivePath.sh' }
        )
        if ($Workflow -eq 'LinuxNestedDiskProbe') {
            $Files = @(
                @{ Source = 'tools\host-runner\guest\Invoke-LinuxNestedDiskProbe.sh'; Name = 'Invoke-LinuxNestedDiskProbe.sh' }
            )
        }
        elseif ($Workflow -in @('LinuxNestedWindowsE2E',
                'LinuxBootNextE2E')) {
            $PreparedDisk = Get-LatestNestedPreparedDisk
            $PreparedRunId = $PreparedDisk.BaseName
            $PreparedEvidence = Join-Path (Join-Path (Join-Path `
                $Config.StateRoot 'runs') $PreparedRunId) 'evidence'
            $PreparedStdout = Join-Path $PreparedEvidence 'stdout.txt'
            $PreparedLog = Join-Path $PreparedEvidence 'tcglog.ib'
            if (-not (Test-Path -LiteralPath $PreparedStdout -PathType Leaf) -or
                -not (Test-Path -LiteralPath $PreparedLog -PathType Leaf)) {
                throw 'Prepared disk provenance evidence is incomplete.'
            }
            $PreparedText = Get-Content -Raw -LiteralPath $PreparedStdout
            $HashMatches = [regex]::Matches($PreparedText,
                '(?m)^NESTED_PREP_BOOTMGFW_SHA256=([0-9A-F]{64})$')
            if ($HashMatches.Count -ne 1 -or
                $PreparedText -notmatch '(?m)^NESTED_PREP_TASK=INSTALLED$' -or
                $PreparedText -notmatch '(?m)^NESTED_PREP_KERNEL_DEBUG=DISABLED$' -or
                $PreparedText -notmatch '(?m)^\s*Conversion Status:\s+Fully Decrypted\s*$') {
                throw 'Prepared disk provenance did not prove task, hash, disabled debugging, and decrypted OS volume.'
            }
            $HashFile = Join-Path $RunRoot 'bootmgfw.sha256'
            [IO.File]::WriteAllText($HashFile,
                $HashMatches[0].Groups[1].Value + "`r`n",
                [Text.ASCIIEncoding]::new())
            $NestedManifest = Join-Path $RunRoot 'nested-SHA256SUMS.txt'
            $NestedFiles = @(
                (Join-Path $Config.ProjectRoot `
                    'build\windows-static\ib-tcg-dump.exe'),
                (Join-Path $Config.ProjectRoot `
                    'build\windows-static\ibaudit.exe'))
            $NestedLines = @($NestedFiles | ForEach-Object {
                (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash +
                    '  ' + (Split-Path -Leaf $_)
            })
            [IO.File]::WriteAllText($NestedManifest,
                ($NestedLines -join "`n") + "`n",
                [Text.UTF8Encoding]::new($false))
            $Marker = Join-Path $RunRoot 'UEFILoader-Nested-E2E.marker'
            [IO.File]::WriteAllText($Marker, "schema=1`n",
                [Text.UTF8Encoding]::new($false))
            if ($Workflow -eq 'LinuxBootNextE2E') {
                $NestedFiles = @(
                    (Join-Path $Config.ProjectRoot `
                        'build\windows-static\ib-install.exe'),
                    (Join-Path $Config.ProjectRoot 'build\windows\loader.efi'),
                    (Join-Path $Config.ProjectRoot 'src\vendor\shimx64.efi'))
                $NestedLines = @($NestedFiles | ForEach-Object {
                    (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash +
                        '  ' + (Split-Path -Leaf $_)
                })
                [IO.File]::WriteAllText($NestedManifest,
                    ($NestedLines -join "`n") + "`n",
                    [Text.UTF8Encoding]::new($false))
                $BootNextMarker = Join-Path $RunRoot `
                    'UEFILoader-Nested-BootNext-Setup.marker'
                [IO.File]::WriteAllText($BootNextMarker, "schema=1`n",
                    [Text.UTF8Encoding]::new($false))
                $Files = @(
                    @{ Source = 'tools\host-runner\guest\Invoke-LinuxNestedWindowsE2E.sh'; Name = 'Invoke-LinuxNestedWindowsE2E.sh' },
                    @{ Source = 'tools\patches\edk2-stable202605-no-measure.patch'; Name = 'edk2-no-measure.patch' },
                    @{ Source = 'build\windows\loader.efi'; Name = 'loader.efi' },
                    @{ Source = 'src\vendor\shimx64.efi'; Name = 'shimx64.efi' },
                    @{ Source = 'build\windows-static\ib-install.exe'; Name = 'ib-install.exe' },
                    @{ AbsoluteSource = $NestedManifest; Name = 'nested-SHA256SUMS.txt' },
                    @{ AbsoluteSource = $BootNextMarker; Name = 'UEFILoader-Nested-BootNext-Setup.marker' }
                )
            }
            else {
                $Files = @(
                    @{ Source = 'tools\host-runner\guest\Invoke-LinuxNestedWindowsE2E.sh'; Name = 'Invoke-LinuxNestedWindowsE2E.sh' },
                    @{ Source = 'tools\patches\edk2-stable202605-no-measure.patch'; Name = 'edk2-no-measure.patch' },
                    @{ Source = 'build\windows\loader.efi'; Name = 'loader.efi' },
                    @{ Source = 'build\windows\handoff_payload_stub.efi'; Name = 'payload.efi' },
                    @{ AbsoluteSource = $PreparedLog; Name = 'tcglog.ib' },
                    @{ AbsoluteSource = $HashFile; Name = 'bootmgfw.sha256' },
                    @{ Source = 'build\windows-static\ib-tcg-dump.exe'; Name = 'ib-tcg-dump.exe' },
                    @{ Source = 'build\windows-static\ibaudit.exe'; Name = 'ibaudit.exe' },
                    @{ AbsoluteSource = $NestedManifest; Name = 'nested-SHA256SUMS.txt' },
                    @{ AbsoluteSource = $Marker; Name = 'UEFILoader-Nested-E2E.marker' }
                )
            }
        }
        if ($Workflow -ne 'LinuxPeRehash' -and
            $Workflow -ne 'LinuxNestedDiskProbe' -and
            $Workflow -ne 'LinuxNestedWindowsE2E' -and
            $Workflow -ne 'LinuxBootNextE2E') {
            $Files += @{
                Source = 'tools\patches\edk2-stable202605-no-measure.patch'
                Name = 'edk2-no-measure.patch'
            }
        }
        if ($Workflow -eq 'LinuxReplayDryRun') {
            $Files += @{
                Source = 'build\windows\loader.efi'
                Name = 'pcrcheck.efi'
            }
        }
        elseif ($Workflow -eq 'LinuxPeRehash') {
            $HostBootManager = [IO.Path]::GetFullPath(
                (Join-Path $env:SystemRoot 'Boot\EFI\bootmgfw.efi'))
            if (-not $HostBootManager.Equals(
                    'C:\Windows\Boot\EFI\bootmgfw.efi',
                    [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refuse unexpected host boot-manager path: $HostBootManager"
            }
            $Files += @{
                AbsoluteSource = $HostBootManager
                Name = 'bootmgfw.efi'
            }
        }
        elseif ($Workflow -in @(
                'LinuxHandoffMechanics', 'LinuxProductionHandoff',
                'LinuxOneShotCycle')) {
            $GeneratedLog = Join-Path $RunRoot 'tcglog.ib'
            $Generator = Invoke-UhrProcess `
                -FilePath (Join-Path $Config.ProjectRoot `
                    'build\windows\handoff_fixture_gen.exe') `
                -ArgumentList @($GeneratedLog) `
                -WorkingDirectory $Config.ProjectRoot -TimeoutSeconds 30
            if ($Generator.TimedOut -or $Generator.ExitCode -ne 0 -or
                -not (Test-Path -LiteralPath $GeneratedLog -PathType Leaf)) {
                throw "Handoff tcglog.ib generation failed: $($Generator.Stderr.Trim())"
            }
            $Replacement = if ($Workflow -eq 'LinuxOneShotCycle') {
                'src\vendor\shimx64.efi'
            } else {
                'build\windows\handoff_efi_smoke.efi'
            }
            $Files += @(
                @{ AbsoluteSource = $GeneratedLog; Name = 'tcglog.ib' },
                @{ Source = 'build\windows\handoff_payload_stub.efi'; Name = 'payload.efi' },
                @{ Source = 'build\windows\handoff_bootmgr_stub.efi'; Name = 'bootmgfw.efi.bak' },
                @{ Source = $Replacement; Name = 'replacement.efi' }
            )
            if ($Workflow -eq 'LinuxOneShotCycle') {
                $Files += @{
                    Source = 'build\windows\loader.efi'
                    Name = 'grubx64.efi'
                }
            }
        }
    }
    else {
        $GuestTestTemplate = Get-Content -Raw -LiteralPath (Join-Path `
            $Config.ProjectRoot `
            'tools\host-runner\guest\Invoke-WindowsGuestTest.ps1')
        $Placeholder = '__NESTED_SCRIPT_BASE64__'
        if ([regex]::Matches($GuestTestTemplate,
                [regex]::Escape($Placeholder)).Count -ne 1) {
            throw 'Windows guest-test template has an invalid nested-script placeholder.'
        }
        $NestedScriptBytes = [IO.File]::ReadAllBytes((Join-Path `
            $Config.ProjectRoot `
            'tools\host-runner\guest\Invoke-NestedE2ECapture.ps1'))
        $GeneratedGuestTest = Join-Path $RunRoot `
            'Invoke-WindowsGuestTest.ps1'
        [IO.File]::WriteAllText($GeneratedGuestTest,
            $GuestTestTemplate.Replace($Placeholder,
                [Convert]::ToBase64String($NestedScriptBytes)),
            [Text.UTF8Encoding]::new($false))
        $Files = @(
            @{ Source = 'build\windows-static\ib-tcg-dump.exe'; Name = 'ib-tcg-dump.exe' },
            @{ AbsoluteSource = $GeneratedGuestTest; Name = 'Invoke-WindowsGuestTest.ps1' }
        )
    }
    $Manifest = @()
    foreach ($File in $Files) {
        $Source = if ($File.ContainsKey('AbsoluteSource')) {
            [IO.Path]::GetFullPath([string]$File.AbsoluteSource)
        } else {
            Join-Path $Config.ProjectRoot $File.Source
        }
        if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
            throw "Staged source file is missing: $Source"
        }
        $Destination = Join-Path $Stage $File.Name
        Copy-Item -LiteralPath $Source -Destination $Destination
        $Manifest += [pscustomobject]@{
            Name = $File.Name
            Sha256 = (Get-FileHash -LiteralPath $Destination `
                -Algorithm SHA256).Hash
        }
    }
    $SumLines = @($Manifest | ForEach-Object { "$($_.Sha256)  $($_.Name)" })
    [IO.File]::WriteAllText((Join-Path $Stage 'SHA256SUMS.txt'),
        (($SumLines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
    Write-UhrAtomicJson -Path (Join-Path $RunRoot 'state.json') -Value ([ordered]@{
        SchemaVersion = 1
        RunId = $RunId
        Workflow = $Workflow
        State = 'PREPARED'
        CreatedUtc = [DateTime]::UtcNow.ToString('o')
        Manifest = $Manifest
    })
    return [pscustomobject]@{
        RunRoot = $RunRoot
        Stage = $Stage
        Evidence = $Evidence
    }
}

function Acquire-SharedLease {
    param(
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)]$Selected
    )
    $Resource = [string]$Selected.LeaseResource
    $Status = & $Config.SharedLease.ScriptPath -Action Status `
        -Resource $Resource
    if ($Status.Status -ne 'FREE') {
        throw "Resource '$Resource' is already leased by $($Status.Agent) until $($Status.ExpiresUtc)."
    }
    return & $Config.SharedLease.ScriptPath -Action Acquire `
        -Resource $Resource `
        -Agent 'Codex-UEFILoaderHostRunner' `
        -Project ([string]$Config.ProjectRoot) `
        -RunId $RunId -LeaseMinutes ([int]$Config.Policy.LeaseMinutes)
}

function Release-SharedLease {
    param([Parameter(Mandatory=$true)]$Lease)
    & $Config.SharedLease.ScriptPath -Action Release `
        -Resource ([string]$Lease.Resource) `
        -LeaseToken $Lease.LeaseToken | Out-Null
}

function Acquire-StartupLease {
    param([Parameter(Mandatory=$true)][string]$RunId)
    $Resource = 'host-vm-startup'
    $Deadline = [DateTime]::UtcNow.AddMinutes(
        [int]$Config.Policy.StartupLeaseMinutes)
    do {
        $Status = & $Config.SharedLease.ScriptPath -Action Status `
            -Resource $Resource
        if ($Status.Status -ceq 'EXPIRED') {
            throw "The '$Resource' lease is expired; inspect host startup state before takeover."
        }
        if ($Status.Status -ceq 'FREE') {
            try {
                return & $Config.SharedLease.ScriptPath -Action Acquire `
                    -Resource $Resource `
                    -Agent 'Codex-UEFILoaderHostRunner' `
                    -Project ([string]$Config.ProjectRoot) `
                    -RunId $RunId `
                    -LeaseMinutes ([int]$Config.Policy.StartupLeaseMinutes)
            }
            catch {
                $AcquireError = $_
                $AfterRace = & $Config.SharedLease.ScriptPath -Action Status `
                    -Resource $Resource
                if ($AfterRace.Status -cne 'ACTIVE' -or
                    [DateTime]::UtcNow -ge $Deadline) {
                    throw $AcquireError
                }
            }
        }
        Start-Sleep -Seconds 2
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "Timed out waiting for the '$Resource' lease."
}

function Start-ConfiguredVm {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$RunId
    )
    if (-not $AuthorizeLiveVmRun) {
        throw 'Starting the configured VM requires -AuthorizeLiveVmRun.'
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$Selected.SnapshotName) -and
        -not $AuthorizeSnapshotRevert) {
        throw 'The configured snapshot requires -AuthorizeSnapshotRevert.'
    }
    $StartupLease = $null
    try {
        $StartupLease = Acquire-StartupLease $RunId
        Assert-ConcurrentVmSetSafe $Selected
        Assert-HostCapacity $Selected
        if (-not [string]::IsNullOrWhiteSpace([string]$Selected.SnapshotName)) {
            Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                'revertToSnapshot', [string]$Selected.VmxPath,
                [string]$Selected.SnapshotName) | Out-Null
        }
        Assert-ConfiguredWindowsMailbox $Selected
        Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
            'start', [string]$Selected.VmxPath, 'nogui') | Out-Null
        $Deadline = [DateTime]::UtcNow.AddSeconds(
            [int]$Config.Policy.GuestReadyTimeoutSeconds)
        $ToolsReady = $false
        do {
            Start-Sleep -Seconds 2
            $Tools = Invoke-ConfiguredVmrun -Selected $Selected `
                -Operation @('checkToolsState', [string]$Selected.VmxPath) `
                -TimeoutSeconds 20 -AllowFailure
            if ($Tools.ExitCode -eq 0 -and
                ($Tools.Stdout + $Tools.Stderr).Trim() -match `
                    '^(running|installed)$') {
                $ToolsReady = $true
                break
            }
        } while ([DateTime]::UtcNow -lt $Deadline)
        if (-not $ToolsReady) {
            throw 'VMware Tools did not become ready before the deadline.'
        }
        if (Test-VmwareGuestInfo $Selected) {
            [void](Wait-VmwareGuestInfoWorkerReady -Selected $Selected)
        }
        return
    }
    finally {
        if ($null -ne $StartupLease) {
            Release-SharedLease $StartupLease
        }
    }
}

function Stop-ConfiguredVmAfterPass {
    param([Parameter(Mandatory=$true)]$Selected)
    if (-not $AuthorizeVmStop) {
        throw 'A complete live run requires -AuthorizeVmStop for deterministic cleanup.'
    }
    Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
        'stop', [string]$Selected.VmxPath, 'hard') | Out-Null
    if (-not [string]::IsNullOrWhiteSpace([string]$Selected.SnapshotName)) {
        if (-not $AuthorizeSnapshotRevert) {
            throw 'Post-run snapshot restore requires -AuthorizeSnapshotRevert.'
        }
        Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
            'revertToSnapshot', [string]$Selected.VmxPath,
            [string]$Selected.SnapshotName) | Out-Null
    }
}

function Export-PreparedWindowsDisk {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$RunId
    )
    if (-not $AuthorizeVmStop -or -not $AuthorizeSnapshotRevert) {
        throw 'Prepared-disk export requires VM stop and snapshot-revert authorization.'
    }
    if ([string]::IsNullOrWhiteSpace([string]$Selected.SnapshotName)) {
        throw 'Prepared-disk export requires a configured clean snapshot.'
    }

    Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
        'stop', [string]$Selected.VmxPath, 'soft') | Out-Null
    $Deadline = [DateTime]::UtcNow.AddSeconds(180)
    while ((Test-ConfiguredVmRunning $Selected) -and
        [DateTime]::UtcNow -lt $Deadline) {
        Start-Sleep -Seconds 2
    }
    if (Test-ConfiguredVmRunning $Selected) {
        throw 'Windows did not complete a clean shutdown; refuse to export its disk.'
    }

    $VmxPath = [IO.Path]::GetFullPath([string]$Selected.VmxPath)
    $VmxText = Get-Content -Raw -LiteralPath $VmxPath
    $DiskMatches = [regex]::Matches($VmxText,
        '(?im)^nvme0:0\.fileName\s*=\s*"([^"]+\.vmdk)"\s*$')
    if ($DiskMatches.Count -ne 1) {
        throw 'The Windows VMX must contain exactly one nvme0:0 VMDK descriptor.'
    }
    $DiskName = $DiskMatches[0].Groups[1].Value
    $SourceDisk = if ([IO.Path]::IsPathRooted($DiskName)) {
        [IO.Path]::GetFullPath($DiskName)
    } else {
        [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $VmxPath) `
            $DiskName))
    }
    if (-not (Test-UhrPathWithin -Root (Split-Path -Parent $VmxPath) `
            -Path $SourceDisk) -or
        -not (Test-Path -LiteralPath $SourceDisk -PathType Leaf)) {
        throw 'The Windows disk descriptor escaped its VM directory or is missing.'
    }

    $PreparedDiskRoot = Get-NestedPreparedDiskRoot
    New-Item -ItemType Directory -Force -Path $PreparedDiskRoot | Out-Null
    $DestinationDisk = Join-Path $PreparedDiskRoot ($RunId + '.vmdk')
    if (Test-Path -LiteralPath $DestinationDisk) {
        throw "Prepared disk destination already exists: $DestinationDisk"
    }
    $VdiskManager = Join-Path (Split-Path -Parent `
        ([string]$Config.Tools.VmrunPath)) 'vmware-vdiskmanager.exe'
    if (-not (Test-Path -LiteralPath $VdiskManager -PathType Leaf)) {
        throw "VMware disk manager is missing: $VdiskManager"
    }
    $Convert = Invoke-UhrProcess -FilePath $VdiskManager -ArgumentList @(
        '-r', $SourceDisk, '-t', '0', $DestinationDisk) `
        -WorkingDirectory $Config.ProjectRoot -TimeoutSeconds 1200
    if ($Convert.TimedOut -or $Convert.ExitCode -ne 0) {
        throw "Prepared disk export failed: $($Convert.Stderr.Trim())"
    }
    $DiskItem = Get-Item -LiteralPath $DestinationDisk
    if ($DiskItem.Length -lt 1GB) {
        throw 'Prepared disk export is unexpectedly small.'
    }

    Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
        'revertToSnapshot', [string]$Selected.VmxPath,
        [string]$Selected.SnapshotName) | Out-Null
    if (Test-ConfiguredVmRunning $Selected) {
        throw 'The Windows VM appears running after prepared-disk export.'
    }
    return [pscustomobject]@{
        Path = $DestinationDisk
        Bytes = $DiskItem.Length
        Source = $SourceDisk
    }
}

function New-ConfiguredWindowsSnapshot {
    param([Parameter(Mandatory=$true)][string]$Name)
    if (-not $AuthorizeSnapshotCreate) {
        throw 'Snapshot creation requires -AuthorizeSnapshotCreate.'
    }
    if ($Name -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{2,63}$') {
        throw 'SnapshotName must be a safe 3-64 character name.'
    }
    $Selected = Get-ProfileConfig Windows
    Assert-NotQuarantined Windows
    Assert-ProfileCredentialsReady $Selected
    if (Test-ConfiguredVmRunning $Selected) {
        throw 'The configured Windows VM must be powered off before snapshot creation.'
    }
    Assert-ConcurrentVmSetSafe $Selected
    $RunId = 'uefi-windows-snapshot-' +
        [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' +
        [Guid]::NewGuid().ToString('N').Substring(0, 10)
    $Lease = $null
    try {
        $Lease = Acquire-SharedLease -RunId $RunId -Selected $Selected
        $Before = Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
            'listSnapshots', [string]$Selected.VmxPath)
        $Existing = @($Before.Stdout -split "`r?`n" | Select-Object -Skip 1 |
            Where-Object { $_.Trim() -ceq $Name })
        if ($Existing.Count -ne 0) {
            throw "Snapshot already exists; refusing to overwrite it: $Name"
        }
        Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
            'snapshot', [string]$Selected.VmxPath, $Name) | Out-Null
        $After = Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
            'listSnapshots', [string]$Selected.VmxPath)
        $Created = @($After.Stdout -split "`r?`n" | Select-Object -Skip 1 |
            Where-Object { $_.Trim() -ceq $Name })
        if ($Created.Count -ne 1) {
            throw "Snapshot creation could not be verified: $Name"
        }
        $RecordPath = Join-Path (Join-Path $Config.StateRoot 'snapshots') `
            ($Name + '.json')
        Write-UhrAtomicJson -Path $RecordPath -Value ([ordered]@{
            SchemaVersion = 1
            Profile = 'Windows'
            VmxPath = [IO.Path]::GetFullPath([string]$Selected.VmxPath)
            SnapshotName = $Name
            CreatedUtc = [DateTime]::UtcNow.ToString('o')
            RunId = $RunId
        })
        return [pscustomobject]@{
            Result = 'PASS'
            Profile = 'Windows'
            Snapshot = $Name
            Evidence = $RecordPath
        }
    }
    finally {
        if ($null -ne $Lease) { Release-SharedLease $Lease }
    }
}

function Copy-StageToGuest {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)]$Paths,
        [Parameter(Mandatory=$true)][string]$GuestRunRoot,
        [Parameter(Mandatory=$true)][ValidateSet('Linux', 'Windows')][string]$Name
    )
    if ($Name -eq 'Linux') {
        $Create = @('runProgramInGuest', [string]$Selected.VmxPath,
            '/bin/bash', '-lc', "mkdir -p -- '$GuestRunRoot'")
        $Separator = '/'
    }
    else {
        $CreateScript = "New-Item -ItemType Directory -Force -LiteralPath '$GuestRunRoot' | Out-Null"
        $Create = @('runScriptInGuest', [string]$Selected.VmxPath,
            'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
            $CreateScript)
        $Separator = '\'
    }
    Invoke-ConfiguredVmrun -Selected $Selected -GuestAuthentication `
        -Operation $Create | Out-Null
    foreach ($File in Get-ChildItem -LiteralPath $Paths.Stage -File) {
        $GuestPath = $GuestRunRoot.TrimEnd([char[]]@('/', '\')) + $Separator + $File.Name
        Invoke-ConfiguredVmrun -Selected $Selected -GuestAuthentication `
            -Operation @('CopyFileFromHostToGuest',
                [string]$Selected.VmxPath, $File.FullName, $GuestPath) | Out-Null
    }
}

function Copy-GuestEvidence {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)]$Paths,
        [Parameter(Mandatory=$true)][string]$GuestRunRoot,
        [Parameter(Mandatory=$true)][ValidateSet('Linux', 'Windows')][string]$Name,
        [Parameter(Mandatory=$true)][string[]]$Files
    )
    $Separator = if ($Name -eq 'Linux') { '/' } else { '\' }
    foreach ($File in $Files) {
        $GuestPath = $GuestRunRoot.TrimEnd([char[]]@('/', '\')) + $Separator + $File
        $HostPath = Join-Path $Paths.Evidence $File
        $Copy = Invoke-ConfiguredVmrun -Selected $Selected -GuestAuthentication `
            -Operation @('CopyFileFromGuestToHost',
                [string]$Selected.VmxPath, $GuestPath, $HostPath) -AllowFailure
        if ($Copy.ExitCode -ne 0 -and $File -eq 'result.json') {
            throw 'The guest did not publish result.json.'
        }
    }
}

function Invoke-LiveWorkflow {
    param(
        [Parameter(Mandatory=$true)]
        [ValidateSet(
            'LinuxPositive', 'LinuxReplayDryRun', 'LinuxPeRehash',
            'LinuxDivergenceManifest', 'LinuxHandoffMechanics',
            'LinuxProductionHandoff', 'LinuxOneShotCycle',
            'LinuxNestedDiskProbe', 'LinuxNestedWindowsE2E',
            'WindowsProbe', 'WindowsCapture', 'WindowsNestedPrep')]
        [string]$Workflow
    )
    if (-not $AuthorizeLiveVmRun) {
        throw 'Live workflows require -AuthorizeLiveVmRun.'
    }
    if (-not $AuthorizeVmStop) {
        throw 'Live workflows require -AuthorizeVmStop.'
    }
    if ($Workflow -eq 'LinuxPositive') {
        $Name = 'Linux'
        $WorkflowId = 'linux-positive'
    }
    elseif ($Workflow -eq 'LinuxReplayDryRun') {
        $Name = 'Linux'
        $WorkflowId = 'linux-replay-dry-run'
    }
    elseif ($Workflow -eq 'LinuxPeRehash') {
        $Name = 'Linux'
        $WorkflowId = 'linux-pe-rehash'
    }
    elseif ($Workflow -eq 'LinuxDivergenceManifest') {
        $Name = 'Linux'
        $WorkflowId = 'linux-divergence-manifest'
    }
    elseif ($Workflow -eq 'LinuxHandoffMechanics') {
        $Name = 'Linux'
        $WorkflowId = 'linux-handoff-mechanics'
    }
    elseif ($Workflow -eq 'LinuxProductionHandoff') {
        $Name = 'Linux'
        $WorkflowId = 'linux-production-handoff'
    }
    elseif ($Workflow -eq 'LinuxOneShotCycle') {
        $Name = 'Linux'
        $WorkflowId = 'linux-oneshot-cycle'
    }
    elseif ($Workflow -eq 'LinuxNestedDiskProbe') {
        $Name = 'Linux'
        $WorkflowId = 'linux-nested-disk-probe'
    }
    elseif ($Workflow -eq 'LinuxNestedWindowsE2E') {
        $Name = 'Linux'
        $WorkflowId = 'linux-nested-windows-e2e'
    }
    elseif ($Workflow -eq 'WindowsProbe') {
        $Name = 'Windows'
        $WorkflowId = 'windows-probe'
    }
    elseif ($Workflow -eq 'WindowsCapture') {
        $Name = 'Windows'
        $WorkflowId = 'windows-capture'
    }
    else {
        $Name = 'Windows'
        $WorkflowId = 'windows-capture'
    }
    $Selected = Get-ProfileConfig $Name
    Assert-NotQuarantined $Name
    Assert-ProfileCredentialsReady $Selected
    Assert-ConcurrentVmSetSafe $Selected
    if (-not $SkipBuild) { Invoke-HostBuild | Out-Host }
    $RunId = New-UhrRunId $WorkflowId
    $Paths = New-HostStage -RunId $RunId -Workflow $Workflow
    $GuestRunRoot = [string]$Selected.GuestStageRoot
    $GuestRunRoot = $GuestRunRoot.TrimEnd([char[]]@('/', '\')) +
        $(if ($Name -eq 'Linux') { '/' } else { '\' }) + 'runs' +
        $(if ($Name -eq 'Linux') { '/' } else { '\' }) + $RunId
    $Lease = $null
    $LiveTouched = $false
    $Success = $false
    $GuestInfoTransport = $null
    $GuestInfoResult = $null
    $NestedShareAdded = $false
    $NestedDisk = $null
    try {
        $Lease = Acquire-SharedLease -RunId $RunId -Selected $Selected
        $LiveTouched = $true
        Start-ConfiguredVm -Selected $Selected -RunId $RunId
        if ($Workflow -in @('LinuxNestedDiskProbe',
                'LinuxNestedWindowsE2E')) {
            $PreparedDiskRoot = Get-NestedPreparedDiskRoot
            $NestedDisk = Get-LatestNestedPreparedDisk
            Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                'enableSharedFolders', [string]$Selected.VmxPath) | Out-Null
            Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                'addSharedFolder', [string]$Selected.VmxPath,
                'UEFILoaderNestedE2E', $PreparedDiskRoot) | Out-Null
            $NestedShareAdded = $true
        }
        $GuestResult = $null
        $Mailbox = $null
        $Operation = $null
        $EvidenceFiles = $null
        if ($Name -eq 'Windows' -and
            (Test-SharedFolderMailbox $Selected)) {
            $GuestAction = if ($Workflow -eq 'WindowsProbe') {
                'Probe'
            } else { 'Capture' }
            $Mailbox = Publish-WindowsMailboxRequest -Selected $Selected `
                -Paths $Paths -RunId $RunId -GuestAction $GuestAction
            $Result = Wait-WindowsMailboxResult -Selected $Selected `
                -Mailbox $Mailbox -RunId $RunId
            Copy-WindowsMailboxEvidence -Mailbox $Mailbox -Paths $Paths `
                -Result $Result
        }
        elseif ($Name -eq 'Windows' -and (Test-VmwareGuestInfo $Selected)) {
            $GuestAction = if ($Workflow -eq 'WindowsProbe') {
                'Probe'
            } else { 'Capture' }
            $GuestInfoTransport = Publish-WindowsGuestInfoRequest `
                -Selected $Selected -Paths $Paths -RunId $RunId `
                -GuestAction $GuestAction `
                -NestedPrep:($Workflow -eq 'WindowsNestedPrep')
            $GuestInfoResult = Wait-WindowsGuestInfoEvidence -Selected $Selected `
                -Transport $GuestInfoTransport -Paths $Paths -RunId $RunId
        }
        else {
            Copy-StageToGuest -Selected $Selected -Paths $Paths `
                -GuestRunRoot $GuestRunRoot -Name $Name
        }
        if ($Name -eq 'Linux') {
            $GuestMode = switch ($Workflow) {
                'LinuxReplayDryRun' { 'ReplayDryRun' }
                'LinuxPeRehash' { 'PeRehash' }
                'LinuxDivergenceManifest' { 'DivergenceManifest' }
                'LinuxHandoffMechanics' { 'HandoffMechanics' }
                'LinuxProductionHandoff' { 'ProductionHandoff' }
                'LinuxOneShotCycle' { 'OneShotCycle' }
                'LinuxNestedDiskProbe' { 'NestedDiskProbe' }
                'LinuxNestedWindowsE2E' { 'NestedWindowsE2E' }
                default { 'PositiveGate' }
            }
            if ($Workflow -eq 'LinuxNestedDiskProbe') {
                $GuestDisk = '/mnt/hgfs/UEFILoaderNestedE2E/' +
                    $NestedDisk.Name
                $Operation = @('runProgramInGuest',
                    [string]$Selected.VmxPath, '/bin/bash',
                    "$GuestRunRoot/Invoke-LinuxNestedDiskProbe.sh",
                    $GuestRunRoot, $GuestDisk)
            }
            elseif ($Workflow -eq 'LinuxNestedWindowsE2E') {
                $GuestDisk = '/mnt/hgfs/UEFILoaderNestedE2E/' +
                    $NestedDisk.Name
                $Operation = @('runProgramInGuest',
                    [string]$Selected.VmxPath, '/bin/bash',
                    "$GuestRunRoot/Invoke-LinuxNestedWindowsE2E.sh",
                    $GuestRunRoot, $GuestDisk)
            }
            else {
                $Operation = @('runProgramInGuest',
                    [string]$Selected.VmxPath, '/bin/bash',
                    "$GuestRunRoot/Invoke-LinuxPositivePath.sh",
                    $GuestRunRoot, $GuestMode)
            }
            $EvidenceFiles = @('result.json', 'baseline.txt',
                'after-pcr-check.txt',
                'after-raw-extend.txt', 'divergence.ib',
                'manifest-parse.txt',
                'restore-verify.txt', 'final-bootmgfw.efi',
                'fat-directory.txt', 'handoff-order.txt',
                'handoff-status.txt',
                'qemu-debugcon.log', 'qemu-serial.log', 'qemu-stderr.log',
                'swtpm.log', 'pcrread-stderr.log', 'edk2-build.log',
                'windows-disk-info.json', 'windows-esp-map.txt')
            if ($Workflow -eq 'LinuxNestedWindowsE2E') {
                $EvidenceFiles += @(
                    'condition-result.json', 'conditioning-serial.log',
                    'conditioning-debugcon.log',
                    'conditioning-qemu-stderr.log',
                    'conditioning-swtpm.log',
                    'windows-overlay-create.txt',
                    'nested-result.json', 'postboot-tcglog.ib',
                    'postboot-tcglog-full.bin',
                    'postboot-probe.stdout.txt',
                    'postboot-probe.stderr.txt',
                    'postboot-capture.stdout.txt',
                    'postboot-capture.stderr.txt',
                    'postboot-audit-dump.stdout.txt',
                    'postboot-audit-dump.stderr.txt',
                    'postboot-verify-pcrs.stdout.txt',
                    'postboot-verify-pcrs.stderr.txt')
            }
        }
        elseif ($Name -eq 'Windows' -and
            -not (Test-SharedFolderMailbox $Selected) -and
            -not (Test-VmwareGuestInfo $Selected)) {
            $GuestAction = if ($Workflow -eq 'WindowsProbe') {
                'Probe'
            } else { 'Capture' }
            $Operation = @('runProgramInGuest', [string]$Selected.VmxPath,
                'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
                '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                '-File', "$GuestRunRoot\Invoke-WindowsGuestTest.ps1",
                '-RunRoot', $GuestRunRoot, '-Action', $GuestAction,
                '-ExpectedProbeVerdict', [string]$Selected.ExpectedProbeVerdict)
            $EvidenceFiles = @('result.json', 'stdout.txt', 'stderr.txt',
                'tcglog.ib', 'tcglog-full.bin', 'bitlocker-status.txt',
                'bootmgfw.sha256')
        }
        if ($null -ne $Operation) {
            $GuestResult = Invoke-ConfiguredVmrun -Selected $Selected `
                -GuestAuthentication -Operation $Operation -AllowFailure `
                -TimeoutSeconds ([int]$Config.Policy.OperationTimeoutSeconds)
            Copy-GuestEvidence -Selected $Selected -Paths $Paths `
                -GuestRunRoot $GuestRunRoot -Name $Name -Files $EvidenceFiles
        }
        $ResultPath = Join-Path $Paths.Evidence 'result.json'
        $Result = Get-Content -Raw -LiteralPath $ResultPath | ConvertFrom-Json
        if ($Result.SchemaVersion -ne 1 -or $Result.RunId -cne $RunId) {
            throw 'Guest result identity/schema mismatch.'
        }
        if ($Result.Status -cne 'PASS') {
            throw "Guest workflow reported $($Result.Status): $($Result.Message)"
        }
        if ($null -ne $GuestResult -and $GuestResult.TimedOut) {
            throw 'Guest vmrun operation timed out.'
        }
        if ($null -ne $GuestInfoTransport) {
            Clear-VmwareGuestInfoVariables -Selected $Selected -VariableNames @(
                $GuestInfoTransport.RequestVariableNames +
                $GuestInfoResult.ResponseVariableNames)
            $GuestInfoTransport = $null
        }
        if ($NestedShareAdded) {
            Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                'removeSharedFolder', [string]$Selected.VmxPath,
                'UEFILoaderNestedE2E') -AllowFailure | Out-Null
            $NestedShareAdded = $false
        }
        $PreparedDisk = $null
        if ($Workflow -eq 'WindowsNestedPrep') {
            $PreparedDisk = Export-PreparedWindowsDisk -Selected $Selected `
                -RunId $RunId
        }
        else {
            Stop-ConfiguredVmAfterPass $Selected
        }
        Release-SharedLease $Lease
        $Lease = $null
        $Success = $true
        return [pscustomobject]@{
            Result = 'PASS'
            RunId = $RunId
            Workflow = $Workflow
            Evidence = $Paths.Evidence
            PreparedDisk = if ($null -ne $PreparedDisk) {
                $PreparedDisk.Path
            } else { $null }
        }
    }
    finally {
        if ($NestedShareAdded) {
            try {
                Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                    'removeSharedFolder', [string]$Selected.VmxPath,
                    'UEFILoaderNestedE2E') -AllowFailure | Out-Null
            }
            catch {}
        }
        if ($null -ne $GuestInfoTransport) {
            $CleanupNames = @($GuestInfoTransport.RequestVariableNames)
            if ($null -ne $GuestInfoResult) {
                $CleanupNames += @($GuestInfoResult.ResponseVariableNames)
            }
            Clear-VmwareGuestInfoVariables -Selected $Selected `
                -VariableNames $CleanupNames
        }
        if (-not $Success -and $LiveTouched) {
            $Recovered = $false
            $RecoveryMessage = $null
            if ($AuthorizeVmStop -and
                ([string]::IsNullOrWhiteSpace(
                    [string]$Selected.SnapshotName) -or
                 $AuthorizeSnapshotRevert)) {
                try {
                    if (Test-ConfiguredVmRunning $Selected) {
                        Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                            'stop', [string]$Selected.VmxPath, 'hard') |
                            Out-Null
                    }
                    if (-not [string]::IsNullOrWhiteSpace(
                            [string]$Selected.SnapshotName)) {
                        Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                            'revertToSnapshot', [string]$Selected.VmxPath,
                            [string]$Selected.SnapshotName) | Out-Null
                    }
                    if (Test-ConfiguredVmRunning $Selected) {
                        throw 'The VM still appears running after failure recovery.'
                    }
                    $Recovered = $true
                    $RecoveryMessage = 'The failed workflow VM was hard-stopped, reverted, and verified off.'
                }
                catch {
                    $RecoveryMessage = $_.Exception.Message
                }
            }
            if ($Recovered) {
                if ($null -ne $Lease) {
                    Release-SharedLease $Lease
                    $Lease = $null
                }
                Write-UhrAtomicJson -Path (
                    Join-Path $Paths.Evidence 'failure-recovery.json') `
                    -Value ([ordered]@{
                        SchemaVersion = 1
                        Profile = $Name
                        RunId = $RunId
                        Result = 'RECOVERED'
                        Message = $RecoveryMessage
                        Snapshot = [string]$Selected.SnapshotName
                        CompletedUtc = [DateTime]::UtcNow.ToString('o')
                    })
            }
            else {
                $Quarantine = Get-QuarantinePath $Name
                Write-UhrAtomicJson -Path $Quarantine -Value ([ordered]@{
                    SchemaVersion = 1
                    Profile = $Name
                    RunId = $RunId
                    Message = 'A live workflow failed and deterministic VM recovery did not complete. Inspect the VM and evidence; do not retry automatically.'
                    RecoveryMessage = $RecoveryMessage
                    CreatedUtc = [DateTime]::UtcNow.ToString('o')
                    LeaseId = if ($null -ne $Lease) { $Lease.LeaseId } else { $null }
                    LeaseResource = [string]$Selected.LeaseResource
                })
            }
        }
    }
}

function Invoke-WslNestedWindowsE2E {
    param([switch]$BootNext)
    if (-not $AuthorizeLiveVmRun -or -not $AuthorizeVmStop) {
        throw 'The WSL nested E2E gate requires VM-run and VM-stop authorization.'
    }
    $WslCommand = Get-Command 'wsl.exe' -ErrorAction Stop
    $WslPath = $WslCommand.Source
    $InvokeWsl = {
        param(
            [Parameter(Mandatory=$true)][string[]]$WslArguments,
            [int]$TimeoutSeconds = 30
        )
        Invoke-UhrProcess -FilePath $WslPath `
            -ArgumentList (@('-d', $WslDistribution, '--') + $WslArguments) `
            -WorkingDirectory $Config.ProjectRoot `
            -TimeoutSeconds $TimeoutSeconds
    }
    $ToWslPath = {
        param([Parameter(Mandatory=$true)][string]$WindowsPath)
        $FullPath = [IO.Path]::GetFullPath($WindowsPath)
        if ($FullPath -cnotmatch '^([A-Za-z]):\\(.+)$') {
            throw "WSL conversion requires a drive-rooted Windows path: $FullPath"
        }
        $Drive = $Matches[1].ToLowerInvariant()
        $Tail = $Matches[2].Replace('\', '/')
        $Value = "/mnt/$Drive/$Tail"
        if ($Value -cnotmatch '^/mnt/[a-z]/[A-Za-z0-9_./-]+$') {
            throw "WSL returned an unsafe mounted path: $Value"
        }
        return $Value
    }

    $Preflight = & $InvokeWsl -WslArguments @('/bin/bash', '-lc',
        'set -e; test -r /dev/kvm; test -w /dev/kvm; command -v git make gcc nasm iasl python3 qemu-system-x86_64 qemu-img swtpm swtpm_setup tpm2_pcrread truncate parted mformat mmd mcopy pkg-config >/dev/null; pkg-config --exists uuid; test -s /usr/share/efi-shell-x64/shellx64.efi')
    if ($Preflight.TimedOut -or $Preflight.ExitCode -ne 0) {
        throw "WSL nested-E2E prerequisites are not ready: $($Preflight.Stderr.Trim())"
    }
    $Existing = & $InvokeWsl -WslArguments @('/usr/bin/pgrep', '-a', '-f',
        'Invoke-LinuxNestedWindowsE2E.sh|^qemu-system-x86_64 |^swtpm ') 
    if (-not $Existing.TimedOut -and $Existing.ExitCode -eq 0 -and
        -not [string]::IsNullOrWhiteSpace($Existing.Stdout)) {
        throw "A WSL nested-E2E process already exists; refuse to attach: $($Existing.Stdout.Trim())"
    }

    if (-not $SkipBuild) { Invoke-HostBuild | Out-Host }
    $RunId = New-UhrRunId $(if ($BootNext) {
            'linux-bootnext-e2e'
        } else {
            'linux-nested-windows-e2e'
        })
    $Paths = New-HostStage -RunId $RunId `
        -Workflow $(if ($BootNext) {
            'LinuxBootNextE2E'
        } else {
            'LinuxNestedWindowsE2E'
        })
    $PreparedDisk = Get-LatestNestedPreparedDisk
    $StageWsl = & $ToWslPath $Paths.Stage
    $DiskWsl = & $ToWslPath $PreparedDisk.FullName
    $LeaseTarget = [pscustomobject]@{
        LeaseResource = 'vm-uefiloader-wsl2'
    }
    $Lease = $null
    $StartupLease = $null
    $Success = $false
    $ProcessStarted = $false
    try {
        $Lease = Acquire-SharedLease -RunId $RunId -Selected $LeaseTarget
        $StartupLease = Acquire-StartupLease $RunId
        try {
            $ConcurrencyTarget = [pscustomobject]@{
                VmxPath = Join-Path $Config.StateRoot 'wsl2-qemu-placeholder.vmx'
            }
            Assert-ConcurrentVmSetSafe $ConcurrencyTarget
            Assert-HostCapacity (Get-ProfileConfig Linux)
        }
        finally {
            if ($null -ne $StartupLease) {
                Release-SharedLease $StartupLease
                $StartupLease = $null
            }
        }
        $ProcessStarted = $true
        $Execution = & $InvokeWsl -WslArguments @('/bin/bash',
            "$StageWsl/Invoke-LinuxNestedWindowsE2E.sh",
            $StageWsl, $DiskWsl, $(if ($BootNext) {
                'BootNext'
            } else {
                'Replay'
            })) `
            -TimeoutSeconds ([int]$Config.Policy.OperationTimeoutSeconds)
        [IO.File]::WriteAllText((Join-Path $Paths.Evidence 'wsl-stdout.txt'),
            $Execution.Stdout, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $Paths.Evidence 'wsl-stderr.txt'),
            $Execution.Stderr, [Text.UTF8Encoding]::new($false))
        foreach ($Name in @(
                'result.json', 'baseline.txt', 'condition-result.json',
                'conditioning-serial.log', 'conditioning-debugcon.log',
                'conditioning-qemu-stderr.log', 'conditioning-swtpm.log',
                'windows-overlay-create.txt', 'nested-result.json',
                'postboot-tcglog.ib', 'postboot-tcglog-full.bin',
                'postboot-probe.stdout.txt', 'postboot-probe.stderr.txt',
                'postboot-capture.stdout.txt', 'postboot-capture.stderr.txt',
                'postboot-audit-dump.stdout.txt',
                'postboot-audit-dump.stderr.txt',
                'postboot-verify-pcrs.stdout.txt',
                'postboot-verify-pcrs.stderr.txt',
                'divergence.ib', 'handoff-status.txt',
                'qemu-debugcon.log', 'qemu-serial.log', 'qemu-stderr.log',
                'swtpm.log', 'edk2-build.log', 'windows-disk-info.json')) {
            $Source = Join-Path $Paths.Stage $Name
            if (Test-Path -LiteralPath $Source -PathType Leaf) {
                Copy-Item -LiteralPath $Source `
                    -Destination (Join-Path $Paths.Evidence $Name) -Force
            }
        }
        if ($BootNext) {
            foreach ($Name in @(
                    'bootnext-setup-result.json', 'bootnext-result.json',
                    'bootnext-set.stdout.txt', 'bootnext-set.stderr.txt',
                    'bootnext-clear.stdout.txt', 'bootnext-clear.stderr.txt',
                    'bootnext-firmware-before.txt',
                    'bootnext-firmware-after-set.txt',
                    'bootnext-firmware-before-cleanup.txt',
                    'bootnext-firmware-after-cleanup.txt',
                    'bootnext-bootmgfw-before.sha256',
                    'bootnext-bootmgfw-after.sha256',
                    'bootnext-handoff-status.txt',
                    'bootnext-first-serial.log',
                    'bootnext-first-debugcon.log',
                    'bootnext-first-qemu-stderr.log',
                    'bootnext-second-serial.log',
                    'bootnext-second-debugcon.log',
                    'bootnext-second-qemu-stderr.log',
                    'bootnext-swtpm-setup.log',
                    'bootnext-swtpm-first.log',
                    'bootnext-swtpm-second.log')) {
                $Source = Join-Path $Paths.Stage $Name
                if (Test-Path -LiteralPath $Source -PathType Leaf) {
                    Copy-Item -LiteralPath $Source `
                        -Destination (Join-Path $Paths.Evidence $Name) -Force
                }
            }
        }
        if ($Execution.TimedOut -or $Execution.ExitCode -ne 0) {
            throw "WSL nested E2E exited $($Execution.ExitCode) (timed out: $($Execution.TimedOut))."
        }
        $ResultPath = Join-Path $Paths.Evidence 'result.json'
        $Result = Get-Content -Raw -LiteralPath $ResultPath | ConvertFrom-Json
        if ($Result.SchemaVersion -ne 1 -or $Result.RunId -cne $RunId -or
            $Result.Status -cne 'PASS') {
            throw 'WSL nested E2E result identity, schema, or status mismatch.'
        }
        Release-SharedLease $Lease
        $Lease = $null
        $Success = $true
        return [pscustomobject]@{
            Result = 'PASS'
            RunId = $RunId
            Workflow = if ($BootNext) {
                'WslBootNextE2E'
            } else {
                'WslNestedWindowsE2E'
            }
            Evidence = $Paths.Evidence
            PreparedDisk = $PreparedDisk.FullName
        }
    }
    finally {
        if ($null -ne $StartupLease) {
            Release-SharedLease $StartupLease
        }
        if (-not $Success -and $ProcessStarted) {
            [void](& $InvokeWsl -WslArguments @('/usr/bin/pkill', '-TERM', '-f',
                $RunId))
            Start-Sleep -Milliseconds 500
            $Remaining = & $InvokeWsl -WslArguments @('/usr/bin/pgrep', '-a',
                '-f', $RunId)
            if ($Remaining.ExitCode -ne 0 -and $null -ne $Lease) {
                Release-SharedLease $Lease
                $Lease = $null
                Write-UhrAtomicJson -Path (
                    Join-Path $Paths.Evidence 'failure-recovery.json') `
                    -Value ([ordered]@{
                        SchemaVersion = 1
                        Profile = 'Wsl'
                        RunId = $RunId
                        Result = 'RECOVERED'
                        Message = 'The failed WSL E2E process set was terminated and verified absent.'
                        CompletedUtc = [DateTime]::UtcNow.ToString('o')
                    })
            }
        }
    }
}

switch ($Action) {
    'Doctor' {
        $Names = if ($Profile -eq 'All') { @('Linux', 'Windows') } else { @($Profile) }
        foreach ($Name in $Names) {
            $Selected = Get-ProfileConfig $Name
            Assert-ConfiguredWindowsMailbox $Selected
            [pscustomobject]@{
                Result = 'PASS'
                Profile = $Name
                Vm = $Selected.VmxPath
                Running = Test-ConfiguredVmRunning $Selected
                Snapshot = $Selected.SnapshotName
                Quarantined = Test-Path -LiteralPath (Get-QuarantinePath $Name)
                GuestTransport = if ($null -ne $Selected.PSObject.Properties['GuestTransport']) {
                    [string]$Selected.GuestTransport
                } else { 'VmrunGuest' }
                GuestCredentialRequired = -not (
                    (Test-SharedFolderMailbox $Selected) -or
                    (Test-VmwareGuestInfo $Selected))
                GuestCredential = (-not (Test-SharedFolderMailbox $Selected)) -and
                    (-not (Test-VmwareGuestInfo $Selected)) -and
                    (Test-UhrCredential -Config $Config `
                        -FileName ([string]$Selected.GuestCredentialFile))
                MailboxCredential = ((Test-SharedFolderMailbox $Selected) -or
                    (Test-VmwareGuestInfo $Selected)) -and
                    (Test-UhrCredential -Config $Config `
                        -FileName ([string]$Selected.MailboxCredentialFile))
                VmCredential = Test-UhrCredential -Config $Config `
                    -FileName ([string]$Selected.VmCredentialFile)
            }
        }
        break
    }
    'Build' { Invoke-HostBuild; break }
    'CredentialStatus' {
        $Names = if ($Profile -eq 'All') { @('Linux', 'Windows') } else { @($Profile) }
        foreach ($Name in $Names) { Get-CredentialSummary $Name }
        break
    }
    'StoreGuestCredential' {
        if ($Profile -eq 'All') { throw 'Choose Linux or Windows.' }
        $Selected = Get-ProfileConfig $Profile
        if ((Test-SharedFolderMailbox $Selected) -or
            (Test-VmwareGuestInfo $Selected)) {
            throw "$Profile uses a passwordless mailbox transport and does not use a guest credential."
        }
        $Credential = Get-Credential -UserName $Selected.GuestUser `
            -Message "Store the $Profile VMware guest credential for UEFILoader."
        Set-UhrCredential -Config $Config `
            -FileName ([string]$Selected.GuestCredentialFile) `
            -Credential $Credential | Out-Null
        Get-CredentialSummary $Profile
        break
    }
    'StoreVmCredential' {
        if ($Profile -eq 'All') { throw 'Choose Linux or Windows.' }
        $Selected = Get-ProfileConfig $Profile
        if ([string]::IsNullOrWhiteSpace([string]$Selected.VmCredentialFile)) {
            throw "The $Profile VM is not configured as encrypted."
        }
        $Password = Read-Host "Encrypted VMware password for $Profile" -AsSecureString
        $Credential = [PSCredential]::new('UEFILoaderVm', $Password)
        Set-UhrCredential -Config $Config `
            -FileName ([string]$Selected.VmCredentialFile) `
            -Credential $Credential | Out-Null
        Get-CredentialSummary $Profile
        break
    }
    'RemoveGuestCredential' {
        if ($Profile -eq 'All') { throw 'Choose Linux or Windows.' }
        if (-not $AuthorizeCredentialRemoval) {
            throw 'Credential removal requires -AuthorizeCredentialRemoval.'
        }
        $Selected = Get-ProfileConfig $Profile
        if ([string]::IsNullOrWhiteSpace(
                [string]$Selected.GuestCredentialFile)) {
            Get-CredentialSummary $Profile
            break
        }
        Remove-Item -LiteralPath (Get-UhrCredentialPath -Config $Config `
            -FileName ([string]$Selected.GuestCredentialFile)) -Force `
            -ErrorAction SilentlyContinue
        Get-CredentialSummary $Profile
        break
    }
    'RemoveVmCredential' {
        if ($Profile -eq 'All') { throw 'Choose Linux or Windows.' }
        if (-not $AuthorizeCredentialRemoval) {
            throw 'Credential removal requires -AuthorizeCredentialRemoval.'
        }
        $Selected = Get-ProfileConfig $Profile
        if (-not [string]::IsNullOrWhiteSpace([string]$Selected.VmCredentialFile)) {
            Remove-Item -LiteralPath (Get-UhrCredentialPath -Config $Config `
                -FileName ([string]$Selected.VmCredentialFile)) -Force `
                -ErrorAction SilentlyContinue
        }
        Get-CredentialSummary $Profile
        break
    }
    'RunInstallerHostGate' {
        if (-not $SkipBuild) { Invoke-HostBuild | Out-Host }
        & (Join-Path $PSScriptRoot 'tests\Invoke-InstallerHostGate.ps1') `
            -ProjectRoot $Config.ProjectRoot -StateRoot $Config.StateRoot
        break
    }
    'RunInstallerOneShotHostGate' {
        if (-not $SkipBuild) { Invoke-HostBuild | Out-Host }
        & (Join-Path $PSScriptRoot `
            'tests\Invoke-InstallerOneShotHostGate.ps1') `
            -ProjectRoot $Config.ProjectRoot -StateRoot $Config.StateRoot
        break
    }
    'RunAuditHostGate' {
        if (-not $SkipBuild) { Invoke-HostBuild | Out-Host }
        & (Join-Path $PSScriptRoot 'tests\Invoke-AuditHostGate.ps1') `
            -ProjectRoot $Config.ProjectRoot -StateRoot $Config.StateRoot
        break
    }
    'RunFixtureHostGate' {
        if (-not $SkipBuild) { Invoke-HostBuild | Out-Host }
        & (Join-Path $PSScriptRoot 'tests\Invoke-FixtureHostGate.ps1') `
            -ProjectRoot $Config.ProjectRoot -StateRoot $Config.StateRoot
        break
    }
    'RunLinuxPositivePath' { Invoke-LiveWorkflow LinuxPositive; break }
    'RunLinuxReplayDryRun' { Invoke-LiveWorkflow LinuxReplayDryRun; break }
    'RunLinuxPeRehash' { Invoke-LiveWorkflow LinuxPeRehash; break }
    'RunLinuxDivergenceManifest' {
        Invoke-LiveWorkflow LinuxDivergenceManifest
        break
    }
    'RunLinuxHandoffMechanics' {
        Invoke-LiveWorkflow LinuxHandoffMechanics
        break
    }
    'RunLinuxProductionHandoff' {
        Invoke-LiveWorkflow LinuxProductionHandoff
        break
    }
    'RunLinuxOneShotCycle' {
        Invoke-LiveWorkflow LinuxOneShotCycle
        break
    }
    'RunLinuxNestedDiskProbe' {
        Invoke-LiveWorkflow LinuxNestedDiskProbe
        break
    }
    'RunLinuxNestedWindowsE2E' {
        Invoke-LiveWorkflow LinuxNestedWindowsE2E
        break
    }
    'RunWslNestedWindowsE2E' {
        Invoke-WslNestedWindowsE2E
        break
    }
    'RunWslBootNextE2E' {
        Invoke-WslNestedWindowsE2E -BootNext
        break
    }
    'RunWindowsProbe' { Invoke-LiveWorkflow WindowsProbe; break }
    'RunWindowsCapture' { Invoke-LiveWorkflow WindowsCapture; break }
    'RunWindowsNestedPrep' { Invoke-LiveWorkflow WindowsNestedPrep; break }
    'CreateWindowsSnapshot' {
        if ([string]::IsNullOrWhiteSpace($SnapshotName)) {
            throw 'CreateWindowsSnapshot requires -SnapshotName.'
        }
        New-ConfiguredWindowsSnapshot -Name $SnapshotName
        break
    }
    'ClearQuarantine' {
        if ($Profile -eq 'All') { throw 'Choose Linux or Windows.' }
        if (-not $AuthorizeQuarantineClear) {
            throw 'Clearing quarantine requires -AuthorizeQuarantineClear.'
        }
        $Selected = Get-ProfileConfig $Profile
        if (Test-ConfiguredVmRunning $Selected) {
            throw 'Stop or revert the configured VM before clearing quarantine.'
        }
        $QuarantinePath = Get-QuarantinePath $Profile
        if (Test-Path -LiteralPath $QuarantinePath -PathType Leaf) {
            $Quarantine = Get-Content -Raw -LiteralPath $QuarantinePath |
                ConvertFrom-Json
            if ($Quarantine.LeaseResource -cne
                [string]$Selected.LeaseResource) {
                throw 'Quarantine lease resource does not match the configured profile.'
            }
            $LeaseStatus = & $Config.SharedLease.ScriptPath -Action Status `
                -Resource ([string]$Selected.LeaseResource)
            if ($LeaseStatus.Status -ceq 'ACTIVE') {
                throw "The retained lease remains active until $($LeaseStatus.ExpiresUtc); do not clear quarantine yet."
            }
            if ($LeaseStatus.Status -ceq 'EXPIRED') {
                if ($LeaseStatus.LeaseId -cne $Quarantine.LeaseId) {
                    throw 'Expired lease identity does not match quarantine; fail closed.'
                }
                $RecoveryRunId = if ($null -ne
                    $Quarantine.PSObject.Properties['RecoveryRunId']) {
                    [string]$Quarantine.RecoveryRunId
                }
                else {
                    [string]$Quarantine.RunId
                }
                $RecoveryLease = & $Config.SharedLease.ScriptPath `
                    -Action Acquire `
                    -Resource ([string]$Selected.LeaseResource) `
                    -Agent 'Codex-UEFILoaderHostRunner-Recovery' `
                    -Project ([string]$Config.ProjectRoot) `
                    -RunId $RecoveryRunId `
                    -LeaseMinutes 5 `
                    -TakeOverExpiredLease
                Release-SharedLease $RecoveryLease
            }
            elseif ($LeaseStatus.Status -cne 'FREE') {
                throw "Unexpected retained lease status: $($LeaseStatus.Status)"
            }
            Remove-Item -LiteralPath $QuarantinePath -Force
        }
        [pscustomobject]@{ Result = 'PASS'; Profile = $Profile; Quarantined = $false }
        break
    }
}
