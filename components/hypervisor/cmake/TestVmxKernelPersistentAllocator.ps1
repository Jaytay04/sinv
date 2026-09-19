param([Parameter(Mandatory=$true)][string]$SourcePath,
      [Parameter(Mandatory=$true)][string]$HeaderPath)
$ErrorActionPreference='Stop'
function Assert-C([bool]$ok,[string]$why){if(-not $ok){throw $why}}
function Slice([string]$t,[string]$a,[string]$b){
 $i=$t.IndexOf($a,[StringComparison]::Ordinal);$j=$t.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal)
 Assert-C ($i-ge 0-and $j-gt $i) "Missing section $a";$t.Substring($i,$j-$i)
}
function Order([string]$t,[string[]]$tokens,[string]$name){$last=-1;foreach($x in $tokens){$at=$t.IndexOf($x,$last+1,[StringComparison]::Ordinal);Assert-C ($at-gt $last) "$name order changed at $x";$last=$at}}
function Count([string]$t,[string]$x){([regex]::Matches($t,[regex]::Escape($x))).Count}
function Verify([string]$s,[string]$h){
 foreach($x in @('Immutable WDK allocator seam','Persistent V3 only','PASSIVE_LEVEL','Context is intentionally NULL','nonpaged','highest PA','cache type','page-alignment','stable','no destructor')){Assert-C ($h-match "(?i)$x") "Missing contract $x"}
 Assert-C ($h-match '#include "vmx_persistent_context.h"') 'Allocator type include missing'
 Assert-C ($h-match 'const VMX_PERSISTENT_ALLOCATOR\* VmxKernelPersistentAllocator\(void\);') 'Factory declaration changed'
 $legacy=Slice $s 'const VMX_ALLOCATION_INTERFACE KernelAllocationInterface' 'Persistent V3 allocator callbacks'
 $new=Slice $s 'Persistent V3 allocator callbacks' 'Persistent V3 kernel topology and affinity callbacks'
 Assert-C ($new-notmatch '(?i)VMX_V2|vmx_transient|CPU0|Affinity|DriverEntry|IRP_MJ|IOCTL|ExAllocate|ExFreePool|MmAllocate|MmFree|MmGetPhysicalAddress|g_[A-Za-z]') 'Persistent allocator crossed legacy/WDK boundary'
 Assert-C ((Count $new 'static const VMX_PERSISTENT_ALLOCATOR allocator')-eq 1) 'Immutable allocator storage changed'
 Order $legacy @('KernelAllocatePool','KernelFreePool','KernelAllocateContiguous','KernelFreeContiguous','KernelGetPhysicalAddress') 'Legacy allocator preserved'
 $pool=Slice $new 'static void* KernelPersistentAllocatePool' 'static void KernelPersistentFreePool'
 $free=Slice $new 'static void KernelPersistentFreePool' 'static void* KernelPersistentAllocateContiguous'
 $contig=Slice $new 'static void* KernelPersistentAllocateContiguous' 'static void KernelPersistentFreeContiguous'
 $freec=Slice $new 'static void KernelPersistentFreeContiguous' 'static uint64_t KernelPersistentPhysicalAddress'
 $pa=Slice $new 'static uint64_t KernelPersistentPhysicalAddress' 'const VMX_PERSISTENT_ALLOCATOR* VmxKernelPersistentAllocator'
 $factory=$new.Substring($new.IndexOf('const VMX_PERSISTENT_ALLOCATOR* VmxKernelPersistentAllocator'))
 Order $pool @('UNREFERENCED_PARAMETER(context)','return KernelAllocatePool(size)') 'Pool allocation delegation'
 Order $free @('UNREFERENCED_PARAMETER(context)','KernelFreePool(memory)') 'Pool free delegation'
 Order $contig @('UNREFERENCED_PARAMETER(context)','return KernelAllocateContiguous(size, highest_physical_address)') 'Contiguous delegation'
 Order $freec @('UNREFERENCED_PARAMETER(context)','KernelFreeContiguous(memory)') 'Contiguous free delegation'
 Order $pa @('UNREFERENCED_PARAMETER(context)','return KernelGetPhysicalAddress(virtual_address)') 'Physical-address delegation'
 Order $factory @('static const VMX_PERSISTENT_ALLOCATOR allocator','NULL','KernelPersistentAllocatePool','KernelPersistentFreePool','KernelPersistentAllocateContiguous','KernelPersistentFreeContiguous','KernelPersistentPhysicalAddress','return &allocator') 'Immutable exact factory'
 Assert-C ((Count $factory 'return &allocator')-eq 1) 'Factory stability changed'
}
$s=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath));Verify $s $h
$marker=' * Persistent V3 kernel topology and affinity callbacks'
$mutations=@(
 $h.Replace('#include "vmx_persistent_context.h"','#include "missing.h"'),
 $h.Replace('PASSIVE_LEVEL','APC_LEVEL'),
 $h.Replace('nonpaged','paged'),
 $s.Replace('static const VMX_PERSISTENT_ALLOCATOR allocator','static VMX_PERSISTENT_ALLOCATOR allocator'),
 $s.Replace('        NULL,','        (void*)1,'),
 $s.Replace('return KernelAllocatePool(size)','return ExAllocatePool2(0, size, 0)'),
 $s.Replace('KernelFreePool(memory)','ExFreePool(memory)'),
 $s.Replace('KernelAllocateContiguous(size, highest_physical_address)','KernelAllocateContiguous(size, UINT64_MAX)'),
 $s.Replace('KernelFreeContiguous(memory)','MmFreeContiguousMemory(memory)'),
 $s.Replace('KernelGetPhysicalAddress(virtual_address)','0'),
 $s.Replace('        KernelPersistentFreeContiguous,','        KernelPersistentFreePool,'),
 $s.Replace('return &allocator','return NULL'),
 $s.Replace('const VMX_ALLOCATION_INTERFACE KernelAllocationInterface','const VMX_ALLOCATION_INTERFACE LegacyChanged'),
 $s.Replace($marker,'CPU0 forbiddenMutation;'+[Environment]::NewLine+$marker))
foreach($m in $mutations){Assert-C ($m-ne $s) 'Mutation did not apply';$bad=$false;try{if($m.Contains('#ifndef')){Verify $s $m}else{Verify $m $h}}catch{$bad=$true};Assert-C $bad 'Verifier accepted required-contract mutation'}
Write-Output 'Verified A4a1 immutable NULL-context Persistent V3 allocator, PASSIVE/nonpaged contract, exact existing-wrapper delegation, legacy separation, stable factory, and mutations.'
