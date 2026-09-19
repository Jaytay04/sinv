Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-UhrFullPath {
    param([Parameter(Mandatory=$true)][string]$Path)
    return [IO.Path]::GetFullPath($Path)
}

function Test-UhrPathWithin {
    param(
        [Parameter(Mandatory=$true)][string]$Root,
        [Parameter(Mandatory=$true)][string]$Path
    )
    $RootFull = (Get-UhrFullPath $Root).TrimEnd('\') + '\'
    return (Get-UhrFullPath $Path).StartsWith(
        $RootFull,
        [StringComparison]::OrdinalIgnoreCase)
}

function Test-UhrRunId {
    param([Parameter(Mandatory=$true)][string]$RunId)
    return $RunId -cmatch `
        '^uefi-(linux-positive|linux-replay-dry-run|linux-pe-rehash|linux-divergence-manifest|linux-handoff-mechanics|linux-production-handoff|linux-oneshot-cycle|linux-nested-disk-probe|linux-nested-windows-e2e|linux-bootnext-e2e|windows-probe|windows-capture|windows-nested-prep)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$'
}

function New-UhrRunId {
    param(
        [Parameter(Mandatory=$true)]
        [ValidateSet(
            'linux-positive', 'linux-replay-dry-run', 'linux-pe-rehash',
            'linux-divergence-manifest', 'linux-handoff-mechanics',
            'linux-production-handoff', 'linux-oneshot-cycle',
            'linux-nested-disk-probe', 'linux-nested-windows-e2e',
            'linux-bootnext-e2e',
            'windows-probe', 'windows-capture', 'windows-nested-prep')]
        [string]$Workflow
    )
    $Suffix = [Guid]::NewGuid().ToString('N').Substring(0, 10)
    return 'uefi-{0}-{1}-{2}' -f $Workflow,
        [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'), $Suffix
}

function Write-UhrAtomicJson {
    param(
        [Parameter(Mandatory=$true)]$Value,
        [Parameter(Mandatory=$true)][string]$Path
    )
    $Parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $Parent | Out-Null
    }
    $Temporary = $Path + '.tmp-' + [Guid]::NewGuid().ToString('N')
    $Backup = $Path + '.bak-' + [Guid]::NewGuid().ToString('N')
    $Json = $Value | ConvertTo-Json -Depth 12
    $Bytes = [Text.UTF8Encoding]::new($false).GetBytes($Json + "`n")
    try {
        [IO.File]::WriteAllBytes($Temporary, $Bytes)
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            [IO.File]::Replace($Temporary, $Path, $Backup)
        }
        else {
            [IO.File]::Move($Temporary, $Path)
        }
    }
    finally {
        [Array]::Clear($Bytes, 0, $Bytes.Length)
        if (Test-Path -LiteralPath $Temporary -PathType Leaf) {
            Remove-Item -LiteralPath $Temporary -Force
        }
        if (Test-Path -LiteralPath $Backup -PathType Leaf) {
            Remove-Item -LiteralPath $Backup -Force
        }
    }
}

function Assert-UhrProfile {
    param(
        [Parameter(Mandatory=$true)]$Profile,
        [Parameter(Mandatory=$true)]
        [ValidateSet('Linux', 'Windows')]
        [string]$Name,
        [Parameter(Mandatory=$true)][string]$ProjectRoot
    )
    foreach ($Property in @(
            'Enabled', 'VmxPath', 'DisplayName', 'SnapshotName', 'GuestUser',
            'GuestStageRoot', 'GuestCredentialFile', 'VmCredentialFile',
            'LeaseResource')) {
        if ($null -eq $Profile.PSObject.Properties[$Property]) {
            throw "Profiles.$Name is missing $Property."
        }
    }
    if ($Profile.Enabled -isnot [bool]) {
        throw "Profiles.$Name.Enabled must be Boolean."
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$Profile.SnapshotName) -and
        [string]$Profile.SnapshotName -cnotmatch
            '^[A-Za-z0-9][A-Za-z0-9 ._-]{0,127}$') {
        throw "Profiles.$Name.SnapshotName contains unsafe characters."
    }
    if ([string]$Profile.LeaseResource -cnotmatch `
            '^vm-[a-z0-9][a-z0-9.-]{0,60}$') {
        throw "Profiles.$Name.LeaseResource must be a lowercase vm-* identifier."
    }
    if (-not $Profile.Enabled) { return }
    if (-not [IO.Path]::IsPathRooted([string]$Profile.VmxPath) -or
        -not (Test-Path -LiteralPath $Profile.VmxPath -PathType Leaf)) {
        throw "Profiles.$Name.VmxPath is not an existing absolute VMX path."
    }
    if ([string]::IsNullOrWhiteSpace([string]$Profile.DisplayName)) {
        throw "Profiles.$Name.DisplayName is required."
    }
    if ([string]$Profile.GuestUser -cnotmatch '^[A-Za-z0-9_.@-]{1,80}$') {
        throw "Profiles.$Name.GuestUser is unsafe."
    }
    foreach ($CredentialFile in @(
            [string]$Profile.GuestCredentialFile,
            [string]$Profile.VmCredentialFile,
            $(if ($null -ne $Profile.PSObject.Properties[
                    'MailboxCredentialFile']) {
                [string]$Profile.MailboxCredentialFile
            } else { '' }))) {
        if (-not [string]::IsNullOrWhiteSpace($CredentialFile) -and
            $CredentialFile -cnotmatch '^[A-Za-z0-9_.-]{1,100}\.xml$') {
            throw "Profiles.$Name contains an unsafe credential filename."
        }
    }
    if ($Name -eq 'Linux') {
        if ([string]::IsNullOrWhiteSpace([string]$Profile.GuestCredentialFile)) {
            throw 'Profiles.Linux.GuestCredentialFile is required.'
        }
        if ([string]$Profile.GuestStageRoot -cnotmatch `
                '^/home/[a-z_][a-z0-9_-]{0,31}/[A-Za-z0-9_./-]+$') {
            throw 'Profiles.Linux.GuestStageRoot must be a safe absolute path under /home.'
        }
        $Vmx = Get-Content -Raw -LiteralPath $Profile.VmxPath
        if ($Vmx -notmatch '(?im)^vhv\.enable\s*=\s*"TRUE"\s*$') {
            throw 'The Linux VMX must enable nested virtualization (vhv.enable = "TRUE").'
        }
    }
    else {
        if ($null -eq $Profile.PSObject.Properties['GuestTransport'] -or
            [string]::IsNullOrWhiteSpace([string]$Profile.GuestTransport)) {
            $Profile | Add-Member -NotePropertyName GuestTransport `
                -NotePropertyValue 'VmrunGuest' -Force
        }
        if ([string]$Profile.GuestTransport -notin @(
                'VmrunGuest', 'SharedFolderMailbox', 'VmwareGuestInfo')) {
            throw 'Profiles.Windows.GuestTransport must be VmrunGuest, SharedFolderMailbox, or VmwareGuestInfo.'
        }
        if ([string]$Profile.GuestTransport -ceq 'VmrunGuest' -and
            [string]::IsNullOrWhiteSpace([string]$Profile.GuestCredentialFile)) {
            throw 'Profiles.Windows.GuestCredentialFile is required for VmrunGuest.'
        }
        if ($null -eq $Profile.PSObject.Properties['ExpectedProbeVerdict']) {
            throw 'Profiles.Windows is missing ExpectedProbeVerdict.'
        }
        if ([string]$Profile.GuestStageRoot -cnotmatch `
                '^[A-Za-z]:\\[A-Za-z0-9_.\\-]+$') {
            throw 'Profiles.Windows.GuestStageRoot must be a safe absolute Windows path.'
        }
        if ([string]$Profile.ExpectedProbeVerdict -notin @('MET', 'NOT_MET')) {
            throw 'Profiles.Windows.ExpectedProbeVerdict must be MET or NOT_MET.'
        }
        if ([string]$Profile.GuestTransport -ceq 'SharedFolderMailbox') {
            if ($null -eq $Profile.PSObject.Properties['MailboxHostRoot'] -or
                [string]::IsNullOrWhiteSpace([string]$Profile.MailboxHostRoot)) {
                throw 'Profiles.Windows.MailboxHostRoot is required for SharedFolderMailbox.'
            }
            $MailboxHostRoot = [string]$Profile.MailboxHostRoot
            if (-not [IO.Path]::IsPathRooted($MailboxHostRoot) -or
                -not (Test-UhrPathWithin -Root $ProjectRoot -Path $MailboxHostRoot)) {
                throw 'Profiles.Windows.MailboxHostRoot must be an absolute path contained under ProjectRoot.'
            }
            if ($null -eq $Profile.PSObject.Properties[
                    'MailboxCredentialFile'] -or
                [string]::IsNullOrWhiteSpace(
                    [string]$Profile.MailboxCredentialFile)) {
                throw 'Profiles.Windows.MailboxCredentialFile is required for SharedFolderMailbox.'
            }
        }
        elseif ([string]$Profile.GuestTransport -ceq 'VmwareGuestInfo') {
            if ($null -eq $Profile.PSObject.Properties[
                    'MailboxCredentialFile'] -or
                [string]::IsNullOrWhiteSpace(
                    [string]$Profile.MailboxCredentialFile)) {
                throw 'Profiles.Windows.MailboxCredentialFile is required for VmwareGuestInfo.'
            }
        }
    }
}

function Import-UhrConfig {
    param([Parameter(Mandatory=$true)][string]$ConfigPath)
    if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
        throw "Host-runner configuration not found: $ConfigPath"
    }
    try {
        $Config = Get-Content -Raw -LiteralPath $ConfigPath | ConvertFrom-Json
    }
    catch {
        throw "Host-runner configuration is invalid JSON: $($_.Exception.Message)"
    }
    if ($Config.SchemaVersion -ne 1) {
        throw "Unsupported host-runner schema: $($Config.SchemaVersion)"
    }
    foreach ($Property in @('ProjectRoot', 'StateRoot', 'Tools', 'SharedLease',
            'Policy', 'Profiles')) {
        if ($null -eq $Config.PSObject.Properties[$Property]) {
            throw "Configuration is missing $Property."
        }
    }
    foreach ($Path in @(
            [string]$Config.ProjectRoot,
            [string]$Config.StateRoot,
            [string]$Config.Tools.VmrunPath,
            [string]$Config.Tools.VsDevCmdPath,
            [string]$Config.SharedLease.ScriptPath)) {
        if (-not [IO.Path]::IsPathRooted($Path)) {
            throw "Configuration path must be absolute: $Path"
        }
    }
    if (-not (Test-Path -LiteralPath $Config.ProjectRoot -PathType Container)) {
        throw "ProjectRoot does not exist: $($Config.ProjectRoot)"
    }
    foreach ($ToolPath in @(
            [string]$Config.Tools.VmrunPath,
            [string]$Config.Tools.VsDevCmdPath,
            [string]$Config.SharedLease.ScriptPath)) {
        if (-not (Test-Path -LiteralPath $ToolPath -PathType Leaf)) {
            throw "Configured tool not found: $ToolPath"
        }
    }
    foreach ($Property in @(
            'OperationTimeoutSeconds', 'BuildTimeoutSeconds',
            'GuestReadyTimeoutSeconds', 'LeaseMinutes',
            'StartupLeaseMinutes', 'MaxConcurrentVms',
            'MinimumHostReserveMib')) {
        if ($null -eq $Config.Policy.PSObject.Properties[$Property]) {
            throw "Policy is missing $Property."
        }
    }
    if ([int]$Config.Policy.OperationTimeoutSeconds -lt 30 -or
        [int]$Config.Policy.OperationTimeoutSeconds -gt 7200) {
        throw 'Policy.OperationTimeoutSeconds must be between 30 and 7200.'
    }
    if ([int]$Config.Policy.BuildTimeoutSeconds -lt 30 -or
        [int]$Config.Policy.BuildTimeoutSeconds -gt 7200) {
        throw 'Policy.BuildTimeoutSeconds must be between 30 and 7200.'
    }
    if ([int]$Config.Policy.GuestReadyTimeoutSeconds -lt 15 -or
        [int]$Config.Policy.GuestReadyTimeoutSeconds -gt 900) {
        throw 'Policy.GuestReadyTimeoutSeconds must be between 15 and 900.'
    }
    if ([int]$Config.Policy.LeaseMinutes -lt 5 -or
        [int]$Config.Policy.LeaseMinutes -gt 480) {
        throw 'Policy.LeaseMinutes must be between 5 and 480.'
    }
    if ([int]$Config.Policy.StartupLeaseMinutes -lt 5 -or
        [int]$Config.Policy.StartupLeaseMinutes -gt 30) {
        throw 'Policy.StartupLeaseMinutes must be between 5 and 30.'
    }
    if ([int]$Config.Policy.MaxConcurrentVms -lt 1 -or
        [int]$Config.Policy.MaxConcurrentVms -gt 2) {
        throw 'Policy.MaxConcurrentVms must be between 1 and 2.'
    }
    if ([int]$Config.Policy.MinimumHostReserveMib -lt 512 -or
        [int]$Config.Policy.MinimumHostReserveMib -gt 8192) {
        throw 'Policy.MinimumHostReserveMib must be between 512 and 8192.'
    }
    Assert-UhrProfile -Profile $Config.Profiles.Linux -Name Linux `
        -ProjectRoot ([string]$Config.ProjectRoot)
    Assert-UhrProfile -Profile $Config.Profiles.Windows -Name Windows `
        -ProjectRoot ([string]$Config.ProjectRoot)
    if ($Config.Profiles.Linux.Enabled -and $Config.Profiles.Windows.Enabled -and
        [string]$Config.Profiles.Linux.LeaseResource -ceq
            [string]$Config.Profiles.Windows.LeaseResource) {
        throw 'Enabled profiles must use distinct LeaseResource values.'
    }
    return $Config
}

function Get-UhrCredentialPath {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)][string]$FileName
    )
    if ($FileName -cnotmatch '^[A-Za-z0-9_.-]{1,100}\.xml$') {
        throw "Unsafe credential filename: $FileName"
    }
    return Join-Path (Join-Path $Config.StateRoot 'credentials') $FileName
}

function Set-UhrCredential {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)][string]$FileName,
        [Parameter(Mandatory=$true)][PSCredential]$Credential
    )
    $Path = Get-UhrCredentialPath -Config $Config -FileName $FileName
    $Parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $Parent | Out-Null
    $Credential | Export-Clixml -LiteralPath $Path -Force
    return $Path
}

function Get-UhrCredential {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)][string]$FileName
    )
    $Path = Get-UhrCredentialPath -Config $Config -FileName $FileName
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Credential is not stored: $FileName"
    }
    $Credential = Import-Clixml -LiteralPath $Path
    if ($Credential -isnot [PSCredential] -or
        [string]::IsNullOrWhiteSpace($Credential.UserName)) {
        throw "Stored credential is malformed: $FileName"
    }
    return $Credential
}

function Test-UhrCredential {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)][AllowEmptyString()][string]$FileName
    )
    if ([string]::IsNullOrWhiteSpace($FileName)) { return $false }
    return Test-Path -LiteralPath `
        (Get-UhrCredentialPath -Config $Config -FileName $FileName) -PathType Leaf
}

function ConvertTo-UhrNativeArgument {
    param([AllowEmptyString()][string]$Value)
    if ($null -eq $Value -or $Value.Length -eq 0) { return '""' }
    if ($Value -notmatch '[\s"]') { return $Value }
    $Escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $Escaped = [regex]::Replace($Escaped, '(\\+)$', '$1$1')
    return '"' + $Escaped + '"'
}

function Invoke-UhrProcess {
    param(
        [Parameter(Mandatory=$true)][string]$FilePath,
        [Parameter(Mandatory=$true)][string[]]$ArgumentList,
        [Parameter(Mandatory=$true)][string]$WorkingDirectory,
        [Parameter(Mandatory=$true)][int]$TimeoutSeconds,
        [string[]]$Redactions = @()
    )
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $FilePath
    $Info.Arguments = (($ArgumentList | ForEach-Object {
        ConvertTo-UhrNativeArgument ([string]$_)
    }) -join ' ')
    $Info.WorkingDirectory = $WorkingDirectory
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    if (-not $Process.Start()) { throw "Failed to start process: $FilePath" }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    $TimedOut = -not $Process.WaitForExit($TimeoutSeconds * 1000)
    if ($TimedOut) {
        try { $Process.Kill() } catch { }
        $Process.WaitForExit()
    }
    $Stdout = [string]$StdoutTask.Result
    $Stderr = [string]$StderrTask.Result
    foreach ($Secret in $Redactions) {
        if (-not [string]::IsNullOrEmpty($Secret)) {
            $Stdout = $Stdout.Replace($Secret, '<redacted>')
            $Stderr = $Stderr.Replace($Secret, '<redacted>')
        }
    }
    $Result = [pscustomobject]@{
        ExitCode = if ($TimedOut) { -1 } else { $Process.ExitCode }
        TimedOut = $TimedOut
        Stdout = $Stdout
        Stderr = $Stderr
    }
    $Process.Dispose()
    return $Result
}

Export-ModuleMember -Function @(
    'Get-UhrFullPath',
    'Test-UhrPathWithin',
    'Test-UhrRunId',
    'New-UhrRunId',
    'Write-UhrAtomicJson',
    'Import-UhrConfig',
    'Get-UhrCredentialPath',
    'Set-UhrCredential',
    'Get-UhrCredential',
    'Test-UhrCredential',
    'Invoke-UhrProcess'
)
