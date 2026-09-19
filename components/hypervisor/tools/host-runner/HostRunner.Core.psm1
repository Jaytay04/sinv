Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not ('Hypersinv.HostRunner.NativeMemory' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace Hypersinv.HostRunner
{
    public sealed class SystemMemorySnapshot
    {
        public ulong CommitTotalBytes { get; internal set; }
        public ulong CommitLimitBytes { get; internal set; }
        public ulong CommitPeakBytes { get; internal set; }
        public ulong PhysicalTotalBytes { get; internal set; }
        public ulong PhysicalAvailableBytes { get; internal set; }
        public ulong SystemCacheBytes { get; internal set; }
        public ulong KernelTotalBytes { get; internal set; }
        public ulong KernelPagedBytes { get; internal set; }
        public ulong KernelNonpagedBytes { get; internal set; }
        public uint HandleCount { get; internal set; }
        public uint ProcessCount { get; internal set; }
        public uint ThreadCount { get; internal set; }
    }

    public static class NativeMemory
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct PERFORMANCE_INFORMATION
        {
            public uint cb;
            public UIntPtr CommitTotal;
            public UIntPtr CommitLimit;
            public UIntPtr CommitPeak;
            public UIntPtr PhysicalTotal;
            public UIntPtr PhysicalAvailable;
            public UIntPtr SystemCache;
            public UIntPtr KernelTotal;
            public UIntPtr KernelPaged;
            public UIntPtr KernelNonpaged;
            public UIntPtr PageSize;
            public uint HandleCount;
            public uint ProcessCount;
            public uint ThreadCount;
        }

        [DllImport("psapi.dll", SetLastError = true)]
        private static extern bool GetPerformanceInfo(
            ref PERFORMANCE_INFORMATION information,
            uint size);

        private static ulong Bytes(UIntPtr pages, ulong pageSize)
        {
            return checked(pages.ToUInt64() * pageSize);
        }

        public static SystemMemorySnapshot Read()
        {
            PERFORMANCE_INFORMATION information = new PERFORMANCE_INFORMATION();
            uint size = (uint)Marshal.SizeOf(typeof(PERFORMANCE_INFORMATION));
            information.cb = size;
            if (!GetPerformanceInfo(ref information, size))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            ulong pageSize = information.PageSize.ToUInt64();
            return new SystemMemorySnapshot
            {
                CommitTotalBytes = Bytes(information.CommitTotal, pageSize),
                CommitLimitBytes = Bytes(information.CommitLimit, pageSize),
                CommitPeakBytes = Bytes(information.CommitPeak, pageSize),
                PhysicalTotalBytes = Bytes(information.PhysicalTotal, pageSize),
                PhysicalAvailableBytes = Bytes(information.PhysicalAvailable, pageSize),
                SystemCacheBytes = Bytes(information.SystemCache, pageSize),
                KernelTotalBytes = Bytes(information.KernelTotal, pageSize),
                KernelPagedBytes = Bytes(information.KernelPaged, pageSize),
                KernelNonpagedBytes = Bytes(information.KernelNonpaged, pageSize),
                HandleCount = information.HandleCount,
                ProcessCount = information.ProcessCount,
                ThreadCount = information.ThreadCount
            };
        }
    }
}
'@
}

function Get-HrSystemMemoryState {
    $Snapshot = [Hypersinv.HostRunner.NativeMemory]::Read()
    $ToMb = { param([UInt64]$Bytes) [int][math]::Floor($Bytes / 1MB) }
    return [pscustomobject]@{
        AvailablePhysicalMb = & $ToMb $Snapshot.PhysicalAvailableBytes
        TotalPhysicalMb = & $ToMb $Snapshot.PhysicalTotalBytes
        CommitTotalMb = & $ToMb $Snapshot.CommitTotalBytes
        CommitLimitMb = & $ToMb $Snapshot.CommitLimitBytes
        CommitPeakMb = & $ToMb $Snapshot.CommitPeakBytes
        SystemCacheMb = & $ToMb $Snapshot.SystemCacheBytes
        KernelTotalMb = & $ToMb $Snapshot.KernelTotalBytes
        KernelPagedMb = & $ToMb $Snapshot.KernelPagedBytes
        KernelNonpagedMb = & $ToMb $Snapshot.KernelNonpagedBytes
        HandleCount = [uint32]$Snapshot.HandleCount
        ProcessCount = [uint32]$Snapshot.ProcessCount
        ThreadCount = [uint32]$Snapshot.ThreadCount
    }
}

function Get-HrFullPath {
    param([Parameter(Mandatory=$true)][string]$Path)

    return [IO.Path]::GetFullPath($Path)
}

function Test-HrPathWithin {
    param(
        [Parameter(Mandatory=$true)][string]$Root,
        [Parameter(Mandatory=$true)][string]$Path
    )

    $RootFull = (Get-HrFullPath $Root).TrimEnd('\') + '\'
    $PathFull = Get-HrFullPath $Path
    return $PathFull.StartsWith(
        $RootFull,
        [StringComparison]::OrdinalIgnoreCase)
}

function Test-HrSafeGateName {
    param([Parameter(Mandatory=$true)][string]$Name)

    return $Name -cmatch '^gate[0-9]+-[a-z0-9-]+\.ps1$'
}

function Get-HrSha256 {
    param([Parameter(Mandatory=$true)][string]$Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Test-HrAdministrator {
    $Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $Principal = [Security.Principal.WindowsPrincipal]::new($Identity)
    return $Principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Import-HrConfig {
    param([Parameter(Mandatory=$true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Host-runner configuration not found: $Path"
    }

    $Config = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    if ($Config.SchemaVersion -ne 2) {
        throw "Unsupported host-runner schema: $($Config.SchemaVersion)"
    }

    foreach ($Property in @(
            'Vm', 'Tools', 'Mailbox', 'Policy', 'Debugger', 'Credential')) {
        if ($null -eq $Config.$Property) {
            throw "Configuration is missing '$Property'."
        }
    }

    # Keep schema 2 configurations created before the debugger resource
    # guards backwards compatible. Defaults are injected in memory only; the
    # user's local credential-bearing config is never rewritten.
    if ($null -eq $Config.Debugger.PSObject.Properties['HostMemoryReserveMb']) {
        $Config.Debugger | Add-Member -NotePropertyName HostMemoryReserveMb `
            -NotePropertyValue 2048
    }
    if ($null -eq $Config.Debugger.PSObject.Properties['MaximumDebuggerWorkingSetMb']) {
        $Config.Debugger | Add-Member -NotePropertyName MaximumDebuggerWorkingSetMb `
            -NotePropertyValue 512
    }
    if ($null -eq $Config.Debugger.PSObject.Properties['MinimumHostFreeMemoryMb']) {
        $Config.Debugger | Add-Member -NotePropertyName MinimumHostFreeMemoryMb `
            -NotePropertyValue 2048
    }
    if ($null -eq $Config.Debugger.PSObject.Properties['HostMemoryBreachGraceSeconds']) {
        $Config.Debugger | Add-Member -NotePropertyName HostMemoryBreachGraceSeconds `
            -NotePropertyValue 2
    }
    if ($null -eq $Config.Debugger.PSObject.Properties['PoolSnapshotTriggerNonpagedMb']) {
        $Config.Debugger | Add-Member -NotePropertyName PoolSnapshotTriggerNonpagedMb `
            -NotePropertyValue 2048
    }
    if ($null -eq $Config.Debugger.PSObject.Properties['StopAfterPoolSnapshot']) {
        $Config.Debugger | Add-Member -NotePropertyName StopAfterPoolSnapshot `
            -NotePropertyValue $false
    }
    if ($null -eq $Config.Debugger.PSObject.Properties['MaximumRunLogBytes']) {
        $Config.Debugger | Add-Member -NotePropertyName MaximumRunLogBytes `
            -NotePropertyValue 8388608
    }
    if ($null -eq $Config.Debugger.PSObject.Properties['GuestMailboxReadinessTimeoutSeconds']) {
        $Config.Debugger | Add-Member -NotePropertyName GuestMailboxReadinessTimeoutSeconds `
            -NotePropertyValue 120
    }
    if ($null -eq $Config.Debugger.PSObject.Properties['GuestMailboxReadinessPollIntervalMilliseconds']) {
        $Config.Debugger | Add-Member -NotePropertyName GuestMailboxReadinessPollIntervalMilliseconds `
            -NotePropertyValue 1000
    }
    if ($null -eq $Config.Debugger.PSObject.Properties['GuestMailboxStabilizationSeconds']) {
        $Config.Debugger | Add-Member -NotePropertyName GuestMailboxStabilizationSeconds `
            -NotePropertyValue 15
    }

    foreach ($Property in @('PoolmonPath', 'PoolTagPath')) {
        if ($null -eq $Config.Tools.PSObject.Properties[$Property]) {
            $Config.Tools | Add-Member -NotePropertyName $Property `
                -NotePropertyValue $null
        }
    }
    $HasPoolmon = -not [string]::IsNullOrWhiteSpace(
        [string]$Config.Tools.PoolmonPath)
    $HasPoolTags = -not [string]::IsNullOrWhiteSpace(
        [string]$Config.Tools.PoolTagPath)
    if ($HasPoolmon -xor $HasPoolTags) {
        throw 'Tools.PoolmonPath and Tools.PoolTagPath must be configured together.'
    }

    $RequiredTools = @($Config.Tools.VmrunPath, $Config.Tools.KdPath)
    if ($HasPoolmon) {
        $RequiredTools += @($Config.Tools.PoolmonPath, $Config.Tools.PoolTagPath)
    }
    foreach ($ToolPath in $RequiredTools) {
        if (-not (Test-Path -LiteralPath $ToolPath -PathType Leaf)) {
            throw "Required host tool not found: $ToolPath"
        }
    }
    if (-not (Test-Path -LiteralPath $Config.Vm.VmxPath -PathType Leaf)) {
        throw "Configured VMX not found: $($Config.Vm.VmxPath)"
    }

    if ($Config.Policy.MaximumRuntimeSeconds -lt 30 -or
        $Config.Policy.MaximumRuntimeSeconds -gt 7200) {
        throw 'MaximumRuntimeSeconds must be between 30 and 7200.'
    }
    if ($Config.Debugger.ConnectionTimeoutSeconds -lt 10 -or
        $Config.Debugger.ConnectionTimeoutSeconds -gt 300) {
        throw 'Debugger.ConnectionTimeoutSeconds must be between 10 and 300.'
    }
    if ($Config.Debugger.MaximumStartupLogBytes -lt 65536 -or
        $Config.Debugger.MaximumStartupLogBytes -gt 4194304) {
        throw 'Debugger.MaximumStartupLogBytes must be between 65536 and 4194304.'
    }
    if ($Config.Debugger.HostMemoryReserveMb -lt 512 -or
        $Config.Debugger.HostMemoryReserveMb -gt 131072) {
        throw 'Debugger.HostMemoryReserveMb must be between 512 and 131072.'
    }
    if ($Config.Debugger.MaximumDebuggerWorkingSetMb -lt 128 -or
        $Config.Debugger.MaximumDebuggerWorkingSetMb -gt 8192) {
        throw 'Debugger.MaximumDebuggerWorkingSetMb must be between 128 and 8192.'
    }
    if ($Config.Debugger.MinimumHostFreeMemoryMb -lt 512 -or
        $Config.Debugger.MinimumHostFreeMemoryMb -gt 131072) {
        throw 'Debugger.MinimumHostFreeMemoryMb must be between 512 and 131072.'
    }
    if ($Config.Debugger.HostMemoryBreachGraceSeconds -lt 1 -or
        $Config.Debugger.HostMemoryBreachGraceSeconds -gt 10) {
        throw 'Debugger.HostMemoryBreachGraceSeconds must be between 1 and 10.'
    }
    if ($Config.Debugger.PoolSnapshotTriggerNonpagedMb -lt 1024 -or
        $Config.Debugger.PoolSnapshotTriggerNonpagedMb -gt 8192) {
        throw 'Debugger.PoolSnapshotTriggerNonpagedMb must be between 1024 and 8192.'
    }
    if ($Config.Debugger.StopAfterPoolSnapshot -isnot [bool]) {
        throw 'Debugger.StopAfterPoolSnapshot must be a Boolean.'
    }
    if ($Config.Debugger.MaximumRunLogBytes -lt 65536 -or
        $Config.Debugger.MaximumRunLogBytes -gt 8388608) {
        throw 'Debugger.MaximumRunLogBytes must be between 65536 and 8388608.'
    }
    if ($Config.Debugger.GuestMailboxReadinessTimeoutSeconds -lt 30 -or
        $Config.Debugger.GuestMailboxReadinessTimeoutSeconds -gt 900) {
        throw 'Debugger.GuestMailboxReadinessTimeoutSeconds must be between 30 and 900.'
    }
    if ($Config.Debugger.GuestMailboxReadinessPollIntervalMilliseconds -lt 250 -or
        $Config.Debugger.GuestMailboxReadinessPollIntervalMilliseconds -gt 5000) {
        throw 'Debugger.GuestMailboxReadinessPollIntervalMilliseconds must be between 250 and 5000.'
    }
    if ($Config.Debugger.GuestMailboxStabilizationSeconds -lt 5 -or
        $Config.Debugger.GuestMailboxStabilizationSeconds -gt 300) {
        throw 'Debugger.GuestMailboxStabilizationSeconds must be between 5 and 300.'
    }
    if ($Config.Vm.DebuggerPipe -cnotmatch '^\\\\\.\\pipe\\[A-Za-z0-9_-]+$') {
        throw 'Vm.DebuggerPipe must be a local named-pipe path.'
    }
    if ([string]$Config.Credential.TargetName -cnotmatch `
            '^HypersinvHostRunner/[A-Za-z0-9._-]{1,80}$') {
        throw 'Credential.TargetName is not a safe runner credential name.'
    }

    return $Config
}

function New-HrRunPlan {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)][string]$PackagePath,
        [Parameter(Mandatory=$true)][string]$ExpectedPackageSha256,
        [Parameter(Mandatory=$true)][string]$GateScript,
        [Parameter(Mandatory=$true)][string]$SymbolPath,
        [string]$RunId
    )

    if (-not (Test-HrSafeGateName $GateScript)) {
        throw "Gate script is not allowlisted: $GateScript"
    }
    if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
        throw "Gate package not found: $PackagePath"
    }
    if ([IO.Path]::GetExtension($PackagePath) -cne '.zip') {
        throw 'The gate package must be a ZIP archive.'
    }
    if (-not (Test-Path -LiteralPath $SymbolPath -PathType Container)) {
        throw "Symbol directory not found: $SymbolPath"
    }
    if ($SymbolPath -match '[;\r\n"]') {
        throw 'SymbolPath contains characters that are unsafe in a debugger command.'
    }

    $Expected = $ExpectedPackageSha256.ToUpperInvariant()
    if ($Expected -cnotmatch '^[0-9A-F]{64}$') {
        throw 'ExpectedPackageSha256 must contain exactly 64 hexadecimal digits.'
    }
    $Actual = Get-HrSha256 $PackagePath
    if ($Actual -cne $Expected) {
        throw "Package hash mismatch: expected $Expected, actual $Actual"
    }

    if ([string]::IsNullOrWhiteSpace($RunId)) {
        $Stem = [IO.Path]::GetFileNameWithoutExtension($GateScript)
        $Nonce = [Guid]::NewGuid().ToString('N').Substring(0, 10)
        $RunId = '{0}-{1}-{2}' -f $Stem,
            (Get-Date -Format 'yyyyMMdd-HHmmss'), $Nonce
    }
    if ($RunId -cnotmatch '^gate[0-9]+-[a-z0-9-]+-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw "RunId is not canonical: $RunId"
    }

    [pscustomobject]@{
        SchemaVersion = 1
        RunId = $RunId
        GateScript = $GateScript
        PackagePath = Get-HrFullPath $PackagePath
        PackageSha256 = $Actual
        SymbolPath = Get-HrFullPath $SymbolPath
        TimeoutSeconds = [int]$Config.Policy.MaximumRuntimeSeconds
    }
}

function Save-HrRunState {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)]$Plan
    )

    $StateDirectory = Join-Path (Get-HrFullPath $Config.Mailbox.HostRoot) 'state'
    New-Item -ItemType Directory -Force -Path $StateDirectory | Out-Null
    $StatePath = Join-Path $StateDirectory ($Plan.RunId + '.json')
    if (Test-Path -LiteralPath $StatePath) {
        throw "Run state already exists: $StatePath"
    }

    $Temporary = "$StatePath.tmp"
    $Plan | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath $Temporary -Encoding utf8
    Move-Item -LiteralPath $Temporary -Destination $StatePath
    return $StatePath
}

function Read-HrRunState {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)][string]$RunId
    )

    if ($RunId -cnotmatch '^gate[0-9]+-[a-z0-9-]+-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw "RunId is not canonical: $RunId"
    }
    $StatePath = Join-Path (Get-HrFullPath $Config.Mailbox.HostRoot) `
        "state\$RunId.json"
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
        throw "Run state not found: $StatePath"
    }
    return Get-Content -Raw -LiteralPath $StatePath | ConvertFrom-Json
}

function Publish-HrRequest {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)]$Plan
    )

    $Root = Get-HrFullPath $Config.Mailbox.HostRoot
    $Requests = Join-Path $Root 'requests'
    $Artifacts = Join-Path $Root 'artifacts'
    $Responses = Join-Path $Root 'responses'
    $State = Join-Path $Root 'state'
    foreach ($Directory in @($Root, $Requests, $Artifacts, $Responses, $State)) {
        New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    }

    $StatePath = Join-Path $State ($Plan.RunId + '.json')
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
        throw "Prepared run state is missing: $StatePath"
    }
    $Prepared = Get-Content -Raw -LiteralPath $StatePath | ConvertFrom-Json
    foreach ($Property in @('RunId', 'GateScript', 'PackagePath',
            'PackageSha256', 'SymbolPath', 'TimeoutSeconds')) {
        if ([string]$Prepared.$Property -cne [string]$Plan.$Property) {
            throw "Published plan differs from prepared state: $Property"
        }
    }

    $ArtifactDirectory = Join-Path $Artifacts $Plan.RunId
    $RequestPath = Join-Path $Requests ($Plan.RunId + '.json')
    if ((Test-Path -LiteralPath $ArtifactDirectory) -or
        (Test-Path -LiteralPath $RequestPath)) {
        throw "RunId has already been published: $($Plan.RunId)"
    }
    $ResultPath = Join-Path $Responses "$($Plan.RunId)\result.json"
    if (Test-Path -LiteralPath $ResultPath) {
        throw "RunId already has a result: $($Plan.RunId)"
    }

    New-Item -ItemType Directory -Path $ArtifactDirectory | Out-Null
    $MailboxPackage = Join-Path $ArtifactDirectory 'package.zip'
    Copy-Item -LiteralPath $Plan.PackagePath -Destination $MailboxPackage
    $CopiedHash = Get-HrSha256 $MailboxPackage
    if ($CopiedHash -cne $Plan.PackageSha256) {
        throw 'Mailbox package hash changed during copy.'
    }

    $Request = [ordered]@{
        SchemaVersion = 1
        RunId = $Plan.RunId
        GateScript = $Plan.GateScript
        PackageRelativePath = "artifacts/$($Plan.RunId)/package.zip"
        PackageSha256 = $Plan.PackageSha256
        TimeoutSeconds = $Plan.TimeoutSeconds
        SubmittedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $Temporary = "$RequestPath.tmp"
    $Request | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath $Temporary -Encoding utf8
    Move-Item -LiteralPath $Temporary -Destination $RequestPath

    [pscustomobject]@{
        RunId = $Plan.RunId
        RequestPath = $RequestPath
        StatePath = $StatePath
        MailboxPackage = $MailboxPackage
    }
}

function Wait-HrResult {
    param(
        [Parameter(Mandatory=$true)]$Config,
        [Parameter(Mandatory=$true)][string]$RunId,
        [int]$TimeoutSeconds
    )

    if ($RunId -cnotmatch '^gate[0-9]+-[a-z0-9-]+-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw "RunId is not canonical: $RunId"
    }
    if ($TimeoutSeconds -le 0) {
        $TimeoutSeconds = [int]$Config.Policy.MaximumRuntimeSeconds + 60
    }

    $ResultPath = Join-Path $Config.Mailbox.HostRoot `
        "responses\$RunId\result.json"
    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $Deadline) {
        if (Test-Path -LiteralPath $ResultPath -PathType Leaf) {
            return Get-Content -Raw -LiteralPath $ResultPath | ConvertFrom-Json
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Timed out waiting for guest result. Preserve the VM; do not retry: $RunId"
}

function New-HrDebuggerCommandFile {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string[]]$Commands
    )

    foreach ($Command in $Commands) {
        if ([string]::IsNullOrWhiteSpace($Command) -or
            $Command.Contains("`r") -or $Command.Contains("`n")) {
            throw 'Each debugger command must be one non-empty line.'
        }
    }
    Set-Content -LiteralPath $Path -Value $Commands -Encoding ascii
    return $Path
}

Export-ModuleMember -Function @(
    'Get-HrSystemMemoryState',
    'Test-HrAdministrator',
    'Get-HrFullPath',
    'Test-HrPathWithin',
    'Test-HrSafeGateName',
    'Get-HrSha256',
    'Import-HrConfig',
    'New-HrRunPlan',
    'Save-HrRunState',
    'Read-HrRunState',
    'Publish-HrRequest',
    'Wait-HrResult',
    'New-HrDebuggerCommandFile'
)
