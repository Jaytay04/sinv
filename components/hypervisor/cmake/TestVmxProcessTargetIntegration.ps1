param(
 [Parameter(Mandatory=$true)][string]$InterfacePath,
 [Parameter(Mandatory=$true)][string]$RequestFramePath,
 [Parameter(Mandatory=$true)][string]$ResponseFramePath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$StartPath,
 [Parameter(Mandatory=$true)][string]$TeardownPath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$BoundaryPath,
 [Parameter(Mandatory=$true)][string]$RouteOwnerPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function Has([string]$t,[string[]]$v,[string]$n){foreach($x in $v){A($t.Contains($x))"$n missing $x"}}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A($q-gt$p)"$n order $x";$p=$q}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A($i-ge0-and$j-gt$i)"section $a";$t.Substring($i,$j-$i)}
function V([string[]]$t){
 $i=$t[0];$q=$t[1];$r=$t[2];$d=$t[3];$s=$t[4];$x=$t[5]
 $m=$t[6];$b=$t[7];$o=$t[8];$l=$t[9];$dc=$t[10];$tc=$t[11]
 Has $i @('vmx_process_target_abi.h','0x818, METHOD_BUFFERED, FILE_ANY_ACCESS','0x819, METHOD_BUFFERED, FILE_ANY_ACCESS') 'interface'
 Has $q @('IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE','sizeof(HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST)','IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY','sizeof(HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST)') 'request frame'
 Has $r @('IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE','IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY','HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE') 'response frame'
 O $d @('VmxProcessTargetConfigureRequestValidate(','VmxPersistentDriverOwnerQueryPhase(','Phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED','VmxProcessTargetOwnerConfigure(','VmxProcessTargetOwnerQuery(','IoctlCompleteBufferedResponse') 'configure dispatch'
 Has $d @('VmxProcessTargetStatusQueryRequestValidate(','IoctlDispatchProcessTargetStatusQuery(','IoctlMapProcessTargetStatus(','HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR') 'query dispatch'
 O $s @('HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED','VmxProcessTargetOwnerActivate(','VmxPersistentDriverOwnerBindCr3RouteGuarded(','HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE','VmxPersistentDriverOwnerStartDiagnosed(') 'start activation'
 $finish=B $x 'FinishDetached(VMX_PERSISTENT_DEVICE_CONTEXT* context)' 'VmxEptPersistentDeviceTeardown(VMX_PERSISTENT_DEVICE_CONTEXT* context)'
 O $finish @('phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED','HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE','VmxProcessTargetOwnerDeactivate(','HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED','VmxProcessTargetOwnerRelease(') 'detached release'
 Has $x @('phase == VMX_PERSISTENT_DRIVER_OWNER_DETACHED','return FinishDetached(context);') 'detached fast path'
 O $m @('PsSetCreateProcessNotifyRoutineEx(DriverProcessNotify, FALSE)','g_process_notify_registered = TRUE','PsSetCreateProcessNotifyRoutineEx(','DriverProcessNotify, TRUE)','VmxEptPersistentDeviceTeardown(device_context)') 'notify lifetime'
 Has $b @('CallerIsTarget = PsGetCurrentProcess() == TargetProcess','SystemIsTarget = TargetProcess == PsInitialSystemProcess','if (CallerIsTarget == SystemIsTarget)','Work.ExpectedProcess = PsInitialSystemProcess','CallerIsTarget ? 1u : 0u','SystemIsTarget ? 1u : 0u') 'safe boundary modes'
 A(-not$b.Contains('VmxPersistentProcessVisitAttached')) 'boundary attaches to a third-party process'
 O $o @('VmxCr3ResidencyApply(&Previous, &Decision, &Expected)','VmxCr3ResidencyApply(&Previous, &Decision, &DisabledExpected)','if (!TargetEnabled(Owner))','Expected = DisabledExpected','PublishState(State, &Expected.Next)') 'expiry commit guard'
 O $l @('ConfigureCurrentProcessTarget()','Sending IOCTL_LOAD','QueryProcessTargetStatusOnOpenDevice(&target)','RunCr3ResidencyProbeOnOpenDevice(TRUE)','READY: Process target is ACTIVE') 'loader target transaction'
 Has $l @('load-target-self','target-status','Exact redacted self-target LEAVE/RETURN roundtrip','Sleep(INFINITE)') 'loader commands'
 Has $dc @('target_link_options(hypervisor_driver PRIVATE /INTEGRITYCHECK)') 'driver integrity'
 A(([regex]::Matches($tc,'test_vmx_process_target_integration_contracts')).Count-eq1) 'integration contract registration'
 A($d-notmatch'(?i)(DbgPrint|KdPrint)[^\r\n]*(?:start_key|generation|ProcessId)') 'driver logs process identity'
}
$p=@($InterfacePath,$RequestFramePath,$ResponseFramePath,$IoctlPath,$StartPath,
 $TeardownPath,$DriverMainPath,$BoundaryPath,$RouteOwnerPath,$LoaderPath,
 $DriverCMakePath,$TestsCMakePath)
$t=@($p|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))});V $t
$u=@(
 @{I=1;S=$t[1].Replace('sizeof(HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST)','0U')},
 @{I=3;S=$t[3].Replace('Phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED','false')},
 @{I=4;S=$t[4].Replace('VmxPersistentDriverOwnerBindCr3RouteGuarded(','VmxPersistentDriverOwnerBindCr3Route(')},
 @{I=5;S=$t[5].Replace('return FinishDetached(context);','return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK;')},
 @{I=6;S=$t[6].Replace('DriverProcessNotify, TRUE','DriverProcessNotify, FALSE')},
 @{I=7;S=$t[7].Replace('if (CallerIsTarget == SystemIsTarget)','if (FALSE)')},
 @{I=8;S=$t[8].Replace('if (!TargetEnabled(Owner))','if (0)')},
 @{I=9;S=$t[9].Replace('RunCr3ResidencyProbeOnOpenDevice(TRUE)','RunCr3ResidencyProbeOnOpenDevice(FALSE)')},
 @{I=10;S=$t[10].Replace('/INTEGRITYCHECK','')})
foreach($z in $u){A($z.S-ne$t[$z.I])'vacuous mutation';$c=$t.Clone();$c[$z.I]=$z.S;$bad=$false;try{V $c}catch{$bad=$true};A($bad)"accepted mutation $($z.I)"}
Write-Host 'PASS: Runtime V17 integrates exact process-target framing, detached-only configuration, guarded activation/expiry, safe self-boundary proof, process-notify lifetime, and post-detach release.'
