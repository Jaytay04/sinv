param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$RuntimePath,
 [Parameter(Mandatory=$true)][string]$HookPath,
 [Parameter(Mandatory=$true)][string]$NativePath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath
$h=Get-Content -Raw -LiteralPath $HeaderPath
$c=Get-Content -Raw -LiteralPath $DriverCMakePath
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($i-lt 0-or$j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function V([string]$x){
 $capture=B $x 'static bool CaptureHighest' 'static bool NativePhysicalAddressBits'
 $bits=B $x 'static bool NativePhysicalAddressBits' 'static PPHYSICAL_MEMORY_RANGE NativeQueryRanges'
 $native=B $x 'static PPHYSICAL_MEMORY_RANGE NativeQueryRanges' 'VmxEptKernelSessionConfigBuildWithSource('
 $build=B $x 'VmxEptKernelSessionConfigBuildWithSource(' 'VmxEptKernelSessionConfigBuild('
 $trusted=$x.Substring($x.IndexOf('VmxEptKernelSessionConfigBuild('))
 return (O $capture @('index < VMX_EPT_KERNEL_RANGE_LIMIT','signed_base == 0 && signed_bytes == 0','!any','previous_end - 1','signed_base < 0','signed_bytes <= 0','(base & (PAGE_SIZE - 1)) != 0','(bytes & (PAGE_SIZE - 1)) != 0','base > UINT64_MAX - bytes','base + bytes','base < previous_end','return false;'))-and
  (O $bits @('__cpuid','0x80000000','(uint32_t)registers[0] < UINT32_C(0x80000008)','__cpuid(registers, (int)UINT32_C(0x80000008))','registers[0] & UINT32_C(0xFF)'))-and
  (O $native @('MmGetPhysicalMemoryRangesEx2(MM_SYSTEM_PARTITION_OBJECT, 0)','ExFreePool(ranges)'))-and
  (O $build @('source == NULL','source->CheckCpuidVmx == NULL','source->ReadMsr == NULL','source->QueryPhysicalAddressBits == NULL','source->QueryRanges == NULL','source->FreeRanges == NULL','out_config == NULL','Zero(out_config','KeGetCurrentIrql() != PASSIVE_LEVEL','source->CheckCpuidVmx()','IA32_VMX_EPT_VPID_CAP','QueryPhysicalAddressBits','source->QueryRanges()','ranges == NULL','CaptureHighest(ranges, &highest)','source->FreeRanges(ranges)','!range_valid','HighestPresentPhysicalAddress = highest','VmxEptSessionConfigBuild','VmxEptSessionConfigValidate','RtlCopyMemory(out_config'))-and
  (O $trusted @('KernelPreflightPlatform.CheckCpuidVmx','KernelPreflightPlatform.ReadMsr','NativePhysicalAddressBits','NativeQueryRanges','NativeFreeRanges','VmxEptKernelSessionConfigBuildWithSource'))
}
A(V $s)'trusted config capture ordering'
foreach($v in @('Injectable only for bounded verification','immutable','zero-terminated','nonpaged','exact FreeRanges','PASSIVE-only trusted capture','Out must be zero','owned Windows physical-range snapshot','Hot-add/remove is excluded','no D4b construction','VMCS','runtime','hook','IOCTL','live-VMX')){A($h.Contains($v))"header $v"}
A($h.Contains('#include <stdbool.h>'))'C bool definition'
A($h.Contains('VMX_EPT_KERNEL_RANGE_LIMIT UINT32_C(4096)'))'range cap'
A(($c|Select-String -Pattern 'vmx_ept_session_config_adapter\.c' -AllMatches).Matches.Count-eq 1)'source once'
A($s-notmatch '(?i)__readmsr|__vmx|VmxEptInveptAsm|VmxEptNative|VMCS|DeviceIoControl|IOCTL|hook|Runtime(Start|Stop|Initialize)|DriverEntry|Wdf')'forbidden direct wiring'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath,$NativePath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptKernelSessionConfig')"adapter remains unwired: $p"}
$m=@(
 $s.Replace('index < VMX_EPT_KERNEL_RANGE_LIMIT','index <= VMX_EPT_KERNEL_RANGE_LIMIT'),
 $s.Replace('signed_base == 0 && signed_bytes == 0','signed_bytes == 0'),
 $s.Replace('signed_base < 0 || signed_bytes <= 0','signed_base < 0'),
 $s.Replace('(bytes & (PAGE_SIZE - 1)) != 0 ||',''),
 $s.Replace('base > UINT64_MAX - bytes','0'),
 $s.Replace('any && base < previous_end','0'),
 $s.Replace('(uint32_t)registers[0] < UINT32_C(0x80000008)','0'),
 $s.Replace('MmGetPhysicalMemoryRangesEx2(MM_SYSTEM_PARTITION_OBJECT, 0)','MmGetPhysicalMemoryRangesEx2(MM_ALL_PARTITIONS_OBJECT, 0)'),
 $s.Replace('ExFreePool(ranges);',''),
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','0'),
 $s.Replace('!source->CheckCpuidVmx()','0'),
 $s.Replace('IA32_VMX_EPT_VPID_CAP','IA32_VMX_BASIC'),
 $s.Replace('!source->QueryPhysicalAddressBits(&input.PhysicalAddressBits)','0'),
 $s.Replace('range_valid = CaptureHighest(ranges, &highest);','range_valid = true;'),
 $s.Replace('source->FreeRanges(ranges);',''),
 $s.Replace('!range_valid','0'),
 $s.Replace('VmxEptSessionConfigBuild(&input, &staged)','VMX_EPT_SESSION_CONFIG_OK'),
 $s.Replace('VmxEptSessionConfigValidate(&input, &staged)','VMX_EPT_SESSION_CONFIG_OK'),
 $s.Replace('RtlCopyMemory(out_config, &staged, sizeof(staged));','')
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i]-ne$s)"mutation $i missing";A(-not(V $m[$i]))"accepted mutation $i"}
Write-Host 'PASS: EPT trusted kernel configuration capture contracts'