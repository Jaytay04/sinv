param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$OperationsSourcePath,
 [Parameter(Mandatory=$true)][string]$OperationsHeaderPath,
 [Parameter(Mandatory=$true)][string]$CoreCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$TestPath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath;$h=Get-Content -Raw -LiteralPath $HeaderPath
$o=Get-Content -Raw -LiteralPath $OperationsSourcePath;$oh=Get-Content -Raw -LiteralPath $OperationsHeaderPath
$c=Get-Content -Raw -LiteralPath $CoreCMakePath;$tc=Get-Content -Raw -LiteralPath $TestsCMakePath;$t=Get-Content -Raw -LiteralPath $TestPath
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function V([string]$source,[string]$operations){
 return (O $source @(
  'OwnerSeparate(owner, platform','OwnerSeparate(owner, report',
  'identity_snapshot = *identity','staged_platform = *platform',
  'staged_platform.Write == NULL',
  'staged_platform.Read == NULL','staged_platform.SupportsActivation == NULL',
  'staged_platform.ReadInstructionError == NULL','CurrentVmcsValid != 1',
  'supported = staged_platform.SupportsActivation','supported > 1','supported == 0',
  'VmxEptSessionPrepareInstallation(','owner, &identity_snapshot, &ept_pointer',
  'staged_report.CapabilityChecked = 1',
  'VMCS_CTRL_CPU_BASED','VMX_EPT_INSTALLATION_STAGE_READ_PRIMARY',
  'VMCS_CTRL_SECONDARY','VMX_EPT_INSTALLATION_STAGE_READ_SECONDARY',
  'OriginalSecondaryControls & SECONDARY_ENABLE_EPT',
  'ActivatedPrimaryControls =','CPU_BASED_ACTIVATE_SECONDARY_CTLS',
  'ActivatedSecondaryControls =','SECONDARY_ENABLE_EPT',
  'VMCS_CTRL_EPTP, ept_pointer','VMX_EPT_INSTALLATION_STAGE_WRITE_EPTP',
  'VMCS_CTRL_SECONDARY','VMX_EPT_INSTALLATION_STAGE_WRITE_SECONDARY',
  'VMCS_CTRL_CPU_BASED','VMX_EPT_INSTALLATION_STAGE_WRITE_PRIMARY',
  'VMCS_CTRL_EPTP','VMX_EPT_INSTALLATION_STAGE_VERIFY_EPTP, 1',
  'VMCS_CTRL_SECONDARY','VMX_EPT_INSTALLATION_STAGE_VERIFY_SECONDARY',
  'VMCS_CTRL_CPU_BASED','VMX_EPT_INSTALLATION_STAGE_VERIFY_PRIMARY',
  'ObservedEptPointer != ept_pointer',
  'ObservedSecondaryControls !=','ActivatedSecondaryControls',
  'ObservedPrimaryControls !=','ActivatedPrimaryControls',
  'VMX_EPT_INSTALLATION_STAGE_COMPLETE','*report = staged_report',
  'return VMX_EPT_INSTALLATION_OK')) -and
 (O $operations @('VmxEptSessionPrepareInstallation(','Resolve(owner, identity, &index)',
  'VmxEptCoverageMarkUse(owner->Coverage, index)',
  '*ept_pointer = owner->Storage->Domain.EptPointer'))
}
A(V $s $o)'full EPT activation ordering and coverage'
foreach($v in @('one-shot EPT activation','primary/secondary EPT controls are supported',
 'processor identity and callback platform before the first callback',
 'snapshots the live controls','conservatively','Exact VMREAD back',
 'Capability and session preflight failures preserve report',
 'stop the VMX root before release','Post-entry mutation','allocation-free','externally serialized',
 'no native binding','CPU pinning','root transition','runtime','hook','IOCTL','live-VMX')){
 A($h.Contains($v))"header $v"
}
foreach($v in @('VmxEptReadFieldFn','VmxEptSupportsActivationFn',
 'VMX_EPT_INSTALLATION_STAGE_WRITE_EPTP','VMX_EPT_INSTALLATION_STAGE_WRITE_SECONDARY',
 'VMX_EPT_INSTALLATION_STAGE_WRITE_PRIMARY','VMX_EPT_INSTALLATION_STAGE_VERIFY_PRIMARY',
 'OriginalPrimaryControls','ActivatedSecondaryControls','ObservedEptPointer',
 'CapabilityChecked','VerifyCalls')){A($h.Contains($v))"ABI $v"}
A($oh.Contains('VmxEptSessionPrepareInstallation'))'facade declaration'
A(($c|Select-String 'vmx_ept_installation_coordinator\.c' -AllMatches).Matches.Count-eq 1)'core source once'
A(($tc|Select-String 'test_vmx_ept_installation_coordinator\.c' -AllMatches).Matches.Count-eq 2)'test source and verifier path'
A($s-notmatch '(?i)<ntddk|__vmx|__readmsr|DeviceIoControl|Wdf|KeGetCurrent|VmxEptInveptAsm')'native operations forbidden'
foreach($v in @('capability checked before mark use','use marked before VMCS read',
 'unsupported before mark use','full activation succeeds','two reads and three verifies',
 'EPTP and two controls written','exact instruction count','exact field order',
 'primary activates secondary controls','secondary enables EPT','complete stage',
 'readback mismatch rejected','readback mismatch retains coverage',
 'secondary write failure reported','failed activation retains coverage',
 'capability callback mutation cannot redirect installation',
 'callback mutated caller identity','callback poisoned caller platform',
 'frozen platform completes exact instruction sequence',
 'frozen identity marks admitted processor use',
 'overlapping platform and report rejected',
 'overlap rejection preserves caller storage',
 'overlap rejection reaches no callback')){
 A($t.Contains($v))"test $v"
}
$m=@(
 @($s.Replace('identity_snapshot = *identity','memset(&identity_snapshot, 0, sizeof(identity_snapshot))'),$o),
 @($s.Replace('owner, &identity_snapshot, &ept_pointer','owner, identity, &ept_pointer'),$o),
 @($s.Replace('supported = staged_platform.SupportsActivation','supported = 1; /* removed capability */'),$o),
 @($s.Replace('VmxEptSessionPrepareInstallation(','VmxEptSessionMarkUse('),$o),
 @($s.Replace('VMCS_CTRL_EPTP, ept_pointer','VMCS_CTRL_EPTP, 0'),$o),
 @($s.Replace('CPU_BASED_ACTIVATE_SECONDARY_CTLS','0'),$o),
 @($s.Replace('SECONDARY_ENABLE_EPT','0'),$o),
 @($s.Replace('VMX_EPT_INSTALLATION_STAGE_VERIFY_EPTP, 1','VMX_EPT_INSTALLATION_STAGE_VERIFY_EPTP, 0'),$o),
 @($s.Replace('staged_report.ObservedEptPointer != ept_pointer','0'),$o),
 @($s.Replace('staged_report.Stage = VMX_EPT_INSTALLATION_STAGE_COMPLETE','staged_report.Stage = VMX_EPT_INSTALLATION_STAGE_NONE'),$o),
 @($s,$o.Replace('VmxEptCoverageMarkUse(owner->Coverage, index)','VMX_EPT_COVERAGE_OK')),
 @($s,$o.Replace('*ept_pointer = owner->Storage->Domain.EptPointer','*ept_pointer = 0'))
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i][0]-ne$s-or$m[$i][1]-ne$o)"mutation $i missing";A(-not(V $m[$i][0] $m[$i][1]))"accepted mutation $i"}
Write-Host 'PASS: EPT activation coordinator contracts'
