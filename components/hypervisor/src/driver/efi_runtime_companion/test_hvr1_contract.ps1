[CmdletBinding()]
param(
    [string]$SourceDirectory = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

if ([String]::IsNullOrWhiteSpace($SourceDirectory)) {
    $SourceDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
}

function Assert-Contains([string]$Path, [string]$Needle) {
    $text = Get-Content -LiteralPath $Path -Raw
    if ($text.IndexOf($Needle, [StringComparison]::Ordinal) -lt 0) {
        throw "Missing '$Needle' in $Path"
    }
}

function Assert-NotContains([string]$Path, [string]$Needle) {
    $text = Get-Content -LiteralPath $Path -Raw
    if ($text.IndexOf($Needle, [StringComparison]::Ordinal) -ge 0) {
        throw "Unexpected '$Needle' in $Path"
    }
}

$protocol = Join-Path $SourceDirectory 'hvr1_protocol.h'
$source = Join-Path $SourceDirectory 'efi_runtime_companion.c'
$header = Join-Path $SourceDirectory 'efi_runtime_companion.h'
$driver = Join-Path $SourceDirectory 'hvr1_driver.c'
$asm = Join-Path $SourceDirectory 'hvr1_vmcall.asm'
$cmake = Join-Path $SourceDirectory 'CMakeLists.txt'
$readme = Join-Path $SourceDirectory 'README.md'
$auth = Join-Path $SourceDirectory 'hvr1_auth.c'

foreach ($path in @($protocol, $source, $header, $driver, $asm, $cmake, $readme, $auth)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing contract file: $path"
    }
}

Assert-Contains $protocol 'HVR1_PROTOCOL_MAGIC UINT32_C(0x31525648)'
Assert-Contains $protocol 'HVR1_MAILBOX_BYTES UINT32_C(65536)'
Assert-Contains $protocol 'HVR1_MAILBOX_HEADER_BYTES UINT32_C(84)'
Assert-Contains $protocol 'HVR1_MAILBOX_ACK_SLOTS UINT32_C(4096)'
Assert-Contains $protocol 'HVR1_MAILBOX_ACK_OFFSET UINT32_C(49152)'
Assert-Contains $protocol 'HVR1_MAILBOX_ACK_PREPARED UINT32_C(3)'
Assert-Contains $protocol 'HVR1_EFI_CALL_QUERY_SESSION UINT32_C(3)'
Assert-Contains $protocol 'HVR1_EFI_CALL_SHUTDOWN_PREPARE UINT32_C(4)'
Assert-Contains $protocol 'HVR1_EFI_CALL_SHUTDOWN_COMMIT UINT32_C(5)'
Assert-Contains $protocol 'HVR1_METHOD_BUFFERED UINT32_C(0)'

Assert-Contains $source 'MmAllocateContiguousMemorySpecifyCache'
Assert-Contains $source 'POOL_FLAG_NON_PAGED'
Assert-Contains $source 'MmGetPhysicalAddress'
Assert-Contains $source 'Hvr1ValidateMailbox'
Assert-Contains $source 'Hvr1VerifyMailboxAuthTag'
Assert-Contains $source 'BCryptGenRandom'
Assert-Contains $auth 'BCryptHash'
Assert-Contains $auth 'volatile UCHAR difference'
Assert-Contains $source 'view.Sequence <= (UINT64)Companion->LastSequence'
Assert-Contains $source 'KeIpiGenericCall'
Assert-Contains $source 'Hvr1Cpl0QuerySession'
Assert-Contains $source 'Hvr1Cpl0Detach'
Assert-Contains $source 'HVR1_COMPANION_STATE_QUARANTINED'
Assert-Contains $source 'Hvr1CompanionPrepareForPower'
Assert-Contains $source 'Hvr1CompanionRecordCrashMarker'
Assert-Contains $source 'InterlockedCompareExchange(&Companion->Crash.Recorded'
Assert-Contains $source 'Hvr1CompanionCanUnload'
Assert-NotContains $source 'MmMapIoSpace'

# Every processor must execute the complete sequence itself, with PREPARE and
# COMMIT fused inside the assembly wrapper.
$sourceText = Get-Content -LiteralPath $source -Raw
$cpuStart = $sourceText.IndexOf('Hvr1ShutdownOnCpu(', [StringComparison]::Ordinal)
$submitStart = $sourceText.IndexOf('Hvr1SubmitShutdownLocked(', [StringComparison]::Ordinal)
if ($cpuStart -lt 0 -or $submitStart -le $cpuStart) {
    throw 'Could not isolate per-CPU shutdown callback'
}
$cpuBody = $sourceText.Substring($cpuStart, $submitStart - $cpuStart)
Assert-Contains $source 'Hvr1Cpl0QuerySession(&cpu->Session)'
Assert-Contains $source 'status = Hvr1Cpl0Detach('
Assert-Contains $source '&companion->DetachBarrier'
if ($cpuBody.IndexOf('Hvr1Cpl0Detach(', [StringComparison]::Ordinal) -lt 0) {
    throw 'The fused PREPARE/COMMIT transaction must remain inside Hvr1ShutdownOnCpu'
}

# The crash marker path is deliberately evidence-only.  Keep this check local
# to its function body so helper code can still contain the VMX wrappers.
$crashStart = $sourceText.LastIndexOf('Hvr1CompanionRecordCrashMarker', [StringComparison]::Ordinal)
$unloadStart = $sourceText.LastIndexOf('Hvr1CompanionCanUnload', [StringComparison]::Ordinal)
if ($crashStart -lt 0 -or $unloadStart -le $crashStart) {
    throw 'Could not isolate crash marker function'
}
$crashBody = $sourceText.Substring($crashStart, $unloadStart - $crashStart)
if ($crashBody.Contains('Hvr1Cpl0') -or $crashBody.Contains('KeIpiGenericCall') -or
    $crashBody.Contains('ExAllocatePool')) {
    throw 'Crash marker path must not invoke VMCALL, VMXOFF, IPI, or allocation'
}

Assert-Contains $driver 'IoCreateDeviceSecure'
Assert-Contains $driver 'D:P(A;;GA;;;SY)'
Assert-Contains $driver 'DO_BUFFERED_IO'
Assert-Contains $driver 'IRP_MJ_DEVICE_CONTROL'
Assert-Contains $driver 'IRP_MJ_POWER'
Assert-Contains $driver 'KeRegisterBugCheckReasonCallback'
Assert-Contains $driver 'KeDeregisterBugCheckReasonCallback'
Assert-Contains $driver 'Hvr1CompanionCanUnload'
Assert-Contains $driver 'Hvr1CompanionRecordCrashMarker'
Assert-Contains $driver 'DriverObject->DriverUnload = NULL'
Assert-Contains $driver 'Hvr1DispatchUnsupported'
Assert-NotContains $driver 'DriverObject->MajorFunction[major] = Hvr1DispatchCreateClose'

Assert-Contains $asm 'HVR1_EFI_CALL_MAGIC EQU 055454649564D5831h'
Assert-Contains $asm 'HVR1_EFI_CALL_QUERY_SESSION EQU 3'
Assert-Contains $asm 'HVR1_EFI_CALL_PREPARE_NATIVE_DETACH EQU 4'
Assert-Contains $asm 'HVR1_EFI_CALL_COMMIT_NATIVE_DETACH EQU 5'
Assert-Contains $asm 'mov rdx, qword ptr [r13 + 00h] ; token0'
Assert-Contains $asm 'Hvr1Cpl0Detach PROC'
Assert-Contains $asm 'lea rdx, [rsp + 20h]'
Assert-Contains $asm 'pushfq'
Assert-Contains $asm 'cli'
Assert-Contains $asm 'Hvr1Cpl0DetachWaitPrepared:'
Assert-Contains $asm 'lock inc dword ptr [r14 + 00h]'
Assert-Contains $asm 'Hvr1Cpl0DetachWaitDetached:'
Assert-Contains $asm 'lock inc dword ptr [r14 + 04h]'
Assert-Contains $asm 'vmcall'
Assert-Contains $asm 'vmclear qword ptr [r13]'
Assert-Contains $asm 'and r11, NOT 2000h'
Assert-Contains $asm 'HVR1_MAILBOX_ACK_FAILURE EQU 2'
Assert-Contains $cmake 'ENTRY:DriverEntry'
Assert-Contains $cmake 'wdmsec.lib'
Assert-Contains $cmake 'hvr1_auth.c'
Assert-Contains $readme 'shared HMAC session key'
Assert-Contains $readme 'KeIpiGenericCall'
Assert-Contains $readme 'QUARANTINED'
Assert-Contains $readme 'LocalSystem'
Assert-Contains $readme 'not a PnP power-stack filter'
Assert-Contains $readme 'verifies `AuthTag`'

$contract = Join-Path $SourceDirectory 'hvr1_abi_contract.c'
$compiler = Get-Command clang.exe -ErrorAction SilentlyContinue
if ($null -eq $compiler) {
    $compiler = Get-Command gcc.exe -ErrorAction SilentlyContinue
}
if ($null -eq $compiler) {
    $compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
}

if ($null -ne $compiler) {
    $tempDirectory = Join-Path ([IO.Path]::GetTempPath()) ('hvr1-contract-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tempDirectory | Out-Null
    try {
        $objectPath = Join-Path $tempDirectory 'hvr1_abi_contract.obj'
        if ($compiler.Name -ieq 'cl.exe') {
            & $compiler.Source /nologo /TC /W4 /WX /c $contract "/Fo$objectPath"
        } else {
            & $compiler.Source -std=c11 -Wall -Wextra -Werror -c $contract -o $objectPath
        }
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $objectPath)) {
            throw 'WDK-free ABI contract compilation failed'
        }
    } finally {
        Remove-Item -LiteralPath $tempDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Output 'HVR1 companion source contracts: PASS'
