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
 $mc=B $x 'static VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS MapConfig' 'static VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS MapSession'
 $ms=B $x 'static VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS MapSession' 'VmxEptKernelConfiguredSessionInitialize('
 $init=B $x 'VmxEptKernelConfiguredSessionInitialize(' 'VmxEptKernelConfiguredSessionDestroy('
 $destroy=$x.Substring($x.IndexOf('VmxEptKernelConfiguredSessionDestroy('))
 return (O $mc @('VMX_EPT_KERNEL_CONFIG_OK','CONFIGURED_SESSION_OK','VMX_EPT_KERNEL_CONFIG_INVALID','CONFIGURED_SESSION_INVALID','VMX_EPT_KERNEL_CONFIG_WRONG_IRQL','CONFIGURED_SESSION_WRONG_IRQL','VMX_EPT_KERNEL_CONFIG_CAPTURE_FAILED','CONFIGURED_SESSION_CAPTURE_FAILED','VMX_EPT_KERNEL_CONFIG_RANGE_INVALID','CONFIGURED_SESSION_RANGE_INVALID','VMX_EPT_KERNEL_CONFIG_UNSUPPORTED','CONFIGURED_SESSION_UNSUPPORTED','VMX_EPT_KERNEL_CONFIG_OVERFLOW','CONFIGURED_SESSION_OVERFLOW','VMX_EPT_KERNEL_CONFIG_INVARIANT','CONFIGURED_SESSION_INVARIANT'))-and
  (O $ms @('VMX_EPT_KERNEL_SESSION_OK','CONFIGURED_SESSION_OK','VMX_EPT_KERNEL_SESSION_INVALID','CONFIGURED_SESSION_INVALID','VMX_EPT_KERNEL_SESSION_WRONG_IRQL','CONFIGURED_SESSION_WRONG_IRQL','VMX_EPT_KERNEL_SESSION_TOPOLOGY_FAILED','CONFIGURED_SESSION_TOPOLOGY_FAILED','VMX_EPT_KERNEL_SESSION_UNSUPPORTED','CONFIGURED_SESSION_UNSUPPORTED','VMX_EPT_KERNEL_SESSION_ALLOCATION_FAILED','CONFIGURED_SESSION_ALLOCATION_FAILED','VMX_EPT_KERNEL_SESSION_REGION_INVALID','CONFIGURED_SESSION_REGION_INVALID','VMX_EPT_KERNEL_SESSION_BUSY','CONFIGURED_SESSION_BUSY','VMX_EPT_KERNEL_SESSION_DIRTY','CONFIGURED_SESSION_DIRTY'))-and
  (O $init @('VMX_EPT_SESSION_CONFIG config = { 0 }','owner == NULL','KeGetCurrentIrql() != PASSIVE_LEVEL','!ZeroOwner(owner)','VmxEptKernelSessionConfigBuild(&config)','config_status != VMX_EPT_KERNEL_CONFIG_OK','RtlSecureZeroMemory(&config, sizeof(config))','return MapConfig(config_status)','VmxEptKernelSessionInitialize(&config.Capabilities','&config.Identity, &config.Populate, owner)','RtlSecureZeroMemory(&config, sizeof(config))','return MapSession(session_status)'))-and
  (O $destroy @('MapSession(VmxEptKernelSessionDestroy(owner))'))
}
A(V $s)'configured-session composition ordering'
foreach($v in @('PASSIVE-only production composition','D4d capture','D4b construction','Owner must be caller-owned','nonpaged','zero','externally serialized','stack-local','securely wiped','hot-add/remove remains excluded','no native','VMCS','Persistent V3','hook','IOCTL','live-VMX')){A($h.Contains($v))"header $v"}
foreach($v in @('CAPTURE_FAILED','RANGE_INVALID','UNSUPPORTED','OVERFLOW','INVARIANT','TOPOLOGY_FAILED','ALLOCATION_FAILED','REGION_INVALID','BUSY','DIRTY')){A($h.Contains("CONFIGURED_SESSION_$v"))"status $v"}
A(($c|Select-String -Pattern 'vmx_ept_configured_session_adapter\.c' -AllMatches).Matches.Count-eq 1)'source once'
A($s-notmatch 'VmxEptSessionOwnerInitialize|VmxEptKernelSessionConfigBuildWithSource|__readmsr|__vmx|VmxEptInveptAsm|VmxEptNative|DeviceIoControl|IOCTL|Wdf|DriverEntry')'forbidden bypass/wiring'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath,$NativePath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptKernelConfiguredSession')"composition remains unwired: $p"}
$m=@(
 $s.Replace('owner == NULL','0'),
 $s.Replace('!ZeroOwner(owner)','0'),
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','0'),
 $s.Replace('VmxEptKernelSessionConfigBuild(&config)','VMX_EPT_KERNEL_CONFIG_OK'),
 $s.Replace('config_status != VMX_EPT_KERNEL_CONFIG_OK','0'),
 $s.Replace('return MapConfig(config_status);','return VMX_EPT_KERNEL_CONFIGURED_SESSION_OK;'),
 $s.Replace('&config.Identity, &config.Populate, owner','&config.Identity, &config.Populate, NULL'),
 $s.Replace('session_status = VmxEptKernelSessionInitialize','session_status = VMX_EPT_KERNEL_SESSION_OK; /*'),
 $s.Replace('RtlSecureZeroMemory(&config, sizeof(config));',''),
 $s.Replace('return MapSession(session_status);','return VMX_EPT_KERNEL_CONFIGURED_SESSION_OK;'),
 $s.Replace('MapSession(VmxEptKernelSessionDestroy(owner))','VMX_EPT_KERNEL_CONFIGURED_SESSION_OK'),
 $s.Replace('return VMX_EPT_KERNEL_CONFIGURED_SESSION_CAPTURE_FAILED;','return VMX_EPT_KERNEL_CONFIGURED_SESSION_INVALID;'),
 $s.Replace('return VMX_EPT_KERNEL_CONFIGURED_SESSION_BUSY;','return VMX_EPT_KERNEL_CONFIGURED_SESSION_INVALID;'),
 $s.Replace('return VMX_EPT_KERNEL_CONFIGURED_SESSION_DIRTY;','return VMX_EPT_KERNEL_CONFIGURED_SESSION_INVALID;')
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i]-ne$s)"mutation $i missing";A(-not(V $m[$i]))"accepted mutation $i"}
Write-Host 'PASS: EPT trusted configured-session composition contracts'