[CmdletBinding()]
param(
    [string]$RunRoot = $PSScriptRoot,
    [ValidateSet('Capture')]
    [string]$Action = 'Capture',
    [ValidateSet('MET', 'NOT_MET')]
    [string]$ExpectedProbeVerdict = 'NOT_MET',
    [ValidateSet('Initial', 'Worker')]
    [string]$Mode = 'Initial'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This script is staged under Invoke-WindowsGuestTest.ps1 because the fixed
# GuestInfo worker intentionally accepts only the Capture action and this
# exact package shape.  The worker invocation is short-lived; the SYSTEM task
# below owns all potentially long Windows servicing work.
$TargetBuild = [uint64]26200
$TargetUbr = [uint64]9445
$TargetKb = 'KB5124008'
$CacheResetFloorUbr = [uint64]9168
$ExpectedRunIdPattern = '^uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$'
$ExpectedInstallRoot = 'C:\ProgramData\UEFILoaderRunner'
$ExpectedKeyPath = Join-Path $ExpectedInstallRoot 'mailbox.key'
$StatusVariable = 'guestinfo.uefiloader.exact.status'
$TaskPrefix = 'UEFILoader-ExactBuild-'
$ExpectedPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$ExpectedSchtasks = Join-Path $env:SystemRoot 'System32\schtasks.exe'
$ExpectedShutdown = Join-Path $env:SystemRoot 'System32\shutdown.exe'
$ExpectedDism = Join-Path $env:SystemRoot 'System32\dism.exe'
$ExpectedSfc = Join-Path $env:SystemRoot 'System32\sfc.exe'
$ExpectedVmtools = @(
    'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe',
    'C:\Program Files (x86)\VMware\VMware Tools\vmtoolsd.exe')

function Test-PathWithin {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $RootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $PathFull = [IO.Path]::GetFullPath($Path)
    return $PathFull.StartsWith($RootFull, [StringComparison]::OrdinalIgnoreCase)
}

function Get-RunId {
    $FullRoot = [IO.Path]::GetFullPath($RunRoot)
    if (-not (Test-PathWithin -Root (Join-Path $ExpectedInstallRoot 'runs') -Path $FullRoot) -or
        -not (Test-Path -LiteralPath $FullRoot -PathType Container)) {
        throw 'The exact-build RunRoot is outside the fixed GuestInfo run root.'
    }
    $Id = [IO.Path]::GetFileName($FullRoot.TrimEnd('\'))
    if ($Id -cnotmatch $ExpectedRunIdPattern) {
        throw "Unsafe exact-build RunId: $Id"
    }
    return $Id
}

function Get-VmtoolsdPath {
    foreach ($Candidate in $ExpectedVmtools) {
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) { return $Candidate }
    }
    throw 'VMware Tools vmtoolsd.exe was not found at an allowlisted path.'
}

function Invoke-Vmtoolsd {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('info-get', 'info-set')][string]$Operation,
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowEmptyString()][string]$Value = ''
    )
    if ($Name -cne $StatusVariable) { throw 'The exact-build script may publish only its fixed status variable.' }
    if ($Value.Length -gt 12000) { throw 'The exact-build status exceeds the GuestInfo bound.' }
    $Vmtoolsd = Get-VmtoolsdPath
    $Command = if ($Operation -ceq 'info-get') {
        "$Operation $Name"
    } else {
        "$Operation $Name $Value"
    }
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $Vmtoolsd
    $Info.Arguments = '--cmd "' + $Command.Replace('"', '\"') + '"'
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    if (-not $Process.Start()) { throw 'Failed to start VMware Tools.' }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit(15000)) {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            & "$env:SystemRoot\System32\taskkill.exe" /PID $Process.Id /T /F | Out-Null
        }
        $Process.Dispose()
        throw "vmtoolsd $Operation timed out."
    }
    $Stdout = [string]$StdoutTask.Result
    $Stderr = [string]$StderrTask.Result
    $ExitCode = $Process.ExitCode
    $Process.Dispose()
    if ($Operation -ceq 'info-get') {
        $Trimmed = $Stdout.Trim()
        if ($ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($Trimmed) -or
            $Trimmed -match '(?i)(no value|unknown property|not found)') {
            return $null
        }
        return $Trimmed
    }
    if ($ExitCode -ne 0) {
        throw "vmtoolsd info-set failed: $($Stderr.Trim())"
    }
    return $true
}

function Get-VmtoolsdInfo {
    return Invoke-Vmtoolsd -Operation 'info-get' -Name $StatusVariable
}

function Get-HmacSha256Hex {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Key,
        [Parameter(Mandatory = $true)][string]$Text
    )
    $Data = [Text.Encoding]::UTF8.GetBytes($Text)
    $Hmac = [Security.Cryptography.HMACSHA256]::new($Key)
    try {
        return ([BitConverter]::ToString($Hmac.ComputeHash($Data))).Replace('-', '')
    }
    finally {
        $Hmac.Dispose()
        [Array]::Clear($Data, 0, $Data.Length)
    }
}

function Get-CanonicalStatus {
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

function Read-MailboxKey {
    $KeyPath = [IO.Path]::GetFullPath($ExpectedKeyPath)
    if ($KeyPath -cne 'C:\ProgramData\UEFILoaderRunner\mailbox.key') {
        throw 'The mailbox key path is not canonical.'
    }
    $Key = [IO.File]::ReadAllBytes($KeyPath)
    if ($Key.Length -ne 32) {
        [Array]::Clear($Key, 0, $Key.Length)
        throw 'The installed mailbox key is not exactly 32 bytes.'
    }
    return $Key
}

function Publish-ExactStatus {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Key,
        [Parameter(Mandatory = $true)][string]$RunId,
        [Parameter(Mandatory = $true)][ValidateSet(
            'STARTING', 'QUEUED', 'REPAIRING', 'SEARCHING', 'DOWNLOADING', 'INSTALLING',
            'REBOOTING', 'VERIFYING', 'COMPLETE', 'FAILED')][string]$State,
        [Parameter(Mandatory = $true)][uint64]$WindowsBuild,
        [Parameter(Mandatory = $true)][uint64]$WindowsUbr,
        [Parameter(Mandatory = $true)][bool]$RebootRequired,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if ($Message.Length -gt 4096) { $Message = $Message.Substring(0, 4096) }
    $MessageBytes = [Text.Encoding]::UTF8.GetBytes($Message)
    try {
        $Status = [ordered]@{
            SchemaVersion = 1
            RunId = $RunId
            State = $State
            Package = $TargetKb
            WindowsBuild = $WindowsBuild
            WindowsUbr = $WindowsUbr
            RebootRequired = $RebootRequired
            MessageBase64 = [Convert]::ToBase64String($MessageBytes)
            UpdatedUtc = [DateTime]::UtcNow.ToString('o')
        }
    }
    finally { [Array]::Clear($MessageBytes, 0, $MessageBytes.Length) }
    $Status.StatusHmacSha256 = Get-HmacSha256Hex -Key $Key -Text (Get-CanonicalStatus $Status)
    $JsonBytes = [Text.Encoding]::UTF8.GetBytes(($Status | ConvertTo-Json -Compress -Depth 4))
    try {
        # Keep a task-local, authenticated breadcrumb before using VMware
        # Tools.  It survives a Tools transport failure long enough for an
        # administrator to inspect the disposable guest and never contains
        # the mailbox key.
        $LocalStatusPath = Join-Path ([IO.Path]::GetFullPath($RunRoot)) `
            'exact-build-worker-status.json'
        $LocalStatusTemp = "$LocalStatusPath.$PID.tmp"
        [IO.File]::WriteAllBytes($LocalStatusTemp, $JsonBytes)
        Move-Item -LiteralPath $LocalStatusTemp -Destination $LocalStatusPath -Force
        $Encoded = [Convert]::ToBase64String($JsonBytes)
    }
    finally { [Array]::Clear($JsonBytes, 0, $JsonBytes.Length) }
    # VMware Tools can start after the ONSTART task.  Keep the retry bounded;
    # a missing tools channel is a real failure, never silently ignored.
    $Deadline = [DateTime]::UtcNow.AddSeconds(120)
    do {
        try {
            Invoke-Vmtoolsd -Operation 'info-set' -Name $StatusVariable -Value $Encoded
            return $Status
        }
        catch {
            if ([DateTime]::UtcNow -ge $Deadline) { throw }
            Start-Sleep -Seconds 2
        }
    } while ($true)
}

function Get-CurrentWindowsBuild {
    $Version = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop
    [uint64]$Build = 0
    [uint64]$Ubr = 0
    if (-not [uint64]::TryParse([string]$Version.CurrentBuild,
            [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$Build) -or
        -not [uint64]::TryParse([string]$Version.UBR,
            [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$Ubr) -or
        $Build -gt [uint64]4294967295 -or $Ubr -gt [uint64]4294967295) {
        throw 'Windows CurrentVersion build metadata is invalid.'
    }
    return [pscustomobject]@{ Build = $Build; Ubr = $Ubr }
}

function Get-SystemDriveFreeGiB {
    $Drive = [IO.DriveInfo]::new($env:SystemDrive)
    if (-not $Drive.IsReady) { throw 'The Windows system drive is not ready.' }
    return [math]::Round($Drive.AvailableFreeSpace / 1GB, 2)
}

function Format-HResult {
    param([Parameter(Mandatory = $true)][int32]$Value)
    $Bytes = [BitConverter]::GetBytes($Value)
    try { return ('0x{0:X8}' -f [BitConverter]::ToUInt32($Bytes, 0)) }
    finally { [Array]::Clear($Bytes, 0, $Bytes.Length) }
}

function Get-ServicingFailureContext {
    $Candidates = @(
        (Join-Path $env:SystemRoot 'Logs\CBS\CBS.log'),
        (Join-Path $env:SystemRoot 'Logs\DISM\dism.log'))
    $Lines = [Collections.Generic.List[string]]::new()
    foreach ($Path in $Candidates) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { continue }
        try {
            $Matches = @(Get-Content -LiteralPath $Path -Tail 800 -ErrorAction Stop |
                Where-Object {
                    $_ -match '(?i)0x80070306|0x80240022|\berror\b|\bfailed\b'
                } | Select-Object -Last 12)
            foreach ($Line in $Matches) {
                $Normalized = ([string]$Line -replace '[\r\n\x00]', ' ').Trim()
                if ($Normalized.Length -gt 300) {
                    $Normalized = $Normalized.Substring(0, 300)
                }
                if ($Normalized.Length -gt 0) {
                    $Lines.Add("$([IO.Path]::GetFileName($Path)): $Normalized")
                }
            }
        }
        catch { }
    }
    $Summary = $Lines -join ' | '
    if ($Summary.Length -gt 3000) { $Summary = $Summary.Substring(0, 3000) }
    return $Summary
}

function Test-PendingReboot {
    return Test-Path -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired'
}

function Reset-ExactWindowsUpdateCache {
    param([Parameter(Mandatory = $true)][string]$RunId)
    $Services = @('bits', 'wuauserv', 'cryptsvc')
    $Roots = @(
        (Join-Path $env:SystemRoot 'SoftwareDistribution'),
        (Join-Path $env:SystemRoot 'System32\catroot2'))
    try {
        foreach ($Name in $Services) {
            $Service = Get-Service -Name $Name -ErrorAction Stop
            if ($Service.Status -ne [ServiceProcess.ServiceControllerStatus]::Stopped) {
                Stop-Service -Name $Name -Force -ErrorAction Stop
            }
        }
        foreach ($Root in $Roots) {
            if (-not (Test-Path -LiteralPath $Root -PathType Container)) { continue }
            $Backup = "$Root.UEFILoader-$RunId"
            if (Test-Path -LiteralPath $Backup) {
                # A host/VM interruption can restart this same authenticated
                # task after the cache rotation.  The same-run backup plus a
                # newly recreated live cache proves rotation already happened.
                continue
            }
            Move-Item -LiteralPath $Root -Destination $Backup -ErrorAction Stop
        }
    }
    finally {
        foreach ($Name in @('cryptsvc', 'wuauserv', 'bits')) {
            try { Start-Service -Name $Name -ErrorAction Stop } catch { }
        }
    }
}

function Remove-ExactWindowsUpdateCacheBackup {
    param([Parameter(Mandatory = $true)][string]$RunId)
    foreach ($Root in @(
            (Join-Path $env:SystemRoot 'SoftwareDistribution'),
            (Join-Path $env:SystemRoot 'System32\catroot2'))) {
        $Backup = "$Root.UEFILoader-$RunId"
        if (Test-Path -LiteralPath $Backup -PathType Container) {
            Remove-Item -LiteralPath $Backup -Recurse -Force -ErrorAction Stop
        }
    }
}

function Invoke-Schtasks {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $Output = & $ExpectedSchtasks @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "schtasks.exe failed: $([string]($Output -join ' '))"
    }
}

function Remove-ExactTask {
    param([Parameter(Mandatory = $true)][string]$TaskName)
    try {
        & $ExpectedSchtasks /Delete /TN $TaskName /F 2>&1 | Out-Null
    }
    catch { }
}

function Test-ExactCancellation {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Key,
        [Parameter(Mandatory = $true)][string]$RunId
    )
    $Bytes = $null
    try {
        $Encoded = Get-VmtoolsdInfo
        if ($null -eq $Encoded -or $Encoded.Length -gt 12000) { return $false }
        $Bytes = [Convert]::FromBase64String($Encoded)
        if ([Convert]::ToBase64String($Bytes) -cne $Encoded -or $Bytes.Length -gt 8192) { return $false }
        $Raw = [Text.Encoding]::UTF8.GetString($Bytes) | ConvertFrom-Json
        if ([string]$Raw.RunId -cne $RunId -or [string]$Raw.State -cne 'CANCELLED' -or
            [string]$Raw.Package -cne $TargetKb -or
            [string]$Raw.StatusHmacSha256 -cnotmatch '^[0-9A-F]{64}$') { return $false }
        $Expected = Get-HmacSha256Hex -Key $Key -Text (Get-CanonicalStatus $Raw)
        return $Expected -ceq [string]$Raw.StatusHmacSha256
    }
    catch { return $false }
    finally { if ($null -ne $Bytes) { [Array]::Clear($Bytes, 0, $Bytes.Length) } }
}

function Stop-ExactUpdateIfCancelled {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Key,
        [Parameter(Mandatory = $true)][string]$RunId,
        [Parameter(Mandatory = $true)][string]$TaskName
    )
    if (-not (Test-ExactCancellation -Key $Key -RunId $RunId)) { return }
    Remove-ExactTask $TaskName
    $Current = Get-CurrentWindowsBuild
    [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'FAILED' `
        -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
        -Message 'The authenticated host cancellation marker stopped and unregistered the updater task.')
    throw 'The host requested exact-build servicing cancellation.'
}

function Install-ExactTask {
    param(
        [Parameter(Mandatory = $true)][string]$RunId,
        [Parameter(Mandatory = $true)][string]$TaskScript,
        [Parameter(Mandatory = $true)][byte[]]$Key,
        [Parameter(Mandatory = $true)]$Current
    )
    $TaskName = $TaskPrefix + $RunId
    if ($TaskName.Length -gt 200 -or $TaskName -notmatch '^[A-Za-z0-9-]+$') {
        throw 'The exact-build scheduled-task name is unsafe.'
    }
    # RunId is globally unique and the task name is derived from it. Avoid an
    # expected failing /Query: Windows PowerShell promotes native stderr to a
    # terminating error under ErrorActionPreference=Stop.
    if (-not (Test-Path -LiteralPath $TaskScript -PathType Leaf) -or
        [IO.Path]::GetFullPath($TaskScript) -cne [IO.Path]::GetFullPath($PSCommandPath)) {
        throw 'The exact-build task script path is not the staged script.'
    }
    # Use two triggers on one task: a delayed one-shot trigger starts servicing
    # only after the fixed GuestInfo worker has returned the setup receipt, and
    # the boot trigger persists across Windows Update's required reboot.
    $TaskRoot = [IO.Path]::GetDirectoryName($TaskScript)
    $TaskAction = New-ScheduledTaskAction -Execute $ExpectedPowerShell -Argument (
        '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{0}" -RunRoot "{1}" -Mode Worker' -f
            $TaskScript, $TaskRoot)
    $Triggers = @(
        (New-ScheduledTaskTrigger -Once -At ([DateTime]::Now.AddMinutes(2)))
        (New-ScheduledTaskTrigger -AtStartup)
    )
    $Principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' `
        -LogonType ServiceAccount -RunLevel Highest
    $Settings = New-ScheduledTaskSettingsSet -StartWhenAvailable `
        -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Hours 6)
    Register-ScheduledTask -TaskName $TaskName -Action $TaskAction `
        -Trigger $Triggers -Principal $Principal -Settings $Settings `
        -Force | Out-Null
    try {
        # Publish from the already authenticated setup process before starting
        # the asynchronous task.  The host can now distinguish "registered but
        # never started" from a request/setup transport failure.
        [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'QUEUED' `
            -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr `
            -RebootRequired $false `
            -Message "The SYSTEM task $TaskName is registered and will start exact servicing for $TargetKb.")
        # MultipleInstances prevents the delayed safety trigger from creating
        # a second updater while this explicit invocation is active.
        Start-ScheduledTask -TaskName $TaskName
    }
    catch {
        Remove-ExactTask $TaskName
        throw
    }
    return $TaskName
}

function Find-ExactUpdate {
    $Session = New-Object -ComObject Microsoft.Update.Session
    $Searcher = $Session.CreateUpdateSearcher()
    $Search = $Searcher.Search("IsInstalled=0 and Type='Software' and IsHidden=0")
    $Matches = @($Search.Updates | Where-Object {
        $Ids = @($_.KBArticleIDs | ForEach-Object { ([string]$_).Trim().ToUpperInvariant() })
        ($Ids -contains $TargetKb -or
            $Ids -contains $TargetKb.Substring(2)) -and
            ([string]$_.Title -match '(?i)KB5124008')
    })
    if ($Matches.Count -ne 1) {
        throw "Windows Update did not expose exactly one applicable $TargetKb update (found $($Matches.Count))."
    }
    return $Matches[0]
}

function Invoke-ExactUpdate {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Key,
        [Parameter(Mandatory = $true)][string]$RunId,
        [Parameter(Mandatory = $true)][string]$TaskName
    )
    $Current = Get-CurrentWindowsBuild
    $SystemDriveFreeGiB = Get-SystemDriveFreeGiB
    [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'STARTING' `
        -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
        -Message "Target is $TargetBuild.$TargetUbr via $TargetKb; system drive free space is $SystemDriveFreeGiB GiB.")
    Stop-ExactUpdateIfCancelled -Key $Key -RunId $RunId -TaskName $TaskName
    if ($Current.Build -gt $TargetBuild -or
        ($Current.Build -eq $TargetBuild -and $Current.Ubr -gt $TargetUbr)) {
        throw "Current Windows build $($Current.Build).$($Current.Ubr) supersedes the exact target."
    }
    if ($Current.Build -eq $TargetBuild -and $Current.Ubr -eq $TargetUbr) {
        [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'VERIFYING' `
            -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
            -Message 'Exact target build is already installed; no servicing was performed.')
        Remove-ExactWindowsUpdateCacheBackup -RunId $RunId
        Remove-ExactTask $TaskName
        [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'COMPLETE' `
            -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
            -Message 'Exact target build verified.')
        return
    }
    if ($Current.Build -ne $TargetBuild) {
        throw "Current Windows build $($Current.Build).$($Current.Ubr) is outside the supported 25H2 build family."
    }
    if ($SystemDriveFreeGiB -lt 20) {
        throw "The Windows system drive has only $SystemDriveFreeGiB GiB free; exact servicing requires at least 20 GiB."
    }
    Stop-ExactUpdateIfCancelled -Key $Key -RunId $RunId -TaskName $TaskName
    [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'REPAIRING' `
        -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
        -Message 'Checking the Windows component store and running SFC before the cumulative update; cache rotation is reserved for the retained recovery baseline.')
    # A full online RestoreHealth on the pinned 26200.9168 image has already
    # been captured failing at 100% with 0x800F0915 because matching repair
    # content is unavailable.  Repeating that 24-minute operation cannot
    # improve this snapshot, so retain a fast in-box health diagnostic here.
    $DismOutput = @(& $ExpectedDism /Online /Cleanup-Image /CheckHealth /NoRestart 2>&1)
    $DismExit = $LASTEXITCODE
    if ($DismExit -ne 0) {
        $Tail = (@($DismOutput | Select-Object -Last 12) -join ' ')
        if ($Tail.Length -gt 1500) { $Tail = $Tail.Substring(0, 1500) }
        [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'REPAIRING' `
            -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr `
            -RebootRequired $false `
            -Message "DISM health check returned $DismExit; preserving that diagnostic and continuing with SFC plus a bounded Windows Update cache reset. $Tail")
    }
    [Array]::Clear($DismOutput, 0, $DismOutput.Length)
    $SfcOutput = @(& $ExpectedSfc /scannow 2>&1)
    $SfcExit = $LASTEXITCODE
    if ($SfcExit -ne 0) {
        $Tail = (@($SfcOutput | Select-Object -Last 12) -join ' ')
        if ($Tail.Length -gt 1500) { $Tail = $Tail.Substring(0, 1500) }
        throw "SFC repair failed with exit code $SfcExit. $Tail"
    }
    [Array]::Clear($SfcOutput, 0, $SfcOutput.Length)
    Stop-ExactUpdateIfCancelled -Key $Key -RunId $RunId -TaskName $TaskName
    if ($Current.Ubr -ge $CacheResetFloorUbr) {
        Reset-ExactWindowsUpdateCache -RunId $RunId
        [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'REPAIRING' `
            -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
            -Message 'SFC completed and the fixed Windows Update caches were reset for the retained recovery baseline; retrying only KB5124008.')
    }
    else {
        [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'REPAIRING' `
            -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
            -Message "SFC completed on fresh baseline $($Current.Build).$($Current.Ubr); preserving its healthy Windows Update caches before $TargetKb.")
    }
    Stop-ExactUpdateIfCancelled -Key $Key -RunId $RunId -TaskName $TaskName
    [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'SEARCHING' `
        -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
        -Message "Searching only for $TargetKb.")
    $Update = Find-ExactUpdate
    if (-not [bool]$Update.EulaAccepted) { [void]$Update.AcceptEula() }
    $Updates = New-Object -ComObject Microsoft.Update.UpdateColl
    [void]$Updates.Add($Update)
    Stop-ExactUpdateIfCancelled -Key $Key -RunId $RunId -TaskName $TaskName
    $Downloader = (New-Object -ComObject Microsoft.Update.Session).CreateUpdateDownloader()
    $Downloader.Updates = $Updates
    [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'DOWNLOADING' `
        -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
        -Message "$TargetKb was selected by its KBArticleIDs and title.")
    Stop-ExactUpdateIfCancelled -Key $Key -RunId $RunId -TaskName $TaskName
    $DownloadResult = $Downloader.Download()
    if ([int]$DownloadResult.ResultCode -ne 2) {
        throw "$TargetKb download did not succeed (result code $($DownloadResult.ResultCode), HRESULT $(Format-HResult ([int32]$DownloadResult.HResult)))."
    }
    $Installer = (New-Object -ComObject Microsoft.Update.Session).CreateUpdateInstaller()
    $Installer.Updates = $Updates
    Stop-ExactUpdateIfCancelled -Key $Key -RunId $RunId -TaskName $TaskName
    [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'INSTALLING' `
        -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
        -Message "Installing only $TargetKb.")
    $InstallResult = $Installer.Install()
    if ([int]$InstallResult.ResultCode -ne 2) {
        $UpdateResult = $InstallResult.GetUpdateResult(0)
        throw "$TargetKb installation did not succeed (aggregate result $($InstallResult.ResultCode), aggregate HRESULT $(Format-HResult ([int32]$InstallResult.HResult)), update result $($UpdateResult.ResultCode), update HRESULT $(Format-HResult ([int32]$UpdateResult.HResult)))."
    }
    Stop-ExactUpdateIfCancelled -Key $Key -RunId $RunId -TaskName $TaskName
    $AfterInstall = Get-CurrentWindowsBuild
    $NeedsReboot = [bool]$InstallResult.RebootRequired -or (Test-PendingReboot)
    if ($AfterInstall.Build -eq $TargetBuild -and $AfterInstall.Ubr -eq $TargetUbr) {
        Remove-ExactWindowsUpdateCacheBackup -RunId $RunId
        Remove-ExactTask $TaskName
        [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'COMPLETE' `
            -WindowsBuild $AfterInstall.Build -WindowsUbr $AfterInstall.Ubr -RebootRequired $false `
            -Message 'Exact target build verified after servicing.')
        return
    }
    if (-not $NeedsReboot) {
        throw "$TargetKb completed without a reboot but Windows remains $($AfterInstall.Build).$($AfterInstall.Ubr)."
    }
    [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'REBOOTING' `
        -WindowsBuild $AfterInstall.Build -WindowsUbr $AfterInstall.Ubr -RebootRequired $true `
        -Message 'Windows Update requires a reboot; the persistent SYSTEM task will verify after startup.')
    & $ExpectedShutdown /r /t 5 /d p:4:1 /f 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'The exact-build reboot request failed.' }
}

$RunId = Get-RunId
if ($Action -cne 'Capture' -or $ExpectedProbeVerdict -notin @('MET', 'NOT_MET')) {
    throw 'The exact-build action contract is invalid.'
}
$Key = Read-MailboxKey
$TaskName = $TaskPrefix + $RunId
$TaskScript = [IO.Path]::GetFullPath($PSCommandPath)
try {
    if ($Mode -eq 'Initial') {
        $Current = Get-CurrentWindowsBuild
        if ($Current.Build -gt $TargetBuild -or
            ($Current.Build -eq $TargetBuild -and $Current.Ubr -gt $TargetUbr)) {
            throw "Current Windows build $($Current.Build).$($Current.Ubr) supersedes the exact target."
        }
        if ($Current.Build -eq $TargetBuild -and $Current.Ubr -eq $TargetUbr) {
            [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'COMPLETE' `
                -WindowsBuild $Current.Build -WindowsUbr $Current.Ubr -RebootRequired $false `
                -Message 'Exact target build already present.')
            $Result = [ordered]@{
                SchemaVersion = 1; RunId = $RunId; Status = 'PASS'
                Message = 'Exact Windows build already present.'
                CompletedUtc = [DateTime]::UtcNow.ToString('o')
            }
        }
        else {
            # Publish the worker response before relying on the asynchronous
            # task's GuestInfo channel.  This keeps task setup observable even
            # if VMware Tools later stalls while servicing is in progress.
            $TaskName = Install-ExactTask -RunId $RunId -TaskScript $TaskScript `
                -Key $Key -Current $Current
            $Result = [ordered]@{
                SchemaVersion = 1; RunId = $RunId; Status = 'PASS'
                Message = "The authenticated SYSTEM task $TaskName was queued for the controlled restart and $TargetKb."
                CompletedUtc = [DateTime]::UtcNow.ToString('o')
            }
        }
        [IO.File]::WriteAllText((Join-Path $RunRoot 'stdout.txt'),
            "WINDOWS_EXACT_TARGET_BUILD=$TargetBuild`r`nWINDOWS_EXACT_TARGET_UBR=$TargetUbr`r`nWINDOWS_EXACT_UPDATE=$TargetKb`r`nWINDOWS_EXACT_TASK=$TaskName`r`nWINDOWS_EXACT_SCHEDULED=PASS`r`n",
            [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $RunRoot 'stderr.txt'), '',
            [Text.UTF8Encoding]::new($false))
        $Result | ConvertTo-Json -Compress -Depth 4 | Set-Content -LiteralPath (Join-Path $RunRoot 'result.json') -Encoding UTF8
    }
    else {
        Invoke-ExactUpdate -Key $Key -RunId $RunId -TaskName $TaskName
    }
}
catch {
    $Current = $null
    try { $Current = Get-CurrentWindowsBuild } catch { }
    $FailureMessage = $_.Exception.Message
    $ServicingContext = Get-ServicingFailureContext
    if (-not [string]::IsNullOrWhiteSpace($ServicingContext)) {
        $FailureMessage = "$FailureMessage | SERVICING_CONTEXT: $ServicingContext"
    }
    try {
        [void](Publish-ExactStatus -Key $Key -RunId $RunId -State 'FAILED' `
            -WindowsBuild $(if ($null -ne $Current) { $Current.Build } else { [uint64]0 }) `
            -WindowsUbr $(if ($null -ne $Current) { $Current.Ubr } else { [uint64]0 }) `
            -RebootRequired $false -Message $FailureMessage)
    }
    catch { }
    if ($Mode -eq 'Initial') {
        [IO.File]::WriteAllText((Join-Path $RunRoot 'stdout.txt'),
            "WINDOWS_EXACT_TARGET_BUILD=$TargetBuild`r`nWINDOWS_EXACT_TARGET_UBR=$TargetUbr`r`nWINDOWS_EXACT_UPDATE=$TargetKb`r`nWINDOWS_EXACT_SCHEDULED=FAIL`r`n",
            [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $RunRoot 'stderr.txt'),
            $_.Exception.Message, [Text.UTF8Encoding]::new($false))
        $Result = [ordered]@{
            SchemaVersion = 1; RunId = $RunId; Status = 'FAIL'
            Message = $_.Exception.Message
            CompletedUtc = [DateTime]::UtcNow.ToString('o')
        }
        $Result | ConvertTo-Json -Compress -Depth 4 | Set-Content -LiteralPath (Join-Path $RunRoot 'result.json') -Encoding UTF8
    }
    if ($Mode -eq 'Worker') { exit 1 }
    throw
}
finally { [Array]::Clear($Key, 0, $Key.Length) }
