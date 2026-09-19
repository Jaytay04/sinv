param(
    [Parameter(Mandatory=$true)][string]$ModelPath,
    [Parameter(Mandatory=$true)][string]$ModelHeader,
    [Parameter(Mandatory=$true)][string]$AbiPath,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$ValidatorPath,
    [Parameter(Mandatory=$true)][string]$LaunchAbiPath,
    [Parameter(Mandatory=$true)][string]$DispatchPath,
    [Parameter(Mandatory=$true)][string]$RuntimePath,
    [Parameter(Mandatory=$true)][string]$OwnerPath,
    [Parameter(Mandatory=$true)][string]$IoctlPath,
    [Parameter(Mandatory=$true)][string]$LoaderPath,
    [Parameter(Mandatory=$true)][string]$GateScript,
    [Parameter(Mandatory=$true)][string]$TestPath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake
)
$ErrorActionPreference = 'Stop'
function A([bool]$Condition,[string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Parse([string]$Text,[string]$Name) {
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Text,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) "$Name does not parse"
}
function Has([string]$Text,[string[]]$Tokens,[string]$Name) {
    foreach($Token in $Tokens) {
        A ($Text.Contains($Token)) "$Name missing: $Token"
    }
}
function Verify([string[]]$Text) {
    $Model=$Text[0];$Header=$Text[1];$Abi=$Text[2];$Interface=$Text[3]
    $Validator=$Text[4];$LaunchAbi=$Text[5];$Dispatch=$Text[6]
    $Runtime=$Text[7];$Owner=$Text[8];$Ioctl=$Text[9]
    $Loader=$Text[10];$Gate=$Text[11];$Test=$Text[12];$Roadmap=$Text[13]
    $Doc=$Text[14];$CMake=$Text[15]
    Parse $Gate 'Gate 43'
    Has $Header @('VMX_DISPATCH_STATUS_VIEW','RawExitReason',
      'VmxDispatchStatusEncode') 'dispatch model header'
    Has $Model @('HYPERVISOR_DISPATCH_STATUS_REASON_MASK',
      'view->RawExitReason &','view->TelemetrySequence == 0',
      'view->DispatchSequence == 0',
      'view->InstructionLength > 15','ErrorPairValid',
      'view->FailClosedCode == 0','memset(&staged, 0') 'dispatch model'
    Has $Abi @('HYPERVISOR_DISPATCH_STATUS_VERSION',
      'HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE UINT32_C(112)',
      'HYPERVISOR_DISPATCH_STATUS_ACTIVE','BasicExitReason',
      'TelemetrySequence','DispatchSequence','FailedVmcsField',
      'sizeof(HYPERVISOR_DISPATCH_STATUS_RESPONSE) == 112') 'dispatch ABI'
    A ($Abi -notmatch '\b(GuestRip|Qualification|GuestCr3|DiagnosticTarget|RawExitReason)\b') `
      'public dispatch ABI exposes a forbidden raw field'
    Has $Interface @('IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY',
      '0x815','HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)',
      'HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST',
      'IoctlValidateDispatchStatusQueryRequest') 'driver interface'
    Has $Validator @('IoctlValidateDispatchStatusQueryRequest',
      'HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_LENGTH',
      'HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_VERSION',
      'HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_SIZE',
      'HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_FLAGS') 'validator'
    Has $LaunchAbi @('uint32_t LastExitInstructionLength, DispatchSequence',
      'VMX_V3D_DISPATCH_SNAPSHOT','DispatchSnapshot',
      'VMX_V3D_CONTEXT_DISPATCH_SEQUENCE_OFFSET 676u',
      'VMX_V3D_CONTEXT_DISPATCH_SNAPSHOT_OFFSET 848u') 'launch ABI'
    Has $Dispatch @('volatile uint32_t* sequence',
      '&Context->DispatchSequence','++(*sequence)',
      'action = DispatchBody','CaptureDispatchSnapshot(Context)',
      'snapshot->AssemblyAction = c->AssemblyAction',
      'snapshot->ProtocolStep = c->ProtocolStep','return action') 'dispatch publication'
    Has $Runtime @('VmxPersistentRuntimeQueryDispatchStatus',
      'RuntimeAcquire(r)','RootLeaseExternalSeparate(r,out,sizeof(*out))',
      'VmxExitTelemetrySnapshot',
      'view.RawExitReason=launch->DispatchSnapshot.RawExitReason',
      'view.AssemblyAction=launch->DispatchSnapshot.AssemblyAction',
      'view.ProtocolStep=launch->DispatchSnapshot.ProtocolStep',
      'first=*sequence','view.DispatchSequence=last',
      'VmxDispatchStatusEncode(&view,&staged)') 'runtime query'
    A ($Runtime -notmatch 'staged\.(GuestRip|Qualification|GuestCr3|DiagnosticTarget)') `
      'runtime query publishes a forbidden raw field'
    Has $Owner @('VmxPersistentDriverOwnerQueryDispatchStatus',
      'OwnerEnter(o)','VMX_PERSISTENT_DRIVER_OWNER_ATTACHED',
      'VmxPersistentRuntimeQueryDispatchStatus') 'driver owner'
    Has $Ioctl @('IOCTL Dispatch: DISPATCH_STATUS_QUERY (0x815)',
      'IoctlValidateDispatchStatusQueryRequest',
      'HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE',
      'VmxPersistentDriverOwnerQueryDispatchStatus',
      'IoctlMapPersistentStatusQueryStatus',
      'IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY') 'driver dispatch'
    Has $Loader @('hypervisor_loader.exe dispatch-status',
      'IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY',
      'Dispatch status query %u timed out',
      'ValidateDispatchStatus','DISPATCH CPU: index=%u',
      'SUCCESS: Exact redacted VM-exit dispatch status verified',
      'L"dispatch-status"') 'loader command'
    Has $Gate @('gate43-dispatch-status-baseline',
      "Invoke-Loader 'dispatch-status' 'dispatch-status'",
      'generation=1 telemetry_sequence=2 dispatch_sequence=2',
      'action=1 protocol=2 protocol_code=0x00000FFF',
      'vmread_outcome=1','vmwrite_outcome=1',
      'Probe=DISPATCH_STATUS_BASELINE','RedactedFields=RIP,QUALIFICATION,CR3,POINTERS',
      "Invoke-Loader 'unload' 'unload",'$AfterUnload.ExitCode -ne 1060',
      '$FailureEvents.Count -ne 0','tar.exe -a -c -f $ZipPath') 'Gate 43'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 43 mutates signing, boot, SCM, or reboot state'
    Has $Test @('TestEncodingAndRedaction','TestEncodingGuards',
      'TestRequestAbi','UINT32_C(0x80002054)',
      'RawExitReason = UINT64_C(18) | (UINT64_C(1) << 32)',
      'Response.BasicExitReason == 18') 'dispatch unit test'
    Has $Roadmap @('Runtime V6 Roadmap - Authenticated Dispatch Diagnostics',
      '| V6A Pure redaction model | Complete',
      '| V6B Runtime query path | Complete','| V6D Live baseline | Complete - GO',
      'Gate 43 - exact initial dispatch baseline','Gate 43 passed on 2026-08-29',
      'Gate 44 - dynamic dispatch/telemetry coherence','One run is sufficient') `
      'Runtime V6 roadmap'
    Has $Doc @('Gate 43: exact redacted dispatch-status baseline',
      'gate43-dispatch-status-baseline.ps1','Do not retry LOAD') `
      'Gate 43 documentation'
    Has $CMake @('test_vmx_dispatch_status',
      'test_vmx_dispatch_status_boundary_contracts',
      'TestVmxDispatchStatusBoundary.ps1') 'dispatch CMake'
}

$Text=@(
 [IO.File]::ReadAllText((Resolve-Path $ModelPath)),
 [IO.File]::ReadAllText((Resolve-Path $ModelHeader)),
 [IO.File]::ReadAllText((Resolve-Path $AbiPath)),
 [IO.File]::ReadAllText((Resolve-Path $InterfacePath)),
 [IO.File]::ReadAllText((Resolve-Path $ValidatorPath)),
 [IO.File]::ReadAllText((Resolve-Path $LaunchAbiPath)),
 [IO.File]::ReadAllText((Resolve-Path $DispatchPath)),
 [IO.File]::ReadAllText((Resolve-Path $RuntimePath)),
 [IO.File]::ReadAllText((Resolve-Path $OwnerPath)),
 [IO.File]::ReadAllText((Resolve-Path $IoctlPath)),
 [IO.File]::ReadAllText((Resolve-Path $LoaderPath)),
 [IO.File]::ReadAllText((Resolve-Path $GateScript)),
 [IO.File]::ReadAllText((Resolve-Path $TestPath)),
 [IO.File]::ReadAllText((Resolve-Path $RoadmapPath)),
 [IO.File]::ReadAllText((Resolve-Path $DocPath)),
 [IO.File]::ReadAllText((Resolve-Path $TestsCMake)))
Verify $Text

$Mutations=@(
 @{I=0;T=$Text[0].Replace('view->RawExitReason &','view->RawExitReason |')},
 @{I=0;T=$Text[0].Replace('view->TelemetrySequence == 0','0 == 1')},
 @{I=0;T=$Text[0].Replace('view->DispatchSequence == 0','0 == 1')},
 @{I=2;T=$Text[2].Replace('BasicExitReason;','GuestRip;')},
 @{I=4;T=$Text[4].Replace('HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_FLAGS','HYPERVISOR_DISPATCH_STATUS_QUERY_VALID')},
 @{I=6;T=$Text[6].Replace('++(*sequence)','--(*sequence)')},
 @{I=7;T=$Text[7].Replace('view.RawExitReason=launch->DispatchSnapshot.RawExitReason','view.RawExitReason=launch->LastExitGuestRip')},
 @{I=9;T=$Text[9].Replace('VmxPersistentDriverOwnerQueryDispatchStatus','VmxPersistentDriverOwnerQueryExitTelemetry')},
 @{I=10;T=$Text[10].Replace('Dispatch status query %u timed out','Dispatch status query has no timeout')},
 @{I=11;T=$Text[11].Replace('dispatch_sequence=2','dispatch_sequence=4')})
foreach($Mutation in $Mutations) {
    A ($Mutation.T -ne $Text[$Mutation.I]) 'dispatch mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try { Verify $Copy } catch { $Rejected=$true }
    A $Rejected "dispatch boundary accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: Runtime V6 exposes an owner-serialized, generation-authenticated, fixed-width and redacted per-processor dispatch status, with a fail-closed Gate 43 baseline.'
