[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
    [Parameter(Mandatory = $true)][string]$VmxPath,
    [Parameter(Mandatory = $true)][string]$RunDirectory,
    [Parameter(Mandatory = $true)][string]$EfiPath,
    [Parameter(Mandatory = $true)][string]$LoaderPath,
    [Parameter(Mandatory = $true)][string]$InstallerPath,
    [Parameter(Mandatory = $true)][string]$ProbePath,
    [Parameter(Mandatory = $true)][string]$CredentialStateRoot,
    [ValidateSet('Model', 'SignedNative')][string]$ValidationProfile = 'Model',
    [string]$ShimPath,
    [string]$CertificatePath,
    [ValidateRange(1, 65535)][int]$ExpectedBuild = 26200,
    [ValidateRange(0, 65535)][int]$ExpectedUbr = 9457,
    [ValidateRange(1, 256)][int]$ExpectedCpus = 4,
    [ValidateRange(180, 600)][int]$TimeoutSeconds = 360,
    [string]$VmrunPath = 'C:\Program Files\VMware\VMware Workstation\vmrun.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
if ($null -eq ('GpuHalWindowsVmSafety.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace GpuHalWindowsVmSafety {
    [StructLayout(LayoutKind.Sequential)]
    public struct ByHandleFileInformation {
        public uint FileAttributes;
        public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }

    public static class NativeMethods {
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetFileInformationByHandle(
            SafeFileHandle fileHandle,
            out ByHandleFileInformation fileInformation);
    }
}
'@
}

$script:RunRoot = $null
$script:CommandLog = $null
$script:CommandNumber = 0
$script:CommandLogLimit = 1024
$script:RunId = $null
$script:InspectRunId = $null
$script:StageRunId = $null
$script:Failure = $null
$script:FailureScriptStackTrace = $null
$script:CleanupFailures = [Collections.Generic.List[string]]::new()
$script:SnapshotBefore = $null
$script:SnapshotAfter = $null
$script:BeforeSnapshotCreated = $false
$script:AfterSnapshotCreated = $false
$script:RevertedBefore = $false
$script:VmStartAttempted = $false
$script:VmOffConfirmed = $false
$script:InspectStatus = 'NOT_RUN'
$script:StageStatus = 'NOT_RUN'
$script:EfiStatus = 'NOT_RUN'
$script:StartupStatus = 'NOT_RUN'
$script:VmStartedAtLeastOnce = $false
$script:SecureBootChanged = $false
$script:ValidationProfile = $ValidationProfile
$script:ModelPassed = $null
$script:SignedNativePassed = $null
$script:SignedNativeArtifactEvidence = $null
$script:InspectEvidence = $null
$script:StageEvidence = $null
$script:HalResults = $null
$script:StartupEvidence = $null
$script:StopEvents = [Collections.Generic.List[string]]::new()
$script:Serial0Path = $null
$script:Serial1Path = $null
$script:GuestInfoTransports = [Collections.Generic.List[object]]::new()
$script:GuestInfoResponses = [Collections.Generic.List[object]]::new()
$script:GuestWorkerReadiness = [Collections.Generic.List[object]]::new()
$script:VmBootStartedUtc = $null
$script:OriginalSecureBoot = $null
$script:OriginalSecureBootPresent = $false
$script:OriginalSecureBootValue = $null
$script:SecureBootRestoreNeeded = $false
$script:ScreenCaptureStatus = 'NOT_ATTEMPTED'
$script:ScreenCapturePath = $null
$script:VmwareLogPath = $null
$script:VmwareLogStatus = 'NOT_ATTEMPTED'
$script:VmwareDbAppendMarker = 'NOT_APPLICABLE'
$script:ResultObject = $null
$script:Selected = $null
$script:Config = $null
$script:Paths = $null
$script:CoreInvokeUhrProcess = $null

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path)
}

function Test-PathWithin([string]$Root, [string]$Path) {
    $RootPath = (Get-FullPath $Root).TrimEnd('\') + '\'
    $Candidate = Get-FullPath $Path
    return $Candidate.StartsWith($RootPath, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NoReparsePoint([string]$Path, [string]$Label) {
    $Item = Get-Item -LiteralPath $Path -Force
    if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label may not be a reparse point: $Path"
    }
}

function Get-WindowsHardLinkCount([string]$Path) {
    $Stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite)
    try {
        $Information = New-Object GpuHalWindowsVmSafety.ByHandleFileInformation
        $Success = [GpuHalWindowsVmSafety.NativeMethods]::GetFileInformationByHandle(
            $Stream.SafeFileHandle, [ref]$Information)
        if (-not $Success) {
            $Code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw ([ComponentModel.Win32Exception]::new($Code)).Message
        }
        return [uint32]$Information.NumberOfLinks
    }
    finally { $Stream.Dispose() }
}

function Assert-SingleHardLink([string]$Path, [string]$Label) {
    $Count = Get-WindowsHardLinkCount $Path
    if ($Count -ne 1) {
        throw "$Label must have exactly one filesystem link; found $Count at $Path"
    }
}

function Assert-NoReparsePathChain {
    param(
        [Parameter(Mandatory = $true)][string]$Boundary,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $BoundaryPath = (Get-FullPath $Boundary).TrimEnd('\')
    $Current = Get-FullPath $Path
    if (-not $Current.Equals($BoundaryPath, [StringComparison]::OrdinalIgnoreCase) -and
        -not (Test-PathWithin $BoundaryPath $Current)) {
        throw "$Label escapes its validated path boundary: $Path"
    }
    while ($true) {
        if (Test-Path -LiteralPath $Current) {
            Assert-NoReparsePoint $Current $Label
        }
        if ($Current.Equals($BoundaryPath, [StringComparison]::OrdinalIgnoreCase)) { break }
        $Parent = Split-Path -Parent $Current
        if ([string]::IsNullOrWhiteSpace($Parent) -or
            $Parent.Equals($Current, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Label path chain did not reach its validated boundary: $Path"
        }
        $Current = $Parent
    }
}

function Write-CommandRecord {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [int]$ExitCode = -999,
        [bool]$TimedOut = $false,
        [string[]]$Redactions = @()
    )
    if ($null -eq $script:CommandLog -or $script:CommandNumber -ge $script:CommandLogLimit) {
        return
    }
    $script:CommandNumber++
    $SafeArguments = [Collections.Generic.List[string]]::new()
    $HideNext = $false
    for ($Index = 0; $Index -lt $ArgumentList.Length; $Index++) {
        $Argument = [string]$ArgumentList[$Index]
        if ($HideNext) {
            $SafeArguments.Add('<redacted>')
            $HideNext = $false
            continue
        }
        if ($Argument -in @('-vp', '-gp')) {
            $SafeArguments.Add($Argument)
            $HideNext = $true
            continue
        }
        if ($Argument -ceq 'writeVariable') {
            $SafeArguments.Add($Argument)
            for ($Tail = $Index + 1; $Tail -lt $ArgumentList.Length; $Tail++) {
                if ($Tail -eq $Index + 4) { $SafeArguments.Add('<guestinfo-value-redacted>') }
                else { $SafeArguments.Add([string]$ArgumentList[$Tail]) }
            }
            break
        }
        $Sanitized = $Argument
        foreach ($Secret in $Redactions) {
            if (-not [string]::IsNullOrEmpty($Secret)) {
                $Sanitized = $Sanitized.Replace($Secret, '<redacted>')
            }
        }
        $SafeArguments.Add($Sanitized)
    }
    $Record = [ordered]@{
        Sequence = $script:CommandNumber
        Utc = [DateTime]::UtcNow.ToString('o')
        Executable = $FilePath
        Arguments = @($SafeArguments)
        WorkingDirectory = $WorkingDirectory
        TimeoutSeconds = $TimeoutSeconds
        ExitCode = $ExitCode
        TimedOut = $TimedOut
    }
    [IO.File]::AppendAllText($script:CommandLog,
        (($Record | ConvertTo-Json -Compress -Depth 5) + "`r`n"),
        [Text.UTF8Encoding]::new($false))
}

function Invoke-LoggedUhrProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [string[]]$Redactions = @()
    )
    $Result = $null
    try {
        $Result = & $script:CoreInvokeUhrProcess -FilePath $FilePath `
            -ArgumentList $ArgumentList -WorkingDirectory $WorkingDirectory `
            -TimeoutSeconds $TimeoutSeconds -Redactions $Redactions
        return $Result
    }
    finally {
        Write-CommandRecord -FilePath $FilePath -ArgumentList $ArgumentList `
            -WorkingDirectory $WorkingDirectory -TimeoutSeconds $TimeoutSeconds `
            -ExitCode $(if ($null -eq $Result) { -999 } else { [int]$Result.ExitCode }) `
            -TimedOut $(if ($null -eq $Result) { $false } else { [bool]$Result.TimedOut }) `
            -Redactions $Redactions
    }
}

function Get-RunningVmPaths {
    $Result = Invoke-ConfiguredVmrun -Selected $script:Selected `
        -Operation @('list') -TimeoutSeconds 30
    $Paths = [Collections.Generic.List[string]]::new()
    foreach ($Line in ([string]$Result.Stdout -split "`r?`n")) {
        $Candidate = $Line.Trim()
        if ($Candidate -and $Candidate -notmatch '^Total running VMs:') {
            try { $Paths.Add([IO.Path]::GetFullPath($Candidate)) } catch { }
        }
    }
    return $Paths.ToArray()
}

function Test-TargetVmRunning {
    $Target = Get-FullPath ([string]$script:Selected.VmxPath)
    foreach ($Path in @(Get-RunningVmPaths)) {
        if ($Path.Equals($Target, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

function Invoke-TargetVmrun {
    param(
        [Parameter(Mandatory = $true)][string[]]$Operation,
        [ValidateRange(1, 600)][int]$Timeout = 30
    )
    return Invoke-ConfiguredVmrun -Selected $script:Selected `
        -Operation $Operation -TimeoutSeconds $Timeout
}

function Wait-GpuGuestTransportReady {
    param(
        [Parameter(Mandatory = $true)][DateTime]$BootStartedUtc,
        [ValidateRange(180, 600)][int]$TimeoutSeconds = 360
    )
    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $ToolsState = $null
    do {
        $Remaining = [int][Math]::Ceiling(($Deadline - [DateTime]::UtcNow).TotalSeconds)
        if ($Remaining -le 0) { break }
        $OperationTimeout = [Math]::Min(20, $Remaining)
        $Tools = Invoke-ConfiguredVmrun -Selected $script:Selected `
            -Operation @('checkToolsState', [string]$script:Selected.VmxPath) `
            -TimeoutSeconds $OperationTimeout -AllowFailure
        $ToolsState = ([string]$Tools.Stdout + [string]$Tools.Stderr).Trim()
        if (-not $Tools.TimedOut -and $Tools.ExitCode -eq 0 -and
            $ToolsState -match '^(?i:running|installed)$') {
            break
        }
        Start-Sleep -Seconds ([Math]::Min(2, $Remaining))
    } while ([DateTime]::UtcNow -lt $Deadline)
    if ($null -eq $Tools -or $Tools.TimedOut -or $Tools.ExitCode -ne 0 -or
        $ToolsState -notmatch '^(?i:running|installed)$') {
        throw 'VMware Tools did not become ready before the guest transport deadline.'
    }

    # Guest UTC may briefly differ from host UTC at boot. Permit only the worker's
    # existing 15-second freshness window; its authenticated status must still be
    # fresh and its start time must be from this boot.
    $MinimumWorkerStartUtc = $BootStartedUtc.ToUniversalTime().AddSeconds(-15)
    $Remaining = [int][Math]::Ceiling(($Deadline - [DateTime]::UtcNow).TotalSeconds)
    if ($Remaining -le 0) {
        throw 'VMware Tools consumed the guest transport readiness deadline.'
    }
    $Worker = Wait-VmwareGuestInfoWorkerReady -Selected $script:Selected `
        -MinimumStartedUtc $MinimumWorkerStartUtc `
        -ReadyTimeoutSeconds $Remaining
    return [pscustomobject]@{
        ToolsState = $ToolsState
        WorkerState = [string]$Worker.State
        WorkerSid = [string]$Worker.WorkerSid
        WorkerStartedUtc = [string]$Worker.StartedUtc
        WorkerUpdatedUtc = [string]$Worker.UpdatedUtc
    }
}

function Set-VmxSetting {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][AllowEmptyString()][Collections.Generic.List[string]]$Lines,
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value
    )
    if ($Key -notmatch '^[A-Za-z0-9_.:-]+$' -or $Value.Contains('"') -or
        $Value.Contains("`r") -or $Value.Contains("`n")) {
        throw "Unsafe VMware configuration value for $Key."
    }
    $Pattern = '^\s*' + [regex]::Escape($Key) + '\s*='
    $Found = -1
    for ($Index = 0; $Index -lt $Lines.Count; $Index++) {
        if ($Lines[$Index] -match $Pattern) {
            if ($Found -ge 0) { throw "VMX contains duplicate setting: $Key" }
            $Found = $Index
        }
    }
    $NewLine = "$Key = `"$Value`""
    if ($Found -ge 0) { $Lines[$Found] = $NewLine }
    else { $Lines.Add($NewLine) }
}

function Remove-VmxSetting {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][AllowEmptyString()][Collections.Generic.List[string]]$Lines,
        [Parameter(Mandatory = $true)][string]$Key
    )
    $Pattern = '^\s*' + [regex]::Escape($Key) + '\s*='
    for ($Index = $Lines.Count - 1; $Index -ge 0; $Index--) {
        if ($Lines[$Index] -match $Pattern) { $Lines.RemoveAt($Index) }
    }
}

function Get-VmxSetting {
    param([string[]]$Lines, [string]$Key)
    $Pattern = '^\s*' + [regex]::Escape($Key) + '\s*=\s*"([^"]*)"\s*$'
    $Values = @($Lines | ForEach-Object {
        if ($_ -match $Pattern) { $Matches[1] }
    })
    if ($Values.Count -gt 1) { throw "VMX contains duplicate setting: $Key" }
    if ($Values.Count -eq 0) { return $null }
    return [string]$Values[0]
}

function Write-VmxLines {
    param([Parameter(Mandatory = $true)][AllowEmptyCollection()][AllowEmptyString()][string[]]$Lines)
    $Vmx = [string]$script:Selected.VmxPath
    $Temporary = $Vmx + '.gpu-hal-tmp-' + [Guid]::NewGuid().ToString('N')
    $Backup = $Vmx + '.gpu-hal-bak-' + [Guid]::NewGuid().ToString('N')
    $Text = ($Lines -join "`r`n") + "`r`n"
    try {
        [IO.File]::WriteAllText($Temporary, $Text, [Text.UTF8Encoding]::new($false))
        [IO.File]::Replace($Temporary, $Vmx, $Backup)
    }
    finally {
        if (Test-Path -LiteralPath $Temporary -PathType Leaf) {
            Remove-Item -LiteralPath $Temporary -Force
        }
        if (Test-Path -LiteralPath $Backup -PathType Leaf) {
            Remove-Item -LiteralPath $Backup -Force
        }
    }
}

function Resolve-VmxRelativePath([string]$BaseDirectory, [string]$Path) {
    $Value = $Path.Replace('/', '\')
    if ([IO.Path]::IsPathRooted($Value)) { return [IO.Path]::GetFullPath($Value) }
    return [IO.Path]::GetFullPath((Join-Path $BaseDirectory $Value))
}

function Get-SafeRelativePath([string]$BaseDirectory, [string]$Path) {
    $Base = (Get-FullPath $BaseDirectory).TrimEnd('\') + '\'
    $Target = Get-FullPath $Path
    if (-not $Target.StartsWith($Base, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'A VMX summary path escaped the validated build root.'
    }
    return $Target.Substring($Base.Length)
}

function Get-SafeVmxSummary {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][AllowEmptyString()][Collections.Generic.List[string]]$Lines,
        [Parameter(Mandatory = $true)][string]$Vmx,
        [Parameter(Mandatory = $true)][string]$BuildRoot
    )
    $RawLines = $Lines.ToArray()
    $DiskPaths = [Collections.Generic.List[string]]::new()
    $VmxDirectory = Split-Path -Parent $Vmx
    foreach ($Line in $RawLines) {
        if ($Line -match '^\s*((?:scsi|sata|ide|nvme)\d+:\d+)\.fileName\s*=\s*"([^"]+\.vmdk)"\s*$') {
            $Resolved = Resolve-VmxRelativePath $VmxDirectory ([string]$Matches[2])
            if (-not (Test-PathWithin $BuildRoot $Resolved)) {
                throw 'A VM disk path escaped the private build workspace while creating the safe VMX summary.'
            }
            $DiskPaths.Add((Get-SafeRelativePath $BuildRoot $Resolved))
        }
    }
    $EthernetDevices = [Collections.Generic.List[object]]::new()
    foreach ($Line in $RawLines) {
        if ($Line -match '^\s*(ethernet\d+)\.present\s*=\s*"(TRUE|FALSE)"\s*$') {
            $EthernetDevices.Add([pscustomobject]@{
                Device = [string]$Matches[1]
                Present = [bool]::Parse([string]$Matches[2])
            })
        }
    }
    $SecureBoot = Get-VmxSetting $RawLines 'uefi.secureBoot.enabled'
    return [ordered]@{
        Firmware = Get-VmxSetting $RawLines 'firmware'
        VmxVersion = Get-VmxSetting $RawLines 'virtualHW.version'
        MemoryMiB = Get-VmxSetting $RawLines 'memsize'
        LogicalProcessors = Get-VmxSetting $RawLines 'numvcpus'
        CoresPerSocket = Get-VmxSetting $RawLines 'cpuid.coresPerSocket'
        NestedVirtualization = Get-VmxSetting $RawLines 'vhv.enable'
        SecureBootSetting = if ($null -eq $SecureBoot) { $null } else { [string]$SecureBoot }
        EthernetDevices = @($EthernetDevices)
        DiskRelativePaths = @($DiskPaths)
    }
}

function Get-VerifiedLiteralAssignmentValue {
    param([Parameter(Mandatory = $true)]
          [Management.Automation.Language.AssignmentStatementAst]$Assignment)
    if ($Assignment.Operator -ne [Management.Automation.Language.TokenKind]::Equals) {
        throw 'GuestInfo constants must use a plain assignment operator.'
    }
    $Node = $Assignment.Right
    while ($true) {
        if ($Node -is [Management.Automation.Language.StatementBlockAst]) {
            if ($Node.Statements.Count -ne 1) {
                throw 'GuestInfo constant assignment contains multiple statements.'
            }
            $Node = $Node.Statements[0]
            continue
        }
        if ($Node -is [Management.Automation.Language.PipelineAst]) {
            if ($Node.PipelineElements.Count -ne 1 -or $Node.Redirections.Count -ne 0) {
                throw 'GuestInfo constant assignment must contain one non-redirected expression.'
            }
            $Node = $Node.PipelineElements[0]
            continue
        }
        if ($Node -is [Management.Automation.Language.CommandExpressionAst]) {
            if ($Node.Redirections.Count -ne 0) {
                throw 'GuestInfo constant assignment may not use redirection.'
            }
            $Node = $Node.Expression
            continue
        }
        if ($Node -is [Management.Automation.Language.ConstantExpressionAst]) {
            return $Node.Value
        }
        throw 'GuestInfo constant right-hand side is not a single literal value.'
    }
}

function Import-VmwareGuestInfoConstants {
    param([Parameter(Mandatory = $true)]
          [Management.Automation.Language.NamedBlockAst]$HostRunnerEndBlock)
    $RequiredNames = @(
        'VmwareGuestInfoPrefix',
        'VmwareGuestInfoRequestManifest',
        'VmwareGuestInfoResponseManifest',
        'VmwareGuestInfoWorkerStatus',
        'VmwareGuestInfoRequestChunkPrefix',
        'VmwareGuestInfoResponseChunkPrefix',
        'VmwareGuestInfoDirectStage2Prefix',
        'VmwareGuestInfoExactStatus',
        'VmwareGuestInfoExactStatusGuestName',
        'VmwareGuestInfoChunkBytes',
        'VmwareGuestInfoChunkChars',
        'VmwareGuestInfoMaxPackageBytes',
        'VmwareGuestInfoMaxRequestChunks',
        'VmwareGuestInfoMaxEvidenceBytes',
        'VmwareGuestInfoMaxManifestChars',
        'VmwareGuestInfoTombstone'
    )
    $Allowed = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($Name in $RequiredNames) { [void]$Allowed.Add($Name) }
    $Seen = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)

    foreach ($Statement in $HostRunnerEndBlock.Statements) {
        if ($Statement -isnot [Management.Automation.Language.AssignmentStatementAst] -or
            $Statement.Left -isnot [Management.Automation.Language.VariableExpressionAst]) {
            continue
        }
        $UserPath = [string]$Statement.Left.VariablePath.UserPath
        if ($UserPath -notmatch '^script:VmwareGuestInfo([A-Za-z0-9]+)$') {
            continue
        }
        $Name = 'VmwareGuestInfo' + $Matches[1]
        if (-not $Allowed.Contains($Name)) {
            throw "Unrecognized top-level GuestInfo constant requires review: $Name"
        }
        if (-not $Seen.Add($Name)) {
            throw "HostRunner defines a duplicate GuestInfo constant: $Name"
        }
        [void](Get-VerifiedLiteralAssignmentValue -Assignment $Statement)
        . ([scriptblock]::Create($Statement.Extent.Text))
    }

    $Missing = @($RequiredNames | Where-Object { -not $Seen.Contains($_) })
    if ($Missing.Count -gt 0) {
        throw ('HostRunner is missing required literal GuestInfo constants: ' +
            ($Missing -join ', '))
    }
    foreach ($Name in $RequiredNames) {
        $Variable = Get-Variable -Name $Name -Scope Script -ErrorAction SilentlyContinue
        if ($null -eq $Variable -or $null -eq $Variable.Value) {
            throw "Required GuestInfo constant was not initialized: $Name"
        }
    }
    return ,$RequiredNames
}

function Assert-StandaloneDisk {
    param([Parameter(Mandatory = $true)][string]$DiskPath,
          [Parameter(Mandatory = $true)][string]$BuildRoot)
    $Current = Get-FullPath $DiskPath
    $Visited = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $ExpectedCid = $null
    $ExtentCount = 0
    $MaximumBackingDepth = 32
    $MaximumExtentCount = 128
    for ($Depth = 0; $Depth -lt $MaximumBackingDepth; $Depth++) {
        if (-not (Test-PathWithin $BuildRoot $Current) -or
            [IO.Path]::GetExtension($Current) -ine '.vmdk' -or
            -not (Test-Path -LiteralPath $Current -PathType Leaf)) {
            throw "VMDK backing descriptor is missing or outside the private build workspace: $Current"
        }
        if (-not $Visited.Add($Current)) {
            throw "VMDK backing chain contains a cycle: $Current"
        }
        Assert-NoReparsePathChain -Boundary $BuildRoot -Path $Current -Label 'VMDK descriptor path'
        Assert-NoReparsePoint $Current 'VMDK descriptor'
        Assert-SingleHardLink $Current 'VMDK descriptor'

        $Stream = [IO.File]::Open($Current, [IO.FileMode]::Open,
            [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            $Count = [int][Math]::Min([int64]1048576, $Stream.Length)
            $Buffer = [byte[]]::new($Count)
            if ($Count -gt 0) { [void]$Stream.Read($Buffer, 0, $Count) }
            $Header = [Text.Encoding]::ASCII.GetString($Buffer)
            [Array]::Clear($Buffer, 0, $Buffer.Length)
        }
        finally { $Stream.Dispose() }

        $CidLine = [regex]::Match($Header, '(?im)^\s*CID\s*=')
        $CidMatch = [regex]::Match($Header, '(?im)^\s*CID\s*=\s*"?([0-9a-f]{8})"?\s*(?:#.*)?$')
        if ($CidLine.Success -and -not $CidMatch.Success) {
            throw "VMDK descriptor has a malformed CID: $Current"
        }
        if ($null -ne $ExpectedCid) {
            if (-not $CidMatch.Success -or
                $CidMatch.Groups[1].Value -ine $ExpectedCid) {
                throw "VMDK parent CID does not match its child descriptor: $Current"
            }
            $ExpectedCid = $null
        }

        $ParentKey = [regex]::Match($Header, '(?im)^\s*parentFileNameHint\s*=')
        $ParentHint = [regex]::Match($Header,
            '(?im)^\s*parentFileNameHint\s*=\s*"([^"]*)"\s*$')
        $ParentCidKey = [regex]::Match($Header, '(?im)^\s*parentCID\s*=')
        $ParentCid = [regex]::Match($Header,
            '(?im)^\s*parentCID\s*=\s*"?([0-9a-f]{8})"?\s*(?:#.*)?$')
        if (($ParentKey.Success -and -not $ParentHint.Success) -or
            ($ParentCidKey.Success -and -not $ParentCid.Success)) {
            throw "VMDK descriptor has malformed parent metadata: $Current"
        }
        $HasParent = $ParentHint.Success -and $ParentHint.Groups[1].Value.Length -gt 0

        $ExtentLines = [regex]::Matches($Header,
            '(?im)^\s*(?:RW|RDONLY|NOACCESS)\s+[0-9]+\s+(?:ZERO\s*|\S+\s+.+)$')
        if ($ExtentLines.Count -eq 0) {
            throw "VMDK descriptor has no recognized extent entries: $Current"
        }
        foreach ($ExtentLine in $ExtentLines) {
            $ExtentEntry = [regex]::Match($ExtentLine.Value,
                '(?im)^\s*(?:RW|RDONLY|NOACCESS)\s+[0-9]+\s+\S+\s+"([^"]+)"(?:\s+[0-9]+)?\s*$')
            $ZeroExtent = [regex]::IsMatch($ExtentLine.Value,
                '(?im)^\s*(?:RW|RDONLY|NOACCESS)\s+[0-9]+\s+ZERO\s*$')
            if (-not $ExtentEntry.Success -and -not $ZeroExtent) {
                throw "VMDK descriptor contains an unsupported extent entry: $Current"
            }
            if ($ZeroExtent) { continue }
            $ExtentCount++
            if ($ExtentCount -gt $MaximumExtentCount) {
                throw 'VMDK backing chain exceeds the bounded extent count.'
            }
            $Extent = Resolve-VmxRelativePath (Split-Path -Parent $Current) `
                $ExtentEntry.Groups[1].Value
            if ([IO.Path]::GetExtension($Extent) -ine '.vmdk' -or
                -not (Test-PathWithin $BuildRoot $Extent) -or
                -not (Test-Path -LiteralPath $Extent -PathType Leaf)) {
                throw "VMDK extent is missing or outside the private build workspace: $Extent"
            }
            Assert-NoReparsePathChain -Boundary $BuildRoot -Path $Extent -Label 'VMDK extent path'
            Assert-NoReparsePoint $Extent 'VMDK extent'
            Assert-SingleHardLink $Extent 'VMDK extent'
        }

        if ($HasParent) {
            if (-not $ParentCid.Success -or
                $ParentCid.Groups[1].Value -ieq 'ffffffff') {
                throw "VMDK child descriptor lacks a valid parent CID: $Current"
            }
            $ParentPath = Resolve-VmxRelativePath (Split-Path -Parent $Current) `
                $ParentHint.Groups[1].Value
            if (-not (Test-PathWithin $BuildRoot $ParentPath) -or
                [IO.Path]::GetExtension($ParentPath) -ine '.vmdk') {
                throw "VMDK parent descriptor escapes the private build workspace: $ParentPath"
            }
            $ExpectedCid = $ParentCid.Groups[1].Value
            $Current = $ParentPath
            continue
        }
        if ($ParentCid.Success -and $ParentCid.Groups[1].Value -ine 'ffffffff') {
            throw "Root VMDK descriptor has a non-root parent CID: $Current"
        }
        return
    }
    throw "VMDK backing chain exceeds the maximum depth of ${MaximumBackingDepth}: $DiskPath"
}

function Assert-PreparedClone {
    param([string]$Root, [string]$BuildRoot, [string]$Vmx)
    if (-not (Test-Path -LiteralPath $Vmx -PathType Leaf) -or
        -not (Test-PathWithin $BuildRoot $Vmx)) {
        throw 'VmxPath must name a prepared disposable clone under WorkspaceRoot\.codex-build.'
    }
    Assert-NoReparsePathChain -Boundary $BuildRoot -Path $Vmx -Label 'Disposable VMX path'
    Assert-NoReparsePoint $Vmx 'Disposable VMX'
    Assert-SingleHardLink $Vmx 'Disposable VMX'
    $Lines = [IO.File]::ReadAllLines($Vmx)
    if ((Get-VmxSetting $Lines 'vhv.enable') -cne 'TRUE') {
        throw 'The prepared clone must have vhv.enable = "TRUE".'
    }
    if ($Lines -match '(?im)^\s*(?:pciPassthru\.|pciPassthru|physicalDrive|rawDisk)') {
        throw 'The prepared clone contains a PCI or physical disk passthrough setting.'
    }
    $DiskKeys = @($Lines | ForEach-Object {
        if ($_ -match '^\s*((?:scsi|sata|ide|nvme)\d+:\d+)\.fileName\s*=\s*"([^"]+\.vmdk)"\s*$') {
            [pscustomobject]@{ Device = $Matches[1]; Value = $Matches[2] }
        }
    })
    if ($DiskKeys.Count -lt 1) { throw 'The prepared clone has no attached VMDK.' }
    $VmxDirectory = Split-Path -Parent $Vmx
    foreach ($Disk in $DiskKeys) {
        $DiskPath = Resolve-VmxRelativePath $VmxDirectory ([string]$Disk.Value)
        Assert-StandaloneDisk -DiskPath $DiskPath -BuildRoot $BuildRoot
    }
    foreach ($Line in $Lines) {
        if ($Line -match '^\s*([A-Za-z0-9_.:-]+)\.fileName\s*=\s*"([^"]+)"\s*$') {
            $Key = [string]$Matches[1]
            if ($Key -match '^serial\d+$') { continue }
            $Value = [string]$Matches[2]
            if ($Value -and $Value -notmatch '(?i)\.iso$') {
                $Referenced = Resolve-VmxRelativePath $VmxDirectory $Value
                if (-not (Test-PathWithin $BuildRoot $Referenced)) {
                    throw "The clone references a file outside WorkspaceRoot\.codex-build: $Value"
                }
            }
        }
    }
    $NvramSetting = Get-VmxSetting $Lines 'nvram'
    if ($null -eq $NvramSetting) {
        $NvramPath = [IO.Path]::ChangeExtension($Vmx, '.nvram')
    }
    else {
        $NvramPath = Resolve-VmxRelativePath (Split-Path -Parent $Vmx) ([string]$NvramSetting)
    }
    if (Test-Path -LiteralPath $NvramPath -PathType Leaf) {
        if (-not (Test-PathWithin $BuildRoot $NvramPath)) {
            throw 'The clone NVRAM file must remain under WorkspaceRoot\.codex-build.'
        }
        Assert-NoReparsePathChain -Boundary $BuildRoot -Path $NvramPath -Label 'VM NVRAM path'
        Assert-NoReparsePoint $NvramPath 'VM NVRAM file'
        Assert-SingleHardLink $NvramPath 'VM NVRAM file'
    }
    elseif ($null -ne $NvramSetting) {
        throw 'The clone references a missing NVRAM file.'
    }
    return ,([Collections.Generic.List[string]]::new([string[]]$Lines))
}

function Configure-DisposableClone {
    param([Collections.Generic.List[string]]$Lines)
    $Original = Get-VmxSetting $Lines.ToArray() 'uefi.secureBoot.enabled'
    $script:OriginalSecureBootPresent = $null -ne $Original
    $script:OriginalSecureBootValue = if ($null -eq $Original) { $null } else { [string]$Original }
    if ($null -eq $Original) { $script:OriginalSecureBoot = $null }
    elseif ($Original -ieq 'TRUE') { $script:OriginalSecureBoot = $true }
    elseif ($Original -ieq 'FALSE') { $script:OriginalSecureBoot = $false }
    else { throw 'The clone Secure Boot setting must be TRUE or FALSE.' }
    if ($script:ValidationProfile -ceq 'SignedNative' -and $Original -ine 'TRUE') {
        throw 'SignedNative requires the provisioned clone VMX Secure Boot setting to remain TRUE.'
    }

    $script:Serial0Path = Join-Path $script:RunRoot 'efi-serial.log'
    $script:Serial1Path = Join-Path $script:RunRoot 'windows-session.log'
    foreach ($Path in @($script:Serial0Path, $script:Serial1Path)) {
        if (Test-Path -LiteralPath $Path) { throw "Serial capture path is not fresh: $Path" }
    }
    $Settings = [ordered]@{
        'ethernet0.present' = 'FALSE'
        'serial0.present' = 'TRUE'
        'serial0.fileType' = 'file'
        'serial0.fileName' = $script:Serial0Path
        'serial0.yieldOnMsrRead' = 'TRUE'
        'serial0.startConnected' = 'TRUE'
        'serial1.present' = 'TRUE'
        'serial1.fileType' = 'file'
        'serial1.fileName' = $script:Serial1Path
        'serial1.yieldOnMsrRead' = 'TRUE'
        'serial1.startConnected' = 'TRUE'
        'usb.present' = 'FALSE'
        'usb.autoConnect.device0' = 'FALSE'
        'sound.present' = 'FALSE'
        'mks.enable3d' = 'FALSE'
        'sharedFolder.maxNum' = '0'
        'isolation.tools.hgfs.disable' = 'TRUE'
    }
    foreach ($Key in $Settings.Keys) {
        Set-VmxSetting -Lines $Lines -Key ([string]$Key) -Value ([string]$Settings[$Key])
    }
    foreach ($Line in @($Lines.ToArray())) {
        if ($Line -match '^\s*(ethernet\d+\.present)\s*=') {
            Set-VmxSetting -Lines $Lines -Key $Matches[1] -Value 'FALSE'
        }
        elseif ($Line -match '^\s*(usb\.autoConnect\.device\d+)\s*=') {
            Set-VmxSetting -Lines $Lines -Key $Matches[1] -Value 'FALSE'
        }
        elseif ($Line -match '^\s*(sound\d+\.present)\s*=') {
            Set-VmxSetting -Lines $Lines -Key $Matches[1] -Value 'FALSE'
        }
        elseif ($Line -match '^\s*(sharedFolder\d+\.present)\s*=') {
            Set-VmxSetting -Lines $Lines -Key $Matches[1] -Value 'FALSE'
        }
        elseif ($Line -match '^\s*((?:ide|sata|scsi|nvme)\d+:\d+)\.deviceType\s*=\s*"(?:cdrom-image|atapi-cdrom)"') {
            $Device = $Matches[1]
            Set-VmxSetting -Lines $Lines -Key ($Device + '.present') -Value 'FALSE'
            Set-VmxSetting -Lines $Lines -Key ($Device + '.fileName') -Value ''
        }
        elseif ($Line -match '^\s*((?:ide|sata|scsi|nvme)\d+:\d+)\.fileName\s*=\s*"[^"]+\.iso"') {
            $Device = $Matches[1]
            Set-VmxSetting -Lines $Lines -Key ($Device + '.present') -Value 'FALSE'
            Set-VmxSetting -Lines $Lines -Key ($Device + '.fileName') -Value ''
        }
    }
    Write-VmxLines -Lines $Lines.ToArray()
}

function Set-CloneSecureBoot([bool]$Enabled) {
    $Lines = [Collections.Generic.List[string]]::new([IO.File]::ReadAllLines(
        [string]$script:Selected.VmxPath))
    Set-VmxSetting -Lines $Lines -Key 'uefi.secureBoot.enabled' `
        -Value $(if ($Enabled) { 'TRUE' } else { 'FALSE' })
    Write-VmxLines -Lines $Lines.ToArray()
}

function Restore-CloneSecureBoot {
    $Lines = [Collections.Generic.List[string]]::new([IO.File]::ReadAllLines(
        [string]$script:Selected.VmxPath))
    if ($script:OriginalSecureBootPresent) {
        Set-VmxSetting -Lines $Lines -Key 'uefi.secureBoot.enabled' `
            -Value ([string]$script:OriginalSecureBootValue)
    }
    else { Remove-VmxSetting -Lines $Lines -Key 'uefi.secureBoot.enabled' }
    Write-VmxLines -Lines $Lines.ToArray()
}

function Read-BoundedText([string]$Path, [int]$MaximumBytes = 33554432) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $File = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite)
    try {
        if ($File.Length -gt $MaximumBytes) { throw "Evidence file exceeds the read bound: $Path" }
        $Reader = [IO.StreamReader]::new($File, [Text.Encoding]::UTF8, $true, 4096, $true)
        try { return $Reader.ReadToEnd() } finally { $Reader.Dispose() }
    }
    finally { $File.Dispose() }
}

function Get-Sha256([string]$Path) {
    $Stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite)
    $Sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($Sha.ComputeHash($Stream))).Replace('-', '')
    }
    finally {
        $Sha.Dispose()
        $Stream.Dispose()
    }
}

function Get-SignedNativeArtifactEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$ExpectedSignerThumbprint
    )
    $Signature = Get-AuthenticodeSignature -FilePath $Path
    if ($null -eq $Signature.SignerCertificate) {
        throw "$Label does not contain an Authenticode signer certificate."
    }
    $ActualThumbprint = ([string]$Signature.SignerCertificate.Thumbprint).ToUpperInvariant()
    if ($ActualThumbprint -cne $ExpectedSignerThumbprint) {
        throw "$Label signer thumbprint does not match the supplied diagnostic certificate."
    }
    # NotTrusted can mean that the build host does not trust the test certificate
    # chain. The embedded signer must still match the pinned certificate exactly;
    # Secure Boot acceptance is proven later by the SignedNative guest boot.
    if ([string]$Signature.Status -cnotin @('Valid', 'NotTrusted')) {
        throw "$Label Authenticode signature is invalid: $($Signature.Status): $($Signature.StatusMessage)"
    }
    return [pscustomobject]@{
        Path = $Path
        Sha256 = Get-Sha256 $Path
        Bytes = (Get-Item -LiteralPath $Path -Force).Length
        SignatureStatus = [string]$Signature.Status
        SignatureStatusMessage = [string]$Signature.StatusMessage
        SignerThumbprint = $ActualThumbprint
    }
}

function Test-SignedNativeEfiMarkers([string]$Text) {
    if ([string]::IsNullOrEmpty($Text)) { return $false }
    return $Text.Contains('uefi hypervisor: DMA isolation admission: PASS') -and
        $Text.Contains('uefi hypervisor: BSP firmware return: PASS') -and
        $Text -notmatch '(?i)uefi hypervisor: .*?(?:FAIL|FATAL)|BSP virtualization failure:'
}

function Test-SignedNativePayloadEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$HandoffStatus,
        [Parameter(Mandatory = $true)][string]$PayloadDiagnostic
    )
    $Handoff = @{}
    foreach ($Line in ($HandoffStatus -split "`r?`n")) {
        if ($Line -match '^([a-z_]+)=([A-Za-z0-9_]+)$') {
            if ($Handoff.ContainsKey($Matches[1])) { return $false }
            $Handoff[$Matches[1]] = $Matches[2]
        }
        elseif (-not [string]::IsNullOrWhiteSpace($Line)) { return $false }
    }
    $Diagnostic = @{}
    foreach ($Line in ($PayloadDiagnostic -split "`r?`n")) {
        if ($Line -match '^([a-z_]+)=([A-Za-z0-9_]+|0x[0-9A-F]{16})$') {
            if ($Diagnostic.ContainsKey($Matches[1])) { return $false }
            $Diagnostic[$Matches[1]] = $Matches[2]
        }
        elseif (-not [string]::IsNullOrWhiteSpace($Line)) { return $false }
    }
    return $Handoff['schema'] -ceq '1' -and
        $Handoff['state'] -ceq 'READY_BOOT_MANAGER' -and
        $Handoff['restore'] -ceq 'PASS' -and
        $Handoff['payload'] -ceq 'PASS' -and
        $Diagnostic['schema'] -ceq '1' -and
        $Diagnostic['payload_launch'] -ceq 'ok' -and
        $Diagnostic['payload_status'] -ceq '0x0000000000000000' -and
        $Diagnostic['shim_load_phase'] -ceq 'firmware_load' -and
        $Diagnostic['shim_load_status'] -ceq '0x0000000000000000'
}

function Compress-FileBase64 {
    param([string]$Path, [int]$MaximumBytes, [string]$Label)
    $Item = Get-Item -LiteralPath $Path -Force
    if ($Item.Length -lt 1024 -or $Item.Length -gt $MaximumBytes) {
        throw "$Label input length is outside its bound."
    }
    Assert-NoReparsePoint $Path $Label
    $Bytes = [IO.File]::ReadAllBytes($Path)
    try {
        if ($Bytes[0] -ne 0x4d -or $Bytes[1] -ne 0x5a) {
            throw "$Label is not a PE image."
        }
        $Output = [IO.MemoryStream]::new()
        try {
            $Compressor = [IO.Compression.DeflateStream]::new(
                $Output, [IO.Compression.CompressionLevel]::Optimal, $true)
            try { $Compressor.Write($Bytes, 0, $Bytes.Length) }
            finally { $Compressor.Dispose() }
            return [Convert]::ToBase64String($Output.ToArray())
        }
        finally { $Output.Dispose() }
    }
    finally { [Array]::Clear($Bytes, 0, $Bytes.Length) }
}

function Render-GpuStartupTemplate {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Template,
        [Parameter(Mandatory = $true)][ValidateSet('Model', 'SignedNative')][string]$Profile
    )
    $Replacements = [ordered]@{
        '__GPU_VALIDATION_PROFILE__' = $Profile
        '__GPU_EXPECTED_SECURE_BOOT__' = [string]($Profile -ceq 'SignedNative')
    }
    foreach ($Token in $Replacements.Keys) {
        $First = $Template.IndexOf([string]$Token, [StringComparison]::Ordinal)
        if ($First -lt 0 -or $Template.IndexOf([string]$Token,
                $First + ([string]$Token).Length, [StringComparison]::Ordinal) -ge 0) {
            throw "GPU startup template token must appear exactly once: $Token"
        }
        $Template = $Template.Replace([string]$Token, [string]$Replacements[$Token])
    }
    if ($Template -match '__GPU_[A-Z0-9_]+__') {
        throw 'The rendered GPU startup template contains an unresolved token.'
    }
    return $Template
}

function Render-GpuGuestScript([string]$Mode, [string]$EfiHash) {
    $Template = [IO.File]::ReadAllText(
        (Join-Path $PSScriptRoot 'guest\Invoke-GpuHalWindowsGuest.ps1'),
        [Text.Encoding]::UTF8)
    $StartupBytes = [IO.File]::ReadAllBytes(
        (Join-Path $PSScriptRoot 'guest\Invoke-GpuHalWindowsStartup.ps1'))
    if ($StartupBytes.Length -lt 1 -or $StartupBytes.Length -gt 2097152) {
        [Array]::Clear($StartupBytes, 0, $StartupBytes.Length)
        throw 'The GPU startup template is outside its bounded size.'
    }
    try {
        $StartupText = Render-GpuStartupTemplate `
            -Template ([Text.UTF8Encoding]::new($false, $true).GetString($StartupBytes)) `
            -Profile $script:ValidationProfile
        $StartupRenderedBytes = [Text.UTF8Encoding]::new($false).GetBytes($StartupText)
        $StartupBase64 = [Convert]::ToBase64String($StartupRenderedBytes)
        [Array]::Clear($StartupRenderedBytes, 0, $StartupRenderedBytes.Length)
    }
    finally { [Array]::Clear($StartupBytes, 0, $StartupBytes.Length) }
    $ShimBase64 = ''
    $CertificateBase64 = ''
    $NativeEvidence = $script:SignedNativeArtifactEvidence
    if ($script:ValidationProfile -ceq 'SignedNative' -and $Mode -ceq 'Stage') {
        $ShimBase64 = Compress-FileBase64 $ShimPath 16777216 'Pinned vendor shim'
        $CertificateBytes = [IO.File]::ReadAllBytes($CertificatePath)
        try {
            if ($CertificateBytes.Length -eq 0 -or $CertificateBytes.Length -gt 65536) {
                throw 'The diagnostic signing certificate exceeds its 64 KiB bound.'
            }
            $CertificateBase64 = [Convert]::ToBase64String($CertificateBytes)
        }
        finally { [Array]::Clear($CertificateBytes, 0, $CertificateBytes.Length) }
    }
    $Replacements = [ordered]@{
        '__GPU_MODE__' = $Mode
        '__GPU_VALIDATION_PROFILE__' = $script:ValidationProfile
        '__GPU_LOADER_DEFLATE_BASE64__' = $(if ($Mode -ceq 'Stage') { Compress-FileBase64 $LoaderPath 16777216 'Loader' } else { '' })
        '__GPU_EFI_DEFLATE_BASE64__' = $(if ($Mode -ceq 'Stage') { Compress-FileBase64 $EfiPath 33554432 'EFI payload' } else { '' })
        '__GPU_INSTALLER_DEFLATE_BASE64__' = $(if ($Mode -ceq 'Stage') { Compress-FileBase64 $InstallerPath 16777216 'Installer' } else { '' })
        '__GPU_SHIM_DEFLATE_BASE64__' = $ShimBase64
        '__GPU_CERTIFICATE_BASE64__' = $CertificateBase64
        '__GPU_STARTUP_BASE64__' = $(if ($Mode -ceq 'Stage') { $StartupBase64 } else { '' })
        '__GPU_EXPECTED_BUILD__' = [string]$ExpectedBuild
        '__GPU_EXPECTED_UBR__' = [string]$ExpectedUbr
        '__GPU_EXPECTED_CPUS__' = [string]$ExpectedCpus
        '__GPU_EFI_SHA256__' = $EfiHash
        '__GPU_LOADER_SHA256__' = if ($null -ne $NativeEvidence) { [string]$NativeEvidence.Loader.Sha256 } else { '' }
        '__GPU_PAYLOAD_SHA256__' = if ($null -ne $NativeEvidence) { [string]$NativeEvidence.Payload.Sha256 } else { '' }
        '__GPU_SHIM_SHA256__' = if ($null -ne $NativeEvidence) { [string]$NativeEvidence.Shim.Sha256 } else { '' }
        '__GPU_CERTIFICATE_SHA256__' = if ($null -ne $NativeEvidence) { [string]$NativeEvidence.Certificate.Sha256 } else { '' }
        '__GPU_SIGNER_THUMBPRINT__' = if ($null -ne $NativeEvidence) { [string]$NativeEvidence.SignerThumbprint } else { '' }
        '__GPU_EXPECTED_SECURE_BOOT__' = [string]($script:ValidationProfile -ceq 'SignedNative')
    }
    foreach ($Token in $Replacements.Keys) {
        $First = $Template.IndexOf([string]$Token, [StringComparison]::Ordinal)
        if ($First -lt 0 -or $Template.IndexOf([string]$Token,
                $First + ([string]$Token).Length, [StringComparison]::Ordinal) -ge 0) {
            throw "GPU guest template token must appear exactly once: $Token"
        }
        $Template = $Template.Replace([string]$Token, [string]$Replacements[$Token])
    }
    if ($Template -match '__GPU_[A-Z0-9_]+__') {
        throw 'The rendered GPU guest template contains an unresolved token.'
    }
    return $Template
}

function New-GpuGuestPaths([string]$RunId) {
    $Root = Join-Path $script:RunRoot $RunId
    $Stage = Join-Path $Root 'stage'
    $Evidence = Join-Path $Root 'evidence'
    foreach ($Path in @($Root, $Stage, $Evidence)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
    return [pscustomobject]@{ RunRoot = $Root; Stage = $Stage; Evidence = $Evidence }
}

function New-GuestPackage {
    param([string]$Mode, [string]$EfiHash, [string]$RunId)
    $Paths = New-GpuGuestPaths $RunId
    Copy-Item -LiteralPath $ProbePath -Destination (
        Join-Path $Paths.Stage 'ib-tcg-dump.exe')
    $Rendered = Render-GpuGuestScript -Mode $Mode -EfiHash $EfiHash
    [IO.File]::WriteAllText((Join-Path $Paths.Stage 'Invoke-WindowsGuestTest.ps1'),
        $Rendered, [Text.UTF8Encoding]::new($false))
    $ProbeHash = Get-Sha256 (Join-Path $Paths.Stage 'ib-tcg-dump.exe')
    $GuestHash = Get-Sha256 (Join-Path $Paths.Stage 'Invoke-WindowsGuestTest.ps1')
    $ChecksumText = "$ProbeHash  ib-tcg-dump.exe`r`n$GuestHash  Invoke-WindowsGuestTest.ps1`r`n"
    [IO.File]::WriteAllText((Join-Path $Paths.Stage 'SHA256SUMS.txt'),
        $ChecksumText, [Text.UTF8Encoding]::new($false))
    $ExpectedNames = @('ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1', 'SHA256SUMS.txt')
    $ActualNames = @(Get-ChildItem -LiteralPath $Paths.Stage -File |
        ForEach-Object { $_.Name } | Sort-Object)
    if (($ActualNames -join "`n") -cne (($ExpectedNames | Sort-Object) -join "`n")) {
        throw 'The authenticated GPU package does not contain exactly its three allowlisted files.'
    }
    return $Paths
}

function Invoke-GpuGuestPhase {
    param([string]$Mode, [string]$RunId, [string]$EfiHash)
    if ($null -eq $script:VmBootStartedUtc) {
        throw 'GuestInfo request cannot be published without a recorded VM boot start.'
    }
    $Readiness = Wait-GpuGuestTransportReady -BootStartedUtc $script:VmBootStartedUtc `
        -TimeoutSeconds $script:Config.Policy.GuestReadyTimeoutSeconds
    $script:GuestWorkerReadiness.Add([pscustomobject]@{
        Mode = $Mode
        BootStartedUtc = $script:VmBootStartedUtc.ToUniversalTime().ToString('o')
        ToolsState = $Readiness.ToolsState
        WorkerState = $Readiness.WorkerState
        WorkerStartedUtc = $Readiness.WorkerStartedUtc
        WorkerUpdatedUtc = $Readiness.WorkerUpdatedUtc
    })
    $Paths = New-GuestPackage -Mode $Mode -EfiHash $EfiHash -RunId $RunId
    $script:Paths = $Paths
    $Transport = Publish-WindowsGuestInfoRequest -Selected $script:Selected `
        -Paths $Paths -RunId $RunId -GuestAction Probe
    $DiagnosticVariable = 'uefiloader.direct.stage2.' + $RunId
    $Transport.RequestVariableNames = @($Transport.RequestVariableNames) + $DiagnosticVariable
    $script:GuestInfoTransports.Add($Transport)
    $Response = Wait-WindowsGuestInfoEvidence -Selected $script:Selected `
        -Transport $Transport -Paths $Paths -RunId $RunId `
        -ExtendedPickup:($Mode -ceq 'Stage')
    $script:GuestInfoResponses.Add([pscustomobject]@{
        Transport = $Transport
        Response = $Response
    })
    if ([string]$Response.Result.Status -cne 'PASS' -or
        [string]$Response.Result.RunId -cne $RunId) {
        if ($Mode -ceq 'Inspect' -and
            [string]$Response.Result.Message -ceq 'The local RunId has already been used.') {
            # Preserve the failure while allowing an already-running read-only
            # inspector to emit diagnostics before snapshot recovery stops it.
            $DiagnosticDeadline = [DateTime]::UtcNow.AddSeconds(45)
            do {
                if ([regex]::Matches((Read-BoundedText $script:Serial1Path),
                        '(?m)^GPU_HAL_WINDOWS:SETUP_DIAG:').Count -ge 2) { break }
                Start-Sleep -Milliseconds 500
            } while ([DateTime]::UtcNow -lt $DiagnosticDeadline)
            $LateManifest = Invoke-VmwareGuestInfoRead -Selected $script:Selected `
                -VariableName ([string]$Transport.ResponseManifestName)
            if ($LateManifest -and $LateManifest.Length -le 65536) {
                [IO.File]::WriteAllText((Join-Path $script:RunRoot 'setup-late-response.base64.txt'),
                    $LateManifest, [Text.UTF8Encoding]::new($false))
            }
            $GuestDiagnostic = Invoke-VmwareGuestInfoRead -Selected $script:Selected `
                -VariableName $DiagnosticVariable
            if ($GuestDiagnostic -and $GuestDiagnostic.Length -le 65536) {
                [IO.File]::WriteAllText((Join-Path $script:RunRoot 'setup-guestvar-diagnostic.txt'),
                    $GuestDiagnostic, [Text.UTF8Encoding]::new($false))
            }
        }
        if ($Mode -cne 'Diagnose') {
            # A fresh authenticated read-only request preserves diagnostics
            # before snapshot recovery. Its result never changes the failure.
            try {
                $DiagnosticRunId = New-UhrRunId 'windows-probe'
                $null = Invoke-GpuGuestPhase -Mode 'Diagnose' `
                    -RunId $DiagnosticRunId -EfiHash $EfiHash
            }
            catch {
                [IO.File]::WriteAllText((Join-Path $script:RunRoot 'worker-diagnostic-failure.txt'),
                    $_.Exception.Message, [Text.UTF8Encoding]::new($false))
            }
        }
        throw "$Mode guest task did not return an authenticated PASS for ${RunId}: $($Response.Result.Message)"
    }
    Clear-VmwareGuestInfoVariables -Selected $script:Selected `
        -VariableNames @($Transport.RequestVariableNames +
            $Response.ResponseVariableNames)
    return $Paths
}

function Assert-GuestInspection($Paths, [string]$Phase) {
    $HostResult = Get-Content -Raw -LiteralPath (Join-Path $Paths.Evidence 'result.json') |
        ConvertFrom-Json
    if ([int]$HostResult.SchemaVersion -ne 1 -or
        [string]$HostResult.Status -cne 'PASS') {
        throw "$Phase GuestInfo response did not report PASS."
    }
    $Output = Read-BoundedText (Join-Path $Paths.Evidence 'stdout.txt') 33554432
    $BuildMatches = [regex]::Matches($Output,
        '(?m)^WindowsBuild=([0-9]+)\.([0-9]+)\r?$')
    $CpuMatches = [regex]::Matches($Output,
        '(?m)^ExpectedCpus=([0-9]+) ActualCpus=([0-9]+)\r?$')
    $SecureBootMatches = [regex]::Matches($Output,
        '(?m)^SecureBoot=(True|False)?\r?$')
    $BootManagerMatches = [regex]::Matches($Output,
        '(?m)^BootManagerSha256=([0-9A-F]{64})\r?$')
    if ($BuildMatches.Count -ne 1 -or $CpuMatches.Count -ne 1 -or
        $SecureBootMatches.Count -ne 1 -or $BootManagerMatches.Count -ne 1 -or
        [int]$BuildMatches[0].Groups[1].Value -ne $ExpectedBuild -or
        [int]$BuildMatches[0].Groups[2].Value -ne $ExpectedUbr -or
        [int]$CpuMatches[0].Groups[1].Value -ne $ExpectedCpus -or
        [int]$CpuMatches[0].Groups[2].Value -ne $ExpectedCpus) {
        throw "$Phase Windows inspection evidence did not match $ExpectedBuild.$ExpectedUbr with $ExpectedCpus CPUs."
    }
    $SecureBootText = [string]$SecureBootMatches[0].Groups[1].Value
    $SecureBootValue = if ([string]::IsNullOrEmpty($SecureBootText)) {
        $null
    } else { [bool]::Parse($SecureBootText) }
    if ($script:ValidationProfile -ceq 'SignedNative' -and
        $SecureBootValue -ne $true) {
        throw "$Phase guest inspection did not prove that Secure Boot is enabled."
    }
    # HostRunner synthesizes result.json from its authenticated response. The
    # guest's extended metadata is carried in the authenticated stdout archive.
    return [pscustomobject]@{
        RunId = [string]$HostResult.RunId
        Status = [string]$HostResult.Status
        WindowsBuild = [int]$BuildMatches[0].Groups[1].Value
        WindowsUbr = [int]$BuildMatches[0].Groups[2].Value
        ExpectedCpus = [int]$CpuMatches[0].Groups[1].Value
        ActualCpus = [int]$CpuMatches[0].Groups[2].Value
        SecureBoot = $SecureBootValue
        BootManagerSha256 = [string]$BootManagerMatches[0].Groups[1].Value
        OutputSha256 = Get-Sha256 (Join-Path $Paths.Evidence 'stdout.txt')
    }
}

function Clear-GpuGuestInfoRunState {
    foreach ($Transport in $script:GuestInfoTransports) {
        $Names = [Collections.Generic.List[string]]::new()
        foreach ($Name in @($Transport.RequestVariableNames)) {
            if (-not [string]::IsNullOrWhiteSpace([string]$Name)) { $Names.Add([string]$Name) }
        }
        foreach ($GuestResponse in $script:GuestInfoResponses) {
            if ([string]$GuestResponse.Transport.RunId -ceq [string]$Transport.RunId) {
                foreach ($Name in @($GuestResponse.Response.ResponseVariableNames)) {
                    if (-not [string]::IsNullOrWhiteSpace([string]$Name)) { $Names.Add([string]$Name) }
                }
            }
        }
        try {
            Clear-VmwareGuestInfoVariables -Selected $script:Selected `
                -VariableNames $Names.ToArray()
        }
        catch { $script:CleanupFailures.Add('GuestInfo cleanup failed: ' + $_.Exception.Message) }
    }
}

function Capture-CloneScreen {
    try { if (-not (Test-TargetVmRunning)) { return } }
    catch {
        $script:ScreenCaptureStatus = 'UNAVAILABLE: ' + $_.Exception.Message
        return
    }
    $script:ScreenCapturePath = Join-Path $script:RunRoot 'windows-screen.png'
    try {
        [void](Invoke-TargetVmrun -Operation @('captureScreen',
                [string]$script:Selected.VmxPath, $script:ScreenCapturePath) -Timeout 30)
        if (Test-Path -LiteralPath $script:ScreenCapturePath -PathType Leaf) {
            $Capture = Get-Item -LiteralPath $script:ScreenCapturePath
            if ($Capture.Length -gt 16777216) {
                Remove-Item -LiteralPath $script:ScreenCapturePath -Force
                throw 'VM screen capture exceeded the 16 MiB evidence bound.'
            }
            $script:ScreenCaptureStatus = 'CAPTURED'
        } else { $script:ScreenCaptureStatus = 'UNAVAILABLE' }
    }
    catch {
        $script:ScreenCaptureStatus = 'UNAVAILABLE: ' + $_.Exception.Message
        $script:ScreenCapturePath = $null
    }
}

function Copy-CloneVmwareLog {
    $Source = Join-Path (Split-Path -Parent ([string]$script:Selected.VmxPath)) 'vmware.log'
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        $script:VmwareLogStatus = 'NOT_FOUND'
        return
    }
    Assert-NoReparsePoint $Source 'Clone VMware log'
    $Log = Get-Item -LiteralPath $Source
    if ($Log.Length -gt 33554432) {
        $script:VmwareLogStatus = 'EXCEEDS_32_MIB_BOUND'
        return
    }
    $script:VmwareLogPath = Join-Path $script:RunRoot 'vmware.log'
    Copy-Item -LiteralPath $Source -Destination $script:VmwareLogPath
    $script:VmwareLogStatus = 'COPIED'
}

function Confirm-SignedNativeVmwareDbAppend {
    if ($script:ValidationProfile -cne 'SignedNative') { return }
    $LogText = $null
    if ($script:VmwareLogStatus -ceq 'COPIED' -and $script:VmwareLogPath -and
        (Test-Path -LiteralPath $script:VmwareLogPath -PathType Leaf)) {
        $LogText = [IO.File]::ReadAllText($script:VmwareLogPath)
    }
    if ($null -ne $LogText -and
        $LogText -match '(?i)Added uefi\.secureBoot\.dbDefault\.file0 to dbDefault') {
        $script:VmwareDbAppendMarker = 'PASS_LOG_MARKER'
        return
    }
    $script:VmwareDbAppendMarker = 'NOT_CONFIRMED'
    if ([string]::IsNullOrWhiteSpace($script:Failure)) {
        $script:Failure = 'VMware log did not confirm uefi.secureBoot.dbDefault.file0 was appended to dbDefault.'
    }
}

function Test-ConstantHex([string]$Left, [string]$Right) {
    if ($Left -cnotmatch '^[0-9A-F]{64}$' -or $Right -cnotmatch '^[0-9A-F]{64}$') {
        return $false
    }
    $Different = 0
    for ($Index = 0; $Index -lt 64; $Index++) {
        $Different = $Different -bor ([int][char]$Left[$Index] -bxor [int][char]$Right[$Index])
    }
    return $Different -eq 0
}

function Get-AuthenticatedWindowsResult {
    param([string]$SerialText, [string]$ExpectedRunId)
    $Key = Get-MailboxKeyBytes $script:Selected
    try {
        $Frames = [regex]::Matches($SerialText,
            '(?m)^GPU_HAL_WINDOWS:RESULT:([A-Za-z0-9+/=]+)\.([0-9A-F]{64})\r?$')
        foreach ($Frame in $Frames) {
            $PayloadBytes = $null
            $Hmac = $null
            $Digest = $null
            try {
                $PayloadBytes = [Convert]::FromBase64String($Frame.Groups[1].Value)
                if ([Convert]::ToBase64String($PayloadBytes) -cne $Frame.Groups[1].Value) {
                    continue
                }
                $Hmac = [Security.Cryptography.HMACSHA256]::new($Key)
                $Digest = $Hmac.ComputeHash($PayloadBytes)
                $ActualHex = ([BitConverter]::ToString($Digest)).Replace('-', '')
                if (-not (Test-ConstantHex $ActualHex $Frame.Groups[2].Value)) { continue }
                $Json = [Text.UTF8Encoding]::new($false, $true).GetString($PayloadBytes)
                $Value = $Json | ConvertFrom-Json
                $Sha = [Security.Cryptography.SHA256]::Create()
                try { $PayloadHash = $Sha.ComputeHash($PayloadBytes) }
                finally { $Sha.Dispose() }
                try { $PayloadHashText = ([BitConverter]::ToString($PayloadHash)).Replace('-', '') }
                finally { [Array]::Clear($PayloadHash, 0, $PayloadHash.Length) }
                $Evidence = [pscustomobject]@{
                    Result = $Value
                    PayloadSha256 = $PayloadHashText
                    PayloadBytes = $PayloadBytes.Length
                    HmacSha256 = $ActualHex
                }
                if ([int]$Value.SchemaVersion -ne 1 -or
                    [string]$Value.RunId -cne $ExpectedRunId) { continue }
                $script:StartupEvidence = [ordered]@{
                    RunId = [string]$Value.RunId
                    ValidationProfile = [string]$Value.ValidationProfile
                    Status = [string]$Value.Status
                    SecureBoot = $Value.SecureBoot
                    ExpectedSecureBoot = $Value.ExpectedSecureBoot
                    ExpectedCertificateDerPresent = $Value.ExpectedCertificateDerPresent
                    ExpectedCertificateSha256 = [string]$Value.ExpectedCertificateSha256
                    CertificateDbInspection = [string]$Value.CertificateDbInspection
                    HandoffStatus = [string]$Value.HandoffStatus
                    HandoffStatusBytes = $Value.HandoffStatusBytes
                    HandoffStatusCapturedBytes = $Value.HandoffStatusCapturedBytes
                    HandoffStatusTooLarge = $Value.HandoffStatusTooLarge
                    PayloadDiagnostic = [string]$Value.PayloadDiagnostic
                    PayloadDiagnosticBytes = $Value.PayloadDiagnosticBytes
                    PayloadDiagnosticCapturedBytes = $Value.PayloadDiagnosticCapturedBytes
                    PayloadDiagnosticTooLarge = $Value.PayloadDiagnosticTooLarge
                    DiagnosticTextEncoding = [string]$Value.DiagnosticTextEncoding
                    PayloadSha256 = $PayloadHashText
                    PayloadBytes = $PayloadBytes.Length
                    HmacSha256 = $ActualHex
                }
                if ([string]$Value.Status -ceq 'FAIL') {
                    $script:StartupStatus = 'FAIL_AUTHENTICATED_GUEST'
                    $Message = [string]$Value.Message
                    if ($script:ValidationProfile -ceq 'SignedNative') {
                        $Message += "`nHandoffStatus:`n$([string]$Value.HandoffStatus)`nPayloadDiagnostic:`n$([string]$Value.PayloadDiagnostic)"
                    }
                    throw ('The authenticated Windows startup result failed: ' + $Message)
                }
                $CommonValid = [string]$Value.Status -ceq 'PASS' -and
                    [int]$Value.CurrentBuild -eq $ExpectedBuild -and
                    [int]$Value.Ubr -eq $ExpectedUbr -and
                    [int]$Value.ExpectedCpus -eq $ExpectedCpus -and
                    [int]$Value.CpuPassCount -eq $ExpectedCpus -and
                    $Value.HealthPassed -is [bool] -and [bool]$Value.HealthPassed -and
                    $Value.BootManagerUnchanged -is [bool] -and
                    [bool]$Value.BootManagerUnchanged -and
                    [string]$Value.BootManagerSha256Before -cmatch '^[0-9A-F]{64}$' -and
                    [string]$Value.BootManagerSha256After -ceq
                        [string]$Value.BootManagerSha256Before
                if (-not $CommonValid) {
                    $script:StartupStatus = 'FAIL_AUTHENTICATED_RESULT_VALIDATION'
                    throw 'The authenticated Windows startup result failed its exact identity, build, CPU, health, or boot-manager checks.'
                }
                if ([string]$Value.ValidationProfile -cne $script:ValidationProfile) {
                    $script:StartupStatus = 'FAIL_PROFILE_MISMATCH'
                    throw 'The authenticated Windows startup result used a different validation profile.'
                }
                if ($script:ValidationProfile -ceq 'SignedNative') {
                    $NativeValid = $Value.ExpectedSecureBoot -is [bool] -and
                        [bool]$Value.ExpectedSecureBoot -and
                        $Value.SecureBoot -is [bool] -and [bool]$Value.SecureBoot -and
                        $Value.ExpectedCertificateDerPresent -is [bool] -and
                        [bool]$Value.ExpectedCertificateDerPresent -and
                        [string]$Value.CertificateDbInspection -ceq
                            'BOUNDED_RAW_DER_SUBSEQUENCE_ONLY' -and
                        [string]$Value.ExpectedCertificateSha256 -ceq
                            [string]$script:SignedNativeArtifactEvidence.Certificate.Sha256 -and
                        $Value.HandoffStatusBytes -is [ValueType] -and
                        [long]$Value.HandoffStatusBytes -ge 0 -and
                        [long]$Value.HandoffStatusBytes -le 4096 -and
                        $Value.HandoffStatusCapturedBytes -is [ValueType] -and
                        [int]$Value.HandoffStatusCapturedBytes -eq [long]$Value.HandoffStatusBytes -and
                        $Value.HandoffStatusTooLarge -is [bool] -and
                        -not [bool]$Value.HandoffStatusTooLarge -and
                        $Value.PayloadDiagnosticBytes -is [ValueType] -and
                        [long]$Value.PayloadDiagnosticBytes -ge 0 -and
                        [long]$Value.PayloadDiagnosticBytes -le 4096 -and
                        $Value.PayloadDiagnosticCapturedBytes -is [ValueType] -and
                        [int]$Value.PayloadDiagnosticCapturedBytes -eq [long]$Value.PayloadDiagnosticBytes -and
                        $Value.PayloadDiagnosticTooLarge -is [bool] -and
                        -not [bool]$Value.PayloadDiagnosticTooLarge -and
                        [string]$Value.DiagnosticTextEncoding -ceq 'ISO-8859-1 byte-preserving' -and
                        (Test-SignedNativePayloadEvidence `
                            -HandoffStatus ([string]$Value.HandoffStatus) `
                            -PayloadDiagnostic ([string]$Value.PayloadDiagnostic))
                    if (-not $NativeValid) {
                        $script:StartupStatus = 'FAIL_SIGNED_NATIVE_EVIDENCE'
                        throw ('The authenticated SignedNative startup result failed Secure Boot, db certificate, ' +
                            'handoff, or payload-launch evidence checks. HandoffStatus=' +
                            [string]$Value.HandoffStatus + '; PayloadDiagnostic=' +
                            [string]$Value.PayloadDiagnostic)
                    }
                } else {
                    $ModelValid = $Value.ExpectedSecureBoot -is [bool] -and
                        -not [bool]$Value.ExpectedSecureBoot -and
                        $Value.SecureBoot -is [bool] -and -not [bool]$Value.SecureBoot
                    if (-not $ModelValid) {
                        $script:StartupStatus = 'FAIL_MODEL_SECURE_BOOT'
                        throw 'The authenticated Model startup result did not confirm its expected Secure Boot state.'
                    }
                }
                return $Evidence
            }
            catch {
                if ($_.Exception.Message -like 'The authenticated Windows startup result*') { throw }
                if ($_.Exception.Message -like 'The authenticated SignedNative startup result*' -or
                    $_.Exception.Message -like 'The authenticated Model startup result*' -or
                    $_.Exception.Message -like 'The authenticated Windows startup result used*') { throw }
            }
            finally {
                if ($null -ne $Digest) { [Array]::Clear($Digest, 0, $Digest.Length) }
                if ($null -ne $Hmac) { $Hmac.Dispose() }
                if ($null -ne $PayloadBytes) { [Array]::Clear($PayloadBytes, 0, $PayloadBytes.Length) }
            }
        }
    }
    finally { [Array]::Clear($Key, 0, $Key.Length) }
    return $null
}

function Test-EfiMarkers([string]$Text) {
    $Required = @(
        'GPU_HAL_MODEL:ALL_PASS',
        'GPU_ENFORCEMENT_MODEL:ALL_PASS SYNTHETIC_ONLY',
        'GPU_HAL_VM:ROOT_WC_NO_BACKEND PASS',
        'GPU_HAL_VM:ROOT_WC_BOUNDS PASS',
        'GPU_HAL_VM:ROOT_WC_RENDER PASS',
        'GPU_HAL_VM:ROOT_WC_UNMAPPED PASS',
        'GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS')
    foreach ($Marker in $Required) {
        if (-not $Text.Contains($Marker)) { return $false }
    }
    if ($Text -match 'GPU_HAL_MODEL:FAIL|GPU_ENFORCEMENT_MODEL:FAIL|(?i)uefi hypervisor: .*?(?:FAIL|FATAL)|BSP virtualization failure:|GPU_HAL_VM:.*? FAIL') {
        throw 'EFI serial output contains a HAL, model, or hypervisor failure marker.'
    }
    return $true
}

function Wait-WindowsVmEvidence([string]$StartupRunId) {
    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $LastEfi = ''
    $LastWindows = ''
    $Startup = $null
    do {
        $LastEfi = Read-BoundedText $script:Serial0Path
        $LastWindows = Read-BoundedText $script:Serial1Path
        if ($LastEfi -match 'GPU_HAL_MODEL:FAIL|GPU_ENFORCEMENT_MODEL:FAIL|(?i)uefi hypervisor: .*?(?:FAIL|FATAL)|BSP virtualization failure:|GPU_HAL_VM:.*? FAIL') {
            throw 'EFI serial output reported a fatal GPU HAL or VM failure.'
        }
        $EfiPass = if ($script:ValidationProfile -ceq 'SignedNative') {
            Test-SignedNativeEfiMarkers $LastEfi
        } else { Test-EfiMarkers $LastEfi }
        if ($EfiPass) { $script:EfiStatus = 'PASS' }
        # Surface a signed guest failure even when the EFI chain never ran.
        $Startup = Get-AuthenticatedWindowsResult -SerialText $LastWindows `
            -ExpectedRunId $StartupRunId
        if ($EfiPass) {
            if ($null -ne $Startup) {
                $script:StartupStatus = 'PASS'
                break
            }
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $Deadline)
    if ($script:EfiStatus -cne 'PASS') {
        $ExpectedMarkers = if ($script:ValidationProfile -ceq 'SignedNative') {
            'DMA isolation admission and BSP firmware return'
        } else { 'EFI model, ownership, and root-WC' }
        throw "Timed out waiting for $ExpectedMarkers markers; see $script:Serial0Path."
    }
    if ($null -eq $Startup) {
        throw "Timed out waiting for authenticated Windows COM2 startup evidence; see $script:Serial1Path."
    }
    $ObservedMarkers = if ($script:ValidationProfile -ceq 'SignedNative') {
        @('uefi hypervisor: DMA isolation admission: PASS',
            'uefi hypervisor: BSP firmware return: PASS')
    } else {
        @('GPU_HAL_MODEL:ALL_PASS',
            'GPU_ENFORCEMENT_MODEL:ALL_PASS SYNTHETIC_ONLY',
            'GPU_HAL_VM:ROOT_WC_NO_BACKEND PASS',
            'GPU_HAL_VM:ROOT_WC_BOUNDS PASS',
            'GPU_HAL_VM:ROOT_WC_RENDER PASS',
            'GPU_HAL_VM:ROOT_WC_UNMAPPED PASS',
            'GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS')
    }
    return [pscustomobject]@{
        ValidationProfile = $script:ValidationProfile
        EfiMarkers = $ObservedMarkers
        Startup = $Startup
        EfiSerialSha256 = if (Test-Path -LiteralPath $script:Serial0Path -PathType Leaf) {
            Get-Sha256 $script:Serial0Path
        } else { $null }
        WindowsSerialSha256 = if (Test-Path -LiteralPath $script:Serial1Path -PathType Leaf) {
            Get-Sha256 $script:Serial1Path
        } else { $null }
    }
}

function Stop-OnlyTargetVm {
    if (-not $script:VmStartAttempted) {
        $script:VmOffConfirmed = $true
        return
    }
    $Running = $false
    try { $Running = Test-TargetVmRunning }
    catch { $script:CleanupFailures.Add('Could not establish target VM running state before stop: ' + $_.Exception.Message) }
    if ($Running) {
        try { [void](Invoke-TargetVmrun -Operation @('stop',
                [string]$script:Selected.VmxPath, 'soft') -Timeout 90) }
        catch { $script:StopEvents.Add('Soft stop reported: ' + $_.Exception.Message) }
        $StopDeadline = [DateTime]::UtcNow.AddSeconds(30)
        do {
            try {
                if (-not (Test-TargetVmRunning)) { $Running = $false; break }
            }
            catch { $script:CleanupFailures.Add('Could not verify soft stop: ' + $_.Exception.Message); break }
            Start-Sleep -Seconds 1
        } while ([DateTime]::UtcNow -lt $StopDeadline)
        if ($Running) {
            try { [void](Invoke-TargetVmrun -Operation @('stop',
                    [string]$script:Selected.VmxPath, 'hard') -Timeout 30) }
            catch { $script:StopEvents.Add('Bounded hard stop reported: ' + $_.Exception.Message) }
        }
    }
    try {
        $script:VmOffConfirmed = -not (Test-TargetVmRunning)
    }
    catch {
        $script:VmOffConfirmed = $false
        $script:CleanupFailures.Add('Could not confirm target VM stopped: ' + $_.Exception.Message)
    }
    if (-not $script:VmOffConfirmed) {
        $script:CleanupFailures.Add('Target VM is still running; snapshot recovery was skipped.')
    }
}

function Save-RunResult {
    if ($null -eq $script:RunRoot -or -not (Test-Path -LiteralPath $script:RunRoot -PathType Container)) {
        return
    }
    if ($script:ValidationProfile -ceq 'SignedNative' -and
        $script:VmwareDbAppendMarker -cne 'PASS_LOG_MARKER' -and
        [string]::IsNullOrWhiteSpace($script:Failure)) {
        $script:Failure = 'VMware log did not confirm uefi.secureBoot.dbDefault.file0 was appended to dbDefault.'
    }
    if ($script:ValidationProfile -ceq 'SignedNative' -and
        $null -ne $script:HalResults -and $script:HalResults -is [Collections.IDictionary]) {
        $script:HalResults['VmwareDbAppendMarker'] = $script:VmwareDbAppendMarker
        if ($script:VmwareDbAppendMarker -cne 'PASS_LOG_MARKER') {
            $script:HalResults['DmaIsolationAdmission'] = 'EXTERNAL_VMM_ADMISSION_NOT_CONFIRMED'
        }
    }
    $Passed = -not $script:Failure -and $script:CleanupFailures.Count -eq 0 -and
        $script:InspectStatus -eq 'PASS' -and $script:StageStatus -eq 'PASS' -and
        $script:EfiStatus -eq 'PASS' -and $script:StartupStatus -eq 'PASS' -and
        $script:BeforeSnapshotCreated -and $script:AfterSnapshotCreated -and
        $script:RevertedBefore
    $script:ModelPassed = if ($script:ValidationProfile -ceq 'Model') { [bool]$Passed } else { $null }
    $script:SignedNativePassed = if ($script:ValidationProfile -ceq 'SignedNative') { [bool]$Passed } else { $null }
    $HalResults = if ($null -ne $script:HalResults) { $script:HalResults }
        elseif ($script:ValidationProfile -ceq 'SignedNative') {
            [ordered]@{
                SignedNativeEfiAdmissionMarkers = if ($script:EfiStatus -ceq 'PASS') { 'PASS' } else { 'NOT_CONFIRMED' }
                DmaIsolationAdmission = if ($script:EfiStatus -ceq 'PASS') {
                    'EXTERNAL_VMM_ADMISSION'
                } else { 'EXTERNAL_VMM_ADMISSION_NOT_CONFIRMED' }
                NativeGpuBackend = 'NOT_EXERCISED_EXTERNAL_VMM'
                SyntheticEnforcementModel = 'NOT_EXERCISED'
                ActualVmxRootWcRenderDrainAndUnmap = 'NOT_EXERCISED_EXTERNAL_VMM'
                WindowsStartupHealth = 'NOT_CONFIRMED'
                IntelPhysicalPlaneAndDmaEnforcement = 'NOT_TESTED_BY_THIS_VM'
                HostDualGpuTopology = 'NOT_TESTED_BY_THIS_VM'
            }
        } else {
            [ordered]@{
                SyntheticEnforcementModel = 'NOT_CONFIRMED'
                ActualVmxRootWcRenderDrainAndUnmap = 'NOT_CONFIRMED'
                WindowsStartupHealth = 'NOT_CONFIRMED'
                IntelPhysicalPlaneAndDmaEnforcement = 'NOT_TESTED_BY_THIS_VM'
                HostDualGpuTopology = 'NOT_TESTED_BY_THIS_VM'
            }
        }
    $Result = [ordered]@{
        SchemaVersion = 1
        RunId = $script:RunId
        ValidationProfile = $script:ValidationProfile
        SecureBootTrust = if ($script:ValidationProfile -ceq 'SignedNative') {
            'VMWARE_FIRMWARE_DB_CERTIFICATE; DISTINCT_FROM_HOST_MOK'
        } else { 'MODEL_PROFILE' }
        VmwareDbAppendMarker = $script:VmwareDbAppendMarker
        Result = if ($Passed) { 'PASS' } else { 'FAIL' }
        ModelPassed = $script:ModelPassed
        SignedNativePassed = $script:SignedNativePassed
        SignedNativeArtifacts = if ($script:ValidationProfile -ceq 'SignedNative') {
            $script:SignedNativeArtifactEvidence
        } else { $null }
        Failure = $script:Failure
        FailureScriptStackTrace = $script:FailureScriptStackTrace
        CleanupFailures = @($script:CleanupFailures)
        StopEvents = @($script:StopEvents)
        Vm = [ordered]@{
            VmxPath = if ($null -ne $script:Selected) {
                [string]$script:Selected.VmxPath
            } else { $null }
            ValidationProfile = $script:ValidationProfile
            Build = $ExpectedBuild
            Ubr = $ExpectedUbr
            LogicalCpus = $ExpectedCpus
            NestedVirtualization = $true
            Network = 'disabled'
            Passthrough = 'none'
            OriginalSecureBoot = $script:OriginalSecureBoot
            ExpectedSecureBoot = ($script:ValidationProfile -ceq 'SignedNative')
            SecureBootChangedOnlyAfterStage = $script:SecureBootChanged
        }
        Phases = [ordered]@{
            Inspect = $script:InspectStatus
            Stage = $script:StageStatus
            EfiAndRootWc = if ($script:ValidationProfile -ceq 'Model') { $script:EfiStatus } else { 'NOT_APPLICABLE' }
            SignedNativeEfi = if ($script:ValidationProfile -ceq 'SignedNative') { $script:EfiStatus } else { 'NOT_APPLICABLE' }
            WindowsStartup = $script:StartupStatus
        }
        GuestInspection = [ordered]@{
            Inspect = if ($null -ne $script:InspectEvidence) { $script:InspectEvidence } else { $null }
            Stage = if ($null -ne $script:StageEvidence) { $script:StageEvidence } else { $null }
        }
        GuestInfoWorkerReadiness = @($script:GuestWorkerReadiness)
        HalResults = $HalResults
        WindowsStartupEvidence = if ($null -ne $script:StartupEvidence) { $script:StartupEvidence } else { $null }
        RunIds = [ordered]@{ Inspect = $script:InspectRunId; Stage = $script:StageRunId }
        Snapshots = [ordered]@{
            Before = $script:SnapshotBefore
            BeforeCreated = $script:BeforeSnapshotCreated
            After = $script:SnapshotAfter
            AfterCreated = $script:AfterSnapshotCreated
            RevertedBefore = $script:RevertedBefore
            VmOffConfirmed = $script:VmOffConfirmed
        }
        Evidence = [ordered]@{
            EfiSerialPath = $script:Serial0Path
            EfiSerialSha256 = if ($script:Serial0Path -and
                (Test-Path -LiteralPath $script:Serial0Path -PathType Leaf)) { Get-Sha256 $script:Serial0Path } else { $null }
            WindowsSerialPath = $script:Serial1Path
            WindowsSerialSha256 = if ($script:Serial1Path -and
                (Test-Path -LiteralPath $script:Serial1Path -PathType Leaf)) { Get-Sha256 $script:Serial1Path } else { $null }
            ScreenCaptureStatus = $script:ScreenCaptureStatus
            ScreenCapturePath = $script:ScreenCapturePath
            ScreenCaptureSha256 = if ($script:ScreenCapturePath -and
                (Test-Path -LiteralPath $script:ScreenCapturePath -PathType Leaf)) { Get-Sha256 $script:ScreenCapturePath } else { $null }
            VmwareLogPath = $script:VmwareLogPath
            VmwareLogStatus = $script:VmwareLogStatus
            VmwareDbAppendMarker = $script:VmwareDbAppendMarker
            VmwareLogSha256 = if ($script:VmwareLogPath -and
                (Test-Path -LiteralPath $script:VmwareLogPath -PathType Leaf)) { Get-Sha256 $script:VmwareLogPath } else { $null }
            Commands = $script:CommandLog
            CredentialsRecorded = $false
            HmacKeyRecorded = $false
        }
        CompletedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $script:ResultObject = [pscustomobject]$Result
    [IO.File]::WriteAllText((Join-Path $script:RunRoot 'result.json'),
        ($script:ResultObject | ConvertTo-Json -Depth 10),
        [Text.UTF8Encoding]::new($false))
}

try {
    $script:ValidationProfile = $ValidationProfile
    $WorkspaceRoot = Get-FullPath $WorkspaceRoot
    $BuildRoot = Join-Path $WorkspaceRoot '.codex-build'
    $VmxPath = Get-FullPath $VmxPath
    $RunDirectory = Get-FullPath $RunDirectory
    $CredentialStateRoot = Get-FullPath $CredentialStateRoot
    $EfiPath = Get-FullPath $EfiPath
    $LoaderPath = Get-FullPath $LoaderPath
    $InstallerPath = Get-FullPath $InstallerPath
    $ProbePath = Get-FullPath $ProbePath
    $VmrunPath = Get-FullPath $VmrunPath
    if ($script:ValidationProfile -ceq 'SignedNative') {
        if ([string]::IsNullOrWhiteSpace($ShimPath) -or
            [string]::IsNullOrWhiteSpace($CertificatePath)) {
            throw 'SignedNative requires both ShimPath and CertificatePath.'
        }
        $ShimPath = Get-FullPath $ShimPath
        $CertificatePath = Get-FullPath $CertificatePath
    }

    if (-not (Test-Path -LiteralPath $WorkspaceRoot -PathType Container) -or
        -not (Test-Path -LiteralPath $BuildRoot -PathType Container)) {
        throw 'WorkspaceRoot and its .codex-build directory must already exist.'
    }
    if (-not (Test-PathWithin $WorkspaceRoot $BuildRoot) -or
        -not (Test-PathWithin $BuildRoot $VmxPath) -or
        -not (Test-PathWithin $BuildRoot $RunDirectory)) {
        throw 'The disposable VMX and fresh RunDirectory must remain under WorkspaceRoot\.codex-build.'
    }
    Assert-NoReparsePathChain -Boundary $WorkspaceRoot -Path $BuildRoot -Label 'Build root path'
    Assert-NoReparsePathChain -Boundary $BuildRoot -Path $RunDirectory -Label 'RunDirectory path'
    if (Test-Path -LiteralPath $RunDirectory) {
        throw 'RunDirectory must be a fresh, nonexistent directory.'
    }
    $InputPaths = [Collections.Generic.List[string]]::new()
    foreach ($InputPath in @($EfiPath, $LoaderPath, $InstallerPath, $ProbePath,
            (Join-Path $PSScriptRoot 'guest\Invoke-GpuHalWindowsGuest.ps1'),
            (Join-Path $PSScriptRoot 'guest\Invoke-GpuHalWindowsStartup.ps1'))) {
        $InputPaths.Add([string]$InputPath)
    }
    if ($script:ValidationProfile -ceq 'SignedNative') {
        $InputPaths.Add($ShimPath)
        $InputPaths.Add($CertificatePath)
    }
    foreach ($InputPath in $InputPaths) {
        if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf) -or
            -not (Test-PathWithin $WorkspaceRoot $InputPath)) {
            throw "Required artifact is missing or outside WorkspaceRoot: $InputPath"
        }
        Assert-NoReparsePoint $InputPath 'Input artifact'
    }
    if ($script:ValidationProfile -ceq 'SignedNative') {
        $CertificateItem = Get-Item -LiteralPath $CertificatePath -Force
        if ($CertificateItem.Length -eq 0 -or $CertificateItem.Length -gt 65536) {
            throw 'The diagnostic signing certificate must be between 1 byte and 64 KiB.'
        }
        $CertificateBytes = [IO.File]::ReadAllBytes($CertificatePath)
        $SignerCertificate = $null
        try {
            $SignerCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($CertificateBytes)
            if ($SignerCertificate.HasPrivateKey) {
                throw 'CertificatePath must contain only the public signing certificate.'
            }
            $SignerThumbprint = ([string]$SignerCertificate.Thumbprint).ToUpperInvariant()
        }
        finally {
            [Array]::Clear($CertificateBytes, 0, $CertificateBytes.Length)
            if ($null -ne $SignerCertificate) { $SignerCertificate.Dispose() }
        }
        if ($SignerThumbprint -cnotmatch '^[0-9A-F]{40}$') {
            throw 'The supplied diagnostic signing certificate has an invalid SHA-1 thumbprint.'
        }
        $ShimEvidence = [pscustomobject]@{
            Path = $ShimPath
            Sha256 = Get-Sha256 $ShimPath
            Bytes = (Get-Item -LiteralPath $ShimPath -Force).Length
            PinnedSha256 = '6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B'
        }
        if ($ShimEvidence.Sha256 -cne $ShimEvidence.PinnedSha256) {
            throw 'ShimPath does not match the repository-pinned vendor shim image.'
        }
        $LoaderEvidence = Get-SignedNativeArtifactEvidence -Path $LoaderPath `
            -Label 'Signed loader' -ExpectedSignerThumbprint $SignerThumbprint
        $PayloadEvidence = Get-SignedNativeArtifactEvidence -Path $EfiPath `
            -Label 'Signed EFI payload' -ExpectedSignerThumbprint $SignerThumbprint
        $script:SignedNativeArtifactEvidence = [pscustomobject]@{
            Shim = $ShimEvidence
            Loader = $LoaderEvidence
            Payload = $PayloadEvidence
            Certificate = [pscustomobject]@{
                Path = $CertificatePath
                Sha256 = Get-Sha256 $CertificatePath
                Bytes = $CertificateItem.Length
            }
            SignerThumbprint = $SignerThumbprint
        }
    }
    if (-not (Test-Path -LiteralPath $CredentialStateRoot -PathType Container) -or
        -not (Test-PathWithin $WorkspaceRoot $RunDirectory)) {
        throw 'CredentialStateRoot must exist and RunDirectory must remain isolated from credentials.'
    }
    if (-not (Test-Path -LiteralPath $VmrunPath -PathType Leaf)) {
        throw "VMware vmrun.exe was not found: $VmrunPath"
    }
    foreach ($CredentialName in @('windows-vm.xml', 'windows-clone-mailbox-key.xml')) {
        $CredentialPath = Join-Path (Join-Path $CredentialStateRoot 'credentials') $CredentialName
        if (-not (Test-Path -LiteralPath $CredentialPath -PathType Leaf)) {
            throw "Required archived credential is missing: $CredentialName"
        }
        Assert-NoReparsePoint $CredentialPath 'Credential file'
    }

    $script:RunRoot = $RunDirectory
    New-Item -ItemType Directory -Path $script:RunRoot | Out-Null
    $script:CommandLog = Join-Path $script:RunRoot 'commands.jsonl'
    [IO.File]::WriteAllText($script:CommandLog, '', [Text.UTF8Encoding]::new($false))
    $script:RunId = 'gpu-hal-windows-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' +
        [Guid]::NewGuid().ToString('N').Substring(0, 10)
    $script:Serial0Path = Join-Path $script:RunRoot 'efi-serial.log'
    $script:Serial1Path = Join-Path $script:RunRoot 'windows-session.log'

    $script:Config = [pscustomobject]@{
        ProjectRoot = $WorkspaceRoot
        Tools = [pscustomobject]@{ VmrunPath = $VmrunPath }
        Policy = [pscustomobject]@{
            OperationTimeoutSeconds = $TimeoutSeconds
            GuestReadyTimeoutSeconds = $TimeoutSeconds
        }
        StateRoot = $CredentialStateRoot
    }
    $script:Selected = [pscustomobject]@{
        VmxPath = $VmxPath
        VmCredentialFile = 'windows-vm.xml'
        MailboxCredentialFile = 'windows-clone-mailbox-key.xml'
        GuestTransport = 'VmwareGuestInfo'
        ExpectedProbeVerdict = 'NOT_MET'
    }

    $CoreModulePath = Join-Path $WorkspaceRoot 'tools\host-runner\HostRunner.Core.psm1'
    Import-Module $CoreModulePath -Force
    $CoreModule = @(Get-Module | Where-Object {
        [IO.Path]::GetFullPath([string]$_.Path).Equals(
            [IO.Path]::GetFullPath($CoreModulePath), [StringComparison]::OrdinalIgnoreCase)
    }) | Select-Object -First 1
    if ($null -eq $CoreModule) { throw 'HostRunner.Core did not load.' }
    $script:CoreInvokeUhrProcess = Get-Command Invoke-UhrProcess `
        -Module $CoreModule.Name -CommandType Function
    Set-Alias -Name Invoke-UhrProcess -Value Invoke-LoggedUhrProcess `
        -Scope Script -Force

    $HostRunnerPath = Join-Path $WorkspaceRoot 'tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'
    $Tokens = $null
    $ParseErrors = $null
    $HostRunnerAst = [Management.Automation.Language.Parser]::ParseFile(
        $HostRunnerPath, [ref]$Tokens, [ref]$ParseErrors)
    if ($ParseErrors.Count -gt 0) { throw 'The existing host runner did not parse cleanly.' }
    $Functions = @{}
    foreach ($Statement in $HostRunnerAst.EndBlock.Statements) {
        if ($Statement -is [Management.Automation.Language.FunctionDefinitionAst]) {
            $Functions[[string]$Statement.Name] = $Statement
        }
    }
    $Needed = [Collections.Generic.Queue[string]]::new()
    foreach ($RootFunction in @('Publish-WindowsGuestInfoRequest',
            'Wait-WindowsGuestInfoEvidence',
            'Wait-VmwareGuestInfoWorkerReady')) { $Needed.Enqueue($RootFunction) }
    $Loaded = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    while ($Needed.Count -gt 0) {
        $Name = $Needed.Dequeue()
        if (-not $Loaded.Add($Name)) { continue }
        if (-not $Functions.ContainsKey($Name)) { throw "Required GuestInfo helper is absent: $Name" }
        $Definition = $Functions[$Name]
        foreach ($Command in $Definition.FindAll({
                    param($Node)
                    $Node -is [Management.Automation.Language.CommandAst]
                }, $true)) {
            $CommandName = $Command.GetCommandName()
            if ($CommandName -and $Functions.ContainsKey($CommandName) -and
                -not $Loaded.Contains($CommandName)) { $Needed.Enqueue($CommandName) }
        }
        . ([scriptblock]::Create($Definition.Extent.Text))
    }
    [void](Import-VmwareGuestInfoConstants -HostRunnerEndBlock $HostRunnerAst.EndBlock)
    foreach ($Helper in @('Publish-WindowsGuestInfoRequest',
            'Wait-WindowsGuestInfoEvidence', 'Wait-VmwareGuestInfoWorkerReady',
            'Invoke-ConfiguredVmrun',
            'Get-MailboxKeyBytes', 'Clear-VmwareGuestInfoVariables',
            'Get-VmwareGuestInfoBase')) {
        if (-not (Get-Command $Helper -CommandType Function -ErrorAction SilentlyContinue)) {
            throw "Authenticated GuestInfo helper closure did not load: $Helper"
        }
    }

    $script:InspectRunId = New-UhrRunId 'windows-probe'
    $script:StageRunId = New-UhrRunId 'windows-probe'
    if ($script:InspectRunId -ceq $script:StageRunId) { throw 'RunId generator returned a duplicate identity.' }
    $GuestInfoBase = Get-VmwareGuestInfoBase -RunId $script:InspectRunId
    if ([string]::IsNullOrWhiteSpace([string]$GuestInfoBase) -or
        [string]$GuestInfoBase -cne [string]$script:VmwareGuestInfoPrefix) {
        throw 'HostRunner GuestInfo base did not resolve from its verified literal constants.'
    }
    $script:SnapshotBefore = 'before-gpu-hal-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $script:SnapshotAfter = 'after-gpu-hal-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)

    # Read the protected values only through the existing credential loader.
    $TestKey = Get-MailboxKeyBytes $script:Selected
    try {
        if ($TestKey.Length -ne 32) { throw 'The archived mailbox credential is invalid.' }
    }
    finally { [Array]::Clear($TestKey, 0, $TestKey.Length) }
    $VmCredential = Get-UhrCredential -Config $script:Config -FileName 'windows-vm.xml'
    if ($VmCredential -isnot [PSCredential]) { throw 'The archived VM credential is malformed.' }
    $VmCredential = $null

    if (Test-TargetVmRunning) {
        throw 'Refusing to attach to or stop the target clone because it is already running.'
    }
    $VmBefore = Get-FileHash -LiteralPath $VmxPath -Algorithm SHA256
    Write-Output "Prepared clone VMX SHA256 before run config: $($VmBefore.Hash.ToUpperInvariant())"
    $Lines = Assert-PreparedClone -Root $WorkspaceRoot -BuildRoot $BuildRoot -Vmx $VmxPath
    $SafeVmxSummary = Get-SafeVmxSummary -Lines $Lines -Vmx $VmxPath -BuildRoot $BuildRoot
    Configure-DisposableClone -Lines $Lines

    $EfiHash = Get-Sha256 $EfiPath
    $Artifacts = [ordered]@{
        EFI = [ordered]@{ Path = $EfiPath; Sha256 = $EfiHash; Bytes = (Get-Item $EfiPath).Length }
        Loader = [ordered]@{ Path = $LoaderPath; Sha256 = (Get-Sha256 $LoaderPath); Bytes = (Get-Item $LoaderPath).Length }
        Installer = [ordered]@{ Path = $InstallerPath; Sha256 = (Get-Sha256 $InstallerPath); Bytes = (Get-Item $InstallerPath).Length }
        Probe = [ordered]@{ Path = $ProbePath; Sha256 = (Get-Sha256 $ProbePath); Bytes = (Get-Item $ProbePath).Length }
    }
    if ($script:ValidationProfile -ceq 'SignedNative') {
        $Artifacts['Shim'] = $script:SignedNativeArtifactEvidence.Shim
        $Artifacts['Certificate'] = $script:SignedNativeArtifactEvidence.Certificate
        $Artifacts['SignerThumbprint'] = $script:SignedNativeArtifactEvidence.SignerThumbprint
        $Artifacts['LoaderSignature'] = $script:SignedNativeArtifactEvidence.Loader
        $Artifacts['PayloadSignature'] = $script:SignedNativeArtifactEvidence.Payload
    }
    [IO.File]::WriteAllText((Join-Path $script:RunRoot 'provenance.json'),
        ([ordered]@{
            SchemaVersion = 1
            RunId = $script:RunId
            ValidationProfile = $script:ValidationProfile
            ExpectedSecureBoot = ($script:ValidationProfile -ceq 'SignedNative')
            OriginalVmxSha256 = $VmBefore.Hash.ToUpperInvariant()
            PreparedVmSettings = $SafeVmxSummary
            Artifacts = $Artifacts
            ExpectedWindowsBuild = $ExpectedBuild
            ExpectedWindowsUbr = $ExpectedUbr
            ExpectedLogicalCpus = $ExpectedCpus
        SecureBootTrust = if ($script:ValidationProfile -ceq 'SignedNative') {
            'VMWARE_FIRMWARE_DB_CERTIFICATE_PROVISIONING; NOT_HOST_MOK'
        } else { 'MODEL_PROFILE' }
            TimeoutSeconds = $TimeoutSeconds
            CredentialValuesRecorded = $false
            HmacKeyRecorded = $false
        } | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))

    if (Test-TargetVmRunning) { throw 'The target clone started unexpectedly before its baseline snapshot.' }
    [void](Invoke-TargetVmrun -Operation @('snapshot', $VmxPath,
            $script:SnapshotBefore) -Timeout 60)
    $script:BeforeSnapshotCreated = $true

    $script:VmBootStartedUtc = [DateTime]::UtcNow
    $script:VmStartAttempted = $true
    [void](Invoke-TargetVmrun -Operation @('start', $VmxPath, 'nogui') -Timeout 60)
    $script:VmStartedAtLeastOnce = $true
    $script:VmOffConfirmed = $false

    $InspectPaths = Invoke-GpuGuestPhase -Mode 'Inspect' `
        -RunId $script:InspectRunId -EfiHash $EfiHash
    $script:InspectStatus = 'PASS'
    $script:InspectEvidence = Assert-GuestInspection -Paths $InspectPaths -Phase 'Inspect'

    $StagePaths = Invoke-GpuGuestPhase -Mode 'Stage' `
        -RunId $script:StageRunId -EfiHash $EfiHash
    $script:StageStatus = 'PASS'
    $script:StageEvidence = Assert-GuestInspection -Paths $StagePaths -Phase 'Stage'

    Stop-OnlyTargetVm
    if (-not $script:VmOffConfirmed) { throw 'Windows did not stop after the staged BootNext setup.' }
    if ($script:ValidationProfile -ceq 'Model') {
        $script:SecureBootRestoreNeeded = $true
        Set-CloneSecureBoot $false
        $script:SecureBootChanged = $true
    } else {
        if ($script:OriginalSecureBoot -ne $true -or
            $script:StageEvidence.SecureBoot -ne $true) {
            throw 'SignedNative requires Secure Boot to remain enabled through staging.'
        }
        $CurrentCloneLines = [IO.File]::ReadAllLines([string]$script:Selected.VmxPath)
        if ((Get-VmxSetting $CurrentCloneLines 'uefi.secureBoot.enabled') -ine 'TRUE') {
            throw 'SignedNative refuses to start if the clone Secure Boot VMX setting changed from TRUE.'
        }
    }

    $script:VmStartAttempted = $true
    [void](Invoke-TargetVmrun -Operation @('start', $VmxPath, 'nogui') -Timeout 60)
    $script:VmStartedAtLeastOnce = $true
    $script:VmOffConfirmed = $false
    $VmEvidence = Wait-WindowsVmEvidence -StartupRunId $script:StageRunId

    $script:HalResults = if ($script:ValidationProfile -ceq 'SignedNative') {
        [ordered]@{
            SignedNativeEfiAdmissionMarkers = 'PASS'
            DmaIsolationAdmission = 'EXTERNAL_VMM_ADMISSION'
            NativeGpuBackend = 'NOT_EXERCISED_EXTERNAL_VMM'
            SyntheticEnforcementModel = 'NOT_EXERCISED'
            ActualVmxRootWcRenderDrainAndUnmap = 'NOT_EXERCISED_EXTERNAL_VMM'
            WindowsStartupHealth = 'PASS'
            IntelPhysicalPlaneAndDmaEnforcement = 'NOT_TESTED_BY_THIS_VM'
            HostDualGpuTopology = 'NOT_TESTED_BY_THIS_VM'
        }
    } else {
        [ordered]@{
            SyntheticEnforcementModel = 'PASS'
            ActualVmxRootWcRenderDrainAndUnmap = 'PASS'
            WindowsStartupHealth = 'PASS'
            IntelPhysicalPlaneAndDmaEnforcement = 'NOT_TESTED_BY_THIS_VM'
            HostDualGpuTopology = 'NOT_TESTED_BY_THIS_VM'
        }
    }
    $script:StartupEvidence = [ordered]@{
        RunId = [string]$VmEvidence.Startup.Result.RunId
        ValidationProfile = [string]$VmEvidence.Startup.Result.ValidationProfile
        Status = [string]$VmEvidence.Startup.Result.Status
        CurrentBuild = [int]$VmEvidence.Startup.Result.CurrentBuild
        Ubr = [int]$VmEvidence.Startup.Result.Ubr
        ExpectedCpus = [int]$VmEvidence.Startup.Result.ExpectedCpus
        CpuPassCount = [int]$VmEvidence.Startup.Result.CpuPassCount
        HealthPassed = [bool]$VmEvidence.Startup.Result.HealthPassed
        BootManagerUnchanged = [bool]$VmEvidence.Startup.Result.BootManagerUnchanged
        SecureBoot = $VmEvidence.Startup.Result.SecureBoot
        ExpectedSecureBoot = $VmEvidence.Startup.Result.ExpectedSecureBoot
        ExpectedCertificateDerPresent = $VmEvidence.Startup.Result.ExpectedCertificateDerPresent
        ExpectedCertificateSha256 = [string]$VmEvidence.Startup.Result.ExpectedCertificateSha256
        CertificateDbInspection = [string]$VmEvidence.Startup.Result.CertificateDbInspection
        HandoffStatus = [string]$VmEvidence.Startup.Result.HandoffStatus
        HandoffStatusBytes = [long]$VmEvidence.Startup.Result.HandoffStatusBytes
        HandoffStatusCapturedBytes = [int]$VmEvidence.Startup.Result.HandoffStatusCapturedBytes
        HandoffStatusTooLarge = [bool]$VmEvidence.Startup.Result.HandoffStatusTooLarge
        PayloadDiagnostic = [string]$VmEvidence.Startup.Result.PayloadDiagnostic
        PayloadDiagnosticBytes = [long]$VmEvidence.Startup.Result.PayloadDiagnosticBytes
        PayloadDiagnosticCapturedBytes = [int]$VmEvidence.Startup.Result.PayloadDiagnosticCapturedBytes
        PayloadDiagnosticTooLarge = [bool]$VmEvidence.Startup.Result.PayloadDiagnosticTooLarge
        DiagnosticTextEncoding = [string]$VmEvidence.Startup.Result.DiagnosticTextEncoding
        PayloadSha256 = [string]$VmEvidence.Startup.PayloadSha256
        HmacSha256 = [string]$VmEvidence.Startup.HmacSha256
    }
}
catch {
    $script:Failure = $_.Exception.Message
    $script:FailureScriptStackTrace = [string]$_.ScriptStackTrace
    if ($script:FailureScriptStackTrace.Length -gt 16384) {
        $script:FailureScriptStackTrace = $script:FailureScriptStackTrace.Substring(0, 16384)
    }
}
finally {
    if ($script:RunRoot) {
        if ($script:VmStartAttempted) {
            try { Capture-CloneScreen }
            catch { $script:ScreenCaptureStatus = 'UNAVAILABLE: ' + $_.Exception.Message }
            try {
                if (Test-TargetVmRunning) { Clear-GpuGuestInfoRunState }
            }
            catch { $script:CleanupFailures.Add('Could not establish VM state before GuestInfo cleanup: ' + $_.Exception.Message) }
        }
        try { Stop-OnlyTargetVm }
        catch { $script:CleanupFailures.Add('Target VM stop cleanup failed: ' + $_.Exception.Message) }
        if ($script:VmOffConfirmed) {
            if ($script:VmStartAttempted) {
                try { Copy-CloneVmwareLog }
                catch { $script:VmwareLogStatus = 'COPY_FAILED: ' + $_.Exception.Message }
            }
            try { Confirm-SignedNativeVmwareDbAppend }
            catch {
                $script:VmwareDbAppendMarker = 'NOT_CONFIRMED'
                if ([string]::IsNullOrWhiteSpace($script:Failure)) {
                    $script:Failure = 'Could not verify the VMware Secure Boot db append log marker: ' + $_.Exception.Message
                }
            }
            if ($script:BeforeSnapshotCreated) {
                try {
                    [void](Invoke-TargetVmrun -Operation @('snapshot',
                            [string]$script:Selected.VmxPath,
                            [string]$script:SnapshotAfter) -Timeout 60)
                    $script:AfterSnapshotCreated = $true
                }
                catch { $script:CleanupFailures.Add('After snapshot failed: ' + $_.Exception.Message) }
                try {
                    [void](Invoke-TargetVmrun -Operation @('revertToSnapshot',
                            [string]$script:Selected.VmxPath,
                            [string]$script:SnapshotBefore) -Timeout 120)
                    $script:RevertedBefore = $true
                    if (Test-TargetVmRunning) {
                        throw 'VM was running after baseline snapshot revert.'
                    }
                }
                catch { $script:CleanupFailures.Add('Baseline snapshot revert failed: ' + $_.Exception.Message) }
            }
            if ($script:SecureBootRestoreNeeded) {
                try { Restore-CloneSecureBoot }
                catch { $script:CleanupFailures.Add('Original Secure Boot setting restore failed: ' + $_.Exception.Message) }
            }
        }
        else {
            if ($script:ValidationProfile -ceq 'SignedNative') {
                $script:VmwareDbAppendMarker = 'NOT_CONFIRMED'
                if ([string]::IsNullOrWhiteSpace($script:Failure)) {
                    $script:Failure = 'VMware Secure Boot db append evidence is unavailable while the VM remains running.'
                }
            }
            $script:CleanupFailures.Add('VM remained running; snapshots and Secure Boot restoration were skipped.')
        }
        try {
            $ScriptStatus = if ($script:ValidationProfile -ceq 'SignedNative') {
                [ordered]@{
                    ValidationProfile = $script:ValidationProfile
                    Inspect = $script:InspectStatus
                    Stage = $script:StageStatus
                    SignedNativeEfi = $script:EfiStatus
                    Startup = $script:StartupStatus
                    DmaIsolationAdmission = if ($script:VmwareDbAppendMarker -ceq 'PASS_LOG_MARKER') {
                        'EXTERNAL_VMM_ADMISSION'
                    } else { 'EXTERNAL_VMM_ADMISSION_NOT_CONFIRMED' }
                    VmwareDbAppendMarker = $script:VmwareDbAppendMarker
                    NativeGpuBackend = 'NOT_EXERCISED_EXTERNAL_VMM'
                    SyntheticEnforcementModel = 'NOT_EXERCISED'
                    ActualVmxRootWc = 'NOT_EXERCISED_EXTERNAL_VMM'
                    Windows = if ($script:StartupStatus -eq 'PASS') { 'PASS' } else { 'NOT_CONFIRMED' }
                    IntelPhysicalPlaneAndDmaEnforcement = 'NOT_TESTED_BY_THIS_VM'
                    HostDualGpuTopology = 'NOT_TESTED_BY_THIS_VM'
                }
            } else {
                [ordered]@{
                    ValidationProfile = $script:ValidationProfile
                    Inspect = $script:InspectStatus
                    Stage = $script:StageStatus
                    Efi = $script:EfiStatus
                    Startup = $script:StartupStatus
                    SyntheticEnforcementModel = if ($script:EfiStatus -eq 'PASS') { 'PASS' } else { 'NOT_CONFIRMED' }
                    ActualVmxRootWc = if ($script:EfiStatus -eq 'PASS') { 'PASS' } else { 'NOT_CONFIRMED' }
                    Windows = if ($script:StartupStatus -eq 'PASS') { 'PASS' } else { 'NOT_CONFIRMED' }
                    IntelPhysicalPlaneAndDmaEnforcement = 'NOT_TESTED_BY_THIS_VM'
                    HostDualGpuTopology = 'NOT_TESTED_BY_THIS_VM'
                }
            }
            [IO.File]::WriteAllText((Join-Path $script:RunRoot 'validation.json'),
                ($ScriptStatus | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
        }
        catch { $script:CleanupFailures.Add('Could not write validation summary: ' + $_.Exception.Message) }
        try { Save-RunResult }
        catch { $script:CleanupFailures.Add('Could not write final result.json: ' + $_.Exception.Message) }
    }
}

if ($script:Failure -or $script:CleanupFailures.Count -gt 0 -or
    $null -eq $script:ResultObject -or $script:ResultObject.Result -cne 'PASS') {
    $Reason = if ($script:Failure) { $script:Failure }
        elseif ($script:CleanupFailures.Count -gt 0) { $script:CleanupFailures -join '; ' }
        else { 'The GPU HAL Windows VM validation did not satisfy all pass gates.' }
    throw $Reason
}
Write-Output (Join-Path $script:RunRoot 'result.json')
