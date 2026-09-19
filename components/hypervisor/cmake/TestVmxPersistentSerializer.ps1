param(
    [Parameter(Mandatory = $true)] [string]$SourcePath,
    [Parameter(Mandatory = $true)] [string]$HeaderPath
)
$ErrorActionPreference = 'Stop'
function Assert-Contract([bool]$condition, [string]$message) { if (-not $condition) { throw $message } }
function Get-Slice([string]$text, [string]$start, [string]$next) {
    $first = $text.IndexOf($start, [StringComparison]::Ordinal)
    $last = $text.IndexOf($next, $first + $start.Length, [StringComparison]::Ordinal)
    Assert-Contract ($first -ge 0 -and $last -gt $first) "Missing section: $start"
    return $text.Substring($first, $last - $first)
}
function Count-Token([string]$text, [string]$token) { return [regex]::Matches($text, [regex]::Escape($token)).Count }
function Assert-Order([string]$body, [string[]]$tokens, [string]$name) {
    $last = -1
    foreach ($token in $tokens) {
        $at = $body.IndexOf($token, $last + 1, [StringComparison]::Ordinal)
        Assert-Contract ($at -gt $last) "$name order changed at $token"
        $last = $at
    }
}
function Test-Serializer([string]$source, [string]$header) {
    Assert-Contract ($header -match 'EX_PUSH_LOCK') 'Serializer must own an EX_PUSH_LOCK'
    Assert-Contract ($header -match 'PVOID volatile Owner') 'Serializer must track its owner atomically'
    foreach ($term in @('caller-owned','zero-initialized','nonpaged','non-reentrant','no destructor')) {
        Assert-Contract ($header -match "(?i)$term") "Missing header contract: $term"
    }
    Assert-Contract ($header -match '(?is)outlive.*VMX_PERSISTENT_RESOURCE') 'Resource lifetime must be documented'
    Assert-Contract ($header -match '(?is)ResourceDestroy zeroes.*before invoking.*Leave') 'Destroy/Leave hazard must be documented'
    Assert-Contract ($header -match '(?is)Shutdown is PASSIVE-only and permanent.*after the final.*ResourceDestroy.*post-zero Leave') 'Shutdown timing must be documented'
    Assert-Contract ($header -match '(?i)cannot be reused/reinitialized') 'Post-shutdown rejection must be documented'

    $forbidden = '(?i)FAST_MUTEX|ExAcquireFastMutex|ExInitializeFastMutex|RunOnce|InitOnce|DriverEntry|IRP_MJ|IOCTL|KeSetSystemAffinityThread|VMX_V2'
    Assert-Contract ($source -notmatch $forbidden) 'Forbidden legacy/global/wiring pattern found'
    Assert-Contract ($source -notmatch '(?m)^\s*static\s+(?:EX_PUSH_LOCK|VMX_PERSISTENT_SERIALIZER)\s+\w+') 'Global serializer storage is forbidden'
    Assert-Contract ($source -notmatch 'VmxPersistentSerializerDestroy') 'This unit must not add a destructor'
    Assert-Contract ($source -match '#define VMX_SERIALIZER_READY \(\(LONG\)2\)') 'READY state changed'
    Assert-Contract ($source -match '#define VMX_SERIALIZER_SHUTDOWN \(\(LONG\)3\)') 'SHUTDOWN must be distinct and permanent'

    $enter = Get-Slice $source 'static bool VmxPersistentSerializerEnter' 'static void VmxPersistentSerializerLeave'
    $leave = Get-Slice $source 'static void VmxPersistentSerializerLeave' 'VMX_PERSISTENT_STATUS VmxPersistentSerializerInitialize'
    $init = Get-Slice $source 'VMX_PERSISTENT_STATUS VmxPersistentSerializerInitialize' 'VMX_PERSISTENT_STATUS VmxPersistentSerializerShutdown'
    $shutdown = Get-Slice $source 'VMX_PERSISTENT_STATUS VmxPersistentSerializerShutdown' 'VMX_PERSISTENT_COORDINATOR_SERIALIZER VmxPersistentSerializerFactory'
    $factory = $source.Substring($source.IndexOf('VMX_PERSISTENT_COORDINATOR_SERIALIZER VmxPersistentSerializerFactory', [StringComparison]::Ordinal))

    Assert-Contract ((Count-Token $init 'ExInitializePushLock(') -eq 1) 'Push lock must be initialized explicitly once'
    Assert-Contract ($enter -notmatch 'ExInitializePushLock') 'Enter must not lazily initialize'
    Assert-Order $init @('InterlockedCompareExchange(', 'VMX_SERIALIZER_UNINITIALIZED', 'ExInitializePushLock(', 'InterlockedExchangePointer(', 'VMX_SERIALIZER_READY') 'Initialize'

    Assert-Contract ((Count-Token $enter 'KeGetCurrentIrql()') -eq 1) 'Enter must check IRQL exactly once'
    Assert-Contract ((Count-Token $enter 'VmxPersistentSerializerState(') -eq 2) 'Enter must check READY before and after acquire'
    Assert-Order $enter @('KeGetCurrentIrql()', 'PsGetCurrentThread()', 'VmxPersistentSerializerOwner(serializer) == current', 'KeEnterCriticalRegion()', 'ExAcquirePushLockExclusive(', 'VmxPersistentSerializerState(', 'InterlockedCompareExchangePointer(') 'Enter owner/acquire'
    Assert-Order $enter @('InterlockedCompareExchangePointer(', 'ExReleasePushLockExclusive(', 'KeLeaveCriticalRegion()', 'return false;') 'Enter failure unwind'
    Assert-Contract ((Count-Token $enter 'ExAcquirePushLockExclusive(') -eq 1 -and (Count-Token $enter 'ExReleasePushLockExclusive(') -eq 1 -and (Count-Token $enter 'KeLeaveCriticalRegion()') -eq 1) 'Enter acquire-failure balance changed'

    Assert-Order $leave @('InterlockedExchangePointer(', 'ExReleasePushLockExclusive(', 'KeLeaveCriticalRegion()') 'Leave owner/release'
    Assert-Contract ((Count-Token $leave 'ExReleasePushLockExclusive(') -eq 1 -and (Count-Token $leave 'KeLeaveCriticalRegion()') -eq 1) 'Leave balance changed'

    Assert-Contract ((Count-Token $shutdown 'KeGetCurrentIrql()') -eq 1) 'Shutdown must PASSIVE-gate once'
    Assert-Order $shutdown @('KeGetCurrentIrql()', 'PsGetCurrentThread()', 'VmxPersistentSerializerOwner(serializer) == current', 'VMX_SERIALIZER_SHUTDOWN', 'KeEnterCriticalRegion()', 'ExAcquirePushLockExclusive(', 'VmxPersistentSerializerState(', 'InterlockedCompareExchangePointer(', 'InterlockedExchange(&serializer->Initialized, VMX_SERIALIZER_SHUTDOWN)', 'InterlockedExchangePointer(', 'ExReleasePushLockExclusive(', 'KeLeaveCriticalRegion()') 'Shutdown transition'
    Assert-Contract ((Count-Token $shutdown 'ExAcquirePushLockExclusive(') -eq 1 -and (Count-Token $shutdown 'ExReleasePushLockExclusive(') -eq 2 -and (Count-Token $shutdown 'KeLeaveCriticalRegion()') -eq 2) 'Shutdown branch balance changed'
    Assert-Contract ($factory -match '(?s)VmxPersistentSerializerState.*VMX_SERIALIZER_READY') 'Factory must reject shutdown storage'
}
$source = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath))
$header = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath))
Test-Serializer $source $header
$releaseLeavePattern = [regex]::new('ExReleasePushLockExclusive\(&serializer->Lock\);\r?\n    KeLeaveCriticalRegion\(\);')
$reversedReleaseLeave = $releaseLeavePattern.Replace(
    $source,
    "KeLeaveCriticalRegion();`n    ExReleasePushLockExclusive(&serializer->Lock);",
    1)
Assert-Contract ($reversedReleaseLeave -ne $source) 'Release/leave mutation must be non-vacuous'
$mutations = @(
    @($source.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL', 'FALSE'), $header, 'missing PASSIVE gate'),
    @($source.Replace('VmxPersistentSerializerOwner(serializer) == current', 'false'), $header, 'missing owner reentry gate'),
    @($source.Replace('ExAcquirePushLockExclusive', 'AcquireMutated'), $header, 'missing exclusive acquire'),
    @($source.Replace('InterlockedExchange(&serializer->Initialized, VMX_SERIALIZER_SHUTDOWN);', 'InterlockedExchange(&serializer->Initialized, VMX_SERIALIZER_READY);'), $header, 'missing shutdown transition'),
    @($reversedReleaseLeave, $header, 'reversed release/leave order'),
    @($source + "`nFAST_MUTEX forbiddenMutation;", $header, 'FAST_MUTEX'),
    @($source, $header.Replace('cannot be reused/reinitialized', 'may be reused'), 'missing post-shutdown contract')
)
foreach ($mutation in $mutations) {
    $rejected = $false
    try { Test-Serializer $mutation[0] $mutation[1] } catch { $rejected = $true }
    Assert-Contract $rejected "Verifier accepted mutation: $($mutation[2])"
}
Write-Output 'Verified explicit init, owner-based reentry rejection, PASSIVE/exclusive ordering, permanent shutdown, balanced unwind/Leave, lifetime, and mutation rejection.'