param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$RuntimePath,
 [Parameter(Mandatory=$true)][string]$HookPath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath;$h=Get-Content -Raw -LiteralPath $HeaderPath
$c=Get-Content -Raw -LiteralPath $DriverCMakePath
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);if($i-lt 0){return ''};if([string]::IsNullOrEmpty($b)){return $x.Substring($i)};$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function V([string]$x){
 $current=B $x 'static VMX_EPT_INSTALL_NATIVE_STATUS Current(' 'static int MapInstructionOutcome('
 $cap=B $x 'static uint32_t NativeSupportsActivation(' 'static int NativeRead('
 $read=B $x 'static int NativeRead(' 'static int NativeWrite('
 $write=B $x 'static int NativeWrite(' 'static uint32_t NativeReadError('
 $error=B $x 'static uint32_t NativeReadError(' 'VMX_EPT_INSTALL_NATIVE_STATUS VmxEptInstallationNativeInitialize('
 $init=B $x 'VMX_EPT_INSTALL_NATIVE_STATUS VmxEptInstallationNativeInitialize(' 'VMX_EPT_INSTALL_NATIVE_STATUS VmxEptInstallationNativeValidate('
 $validate=B $x 'VMX_EPT_INSTALL_NATIVE_STATUS VmxEptInstallationNativeValidate(' 'static int OwnerSeparate('
 $call=B $x 'VMX_EPT_KERNEL_INSTALLATION_STATUS VmxEptKernelCoordinateInstallation(' ''
 return (O $current @('KeGetCurrentIrql() != DISPATCH_LEVEL','Snapshot(context, &snapshot)',
  'VmxEptNativePlatformValidate(','__vmx_vmptrst(&current_vmcs)',
  'current_vmcs == snapshot.CurrentVmcsPhysicalAddress')) -and
 (O $cap @('Current(','VMX_EPT_INSTALL_NATIVE_OK','__readmsr(IA32_VMX_BASIC)',
  'VmxSelectTrueControl(','IA32_VMX_TRUE_PROCBASED_CTLS','IA32_VMX_PROCBASED_CTLS',
  '__readmsr(IA32_VMX_PROCBASED_CTLS2)','primary >> 32','secondary >> 32',
  'CPU_BASED_ACTIVATE_SECONDARY_CTLS','SECONDARY_ENABLE_EPT','? 1u : 0u')) -and
 (O $read @('field != VMCS_CTRL_EPTP','field != VMCS_CTRL_CPU_BASED',
  'field != VMCS_CTRL_SECONDARY','Separate(opaque','output, sizeof(*output)',
  'Current(','__vmx_vmread(field, &value)','MapInstructionOutcome(result)',
  '*output = (uint64_t)value','VMX_VMWRITE_SUCCESS')) -and
 (O $write @('field == VMCS_CTRL_EPTP && value != 0',
  'field == VMCS_CTRL_SECONDARY','SECONDARY_ENABLE_EPT',
  'field == VMCS_CTRL_CPU_BASED','CPU_BASED_ACTIVATE_SECONDARY_CTLS',
  'Current(','__vmx_vmwrite(field, value)','MapInstructionOutcome(result)')) -and
 (O $error @('Separate(opaque','Current(','__vmx_vmread(VMCS_VM_INSTRUCTION_ERROR',
  '*error = (uint64_t)value','return 1')) -and
 (O $init @('KeGetCurrentIrql() != PASSIVE_LEVEL','RtlCompareMemory(context, &zero',
  'RtlCompareMemory(platform, &zero_platform','VmxEptNativeContextValidate(native_context)',
  'current_vmcs_physical_address == 0','staged.NativeContext = native_context',
  'staged.NativePlatform = native_platform','staged_platform.Write = NativeWrite',
  'staged_platform.Read = NativeRead',
  'staged_platform.SupportsActivation = NativeSupportsActivation',
  'staged_platform.ReadInstructionError = NativeReadError',
  'RtlCopyMemory(context, &staged','*platform = staged_platform','InterlockedExchange')) -and
 (O $validate @('Snapshot(context, &snapshot)','IdentityMatches(&snapshot, identity)',
  'platform_snapshot.Context != context','Write != NativeWrite','Read != NativeRead',
  'SupportsActivation != NativeSupportsActivation',
  'ReadInstructionError != NativeReadError','CurrentVmcsValid != 1','Current(context)')) -and
 (O $call @('KeGetCurrentIrql() != DISPATCH_LEVEL','Snapshot(context, &snapshot)',
  'OwnerSeparate(owner, context','OwnerSeparate(owner, platform',
  'OwnerSeparate(owner, report','VmxEptInstallationNativeValidate(context, identity, platform)',
  'native_status != VMX_EPT_INSTALL_NATIVE_OK','MapNative(native_status)',
  'MapCoordinator(VmxEptCoordinateInstallation(','owner, identity, platform, report'))
}
A(V $s)'trusted activation adapter ordering'
foreach($v in @('caller-owned nonpaged','PASSIVE_LEVEL','immutable','page-aligned',
 'exact context/platform pair','DISPATCH_LEVEL','current VMCS','without modifying the VMCS',
 'Trusted WDK binding','pinned','externally serializes','Pre-instruction failures preserve report',
 'allocates nothing','no pin','root transition','table write','runtime','hook','IOCTL',
 'native MSR reads and VMREAD/VMWRITE','authenticated E3a seam')){A($h.Contains($v))"header $v"}
A((([regex]::Matches($c,'(?m)^\s{8}vmx_ept_installation_adapter\.c\r?$')).Count-eq 1))'driver source once'
A(([regex]::Matches($s,'__vmx_vmwrite')).Count-eq 1)'exactly one native VMWRITE site'
A(([regex]::Matches($s,'__vmx_vmread')).Count-eq 2)'exactly two native VMREAD sites'
A(([regex]::Matches($s,'__vmx_vmptrst')).Count-eq 1)'exactly one native VMPTRST site'
A(([regex]::Matches($s,'__readmsr')).Count-eq 3)'exactly three capability MSR reads'
A($s-notmatch 'VmxEpt(SessionPrepareInstallation|Coverage|Domain|RootCoordinator)|VmxEptInveptAsm|KeSetSystemGroupAffinityThread|KeRevertToUserGroupAffinityThread')'facade/native boundary bypass forbidden'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptKernelCoordinateInstallation|vmx_ept_installation_adapter')"direct adapter wiring forbidden: $p"}
$m=@(
 $s.Replace('__vmx_vmptrst(&current_vmcs);','current_vmcs = snapshot.CurrentVmcsPhysicalAddress;'),
 $s.Replace('__readmsr(IA32_VMX_PROCBASED_CTLS2)','UINT64_MAX'),
 $s.Replace('(primary_allowed & CPU_BASED_ACTIVATE_SECONDARY_CTLS) != 0','1'),
 $s.Replace('field != VMCS_CTRL_SECONDARY','field == VMCS_CTRL_SECONDARY'),
 $s.Replace('__vmx_vmread(field, &value)','__vmx_vmread(0, &value)'),
 $s.Replace('field == VMCS_CTRL_SECONDARY','field == 0'),
 $s.Replace('__vmx_vmwrite(field, value)','__vmx_vmwrite(0, value)'),
 $s.Replace('staged_platform.Read = NativeRead','staged_platform.Read = NULL'),
 $s.Replace('staged_platform.SupportsActivation = NativeSupportsActivation','staged_platform.SupportsActivation = NULL'),
 $s.Replace('platform_snapshot.Read != NativeRead','0'),
 $s.Replace('VmxEptInstallationNativeValidate(context, identity, platform)','VMX_EPT_INSTALL_NATIVE_OK'),
 $s.Replace('VmxEptCoordinateInstallation(','VmxEptKernelCoordinateInstallation(')
)
for($i=0;$i-lt$m.Count;$i++){$x=$m[$i];A($x-ne$s)("mutation $i missing");A(-not(V $x))("accepted adapter mutation $i")}
Write-Host 'PASS: EPT trusted native activation binding contracts'
