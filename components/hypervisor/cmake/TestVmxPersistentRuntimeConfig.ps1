param([Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw$m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A ($i-ge0-and$j-gt$i) "section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n order $x";$p=$q}}
function V([string]$s,[string]$h,[string]$c){
 foreach($x in @('WDK-only','Structural validation authenticates','Out must be zero','remains unchanged','PASSIVE-only','INVALID means','NOT_READY means CET','fresh nonzero-cookie','INVARIANT means','only the current caller','NOT sufficient for LOAD','every target CPU','VmxPersistentRuntimeConfigValidate','VmxPersistentRuntimeConfigBuildWithSource','VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic','does not resample a caller CPU','Per-processor preentry checks remain active','VmxPersistentRuntimeConfigBuild')){A ($h.Contains($x)) "header $x"}
 foreach($x in @('uint64_t (*ReadCr4)(void);','bool (*CheckCpuidVmx)(void);','bool (*ReadMsr)(uint32_t msr, uint64_t* value);','bool (*GenerateRandom)(uint8_t* buffer, size_t size);')){A ($h.Contains($x)) "seam $x"}
 A ($h-notmatch 'PROTOCOL_COOKIE|0x56334633434F4F4B') 'no fixed cookie'
 A (([regex]::Matches($c,'vmx_persistent_runtime_config.c')).Count-eq1) 'driver source once'
 $valid=B $s 'bool VmxPersistentRuntimeConfigValidate' 'static VMX_PERSISTENT_STATUS BuildApprovedConfig'
 foreach($x in @('VmxKernelPersistentAllocator()','PlatformEqual(&config->Kernel, &KernelPersistentPlatform)','AllocatorEqual(&config->Allocator, allocator)','VmxParseBasicCapabilities(config->VmxBasic','LaunchStorageSize == VMX_V3D_CONTEXT_SIZE','Cookie != 0','Dispatcher == VmxV3dPersistentDispatch','&VmxV3dPersistentGuestContinuation','&VmxV3dPersistentVmExitEntry')){A ($valid.Contains($x)) "validate $x"}
 $approved=B $s 'static VMX_PERSISTENT_STATUS BuildApprovedConfig' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuildWithSource'
 O $approved @('GenerateRandom == NULL','out_config == NULL','!Zero(out_config, sizeof(*out_config))','VMX_PERSISTENT_STATUS_INVALID','KeGetCurrentIrql() != PASSIVE_LEVEL','VMX_PERSISTENT_STATUS_NOT_READY','VmxParseBasicCapabilities(vmx_basic','cookie = 0','GenerateRandom((uint8_t*)&cookie','cookie == 0','VmxKernelPersistentAllocator()','VMX_PERSISTENT_STATUS_INVARIANT','staged.Kernel = KernelPersistentPlatform','staged.Allocator = *allocator','staged.VmxBasic = vmx_basic','staged.LaunchStorageSize = VMX_V3D_CONTEXT_SIZE','staged.Cookie = cookie','staged.Dispatcher = VmxV3dPersistentDispatch','&VmxV3dPersistentGuestContinuation','&VmxV3dPersistentVmExitEntry','VmxPersistentRuntimeConfigValidate(&staged)','RtlCopyMemory(out_config, &staged','VMX_PERSISTENT_STATUS_OK') 'approved build'
 A (([regex]::Matches($approved,'RtlCopyMemory\(out_config')).Count-eq1) 'publish once'
 $build=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuildWithSource' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic'
 O $build @('source == NULL','source->ReadCr4 == NULL','source->CheckCpuidVmx == NULL','source->ReadMsr == NULL','source->GenerateRandom == NULL','out_config == NULL','!Zero(out_config, sizeof(*out_config))','KeGetCurrentIrql() != PASSIVE_LEVEL','cr4 = source->ReadCr4()','(cr4 & CR4_CET) != 0','!source->CheckCpuidVmx()','source->ReadMsr(IA32_FEATURE_CONTROL','FEATURE_CONTROL_LOCK_BIT |','FEATURE_CONTROL_VMXON_OUTSIDE_SMX','source->ReadMsr(IA32_VMX_BASIC','BuildApprovedConfig(vmx_basic, source->GenerateRandom, out_config)') 'bounded source build'
 $consensus=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuild('
 O $consensus @('BuildApprovedConfig(vmx_basic','KernelPreflightPlatform.GenerateRandom','out_config') 'consensus build'
 A ($consensus-notmatch'ReadCr4|CheckCpuidVmx|ReadMsr|IA32_VMX_BASIC') 'consensus builder resamples CPU'
 $native=$s.Substring($s.IndexOf('VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuild('))
 O $native @('KernelPreflightPlatform.ReadCr4','KernelPreflightPlatform.CheckCpuidVmx','KernelPreflightPlatform.ReadMsr','KernelPreflightPlatform.GenerateRandom','VmxPersistentRuntimeConfigBuildWithSource(&source, out_config)') 'native seam'
 A ($s-notmatch '(?i)IOCTL|Wdf|DriverEntry|DriverEvtUnload|OwnerAttach|Runtime(Start|Stop|Initialize)|__vmx|vmlaunch|vmresume|vmxoff|__writecr|ExAllocate|KeSetSystem|CPU0|g_vmx') 'forbidden'
}
$s=[IO.File]::ReadAllText((Resolve-Path $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath));$c=[IO.File]::ReadAllText((Resolve-Path $DriverCMakePath));V $s $h $c
$sm=@(
 $s.Replace('!Zero(out_config, sizeof(*out_config))','false'),
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','false'),
 $s.Replace('(cr4 & CR4_CET) != 0','false'),
 $s.Replace('!source->CheckCpuidVmx()','false'),
 $s.Replace('FEATURE_CONTROL_LOCK_BIT |','UINT64_C(0) |'),
 $s.Replace('!VmxParseBasicCapabilities(vmx_basic, &capabilities)','false'),
 $s.Replace('!GenerateRandom((uint8_t*)&cookie, sizeof(cookie))','false'),
 $s.Replace('cookie == 0','false'),
 $s.Replace('staged.Kernel = KernelPersistentPlatform','RtlZeroMemory(&staged.Kernel,sizeof(staged.Kernel))'),
 $s.Replace('staged.LaunchStorageSize = VMX_V3D_CONTEXT_SIZE','staged.LaunchStorageSize = 1'),
 $s.Replace('staged.Cookie = cookie','staged.Cookie = 0'),
 $s.Replace('staged.Dispatcher = VmxV3dPersistentDispatch','staged.Dispatcher = NULL'),
 $s.Replace('RtlCopyMemory(out_config, &staged, sizeof(staged));','RtlZeroMemory(out_config,sizeof(*out_config));')
)
$i=0;foreach($m in $sm){$i++;A ($m-ne$s) 'vacuous source mutation';$bad=$false;try{V $m $h $c}catch{$bad=$true};A $bad "accepted source mutation $i"}
$hm=@($h.Replace('NOT sufficient for LOAD','sufficient for LOAD'),$h.Replace('uint64_t (*ReadCr4)(void);','void* ReadCr4;'),$h.Replace('bool (*GenerateRandom)(uint8_t* buffer, size_t size);','void* GenerateRandom;'))
$i=0;foreach($m in $hm){$i++;A ($m-ne$h) 'vacuous header mutation';$bad=$false;try{V $s $m $c}catch{$bad=$true};A $bad "accepted header mutation $i"}
Write-Output 'Verified runtime config builder: trusted adapters, safe capability/CET/FEATURE_CONTROL gates, fresh nonzero cookie, exact mapping, atomic publication, and forbidden mutation rejection.'
