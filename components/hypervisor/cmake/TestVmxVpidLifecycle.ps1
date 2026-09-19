param(
 [Parameter(Mandatory=$true)][string]$AbiPath,
 [Parameter(Mandatory=$true)][string]$InterfacePath,
 [Parameter(Mandatory=$true)][string]$ValidatorPath,
 [Parameter(Mandatory=$true)][string]$OwnerSource,
 [Parameter(Mandatory=$true)][string]$VmcsSource,
 [Parameter(Mandatory=$true)][string]$VmcsAdapterSource,
 [Parameter(Mandatory=$true)][string]$StartSource,
 [Parameter(Mandatory=$true)][string]$TeardownSource,
 [Parameter(Mandatory=$true)][string]$IoctlSource,
 [Parameter(Mandatory=$true)][string]$DriverMain,
 [Parameter(Mandatory=$true)][string]$LoaderSource,
 [Parameter(Mandatory=$true)][string]$GateScript,
 [Parameter(Mandatory=$true)][string]$RoadmapPath)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function O([string]$Text,[string[]]$Tokens,[string]$Name){$p=-1;foreach($t in $Tokens){$n=$Text.IndexOf($t,$p+1);A ($n-gt$p) "$Name missing/order: $t";$p=$n}}
function P([string]$Text,[string]$Name){$tokens=$null;$errors=$null;[void][Management.Automation.Language.Parser]::ParseInput($Text,[ref]$tokens,[ref]$errors);A ($errors.Count-eq0) "$Name does not parse"}
function V($T){
 $abi=$T[0];$int=$T[1];$val=$T[2];$own=$T[3];$vmcs=$T[4];$adapter=$T[5];$start=$T[6];$stop=$T[7];$io=$T[8];$main=$T[9];$loader=$T[10];$gate=$T[11];$road=$T[12]
 foreach($x in @('HYPERVISOR_VPID_STATUS_DISABLED = 1','HYPERVISOR_VPID_STATUS_READY = 2','HYPERVISOR_VPID_STATUS_ACTIVE = 3','HYPERVISOR_VPID_STATUS_TEARDOWN_PENDING = 4','HYPERVISOR_VPID_STATUS_REQUIRED_FLAGS','HYPERVISOR_VPID_CAP_RUNTIME_REQUIRED','HYPERVISOR_VPID_STATUS_RESPONSE_SIZE UINT32_C(64)','sizeof(HYPERVISOR_VPID_STATUS_RESPONSE) == 64')){A ($abi.Contains($x)) "VPID ABI missing: $x"}
 A ($int.Contains('0x813, METHOD_BUFFERED, FILE_ANY_ACCESS')) 'VPID IOCTL 0x813 missing'
 foreach($x in @('HYPERVISOR_VPID_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)','uint32_t SystemIndex;','IoctlValidateVpidStatusQueryRequest(')){A ($int.Contains($x)) "VPID request missing: $x"}
 O $val @('IoctlValidateVpidStatusQueryRequest(','request == NULL','input_length != sizeof(*request)','request->Version != HYPERVISOR_VPID_STATUS_QUERY_VERSION','request->Size != HYPERVISOR_VPID_STATUS_QUERY_REQUEST_SIZE','request->Flags != HYPERVISOR_VPID_STATUS_QUERY_FLAGS_NONE','return HYPERVISOR_VPID_STATUS_QUERY_VALID;') 'validator'
 O $own @('RunPinned(Owner, Index, OP_CAPABILITY)','VMX_VPID_PERSISTENT_UNSUPPORTED','ReleaseUnactivatedOwner(Owner)','RunPinned(Owner, Index, OP_ACTIVATE)') 'capability-before-activation'
 O $own @('NativeStatus = VmxVpidNativeRootTransition(','VmxVpidOwnerAcquire(','VmxVpidExecuteInvalidation(','VmxVpidVmcsActivate(','Owner->States[Index].Active = VmcsReport.Attempted != 0 ? 1u : 0u','VmcsReport.FailedField == VMCS_CTRL_VPID','Owner->States[Index].Active = 1') 'activation'
 O $own @('VmxVpidVmcsDeactivate(','VmxVpidOwnerRetire(','VmxVpidExecuteInvalidation(','VmxVpidOwnerAcknowledgeInvalidation(','VmxVpidNativeRootTransition(') 'retirement'
 foreach($x in @('GraphBindingsValid','RtlSecureZeroMemory(Staged->Graph','HYPERVISOR_VPID_STATUS_FLAG_VMCS_AUTHENTICATED','VMCS_CTRL_SECONDARY','VMCS_CTRL_VPID','ObservedVpid != Record->Vpid','VmxVpidValidateSecondaryControls(RawSecondary)','hypersinv: VPID_UNSUPPORTED','hypersinv: VPID_FAILURE phase=%s','vmcs_reads=%lu','vmcs_writes=%lu','activate-invvpid','activate-vmcs','retire-invvpid','deactivate-vmcs')){A ($own.Contains($x)) "owner contract missing: $x"}
 O $vmcs @('Read(&PlatformSnapshot, VMCS_CTRL_SECONDARY','Read(&PlatformSnapshot, VMCS_CTRL_VPID','*Report = Staged','Write(&PlatformSnapshot, VMCS_CTRL_VPID, Vpid','Write(&PlatformSnapshot, VMCS_CTRL_SECONDARY','Read(&PlatformSnapshot, VMCS_CTRL_VPID','Read(&PlatformSnapshot, VMCS_CTRL_SECONDARY') 'transactional activation preflight/writes/verification'
 foreach($x in @('VMX_PROCESSOR_IDENTITY Identity;','Identity = SnapshotValue.NativeContext->Identity;','VmxVpidNativePlatformValidate(SnapshotValue.NativeContext,','&Identity,')){A ($adapter.Contains($x)) "VMCS native adapter contract missing: $x"}
 A (-not$adapter.Contains('&SnapshotValue.NativeContext->Identity')) 'VMCS native adapter reintroduced overlapping embedded identity validation'
 O $start @('VmxEptPersistentDeviceLifecycleInitialize(context)','VmxVpidPersistentOwnerInitialize(','VMX_VPID_PERSISTENT_UNSUPPORTED','VmxVpidPersistentOwnerValidate(') 'device start'
 O $stop @('VmxEptPersistentDeviceHookDrain(context)','VmxEptHookRoundtripRelease(context)','VmxVpidPersistentOwnerDestroy(&context->VpidLifecycle)','VmxPersistentDriverOwnerStop(&context->Owner)','VmxEptPersistentDeviceLifecycleDestroy(context)') 'device teardown'
 O $io @('if (IoControlCode == IOCTL_HYPERVISOR_VPID_STATUS_QUERY)','IoctlDispatchVpidStatusQuery(','if (IoControlCode == IOCTL_HYPERVISOR_CET_VMCS_PROBE)') 'isolated dispatch'
 foreach($x in @('WdfIoQueueDispatchSequential','WdfExecutionLevelPassive')){A ($main.Contains($x)) "queue contract missing: $x"}
 foreach($x in @('hypervisor_loader.exe vpid-status','IOCTL_HYPERVISOR_VPID_STATUS_QUERY','ValidateActiveVpidStatus','ValidateDisabledVpidStatus','VPID activation is unsupported by the current VMX control surface','response->Vpid == (uint16_t)(expected_index + 1u)','SUCCESS: Exact ACTIVE VPID state verified')){A ($loader.Contains($x)) "loader contract missing: $x"}
 P $gate 'Gate 36'
 foreach($x in @('gate36-vpid-activation','Invoke-Loader ''load'' ''load''','Invoke-Loader ''vpid-status'' ''vpid-status''','Invoke-Loader ''unload'' ''unsupported-unload''','ENVIRONMENT_UNSUPPORTED','Invoke-Loader ''unload'' ''unload''','Vpids=1,2,3,4','ServiceAfterUnload=ABSENT','tar.exe -a -c -f $ZipPath')){A ($gate.Contains($x)) "Gate 36 missing: $x"}
 A ($gate-notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 36 mutates boot, signing, SCM, or power state'
 foreach($x in @('V4C - native invalidation adapter','V4D - VMCS and lifecycle composition','V4E - static authorization','Gate 36')){A ($road.Contains($x)) "roadmap missing: $x"}
}
$paths=@($AbiPath,$InterfacePath,$ValidatorPath,$OwnerSource,$VmcsSource,$VmcsAdapterSource,$StartSource,$TeardownSource,$IoctlSource,$DriverMain,$LoaderSource,$GateScript,$RoadmapPath)
$text=@($paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path -LiteralPath $_))})
V $text
$mutations=@(
 @{I=3;T=$text[3].Replace('RunPinned(Owner, Index, OP_CAPABILITY)','RunPinned(Owner, Index, OP_ACTIVATE)')},
 @{I=3;T=$text[3].Replace('VmxVpidOwnerAcknowledgeInvalidation(','VmxVpidOwnerAcquire(')},
 @{I=5;T=$text[5].Replace('Identity = SnapshotValue.NativeContext->Identity;','Identity = *Identity;')},
 @{I=6;T=$text[6].Replace('VMX_VPID_PERSISTENT_UNSUPPORTED','VMX_VPID_PERSISTENT_OK')},
 @{I=7;T=$text[7].Replace('VmxVpidPersistentOwnerDestroy(&context->VpidLifecycle)','VmxPersistentDriverOwnerStop(&context->Owner)')},
 @{I=10;T=$text[10].Replace('response->Vpid == (uint16_t)(expected_index + 1u)','response->Vpid != 0u')},
 @{I=11;T=$text[11].Replace('Invoke-Loader ''vpid-status'' ''vpid-status''','Write-Host vpid-status-skipped')})
foreach($m in $mutations){A ($m.T-ne$text[$m.I]) 'vacuous VPID mutation';$c=$text.Clone();$c[$m.I]=$m.T;$bad=$false;try{V $c}catch{$bad=$true};A $bad "VPID contract accepted mutation $($m.I)"}
Write-Host 'PASS: VPID capability fallback, group-safe ownership, INVVPID ordering, transactional VMCS state, truthful status, teardown, and Gate 36 are statically authorized.'
