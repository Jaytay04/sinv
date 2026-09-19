param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$CoreCMakePath,
 [Parameter(Mandatory=$true)][string]$TestPath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$RuntimePath,
 [Parameter(Mandatory=$true)][string]$HookPath,
 [Parameter(Mandatory=$true)][string]$NativePath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath
$h=Get-Content -Raw -LiteralPath $HeaderPath
$c=Get-Content -Raw -LiteralPath $CoreCMakePath
$t=Get-Content -Raw -LiteralPath $TestPath
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($i-lt 0-or$j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function V([string]$x){
 $round=B $x 'static int RoundUp' 'static int CapabilitiesEqual'
 $equal=B $x 'static int ConfigEqual' 'VmxEptSessionConfigBuild('
 $build=B $x 'VmxEptSessionConfigBuild(' 'VmxEptSessionConfigValidate('
 $validate=$x.Substring($x.IndexOf('VmxEptSessionConfigValidate('))
 return (O $round @('alignment - 1','alignment == 0','alignment & mask','UINT64_MAX - mask','value + mask'))-and
  (O $equal @('CapabilitiesEqual','IdentityEqual','PopulateEqual','PlanEqual'))-and
  (O $build @('input == NULL','config == NULL','Separate(input, sizeof(*input), config, sizeof(*config))','input_snapshot = *input','input_snapshot.Reserved != 0','PhysicalAddressBits < 36','PhysicalAddressBits > 52','VmxEptDecodeCapabilities','input_snapshot.EptVpidCapabilities','VmxEptValidateCapabilities','HighestPresentPhysicalAddress == UINT64_MAX','PhysicalAddressBits < 48','UINT64_C(1) << effective_bits','HighestPresentPhysicalAddress >= address_limit','HighestPresentPhysicalAddress + 1','VMX_EPT_POLICY_LEAF_2MB','VMX_EPT_ENTRY_LEAF_2MB','RoundUp','mapped_bytes > address_limit','MappedBytes = mapped_bytes','PhysicalAddressBits = input_snapshot.PhysicalAddressBits','VMX_EPT_PERMISSION_READ','VMX_EPT_PERMISSION_WRITE','VMX_EPT_PERMISSION_EXECUTE','VMX_EPT_MEMORY_TYPE_UC','EnableAccessedDirty = 0','IgnorePat = 0','VmxEptBuildIdentityPlan','*config = staged'))-and
  (($build|Select-String -Pattern '\*config = staged;' -AllMatches).Matches.Count-eq 1)-and
  (O $validate @('input == NULL','config == NULL','Separate(input, sizeof(*input), config, sizeof(*config))','input_snapshot = *input','config_snapshot = *config','VmxEptSessionConfigBuild(&input_snapshot, &expected)','ConfigEqual(&config_snapshot, &expected)'))
}
A(V $s)'session config policy ordering'
foreach($v in @('WDK-free authenticated input','inclusive','Reserved must be zero','snapshot every admitted','frozen values','conservative initial policy','leaves UC','RWX permissions','A/D disabled','RAM','holes','MMIO')){A($h.Contains($v))"header $v"}
foreach($v in @('sizeof(VMX_EPT_SESSION_CONFIG_INPUT) == 24','sizeof(VMX_EPT_SESSION_CONFIG) == 128','Identity) == 16','Populate) == 40','Plan) == 56')){A($h.Contains($v))"ABI $v"}
A(($c|Select-String -Pattern 'vmx_ept_session_config\.c' -AllMatches).Matches.Count-eq 1)'source once'
foreach($v in @('extent rounded to 2MB','conservative UC leaves','2MB retained for hook splits','2MB policy remains hookable','36-bit exact boundary','four-level exact boundary','failure preserves full config','input/output alias','required capability missing','inclusive address addition overflow','validator fixture')){A($t.Contains($v))"test $v"}
A($s-notmatch '(?i)ntddk|wdm\.h|wdf|__vmx|VmxEptInveptAsm|VMCS|DeviceIoControl|IOCTL|hook|Runtime|ExAllocate|MmGetPhysicalMemoryRanges|__readmsr')'forbidden production dependency'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath,$NativePath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptSessionConfig|VMX_EPT_SESSION_CONFIG')"unwired $p"}
$m=@(
 $s.Replace('input_snapshot = *input','memset(&input_snapshot, 0, sizeof(input_snapshot))'),
 $s.Replace('config_snapshot = *config','memset(&config_snapshot, 0, sizeof(config_snapshot))'),
 $s.Replace('VmxEptSessionConfigBuild(&input_snapshot, &expected)','VmxEptSessionConfigBuild(input, &expected)'),
 $s.Replace('ConfigEqual(&config_snapshot, &expected)','ConfigEqual(config, &expected)'),
 $s.Replace('value > UINT64_MAX - mask','0'),
 $s.Replace('CapabilitiesEqual(&first->Capabilities, &second->Capabilities)','1'),
 $s.Replace('!Separate(input, sizeof(*input), config, sizeof(*config))','0'),
 $s.Replace('input_snapshot.Reserved != 0','0'),
 $s.Replace('input_snapshot.PhysicalAddressBits < 36','0'),
 $s.Replace('VmxEptDecodeCapabilities(','BypassDecodeCapabilities('),
 $s.Replace('VmxEptValidateCapabilities(&staged.Capabilities)','VMX_EPT_STATUS_OK'),
 $s.Replace('input_snapshot.HighestPresentPhysicalAddress == UINT64_MAX','0'),
 $s.Replace('input_snapshot.HighestPresentPhysicalAddress >= address_limit','0'),
 $s.Replace('staged.Identity.LeafKind = VMX_EPT_ENTRY_LEAF_2MB;','staged.Identity.LeafKind = VMX_EPT_ENTRY_LEAF_1GB;'),
 $s.Replace('!RoundUp(present_bytes, leaf_bytes, &mapped_bytes)','0'),
 $s.Replace('mapped_bytes > address_limit','0'),
 $s.Replace('VMX_EPT_PERMISSION_WRITE |',''),
 $s.Replace('staged.Populate.MemoryType = VMX_EPT_MEMORY_TYPE_UC;','staged.Populate.MemoryType = VMX_EPT_MEMORY_TYPE_WB;'),
 $s.Replace('VmxEptBuildIdentityPlan(&staged.Capabilities,','BypassIdentityPlan(&staged.Capabilities,'),
 $s.Replace('*config = staged;',''),
 $s.Replace('ConfigEqual(&config_snapshot, &expected)','1')
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i]-ne$s)"mutation $i missing";A(-not(V $m[$i]))"accepted mutation $i"}
Write-Host 'PASS: EPT session configuration policy contracts'
