[CmdletBinding()]
param(
    [ValidateSet(
        'Doctor', 'Build', 'CredentialStatus', 'StoreGuestCredential',
        'StoreVmCredential', 'StoreMailboxCredential',
        'RemoveGuestCredential', 'RemoveVmCredential',
        'RunInstallerHostGate', 'RunInstallerOneShotHostGate',
        'RunAuditHostGate', 'RunFixtureHostGate',
        'RunLinuxPositivePath', 'RunLinuxHypervisorSmoke',
        'RunLinuxHypervisorPostEbsSmoke',
        'RunLinuxHypervisorPostEbsInterruptSmoke', 'RunLinuxReplayDryRun', 'RunLinuxPeRehash',
        'RunLinuxDivergenceManifest', 'RunLinuxHandoffMechanics',
        'RunLinuxProductionHandoff', 'RunLinuxOneShotCycle',
        'RunLinuxNestedDiskProbe', 'RunLinuxNestedWindowsE2E',
        'RunLinuxNestedWindowsHypervisorE2E',
        'RunWslNestedWindowsE2E', 'RunWslNestedWindowsHypervisorE2E',
        'RunWslBootNextE2E',
        'RunWindowsProbe', 'RunWindowsCapture', 'RunWindowsNestedPrep',
        'RunWindowsExactBuildUpdate',
        'ResumeWindowsExactBuildUpdate',
        'RunWindowsDirectHypervisorE2E', 'RunWindowsDirectScannerE2E',
        'CreateWindowsSnapshot',
        'ClearQuarantine')]
    [string]$Action = 'Doctor',

    [ValidateSet('Linux', 'Windows', 'All')]
    [string]$Profile = 'All',

    [string]$ConfigPath,
    [ValidatePattern('^[A-Za-z0-9._-]{1,64}$')]
    [string]$WslDistribution = 'Ubuntu',
    [ValidateSet(1, 4, 20)]
    [int]$WindowsDirectProcessorCount = 20,
    [ValidateRange(2048, 32768)]
    [int]$WindowsDirectMemoryMib = 8192,
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

$UefiProjectRoot = [IO.Path]::GetFullPath([string]$Config.UefiProjectRoot)
$HypervisorProjectRoot = if ($null -ne $Config.PSObject.Properties['HypervisorProjectRoot']) {
    [IO.Path]::GetFullPath([string]$Config.HypervisorProjectRoot)
} else {
    [IO.Path]::GetFullPath((Join-Path $Config.ProjectRoot 'components\hypervisor'))
}
if (-not (Test-UhrPathWithin -Root $Config.ProjectRoot -Path $UefiProjectRoot) -or
    -not (Test-Path -LiteralPath $UefiProjectRoot -PathType Container)) {
    throw 'UefiProjectRoot must be an existing directory contained under ProjectRoot.'
}
if (-not (Test-UhrPathWithin -Root $Config.ProjectRoot -Path $HypervisorProjectRoot) -or
    -not (Test-Path -LiteralPath $HypervisorProjectRoot -PathType Container)) {
    throw 'HypervisorProjectRoot must be an existing directory contained under ProjectRoot.'
}

function Get-UhrUefiPath {
    param([Parameter(Mandatory=$true)][string]$RelativePath)
    return Join-Path $UefiProjectRoot $RelativePath
}

function Get-UhrHypervisorPath {
    param([Parameter(Mandatory=$true)][string]$RelativePath)
    return Join-Path $HypervisorProjectRoot $RelativePath
}

function Get-UhrSignToolPath {
    $ProgramFilesX86 = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::ProgramFilesX86)
    $WindowsKitsBin = Join-Path $ProgramFilesX86 'Windows Kits\10\bin'
    if (-not (Test-Path -LiteralPath $WindowsKitsBin -PathType Container)) {
        throw 'The Windows SDK binary directory required for page-hash signing is missing.'
    }

    $VersionRoots = @(Get-ChildItem -LiteralPath $WindowsKitsBin -Directory |
        Where-Object { $_.Name -match '^10\.0\.[0-9]+\.0$' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64' })
    $PreferredRoot = Join-Path $WindowsKitsBin '10.0.26100.0\x64'
    if (Test-Path -LiteralPath $PreferredRoot -PathType Container) {
        $VersionRoots = @($PreferredRoot) + @($VersionRoots |
            Where-Object { $_ -cne $PreferredRoot })
    }
    $Candidates = @($VersionRoots | ForEach-Object {
        Join-Path $_ 'signtool.exe'
    } | Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    } | Select-Object -First 1)
    if ($Candidates.Count -ne 1) {
        throw 'No x64 Windows SDK SignTool executable is installed.'
    }
    $Path = [string]$Candidates[0]
    $Signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($Signature.Status -ne
            [System.Management.Automation.SignatureStatus]::Valid -or
        $null -eq $Signature.SignerCertificate -or
        $Signature.SignerCertificate.Subject -notmatch
            '(^|,\s*)O=Microsoft Corporation(,|$)') {
        throw "The Windows SDK SignTool executable is not Microsoft-signed: $Path"
    }
    return $Path
}

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
        $RunId -cnotmatch '^uefi-windows-(probe|capture|nested-prep|exact-build-update)-') {
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
                'stdout.txt', 'stderr.txt', 'tcglog.ib', 'tcglog-full.bin',
                'kernel-metadata.json')
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
$script:VmwareGuestInfoDirectStage2Prefix = 'uefiloader.direct.stage2.'
$script:VmwareGuestInfoExactStatus = 'uefiloader.exact.status'
$script:VmwareGuestInfoExactStatusGuestName = 'guestinfo.uefiloader.exact.status'
$script:VmwareGuestInfoChunkBytes = 9000
$script:VmwareGuestInfoChunkChars = 12000
$script:VmwareGuestInfoMaxPackageBytes = 4MB
$script:VmwareGuestInfoMaxRequestChunks = 256
$script:VmwareGuestInfoMaxEvidenceBytes = 32MB
$script:VmwareGuestInfoMaxManifestChars = 65536
$script:VmwareGuestInfoTombstone = 'MA=='

function Test-UhrGuestInfoRunId {
    param([Parameter(Mandatory=$true)][string]$RunId)
    return (Test-UhrRunId $RunId)
}

function Get-VmwareGuestInfoBase {
    param([Parameter(Mandatory=$true)][string]$RunId)
    if (-not (Test-UhrGuestInfoRunId $RunId) -or
        $RunId -cnotmatch
            '^uefi-windows-(probe|capture|nested-prep|exact-build-update)-') {
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
    if (-not (Test-UhrGuestInfoRunId $RunId) -or $Index -lt 0 -or $Index -gt 4095) {
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
            'evidence.zip', 'stdout.txt', 'stderr.txt', 'tcglog.ib',
            'tcglog-full.bin', 'kernel-metadata.json')) {
        throw "Windows Guest Variables evidence is not allowlisted: $EvidenceName"
    }
    if (-not (Test-UhrGuestInfoRunId $RunId) -or $Index -lt 0 -or $Index -gt 4095) {
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
        $VariableName -ne $script:VmwareGuestInfoExactStatus -and
        $VariableName -notmatch '^uefiloader\.direct\.stage2\.uefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$' -and
        $VariableName -notmatch '^uefiloader\.(request|response)\.manifest$' -and
        $VariableName -notmatch '^uefiloader\.(request|response)\.chunk\.uefi-windows-(probe|capture|nested-prep|exact-build-update)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.[0-9]{6}$') {
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
        $VariableName -ne $script:VmwareGuestInfoExactStatus -and
        $VariableName -notmatch '^uefiloader\.direct\.stage2\.uefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$' -and
        $VariableName -notmatch '^uefiloader\.(request|response)\.manifest$' -and
        $VariableName -notmatch '^uefiloader\.(request|response)\.chunk\.uefi-windows-(probe|capture|nested-prep|exact-build-update)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.[0-9]{6}$') {
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

function Get-CanonicalWindowsDirectHypervisorResult {
    param($Result)
    return @(
        "SchemaVersion=$($Result.SchemaVersion)"
        "RunId=$($Result.RunId)"
        "Status=$($Result.Status)"
        "MessageBase64=$($Result.MessageBase64)"
        "CompletedUtc=$($Result.CompletedUtc)"
        "StdoutBase64=$($Result.StdoutBase64)"
        "StderrBase64=$($Result.StderrBase64)"
    ) -join "`n"
}

function Get-CanonicalWindowsDirectStageTwo {
    param($Marker)
    return @(
        "SchemaVersion=$($Marker.SchemaVersion)"
        "RunId=$($Marker.RunId)"
        "State=$($Marker.State)"
        "CompletedUtc=$($Marker.CompletedUtc)"
    ) -join "`n"
}

function Wait-WindowsDirectStageTwoReady {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$RunId,
        [int]$TimeoutSeconds = 180
    )
    if ($RunId -cnotmatch
            '^uefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw 'The direct stage-two RunId is invalid.'
    }
    $VariableName = $script:VmwareGuestInfoDirectStage2Prefix + $RunId
    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $Deadline) {
        $Encoded = Invoke-VmwareGuestInfoRead -Selected $Selected `
            -VariableName $VariableName -TimeoutSeconds 2
        if ($null -eq $Encoded -or
            $Encoded -ceq $script:VmwareGuestInfoTombstone) {
            Start-Sleep -Milliseconds 500
            continue
        }
        $Bytes = $null
        try {
            $Bytes = [Convert]::FromBase64String($Encoded)
            if ([Convert]::ToBase64String($Bytes) -cne $Encoded -or
                $Bytes.Length -gt 4096) {
                throw 'invalid bounded base64'
            }
            $Marker = ([Text.Encoding]::UTF8.GetString($Bytes) |
                ConvertFrom-Json)
        }
        catch { throw 'The direct stage-two marker is invalid base64 JSON.' }
        finally {
            if ($null -ne $Bytes) {
                [Array]::Clear($Bytes, 0, $Bytes.Length)
            }
        }
        $ExpectedProperties = @('CompletedUtc', 'HmacSha256', 'RunId',
            'SchemaVersion', 'State') | Sort-Object
        $ActualProperties = @($Marker.PSObject.Properties.Name) | Sort-Object
        if (($ExpectedProperties -join "`n") -cne
            ($ActualProperties -join "`n") -or
            [int]$Marker.SchemaVersion -ne 1 -or
            [string]$Marker.RunId -cne $RunId -or
            [string]$Marker.State -cne 'BOOTNEXT_READY' -or
            [string]$Marker.HmacSha256 -cnotmatch '^[0-9A-F]{64}$') {
            throw 'The direct stage-two marker identity or schema is invalid.'
        }
        try {
            $CompletedUtc = [DateTime]::Parse(
                [string]$Marker.CompletedUtc,
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
        }
        catch { throw 'The direct stage-two timestamp is invalid.' }
        if ([Math]::Abs(([DateTime]::UtcNow - $CompletedUtc).TotalMinutes) `
                -gt 5) {
            throw 'The direct stage-two marker is stale or from the future.'
        }
        $Key = Get-MailboxKeyBytes $Selected
        try {
            $ExpectedHmac = Get-HmacSha256Hex -Key $Key `
                -Text (Get-CanonicalWindowsDirectStageTwo $Marker)
        }
        finally { [Array]::Clear($Key, 0, $Key.Length) }
        if (-not (Test-FixedHexEqual $ExpectedHmac `
                ([string]$Marker.HmacSha256))) {
            throw 'The direct stage-two marker authentication failed.'
        }
        return [pscustomobject]@{
            VariableName = $VariableName
            CompletedUtc = $CompletedUtc
        }
    }
    throw "Windows did not authenticate the direct stage-two BootNext state within $TimeoutSeconds seconds."
}

function Wait-WindowsDirectStageTwoOrResult {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)]$Capture,
        $DebugCapture,
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)][DateTime]$MinimumCompletedUtc,
        [int]$TimeoutSeconds = 180
    )
    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $Deadline) {
        try {
            $DirectResult = Wait-WindowsDirectHypervisorResult `
                -Selected $Selected -Capture $Capture -RunId $RunId `
                -DebugCapture $DebugCapture `
                -MinimumCompletedUtc $MinimumCompletedUtc -TimeoutSeconds 1
            return [pscustomobject]@{
                Kind = 'DIRECT_RESULT'
                DirectResult = $DirectResult
                StageTwo = $null
            }
        }
        catch {
            $ExpectedTimeout =
                "Windows direct startup probe did not publish within 1 seconds: $RunId"
            if ($_.Exception.Message -cne $ExpectedTimeout) { throw }
        }
        try {
            $StageTwo = Wait-WindowsDirectStageTwoReady -Selected $Selected `
                -RunId $RunId -TimeoutSeconds 1
            return [pscustomobject]@{
                Kind = 'STAGE_TWO'
                DirectResult = $null
                StageTwo = $StageTwo
            }
        }
        catch {
            $ExpectedTimeout =
                "Windows did not authenticate the direct stage-two BootNext state within 1 seconds."
            if ($_.Exception.Message -cne $ExpectedTimeout) { throw }
        }
    }
    throw "Windows published neither an authenticated direct result nor stage-two state within $TimeoutSeconds seconds."
}

function ConvertFrom-WindowsDirectResultBase64 {
    param(
        [Parameter(Mandatory=$true)][AllowEmptyString()][string]$Value,
        [Parameter(Mandatory=$true)][string]$Description
    )
    $Bytes = $null
    try {
        $Bytes = [Convert]::FromBase64String($Value)
        if ([Convert]::ToBase64String($Bytes) -cne $Value -or
            $Bytes.Length -gt 32768) {
            throw 'non-canonical or oversized data'
        }
        return [Text.Encoding]::UTF8.GetString($Bytes)
    }
    catch { throw "Windows direct result $Description is invalid base64." }
    finally {
        if ($null -ne $Bytes) { [Array]::Clear($Bytes, 0, $Bytes.Length) }
    }
}

function Get-WindowsDirectBuildMetadata {
    param(
        [Parameter(Mandatory=$true)][AllowEmptyString()][string]$Stdout
    )
    $BuildMatches = [regex]::Matches($Stdout,
        '(?m)^WINDOWS_BUILD=([0-9]+)\r?$')
    $UbrMatches = [regex]::Matches($Stdout,
        '(?m)^WINDOWS_UBR=([0-9]+)\r?$')
    if ($BuildMatches.Count -ne 1 -or $UbrMatches.Count -ne 1) {
        throw 'Windows direct result did not report exactly one build and UBR.'
    }
    $Build = [uint64]0
    $Ubr = [uint64]0
    if (-not [uint64]::TryParse(
            $BuildMatches[0].Groups[1].Value,
            [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$Build) -or
        -not [uint64]::TryParse(
            $UbrMatches[0].Groups[1].Value,
            [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$Ubr) -or
        $Build -gt [uint64]4294967295 -or
        $Ubr -gt [uint64]4294967295) {
        throw 'Windows direct result build/UBR is not an unsigned 32-bit value.'
    }
    return [pscustomobject]@{
        WindowsBuild = $Build
        WindowsUbr = $Ubr
    }
}

function Start-WindowsDirectResultCapture {
    param(
        [Parameter(Mandatory=$true)][string]$EvidenceRoot,
        [ValidateSet('uefiloader_direct_result', 'uefiloader_clone_debug')]
        [string]$PipeName = 'uefiloader_direct_result',
        [ValidateSet('direct-result-serial.log', 'direct-debug-serial.log')]
        [string]$OutputName = 'direct-result-serial.log'
    )
    $Evidence = [IO.Path]::GetFullPath($EvidenceRoot)
    if (-not (Test-UhrPathWithin -Root $Config.StateRoot -Path $Evidence) -or
        -not (Test-Path -LiteralPath $Evidence -PathType Container)) {
        throw 'Direct-result evidence root escaped the runner state root.'
    }
    $OutputPath = Join-Path $Evidence $OutputName
    if (Test-Path -LiteralPath $OutputPath) {
        throw "Direct-result capture path already exists: $OutputPath"
    }
    $Pipe = [IO.Pipes.NamedPipeClientStream]::new(
        '.', $PipeName, [IO.Pipes.PipeDirection]::In,
        [IO.Pipes.PipeOptions]::Asynchronous)
    try { $Pipe.Connect(15000) }
    catch {
        $Pipe.Dispose()
        throw 'Direct-result capture did not connect within 15 seconds.'
    }
    $Buffer = [byte[]]::new(4096)
    $Bytes = [IO.MemoryStream]::new()
    [IO.File]::WriteAllBytes($OutputPath, [byte[]]@())
    return [pscustomobject]@{
        Pipe = $Pipe
        Buffer = $Buffer
        Bytes = $Bytes
        ReadTask = $null
        OutputPath = $OutputPath
    }
}

function Update-WindowsDirectCapture {
    param(
        [Parameter(Mandatory=$true)]$Capture,
        [ValidateRange(0, 1000)][int]$WaitMilliseconds = 0
    )
    if ($null -eq $Capture.ReadTask) {
        $Capture.ReadTask = $Capture.Pipe.ReadAsync(
            $Capture.Buffer, 0, $Capture.Buffer.Length)
    }
    if ($Capture.ReadTask.Wait($WaitMilliseconds)) {
        $Count = [int]$Capture.ReadTask.Result
        $Capture.ReadTask = $null
        if ($Count -gt 0) {
            $Capture.Bytes.Write($Capture.Buffer, 0, $Count)
            if ($Capture.Bytes.Length -gt 1MB) {
                throw 'Windows direct serial capture exceeds 1 MiB.'
            }
            [IO.File]::WriteAllBytes($Capture.OutputPath,
                $Capture.Bytes.ToArray())
        }
    }
}

function Stop-WindowsDirectResultCapture {
    param($Capture)
    if ($null -eq $Capture) { return }
    try {
        if ($null -ne $Capture.Pipe) { $Capture.Pipe.Dispose() }
    }
    catch {}
    try {
        if ($null -ne $Capture.Bytes) { $Capture.Bytes.Dispose() }
    }
    catch {}
    if ($null -ne $Capture.Buffer) {
        [Array]::Clear($Capture.Buffer, 0, $Capture.Buffer.Length)
    }
}

function Wait-WindowsDirectHypervisorResult {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)]$Capture,
        $DebugCapture,
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)][DateTime]$MinimumCompletedUtc,
        [ValidateRange(1, 600)][int]$TimeoutSeconds = 180
    )
    if (-not (Test-UhrRunId $RunId) -or
        $RunId -cnotmatch '^uefi-windows-probe-') {
        throw 'Unsafe Windows direct verification RunId.'
    }
    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $Deadline) {
        Update-WindowsDirectCapture -Capture $Capture -WaitMilliseconds 250
        if ($null -ne $DebugCapture) {
            Update-WindowsDirectCapture -Capture $DebugCapture
        }
        $SerialText = [Text.Encoding]::ASCII.GetString(
            $Capture.Bytes.GetBuffer(), 0, [int]$Capture.Bytes.Length)
        $Frames = [regex]::Matches($SerialText,
            'UEFI2HSINV-DIRECT:([A-Za-z0-9+/]+={0,2})' +
            '(?=UEFI2HSINV-DIRECT:|\r|\n|$)')
        $Result = $null
        foreach ($Frame in $Frames) {
            $Encoded = $Frame.Groups[1].Value
            if ($Encoded.Length -gt 65536) { continue }
            $EnvelopeBytes = $null
            try {
                $EnvelopeBytes = [Convert]::FromBase64String($Encoded)
                if ([Convert]::ToBase64String($EnvelopeBytes) -cne $Encoded) {
                    continue
                }
                $Candidate = [Text.Encoding]::UTF8.GetString($EnvelopeBytes) |
                    ConvertFrom-Json
                if ([string]$Candidate.RunId -ceq $RunId) {
                    $Result = $Candidate
                    break
                }
            }
            catch {}
            finally {
                if ($null -ne $EnvelopeBytes) {
                    [Array]::Clear($EnvelopeBytes, 0, $EnvelopeBytes.Length)
                }
            }
        }
        if ($null -eq $Result) {
            Start-Sleep -Milliseconds 250
            continue
        }
        $ExpectedProperties = @(
            'CompletedUtc', 'MessageBase64', 'ResponseHmacSha256', 'RunId',
            'SchemaVersion', 'Status', 'StderrBase64', 'StdoutBase64') |
            Sort-Object
        $ActualProperties = @($Result.PSObject.Properties.Name) | Sort-Object
        if (($ExpectedProperties -join "`n") -cne
                ($ActualProperties -join "`n") -or
            [int]$Result.SchemaVersion -ne 1 -or
            [string]$Result.RunId -cne $RunId -or
            [string]$Result.Status -notin @('PASS', 'FAIL') -or
            [string]$Result.ResponseHmacSha256 -cnotmatch '^[0-9A-F]{64}$') {
            throw 'Windows direct result schema is invalid.'
        }
        try {
            $Completed = [DateTime]::Parse(
                [string]$Result.CompletedUtc,
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
        }
        catch { throw 'Windows direct result completion time is invalid.' }
        if ($Completed -lt $MinimumCompletedUtc.ToUniversalTime() -or
            $Completed -gt [DateTime]::UtcNow.AddMinutes(1)) {
            throw 'Windows direct result is stale or from the future.'
        }
        $Key = Get-MailboxKeyBytes $Selected
        try {
            $ExpectedHmac = Get-HmacSha256Hex -Key $Key `
                -Text (Get-CanonicalWindowsDirectHypervisorResult $Result)
        }
        finally { [Array]::Clear($Key, 0, $Key.Length) }
        if (-not (Test-FixedHexEqual $ExpectedHmac `
                ([string]$Result.ResponseHmacSha256))) {
            throw 'Windows direct result authentication failed.'
        }
        return [pscustomobject]@{
            SchemaVersion = 1
            RunId = $RunId
            Status = [string]$Result.Status
            Message = ConvertFrom-WindowsDirectResultBase64 `
                -Value ([string]$Result.MessageBase64) -Description 'message'
            CompletedUtc = [string]$Result.CompletedUtc
            Stdout = ConvertFrom-WindowsDirectResultBase64 `
                -Value ([string]$Result.StdoutBase64) -Description 'stdout'
            Stderr = ConvertFrom-WindowsDirectResultBase64 `
                -Value ([string]$Result.StderrBase64) -Description 'stderr'
            ResponseHmacSha256 = [string]$Result.ResponseHmacSha256
        }
    }
    throw "Windows direct startup probe did not publish within $TimeoutSeconds seconds: $RunId"
}

function Wait-VmwareGuestInfoWorkerReady {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [DateTime]$MinimumStartedUtc = [DateTime]::MinValue,
        [ValidateRange(0, 3600)][int]$ReadyTimeoutSeconds = 0
    )

    Invoke-VmwareGuestInfoWrite -Selected $Selected `
        -VariableName $script:VmwareGuestInfoWorkerStatus `
        -Value $script:VmwareGuestInfoTombstone | Out-Null
    if ($ReadyTimeoutSeconds -eq 0) {
        $ReadyTimeoutSeconds = [int]$Config.Policy.GuestReadyTimeoutSeconds
    }
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
        if ($MinimumStartedUtc -ne [DateTime]::MinValue -and
            $Started -lt $MinimumStartedUtc.ToUniversalTime()) {
            Invoke-VmwareGuestInfoWrite -Selected $Selected `
                -VariableName $script:VmwareGuestInfoWorkerStatus `
                -Value $script:VmwareGuestInfoTombstone | Out-Null
            Start-Sleep -Milliseconds 500
            continue
        }
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
            throw 'Windows Guest Variables package must be greater than zero and at most 4 MiB.'
        }
        $Chunks = ConvertTo-VmwareGuestInfoChunks -Bytes $PackageBytes
        if ($Chunks.Count -lt 1 -or
            $Chunks.Count -gt $script:VmwareGuestInfoMaxRequestChunks) {
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
        if ($VariableNames.Count -gt 0) {
            Clear-VmwareGuestInfoVariables -Selected $Selected `
                -VariableNames @($VariableNames)
        }
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
        [Parameter(Mandatory=$true)][string]$RunId,
        [switch]$ExtendedPickup
    )
    $Deadline = [DateTime]::UtcNow.AddSeconds(
        [Math]::Min([int]$Config.Policy.OperationTimeoutSeconds, 600) + 60)
    $PickupSeconds = if ($ExtendedPickup -or
        $RunId -match '-nested-prep-') { 300 } else { 60 }
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
                        'bootmgfw.sha256', 'kernel-metadata.json')
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

function Get-CanonicalWindowsExactStatus {
    param($Status)
    return @(
        "SchemaVersion=$($Status.SchemaVersion)"
        "RunId=$($Status.RunId)"
        "State=$($Status.State)"
        "Package=$($Status.Package)"
        "WindowsBuild=$($Status.WindowsBuild)"
        "WindowsUbr=$($Status.WindowsUbr)"
        "RebootRequired=$($Status.RebootRequired)"
        "MessageBase64=$($Status.MessageBase64)"
        "UpdatedUtc=$($Status.UpdatedUtc)"
    ) -join "`n"
}

function Publish-WindowsExactCancellation {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$RunId
    )
    if ($RunId -cnotmatch '^uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw 'Unsafe exact Windows cancellation RunId.'
    }
    $MessageBytes = [Text.Encoding]::UTF8.GetBytes(
        'Host requested bounded cancellation before snapshot recovery.')
    try {
        $Cancel = [ordered]@{
            SchemaVersion = 1
            RunId = $RunId
            State = 'CANCELLED'
            Package = 'KB5124008'
            WindowsBuild = 0
            WindowsUbr = 0
            RebootRequired = $false
            MessageBase64 = [Convert]::ToBase64String($MessageBytes)
            UpdatedUtc = [DateTime]::UtcNow.ToString('o')
        }
    }
    finally { [Array]::Clear($MessageBytes, 0, $MessageBytes.Length) }
    $Key = Get-MailboxKeyBytes $Selected
    try {
        $Cancel.StatusHmacSha256 = Get-HmacSha256Hex -Key $Key `
            -Text (Get-CanonicalWindowsExactStatus $Cancel)
    }
    finally { [Array]::Clear($Key, 0, $Key.Length) }
    $Bytes = [Text.Encoding]::UTF8.GetBytes(
        ($Cancel | ConvertTo-Json -Compress -Depth 4))
    try { $Encoded = [Convert]::ToBase64String($Bytes) }
    finally { [Array]::Clear($Bytes, 0, $Bytes.Length) }
    Invoke-VmwareGuestInfoWrite -Selected $Selected `
        -VariableName $script:VmwareGuestInfoExactStatus -Value $Encoded | Out-Null
}

function ConvertFrom-WindowsExactStatus {
    param(
        [Parameter(Mandatory=$true)]$Raw,
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)]$Selected
    )
    $Expected = @(
        'MessageBase64', 'Package', 'RebootRequired', 'RunId',
        'SchemaVersion', 'State', 'StatusHmacSha256', 'UpdatedUtc',
        'WindowsBuild', 'WindowsUbr') | Sort-Object
    $Actual = @($Raw.PSObject.Properties.Name) | Sort-Object
    if (($Expected -join "`n") -cne ($Actual -join "`n") -or
        [int]$Raw.SchemaVersion -ne 1 -or [string]$Raw.RunId -cne $RunId -or
        [string]$Raw.Package -cne 'KB5124008' -or
        [string]$Raw.State -notin @(
            'STARTING', 'QUEUED', 'REPAIRING', 'SEARCHING', 'DOWNLOADING', 'INSTALLING',
            'REBOOTING', 'VERIFYING', 'COMPLETE', 'FAILED') -or
        [string]$Raw.StatusHmacSha256 -cnotmatch '^[0-9A-F]{64}$' -or
        $Raw.RebootRequired -isnot [bool]) {
        throw 'Exact Windows build status identity or schema is invalid.'
    }
    [uint64]$Build = 0
    [uint64]$Ubr = 0
    if (-not [uint64]::TryParse([string]$Raw.WindowsBuild,
            [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$Build) -or
        -not [uint64]::TryParse([string]$Raw.WindowsUbr,
            [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$Ubr) -or
        $Build -gt [uint64]4294967295 -or $Ubr -gt [uint64]4294967295) {
        throw 'Exact Windows build status numeric metadata is invalid.'
    }
    try {
        $Bytes = [Convert]::FromBase64String([string]$Raw.MessageBase64)
        if ([Convert]::ToBase64String($Bytes) -cne [string]$Raw.MessageBase64 -or
            $Bytes.Length -gt 4096) { throw 'non-canonical message' }
        $Message = [Text.Encoding]::UTF8.GetString($Bytes)
    }
    catch { throw 'Exact Windows build status message is invalid base64.' }
    finally { if ($null -ne $Bytes) { [Array]::Clear($Bytes, 0, $Bytes.Length) } }
    try {
        $Updated = [DateTime]::Parse([string]$Raw.UpdatedUtc,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    }
    catch { throw 'Exact Windows build status timestamp is invalid.' }
    if ($Updated -gt [DateTime]::UtcNow.AddMinutes(1)) {
        throw 'Exact Windows build status is from the future.'
    }
    $Key = Get-MailboxKeyBytes $Selected
    try {
        $ExpectedHmac = Get-HmacSha256Hex -Key $Key `
            -Text (Get-CanonicalWindowsExactStatus $Raw)
    }
    finally { [Array]::Clear($Key, 0, $Key.Length) }
    if (-not (Test-FixedHexEqual $ExpectedHmac `
            ([string]$Raw.StatusHmacSha256))) {
        throw 'Exact Windows build status authentication failed.'
    }
    return [pscustomobject]@{
        SchemaVersion = 1
        RunId = $RunId
        State = [string]$Raw.State
        Package = [string]$Raw.Package
        WindowsBuild = $Build
        WindowsUbr = $Ubr
        RebootRequired = [bool]$Raw.RebootRequired
        Message = $Message
        UpdatedUtc = $Updated.ToString('o')
        StatusHmacSha256 = [string]$Raw.StatusHmacSha256
    }
}

function Wait-WindowsExactBuildStatus {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$RunId,
        [switch]$InitialOnly,
        $SeedStatus,
        [string]$EvidenceRoot
    )
    if ($RunId -cnotmatch '^uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw 'Unsafe exact Windows build status RunId.'
    }
    $LiveStatusPath = $null
    if (-not [string]::IsNullOrWhiteSpace($EvidenceRoot)) {
        $ResolvedEvidence = [IO.Path]::GetFullPath($EvidenceRoot)
        if (-not (Test-UhrPathWithin -Root $Config.StateRoot `
                -Path $ResolvedEvidence) -or
            -not (Test-Path -LiteralPath $ResolvedEvidence -PathType Container)) {
            throw 'Exact Windows build evidence root escaped the runner state root.'
        }
        $LiveStatusPath = Join-Path $ResolvedEvidence 'exact-build-status-live.json'
    }
    $Deadline = [DateTime]::UtcNow.AddSeconds(
        [Math]::Max([int]$Config.Policy.OperationTimeoutSeconds, 3600) + 300)
    $History = [Collections.Generic.List[object]]::new()
    $LastRank = -1
    $FirstStatusDeadline = [DateTime]::UtcNow.AddMinutes(7)
    $Ranks = @{
        QUEUED = 0; STARTING = 1; REPAIRING = 2; SEARCHING = 3;
        DOWNLOADING = 4; INSTALLING = 5; REBOOTING = 6;
        VERIFYING = 7; COMPLETE = 8; FAILED = 9
    }
    $LastState = $null
    $LastObservedUtc = [DateTime]::UtcNow
    $RestartAttempts = 0
    $RebootTransitions = 0
    if ($null -ne $SeedStatus) {
        if ([string]$SeedStatus.RunId -cne $RunId -or
            [string]$SeedStatus.State -notin @($Ranks.Keys)) {
            throw 'Exact Windows build seed status is invalid.'
        }
        $History.Add($SeedStatus)
        $LastState = [string]$SeedStatus.State
        $LastRank = [int]$Ranks[$LastState]
    }
    while ([DateTime]::UtcNow -lt $Deadline) {
        Assert-HostDiskCapacity $Selected
        if (-not (Test-ConfiguredVmRunning $Selected)) {
            if ($null -eq $LastState) {
                if ([DateTime]::UtcNow -ge $FirstStatusDeadline) {
                    throw 'The exact Windows build VM powered off before publishing an authenticated status.'
                }
                Start-Sleep -Seconds 2
                continue
            }
            if ($LastState -notin @('INSTALLING', 'REBOOTING') -or
                $RestartAttempts -ge 1) {
                throw "The exact Windows build VM powered off unexpectedly after status $LastState."
            }
            Resume-WindowsExactBuildUpdateVm -Selected $Selected
            $RestartAttempts++
            continue
        }
        $Encoded = Invoke-VmwareGuestInfoRead -Selected $Selected `
            -VariableName $script:VmwareGuestInfoExactStatus -TimeoutSeconds 20
        if ($null -eq $Encoded -or $Encoded -ceq $script:VmwareGuestInfoTombstone) {
            if ($null -eq $LastState -and
                [DateTime]::UtcNow -ge $FirstStatusDeadline) {
                throw 'The exact Windows build worker did not publish its initial authenticated status within seven minutes.'
            }
            if ($null -ne $LastState -and
                ([DateTime]::UtcNow - $LastObservedUtc).TotalMinutes -gt 5) {
                throw 'The exact Windows build authenticated status channel was unavailable for more than five minutes.'
            }
            Start-Sleep -Seconds 2
            continue
        }
        try {
            $StatusBytes = [Convert]::FromBase64String($Encoded)
            if ([Convert]::ToBase64String($StatusBytes) -cne $Encoded -or
                $StatusBytes.Length -gt $script:VmwareGuestInfoMaxManifestChars) {
                throw 'non-canonical or oversized status'
            }
            $Raw = [Text.Encoding]::UTF8.GetString($StatusBytes) | ConvertFrom-Json
        }
        catch { throw 'Exact Windows build status is invalid base64 JSON.' }
        finally { if ($null -ne $StatusBytes) { [Array]::Clear($StatusBytes, 0, $StatusBytes.Length) } }
        if ([string]$Raw.RunId -cne $RunId) {
            # A cached GuestInfo value from an older run is not evidence that
            # this worker started.  Apply the same bounded initial-status
            # deadline used for an empty/tombstoned channel.
            if ([DateTime]::UtcNow -ge $FirstStatusDeadline) {
                throw 'The exact Windows build worker did not publish its initial authenticated status within seven minutes.'
            }
            Start-Sleep -Milliseconds 500
            continue
        }
        $Status = ConvertFrom-WindowsExactStatus -Raw $Raw -RunId $RunId `
            -Selected $Selected
        if ($Status.State -in @('QUEUED', 'STARTING') -and
            ([DateTime]::UtcNow - [DateTime]::Parse(
                $Status.UpdatedUtc,
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind)).TotalMinutes -gt 5) {
            throw "The exact Windows build worker remained in $($Status.State) for more than five minutes."
        }
        $LastObservedUtc = [DateTime]::UtcNow
        if ($History.Count -gt 0 -and
            [string]$History[$History.Count - 1].StatusHmacSha256 -ceq
                [string]$Status.StatusHmacSha256) {
            # Polling the same signed record is transport liveness, not a new
            # servicing transition.  Do not inflate durable evidence with
            # duplicate samples.
            Start-Sleep -Seconds 2
            continue
        }
        if ($Ranks[$Status.State] -lt $LastRank) {
            if ($LastState -notin @('INSTALLING', 'REBOOTING') -or
                $Status.State -cne 'STARTING' -or
                $RebootTransitions -ge 1) {
                throw 'Exact Windows build status moved backwards.'
            }
            # The persistent task begins a new authenticated phase after the
            # servicing reboot.  VMware can power off before the host samples
            # the short-lived REBOOTING record, so only one authenticated
            # INSTALLING/REBOOTING -> STARTING edge may reset the phase rank.
            $RebootTransitions++
        }
        $LastRank = $Ranks[$Status.State]
        $LastState = [string]$Status.State
        $History.Add($Status)
        if ($null -ne $LiveStatusPath) {
            Write-UhrAtomicJson -Path $LiveStatusPath -Value ([ordered]@{
                SchemaVersion = 1
                RunId = $RunId
                Last = $Status
                History = @($History)
                CapturedUtc = [DateTime]::UtcNow.ToString('o')
            })
        }
        if ($Status.WindowsBuild -gt [uint64]26200 -or
            ($Status.WindowsBuild -eq [uint64]26200 -and
                $Status.WindowsUbr -gt [uint64]9445)) {
            throw "Guest reported a superseding Windows build: $($Status.WindowsBuild).$($Status.WindowsUbr)."
        }
        if ($Status.State -ceq 'FAILED') {
            throw "Exact Windows build servicing failed: $($Status.Message)"
        }
        if ($InitialOnly) {
            return [pscustomobject]@{ Final = $Status; History = @($History) }
        }
        if ($Status.State -ceq 'COMPLETE') {
            if ($Status.WindowsBuild -ne [uint64]26200 -or
                $Status.WindowsUbr -ne [uint64]9445 -or
                $Status.RebootRequired) {
                throw 'Exact Windows build completion did not prove 26200.9445.'
            }
            return [pscustomobject]@{ Final = $Status; History = @($History) }
        }
        Start-Sleep -Seconds 2
    }
    throw "Timed out waiting for exact Windows build status: $RunId"
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
    if ($Result.TimedOut -and -not $AllowFailure) {
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

function Assert-HostDiskCapacity {
    param([Parameter(Mandatory=$true)]$Selected)
    $MinimumFreeDiskMib = [uint64]$Config.Policy.MinimumHostFreeDiskMib
    $DiskRoots = @(
        [IO.Path]::GetPathRoot([IO.Path]::GetFullPath(
            [string]$Selected.VmxPath)),
        [IO.Path]::GetPathRoot([IO.Path]::GetFullPath(
            [string]$Config.StateRoot))
    ) | Sort-Object -Unique
    foreach ($DiskRoot in $DiskRoots) {
        $Drive = [IO.DriveInfo]::new([string]$DiskRoot)
        $FreeDiskMib = [uint64][Math]::Floor(
            [double]$Drive.AvailableFreeSpace / 1MB)
        if (-not $Drive.IsReady -or $FreeDiskMib -lt $MinimumFreeDiskMib) {
            throw "Insufficient host disk space on $DiskRoot`: $FreeDiskMib MiB free; policy requires at least $MinimumFreeDiskMib MiB."
        }
    }
}

function Assert-HostCapacity {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [int]$VmMemoryMibOverride = 0
    )
    $MinimumFreeDiskMib = [uint64]$Config.Policy.MinimumHostFreeDiskMib
    $DiskRoots = @(
        [IO.Path]::GetPathRoot([IO.Path]::GetFullPath(
            [string]$Selected.VmxPath)),
        [IO.Path]::GetPathRoot([IO.Path]::GetFullPath(
            [string]$Config.StateRoot))
    ) | Sort-Object -Unique
    foreach ($DiskRoot in $DiskRoots) {
        try {
            $Drive = [IO.DriveInfo]::new([string]$DiskRoot)
            $FreeDiskMib = [uint64][Math]::Floor(
                [double]$Drive.AvailableFreeSpace / 1MB)
        }
        catch {
            throw "Unable to determine free disk space for $DiskRoot; fail closed: $($_.Exception.Message)"
        }
        if (-not $Drive.IsReady -or $FreeDiskMib -lt $MinimumFreeDiskMib) {
            throw "Insufficient host disk space on $DiskRoot`: $FreeDiskMib MiB free; policy requires at least $MinimumFreeDiskMib MiB."
        }
    }
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
    $VmMib = if ($VmMemoryMibOverride -gt 0) {
        $VmMemoryMibOverride
    } else {
        Get-ConfiguredVmMemoryMib $Selected
    }
    $RequiredMib = [uint64]$VmMib +
        [uint64]$Config.Policy.MinimumHostReserveMib
    if ($FreeMib -lt $RequiredMib) {
        throw "Insufficient host memory: $FreeMib MiB free; need $RequiredMib MiB for the VM and reserve."
    }
}

function Invoke-HostBuild {
    param([switch]$PayloadReplayIndependent)
    $LogRoot = Join-Path $Config.StateRoot 'build'
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $Command = 'call {0} -arch=x64 -host_arch=x64' -f `
        [string]$Config.Tools.VsDevCmdPath
    $PayloadPolicy = if ($PayloadReplayIndependent) { 'ON' } else { 'OFF' }
    $Command += " && cmake --preset windows -DIB_PAYLOAD_REPLAY_INDEPENDENT=$PayloadPolicy"
    $Command += ' && cmake --build --preset windows'
    $Command += ' && ctest --test-dir build\windows --output-on-failure'
    $Command += ' && cmake --preset windows-static'
    $Command += ' && cmake --build --preset windows-static'
    $Result = Invoke-UhrProcess -FilePath $env:ComSpec `
        -ArgumentList @('/d', '/c', $Command) `
        -WorkingDirectory $UefiProjectRoot `
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
    # rewrite: their CMake targets were built from src/loader sources that no
    # longer exist, so a clean build cannot produce them.  Requiring them here
    # would fail every clean run and only a stale binary left in the build tree
    # could satisfy them, so they are not required at all.
    foreach ($Artifact in @(
            'build\windows\loader.efi',
            'build\windows\tcglog_efi_smoke.efi',
            'build\windows\handoff_payload_stub.efi',
            'build\windows\handoff_bootmgr_stub.efi',
            'build\windows-static\ib-tcg-dump.exe',
            'build\windows-static\ib-nested-e2e-service.exe',
            'build\windows-static\install_authenticode_test.exe',
            'build\windows-static\ib-install.exe')) {
        $Path = Join-Path $UefiProjectRoot $Artifact
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "Expected build artifact is missing: $Path"
        }
    }
    return [pscustomobject]@{
        Result = 'PASS'
        LoaderPath = [IO.Path]::GetFullPath((Join-Path $UefiProjectRoot `
            'build\windows\loader.efi'))
        LoaderSha256 = (Get-FileHash -LiteralPath `
            (Join-Path $UefiProjectRoot 'build\windows\loader.efi') `
            -Algorithm SHA256).Hash
        TcglogEfiSmokeSha256 = (Get-FileHash -LiteralPath `
            (Join-Path $UefiProjectRoot `
                'build\windows\tcglog_efi_smoke.efi') `
            -Algorithm SHA256).Hash
        TcgDumpSha256 = (Get-FileHash -LiteralPath `
            (Join-Path $UefiProjectRoot `
                'build\windows-static\ib-tcg-dump.exe') `
            -Algorithm SHA256).Hash
        InstallerSha256 = (Get-FileHash -LiteralPath `
            (Join-Path $UefiProjectRoot `
                'build\windows-static\ib-install.exe') `
            -Algorithm SHA256).Hash
    }
}

function Get-HypervisorEfiArtifact {
    param(
        [Parameter(Mandatory=$true)]
        [ValidateSet('uefi_hypervisor.efi', 'uefi_hypervisor_probe.efi',
            'uefi_hypervisor_post_ebs_probe.efi',
            'uefi_hypervisor_post_ebs_interrupt_probe.efi')]
        [string]$Name
    )
    $BuildRoot = Join-Path $HypervisorProjectRoot 'build'
    if (-not (Test-Path -LiteralPath $BuildRoot -PathType Container)) {
        throw "Hypervisor build directory is missing: $BuildRoot"
    }
    $Candidates = @(Get-ChildItem -LiteralPath $BuildRoot -Recurse -File `
        -Filter $Name | Where-Object {
            (Test-UhrPathWithin -Root $BuildRoot -Path $_.FullName) -and
            $_.Length -gt 0
        })
    if ($Candidates.Count -ne 1) {
        throw "Expected exactly one $Name artifact under $BuildRoot; found $($Candidates.Count)."
    }
    return [IO.Path]::GetFullPath($Candidates[0].FullName)
}

function Invoke-HypervisorEfiBuild {
    $LogRoot = Join-Path $Config.StateRoot 'build'
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $BuildRoot = Join-Path $HypervisorProjectRoot 'build\uefi-hypervisor'
    New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
    $UefiSourceCmake = $UefiProjectRoot.Replace('\', '/').Replace('$', '\$')
    $UefiSupportCmake = (Join-Path $UefiProjectRoot 'src\vendor').Replace(
        '\', '/').Replace('$', '\$')
    $WindowsProfile = $Config.Profiles.Windows
    $KernelProfileEnabled = $false
    $PathCache = Join-Path $BuildRoot 'uhr-uefi-paths.cmake'
    $CacheText =
        "set(HYPERSINV_UEFI_SOURCE_DIR `"$UefiSourceCmake`" CACHE PATH `"`" FORCE)`n" +
        "set(HYPERSINV_UEFI_SUPPORT_DIR `"$UefiSupportCmake`" CACHE PATH `"`" FORCE)`n" +
        "set(HYPERSINV_KERNEL_IMAGE `"`" CACHE FILEPATH `"`" FORCE)`n" +
        "set(HYPERSINV_KERNEL_PDB `"`" CACHE FILEPATH `"`" FORCE)`n" +
        "set(HYPERSINV_KERNEL_BUILD `"`" CACHE STRING `"`" FORCE)`n" +
        "set(HYPERSINV_KERNEL_UBR `"`" CACHE STRING `"`" FORCE)`n" +
        "set(HYPERSINV_REQUIRE_KERNEL_PROFILE OFF CACHE BOOL `"`" FORCE)`n"
    $HasKernelImage = $null -ne $WindowsProfile.PSObject.Properties[
        'KernelProfileImage'] -and -not [string]::IsNullOrWhiteSpace(
            [string]$WindowsProfile.KernelProfileImage)
    $HasKernelPdb = $null -ne $WindowsProfile.PSObject.Properties[
        'KernelProfilePdb'] -and -not [string]::IsNullOrWhiteSpace(
            [string]$WindowsProfile.KernelProfilePdb)
    if ($HasKernelImage -xor $HasKernelPdb) {
        throw 'Windows KernelProfileImage and KernelProfilePdb must be configured together.'
    }
    if ($HasKernelImage) {
        if ($null -eq $WindowsProfile.PSObject.Properties[
                'ExpectedWindowsBuild'] -or
            $null -eq $WindowsProfile.PSObject.Properties[
                'ExpectedWindowsUbr']) {
            throw 'A pinned kernel profile requires ExpectedWindowsBuild and ExpectedWindowsUbr.'
        }
        $KernelImage = [IO.Path]::GetFullPath(
            [string]$WindowsProfile.KernelProfileImage)
        $KernelPdb = [IO.Path]::GetFullPath(
            [string]$WindowsProfile.KernelProfilePdb)
        foreach ($ProfileInput in @($KernelImage, $KernelPdb)) {
            if (-not (Test-UhrPathWithin -Root $Config.ProjectRoot `
                    -Path $ProfileInput) -or
                -not (Test-Path -LiteralPath $ProfileInput -PathType Leaf)) {
                throw "Kernel profile input must be an existing file under ProjectRoot: $ProfileInput"
            }
        }
        $KernelBuild = [uint64]$WindowsProfile.ExpectedWindowsBuild
        $KernelUbr = [uint64]$WindowsProfile.ExpectedWindowsUbr
        if ($KernelBuild -eq 0 -or $KernelBuild -gt [uint64]4294967295 -or
            $KernelUbr -gt [uint64]4294967295) {
            throw 'Pinned kernel build metadata must fit unsigned 32-bit fields.'
        }
        $KernelImageCmake = $KernelImage.Replace('\', '/').Replace('$', '\$')
        $KernelPdbCmake = $KernelPdb.Replace('\', '/').Replace('$', '\$')
        $CacheText +=
            "set(HYPERSINV_KERNEL_IMAGE `"$KernelImageCmake`" CACHE FILEPATH `"`" FORCE)`n" +
            "set(HYPERSINV_KERNEL_PDB `"$KernelPdbCmake`" CACHE FILEPATH `"`" FORCE)`n" +
            "set(HYPERSINV_KERNEL_BUILD `"$KernelBuild`" CACHE STRING `"`" FORCE)`n" +
            "set(HYPERSINV_KERNEL_UBR `"$KernelUbr`" CACHE STRING `"`" FORCE)`n" +
            "set(HYPERSINV_REQUIRE_KERNEL_PROFILE ON CACHE BOOL `"`" FORCE)`n"
        $KernelProfileEnabled = $true
    }
    [IO.File]::WriteAllText($PathCache, $CacheText,
        [Text.UTF8Encoding]::new($false))
    $ConfigureCommand = 'call {0} -arch=x64 -host_arch=x64' -f `
        [string]$Config.Tools.VsDevCmdPath
    # Invoke-UhrProcess quotes the complete cmd.exe /c payload. Keep these
    # paths relative to WorkingDirectory so cmd.exe does not parse nested
    # quotes around a repository path containing spaces.
    $ConfigureCommand += ' && cmake -C build\uefi-hypervisor\uhr-uefi-paths.cmake -S . -B build\uefi-hypervisor'
    $ConfigureCommand += ' -DHYPERSINV_BUILD_DRIVER=OFF -DHYPERSINV_BUILD_LOADER=OFF -DHYPERSINV_BUILD_CORE=OFF -DHYPERSINV_BUILD_UEFI=ON'
    $ConfigureCommand += ' && cmake --build build\uefi-hypervisor --target uefi_hypervisor uefi_hypervisor_probe uefi_hypervisor_post_ebs_probe uefi_hypervisor_post_ebs_interrupt_probe --config Release'
    $Result = Invoke-UhrProcess -FilePath $env:ComSpec `
        -ArgumentList @('/d', '/c', $ConfigureCommand) `
        -WorkingDirectory $HypervisorProjectRoot `
        -TimeoutSeconds ([int]$Config.Policy.BuildTimeoutSeconds)
    $Stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    [IO.File]::WriteAllText((Join-Path $LogRoot "$Stamp-hypervisor-stdout.log"),
        $Result.Stdout, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $LogRoot "$Stamp-hypervisor-stderr.log"),
        $Result.Stderr, [Text.UTF8Encoding]::new($false))
    if ($Result.TimedOut) { throw 'The hypervisor EFI build timed out.' }
    if ($Result.ExitCode -ne 0) {
        throw "The hypervisor EFI build failed with exit code $($Result.ExitCode)."
    }
    $Artifact = Get-HypervisorEfiArtifact -Name 'uefi_hypervisor.efi'
    $Probe = Get-HypervisorEfiArtifact -Name 'uefi_hypervisor_probe.efi'
    $PostEbsProbe = Get-HypervisorEfiArtifact -Name `
        'uefi_hypervisor_post_ebs_probe.efi'
    $PostEbsInterruptProbe = Get-HypervisorEfiArtifact -Name `
        'uefi_hypervisor_post_ebs_interrupt_probe.efi'
    return [pscustomobject]@{
        Result = 'PASS'
        Path = $Artifact
        Sha256 = (Get-FileHash -LiteralPath $Artifact -Algorithm SHA256).Hash
        ProbePath = $Probe
        ProbeSha256 = (Get-FileHash -LiteralPath $Probe -Algorithm SHA256).Hash
        PostEbsProbePath = $PostEbsProbe
        PostEbsProbeSha256 = (Get-FileHash -LiteralPath $PostEbsProbe `
            -Algorithm SHA256).Hash
        PostEbsInterruptProbePath = $PostEbsInterruptProbe
        PostEbsInterruptProbeSha256 = (Get-FileHash -LiteralPath `
            $PostEbsInterruptProbe -Algorithm SHA256).Hash
        KernelProfileEnabled = $KernelProfileEnabled
    }
}

function Invoke-Hvr1RuntimeCompanionBuild {
    $LogRoot = Join-Path $Config.StateRoot 'build'
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $BuildRoot = Join-Path $HypervisorProjectRoot 'build\hvr1-direct'
    New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
    $Command = 'call {0} -arch=x64 -host_arch=x64' -f `
        [string]$Config.Tools.VsDevCmdPath
    # Let CMake select the newest installed Visual Studio generator. Quoting a
    # generator name inside cmd.exe's already quoted /c payload truncates it at
    # the first space (for example, to "Visual"). The existing build tree still
    # pins its generator through CMakeCache.txt on subsequent invocations.
    $Command += ' && cmake -S . -B build\hvr1-direct -A x64'
    $Command += ' -DHYPERSINV_BUILD_DRIVER=OFF -DHYPERSINV_BUILD_LOADER=OFF -DHYPERSINV_BUILD_CORE=OFF -DHYPERSINV_BUILD_UEFI=OFF'
    $Command += ' -DHYPERSINV_BUILD_EFI_RUNTIME_COMPANION=ON -DHYPERSINV_BUILD_EFI_RUNTIME_SERVICE=ON -DHYPERSINV_TEST_EFI_RUNTIME_COMPANION=ON'
    $Command += ' && cmake --build build\hvr1-direct --target hvr1_efi_runtime_companion hvr1_runtime_service hvr1_auth_tests hvr1_overlay_adapter_tests --config Release'
    $Command += ' && ctest --test-dir build\hvr1-direct -C Release --output-on-failure'
    $Result = Invoke-UhrProcess -FilePath $env:ComSpec `
        -ArgumentList @('/d', '/c', $Command) `
        -WorkingDirectory $HypervisorProjectRoot `
        -TimeoutSeconds ([int]$Config.Policy.BuildTimeoutSeconds)
    $Stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    [IO.File]::WriteAllText((Join-Path $LogRoot `
            "$Stamp-hvr1-companion-stdout.log"), $Result.Stdout,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $LogRoot `
            "$Stamp-hvr1-companion-stderr.log"), $Result.Stderr,
        [Text.UTF8Encoding]::new($false))
    if ($Result.TimedOut) { throw 'The HVR1 runtime companion build timed out.' }
    if ($Result.ExitCode -ne 0) {
        throw "The HVR1 runtime companion build failed with exit code $($Result.ExitCode)."
    }
    $ArtifactRoot = Join-Path $BuildRoot `
        'src\driver\efi_runtime_companion\Release'
    $Driver = Join-Path $ArtifactRoot 'hvr1_efi_runtime_companion.sys'
    $Service = Join-Path $ArtifactRoot 'hvr1_runtime_service.exe'
    foreach ($Artifact in @($Driver, $Service)) {
        if (-not (Test-Path -LiteralPath $Artifact -PathType Leaf) -or
            (Get-Item -LiteralPath $Artifact).Length -lt 1024) {
            throw "The HVR1 runtime companion artifact is invalid: $Artifact"
        }
    }
    return [pscustomobject]@{
        Result = 'PASS'
        DriverPath = [IO.Path]::GetFullPath($Driver)
        DriverSha256 = (Get-FileHash -LiteralPath $Driver -Algorithm SHA256).Hash
        ServicePath = [IO.Path]::GetFullPath($Service)
        ServiceSha256 = (Get-FileHash -LiteralPath $Service -Algorithm SHA256).Hash
    }
}

function Invoke-CombinedHostBuild {
    param(
        [switch]$PayloadReplayIndependent,
        [switch]$IncludeRuntimeCompanion
    )
    $LoaderBuild = Invoke-HostBuild `
        -PayloadReplayIndependent:$PayloadReplayIndependent
    $HypervisorBuild = Invoke-HypervisorEfiBuild
    $CompanionBuild = if ($IncludeRuntimeCompanion) {
        Invoke-Hvr1RuntimeCompanionBuild
    } else { $null }
    $Result = [ordered]@{
        Result = 'PASS'
        LoaderEfiPath = $LoaderBuild.LoaderPath
        LoaderSha256 = $LoaderBuild.LoaderSha256
        HypervisorEfiPath = $HypervisorBuild.Path
        HypervisorEfiSha256 = $HypervisorBuild.Sha256
        HypervisorProbeEfiPath = $HypervisorBuild.ProbePath
        HypervisorProbeEfiSha256 = $HypervisorBuild.ProbeSha256
        HypervisorPostEbsProbeEfiPath = $HypervisorBuild.PostEbsProbePath
        HypervisorPostEbsProbeEfiSha256 = $HypervisorBuild.PostEbsProbeSha256
        HypervisorPostEbsInterruptProbeEfiPath = $HypervisorBuild.PostEbsInterruptProbePath
        HypervisorPostEbsInterruptProbeEfiSha256 = $HypervisorBuild.PostEbsInterruptProbeSha256
        KernelProfileEnabled = $HypervisorBuild.KernelProfileEnabled
    }
    if ($null -ne $CompanionBuild) {
        $Result.CompanionDriverPath = $CompanionBuild.DriverPath
        $Result.CompanionDriverSha256 = $CompanionBuild.DriverSha256
        $Result.CompanionServicePath = $CompanionBuild.ServicePath
        $Result.CompanionServiceSha256 = $CompanionBuild.ServiceSha256
    }
    return [pscustomobject]$Result
}

function Compress-UhrArtifactBytes {
    param(
        [Parameter(Mandatory=$true)]
        [AllowEmptyCollection()]
        [byte[]]$Bytes
    )
    if ($Bytes.Length -eq 0) { return ,([byte[]]@()) }
    $Output = [IO.MemoryStream]::new()
    try {
        $Compressor = [IO.Compression.DeflateStream]::new(
            $Output, [IO.Compression.CompressionLevel]::Optimal, $true)
        try { $Compressor.Write($Bytes, 0, $Bytes.Length) }
        finally { $Compressor.Dispose() }
        return ,$Output.ToArray()
    }
    finally { $Output.Dispose() }
}

function New-HostStage {
    param(
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)]
        [ValidateSet(
            'LinuxPositive', 'LinuxHypervisorSmoke',
            'LinuxHypervisorPostEbsSmoke', 'LinuxHypervisorPostEbsInterruptSmoke',
            'LinuxReplayDryRun', 'LinuxPeRehash',
            'LinuxDivergenceManifest', 'LinuxHandoffMechanics',
            'LinuxProductionHandoff', 'LinuxOneShotCycle',
            'LinuxNestedDiskProbe', 'LinuxNestedWindowsE2E',
            'LinuxNestedWindowsHypervisorE2E',
            'LinuxBootNextE2E',
            'WindowsProbe', 'WindowsCapture', 'WindowsNestedPrep',
            'WindowsExactBuildUpdate',
            'WindowsDirectHypervisorSetup',
            'WindowsDirectHypervisorVerify')]
        [string]$Workflow,
        [string]$HypervisorEfiPath,
        [string]$LoaderEfiPath,
        [string]$DirectVerifyRunId,
        [switch]$DirectScannerOnly,
        [switch]$DirectProcessProfileEnabled,
        [string]$CompanionDriverPath,
        [string]$CompanionServicePath,
        [string]$HypervisorProbeEfiPath,
        [string]$HypervisorPostEbsProbeEfiPath,
        [string]$HypervisorPostEbsInterruptProbeEfiPath
    )
    if (-not (Test-UhrGuestInfoRunId $RunId)) { throw "Unsafe RunId: $RunId" }
    $RunRoot = Join-Path (Join-Path $Config.StateRoot 'runs') $RunId
    $Stage = Join-Path $RunRoot 'stage'
    $Evidence = Join-Path $RunRoot 'evidence'
    if (Test-Path -LiteralPath $RunRoot) {
        throw "RunId already exists: $RunId"
    }
    New-Item -ItemType Directory -Force -Path $Stage,$Evidence | Out-Null
    # Retired with the loader rewrite.  These workflows stage loader binaries
    # whose CMake targets were built from src/loader sources that no longer exist:
    # the replay/pe_rehash/divergence/handoff smoke images, and ibaudit.exe, which
    # the nested-Windows branch hashes into nested-SHA256SUMS.txt and hands to the
    # guest.  handoff_fixture_gen.exe went away the same way, so the production-
    # handoff, one-shot and hypervisor-smoke branches that synthesise a tcglog.ib
    # with it are retired here too.  Refusing here reports the reason instead of
    # failing on a missing file deep inside staging.
    if ($Workflow -in @(
            'LinuxReplayDryRun', 'LinuxPeRehash',
            'LinuxDivergenceManifest', 'LinuxHandoffMechanics',
            'LinuxNestedWindowsE2E', 'LinuxNestedWindowsHypervisorE2E',
            'LinuxBootNextE2E',
            'LinuxProductionHandoff', 'LinuxOneShotCycle',
            'LinuxHypervisorSmoke', 'LinuxHypervisorPostEbsSmoke',
            'LinuxHypervisorPostEbsInterruptSmoke')) {
        throw "Workflow '$Workflow' is retired: its loader artifacts no longer have build targets."
    }
    if ($Workflow -in @(
            'LinuxPositive', 'LinuxHypervisorSmoke',
            'LinuxHypervisorPostEbsSmoke', 'LinuxHypervisorPostEbsInterruptSmoke',
            'LinuxReplayDryRun', 'LinuxPeRehash',
            'LinuxDivergenceManifest', 'LinuxHandoffMechanics',
            'LinuxProductionHandoff', 'LinuxOneShotCycle',
            'LinuxNestedDiskProbe', 'LinuxNestedWindowsE2E',
            'LinuxNestedWindowsHypervisorE2E',
            'LinuxBootNextE2E')) {
        $EfiSource = switch ($Workflow) {
            'LinuxPositive' { 'build\windows\loader.efi' }
            'LinuxHypervisorSmoke' { 'build\windows\loader.efi' }
            'LinuxHypervisorPostEbsSmoke' { 'build\windows\loader.efi' }
            'LinuxHypervisorPostEbsInterruptSmoke' { 'build\windows\loader.efi' }
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
            'LinuxNestedWindowsHypervisorE2E' { 'build\windows\loader.efi' }
            'LinuxBootNextE2E' { 'build\windows\loader.efi' }
        }
        $Files = @(
            @{ Source = $EfiSource; Name = 'loader.efi' },
            @{ Source = 'tools\host-runner\guest\Invoke-LinuxPositivePath.sh'; Name = 'Invoke-LinuxPositivePath.sh' }
        )
        if ($Workflow -in @('LinuxHypervisorSmoke',
                'LinuxNestedWindowsHypervisorE2E',
                'LinuxHypervisorPostEbsSmoke',
                'LinuxHypervisorPostEbsInterruptSmoke')) {
            if ([string]::IsNullOrWhiteSpace($HypervisorEfiPath) -or
                ($Workflow -eq 'LinuxHypervisorSmoke' -and
                    [string]::IsNullOrWhiteSpace($HypervisorProbeEfiPath)) -or
                ($Workflow -eq 'LinuxHypervisorPostEbsSmoke' -and
                    [string]::IsNullOrWhiteSpace($HypervisorPostEbsProbeEfiPath)) -or
                ($Workflow -eq 'LinuxHypervisorPostEbsInterruptSmoke' -and
                    [string]::IsNullOrWhiteSpace($HypervisorPostEbsInterruptProbeEfiPath))) {
                throw "$Workflow requires the matching built hypervisor EFI artifacts."
            }
            $HypervisorEfiPath = [IO.Path]::GetFullPath($HypervisorEfiPath)
            $SelectedProbePath = switch ($Workflow) {
                'LinuxHypervisorSmoke' {
                    [IO.Path]::GetFullPath($HypervisorProbeEfiPath)
                }
                'LinuxHypervisorPostEbsSmoke' {
                    [IO.Path]::GetFullPath($HypervisorPostEbsProbeEfiPath)
                }
                'LinuxHypervisorPostEbsInterruptSmoke' {
                    [IO.Path]::GetFullPath($HypervisorPostEbsInterruptProbeEfiPath)
                }
                default { $null }
            }
            if (-not (Test-UhrPathWithin -Root (Join-Path $HypervisorProjectRoot 'build') -Path $HypervisorEfiPath) -or
                -not (Test-Path -LiteralPath $HypervisorEfiPath -PathType Leaf) -or
                [IO.Path]::GetFileName($HypervisorEfiPath) -cne 'uefi_hypervisor.efi') {
                throw 'The hypervisor EFI artifact must be uefi_hypervisor.efi under the Hypervisor build directory.'
            }
            $ExpectedProbeName = switch ($Workflow) {
                'LinuxHypervisorSmoke' { 'uefi_hypervisor_probe.efi' }
                'LinuxHypervisorPostEbsSmoke' {
                    'uefi_hypervisor_post_ebs_probe.efi'
                }
                'LinuxHypervisorPostEbsInterruptSmoke' {
                    'uefi_hypervisor_post_ebs_interrupt_probe.efi'
                }
                default { $null }
            }
            if ($Workflow -ne 'LinuxNestedWindowsHypervisorE2E') {
                if (-not (Test-UhrPathWithin -Root (Join-Path $HypervisorProjectRoot 'build') -Path $SelectedProbePath) -or
                    -not (Test-Path -LiteralPath $SelectedProbePath -PathType Leaf) -or
                    [IO.Path]::GetFileName($SelectedProbePath) -cne $ExpectedProbeName) {
                    throw "The hypervisor probe artifact must be $ExpectedProbeName under the Hypervisor build directory."
                }
                $Files += @(
                    @{ AbsoluteSource = $HypervisorEfiPath; Name = 'payload.efi' },
                    @{ AbsoluteSource = $SelectedProbePath; Name = 'replacement.efi' },
                    @{ AbsoluteSource = $SelectedProbePath; Name = 'bootmgfw.efi.bak' }
                )
            }
        }
        if ($Workflow -eq 'LinuxNestedDiskProbe') {
            $Files = @(
                @{ Source = 'tools\host-runner\guest\Invoke-LinuxNestedDiskProbe.sh'; Name = 'Invoke-LinuxNestedDiskProbe.sh' }
            )
        }
        elseif ($Workflow -in @('LinuxNestedWindowsE2E',
                'LinuxNestedWindowsHypervisorE2E',
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
                $PreparedText -notmatch '(?m)^NESTED_PREP_SERVICE=INSTALLED$' -or
                $PreparedText -notmatch '(?m)^NESTED_PREP_SERVICE_SHA256=[0-9A-F]{64}$' -or
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
                (Join-Path $UefiProjectRoot `
                    'build\windows-static\ib-tcg-dump.exe'),
                (Join-Path $UefiProjectRoot `
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
                    (Join-Path $UefiProjectRoot `
                        'build\windows-static\ib-install.exe'),
                    (Join-Path $UefiProjectRoot 'build\windows\loader.efi'),
                    (Join-Path $UefiProjectRoot 'src\vendor\shimx64.efi'))
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
            elseif ($Workflow -eq 'LinuxNestedWindowsHypervisorE2E') {
                $Files = @(
                    @{ Source = 'tools\host-runner\guest\Invoke-LinuxNestedWindowsE2E.sh'; Name = 'Invoke-LinuxNestedWindowsE2E.sh' },
                    @{ Source = 'tools\patches\edk2-stable202605-no-measure.patch'; Name = 'edk2-no-measure.patch' },
                    @{ Source = 'build\windows\loader.efi'; Name = 'loader.efi' },
                    @{ AbsoluteSource = $HypervisorEfiPath; Name = 'payload.efi' },
                    @{ AbsoluteSource = $PreparedLog; Name = 'tcglog.ib' },
                    @{ AbsoluteSource = $HashFile; Name = 'bootmgfw.sha256' },
                    @{ Source = 'build\windows-static\ib-tcg-dump.exe'; Name = 'ib-tcg-dump.exe' },
                    @{ Source = 'build\windows-static\ibaudit.exe'; Name = 'ibaudit.exe' },
                    @{ AbsoluteSource = $NestedManifest; Name = 'nested-SHA256SUMS.txt' },
                    @{ AbsoluteSource = $Marker; Name = 'UEFILoader-Nested-E2E.marker' }
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
        $Files += @{
            Source = 'tools\host-runner\guest\Cleanup-LinuxGuestRun.sh'
            Name = 'Cleanup-LinuxGuestRun.sh'
        }
        if ($Workflow -ne 'LinuxPeRehash' -and
            $Workflow -ne 'LinuxNestedDiskProbe' -and
            $Workflow -ne 'LinuxNestedWindowsE2E' -and
            $Workflow -ne 'LinuxNestedWindowsHypervisorE2E' -and
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
                -FilePath (Join-Path $UefiProjectRoot `
                    'build\windows\handoff_fixture_gen.exe') `
                -ArgumentList @($GeneratedLog) `
                -WorkingDirectory $UefiProjectRoot -TimeoutSeconds 30
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
        elseif ($Workflow -in @('LinuxHypervisorSmoke',
                'LinuxHypervisorPostEbsSmoke',
                'LinuxHypervisorPostEbsInterruptSmoke')) {
            $GeneratedLog = Join-Path $RunRoot 'tcglog.ib'
            $Generator = Invoke-UhrProcess `
                -FilePath (Join-Path $UefiProjectRoot 'build\windows\handoff_fixture_gen.exe') `
                -ArgumentList @($GeneratedLog) `
                -WorkingDirectory $UefiProjectRoot -TimeoutSeconds 30
            if ($Generator.TimedOut -or $Generator.ExitCode -ne 0 -or
                -not (Test-Path -LiteralPath $GeneratedLog -PathType Leaf)) {
                throw "Hypervisor smoke tcglog.ib generation failed: $($Generator.Stderr.Trim())"
            }
            $Files += @{
                AbsoluteSource = $GeneratedLog
                Name = 'tcglog.ib'
            }
        }
    }
    elseif ($Workflow -eq 'WindowsExactBuildUpdate') {
        $ExactScript = Join-Path $PSScriptRoot `
            'guest\Invoke-WindowsExactBuildUpdate.ps1'
        if (-not (Test-Path -LiteralPath $ExactScript -PathType Leaf)) {
            throw 'The exact Windows build servicing guest script is missing.'
        }
        $ExactText = Get-Content -Raw -LiteralPath $ExactScript
        foreach ($Token in @(
                'KB5124008', '26200', '9445', 'Mode', 'Worker',
                'guestinfo.uefiloader.exact.status', 'SYSTEM',
                'Microsoft.Update.Session', 'IsInstalled=0 and Type=''Software''')) {
            if (-not $ExactText.Contains($Token)) {
                throw "The exact Windows build servicing script is missing its fixed contract token: $Token"
            }
        }
        $Files = @(
            @{ Source = 'build\windows-static\ib-tcg-dump.exe'; Name = 'ib-tcg-dump.exe' },
            @{ AbsoluteSource = $ExactScript; Name = 'Invoke-WindowsGuestTest.ps1' }
        )
    }
    else {
        $GuestTestTemplate = Get-Content -Raw -LiteralPath (Join-Path `
            $PSScriptRoot 'guest\Invoke-WindowsGuestTest.ps1')
        $Placeholder = '__NESTED_SCRIPT_BASE64__'
        $ServicePlaceholder = '__NESTED_SERVICE_BASE64__'
        $DirectModePlaceholder = '__DIRECT_HYPERVISOR_MODE__'
        $DirectLoaderPlaceholder = '__DIRECT_LOADER_EFI_DEFLATE_BASE64__'
        $DirectEfiPlaceholder = '__DIRECT_HYPERVISOR_EFI_DEFLATE_BASE64__'
        $DirectInstallerPlaceholder = '__DIRECT_INSTALLER_DEFLATE_BASE64__'
        $DirectStartupPlaceholder = '__DIRECT_STARTUP_SCRIPT_BASE64__'
        $DirectCompanionDriverPlaceholder = `
            '__DIRECT_COMPANION_DRIVER_DEFLATE_BASE64__'
        $DirectCompanionServicePlaceholder = `
            '__DIRECT_COMPANION_SERVICE_DEFLATE_BASE64__'
        $DirectCompanionCertificatePlaceholder = `
            '__DIRECT_COMPANION_CERTIFICATE_BASE64__'
        $DirectProcessorCountPlaceholder = '__DIRECT_PROCESSOR_COUNT__'
        if ([regex]::Matches($GuestTestTemplate,
                [regex]::Escape($Placeholder)).Count -ne 1 -or
            [regex]::Matches($GuestTestTemplate,
                [regex]::Escape($ServicePlaceholder)).Count -ne 1 -or
            [regex]::Matches($GuestTestTemplate,
                [regex]::Escape($DirectModePlaceholder)).Count -ne 1 -or
            [regex]::Matches($GuestTestTemplate,
                [regex]::Escape($DirectLoaderPlaceholder)).Count -ne 1 -or
            [regex]::Matches($GuestTestTemplate,
                [regex]::Escape($DirectEfiPlaceholder)).Count -ne 1 -or
            [regex]::Matches($GuestTestTemplate,
                [regex]::Escape($DirectInstallerPlaceholder)).Count -ne 1 -or
            [regex]::Matches($GuestTestTemplate,
                [regex]::Escape($DirectProcessorCountPlaceholder)).Count -ne 1) {
            throw 'Windows guest-test template has invalid nested artifact placeholders.'
        }
        if ([regex]::Matches($GuestTestTemplate,
                [regex]::Escape($DirectStartupPlaceholder)).Count -ne 1) {
            throw 'Windows guest-test template has invalid direct startup placeholder.'
        }
        if ([regex]::Matches($GuestTestTemplate,
                [regex]::Escape($DirectCompanionDriverPlaceholder)).Count -ne 1 -or
            [regex]::Matches($GuestTestTemplate,
                [regex]::Escape($DirectCompanionServicePlaceholder)).Count -ne 1 -or
            [regex]::Matches($GuestTestTemplate,
                [regex]::Escape($DirectCompanionCertificatePlaceholder)).Count -ne 1) {
            throw 'Windows guest-test template has invalid HVR1 companion placeholders.'
        }
        $NestedScriptBytes = [IO.File]::ReadAllBytes((Join-Path `
            $PSScriptRoot 'guest\Invoke-NestedE2ECapture.ps1'))
        $NestedServiceBytes = [IO.File]::ReadAllBytes((Join-Path `
            $UefiProjectRoot `
            'build\windows-static\ib-nested-e2e-service.exe'))
        if ($NestedServiceBytes.Length -eq 0) {
            throw 'The nested E2E service build artifact is empty.'
        }
        $DirectMode = switch ($Workflow) {
            'WindowsDirectHypervisorSetup' { 'Setup' }
            'WindowsDirectHypervisorVerify' { 'Verify' }
            default { 'None' }
        }
        $DirectEfiBytes = [byte[]]@()
        $DirectLoaderBytes = [byte[]]@()
        $DirectStartupBytes = [byte[]]@()
        $DirectCompanionDriverBytes = [byte[]]@()
        $DirectCompanionServiceBytes = [byte[]]@()
        $DirectCompanionDriverCompressedBytes = [byte[]]@()
        $DirectCompanionServiceCompressedBytes = [byte[]]@()
        $DirectCompanionCertificateBytes = [byte[]]@()
        if ($Workflow -eq 'WindowsDirectHypervisorSetup') {
            if ([string]::IsNullOrWhiteSpace($LoaderEfiPath) -or
                [string]::IsNullOrWhiteSpace($HypervisorEfiPath) -or
                [string]::IsNullOrWhiteSpace($CompanionDriverPath) -or
                [string]::IsNullOrWhiteSpace($CompanionServicePath)) {
                throw 'WindowsDirectHypervisorSetup requires the loader, EFI hypervisor, and HVR1 companion artifacts.'
            }
            if (-not (Test-UhrRunId $DirectVerifyRunId) -or
                $DirectVerifyRunId -cnotmatch '^uefi-windows-probe-') {
                throw 'WindowsDirectHypervisorSetup requires a safe verification RunId.'
            }
            $LoaderEfiPath = [IO.Path]::GetFullPath($LoaderEfiPath)
            $HypervisorEfiPath = [IO.Path]::GetFullPath($HypervisorEfiPath)
            if (-not (Test-UhrPathWithin -Root $UefiProjectRoot `
                    -Path $LoaderEfiPath) -or
                -not (Test-Path -LiteralPath $LoaderEfiPath `
                    -PathType Leaf) -or
                [IO.Path]::GetFileName($LoaderEfiPath) -cne 'loader.efi') {
                throw 'The direct Windows loader artifact must be loader.efi under the UEFI project.'
            }
            if (-not (Test-UhrPathWithin `
                    -Root (Join-Path $HypervisorProjectRoot 'build') `
                    -Path $HypervisorEfiPath) -or
                -not (Test-Path -LiteralPath $HypervisorEfiPath -PathType Leaf) -or
                [IO.Path]::GetFileName($HypervisorEfiPath) -cne
                    'uefi_hypervisor.efi') {
                throw 'The direct Windows EFI artifact must be uefi_hypervisor.efi under the Hypervisor build directory.'
            }
            $DirectLoaderBytes = [IO.File]::ReadAllBytes($LoaderEfiPath)
            $DirectEfiBytes = [IO.File]::ReadAllBytes($HypervisorEfiPath)
            $CompanionDriverPath = [IO.Path]::GetFullPath($CompanionDriverPath)
            $CompanionServicePath = [IO.Path]::GetFullPath($CompanionServicePath)
            foreach ($CompanionArtifact in @(
                    @{ Path = $CompanionDriverPath; Name = 'hvr1_efi_runtime_companion.sys' },
                    @{ Path = $CompanionServicePath; Name = 'hvr1_runtime_service.exe' })) {
                if (-not (Test-UhrPathWithin `
                        -Root (Join-Path $HypervisorProjectRoot 'build') `
                        -Path $CompanionArtifact.Path) -or
                    -not (Test-Path -LiteralPath $CompanionArtifact.Path `
                        -PathType Leaf) -or
                    [IO.Path]::GetFileName($CompanionArtifact.Path) -cne
                        $CompanionArtifact.Name) {
                    throw "The direct Windows companion artifact is invalid: $($CompanionArtifact.Name)"
                }
            }
            $DirectCompanionServiceBytes = [IO.File]::ReadAllBytes(
                $CompanionServicePath)
            $TemporaryDriverPath = Join-Path $RunRoot `
                'hvr1_efi_runtime_companion.page-hashed.sys'
            $TemporaryCertificatePath = Join-Path $RunRoot `
                'hvr1-test-public.cer'
            $SigningCertificate = $null
            try {
                Copy-Item -LiteralPath $CompanionDriverPath `
                    -Destination $TemporaryDriverPath
                $SigningCertificate = New-SelfSignedCertificate `
                    -Type CodeSigningCert `
                    -Subject "CN=UEFILoader HVR1 Disposable VM $RunId" `
                    -CertStoreLocation 'Cert:\CurrentUser\My' `
                    -KeyAlgorithm RSA -KeyLength 3072 `
                    -HashAlgorithm SHA256 `
                    -NotAfter ([DateTime]::UtcNow.AddDays(2))
                if ($null -eq $SigningCertificate -or
                    -not $SigningCertificate.HasPrivateKey) {
                    throw 'The host could not create the disposable HVR1 test-signing certificate.'
                }
                $SignToolPath = Get-UhrSignToolPath
                $SignResult = Invoke-UhrProcess -FilePath $SignToolPath `
                    -ArgumentList @('sign', '/v', '/fd', 'SHA256', '/ph',
                        '/s', 'My', '/sha1',
                        $SigningCertificate.Thumbprint,
                        $TemporaryDriverPath) `
                    -WorkingDirectory $RunRoot -TimeoutSeconds 60
                if ($SignResult.TimedOut -or $SignResult.ExitCode -ne 0) {
                    throw "SignTool page-hash signing failed: $($SignResult.Stderr.Trim())"
                }
                $SignedDriver = Get-AuthenticodeSignature `
                    -LiteralPath $TemporaryDriverPath
                if ($null -eq $SignedDriver.SignerCertificate -or
                    $SignedDriver.SignerCertificate.Thumbprint -cne
                        $SigningCertificate.Thumbprint) {
                    throw 'The page-hashed companion signer does not match the disposable certificate.'
                }
                Export-Certificate -Cert $SigningCertificate `
                    -FilePath $TemporaryCertificatePath -Type CERT -Force |
                    Out-Null
                $DirectCompanionDriverBytes = [IO.File]::ReadAllBytes(
                    $TemporaryDriverPath)
                $DirectCompanionCertificateBytes = [IO.File]::ReadAllBytes(
                    $TemporaryCertificatePath)
            }
            finally {
                if ($null -ne $SigningCertificate) {
                    $PrivateCertificatePath = 'Cert:\CurrentUser\My\' +
                        $SigningCertificate.Thumbprint
                    if (Test-Path -LiteralPath $PrivateCertificatePath) {
                        Remove-Item -LiteralPath $PrivateCertificatePath -Force
                    }
                }
                foreach ($TemporaryPath in @($TemporaryDriverPath,
                        $TemporaryCertificatePath)) {
                    if (Test-Path -LiteralPath $TemporaryPath) {
                        if (-not (Test-UhrPathWithin -Root $RunRoot `
                                -Path $TemporaryPath)) {
                            throw 'Refusing unsafe temporary signing-artifact cleanup.'
                        }
                        Remove-Item -LiteralPath $TemporaryPath -Force
                    }
                }
            }
            $DirectStartupTemplatePath = Join-Path $PSScriptRoot `
                'guest\Invoke-WindowsDirectHypervisorStartup.ps1'
            $DirectStartupTemplate = Get-Content -Raw -LiteralPath `
                $DirectStartupTemplatePath
            $DirectVerifyPlaceholder = '__DIRECT_VERIFY_RUN_ID__'
            $DirectScannerOnlyPlaceholder = '__DIRECT_SCANNER_ONLY__'
            $DirectProcessProfilePlaceholder = `
                '__DIRECT_PROCESS_PROFILE_ENABLED__'
            if ([regex]::Matches($DirectStartupTemplate,
                    [regex]::Escape($DirectVerifyPlaceholder)).Count -ne 1 -or
                [regex]::Matches($DirectStartupTemplate,
                    [regex]::Escape(
                        $DirectProcessorCountPlaceholder)).Count -ne 1 -or
                [regex]::Matches($DirectStartupTemplate,
                    [regex]::Escape(
                        $DirectScannerOnlyPlaceholder)).Count -ne 1 -or
                [regex]::Matches($DirectStartupTemplate,
                    [regex]::Escape(
                        $DirectProcessProfilePlaceholder)).Count -ne 1) {
                throw 'Direct startup template has invalid run/topology placeholders.'
            }
            $DirectStartupText = $DirectStartupTemplate.Replace(
                $DirectVerifyPlaceholder, $DirectVerifyRunId).Replace(
                $DirectProcessorCountPlaceholder,
                [string]$WindowsDirectProcessorCount).Replace(
                $DirectScannerOnlyPlaceholder,
                $(if ($DirectScannerOnly) { '$true' } else { '$false' })).Replace(
                $DirectProcessProfilePlaceholder,
                $(if ($DirectProcessProfileEnabled) { '$true' } else { '$false' }))
            $DirectStartupBytes = [Text.Encoding]::UTF8.GetBytes(
                $DirectStartupText)
        }
        $DirectInstallerBytes = [byte[]]@()
        if ($DirectMode -cne 'None') {
            $DirectInstallerPath = Get-UhrUefiPath `
                'build\windows-static\ib-install.exe'
            if (-not (Test-Path -LiteralPath $DirectInstallerPath `
                    -PathType Leaf)) {
                throw 'The static direct EFI installer artifact is missing.'
            }
            $DirectInstallerBytes = [IO.File]::ReadAllBytes(
                $DirectInstallerPath)
        }
        $GeneratedGuestTest = Join-Path $RunRoot `
            'Invoke-WindowsGuestTest.ps1'
        $DirectLoaderCompressedBytes = [byte[]]@()
        $DirectEfiCompressedBytes = [byte[]]@()
        $DirectInstallerCompressedBytes = [byte[]]@()
        try {
            $DirectLoaderCompressedBytes =
                Compress-UhrArtifactBytes $DirectLoaderBytes
            $DirectEfiCompressedBytes =
                Compress-UhrArtifactBytes $DirectEfiBytes
            $DirectInstallerCompressedBytes =
                Compress-UhrArtifactBytes $DirectInstallerBytes
            $DirectCompanionDriverCompressedBytes =
                Compress-UhrArtifactBytes $DirectCompanionDriverBytes
            $DirectCompanionServiceCompressedBytes =
                Compress-UhrArtifactBytes $DirectCompanionServiceBytes
            $GeneratedGuestTestText = $GuestTestTemplate.Replace($Placeholder,
                [Convert]::ToBase64String($NestedScriptBytes)).Replace(
                $ServicePlaceholder,
                [Convert]::ToBase64String($NestedServiceBytes)).Replace(
                $DirectModePlaceholder, $DirectMode).Replace(
                $DirectLoaderPlaceholder,
                [Convert]::ToBase64String(
                    $DirectLoaderCompressedBytes)).Replace(
                $DirectEfiPlaceholder,
                [Convert]::ToBase64String(
                    $DirectEfiCompressedBytes)).Replace(
                $DirectInstallerPlaceholder,
                [Convert]::ToBase64String(
                    $DirectInstallerCompressedBytes)).Replace(
                $DirectStartupPlaceholder,
                [Convert]::ToBase64String($DirectStartupBytes)).Replace(
                $DirectCompanionDriverPlaceholder,
                [Convert]::ToBase64String(
                    $DirectCompanionDriverCompressedBytes)).Replace(
                $DirectCompanionServicePlaceholder,
                [Convert]::ToBase64String(
                    $DirectCompanionServiceCompressedBytes)).Replace(
                $DirectCompanionCertificatePlaceholder,
                [Convert]::ToBase64String(
                    $DirectCompanionCertificateBytes)).Replace(
                $DirectProcessorCountPlaceholder,
                [string]$WindowsDirectProcessorCount)
        }
        finally {
            if ($DirectLoaderBytes.Length -gt 0) {
                [Array]::Clear($DirectLoaderBytes, 0,
                    $DirectLoaderBytes.Length)
            }
            if ($DirectEfiBytes.Length -gt 0) {
                [Array]::Clear($DirectEfiBytes, 0, $DirectEfiBytes.Length)
            }
            if ($DirectInstallerBytes.Length -gt 0) {
                [Array]::Clear($DirectInstallerBytes, 0,
                    $DirectInstallerBytes.Length)
            }
            if ($DirectLoaderCompressedBytes.Length -gt 0) {
                [Array]::Clear($DirectLoaderCompressedBytes, 0,
                    $DirectLoaderCompressedBytes.Length)
            }
            if ($DirectEfiCompressedBytes.Length -gt 0) {
                [Array]::Clear($DirectEfiCompressedBytes, 0,
                    $DirectEfiCompressedBytes.Length)
            }
            if ($DirectInstallerCompressedBytes.Length -gt 0) {
                [Array]::Clear($DirectInstallerCompressedBytes, 0,
                    $DirectInstallerCompressedBytes.Length)
            }
            if ($DirectStartupBytes.Length -gt 0) {
                [Array]::Clear($DirectStartupBytes, 0,
                    $DirectStartupBytes.Length)
            }
            if ($DirectCompanionDriverBytes.Length -gt 0) {
                [Array]::Clear($DirectCompanionDriverBytes, 0,
                    $DirectCompanionDriverBytes.Length)
            }
            if ($DirectCompanionServiceBytes.Length -gt 0) {
                [Array]::Clear($DirectCompanionServiceBytes, 0,
                    $DirectCompanionServiceBytes.Length)
            }
            if ($DirectCompanionDriverCompressedBytes.Length -gt 0) {
                [Array]::Clear($DirectCompanionDriverCompressedBytes, 0,
                    $DirectCompanionDriverCompressedBytes.Length)
            }
            if ($DirectCompanionServiceCompressedBytes.Length -gt 0) {
                [Array]::Clear($DirectCompanionServiceCompressedBytes, 0,
                    $DirectCompanionServiceCompressedBytes.Length)
            }
            if ($DirectCompanionCertificateBytes.Length -gt 0) {
                [Array]::Clear($DirectCompanionCertificateBytes, 0,
                    $DirectCompanionCertificateBytes.Length)
            }
        }
        [IO.File]::WriteAllText($GeneratedGuestTest,
            $GeneratedGuestTestText,
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
        } elseif ([string]$File.Source -cmatch '^tools\\host-runner\\(.+)$') {
            Join-Path $PSScriptRoot $Matches[1]
        } else {
            Join-Path $UefiProjectRoot $File.Source
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

function Wait-WindowsVmxDiskReady {
    param([Parameter(Mandatory=$true)]$Selected)
    $VmxPath = [IO.Path]::GetFullPath([string]$Selected.VmxPath)
    $VmxRoot = Split-Path -Parent $VmxPath
    $Deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $Text = [IO.File]::ReadAllText($VmxPath)
        $Matches = [regex]::Matches($Text,
            '(?im)^nvme0:0\.fileName\s*=\s*"([^"\\/]+\.vmdk)"\s*$')
        if ($Matches.Count -ne 1) {
            throw 'The disposable Windows VMX must contain one local NVMe disk descriptor.'
        }
        $DiskPath = [IO.Path]::GetFullPath((Join-Path $VmxRoot `
            $Matches[0].Groups[1].Value))
        if (-not (Test-UhrPathWithin -Root $VmxRoot -Path $DiskPath)) {
            throw 'The disposable Windows VMX disk escaped its VM directory.'
        }
        if (Test-Path -LiteralPath $DiskPath -PathType Leaf) {
            return $DiskPath
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "VMware did not finish creating the reverted redo disk: $DiskPath"
}

function Set-WindowsDirectHypervisorTopology {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$EvidenceRoot,
        [ValidateSet(1, 4, 20)][int]$ProcessorCount = 20
    )
    if (Test-ConfiguredVmRunning $Selected) {
        throw 'The Windows VM must be powered off before applying direct EFI topology.'
    }
    $VmxPath = [IO.Path]::GetFullPath([string]$Selected.VmxPath)
    $ConfiguredPath = [IO.Path]::GetFullPath(
        [string]$Config.Profiles.Windows.VmxPath)
    if (-not $VmxPath.Equals(
            $ConfiguredPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Direct EFI topology may only modify the configured disposable Windows VM.'
    }
    [void](Wait-WindowsVmxDiskReady -Selected $Selected)
    $Text = [IO.File]::ReadAllText($VmxPath)
    foreach ($Pattern in @(
            '(?im)^firmware\s*=\s*"efi"\s*$',
            '(?im)^numvcpus\s*=\s*"[0-9]+"\s*$',
            '(?im)^cpuid\.coresPerSocket\s*=\s*"[0-9]+"\s*$',
            '(?im)^memsize\s*=\s*"[0-9]+"\s*$',
            '(?im)^vhv\.enable\s*=\s*"(?:TRUE|FALSE)"\s*$')) {
        if ([regex]::Matches($Text, $Pattern).Count -ne 1) {
            throw "The disposable Windows VMX lacks one exact direct EFI setting: $Pattern"
        }
    }
    if ([regex]::Matches($Text, '(?im)^serial1\.').Count -ne 0) {
        throw 'The disposable Windows VMX already defines serial1; refusing to replace it.'
    }
    New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
    Copy-Item -LiteralPath $VmxPath -Destination (Join-Path $EvidenceRoot `
        'vmx-before-direct-topology.vmx')
    $Text = [regex]::Replace($Text,
        '(?im)^numvcpus\s*=\s*"[0-9]+"\s*$',
        "numvcpus = `"$ProcessorCount`"")
    $Text = [regex]::Replace($Text,
        '(?im)^cpuid\.coresPerSocket\s*=\s*"[0-9]+"\s*$',
        "cpuid.coresPerSocket = `"$ProcessorCount`"")
    $Text = [regex]::Replace($Text,
        '(?im)^memsize\s*=\s*"[0-9]+"\s*$',
        "memsize = `"$WindowsDirectMemoryMib`"")
    $Text = [regex]::Replace($Text,
        '(?im)^vhv\.enable\s*=\s*"(?:TRUE|FALSE)"\s*$',
        'vhv.enable = "TRUE"')
    if ([regex]::Matches($Text,
            '(?im)^uefi\.secureBoot\.enabled\s*=').Count -eq 0) {
        $Text += "`r`nuefi.secureBoot.enabled = `"FALSE`"`r`n"
    }
    else {
        $Text = [regex]::Replace($Text,
            '(?im)^uefi\.secureBoot\.enabled\s*=\s*"(?:TRUE|FALSE)"\s*$',
            'uefi.secureBoot.enabled = "FALSE"')
    }
    $Text += @"
serial1.present = "TRUE"
serial1.fileType = "pipe"
serial1.fileName = "\\.\pipe\uefiloader_direct_result"
serial1.pipe.endPoint = "server"
serial1.tryNoRxLoss = "FALSE"
"@
    $Temporary = $VmxPath + '.direct-topology.tmp'
    try {
        [IO.File]::WriteAllText($Temporary, $Text,
            [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $Temporary -Destination $VmxPath -Force
    }
    finally {
        if (Test-Path -LiteralPath $Temporary -PathType Leaf) {
            Remove-Item -LiteralPath $Temporary -Force
        }
    }
    $Verified = [IO.File]::ReadAllText($VmxPath)
    foreach ($Expected in @(
            "numvcpus = `"$ProcessorCount`"",
            "cpuid.coresPerSocket = `"$ProcessorCount`"",
            "memsize = `"$WindowsDirectMemoryMib`"",
            'vhv.enable = "TRUE"', 'uefi.secureBoot.enabled = "FALSE"',
            'serial1.present = "TRUE"', 'serial1.fileType = "pipe"',
            'serial1.fileName = "\\.\pipe\uefiloader_direct_result"',
            'serial1.pipe.endPoint = "server"',
            'serial1.tryNoRxLoss = "FALSE"')) {
        if ($Verified -cnotmatch (
                '(?m)^' + [regex]::Escape($Expected) + '\r?$')) {
            throw "Direct EFI VMX topology verification failed: $Expected"
        }
    }
    [IO.File]::WriteAllText((Join-Path $EvidenceRoot `
        'direct-topology.txt'),
        "processors=$ProcessorCount`r`ncores_per_socket=$ProcessorCount`r`nmemory_mib=$WindowsDirectMemoryMib`r`nnested_vmx=TRUE`r`nsecure_boot=FALSE`r`ndirect_result_pipe=serial1`r`n",
        [Text.ASCIIEncoding]::new())
}

function Enable-WindowsUpdateNetwork {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$EvidenceRoot
    )
    $VmxPath = [IO.Path]::GetFullPath([string]$Selected.VmxPath)
    $Original = [IO.File]::ReadAllText($VmxPath)
    $Present = [regex]::Matches(
        $Original, '(?im)^ethernet0\.present\s*=\s*"(?:TRUE|FALSE)"\s*$')
    if ($Present.Count -ne 1) {
        throw 'The Windows update VMX must contain exactly one ethernet0.present entry.'
    }
    $Backup = Join-Path $EvidenceRoot 'windows-update-network.vmx.backup'
    if (Test-Path -LiteralPath $Backup) {
        throw 'The Windows update VMX backup already exists.'
    }
    [IO.File]::WriteAllText($Backup, $Original,
        [Text.UTF8Encoding]::new($false))
    $Updated = [regex]::Replace($Original,
        '(?im)^ethernet0\.present\s*=\s*"(?:TRUE|FALSE)"\s*$',
        'ethernet0.present = "TRUE"')
    foreach ($Entry in @(
            @{ Pattern = '(?im)^ethernet0\.connectionType\s*=.*$'; Text = 'ethernet0.connectionType = "nat"' },
            @{ Pattern = '(?im)^ethernet0\.startConnected\s*=.*$'; Text = 'ethernet0.startConnected = "TRUE"' },
            @{ Pattern = '(?im)^ethernet0\.virtualDev\s*=.*$'; Text = 'ethernet0.virtualDev = "e1000e"' })) {
        if ([regex]::IsMatch($Updated, $Entry.Pattern)) {
            $Updated = [regex]::Replace($Updated, $Entry.Pattern, $Entry.Text)
        }
        else {
            $Updated = $Updated.TrimEnd("`r", "`n") + "`r`n" +
                $Entry.Text + "`r`n"
        }
    }
    $Temporary = $VmxPath + '.windows-update-network.tmp'
    try {
        [IO.File]::WriteAllText($Temporary, $Updated,
            [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $Temporary -Destination $VmxPath -Force
    }
    finally {
        if (Test-Path -LiteralPath $Temporary) {
            Remove-Item -LiteralPath $Temporary -Force
        }
    }
    $Verified = [IO.File]::ReadAllText($VmxPath)
    foreach ($Expected in @(
            'ethernet0.present = "TRUE"',
            'ethernet0.connectionType = "nat"',
            'ethernet0.startConnected = "TRUE"',
            'ethernet0.virtualDev = "e1000e"')) {
        if ($Verified -cnotmatch ('(?m)^' + [regex]::Escape($Expected) + '\r?$')) {
            throw "Windows update NAT verification failed: $Expected"
        }
    }
}

function Restore-WindowsUpdateNetwork {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$EvidenceRoot
    )
    $Backup = Join-Path $EvidenceRoot 'windows-update-network.vmx.backup'
    if (-not (Test-Path -LiteralPath $Backup -PathType Leaf)) { return }
    if (Test-ConfiguredVmRunning $Selected) {
        throw 'Refuse to restore the Windows update VMX while the VM is running.'
    }
    $Original = [IO.File]::ReadAllText($Backup)
    if ($Original -cnotmatch '(?im)^ethernet0\.present\s*=\s*"FALSE"\s*$') {
        throw 'The saved Windows update VMX does not prove the adapter was disabled.'
    }
    $VmxPath = [IO.Path]::GetFullPath([string]$Selected.VmxPath)
    $Current = [IO.File]::ReadAllText($VmxPath)
    foreach ($Name in @('present', 'connectionType', 'startConnected',
            'virtualDev')) {
        $Pattern = '(?im)^ethernet0\.' + [regex]::Escape($Name) +
            '\s*=.*(?:\r?\n|$)'
        $OriginalMatches = [regex]::Matches($Original, $Pattern)
        $CurrentMatches = [regex]::Matches($Current, $Pattern)
        if ($OriginalMatches.Count -gt 1 -or $CurrentMatches.Count -gt 1) {
            throw "The Windows update VMX has duplicate ethernet0.$Name entries."
        }
        if ($OriginalMatches.Count -eq 0) {
            $Current = [regex]::Replace($Current, $Pattern, '')
        }
        elseif ($CurrentMatches.Count -eq 1) {
            $Current = [regex]::Replace($Current, $Pattern,
                $OriginalMatches[0].Value)
        }
        else {
            $Current = $Current.TrimEnd("`r", "`n") + "`r`n" +
                $OriginalMatches[0].Value.TrimEnd("`r", "`n") + "`r`n"
        }
    }
    $Temporary = $VmxPath + '.windows-update-network-restore.tmp'
    try {
        [IO.File]::WriteAllText($Temporary, $Current,
            [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $Temporary -Destination $VmxPath -Force
    }
    finally {
        if (Test-Path -LiteralPath $Temporary) {
            Remove-Item -LiteralPath $Temporary -Force
        }
    }
    if ([IO.File]::ReadAllText($VmxPath) -cnotmatch
        '(?im)^ethernet0\.present\s*=\s*"FALSE"\s*$') {
        throw 'Windows update network restoration did not disable the adapter.'
    }
}

function Start-ConfiguredVm {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$RunId,
        [switch]$DirectHypervisorTopology,
        [switch]$WindowsUpdateNetwork,
        [string]$EvidenceRoot
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
        if ($DirectHypervisorTopology) {
            Assert-HostCapacity $Selected `
                -VmMemoryMibOverride $WindowsDirectMemoryMib
        }
        else {
            Assert-HostCapacity $Selected
        }
        if (-not [string]::IsNullOrWhiteSpace([string]$Selected.SnapshotName)) {
            Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                'revertToSnapshot', [string]$Selected.VmxPath,
                [string]$Selected.SnapshotName) | Out-Null
        }
        if ($DirectHypervisorTopology) {
            if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
                throw 'Direct EFI topology requires an evidence root.'
            }
            Set-WindowsDirectHypervisorTopology -Selected $Selected `
                -EvidenceRoot $EvidenceRoot `
                -ProcessorCount $WindowsDirectProcessorCount
        }
        if ($WindowsUpdateNetwork) {
            if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
                throw 'Windows update networking requires an evidence root.'
            }
            Enable-WindowsUpdateNetwork -Selected $Selected `
                -EvidenceRoot $EvidenceRoot
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

function Restore-WindowsDirectHypervisorTopology {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$EvidenceRoot
    )
    if (Test-ConfiguredVmRunning $Selected) {
        throw 'Refusing to restore the Windows VMX while the VM is running.'
    }
    $Backup = Join-Path $EvidenceRoot 'vmx-before-direct-topology.vmx'
    if (-not (Test-UhrPathWithin -Root $Config.StateRoot -Path $Backup) -or
        -not (Test-Path -LiteralPath $Backup -PathType Leaf)) {
        throw 'The direct EFI VMX backup is missing or outside runner state.'
    }
    $VmxPath = [IO.Path]::GetFullPath([string]$Selected.VmxPath)
    [void](Wait-WindowsVmxDiskReady -Selected $Selected)
    $Baseline = [IO.File]::ReadAllText($Backup)
    $Current = [IO.File]::ReadAllText($VmxPath)
    foreach ($Pattern in @(
            '(?im)^numvcpus\s*=\s*"[0-9]+"\s*$',
            '(?im)^cpuid\.coresPerSocket\s*=\s*"[0-9]+"\s*$',
            '(?im)^vhv\.enable\s*=\s*"(?:TRUE|FALSE)"\s*$')) {
        $BaselineMatch = [regex]::Matches($Baseline, $Pattern)
        $CurrentMatch = [regex]::Matches($Current, $Pattern)
        if ($BaselineMatch.Count -ne 1 -or $CurrentMatch.Count -ne 1) {
            throw "The direct EFI topology baseline is ambiguous: $Pattern"
        }
        $Current = [regex]::Replace($Current, $Pattern,
            $BaselineMatch[0].Value.TrimEnd("`r", "`n"))
    }
    $SecurePattern =
        '(?im)^uefi\.secureBoot\.enabled\s*=\s*"(?:TRUE|FALSE)"\s*\r?\n?'
    $BaselineSecure = [regex]::Matches($Baseline, $SecurePattern)
    if ($BaselineSecure.Count -gt 1) {
        throw 'The direct EFI Secure Boot baseline is ambiguous.'
    }
    $Current = [regex]::Replace($Current, $SecurePattern, '')
    if ($BaselineSecure.Count -eq 1) {
        $Current += "`r`n" +
            $BaselineSecure[0].Value.TrimEnd([char[]]@("`r", "`n")) + "`r`n"
    }
    $Temporary = $VmxPath + '.direct-restore.tmp'
    try {
        [IO.File]::WriteAllText($Temporary, $Current,
            [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $Temporary -Destination $VmxPath -Force
    }
    finally {
        if (Test-Path -LiteralPath $Temporary -PathType Leaf) {
            Remove-Item -LiteralPath $Temporary -Force
        }
    }
    $Restored = [IO.File]::ReadAllText($VmxPath)
    foreach ($Pattern in @(
            '(?im)^numvcpus\s*=\s*"[0-9]+"\s*$',
            '(?im)^cpuid\.coresPerSocket\s*=\s*"[0-9]+"\s*$',
            '(?im)^vhv\.enable\s*=\s*"(?:TRUE|FALSE)"\s*$')) {
        $BaselineMatch = [regex]::Matches($Baseline, $Pattern)
        $RestoredMatch = [regex]::Matches($Restored, $Pattern)
        if ($BaselineMatch.Count -ne 1 -or $RestoredMatch.Count -ne 1 -or
            -not [string]::Equals(
                $BaselineMatch[0].Value.Trim(),
                $RestoredMatch[0].Value.Trim(),
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "The direct EFI topology restoration does not match its saved baseline: $Pattern"
        }
    }
    $RestoredSecure = [regex]::Matches($Restored, $SecurePattern)
    if ($RestoredSecure.Count -ne $BaselineSecure.Count -or
        ($BaselineSecure.Count -eq 1 -and -not [string]::Equals(
            $BaselineSecure[0].Value.Trim(),
            $RestoredSecure[0].Value.Trim(),
            [StringComparison]::OrdinalIgnoreCase))) {
        throw 'The direct EFI Secure Boot restoration does not match its saved baseline.'
    }
    if ([regex]::IsMatch($Restored, '(?im)^serial1\.')) {
        throw 'The temporary direct EFI result pipe remained after recovery.'
    }
}

function Request-WindowsDirectStageTwoBoot {
    param([Parameter(Mandatory=$true)]$Selected)
    $ResetRequestedUtc = [DateTime]::UtcNow
    # VMware can deliver the guest's graceful reboot request and still leave
    # vmrun waiting past its CLI deadline. The authenticated stage-two marker,
    # not this transport acknowledgment, is the authoritative completion.
    try {
        Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
            'reset', [string]$Selected.VmxPath, 'soft') `
            -TimeoutSeconds 30 -AllowFailure | Out-Null
    }
    catch {
        if ($_.Exception.Message -cne 'vmrun operation timed out: reset') {
            throw
        }
    }
    return $ResetRequestedUtc
}

function Start-WindowsDirectHypervisorBoot {
    param([Parameter(Mandatory=$true)]$Selected)
    $ResetRequestedUtc = [DateTime]::UtcNow
    try {
        Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
            'reset', [string]$Selected.VmxPath, 'hard') `
            -TimeoutSeconds 30 | Out-Null
    }
    catch {
        try {
            Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                'stop', [string]$Selected.VmxPath, 'hard') `
                -TimeoutSeconds 30 -AllowFailure | Out-Null
        }
        catch {}
        Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
            'start', [string]$Selected.VmxPath, 'nogui') `
            -TimeoutSeconds 30 | Out-Null
    }
    return $ResetRequestedUtc
}

function Save-WindowsDirectHypervisorFailureEvidence {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)][string]$EvidenceRoot
    )
    $Evidence = [IO.Path]::GetFullPath($EvidenceRoot)
    if (-not (Test-UhrPathWithin -Root $Config.StateRoot -Path $Evidence) -or
        -not (Test-Path -LiteralPath $Evidence -PathType Container)) {
        throw 'Direct EFI failure evidence root escaped the runner state root.'
    }
    $ScreenPath = Join-Path $Evidence 'direct-failure-screen.png'
    $Screen = Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
        'captureScreen', [string]$Selected.VmxPath, $ScreenPath) `
        -TimeoutSeconds 30 -AllowFailure
    [IO.File]::WriteAllText(
        (Join-Path $Evidence 'direct-failure-capture-screen.txt'),
        ("exit={0}`r`nstdout={1}`r`nstderr={2}`r`n" -f
            $Screen.ExitCode, $Screen.Stdout.Trim(), $Screen.Stderr.Trim()),
        [Text.UTF8Encoding]::new($false))
    $VmxText = Get-Content -Raw -LiteralPath ([string]$Selected.VmxPath)
    $VncEnabled = [regex]::Matches($VmxText,
        '(?im)^\s*RemoteDisplay\.vnc\.enabled\s*=\s*"TRUE"\s*$')
    $VncLocal = [regex]::Matches($VmxText,
        '(?im)^\s*RemoteDisplay\.vnc\.ip\s*=\s*"127\.0\.0\.1"\s*$')
    $VncPortMatches = [regex]::Matches($VmxText,
        '(?im)^\s*RemoteDisplay\.vnc\.port\s*=\s*"([0-9]{1,5})"\s*$')
    if ($VncEnabled.Count -eq 1 -and $VncLocal.Count -eq 1 -and
        $VncPortMatches.Count -eq 1) {
        $VncPort = 0
        if ([int]::TryParse($VncPortMatches[0].Groups[1].Value,
                [ref]$VncPort) -and $VncPort -ge 1 -and
            $VncPort -le 65535) {
            try {
                & (Join-Path $PSScriptRoot `
                    'provision\Capture-LocalVncScreen.ps1') `
                    -OutputPath (Join-Path $Evidence `
                        'direct-failure-vnc-screen.png') -Port $VncPort |
                    Out-Null
            }
            catch {
                [IO.File]::WriteAllText((Join-Path $Evidence `
                    'direct-failure-vnc-screen.txt'),
                    $_.Exception.Message,
                    [Text.UTF8Encoding]::new($false))
            }
        }
    }
    $VmwareLog = Join-Path (Split-Path -Parent ([string]$Selected.VmxPath)) `
        'vmware.log'
    if (Test-Path -LiteralPath $VmwareLog -PathType Leaf) {
        Copy-Item -LiteralPath $VmwareLog -Destination (Join-Path $Evidence `
            'direct-failure-vmware.log') -Force
    }
}

function Invoke-WindowsDirectHypervisorE2E {
    param([switch]$ScannerOnly)
    if (-not $AuthorizeLiveVmRun -or -not $AuthorizeVmStop -or
        -not $AuthorizeSnapshotRevert) {
        throw 'The direct Windows EFI gate requires live-run, VM-stop, and snapshot-revert authorization.'
    }
    if ($SkipBuild) {
        throw 'The direct Windows EFI gate always builds fresh artifacts; -SkipBuild is not allowed.'
    }
    $Selected = Get-ProfileConfig 'Windows'
    Assert-NotQuarantined 'Windows'
    Assert-ProfileCredentialsReady $Selected
    Assert-ConcurrentVmSetSafe $Selected
    if (-not (Test-VmwareGuestInfo $Selected)) {
        throw 'The direct Windows EFI gate requires the authenticated Guest Variables transport.'
    }
    if ([string]::IsNullOrWhiteSpace([string]$Selected.SnapshotName)) {
        throw 'The direct Windows EFI gate requires a disposable clean snapshot.'
    }
    $Build = Invoke-CombinedHostBuild -PayloadReplayIndependent `
        -IncludeRuntimeCompanion
    $SetupRunId = New-UhrRunId 'windows-capture'
    $VerifyRunId = New-UhrRunId 'windows-probe'
    $SetupPaths = New-HostStage -RunId $SetupRunId `
        -Workflow WindowsDirectHypervisorSetup `
        -LoaderEfiPath ([string]$Build.LoaderEfiPath) `
        -HypervisorEfiPath ([string]$Build.HypervisorEfiPath) `
        -CompanionDriverPath ([string]$Build.CompanionDriverPath) `
        -CompanionServicePath ([string]$Build.CompanionServicePath) `
        -DirectVerifyRunId $VerifyRunId `
        -DirectScannerOnly:$ScannerOnly `
        -DirectProcessProfileEnabled:([bool]$Build.KernelProfileEnabled)
    $VerifyPaths = New-HostStage -RunId $VerifyRunId `
        -Workflow WindowsDirectHypervisorVerify
    $Lease = $null
    $SetupTransport = $null
    $SetupResponse = $null
    $DirectCapture = $null
    $DirectDebugCapture = $null
    $StageTwoMarkerVariable = $null
    $DirectResultTimeoutSeconds = [Math]::Min(600, [Math]::Max(
        180, [int]$Config.Policy.GuestReadyTimeoutSeconds))
    $LiveTouched = $false
    $Success = $false
    try {
        $Lease = Acquire-SharedLease -RunId $SetupRunId -Selected $Selected
        $LiveTouched = $true
        Start-ConfiguredVm -Selected $Selected -RunId $SetupRunId `
            -DirectHypervisorTopology -EvidenceRoot $SetupPaths.Evidence
        $SetupTransport = Publish-WindowsGuestInfoRequest `
            -Selected $Selected -Paths $SetupPaths -RunId $SetupRunId `
            -GuestAction Capture
        $SetupResponse = Wait-WindowsGuestInfoEvidence -Selected $Selected `
            -Transport $SetupTransport -Paths $SetupPaths -RunId $SetupRunId
        $SetupResult = Get-Content -Raw -LiteralPath (Join-Path `
            $SetupPaths.Evidence 'result.json') | ConvertFrom-Json
        if ($SetupResult.Status -cne 'PASS') {
            throw "Direct EFI setup reported $($SetupResult.Status): $($SetupResult.Message)"
        }
        $SetupStdout = Get-Content -Raw -LiteralPath (Join-Path `
            $SetupPaths.Evidence 'stdout.txt')
        $BitLockerMatches = [regex]::Matches($SetupStdout,
            '(?m)^DIRECT_HYPERVISOR_BITLOCKER_PROTECTORS=(SUSPENDED_2_REBOOTS|NOT_ACTIVE)\r?$')
        if ($BitLockerMatches.Count -ne 1) {
            throw 'Direct EFI setup did not report the BitLocker protection state exactly once.'
        }
        $BitLockerWasSuspended =
            $BitLockerMatches[0].Groups[1].Value -ceq
                'SUSPENDED_2_REBOOTS'
        Clear-VmwareGuestInfoVariables -Selected $Selected -VariableNames @(
            $SetupTransport.RequestVariableNames +
            $SetupResponse.ResponseVariableNames)
        $SetupTransport = $null
        $SetupResponse = $null

        $DirectCapture = Start-WindowsDirectResultCapture `
            -EvidenceRoot $VerifyPaths.Evidence
        $DirectDebugCapture = Start-WindowsDirectResultCapture `
            -EvidenceRoot $VerifyPaths.Evidence `
            -PipeName 'uefiloader_clone_debug' `
            -OutputName 'direct-debug-serial.log'
        $InitialResetRequestedUtc = Request-WindowsDirectStageTwoBoot `
            -Selected $Selected
        $StageTwoMarkerVariable =
            $script:VmwareGuestInfoDirectStage2Prefix + $VerifyRunId
        $FirstBoot = Wait-WindowsDirectStageTwoOrResult `
            -Selected $Selected -Capture $DirectCapture -RunId $VerifyRunId `
            -DebugCapture $DirectDebugCapture `
            -MinimumCompletedUtc $InitialResetRequestedUtc `
            -TimeoutSeconds $DirectResultTimeoutSeconds
        if ($FirstBoot.Kind -ceq 'DIRECT_RESULT') {
            $VerifyResult = $FirstBoot.DirectResult
            $StageTwoMarkerVariable = $null
        }
        elseif ($FirstBoot.Kind -ceq 'STAGE_TWO') {
            $StageTwo = $FirstBoot.StageTwo
            if ([string]$StageTwo.VariableName -cne
                $StageTwoMarkerVariable) {
                throw 'The authenticated direct stage-two marker name changed.'
            }
            Clear-VmwareGuestInfoVariables -Selected $Selected `
                -VariableNames @($StageTwoMarkerVariable)
            $StageTwoMarkerVariable = $null
            $ResetRequestedUtc = Start-WindowsDirectHypervisorBoot `
                -Selected $Selected
            $VerifyResult = Wait-WindowsDirectHypervisorResult `
                -Selected $Selected -Capture $DirectCapture `
                -DebugCapture $DirectDebugCapture `
                -RunId $VerifyRunId `
                -MinimumCompletedUtc $ResetRequestedUtc `
                -TimeoutSeconds $DirectResultTimeoutSeconds
        }
        else {
            throw 'The direct first-boot outcome is invalid.'
        }
        [IO.File]::WriteAllText((Join-Path $VerifyPaths.Evidence `
            'stdout.txt'), [string]$VerifyResult.Stdout,
            [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $VerifyPaths.Evidence `
            'stderr.txt'), [string]$VerifyResult.Stderr,
            [Text.UTF8Encoding]::new($false))
        if ($VerifyResult.Status -cne 'PASS') {
            # Preserve the guest's authenticated failure result for the
            # normal status check below. A failed guest does not have to
            # publish build metadata because the direct gate cannot pass.
            $VerifyBuildMetadata = $null
        }
        else {
            $VerifyBuildMetadata = Get-WindowsDirectBuildMetadata `
                -Stdout ([string]$VerifyResult.Stdout)
            if ($null -ne $Selected.PSObject.Properties[
                    'ExpectedWindowsBuild'] -and
                [uint64]$Selected.ExpectedWindowsBuild -ne
                    $VerifyBuildMetadata.WindowsBuild) {
                throw "Windows build mismatch: expected $($Selected.ExpectedWindowsBuild), received $($VerifyBuildMetadata.WindowsBuild)."
            }
            if ($null -ne $Selected.PSObject.Properties[
                    'ExpectedWindowsUbr'] -and
                [uint64]$Selected.ExpectedWindowsUbr -ne
                    $VerifyBuildMetadata.WindowsUbr) {
                throw "Windows UBR mismatch: expected $($Selected.ExpectedWindowsUbr), received $($VerifyBuildMetadata.WindowsUbr)."
            }
            $VerifyResult | Add-Member -NotePropertyName WindowsBuild `
                -NotePropertyValue $VerifyBuildMetadata.WindowsBuild
            $VerifyResult | Add-Member -NotePropertyName WindowsUbr `
                -NotePropertyValue $VerifyBuildMetadata.WindowsUbr
        }
        Write-UhrAtomicJson -Path (Join-Path $VerifyPaths.Evidence `
            'result.json') -Value $VerifyResult
        if ($VerifyResult.Status -cne 'PASS') {
            throw "Direct EFI verification reported $($VerifyResult.Status): $($VerifyResult.Message)"
        }
        $VerifyStdout = [string]$VerifyResult.Stdout
        $CpuPasses = [regex]::Matches($VerifyStdout,
            '(?m)^UEFI_HYPERVISOR_WINDOWS_CPU:[0-9]+:[0-9]+:PASS\r?$')
        $CommonProofMissing =
            $CpuPasses.Count -ne $WindowsDirectProcessorCount -or
            [regex]::Matches($VerifyStdout,
                '(?m)^UEFI_HYPERVISOR_WINDOWS_CPUID:PASS\r?$').Count -ne 1 -or
            $VerifyStdout -notmatch
                ("(?m)^DIRECT_HYPERVISOR_VERIFIED_PROCESSORS={0}\r?$" -f
                    $WindowsDirectProcessorCount) -or
            [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_RUNTIME_SCAN=PASS\r?$').Count -ne 1
        $ScannerProofMissing = $ScannerOnly -and
            [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_RUNTIME_SCAN_ONLY=PASS\r?$').Count -ne 1
        $ProcessProofMissing = [bool]$Build.KernelProfileEnabled -and
            ([regex]::Matches($VerifyStdout,
                '(?m)^HVR1_PROCESS_LOCATE=PASS\r?$').Count -ne 1 -or
             [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_DYNAMIC_ANALYSIS=PASS\r?$').Count -ne 1)
        $DetachProofMissing = -not $ScannerOnly -and (
            [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_CRASH_RECOVERY_PRIOR_BOOT=PASS\r?$').Count -ne 1 -or
            [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_POWER_REQUEST_HELD=PASS\r?$').Count -ne 1 -or
            [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_AUTH_INVALID_TAG_REJECTED=PASS\r?$').Count -ne 1 -or
            [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_AUTH_VALID_SHUTDOWN=PASS\r?$').Count -ne 1 -or
            [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_AUTH_REPLAY_REJECTED=PASS\r?$').Count -ne 1 -or
            [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_POWER_REQUEST_RELEASED_AFTER_OFFLINE=PASS\r?$').Count -ne 1 -or
            [regex]::Matches($VerifyStdout,
                ("(?m)^HVR1_AUTH_SELFTEST=PASS cpus={0}/{0} generation=[1-9][0-9]*\r?$" -f
                    $WindowsDirectProcessorCount)).Count -ne 1 -or
            [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_POST_DETACH_CPUID_ABSENT=PASS\r?$').Count -ne 1 -or
            ($BitLockerWasSuspended -and
                [regex]::Matches($VerifyStdout,
                    '(?m)^HVR1_BITLOCKER_PROTECTORS_RESTORED=PASS\r?$').Count -ne 1) -or
            [regex]::Matches($VerifyStdout,
                '(?m)^HVR1_COMPANION_UNLOADED=PASS\r?$').Count -ne 1)
        if ($CommonProofMissing -or $ScannerProofMissing -or
            $ProcessProofMissing -or
            $DetachProofMissing) {
            throw "Host validation did not find the complete $WindowsDirectProcessorCount-processor health and authenticated runtime proof."
        }
        Stop-WindowsDirectResultCapture $DirectCapture
        $DirectCapture = $null
        Stop-WindowsDirectResultCapture $DirectDebugCapture
        $DirectDebugCapture = $null
        Stop-ConfiguredVmAfterPass $Selected
        Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
            'revertToSnapshot', [string]$Selected.VmxPath,
            [string]$Selected.SnapshotName) | Out-Null
        Restore-WindowsDirectHypervisorTopology -Selected $Selected `
            -EvidenceRoot $SetupPaths.Evidence
        Release-SharedLease $Lease
        $Lease = $null
        $Success = $true
        return [pscustomobject]@{
            Result = 'PASS'
            Workflow = $(if ($ScannerOnly) {
                    'WindowsDirectScannerE2E'
                } else {
                    'WindowsDirectHypervisorE2E'
                })
            ProcessorCount = $WindowsDirectProcessorCount
            SetupRunId = $SetupRunId
            SetupEvidence = $SetupPaths.Evidence
            VerifyRunId = $VerifyRunId
            VerifyEvidence = $VerifyPaths.Evidence
        }
    }
    finally {
        if (-not [string]::IsNullOrWhiteSpace($StageTwoMarkerVariable)) {
            try {
                Clear-VmwareGuestInfoVariables -Selected $Selected `
                    -VariableNames @($StageTwoMarkerVariable)
            }
            catch {}
        }
        if ($null -ne $SetupTransport) {
            try {
                $Names = @($SetupTransport.RequestVariableNames)
                if ($null -ne $SetupResponse) {
                    $Names += @($SetupResponse.ResponseVariableNames)
                }
                Clear-VmwareGuestInfoVariables -Selected $Selected `
                    -VariableNames $Names
            }
            catch {}
        }
        Stop-WindowsDirectResultCapture $DirectCapture
        $DirectCapture = $null
        Stop-WindowsDirectResultCapture $DirectDebugCapture
        $DirectDebugCapture = $null
        if (-not $Success -and $LiveTouched) {
            try {
                Save-WindowsDirectHypervisorFailureEvidence `
                    -Selected $Selected -EvidenceRoot $SetupPaths.Evidence
                if (Test-ConfiguredVmRunning $Selected) {
                    $StopResult = Invoke-ConfiguredVmrun -Selected $Selected `
                        -Operation @('stop', [string]$Selected.VmxPath, 'hard') `
                        -AllowFailure
                    # A guest may complete a reboot/power-off between the
                    # running-state probe and vmrun stop.  That is already the
                    # required stopped state; fail only if it is still live.
                    if (($StopResult.ExitCode -ne 0 -or $StopResult.TimedOut) -and
                        (Test-ConfiguredVmRunning $Selected)) {
                        $Detail = ($StopResult.Stdout + "`n" +
                            $StopResult.Stderr).Trim()
                        throw "Windows direct VM stop failed while the VM remained running: $Detail"
                    }
                }
                if (Test-ConfiguredVmRunning $Selected) {
                    throw 'The Windows direct VM remained running before snapshot recovery.'
                }
                Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                    'revertToSnapshot', [string]$Selected.VmxPath,
                    [string]$Selected.SnapshotName) | Out-Null
                Restore-WindowsDirectHypervisorTopology -Selected $Selected `
                    -EvidenceRoot $SetupPaths.Evidence
            }
            catch {
                Write-Warning "Direct EFI VM recovery failed: $($_.Exception.Message)"
            }
        }
        if ($null -ne $Lease) {
            Release-SharedLease $Lease
        }
    }
}

function Remove-SupersededPreparedDisks {
    param([Parameter(Mandatory=$true)][string]$KeepPath)
    $Root = Get-NestedPreparedDiskRoot
    $RootItem = Get-Item -LiteralPath $Root -ErrorAction Stop
    if (($RootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Prepared-disk retention root may not be a reparse point.'
    }
    $Keep = [IO.Path]::GetFullPath($KeepPath)
    if (-not (Test-UhrPathWithin -Root $Root -Path $Keep) -or
        -not (Test-Path -LiteralPath $Keep -PathType Leaf)) {
        throw 'Prepared-disk retention target escaped its fixed root or is missing.'
    }
    $Removed = @()
    foreach ($Candidate in Get-ChildItem -LiteralPath $Root -File `
            -Filter 'uefi-windows-capture-*.vmdk') {
        if ($Candidate.FullName.Equals(
                $Keep, [StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($Candidate.BaseName -cnotmatch
            '^uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$' -or
            ($Candidate.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            -not $Candidate.Directory.FullName.Equals(
                $Root, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refuse unsafe prepared-disk retention candidate: $($Candidate.FullName)"
        }
        Remove-Item -LiteralPath $Candidate.FullName -Force
        $Removed += $Candidate.Name
    }
    return @($Removed)
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
    $RemovedDisks = @(Remove-SupersededPreparedDisks -KeepPath $DestinationDisk)
    return [pscustomobject]@{
        Path = $DestinationDisk
        Bytes = $DiskItem.Length
        Source = $SourceDisk
        RemovedSupersededDisks = $RemovedDisks
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
        $Create = @('createDirectoryInGuest', [string]$Selected.VmxPath,
            $GuestRunRoot)
        $Separator = '/'
    }
    else {
        $CreateScript = "New-Item -ItemType Directory -Force -LiteralPath '$GuestRunRoot' | Out-Null"
        $Create = @('runScriptInGuest', [string]$Selected.VmxPath,
            'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
            $CreateScript)
        $Separator = '\'
    }
    $CreateResult = Invoke-ConfiguredVmrun -Selected $Selected `
        -GuestAuthentication -Operation $Create -AllowFailure
    if ($CreateResult.ExitCode -ne 0) {
        if ($Name -eq 'Linux') {
            $RunnerRoot = [string]$Selected.GuestStageRoot
            $CacheRoot = $RunnerRoot.Substring(0, $RunnerRoot.LastIndexOf('/'))
            $RunsRoot = $RunnerRoot.TrimEnd('/') + '/runs'
            $TargetExists = Invoke-ConfiguredVmrun -Selected $Selected `
                -GuestAuthentication -AllowFailure -Operation @(
                    'runProgramInGuest', [string]$Selected.VmxPath,
                    '/usr/bin/test', '-d', $GuestRunRoot)
            $TargetWritable = Invoke-ConfiguredVmrun -Selected $Selected `
                -GuestAuthentication -AllowFailure -Operation @(
                    'runProgramInGuest', [string]$Selected.VmxPath,
                    '/usr/bin/test', '-w', $GuestRunRoot)
            $TargetOwned = Invoke-ConfiguredVmrun -Selected $Selected `
                -GuestAuthentication -AllowFailure -Operation @(
                    'runProgramInGuest', [string]$Selected.VmxPath,
                    '/usr/bin/test', '-O', $GuestRunRoot)
            $TargetSymlink = Invoke-ConfiguredVmrun -Selected $Selected `
                -GuestAuthentication -AllowFailure -Operation @(
                    'runProgramInGuest', [string]$Selected.VmxPath,
                    '/usr/bin/test', '-L', $GuestRunRoot)
            if ($TargetExists.ExitCode -eq 0 -and
                $TargetWritable.ExitCode -eq 0 -and
                $TargetOwned.ExitCode -eq 0 -and
                $TargetSymlink.ExitCode -ne 0) {
                [IO.File]::WriteAllText((Join-Path $Paths.Evidence `
                    'linux-stage-create-warning.txt'),
                    "vmrun reported exit $($CreateResult.ExitCode), but the exact bounded guest directory was verified present, writable, owned, and not a symlink.`n",
                    [Text.UTF8Encoding]::new($false))
            }
            else {
            $RunLeaf = $GuestRunRoot.Substring(
                $GuestRunRoot.LastIndexOf('/') + 1)
            $ProbeSource = Join-Path $Paths.Stage 'SHA256SUMS.txt'
            $WriteStates = foreach ($Probe in @(
                    @{ Label = 'tmp-write'; Path =
                        "/tmp/uefiloader-write-probe-$RunLeaf.txt" },
                    @{ Label = 'runs-write'; Path =
                        "$RunsRoot/.uefiloader-write-probe-$RunLeaf.txt" })) {
                $Write = Invoke-ConfiguredVmrun -Selected $Selected `
                    -GuestAuthentication -AllowFailure -Operation @(
                        'CopyFileFromHostToGuest',
                        [string]$Selected.VmxPath, $ProbeSource,
                        [string]$Probe.Path)
                if ($Write.ExitCode -eq 0) {
                    Invoke-ConfiguredVmrun -Selected $Selected `
                        -GuestAuthentication -AllowFailure -Operation @(
                            'deleteFileInGuest', [string]$Selected.VmxPath,
                            [string]$Probe.Path) | Out-Null
                }
                $WriteDetail = ($Write.Stdout + ' ' + $Write.Stderr).Trim()
                if ($WriteDetail.Length -gt 160) {
                    $WriteDetail = $WriteDetail.Substring(0, 160)
                }
                '{0}:exit={1},detail={2}' -f $Probe.Label,
                    $Write.ExitCode, $WriteDetail
            }
            $States = foreach ($Probe in @(
                    @{ Label = 'home'; Path = '/home/' + [string]$Selected.GuestUser },
                    @{ Label = 'cache'; Path = $CacheRoot },
                    @{ Label = 'runner'; Path = $RunnerRoot },
                    @{ Label = 'runs'; Path = $RunsRoot },
                    @{ Label = 'target'; Path = $GuestRunRoot })) {
                $Exists = Invoke-ConfiguredVmrun -Selected $Selected `
                    -GuestAuthentication -AllowFailure -Operation @(
                        'runProgramInGuest', [string]$Selected.VmxPath,
                        '/usr/bin/test', '-d', [string]$Probe.Path)
                $Writable = Invoke-ConfiguredVmrun -Selected $Selected `
                    -GuestAuthentication -AllowFailure -Operation @(
                        'runProgramInGuest', [string]$Selected.VmxPath,
                        '/usr/bin/test', '-w', [string]$Probe.Path)
                '{0}:exists={1},writable={2}' -f $Probe.Label,
                    ($Exists.ExitCode -eq 0), ($Writable.ExitCode -eq 0)
            }
            throw ('Linux guest stage creation failed: ' +
                ($States -join '; ') + '; ' + ($WriteStates -join '; '))
            }
        }
        else {
            $Detail = ($CreateResult.Stdout + "`n" + $CreateResult.Stderr).Trim()
            throw "Windows guest stage creation failed: $Detail"
        }
    }
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

function Remove-LinuxGuestRunRoot {
    param(
        [Parameter(Mandatory=$true)]$Selected,
        [Parameter(Mandatory=$true)]$Paths,
        [Parameter(Mandatory=$true)][string]$GuestRunRoot
    )
    $GuestStageRoot = ([string]$Selected.GuestStageRoot).TrimEnd('/')
    $ExpectedPrefix = $GuestStageRoot + '/runs/'
    $GuestRunName = Split-Path -Leaf $GuestRunRoot
    if (-not $GuestRunRoot.StartsWith(
            $ExpectedPrefix, [StringComparison]::Ordinal) -or
        $GuestRunName -cnotmatch
            '^uefi-linux-[a-z0-9-]+-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw 'Refuse unsafe Linux guest run-root cleanup target.'
    }
    $Cleanup = Invoke-ConfiguredVmrun -Selected $Selected `
        -GuestAuthentication -Operation @(
            'runProgramInGuest', [string]$Selected.VmxPath,
            '/bin/bash', "$GuestRunRoot/Cleanup-LinuxGuestRun.sh",
            $GuestRunRoot) -AllowFailure
    if ($Cleanup.ExitCode -ne 0) {
        [IO.File]::WriteAllText(
            (Join-Path $Paths.Evidence 'linux-guest-cleanup-warning.txt'),
            (($Cleanup.Stdout + "`n" + $Cleanup.Stderr).Trim() + "`n"),
            [Text.UTF8Encoding]::new($false))
    }
}

function Stop-WindowsExactBuildUpdateVm {
    param([Parameter(Mandatory=$true)]$Selected)
    if (-not $AuthorizeVmStop) {
        throw 'Exact Windows build servicing requires -AuthorizeVmStop.'
    }
    Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
        'stop', [string]$Selected.VmxPath, 'hard') | Out-Null
    if (Test-ConfiguredVmRunning $Selected) {
        throw 'The exact Windows build servicing VM remained running after stop.'
    }
}

function Resume-WindowsExactBuildUpdateVm {
    param([Parameter(Mandatory=$true)]$Selected)
    if (-not $AuthorizeLiveVmRun) {
        throw 'Resuming the exact Windows build reboot requires -AuthorizeLiveVmRun.'
    }
    if (Test-ConfiguredVmRunning $Selected) { return }
    Assert-ConcurrentVmSetSafe $Selected
    Assert-HostCapacity $Selected
    # This is deliberately not Start-ConfiguredVm: servicing must continue
    # from the current update delta, never from the configured clean snapshot.
    Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
        'start', [string]$Selected.VmxPath, 'nogui') | Out-Null
    $Deadline = [DateTime]::UtcNow.AddSeconds(
        [int]$Config.Policy.GuestReadyTimeoutSeconds)
    do {
        Start-Sleep -Seconds 2
        $Tools = Invoke-ConfiguredVmrun -Selected $Selected `
            -Operation @('checkToolsState', [string]$Selected.VmxPath) `
            -TimeoutSeconds 20 -AllowFailure
        if ($Tools.ExitCode -eq 0 -and
            ($Tools.Stdout + $Tools.Stderr).Trim() -match
                '^(running|installed)$') {
            return
        }
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw 'VMware Tools did not become ready after the exact-build servicing reboot.'
}

function Invoke-WindowsExactBuildUpdate {
    if (-not $AuthorizeLiveVmRun -or -not $AuthorizeVmStop -or
        -not $AuthorizeSnapshotRevert) {
        throw 'The exact Windows build workflow requires live-run, VM-stop, and snapshot-revert authorization.'
    }
    $Selected = Get-ProfileConfig 'Windows'
    Assert-NotQuarantined 'Windows'
    Assert-ProfileCredentialsReady $Selected
    Assert-ConcurrentVmSetSafe $Selected
    if (-not (Test-VmwareGuestInfo $Selected)) {
        throw 'The exact Windows build workflow requires the authenticated Guest Variables transport.'
    }
    if ([string]::IsNullOrWhiteSpace([string]$Selected.SnapshotName)) {
        throw 'The exact Windows build workflow requires a disposable clean snapshot.'
    }
    foreach ($Property in @('ExpectedWindowsBuild', 'ExpectedWindowsUbr')) {
        if ($null -ne $Selected.PSObject.Properties[$Property] -and
            [uint64]$Selected.$Property -ne $(if ($Property -ceq 'ExpectedWindowsBuild') {
                [uint64]26200
            } else { [uint64]9445 })) {
            throw "The configured $Property does not match the exact servicing target."
        }
    }
    # HostRunner.Core deliberately keeps its historical workflow ValidateSet;
    # derive the exact-update identity from that approved capture generator and
    # replace only the fixed, validated workflow segment.
    # The installed GuestInfo worker deliberately admits only its historical
    # probe/capture identities.  The staged script and authenticated status
    # schema distinguish this exact-build workflow without widening that TCB.
    $RunId = New-UhrRunId 'windows-capture'
    $Paths = New-HostStage -RunId $RunId -Workflow WindowsExactBuildUpdate
    $Lease = $null
    $Transport = $null
    $Response = $null
    $LiveTouched = $false
    $Success = $false
    try {
        $Lease = Acquire-SharedLease -RunId $RunId -Selected $Selected
        $LiveTouched = $true
        Start-ConfiguredVm -Selected $Selected -RunId $RunId `
            -WindowsUpdateNetwork -EvidenceRoot $Paths.Evidence
        Invoke-VmwareGuestInfoWrite -Selected $Selected `
            -VariableName $script:VmwareGuestInfoExactStatus `
            -Value $script:VmwareGuestInfoTombstone | Out-Null
        $Transport = Publish-WindowsGuestInfoRequest -Selected $Selected `
            -Paths $Paths -RunId $RunId -GuestAction Capture
        $Response = Wait-WindowsGuestInfoEvidence -Selected $Selected `
            -Transport $Transport -Paths $Paths -RunId $RunId -ExtendedPickup
        $InitialResult = Get-Content -Raw -LiteralPath (Join-Path `
            $Paths.Evidence 'result.json') | ConvertFrom-Json
        if ([string]$InitialResult.Status -cne 'PASS') {
            throw "Exact Windows build task setup reported $($InitialResult.Status): $($InitialResult.Message)"
        }
        # Prove that the separately scheduled SYSTEM task has published a
        # same-run authenticated status before removing its one-shot request.
        # This fails fast if the VMware GuestInfo return channel is broken.
        $InitialStatusResult = Wait-WindowsExactBuildStatus `
            -Selected $Selected -RunId $RunId -InitialOnly `
            -EvidenceRoot $Paths.Evidence
        # Remove the one-shot request before the delayed servicing task starts.
        # The task's boot trigger handles only a later Windows Update reboot.
        Clear-VmwareGuestInfoVariables -Selected $Selected -VariableNames @(
            $Transport.RequestVariableNames + $Response.ResponseVariableNames)
        $Transport = $null
        $Response = $null
        $StatusResult = Wait-WindowsExactBuildStatus -Selected $Selected `
            -RunId $RunId -SeedStatus $InitialStatusResult.Final `
            -EvidenceRoot $Paths.Evidence
        Write-UhrAtomicJson -Path (Join-Path $Paths.Evidence `
            'exact-build-status.json') -Value ([ordered]@{
                SchemaVersion = 1
                RunId = $RunId
                TargetUpdate = 'KB5124008'
                TargetBuild = 26200
                TargetUbr = 9445
                Final = $StatusResult.Final
                History = @($StatusResult.History)
                CompletedUtc = [DateTime]::UtcNow.ToString('o')
            })
        Clear-VmwareGuestInfoVariables -Selected $Selected -VariableNames @(
            $script:VmwareGuestInfoExactStatus)
        $Transport = $null
        $Response = $null
        Stop-WindowsExactBuildUpdateVm $Selected
        Restore-WindowsUpdateNetwork -Selected $Selected `
            -EvidenceRoot $Paths.Evidence
        Release-SharedLease $Lease
        $Lease = $null
        $Success = $true
        return [pscustomobject]@{
            Result = 'PASS'
            Workflow = 'WindowsExactBuildUpdate'
            RunId = $RunId
            TargetUpdate = 'KB5124008'
            WindowsBuild = 26200
            WindowsUbr = 9445
            Evidence = $Paths.Evidence
            SnapshotPreserved = [string]$Selected.SnapshotName
        }
    }
    finally {
        if (-not $Success -and $LiveTouched) {
            # GuestInfo has no arbitrary guest-execution capability.  Send a
            # same-run authenticated cancellation marker first; the updater
            # task consumes it at its bounded stage boundaries and unregisters
            # itself before the disposable snapshot is restored.
            try { Publish-WindowsExactCancellation -Selected $Selected -RunId $RunId }
            catch { Write-Warning "Exact Windows updater cancellation marker failed: $($_.Exception.Message)" }
        }
        if ($null -ne $Transport) {
            try {
                $Names = @($Transport.RequestVariableNames) +
                    $script:VmwareGuestInfoExactStatus
                if ($null -ne $Response) { $Names += @($Response.ResponseVariableNames) }
                Clear-VmwareGuestInfoVariables -Selected $Selected `
                    -VariableNames $Names
            }
            catch {}
        }
        if (-not $Success -and $LiveTouched) {
            $Recovered = $false
            try {
                if (Test-ConfiguredVmRunning $Selected) {
                    $StopResult = Invoke-ConfiguredVmrun -Selected $Selected `
                        -Operation @('stop', [string]$Selected.VmxPath, 'hard') `
                        -AllowFailure
                    # Windows servicing can complete its power transition
                    # after the probe but before vmrun stop.  Treat that race
                    # as success only when a fresh running-state check proves
                    # that the VM is now off.
                    if (($StopResult.ExitCode -ne 0 -or $StopResult.TimedOut) -and
                        (Test-ConfiguredVmRunning $Selected)) {
                        $Detail = ($StopResult.Stdout + "`n" +
                            $StopResult.Stderr).Trim()
                        throw "Exact Windows build VM stop failed while the VM remained running: $Detail"
                    }
                }
                if (Test-ConfiguredVmRunning $Selected) {
                    throw 'The exact Windows build VM remained running before snapshot recovery.'
                }
                Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                    'revertToSnapshot', [string]$Selected.VmxPath,
                    [string]$Selected.SnapshotName) | Out-Null
                if (Test-ConfiguredVmRunning $Selected) {
                    throw 'The exact Windows build VM remained running after failure recovery.'
                }
                $Recovered = $true
                Write-UhrAtomicJson -Path (Join-Path $Paths.Evidence `
                    'failure-recovery.json') -Value ([ordered]@{
                        SchemaVersion = 1; Profile = 'Windows'; RunId = $RunId
                        Result = 'RECOVERED'
                        Snapshot = [string]$Selected.SnapshotName
                        CompletedUtc = [DateTime]::UtcNow.ToString('o')
                    })
            }
            catch {
                Write-Warning "Exact Windows build VM recovery failed: $($_.Exception.Message)"
            }
            if (-not $Recovered) {
                $Quarantine = [ordered]@{
                    SchemaVersion = 1; Profile = 'Windows'; RunId = $RunId
                    Message = 'Exact Windows build servicing failed and deterministic snapshot recovery did not complete.'
                    LeaseResource = [string]$Selected.LeaseResource
                    RecoveryRunId = $RunId
                    CreatedUtc = [DateTime]::UtcNow.ToString('o')
                }
                if ($null -ne $Lease) {
                    $Quarantine.LeaseId = [string]$Lease.LeaseId
                }
                Write-UhrAtomicJson -Path (Get-QuarantinePath 'Windows') `
                    -Value $Quarantine
            }
        }
        if (-not (Test-ConfiguredVmRunning $Selected)) {
            try {
                Restore-WindowsUpdateNetwork -Selected $Selected `
                    -EvidenceRoot $Paths.Evidence
            }
            catch {
                Write-Warning "Windows update network restoration failed: $($_.Exception.Message)"
            }
        }
        if ($null -ne $Lease) { Release-SharedLease $Lease }
    }
}

function Invoke-WindowsExactBuildResume {
    if (-not $AuthorizeLiveVmRun -or -not $AuthorizeVmStop -or
        -not $AuthorizeSnapshotRevert -or -not $AuthorizeQuarantineClear) {
        throw 'Exact-build resume requires live-run, VM-stop, snapshot-revert, and quarantine-clear authorization.'
    }
    $Selected = Get-ProfileConfig 'Windows'
    Assert-ProfileCredentialsReady $Selected
    Assert-ConcurrentVmSetSafe $Selected
    if (-not (Test-VmwareGuestInfo $Selected)) {
        throw 'Exact-build resume requires the authenticated Guest Variables transport.'
    }
    if (Test-ConfiguredVmRunning $Selected) {
        throw 'The quarantined exact-build VM must be powered off before resume.'
    }
    $QuarantinePath = Get-QuarantinePath 'Windows'
    if (-not (Test-Path -LiteralPath $QuarantinePath -PathType Leaf)) {
        throw 'No Windows quarantine marker is available for exact-build resume.'
    }
    $Quarantine = Get-Content -Raw -LiteralPath $QuarantinePath |
        ConvertFrom-Json
    $RunId = [string]$Quarantine.RunId
    if ([int]$Quarantine.SchemaVersion -ne 1 -or
        [string]$Quarantine.Profile -cne 'Windows' -or
        $RunId -cnotmatch '^uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$' -or
        [string]$Quarantine.LeaseResource -cne
            [string]$Selected.LeaseResource) {
        throw 'Windows quarantine marker is not a valid exact-build recovery identity.'
    }
    $RunEvidencePath = Join-Path $Config.StateRoot "runs\$RunId\evidence"
    $EvidenceRoot = [IO.Path]::GetFullPath($RunEvidencePath)
    if (-not (Test-UhrPathWithin -Root $Config.StateRoot -Path $EvidenceRoot) -or
        -not (Test-Path -LiteralPath $EvidenceRoot -PathType Container)) {
        throw 'Exact-build recovery evidence root is missing or unsafe.'
    }
    $LivePath = Join-Path $EvidenceRoot 'exact-build-status-live.json'
    if (-not (Test-Path -LiteralPath $LivePath -PathType Leaf)) {
        throw 'Exact-build recovery lacks durable authenticated status evidence.'
    }
    $Live = Get-Content -Raw -LiteralPath $LivePath | ConvertFrom-Json
    if ([int]$Live.SchemaVersion -ne 1 -or
        [string]$Live.RunId -cne $RunId -or $null -eq $Live.Last) {
        throw 'Exact-build durable status evidence has an invalid identity.'
    }
    $MessageBytes = [Text.Encoding]::UTF8.GetBytes([string]$Live.Last.Message)
    try {
        $SeedRaw = [pscustomobject][ordered]@{
            SchemaVersion = [int]$Live.Last.SchemaVersion
            RunId = [string]$Live.Last.RunId
            State = [string]$Live.Last.State
            Package = [string]$Live.Last.Package
            WindowsBuild = [uint64]$Live.Last.WindowsBuild
            WindowsUbr = [uint64]$Live.Last.WindowsUbr
            RebootRequired = [bool]$Live.Last.RebootRequired
            MessageBase64 = [Convert]::ToBase64String($MessageBytes)
            UpdatedUtc = [string]$Live.Last.UpdatedUtc
            StatusHmacSha256 = [string]$Live.Last.StatusHmacSha256
        }
        $SeedStatus = ConvertFrom-WindowsExactStatus -Raw $SeedRaw `
            -RunId $RunId -Selected $Selected
    }
    finally { [Array]::Clear($MessageBytes, 0, $MessageBytes.Length) }
    if ($SeedStatus.State -notin @('INSTALLING', 'REBOOTING')) {
        throw "Exact-build recovery refuses to resume from authenticated state $($SeedStatus.State)."
    }

    $Lease = $null
    $Recovered = $false
    $Success = $false
    try {
        $LeaseStatus = & $Config.SharedLease.ScriptPath -Action Status `
            -Resource ([string]$Selected.LeaseResource)
        if ($LeaseStatus.Status -ceq 'ACTIVE') {
            throw "The retained exact-build lease remains active until $($LeaseStatus.ExpiresUtc)."
        }
        if ($LeaseStatus.Status -ceq 'EXPIRED') {
            if ($null -ne $Quarantine.PSObject.Properties['LeaseId']) {
                if ([string]$Quarantine.LeaseId -cne [string]$LeaseStatus.LeaseId) {
                    throw 'Expired exact-build lease identity does not match quarantine.'
                }
            }
            elseif ([string]$LeaseStatus.RunId -cne $RunId -or
                [string]$LeaseStatus.Agent -cne 'Codex-UEFILoaderHostRunner' -or
                [string]$LeaseStatus.Project -cne [string]$Config.ProjectRoot) {
                throw 'Legacy exact-build quarantine does not match the expired lease owner.'
            }
            $Lease = & $Config.SharedLease.ScriptPath -Action Acquire `
                -Resource ([string]$Selected.LeaseResource) `
                -Agent 'Codex-UEFILoaderHostRunner-Recovery' `
                -Project ([string]$Config.ProjectRoot) -RunId $RunId `
                -LeaseMinutes ([int]$Config.Policy.LeaseMinutes) `
                -TakeOverExpiredLease
        }
        elseif ($LeaseStatus.Status -ceq 'FREE') {
            $Lease = Acquire-SharedLease -RunId $RunId -Selected $Selected
        }
        else {
            throw "Unexpected exact-build retained lease status: $($LeaseStatus.Status)"
        }
        Remove-Item -LiteralPath $QuarantinePath -Force
        $StatusResult = Wait-WindowsExactBuildStatus -Selected $Selected `
            -RunId $RunId -SeedStatus $SeedStatus -EvidenceRoot $EvidenceRoot
        $FinalStatusPath = Join-Path $EvidenceRoot 'exact-build-status.json'
        Write-UhrAtomicJson -Path $FinalStatusPath -Value ([ordered]@{
                SchemaVersion = 1; RunId = $RunId
                TargetUpdate = 'KB5124008'; TargetBuild = 26200
                TargetUbr = 9445; Final = $StatusResult.Final
                History = @($StatusResult.History)
                ResumedFromQuarantine = $true
                CompletedUtc = [DateTime]::UtcNow.ToString('o')
            })
        Clear-VmwareGuestInfoVariables -Selected $Selected -VariableNames @(
            $script:VmwareGuestInfoExactStatus)
        Stop-WindowsExactBuildUpdateVm $Selected
        Restore-WindowsUpdateNetwork -Selected $Selected `
            -EvidenceRoot $EvidenceRoot
        $Success = $true
        return [pscustomobject]@{
            Result = 'PASS'; Workflow = 'WindowsExactBuildUpdateResume'
            RunId = $RunId; TargetUpdate = 'KB5124008'
            WindowsBuild = 26200; WindowsUbr = 9445
            Evidence = $EvidenceRoot
            SnapshotPreserved = [string]$Selected.SnapshotName
        }
    }
    finally {
        if (-not $Success -and $null -ne $Lease) {
            try {
                if (Test-ConfiguredVmRunning $Selected) {
                    try { Publish-WindowsExactCancellation -Selected $Selected `
                        -RunId $RunId } catch { }
                    $StopResult = Invoke-ConfiguredVmrun -Selected $Selected `
                        -Operation @('stop', [string]$Selected.VmxPath, 'hard') `
                        -AllowFailure
                    if (($StopResult.ExitCode -ne 0 -or $StopResult.TimedOut) -and
                        (Test-ConfiguredVmRunning $Selected)) {
                        throw 'The resumed exact-build VM could not be stopped.'
                    }
                }
                if (Test-ConfiguredVmRunning $Selected) {
                    throw 'The resumed exact-build VM remained running before snapshot recovery.'
                }
                Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                    'revertToSnapshot', [string]$Selected.VmxPath,
                    [string]$Selected.SnapshotName) | Out-Null
                Restore-WindowsUpdateNetwork -Selected $Selected `
                    -EvidenceRoot $EvidenceRoot
                $Recovered = $true
                $RecoveryPath = Join-Path $EvidenceRoot 'resume-failure-recovery.json'
                Write-UhrAtomicJson -Path $RecoveryPath -Value ([ordered]@{
                        SchemaVersion = 1; Profile = 'Windows'; RunId = $RunId
                        Result = 'RECOVERED'
                        Snapshot = [string]$Selected.SnapshotName
                        CompletedUtc = [DateTime]::UtcNow.ToString('o')
                    })
            }
            catch {
                Write-UhrAtomicJson -Path $QuarantinePath -Value ([ordered]@{
                    SchemaVersion = 1; Profile = 'Windows'; RunId = $RunId
                    Message = 'Resumed exact-build servicing failed and deterministic snapshot recovery did not complete.'
                    LeaseResource = [string]$Selected.LeaseResource
                    LeaseId = [string]$Lease.LeaseId
                    RecoveryRunId = $RunId
                    CreatedUtc = [DateTime]::UtcNow.ToString('o')
                })
            }
        }
        if ($null -ne $Lease -and ($Success -or $Recovered)) {
            Release-SharedLease $Lease
        }
    }
}

function Invoke-LiveWorkflow {
    param(
        [Parameter(Mandatory=$true)]
        [ValidateSet(
            'LinuxPositive', 'LinuxReplayDryRun', 'LinuxPeRehash',
            'LinuxHypervisorSmoke', 'LinuxHypervisorPostEbsSmoke',
            'LinuxHypervisorPostEbsInterruptSmoke',
            'LinuxDivergenceManifest', 'LinuxHandoffMechanics',
            'LinuxProductionHandoff', 'LinuxOneShotCycle',
            'LinuxNestedDiskProbe', 'LinuxNestedWindowsE2E',
            'LinuxNestedWindowsHypervisorE2E',
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
    elseif ($Workflow -eq 'LinuxHypervisorSmoke') {
        $Name = 'Linux'
        $WorkflowId = 'linux-hypervisor-smoke'
    }
    elseif ($Workflow -eq 'LinuxHypervisorPostEbsSmoke') {
        $Name = 'Linux'
        $WorkflowId = 'linux-hypervisor-post-ebs-smoke'
    }
    elseif ($Workflow -eq 'LinuxHypervisorPostEbsInterruptSmoke') {
        $Name = 'Linux'
        $WorkflowId = 'linux-hypervisor-post-ebs-interrupt-smoke'
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
    elseif ($Workflow -eq 'LinuxNestedWindowsHypervisorE2E') {
        $Name = 'Linux'
        $WorkflowId = 'linux-nested-windows-hypervisor-e2e'
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
    if ($Workflow -eq 'LinuxNestedWindowsHypervisorE2E' -and $SkipBuild) {
        throw 'LinuxNestedWindowsHypervisorE2E always builds fresh loader, hypervisor, and static Windows tools; -SkipBuild is not allowed.'
    }
    $HypervisorBuild = $null
    if (-not $SkipBuild) {
        if ($Workflow -in @('LinuxHypervisorSmoke',
                'LinuxHypervisorPostEbsSmoke',
                'LinuxHypervisorPostEbsInterruptSmoke',
                'LinuxNestedWindowsHypervisorE2E')) {
            $HypervisorBuild = Invoke-CombinedHostBuild
        } else {
            Invoke-HostBuild | Out-Host
        }
    }
    elseif ($Workflow -in @('LinuxHypervisorSmoke',
            'LinuxHypervisorPostEbsSmoke',
            'LinuxHypervisorPostEbsInterruptSmoke')) {
        $HypervisorBuild = [pscustomobject]@{
            HypervisorEfiPath = Get-HypervisorEfiArtifact -Name 'uefi_hypervisor.efi'
            HypervisorProbeEfiPath = Get-HypervisorEfiArtifact -Name 'uefi_hypervisor_probe.efi'
            HypervisorPostEbsProbeEfiPath = Get-HypervisorEfiArtifact `
                -Name 'uefi_hypervisor_post_ebs_probe.efi'
            HypervisorPostEbsInterruptProbeEfiPath = Get-HypervisorEfiArtifact `
                -Name 'uefi_hypervisor_post_ebs_interrupt_probe.efi'
        }
    }
    $RunId = New-UhrRunId $WorkflowId
    $StageParameters = @{ RunId = $RunId; Workflow = $Workflow }
    if ($Workflow -in @('LinuxHypervisorSmoke',
            'LinuxHypervisorPostEbsSmoke',
            'LinuxHypervisorPostEbsInterruptSmoke',
            'LinuxNestedWindowsHypervisorE2E')) {
        $StageParameters.HypervisorEfiPath = [string]$HypervisorBuild.HypervisorEfiPath
        if ($Workflow -eq 'LinuxHypervisorSmoke') {
            $StageParameters.HypervisorProbeEfiPath = `
                [string]$HypervisorBuild.HypervisorProbeEfiPath
        } elseif ($Workflow -eq 'LinuxHypervisorPostEbsSmoke') {
            $StageParameters.HypervisorPostEbsProbeEfiPath = `
                [string]$HypervisorBuild.HypervisorPostEbsProbeEfiPath
        } elseif ($Workflow -eq 'LinuxHypervisorPostEbsInterruptSmoke') {
            $StageParameters.HypervisorPostEbsInterruptProbeEfiPath = `
                [string]$HypervisorBuild.HypervisorPostEbsInterruptProbeEfiPath
        }
    }
    $Paths = New-HostStage @StageParameters
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
                'LinuxNestedWindowsE2E',
                'LinuxNestedWindowsHypervisorE2E')) {
            $PreparedDiskRoot = Get-NestedPreparedDiskRoot
            $NestedDisk = Get-LatestNestedPreparedDisk
            Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                'enableSharedFolders', [string]$Selected.VmxPath) | Out-Null
            Invoke-ConfiguredVmrun -Selected $Selected -Operation @(
                'removeSharedFolder', [string]$Selected.VmxPath,
                'UEFILoaderNestedE2E') -AllowFailure | Out-Null
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
                'LinuxHypervisorSmoke' { 'HypervisorSmoke' }
                'LinuxHypervisorPostEbsSmoke' { 'HypervisorPostEbsSmoke' }
                'LinuxHypervisorPostEbsInterruptSmoke' {
                    'HypervisorPostEbsInterruptSmoke'
                }
                'LinuxPeRehash' { 'PeRehash' }
                'LinuxDivergenceManifest' { 'DivergenceManifest' }
                'LinuxHandoffMechanics' { 'HandoffMechanics' }
                'LinuxProductionHandoff' { 'ProductionHandoff' }
                'LinuxOneShotCycle' { 'OneShotCycle' }
                'LinuxNestedDiskProbe' { 'NestedDiskProbe' }
                'LinuxNestedWindowsE2E' { 'NestedWindowsE2E' }
                'LinuxNestedWindowsHypervisorE2E' { 'NestedWindowsHypervisorE2E' }
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
            elseif ($Workflow -in @('LinuxNestedWindowsE2E',
                    'LinuxNestedWindowsHypervisorE2E')) {
                $GuestDisk = '/mnt/hgfs/UEFILoaderNestedE2E/' +
                    $NestedDisk.Name
                $Operation = @('runProgramInGuest',
                    [string]$Selected.VmxPath, '/bin/bash',
                    "$GuestRunRoot/Invoke-LinuxNestedWindowsE2E.sh",
                    $GuestRunRoot, $GuestDisk,
                    $(if ($Workflow -eq 'LinuxNestedWindowsHypervisorE2E') {
                        'Hypervisor'
                    } else { 'Replay' }))
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
                'hypervisor-order.txt',
                'hypervisor-post-ebs-order.txt',
                'hypervisor-post-ebs-interrupt-order.txt',
                'handoff-status.txt',
                'qemu-debugcon.log', 'qemu-serial.log', 'qemu-stderr.log',
                'qemu-exit-code.txt',
                'swtpm.log', 'pcrread-stderr.log', 'edk2-build.log',
                'windows-disk-info.json', 'windows-esp-map.txt',
                'windows-base-cache-info.json',
                'windows-base-cache-convert.log',
                'windows-base-cache-hit.txt',
                'windows-base-cache-published.txt')
            if ($Workflow -in @('LinuxNestedWindowsE2E',
                    'LinuxNestedWindowsHypervisorE2E')) {
                $EvidenceFiles += @(
                    'condition-result.json', 'conditioning-serial.log',
                    'conditioning-debugcon.log',
                    'conditioning-qemu-stderr.log',
                    'conditioning-fat-directory.txt',
                    'conditioning-swtpm.log',
                    'conditioning-swtpm-retry.log',
                    'conditioning-first-attempt-serial.log',
                    'conditioning-first-attempt-debugcon.log',
                    'conditioning-first-attempt-qemu-stderr.log',
                    'conditioning-post-result-qemu-exit.txt',
                    'conditioning-cache-hit.txt',
                    'conditioning-cache-published.txt',
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
                    'postboot-verify-pcrs.stderr.txt',
                    'postboot-progress.txt',
                    'postboot-fat-directory.txt',
                    'qemu-fatal-marker.txt',
                    'qemu-register-snapshots.txt',
                    'qemu-stall-marker.txt',
                    'qemu-screen.ppm',
                    'windows-replay-retry.txt',
                    'swtpm-retry.log',
                    'swtpm-retry-setup.log',
                    'first-attempt-qemu-debugcon.log',
                    'first-attempt-qemu-serial.log',
                    'first-attempt-qemu-stderr.log',
                    'first-attempt-qemu-exit-code.txt',
                    'first-attempt-qemu-register-snapshots.txt',
                    'first-attempt-qemu-screen.ppm',
                    'first-attempt-postboot-fat-directory.txt',
                    'nested-windows-hypervisor-order.txt')
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
                'bootmgfw.sha256', 'kernel-metadata.json')
        }
        if ($null -ne $Operation) {
            $GuestResult = Invoke-ConfiguredVmrun -Selected $Selected `
                -GuestAuthentication -Operation $Operation -AllowFailure `
                -TimeoutSeconds ([int]$Config.Policy.OperationTimeoutSeconds)
            Copy-GuestEvidence -Selected $Selected -Paths $Paths `
                -GuestRunRoot $GuestRunRoot -Name $Name -Files $EvidenceFiles
            if ($Name -eq 'Linux') {
                Remove-LinuxGuestRunRoot -Selected $Selected -Paths $Paths `
                    -GuestRunRoot $GuestRunRoot
            }
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
            RemovedSupersededPreparedDisks = if ($null -ne $PreparedDisk) {
                @($PreparedDisk.RemovedSupersededDisks)
            } else { @() }
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
    param(
        [switch]$BootNext,
        [switch]$Hypervisor
    )
    if ($BootNext -and $Hypervisor) {
        throw 'The WSL nested E2E gate cannot combine BootNext and hypervisor modes.'
    }
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
        'set -e; test -r /dev/kvm; test -w /dev/kvm; command -v git make gcc nasm iasl python3 qemu-system-x86_64 qemu-img swtpm swtpm_setup tpm2_pcrread truncate parted mformat mmd mcopy mdir >/dev/null; test -f /usr/include/uuid/uuid.h; test -s /usr/share/efi-shell-x64/shellx64.efi')
    if ($Preflight.TimedOut -or $Preflight.ExitCode -ne 0) {
        $PreflightDetail = ($Preflight.Stdout + "`n" +
            $Preflight.Stderr).Trim()
        throw "WSL nested-E2E prerequisites are not ready: $PreflightDetail"
    }
    $Existing = & $InvokeWsl -WslArguments @('/usr/bin/pgrep', '-a', '-f',
        'Invoke-LinuxNestedWindowsE2E.sh|^qemu-system-x86_64 |^swtpm ') 
    if (-not $Existing.TimedOut -and $Existing.ExitCode -eq 0 -and
        -not [string]::IsNullOrWhiteSpace($Existing.Stdout)) {
        throw "A WSL nested-E2E process already exists; refuse to attach: $($Existing.Stdout.Trim())"
    }

    $HypervisorBuild = $null
    if (-not $SkipBuild) {
        if ($Hypervisor) {
            $HypervisorBuild = Invoke-CombinedHostBuild
            $HypervisorBuild | Out-Host
        } else {
            Invoke-HostBuild | Out-Host
        }
    }
    elseif ($Hypervisor) {
        $HypervisorBuild = [pscustomobject]@{
            HypervisorEfiPath = Get-HypervisorEfiArtifact `
                -Name 'uefi_hypervisor.efi'
        }
    }
    $RunId = New-UhrRunId $(if ($BootNext) {
            'linux-bootnext-e2e'
        } elseif ($Hypervisor) {
            'linux-nested-windows-hypervisor-e2e'
        } else {
            'linux-nested-windows-e2e'
        })
    $StageParameters = @{
        RunId = $RunId
        Workflow = $(if ($BootNext) {
            'LinuxBootNextE2E'
        } elseif ($Hypervisor) {
            'LinuxNestedWindowsHypervisorE2E'
        } else {
            'LinuxNestedWindowsE2E'
        })
    }
    if ($Hypervisor) {
        $StageParameters.HypervisorEfiPath =
            [string]$HypervisorBuild.HypervisorEfiPath
    }
    $Paths = New-HostStage @StageParameters
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
            } elseif ($Hypervisor) {
                'Hypervisor'
            } else {
                'Replay'
            })) `
            -TimeoutSeconds ([int]$Config.Policy.OperationTimeoutSeconds)
        [IO.File]::WriteAllText((Join-Path $Paths.Evidence 'wsl-stdout.txt'),
            $Execution.Stdout, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $Paths.Evidence 'wsl-stderr.txt'),
            $Execution.Stderr, [Text.UTF8Encoding]::new($false))
        $EvidenceNames = @(
                'result.json', 'baseline.txt', 'condition-result.json',
                'conditioning-serial.log', 'conditioning-debugcon.log',
                'conditioning-qemu-stderr.log', 'conditioning-swtpm.log',
                'conditioning-fat-directory.txt',
                'conditioning-swtpm-retry.log',
                'conditioning-first-attempt-serial.log',
                'conditioning-first-attempt-debugcon.log',
                'conditioning-first-attempt-qemu-stderr.log',
                'conditioning-post-result-qemu-exit.txt',
                'conditioning-cache-hit.txt',
                'conditioning-cache-published.txt',
                'windows-overlay-create.txt', 'nested-result.json',
                'postboot-tcglog.ib', 'postboot-tcglog-full.bin',
                'postboot-probe.stdout.txt', 'postboot-probe.stderr.txt',
                'postboot-capture.stdout.txt', 'postboot-capture.stderr.txt',
                'postboot-audit-dump.stdout.txt',
                'postboot-audit-dump.stderr.txt',
                'postboot-verify-pcrs.stdout.txt',
                'postboot-verify-pcrs.stderr.txt',
                'postboot-progress.txt',
                'postboot-fat-directory.txt',
                'divergence.ib', 'handoff-status.txt',
                'qemu-fatal-marker.txt',
                'qemu-register-snapshots.txt',
                 'qemu-stall-marker.txt',
                 'qemu-screen.ppm',
                 'windows-replay-retry.txt',
                 'swtpm-retry.log',
                 'swtpm-retry-setup.log',
                 'first-attempt-qemu-debugcon.log',
                 'first-attempt-qemu-serial.log',
                 'first-attempt-qemu-stderr.log',
                 'first-attempt-qemu-exit-code.txt',
                 'first-attempt-qemu-register-snapshots.txt',
                 'first-attempt-qemu-screen.ppm',
                 'first-attempt-postboot-fat-directory.txt',
                 'qemu-debugcon.log', 'qemu-serial.log', 'qemu-stderr.log',
                 'swtpm.log', 'edk2-build.log', 'windows-disk-info.json',
                 'windows-base-cache-info.json',
                 'windows-base-cache-convert.log',
                 'windows-base-cache-hit.txt',
                 'windows-base-cache-published.txt')
        if ($Hypervisor) {
            $EvidenceNames += 'nested-windows-hypervisor-order.txt'
        }
        foreach ($Name in $EvidenceNames) {
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
            } elseif ($Hypervisor) {
                'WslNestedWindowsHypervisorE2E'
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
    'StoreMailboxCredential' {
        if ($Profile -ne 'Windows') { throw 'Choose the Windows profile.' }
        $Selected = Get-ProfileConfig $Profile
        if ([string]::IsNullOrWhiteSpace(
                [string]$Selected.MailboxCredentialFile)) {
            throw 'The Windows profile has no mailbox credential file.'
        }
        $Secret = Read-Host `
            'Base64-encoded 32-byte Windows mailbox HMAC key' -AsSecureString
        $Credential = [PSCredential]::new('UEFILoaderMailbox', $Secret)
        $Plaintext = $Credential.GetNetworkCredential().Password
        $KeyBytes = $null
        try {
            $KeyBytes = [Convert]::FromBase64String($Plaintext)
            if ($KeyBytes.Length -ne 32) {
                throw 'The mailbox key must decode to exactly 32 bytes.'
            }
            Set-UhrCredential -Config $Config `
                -FileName ([string]$Selected.MailboxCredentialFile) `
                -Credential $Credential | Out-Null
        }
        catch [FormatException] {
            throw 'The mailbox key is not valid base64.'
        }
        finally {
            if ($null -ne $KeyBytes) {
                [Array]::Clear($KeyBytes, 0, $KeyBytes.Length)
            }
            $Plaintext = $null
            $Credential = $null
            $Secret = $null
        }
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
            -ProjectRoot $UefiProjectRoot -StateRoot $Config.StateRoot
        break
    }
    'RunInstallerOneShotHostGate' {
        if (-not $SkipBuild) { Invoke-HostBuild | Out-Host }
        & (Join-Path $PSScriptRoot `
            'tests\Invoke-InstallerOneShotHostGate.ps1') `
            -ProjectRoot $UefiProjectRoot -StateRoot $Config.StateRoot
        break
    }
    'RunAuditHostGate' {
        if (-not $SkipBuild) { Invoke-HostBuild | Out-Host }
        & (Join-Path $PSScriptRoot 'tests\Invoke-AuditHostGate.ps1') `
            -ProjectRoot $UefiProjectRoot -StateRoot $Config.StateRoot
        break
    }
    'RunFixtureHostGate' {
        if (-not $SkipBuild) { Invoke-HostBuild | Out-Host }
        & (Join-Path $PSScriptRoot 'tests\Invoke-FixtureHostGate.ps1') `
            -ProjectRoot $UefiProjectRoot -StateRoot $Config.StateRoot
        break
    }
    'RunLinuxPositivePath' { Invoke-LiveWorkflow LinuxPositive; break }
    'RunLinuxHypervisorSmoke' {
        Invoke-LiveWorkflow LinuxHypervisorSmoke
        break
    }
    'RunLinuxHypervisorPostEbsSmoke' {
        Invoke-LiveWorkflow LinuxHypervisorPostEbsSmoke
        break
    }
    'RunLinuxHypervisorPostEbsInterruptSmoke' {
        Invoke-LiveWorkflow LinuxHypervisorPostEbsInterruptSmoke
        break
    }
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
    'RunLinuxNestedWindowsHypervisorE2E' {
        Invoke-LiveWorkflow LinuxNestedWindowsHypervisorE2E
        break
    }
    'RunWslNestedWindowsE2E' {
        Invoke-WslNestedWindowsE2E
        break
    }
    'RunWslNestedWindowsHypervisorE2E' {
        Invoke-WslNestedWindowsE2E -Hypervisor
        break
    }
    'RunWslBootNextE2E' {
        Invoke-WslNestedWindowsE2E -BootNext
        break
    }
    'RunWindowsProbe' { Invoke-LiveWorkflow WindowsProbe; break }
    'RunWindowsCapture' { Invoke-LiveWorkflow WindowsCapture; break }
    'RunWindowsNestedPrep' { Invoke-LiveWorkflow WindowsNestedPrep; break }
    'RunWindowsExactBuildUpdate' {
        Invoke-WindowsExactBuildUpdate
        break
    }
    'ResumeWindowsExactBuildUpdate' {
        Invoke-WindowsExactBuildResume
        break
    }
    'RunWindowsDirectHypervisorE2E' {
        Invoke-WindowsDirectHypervisorE2E
        break
    }
    'RunWindowsDirectScannerE2E' {
        Invoke-WindowsDirectHypervisorE2E -ScannerOnly
        break
    }
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
